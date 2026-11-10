#version 450 core
in vec2 vUV;
out vec4 outLum;
layout(binding = 0) uniform sampler2D uScene;
void main()
{
	vec3 sum = vec3(0.0);
	vec2 texel = 1.0 / vec2(textureSize(uScene, 0));
	vec2 cell = vec2(1.0 / 32.0, 1.0 / 18.0);
	vec2 base = floor(vUV / cell) * cell;
	for (int y = 0; y < 4; ++y)
		for (int x = 0; x < 4; ++x)
			sum += min(texture(uScene, base + (vec2(x, y) + 0.5) / 4.0 * cell).rgb, vec3(64.0));
	outLum = vec4(sum / 16.0, 1.0);
}
