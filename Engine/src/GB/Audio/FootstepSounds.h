#pragma once
#include "GB/Audio/AudioEngine.h"
#include <functional>
#include <glm/glm.hpp>

namespace GB
{
	// Plays a footstep for a foot landing: the sound of the surface under the foot (grass, stone, snow...), a few
	// variations rotated through, spatial at the foot so it pans and fades with the listener.
	class FootstepSounds
	{
	public:
		using SurfaceFunction = std::function<Surface(const glm::vec3& worldPosition)>;

		explicit FootstepSounds(AudioEngine& audio) : m_Audio(audio) {}
		void SetSurfaceFunction(SurfaceFunction fn) { m_Surface = std::move(fn); }

		// `speed` (m/s) makes running steps louder than walking ones; `weight` scales with how much of the pose is gait.
		VoiceId Step(const glm::vec3& worldPosition, float speed, float weight = 1.0f);
		Surface LastSurface() const { return m_Last; }
		int Count() const { return m_Count; }

		static const char* PresetFor(Surface surface);

	private:
		AudioEngine& m_Audio;
		SurfaceFunction m_Surface;
		Surface m_Last = Surface::Grass;
		int m_Count = 0;
	};
}
