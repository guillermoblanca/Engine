#include "Shader.h"
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <utility>

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

	int Shader::Location(std::string_view name)
	{
		if (RHI::Tuning::Current.StringUniforms)   // (T70: the lookup as before, through a temporary string, only to measure what it cost)
		{
			const std::string key(name);
			const auto it = m_Locations.find(key);
			if (it != m_Locations.end())
				return it->second;
		}
		else if (const auto it = m_Locations.find(name); it != m_Locations.end())
			return it->second;
		std::string key(name);
		const int location = m_Device.GetUniformLocation(m_Program, key);
		m_Locations.emplace(std::move(key), location);
		return location;
	}

	void Shader::SetInt(std::string_view name, int v) { m_Device.Commands().SetUniformInt(m_Program, Location(name), v); }
	void Shader::SetFloat(std::string_view name, float v) { m_Device.Commands().SetUniformFloats(m_Program, Location(name), 1, 1, &v); }
	void Shader::SetVec2(std::string_view name, const glm::vec2& v) { m_Device.Commands().SetUniformFloats(m_Program, Location(name), 2, 1, glm::value_ptr(v)); }
	void Shader::SetVec3(std::string_view name, const glm::vec3& v) { m_Device.Commands().SetUniformFloats(m_Program, Location(name), 3, 1, glm::value_ptr(v)); }
	void Shader::SetVec4(std::string_view name, const glm::vec4& v) { m_Device.Commands().SetUniformFloats(m_Program, Location(name), 4, 1, glm::value_ptr(v)); }
	void Shader::SetVec3Array(std::string_view name, const glm::vec3* v, int count) { m_Device.Commands().SetUniformFloats(m_Program, Location(name), 3, static_cast<uint32_t>(count), glm::value_ptr(*v)); }
	void Shader::SetFloatArray(std::string_view name, const float* v, int count) { m_Device.Commands().SetUniformFloats(m_Program, Location(name), 1, static_cast<uint32_t>(count), v); }
	void Shader::SetMat4(std::string_view name, const glm::mat4& v) { m_Device.Commands().SetUniformMat4(m_Program, Location(name), v); }
}
