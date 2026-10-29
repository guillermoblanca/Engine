#include "GameView.h"
#include "PlayerMode.h"
#include "WorldInstance.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace GB::Editor
{
	void GameView::Configure(WorldInstance& world)
	{
		// Every frame: the world may have been generated again (a new terrain), and `pack load` may have changed the parameters.
		if (world.IsBuilt())
		{
			m_Rig.Configure(Params, world.GroundFunction());   // the lake's surface counts as ground: no camera under the water
			const Heightmap& hm = world.GetTerrain().GetHeightmap();
			m_Rig.SetBounds(hm.GetMin(), hm.GetMax());
		}
		else
			m_Rig.Configure(Params, nullptr);
	}

	bool GameView::Subjects(const ActorWorld& actors, const PlayerMode& player)
	{
		switch (m_Rig.Mode())
		{
		case GameCameraMode::Follow:
		{
			const std::string& name = m_Follow.empty() ? player.ControlledName() : m_Follow;
			const Actor* a = name.empty() ? nullptr : actors.Find(name);
			if (!a) return false;
			m_Rig.SetFollowSubject(a->GetPosition(), a->GetYaw());
			if (name == player.ControlledName() && player.IsActive()) m_Rig.SetFollowView(player.CameraYaw(), player.CameraPitch());
			return true;
		}
		case GameCameraMode::Observe:
		{
			std::vector<glm::vec3> feet;
			for (const std::string& n : m_Observe.empty() ? actors.Names() : m_Observe)
				if (const Actor* a = actors.Find(n)) feet.push_back(a->GetPosition());
			if (feet.empty()) return false;
			m_Rig.SetObserveGroup(feet);
			return true;
		}
		case GameCameraMode::Overview:
			return true;
		}
		return true;
	}

	CommandResult GameView::Command(const std::vector<std::string>& a, const ActorWorld& actors, const PlayerMode& player, WorldInstance& world, const Camera& current)
	{
		if (a.empty()) return { true, Describe() };
		const std::string& mode = a[0];
		if (mode == "off")
		{
			m_Active = false;
			return { true, "game camera off (the editor camera is back)" };
		}
		Configure(world);
		if (mode == "follow")
		{
			const std::string name = a.size() > 1 ? a[1] : "";
			if (name.empty() && !player.IsActive()) return { false, "nobody to follow: camera game follow <actor> (or start the player)" };
			if (!name.empty() && !actors.Find(name)) return { false, "no actor called '" + name + "' (actor list)" };
			m_Follow = name == player.ControlledName() ? "" : name;
			m_Rig.SetMode(GameCameraMode::Follow, current, false);   // set the mode first so Subjects feeds the follow camera...
			Subjects(actors, player);
			m_Rig.SetMode(GameCameraMode::Follow, current, true);    // ... then snap it behind the subject and blend from the view on screen
		}
		else if (mode == "overview")
		{
			m_Rig.SetMode(GameCameraMode::Overview, current, true);
			if (a.size() >= 3)
			{
				const glm::vec2 focus(static_cast<float>(std::atof(a[1].c_str())), static_cast<float>(std::atof(a[2].c_str())));
				const float distance = a.size() >= 4 ? static_cast<float>(std::atof(a[3].c_str())) : Params.Overview.Distance;
				m_Rig.Overview().Reset(focus, std::atan2(current.Position.x - current.Target.x, current.Position.z - current.Target.z), distance);   // same heading
			}
		}
		else if (mode == "observe")
		{
			std::vector<std::string> names;
			for (size_t i = 1; i < a.size(); ++i)
				if (a[i] != "all") names.push_back(a[i]);
			for (const std::string& n : names)
				if (!actors.Find(n)) return { false, "no actor called '" + n + "' (actor list)" };
			if (names.empty() && actors.Count() == 0) return { false, "no actors to observe (actor spawn, tribe build)" };
			m_Observe = names;
			m_Rig.SetMode(GameCameraMode::Observe, current, true);
			Subjects(actors, player);
		}
		else
			return { false, "usage: camera game [follow [actor] | overview [x z [distance]] | observe [actor...|all] | off]" };
		m_Active = true;
		return { true, Describe() };
	}

	void GameView::Update(float dt, bool acceptInput, const ActorWorld& actors, const PlayerMode& player, WorldInstance& world, Camera& camera)
	{
		if (!m_Active) return;
		Configure(world);
		if (!Subjects(actors, player) && m_Rig.Mode() != GameCameraMode::Overview)
		{
			// The followed actor went away (removed, the player stopped): fall back to the clan view over where the camera looked.
			m_Rig.SetMode(GameCameraMode::Overview, m_Rig.Output(), true);
		}
		GameCameraInput input;
		if (acceptInput)
		{
			input = ReadGameCameraInput(m_Rig.Mode(), QeTurns);
			const bool playerSteers = m_Rig.Mode() == GameCameraMode::Follow && m_Follow.empty();
			if (playerSteers) input = GameCameraInput{ .ZoomDelta = input.ZoomDelta };   // the player's camera reads the turns itself
		}
		m_Rig.Update(dt, input);
		m_Rig.Apply(camera);
	}

	std::string GameView::Describe() const
	{
		if (!m_Active) return "game camera off (camera game follow [actor] | overview [x z [distance]] | observe [actor...|all])";
		std::string s = std::string("game camera: ") + GameCameraModeName(m_Rig.Mode());
		if (m_Rig.Mode() == GameCameraMode::Follow) s += m_Follow.empty() ? " (the player)" : " " + m_Follow;
		if (m_Rig.Mode() == GameCameraMode::Observe)
		{
			if (m_Observe.empty()) s += " (everyone)";
			for (const std::string& n : m_Observe) s += " " + n;
		}
		const glm::vec3 f = m_Rig.Focus();
		char buf[96];
		std::snprintf(buf, sizeof buf, ", focus (%.1f, %.1f, %.1f)", static_cast<double>(f.x), static_cast<double>(f.y), static_cast<double>(f.z));
		return s + buf + (m_Rig.Transitioning() ? ", blending" : "");
	}
}
