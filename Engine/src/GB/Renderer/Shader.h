#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace GB
{
	class Shader
	{
	public:
		// Compiles and links; on failure logs the GL info log and IsValid() is false.
		Shader(const std::string& vertexSource, const std::string& fragmentSource);
		~Shader();
		Shader(const Shader&) = delete;
		Shader& operator=(const Shader&) = delete;

		bool IsValid() const { return m_Program != 0; }
		void Bind() const;
		uint32_t GetID() const { return m_Program; }

		// Uniform setters do not require the program to be bound. Unknown names are ignored.
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
		uint32_t m_Program = 0;
		std::unordered_map<std::string, int> m_Locations;
	};
}
