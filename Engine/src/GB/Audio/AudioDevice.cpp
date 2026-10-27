#include "GB/Audio/AudioDevice.h"
#include "GB/Log.h"

#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

namespace GB
{
	bool DecodeAudioFile(const std::string& path, std::vector<float>& mono, int& sampleRate)
	{
		ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, 0);   // 1 channel: mixed down; rate 0: the file's own
		ma_decoder decoder;
		if (ma_decoder_init_file(path.c_str(), &cfg, &decoder) != MA_SUCCESS) return false;
		sampleRate = static_cast<int>(decoder.outputSampleRate);
		mono.clear();
		std::vector<float> block(4096);
		for (;;)
		{
			ma_uint64 read = 0;
			const ma_result r = ma_decoder_read_pcm_frames(&decoder, block.data(), block.size(), &read);
			mono.insert(mono.end(), block.begin(), block.begin() + static_cast<std::ptrdiff_t>(read));
			if (r != MA_SUCCESS || read == 0) break;
		}
		ma_decoder_uninit(&decoder);
		return !mono.empty();
	}

	struct AudioDevice::Impl
	{
		ma_context Context;
		ma_device Device;
		bool ContextReady = false, DeviceReady = false;

		static void Callback(ma_device* device, void* output, const void*, ma_uint32 frames)
		{
			static_cast<AudioMixer*>(device->pUserData)->Render(static_cast<float*>(output), frames);
		}

		bool Open(AudioMixer& mixer, bool nullBackend)
		{
			const ma_backend nullOnly[] = { ma_backend_null };
			if (ma_context_init(nullBackend ? nullOnly : nullptr, nullBackend ? 1 : 0, nullptr, &Context) != MA_SUCCESS) return false;
			ContextReady = true;
			ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
			cfg.playback.format = ma_format_f32;
			cfg.playback.channels = 2;
			cfg.sampleRate = static_cast<ma_uint32>(mixer.SampleRate());
			cfg.dataCallback = &Impl::Callback;
			cfg.pUserData = &mixer;
			if (ma_device_init(&Context, &cfg, &Device) != MA_SUCCESS) return false;
			DeviceReady = true;
			return ma_device_start(&Device) == MA_SUCCESS;
		}

		~Impl()
		{
			if (DeviceReady) ma_device_uninit(&Device);
			if (ContextReady) ma_context_uninit(&Context);
		}
	};

	std::unique_ptr<AudioDevice> AudioDevice::Create(AudioMixer& mixer, bool nullBackend)
	{
		for (int attempt = 0; attempt < 2; ++attempt)
		{
			const bool useNull = nullBackend || attempt == 1;
			if (attempt == 1 && nullBackend) break;
			std::unique_ptr<AudioDevice> dev(new AudioDevice());
			dev->m_Impl = std::make_unique<Impl>();
			if (!dev->m_Impl->Open(mixer, useNull)) continue;
			dev->m_Null = useNull;
			dev->m_Name = dev->m_Impl->Device.playback.name;
			if (!nullBackend && useNull) GB_CORE_WARN("Audio: no sound device available, running silent");
			return dev;
		}
		return nullptr;
	}

	AudioDevice::~AudioDevice() = default;
}
