#version 450 core
layout(location = 0) in vec4 aPosSize;     // xyz position, w size
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec4 aVelRot;      // xyz velocity, w rotation
layout(location = 3) in vec4 aFrame;       // x = flipbook frame
uniform mat4 uViewProj;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform float uStretch;
uniform vec2 uSheet;                       // columns, rows
out vec2 vUV;
out vec4 vColor;
out vec3 vWorld;
void main()
{
	// Triangle strip corners: (-1,-1) (1,-1) (-1,1) (1,1)
	vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
	float size = aPosSize.w;
	vec2 offset;
	vec2 v = vec2(dot(aVelRot.xyz, uCamRight), dot(aVelRot.xyz, uCamUp));
	float speed = length(v);
	if (uStretch > 0.0 && speed > 1e-4)
	{
		vec2 dir = v / speed;
		vec2 perp = vec2(-dir.y, dir.x);
		offset = dir * corner.x * size * (1.0 + uStretch * speed) + perp * corner.y * size;
	}
	else
	{
		float c = cos(aVelRot.w), s = sin(aVelRot.w);
		offset = vec2(c * corner.x - s * corner.y, s * corner.x + c * corner.y) * size;
	}
	vec3 world = aPosSize.xyz + uCamRight * offset.x + uCamUp * offset.y;
	gl_Position = uViewProj * vec4(world, 1.0);
	vWorld = world;

	vec2 uv = vec2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5);   // top-left origin
	float frame = aFrame.x;
	float col = mod(frame, uSheet.x);
	float row = floor(frame / uSheet.x);
	vUV = (vec2(col, row) + uv) / uSheet;
	vColor = aColor;
}
