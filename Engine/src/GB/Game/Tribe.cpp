#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Resources.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		constexpr float kPi = 3.14159265f;
		constexpr float kNearCamp = 5.0f;        // within this of the camp the members trade what they know
		constexpr float kNearFire = 4.5f;        // within this of a lit fire it warms
		constexpr float kCompany = 6.0f;         // within this of another member it is not alone

		std::string Underscored(std::string s)
		{
			std::replace(s.begin(), s.end(), ' ', '_');
			return s;
		}

		const char* AbilityFor(const std::string& kind)
		{
			return kind == "tree" ? "Chop" : kind == "rock" ? "Mine" : kind == "fiber_grass" ? "PullFiber" : "Gather";
		}

		bool IsTool(const ItemLibrary& items, const std::string& id)
		{
			const ItemDef* d = items.Find(id);
			return d && d->Category == "tool";
		}

		int Carried(const ItemLibrary& items, const Inventory& bag)
		{
			int n = 0;
			for (const ItemSlot& s : bag.Stacks()) if (!IsTool(items, s.Id)) n += s.Count;
			return n;
		}

		// The tool of a trade: the role decides (the weights of the options do not).
		void ToolFor(const std::string& role, std::string& tool, std::string& tag)
		{
			tool.clear();
			tag.clear();
			if (role == "woodcutter") { tool = "stone_axe"; tag = "Item.Tool.Axe"; }
			else if (role == "miner") { tool = "stone_pickaxe"; tag = "Item.Tool.Pickaxe"; }
		}

		// A number in 0..1 from a name (FNV-1a): the only "randomness" of the brain, and it is not random.
		float Hash01(const std::string& s, int salt)
		{
			unsigned h = 2166136261u ^ static_cast<unsigned>(salt) * 16777619u;
			for (const char c : s) { h ^= static_cast<unsigned char>(c); h *= 16777619u; }
			return static_cast<float>(h % 10007u) / 10007.0f;
		}

		void Wait(char* buffer, size_t size, const glm::vec2& at, const char* tail)
		{
			std::snprintf(buffer, size, "goto %.2f %.2f | %s", static_cast<double>(at.x), static_cast<double>(at.y), tail);
		}

		EntityId Part(EntityWorld& w, EntityId parent, const std::string& name, MeshShape shape, const glm::vec3& pos, const glm::vec3& scale, const glm::vec3& color)
		{
			const EntityId id = w.Create(name, parent);
			Entity* e = w.Find(id);
			e->Transform.Position = pos;
			e->Transform.Scale = scale;
			e->Mesh.emplace();
			e->Mesh->Shape = shape;
			e->Mesh->Size = 1.0f;
			e->Mesh->Albedo = color;
			e->Mesh->Roughness = 0.9f;
			return id;
		}

		void Factor(UtilityOption& o, const char* name, float value) { o.Factors.push_back({ name, value }); }
	}

	std::map<std::string, std::map<std::string, float>> DefaultRoles()
	{
		return {
			{ "forager",    { { "berry_bush", 1.0f }, { "fiber_grass", 0.9f }, { "tree", 0.25f }, { "rock", 0.2f }, { "fire", 0.0f } } },
			{ "woodcutter", { { "tree", 1.0f }, { "rock", 0.25f }, { "berry_bush", 0.35f }, { "fiber_grass", 0.35f }, { "fire", 1.0f } } },
			{ "miner",      { { "rock", 1.0f }, { "tree", 0.25f }, { "berry_bush", 0.35f }, { "fiber_grass", 0.35f }, { "fire", 0.0f } } },
		};
	}

	Tribe::Tribe() { Config.Roles = DefaultRoles(); }

	void Tribe::Bind(EntityWorld& entities, GameplayWorld& gameplay, const ItemLibrary& items, const RecipeBook& recipes, const std::map<std::string, ResourceNodeDef>& nodes)
	{
		m_Defs = nodes;
		m_Entities = &entities;
		m_Gameplay = &gameplay;
		m_Items = &items;
		m_Recipes = &recipes;
		m_PosCache.clear();
		m_CacheEntities = 0;
		RegisterNeedContent(gameplay.Library);
	}

	void Tribe::AddMember(const std::string& name, const std::string& role, ITribeBody* body)
	{
		TribeMember m;
		m.Name = name;
		m.Role = role;
		m.Body = body;
		m_Members.push_back(m);
		if (m_Gameplay)
			if (AbilitySystemComponent* asc = m_Gameplay->Find(name)) DefineNeeds(*asc);
	}

	void Tribe::Clear()
	{
		m_Members.clear();
		m_Claims.clear();
		m_Log.clear();
		m_Store = Inventory(40, 0.0f);
		m_Time = 0.0;
		m_Clock = 0.0;
		m_ClockSet = false;
		m_Think = 0.0f;
		m_NeedTimer = 0.0f;
		m_Fire = false;
		m_Burning = false;
		m_FireTimer = 0.0f;
		m_Sounds.Clear();
		m_Clan.Clear();
		m_Sights.clear();
		m_SightIndex.clear();
		m_Occluders.clear();
		m_PosCache.clear();
	}

	size_t Tribe::IndexOf(const TribeMember& m) const
	{
		for (size_t i = 0; i < m_Members.size(); ++i) if (&m_Members[i] == &m) return i;
		return 0;
	}

	// The campfire burns wood from the store. Without a sink the stores fill and everyone rests for good.
	void Tribe::Burn(float dt)
	{
		if (!m_Fire) return;
		m_FireTimer += dt;
		if (m_FireTimer >= Config.FireSeconds)
		{
			m_FireTimer -= Config.FireSeconds;
			const bool fed = m_Store.Remove("wood", 1) == 1;
			if (fed != m_Burning)
			{
				m_Burning = fed;
				ShowFlame(fed);
				if (!m_Members.empty()) Say(m_Members.front(), fed ? "the campfire burns again" : "the campfire has no wood and goes out");
			}
		}
	}

	void Tribe::ShowFlame(bool on)
	{
		if (!m_Entities) return;
		for (const char* part : { "Campfire Flame", "Campfire Light" })
			if (const EntityId id = m_Entities->FindByName(part); id != NullEntity) m_Entities->Find(id)->Active = on;
	}

	std::string Tribe::WhyNot(const std::string& kind) const
	{
		const auto def = m_Defs.find(kind);
		if (def != m_Defs.end() && m_Store.Count(def->second.Item) >= Config.StockCap)
			return "the store has enough " + def->second.Item + " (" + std::to_string(m_Store.Count(def->second.Item)) + "/" + std::to_string(Config.StockCap) + ")";
		int total = 0, empty = 0;
		for (const std::string& name : m_Gameplay->Names())
		{
			const AbilitySystemComponent* node = m_Gameplay->Find(name);
			if (!node || !node->HasTag("Resource.Node." + kind)) continue;
			++total;
			if (ResourceSystem::Amount(*node) <= 0) ++empty;
		}
		if (total == 0) return "there are no " + kind + " nodes";
		if (empty == total) return "every " + kind + " is empty (regrowing or gone)";
		return "the " + kind + " nodes left are taken by others";
	}

	// Each worker has its own place around the camp, so they do not wait on each other for the same spot.
	glm::vec2 Tribe::CampSpot(const TribeMember& m) const
	{
		const float angle = static_cast<float>(IndexOf(m)) * 2.0f * kPi / static_cast<float>(std::max<size_t>(1, m_Members.size()));
		return Config.Camp + glm::vec2(std::cos(angle), std::sin(angle)) * 1.8f;
	}

	glm::vec2 Tribe::FireSpot(const TribeMember& m) const
	{
		const float angle = static_cast<float>(IndexOf(m)) * 2.0f * kPi / static_cast<float>(std::max<size_t>(1, m_Members.size()));
		return FirePos() + glm::vec2(std::cos(angle), std::sin(angle)) * 1.3f;
	}

	void Tribe::Say(const TribeMember& m, const std::string& text)
	{
		char t[32];
		std::snprintf(t, sizeof t, "%6.1f ", m_Time);
		m_Log.push_back(t + m.Name + " (" + m.Role + ") " + text);
	}

	void Tribe::Shout(const std::string& who, const glm::vec2& pos, float radius)
	{
		m_Sounds.Emit("scream", who, "", pos, radius, m_Time);
	}

	std::map<std::string, float> Tribe::NeedLevels(const TribeMember& m) const
	{
		std::map<std::string, float> out;
		const AbilitySystemComponent* asc = m_Gameplay ? m_Gameplay->Find(m.Name) : nullptr;
		if (!asc) return out;
		for (const char* n : { NeedName::Hunger, NeedName::Thirst, NeedName::Rest, NeedName::Warmth, NeedName::Safety, NeedName::Company, NeedName::Health, NeedName::Vigor })
			if (asc->Attributes().Has(n)) out[n] = asc->Attribute(n);
		return out;
	}

	std::string Tribe::Because(const TribeMember& m) const
	{
		const AgentMind& mind = m.Mind;
		if (m.Dead) return "dead (" + m.Cause + ")";
		if (mind.Chosen < 0 || static_cast<size_t>(mind.Chosen) >= mind.Options.size()) return "no decision yet";
		const UtilityOption& o = mind.Options[static_cast<size_t>(mind.Chosen)];
		char score[32];
		std::snprintf(score, sizeof score, "%.2f", static_cast<double>(o.Score));
		std::string s = o.Kind + (o.Target.empty() ? "" : " " + o.Target) + " (score " + score + ")";
		if (!o.Note.empty()) s += ": " + o.Note;
		return s;
	}

	// ------------------------------------------------------------------ the senses
	void Tribe::RefreshSights()
	{
		m_Sights.clear();
		m_SightIndex.clear();
		m_Occluders.clear();
		if (m_Entities->Count() != m_CacheEntities) { m_PosCache.clear(); m_CacheEntities = m_Entities->Count(); }
		for (const std::string& name : m_Gameplay->Names())
		{
			const AbilitySystemComponent* asc = m_Gameplay->Find(name);
			if (!asc) continue;
			std::string kind;
			if (asc->HasTag("Resource.Node"))
			{
				for (const auto& [id, def] : m_Defs)
					if (asc->HasTag("Resource.Node." + id)) { kind = id; break; }
			}
			else if (asc->HasTag("Interactive.Well")) kind = "well";
			if (kind.empty()) continue;
			auto cached = m_PosCache.find(name);
			if (cached == m_PosCache.end())
			{
				const EntityId id = m_Entities->FindByName(name);
				if (id == NullEntity) continue;
				const glm::vec4 p = m_Entities->WorldMatrix(id)[3];
				cached = m_PosCache.emplace(name, glm::vec2(p.x, p.z)).first;
			}
			Sight s;
			s.Name = name;
			s.Kind = kind;
			s.Pos = cached->second;
			s.Amount = kind == "well" ? static_cast<int>(asc->Attribute("Water")) : ResourceSystem::Amount(*asc);
			m_SightIndex[name] = m_Sights.size();
			m_Sights.push_back(s);
			if ((kind == "tree" || kind == "rock") && s.Amount > 0) m_Occluders.push_back({ name, s.Pos, kind == "tree" ? 0.6f : 0.9f });
		}
		for (const TribeMember& m : m_Members)
		{
			if (m.Dead || !m.Body) continue;
			Sight s;
			s.Name = m.Name;
			s.Kind = "agent";
			s.Pos = m.Body->Position();
			m_SightIndex[m.Name] = m_Sights.size();
			m_Sights.push_back(s);
		}
	}

	// Turns the head (it follows the way it walks, and scans all round when it stands still) and sees what is in view.
	void Tribe::Look(TribeMember& m)
	{
		AgentMind& mind = m.Mind;
		const glm::vec2 pos = m.Body->Position();
		if (!mind.HasLast)
		{
			mind.LastPos = pos;
			mind.HasLast = true;
			mind.ScanAngle = std::atan2(mind.Facing.y, mind.Facing.x);
		}
		const glm::vec2 moved = pos - mind.LastPos;
		if (glm::length(moved) > 0.2f)
		{
			mind.Facing = glm::normalize(moved);
			mind.ScanAngle = std::atan2(mind.Facing.y, mind.Facing.x);
		}
		else
		{
			mind.ScanAngle += 1.0f;
			if (mind.ScanAngle > kPi) mind.ScanAngle -= 2.0f * kPi;
			mind.Facing = { std::cos(mind.ScanAngle), std::sin(mind.ScanAngle) };
		}
		mind.LastPos = pos;

		mind.Visible.clear();
		for (const Sight& s : m_Sights)
		{
			if (s.Kind == "agent" && s.Name == m.Name) continue;
			if (!CanSee(Config.Vision, pos, mind.Facing, s.Pos, m_Occluders, s.Name)) continue;
			mind.Memory.Observe({ s.Name, s.Kind, s.Pos, s.Amount, m_Time, false });
			if (mind.Visible.size() < 12) mind.Visible.push_back(s.Name);
		}
		// what it remembers here and is no longer there (felled, taken)
		std::vector<std::string> gone;
		for (const auto& [name, e] : mind.Memory.All())
			if (e.Kind != "agent" && m_SightIndex.find(name) == m_SightIndex.end() && InCone(Config.Vision, pos, mind.Facing, e.Pos)) gone.push_back(name);
		for (const std::string& name : gone) mind.Memory.Forget(name);
		mind.Memory.Expire(m_Time, Config.Memory);
	}

	void Tribe::Listen(TribeMember& m)
	{
		AgentMind& mind = m.Mind;
		if (mind.HeardSeq > m_Sounds.LastSeq()) mind.HeardSeq = m_Sounds.LastSeq();   // the bus started over (a loaded game)
		const std::vector<SoundEvent> heard = m_Sounds.Heard(m.Name, m.Body->Position(), Config.Hearing, mind.HeardSeq);
		mind.Heard.clear();
		AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
		for (const SoundEvent& e : heard)
		{
			mind.HeardSeq = std::max(mind.HeardSeq, e.Seq);
			if (e.Kind == "chop" && !e.Subject.empty()) mind.Memory.Observe({ e.Subject, "tree", e.Pos, -1, e.Time, true });
			else if (e.Kind == "steps" || e.Kind == "scream") mind.Memory.Observe({ e.Source, "agent", e.Pos, -1, e.Time, true });
			if (e.Kind == "scream" && asc) SatisfyNeed(*asc, NeedName::Safety, -35.0f);
			if (mind.Heard.size() < 6) mind.Heard.push_back(e.Kind + " from " + e.Source);
		}
	}

	// At the camp the members tell the clan what they know and learn what the others told.
	void Tribe::Share(TribeMember& m)
	{
		if (glm::distance(m.Body->Position(), Config.Camp) > kNearCamp) return;
		m_Clan.Merge(m.Mind.Memory);
		m.Mind.Memory.Merge(m_Clan);
	}

	// ------------------------------------------------------------------ the needs
	void Tribe::StepNeeds(TribeMember& m, float dt)
	{
		AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
		if (!asc || m.Dead) return;
		const glm::vec2 pos = m.Body->Position();
		NeedContext ctx;
		ctx.Time = CurrentSeason();
		ctx.Hour = Hour();
		ctx.Sleeping = m.Job == "sleep";
		ctx.NearFire = FireBurning() && glm::distance(pos, FirePos()) < kNearFire;
		ctx.Sheltered = glm::distance(pos, Config.Camp) < kNearCamp;
		for (const TribeMember& o : m_Members)
			if (&o != &m && !o.Dead && o.Body && glm::distance(pos, o.Body->Position()) < kCompany) ctx.WithOthers = true;
		UpdateNeeds(*asc, Config.Needs, ctx, dt);
		m.Hungry = asc->Attribute(NeedName::Hunger) < 35.0f;
		if (IsDead(*asc)) Die(m);
	}

	void Tribe::Die(TribeMember& m)
	{
		const AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
		m.Dead = true;
		m.Cause = asc ? CauseOfDeath(*asc) : "Unknown";
		for (auto it = m_Claims.begin(); it != m_Claims.end();) it = it->second == m.Name ? m_Claims.erase(it) : std::next(it);
		m.Job = "dead";
		m.Detail.clear();
		std::string error;
		m.Body->Do("wait 1000000", error);
		Say(m, "dies of " + m.Cause);
	}

	// ------------------------------------------------------------------ the loop
	void Tribe::Update(float dt)
	{
		if (!m_Entities || !m_Gameplay || !m_Items || !m_Recipes) return;
		m_Time += static_cast<double>(dt);
		Burn(dt);
		m_NeedTimer += dt;
		if (m_NeedTimer >= Config.NeedSeconds)
		{
			const float step = m_NeedTimer;
			m_NeedTimer = 0.0f;
			for (TribeMember& m : m_Members) StepNeeds(m, step);
		}
		m_Think -= dt;
		if (m_Think > 0.0f) return;
		m_Think = Config.ThinkSeconds;

		RefreshSights();
		m_Sounds.Prune(m_Time);
		for (TribeMember& m : m_Members)   // look, and make the noises of the work
		{
			if (m.Dead || !m.Body) continue;
			const glm::vec2 before = m.Mind.HasLast ? m.Mind.LastPos : m.Body->Position();
			Look(m);
			const glm::vec2 pos = m.Body->Position();
			if (glm::distance(pos, before) > 0.2f) m_Sounds.Emit("steps", m.Name, "", pos, 8.0f, m_Time);
			if (m.Job == "gather")
				if (const MemoryEntry* node = m.Mind.Memory.Find(m.Detail); node && node->Kind == "tree" && glm::distance(pos, node->Pos) < 3.5f)
					m_Sounds.Emit("chop", m.Name, node->Name, node->Pos, 28.0f, m_Time);
		}
		for (TribeMember& m : m_Members)
			if (!m.Dead && m.Body) Listen(m);
		for (TribeMember& m : m_Members)
		{
			if (m.Dead || !m.Body || m.Body->Busy()) continue;
			if (!m.Job.empty()) Finish(m);
			if (m.Job.empty()) Decide(m);
		}
	}

	std::string Tribe::NearestKnown(const TribeMember& m, const std::string& kind, float* distance) const
	{
		std::string best;
		float bestDistance = 1e30f;
		const glm::vec2 from = m.Body->Position();
		for (const MemoryEntry* e : m.Mind.Memory.OfKind(kind))   // in name order: ties keep the first
		{
			if (SpatialMemory::Confidence(*e, m_Time, Config.Memory) <= 0.0f) continue;
			if (auto claim = m_Claims.find(e->Name); claim != m_Claims.end() && claim->second != m.Name) continue;
			const float d = glm::distance(from, e->Pos);
			if (d < bestDistance) { bestDistance = d; best = e->Name; }
		}
		if (distance) *distance = bestDistance;
		return best;
	}

	// Every worker has the tool of its trade: the campfire must not eat the stone a pickaxe still needs.
	bool Tribe::ToolsReady() const
	{
		for (const TribeMember& w : m_Members)
		{
			std::string tool, tag;
			ToolFor(w.Role, tool, tag);
			if (!tag.empty() && !w.Dead && w.Body->Bag().CountTagged(*m_Items, tag) == 0) return false;
		}
		return true;
	}

	bool Tribe::CanCraftTool(const TribeMember& m) const
	{
		std::string tool, tag;
		ToolFor(m.Role, tool, tag);
		return !tool.empty() && m_Recipes->WhyNot(*m_Items, m_Store, tool, "workbench").empty();
	}

	// How much the role likes an option (kinds it does not list get a low default: anyone can help).
	float Tribe::RoleWeight(const TribeMember& m, const std::string& kind) const
	{
		const auto role = Config.Roles.find(m.Role);
		if (role != Config.Roles.end())
			if (const auto w = role->second.find(kind); w != role->second.end()) return w->second;
		return 0.3f;
	}

	// ------------------------------------------------------------------ deciding
	void Tribe::Decide(TribeMember& m)
	{
		AgentMind& mind = m.Mind;
		Inventory& bag = m.Body->Bag();
		const AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
		auto level = [&](const char* need) { return asc && asc->Attributes().Has(need) ? asc->Attribute(need) : 100.0f; };
		const float hunger = level(NeedName::Hunger), thirst = level(NeedName::Thirst), rest = level(NeedName::Rest);
		const float warmth = level(NeedName::Warmth), safety = level(NeedName::Safety), company = level(NeedName::Company);
		const glm::vec2 pos = m.Body->Position();
		const double now = m_Time;
		const bool night = IsNight(Config.Needs, Hour());
		auto weight = [&](const std::string& kind) { return RoleWeight(m, kind); };
		char buffer[160];
		std::vector<UtilityOption> opts;

		std::string tool, toolTag;
		ToolFor(m.Role, tool, toolTag);
		const bool hasTool = tool.empty() || bag.CountTagged(*m_Items, toolTag) > 0;
		const int carried = Carried(*m_Items, bag);
		const int berries = bag.Count("berry"), storeBerries = m_Store.Count("berry");
		const int foodHeld = berries + storeBerries;
		const float hungerUrgency = Urgency(hunger, 60.0f);

		// eat: from the bag, else from the store at the camp
		if (foodHeld > 0)
		{
			UtilityOption o;
			o.Kind = "eat";
			o.Target = berries > 0 ? "bag" : "store";
			o.Where = berries > 0 ? pos : CampSpot(m);
			const float reach = Falloff(glm::distance(pos, o.Where), 60.0f);
			o.Score = 2.2f * hungerUrgency * reach;
			o.Commit = 4.0f;
			Factor(o, "hunger", hungerUrgency);
			Factor(o, "weight", 2.2f);
			Factor(o, "distance", reach);
			std::snprintf(buffer, sizeof buffer, "hunger %.0f/100, %d berries in the %s", static_cast<double>(hunger), berries > 0 ? berries : storeBerries, berries > 0 ? "bag" : "store");
			o.Note = buffer;
			opts.push_back(o);
		}

		// drink: from the bag, else at the nearest well it knows
		{
			const int water = bag.Count("water");
			std::string well;
			float wellDistance = 0.0f;
			if (water == 0) well = NearestKnown(m, "well", &wellDistance);
			if (water > 0 || !well.empty())
			{
				UtilityOption o;
				o.Kind = "drink";
				o.Target = water > 0 ? "bag" : well;
				const MemoryEntry* e = water > 0 ? nullptr : mind.Memory.Find(well);
				o.Where = e ? e->Pos : pos;
				const float u = Urgency(thirst, 60.0f);
				const float reach = Falloff(water > 0 ? 0.0f : wellDistance, 80.0f);
				o.Score = 2.0f * u * reach;
				o.Commit = 4.0f;
				Factor(o, "thirst", u);
				Factor(o, "weight", 2.0f);
				Factor(o, "distance", reach);
				std::snprintf(buffer, sizeof buffer, "thirst %.0f/100, %s", static_cast<double>(thirst), water > 0 ? "water in the bag" : "knows a well");
				o.Note = buffer;
				opts.push_back(o);
			}
		}

		// sleep at the camp: tired, and the night makes it likelier
		{
			UtilityOption o;
			o.Kind = "sleep";
			o.Where = CampSpot(m);
			const float tired = 1.7f * Urgency(rest, 70.0f, 1.2f);
			const float dark = night ? 1.3f * Ramp(95.0f - rest, 0.0f, 40.0f) : 0.0f;
			o.Score = (tired + dark) * Falloff(glm::distance(pos, o.Where), 120.0f);
			o.Commit = 40.0f;
			Factor(o, "tired", tired);
			Factor(o, "night", dark);
			std::snprintf(buffer, sizeof buffer, "rest %.0f/100%s", static_cast<double>(rest), night ? ", it is night" : "");
			o.Note = buffer;
			opts.push_back(o);
		}

		// warm up beside a lit fire
		if (FireBurning())
		{
			UtilityOption o;
			o.Kind = "warm";
			o.Where = FireSpot(m);
			const float u = Urgency(warmth, 50.0f);
			o.Score = 1.5f * u * Falloff(glm::distance(pos, o.Where), 60.0f);
			o.Commit = 10.0f;
			Factor(o, "cold", u);
			std::snprintf(buffer, sizeof buffer, "warmth %.0f/100", static_cast<double>(warmth));
			o.Note = buffer;
			opts.push_back(o);
		}

		// regroup at the camp when frightened
		{
			UtilityOption o;
			o.Kind = "regroup";
			o.Where = CampSpot(m);
			const float u = Urgency(safety, 60.0f);
			o.Score = 1.5f * u;
			o.Commit = 8.0f;
			Factor(o, "fear", u);
			std::snprintf(buffer, sizeof buffer, "safety %.0f/100", static_cast<double>(safety));
			o.Note = buffer;
			opts.push_back(o);
		}

		// socialize: go to the nearest companion it knows of
		if (company < 45.0f)
		{
			std::string friendName;
			float friendDistance = 0.0f;
			for (const MemoryEntry* e : mind.Memory.OfKind("agent"))
			{
				if (e->Name == m.Name || m_Time - e->Seen > 120.0) continue;
				const float d = glm::distance(pos, e->Pos);
				if (friendName.empty() || d < friendDistance) { friendName = e->Name; friendDistance = d; }
			}
			if (!friendName.empty() && friendDistance > kCompany * 0.5f)
			{
				UtilityOption o;
				o.Kind = "socialize";
				o.Target = friendName;
				o.Where = mind.Memory.Find(friendName)->Pos;
				const float u = Urgency(company, 45.0f);
				o.Score = 1.0f * u * Falloff(friendDistance, 40.0f);
				o.Commit = 6.0f;
				Factor(o, "lonely", u);
				std::snprintf(buffer, sizeof buffer, "company %.0f/100", static_cast<double>(company));
				o.Note = buffer;
				opts.push_back(o);
			}
		}

		// the tool of the trade
		if (!hasTool && CanCraftTool(m))
		{
			UtilityOption o;
			o.Kind = "tool";
			o.Target = tool;
			o.Where = CampSpot(m);
			o.Score = 1.25f * Falloff(glm::distance(pos, o.Where), 100.0f);
			Factor(o, "no tool", 1.0f);
			o.Note = "has no " + tool + " and the store can pay for one";
			opts.push_back(o);
		}

		// gather: one option per resource kind, the nearest it remembers
		const Recipe* wanted = !hasTool ? m_Recipes->Find(tool) : nullptr;
		bool lacksFood = foodHeld == 0;
		for (const auto& [kind, def] : m_Defs)
		{
			if (m_Store.Count(def.Item) >= Config.StockCap) continue;   // enough of that for now
			if (!def.RequiresTag.empty() && bag.CountTagged(*m_Items, def.RequiresTag) == 0) continue;
			float distance = 0.0f;
			const std::string node = NearestKnown(m, kind, &distance);
			if (node.empty()) continue;
			const MemoryEntry* e = mind.Memory.Find(node);
			const float conf = SpatialMemory::Confidence(*e, now, Config.Memory);
			float target = 12.0f;
			if (def.Item == "berry") target = std::max(6.0f, static_cast<float>(m_Members.size()) * 3.0f);
			else if (def.Item == "wood") target = 10.0f;
			else if (def.Item == "stone") target = 8.0f;
			const float have = static_cast<float>(m_Store.Count(def.Item) + bag.Count(def.Item));
			const float need = std::clamp(1.0f - have / target, 0.1f, 1.0f);
			const float w = weight(kind);
			const float reach = Falloff(distance, 30.0f);
			UtilityOption o;
			o.Kind = "gather";
			o.Target = node;
			o.Where = e->Pos;
			o.Score = w * (0.4f + 0.7f * need) * reach * conf;
			Factor(o, "role", w);
			Factor(o, "need", need);
			Factor(o, "distance", reach);
			Factor(o, "memory", conf);
			std::snprintf(buffer, sizeof buffer, "%s: the store has %d %s", kind.c_str(), m_Store.Count(def.Item), def.Item.c_str());
			o.Note = buffer;
			// help with what the tool is missing
			if (wanted)
			{
				const auto input = wanted->Inputs.find(def.Item);
				if (input != wanted->Inputs.end() && m_Store.Count(def.Item) < input->second)
				{
					const float boost = 0.7f * reach * conf;
					o.Score += boost;
					Factor(o, "helps with the tool", boost);
					o.Note += "; the " + tool + " lacks it";
				}
			}
			// food when hungry
			if (def.Item == "berry")
			{
				const float food = (lacksFood ? 1.8f : 0.4f) * hungerUrgency * reach * conf;
				o.Score += food;
				Factor(o, "hunger", food);
			}
			opts.push_back(o);
		}

		// carry what it holds to the store
		if (carried > 0)
		{
			UtilityOption o;
			o.Kind = "deposit";
			o.Where = CampSpot(m);
			const float full = carried >= Config.DepositAt ? 1.1f + std::min(0.2f, 0.02f * static_cast<float>(carried - Config.DepositAt)) : 0.25f * Ramp(static_cast<float>(carried), 0.0f, static_cast<float>(Config.DepositAt));
			o.Score = full * Falloff(glm::distance(pos, o.Where), 120.0f);
			Factor(o, "load", full);
			o.Note = std::to_string(carried) + " things in the bag";
			opts.push_back(o);
		}

		// light the campfire once the store can pay for it
		if (weight("fire") > 0.0f && !m_Fire && !tool.empty() && hasTool && ToolsReady() && m_Store.Count("wood") >= 4 && m_Store.Count("stone") >= 3)
		{
			UtilityOption o;
			o.Kind = "fire";
			o.Where = CampSpot(m);
			o.Score = 1.2f * weight("fire");
			Factor(o, "role", weight("fire"));
			o.Note = "the tools are ready and the store has 4 wood and 3 stone";
			opts.push_back(o);
		}

		// explore: nothing it knows of the thing its trade (or its stomach) asks for
		{
			float want = 0.0f;
			std::string what;
			for (const auto& [kind, def] : m_Defs)
			{
				if (m_Store.Count(def.Item) >= Config.StockCap) continue;
				if (!def.RequiresTag.empty() && bag.CountTagged(*m_Items, def.RequiresTag) == 0) continue;
				if (!NearestKnown(m, kind, nullptr).empty()) continue;
				float u = 0.8f * weight(kind);
				if (u < 0.5f) continue;
				if (!mind.Memory.OfKind(kind).empty()) u *= 0.1f;   // it knows where they are, they are only empty for now: it waits (resting is worth 0.1)
				if (u > want) { want = u; what = kind; }
				if (def.Item == "berry" && lacksFood)
				{
					const float starve = 0.5f + 1.2f * hungerUrgency;
					if (starve > want) { want = starve; what = kind; }
				}
			}
			if (want > 0.0f)
			{
				UtilityOption o;
				o.Kind = "explore";
				o.Target = what;
				const float angle = (static_cast<float>(mind.ExploreCount) * 2.3999632f + Hash01(m.Name, 7) * 2.0f * kPi);
				const float radius = Config.ExploreRadius + 8.0f * static_cast<float>(mind.ExploreCount % 4);
				o.Where = Config.Camp + glm::vec2(std::cos(angle), std::sin(angle)) * radius;
				o.Score = want;
				Factor(o, "unknown", want);
				o.Note = "knows no " + what;
				opts.push_back(o);
			}
		}

		// nothing better: stand by (and look around)
		{
			UtilityOption o;
			o.Kind = "rest";
			o.Where = pos;
			o.Score = 0.1f;
			o.Note = "nothing to do";
			opts.push_back(o);
		}

		// choose, and fall back on the next best when the orders cannot be given
		Commitment current = mind.Current;
		int pick = ChooseOption(opts, current, now, Config.Chooser);
		std::vector<UtilityOption> shown = opts;
		for (size_t tries = 0; tries < opts.size() && pick >= 0; ++tries)
		{
			if (Execute(m, opts[static_cast<size_t>(pick)])) break;
			opts[static_cast<size_t>(pick)].Score = -1.0f;
			pick = ChooseOption(opts, current, now, Config.Chooser);
			if (pick >= 0 && opts[static_cast<size_t>(pick)].Score < 0.0f) pick = -1;
		}
		mind.Options = shown;
		mind.Chosen = pick;
		mind.LastDecision = now;
		if (pick >= 0)
		{
			const UtilityOption& o = opts[static_cast<size_t>(pick)];
			if (o.Kind != mind.Current.Kind || o.Target != mind.Current.Target) mind.Current = { o.Kind, o.Target, now, o.Commit };
		}
	}

	bool Tribe::Order(TribeMember& m, const std::string& orders)
	{
		std::string error;
		if (!m.Body->Do(orders, error)) { Say(m, "cannot: " + error); return false; }
		m.Mind.Orders = orders;
		return true;
	}

	bool Tribe::Execute(TribeMember& m, const UtilityOption& o)
	{
		char go[96];
		Inventory& bag = m.Body->Bag();
		if (o.Kind == "eat")
		{
			if (o.Target == "bag") { if (!Order(m, "wait 2")) return false; }
			else { Wait(go, sizeof go, o.Where, "wait 2"); if (!Order(m, go)) return false; }
			m.Job = "eat";
			m.Detail = o.Target;
			return true;
		}
		if (o.Kind == "drink")
		{
			if (o.Target == "bag") { if (!Order(m, "wait 2")) return false; }
			else if (!Order(m, "ability DrawWater " + Underscored(o.Target) + " | wait 1")) return false;
			m.Job = "drink";
			m.Detail = o.Target;
			return true;
		}
		if (o.Kind == "sleep" || o.Kind == "warm" || o.Kind == "regroup" || o.Kind == "socialize")
		{
			const char* tail = o.Kind == "sleep" ? "wait 20" : o.Kind == "warm" ? "wait 10" : o.Kind == "regroup" ? "wait 8" : "wait 6";
			Wait(go, sizeof go, o.Where, tail);
			if (!Order(m, go)) return false;
			m.Job = o.Kind;
			m.Detail = o.Target;
			if (o.Kind == "sleep" && m.Mind.Current.Kind != "sleep") Say(m, "lies down to sleep");
			return true;
		}
		if (o.Kind == "tool")
		{
			Wait(go, sizeof go, o.Where, "wait 1");
			if (!Order(m, go)) return false;
			m.Job = "craft";
			m.Detail = o.Target;
			Say(m, "goes to the camp to make a " + o.Target);
			return true;
		}
		if (o.Kind == "deposit" || o.Kind == "fire")
		{
			Wait(go, sizeof go, o.Where, "wait 1");
			if (!Order(m, go)) return false;
			m.Job = o.Kind;
			if (o.Kind == "deposit") Say(m, "carries " + std::to_string(Carried(*m_Items, bag)) + " things to the store");
			else Say(m, "goes to light the campfire");
			return true;
		}
		if (o.Kind == "explore")
		{
			Wait(go, sizeof go, o.Where, "wait 1");
			if (!Order(m, go)) return false;
			m.Job = "explore";
			m.Detail = o.Target;
			++m.Mind.ExploreCount;
			Say(m, "goes to look for " + o.Target + " at " + std::to_string(static_cast<int>(o.Where.x)) + ", " + std::to_string(static_cast<int>(o.Where.y)));
			return true;
		}
		if (o.Kind == "gather")
		{
			const MemoryEntry* e = m.Mind.Memory.Find(o.Target);
			if (!e) return false;
			const std::string kind = e->Kind;
			const auto def = m_Defs.find(kind);
			const glm::vec2 pos = m.Body->Position();
			const bool fresh = m_Time - e->Seen <= static_cast<double>(Config.FreshSeconds) && e->Amount != 0 && !e->Heard;
			const float gap = glm::distance(pos, e->Pos);
			if (!fresh && gap > 3.5f)
			{
				// Not seen lately: walk up to it first and look, so nothing is planned from an old amount.
				const glm::vec2 away = glm::normalize(pos - e->Pos);
				Wait(go, sizeof go, e->Pos + away * 2.0f, "wait 0.1");
				if (!Order(m, go)) return false;
				m_Claims[o.Target] = m.Name;
				m.Job = "approach";
				m.Detail = o.Target;
				Say(m, "heads for " + o.Target + " to see how it is");
				return true;
			}
			const AbilitySystemComponent* node = m_Gameplay->Find(o.Target);
			if (!node) { m.Mind.Memory.Forget(o.Target); return false; }
			const int amount = ResourceSystem::Amount(*node);
			if (amount <= 0)
			{
				m.Mind.Memory.Observe({ e->Name, e->Kind, e->Pos, 0, m_Time, false });
				return false;
			}
			const int per = def == m_Defs.end() ? 1 : std::max(1, def->second.PerGather);
			const AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
			const float vigor = asc && asc->Attributes().Has(NeedName::Vigor) ? asc->Attribute(NeedName::Vigor) : 1.0f;
			const int most = std::max(1, static_cast<int>(std::lround(static_cast<float>(Config.Swings) * vigor)));
			const int swings = std::max(1, std::min(most, (amount + per - 1) / per));
			const std::string ability = def != m_Defs.end() && !def->second.Ability.empty() ? def->second.Ability : AbilityFor(kind);
			std::string text;
			for (int i = 0; i < swings; ++i)
				text += std::string(i ? " | " : "") + "ability " + ability + " " + Underscored(o.Target) + " | wait 1";
			if (!Order(m, text)) return false;
			m_Claims[o.Target] = m.Name;
			m.Job = "gather";
			m.Detail = o.Target;
			Say(m, "goes to " + o.Target + " (" + ability + ")");
			return true;
		}
		// rest
		{
			const std::string reason = [&]()
			{
				std::string r;
				for (const auto& [kind, def] : m_Defs)
				{
					if (RoleWeight(m, kind) < 0.5f) continue;
					r += (r.empty() ? "" : "; ") + WhyNot(kind);
				}
				return r;
			}();
			if (m.Detail != "nothing to do") Say(m, "has nothing to do and rests: " + reason);
			m.Why = reason;
			m.Job = "rest";
			m.Detail = "nothing to do";
			m.Mind.Orders.clear();
			return true;
		}
	}

	// ------------------------------------------------------------------ what a finished job leaves
	void Tribe::Eat(TribeMember& m)
	{
		AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
		const bool ate = m.Body->Bag().Remove("berry", 1) == 1 || m_Store.Remove("berry", 1) == 1;   // what it carries first
		if (ate)
		{
			++m.Meals;
			if (asc)
			{
				SatisfyNeed(*asc, NeedName::Hunger, static_cast<float>(Config.Needs.FoodHunger));
				SatisfyNeed(*asc, NeedName::Thirst, static_cast<float>(Config.Needs.FoodThirst));
			}
			if (m.Hungry) Say(m, "eats again");
		}
		else if (!m.Hungry) Say(m, "is hungry: there is nothing to eat");
		m.Hungry = !ate;
	}

	void Tribe::Drink(TribeMember& m)
	{
		AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
		if (!asc) return;
		int units = 0;
		while (asc->Attribute(NeedName::Thirst) <= 100.0f - static_cast<float>(Config.Needs.WaterThirst) * 0.7f && m.Body->Bag().Remove("water", 1) == 1)
		{
			SatisfyNeed(*asc, NeedName::Thirst, static_cast<float>(Config.Needs.WaterThirst));
			++units;
		}
		if (units) Say(m, "drinks");
	}

	void Tribe::Finish(TribeMember& m)
	{
		Inventory& bag = m.Body->Bag();
		const std::string job = m.Job;
		m.Job.clear();
		m.Why.clear();
		m.Mind.Orders.clear();
		if (job != "rest" && m.Body->Failed())   // (a rest has no orders: an old failure would count again every half second)
		{
			++m.Failures;
			Say(m, "could not finish " + job + (m.Detail.empty() ? "" : " " + m.Detail));
		}
		if (job == "gather")
		{
			m_Claims.erase(m.Detail);
		}
		else if (job == "approach")
		{
			m_Claims.erase(m.Detail);
			Look(m);   // it is next to the node now: what it sees is what is there
		}
		else if (job == "deposit")
		{
			int moved = 0;
			for (const ItemSlot& s : std::vector<ItemSlot>(bag.Stacks()))
				if (!IsTool(*m_Items, s.Id)) moved += bag.Transfer(*m_Items, m_Store, s.Id, s.Count);
			m.Deposited += moved;
			Say(m, "leaves " + std::to_string(moved) + " in the store");
			Share(m);
		}
		else if (job == "craft")
		{
			if (m_Recipes->Craft(*m_Items, m_Store, m.Detail, "workbench"))
			{
				m_Store.Transfer(*m_Items, bag, m.Detail, 1);
				if (AbilitySystemComponent* asc = m_Gameplay->Find(m.Name)) ResourceSystem::SyncTools(*asc, bag, *m_Items);   // the new tool counts at once
				++m.Made;
				Say(m, "makes a " + m.Detail + " and takes it");
			}
			else Say(m, "cannot make the " + m.Detail + ": " + m_Recipes->WhyNot(*m_Items, m_Store, m.Detail, "workbench"));
			Share(m);
		}
		else if (job == "fire")
		{
			m_Store.Transfer(*m_Items, bag, "wood", 4);
			m_Store.Transfer(*m_Items, bag, "stone", 3);
			if (m_Recipes->Craft(*m_Items, bag, "campfire", ""))
			{
				bag.Remove("campfire", 1);
				BuildFire();
				++m.Made;
				Say(m, "lights the campfire");
			}
			else
			{
				// give back what was taken
				bag.Transfer(*m_Items, m_Store, "wood", 4);
				bag.Transfer(*m_Items, m_Store, "stone", 3);
				Say(m, "cannot build the campfire: " + m_Recipes->WhyNot(*m_Items, bag, "campfire", ""));
			}
		}
		else if (job == "eat")
		{
			Eat(m);
			Share(m);
		}
		else if (job == "drink") Drink(m);
		else if (job == "sleep" || job == "warm" || job == "regroup" || job == "socialize" || job == "explore") Share(m);
		if (job != "rest") m.Detail.clear();
	}

	void Tribe::BuildFire()
	{
		m_Fire = true;
		m_Burning = true;
		m_FireTimer = 0.0f;
		if (!m_Entities) return;
		if (const EntityId old = m_Entities->FindByName("Campfire"); old != NullEntity) m_Entities->Destroy(old);
		const EntityId fire = m_Entities->Create("Campfire");
		m_Entities->Find(fire)->Transform.Position = { Config.Camp.x + 1.5f, Config.CampY, Config.Camp.y };
		for (int i = 0; i < 6; ++i)
		{
			const float a = static_cast<float>(i) * 1.0472f;
			Part(*m_Entities, fire, "Campfire Stone " + std::to_string(i + 1), MeshShape::Sphere, { std::cos(a) * 0.45f, 0.1f, std::sin(a) * 0.45f }, glm::vec3(0.22f), { 0.4f, 0.4f, 0.42f });
		}
		Part(*m_Entities, fire, "Campfire Log", MeshShape::Cube, { 0.0f, 0.12f, 0.0f }, { 0.7f, 0.12f, 0.12f }, { 0.3f, 0.2f, 0.1f });
		const EntityId flame = Part(*m_Entities, fire, "Campfire Flame", MeshShape::Sphere, { 0.0f, 0.4f, 0.0f }, { 0.3f, 0.5f, 0.3f }, { 1.0f, 0.55f, 0.1f });
		m_Entities->Find(flame)->Mesh->Emissive = { 1.6f, 0.7f, 0.1f };
		Entity& light = *m_Entities->Find(m_Entities->Create("Campfire Light", fire));
		light.Transform.Position = { 0.0f, 0.8f, 0.0f };
		light.Light.emplace();
		light.Light->Color = { 1.0f, 0.6f, 0.25f };
		light.Light->Intensity = 12.0f;
		light.Light->Radius = 9.0f;
	}

	std::string Tribe::Report() const
	{
		std::string s = "store:";
		for (const std::string& id : m_Items->Ids())
			if (const int n = m_Store.Count(id)) s += " " + id + " x" + std::to_string(n);
		if (m_Store.Stacks().empty()) s += " empty";
		s += m_Fire ? "; the campfire is lit\n" : "; no campfire yet\n";
		for (const TribeMember& m : m_Members)
		{
			s += m.Name + " (" + m.Role + "): " + (m.Dead ? "dead of " + m.Cause : m.Job.empty() ? "idle" : m.Job + (m.Detail.empty() ? "" : " " + m.Detail))
				+ ", carried " + std::to_string(m.Deposited) + " to the store, made " + std::to_string(m.Made) + ", ate " + std::to_string(m.Meals) + (m.Hungry ? " (hungry)" : "") + (m.Why.empty() ? "" : " [" + m.Why + "]");
			const std::map<std::string, float> needs = NeedLevels(m);
			if (!needs.empty())
			{
				char b[160];
				std::snprintf(b, sizeof b, ", hunger %.0f thirst %.0f rest %.0f warmth %.0f health %.0f", static_cast<double>(needs.at(NeedName::Hunger)), static_cast<double>(needs.at(NeedName::Thirst)),
					static_cast<double>(needs.at(NeedName::Rest)), static_cast<double>(needs.at(NeedName::Warmth)), static_cast<double>(needs.at(NeedName::Health)));
				s += b;
			}
			if (m.Failures) s += ", " + std::to_string(m.Failures) + " failed trips";
			s += "\n";
		}
		return s;
	}

	// ------------------------------------------------------------------ saving
	std::string Tribe::ToJson() const
	{
		Json j;
		j["time"] = m_Time;
		j["clock"] = m_Clock;
		j["clockSet"] = m_ClockSet ? 1 : 0;
		j["think"] = m_Think;
		j["needTimer"] = m_NeedTimer;
		j["fire"] = m_Fire ? 1 : 0;
		j["burning"] = m_Burning ? 1 : 0;
		j["fireTimer"] = m_FireTimer;
		j["camp"] = Json::array({ Config.Camp.x, Config.Camp.y, Config.CampY });
		j["store"] = Json::parse(m_Store.ToJson());
		j["clan"] = Json::parse(m_Clan.ToJson());
		j["soundSeq"] = m_Sounds.LastSeq();
		Json claims = Json::object();
		for (const auto& [node, who] : m_Claims) claims[node] = who;
		j["claims"] = claims;
		Json log = Json::array();
		for (size_t i = m_Log.size() > 200 ? m_Log.size() - 200 : 0; i < m_Log.size(); ++i) log.push_back(m_Log[i]);
		j["log"] = log;
		Json members = Json::array();
		for (const TribeMember& m : m_Members)
			members.push_back(Json{ { "name", m.Name }, { "role", m.Role }, { "job", m.Job }, { "detail", m.Detail }, { "deposited", m.Deposited }, { "made", m.Made },
				{ "failures", m.Failures }, { "why", m.Why }, { "meals", m.Meals }, { "hungry", m.Hungry ? 1 : 0 }, { "dead", m.Dead ? 1 : 0 }, { "cause", m.Cause },
				{ "mind", Json::parse(m.Mind.ToJson()) } });
		j["members"] = members;
		return j.dump();
	}

	std::vector<std::pair<std::string, std::string>> Tribe::SavedMembers(const std::string& json)
	{
		std::vector<std::pair<std::string, std::string>> out;
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object() || !j.contains("members") || !j["members"].is_array()) return out;
		for (const Json& m : j["members"])
			if (m.is_object()) out.emplace_back(m.value("name", std::string()), m.value("role", std::string()));
		return out;
	}

	bool Tribe::FromJson(const std::string& json, bool reissue)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object() || !j.contains("members") || !j["members"].is_array() || !j.contains("store") || !j.contains("clan")) return false;
		Inventory store;
		SpatialMemory clan;
		if (!store.FromJson(j["store"].dump()) || !clan.FromJson(j["clan"].dump())) return false;
		std::vector<AgentMind> minds(m_Members.size());
		std::vector<bool> found(m_Members.size(), false);
		for (const Json& jm : j["members"])
		{
			if (!jm.is_object()) return false;
			for (size_t i = 0; i < m_Members.size(); ++i)
				if (m_Members[i].Name == jm.value("name", std::string()))
				{
					if (!jm.contains("mind") || !minds[i].FromJson(jm["mind"].dump())) return false;
					found[i] = true;
				}
		}
		// all checked: nothing above changed the tribe
		m_Store = store;
		m_Clan = clan;
		m_Time = j.value("time", 0.0);
		m_Clock = j.value("clock", 0.0);
		m_ClockSet = j.value("clockSet", 0) != 0;
		m_Think = j.value("think", 0.0f);
		m_NeedTimer = j.value("needTimer", 0.0f);
		m_Fire = j.value("fire", 0) != 0;
		m_Burning = j.value("burning", 0) != 0;
		m_FireTimer = j.value("fireTimer", 0.0f);
		if (j.contains("camp") && j["camp"].is_array() && j["camp"].size() == 3)
		{
			Config.Camp = { j["camp"][0].get<float>(), j["camp"][1].get<float>() };
			Config.CampY = j["camp"][2].get<float>();
		}
		m_Claims.clear();
		if (j.contains("claims") && j["claims"].is_object())
			for (const auto& [node, who] : j["claims"].items()) if (who.is_string()) m_Claims[node] = who.get<std::string>();
		m_Log.clear();
		if (j.contains("log") && j["log"].is_array())
			for (const Json& l : j["log"]) if (l.is_string()) m_Log.push_back(l.get<std::string>());
		m_Sounds.Clear();
		m_Sounds.SetSeq(j.value("soundSeq", static_cast<uint64_t>(0)));
		for (const Json& jm : j["members"])
			for (size_t i = 0; i < m_Members.size(); ++i)
			{
				TribeMember& m = m_Members[i];
				if (!found[i] || m.Name != jm.value("name", std::string())) continue;
				m.Role = jm.value("role", m.Role);
				m.Job = jm.value("job", std::string());
				m.Detail = jm.value("detail", std::string());
				m.Deposited = jm.value("deposited", 0);
				m.Made = jm.value("made", 0);
				m.Failures = jm.value("failures", 0);
				m.Why = jm.value("why", std::string());
				m.Meals = jm.value("meals", 0);
				m.Hungry = jm.value("hungry", 0) != 0;
				m.Dead = jm.value("dead", 0) != 0;
				m.Cause = jm.value("cause", std::string());
				m.Mind = std::move(minds[i]);
				if (reissue && !m.Dead && m.Body && !m.Job.empty() && m.Job != "rest")
				{
					std::string error;
					if (m.Mind.Orders.empty() || !m.Body->Do(m.Mind.Orders, error)) { m_Claims.erase(m.Detail); m.Job.clear(); m.Mind.Orders.clear(); }
				}
			}
		return true;
	}
}
