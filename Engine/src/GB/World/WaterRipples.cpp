#include "WaterRipples.h"
#include "GB/Renderer/Framebuffer.h"
#include "GB/Renderer/Shader.h"
#include <algorithm>
#include <cmath>
#include <glad/gl.h>

namespace GB
{
	namespace
	{
		const char* kFullscreenVS = R"GLSL(#version 450 core
out vec2 vUV;
void main()
{
	vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
	vUV = p;
	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

		// One explicit step of h'' = c^2 laplacian(h) - damping * h'. State: R height, G velocity.
		const char* kStepFS = R"GLSL(#version 450 core
in vec2 vUV;
out vec4 outState;
layout(binding = 0) uniform sampler2D uState;
uniform float uDt;
uniform float uC2;            // (wave speed / texel size)^2
uniform float uDamping;
uniform vec2 uTexel;          // 1 / resolution
uniform float uEdge;          // extra damping near the window border
uniform vec2 uCenterUV;       // window center in texture space
void main()
{
	vec2 s = texture(uState, vUV).rg;
	float l = texture(uState, vUV - vec2(uTexel.x, 0.0)).r;
	float r = texture(uState, vUV + vec2(uTexel.x, 0.0)).r;
	float d = texture(uState, vUV - vec2(0.0, uTexel.y)).r;
	float u = texture(uState, vUV + vec2(0.0, uTexel.y)).r;
	float lap = l + r + d + u - 4.0 * s.r;
	// The window is a torus around its center: absorb waves where they would wrap, near the borders.
	vec2 fromCenter = abs(fract(vUV - uCenterUV + 0.5) - 0.5) * 2.0;   // 0 center .. 1 border
	float edge = smoothstep(0.78, 1.0, max(fromCenter.x, fromCenter.y));
	float v = (s.g + uC2 * lap * uDt) * (1.0 - (uDamping + uEdge * edge) * uDt);
	float h = s.r + v * uDt;
	outState = vec4(h, v, 0.0, 1.0);
}
)GLSL";

		const char* kDropVS = R"GLSL(#version 450 core
layout(location = 0) in vec4 aDrop;    // center xz (window space meters), radius, strength
uniform float uSize;
out vec2 vCorner;
flat out vec2 vParams;
void main()
{
	vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
	vCorner = corner;
	vParams = vec2(aDrop.z, aDrop.w);
	vec2 world = aDrop.xy + corner * aDrop.z * 2.5;
	gl_Position = vec4(world / uSize * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

		const char* kDropFS = R"GLSL(#version 450 core
in vec2 vCorner;
flat in vec2 vParams;
out vec4 outState;
void main()
{
	float r = length(vCorner * 2.5);    // in radii
	float g = exp(-r * r * 0.9);
	outState = vec4(vParams.y * g, 0.0, 0.0, 0.0);   // added to the height
}
)GLSL";
		int Mod(int a, int n) { return ((a % n) + n) % n; }
	}

	WaterRipples::WaterRipples(const WaterRipplesDesc& desc) : Desc(desc)
	{
		FramebufferSpec spec;
		spec.Width = spec.Height = std::max(Desc.Resolution, 16u);
		spec.Format = ColorFormat::RGBA16F;
		spec.Depth = false;
		for (auto& b : m_Buffers)
		{
			b = std::make_unique<Framebuffer>(spec);
			glTextureParameteri(b->GetColorTexture(), GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTextureParameteri(b->GetColorTexture(), GL_TEXTURE_WRAP_T, GL_REPEAT);
		}
		m_StepShader = std::make_unique<Shader>(kFullscreenVS, kStepFS);
		m_DropShader = std::make_unique<Shader>(kDropVS, kDropFS);
		glCreateVertexArrays(1, &m_EmptyVAO);
		glCreateBuffers(1, &m_VBO);
		glCreateVertexArrays(1, &m_VAO);
		glVertexArrayVertexBuffer(m_VAO, 0, m_VBO, 0, sizeof(glm::vec4));
		glVertexArrayBindingDivisor(m_VAO, 0, 1);
		glEnableVertexArrayAttrib(m_VAO, 0);
		glVertexArrayAttribFormat(m_VAO, 0, 4, GL_FLOAT, GL_FALSE, 0);
		glVertexArrayAttribBinding(m_VAO, 0, 0);
		Clear();
	}

	WaterRipples::~WaterRipples()
	{
		glDeleteVertexArrays(1, &m_EmptyVAO);
		glDeleteVertexArrays(1, &m_VAO);
		glDeleteBuffers(1, &m_VBO);
	}

	bool WaterRipples::IsValid() const
	{
		return m_Buffers[0] && m_Buffers[0]->IsComplete() && m_Buffers[1]->IsComplete() && m_StepShader->IsValid() && m_DropShader->IsValid();
	}

	uint32_t WaterRipples::GetTexture() const { return m_Buffers[m_Current]->GetColorTexture(); }

	void WaterRipples::Clear()
	{
		const float zero[4] = { 0, 0, 0, 0 };
		for (auto& b : m_Buffers)
			glClearNamedFramebufferfv(b->GetID(), GL_COLOR, 0, zero);
		m_Pending.clear();
	}

	void WaterRipples::Impulse(const glm::vec3& position, float radius, float strength)
	{
		m_Pending.push_back({ { position.x, position.z }, radius, strength });
	}

	void WaterRipples::ClearStrips(const glm::ivec2& from, const glm::ivec2& count)
	{
		const int n = static_cast<int>(Desc.Resolution);
		const float zero[4] = { 0, 0, 0, 0 };
		glEnable(GL_SCISSOR_TEST);
		for (auto& b : m_Buffers)
		{
			glBindFramebuffer(GL_FRAMEBUFFER, b->GetID());
			auto clear = [&](int x, int y, int w, int h)
			{
				glScissor(x, y, w, h);
				glClearNamedFramebufferfv(b->GetID(), GL_COLOR, 0, zero);
			};
			if (count.x > 0)
			{
				const int x0 = Mod(from.x, n), first = std::min(count.x, n - x0);
				clear(x0, 0, first, n);
				if (count.x > first) clear(0, 0, count.x - first, n);
			}
			if (count.y > 0)
			{
				const int y0 = Mod(from.y, n), first = std::min(count.y, n - y0);
				clear(0, y0, n, first);
				if (count.y > first) clear(0, 0, n, count.y - first);
			}
		}
		glDisable(GL_SCISSOR_TEST);
	}

	void WaterRipples::Step(float dt)
	{
		const int next = 1 - m_Current;
		m_Buffers[next]->Bind();
		glDisable(GL_BLEND);
		glDisable(GL_DEPTH_TEST);
		glBindTextureUnit(0, m_Buffers[m_Current]->GetColorTexture());
		const float n = static_cast<float>(Desc.Resolution);
		const float texel = Desc.Size / n;
		m_StepShader->Bind();
		m_StepShader->SetFloat("uDt", dt);
		m_StepShader->SetFloat("uC2", Desc.WaveSpeed * Desc.WaveSpeed / (texel * texel));
		m_StepShader->SetFloat("uDamping", Desc.Damping);
		m_StepShader->SetVec2("uTexel", { 1.0f / n, 1.0f / n });
		m_StepShader->SetFloat("uEdge", 8.0f);
		m_StepShader->SetVec2("uCenterUV", { m_Center.x / Desc.Size - std::floor(m_Center.x / Desc.Size), m_Center.y / Desc.Size - std::floor(m_Center.y / Desc.Size) });
		glBindVertexArray(m_EmptyVAO);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		m_Current = next;
	}

	void WaterRipples::Update(float dt, const glm::vec3& center)
	{
		if (!IsValid())
			return;
		const int n = static_cast<int>(Desc.Resolution);
		const float texel = Desc.Size / static_cast<float>(n);
		const glm::ivec2 texelPos(static_cast<int>(std::floor(center.x / texel)), static_cast<int>(std::floor(center.z / texel)));
		if (!m_HasCenter)
		{
			m_HasCenter = true;
			m_CenterTexel = texelPos;
		}
		else if (texelPos != m_CenterTexel)
		{
			const glm::ivec2 d = texelPos - m_CenterTexel;
			glm::ivec2 from(0), count(0);
			if (d.x > 0) { from.x = m_CenterTexel.x + n / 2; count.x = d.x; } else if (d.x < 0) { from.x = texelPos.x - n / 2; count.x = -d.x; }
			if (d.y > 0) { from.y = m_CenterTexel.y + n / 2; count.y = d.y; } else if (d.y < 0) { from.y = texelPos.y - n / 2; count.y = -d.y; }
			if (std::abs(d.x) >= n || std::abs(d.y) >= n)
			{
				Clear();
			}
			else
				ClearStrips(from, count);
			m_CenterTexel = texelPos;
		}
		m_Center = glm::vec2(static_cast<float>(m_CenterTexel.x), static_cast<float>(m_CenterTexel.y)) * texel;

		// Drops are added to the current state.
		if (!m_Pending.empty())
		{
			std::vector<glm::vec4> drops;
			const float S = Desc.Size;
			for (const Drop& d : m_Pending)
			{
				if (std::fabs(d.Center.x - m_Center.x) > S * 0.4f || std::fabs(d.Center.y - m_Center.y) > S * 0.4f)
					continue;
				const glm::vec2 cell = glm::floor(d.Center / S);
				const glm::vec2 local = d.Center - cell * S;
				const float reach = d.Radius * 2.5f;
				for (int oy = -1; oy <= 1; ++oy)
					for (int ox = -1; ox <= 1; ++ox)
					{
						const glm::vec2 c = local + glm::vec2(static_cast<float>(ox), static_cast<float>(oy)) * S;
						if (c.x + reach < 0.0f || c.x - reach > S || c.y + reach < 0.0f || c.y - reach > S)
							continue;
						drops.push_back({ c.x, c.y, d.Radius, d.Strength });
					}
			}
			if (!drops.empty())
			{
				m_Buffers[m_Current]->Bind();
				glDisable(GL_DEPTH_TEST);
				glNamedBufferData(m_VBO, static_cast<GLsizeiptr>(drops.size() * sizeof(glm::vec4)), drops.data(), GL_STREAM_DRAW);
				glEnable(GL_BLEND);
				glBlendEquation(GL_FUNC_ADD);
				glBlendFunc(GL_ONE, GL_ONE);
				m_DropShader->Bind();
				m_DropShader->SetFloat("uSize", S);
				glBindVertexArray(m_VAO);
				glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, static_cast<GLsizei>(drops.size()));
				glDisable(GL_BLEND);
			}
			m_Pending.clear();
		}

		// Fixed steps keep the explicit scheme stable whatever the frame time.
		const float maxStep = 0.5f * texel / std::max(Desc.WaveSpeed, 1e-3f);   // CFL: c dt / dx <= 0.5
		m_Accumulator += std::min(dt, 0.1f);
		const float stepDt = std::min(1.0f / 120.0f, maxStep);
		int steps = 0;
		while (m_Accumulator >= stepDt && steps < 16)
		{
			Step(stepDt);
			m_Accumulator -= stepDt;
			++steps;
		}
		if (steps == 16)
			m_Accumulator = 0.0f;
	}

	float WaterRipples::ReadHeight(float x, float z) const
	{
		const int n = static_cast<int>(Desc.Resolution);
		const float u = x / Desc.Size - std::floor(x / Desc.Size), v = z / Desc.Size - std::floor(z / Desc.Size);
		const int ix = std::clamp(static_cast<int>(u * static_cast<float>(n)), 0, n - 1);
		const int iy = std::clamp(static_cast<int>(v * static_cast<float>(n)), 0, n - 1);
		float value = 0.0f;
		glGetTextureSubImage(m_Buffers[m_Current]->GetColorTexture(), 0, ix, iy, 0, 1, 1, 1, GL_RED, GL_FLOAT, sizeof(float), &value);
		return value;
	}
}
