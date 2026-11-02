#include "Window.h"
#include "Log.h"
#include <glad/gl.h>
#include <GLFW/glfw3.h>

namespace GB
{
	namespace
	{
		void GLFWErrorCallback(int error, const char* description)
		{
			GB_CORE_ERROR("GLFW error {}: {}", error, description);
		}
	}

	std::shared_ptr<GlfwContext> GlfwContext::Create()
	{
		glfwSetErrorCallback(GLFWErrorCallback);
		if (!glfwInit())
		{
			GB_CORE_ERROR("glfwInit failed");
			return nullptr;
		}
		return std::shared_ptr<GlfwContext>(new GlfwContext());
	}

	GlfwContext::~GlfwContext() { glfwTerminate(); }

	std::unique_ptr<Window> Window::Create(const WindowProps& props, std::shared_ptr<GlfwContext> glfw)
	{
		if (!glfw)
			glfw = GlfwContext::Create();
		if (!glfw)
			return nullptr;

		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
		glfwWindowHint(GLFW_VISIBLE, props.Visible ? GLFW_TRUE : GLFW_FALSE);
#ifdef GB_DEBUG_GL
		glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

		std::unique_ptr<Window> window(new Window());
		window->m_Glfw = std::move(glfw);   // from here on ~Window releases the library reference
		window->m_Width = props.Width;
		window->m_Height = props.Height;
		window->m_Handle = glfwCreateWindow(static_cast<int>(props.Width), static_cast<int>(props.Height),
			props.Title.c_str(), nullptr, nullptr);
		if (!window->m_Handle)
		{
			GB_CORE_ERROR("Could not create a {}x{} OpenGL 4.5 window", props.Width, props.Height);
			return nullptr;
		}

		glfwMakeContextCurrent(window->m_Handle);
		if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress)))
		{
			GB_CORE_ERROR("Failed to load OpenGL functions");
			return nullptr;
		}
		glfwSwapInterval(props.VSync ? 1 : 0);
		window->m_Input.Bind(window->m_Handle);
		// The scroll wheel reaches this window's own input through the GLFW user pointer (no global to find it by).
		glfwSetWindowUserPointer(window->m_Handle, window.get());
		glfwSetScrollCallback(window->m_Handle, [](GLFWwindow* handle, double, double yOffset)
		{
			if (Window* w = static_cast<Window*>(glfwGetWindowUserPointer(handle)))
				w->m_Input.AddScroll(static_cast<float>(yOffset));
		});

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
			m_Input.Bind(nullptr);
			glfwDestroyWindow(m_Handle);
		}
		// m_Glfw is released after this body: glfwTerminate runs when the last window of the context is gone.
	}

	void Window::PollEvents()
	{
		m_Input.NewFrame();   // the scroll accumulator starts over, then the events add to it
		glfwPollEvents();
		m_Input.PostPoll();
		// Follow the real size of the framebuffer (the user resizing the window). A minimised or hidden window may
		// report 0: keep the last good size then.
		int w = 0, h = 0;
		glfwGetFramebufferSize(m_Handle, &w, &h);
		if (w > 0 && h > 0)
		{
			m_Width = static_cast<uint32_t>(w);
			m_Height = static_cast<uint32_t>(h);
		}
	}
	void Window::SwapBuffers() { glfwSwapBuffers(m_Handle); }
	bool Window::ShouldClose() const { return glfwWindowShouldClose(m_Handle) != 0; }
	void Window::RequestClose() { glfwSetWindowShouldClose(m_Handle, GLFW_TRUE); }
}
