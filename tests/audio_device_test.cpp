// The audio output path with miniaudio's silent backend: the device must pull audio from the mixer on its own thread.
// SKIPs (77) when no device (not even the silent one) can be opened.
#include "GB/Audio/AudioEngine.h"
#include "GB/Log.h"
#include <chrono>
#include <cstdio>
#include <thread>

int main()
{
	GB::Log::Init();
	GB::AudioConfig cfg;
	cfg.NullDevice = true;
	GB::AudioEngine engine(cfg);
	if (!engine.HasDevice())
	{
		std::fprintf(stderr, "SKIP: no audio device\n");
		return 77;
	}
	int failures = 0;
	const GB::VoiceId v = engine.Play("tone:440");   // 1 second long
	if (v == GB::NoVoice) { std::fprintf(stderr, "FAIL: could not play\n"); return 1; }
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	const auto voices = engine.Mixer().Voices();
	if (voices.size() != 1 || voices[0].Time <= 0.05f) { std::fprintf(stderr, "FAIL: the device did not advance the voice\n"); ++failures; }
	if (engine.Mixer().MasterLevel() <= 0.0f) { std::fprintf(stderr, "FAIL: nothing was mixed\n"); ++failures; }
	engine.Mixer().StopAll();
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	if (engine.Mixer().ActiveVoices() != 0) { std::fprintf(stderr, "FAIL: stop did not end the voice\n"); ++failures; }
	if (failures) return 1;
	std::puts("audio device tests passed");
	return 0;
}
