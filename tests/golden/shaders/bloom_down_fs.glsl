#version 450 core
in vec2 vUV;
out vec4 outColor;
layout(binding = 0) uniform sampler2D uSrc;
uniform vec2 uTexel;       // 1 / source size
uniform int uPrefilter;
uniform float uThreshold;
uniform float uKnee;

vec3 Prefilter(vec3 c)
{
	if (uPrefilter == 0) return c;
	c = clamp(c, vec3(0.0), vec3(32.0));
	float brightness = max(c.r, max(c.g, c.b));
	float soft = clamp(brightness - uThreshold + uKnee, 0.0, 2.0 * uKnee);
	soft = soft * soft / (4.0 * uKnee + 1e-4);
	float contribution = max(soft, brightness - uThreshold) / max(brightness, 1e-4);
	return c * contribution;
}

vec3 T(vec2 o) { return Prefilter(texture(uSrc, vUV + o * uTexel).rgb); }

void main()
{
	vec3 a = T(vec2(-2, 2)), b = T(vec2(0, 2)), c = T(vec2(2, 2));
	vec3 d = T(vec2(-2, 0)), e = T(vec2(0, 0)), f = T(vec2(2, 0));
	vec3 g = T(vec2(-2, -2)), h = T(vec2(0, -2)), i = T(vec2(2, -2));
	vec3 j = T(vec2(-1, 1)), k = T(vec2(1, 1)), l = T(vec2(-1, -1)), m = T(vec2(1, -1));
	vec3 col = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
	outColor = vec4(col, 1.0);
}
