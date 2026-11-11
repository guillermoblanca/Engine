#version 450 core
in vec2 vUV;
out vec4 outAO;
layout(binding = 0) uniform sampler2D uAO;
layout(binding = 1) uniform sampler2D uDepth;
uniform vec2 uTexel;       // of the AO texture
uniform vec2 uNearFar;
float Lin(float d) { return uNearFar.x * uNearFar.y / (uNearFar.x + d * (uNearFar.y - uNearFar.x)); }   // reversed Z (T137)
void main()
{
	float c = Lin(texture(uDepth, vUV).r);
	float sum = 0.0, wsum = 0.0;
	for (int y = -2; y <= 1; ++y)
		for (int x = -2; x <= 1; ++x)
		{
			vec2 uv = vUV + (vec2(x, y) + 0.5) * uTexel;
			float z = Lin(texture(uDepth, uv).r);
			float w = exp(-abs(z - c) / (0.04 * c + 0.05));   // do not blur across depth edges
			sum += texture(uAO, uv).r * w; wsum += w;
		}
	outAO = vec4(sum / max(wsum, 1e-4), 0.0, 0.0, 1.0);
}
