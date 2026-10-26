// Renders a triangle offscreen and checks the pixels. Exits with 77 (CTest SKIP)
// when no OpenGL 4.5 context is available, e.g. on a machine without a display/GPU.
#include "GB/Log.h"
#include "GB/Window.h"
#include "GB/Renderer/Framebuffer.h"
#include "GB/Renderer/Shader.h"
#include <glad/gl.h>
#include <cstdio>
#include <filesystem>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	bool Near(int a, int b) { return a - b <= 2 && b - a <= 2; }
}

int main()
{
	GB::Log::Init();

	GB::WindowProps props;
	props.Visible = false;
	props.VSync = false;
	auto window = GB::Window::Create(props);
	if (!window)
	{
		std::fprintf(stderr, "SKIP: no OpenGL 4.5 context\n");
		return 77;
	}

	constexpr uint32_t W = 64, H = 64;
	GB::Framebuffer fb(W, H);
	Check(fb.IsComplete(), "framebuffer complete");

	GB::Shader shader(
		"#version 450 core\n"
		"const vec2 pos[3] = vec2[](vec2(-1,-1), vec2(3,-1), vec2(-1,3));\n"
		"void main(){ gl_Position = vec4(pos[gl_VertexID], 0, 1); }\n",
		"#version 450 core\nout vec4 c; void main(){ c = vec4(0.0, 1.0, 0.0, 1.0); }\n");
	Check(shader.IsValid(), "shader compiles and links");

	GB::Shader broken("#version 450 core\nvoid main(){ nonsense }\n", "#version 450 core\nvoid main(){}\n");
	Check(!broken.IsValid(), "invalid shader reports failure");

	GLuint vao = 0;
	glCreateVertexArrays(1, &vao);

	fb.Bind();
	glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	auto cleared = fb.ReadPixels();
	Check(Near(cleared[0], 51) && Near(cleared[1], 102) && Near(cleared[2], 153), "clear color read back");

	shader.Bind();
	glBindVertexArray(vao);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	auto pixels = fb.ReadPixels();
	const size_t center = (static_cast<size_t>(H / 2) * W + W / 2) * 4;
	Check(pixels[center] == 0 && pixels[center + 1] == 255 && pixels[center + 2] == 0, "fullscreen triangle is green");

	const std::string png = (std::filesystem::temp_directory_path() / "gb_test.png").string();
	Check(fb.SavePNG(png.c_str()), "PNG written");
	Check(std::filesystem::exists(png) && std::filesystem::file_size(png) > 0, "PNG exists");
	std::filesystem::remove(png);

	glDeleteVertexArrays(1, &vao);
	return failures;
}
