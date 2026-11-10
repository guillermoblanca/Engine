// CPU-only tests for the shader assembler (render M2, T68): `#include` resolution, include once, cycles, missing pieces and variant
// defines; and every built-in stage, assembled as the renderers do it, byte for byte equal to tests/golden/shaders/<stage>.glsl (the
// sources compiled before Shaders.h was split into pieces). Update the references: GB_UPDATE_GOLDEN=1 ctest -R shader_assembly
#include "GB/Qa/Qa.h"
#include "GB/Renderer/ShaderAssembler.h"
#include "GB/Renderer/Shaders.h"
#include "GB/World/Water/WaterWaves.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}

	void AssemblerCases()
	{
		using GB::ShaderPiece;
		using GB::AssembleShader;
		const ShaderPiece pieces[] = {
			{ "lib/a", "float A;\n" },
			{ "lib/b", "#include \"lib/a\"\nfloat B;\n" },
			{ "lib/self", "#include \"lib/self\"\n" },
			{ "lib/x", "#include \"lib/y\"\n" },
			{ "lib/y", "#include \"lib/x\"\n" },
			{ "lib/broken", "#include \"lib/nowhere\"\n" },
			{ "lib/noeol", "float N;" },
		};
		const std::span<const std::string_view> none;

		// No includes, no defines: the text comes back untouched.
		const std::string_view plain = "#version 450 core\nvoid main() {}\n";
		Check(AssembleShader(plain, none, pieces).Source == plain, "plain source unchanged");

		// The include line (with its newline) becomes the piece, nested includes too, blanks around the directive allowed.
		GB::ShaderAssembly r = AssembleShader("#version 450 core\n  #include \"lib/b\"  \nvoid main() {}\n", none, pieces);
		Check(r.Ok() && r.Source == "#version 450 core\nfloat A;\nfloat B;\nvoid main() {}\n", "nested include expands in place");

		// Include once: the second include of a piece (direct or through another) expands to nothing.
		r = AssembleShader("#include \"lib/a\"\n#include \"lib/b\"\n#include \"lib/a\"\nx\n", none, pieces);
		Check(r.Ok() && r.Source == "float A;\nfloat B;\nx\n", "each piece goes in once");

		// A last line without newline; a piece without a final newline does not glue to the next line.
		r = AssembleShader("#include \"lib/a\"", none, pieces);
		Check(r.Ok() && r.Source == "float A;\n", "include on the last line without newline");
		r = AssembleShader("#include \"lib/noeol\"\nx\n", none, pieces);
		Check(r.Ok() && r.Source == "float N;\nx\n", "piece without a final newline");

		// Not directives: other text with #include in it, or a malformed one, is left as it is.
		const std::string_view notDirectives = "// #include \"lib/a\"\n#include <lib/a>\n#includex \"lib/a\"\n#include \"lib/a\" x\n";
		r = AssembleShader(notDirectives, none, pieces);
		Check(r.Ok() && r.Source == notDirectives, "lines that are not include directives stay");

		// Missing piece: an error that names it (and who included it); no source.
		r = AssembleShader("#include \"lib/missing\"\n", none, pieces);
		Check(!r.Ok() && r.Source.empty() && r.Error.find("\"lib/missing\"") != std::string::npos, "missing include is an error with its name: " + r.Error);
		r = AssembleShader("#include \"lib/broken\"\n", none, pieces);
		Check(!r.Ok() && r.Error.find("lib/nowhere") != std::string::npos && r.Error.find("lib/broken") != std::string::npos,
			"missing nested include names the includer: " + r.Error);

		// Cycles, direct and through another piece: an error with the chain, not a silent skip by the include-once guard.
		r = AssembleShader("#include \"lib/self\"\n", none, pieces);
		Check(!r.Ok() && r.Error.find("cycle") != std::string::npos && r.Error.find("lib/self -> lib/self") != std::string::npos, "self include is a cycle: " + r.Error);
		r = AssembleShader("#include \"lib/x\"\n", none, pieces);
		Check(!r.Ok() && r.Error.find("lib/x -> lib/y -> lib/x") != std::string::npos, "x -> y -> x is a cycle: " + r.Error);

		// Variant defines: right after #version, in order; at the start without #version; after a #version on a later line.
		const std::string_view defines[] = { "GB_SKINNED", "GB_MODEL 2" };
		r = AssembleShader("#version 450 core\n#include \"lib/a\"\n", defines, pieces);
		Check(r.Ok() && r.Source == "#version 450 core\n#define GB_SKINNED\n#define GB_MODEL 2\nfloat A;\n", "defines after #version");
		r = AssembleShader("float A;\n", defines, pieces);
		Check(r.Ok() && r.Source == "#define GB_SKINNED\n#define GB_MODEL 2\nfloat A;\n", "defines at the start without #version");
		r = AssembleShader("// header\n#version 450 core\nx\n", defines, pieces);
		Check(r.Ok() && r.Source == "// header\n#version 450 core\n#define GB_SKINNED\n#define GB_MODEL 2\nx\n", "defines after a later #version");
		r = AssembleShader("#version 450 core", defines, pieces);
		Check(r.Ok() && r.Source == "#version 450 core\n#define GB_SKINNED\n#define GB_MODEL 2\n", "defines after a #version without newline");
	}

	// Every built-in stage against its reference, byte for byte.
	void BuiltInStages()
	{
		namespace fs = std::filesystem;
		const fs::path dir = fs::path(GB_GOLDEN_DIR) / "shaders";
		const bool update = GB::Qa::Config::FromEnvironment().UpdateGolden;
		if (update) fs::create_directories(dir);

		// Every piece of the library is used by some stage, and every piece resolves on its own.
		for (const GB::ShaderPiece& piece : GB::Shaders::Library())
		{
			bool used = false;
			const std::string directive = "#include \"" + std::string(piece.Name) + "\"";
			for (const GB::Shaders::Stage& stage : GB::Shaders::Stages()) used = used || stage.Source.find(directive) != std::string_view::npos;
			for (const GB::ShaderPiece& other : GB::Shaders::Library()) used = used || other.Source.find(directive) != std::string_view::npos;
			Check(used, std::string(piece.Name) + ": used by some stage");
			Check(GB::AssembleShader(piece.Source, {}, GB::Shaders::Library()).Ok(), std::string(piece.Name) + ": assembles");
		}

		int compared = 0;
		for (const GB::Shaders::Stage& stage : GB::Shaders::Stages())
		{
			const std::string name(stage.Name);
			const GB::ShaderAssembly assembly = GB::AssembleShader(stage.Source, {}, GB::Shaders::Library());
			Check(assembly.Ok(), name + ": assembles (" + assembly.Error + ")");
			std::string source = GB::Shaders::Assemble(stage.Source);
			if (name.starts_with("water_")) source = GB::InjectWaveSet(source);   // as WaterRenderer does
			Check(source.find("#include") == std::string::npos, name + ": no include left");
			Check(source.find('\r') == std::string::npos, name + ": no carriage returns");
			const fs::path file = dir / (name + ".glsl");
			if (update)
			{
				std::ofstream out(file, std::ios::binary);
				out.write(source.data(), static_cast<std::streamsize>(source.size()));
				continue;
			}
			std::ifstream in(file, std::ios::binary);
			Check(static_cast<bool>(in), name + ": reference " + file.string() + " exists");
			std::string reference((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			std::erase(reference, '\r');   // a checkout that turned the newlines into CRLF still compares
			if (reference != source)
			{
				const auto diff = std::mismatch(reference.begin(), reference.end(), source.begin(), source.end());
				const size_t at = static_cast<size_t>(diff.first - reference.begin());
				Check(false, name + ": differs from the reference at byte " + std::to_string(at) + " (reference " + std::to_string(reference.size())
					+ " bytes, assembled " + std::to_string(source.size()) + ")");
			}
			++compared;
		}
		if (!update) std::printf("shader_assembly: %d stages byte for byte equal to tests/golden/shaders\n", compared);
	}
}

int GBTest_shader_assembly(int, char**)
{
	AssemblerCases();
	BuiltInStages();
	if (failures == 0) std::printf("shader_assembly: ok\n");
	return failures == 0 ? 0 : 1;
}
