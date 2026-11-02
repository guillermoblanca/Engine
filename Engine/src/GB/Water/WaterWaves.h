#pragma once
#include <string>

namespace GB
{
	// The GLSL constants of the water shader's wave set (kAngle, kLen, kAmp), written from GB::WaveSet (GB/World/Water.h):
	// the CPU waves (WaterSurface::HeightAt) and the GPU waves read the same numbers, so floating objects bob with the
	// water they float on. Pure text, no OpenGL.
	std::string WaveSetGlsl();

	// Replaces the line `@WAVESET@` of a shader source with WaveSetGlsl().
	std::string InjectWaveSet(std::string shaderSource);
}
