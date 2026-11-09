#include "GB/World/Water/WaterWaves.h"
#include "GB/World/Water.h"
#include <cstdio>

namespace GB
{
	namespace
	{
		// "0.42", "1.0": always a decimal point, so GLSL reads a float.
		std::string Literal(float v)
		{
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
			std::string s = buf;
			if (s.find_first_of(".eE") == std::string::npos) s += ".0";
			return s;
		}

		std::string Array(const char* name, const float (&values)[WaveSet::Count])
		{
			std::string s = std::string("const float ") + name + "[" + std::to_string(WaveSet::Count) + "] = float[" + std::to_string(WaveSet::Count) + "](";
			for (int i = 0; i < WaveSet::Count; ++i) s += (i ? ", " : "") + Literal(values[i]);
			return s + ");\n";
		}
	}

	std::string WaveSetGlsl()
	{
		return Array("kAngle", WaveSet::Angle) + Array("kLen", WaveSet::Length) + Array("kAmp", WaveSet::Amp);
	}

	std::string InjectWaveSet(std::string shaderSource)
	{
		const std::string marker = "@WAVESET@\n";
		const size_t at = shaderSource.find(marker);
		if (at != std::string::npos) shaderSource.replace(at, marker.size(), WaveSetGlsl());
		return shaderSource;
	}
}
