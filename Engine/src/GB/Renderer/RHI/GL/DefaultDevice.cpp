// The default device of the render (T65): the GL one, created on the first GetDevice() with no device installed. It lives in the GL backend
// (GB_RHI_GL) so that GB_RHI, the interface and the null backend, links without GL: a program that links only GB_RHI installs its own
// device with SetDevice and never calls GetDevice.
#include "GB/Renderer/RHI/RHI.h"

namespace GB::RHI
{
	Device& GetDevice()
	{
		if (Device* current = CurrentDevice())
			return *current;
		static std::unique_ptr<Device> fallback;
		if (!fallback)
			fallback = CreateGLDevice();
		return *fallback;
	}
}
