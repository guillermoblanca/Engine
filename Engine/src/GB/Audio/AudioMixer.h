#pragma once
#include "GB/Audio/AudioClip.h"
#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <mutex>
#include <vector>

namespace GB
{
	enum class Bus { Sfx, Ambient, Music, Ui };
	constexpr int kBusCount = 4;
	const char* BusName(Bus bus);

	using VoiceId = uint32_t;
	constexpr VoiceId NoVoice = 0;

	struct PlayParams
	{
		Bus BusId = Bus::Sfx;
		float Volume = 1.0f;
		float Pitch = 1.0f;            // playback speed (1 = normal)
		bool Loop = false;
		float FadeIn = 0.0f;           // seconds
		// 3D sound: louder when close, panned by where it is relative to the listener.
		bool Spatial = false;
		glm::vec3 Position{ 0.0f };
		float MinDistance = 1.0f;      // full volume inside
		float MaxDistance = 50.0f;     // silent beyond
		float Rolloff = 1.0f;          // how fast it fades between the two
		float Pan = 0.0f;              // non-spatial sounds: -1 left .. 1 right
	};

	struct Listener
	{
		glm::vec3 Position{ 0.0f };
		glm::vec3 Forward{ 0.0f, 0.0f, -1.0f };
		glm::vec3 Up{ 0.0f, 1.0f, 0.0f };
	};

	struct VoiceInfo
	{
		VoiceId Id = NoVoice;
		std::string Clip;
		Bus BusId = Bus::Sfx;
		float Volume = 1.0f;
		float Gain = 0.0f;             // volume after distance, as heard
		bool Loop = false, Spatial = false;
		float Time = 0.0f, Duration = 0.0f;
	};

	// Software mixer: voices with volume, pitch, looping and 3D position are summed into buses and then into a stereo
	// float stream. Render() is plain CPU code, so a game thread, an audio device callback or a test can all call it;
	// everything is protected by one mutex. Output is deterministic.
	class AudioMixer
	{
	public:
		static constexpr size_t kMaxVoices = 64;

		explicit AudioMixer(int sampleRate = 48000) : m_Rate(sampleRate) {}

		int SampleRate() const { return m_Rate; }

		// Returns NoVoice when every voice is busy with looping sounds. Otherwise the quietest one-shot is stolen if needed.
		VoiceId Play(std::shared_ptr<const AudioClip> clip, const PlayParams& params = {}, const std::string& label = "");
		void Stop(VoiceId id, float fadeSeconds = 0.0f);
		void StopAll(float fadeSeconds = 0.0f);
		bool IsPlaying(VoiceId id) const;
		size_t ActiveVoices() const;
		std::vector<VoiceInfo> Voices() const;

		void SetVolume(VoiceId id, float volume);
		void SetPitch(VoiceId id, float pitch);
		void SetPosition(VoiceId id, const glm::vec3& position);

		void SetListener(const Listener& listener);
		Listener GetListener() const;
		void SetBusVolume(Bus bus, float volume);
		float GetBusVolume(Bus bus) const;
		void SetMasterVolume(float volume);
		float GetMasterVolume() const;

		// Peak of the last rendered block, per bus (before the bus volume) and master.
		float BusLevel(Bus bus) const;
		float MasterLevel() const;

		// Fills `out` with `frames` interleaved stereo frames.
		void Render(float* out, uint32_t frames);

		// Gain a spatial sound at `distance` gets (exposed for tests and tools).
		static float DistanceGain(float distance, float minDistance, float maxDistance, float rolloff);

	private:
		struct Voice
		{
			VoiceId Id = NoVoice;
			std::shared_ptr<const AudioClip> Clip;
			std::string Label;
			PlayParams P;
			double Pos = 0.0;          // in clip frames
			float GainL = 0.0f, GainR = 0.0f;
			bool GainsReady = false;
			float Fade = 1.0f, FadeTarget = 1.0f, FadeStep = 0.0f;   // per output frame
			float Heard = 0.0f;        // last block's gain (for stealing and the voice list)
			bool Finished = false;
		};

		void Gains(const Voice& v, float& left, float& right, float& heard) const;
		static void SetFade(Voice& v, float target, float seconds, int rate);

		int m_Rate;
		mutable std::mutex m_Mutex;
		std::vector<Voice> m_Voices;
		VoiceId m_NextId = 1;
		Listener m_Listener;
		std::array<float, kBusCount> m_BusVolume{ 1.0f, 1.0f, 1.0f, 1.0f }, m_BusApplied{ 1.0f, 1.0f, 1.0f, 1.0f }, m_BusLevel{};
		float m_Master = 1.0f, m_MasterApplied = 1.0f, m_MasterLevel = 0.0f;
		std::vector<float> m_Scratch;
	};
}
