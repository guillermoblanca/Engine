#pragma once
#include "Texture.h"
#include "GB/Scene/AlphaMode.h"
#include <glm/glm.hpp>

namespace GB
{
	// Metallic/roughness PBR material (constant parameters; textures come with the glTF importer).
	struct Material
	{
		glm::vec3 Albedo{ 1.0f };
		float Metallic = 0.0f;
		float Roughness = 0.5f;
		float AO = 1.0f;
		glm::vec3 Emissive{ 0.0f };

		const Texture2D* AlbedoMap = nullptr;
		const Texture2D* NormalMap = nullptr;
		const Texture2D* HeightMap = nullptr;
		float UVScale = 1.0f;
		float HeightScale = 0.1f;
		float NormalStrength = 1.0f;
		bool NormalFlipX = false;
		bool NormalFlipY = false;
		float CavityStrength = 0.6f;

		const Texture2D* MetallicRoughnessMap = nullptr;
		const Texture2D* OcclusionMap = nullptr;
		float OcclusionStrength = 1.0f;
		const Texture2D* EmissiveMap = nullptr;

		const Texture2D* MacroMap = nullptr;
		glm::vec4 MacroRect{ 0.0f, 0.0f, 1.0f, 1.0f };
		const Texture2D* SurfaceNormalMap = nullptr;

		float TrailDepth = 0.0f;
		glm::vec3 TrailTint{ 1.0f };
		float TrailNormalBoost = 1.0f;

		float WindSway = 0.0f;
		float WindHeight = 1.0f;
		float WindFlutter = 0.0f;
		bool FoliageNormals = false;   // the cards keep their (crown) normal on back faces instead of flipping it to the viewer

		// T46: foliage (leaves, bushes, grass cards; opaque or masked). All 0: off, the material packs as before (MATERIALES.md, "Follaje").
		// - Translucency (0-1): the sun seen through the leaf: a back light around -L plus a diffuse part on the face away from the sun,
		//   tinted by TransmissionTint times the albedo, and shadowed like the sun (a leaf in the shadow of another does not glow).
		float Translucency = 0.0f;
		glm::vec3 TransmissionTint{ 0.85f, 1.0f, 0.42f };   // a light yellow-green: the light through a leaf is warmer than the one off it
		// - ColorVariation (0-1): hue (toward yellow), saturation (toward olive) and brightness shifted per instance by a hash of the object's
		//   position in the world (the instance's matrix in an instanced batch); MacroVariation (0-1): a low-frequency world patch between a
		//   yellowish and an olive green on top.
		float ColorVariation = 0.0f;
		float MacroVariation = 0.0f;
		// - NormalBlend (0-1): the card's normal mixed with the normal of a sphere around the object's origin (its pivot, on the ground, so
		//   the sphere's normals lean up): the inside of a bush or a crown gets sky and soft sun instead of black. The normal map then
		//   perturbs the blended normal, so the card's relief still shows.
		float NormalBlend = 0.0f;
		// - AlphaMipScale (0-~0.5, masked only): the alpha of the mask raised by mip level * scale before the cutoff, in the colour and in the
		//   shadow, so thin cards do not thin out and flicker in the distance (mip averaging lowers their alpha).
		float AlphaMipScale = 0.0f;

		float Grain = 0.0f;
		float GrainScale = 3.0f;
		float Moss = 0.0f;
		glm::vec3 MossColor{ 0.20f, 0.36f, 0.10f };
		float Sand = 0.0f;
		glm::vec3 SandColor{ 0.76f, 0.66f, 0.45f };
		float BaseY = 0.0f;
		float WaterLine = -1e9f;

		AlphaMode Alpha = AlphaMode::Opaque;
		float AlphaCutoff = 0.5f;
		float Opacity = 1.0f;
		bool DoubleSided = false;

		// T34: transmission (glass, ice), for AlphaMode::Blend only. The surface shows the opaque scene behind it, refracted by Ior and dimmed by
		// Beer-Lambert absorption (exp(-Absorption * Thickness), Absorption per metre and channel: what is absorbed, so a bluish ice absorbs
		// red); its reflection is not dimmed by the opacity, and Opacity is not used (Transmission says how much goes through). 0: off.
		float Transmission = 0.0f;
		float Ior = 1.5f;
		glm::vec3 Absorption{ 0.0f };
		float Thickness = 0.05f;   // metres: the absorption's path and how far the refracted ray goes before it is looked up on the screen

		// T101, with Transmission only (MATERIALES.md, "Hielo y cristal v2"):
		// - an interior map (rgb the colour of what is inside: cracks, bubbles; a its density) seen at two depths under the surface, shifted by
		//   the view (parallax), so the cracks look inside the ice and move against the surface. InteriorDepth is in uv units of the map.
		const Texture2D* InteriorMap = nullptr;
		float InteriorDepth = 0.03f;
		float InteriorStrength = 0.8f;
		glm::vec3 InteriorTint{ 1.0f };
		// - scatter (0-1): sunlight through the thin parts (the silhouette and where the sun is behind) and a little wrap in the diffuse.
		float Scatter = 0.0f;
		// - a faint shadow (0: none, as before T101): the density of a dithered shadow in the shadow map.
		float ShadowDensity = 0.0f;
		// - how much the height map (or else the interior's density) changes the absorption's thickness (0: a fixed thickness).
		float ThicknessVariation = 0.5f;
	};
}
