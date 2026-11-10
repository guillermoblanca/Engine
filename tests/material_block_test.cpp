// T36 (render M4): the per-material storage block. GB::MaterialData (Renderer/MaterialData.h) is uploaded byte for byte into the array
// `uMaterials[]` of `lib/material` (ShaderLib/Pieces.h) that the PBR and shadow programs index with uMaterialId; its static_asserts pin the
// C++ side to the std430 rules. This test asks the driver, with a hidden window, where it put each member of one element and the stride of
// the array, and compares; it checks that the GLSL constants (map bits, flags, layer bits and slots) are the C++ ones; and, on the CPU, that
// the MaterialTable gives equal materials one id and different ones another.
#include "GB/Log.h"
#include "GB/Platform/Window.h"
#include "GB/Renderer/MaterialData.h"
#include "GB/Renderer/MaterialTable.h"
#include "GB/Renderer/RHI/RHI.h"
#include "GB/Renderer/Shaders.h"
#include <glad/gl.h>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
		else std::fprintf(stderr, "ok: %s\n", what.c_str());
	}

	void CheckTable()
	{
		const GB::MaterialContext context{ true, true };
		GB::Material a;
		a.Albedo = { 0.5f, 0.4f, 0.3f };
		a.Moss = 0.6f;
		GB::Material b = a;          // equal content
		GB::Material c = a;
		c.WaterLine = 1.5f;          // a per-object value (the procedural rocks)
		GB::MaterialTable table;
		const GB::MaterialId ia = table.Acquire(GB::PackMaterial(a, context));
		const GB::MaterialId ib = table.Acquire(GB::PackMaterial(b, context));
		const GB::MaterialId ic = table.Acquire(GB::PackMaterial(c, context));
		Check(ia == ib && ia != ic && table.Size() == 2, "equal materials share an id, a different one gets its own (2 entries)");
		Check(table.Matches(ia, GB::PackMaterial(b, context)) && !table.Matches(ia, GB::PackMaterial(c, context)), "Matches compares the content");
		Check(table.Desc(ic).LayerMask() == (GB::MaterialData::LayerMoss | GB::MaterialData::LayerWaterLine), "the layer mask of the rock: moss and water line");
		const GB::MaterialDesc noGround = GB::PackMaterial(a, { false, true });
		Check(noGround == GB::PackMaterial(a, context), "a material without ground marks packs the same with and without a ground");
		GB::Material marked = a;
		marked.TrailDepth = 0.1f;
		Check(GB::PackMaterial(marked, { false, true }).Data.Layer[GB::MaterialData::SlotTrail].x == 0.0f &&
			GB::PackMaterial(marked, context).Data.Layer[GB::MaterialData::SlotTrail].x == 0.1f, "the marks' depth is 0 without a ground (as uTrailDepth was)");
		GB::Material masked = a;
		masked.Alpha = GB::AlphaMode::Mask;
		Check((GB::PackMaterial(masked, { true, false }).Data.Flags.w & GB::MaterialData::FlagAlphaToCoverage) == 0 &&
			(GB::PackMaterial(masked, context).Data.Flags.w & GB::MaterialData::FlagAlphaToCoverage) != 0, "alpha to coverage only with samples");
		// T34: transmission packs only on a Blend material; on any other the bytes are those of the material without it
		GB::Material glass = a;
		glass.Transmission = 0.9f; glass.Ior = 1.31f; glass.Absorption = { 0.6f, 0.15f, 0.05f }; glass.Thickness = 0.4f;
		Check(GB::PackMaterial(glass, context) == GB::PackMaterial(a, context), "transmission on an opaque material packs nothing (same bytes)");
		glass.Alpha = GB::AlphaMode::Blend;
		const GB::MaterialData g = GB::PackMaterial(glass, context).Data;
		Check((g.Flags.x & GB::MaterialData::LayerTransmission) != 0 && g.ModelParams.z == 1.31f && g.ModelParams.w == 0.9f &&
			g.Layer[GB::MaterialData::SlotTransmission] == glm::vec4(0.6f, 0.15f, 0.05f, 0.4f), "a Blend glass packs its bit, IOR, amount, absorption and thickness");
		GB::Material plainBlend = a;
		plainBlend.Alpha = GB::AlphaMode::Blend;
		const GB::MaterialData p = GB::PackMaterial(plainBlend, context).Data;
		Check((p.Flags.x & GB::MaterialData::LayerTransmission) == 0 && p.ModelParams.z == 0.0f && p.ModelParams.w == 0.0f &&
			p.Layer[GB::MaterialData::SlotTransmission] == glm::vec4(0.0f), "a Blend material without transmission leaves the slot at zero");
		Check(table.UploadedEnd() == 0 && table.Dirty(), "the new entries wait for an upload");
		table.MarkUploaded();
		Check(!table.Dirty(), "nothing left to upload");
		const uint32_t generation = table.Generation();
		table.Clear();
		Check(table.Size() == 0 && table.Generation() != generation && table.Acquire(GB::PackMaterial(c, context)) == 0, "Clear starts the ids again");
	}

	// What the CPU side costs, printed, not checked: packing a material and comparing it with its entry (an object whose material did not
	// change, MaterialBuffer's usual path), and a lookup by hash that finds it (a changed or unknown material that the table has).
	void TimeTable()
	{
		const GB::MaterialContext context{ true, true };
		std::vector<GB::Material> materials(64);
		for (size_t i = 0; i < materials.size(); ++i)
		{
			materials[i].Albedo = glm::vec3(static_cast<float>(i) / 64.0f, 0.5f, 0.25f);
			materials[i].Moss = (i % 3 == 0) ? 0.5f : 0.0f;
		}
		GB::MaterialTable table;
		std::vector<GB::MaterialId> ids;
		for (const GB::Material& m : materials) ids.push_back(table.Acquire(GB::PackMaterial(m, context)));
		constexpr int kRounds = 100000;
		using Clock = std::chrono::steady_clock;
		uint32_t sink = 0;
		auto start = Clock::now();
		for (int r = 0; r < kRounds; ++r)
		{
			const size_t i = static_cast<size_t>(r) % materials.size();
			sink += table.Matches(ids[i], GB::PackMaterial(materials[i], context)) ? 1u : 0u;
		}
		const double packNs = std::chrono::duration<double, std::nano>(Clock::now() - start).count() / kRounds;
		start = Clock::now();
		for (int r = 0; r < kRounds; ++r)
		{
			const size_t i = static_cast<size_t>(r) % materials.size();
			sink += table.Acquire(GB::PackMaterial(materials[i], context));
		}
		const double acquireNs = std::chrono::duration<double, std::nano>(Clock::now() - start).count() / kRounds;
		std::fprintf(stderr, "time: PackMaterial + Matches %.1f ns, PackMaterial + Acquire (found) %.1f ns, %u entries (sink %u)\n", packNs,
			acquireNs, table.Size(), sink);
	}

	// The constants of lib/material, as text: "const uint NAME = <value>u;".
	void CheckConstants()
	{
		const std::string_view lib = GB::Shaders::Lib::Material;
		struct Constant { const char* Name; uint32_t Value; };
		using D = GB::MaterialData;
		const Constant constants[] = {
			{ "TEX_ALBEDO", D::TexAlbedo }, { "TEX_NORMAL", D::TexNormal }, { "TEX_HEIGHT_MAP", D::TexHeightMap }, { "TEX_HEIGHT", D::TexHeight },
			{ "TEX_MR", D::TexMR }, { "TEX_OCCLUSION", D::TexOcclusion }, { "TEX_EMISSIVE", D::TexEmissive }, { "TEX_MACRO", D::TexMacro },
			{ "TEX_TERRAIN_NORMALS", D::TexTerrainNormals },
			{ "FLAG_FOLIAGE_NORMALS", D::FlagFoliageNormals }, { "FLAG_ALPHA_TO_COVERAGE", D::FlagAlphaToCoverage }, { "FLAG_DOUBLE_SIDED", D::FlagDoubleSided },
			{ "LAYER_GRAIN", D::LayerGrain }, { "LAYER_MOSS", D::LayerMoss }, { "LAYER_SAND", D::LayerSand }, { "LAYER_WATER_LINE", D::LayerWaterLine },
			{ "LAYER_TRAIL", D::LayerTrail }, { "LAYER_MACRO", D::LayerMacro }, { "LAYER_TRANSMISSION", D::LayerTransmission },
			{ "SLOT_GRAIN", D::SlotGrain }, { "SLOT_MOSS", D::SlotMoss }, { "SLOT_SAND", D::SlotSand }, { "SLOT_WATER_LINE", D::SlotWaterLine },
			{ "SLOT_TRAIL", D::SlotTrail }, { "SLOT_TRAIL_TINT", D::SlotTrailTint }, { "SLOT_MACRO", D::SlotMacro },
			{ "SLOT_TRANSMISSION", D::SlotTransmission },
			{ "LAYER_SLOTS", D::LayerSlots },
		};
		int found = 0;
		for (const Constant& c : constants)
		{
			const std::string line = std::string("const uint ") + c.Name + " = " + std::to_string(c.Value) + "u;";
			const bool ok = lib.find(line) != std::string_view::npos;
			if (!ok) Check(false, "lib/material has \"" + line + "\"");
			found += ok ? 1 : 0;
		}
		Check(found == static_cast<int>(std::size(constants)), std::to_string(found) + " of " + std::to_string(std::size(constants)) +
			" constants of lib/material equal to MaterialData's");
	}
}

int GBTest_material_block(int, char**)
{
	GB::Log::Init();
	CheckTable();
	TimeTable();
	CheckConstants();

	GB::WindowProps props;
	props.Visible = false;
	props.VSync = false;
	auto window = GB::Window::Create(props);
	if (!window)
	{
		std::fprintf(stderr, "SKIP: no OpenGL 4.5 context\n");
		return failures == 0 ? 77 : 1;
	}
	GB::RHI::Device& device = GB::RHI::GetDevice();

	// The block as the programs get it: through the assembler. The fragment shader reads every member so none is optimized away.
	const std::string vs = GB::Shaders::Assemble("#version 450 core\nvoid main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n");
	const std::string fs = GB::Shaders::Assemble("#version 450 core\n#include \"lib/material\"\nout vec4 FragColor;\n"
		"void main() {\n"
		"  vec4 s = MATERIAL.BaseColor + MATERIAL.Emissive + MATERIAL.Surface + MATERIAL.Uv + vec4(MATERIAL.Tex) + vec4(MATERIAL.Flags)\n"
		"         + MATERIAL.Wind + MATERIAL.ModelParams;\n"
		"  for (int i = 0; i < int(LAYER_SLOTS); ++i) s += MATERIAL.Layer[i];\n"
		"  FragColor = s;\n"
		"}\n");
	const GB::RHI::ProgramHandle program = device.CreateProgram(vs, fs);
	Check(static_cast<bool>(program), "a program with lib/material compiles");
	if (!program)
		return 1;
	const GLuint id = program.Id;

	const GLuint block = glGetProgramResourceIndex(id, GL_SHADER_STORAGE_BLOCK, "MaterialBuffer");
	Check(block != GL_INVALID_INDEX, "the program has the storage block MaterialBuffer");
	if (block != GL_INVALID_INDEX)
	{
		const GLenum props2[] = { GL_BUFFER_BINDING };
		GLint binding = -1;
		glGetProgramResourceiv(id, GL_SHADER_STORAGE_BLOCK, block, 1, props2, 1, nullptr, &binding);
		std::fprintf(stderr, "MaterialBuffer at binding %d (MaterialData::Binding %u, %zu B per material)\n", binding, GB::MaterialData::Binding,
			sizeof(GB::MaterialData));
		Check(binding == static_cast<GLint>(GB::MaterialData::Binding), "the block's binding is MaterialData::Binding");
	}

	struct Member { const char* Name; size_t Offset; };
#define GB_MEMBER(glsl, cpp) Member{ "uMaterials[0]." glsl, offsetof(GB::MaterialData, cpp) }
	const Member members[] = {
		GB_MEMBER("BaseColor", BaseColor), GB_MEMBER("Emissive", Emissive), GB_MEMBER("Surface", Surface), GB_MEMBER("Uv", Uv),
		GB_MEMBER("Tex", Tex), GB_MEMBER("Flags", Flags), GB_MEMBER("Wind", Wind), GB_MEMBER("ModelParams", ModelParams),
		GB_MEMBER("Layer[0]", Layer),
	};
#undef GB_MEMBER
	int placed = 0;
	for (const Member& m : members)
	{
		const GLuint index = glGetProgramResourceIndex(id, GL_BUFFER_VARIABLE, m.Name);
		if (index == GL_INVALID_INDEX)
		{
			Check(false, std::string(m.Name) + ": in the block");
			continue;
		}
		const GLenum props3[] = { GL_OFFSET, GL_BLOCK_INDEX, GL_TOP_LEVEL_ARRAY_STRIDE, GL_ARRAY_STRIDE };
		GLint v[4] = {};
		glGetProgramResourceiv(id, GL_BUFFER_VARIABLE, index, 4, props3, 4, nullptr, v);
		const bool layer = std::string(m.Name).ends_with("[0]");
		const bool ok = v[0] == static_cast<GLint>(m.Offset) && v[1] == static_cast<GLint>(block) &&
			v[2] == static_cast<GLint>(sizeof(GB::MaterialData)) && (!layer || v[3] == 16);
		if (!ok)
			Check(false, std::string(m.Name) + ": GL offset " + std::to_string(v[0]) + " (struct stride " + std::to_string(v[2]) + ", array stride " +
				std::to_string(v[3]) + "), C++ " + std::to_string(m.Offset));
		placed += ok ? 1 : 0;
	}
	Check(placed == static_cast<int>(std::size(members)), std::to_string(placed) + " of " + std::to_string(std::size(members)) +
		" members at the offsets of MaterialData, with a stride of " + std::to_string(sizeof(GB::MaterialData)) + " B");

	device.DestroyProgram(program);
	return failures == 0 ? 0 : 1;
}
