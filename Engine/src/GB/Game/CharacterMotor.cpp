#include "GB/Game/CharacterMotor.h"
#include <algorithm>
#include <cmath>

namespace GB
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;

		glm::vec2 MoveToward(glm::vec2 current, glm::vec2 target, float maxDelta)
		{
			const glm::vec2 d = target - current;
			const float len = glm::length(d);
			return len <= maxDelta || len < 1e-6f ? target : current + d * (maxDelta / len);
		}

		float AngleDelta(float from, float to)
		{
			float d = to - from;
			while (d > kPi) d -= 2.0f * kPi;
			while (d < -kPi) d += 2.0f * kPi;
			return d;
		}

		bool IsYawOnly(const glm::quat& q)
		{
			const glm::vec3 up = q * glm::vec3(0, 1, 0);
			return up.y > 0.9995f;
		}

		// Pushes a sphere out of a static collider; returns the correction (zero when apart).
		glm::vec3 PushOut(const StaticCollider& s, const glm::vec3& c, float r)
		{
			switch (s.Shape)
			{
			case ShapeType::Sphere:
			{
				const glm::vec3 d = c - s.Center;
				const float len = glm::length(d), reach = r + s.Radius;
				if (len >= reach) return glm::vec3(0.0f);
				return (len > 1e-5f ? d / len : glm::vec3(1, 0, 0)) * (reach - len);
			}
			case ShapeType::Capsule:
			{
				const float y = std::clamp(c.y, s.Center.y - s.HalfHeight, s.Center.y + s.HalfHeight);
				const glm::vec3 d = c - glm::vec3(s.Center.x, y, s.Center.z);
				const float len = glm::length(d), reach = r + s.Radius;
				if (len >= reach) return glm::vec3(0.0f);
				return (len > 1e-5f ? d / len : glm::vec3(1, 0, 0)) * (reach - len);
			}
			case ShapeType::Box:
			{
				const glm::quat inv = glm::inverse(s.Rotation);
				const glm::vec3 local = inv * (c - s.Center);
				const glm::vec3 closest = glm::clamp(local, -s.HalfExtents, s.HalfExtents);
				const glm::vec3 d = local - closest;
				const float len = glm::length(d);
				if (len > 1e-6f)
				{
					if (len >= r) return glm::vec3(0.0f);
					return (s.Rotation * (d / len)) * (r - len);
				}
				// the center is inside the box: leave through the nearest face
				const glm::vec3 gap = s.HalfExtents - glm::abs(local);
				int axis = 0;
				if (gap.y < gap[axis]) axis = 1;
				if (gap.z < gap[axis]) axis = 2;
				glm::vec3 n(0.0f);
				n[axis] = local[axis] >= 0.0f ? 1.0f : -1.0f;
				return (s.Rotation * n) * (gap[axis] + r);
			}
			}
			return glm::vec3(0.0f);
		}
	}

	void CharacterMotor::SetPosition(const glm::vec3& feet, float yaw)
	{
		m_State = MotionState();
		m_State.Position = feet;
		m_State.Yaw = yaw;
		m_PrevLocalVelocity = glm::vec2(0.0f);
		m_SinceGrounded = 1.0f;
		m_Started = false;
	}

	float CharacterMotor::SupportHeight(const glm::vec2& xz, float maxY, const PhysicsWorld& world, glm::vec3& normal) const
	{
		// five probes around the footprint: the character stands on the highest thing under any of them
		const float r = m_Config.Radius * 0.7f;
		const glm::vec2 probes[5] = { { 0, 0 }, { r, 0 }, { -r, 0 }, { 0, r }, { 0, -r } };
		float best = -1e9f;
		bool fromTerrain = false;
		normal = glm::vec3(0, 1, 0);
		for (const glm::vec2& o : probes)
		{
			const glm::vec2 p = xz + o;
			const float terrain = world.GroundHeight(p.x, p.y);
			if (terrain <= maxY && terrain > best) { best = terrain; fromTerrain = true; }
			for (const StaticCollider& s : world.GetStatics())
			{
				if (s.Shape != ShapeType::Box || !IsYawOnly(s.Rotation)) continue;   // only level boxes are walkable on top
				const glm::vec3 local = glm::inverse(s.Rotation) * (glm::vec3(p.x, s.Center.y, p.y) - s.Center);
				if (std::fabs(local.x) > s.HalfExtents.x || std::fabs(local.z) > s.HalfExtents.z) continue;
				const float top = s.Center.y + s.HalfExtents.y;
				if (top <= maxY && top > best) { best = top; fromTerrain = false; }
			}
		}
		if (fromTerrain) normal = world.GroundNormalAt(xz.x, xz.y);
		return best;
	}

	glm::vec2 CharacterMotor::ResolveWalls(const glm::vec3& feet, const glm::vec2& wanted, const PhysicsWorld& world) const
	{
		// three spheres from the step height up to the head
		const float r = m_Config.Radius;
		const float low = feet.y + m_Config.StepHeight + r, high = feet.y + m_Config.Height - r;
		glm::vec2 xz = wanted;
		for (int iteration = 0; iteration < 4; ++iteration)
		{
			bool moved = false;
			for (int k = 0; k < 3; ++k)
			{
				const float y = low + (high - low) * static_cast<float>(k) * 0.5f;
				for (const StaticCollider& s : world.GetStatics())
				{
					const glm::vec3 push = PushOut(s, glm::vec3(xz.x, y, xz.y), r);
					if (push.x != 0.0f || push.z != 0.0f)
					{
						xz += glm::vec2(push.x, push.z);
						moved = true;
					}
				}
			}
			if (!moved) break;
		}
		return xz;
	}

	void CharacterMotor::Step(const MoveInput& in, float dt, const PhysicsWorld& world)
	{
		if (dt <= 0.0f) return;
		MotionState& s = m_State;
		const CharacterConfig& c = m_Config;
		s.Jumped = s.Landed = false;
		s.LandingSpeed = 0.0f;
		const bool wasGrounded = s.Grounded;

		if (!m_Started)
		{
			// first step: settle on whatever is below
			glm::vec3 n;
			const float h = SupportHeight({ s.Position.x, s.Position.z }, s.Position.y + c.StepHeight, world, n);
			if (h > -1e8f && h >= s.Position.y - c.StepHeight) { s.Position.y = h; s.Grounded = true; s.GroundNormal = n; }   // placed in the air: it falls
			m_Started = true;
		}

		// slope of the ground under the feet
		const float slope = glm::degrees(std::acos(std::clamp(s.GroundNormal.y, -1.0f, 1.0f)));
		const bool steep = s.Sliding || (s.Grounded && slope > c.MaxSlopeDegrees);

		// horizontal velocity: follow the wish with the ground's (or the air's) acceleration
		glm::vec2 wish = in.Velocity;
		if (s.WaterDepth > c.WadeDepth) wish *= c.WadeSpeedFactor;
		if (steep)
		{
			// no walking up the slope: drop the part of the wish that goes uphill
			const glm::vec2 downhill = glm::normalize(glm::vec2(s.GroundNormal.x, s.GroundNormal.z) + glm::vec2(1e-6f));
			const float uphill = glm::dot(wish, -downhill);
			if (uphill > 0.0f) wish += downhill * uphill;
		}
		glm::vec2 v(s.Velocity.x, s.Velocity.z);
		const bool speedingUp = glm::length(wish) > glm::length(v) - 0.01f;
		float accel = speedingUp ? c.Acceleration : c.Deceleration;
		if (!s.Grounded) accel *= c.AirControl;
		v = MoveToward(v, wish, accel * dt);
		if (steep)
		{
			// slide down: gravity along the slope
			const glm::vec2 downhill = glm::normalize(glm::vec2(s.GroundNormal.x, s.GroundNormal.z) + glm::vec2(1e-6f));
			v += downhill * (c.Gravity * std::sin(glm::radians(slope)) * dt);
		}

		// vertical: jump and gravity
		float vy = s.Velocity.y;
		m_SinceGrounded = s.Grounded ? 0.0f : m_SinceGrounded + dt;
		if (in.Jump && (s.Grounded && !steep ? true : m_SinceGrounded < c.CoyoteTime) && vy <= 0.01f)
		{
			vy = std::sqrt(2.0f * c.Gravity * c.JumpHeight) - c.Gravity * dt;   // gravity already acts this step
			s.Grounded = false;
			s.Jumped = true;
			m_SinceGrounded = c.CoyoteTime;
		}
		else if (!s.Grounded || steep)
			vy = std::max(vy - c.Gravity * dt, -c.MaxFallSpeed);
		else
			vy = 0.0f;

		// move horizontally, then stop at walls and ledges that are too high
		glm::vec2 xz(s.Position.x, s.Position.z);
		const glm::vec2 wanted = xz + v * dt;
		glm::vec2 next = ResolveWalls(s.Position, wanted, world);
		if (glm::distance(next, wanted) > 1e-5f) v = (next - xz) / dt;   // a wall took some of the move: that is the speed it really has
		glm::vec3 normal(0, 1, 0);
		float support = SupportHeight(next, s.Position.y + (s.Grounded ? c.StepHeight : 0.05f), world, normal);
		if (support < -1e8f)
		{
			// the ground just ahead is higher than a step (or a cliff face): try each axis on its own, then stay
			const glm::vec2 tryX(next.x, xz.y), tryZ(xz.x, next.y);
			const float sx = SupportHeight(tryX, s.Position.y + c.StepHeight, world, normal);
			const float sz = SupportHeight(tryZ, s.Position.y + c.StepHeight, world, normal);
			if (sx > -1e8f && (sz < -1e8f || std::fabs(next.x - xz.x) >= std::fabs(next.y - xz.y))) { next = tryX; support = sx; }
			else if (sz > -1e8f) { next = tryZ; support = sz; }
			else { next = xz; support = SupportHeight(xz, s.Position.y + c.StepHeight, world, normal); }
			v = (next - xz) / dt;
		}
		else if (s.Grounded && !steep)
		{
			// terrain too steep to climb: ground that rises faster than the slope limit allows blocks the move
			const float rise = support - s.Position.y;
			const float run = glm::length(next - xz);
			if (rise > c.StepHeight * 0.5f && run > 1e-5f && glm::degrees(std::atan2(rise, run)) > c.MaxSlopeDegrees && glm::degrees(std::acos(std::clamp(normal.y, -1.0f, 1.0f))) > c.MaxSlopeDegrees)
			{
				next = xz;
				support = SupportHeight(xz, s.Position.y + c.StepHeight, world, normal);
				v = glm::vec2(0.0f);
			}
		}

		// vertical resolution against the support
		float y = s.Position.y;
		bool grounded = false;
		if (support < -1e8f) support = world.GroundHeight(next.x, next.y);   // fell through everything (defensive)
		if (wasGrounded && !s.Jumped && support >= y - c.StepHeight && support <= y + c.StepHeight)
		{
			y = support;                   // walking: follow the ground up and down
			grounded = true;
			vy = 0.0f;
		}
		else
		{
			y += vy * dt;
			if (y <= support && vy <= 0.0f)
			{
				if (!wasGrounded) { s.Landed = true; s.LandingSpeed = -vy; }
				y = support;
				grounded = true;
				vy = 0.0f;
			}
		}
		// head against a ceiling (a box above): stop rising
		if (vy > 0.0f)
			for (const StaticCollider& sc : world.GetStatics())
			{
				if (sc.Shape != ShapeType::Box || !IsYawOnly(sc.Rotation)) continue;
				const glm::vec3 local = glm::inverse(sc.Rotation) * (glm::vec3(next.x, sc.Center.y, next.y) - sc.Center);
				if (std::fabs(local.x) > sc.HalfExtents.x + c.Radius * 0.5f || std::fabs(local.z) > sc.HalfExtents.z + c.Radius * 0.5f) continue;
				const float bottom = sc.Center.y - sc.HalfExtents.y;
				if (bottom > y + c.Height - 0.2f && bottom < y + c.Height + vy * dt) { vy = 0.0f; y = std::min(y, bottom - c.Height); }
			}

		s.Position = glm::vec3(next.x, y, next.y);
		s.Velocity = glm::vec3(v.x, vy, v.y);
		s.Grounded = grounded;
		s.GroundNormal = grounded ? normal : glm::vec3(0, 1, 0);
		s.SlopeDegrees = glm::degrees(std::acos(std::clamp(s.GroundNormal.y, -1.0f, 1.0f)));
		s.Sliding = grounded && s.SlopeDegrees > c.MaxSlopeDegrees;
		if (s.Sliding) s.Grounded = false;   // cannot stand: it is falling along the slope
		s.TimeInAir = s.Grounded ? 0.0f : s.TimeInAir + dt;
		s.Speed = glm::length(v);

		// water around the feet
		const float water = world.WaterHeightAt(s.Position.x, s.Position.z);
		s.WaterDepth = std::max(water - s.Position.y, 0.0f);
		s.InWater = s.WaterDepth > c.WadeDepth;

		// facing
		const float before = s.Yaw;
		float target = s.Yaw;
		bool turn = false;
		if (in.HasYaw) { target = in.DesiredYaw; turn = true; }
		else if (s.Speed > 0.3f) { target = std::atan2(v.x, v.y); turn = true; }
		if (turn)
		{
			const float maxTurn = glm::radians(c.TurnRate) * dt;
			s.Yaw += std::clamp(AngleDelta(s.Yaw, target), -maxTurn, maxTurn);
		}
		s.YawRate = glm::degrees(AngleDelta(before, s.Yaw)) / dt;

		// the character's own axes
		const glm::vec3 forward(std::sin(s.Yaw), 0.0f, std::cos(s.Yaw)), left(std::cos(s.Yaw), 0.0f, -std::sin(s.Yaw));
		s.LocalVelocity = { glm::dot(s.Velocity, left), glm::dot(s.Velocity, forward) };
		s.LocalAcceleration = (s.LocalVelocity - m_PrevLocalVelocity) / dt;
		m_PrevLocalVelocity = s.LocalVelocity;
	}
}
