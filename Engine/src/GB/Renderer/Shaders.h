#pragma once

// Built-in GLSL sources. Kept as raw strings so the engine has no runtime file dependencies.
//
// Each program is its own text plus `#include "lib/..."` lines; Shaders::Assemble resolves them against the shared pieces in
// ShaderLib/Pieces.h and adds the variant defines (ShaderAssembler.h). Every renderer compiles through Assemble, never the raw text.
// The shader_assembly test compares each stage in Stages() with tests/golden/shaders/<name>.glsl, byte for byte: a change of the assembled
// text is regenerated on purpose (GB_UPDATE_GOLDEN=1) and its diff reviewed.
//
//   ShaderLib/Pieces.h            lib/*: the per-frame block (lib/frame, mirror of FrameConstants.h), the per-material block (lib/material,
//                                 mirror of MaterialData.h), sky, scene targets, trail, skinning,
//                                 height, tree wind, depth, brdf, shadow, PBR noise, foliage, water waves
//   ShaderLib/ObjectShaders.h     ShadowVS/FS, PBRVS/FS
//   ShaderLib/SkyShaders.h        SkyFS
//   ShaderLib/PostShaders.h       FullscreenVS, bloom, SSAO, exposure, composite
//   ShaderLib/ParticleShaders.h   ParticleVS/FS
//   ShaderLib/WaterShaders.h      WaterVS/FS (then InjectWaveSet, World/Water/WaterWaves.h)
//   ShaderLib/GrassShaders.h      GrassVS/FS
#include "GB/Core/Log.h"
#include "GB/Renderer/ShaderAssembler.h"
#include "GB/Renderer/ShaderLib/GrassShaders.h"
#include "GB/Renderer/ShaderLib/ObjectShaders.h"
#include "GB/Renderer/ShaderLib/ParticleShaders.h"
#include "GB/Renderer/ShaderLib/Pieces.h"
#include "GB/Renderer/ShaderLib/PostShaders.h"
#include "GB/Renderer/ShaderLib/SkyShaders.h"
#include "GB/Renderer/ShaderLib/WaterShaders.h"
#include <span>
#include <string>
#include <string_view>

namespace GB::Shaders
{
	// The registry of the `#include` names.
	inline constexpr ShaderPiece kLibrary[] = {
		{ "lib/frame", Lib::Frame },
		{ "lib/material", Lib::Material },
		{ "lib/scene_targets", Lib::SceneTargets },
		{ "lib/sky", Lib::Sky },
		{ "lib/trail_at", Lib::TrailAt },
		{ "lib/object_trail", Lib::ObjectTrail },
		{ "lib/object_skin", Lib::ObjectSkin },
		{ "lib/object_height", Lib::ObjectHeight },
		{ "lib/tree_wind", Lib::TreeWind },
		{ "lib/depth", Lib::Depth },
		{ "lib/brdf", Lib::Brdf },
		{ "lib/shadow", Lib::Shadow },
		{ "lib/noise_pbr", Lib::NoisePbr },
		{ "lib/foliage", Lib::Foliage },
		{ "lib/water_waves", Lib::WaterWaves },
		{ "lib/terrain_layers", Lib::TerrainLayers },   // T115 (PBR FS with GB_TERRAIN_LAYERS)
	};
	inline std::span<const ShaderPiece> Library() { return kLibrary; }

	// A built-in program, ready to compile. On error (a missing piece or an include cycle) it logs and returns an empty string, so the
	// compile fails and the Shader is invalid.
	inline std::string Assemble(std::string_view source, std::span<const std::string_view> defines = {})
	{
		ShaderAssembly assembly = AssembleShader(source, defines, Library());
		if (!assembly.Ok()) GB_CORE_ERROR("Shader assembly failed: {}", assembly.Error);
		return std::move(assembly.Source);
	}

	// Every stage the renderers compile, by the name of its reference file (the shader_assembly test).
	struct Stage
	{
		std::string_view Name;
		std::string_view Source;
	};
	inline constexpr Stage kStages[] = {
		{ "fullscreen_vs", FullscreenVS },
		{ "shadow_vs", ShadowVS },
		{ "shadow_fs", ShadowFS },
		{ "pbr_vs", PBRVS },
		{ "pbr_fs", PBRFS },
		{ "sky_fs", SkyFS },
		{ "bloom_down_fs", BloomDownFS },
		{ "bloom_up_fs", BloomUpFS },
		{ "composite_fs", CompositeFS },
		{ "ao_fs", AOFS },
		{ "ao_blur_fs", AOBlurFS },
		{ "lum_fs", LumFS },
		{ "adapt_fs", AdaptFS },
		{ "particle_vs", ParticleVS },
		{ "particle_fs", ParticleFS },
		{ "water_vs", WaterVS },   // + InjectWaveSet
		{ "water_fs", WaterFS },   // + InjectWaveSet
		{ "grass_vs", GrassVS },
		{ "grass_fs", GrassFS },
	};
	inline std::span<const Stage> Stages() { return kStages; }
}
