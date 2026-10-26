#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace GB
{
	class Framebuffer;
	class Shader;

	struct WaterRipplesDesc
	{
		float Size = 48.0f;          // meters covered around the center
		uint32_t Resolution = 512;
		float WaveSpeed = 2.4f;      // m/s of the ripples
		float Damping = 0.9f;        // 1/s energy loss
	};

	// A 2D wave-equation simulation (height + velocity) on a scrolling window around a center, for ripples
	// from raindrops, footsteps and swimmers. The water shader reads the height for normals and foam.
	// The window is a torus like GroundInteraction; waves are absorbed near its border. Needs OpenGL 4.5.
	class WaterRipples
	{
	public:
		explicit WaterRipples(const WaterRipplesDesc& desc = {});
		~WaterRipples();
		WaterRipples(const WaterRipples&) = delete;
		WaterRipples& operator=(const WaterRipples&) = delete;

		bool IsValid() const;
		WaterRipplesDesc Desc;

		// Queue a drop: a Gaussian bump of `radius` meters and `strength` (meters of initial height).
		void Impulse(const glm::vec3& position, float radius, float strength);
		// Moves the window, applies the queued drops and advances the simulation (fixed internal steps).
		void Update(float dt, const glm::vec3& center);
		void Clear();

		glm::vec4 GetRect() const { return { m_Center.x, m_Center.y, Desc.Size, Desc.Size / static_cast<float>(Desc.Resolution) }; }
		uint32_t GetTexture() const;     // R = height (m), G = velocity
		float ReadHeight(float x, float z) const;   // GPU readback, for tests
		size_t GetPendingCount() const { return m_Pending.size(); }

	private:
		struct Drop { glm::vec2 Center; float Radius, Strength; };
		void ClearStrips(const glm::ivec2& from, const glm::ivec2& to);
		void Step(float dt);

		std::unique_ptr<Framebuffer> m_Buffers[2];
		int m_Current = 0;
		std::unique_ptr<Shader> m_StepShader, m_DropShader;
		uint32_t m_EmptyVAO = 0, m_VAO = 0, m_VBO = 0;
		std::vector<Drop> m_Pending;
		glm::ivec2 m_CenterTexel{ 0 };
		glm::vec2 m_Center{ 0.0f };
		bool m_HasCenter = false;
		float m_Accumulator = 0.0f;
	};
}
