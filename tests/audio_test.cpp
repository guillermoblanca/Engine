// Audio without a sound card: synthesizers, the mixer (distance, pan, pitch, loops, fades, buses), wav files, a
// silent output device, and golden statistics of the presets. CPU only.
#include "GB/Audio/AudioEngine.h"
#include "GB/Log.h"
#include "GB/Qa/Qa.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <thread>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	bool Finite(const GB::AudioClip& c)
	{
		for (float s : c.Samples()) if (!std::isfinite(s)) return false;
		return true;
	}
	std::shared_ptr<const GB::AudioClip> Tone(float f = 440.0f, float secs = 1.0f, int rate = 48000)
	{
		return std::make_shared<GB::AudioClip>(GB::AudioClip::Tone(f, secs, 0.5f, rate));
	}
	// RMS of the left and right channels of a block.
	void Rms(const std::vector<float>& b, float& l, float& r)
	{
		double sl = 0, sr = 0;
		for (size_t i = 0; i + 1 < b.size(); i += 2) { sl += static_cast<double>(b[i]) * b[i]; sr += static_cast<double>(b[i + 1]) * b[i + 1]; }
		const double n = static_cast<double>(b.size() / 2);
		l = static_cast<float>(std::sqrt(sl / n));
		r = static_cast<float>(std::sqrt(sr / n));
	}
	std::vector<float> Block(GB::AudioMixer& m, uint32_t frames = 4800)
	{
		std::vector<float> b(frames * 2);
		m.Render(b.data(), frames);
		return b;
	}
	int ZeroCrossings(const std::vector<float>& s)
	{
		int n = 0;
		for (size_t i = 1; i < s.size(); ++i) if ((s[i - 1] < 0) != (s[i] < 0)) ++n;
		return n;
	}
}

int main()
{
	using namespace GB;
	Log::Init();

	// ---- synthesizers
	{
		const AudioClip a = AudioClip::Wind(4.0f, 7), b = AudioClip::Wind(4.0f, 7), c = AudioClip::Wind(4.0f, 8);
		Check(a.Samples() == b.Samples(), "the same seed gives the same wind");
		Check(a.Samples() != c.Samples(), "another seed gives another wind");

		const AudioClip tone = AudioClip::Tone(440.0f, 1.0f, 0.5f, 44100);
		Check(std::abs(ZeroCrossings(tone.Samples()) - 880) <= 4, "a 440 Hz tone crosses zero about 880 times a second");
		Check(std::fabs(tone.Duration() - 1.0f) < 0.01f, "tone duration");

		struct Named { const char* Name; AudioClip Clip; };
		const Named all[] = {
			{ "white", AudioClip::Noise(NoiseColor::White, 1.0f) }, { "pink", AudioClip::Noise(NoiseColor::Pink, 1.0f) }, { "brown", AudioClip::Noise(NoiseColor::Brown, 1.0f) },
			{ "wind", a }, { "rain", AudioClip::Rain(3.0f, 0.6f) }, { "river", AudioClip::River(3.0f) }, { "thunder", AudioClip::Thunder(4.0f) },
			{ "birds", AudioClip::Birds(4.0f) }, { "crickets", AudioClip::Crickets(3.0f) }, { "boom", AudioClip::Boom(2.0f) },
			{ "grass", AudioClip::Footstep(Surface::Grass) }, { "dirt", AudioClip::Footstep(Surface::Dirt) }, { "stone", AudioClip::Footstep(Surface::Stone) },
			{ "sand", AudioClip::Footstep(Surface::Sand) }, { "wood", AudioClip::Footstep(Surface::Wood) }, { "snow", AudioClip::Footstep(Surface::Snow) },
			{ "water", AudioClip::Footstep(Surface::Water) } };
		for (const Named& n : all)
		{
			char msg[96];
			std::snprintf(msg, sizeof msg, "%s: finite, audible and not clipping", n.Name);
			Check(Finite(n.Clip) && n.Clip.Peak() > 0.05f && n.Clip.Peak() <= 1.0f && n.Clip.Rms() > 0.005f, msg);
		}
		Check(AudioClip::Rain(3.0f, 1.0f).Rms() > AudioClip::Rain(3.0f, 0.1f).Rms(), "heavy rain is louder than light rain");

		// loops: the end continues into the start (jump no bigger than a typical sample step)
		for (const AudioClip* loop : { &a })
		{
			const std::vector<float>& s = loop->Samples();
			double step = 0;
			for (size_t i = 1; i < s.size(); ++i) step += std::fabs(s[i] - s[i - 1]);
			step /= static_cast<double>(s.size());
			Check(std::fabs(s.front() - s.back()) < step * 6.0 + 0.02, "wind loops without a click");
		}

		// footsteps die away
		const AudioClip step = AudioClip::Footstep(Surface::Stone);
		const size_t n = step.Frames();
		AudioClip head(std::vector<float>(step.Samples().begin(), step.Samples().begin() + static_cast<std::ptrdiff_t>(n / 4)), step.SampleRate());
		AudioClip tail(std::vector<float>(step.Samples().end() - static_cast<std::ptrdiff_t>(n / 4), step.Samples().end()), step.SampleRate());
		Check(tail.Rms() < head.Rms() * 0.3f, "a footstep decays");
	}

	// ---- wav round trip
	{
		const AudioClip tone = AudioClip::Tone(300.0f, 0.5f, 0.5f, 22050);
		const std::string path = (std::filesystem::temp_directory_path() / "gb_audio_test.wav").string();
		Check(SaveWav(path, tone.Samples(), 22050, 1), "write wav");
		auto back = AudioClip::LoadFile(path);
		Check(back && back->SampleRate() == 22050 && back->Frames() == tone.Frames(), "read the wav back");
		double err = 0;
		if (back) for (size_t i = 0; i < tone.Frames(); ++i) err = std::max(err, static_cast<double>(std::fabs(tone.Samples()[i] - back->Samples()[i])));
		Check(err < 1e-3, "wav keeps the samples (16-bit)");
		Check(!AudioClip::LoadFile("/no/such/file.wav"), "a missing file is reported");
		std::filesystem::remove(path);
	}

	// ---- mixer: distance
	{
		Check(AudioMixer::DistanceGain(0.5f, 1.0f, 50.0f, 1.0f) == 1.0f, "full volume inside the min distance");
		Check(AudioMixer::DistanceGain(50.0f, 1.0f, 50.0f, 1.0f) == 0.0f, "silent at the max distance");
		float prev = 1.0f; bool mono = true;
		for (float d = 1.0f; d <= 50.0f; d += 1.0f) { const float g = AudioMixer::DistanceGain(d, 1.0f, 50.0f, 1.0f); if (g > prev + 1e-6f) mono = false; prev = g; }
		Check(mono, "volume only falls with distance");
		Check(std::fabs(AudioMixer::DistanceGain(3.0f, 1.0f, 100.0f, 1.0f) - 1.0f / 3.0f) < 0.01f, "inverse distance in between");
	}

	// ---- mixer: panning and attenuation
	{
		auto level = [&](glm::vec3 pos, float& l, float& r)
		{
			AudioMixer m(48000);
			PlayParams p; p.Spatial = true; p.Position = pos; p.Loop = true; p.MaxDistance = 100.0f;
			m.Play(Tone(), p);
			Block(m, 480);   // settle the gain ramp
			Rms(Block(m), l, r);
		};
		float l, r;
		level({ 5, 0, 0 }, l, r);
		Check(r > l * 3.0f, "a sound on the right is heard on the right");
		level({ -5, 0, 0 }, l, r);
		Check(l > r * 3.0f, "a sound on the left is heard on the left");
		level({ 0, 0, -5 }, l, r);
		Check(std::fabs(l - r) < l * 0.05f, "a sound ahead is centered");
		float l2, r2;
		level({ 0, 0, -5 }, l, r);
		level({ 0, 0, -20 }, l2, r2);
		Check(l2 < l * 0.5f, "a farther sound is quieter");
		level({ 0, 0, -500 }, l2, r2);
		Check(l2 == 0.0f && r2 == 0.0f, "beyond the max distance nothing is heard");

		// turning the listener swaps the sides
		AudioMixer m(48000);
		PlayParams p; p.Spatial = true; p.Position = { 5, 0, 0 }; p.Loop = true;
		m.Play(Tone(), p);
		Listener lis; lis.Forward = { 0, 0, 1 };   // looking the other way
		m.SetListener(lis);
		Block(m, 480);
		Rms(Block(m), l, r);
		Check(l > r * 3.0f, "the listener's orientation matters");
	}

	// ---- mixer: pitch, loops, fades, stopping
	{
		AudioMixer m(48000);
		PlayParams p; p.Pitch = 2.0f;
		const VoiceId v = m.Play(Tone(440.0f, 1.0f), p);
		Block(m, 24000 - 100);
		Check(m.IsPlaying(v), "double pitch: still playing just before half the time");
		Block(m, 400);
		Check(!m.IsPlaying(v), "double pitch plays in half the time");

		PlayParams lp; lp.Loop = true;
		const VoiceId loop = m.Play(Tone(440.0f, 0.2f), lp);
		for (int i = 0; i < 10; ++i) Block(m, 48000 / 4);
		Check(m.IsPlaying(loop), "loops keep playing");
		m.Stop(loop, 0.1f);
		Block(m, 2400);
		Check(m.IsPlaying(loop), "a fading voice is still there mid-fade");
		Block(m, 4800);
		Check(!m.IsPlaying(loop) && m.ActiveVoices() == 0, "stop with a fade ends the voice");

		PlayParams fi; fi.FadeIn = 0.5f; fi.Loop = true;
		m.Play(Tone(), fi);
		float a, b, c, d;
		Rms(Block(m, 4800), a, b);
		for (int i = 0; i < 6; ++i) Block(m, 4800);
		Rms(Block(m, 4800), c, d);
		Check(a < c * 0.5f, "fade in starts quiet");
		m.StopAll();
		Block(m, 480);
		Check(m.ActiveVoices() == 0, "stop all");
	}

	// ---- mixer: buses, master, limiter, voice limit
	{
		auto measure = [&](Bus bus, float busVol, float master)
		{
			AudioMixer m(48000);
			m.SetBusVolume(bus, busVol);
			m.SetMasterVolume(master);
			PlayParams p; p.BusId = bus; p.Loop = true;
			m.Play(Tone(), p);
			Block(m, 4800);   // let the volume ramps settle
			float l, r;
			Rms(Block(m), l, r);
			return l;
		};
		const float base = measure(Bus::Sfx, 1.0f, 1.0f);
		Check(base > 0.1f, "a plain voice is audible");
		Check(std::fabs(measure(Bus::Sfx, 0.5f, 1.0f) - base * 0.5f) < base * 0.03f, "bus volume scales the sound");
		Check(std::fabs(measure(Bus::Music, 1.0f, 0.25f) - base * 0.25f) < base * 0.03f, "master volume scales everything");
		Check(measure(Bus::Ambient, 0.0f, 1.0f) == 0.0f, "a muted bus is silent");

		AudioMixer m(48000);
		PlayParams loud; loud.Loop = true; loud.Volume = 8.0f;
		for (int i = 0; i < 6; ++i) m.Play(Tone(100.0f + 50.0f * static_cast<float>(i)), loud);
		float peak = 0;
		for (float s : Block(m, 9600)) peak = std::max(peak, std::fabs(s));
		Check(peak <= 1.0f, "the limiter keeps the output within [-1, 1]");
		Check(m.MasterLevel() > 0.8f && m.BusLevel(Bus::Sfx) > 1.0f && m.BusLevel(Bus::Music) == 0.0f, "level meters");

		AudioMixer full(48000);
		PlayParams lp; lp.Loop = true;
		for (size_t i = 0; i < AudioMixer::kMaxVoices; ++i) full.Play(Tone(), lp);
		Check(full.Play(Tone(), lp) == NoVoice, "no voice is stolen from loops");
		Check(full.ActiveVoices() == AudioMixer::kMaxVoices, "voice limit");
	}

	// ---- the engine: presets, output determinism, offline wav
	{
		AudioConfig cfg; cfg.Device = false;
		AudioEngine e1(cfg), e2(cfg);
		for (AudioEngine* e : { &e1, &e2 })
		{
			PlayParams p; p.Loop = true; p.BusId = Bus::Ambient;
			e->Play("wind", p);
			p.Spatial = true; p.Position = { 4, 0, -3 }; p.BusId = Bus::Sfx; p.Loop = false;
			e->Play("step_stone:2", p);
			e->Play("tone:880", {});
		}
		Check(e1.Library().Get("wind") == e1.Library().Get("wind"), "presets are created once");
		Check(!e1.Library().Get("not_a_sound"), "unknown names are rejected");
		Check(e1.Play("not_a_sound") == NoVoice, "playing an unknown sound does nothing");
		Qa::Fingerprint f1, f2;
		for (int i = 0; i < 20; ++i)
		{
			for (float s : Block(e1.Mixer(), 1024)) f1.Add(s, 1e-4f);
			for (float s : Block(e2.Mixer(), 1024)) f2.Add(s, 1e-4f);
		}
		Check(f1.Value() == f2.Value(), "two engines mix identical output");

		const std::string wav = (std::filesystem::temp_directory_path() / "gb_audio_mix.wav").string();
		Check(e1.RenderToWav(wav, 1.0f), "render to wav");
		auto mixed = AudioClip::LoadFile(wav);
		Check(mixed && mixed->SampleRate() == 48000 && mixed->Frames() == 48000, "the rendered wav has the right length");
		std::filesystem::remove(wav);
		Check(AudioLibrary::PresetNames().size() >= 15, "preset list");
	}

	// ---- golden statistics of the presets (guards the synthesizers against accidental changes)
	{
		Qa::GoldenData d;
		auto add = [&](const char* name, const AudioClip& c)
		{
			d.Set(std::string(name) + ".rms", c.Rms(), 0.02, 0.15);
			d.Set(std::string(name) + ".peak", c.Peak(), 0.02, 0.1);
			d.Set(std::string(name) + ".frames", static_cast<double>(c.Frames()), 2.0);
			d.Set(std::string(name) + ".zerocross", static_cast<double>(ZeroCrossings(c.Samples())), 10.0, 0.2);
		};
		add("wind", AudioClip::Wind(6.0f, 1));
		add("rain", AudioClip::Rain(4.0f, 0.55f, 1));
		add("river", AudioClip::River(4.0f, 1));
		add("thunder", AudioClip::Thunder(4.0f, 1));
		add("birds", AudioClip::Birds(5.0f, 1));
		add("crickets", AudioClip::Crickets(4.0f, 1));
		add("boom", AudioClip::Boom(2.0f, 1));
		add("step_grass", AudioClip::Footstep(Surface::Grass, 1));
		add("step_stone", AudioClip::Footstep(Surface::Stone, 1));
		add("step_snow", AudioClip::Footstep(Surface::Snow, 1));
		const Qa::Result r = d.Check(GB_GOLDEN_DIR "/data", "audio_presets");
		if (!r.Passed) { std::fprintf(stderr, "FAIL: %s\n", r.Message.c_str()); ++failures; }
	}

	if (failures) return 1;
	std::puts("audio tests passed");
	return 0;
}
