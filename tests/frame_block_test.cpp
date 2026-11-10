// T69 (render M3): the per-frame uniform block. GB::FrameConstants (Renderer/FrameConstants.h) is written byte for byte into the block
// `lib/frame` (ShaderLib/Pieces.h) that the Sky, PBR, Shadow, Grass, Water and Particle programs read; its static_asserts pin the C++ side to
// the std140 rules. This test asks the driver where it put each member of the GLSL block, with a hidden window, and compares: the size of the
// block, its binding, and the offset (and the array stride) of every member. A member out of place would shift every value after it.
#include "GB/Log.h"
#include "GB/Platform/Window.h"
#include "GB/Renderer/FrameConstants.h"
#include "GB/Renderer/RHI/RHI.h"
#include "GB/Renderer/Shaders.h"
#include <glad/gl.h>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <string>

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
		else std::fprintf(stderr, "ok: %s\n", what.c_str());
	}
}

int GBTest_frame_block(int, char**)
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
	GB::RHI::Device& device = GB::RHI::GetDevice();

	// The block as the programs get it: through the assembler.
	const std::string vs = GB::Shaders::Assemble("#version 450 core\n#include \"lib/frame\"\nvoid main() { gl_Position = uViewProj * vec4(uCamPos, 1.0); }\n");
	const std::string fs = GB::Shaders::Assemble("#version 450 core\n#include \"lib/frame\"\nout vec4 FragColor;\n"
		"void main() { FragColor = uPointColor[uPointCount] + uPointPosRadius[uOrtho] * uTime; }\n");
	const GB::RHI::ProgramHandle program = device.CreateProgram(vs, fs);
	Check(static_cast<bool>(program), "a program with lib/frame compiles");
	if (!program)
		return 1;
	const GLuint id = program.Id;

	const GLuint block = glGetProgramResourceIndex(id, GL_UNIFORM_BLOCK, "FrameBlock");
	Check(block != GL_INVALID_INDEX, "the program has the block FrameBlock");
	if (block != GL_INVALID_INDEX)
	{
		const GLenum props2[] = { GL_BUFFER_DATA_SIZE, GL_BUFFER_BINDING };
		GLint values[2] = {};
		glGetProgramResourceiv(id, GL_UNIFORM_BLOCK, block, 2, props2, 2, nullptr, values);
		std::fprintf(stderr, "FrameBlock: %d B at binding %d (FrameConstants: %zu B at %u)\n", values[0], values[1], sizeof(GB::FrameConstants),
			GB::FrameConstants::Binding);
		Check(values[0] == static_cast<GLint>(sizeof(GB::FrameConstants)), "the block's size is sizeof(FrameConstants)");
		Check(values[1] == static_cast<GLint>(GB::FrameConstants::Binding), "the block's binding is FrameConstants::Binding");
	}

	struct Member { const char* Name; size_t Offset; };
#define GB_MEMBER(glsl, cpp) Member{ glsl, offsetof(GB::FrameConstants, cpp) }
	const Member members[] = {
		GB_MEMBER("uViewProj", ViewProj), GB_MEMBER("uInvViewProj", InvViewProj), GB_MEMBER("uLightVP", LightVP),
		GB_MEMBER("uCamPos", CamPos), GB_MEMBER("uEnvIntensity", EnvIntensity),
		GB_MEMBER("uSunDir", SunDir), GB_MEMBER("uTime", Time),
		GB_MEMBER("uSunColor", SunColor), GB_MEMBER("uShadowTexel", ShadowTexel),
		GB_MEMBER("uSkyTop", SkyTop), GB_MEMBER("uFogDensity", FogDensity),
		GB_MEMBER("uSkyHorizon", SkyHorizon), GB_MEMBER("uFogHeight", FogHeight),
		GB_MEMBER("uSkyGround", SkyGround), GB_MEMBER("uWetness", Wetness),
		GB_MEMBER("uFogColor", FogColor), GB_MEMBER("uSnowCover", SnowCover),
		GB_MEMBER("uWind", Wind), GB_MEMBER("uEnvMaxLod", EnvMaxLod),
		GB_MEMBER("uCamRight", CamRight), GB_MEMBER("uEnvRotation", EnvRotation),
		GB_MEMBER("uCamUp", CamUp), GB_MEMBER("uEnvExposure", EnvExposure),
		GB_MEMBER("uTrailRect", TrailRect),
		GB_MEMBER("uNearFar", NearFar), GB_MEMBER("uStars", Stars), GB_MEMBER("uCloudCover", CloudCover),
		GB_MEMBER("uShadowsOn", ShadowsOn), GB_MEMBER("uPointCount", PointCount), GB_MEMBER("uOrtho", Ortho), GB_MEMBER("uHasEnvMap", HasEnvMap),
		GB_MEMBER("uPointPosRadius[0]", PointPosRadius), GB_MEMBER("uPointColor[0]", PointColor),
	};
#undef GB_MEMBER
	int placed = 0;
	for (const Member& m : members)
	{
		const GLuint index = glGetProgramResourceIndex(id, GL_UNIFORM, m.Name);
		if (index == GL_INVALID_INDEX)
		{
			Check(false, std::string(m.Name) + ": in the block");
			continue;
		}
		const GLenum props3[] = { GL_OFFSET, GL_BLOCK_INDEX, GL_ARRAY_STRIDE, GL_MATRIX_STRIDE };
		GLint v[4] = {};
		glGetProgramResourceiv(id, GL_UNIFORM, index, 4, props3, 4, nullptr, v);
		const bool array = std::string(m.Name).ends_with("[0]");
		const bool ok = v[0] == static_cast<GLint>(m.Offset) && v[1] == static_cast<GLint>(block) && (!array || v[2] == 16) &&
			(std::string(m.Name).find("VP") == std::string::npos || v[3] == 16);
		if (!ok)
			Check(false, std::string(m.Name) + ": GL offset " + std::to_string(v[0]) + " (stride " + std::to_string(v[2]) + "), C++ " + std::to_string(m.Offset));
		placed += ok ? 1 : 0;
	}
	Check(placed == static_cast<int>(std::size(members)), std::to_string(placed) + " of " + std::to_string(std::size(members)) +
		" members at the offsets of FrameConstants");

	device.DestroyProgram(program);
	return failures == 0 ? 0 : 1;
}
