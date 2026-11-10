// T36 (render M4): the per-material storage block. GB::MaterialData (Renderer/MaterialData.h) is uploaded byte for byte into the array
// `uMaterials[]` of `lib/material` (ShaderLib/Pieces.h) that the PBR and shadow programs index with uMaterialId; its static_asserts pin the
// C++ side to the std430 rules. This test asks the driver, with a hidden window, where it put each member of one element and the stride of
// the array, and compares; it checks that the GLSL constants (map bits, flags, layer bits and slots) are the C++ ones; and, on the CPU, that
// the MaterialTable gives equal materials one id and different ones another.
#include "GB/Log.h"
#include "GB/Platform/Window.h"
#include "GB/Renderer/MaterialData.h"
#include "GB/Renderer/MaterialLayers.h"
#include "GB/Visual/VisualDefs.h"
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
		// T101: the interior, scatter and shadow pack only with the transmission; the interior's slot and bit only with a map
		GB::Material ice = glass;
		ice.Scatter = 0.6f; ice.ShadowDensity = 0.35f; ice.ThicknessVariation = 0.8f; ice.InteriorDepth = 0.04f; ice.InteriorStrength = 0.9f;
		ice.InteriorTint = { 0.9f, 1.0f, 1.1f };
		const GB::MaterialData noMap = GB::PackMaterial(ice, context).Data;
		Check((noMap.Tex.x & GB::MaterialData::TexInterior) == 0 && noMap.Layer[GB::MaterialData::SlotInterior] == glm::vec4(0.0f) &&
			noMap.Layer[GB::MaterialData::SlotTransmissionExtra] == glm::vec4(0.0f, 0.6f, 0.35f, 0.8f), "without an interior map: scatter, shadow and variation only");
		ice.InteriorMap = reinterpret_cast<const GB::Texture2D*>(&ice);   // (any non-null pointer: PackMaterial only tests it)
		const GB::MaterialData withMap = GB::PackMaterial(ice, context).Data;
		Check((withMap.Tex.x & GB::MaterialData::TexInterior) != 0 && withMap.Layer[GB::MaterialData::SlotInterior] == glm::vec4(0.9f, 1.0f, 1.1f, 0.9f) &&
			withMap.Layer[GB::MaterialData::SlotTransmissionExtra].x == 0.04f, "with an interior map: its bit, tint, strength and depth");
		GB::Material opaqueIce = ice;
		opaqueIce.Alpha = GB::AlphaMode::Opaque;
		GB::Material opaquePlain = opaqueIce;
		opaquePlain.InteriorMap = nullptr; opaquePlain.Scatter = 0.0f; opaquePlain.ShadowDensity = 0.0f; opaquePlain.Transmission = 0.0f;
		Check(GB::PackMaterial(opaqueIce, context) == GB::PackMaterial(opaquePlain, context), "the T101 values on an opaque material pack nothing (same bytes)");
		// T46: the foliage layer packs only with one of its values on; the tint alone (its default is not zero) changes nothing
		GB::Material tintOnly = a;
		tintOnly.TransmissionTint = { 0.3f, 0.6f, 0.9f };
		tintOnly.AlphaMipScale = 0.3f;   // (masked only: on an opaque material it is nothing)
		Check(GB::PackMaterial(tintOnly, context) == GB::PackMaterial(a, context), "the foliage tint and the alpha mip scale of an opaque material pack nothing (same bytes)");
		GB::Material leaf = a;
		leaf.Alpha = GB::AlphaMode::Mask;
		leaf.Translucency = 0.7f; leaf.TransmissionTint = { 0.8f, 1.0f, 0.4f }; leaf.ColorVariation = 0.5f; leaf.NormalBlend = 0.4f;
		leaf.MacroVariation = 0.25f; leaf.AlphaMipScale = 0.3f;
		const GB::MaterialData fl = GB::PackMaterial(leaf, context).Data;
		Check((fl.Flags.x & GB::MaterialData::LayerFoliage) != 0 && fl.Layer[GB::MaterialData::SlotFoliage] == glm::vec4(0.8f, 1.0f, 0.4f, 0.7f) &&
			fl.Layer[GB::MaterialData::SlotFoliageExtra] == glm::vec4(0.5f, 0.4f, 0.25f, 0.3f), "a foliage material packs its bit, tint, translucency, variation, blend and mip scale");
		GB::Material blendLeaf = leaf;
		blendLeaf.Alpha = GB::AlphaMode::Blend;   // the translucent pass draws with the base program only
		Check(GB::FoliageOn(leaf) && !GB::FoliageOn(blendLeaf) && (GB::PackMaterial(blendLeaf, context).Data.Flags.x & GB::MaterialData::LayerFoliage) == 0 &&
			GB::PackMaterial(blendLeaf, context).Data.Layer[GB::MaterialData::SlotFoliage] == glm::vec4(0.0f), "a Blend material keeps its foliage values but not the layer");
		GB::Material mipOnly = a;
		mipOnly.Alpha = GB::AlphaMode::Mask;
		mipOnly.AlphaMipScale = 0.25f;
		const GB::MaterialData mo = GB::PackMaterial(mipOnly, context).Data;
		Check((mo.Flags.x & GB::MaterialData::LayerFoliage) != 0 && mo.Layer[GB::MaterialData::SlotFoliage].w == 0.0f &&
			mo.Layer[GB::MaterialData::SlotFoliageExtra].w == 0.25f, "a masked material with only the alpha mip scale turns the layer on");
		// T107: the occlusion turns the layer on only with a height to fade over; the spread rides on the layer
		Check(fl.Layer[GB::MaterialData::SlotFoliageShade] == glm::vec4(0.0f), "a foliage material without the T107 values packs its shade slot zero");
		GB::Material occOnly = a;
		occOnly.BaseOcclusion = 0.6f; occOnly.InteriorOcclusion = 0.3f; occOnly.BacklightSpread = 0.5f;
		Check(!GB::FoliageOn(occOnly) && GB::PackMaterial(occOnly, context) == GB::PackMaterial(a, context), "occlusion without a height packs nothing (same bytes)");
		occOnly.OcclusionHeight = 0.25f;
		const GB::MaterialData oc = GB::PackMaterial(occOnly, context).Data;
		Check(GB::FoliageOn(occOnly) && (oc.Flags.x & GB::MaterialData::LayerFoliage) != 0 &&
			oc.Layer[GB::MaterialData::SlotFoliageShade] == glm::vec4(0.6f, 0.25f, 0.3f, 0.5f), "base and interior occlusion with a height turn the layer on and pack the shade slot");
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

	// T101: the transmission layer's new parameters in the data: "scatter", "shadow", "thicknessVariation" and the "interior" object (its "map"
	// a texture name); read onto a render material by the registry, and written back by VisualsToJson to the same library.
	void CheckVisualsJson()
	{
		const char* json = R"({ "visuals": { "materials": { "ice": { "color": [0.9, 0.95, 1.0], "roughness": 0.3, "surface": "translucent",
			"layers": { "transmission": { "ior": 1.31, "absorption": [3.0, 0.5, 0.25], "thickness": 0.5, "scatter": 0.6, "shadow": 0.3,
				"interior": { "map": "ice_cracks", "depth": 0.03, "strength": 0.8, "tint": [0.9, 1.0, 1.0] } } } } } } })";
		GB::VisualLibrary lib;
		const std::string why = GB::LoadVisuals(json, lib);
		Check(why.empty() && lib.Materials.count("ice") == 1, "visuals: a transmission layer with interior, scatter and shadow loads (" + why + ")");
		if (!why.empty() || !lib.Materials.count("ice")) return;
		const GB::VisualMaterial& v = lib.Materials.at("ice");
		GB::Material m;
		GB::ApplyMaterialLayers(v.Layers, m);
		Check(v.InteriorMap == "ice_cracks" && m.Scatter == 0.6f && m.ShadowDensity == 0.3f && m.InteriorDepth == 0.03f && m.InteriorStrength == 0.8f &&
			m.InteriorTint == glm::vec3(0.9f, 1.0f, 1.0f) && m.ThicknessVariation == 0.5f, "visuals: the interior's map name and the values on the material");
		GB::VisualLibrary back;
		Check(GB::LoadVisuals(GB::VisualsToJson(lib), back).empty() && back.Materials.count("ice") && back.Materials.at("ice") == v,
			"visuals: VisualsToJson reads back to the same material");
		GB::VisualLibrary bad;
		Check(!GB::LoadVisuals(R"({ "visuals": { "materials": { "x": { "surface": "translucent", "layers": { "transmission": { "interior": { "deep": 1 } } } } } } })", bad).empty(),
			"visuals: an unknown interior parameter is a mistake");

		// T46: the foliage layer
		const char* leaves = R"({ "visuals": { "materials": { "bush": { "color": [1.0, 1.0, 1.0], "surface": "cutout", "doubleSided": true,
			"layers": { "foliage": { "translucency": 0.6, "transmissionTint": [0.8, 1.0, 0.4], "colorVariation": 0.5, "normalBlend": 0.4,
				"macroVariation": 0.3, "alphaMipScale": 0.25 } } } } } })";
		GB::VisualLibrary fol;
		const std::string whyFol = GB::LoadVisuals(leaves, fol);
		Check(whyFol.empty() && fol.Materials.count("bush") == 1, "visuals: a foliage layer loads (" + whyFol + ")");
		if (whyFol.empty() && fol.Materials.count("bush"))
		{
			GB::Material f;
			GB::ApplyMaterialLayers(fol.Materials.at("bush").Layers, f);
			Check(f.Translucency == 0.6f && f.TransmissionTint == glm::vec3(0.8f, 1.0f, 0.4f) && f.ColorVariation == 0.5f && f.NormalBlend == 0.4f &&
				f.MacroVariation == 0.3f && f.AlphaMipScale == 0.25f, "visuals: the foliage values on the material");
			GB::VisualLibrary folBack;
			Check(GB::LoadVisuals(GB::VisualsToJson(fol), folBack).empty() && folBack.Materials.count("bush") && folBack.Materials.at("bush") == fol.Materials.at("bush"),
				"visuals: VisualsToJson reads the foliage layer back to the same material");
			GB::Material d;
			GB::ApplyMaterialLayers({ { "foliage", {} } }, d);
			Check(d.Translucency == 0.0f && d.ColorVariation == 0.0f && d.NormalBlend == 0.0f && d.MacroVariation == 0.0f && d.AlphaMipScale == 0.0f &&
				GB::PackMaterial(d, { true, true }) == GB::PackMaterial(GB::Material{}, { true, true }), "visuals: an empty foliage layer is off (every default 0)");
		}
		Check(!GB::LoadVisuals(R"({ "visuals": { "materials": { "x": { "layers": { "foliage": { "translucency": 2 } } } } } })", bad).empty(),
			"visuals: a translucency out of range is a mistake");
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
			{ "TEX_TERRAIN_NORMALS", D::TexTerrainNormals }, { "TEX_INTERIOR", D::TexInterior },
			{ "FLAG_FOLIAGE_NORMALS", D::FlagFoliageNormals }, { "FLAG_ALPHA_TO_COVERAGE", D::FlagAlphaToCoverage }, { "FLAG_DOUBLE_SIDED", D::FlagDoubleSided },
			{ "LAYER_GRAIN", D::LayerGrain }, { "LAYER_MOSS", D::LayerMoss }, { "LAYER_SAND", D::LayerSand }, { "LAYER_WATER_LINE", D::LayerWaterLine },
			{ "LAYER_TRAIL", D::LayerTrail }, { "LAYER_MACRO", D::LayerMacro }, { "LAYER_TRANSMISSION", D::LayerTransmission },
			{ "LAYER_FOLIAGE", D::LayerFoliage },
			{ "SLOT_GRAIN", D::SlotGrain }, { "SLOT_MOSS", D::SlotMoss }, { "SLOT_SAND", D::SlotSand }, { "SLOT_WATER_LINE", D::SlotWaterLine },
			{ "SLOT_TRAIL", D::SlotTrail }, { "SLOT_TRAIL_TINT", D::SlotTrailTint }, { "SLOT_MACRO", D::SlotMacro },
			{ "SLOT_TRANSMISSION", D::SlotTransmission }, { "SLOT_INTERIOR", D::SlotInterior },
			{ "SLOT_TRANSMISSION_EXTRA", D::SlotTransmissionExtra }, { "SLOT_FOLIAGE", D::SlotFoliage }, { "SLOT_FOLIAGE_EXTRA", D::SlotFoliageExtra },
			{ "SLOT_FOLIAGE_SHADE", D::SlotFoliageShade }, { "LAYER_SLOTS", D::LayerSlots },
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
	CheckVisualsJson();
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
