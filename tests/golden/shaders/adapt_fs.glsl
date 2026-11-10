#version 450 core
out vec4 outExposure;
layout(binding = 0) uniform sampler2D uLum;
layout(binding = 1) uniform sampler2D uPrev;
uniform float uKey;
uniform float uMin;
uniform float uMax;
uniform float uBlend;      // 1 - exp(-dt * speed), 1 on the first frame
void main()
{
	float logSum = 0.0;
	for (int y = 0; y < 18; ++y)
		for (int x = 0; x < 32; ++x)
		{
			vec3 c = texelFetch(uLum, ivec2(x, y), 0).rgb;
			logSum += log(max(dot(c, vec3(0.2126, 0.7152, 0.0722)), 1e-4));
		}
	float avg = exp(logSum / 576.0);
	float target = clamp(uKey / avg, uMin, uMax);
	float prev = texelFetch(uPrev, ivec2(0, 0), 0).r;
	outExposure = vec4(mix(prev, target, uBlend), 0.0, 0.0, 1.0);
}
