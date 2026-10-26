#pragma once
#include <cstdint>
#include <string>

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

	private:
		uint32_t m_Program = 0;
	};
}
