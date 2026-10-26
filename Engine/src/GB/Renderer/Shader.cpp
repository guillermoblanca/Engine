#include "Shader.h"
#include "GB/Log.h"
#include <glad/gl.h>
#include <vector>

namespace GB
{
	static GLuint Compile(GLenum type, const std::string& source)
	{
		const GLuint shader = glCreateShader(type);
		const char* src = source.c_str();
		glShaderSource(shader, 1, &src, nullptr);
		glCompileShader(shader);

		GLint ok = GL_FALSE;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
		if (!ok)
		{
			GLint len = 0;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
			std::vector<char> log(len > 0 ? len : 1);
			glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
			GB_CORE_ERROR("Shader compile error:\n{}", log.data());
			glDeleteShader(shader);
			return 0;
		}
		return shader;
	}

	Shader::Shader(const std::string& vertexSource, const std::string& fragmentSource)
	{
		const GLuint vs = Compile(GL_VERTEX_SHADER, vertexSource);
		const GLuint fs = Compile(GL_FRAGMENT_SHADER, fragmentSource);
		if (!vs || !fs)
		{
			glDeleteShader(vs);
			glDeleteShader(fs);
			return;
		}

		const GLuint program = glCreateProgram();
		glAttachShader(program, vs);
		glAttachShader(program, fs);
		glLinkProgram(program);
		glDeleteShader(vs);
		glDeleteShader(fs);

		GLint ok = GL_FALSE;
		glGetProgramiv(program, GL_LINK_STATUS, &ok);
		if (!ok)
		{
			GLint len = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
			std::vector<char> log(len > 0 ? len : 1);
			glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
			GB_CORE_ERROR("Shader link error:\n{}", log.data());
			glDeleteProgram(program);
			return;
		}
		m_Program = program;
	}

	Shader::~Shader()
	{
		if (m_Program)
			glDeleteProgram(m_Program);
	}

	void Shader::Bind() const { glUseProgram(m_Program); }
}
