#include "GrassGpu.h"
#include "GB/World/Grass.h"

namespace GB
{
	GrassGpu::~GrassGpu()
	{
		RHI::GetDevice().DestroyBuffer(m_Buffer);
	}

	RHI::BufferHandle GrassGpu::GetHandle() const
	{
		const std::vector<GrassBlade>& blades = m_Field->GetBlades();
		if (m_Revision != m_Field->GetRevision())
		{
			RHI::GetDevice().DestroyBuffer(m_Buffer);
			m_Buffer = {};
			m_Revision = m_Field->GetRevision();
		}
		if (!m_Buffer && !blades.empty())
			m_Buffer = RHI::GetDevice().CreateBuffer(blades.size() * sizeof(GrassBlade), blades.data(), false);
		return m_Buffer;
	}
}
