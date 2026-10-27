#include "GB/Audio/AudioMixer.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;

		float SoftClip(float x)
		{
			const float a = std::fabs(x);
			if (a <= 0.8f) return x;
			const float y = 0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f);
			return x < 0.0f ? -y : y;
		}
	}

	const char* BusName(Bus bus)
	{
		switch (bus)
		{
		case Bus::Sfx: return "sfx";
		case Bus::Ambient: return "ambient";
		case Bus::Music: return "music";
		case Bus::Ui: return "ui";
		}
		return "?";
	}

	float AudioMixer::DistanceGain(float distance, float minDistance, float maxDistance, float rolloff)
	{
		if (distance <= minDistance) return 1.0f;
		if (distance >= maxDistance) return 0.0f;
		const float g = minDistance / (minDistance + rolloff * (distance - minDistance));
		const float tail = std::clamp((maxDistance - distance) / (0.25f * maxDistance), 0.0f, 1.0f);   // reaches silence at the max
		return g * tail;
	}

	void AudioMixer::SetFade(Voice& v, float target, float seconds, int rate)
	{
		v.FadeTarget = target;
		if (seconds <= 0.0f) { v.Fade = target; v.FadeStep = 0.0f; return; }
		v.FadeStep = (target - v.Fade) / (seconds * static_cast<float>(rate));
	}

	VoiceId AudioMixer::Play(std::shared_ptr<const AudioClip> clip, const PlayParams& params, const std::string& label)
	{
		if (!clip || clip->Frames() == 0) return NoVoice;
		std::lock_guard<std::mutex> lock(m_Mutex);
		if (m_Voices.size() >= kMaxVoices)
		{
			auto victim = m_Voices.end();
			for (auto it = m_Voices.begin(); it != m_Voices.end(); ++it)
				if (!it->P.Loop && (victim == m_Voices.end() || it->Heard < victim->Heard)) victim = it;
			if (victim == m_Voices.end()) return NoVoice;
			m_Voices.erase(victim);
		}
		Voice v;
		v.Id = m_NextId++;
		v.Clip = std::move(clip);
		v.Label = label;
		v.P = params;
		v.P.Pitch = std::max(params.Pitch, 0.01f);
		if (params.FadeIn > 0.0f) { v.Fade = 0.0f; SetFade(v, 1.0f, params.FadeIn, m_Rate); }
		m_Voices.push_back(std::move(v));
		return m_Voices.back().Id;
	}

	void AudioMixer::Stop(VoiceId id, float fadeSeconds)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (Voice& v : m_Voices)
			if (v.Id == id)
			{
				if (fadeSeconds <= 0.0f) v.Finished = true;
				else SetFade(v, 0.0f, fadeSeconds, m_Rate);
			}
	}

	void AudioMixer::StopAll(float fadeSeconds)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (Voice& v : m_Voices)
		{
			if (fadeSeconds <= 0.0f) v.Finished = true;
			else SetFade(v, 0.0f, fadeSeconds, m_Rate);
		}
	}

	bool AudioMixer::IsPlaying(VoiceId id) const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (const Voice& v : m_Voices)
			if (v.Id == id) return !v.Finished;
		return false;
	}

	size_t AudioMixer::ActiveVoices() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return static_cast<size_t>(std::count_if(m_Voices.begin(), m_Voices.end(), [](const Voice& v) { return !v.Finished; }));
	}

	std::vector<VoiceInfo> AudioMixer::Voices() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		std::vector<VoiceInfo> out;
		for (const Voice& v : m_Voices)
		{
			if (v.Finished) continue;
			VoiceInfo i;
			i.Id = v.Id;
			i.Clip = v.Label;
			i.BusId = v.P.BusId;
			i.Volume = v.P.Volume;
			i.Gain = v.Heard;
			i.Loop = v.P.Loop;
			i.Spatial = v.P.Spatial;
			i.Time = static_cast<float>(v.Pos / v.Clip->SampleRate());
			i.Duration = v.Clip->Duration();
			out.push_back(i);
		}
		return out;
	}

	void AudioMixer::SetVolume(VoiceId id, float volume)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (Voice& v : m_Voices) if (v.Id == id) v.P.Volume = std::max(volume, 0.0f);
	}

	void AudioMixer::SetPitch(VoiceId id, float pitch)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (Voice& v : m_Voices) if (v.Id == id) v.P.Pitch = std::max(pitch, 0.01f);
	}

	void AudioMixer::SetPosition(VoiceId id, const glm::vec3& position)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (Voice& v : m_Voices) if (v.Id == id) v.P.Position = position;
	}

	void AudioMixer::SetListener(const Listener& listener)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Listener = listener;
	}

	Listener AudioMixer::GetListener() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_Listener;
	}

	void AudioMixer::SetBusVolume(Bus bus, float volume)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_BusVolume[static_cast<size_t>(bus)] = std::max(volume, 0.0f);
	}

	float AudioMixer::GetBusVolume(Bus bus) const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_BusVolume[static_cast<size_t>(bus)];
	}

	void AudioMixer::SetMasterVolume(float volume)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Master = std::max(volume, 0.0f);
	}

	float AudioMixer::GetMasterVolume() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_Master;
	}

	float AudioMixer::BusLevel(Bus bus) const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_BusLevel[static_cast<size_t>(bus)];
	}

	float AudioMixer::MasterLevel() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_MasterLevel;
	}

	// Per-voice gains (before the bus): volume, distance and equal-power pan.
	void AudioMixer::Gains(const Voice& v, float& left, float& right, float& heard) const
	{
		float gain = v.P.Volume, pan = v.P.Pan;
		if (v.P.Spatial)
		{
			const glm::vec3 to = v.P.Position - m_Listener.Position;
			const float d = glm::length(to);
			gain *= DistanceGain(d, v.P.MinDistance, v.P.MaxDistance, v.P.Rolloff);
			const glm::vec3 fwd = glm::normalize(m_Listener.Forward);
			const glm::vec3 right3 = glm::cross(fwd, m_Listener.Up);
			const float rl = glm::length(right3);
			pan = d > 1e-3f && rl > 1e-6f ? glm::dot(to / d, right3 / rl) : 0.0f;
			// sources very close to the listener do not hard-pan
			pan *= std::min(d / std::max(v.P.MinDistance, 0.1f), 1.0f);
		}
		pan = std::clamp(pan, -1.0f, 1.0f);
		const float angle = (pan + 1.0f) * kPi * 0.25f;
		heard = gain;
		left = gain * std::cos(angle);    // equal power: a centered sound is 0.707 * gain on each side
		right = gain * std::sin(angle);
	}

	void AudioMixer::Render(float* out, uint32_t frames)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		const size_t n = static_cast<size_t>(frames) * 2;
		m_Scratch.assign(n * kBusCount, 0.0f);

		for (Voice& v : m_Voices)
		{
			if (v.Finished) continue;
			float tl, tr;
			Gains(v, tl, tr, v.Heard);
			if (!v.GainsReady) { v.GainL = tl; v.GainR = tr; v.GainsReady = true; }
			const AudioClip& clip = *v.Clip;
			const std::vector<float>& smp = clip.Samples();
			const double len = static_cast<double>(smp.size());
			const double step = static_cast<double>(v.P.Pitch) * clip.SampleRate() / m_Rate;
			float* bus = &m_Scratch[static_cast<size_t>(v.P.BusId) * n];
			for (uint32_t i = 0; i < frames; ++i)
			{
				if (v.Pos >= len)
				{
					if (v.P.Loop) v.Pos = std::fmod(v.Pos, len);
					else { v.Finished = true; break; }
				}
				const size_t i0 = static_cast<size_t>(v.Pos);
				const size_t i1 = i0 + 1 < smp.size() ? i0 + 1 : (v.P.Loop ? 0 : i0);
				const float frac = static_cast<float>(v.Pos - static_cast<double>(i0));
				const float s = smp[i0] + (smp[i1] - smp[i0]) * frac;
				const float t = static_cast<float>(i) / static_cast<float>(frames);
				if (v.Fade != v.FadeTarget)
				{
					v.Fade += v.FadeStep;
					if ((v.FadeStep > 0.0f && v.Fade >= v.FadeTarget) || (v.FadeStep <= 0.0f && v.Fade <= v.FadeTarget)) { v.Fade = v.FadeTarget; v.FadeStep = 0.0f; }
				}
				bus[i * 2] += s * (v.GainL + (tl - v.GainL) * t) * v.Fade;
				bus[i * 2 + 1] += s * (v.GainR + (tr - v.GainR) * t) * v.Fade;
				v.Pos += step;
			}
			v.GainL = tl;
			v.GainR = tr;
			if (v.FadeTarget <= 0.0f && v.Fade <= 0.0f) v.Finished = true;
		}
		m_Voices.erase(std::remove_if(m_Voices.begin(), m_Voices.end(), [](const Voice& v) { return v.Finished; }), m_Voices.end());

		std::fill(out, out + n, 0.0f);
		for (int b = 0; b < kBusCount; ++b)
		{
			const float* src = &m_Scratch[static_cast<size_t>(b) * n];
			float peak = 0.0f;
			const float from = m_BusApplied[static_cast<size_t>(b)], to = m_BusVolume[static_cast<size_t>(b)];
			for (uint32_t i = 0; i < frames; ++i)
			{
				const float g = from + (to - from) * (static_cast<float>(i) / static_cast<float>(frames));
				out[i * 2] += src[i * 2] * g;
				out[i * 2 + 1] += src[i * 2 + 1] * g;
				peak = std::max(peak, std::max(std::fabs(src[i * 2]), std::fabs(src[i * 2 + 1])));
			}
			m_BusApplied[static_cast<size_t>(b)] = to;
			m_BusLevel[static_cast<size_t>(b)] = peak;
		}
		float peak = 0.0f;
		for (uint32_t i = 0; i < frames; ++i)
		{
			const float g = m_MasterApplied + (m_Master - m_MasterApplied) * (static_cast<float>(i) / static_cast<float>(frames));
			out[i * 2] = SoftClip(out[i * 2] * g);
			out[i * 2 + 1] = SoftClip(out[i * 2 + 1] * g);
			peak = std::max(peak, std::max(std::fabs(out[i * 2]), std::fabs(out[i * 2 + 1])));
		}
		m_MasterApplied = m_Master;
		m_MasterLevel = peak;
	}
}
