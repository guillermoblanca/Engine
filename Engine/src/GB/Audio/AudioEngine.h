#pragma once
#include "GB/Audio/AudioDevice.h"
#include "GB/Audio/AudioMixer.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	// Clips by name: a synthesized preset ("wind", "rain_heavy", "step_grass:3", "tone:440"...) or a sound file path
	// (relative to the asset directory). Clips are created once and shared.
	class AudioLibrary
	{
	public:
		void SetAssetDir(std::string dir) { m_AssetDir = std::move(dir); }
		// Presets accept ":n": a seed for the footsteps and weather loops, the frequency for "tone".
		std::shared_ptr<const AudioClip> Get(const std::string& name);
		static std::vector<std::string> PresetNames();

	private:
		std::string m_AssetDir;
		std::map<std::string, std::shared_ptr<const AudioClip>> m_Clips;
	};

	struct AudioConfig
	{
		int SampleRate = 48000;
		bool Device = true;          // false: no output at all; drive the mixer with Render() (tests, offline rendering)
		bool NullDevice = false;     // true: a silent device clock instead of the speakers
	};

	// The audio side of the engine: a mixer, the clip library and (optionally) the output device.
	class AudioEngine
	{
	public:
		explicit AudioEngine(const AudioConfig& config = {});
		~AudioEngine();

		AudioMixer& Mixer() { return m_Mixer; }
		AudioLibrary& Library() { return m_Library; }
		bool HasDevice() const { return m_Device != nullptr; }
		std::string DeviceName() const { return m_Device ? m_Device->Name() + (m_Device->IsNull() ? " (silent)" : "") : "none"; }

		VoiceId Play(const std::string& clip, const PlayParams& params = {});

		// Mixes `seconds` of what is playing into a wav file, as fast as possible. For engines without a device.
		bool RenderToWav(const std::string& path, float seconds);

	private:
		AudioMixer m_Mixer;
		AudioLibrary m_Library;
		std::unique_ptr<AudioDevice> m_Device;
	};
}
