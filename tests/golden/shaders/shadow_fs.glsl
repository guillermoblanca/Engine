#version 450 core
in vec2 vUV;
uniform int uAlphaMode;
uniform float uAlphaCutoff;
uniform float uOpacity;
layout(binding = 1) uniform sampler2D uAlbedoMap;
void main()
{
	if (uAlphaMode == 1 && texture(uAlbedoMap, vUV).a * uOpacity < uAlphaCutoff)
		discard;
}
