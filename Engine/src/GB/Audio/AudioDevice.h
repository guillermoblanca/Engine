#pragma once
#include "GB/Audio/AudioMixer.h"
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	// Decodes a wav / flac / mp3 file to mono floats at its own sample rate.
	bool DecodeAudioFile(const std::string& path, std::vector<float>& mono, int& sampleRate);

	// Plays a mixer through the system audio output (miniaudio). With `nullBackend`, or when the machine has no sound
	// device (CI, containers), it runs against a silent clock instead, so the whole audio path still executes.
	class AudioDevice
	{
	public:
		static std::unique_ptr<AudioDevice> Create(AudioMixer& mixer, bool nullBackend = false);
		~AudioDevice();
		AudioDevice(const AudioDevice&) = delete;
		AudioDevice& operator=(const AudioDevice&) = delete;

		const std::string& Name() const { return m_Name; }
		bool IsNull() const { return m_Null; }

	private:
		AudioDevice() = default;
		struct Impl;
		std::unique_ptr<Impl> m_Impl;
		std::string m_Name;
		bool m_Null = false;
	};
}
