#pragma once
#include "Camera.h"
#include "Framebuffer.h"
#include "Mesh.h"
#include "Shader.h"
#include "Texture.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace GB
{
<<<<<<< HEAD
	enum class AlphaMode { Opaque, Mask, Blend };

=======
>>>>>>> origin/master
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
<<<<<<< HEAD

		// glTF-style maps. MetallicRoughnessMap: G = roughness, B = metallic (times the factors above).
		const Texture2D* MetallicRoughnessMap = nullptr;
		const Texture2D* OcclusionMap = nullptr;   // R channel
		float OcclusionStrength = 1.0f;
		const Texture2D* EmissiveMap = nullptr;    // sRGB, multiplied with Emissive

		AlphaMode Alpha = AlphaMode::Opaque;       // Mask: cutout at AlphaCutoff, Blend: sorted translucency
		float AlphaCutoff = 0.5f;
		float Opacity = 1.0f;                      // multiplies the albedo map alpha
		bool DoubleSided = false;
=======
>>>>>>> origin/master
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
	};

	struct RenderObject
	{
		const Mesh* MeshPtr = nullptr;
		Material Mat;
		glm::mat4 Transform{ 1.0f };
	};

	struct Environment
	{
		glm::vec3 SkyTop{ 0.10f, 0.25f, 0.62f };
		glm::vec3 SkyHorizon{ 0.50f, 0.60f, 0.76f };
		glm::vec3 SkyGround{ 0.14f, 0.13f, 0.12f };
		float Intensity = 1.0f;

		float Exposure = 1.0f;
		float BloomThreshold = 1.0f;
		float BloomIntensity = 0.5f;
		float BloomRadius = 1.0f;
		float Vignette = 0.25f;

		// The directional shadow map covers a sphere around this point.
		glm::vec3 ShadowCenter{ 0.0f };
		float ShadowRadius = 12.0f;
	};

	struct Scene
	{
		std::vector<RenderObject> Objects;
		DirectionalLight Sun;
		std::vector<PointLight> PointLights;   // at most MaxPointLights are used
		Environment Env;
	};

	// Forward PBR renderer: shadow map -> MSAA HDR scene -> bloom -> ACES tonemap.
	class Renderer
	{
	public:
		static constexpr uint32_t MaxPointLights = 8;

		Renderer(uint32_t width, uint32_t height, uint32_t msaaSamples = 4, uint32_t shadowMapSize = 2048);
		~Renderer();
		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		bool IsValid() const { return m_Valid; }
		void Resize(uint32_t width, uint32_t height);

		// Renders into `target`, or the default framebuffer when null.
		void Render(const Scene& scene, const Camera& camera, const Framebuffer* target = nullptr);

	private:
		void CreateTargets();
		void ShadowPass(const Scene& scene, const glm::mat4& lightVP);
		void ScenePass(const Scene& scene, const Camera& camera, const glm::mat4& lightVP, bool shadows);
		void BloomPass(const Environment& env);
		void CompositePass(const Environment& env, const Framebuffer* target);

		uint32_t m_Width, m_Height, m_Samples, m_ShadowSize;
		bool m_Valid = false;

		std::unique_ptr<Shader> m_ShadowShader, m_PBRShader, m_SkyShader, m_BloomDown, m_BloomUp, m_Composite;
		std::unique_ptr<Framebuffer> m_ShadowMap, m_SceneMSAA, m_SceneResolved;
		std::vector<std::unique_ptr<Framebuffer>> m_BloomChain;
		uint32_t m_EmptyVAO = 0;
	};
}
