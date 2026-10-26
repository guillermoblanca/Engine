#include "Window.h"
#include "Input.h"
#include "Log.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>

namespace GB
{
	static int s_GLFWRefs = 0;

	static void GLFWErrorCallback(int error, const char* description)
	{
		GB_CORE_ERROR("GLFW error {}: {}", error, description);
	}

	std::unique_ptr<Window> Window::Create(const WindowProps& props)
	{
		if (s_GLFWRefs == 0)
		{
			glfwSetErrorCallback(GLFWErrorCallback);
			if (!glfwInit())
			{
				GB_CORE_ERROR("glfwInit failed");
				return nullptr;
			}
		}
		++s_GLFWRefs;

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
		glfwWindowHint(GLFW_VISIBLE, props.Visible ? GLFW_TRUE : GLFW_FALSE);
#ifdef GB_DEBUG_GL
		glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

		std::unique_ptr<Window> window(new Window());
		window->m_Width = props.Width;
		window->m_Height = props.Height;
		window->m_Handle = glfwCreateWindow(static_cast<int>(props.Width), static_cast<int>(props.Height),
			props.Title.c_str(), nullptr, nullptr);
		if (!window->m_Handle)
		{
			GB_CORE_ERROR("Could not create a {}x{} OpenGL 4.5 window", props.Width, props.Height);
			return nullptr; // ~Window releases the GLFW reference
		}

		glfwMakeContextCurrent(window->m_Handle);
		if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress)))
		{
			GB_CORE_ERROR("Failed to load OpenGL functions");
			return nullptr;
		}
		glfwSwapInterval(props.VSync ? 1 : 0);
		Input::Bind(window->m_Handle);

		GB_CORE_INFO("OpenGL {} | {} | {}",
			reinterpret_cast<const char*>(glGetString(GL_VERSION)),
			reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
			reinterpret_cast<const char*>(glGetString(GL_VENDOR)));
		return window;
	}

	Window::~Window()
	{
		if (m_Handle)
		{
			Input::Bind(nullptr);
			glfwDestroyWindow(m_Handle);
		}
		if (--s_GLFWRefs == 0)
			glfwTerminate();
	}

	void Window::PollEvents() { glfwPollEvents(); }
	void Window::SwapBuffers() { glfwSwapBuffers(m_Handle); }
	bool Window::ShouldClose() const { return glfwWindowShouldClose(m_Handle) != 0; }
	void Window::RequestClose() { glfwSetWindowShouldClose(m_Handle, GLFW_TRUE); }
}
