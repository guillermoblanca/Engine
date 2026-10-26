#include "GB.h"
#include <glad/gl.h>
#include <memory>

class Sandbox : public GB::Application
{
public:
	Sandbox() : GB::Application(MakeConfig())
	{
		if (!IsValid())
			return;

		m_Shader = std::make_unique<GB::Shader>(
			"#version 450 core\n"
			"const vec2 pos[3] = vec2[](vec2(-0.6,-0.6), vec2(0.6,-0.6), vec2(0.0,0.6));\n"
			"const vec3 col[3] = vec3[](vec3(1,0,0), vec3(0,1,0), vec3(0,0,1));\n"
			"out vec3 vColor;\n"
			"uniform vec2 uOffset;\n"
			"void main(){ vColor = col[gl_VertexID]; gl_Position = vec4(pos[gl_VertexID] + uOffset, 0.0, 1.0); }\n",
			"#version 450 core\n"
			"in vec3 vColor; out vec4 outColor;\n"
			"void main(){ outColor = vec4(vColor, 1.0); }\n");
		glCreateVertexArrays(1, &m_VAO);
		m_OffsetLocation = glGetUniformLocation(m_Shader->GetID(), "uOffset");
	}

	~Sandbox() override
	{
		if (m_VAO)
			glDeleteVertexArrays(1, &m_VAO);
	}

protected:
	void OnUpdate(float dt) override
	{
		using GB::Key;
		const float speed = 1.0f;

		float dx = 0.0f, dy = 0.0f;
		if (GB::Input::IsKeyDown(Key::A) || GB::Input::IsKeyDown(Key::Left))  dx -= 1.0f;
		if (GB::Input::IsKeyDown(Key::D) || GB::Input::IsKeyDown(Key::Right)) dx += 1.0f;
		if (GB::Input::IsKeyDown(Key::W) || GB::Input::IsKeyDown(Key::Up))    dy += 1.0f;
		if (GB::Input::IsKeyDown(Key::S) || GB::Input::IsKeyDown(Key::Down))  dy -= 1.0f;

		dx += GB::Input::GetGamepadAxis(GB::GamepadAxis::LeftX);
		dy -= GB::Input::GetGamepadAxis(GB::GamepadAxis::LeftY);

		m_X += dx * speed * dt;
		m_Y += dy * speed * dt;

		if (GB::Input::IsKeyDown(Key::Escape) || GB::Input::IsGamepadButtonDown(GB::GamepadButton::Back))
			Close();
	}

	void OnRender() override
	{
		GB::Framebuffer::BindDefault(GetWindow()->GetWidth(), GetWindow()->GetHeight());
		glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		m_Shader->Bind();
		glUniform2f(m_OffsetLocation, m_X, m_Y);
		glBindVertexArray(m_VAO);
		glDrawArrays(GL_TRIANGLES, 0, 3);
	}

private:
	static GB::ApplicationConfig MakeConfig()
	{
		GB::ApplicationConfig config;
		config.Title = "GB Sandbox";
		config.CreateWindow = true;
		return config;
	}

	std::unique_ptr<GB::Shader> m_Shader;
	unsigned int m_VAO = 0;
	int m_OffsetLocation = -1;
	float m_X = 0.0f, m_Y = 0.0f;
};

GB::Application* GB::CreateApplication()
{
	return new Sandbox();
}
