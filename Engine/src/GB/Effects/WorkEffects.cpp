#include "WorkEffects.h"
#include "GB/Renderer/FrameConstants.h"
#include "GB/Renderer/Scene.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		// Inside the window [from, to] of a cycle, or (from > to) outside (to, from): it wraps round the end of the cycle.
		bool InWindow(float f, const glm::vec2& w) { return w.x <= w.y ? (f >= w.x && f <= w.y) : (f >= w.x || f <= w.y); }
	}

	WorkEffects::WorkEffects(uint32_t seed) : m_Seed(seed)
	{
		m_Rng = 0x9E3779B97F4A7C15ull ^ (static_cast<uint64_t>(seed) * 0xBF58476D1CE4E5B9ull);
		Random(); Random();
	}

	float WorkEffects::Random()
	{
		m_Rng ^= m_Rng << 13; m_Rng ^= m_Rng >> 7; m_Rng ^= m_Rng << 17;
		return static_cast<float>(m_Rng >> 40) / static_cast<float>(1ull << 24);
	}

	void WorkEffects::SetGround(std::function<float(float, float)> ground)
	{
		m_Ground = std::move(ground);
		for (Layer& l : m_Systems)
			if (l.System->Desc.CollideGround) l.System->Desc.GroundHeightFn = m_Ground;
	}

	void WorkEffects::Configure(const WorkFxLook& look)
	{
		if (m_Configured && look == m_Look) return;
		m_Look = look;
		m_Configured = true;
		m_Systems.clear();
		m_Workers.clear();
		uint32_t serial = 0;
		for (const auto& [activity, fx] : m_Look.Work)
			for (size_t i = 0; i < fx.Layers.size(); ++i)
			{
				const WorkParticleLayer& l = fx.Layers[i];
				ParticleEmitterDesc d;
				d.Shape = ParticleShape::Point;
				d.Direction = { 0.0f, 1.0f, 0.0f };   // (each emission turns the system to the worker: see Emit)
				d.ConeAngleDegrees = l.Cone;
				d.Rate = 0.0f;
				d.Looping = true;
				d.MaxParticles = m_Look.MaxParticles;
				d.Seed = m_Seed * 31u + (++serial) * 7u;
				d.Lifetime = l.Life;
				d.Speed = l.Speed;
				d.GravityScale = l.Gravity;
				d.Drag = l.Drag;
				d.StartSize = l.Size;
				d.EndSizeScale = l.EndSize;
				d.StartRotationDegrees = { 0.0f, 360.0f };
				d.AngularVelocityDegrees = { -l.Spin, l.Spin };
				d.StartColor = l.Color;
				d.EndColor = l.EndColor;
				d.FadeInFraction = 0.04f;
				d.Blend = ParticleBlend::Alpha;   // (additive would glow: earth and straw do not)
				d.Sprite = l.Sprite == "smoke" ? ParticleSprite::Smoke : l.Sprite == "chunk" ? ParticleSprite::Chunk : ParticleSprite::SoftCircle;
				d.CollideGround = l.Gravity > 0.0f;
				d.GroundHeightFn = m_Ground;
				d.Bounce = l.Bounce;
				d.BounceFriction = 0.6f;
				m_Systems.push_back({ activity, i, std::make_unique<ParticleSystem>(d) });
			}
	}

	void WorkEffects::Emit(Worker& w, const std::string& activity, const glm::vec3& at, const glm::vec3& forward, float fraction, float dt)
	{
		const auto fx = m_Look.Work.find(activity);
		if (fx == m_Look.Work.end() || dt <= 0.0f) return;
		if (glm::distance(at, m_Viewer) > m_Look.Distance) return;
		if (fraction < 0.0f)
		{
			w.Clock += dt;
			fraction = std::fmod(w.Clock / std::max(fx->second.Cycle, 0.05f), 1.0f);
		}
		w.Carry.resize(fx->second.Layers.size(), 0.0f);
		glm::vec3 fwd(forward.x, 0.0f, forward.z);
		fwd = glm::length(fwd) > 1e-4f ? glm::normalize(fwd) : glm::vec3(0.0f, 0.0f, 1.0f);
		const glm::vec3 side(fwd.z, 0.0f, -fwd.x);
		for (Layer& layer : m_Systems)
		{
			if (layer.Activity != activity) continue;
			const WorkParticleLayer& l = fx->second.Layers[layer.Index];
			if (!InWindow(fraction, l.Window)) continue;
			float& carry = w.Carry[layer.Index];
			carry += l.Rate * dt;
			const int n = static_cast<int>(carry);
			if (n <= 0) continue;
			carry -= static_cast<float>(n);
			// the throw: up, leaning toward the worker's front (the system's rotation turns its +Y cone that way)
			const glm::vec3 dir = glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f) + fwd * l.Lean);
			glm::vec3 axisX = glm::cross(dir, fwd);
			axisX = glm::length(axisX) > 1e-4f ? glm::normalize(axisX) : side;
			const glm::vec3 axisZ = glm::cross(axisX, dir);
			glm::mat4 m(1.0f);
			m[0] = glm::vec4(axisX, 0.0f);
			m[1] = glm::vec4(dir, 0.0f);
			m[2] = glm::vec4(axisZ, 0.0f);
			m[3] = glm::vec4(at, 1.0f);
			layer.System->SetTransform(m);
			for (int k = 0; k < n; ++k)
			{
				const float a = 6.2831853f * Random(), r = l.Radius * std::sqrt(Random());
				const glm::vec3 p = at + fwd * l.Reach + (fwd * std::cos(a) + side * std::sin(a)) * r + glm::vec3(0.0f, l.Height, 0.0f);
				layer.System->EmitAt(p, 1);
			}
		}
	}

	void WorkEffects::Work(const std::string& who, const std::string& activity, const glm::vec3& at, const glm::vec3& forward, float fraction, float dt)
	{
		if (!m_Look.Work.count(activity)) return;
		Worker& w = m_Workers[who];
		w.Seen = true;
		Emit(w, activity, at, forward, fraction, dt);
	}

	void WorkEffects::Touch(const std::string& who, const glm::vec3& at)
	{
		if (glm::distance(at, m_Viewer) > m_Look.Distance) return;
		Pusher& p = m_Pushers[who];
		p.At = at;
		p.Seen = true;
	}

	void WorkEffects::Pretend(const std::string& activity, const glm::vec3& at, const glm::vec3& forward, float seconds)
	{
		m_Pretend.push_back({ activity, at, forward, seconds, "#pretend" + std::to_string(++m_PretendSerial) });
	}

	void WorkEffects::Update(float dt)
	{
		// the pretend workers work (they have no body to push the plants)
		for (auto it = m_Pretend.begin(); it != m_Pretend.end();)
		{
			Worker& w = m_Workers[it->Key];
			w.Seen = true;
			Emit(w, it->Activity, it->At, it->Forward, -1.0f, dt);
			it->Left -= dt;
			if (it->Left <= 0.0f) { m_Workers.erase(it->Key); it = m_Pretend.erase(it); }
			else ++it;
		}
		for (auto it = m_Workers.begin(); it != m_Workers.end();)
		{
			if (!it->second.Seen) it = m_Workers.erase(it);
			else { it->second.Seen = false; ++it; }
		}
		for (Layer& l : m_Systems)
		{
			// the look's colours dimmed by the light (read when the particles are built, so the living ones follow too)
			const WorkParticleLayer& look = m_Look.Work.at(l.Activity).Layers[l.Index];
			const float k = std::clamp(m_Light, 0.0f, 1.0f);
			l.System->Desc.StartColor = glm::vec4(glm::vec3(look.Color) * k, look.Color.a);
			l.System->Desc.EndColor = glm::vec4(glm::vec3(look.EndColor) * k, look.EndColor.a);
			l.System->Update(dt);
		}

		// the pushers: the lean follows each body Lag seconds behind (and lets go as slowly when it leaves or is gone)
		const float lag = std::max(m_Look.Push.Lag, 0.02f);
		const float follow = dt > 0.0f ? 1.0f - std::exp(-dt / lag) : 0.0f;
		for (auto it = m_Pushers.begin(); it != m_Pushers.end();)
		{
			Pusher& p = it->second;
			if (p.Fresh) { p.Lag = p.At; p.Fresh = false; }
			p.Lag += (p.At - p.Lag) * follow;
			p.Strength = p.Seen ? std::min(1.0f, p.Strength + dt / lag) : p.Strength - dt / lag;
			p.Seen = false;
			if (p.Strength <= 0.0f) it = m_Pushers.erase(it);
			else ++it;
		}
		m_Areas.swap(m_AreasNext);
		m_AreasNext.clear();
		auto inArea = [&](const glm::vec3& at)
		{
			if (m_Areas.empty()) return true;
			for (const glm::vec3& a : m_Areas)
				if (glm::distance(glm::vec2(at.x, at.z), glm::vec2(a.x, a.y)) <= a.z + m_Look.Push.Radius) return true;
			return false;
		};
		std::vector<std::pair<float, glm::vec4>> nearest;
		nearest.reserve(m_Pushers.size());
		for (const auto& [who, p] : m_Pushers)
			if (p.Strength > 0.0f && inArea(p.Lag)) nearest.push_back({ glm::distance(p.Lag, m_Viewer), glm::vec4(p.Lag.x, p.Lag.z, m_Look.Push.Radius, p.Strength) });
		std::sort(nearest.begin(), nearest.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		m_Out.clear();
		for (size_t i = 0; i < nearest.size() && i < FrameConstants::MaxPushers; ++i) m_Out.push_back(nearest[i].second);
	}

	void WorkEffects::AddToScene(Scene& scene) const
	{
		for (const Layer& l : m_Systems)
			if (l.System->GetAliveCount() > 0 && std::find(scene.Particles.begin(), scene.Particles.end(), l.System.get()) == scene.Particles.end())
				scene.Particles.push_back(l.System.get());
		scene.Pushers = m_Out;
		scene.PushBend = m_Look.Push.Bend;
		scene.PushDown = m_Look.Push.Down;
	}

	size_t WorkEffects::LiveParticles() const
	{
		size_t n = 0;
		for (const Layer& l : m_Systems) n += l.System->GetAliveCount();
		return n;
	}
}
