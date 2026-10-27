#pragma once
#include "Camera.h"
#include "Framebuffer.h"
#include "Mesh.h"
#include "Particles.h"
#include "GB/Renderer/EnvironmentMap.h"
#include "GB/World/Grass.h"
#include "GB/World/Water.h"
#include "GB/World/WaterRipples.h"
#include "GB/World/GroundInteraction.h"
#include "Shader.h"
#include "Texture.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace GB
{
	enum class AlphaMode { Opaque, Mask, Blend };

	// Metallic/roughness PBR material (constant parameters; textures come with the glTF importer).
	struct Material
	{
		glm::vec3 Albedo{ 1.0f };
		float Metallic = 0.0f;
		float Roughness = 0.5f;
		float AO = 1.0f;
		glm::vec3 Emissive{ 0.0f };

		// Optional maps (not owned). Albedo is multiplied with `Albedo`.
		const Texture2D* AlbedoMap = nullptr;
		const Texture2D* NormalMap = nullptr;
		const Texture2D* HeightMap = nullptr;   // vertex displacement + cavity AO
		float UVScale = 1.0f;                   // texture repeats per UV unit
		float HeightScale = 0.1f;               // displacement amplitude in object units (0 = off)
		float NormalStrength = 1.0f;
		bool NormalFlipX = false;               // invert the red channel (map disagrees with its height map)
		bool NormalFlipY = false;               // true for DirectX-style normal maps
		float CavityStrength = 0.6f;            // how much the height map darkens crevices

		// glTF-style maps. MetallicRoughnessMap: G = roughness, B = metallic (times the factors above).
		const Texture2D* MetallicRoughnessMap = nullptr;
		const Texture2D* OcclusionMap = nullptr;   // R channel
		float OcclusionStrength = 1.0f;
		const Texture2D* EmissiveMap = nullptr;    // sRGB, multiplied with Emissive

		// Large-scale color: sampled with the world XZ position mapped over MacroRect (x, z, sizeX, sizeZ)
		// and multiplied into the albedo. Terrain uses it for slope/altitude coloring.
		const Texture2D* MacroMap = nullptr;
		glm::vec4 MacroRect{ 0.0f, 0.0f, 1.0f, 1.0f };
		// World-space normal map over the same rect (RGB = normal * 0.5 + 0.5): replaces the interpolated vertex normal with a
		// finer per-pixel one. Needs MacroMap's rect.
		const Texture2D* SurfaceNormalMap = nullptr;

		// Ground marks (needs Scene::Ground): the surface is pressed down by `TrailDepth` meters where the
		// interaction map is 1, darkened toward `TrailTint`, and its normal follows the dent's edges.
		float TrailDepth = 0.0f;
		glm::vec3 TrailTint{ 1.0f };
		float TrailNormalBoost = 1.0f;

		// Wind sway (trees, bushes): `WindSway` scales the bend at `WindHeight` (object-space meters),
		// `WindFlutter` adds small fast leaf motion. 0 = rigid.
		float WindSway = 0.0f;
		float WindHeight = 1.0f;
		float WindFlutter = 0.0f;
		bool FoliageNormals = false;   // cards carry crown normals: light both faces the same way

		// Procedural surface layers (rocks, walls, ruins): noise grain and bump, moss on upward faces, sand drifted
		// at the base, and a wet band below the water line. All off by default.
		float Grain = 0.0f;            // 0..1: albedo variation + bump from 3D noise
		float GrainScale = 3.0f;       // noise cycles per meter
		float Moss = 0.0f;             // 0..1: moss coverage on upward-facing surfaces
		glm::vec3 MossColor{ 0.20f, 0.36f, 0.10f };
		float Sand = 0.0f;             // 0..1: sand around the base
		glm::vec3 SandColor{ 0.76f, 0.66f, 0.45f };
		float BaseY = 0.0f;            // world height of the base (where sand is thickest)
		float WaterLine = -1e9f;       // world height of the water: wet and glossy below it

		AlphaMode Alpha = AlphaMode::Opaque;       // Mask: cutout at AlphaCutoff, Blend: sorted translucency
		float AlphaCutoff = 0.5f;
		float Opacity = 1.0f;                      // multiplies the albedo map alpha
		bool DoubleSided = false;
	};

	struct DirectionalLight
	{
		glm::vec3 Direction{ -0.5f, -1.0f, -0.35f };   // direction the light travels
		glm::vec3 Color{ 1.0f, 0.94f, 0.82f };
		float Intensity = 4.0f;
		bool CastShadows = true;
	};

	struct PointLight
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Color{ 1.0f };
		float Intensity = 20.0f;   // radiant intensity; falls off with 1/d^2
		float Radius = 0.0f;       // > 0: the light fades smoothly to zero at this distance (lamps, torches)
	};

	struct RenderObject
	{
		const Mesh* MeshPtr = nullptr;
		Material Mat;
		glm::mat4 Transform{ 1.0f };
		// Skinned meshes: joint palette (joint world * inverse bind), uploaded when drawing. Not owned.
		const std::vector<glm::mat4>* JointMatrices = nullptr;
		// Culling (forests, props): with BoundingRadius > 0 the object is skipped when its bounding sphere is outside
		// the view (and outside the shadow volume for shadows) or farther than MaxDistance (> 0) from the camera.
		glm::vec3 BoundingCenter{ 0.0f };      // in the object's local space
		float BoundingRadius = 0.0f;           // local units (scaled by the transform)
		float MaxDistance = 0.0f;
	};

	struct RenderStats
	{
		uint32_t ObjectsDrawn = 0;
		uint32_t ObjectsCulled = 0;            // by frustum or distance (main pass)
	};

	struct Environment
	{
		glm::vec3 SkyTop{ 0.10f, 0.25f, 0.62f };
		glm::vec3 SkyHorizon{ 0.50f, 0.60f, 0.76f };
		glm::vec3 SkyGround{ 0.14f, 0.13f, 0.12f };
		float Intensity = 1.0f;

		// Exponential-squared distance fog, thinner with height. Density 0 = off.
		glm::vec3 FogColor{ 0.55f, 0.62f, 0.72f };
		float FogDensity = 0.0f;           // ~0.01 light haze, 0.05 heavy fog
		float FogHeightFalloff = 0.0f;     // 0 = uniform; larger = fog hugs the ground
		float Wetness = 0.0f;              // 0..1: darker, glossier non-metal surfaces (rain)
		float SnowCover = 0.0f;            // 0..1: upward-facing surfaces turn white (accumulated snow)
		const EnvironmentMap* EnvMap = nullptr;   // HDRI for the sky, ambient light and reflections (null = procedural sky); not owned
		float Stars = 0.0f;                // 0..1: stars and moon in the sky background (night)
		float CloudCover = 0.0f;           // 0..1: procedural clouds in the sky background
		float Time = 0.0f;                 // seconds; drives wind, waves and other animated shaders
		glm::vec3 Wind{ 2.0f, 0.0f, 0.8f };   // horizontal m/s; grass and trees sway with it

		float Exposure = 1.0f;
		float BloomThreshold = 1.0f;
		float BloomIntensity = 0.5f;
		float BloomRadius = 1.0f;
		float Vignette = 0.25f;

		// The directional shadow map covers a sphere around this point.
		glm::vec3 ShadowCenter{ 0.0f };
		float ShadowRadius = 12.0f;
	};

	enum class Tonemap { ACES, Neutral, Filmic };

	// Post-processing and grading, applied after the HDR scene and the bloom.
	struct PostSettings
	{
		Tonemap Mapper = Tonemap::ACES;
		// Screen-space ambient occlusion: contact shadowing in creases and under objects.
		float SSAO = 0.0f;                 // 0 = off, 1 = full strength
		float SSAORadius = 0.7f;           // meters
		float SSAOBias = 0.04f;
		// Depth of field: blur grows with the distance from the focus plane. 0 = off.
		float DepthOfField = 0.0f;         // maximum blur radius in percent of the screen height (1-3 natural, 5+ strong)
		float FocusDistance = 12.0f;       // meters
		float FocusRange = 8.0f;           // meters around the focus that stay sharp
		// Eye adaptation: exposure follows the average luminance of the frame (Environment::Exposure acts as compensation).
		bool AutoExposure = false;
		float AdaptSpeed = 1.2f;           // 1/s
		float ExposureKey = 0.18f;         // the grey the average luminance is brought to
		float MinExposure = 0.25f, MaxExposure = 6.0f;
		float DeltaTime = 1.0f / 60.0f;    // seconds since the last frame (drives adaptation and grain)
		// Grading (after tone mapping).
		float Saturation = 1.0f;
		float Contrast = 1.0f;
		glm::vec3 Tint{ 1.0f };
		float ChromaticAberration = 0.0f;  // 0..1: colour fringes toward the screen corners
		float Grain = 0.0f;                // 0..1: film grain
	};

	struct Scene
	{
		PostSettings Post;
		std::vector<RenderObject> Objects;
		DirectionalLight Sun;
		std::vector<PointLight> PointLights;   // at most MaxPointLights are used
		std::vector<const ParticleSystem*> Particles;   // drawn after the geometry, not owned
		std::vector<ParticleBatch> ParticleBatches;     // extra ready-made batches (lightning...), drawn after Particles
		const GroundInteraction* Ground = nullptr;      // marks left on the ground (footprints, trails); not owned
		std::vector<const GrassField*> Grass;           // drawn after the opaque geometry, not owned
		std::vector<const WaterSurface*> Water;         // translucent water, drawn after the grass; not owned
		const WaterRipples* Ripples = nullptr;          // ripple heights shared by the water surfaces; not owned
		bool ScreenSpaceReflections = true;             // water reflects the objects around it (screen space); off = sky only
		Environment Env;
	};

	// Forward PBR renderer: shadow map -> MSAA HDR scene -> bloom -> ACES tonemap.
	class Renderer
	{
	public:
		static constexpr uint32_t MaxPointLights = 32;

		Renderer(uint32_t width, uint32_t height, uint32_t msaaSamples = 4, uint32_t shadowMapSize = 2048);
		~Renderer();
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		bool IsValid() const { return m_Valid; }
		void Resize(uint32_t width, uint32_t height);
		const RenderStats& GetStats() const { return m_Stats; }   // of the last Render

		// Renders into `target`, or the default framebuffer when null.
		void Render(const Scene& scene, const Camera& camera, const Framebuffer* target = nullptr);

	private:
		void CreateTargets();
		void ShadowPass(const Scene& scene, const glm::mat4& lightVP);
		void ScenePass(const Scene& scene, const Camera& camera, const glm::mat4& lightVP, bool shadows);
		void ParticlePass(const Scene& scene, const Camera& camera, const glm::mat4& viewProj);
		void DrawBatch(const ParticleBatch& batch);
		void GrassPass(const Scene& scene, const Camera& camera, const glm::mat4& viewProj, const glm::mat4& lightVP, bool shadows);
		void BindEnvironment(Shader& shader, const Environment& env) const;
		void WaterPass(const Scene& scene, const Camera& camera, const glm::mat4& viewProj);
		void BloomPass(const Environment& env);
		void AmbientOcclusionPass(const PostSettings& post, const Camera& camera);
		void AdaptationPass(const PostSettings& post);
		void CompositePass(const Environment& env, const PostSettings& post, const Camera& camera, const Framebuffer* target);

		RenderStats m_Stats;
		uint32_t m_Width, m_Height, m_Samples, m_ShadowSize;
		bool m_Valid = false;

		std::unique_ptr<Shader> m_AOShader, m_AOBlurShader, m_LumShader, m_AdaptShader, m_ShadowShader, m_PBRShader, m_SkyShader, m_BloomDown, m_BloomUp, m_Composite, m_ParticleShader, m_GrassShader, m_WaterShader;
		std::unique_ptr<Framebuffer> m_ShadowMap, m_SceneMSAA, m_SceneResolved, m_SceneDepth;
		std::vector<std::unique_ptr<Framebuffer>> m_BloomChain;
		std::unique_ptr<Framebuffer> m_AO, m_AOBlur, m_LumSmall, m_Adapt[2];
		int m_AdaptCurrent = 0;
		bool m_AdaptValid = false;
		float m_TimeAccum = 0.0f;
		uint32_t m_EmptyVAO = 0;
		uint32_t m_ParticleVAO = 0, m_ParticleVBO = 0, m_GrassVAO = 0;
		size_t m_ParticleVBOCapacity = 0;
		std::unique_ptr<Texture2D> m_SpriteSoft, m_SpriteSmoke, m_SpriteSpark, m_SpriteStreak;
		std::vector<ParticleInstance> m_ParticleScratch;
		uint32_t m_JointBuffer = 0;   // SSBO with the joint palette of the object being drawn

		// Binds the palette for skinned objects and sets uSkinned on `shader`.
		void BindSkin(Shader& shader, const RenderObject& obj);
	};
}
