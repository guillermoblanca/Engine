#include "GB/Game/Interaction.h"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	bool FindInteraction(const EntityWorld& world, const std::string& entity, const std::string& point, InteractionTarget& out, std::string* error)
	{
		auto fail = [&](const std::string& why) { if (error) *error = why; return false; };
		const EntityId id = world.FindByName(entity);
		const Entity* e = world.Find(id);
		if (!e) return fail("no entity called '" + entity + "'");
		if (!e->Interact || e->Interact->Points.empty()) return fail("'" + entity + "' has no interaction points");
		const InteractionPoint* chosen = nullptr;
		for (const InteractionPoint& p : e->Interact->Points)
			if (point.empty() || p.Name == point) { chosen = &p; break; }
		if (!chosen) return fail("'" + entity + "' has no interaction point '" + point + "'");
		const glm::mat4 m = world.WorldMatrix(id);
		const glm::vec3 spot = glm::vec3(m * glm::vec4(chosen->Position, 1.0f));
		const glm::vec3 facing = glm::normalize(glm::vec3(m * glm::vec4(std::sin(glm::radians(chosen->Yaw)), 0.0f, std::cos(glm::radians(chosen->Yaw)), 0.0f)));
		out.Entity = entity;
		out.Point = chosen->Name;
		out.Position = { spot.x, spot.z };
		out.Yaw = std::atan2(facing.x, facing.z);
		out.Montage = chosen->Montage;
		out.Requires = chosen->Requires;
		out.Gives = chosen->Gives;
		out.WarpSeconds = chosen->WarpSeconds;
		return true;
	}

	std::vector<std::string> ListInteractions(const EntityWorld& world)
	{
		std::vector<std::string> names;
		for (const auto& [id, e] : world.All())
			if (e.Interact)
				for (const InteractionPoint& p : e.Interact->Points) names.push_back(e.Name + "/" + p.Name);
		return names;
	}

	void InteractionRunner::Fail(const std::string& why)
	{
		m_Phase = Phase::Failed;
		m_Error = why;
		m_AI.Stop();
	}

	bool InteractionRunner::Start(const InteractionTarget& target, AnimInstance* anim, bool run)
	{
		m_Target = target;
		m_Error.clear();
		m_Events.clear();
		m_MontageStarted = false;
		m_ActTime = 0.0f;
		if (!target.Requires.empty() && (!anim || !anim->HasProp(target.Requires)))
		{
			Fail("needs to carry '" + target.Requires + "'");
			return false;
		}
		if (!target.Montage.empty() && anim && !anim->Montages().Has(target.Montage))
		{
			Fail("this character has no montage '" + target.Montage + "'");
			return false;
		}
		m_AI.Run = run;
		m_AI.ArriveRadius = 0.25f;
		m_AI.MoveTo(target.Position);
		m_Phase = Phase::Approach;
		return true;
	}

	void InteractionRunner::Cancel()
	{
		if (Active() && m_MontageStarted) m_MontageStarted = false;
		m_AI.Stop();
		m_Phase = Phase::Idle;
	}

	CharacterInput InteractionRunner::Update(CharacterController& controller, const CharacterWorld& world, AnimInstance* anim, float dt)
	{
		m_Events.clear();
		CharacterInput none;
		if (!Active()) return none;
		m_AI.WalkSpeed = controller.Config.WalkSpeed;
		m_AI.RunSpeed = controller.Config.RunSpeed;

		if (m_Phase == Phase::Approach)
		{
			if (m_AI.Arrived() || glm::distance(glm::vec2(controller.GetPosition().x, controller.GetPosition().z), m_Target.Position) <= 0.35f)
			{
				m_AI.Stop();
				m_Phase = Phase::Align;
				m_Warp.Start({ controller.GetPosition().x, controller.GetPosition().z }, controller.GetYaw(), m_Target.Position, m_Target.Yaw, m_Target.WarpSeconds);
				if (!m_Target.Montage.empty() && anim)
				{
					if (anim->Montages().Play(m_Target.Montage)) m_MontageStarted = true;
					else { Fail("could not play '" + m_Target.Montage + "'"); return none; }
				}
			}
			else if (m_AI.Stuck())
			{
				Fail("could not reach the interaction point");
				return none;
			}
			else
				return m_AI.Update(controller, dt);
		}

		if (m_Phase == Phase::Align || m_Phase == Phase::Act)
		{
			glm::vec2 xz(controller.GetPosition().x, controller.GetPosition().z);
			float yaw = controller.GetYaw();
			if (m_Phase == Phase::Align)
			{
				m_Warp.Update(dt, xz, yaw);
				if (!m_Warp.Active()) m_Phase = Phase::Act;
			}
			else
			{
				xz = m_Target.Position;
				yaw = m_Target.Yaw;
			}
			controller.SetPosition(world, xz.x, xz.y, yaw);   // the action holds the character on its spot (no velocity: it is not walking)

			m_ActTime += dt;
			bool finished = false;
			if (m_MontageStarted && anim)
			{
				for (const MontageEvent& e : anim->Montages().Events())
				{
					if (e.Montage != m_Target.Montage) continue;
					if (e.What == MontageEvent::Kind::Notify) m_Events.push_back("notify:" + e.Name);
					else if (e.What == MontageEvent::Kind::Finished) finished = true;
					else { Fail("the action was interrupted"); return none; }
				}
			}
			else if (m_Phase == Phase::Act && m_ActTime >= m_Target.WarpSeconds + ActSeconds)
				finished = true;   // nothing to wait for but the time
			if (finished)
			{
				if (!m_Target.Gives.empty()) m_Events.push_back("gives:" + m_Target.Gives);
				m_Events.push_back("done");
				m_Phase = Phase::Done;
			}
		}
		return none;
	}
}
