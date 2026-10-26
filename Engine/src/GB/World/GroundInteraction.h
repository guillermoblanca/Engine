#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace GB
{
	class Framebuffer;
	class Shader;

	enum class StampShape
	{
		Circle,       // soft round dent
		Footprint,    // oval print, narrower at the heel; its long axis points along the heading (+local X = toe)
		Capsule       // a segment with rounded ends (drag marks, wheels, tails)
	};

	struct GroundInteractionDesc
	{
		float Size = 64.0f;                // meters covered around the center (a square window)
		uint32_t Resolution = 1024;        // texels per side (Size / Resolution m per texel)
		float RecoveryPerSecond = 0.05f;   // how fast marks fade: depth *= exp(-rate * dt). Sand ~0.08, snow ~0.004
	};

	// A scrolling height-less "dent" map around a center point (usually the player): feet, wheels and bodies
	// stamp marks into it, it slowly recovers, and ground materials (terrain, grass, snow, water ripples)
	// read it to depress, darken or bend. Values are 0 (untouched) .. 1 (full depth).
	//
	// The window is a torus: it follows the center without copying, clearing only the strips that enter.
	// Marks outside the window are forgotten. Needs an OpenGL 4.5 context.
	class GroundInteraction
	{
	public:
		explicit GroundInteraction(const GroundInteractionDesc& desc = {});
		~GroundInteraction();
		GroundInteraction(const GroundInteraction&) = delete;
		GroundInteraction& operator=(const GroundInteraction&) = delete;

		bool IsValid() const;
		GroundInteractionDesc Desc;

		// Queue a mark; they are drawn by the next Update(). `halfSize` = half length (along `angle`) and half width.
		void Stamp(const glm::vec2& centerXZ, const glm::vec2& halfSize, float angleRadians, float strength, StampShape shape);
		// A footprint at a world position pointing along `heading` (horizontal; y is ignored).
		void StampFootprint(const glm::vec3& position, const glm::vec3& heading, float length, float width, float strength = 1.0f);
		void StampCircle(const glm::vec3& position, float radius, float strength = 1.0f);
		// A dragged mark between two points (wheel, tail, belly).
		void StampLine(const glm::vec3& from, const glm::vec3& to, float width, float strength = 1.0f);

		// Moves the window to `center`, draws the queued marks and lets old marks recover.
		void Update(float dt, const glm::vec3& center);
		void Clear();

		// For shaders: x, z of the window center, then its size and the texel size in meters.
		glm::vec4 GetRect() const { return { m_Center.x, m_Center.y, Desc.Size, Desc.Size / static_cast<float>(Desc.Resolution) }; }
		uint32_t GetTexture() const;
		// Mark depth (0..1) at a world position. Reads the GPU texture back: for tests and tools, not per frame.
		float ReadDepth(float x, float z) const;
		size_t GetPendingStampCount() const { return m_Pending.size(); }

	private:
		struct Stamp4 { glm::vec4 A; glm::vec4 B; };
		struct PendingStamp { glm::vec2 Center; glm::vec2 Half; float Angle, Strength; float Shape; };

		void ClearColumns(int from, int count);
		void ClearRows(int from, int count);

		std::unique_ptr<Framebuffer> m_Target;
		std::unique_ptr<Shader> m_StampShader, m_DecayShader;
		uint32_t m_VAO = 0, m_VBO = 0, m_EmptyVAO = 0;
		std::vector<PendingStamp> m_Pending;
		std::vector<Stamp4> m_Instances;
		glm::ivec2 m_CenterTexel{ 0 };
		glm::vec2 m_Center{ 0.0f };
		bool m_HasCenter = false;
	};
}
