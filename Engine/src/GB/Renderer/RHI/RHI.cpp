#include "RHI.h"
#include <cstdlib>
#include <string>

namespace GB::RHI
{
	namespace
	{
		Device* g_Current = nullptr;

		// getenv without MSVC's deprecation warning (the library builds with warnings as errors), as in Qa.cpp.
		std::string Env(const char* name)
		{
#if defined(_WIN32)
			char* value = nullptr;
			size_t size = 0;
			std::string result;
			if (_dupenv_s(&value, &size, name) == 0 && value)
				result = value;
			std::free(value);
			return result;
#else
			const char* v = std::getenv(name);
			return v ? v : "";
#endif
		}

		size_t AlignUp(size_t value, size_t align) { return align > 1 ? (value + align - 1) / align * align : value; }
	}

	// (GetDevice, with the GL device as the default, is in RHI/GL/DefaultDevice.cpp: T65.)
	void SetDevice(Device* device) { g_Current = device; }
	Device* CurrentDevice() { return g_Current; }

	Options OptionsFromEnvironment()
	{
		Options options;
		options.UniformCache = Env("GB_RENDER_UNIFORM_CACHE") != "0";
		options.StateFilter = Env("GB_RENDER_STATE_FILTER") != "0";
		options.StringUniforms = Env("GB_RENDER_STRING_UNIFORMS") == "1";
		return options;
	}

	// ---- Upload ring (T71) --------------------------------------------------------------------------------------------------------------

	UploadRing::UploadRing(BufferHandle buffer, std::byte* mapped, size_t regionBytes, uint32_t regions, size_t defaultAlign)
		: m_Buffer(buffer), m_Mapped(mapped), m_RegionBytes(regionBytes), m_DefaultAlign(defaultAlign > 0 ? defaultAlign : 1),
		  m_Regions(regions > 0 ? regions : 1), m_Region(m_Regions - 1)   // (the first BeginFrame moves to region 0)
	{
	}

	void UploadRing::BeginFrame()
	{
		m_Region = (m_Region + 1) % m_Regions;
		WaitRegion(m_Region);
		m_Used = 0;
		m_InFrame = true;
	}

	void UploadRing::EndFrame()
	{
		if (!m_InFrame)
			return;
		FenceRegion(m_Region);
		m_InFrame = false;
	}

	RingAllocation UploadRing::Allocate(size_t bytes, size_t align)
	{
		if (!m_InFrame || !m_Mapped || bytes == 0)
			return {};
		const size_t base = static_cast<size_t>(m_Region) * m_RegionBytes;
		const size_t at = AlignUp(base + m_Used, align > 0 ? align : m_DefaultAlign);   // (the offset in the buffer is what the bind checks)
		if (at + bytes > base + m_RegionBytes)
			return {};
		Counters::Frame.RingBytes += at + bytes - (base + m_Used);
		m_Used = at + bytes - base;
		RingAllocation a;
		a.Buffer = m_Buffer;
		a.Offset = at;
		a.Bytes = bytes;
		a.Ptr = m_Mapped + at;
		return a;
	}
}
