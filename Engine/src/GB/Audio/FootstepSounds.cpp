#include "GB/Audio/FootstepSounds.h"
#include <algorithm>
#include <string>

namespace GB
{
	const char* FootstepSounds::PresetFor(Surface surface)
	{
		switch (surface)
		{
		case Surface::Grass: return "step_grass";
		case Surface::Dirt: return "step_dirt";
		case Surface::Stone: return "step_stone";
		case Surface::Sand: return "step_sand";
		case Surface::Wood: return "step_wood";
		case Surface::Snow: return "step_snow";
		case Surface::Water: return "step_water";
		}
		return "step_grass";
	}

	VoiceId FootstepSounds::Step(const glm::vec3& worldPosition, float speed, float weight)
	{
		m_Last = m_Surface ? m_Surface(worldPosition) : Surface::Grass;
		const int variant = m_Count % 4;
		// small deterministic variation so repeated steps do not sound identical
		const float jitter = static_cast<float>((m_Count * 2654435761u) >> 24 & 0xFF) / 255.0f;
		++m_Count;
		PlayParams p;
		p.BusId = Bus::Sfx;
		p.Spatial = true;
		p.Position = worldPosition;
		p.MinDistance = 2.0f;
		p.MaxDistance = 40.0f;
		p.Volume = std::clamp(0.3f + 0.12f * speed, 0.3f, 1.0f) * std::clamp(weight, 0.0f, 1.0f);
		p.Pitch = 0.94f + 0.12f * jitter;
		return m_Audio.Play(std::string(PresetFor(m_Last)) + ":" + std::to_string(variant), p);
	}
}
