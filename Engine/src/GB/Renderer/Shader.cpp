#include "Shader.h"
#include <glm/gtc/type_ptr.hpp>

namespace GB
{
	Shader::Shader(const std::string& vertexSource, const std::string& fragmentSource)
		: m_Device(RHI::GetDevice()), m_Program(m_Device.CreateProgram(vertexSource, fragmentSource))
	{
		if (m_Program)
		{
			RHI::PipelineDesc desc;
			desc.Program = m_Program;
			m_Pipeline = m_Device.CreatePipeline(desc);
		}
	}

	Shader::~Shader()
	{
		m_Device.DestroyPipeline(m_Pipeline);
		m_Device.DestroyProgram(m_Program);
	}

	void Shader::Bind() const { m_Device.Commands().BindPipeline(m_Pipeline); }

	int Shader::Location(const std::string& name)
	{
		auto it = m_Locations.find(name);
		if (it != m_Locations.end())
			return it->second;
		const int location = m_Device.GetUniformLocation(m_Program, name);
		m_Locations.emplace(name, location);
		return location;
	}

	void Shader::SetInt(const std::string& name, int v) { m_Device.Commands().SetUniformInt(m_Program, Location(name), v); }
	void Shader::SetFloat(const std::string& name, float v) { m_Device.Commands().SetUniformFloats(m_Program, Location(name), 1, 1, &v); }
	void Shader::SetVec2(const std::string& name, const glm::vec2& v) { m_Device.Commands().SetUniformFloats(m_Program, Location(name), 2, 1, glm::value_ptr(v)); }
	void Shader::SetVec3(const std::string& name, const glm::vec3& v) { m_Device.Commands().SetUniformFloats(m_Program, Location(name), 3, 1, glm::value_ptr(v)); }
	void Shader::SetVec4(const std::string& name, const glm::vec4& v) { m_Device.Commands().SetUniformFloats(m_Program, Location(name), 4, 1, glm::value_ptr(v)); }
	void Shader::SetVec3Array(const std::string& name, const glm::vec3* v, int count) { m_Device.Commands().SetUniformFloats(m_Program, Location(name), 3, static_cast<uint32_t>(count), glm::value_ptr(*v)); }
	void Shader::SetFloatArray(const std::string& name, const float* v, int count) { m_Device.Commands().SetUniformFloats(m_Program, Location(name), 1, static_cast<uint32_t>(count), v); }
	void Shader::SetMat4(const std::string& name, const glm::mat4& v) { m_Device.Commands().SetUniformMat4(m_Program, Location(name), v); }
}
