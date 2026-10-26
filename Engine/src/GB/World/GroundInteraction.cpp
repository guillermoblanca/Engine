#include "GroundInteraction.h"
#include "GB/Log.h"
#include "GB/Renderer/Framebuffer.h"
#include "GB/Renderer/Shader.h"
#include <algorithm>
#include <cmath>
#include <glad/gl.h>

namespace GB
{
	namespace
	{
		const char* kStampVS = R"GLSL(#version 450 core
layout(location = 0) in vec4 aA;   // center xz (window space, meters), half length, half width
layout(location = 1) in vec4 aB;   // angle, strength, shape
uniform float uSize;
out vec2 vCorner;
out vec2 vHalf;
flat out vec2 vParams;             // strength, shape
void main()
{
	vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
	// Capsules need room for the round caps beyond the half length; the other shapes use the quad as is.
	vec2 extent = aA.zw;
	vCorner = corner;
	vHalf = extent;
	float c = cos(aB.x), s = sin(aB.x);
	vec2 local = corner * extent;
	vec2 world = aA.xy + vec2(c * local.x - s * local.y, s * local.x + c * local.y);
	gl_Position = vec4(world / uSize * 2.0 - 1.0, 0.0, 1.0);
	vParams = aB.yz;
}
)GLSL";

		const char* kStampFS = R"GLSL(#version 450 core
in vec2 vCorner;
in vec2 vHalf;
flat in vec2 vParams;
out vec4 outValue;
void main()
{
	float shape = vParams.y;
	float a;
	if (shape < 0.5)                       // circle
	{
		a = 1.0 - smoothstep(0.35, 1.0, length(vCorner));
	}
	else if (shape < 1.5)                  // footprint: narrow heel (-x), wider ball of the foot (+x), flat bottom
	{
		float w = mix(0.62, 1.0, smoothstep(-1.0, 0.35, vCorner.x));
		vec2 q = vec2(vCorner.x, vCorner.y / w);
		float d = length(q);
		a = 1.0 - smoothstep(0.78, 1.0, d);
		// a shallow ridge between heel and ball keeps the print from being a flat oval
		a *= 0.88 + 0.12 * smoothstep(-0.2, 0.2, vCorner.x);
	}
	else                                   // capsule along x: round caps of radius = half width
	{
		float L = max(vHalf.x - vHalf.y, 0.0);
		vec2 p = vCorner * vHalf;
		vec2 q = vec2(clamp(p.x, -L, L), 0.0);
		a = 1.0 - smoothstep(0.55, 1.0, length(p - q) / max(vHalf.y, 1e-4));
	}
	outValue = vec4(a * vParams.x, 0.0, 0.0, 1.0);
}
)GLSL";

		const char* kFullscreenVS = R"GLSL(#version 450 core
void main()
{
	vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

		const char* kDecayFS = R"GLSL(#version 450 core
out vec4 outValue;
void main() { outValue = vec4(0.0); }   // the blend state does the work: dst *= constant
)GLSL";

		int Mod(int a, int n) { return ((a % n) + n) % n; }
	}

	GroundInteraction::GroundInteraction(const GroundInteractionDesc& desc) : Desc(desc)
	{
		FramebufferSpec spec;
		spec.Width = spec.Height = std::max(Desc.Resolution, 16u);
		spec.Format = ColorFormat::RGBA16F;
		spec.Depth = false;
		m_Target = std::make_unique<Framebuffer>(spec);
		// The window is a torus: lookups outside [0,1] must wrap.
		glTextureParameteri(m_Target->GetColorTexture(), GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(m_Target->GetColorTexture(), GL_TEXTURE_WRAP_T, GL_REPEAT);
		m_StampShader = std::make_unique<Shader>(kStampVS, kStampFS);
		m_DecayShader = std::make_unique<Shader>(kFullscreenVS, kDecayFS);

		glCreateBuffers(1, &m_VBO);
		glCreateVertexArrays(1, &m_VAO);
		glVertexArrayVertexBuffer(m_VAO, 0, m_VBO, 0, sizeof(Stamp4));
		glVertexArrayBindingDivisor(m_VAO, 0, 1);
		for (GLuint i = 0; i < 2; ++i)
		{
			glEnableVertexArrayAttrib(m_VAO, i);
			glVertexArrayAttribFormat(m_VAO, i, 4, GL_FLOAT, GL_FALSE, i * 16);
			glVertexArrayAttribBinding(m_VAO, i, 0);
		}
		glCreateVertexArrays(1, &m_EmptyVAO);
		Clear();
	}

	GroundInteraction::~GroundInteraction()
	{
		glDeleteBuffers(1, &m_VBO);
		glDeleteVertexArrays(1, &m_VAO);
		glDeleteVertexArrays(1, &m_EmptyVAO);
	}

	bool GroundInteraction::IsValid() const
	{
		return m_Target && m_Target->IsComplete() && m_StampShader->IsValid() && m_DecayShader->IsValid();
	}

	uint32_t GroundInteraction::GetTexture() const { return m_Target->GetColorTexture(); }

	void GroundInteraction::Clear()
	{
		const float zero[4] = { 0, 0, 0, 0 };
		glClearNamedFramebufferfv(m_Target->GetID(), GL_COLOR, 0, zero);
		m_Pending.clear();
	}

	void GroundInteraction::Stamp(const glm::vec2& center, const glm::vec2& half, float angle, float strength, StampShape shape)
	{
		m_Pending.push_back({ center, half, angle, strength, static_cast<float>(shape) });
	}

	void GroundInteraction::StampFootprint(const glm::vec3& position, const glm::vec3& heading, float length, float width, float strength)
	{
		const glm::vec2 h(heading.x, heading.z);
		const float angle = glm::length(h) > 1e-5f ? std::atan2(h.y, h.x) : 0.0f;
		Stamp({ position.x, position.z }, { length * 0.5f, width * 0.5f }, angle, strength, StampShape::Footprint);
	}

	void GroundInteraction::StampCircle(const glm::vec3& position, float radius, float strength)
	{
		Stamp({ position.x, position.z }, { radius, radius }, 0.0f, strength, StampShape::Circle);
	}

	void GroundInteraction::StampLine(const glm::vec3& from, const glm::vec3& to, float width, float strength)
	{
		const glm::vec2 a(from.x, from.z), b(to.x, to.z);
		const glm::vec2 d = b - a;
		const float len = glm::length(d);
		const float angle = len > 1e-5f ? std::atan2(d.y, d.x) : 0.0f;
		Stamp((a + b) * 0.5f, { len * 0.5f + width * 0.5f, width * 0.5f }, angle, strength, StampShape::Capsule);
	}

	void GroundInteraction::ClearColumns(int from, int count)
	{
		const int n = static_cast<int>(Desc.Resolution);
		if (count <= 0)
			return;
		const float zero[4] = { 0, 0, 0, 0 };
		if (count >= n)
		{
			glClearNamedFramebufferfv(m_Target->GetID(), GL_COLOR, 0, zero);
			return;
		}
		from = Mod(from, n);
		const int first = std::min(count, n - from);
		glEnable(GL_SCISSOR_TEST);
		glBindFramebuffer(GL_FRAMEBUFFER, m_Target->GetID());
		glScissor(from, 0, first, n);
		glClearNamedFramebufferfv(m_Target->GetID(), GL_COLOR, 0, zero);
		if (count > first)
		{
			glScissor(0, 0, count - first, n);
			glClearNamedFramebufferfv(m_Target->GetID(), GL_COLOR, 0, zero);
		}
		glDisable(GL_SCISSOR_TEST);
	}

	void GroundInteraction::ClearRows(int from, int count)
	{
		const int n = static_cast<int>(Desc.Resolution);
		if (count <= 0)
			return;
		const float zero[4] = { 0, 0, 0, 0 };
		if (count >= n)
		{
			glClearNamedFramebufferfv(m_Target->GetID(), GL_COLOR, 0, zero);
			return;
		}
		from = Mod(from, n);
		const int first = std::min(count, n - from);
		glEnable(GL_SCISSOR_TEST);
		glBindFramebuffer(GL_FRAMEBUFFER, m_Target->GetID());
		glScissor(0, from, n, first);
		glClearNamedFramebufferfv(m_Target->GetID(), GL_COLOR, 0, zero);
		if (count > first)
		{
			glScissor(0, 0, n, count - first);
			glClearNamedFramebufferfv(m_Target->GetID(), GL_COLOR, 0, zero);
		}
		glDisable(GL_SCISSOR_TEST);
	}

	void GroundInteraction::Update(float dt, const glm::vec3& center)
	{
		if (!IsValid())
			return;
		const int n = static_cast<int>(Desc.Resolution);
		const float texel = Desc.Size / static_cast<float>(n);
		const glm::ivec2 texelPos(static_cast<int>(std::floor(center.x / texel)), static_cast<int>(std::floor(center.z / texel)));

		// 1. Scroll: the texels entering the window on the far side are the ones that left on the near side.
		if (!m_HasCenter)
		{
			m_HasCenter = true;
			m_CenterTexel = texelPos;
		}
		else if (texelPos != m_CenterTexel)
		{
			const glm::ivec2 d = texelPos - m_CenterTexel;
			if (d.x > 0) ClearColumns(m_CenterTexel.x + n / 2, d.x);
			else if (d.x < 0) ClearColumns(texelPos.x - n / 2, -d.x);
			if (d.y > 0) ClearRows(m_CenterTexel.y + n / 2, d.y);
			else if (d.y < 0) ClearRows(texelPos.y - n / 2, -d.y);
			m_CenterTexel = texelPos;
		}
		m_Center = glm::vec2(static_cast<float>(m_CenterTexel.x), static_cast<float>(m_CenterTexel.y)) * texel;

		m_Target->Bind();
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
		glDisable(GL_SCISSOR_TEST);

		// 2. Recovery: dst *= exp(-rate * dt).
		if (dt > 0.0f && Desc.RecoveryPerSecond > 0.0f)
		{
			const float k = std::exp(-Desc.RecoveryPerSecond * dt);
			glEnable(GL_BLEND);
			glBlendEquation(GL_FUNC_ADD);
			glBlendColor(k, k, k, k);
			glBlendFunc(GL_ZERO, GL_CONSTANT_COLOR);
			m_DecayShader->Bind();
			glBindVertexArray(m_EmptyVAO);
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}

		// 3. Marks. A stamp near the seam of the torus is drawn again on the other side.
		if (!m_Pending.empty())
		{
			m_Instances.clear();
			const float S = Desc.Size;
			for (const PendingStamp& p : m_Pending)
			{
				// Only stamps inside the window matter.
				if (std::fabs(p.Center.x - m_Center.x) > S * 0.5f || std::fabs(p.Center.y - m_Center.y) > S * 0.5f)
					continue;
				const float radius = glm::length(p.Half) * 1.05f;
				const glm::vec2 cell = glm::floor(p.Center / S);
				const glm::vec2 local = p.Center - cell * S;   // in [0, S)
				for (int oy = -1; oy <= 1; ++oy)
					for (int ox = -1; ox <= 1; ++ox)
					{
						const glm::vec2 c = local + glm::vec2(static_cast<float>(ox), static_cast<float>(oy)) * S;
						if (c.x + radius < 0.0f || c.x - radius > S || c.y + radius < 0.0f || c.y - radius > S)
							continue;
						m_Instances.push_back({ glm::vec4(c, p.Half), glm::vec4(p.Angle, p.Strength, p.Shape, 0.0f) });
					}
			}
			if (!m_Instances.empty())
			{
				glNamedBufferData(m_VBO, static_cast<GLsizeiptr>(m_Instances.size() * sizeof(Stamp4)), m_Instances.data(), GL_STREAM_DRAW);
				glEnable(GL_BLEND);
				glBlendEquation(GL_MAX);       // overlapping marks do not dig deeper than the deepest one
				glBlendFunc(GL_ONE, GL_ONE);
				m_StampShader->Bind();
				m_StampShader->SetFloat("uSize", S);
				glBindVertexArray(m_VAO);
				glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(m_Instances.size()));
			}
			m_Pending.clear();
		}

		glBlendEquation(GL_FUNC_ADD);
		glDisable(GL_BLEND);
	}

	float GroundInteraction::ReadDepth(float x, float z) const
	{
		const int n = static_cast<int>(Desc.Resolution);
		const float u = x / Desc.Size - std::floor(x / Desc.Size);
		const float v = z / Desc.Size - std::floor(z / Desc.Size);
		const int ix = std::clamp(static_cast<int>(u * static_cast<float>(n)), 0, n - 1);
		const int iy = std::clamp(static_cast<int>(v * static_cast<float>(n)), 0, n - 1);
		float value = 0.0f;
		glGetTextureSubImage(m_Target->GetColorTexture(), 0, ix, iy, 0, 1, 1, 1, GL_RED, GL_FLOAT, sizeof(float), &value);
		return value;
	}
}
