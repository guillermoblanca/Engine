#pragma once
#include "GB/Renderer/RHI/RHI.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace GB
{
	// A program of the RHI device, with its uniforms set by name (the locations are cached). T36 turns the uniforms into constant blocks.
	class Shader
	{
	public:
		// Compiles and links; on failure the device logs the errors and IsValid() is false.
		Shader(const std::string& vertexSource, const std::string& fragmentSource);
		~Shader();
		Shader(const Shader&) = delete;
		Shader& operator=(const Shader&) = delete;

		bool IsValid() const { return static_cast<bool>(m_Program); }
		RHI::ProgramHandle GetHandle() const { return m_Program; }
		// Binds the program alone (a pipeline that changes no other state).
		void Bind() const;
		uint32_t GetID() const { return m_Program.Id; }

		// Uniform setters do not require the program to be bound (they are recorded in the device's command list). Unknown names are ignored.
		void SetInt(const std::string& name, int value);
		void SetFloat(const std::string& name, float value);
		void SetVec2(const std::string& name, const glm::vec2& value);
		void SetVec3(const std::string& name, const glm::vec3& value);
		void SetVec4(const std::string& name, const glm::vec4& value);
		void SetVec3Array(const std::string& name, const glm::vec3* values, int count);
		void SetFloatArray(const std::string& name, const float* values, int count);
		void SetMat4(const std::string& name, const glm::mat4& value);

	private:
		int Location(const std::string& name);
		RHI::Device& m_Device;
		RHI::ProgramHandle m_Program;
		RHI::PipelineHandle m_Pipeline;   // the program alone, for Bind
		std::unordered_map<std::string, int> m_Locations;
	};
}
