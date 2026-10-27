#include "Physics.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace GB
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;
		constexpr float kSlop = 0.01f;

		struct Shape
		{
			ShapeType Type = ShapeType::Sphere;
			glm::vec3 C{ 0.0f };
			glm::quat Q{ 1, 0, 0, 0 };
			float R = 0.5f, HH = 0.0f;
			glm::vec3 HE{ 0.5f };
		};
		struct Hit { glm::vec3 Point, Normal; float Pen; };

		Shape ShapeOf(const RigidBody& b)
		{
			Shape s;
			s.Type = b.Desc.Shape == ShapeType::Sphere ? ShapeType::Sphere : ShapeType::Box;
			s.C = b.Position; s.Q = b.Rotation; s.R = b.Desc.Radius; s.HE = b.Desc.HalfExtents;
			return s;
		}
		Shape ShapeOf(const StaticCollider& c)
		{
			Shape s;
			s.Type = c.Shape; s.C = c.Center; s.Q = c.Rotation; s.R = c.Radius; s.HH = c.HalfHeight; s.HE = c.HalfExtents;
			return s;
		}

		void Corners(const Shape& s, glm::vec3 out[8])
		{
			int i = 0;
			for (int x = -1; x <= 1; x += 2)
				for (int y = -1; y <= 1; y += 2)
					for (int z = -1; z <= 1; z += 2)
						out[i++] = s.C + s.Q * (glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) * s.HE);
		}

		// A point inside an oriented box: the way out is through the nearest face.
		bool PointInBox(const glm::vec3& p, const Shape& box, Hit& out)
		{
			const glm::vec3 l = glm::inverse(box.Q) * (p - box.C);
			const glm::vec3 d = box.HE - glm::abs(l);
			if (d.x <= 0.0f || d.y <= 0.0f || d.z <= 0.0f)
				return false;
			int axis = 0;
			if (d.y < d[axis]) axis = 1;
			if (d.z < d[axis]) axis = 2;
			glm::vec3 n(0.0f);
			n[axis] = l[axis] >= 0.0f ? 1.0f : -1.0f;
			out = { p, box.Q * n, d[axis] };
			return true;
		}

		bool PointInSphere(const glm::vec3& p, const Shape& s, Hit& out)
		{
			const glm::vec3 d = p - s.C;
			const float len = glm::length(d);
			if (len >= s.R) return false;
			out = { p, len > 1e-5f ? d / len : glm::vec3(0, 1, 0), s.R - len };
			return true;
		}

		bool PointInCapsule(const glm::vec3& p, const Shape& c, Hit& out)
		{
			const float y = std::clamp(p.y, c.C.y - c.HH, c.C.y + c.HH);
			const glm::vec3 d = p - glm::vec3(c.C.x, y, c.C.z);
			const float len = glm::length(d);
			if (len >= c.R) return false;
			out = { p, len > 1e-5f ? d / len : glm::vec3(1, 0, 0), c.R - len };
			return true;
		}

		bool SphereVs(const Shape& s, const Shape& b, Hit& out)
		{
			if (b.Type == ShapeType::Sphere)
			{
				const glm::vec3 d = s.C - b.C;
				const float len = glm::length(d), sum = s.R + b.R;
				if (len >= sum) return false;
				const glm::vec3 n = len > 1e-5f ? d / len : glm::vec3(0, 1, 0);
				out = { b.C + n * b.R, n, sum - len };
				return true;
			}
			if (b.Type == ShapeType::Capsule)
			{
				const float y = std::clamp(s.C.y, b.C.y - b.HH, b.C.y + b.HH);
				const glm::vec3 axis(b.C.x, y, b.C.z);
				const glm::vec3 d = s.C - axis;
				const float len = glm::length(d), sum = s.R + b.R;
				if (len >= sum) return false;
				const glm::vec3 n = len > 1e-5f ? d / len : glm::vec3(1, 0, 0);
				out = { axis + n * b.R, n, sum - len };
				return true;
			}
			// box
			const glm::quat inv = glm::inverse(b.Q);
			const glm::vec3 l = inv * (s.C - b.C);
			const glm::vec3 closest = glm::clamp(l, -b.HE, b.HE);
			const glm::vec3 diff = l - closest;
			const float d2 = glm::dot(diff, diff);
			if (d2 > 1e-10f)
			{
				const float len = std::sqrt(d2);
				if (len >= s.R) return false;
				out = { b.C + b.Q * closest, b.Q * (diff / len), s.R - len };
				return true;
			}
			// the center is inside: leave through the nearest face
			const glm::vec3 d = b.HE - glm::abs(l);
			int axis = 0;
			if (d.y < d[axis]) axis = 1;
			if (d.z < d[axis]) axis = 2;
			glm::vec3 n(0.0f);
			n[axis] = l[axis] >= 0.0f ? 1.0f : -1.0f;
			out = { s.C, b.Q * n, d[axis] + s.R };
			return true;
		}

		bool PointVs(const glm::vec3& p, const Shape& b, Hit& out)
		{
			switch (b.Type)
			{
			case ShapeType::Sphere: return PointInSphere(p, b, out);
			case ShapeType::Capsule: return PointInCapsule(p, b, out);
			default: return PointInBox(p, b, out);
			}
		}


		// Oriented box vs oriented box (separating axis test + face clipping). Normals push `a` away from `b`.
		void BoxBox(const Shape& a, const Shape& b, std::vector<Hit>& out)
		{
			const glm::mat3 Ra = glm::mat3_cast(a.Q), Rb = glm::mat3_cast(b.Q);
			const glm::vec3 d = b.C - a.C;
			float best = 1e30f, bestPen = 0.0f;
			int bestId = -1;
			glm::vec3 bestN(0.0f);   // from a toward b
			auto test = [&](glm::vec3 L, int id, bool edge) -> bool
			{
				const float len = glm::length(L);
				if (len < (edge ? 0.03f : 1e-5f)) return true;   // nearly parallel edges give no usable axis
				L /= len;
				float ra = 0.0f, rb = 0.0f;
				for (int i = 0; i < 3; ++i) { ra += a.HE[i] * std::fabs(glm::dot(Ra[i], L)); rb += b.HE[i] * std::fabs(glm::dot(Rb[i], L)); }
				const float dist = glm::dot(d, L);
				const float pen = ra + rb - std::fabs(dist);
				if (pen < 0.0f) return false;
				const float score = pen * (edge ? 1.1f : 1.0f) + (edge ? 0.002f : 0.0f);
				if (score < best) { best = score; bestPen = pen; bestId = id; bestN = dist < 0.0f ? -L : L; }
				return true;
			};
			for (int i = 0; i < 3; ++i) if (!test(Ra[i], i, false)) return;
			for (int i = 0; i < 3; ++i) if (!test(Rb[i], 3 + i, false)) return;
			for (int i = 0; i < 3; ++i)
				for (int j = 0; j < 3; ++j)
					if (!test(glm::cross(Ra[i], Rb[j]), 6 + 3 * i + j, true)) return;
			if (bestId < 0) return;

			if (bestId >= 6)
			{
				// edge-edge: one contact between the two supporting edges
				const int i = (bestId - 6) / 3, j = (bestId - 6) % 3;
				glm::vec3 pa = a.C, pb = b.C;
				for (int k = 0; k < 3; ++k)
				{
					if (k != i) pa += Ra[k] * (a.HE[k] * (glm::dot(Ra[k], bestN) > 0.0f ? 1.0f : -1.0f));
					if (k != j) pb += Rb[k] * (b.HE[k] * (glm::dot(Rb[k], bestN) > 0.0f ? -1.0f : 1.0f));
				}
				const glm::vec3 da = Ra[i], db = Rb[j], r = pa - pb;
				const float aa = glm::dot(da, da), bb = glm::dot(db, db), ab = glm::dot(da, db), ra_ = glm::dot(da, r), rb_ = glm::dot(db, r);
				const float den = aa * bb - ab * ab;
				const float s = den > 1e-8f ? (ab * rb_ - bb * ra_) / den : 0.0f, t = den > 1e-8f ? (aa * rb_ - ab * ra_) / den : 0.0f;
				const glm::vec3 ca = pa + da * s * 1.0f, cb = pb + db * t;
				out.push_back({ (ca + cb) * 0.5f, -bestN, bestPen });
				return;
			}
			const bool refIsA = bestId < 3;
			const Shape& ref = refIsA ? a : b;
			const Shape& inc = refIsA ? b : a;
			const glm::mat3& Rr = refIsA ? Ra : Rb;
			const glm::mat3& Ri = refIsA ? Rb : Ra;
			const int fi = refIsA ? bestId : bestId - 3;
			const glm::vec3 n = refIsA ? bestN : -bestN;           // reference face normal, pointing at the incident box
			// incident face: the one of `inc` most facing against n
			int ij = 0; float bestDot = 0.0f;
			for (int j = 0; j < 3; ++j)
			{
				const float dd = glm::dot(Ri[j], n);
				if (std::fabs(dd) > std::fabs(bestDot)) { bestDot = dd; ij = j; }
			}
			const glm::vec3 incN = Ri[ij] * (bestDot > 0.0f ? -1.0f : 1.0f);
			const int u = (ij + 1) % 3, v = (ij + 2) % 3;
			const glm::vec3 fc = inc.C + incN * inc.HE[ij];
			std::vector<glm::vec3> poly = {
				fc + Ri[u] * inc.HE[u] + Ri[v] * inc.HE[v], fc - Ri[u] * inc.HE[u] + Ri[v] * inc.HE[v],
				fc - Ri[u] * inc.HE[u] - Ri[v] * inc.HE[v], fc + Ri[u] * inc.HE[u] - Ri[v] * inc.HE[v] };
			// clip against the four side planes of the reference face
			for (int k = 0; k < 3; ++k)
			{
				if (k == fi) continue;
				for (float sgn : { 1.0f, -1.0f })
				{
					const glm::vec3 pn = Rr[k] * sgn;
					const float off = ref.HE[k];
					std::vector<glm::vec3> next;
					for (size_t q = 0; q < poly.size(); ++q)
					{
						const glm::vec3 p0 = poly[q], p1 = poly[(q + 1) % poly.size()];
						const float d0 = glm::dot(p0 - ref.C, pn) - off, d1 = glm::dot(p1 - ref.C, pn) - off;
						if (d0 <= 0.0f) next.push_back(p0);
						if ((d0 < 0.0f) != (d1 < 0.0f) && d0 != d1)
							next.push_back(p0 + (p1 - p0) * (d0 / (d0 - d1)));
					}
					poly.swap(next);
					if (poly.empty()) return;
				}
			}
			const glm::vec3 refFace = ref.C + n * ref.HE[fi];
			for (const glm::vec3& p : poly)
			{
				const float depth = glm::dot(p - refFace, n);
				if (depth <= 0.002f)
					out.push_back({ p, -bestN, std::max(-depth, 0.0f) });
			}
		}

		// Contacts where `a` (a dynamic sphere or box) overlaps `b`; normals push `a` away from `b`.
		void Collide(const Shape& a, const Shape& b, std::vector<Hit>& out)
		{
			Hit h;
			if (a.Type == ShapeType::Sphere)
			{
				if (SphereVs(a, b, h)) out.push_back(h);
				return;
			}
			if (b.Type == ShapeType::Sphere)
			{
				if (SphereVs(b, a, h)) { h.Normal = -h.Normal; out.push_back(h); }
				return;
			}
			if (b.Type == ShapeType::Box)
			{
				BoxBox(a, b, out);
				return;
			}
			glm::vec3 corners[8];
			Corners(a, corners);
			for (const glm::vec3& c : corners)
				if (PointVs(c, b, h)) out.push_back(h);
		}

		glm::mat3 InvInertiaWorld(const RigidBody& b, const glm::vec3& invBody)
		{
			const glm::mat3 R = glm::mat3_cast(b.Rotation);
			return R * glm::mat3(glm::vec3(invBody.x, 0, 0), glm::vec3(0, invBody.y, 0), glm::vec3(0, 0, invBody.z)) * glm::transpose(R);
		}

		glm::mat3 Skew(const glm::vec3& v)
		{
			return glm::mat3(glm::vec3(0.0f, v.z, -v.y), glm::vec3(-v.z, 0.0f, v.x), glm::vec3(v.y, -v.x, 0.0f));   // Skew(v) * u = v x u
		}

		// The twist of axis `a` between references rA and rB (world space), signed about `a`.
		float TwistAngle(const glm::vec3& a, const glm::vec3& rA, const glm::vec3& rB)
		{
			glm::vec3 pa = rA - a * glm::dot(a, rA), pb = rB - a * glm::dot(a, rB);
			if (glm::length(pa) < 1e-6f || glm::length(pb) < 1e-6f) return 0.0f;
			pa = glm::normalize(pa);
			pb = glm::normalize(pb);
			return std::atan2(glm::dot(a, glm::cross(pa, pb)), glm::dot(pa, pb));
		}

		bool RaySphere(const glm::vec3& o, const glm::vec3& d, const glm::vec3& c, float r, float& t)
		{
			const glm::vec3 m = o - c;
			const float b = glm::dot(m, d), cc = glm::dot(m, m) - r * r;
			if (cc > 0.0f && b > 0.0f) return false;
			const float disc = b * b - cc;
			if (disc < 0.0f) return false;
			t = -b - std::sqrt(disc);
			if (t < 0.0f) t = 0.0f;
			return true;
		}

		bool RayBox(const glm::vec3& o, const glm::vec3& d, const Shape& s, float& tOut, glm::vec3& nOut)
		{
			const glm::quat inv = glm::inverse(s.Q);
			const glm::vec3 lo = inv * (o - s.C), ld = inv * d;
			float tmin = 0.0f, tmax = std::numeric_limits<float>::max();
			glm::vec3 n(0.0f);
			for (int i = 0; i < 3; ++i)
			{
				if (std::fabs(ld[i]) < 1e-8f)
				{
					if (std::fabs(lo[i]) > s.HE[i]) return false;
					continue;
				}
				float t1 = (-s.HE[i] - lo[i]) / ld[i], t2 = (s.HE[i] - lo[i]) / ld[i];
				float sign = -1.0f;
				if (t1 > t2) { std::swap(t1, t2); sign = 1.0f; }
				if (t1 > tmin) { tmin = t1; n = glm::vec3(0.0f); n[i] = sign; }
				tmax = std::min(tmax, t2);
				if (tmin > tmax) return false;
			}
			tOut = tmin;
			nOut = s.Q * n;
			return true;
		}

		bool RayCapsule(const glm::vec3& o, const glm::vec3& d, const Shape& s, float& tOut)
		{
			bool hit = false;
			float best = std::numeric_limits<float>::max();
			float t;
			for (float y : { s.C.y - s.HH, s.C.y + s.HH })
				if (RaySphere(o, d, { s.C.x, y, s.C.z }, s.R, t) && t < best) { best = t; hit = true; }
			// the cylinder between the two caps
			const glm::vec2 oc(o.x - s.C.x, o.z - s.C.z), dc(d.x, d.z);
			const float a = glm::dot(dc, dc);
			if (a > 1e-10f)
			{
				const float b = glm::dot(oc, dc), c = glm::dot(oc, oc) - s.R * s.R;
				const float disc = b * b - a * c;
				if (disc >= 0.0f)
				{
					const float tc = (-b - std::sqrt(disc)) / a;
					const float y = o.y + d.y * tc;
					if (tc >= 0.0f && y >= s.C.y - s.HH && y <= s.C.y + s.HH && tc < best) { best = tc; hit = true; }
				}
			}
			tOut = best;
			return hit;
		}
	}

	glm::mat4 RigidBody::GetTransform() const
	{
		glm::mat4 m = glm::mat4_cast(Rotation);
		m[3] = glm::vec4(Position, 1.0f);
		return m;
	}

	float RigidBody::GetBoundingRadius() const
	{
		return Desc.Shape == ShapeType::Sphere ? Desc.Radius : glm::length(Desc.HalfExtents);
	}

	void RigidBody::AddImpulse(const glm::vec3& impulse, const glm::vec3& worldPoint)
	{
		Wake();
		Velocity += impulse * m_InvMass;
		const glm::mat3 R = glm::mat3_cast(Rotation);
		const glm::mat3 inv = R * glm::mat3(glm::vec3(m_InvInertia.x, 0, 0), glm::vec3(0, m_InvInertia.y, 0), glm::vec3(0, 0, m_InvInertia.z)) * glm::transpose(R);
		AngularVelocity += inv * glm::cross(worldPoint - Position, impulse);
	}

	void PhysicsWorld::SetTerrain(const Heightmap* hm)
	{
		m_GroundHeight = [hm](float x, float z) { return hm->GetHeight(x, z); };
		m_GroundNormal = [hm](float x, float z) { return hm->GetNormal(x, z); };
	}

	void PhysicsWorld::SetGround(std::function<float(float, float)> height, std::function<glm::vec3(float, float)> normal)
	{
		m_GroundHeight = std::move(height);
		m_GroundNormal = normal ? std::move(normal) : [](float, float) { return glm::vec3(0, 1, 0); };
	}

	int PhysicsWorld::AddBody(const BodyDesc& desc, const glm::vec3& position, const glm::quat& rotation)
	{
		auto b = std::make_unique<RigidBody>();
		b->Desc = desc;
		b->Position = position;
		b->Rotation = glm::normalize(rotation);
		const glm::vec3 he = desc.HalfExtents;
		float volume;
		if (desc.Shape == ShapeType::Sphere)
		{
			volume = 4.0f / 3.0f * kPi * desc.Radius * desc.Radius * desc.Radius;
			b->Mass = std::max(desc.Density * volume, 0.01f);
			b->m_InvInertia = glm::vec3(1.0f / (0.4f * b->Mass * desc.Radius * desc.Radius));
		}
		else
		{
			b->Desc.Shape = ShapeType::Box;
			volume = 8.0f * he.x * he.y * he.z;
			b->Mass = std::max(desc.Density * volume, 0.01f);
			const float m = b->Mass;
			b->m_InvInertia = { 1.0f / (m / 3.0f * (he.y * he.y + he.z * he.z)), 1.0f / (m / 3.0f * (he.x * he.x + he.z * he.z)),
				1.0f / (m / 3.0f * (he.x * he.x + he.y * he.y)) };
		}
		b->m_InvInertia /= std::max(desc.InertiaScale, 0.01f);
		b->m_InvMass = 1.0f / b->Mass;
		m_Bodies.push_back(std::move(b));
		return static_cast<int>(m_Bodies.size()) - 1;
	}

	void PhysicsWorld::RemoveBody(int index)
	{
		if (!IsAlive(index)) return;
		m_Bodies[static_cast<size_t>(index)].reset();
		for (Joint& j : m_Joints)
			if (j.Alive && (j.D.A == index || j.D.B == index)) j.Alive = false;   // a joint needs both its bodies
	}

	JointDesc PhysicsWorld::MakeJoint(int a, int b, const glm::vec3& worldPoint, const glm::vec3& worldAxis, const glm::vec3& worldRef) const
	{
		JointDesc d;
		d.A = a;
		d.B = b;
		const RigidBody& A = GetBody(a);
		const glm::quat ia = glm::inverse(A.Rotation);
		const glm::vec3 axis = glm::normalize(worldAxis);
		glm::vec3 ref = worldRef - axis * glm::dot(axis, worldRef);
		ref = glm::length(ref) > 1e-5f ? glm::normalize(ref) : glm::normalize(glm::cross(axis, std::fabs(axis.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
		d.AnchorA = ia * (worldPoint - A.Position);
		d.AxisA = ia * axis;
		d.RefA = ia * ref;
		if (b >= 0)
		{
			const RigidBody& B = GetBody(b);
			const glm::quat ib = glm::inverse(B.Rotation);
			d.AnchorB = ib * (worldPoint - B.Position);
			d.AxisB = ib * axis;
			d.RefB = ib * ref;
		}
		else
		{
			d.AnchorB = worldPoint;
			d.AxisB = axis;
			d.RefB = ref;
		}
		return d;
	}

	int PhysicsWorld::AddJoint(const JointDesc& desc)
	{
		if (!IsAlive(desc.A) || (desc.B >= 0 && !IsAlive(desc.B))) return -1;
		Joint j;
		j.D = desc;
		m_Joints.push_back(j);
		return static_cast<int>(m_Joints.size()) - 1;
	}

	void PhysicsWorld::RemoveJoint(int index)
	{
		if (index >= 0 && static_cast<size_t>(index) < m_Joints.size()) m_Joints[static_cast<size_t>(index)].Alive = false;
	}

	namespace
	{
		struct JointFrame { glm::vec3 PA, PB, WA, WB, RA, RB; };
	}

	static bool FrameOf(const std::vector<std::unique_ptr<RigidBody>>& bodies, const JointDesc& d, JointFrame& f)
	{
		const RigidBody* A = bodies[static_cast<size_t>(d.A)].get();
		const RigidBody* B = d.B >= 0 ? bodies[static_cast<size_t>(d.B)].get() : nullptr;
		if (!A || (d.B >= 0 && !B)) return false;
		f.PA = A->Position + A->Rotation * d.AnchorA;
		f.WA = A->Rotation * d.AxisA;
		f.RA = A->Rotation * d.RefA;
		f.PB = B ? B->Position + B->Rotation * d.AnchorB : d.AnchorB;
		f.WB = B ? B->Rotation * d.AxisB : d.AxisB;
		f.RB = B ? B->Rotation * d.RefB : d.RefB;
		return true;
	}

	float PhysicsWorld::GetJointSwing(int index) const
	{
		JointFrame f;
		if (!IsJointAlive(index) || !FrameOf(m_Bodies, m_Joints[static_cast<size_t>(index)].D, f)) return 0.0f;
		return std::acos(std::clamp(glm::dot(f.WA, f.WB), -1.0f, 1.0f));
	}

	float PhysicsWorld::GetJointTwist(int index) const
	{
		JointFrame f;
		if (!IsJointAlive(index) || !FrameOf(m_Bodies, m_Joints[static_cast<size_t>(index)].D, f)) return 0.0f;
		return TwistAngle(f.WA, f.RA, f.RB);
	}

	float PhysicsWorld::GetJointError(int index) const
	{
		JointFrame f;
		if (!IsJointAlive(index) || !FrameOf(m_Bodies, m_Joints[static_cast<size_t>(index)].D, f)) return 0.0f;
		return glm::distance(f.PA, f.PB);
	}

	void PhysicsWorld::ApplyForces(RigidBody& b, int index, float dt)
	{
		const float r = b.GetBoundingRadius();
		b.Velocity += Gravity * dt;

		// Air drag toward the wind (plus the vortices' winds): a = 0.5 rho Cd A |du| du / m.
		glm::vec3 wind = Wind;
		float grip = 1.0f;
		for (const Vortex& v : Vortices)
		{
			const glm::vec3 d = b.Position - v.Position;
			const glm::vec2 h(d.x, d.z);
			const float dist = glm::length(h);
			const float reach = v.Radius * 3.5f;
			if (dist > reach || d.y < -2.0f || d.y > v.Height) continue;
			const glm::vec2 radial = dist > 1e-3f ? h / dist : glm::vec2(1.0f, 0.0f);
			const glm::vec2 tangent(-radial.y, radial.x);
			// Rankine-like profile: solid rotation inside the core, 1/r outside.
			const float prof = dist < v.Radius ? dist / v.Radius : v.Radius / dist;
			const float fadeTop = 1.0f - glm::smoothstep(v.Height * 0.7f, v.Height, d.y);
			const float inside = 1.0f - glm::smoothstep(v.Radius * 1.3f, reach, dist);
			const glm::vec2 horiz = (tangent * v.TangentialSpeed * prof - radial * v.InwardSpeed * (0.4f + 0.6f * prof)) * fadeTop;
			wind += glm::vec3(horiz.x, v.UpdraftSpeed * inside * fadeTop, horiz.y);
			grip = std::max(grip, 1.0f + (v.Grip - 1.0f) * inside * fadeTop);
			b.Wake();
		}
		const glm::vec3 rel = wind - b.Velocity;
		const float speed = glm::length(rel);
		if (speed > 1e-3f)
		{
			const float area = kPi * r * r * 0.6f;
			glm::vec3 a = rel * (0.5f * AirDensity * grip * area * speed * b.m_InvMass);
			const float maxA = speed / dt * 0.5f;   // never overshoot the wind in one step
			const float la = glm::length(a);
			if (la > maxA) a *= maxA / la;
			b.Velocity += a * dt;
		}

		// Buoyancy.
		b.Submersion = 0.0f;
		if (m_Water)
		{
			const float surface = m_Water(b.Position.x, b.Position.z);
			if (surface > -1e8f)
			{
				float below, size;
				if (b.Desc.Shape == ShapeType::Sphere)
				{
					size = 2.0f * b.Desc.Radius;
					below = surface - (b.Position.y - b.Desc.Radius);
				}
				else
				{
					const glm::mat3 R = glm::mat3_cast(b.Rotation);
					const float hh = std::fabs(R[0].y) * b.Desc.HalfExtents.x + std::fabs(R[1].y) * b.Desc.HalfExtents.y + std::fabs(R[2].y) * b.Desc.HalfExtents.z;
					size = 2.0f * hh;
					below = surface - (b.Position.y - hh);
				}
				b.Submersion = std::clamp(below / size, 0.0f, 1.0f);
				const bool wet = b.Submersion > 0.0f;
				if (wet != b.m_WasWet)
				{
					SplashEvent e;
					e.Position = { b.Position.x, surface, b.Position.z };
					e.Speed = -b.Velocity.y;
					e.Radius = r;
					e.Body = index;
					if (std::fabs(b.Velocity.y) > 0.5f) m_Splashes.push_back(e);
					b.m_WasWet = wet;
				}
				if (wet)
				{
					b.Wake();
					const float up = -Gravity.y * (1000.0f / b.Desc.Density) * b.Submersion;
					b.Velocity.y += up * dt;
					const float drag = std::min(1.0f, 2.2f * b.Submersion * dt);
					const glm::vec3 current = m_Current ? m_Current(b.Position.x, b.Position.z) : glm::vec3(0.0f);
					const glm::vec3 relative = b.Velocity - current;
					b.Velocity -= relative * std::min(1.0f, drag * (1.0f + 0.2f * glm::length(relative)));   // the water carries the body along
					b.AngularVelocity -= b.AngularVelocity * std::min(1.0f, 2.0f * b.Submersion * dt);
				}
			}
		}
		b.Velocity *= std::max(0.0f, 1.0f - b.Desc.LinearDamping * dt);
		b.AngularVelocity *= std::max(0.0f, 1.0f - b.Desc.AngularDamping * dt);
	}

	void PhysicsWorld::GenerateContacts()
	{
		m_Contacts.clear();
		std::vector<Hit> hits;
		for (size_t i = 0; i < m_Bodies.size(); ++i)
		{
			const RigidBody* b = m_Bodies[i].get();
			if (!b) continue;
			const Shape sa = ShapeOf(*b);
			const int ia = static_cast<int>(i);

			// ground
			if (m_GroundHeight)
			{
				auto point = [&](const glm::vec3& p, float radius)
				{
					const float h = m_GroundHeight(p.x, p.z);
					const glm::vec3 n = m_GroundNormal ? m_GroundNormal(p.x, p.z) : glm::vec3(0, 1, 0);
					const float d = glm::dot(p - glm::vec3(p.x, h, p.z), n) - radius;
					if (d < 0.0f)
						m_Contacts.push_back({ ia, -1, p - n * radius, n, -d, ContactEvent::Kind::Ground });
				};
				if (sa.Type == ShapeType::Sphere)
					point(sa.C, sa.R);
				else
				{
					glm::vec3 corners[8];
					Corners(sa, corners);
					for (const glm::vec3& c : corners) point(c, 0.0f);
				}
			}
			// static colliders
			for (const StaticCollider& st : m_Statics)
			{
				hits.clear();
				Collide(sa, ShapeOf(st), hits);
				for (const Hit& h : hits) m_Contacts.push_back({ ia, -1, h.Point, h.Normal, h.Pen, ContactEvent::Kind::Static });
			}
			// other bodies (each pair once)
			for (size_t j = i + 1; j < m_Bodies.size(); ++j)
			{
				const RigidBody* o = m_Bodies[j].get();
				if (!o) continue;
				if (b->Desc.Group != 0 && b->Desc.Group == o->Desc.Group) continue;   // the limbs of one ragdoll do not collide with each other
				if (glm::distance(b->Position, o->Position) > b->GetBoundingRadius() + o->GetBoundingRadius()) continue;
				hits.clear();
				Collide(sa, ShapeOf(*o), hits);
				for (const Hit& h : hits) m_Contacts.push_back({ ia, static_cast<int>(j), h.Point, h.Normal, h.Pen, ContactEvent::Kind::Body });
			}
		}
	}

	void PhysicsWorld::Solve(float dt)
	{
		// Sequential impulses with accumulated, clamped impulses (so friction is limited by the real normal load).
		struct Row
		{
			RigidBody* A; RigidBody* B;
			glm::mat3 IA, IB;
			glm::vec3 ra, rb, n, t1, t2;
			float invA = 0.0f, invB = 0.0f, vn0 = 0.0f;
			float kn, k1, k2, bias, mu, accN = 0.0f, acc1 = 0.0f, acc2 = 0.0f;
		};
		std::vector<Row> rows(m_Contacts.size());
		std::vector<bool> active(m_Contacts.size(), false);
		for (size_t ci = 0; ci < m_Contacts.size(); ++ci)
		{
			const Contact& c = m_Contacts[ci];
			Row& r = rows[ci];
			r.A = m_Bodies[static_cast<size_t>(c.A)].get();
			r.B = c.B >= 0 ? m_Bodies[static_cast<size_t>(c.B)].get() : nullptr;
			// A sleeping body is woken by a moving neighbour; otherwise it acts as an immovable support.
			if (r.B)
			{
				if (r.A->Sleeping && !r.B->Sleeping && glm::length(r.B->Velocity) > SleepSpeed) r.A->Wake();
				if (r.B->Sleeping && !r.A->Sleeping && glm::length(r.A->Velocity) > SleepSpeed) r.B->Wake();
			}
			if (r.A->Sleeping && (!r.B || r.B->Sleeping)) continue;
			active[ci] = true;
			r.IA = r.A->Sleeping ? glm::mat3(0.0f) : InvInertiaWorld(*r.A, r.A->m_InvInertia);
			r.IB = r.B && !r.B->Sleeping ? InvInertiaWorld(*r.B, r.B->m_InvInertia) : glm::mat3(0.0f);
			r.invA = r.A->Sleeping ? 0.0f : r.A->m_InvMass;
			r.invB = r.B && !r.B->Sleeping ? r.B->m_InvMass : 0.0f;
			r.ra = c.Point - r.A->Position;
			r.rb = r.B ? c.Point - r.B->Position : glm::vec3(0.0f);
			r.n = c.Normal;
			glm::vec3 helper = std::fabs(r.n.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
			r.t1 = glm::normalize(glm::cross(r.n, helper));
			r.t2 = glm::cross(r.n, r.t1);
			auto eff = [&](const glm::vec3& d)
			{
				float k = r.invA + glm::dot(d, glm::cross(r.IA * glm::cross(r.ra, d), r.ra));
				if (r.B) k += r.invB + glm::dot(d, glm::cross(r.IB * glm::cross(r.rb, d), r.rb));
				return std::max(k, 1e-9f);
			};
			r.kn = eff(r.n); r.k1 = eff(r.t1); r.k2 = eff(r.t2);
			r.mu = r.B ? std::sqrt(r.A->Desc.Friction * r.B->Desc.Friction) : r.A->Desc.Friction;
			const glm::vec3 va = r.A->Velocity + glm::cross(r.A->AngularVelocity, r.ra);
			const glm::vec3 vb = r.B ? r.B->Velocity + glm::cross(r.B->AngularVelocity, r.rb) : glm::vec3(0.0f);
			r.vn0 = glm::dot(va - vb, r.n);
			const float vn0 = r.vn0;
			const float e = r.B ? std::min(r.A->Desc.Restitution, r.B->Desc.Restitution) : r.A->Desc.Restitution;
			r.bias = (vn0 < -1.5f) ? -e * vn0 : 0.0f;   // bounce only on real impacts
		}
		// Joint rows: the anchors together (3 equations), the swing and the twist (limits, or a hinge's two perpendicular equations).
		struct JRow
		{
			const JointDesc* D = nullptr;
			RigidBody* A = nullptr; RigidBody* B = nullptr;
			glm::mat3 IA{ 0.0f }, IB{ 0.0f };
			float invA = 0.0f, invB = 0.0f;
			glm::vec3 ra{ 0.0f }, rb{ 0.0f }, biasP{ 0.0f };
			glm::mat3 Kinv{ 0.0f };
			glm::vec3 wA{ 0, 1, 0 }, wB{ 0, 1, 0 };
			// angular constraints: direction, bias velocity, sign of the allowed impulse (0 = equality), accumulated impulse
			glm::vec3 dir[3]; float bias[3]; int kind[3]; float acc[3] = { 0.0f, 0.0f, 0.0f }; float k[3]; int count = 0;
		};
		std::vector<JRow> jrows;
		const float beta = 0.2f / std::max(dt, 1e-4f);
		for (const Joint& j : m_Joints)
		{
			if (!j.Alive) continue;
			JointFrame f;
			if (!FrameOf(m_Bodies, j.D, f)) continue;
			JRow r;
			r.D = &j.D;
			r.A = m_Bodies[static_cast<size_t>(j.D.A)].get();
			r.B = j.D.B >= 0 ? m_Bodies[static_cast<size_t>(j.D.B)].get() : nullptr;
			if (r.B)
			{
				if (r.A->Sleeping && !r.B->Sleeping && glm::length(r.B->Velocity) > SleepSpeed) r.A->Wake();
				if (r.B->Sleeping && !r.A->Sleeping && glm::length(r.A->Velocity) > SleepSpeed) r.B->Wake();
			}
			if (r.A->Sleeping && (!r.B || r.B->Sleeping)) continue;
			r.IA = r.A->Sleeping ? glm::mat3(0.0f) : InvInertiaWorld(*r.A, r.A->m_InvInertia);
			r.IB = r.B && !r.B->Sleeping ? InvInertiaWorld(*r.B, r.B->m_InvInertia) : glm::mat3(0.0f);
			r.invA = r.A->Sleeping ? 0.0f : r.A->m_InvMass;
			r.invB = r.B && !r.B->Sleeping ? r.B->m_InvMass : 0.0f;
			r.ra = f.PA - r.A->Position;
			r.rb = r.B ? f.PB - r.B->Position : glm::vec3(0.0f);
			const glm::mat3 sa = Skew(r.ra), sb = Skew(r.rb);
			glm::mat3 K = (r.invA + r.invB) * glm::mat3(1.0f) - sa * r.IA * sa - sb * r.IB * sb;
			r.Kinv = glm::inverse(K);
			r.biasP = beta * (f.PA - f.PB);
			r.wA = f.WA; r.wB = f.WB;
			auto addRow = [&](const glm::vec3& dir, float bias, int kind)
			{
				if (r.count >= 3) return;
				r.dir[r.count] = dir; r.bias[r.count] = bias; r.kind[r.count] = kind;
				r.k[r.count] = std::max(glm::dot(dir, (r.IA + r.IB) * dir), 1e-9f);
				++r.count;
			};
			const float swing = std::acos(std::clamp(glm::dot(f.WA, f.WB), -1.0f, 1.0f));
			const glm::vec3 e = glm::cross(f.WA, f.WB);
			if (j.D.Hinge)
			{
				// the axes stay parallel: no relative rotation except about the axis (two equations perpendicular to it)
				const glm::vec3 helper = std::fabs(f.WA.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
				const glm::vec3 t1 = glm::normalize(glm::cross(f.WA, helper)), t2 = glm::cross(f.WA, t1);
				addRow(t1, -beta * glm::dot(e, t1), 0);
				addRow(t2, -beta * glm::dot(e, t2), 0);
			}
			else if (j.D.SwingLimit < 3.1f && swing > j.D.SwingLimit && glm::length(e) > 1e-6f)
				addRow(glm::normalize(e), -beta * (swing - j.D.SwingLimit), -1);
			jrows.push_back(r);
			JRow& back = jrows.back();
			// the twist limits (a separate pass: up to three rows in all, the hinge uses two for its axis and one for the twist)
			const float twist = TwistAngle(f.WA, f.RA, f.RB);
			auto addTwist = [&](const glm::vec3& dir, float bias, int kind)
			{
				if (back.count >= 3) return;
				back.dir[back.count] = dir; back.bias[back.count] = bias; back.kind[back.count] = kind;
				back.k[back.count] = std::max(glm::dot(dir, (back.IA + back.IB) * dir), 1e-9f);
				++back.count;
			};
			if (twist > j.D.TwistMax) addTwist(f.WA, -beta * (twist - j.D.TwistMax), -1);
			else if (twist < j.D.TwistMin) addTwist(f.WA, beta * (j.D.TwistMin - twist), 1);
		}

		auto apply = [&](Row& r, const glm::vec3& P)
		{
			r.A->Velocity += P * r.invA;
			r.A->AngularVelocity += r.IA * glm::cross(r.ra, P);
			if (r.B)
			{
				r.B->Velocity -= P * r.invB;
				r.B->AngularVelocity -= r.IB * glm::cross(r.rb, P);
			}
		};
		auto relVel = [&](const Row& r)
		{
			const glm::vec3 va = r.A->Velocity + glm::cross(r.A->AngularVelocity, r.ra);
			const glm::vec3 vb = r.B ? r.B->Velocity + glm::cross(r.B->AngularVelocity, r.rb) : glm::vec3(0.0f);
			return va - vb;
		};
		for (int iter = 0; iter < 30; ++iter)
		{
			for (JRow& r : jrows)
			{
				// the anchors
				const glm::vec3 va = r.A->Velocity + glm::cross(r.A->AngularVelocity, r.ra);
				const glm::vec3 vb = r.B ? r.B->Velocity + glm::cross(r.B->AngularVelocity, r.rb) : glm::vec3(0.0f);
				const glm::vec3 P = r.Kinv * (-(va - vb) - r.biasP);
				r.A->Velocity += P * r.invA;
				r.A->AngularVelocity += r.IA * glm::cross(r.ra, P);
				if (r.B)
				{
					r.B->Velocity -= P * r.invB;
					r.B->AngularVelocity -= r.IB * glm::cross(r.rb, P);
				}
				// the angles
				for (int q = 0; q < r.count; ++q)
				{
					const glm::vec3 rel = (r.B ? r.B->AngularVelocity : glm::vec3(0.0f)) - r.A->AngularVelocity;
					const float cdot = glm::dot(rel, r.dir[q]);
					const float lambda = (r.bias[q] - cdot) / r.k[q];
					float next = r.acc[q] + lambda;
					if (r.kind[q] < 0) next = std::min(next, 0.0f);
					else if (r.kind[q] > 0) next = std::max(next, 0.0f);
					const float delta = next - r.acc[q];
					r.acc[q] = r.kind[q] == 0 ? 0.0f : next;   // equations are re-solved from scratch, limits accumulate
					r.A->AngularVelocity -= r.IA * (r.dir[q] * delta);
					if (r.B) r.B->AngularVelocity += r.IB * (r.dir[q] * delta);
				}
			}
			for (size_t ci = 0; ci < rows.size(); ++ci)
			{
				if (!active[ci]) continue;
				Row& r = rows[ci];
				// friction first (bounded by the load from the previous iteration), then the normal
				for (int k = 0; k < 2; ++k)
				{
					const glm::vec3& t = k == 0 ? r.t1 : r.t2;
					float& acc = k == 0 ? r.acc1 : r.acc2;
					const float lambda = -glm::dot(relVel(r), t) / (k == 0 ? r.k1 : r.k2);
					const float limit = r.mu * r.accN;
					const float next = std::clamp(acc + lambda, -limit, limit);
					apply(r, t * (next - acc));
					acc = next;
				}
				const float lambda = (-glm::dot(relVel(r), r.n) + r.bias) / r.kn;
				const float next = std::max(r.accN + lambda, 0.0f);
				apply(r, r.n * (next - r.accN));
				r.accN = next;
			}
		}
		std::vector<float> total(m_Contacts.size(), 0.0f);
		for (size_t ci = 0; ci < rows.size(); ++ci) total[ci] = -rows[ci].vn0 > 0.8f ? rows[ci].accN : 0.0f;   // only real impacts, not the load of a resting body
		// Push bodies out of what they overlap.
		for (const Contact& c : m_Contacts)
		{
			const float corr = std::max(c.Penetration - kSlop, 0.0f) * 0.2f;
			if (corr <= 0.0f) continue;
			RigidBody& A = *m_Bodies[static_cast<size_t>(c.A)];
			if (c.B >= 0)
			{
				RigidBody& B = *m_Bodies[static_cast<size_t>(c.B)];
				const float ia = A.Sleeping ? 0.0f : A.m_InvMass, ib = B.Sleeping ? 0.0f : B.m_InvMass;
				const float s = ia + ib;
				if (s <= 0.0f) continue;
				A.Position += c.Normal * corr * (ia / s);
				B.Position -= c.Normal * corr * (ib / s);
			}
			else if (!A.Sleeping)
				A.Position += c.Normal * corr;
		}
		for (size_t ci = 0; ci < m_Contacts.size(); ++ci)
		{
			if (total[ci] < MinImpactImpulse) continue;
			ContactEvent e;
			e.What = m_Contacts[ci].Kind;
			e.Position = m_Contacts[ci].Point;
			e.Normal = m_Contacts[ci].Normal;
			e.Impulse = total[ci];
			e.Body = m_Contacts[ci].A;
			e.Other = m_Contacts[ci].B;
			m_ContactEvents.push_back(e);
			if (m_Contacts[ci].B >= 0)   // the other body felt the same hit
			{
				e.Body = m_Contacts[ci].B;
				e.Other = m_Contacts[ci].A;
				e.Normal = -e.Normal;
				m_ContactEvents.push_back(e);
			}
		}
	}

	void PhysicsWorld::SubStep(float dt)
	{
		for (size_t i = 0; i < m_Bodies.size(); ++i)
		{
			RigidBody* b = m_Bodies[i].get();
			if (!b || b->Sleeping) continue;
			ApplyForces(*b, static_cast<int>(i), dt);
		}
		GenerateContacts();
		Solve(dt);
		// A body may only fall asleep when everything touching it is nearly still as well (so stacks sleep together).
		std::vector<char> disturbed(m_Bodies.size(), 0);
		auto moving = [&](const RigidBody& b) { return glm::length(b.Velocity) >= SleepSpeed || glm::length(b.AngularVelocity) >= 0.3f; };
		for (const Contact& c : m_Contacts)
			if (c.B >= 0 && (moving(*m_Bodies[static_cast<size_t>(c.A)]) || moving(*m_Bodies[static_cast<size_t>(c.B)])))
				disturbed[static_cast<size_t>(c.A)] = disturbed[static_cast<size_t>(c.B)] = 1;
		for (const Joint& j : m_Joints)
		{
			if (!j.Alive || j.D.B < 0) continue;
			const RigidBody* a = m_Bodies[static_cast<size_t>(j.D.A)].get();
			const RigidBody* b = m_Bodies[static_cast<size_t>(j.D.B)].get();
			if (a && b && (moving(*a) || moving(*b))) disturbed[static_cast<size_t>(j.D.A)] = disturbed[static_cast<size_t>(j.D.B)] = 1;   // a joined group sleeps together
		}
		for (size_t bi = 0; bi < m_Bodies.size(); ++bi)
		{
			RigidBody* b = m_Bodies[bi].get();
			if (!b || b->Sleeping) continue;
			if (disturbed[bi]) b->m_Still = 0.0f;
			b->Position += b->Velocity * dt;
			const float w = glm::length(b->AngularVelocity);
			if (w > 1e-6f)
				b->Rotation = glm::normalize(glm::angleAxis(w * dt, b->AngularVelocity / w) * b->Rotation);
			if (b->Velocity.y == b->Velocity.y)   // NaN guard: a NaN body would poison its neighbours
			{
				if (glm::length(b->Velocity) < SleepSpeed && w < 0.3f && b->Submersion == 0.0f && !disturbed[bi])
				{
					b->m_Still += dt;
					if (b->m_Still > SleepTime) { b->Sleeping = true; b->Velocity = glm::vec3(0.0f); b->AngularVelocity = glm::vec3(0.0f); }
				}
				else
					b->m_Still = 0.0f;
			}
		}
	}

	void PhysicsWorld::Step(float dt)
	{
		m_Accumulator += std::min(dt, 0.1f);
		int steps = 0;
		while (m_Accumulator >= FixedStep && steps < 20)
		{
			SubStep(FixedStep);
			m_Accumulator -= FixedStep;
			++steps;
		}
		if (steps == 20) m_Accumulator = 0.0f;
	}

	std::vector<PhysicsWorld::ExplosionHit> PhysicsWorld::Explode(const glm::vec3& center, float radius, float speedAtCenter)
	{
		std::vector<ExplosionHit> hits;
		for (size_t i = 0; i < m_Bodies.size(); ++i)
		{
			RigidBody* b = m_Bodies[i].get();
			if (!b) continue;
			glm::vec3 d = b->Position - center;
			const float dist = glm::length(d);
			const float reach = radius + b->GetBoundingRadius();
			if (dist >= reach) continue;
			const float t = 1.0f - dist / reach;
			const float falloff = t * t;
			d = dist > 1e-4f ? d / dist : glm::vec3(0, 1, 0);
			const glm::vec3 dir = glm::normalize(d + glm::vec3(0.0f, 0.35f, 0.0f));
			const float dv = speedAtCenter * falloff * glm::clamp(std::sqrt(80.0f / b->Mass), 0.15f, 2.0f);
			const glm::vec3 impulse = dir * dv * b->Mass;
			b->AddImpulse(impulse, b->Position - d * (0.3f * b->GetBoundingRadius()));   // off-center: the body spins
			hits.push_back({ static_cast<int>(i), falloff, dv * b->Mass });
		}
		return hits;
	}

	RayHit PhysicsWorld::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const
	{
		RayHit best;
		best.Distance = maxDistance;
		const glm::vec3 d = glm::normalize(direction);
		auto consider = [&](float t, const glm::vec3& n, int body)
		{
			if (t >= 0.0f && t < best.Distance)
			{
				best.Hit = true; best.Distance = t; best.Position = origin + d * t; best.Normal = n; best.Body = body;
			}
		};
		float t; glm::vec3 n;
		auto test = [&](const Shape& s, int body)
		{
			if (s.Type == ShapeType::Sphere) { if (RaySphere(origin, d, s.C, s.R, t)) consider(t, glm::normalize(origin + d * t - s.C), body); }
			else if (s.Type == ShapeType::Box) { if (RayBox(origin, d, s, t, n)) consider(t, n, body); }
			else if (RayCapsule(origin, d, s, t))
			{
				const glm::vec3 p = origin + d * t;
				const float y = std::clamp(p.y, s.C.y - s.HH, s.C.y + s.HH);
				consider(t, glm::normalize(p - glm::vec3(s.C.x, y, s.C.z)), body);
			}
		};
		for (const StaticCollider& c : m_Statics) test(ShapeOf(c), -1);
		for (size_t i = 0; i < m_Bodies.size(); ++i)
			if (m_Bodies[i]) test(ShapeOf(*m_Bodies[i]), static_cast<int>(i));
		if (m_GroundHeight)
		{
			const float step = 0.25f;
			float prevT = 0.0f;
			bool prevAbove = origin.y > m_GroundHeight(origin.x, origin.z);
			for (float s = step; s <= best.Distance + step; s += step)
			{
				const float ts = std::min(s, best.Distance);
				const glm::vec3 p = origin + d * ts;
				const bool above = p.y > m_GroundHeight(p.x, p.z);
				if (prevAbove && !above)
				{
					float lo = prevT, hi = ts;
					for (int k = 0; k < 10; ++k)
					{
						const float mid = 0.5f * (lo + hi);
						const glm::vec3 q = origin + d * mid;
						if (q.y > m_GroundHeight(q.x, q.z)) lo = mid; else hi = mid;
					}
					const glm::vec3 q = origin + d * hi;
					consider(hi, m_GroundNormal ? m_GroundNormal(q.x, q.z) : glm::vec3(0, 1, 0), -1);
					break;
				}
				prevT = ts;
				prevAbove = above;
				if (ts >= best.Distance) break;
			}
		}
		return best;
	}
}
