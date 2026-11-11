#pragma once
#include "GB/Renderer/RHI/RHI.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace GB
{
	// A program of the RHI device, with its uniforms set by name (the locations are cached). T36 turns the uniforms into constant blocks.
	// T70: the names are looked up as string views (heterogeneous lookup): a set allocates nothing, whatever the length of the name.
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
		void SetInt(std::string_view name, int value);
		void SetFloat(std::string_view name, float value);
		void SetVec2(std::string_view name, const glm::vec2& value);
		void SetVec3(std::string_view name, const glm::vec3& value);
		void SetVec4(std::string_view name, const glm::vec4& value);
		void SetVec3Array(std::string_view name, const glm::vec3* values, int count);
		void SetVec4Array(std::string_view name, const glm::vec4* values, int count);   // T115
		void SetFloatArray(std::string_view name, const float* values, int count);
		void SetMat4(std::string_view name, const glm::mat4& value);

	private:
		int Location(std::string_view name);
		RHI::Device& m_Device;
		RHI::ProgramHandle m_Program;
		RHI::PipelineHandle m_Pipeline;   // the program alone, for Bind
		struct NameHash
		{
			using is_transparent = void;
			size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
		};
		std::unordered_map<std::string, int, NameHash, std::equal_to<>> m_Locations;
	};
}
