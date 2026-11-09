#include "GrassGpu.h"
#include "GB/World/Grass.h"
#include <glad/gl.h>

namespace GB
{
	GrassGpu::~GrassGpu()
	{
		if (m_Buffer)
			glDeleteBuffers(1, &m_Buffer);
	}

	uint32_t GrassGpu::GetBuffer() const
	{
		const std::vector<GrassBlade>& blades = m_Field->GetBlades();
		if (m_Revision != m_Field->GetRevision())
		{
			if (m_Buffer)
			{
				glDeleteBuffers(1, &m_Buffer);
				m_Buffer = 0;
			}
			m_Revision = m_Field->GetRevision();
		}
		if (!m_Buffer && !blades.empty())
		{
			glCreateBuffers(1, &m_Buffer);
			glNamedBufferStorage(m_Buffer, static_cast<GLsizeiptr>(blades.size() * sizeof(GrassBlade)), blades.data(), 0);
		}
		return m_Buffer;
	}
}
