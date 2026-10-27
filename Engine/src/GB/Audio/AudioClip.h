#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	enum class NoiseColor { White, Pink, Brown };
	enum class Surface { Grass, Dirt, Stone, Sand, Wood, Snow, Water };

	// Mono audio data as floats in [-1, 1]. Clips come from files (wav, flac, mp3) or from the synthesizers below,
	// which are deterministic (same seed, same samples) so sound is testable and needs no asset files.
	class AudioClip
	{
	public:
		AudioClip() = default;
		AudioClip(std::vector<float> samples, int sampleRate) : m_Samples(std::move(samples)), m_Rate(sampleRate) {}

		const std::vector<float>& Samples() const { return m_Samples; }
		int SampleRate() const { return m_Rate; }
		size_t Frames() const { return m_Samples.size(); }
		float Duration() const { return m_Rate > 0 ? static_cast<float>(m_Samples.size()) / static_cast<float>(m_Rate) : 0.0f; }
		float Peak() const;
		float Rms() const;

		// Scales so the loudest sample is `peak`.
		void Normalize(float peak);
		// Cross-fades the tail into the start and drops it, so the clip loops without a click (it gets shorter).
		void MakeLoopable(float crossfadeSeconds);

		// Decodes a wav / flac / mp3 file, mixing it down to mono. Null when it cannot be read.
		static std::shared_ptr<AudioClip> LoadFile(const std::string& path);

		// ---- synthesizers (all mono, `rate` Hz) ----
		static AudioClip Tone(float frequency, float seconds, float amplitude = 0.5f, int rate = 44100);
		static AudioClip Noise(NoiseColor color, float seconds, uint32_t seed = 1, int rate = 44100);
		static AudioClip Wind(float seconds, uint32_t seed = 1, float gustiness = 0.6f, int rate = 44100);       // loopable
		static AudioClip Rain(float seconds, float intensity, uint32_t seed = 1, int rate = 44100);               // loopable
		static AudioClip River(float seconds, uint32_t seed = 1, int rate = 44100);                               // loopable
		static AudioClip Thunder(float seconds, uint32_t seed = 1, int rate = 44100);
		static AudioClip Birds(float seconds, uint32_t seed = 1, int rate = 44100);                               // loopable
		static AudioClip Crickets(float seconds, uint32_t seed = 1, int rate = 44100);                            // loopable
		static AudioClip Footstep(Surface surface, uint32_t seed = 1, int rate = 44100);
		static AudioClip Boom(float seconds, uint32_t seed = 1, int rate = 44100);                                // explosions, collapses

	private:
		std::vector<float> m_Samples;
		int m_Rate = 44100;
	};

	// Wav output (16-bit PCM) for QA and for listening to what the engine mixes. `samples` are interleaved.
	bool SaveWav(const std::string& path, const std::vector<float>& samples, int sampleRate, int channels);
}
