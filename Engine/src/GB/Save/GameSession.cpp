#include "GB/Save/GameSession.h"
#include <cmath>
#include <cstdlib>

namespace GB::Save
{
	namespace
	{
		// A tiny deterministic generator (no <random>: its distributions differ between standard libraries). Integers only, so every
		// platform and compiler scatters the same world (no float expression a compiler could fuse differently).
		struct Lcg
		{
			uint64_t State;
			explicit Lcg(uint64_t seed) : State(seed * 6364136223846793005ull + 1442695040888963407ull) {}
			uint32_t Next()   // 0 .. 2^24 - 1
			{
				State = State * 6364136223846793005ull + 1442695040888963407ull;
				return static_cast<uint32_t>((State >> 40) & 0xFFFFFFu);
			}
		};

		float ToFloat(const std::string& s) { return static_cast<float>(std::atof(s.c_str())); }
		constexpr float kRadToDeg = 57.29577951308232f;
	}

	void ScatterWorld(const WorldParams& world, EntityWorld& out)
	{
		out = EntityWorld();
		if (!world.Generated) return;
		Lcg rng(world.Seed);
		auto fill = [&](const char* group, const char* prefix, PropKind kind, int count, int variants)
		{
			if (count <= 0) return;
			const EntityId g = out.Create(group);
			out.Find(g)->Tags.push_back("world");
			for (int i = 1; i <= count; ++i)
			{
				const EntityId id = out.Create(std::string(prefix) + " " + std::to_string(i), g);
				Entity* e = out.Find(id);
				const float x = static_cast<float>(static_cast<int>(rng.Next() % 2001) - 1000) / 2000.0f;   // -0.5 .. 0.5
				const float z = static_cast<float>(static_cast<int>(rng.Next() % 2001) - 1000) / 2000.0f;
				e->Transform.Position = { x * world.Size, 0.0f, z * world.Size };
				e->Transform.Rotation.y = static_cast<float>(rng.Next() % 360);
				e->Transform.Scale = glm::vec3(static_cast<float>(70 + rng.Next() % 80) / 100.0f);
				e->Prop = PropComponent{ kind, static_cast<int>(rng.Next() % static_cast<uint32_t>(variants)) };
			}
		};
		fill("Trees", "Tree", PropKind::Tree, world.TreeBudget, 4);
		fill("Rocks", "Rock", PropKind::Rock, world.RockBudget, 3);
	}

	GameSession::GameSession(SessionConfig config) : m_Config(std::move(config))
	{
		RegisterGardenContent(m_Gameplay.Library);
		m_Clock.Dt = m_Config.Dt;
		m_Clock.DayLengthSeconds = m_Config.DayLengthSeconds;
		Reset(m_Config.World);
	}

	void GameSession::BindRules() { m_Garden.Bind(m_Gameplay, &m_Entities); }

	void GameSession::Reset(const WorldParams& world)
	{
		m_World = world;
		std::string error;
		m_Entities = EntityWorld();
		BuildBaseline(world, m_Entities, error);
		m_Gameplay.Load(m_Entities);
		BindRules();
		m_Walkers.clear();
		m_Clock = ClockState();
		m_Clock.Dt = m_Config.Dt;
		m_Clock.DayLengthSeconds = m_Config.DayLengthSeconds;
		m_Weather = WeatherState();
	}

	bool GameSession::BuildBaseline(const WorldParams& world, EntityWorld& out, std::string& error) const
	{
		if (m_Config.Baseline) return m_Config.Baseline(world, out, error);
		ScatterWorld(world, out);
		return true;
	}

	const CharacterController* GameSession::Character(const std::string& name) const
	{
		auto it = m_Walkers.find(name);
		return it == m_Walkers.end() ? nullptr : &it->second.Controller;
	}

	std::vector<ActorState> GameSession::Actors() const
	{
		if (m_External) return m_External->CaptureActors();
		std::vector<ActorState> out;
		for (const auto& [name, w] : m_Walkers)
		{
			ActorState a;
			a.Name = name;
			a.Position = w.Controller.GetPosition();
			a.Yaw = w.Controller.GetYaw();
			a.Move = w.Input.Move;
			a.Run = w.Input.Run;
			out.push_back(std::move(a));
		}
		return out;
	}

	std::string GameSession::Validate(const LoadedGame& game)
	{
		std::string why = ISaveHost::Validate(game);
		if (!why.empty()) return why;
		if (m_External) return m_External->RestoreActors(game.Actors, true);
		for (const ActorState& a : game.Actors)
			if (!game.Entities.FindByName(a.Name)) return "the save has an actor '" + a.Name + "' with no entity";
		return {};
	}

	void GameSession::Commit(LoadedGame&& game)
	{
		m_World = game.World;
		m_Entities = std::move(game.Entities);
		RestoreGameplay(m_Gameplay, m_Entities, game.Gameplay);
		BindRules();
		m_Clock = game.Clock;
		m_Weather = game.Weather;
		m_Walkers.clear();
		if (m_External) m_External->RestoreActors(game.Actors, false);
		else
			for (const ActorState& a : game.Actors)
			{
				std::string ignore;
				const glm::vec3 at = a.Position;
				if (Spawn(a.Name, &at, a.Yaw * kRadToDeg, ignore))
				{
					Walker& w = m_Walkers[a.Name];
					w.Input.Move = a.Move;
					w.Input.Run = a.Run;
				}
			}
	}

	bool GameSession::Spawn(const std::string& entity, const glm::vec3* at, float yawDegrees, std::string& message)
	{
		const Entity* e = m_Entities.Find(m_Entities.FindByName(entity));
		if (!e) { message = "no entity '" + entity + "'"; return false; }
		const glm::vec3 p = at ? *at : e->Transform.Position;
		const float yaw = at ? yawDegrees : e->Transform.Rotation.y;
		Walker& w = m_Walkers[entity];
		w.Controller.SetPosition(m_Ground, p.x, p.z, yaw / kRadToDeg);
		w.Input = CharacterInput();
		return true;
	}

	void GameSession::Step()
	{
		const float dt = static_cast<float>(m_Clock.Dt);
		for (auto& [name, w] : m_Walkers)
		{
			if (w.Input.Move == glm::vec2(0.0f) && w.Controller.GetSpeed() < 1e-4f) continue;
			w.Controller.Step(w.Input, m_Ground, dt);
			if (Entity* e = m_Entities.Find(m_Entities.FindByName(name)))
			{
				e->Transform.Position = w.Controller.GetPosition();
				e->Transform.Rotation.y = w.Controller.GetYaw() * kRadToDeg;
			}
		}
		m_Gameplay.Update(dt);
		m_Garden.Update(dt);
		++m_Clock.Tick;
		if (m_Clock.DayLengthSeconds > 0.0f)
		{
			m_Clock.Hours += dt * 24.0f / m_Clock.DayLengthSeconds;
			while (m_Clock.Hours >= 24.0f) { m_Clock.Hours -= 24.0f; ++m_Clock.Day; }
		}
	}

	bool GameSession::Command(const std::string& line, std::string& message)
	{
		const std::vector<std::string> a = SplitCommand(line);
		auto fail = [&](const std::string& m) { message = m; return false; };
		if (a.empty()) return fail("empty command");
		const std::string& c = a[0];
		if (c == "garden")
		{
			if (a.size() < 2 || a[1] != "build") return fail("usage: garden build <x> <z>");
			const glm::vec3 origin(a.size() > 2 ? ToFloat(a[2]) : 0.0f, 0.0f, a.size() > 3 ? ToFloat(a[3]) : 0.0f);
			BuildGarden(m_Entities, origin);
			m_Gameplay.Load(m_Entities);
			BindRules();
			m_Garden.SyncVisuals();
			message = "garden built";
			return true;
		}
		if (c == "spawn")
		{
			if (a.size() < 2) return fail("usage: spawn <entity> [x z yaw]");
			if (a.size() >= 4)
			{
				const glm::vec3 p(ToFloat(a[2]), 0.0f, ToFloat(a[3]));
				return Spawn(a[1], &p, a.size() > 4 ? ToFloat(a[4]) : 0.0f, message);
			}
			return Spawn(a[1], nullptr, 0.0f, message);
		}
		if (c == "input")
		{
			if (a.size() < 4) return fail("usage: input <entity> <right> <forward> [run]");
			auto it = m_Walkers.find(a[1]);
			if (it == m_Walkers.end()) return fail("'" + a[1] + "' is not a character (spawn it)");
			it->second.Input.Move = { ToFloat(a[2]), ToFloat(a[3]) };
			it->second.Input.Run = a.size() > 4 && a[4] == "run";
			return true;
		}
		if (c == "activate")
		{
			if (a.size() < 3) return fail("usage: activate <entity> <Ability> [target]");
			AbilitySystemComponent* who = m_Gameplay.Find(a[1]);
			if (!who) return fail("no ability system '" + a[1] + "'");
			AbilitySystemComponent* target = a.size() > 3 ? m_Gameplay.Find(a[3]) : nullptr;
			if (a.size() > 3 && !target) return fail("no ability system '" + a[3] + "'");
			std::string why;
			if (!who->Activate(a[2], target, &why)) return fail(why);
			return true;
		}
		if (c == "fell")
		{
			if (a.size() < 2) return fail("usage: fell <entity>");
			return m_Entities.Destroy(m_Entities.FindByName(a[1])) ? true : fail("no entity '" + a[1] + "'");
		}
		if (c == "add")
		{
			if (a.size() < 5) return fail("usage: add <name> <x> <y> <z>");
			const EntityId id = m_Entities.Create(a[1]);
			Entity* e = m_Entities.Find(id);
			e->Transform.Position = { ToFloat(a[2]), ToFloat(a[3]), ToFloat(a[4]) };
			e->Mesh.emplace();
			return true;
		}
		if (c == "move")
		{
			if (a.size() < 5) return fail("usage: move <entity> <x> <y> <z>");
			Entity* e = m_Entities.Find(m_Entities.FindByName(a[1]));
			if (!e) return fail("no entity '" + a[1] + "'");
			e->Transform.Position = { ToFloat(a[2]), ToFloat(a[3]), ToFloat(a[4]) };
			return true;
		}
		if (c == "time")
		{
			if (a.size() < 2) return fail("usage: time <hours>");
			m_Clock.Hours = std::fmod(std::fabs(ToFloat(a[1])), 24.0f);
			return true;
		}
		if (c == "weather")
		{
			if (a.size() < 2) return fail("usage: weather <preset>");
			m_Weather.Preset = a[1];
			return true;
		}
		return fail("unknown command '" + c + "'");
	}
}
