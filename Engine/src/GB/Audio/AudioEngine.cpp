#include "GB/Audio/AudioEngine.h"
#include "GB/Log.h"
#include <algorithm>
#include <cstdlib>

namespace GB
{
	namespace
	{
		struct Preset { const char* Name; };
		const char* kPresets[] = {
			"tone", "wind", "rain_light", "rain", "rain_heavy", "river", "thunder", "birds", "crickets", "boom",
			"step_grass", "step_dirt", "step_stone", "step_sand", "step_wood", "step_snow", "step_water" };

		bool Synthesize(const std::string& base, uint32_t arg, bool hasArg, AudioClip& out)
		{
			const uint32_t seed = hasArg ? arg + 1 : 1;
			if (base == "tone") { out = AudioClip::Tone(hasArg ? static_cast<float>(arg) : 440.0f, 1.0f); return true; }
			if (base == "wind") { out = AudioClip::Wind(12.0f, seed); return true; }
			if (base == "rain_light") { out = AudioClip::Rain(8.0f, 0.2f, seed); return true; }
			if (base == "rain") { out = AudioClip::Rain(8.0f, 0.55f, seed); return true; }
			if (base == "rain_heavy") { out = AudioClip::Rain(8.0f, 1.0f, seed); return true; }
			if (base == "river") { out = AudioClip::River(8.0f, seed); return true; }
			if (base == "thunder") { out = AudioClip::Thunder(5.0f, seed); return true; }
			if (base == "birds") { out = AudioClip::Birds(10.0f, seed); return true; }
			if (base == "crickets") { out = AudioClip::Crickets(8.0f, seed); return true; }
			if (base == "boom") { out = AudioClip::Boom(2.5f, seed); return true; }
			static const std::pair<const char*, Surface> steps[] = { { "step_grass", Surface::Grass }, { "step_dirt", Surface::Dirt }, { "step_stone", Surface::Stone },
				{ "step_sand", Surface::Sand }, { "step_wood", Surface::Wood }, { "step_snow", Surface::Snow }, { "step_water", Surface::Water } };
			for (const auto& [name, surface] : steps)
				if (base == name) { out = AudioClip::Footstep(surface, seed); return true; }
			return false;
		}
	}

	std::vector<std::string> AudioLibrary::PresetNames() { return { std::begin(kPresets), std::end(kPresets) }; }

	std::shared_ptr<const AudioClip> AudioLibrary::Get(const std::string& name)
	{
		auto it = m_Clips.find(name);
		if (it != m_Clips.end()) return it->second;

		std::shared_ptr<const AudioClip> clip;
		const size_t colon = name.find(':');
		const std::string base = name.substr(0, colon);
		const bool hasArg = colon != std::string::npos && colon + 1 < name.size() && std::all_of(name.begin() + static_cast<std::ptrdiff_t>(colon) + 1, name.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
		AudioClip synth;
		if (Synthesize(base, hasArg ? static_cast<uint32_t>(std::strtoul(name.c_str() + colon + 1, nullptr, 10)) : 0, hasArg, synth))
			clip = std::make_shared<AudioClip>(std::move(synth));
		else
		{
			const std::string path = m_AssetDir.empty() ? name : m_AssetDir + "/" + name;
			clip = AudioClip::LoadFile(path);
			if (!clip) GB_CORE_WARN("Audio: unknown sound '{}' (not a preset, and '{}' could not be read)", name, path);
		}
		m_Clips[name] = clip;   // failures are remembered too (no retry every call)
		return clip;
	}

	AudioEngine::AudioEngine(const AudioConfig& config) : m_Mixer(config.SampleRate)
	{
		if (config.Device)
		{
			m_Device = AudioDevice::Create(m_Mixer, config.NullDevice);
			if (!m_Device) GB_CORE_WARN("Audio: could not open an output device");
		}
	}

	AudioEngine::~AudioEngine() = default;

	VoiceId AudioEngine::Play(const std::string& clip, const PlayParams& params)
	{
		auto c = m_Library.Get(clip);
		return c ? m_Mixer.Play(std::move(c), params, clip) : NoVoice;
	}

	bool AudioEngine::RenderToWav(const std::string& path, float seconds)
	{
		const uint32_t total = static_cast<uint32_t>(std::max(seconds, 0.0f) * static_cast<float>(m_Mixer.SampleRate()));
		std::vector<float> all;
		all.reserve(static_cast<size_t>(total) * 2);
		std::vector<float> block(512 * 2);
		for (uint32_t done = 0; done < total;)
		{
			const uint32_t n = std::min<uint32_t>(512, total - done);
			m_Mixer.Render(block.data(), n);
			all.insert(all.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(n) * 2);
			done += n;
		}
		return SaveWav(path, all, m_Mixer.SampleRate(), 2);
	}
}
