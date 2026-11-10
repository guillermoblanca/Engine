#version 450 core
in vec2 vUV;
out vec4 outColor;
layout(binding = 0) uniform sampler2D uSrc;
uniform vec2 uTexel;       // 1 / source size
uniform float uRadius;

void main()
{
	vec2 o = uTexel * uRadius;
	vec3 col = texture(uSrc, vUV + vec2(-o.x, o.y)).rgb + texture(uSrc, vUV + vec2(0, o.y)).rgb * 2.0 + texture(uSrc, vUV + vec2(o.x, o.y)).rgb;
	col += texture(uSrc, vUV + vec2(-o.x, 0)).rgb * 2.0 + texture(uSrc, vUV).rgb * 4.0 + texture(uSrc, vUV + vec2(o.x, 0)).rgb * 2.0;
	col += texture(uSrc, vUV + vec2(-o.x, -o.y)).rgb + texture(uSrc, vUV + vec2(0, -o.y)).rgb * 2.0 + texture(uSrc, vUV + vec2(o.x, -o.y)).rgb;
	outColor = vec4(col / 16.0, 1.0);
}
