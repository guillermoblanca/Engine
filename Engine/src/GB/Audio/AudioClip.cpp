#include "GB/Audio/AudioClip.h"
#include "GB/Audio/AudioDevice.h"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace GB
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;

		struct Rng
		{
			uint32_t S;
			explicit Rng(uint32_t seed) : S(seed * 2654435761u + 0x9e3779b9u) { if (S == 0) S = 1; Next(); Next(); }
			uint32_t Next() { S ^= S << 13; S ^= S >> 17; S ^= S << 5; return S; }
			float Unit() { return static_cast<float>(Next() >> 8) * (1.0f / 16777216.0f); }      // [0, 1)
			float Signed() { return Unit() * 2.0f - 1.0f; }                                     // [-1, 1)
			float Range(float a, float b) { return a + (b - a) * Unit(); }
		};

		struct LowPass
		{
			float A = 1.0f, Z = 0.0f, Z2 = 0.0f;   // two poles: 12 dB per octave, so rumbles do not hiss
			LowPass(float cutoff, int rate) : A(1.0f - std::exp(-2.0f * kPi * cutoff / static_cast<float>(rate))) {}
			void SetCutoff(float cutoff, int rate) { A = 1.0f - std::exp(-2.0f * kPi * cutoff / static_cast<float>(rate)); }
			float Process(float x) { Z += A * (x - Z); Z2 += A * (Z - Z2); return Z2; }
		};

		// Chamberlin state-variable filter.
		struct Svf
		{
			float Low = 0.0f, Band = 0.0f, High = 0.0f;
			void Process(float x, float cutoff, float q, int rate)
			{
				const float f = 2.0f * std::sin(kPi * std::min(cutoff, static_cast<float>(rate) * 0.45f) / static_cast<float>(rate));
				Low += f * Band;
				High = x - Low - q * Band;
				Band += f * High;
			}
		};

		size_t Count(float seconds, int rate) { return static_cast<size_t>(std::max(seconds, 0.01f) * static_cast<float>(rate)); }

		// Linear attack / exponential decay envelope at time t.
		float Env(float t, float attack, float decay)
		{
			if (t < 0.0f) return 0.0f;
			return (t < attack ? t / attack : 1.0f) * std::exp(-t / decay);
		}

		void EdgeFade(std::vector<float>& s, int rate, float seconds)
		{
			const size_t n = std::min(static_cast<size_t>(seconds * static_cast<float>(rate)), s.size() / 2);
			for (size_t i = 0; i < n; ++i)
			{
				const float g = static_cast<float>(i) / static_cast<float>(n);
				s[i] *= g;
				s[s.size() - 1 - i] *= g;
			}
		}

		// Short filtered noise burst with an envelope: the building block of footsteps.
		void AddBurst(std::vector<float>& out, Rng& rng, int rate, float start, float length, float cutoff, float amp, float decay, float highpass = 0.0f)
		{
			LowPass lp(cutoff, rate), hp(std::max(highpass, 1.0f), rate);
			const size_t first = static_cast<size_t>(start * static_cast<float>(rate)), n = static_cast<size_t>(length * static_cast<float>(rate));
			for (size_t i = 0; i < n && first + i < out.size(); ++i)
			{
				const float t = static_cast<float>(i) / static_cast<float>(rate);
				float x = lp.Process(rng.Signed());
				if (highpass > 0.0f) x -= hp.Process(x);
				out[first + i] += x * amp * Env(t, 0.002f, decay);
			}
		}

		void AddThump(std::vector<float>& out, int rate, float start, float freq, float amp, float decay)
		{
			const size_t first = static_cast<size_t>(start * static_cast<float>(rate));
			for (size_t i = 0; first + i < out.size(); ++i)
			{
				const float t = static_cast<float>(i) / static_cast<float>(rate);
				const float env = Env(t, 0.003f, decay);
				if (env < 1e-4f) break;
				out[first + i] += std::sin(2.0f * kPi * freq * (1.0f + 0.5f * std::exp(-t * 40.0f)) * t) * amp * env;
			}
		}
	}

	// ------------------------------------------------------------------ AudioClip
	float AudioClip::Peak() const
	{
		float p = 0.0f;
		for (float s : m_Samples) p = std::max(p, std::fabs(s));
		return p;
	}

	float AudioClip::Rms() const
	{
		if (m_Samples.empty()) return 0.0f;
		double sum = 0.0;
		for (float s : m_Samples) sum += static_cast<double>(s) * static_cast<double>(s);
		return static_cast<float>(std::sqrt(sum / static_cast<double>(m_Samples.size())));
	}

	void AudioClip::Normalize(float peak)
	{
		const float p = Peak();
		if (p < 1e-9f) return;
		const float g = peak / p;
		for (float& s : m_Samples) s *= g;
	}

	void AudioClip::MakeLoopable(float crossfadeSeconds)
	{
		const size_t x = std::min(static_cast<size_t>(crossfadeSeconds * static_cast<float>(m_Rate)), m_Samples.size() / 3);
		if (x == 0) return;
		const size_t keep = m_Samples.size() - x;
		// the first x samples become a blend of the (continuing) tail and the head, so end -> start is continuous
		for (size_t i = 0; i < x; ++i)
		{
			const float w = static_cast<float>(i) / static_cast<float>(x);     // 0 = tail, 1 = head
			m_Samples[i] = m_Samples[keep + i] * std::cos(w * kPi * 0.5f) + m_Samples[i] * std::sin(w * kPi * 0.5f);
		}
		m_Samples.resize(keep);
	}

	std::shared_ptr<AudioClip> AudioClip::LoadFile(const std::string& path)
	{
		std::vector<float> samples;
		int rate = 0;
		if (!DecodeAudioFile(path, samples, rate)) return nullptr;
		return std::make_shared<AudioClip>(std::move(samples), rate);
	}

	// ------------------------------------------------------------------ synthesizers
	AudioClip AudioClip::Tone(float frequency, float seconds, float amplitude, int rate)
	{
		std::vector<float> s(Count(seconds, rate));
		for (size_t i = 0; i < s.size(); ++i)
			s[i] = std::sin(2.0f * kPi * frequency * static_cast<float>(i) / static_cast<float>(rate)) * amplitude;
		EdgeFade(s, rate, 0.005f);
		return AudioClip(std::move(s), rate);
	}

	AudioClip AudioClip::Noise(NoiseColor color, float seconds, uint32_t seed, int rate)
	{
		Rng rng(seed);
		std::vector<float> s(Count(seconds, rate));
		float b0 = 0, b1 = 0, b2 = 0, brown = 0;
		for (float& v : s)
		{
			const float w = rng.Signed();
			switch (color)
			{
			case NoiseColor::White: v = w * 0.5f; break;
			case NoiseColor::Pink:   // Paul Kellet's economy filter
				b0 = 0.99765f * b0 + w * 0.0990460f;
				b1 = 0.96300f * b1 + w * 0.2965164f;
				b2 = 0.57000f * b2 + w * 1.0526913f;
				v = (b0 + b1 + b2 + w * 0.1848f) * 0.11f;
				break;
			case NoiseColor::Brown:
				brown = (brown + 0.02f * w) / 1.02f;
				v = brown * 3.5f;
				break;
			}
		}
		AudioClip c(std::move(s), rate);
		c.Normalize(0.5f);
		return c;
	}

	AudioClip AudioClip::Wind(float seconds, uint32_t seed, float gustiness, int rate)
	{
		Rng rng(seed);
		const size_t n = Count(seconds, rate);
		const float dur = static_cast<float>(n) / static_cast<float>(rate);
		std::vector<float> s(n);
		Svf svf;
		LowPass body(220.0f, rate);
		// slow modulators with whole cycles per clip, so they repeat seamlessly
		const float c1 = std::max(1.0f, std::round(dur / 6.0f)), c2 = std::max(2.0f, std::round(dur / 2.3f));
		const float ph1 = rng.Range(0.0f, 2.0f * kPi), ph2 = rng.Range(0.0f, 2.0f * kPi);
		for (size_t i = 0; i < n; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(rate);
			const float g1 = 0.5f + 0.5f * std::sin(2.0f * kPi * c1 * t / dur + ph1);
			const float g2 = 0.5f + 0.5f * std::sin(2.0f * kPi * c2 * t / dur + ph2);
			const float gust = 1.0f - gustiness + gustiness * (0.6f * g1 + 0.4f * g2);
			const float w = rng.Signed();
			svf.Process(w, 350.0f + 900.0f * gust * gust, 0.55f, rate);
			s[i] = (svf.Band * 0.9f + body.Process(w) * 0.9f) * (0.25f + 0.75f * gust);
		}
		AudioClip c(std::move(s), rate);
		c.Normalize(0.5f);
		c.MakeLoopable(std::min(0.5f, seconds * 0.2f));
		return c;
	}

	AudioClip AudioClip::Rain(float seconds, float intensity, uint32_t seed, int rate)
	{
		Rng rng(seed);
		intensity = std::clamp(intensity, 0.0f, 1.0f);
		const size_t n = Count(seconds, rate);
		std::vector<float> s(n);
		LowPass lp(7500.0f, rate), hp(900.0f, rate);
		for (float& v : s)
		{
			float x = lp.Process(rng.Signed());
			x -= hp.Process(x);
			v = x * (0.25f + 0.35f * intensity);
		}
		// individual drops: tiny decaying ticks, more of them and louder in heavy rain
		const int drops = static_cast<int>(seconds * (60.0f + 500.0f * intensity));
		for (int d = 0; d < drops; ++d)
			AddBurst(s, rng, rate, rng.Range(0.0f, seconds), 0.012f, rng.Range(2500.0f, 9000.0f), rng.Range(0.1f, 0.45f) * (0.4f + 0.6f * intensity), 0.003f, 1200.0f);
		AudioClip c(std::move(s), rate);
		c.Normalize(0.35f + 0.4f * intensity);
		c.MakeLoopable(std::min(0.5f, seconds * 0.2f));
		return c;
	}

	AudioClip AudioClip::River(float seconds, uint32_t seed, int rate)
	{
		Rng rng(seed);
		const size_t n = Count(seconds, rate);
		const float dur = static_cast<float>(n) / static_cast<float>(rate);
		std::vector<float> s(n);
		Svf a, b;
		const float ph = rng.Range(0.0f, 6.28f);
		for (size_t i = 0; i < n; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(rate);
			const float w = rng.Signed();
			const float wob = 0.5f + 0.5f * std::sin(2.0f * kPi * std::max(1.0f, std::round(dur / 1.7f)) * t / dur + ph);
			a.Process(w, 500.0f + 700.0f * wob, 0.7f, rate);
			b.Process(w, 1800.0f + 1500.0f * (1.0f - wob), 0.9f, rate);
			s[i] = a.Band * 0.8f + b.Band * 0.35f;
		}
		AudioClip c(std::move(s), rate);
		c.Normalize(0.5f);
		c.MakeLoopable(std::min(0.5f, seconds * 0.2f));
		return c;
	}

	AudioClip AudioClip::Thunder(float seconds, uint32_t seed, int rate)
	{
		Rng rng(seed);
		const size_t n = Count(seconds, rate);
		std::vector<float> s(n);
		// a crack, then rolling rumble made of several decaying low-passed bursts
		AddBurst(s, rng, rate, 0.0f, 0.25f, 5000.0f, 1.0f, 0.04f);
		const int rolls = 6 + static_cast<int>(seconds);
		for (int i = 0; i < rolls; ++i)
			AddBurst(s, rng, rate, rng.Range(0.05f, seconds * 0.7f), rng.Range(0.8f, 2.5f), rng.Range(90.0f, 260.0f), rng.Range(0.5f, 1.4f), rng.Range(0.4f, 1.2f));
		AddThump(s, rate, 0.0f, 55.0f, 0.8f, 0.8f);
		AudioClip c(std::move(s), rate);
		c.Normalize(0.85f);
		EdgeFade(c.m_Samples, rate, 0.01f);
		return c;
	}

	AudioClip AudioClip::Birds(float seconds, uint32_t seed, int rate)
	{
		Rng rng(seed);
		const size_t n = Count(seconds, rate);
		std::vector<float> s(n, 0.0f);
		const int calls = std::max(3, static_cast<int>(seconds * 1.6f));
		for (int c = 0; c < calls; ++c)
		{
			const float start = rng.Range(0.0f, seconds - 0.4f);
			const float base = rng.Range(2200.0f, 4800.0f), sweep = rng.Range(-900.0f, 1400.0f);
			const int chirps = 2 + static_cast<int>(rng.Unit() * 4.0f);
			const float amp = rng.Range(0.15f, 0.4f);
			for (int k = 0; k < chirps; ++k)
			{
				const float t0 = start + static_cast<float>(k) * 0.11f, len = 0.08f;
				const size_t first = static_cast<size_t>(t0 * static_cast<float>(rate));
				float phase = 0.0f;
				for (size_t i = 0; i < static_cast<size_t>(len * static_cast<float>(rate)) && first + i < n; ++i)
				{
					const float t = static_cast<float>(i) / static_cast<float>(rate), u = t / len;
					const float f = base + sweep * u + 120.0f * std::sin(2.0f * kPi * 35.0f * t);
					phase += 2.0f * kPi * f / static_cast<float>(rate);
					s[first + i] += std::sin(phase) * amp * std::sin(kPi * u) * std::sin(kPi * u);
				}
			}
		}
		AudioClip c(std::move(s), rate);
		c.Normalize(0.4f);
		return c;
	}

	AudioClip AudioClip::Crickets(float seconds, uint32_t seed, int rate)
	{
		Rng rng(seed);
		const size_t n = Count(seconds, rate);
		std::vector<float> s(n);
		const int voices = 3;
		for (int v = 0; v < voices; ++v)
		{
			const float freq = rng.Range(4200.0f, 5200.0f), rate1 = rng.Range(18.0f, 28.0f), group = rng.Range(0.6f, 1.1f), ph = rng.Range(0.0f, 1.0f);
			for (size_t i = 0; i < n; ++i)
			{
				const float t = static_cast<float>(i) / static_cast<float>(rate);
				const float chirpGate = std::fmod(t / group + ph, 1.0f) < 0.45f ? 1.0f : 0.0f;
				const float pulse = std::max(0.0f, std::sin(2.0f * kPi * rate1 * t));
				s[i] += std::sin(2.0f * kPi * freq * t) * pulse * chirpGate * 0.3f;
			}
		}
		AudioClip c(std::move(s), rate);
		c.Normalize(0.3f);
		c.MakeLoopable(std::min(0.3f, seconds * 0.2f));
		return c;
	}

	AudioClip AudioClip::Footstep(Surface surface, uint32_t seed, int rate)
	{
		Rng rng(seed);
		const float len = surface == Surface::Water ? 0.5f : surface == Surface::Sand || surface == Surface::Snow ? 0.32f : 0.24f;
		std::vector<float> s(Count(len, rate), 0.0f);
		const float v = rng.Range(0.9f, 1.1f);   // small variation per seed
		switch (surface)
		{
		case Surface::Grass:
			AddBurst(s, rng, rate, 0.0f, 0.2f, 1800.0f * v, 0.9f, 0.05f, 250.0f);
			AddThump(s, rate, 0.0f, 90.0f * v, 0.35f, 0.05f);
			break;
		case Surface::Dirt:
			AddBurst(s, rng, rate, 0.0f, 0.2f, 1100.0f * v, 1.0f, 0.045f);
			AddThump(s, rate, 0.0f, 80.0f * v, 0.7f, 0.07f);
			break;
		case Surface::Stone:
			AddBurst(s, rng, rate, 0.0f, 0.12f, 6500.0f * v, 1.0f, 0.012f, 600.0f);
			AddThump(s, rate, 0.0f, 140.0f * v, 0.4f, 0.04f);
			AddThump(s, rate, 0.0f, 2100.0f * v, 0.12f, 0.01f);
			break;
		case Surface::Sand:
			AddBurst(s, rng, rate, 0.0f, 0.3f, 3500.0f * v, 0.7f, 0.09f, 700.0f);
			AddThump(s, rate, 0.0f, 70.0f, 0.25f, 0.05f);
			break;
		case Surface::Wood:
			AddThump(s, rate, 0.0f, 170.0f * v, 0.9f, 0.06f);
			AddThump(s, rate, 0.0f, 340.0f * v, 0.35f, 0.04f);
			AddBurst(s, rng, rate, 0.0f, 0.05f, 3000.0f, 0.3f, 0.01f, 400.0f);
			break;
		case Surface::Snow:
			for (int i = 0; i < 14; ++i)   // crunch: many tiny grains
				AddBurst(s, rng, rate, rng.Range(0.0f, 0.15f), 0.02f, rng.Range(2500.0f, 7000.0f), rng.Range(0.2f, 0.6f), 0.006f, 800.0f);
			AddThump(s, rate, 0.0f, 75.0f, 0.2f, 0.05f);
			break;
		case Surface::Water:
			AddBurst(s, rng, rate, 0.0f, 0.4f, 3200.0f * v, 0.8f, 0.12f, 300.0f);
			for (int i = 0; i < 4; ++i)   // bubbles: rising sine blips
			{
				const float t0 = rng.Range(0.02f, 0.3f), f0 = rng.Range(500.0f, 1200.0f);
				const size_t first = static_cast<size_t>(t0 * static_cast<float>(rate));
				for (size_t k = 0; k < static_cast<size_t>(0.05f * static_cast<float>(rate)) && first + k < s.size(); ++k)
				{
					const float t = static_cast<float>(k) / static_cast<float>(rate);
					s[first + k] += std::sin(2.0f * kPi * (f0 * t + 6000.0f * t * t)) * 0.25f * Env(t, 0.002f, 0.015f);
				}
			}
			break;
		}
		AudioClip c(std::move(s), rate);
		c.Normalize(0.7f);
		EdgeFade(c.m_Samples, rate, 0.003f);
		return c;
	}

	AudioClip AudioClip::Boom(float seconds, uint32_t seed, int rate)
	{
		Rng rng(seed);
		std::vector<float> s(Count(seconds, rate), 0.0f);
		AddBurst(s, rng, rate, 0.0f, seconds, 3500.0f, 1.0f, 0.10f);
		AddBurst(s, rng, rate, 0.0f, seconds, 400.0f, 1.2f, 0.35f);
		AddThump(s, rate, 0.0f, 45.0f, 1.0f, 0.45f);
		AudioClip c(std::move(s), rate);
		c.Normalize(0.9f);
		EdgeFade(c.m_Samples, rate, 0.004f);
		return c;
	}

	bool SaveWav(const std::string& path, const std::vector<float>& samples, int sampleRate, int channels)
	{
		std::ofstream f(path, std::ios::binary);
		if (!f) return false;
		const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * 2);
		auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
		auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
		f.write("RIFF", 4); u32(36 + dataBytes);
		f.write("WAVEfmt ", 8); u32(16); u16(1); u16(static_cast<uint16_t>(channels));
		u32(static_cast<uint32_t>(sampleRate)); u32(static_cast<uint32_t>(sampleRate * channels * 2)); u16(static_cast<uint16_t>(channels * 2)); u16(16);
		f.write("data", 4); u32(dataBytes);
		for (float v : samples)
		{
			const int16_t q = static_cast<int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
			f.write(reinterpret_cast<const char*>(&q), 2);
		}
		return f.good();
	}
}
