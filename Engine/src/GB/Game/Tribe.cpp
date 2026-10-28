#include "GB/Game/Tribe.h"
#include "GB/Gameplay/Resources.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		constexpr float kPi = 3.14159265f;
		constexpr float kNearCamp = 5.0f;        // within this of the camp the members trade what they know
		constexpr float kNearFire = 4.5f;        // within this of a lit fire it warms
		constexpr float kSightCell = 16.0f;      // the cells the sights are filed in
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

		// The tool of a trade: the role's data decides (the weights of the options do not). An unknown role has none.
		void ToolFor(const std::map<std::string, TribeRole>& roles, const std::string& role, std::string& tool, std::string& tag)
		{
			tool.clear();
			tag.clear();
			if (const auto it = roles.find(role); it != roles.end()) { tool = it->second.Tool; tag = it->second.ToolTag; }
		}

		bool IsFuel(const ItemDef& d)
		{
			return std::any_of(d.Tags.begin(), d.Tags.end(), [](const std::string& t) { return t == "Item.Resource.Wood" || t.rfind("Item.Resource.Wood.", 0) == 0; });
		}

		// The piece of fuel Stoke would put in first from this inventory: the most heat (ItemDef::Fuel), then the lowest id; "" = none.
		std::string FuelIn(const ItemLibrary& items, const Inventory& inv)
		{
			std::string best;
			int heat = 0;
			for (const ItemSlot& slot : inv.Stacks())
				if (const ItemDef* d = items.Find(slot.Id); d && slot.Count > 0 && IsFuel(*d))
				{
					const int h = d->Fuel > 0 ? d->Fuel : 2;
					if (best.empty() || h > heat || (h == heat && slot.Id < best)) { best = slot.Id; heat = h; }
				}
			return best;
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

		double Rounded(float v) { return std::round(static_cast<double>(v) * 1000.0) / 1000.0; }   // 0.35f is written as 0.35

		bool HasTagUnder(const ItemDef& d, const std::string& tag)
		{
			return !tag.empty() && std::any_of(d.Tags.begin(), d.Tags.end(), [&](const std::string& t) { return t == tag || (t.size() > tag.size() && t.rfind(tag, 0) == 0 && t[tag.size()] == '.'); });
		}

		// { "night": { "<kind>": factor }, "dusk": { ... }, "day": { ... } } (other keys are left to the caller).
		std::string ReadAgenda(const Json& o, AgendaFactors& out)
		{
			for (const char* part : { "night", "dusk", "day" })
			{
				const auto p = o.find(part);
				if (p == o.end()) continue;
				if (!p->is_object()) return std::string("agenda ") + part + ": expected an object of option -> factor";
				for (const auto& [kind, v] : p->items())
				{
					if (!v.is_number() || v.get<float>() < 0.0f) return std::string("agenda ") + part + ": the factor of " + kind + " must be a number of 0 or more";
					out[part][kind] = v.get<float>();
				}
			}
			return {};
		}

		void WriteAgenda(const AgendaFactors& factors, Json& o)
		{
			for (const auto& [part, kinds] : factors)
			{
				Json k = Json::object();
				for (const auto& [kind, v] : kinds) k[kind] = Rounded(v);
				o[part] = k;
			}
		}

		// "nav": { "maxSlope", "maxWadeDepth", "slopeWeight", "waterWeight", "vegetationWeight", "loadFactor" } over the default walker.
		struct NavField { const char* Key; float NavProfile::*Field; };
		constexpr NavField kNavFields[] = { { "maxSlope", &NavProfile::MaxSlope }, { "maxWadeDepth", &NavProfile::MaxWadeDepth }, { "slopeWeight", &NavProfile::SlopeWeight },
			{ "waterWeight", &NavProfile::WaterWeight }, { "vegetationWeight", &NavProfile::VegetationWeight }, { "loadFactor", &NavProfile::LoadFactor } };
	}

	std::map<std::string, TribeRole> DefaultRoles()
	{
		TribeRole forager;
		forager.Weights = { { "Resource.Plant", 1.0f }, { "fiber_grass", 0.9f }, { "Resource.Plant.Branch", 0.5f }, { "Resource.Loose", 0.6f },
			{ "Resource.Wood", 0.25f }, { "Resource.Stone", 0.2f }, { "fire", 0.0f } };
		forager.Nav.VegetationWeight = 0.8f;   // at home in the undergrowth
		forager.Skill = "foraging";
		TribeRole woodcutter;
		woodcutter.Weights = { { "Resource.Wood", 1.0f }, { "Resource.Plant.Branch", 0.6f }, { "Resource.Plant", 0.35f }, { "Resource.Loose", 0.35f },
			{ "Resource.Stone", 0.25f }, { "fire", 1.0f } };
		woodcutter.Nav.LoadFactor = 2.5f;      // logs are heavy: loaded, it goes round the slopes
		woodcutter.Tool = "stone_axe";
		woodcutter.ToolTag = "Item.Tool.Axe";
		woodcutter.Skill = "woodcutting";
		TribeRole miner;
		miner.Weights = { { "Resource.Stone", 1.0f }, { "Resource.Loose", 0.5f }, { "Resource.Plant", 0.35f }, { "Resource.Wood", 0.25f }, { "fire", 0.0f } };
		miner.Nav.LoadFactor = 3.0f;           // and stone more so
		miner.Tool = "stone_pickaxe";
		miner.ToolTag = "Item.Tool.Pickaxe";
		miner.Skill = "mining";
		return { { "forager", forager }, { "woodcutter", woodcutter }, { "miner", miner } };
	}

	std::string LoadRoles(const std::string& json, std::map<std::string, TribeRole>& roles)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		const auto section = j.find("roles");
		if (section == j.end()) return {};
		if (!section->is_object()) return "roles: expected an object of name -> role";
		std::map<std::string, TribeRole> read;
		for (const auto& [name, o] : section->items())
		{
			if (name.empty() || !o.is_object()) return "role '" + name + "': expected an object";
			TribeRole r;
			if (const auto w = o.find("weights"); w != o.end())
			{
				if (!w->is_object()) return "role '" + name + "': weights must be an object of key -> number";
				for (const auto& [key, v] : w->items())
				{
					if (!v.is_number() || v.get<float>() < 0.0f) return "role '" + name + "': the weight of " + key + " must be a number of 0 or more";
					r.Weights[key] = v.get<float>();
				}
			}
			if (const auto d = o.find("default"); d != o.end())
			{
				if (!d->is_number() || d->get<float>() < 0.0f) return "role '" + name + "': default must be a number of 0 or more";
				r.Default = d->get<float>();
			}
			if (const auto t = o.find("tool"); t != o.end() && t->is_string()) r.Tool = t->get<std::string>();
			if (const auto t = o.find("toolTag"); t != o.end() && t->is_string()) r.ToolTag = t->get<std::string>();
			if (const auto n = o.find("nav"); n != o.end())
			{
				if (!n->is_object()) return "role '" + name + "': nav must be an object";
				for (const auto& [key, v] : n->items())
				{
					const NavField* f = std::find_if(std::begin(kNavFields), std::end(kNavFields), [&](const NavField& x) { return key == x.Key; });
					if (f == std::end(kNavFields)) return "role '" + name + "': nav has no " + key;
					if (!v.is_number() || v.get<float>() < 0.0f) return "role '" + name + "': nav " + key + " must be a number of 0 or more";
					r.Nav.*(f->Field) = v.get<float>();
				}
			}
			if (const auto a = o.find("agenda"); a != o.end())
			{
				if (!a->is_object()) return "role '" + name + "': agenda must be an object";
				if (std::string e = ReadAgenda(*a, r.Agenda); !e.empty()) return "role '" + name + "': " + e;
			}
			if (const auto s = o.find("nightShift"); s != o.end())
			{
				if (!s->is_number() || std::abs(s->get<float>()) > 12.0f) return "role '" + name + "': nightShift must be a number of hours between -12 and 12";
				r.NightShift = s->get<float>();
			}
			if (const auto k = o.find("skill"); k != o.end())
			{
				if (!k->is_string()) return "role '" + name + "': skill must be the name of an aptitude family";
				r.Skill = k->get<std::string>();
			}
			if (const auto f = o.find("fixed"); f != o.end())
			{
				if (!f->is_boolean()) return "role '" + name + "': fixed must be true or false";
				r.Fixed = f->get<bool>();
			}
			if (r.Tool.empty() != r.ToolTag.empty()) return "role '" + name + "': tool and toolTag go together";
			if (!r.ToolTag.empty() && !GameplayTag::IsValidTagName(r.ToolTag)) return "role '" + name + "': bad tool tag " + r.ToolTag;
			read[name] = r;
		}
		for (auto& [name, r] : read) roles[name] = std::move(r);
		return {};
	}

	std::string LoadRolesFile(const std::string& path, std::map<std::string, TribeRole>& roles)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in) return "cannot open " + path;
		std::stringstream ss;
		ss << in.rdbuf();
		const std::string error = LoadRoles(ss.str(), roles);
		return error.empty() ? error : path + ": " + error;
	}

	std::string RolesToJson(const std::map<std::string, TribeRole>& roles)
	{
		Json out = Json::object();
		for (const auto& [name, r] : roles)
		{
			Json o = Json::object();
			Json w = Json::object();
			for (const auto& [key, v] : r.Weights) w[key] = Rounded(v);
			o["weights"] = w;
			if (r.Default != TribeRole{}.Default) o["default"] = Rounded(r.Default);
			if (!r.Tool.empty()) { o["tool"] = r.Tool; o["toolTag"] = r.ToolTag; }
			Json nav = Json::object();
			for (const NavField& f : kNavFields)
				if (r.Nav.*(f.Field) != NavProfile{}.*(f.Field)) nav[f.Key] = Rounded(r.Nav.*(f.Field));
			if (!nav.empty()) o["nav"] = nav;
			if (!r.Agenda.empty()) { Json a = Json::object(); WriteAgenda(r.Agenda, a); o["agenda"] = a; }
			if (r.NightShift != 0.0f) o["nightShift"] = Rounded(r.NightShift);
			if (!r.Skill.empty()) o["skill"] = r.Skill;
			if (r.Fixed) o["fixed"] = true;
			out[name] = o;
		}
		return out.dump(2);
	}

	TribeAgenda DefaultAgenda()
	{
		TribeAgenda a;
		a.Factors["night"] = { { "sleep", 1.6f }, { "gather", 0.4f }, { "explore", 0.4f }, { "tool", 0.4f }, { "fire", 0.4f } };
		a.Factors["dusk"] = { { "socialize", 2.5f }, { "warm", 1.5f }, { "deposit", 1.3f }, { "explore", 0.3f } };
		return a;
	}

	TribeEconomy DefaultEconomy()
	{
		TribeEconomy e;
		e.Stock["berry"] = { 6.0f, 3.0f };
		e.Stock["wood"] = { 10.0f, 0.0f };
		e.Stock["stone"] = { 8.0f, 0.0f };
		return e;
	}

	std::string LoadTribeRules(const std::string& json, TribeConfig& config) { return LoadTribeRules(json, config.Agenda, config.Economy); }

	std::string LoadTribeRules(const std::string& json, TribeAgenda& liveAgenda, TribeEconomy& liveEconomy)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		const auto section = j.find("tribe");
		if (section == j.end()) return {};
		if (!section->is_object()) return "tribe: expected an object";
		const Json& t = *section;
		TribeAgenda agenda = liveAgenda;
		if (const auto a = t.find("agenda"); a != t.end())
		{
			if (!a->is_object()) return "tribe: agenda must be an object";
			agenda = TribeAgenda();
			if (const auto d = a->find("duskHours"); d != a->end())
			{
				if (!d->is_number() || d->get<float>() < 0.0f || d->get<float>() > 12.0f) return "tribe: duskHours must be a number of hours from 0 to 12";
				agenda.DuskHours = d->get<float>();
			}
			if (std::string e = ReadAgenda(*a, agenda.Factors); !e.empty()) return "tribe: " + e;
		}
		TribeEconomy economy = liveEconomy;
		if (t.contains("food") || t.contains("drink") || t.contains("stock"))
		{
			economy = TribeEconomy();
			for (const auto& [key, field] : { std::pair<const char*, std::string*>{ "food", &economy.FoodTag }, std::pair<const char*, std::string*>{ "drink", &economy.DrinkTag } })
				if (const auto f = t.find(key); f != t.end())
				{
					if (!f->is_string() || (!f->get<std::string>().empty() && !GameplayTag::IsValidTagName(f->get<std::string>()))) return std::string("tribe: ") + key + " must be a tag";
					*field = f->get<std::string>();
				}
			if (const auto s = t.find("stock"); s != t.end())
			{
				if (!s->is_object()) return "tribe: stock must be an object of item -> target";
				for (const auto& [item, v] : s->items())
				{
					::GB::StockTarget target;
					if (v.is_number()) target.Min = v.get<float>();
					else if (v.is_object())
					{
						target.Min = v.value("min", 0.0f);
						target.PerMember = v.value("perMember", 0.0f);
					}
					else return "tribe: the stock of " + item + " must be a number or { min, perMember }";
					if (target.Min < 0.0f || target.PerMember < 0.0f || target.Min + target.PerMember <= 0.0f) return "tribe: the stock of " + item + " must be above 0";
					if (item == "default") economy.DefaultStock = target;
					else economy.Stock[item] = target;
				}
			}
		}
		liveAgenda = agenda;
		liveEconomy = economy;
		return {};
	}

	std::string LoadTribeRulesFile(const std::string& path, TribeConfig& config)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in) return "cannot open " + path;
		std::stringstream ss;
		ss << in.rdbuf();
		const std::string error = LoadTribeRules(ss.str(), config);
		return error.empty() ? error : path + ": " + error;
	}

	std::string TribeRulesToJson(const TribeConfig& config)
	{
		Json out = Json::object();
		Json a = Json::object();
		a["duskHours"] = Rounded(config.Agenda.DuskHours);
		WriteAgenda(config.Agenda.Factors, a);
		out["agenda"] = a;
		out["food"] = config.Economy.FoodTag;
		out["drink"] = config.Economy.DrinkTag;
		auto target = [](const ::GB::StockTarget& s) { return s.PerMember == 0.0f ? Json(Rounded(s.Min)) : Json{ { "min", Rounded(s.Min) }, { "perMember", Rounded(s.PerMember) } }; };
		Json stock = Json::object();
		stock["default"] = target(config.Economy.DefaultStock);
		for (const auto& [item, s] : config.Economy.Stock) stock[item] = target(s);
		out["stock"] = stock;
		return out.dump(2);
	}

	Tribe::Tribe()
	{
		Config.Roles = DefaultRoles();
		Config.Agenda = DefaultAgenda();
		Config.Economy = DefaultEconomy();
		Config.Work = DefaultWorkRules();
	}

	// ------------------------------------------------------------------ data: food, stock and the way a member walks
	bool Tribe::IsFood(const std::string& item) const
	{
		const ItemDef* d = m_Items ? m_Items->Find(item) : nullptr;
		return d && HasTagUnder(*d, Config.Economy.FoodTag) && !HasTagUnder(*d, Config.Economy.DrinkTag);
	}

	float Tribe::StockWanted(const std::string& item) const
	{
		return StockFor(item, m_Members.size());
	}

	float Tribe::StockFor(const std::string& item, size_t members) const
	{
		const auto it = Config.Economy.Stock.find(item);
		const ::GB::StockTarget& s = it != Config.Economy.Stock.end() ? it->second : Config.Economy.DefaultStock;
		return std::max(s.Min, static_cast<float>(members) * s.PerMember);
	}

	int Tribe::FoodIn(const Inventory& inv) const
	{
		int n = 0;
		for (const ItemSlot& s : inv.Stacks()) if (IsFood(s.Id)) n += s.Count;
		return n;
	}

	// The food eaten first from a bag or the store: the lowest id (deterministic; with one kind of food it is that one).
	std::string Tribe::FirstFood(const Inventory& inv) const
	{
		std::string best;
		for (const ItemSlot& s : inv.Stacks())
			if (s.Count > 0 && (best.empty() || s.Id < best) && IsFood(s.Id)) best = s.Id;
		return best;
	}

	// The role's profile with the load of the bag: the things carried (not the tools) over FullLoad, or their weight over the bag's limit.
	NavProfile Tribe::Navigation(const TribeMember& m) const
	{
		const auto role = Config.Roles.find(m.Role);
		NavProfile p = role != Config.Roles.end() ? role->second.Nav : NavProfile();
		if (!m.Body || !m_Items) return p;
		Inventory& bag = m.Body->Bag();
		if (bag.MaxWeight() > 0.0f)
		{
			float w = 0.0f;
			for (const ItemSlot& s : bag.Stacks())
				if (const ItemDef* d = m_Items->Find(s.Id); d && d->Category != "tool") w += d->Weight * static_cast<float>(s.Count);
			p.Load = std::clamp(w / bag.MaxWeight(), 0.0f, 1.0f);
		}
		else p.Load = std::clamp(static_cast<float>(Carried(*m_Items, bag)) / std::max(Config.FullLoad, 1.0f), 0.0f, 1.0f);
		return p;
	}

	void Tribe::Steer(TribeMember& m) const
	{
		if (m.Body) m.Body->SetNavigation(Navigation(m));
	}

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
		m_Knowledge.Join(name);   // it knows the technologies of the start (H11)
		m_Society.Join(Config.Society, name);   // a founder: its age, genes and personality (H9, H10); a newborn is in the society already (Born)
		if (body) body->SetGrowth(m_Society.Growth(Config.Society, name));
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
		m_WasBurning = false;
		m_Sounds.Clear();
		m_Clan.Clear();
		m_Sights.clear();
		m_SightIndex.clear();
		m_Occluders.clear();
		m_PosCache.clear();
		m_ByKind.clear();
		m_Runtimes.clear();
		m_Cursor = 0;
		m_UpkeepTimer = m_SpoilTimer = 0.0f;
		m_SightsTime = -1e9;
		m_LodTimer = 0.0f;
		m_OccBuilt.clear();
		m_OccGrid.Clear();
		m_Board.Clear();
		m_BoardTimer = m_ReviewTimer = 0.0f;
		m_Knowledge.Clear();
		m_Explore = ExplorationState();   // (the regions stay: they are the world's)
		m_Society.Clear();   // (the spawner stays: it is the host's)
		m_CampTimer = 0.0f;
		m_FoodBy.clear();
		m_LastWorker.clear();
	}

	size_t Tribe::IndexOf(const TribeMember& m) const
	{
		for (size_t i = 0; i < m_Members.size(); ++i) if (&m_Members[i] == &m) return i;
		return 0;
	}

	// The campfire is the fire pit interactive (H3): the InteractiveSystem of the host burns its Fuel (one unit every BurnSeconds, which the tribe
	// sets to FireSeconds / FireHeat) and the members feed it with Stoke, the same ability and the same fuel rule the player uses. Without a sink
	// the stores fill and everyone rests for good. The tribe only reads its state: warm beside it while it is lit, and say when it goes out.
	bool Tribe::HasFire() const
	{
		const AbilitySystemComponent* a = m_Fire && m_Gameplay ? m_Gameplay->Find(Config.FireName) : nullptr;
		return a && a->HasTag("Interactive.Campfire");
	}

	bool Tribe::FireBurning() const
	{
		return HasFire() && InteractiveSystem::CampfireState(*m_Gameplay->Find(Config.FireName)) == "lit";
	}

	float Tribe::FireFuel() const
	{
		return HasFire() ? m_Gameplay->Find(Config.FireName)->Attributes().Current("Fuel") : 0.0f;
	}

	void Tribe::WatchFire()
	{
		const bool burning = FireBurning();
		if (burning == m_WasBurning || !HasFire()) return;
		m_WasBurning = burning;
		if (!m_Members.empty()) Say(m_Members.front(), burning ? "the campfire burns again" : "the campfire has no wood and goes out");
	}

	std::string Tribe::WhyNot(const std::string& kind) const
	{
		const auto def = m_Defs.find(kind);
		if (def != m_Defs.end() && m_Store.Count(def->second.Item) >= Config.StockCap)
			return "the store has enough " + def->second.Item + " (" + std::to_string(m_Store.Count(def->second.Item)) + "/" + std::to_string(Config.StockCap) + ")";
		int total = 0, empty = 0;
		if (const auto it = m_ByKind.find(kind); it != m_ByKind.end())
			for (const size_t index : it->second)
			{
				++total;
				if (m_Sights[index].Amount <= 0) ++empty;
			}
		if (total == 0) return "there are no " + kind + " nodes";
		if (empty == total) return "every " + kind + " is empty: it waits for them to regrow or looks for more";
		return "the " + kind + " nodes left are taken by others";
	}

	// Each worker has its own place around the camp, so they do not wait on each other for the same spot.
	glm::vec2 Tribe::CampSpot(const TribeMember& m) const
	{
		if (m.HasSpot) return m.Spot;   // chosen once (a path search) and kept until a trip to it fails
		const float angle = static_cast<float>(IndexOf(m)) * 2.0f * kPi / static_cast<float>(std::max<size_t>(1, m_Members.size())) + static_cast<float>(m.SpotTries) * 0.9f;
		m.Spot = StandingSpot(m, Config.Camp, angle, 1.8f + 0.6f * static_cast<float>(m.SpotTries % 4));
		m.HasSpot = true;
		return m.Spot;
	}

	// A place around `centre` the body can be sent to: its own angle first, then turning and moving out until one is not a wall (a tree, deep water).
	glm::vec2 Tribe::StandingSpot(const TribeMember& m, const glm::vec2& centre, float angle, float radius, bool path) const
	{
		for (int ring = 0; ring < 4; ++ring)
			for (int k = 0; k < 12; ++k)
			{
				const float a = angle + static_cast<float>(k) * 0.5236f;
				const glm::vec2 p = centre + glm::vec2(std::cos(a), std::sin(a)) * (radius + 1.2f * static_cast<float>(ring));
				if (!m.Body || (path ? m.Body->CanReach(p) : m.Body->CanStandAt(p))) return p;
			}
		return centre + glm::vec2(std::cos(angle), std::sin(angle)) * radius;
	}

	glm::vec2 Tribe::FireSpot(const TribeMember& m) const
	{
		const float angle = static_cast<float>(IndexOf(m)) * 2.0f * kPi / static_cast<float>(std::max<size_t>(1, m_Members.size()));
		return StandingSpot(m, FirePos(), angle, 1.3f, false);
	}

	void Tribe::Say(const TribeMember& m, const std::string& text)
	{
		char t[32];
		std::snprintf(t, sizeof t, "%6.1f ", m_Time);
		m_Log.push_back(t + m.Name + " (" + m.Role + ") " + text);
		if (static_cast<int>(m_Log.size()) > Config.LogLines + Config.LogLines / 4)   // a ring: the oldest quarter goes at once
			m_Log.erase(m_Log.begin(), m_Log.begin() + static_cast<std::ptrdiff_t>(m_Log.size() - static_cast<size_t>(Config.LogLines)));
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

	// ------------------------------------------------------------------ full or abstract
	bool Tribe::IsBusy(const TribeMember& m) const { return m.Abstract ? m.Agent.Busy() : m.Body->Busy(); }
	bool Tribe::HasFailed(const TribeMember& m) const { return m.Abstract ? m.Agent.Failed() : m.Body->Failed(); }
	std::string Tribe::WhyFailed(const TribeMember& m) const { return m.Abstract ? m.Agent.Error() : m.Body->Error(); }
	glm::vec2 Tribe::Where(const TribeMember& m) const { return m.Abstract ? m.Agent.Position() : m.Body->Position(); }

	bool Tribe::Run(TribeMember& m, const std::string& orders, std::string& error)
	{
		return m.Abstract ? m.Agent.Do(orders, error) : m.Body->Do(orders, error);
	}

	// Full -> abstract: the agent takes over where the body stands, with the same orders; abstract -> full: the body is put where the agent is and
	// given the orders again (from the start of the current job). The decision is the same either way; only who walks is different.
	void Tribe::SetDetail(TribeMember& m, bool full)
	{
		if (m.Abstract == !full) return;
		AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
		std::string error;
		if (!full)
		{
			m.Agent.Place(m.Body->Position());
			m.Body->SetSuspended(true);
			if (asc) { m_Runtimes[m.Name] = asc->Runtime(); asc->SetRuntime(nullptr); asc->CancelAll(); }
			m.Abstract = true;
			if (!m.Mind.Orders.empty() && !m.Job.empty() && m.Job != "dead") m.Agent.Do(m.Mind.Orders, error);
		}
		else
		{
			glm::vec2 at = m.Agent.Position();
			if (!m.Body->CanStandAt(at)) at = m.Agent.SafePosition();   // (between two waypoints of a route it always can; this is for a world that changed under it)
			m.Abstract = false;
			if (asc) { asc->CancelAll(); if (const auto r = m_Runtimes.find(m.Name); r != m_Runtimes.end()) asc->SetRuntime(r->second); }
			m.Body->Teleport(at);
			m.Body->SetSuspended(false);
			if (!m.Mind.Orders.empty() && !m.Job.empty() && m.Job != "dead" && !m.Body->Do(m.Mind.Orders, error)) m.Job.clear();
		}
	}

	// Moves the abstract members: each walks its orders with the speed its vigor allows.
	void Tribe::StepAbstract(float dt)
	{
		for (TribeMember& m : m_Members)
		{
			if (!m.Abstract || m.Dead) continue;
			AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
			const float vigor = asc && asc->Attributes().Has(NeedName::Vigor) ? asc->Attribute(NeedName::Vigor) : 1.0f;
			if (asc) ResourceSystem::SyncTools(*asc, m.Body->Bag(), *m_Items);
			ITribeBody* body = m.Body;
			m.Agent.Update(dt, asc, *m_Gameplay, [this](const std::string& entity, glm::vec2& pos)
			{
				const auto it = m_PosCache.find(entity);
				if (it == m_PosCache.end()) return false;
				pos = it->second;
				return true;
			}, 3.0f * std::max(0.3f, vigor), [body](const glm::vec2& from, const glm::vec2& to, float reach, std::vector<glm::vec2>& points)
			{
				return body->Route(from, to, reach, points);   // the way the body itself would go (its grid and its profile): round the lake, not through it
			});
		}
	}

	bool Tribe::RemoveMember(const std::string& name)
	{
		for (size_t i = 0; i < m_Members.size(); ++i)
		{
			if (m_Members[i].Name != name) continue;
			if (m_Members[i].Abstract) SetDetail(m_Members[i], true);
			for (auto it = m_Claims.begin(); it != m_Claims.end();) it = it->second == name ? m_Claims.erase(it) : std::next(it);
			m_Board.Release(name);
			m_Members.erase(m_Members.begin() + static_cast<std::ptrdiff_t>(i));
			m_Knowledge.Leave(name);
			m_Society.Leave(name);
			if (m_Cursor > i) --m_Cursor;
			return true;
		}
		return false;
	}

	// The agenda of the day (data: TribeAgenda, and a role's own part of it): the night is for sleeping, the dusk for coming home, being together and keeping
	// warm, and the work is for the day. Dusk and night come earlier in autumn and winter, and a role with a NightShift lives its whole day that many hours
	// later. Needs outweigh it (an agenda factor only scales an option; the default agenda does not touch eating and drinking).
	float Tribe::AgendaFactor(const std::string& roleName, const std::string& kind, float hour, Season season) const
	{
		const auto role = roleName.empty() ? Config.Roles.end() : Config.Roles.find(roleName);
		const TribeRole* r = role != Config.Roles.end() ? &role->second : nullptr;
		if (r && r->NightShift != 0.0f) { hour = std::fmod(hour - r->NightShift, 24.0f); if (hour < 0.0f) hour += 24.0f; }
		const float shift = season == Season::Winter ? Config.DuskShift : season == Season::Autumn ? Config.DuskShift * 0.5f : 0.0f;
		const float dusk = Config.Needs.NightFrom - Config.Agenda.DuskHours - shift, night = Config.Needs.NightFrom - shift;
		const bool inNight = hour >= night || hour < Config.Needs.NightTo;
		const char* part = inNight ? "night" : hour >= dusk ? "dusk" : "day";
		auto lookup = [&](const AgendaFactors& factors, float& out)
		{
			const auto p = factors.find(part);
			if (p == factors.end()) return false;
			const auto k = p->second.find(kind);
			if (k == p->second.end()) return false;
			out = k->second;
			return true;
		};
		float f = 1.0f;
		if (r && lookup(r->Agenda, f)) return f;
		lookup(Config.Agenda.Factors, f);
		return f;
	}

	// What the camp uses up: thatch and rope (fibre) and the berries that spoil. Without sinks the stores fill and everyone rests for good.
	void Tribe::Upkeep(float dt)
	{
		int alive = 0;
		for (const TribeMember& m : m_Members) if (!m.Dead) ++alive;
		m_UpkeepTimer += dt;
		if (m_UpkeepTimer >= Config.UpkeepSeconds)
		{
			m_UpkeepTimer -= Config.UpkeepSeconds;
			m_Store.Remove("fiber", std::min(alive, std::max(0, m_Store.Count("fiber") - 12)));   // only the surplus: the tools still need their share
		}
		m_SpoilTimer += dt;
		if (m_SpoilTimer >= Config.SpoilSeconds)
		{
			m_SpoilTimer -= Config.SpoilSeconds;
			if (FoodIn(m_Store) > 20)   // the food there is most of goes off first (ties: the lowest id)
			{
				std::string most;
				for (const ItemSlot& s : m_Store.Stacks())
					if (IsFood(s.Id) && (most.empty() || m_Store.Count(s.Id) > m_Store.Count(most) || (m_Store.Count(s.Id) == m_Store.Count(most) && s.Id < most))) most = s.Id;
				m_Store.Remove(most, 1);
			}
		}
	}

	// ------------------------------------------------------------------ the senses
	void Tribe::RefreshSights()
	{
		m_Sights.clear();
		m_SightIndex.clear();
		m_ByKind.clear();
		m_Occluders.clear();
		m_SightsTime = m_Time;
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
			m_ByKind[kind].push_back(m_Sights.size());
			m_Sights.push_back(s);
			if ((kind == "tree" || kind == "rock") && s.Amount > 0) m_Occluders.push_back({ name, s.Pos, kind == "tree" ? 0.6f : 0.9f });
		}
		bool same = m_Occluders.size() == m_OccBuilt.size();
		for (size_t i = 0; same && i < m_Occluders.size(); ++i) same = m_Occluders[i].Id == m_OccBuilt[i].Id;
		if (!same) { m_OccGrid.Build(m_Occluders); m_OccBuilt = m_Occluders; }   // trees and rocks stay where they are: only felled ones change the set
		for (const TribeMember& m : m_Members)
		{
			if (m.Dead || !m.Body) continue;
			Sight s;
			s.Name = m.Name;
			s.Kind = "agent";
			s.Pos = Where(m);
			m_SightIndex[m.Name] = m_Sights.size();
			m_Sights.push_back(s);
		}
		m_SightCells.clear();
		for (size_t i = 0; i < m_Sights.size(); ++i)
			m_SightCells[(static_cast<long long>(static_cast<int>(std::floor(m_Sights[i].Pos.x / kSightCell))) << 32) ^ static_cast<long long>(static_cast<unsigned>(static_cast<int>(std::floor(m_Sights[i].Pos.y / kSightCell))))].push_back(i);
	}

	// Turns the head (it follows the way it walks, and scans all round when it stands still) and sees what is in view.
	void Tribe::Look(TribeMember& m)
	{
		AgentMind& mind = m.Mind;
		const glm::vec2 pos = Where(m);
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
		const float reach = std::max(Config.Vision.Range, Config.Vision.NearRange);
		const int cx0 = static_cast<int>(std::floor((pos.x - reach) / kSightCell)), cx1 = static_cast<int>(std::floor((pos.x + reach) / kSightCell));
		const int cz0 = static_cast<int>(std::floor((pos.y - reach) / kSightCell)), cz1 = static_cast<int>(std::floor((pos.y + reach) / kSightCell));
		for (int cx = cx0; cx <= cx1; ++cx)
			for (int cz = cz0; cz <= cz1; ++cz)
		{
			const auto cell = m_SightCells.find((static_cast<long long>(cx) << 32) ^ static_cast<long long>(static_cast<unsigned>(cz)));
			if (cell == m_SightCells.end()) continue;
			for (const size_t index : cell->second)
		{
			const Sight& s = m_Sights[index];
			if (s.Kind == "agent" && s.Name == m.Name) continue;
			if (!CanSeeIn(Config.Vision, pos, mind.Facing, s.Pos, m_OccGrid, s.Name)) continue;
			if (!mind.Memory.Refresh(s.Name, s.Pos, s.Amount, m_Time)) mind.Memory.Observe({ s.Name, s.Kind, s.Pos, s.Amount, m_Time, false });
			if (mind.Visible.size() < 12) mind.Visible.push_back(s.Name);
		}
		}
		// what it remembers here and is no longer there (felled, taken)
		std::vector<std::string> gone;
		for (const auto& [name, e] : mind.Memory.All())
			if (e.Kind != "agent" && InCone(Config.Vision, pos, mind.Facing, e.Pos) && m_SightIndex.find(name) == m_SightIndex.end()) gone.push_back(name);
		for (const std::string& name : gone) mind.Memory.Forget(name);
		for (const std::string& name : gone) if (m.Gone.size() < 64) m.Gone.push_back(name);   // H12: the clan forgets them too when it tells it at the camp
		if (m_Time - mind.ExpiredAt >= 10.0) { mind.Memory.Expire(m_Time, Config.Memory); mind.ExpiredAt = m_Time; }
	}

	void Tribe::Listen(TribeMember& m)
	{
		AgentMind& mind = m.Mind;
		if (mind.HeardSeq > m_Sounds.LastSeq()) mind.HeardSeq = m_Sounds.LastSeq();   // the bus started over (a loaded game)
		const std::vector<SoundEvent> heard = m_Sounds.Heard(m.Name, Where(m), Config.Hearing, mind.HeardSeq);
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
		if (glm::distance(Where(m), Config.Camp) > kNearCamp) return;
		Tell(m_Knowledge.Share(m.Name));   // what it has found out (H11): a few names, cheap on every visit
		if (m_Time - m.Mind.SharedAt < static_cast<double>(Config.ShareSeconds)) return;   // (merging whole memories is not cheap: not on every visit)
		m.Mind.SharedAt = m_Time;
		for (const std::string& name : m.Gone) m_Clan.Forget(name);   // what it found gone (an older sighting the clan kept would send others there again)
		m.Gone.clear();
		m_Clan.Merge(m.Mind.Memory);
		m.Mind.Memory.Merge(m_Clan);
		ShareAtlas(m);   // H12: the ground it saw joins the clan's map
	}

	// ------------------------------------------------------------------ the needs
	void Tribe::StepNeeds(TribeMember& m, float dt)
	{
		AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
		if (!asc || m.Dead || !m.Body) return;
		const glm::vec2 pos = Where(m);
		NeedContext ctx;
		ctx.Time = CurrentSeason();
		ctx.Hour = Hour();
		ctx.Sleeping = m.Job == "sleep";
		ctx.NearFire = FireBurning() && glm::distance(pos, FirePos()) < kNearFire;
		ctx.Sheltered = glm::distance(pos, Config.Camp) < kNearCamp;
		for (const TribeMember& o : m_Members)
			if (&o != &m && !o.Dead && o.Body && glm::distance(pos, Where(o)) < kCompany) ctx.WithOthers = true;
		UpdateNeeds(*asc, Config.Needs, ctx, dt);
		m.Hungry = asc->Attribute(NeedName::Hunger) < 35.0f;
		if (IsDead(*asc)) Die(m);
	}

	void Tribe::Die(TribeMember& m, const std::string& cause)
	{
		const AbilitySystemComponent* asc = m_Gameplay->Find(m.Name);
		m.Dead = true;
		m.Cause = !cause.empty() ? cause : asc ? CauseOfDeath(*asc) : "Unknown";
		for (auto it = m_Claims.begin(); it != m_Claims.end();) it = it->second == m.Name ? m_Claims.erase(it) : std::next(it);
		m_Board.Release(m.Name);
		m.Job = "dead";
		m.Detail.clear();
		std::string error;
		Run(m, "wait 1000000", error);
		Say(m, "dies of " + m.Cause);
		SocietyEvents(m_Society.Died(Config.Society, m.Name, m.Cause));   // its partner is free and the clan mourns (H9, H10)
	}

	// ------------------------------------------------------------------ the loop
	void Tribe::Update(float dt)
	{
		if (!m_Entities || !m_Gameplay || !m_Items || !m_Recipes) return;
		m_Time += static_cast<double>(dt);
		WatchFire();
		Upkeep(dt);
		m_NeedTimer += dt;
		if (m_NeedTimer >= Config.NeedSeconds)
		{
			const float step = m_NeedTimer;
			m_NeedTimer = 0.0f;
			for (TribeMember& m : m_Members)
			{
				StepNeeds(m, step);
				if (!m.Dead) DecaySkills(m.Skills, Config.Work.Skills, step, Config.Needs.DaySeconds);   // what it does not practise it slowly forgets
			}
		}
		StepSociety(dt);   // H9 + H10: the days of the society (ages, deaths, pairs, births, culture) and the bonds made at the camp
		// H7: the clan posts what it needs on the job board, and now and then reviews who does which trade
		m_BoardTimer += dt;
		if (m_BoardTimer >= Config.Work.Jobs.Every) { m_BoardTimer = 0.0f; PostJobs(); }
		m_ReviewTimer += dt;
		if (m_ReviewTimer >= Config.Work.Roles.Review) { m_ReviewTimer = 0.0f; ReviewRoles(); }
		StepExploration(dt);   // H12: what the members have in view, the expedition and the home (TribeExplore.cpp)
		StepAbstract(dt);
		if (m_Knowledge.Tick(dt)) Tell(m_Knowledge.Lesson(KnowledgeView(true)));   // a lesson at the camp, and what the clan forgets (H11)
		m_LodTimer += dt;
		if (m_LodTimer >= Config.ThinkSeconds)   // the level of detail follows the viewer
		{
			m_LodTimer = 0.0f;
			for (TribeMember& m : m_Members)
			{
				if (m.Dead || !m.Body) continue;
				if (m_HasViewer && Config.DetailRadius > 0.0f)
				{
					const float d = glm::distance(m_Viewer, Where(m));
					if (!m.Abstract && d > Config.DetailRadius) SetDetail(m, false);
					else if (m.Abstract && d < Config.DetailRadius - Config.DetailMargin) SetDetail(m, true);
				}
				else if (m.Abstract) SetDetail(m, true);
			}
		}

		// Each member has its own turn every ThinkSeconds; the turns of one call are limited, so with many members the work is spread over frames.
		const size_t n = m_Members.size();
		if (n == 0) return;
		const int needed = static_cast<int>(std::ceil(static_cast<float>(n) * dt / std::max(Config.ThinkSeconds, 0.01f))) + 1;
		int budget = std::max(Config.ThinkPerFrame, needed);
		bool refreshed = false;
		size_t last = m_Cursor % n;
		for (size_t k = 0; k < n && budget > 0; ++k)
		{
			const size_t i = (m_Cursor + k) % n;
			TribeMember& m = m_Members[i];
			if (m.Dead || !m.Body) continue;
			if (m_Time < m.Mind.NextThink && !(!m.Job.empty() && !IsBusy(m))) continue;   // its turn, or its orders just ended
			if (!refreshed && m_Time - m_SightsTime >= static_cast<double>(Config.ThinkSeconds) * 0.9)
			{
				RefreshSights();
			}
			if (!refreshed) { m_Sounds.Prune(m_Time); refreshed = true; }
			--budget;
			last = (i + 1) % n;

			const glm::vec2 before = m.Mind.HasLast ? m.Mind.LastPos : Where(m);
			Look(m);
			const glm::vec2 pos = Where(m);
			if (glm::distance(pos, before) > 0.2f) m_Sounds.Emit("steps", m.Name, "", pos, 8.0f, m_Time);   // the noises of the work
			if (m.Job == "gather")
				if (const MemoryEntry* node = m.Mind.Memory.Find(m.Detail); node && node->Kind == "tree" && glm::distance(pos, node->Pos) < 3.5f)
					m_Sounds.Emit("chop", m.Name, node->Name, node->Pos, 28.0f, m_Time);
			Listen(m);
			if (!IsBusy(m))
			{
				if (!m.Job.empty()) Finish(m);
				if (m.Job.empty()) Decide(m);
			}
			m.Mind.NextThink = m_Time + static_cast<double>(IsBusy(m) ? Config.BusyThinkSeconds * (m.Abstract ? 4.0f : 1.0f) : m.Job == "rest" ? Config.IdleThinkSeconds : Config.ThinkSeconds);
		}
		m_Cursor = last;
	}

	// What the member remembers, in one pass: for each kind the nearest place worth going to (trusted, not claimed by another) and whether it knows any at all.
	std::map<std::string, Tribe::Nearest> Tribe::ScanMemory(const TribeMember& m) const
	{
		std::map<std::string, Nearest> out;
		const glm::vec2 from = Where(m);
		for (const auto& [name, e] : m.Mind.Memory.All())   // in name order: ties keep the first
		{
			if (e.Kind == "agent") continue;
			Nearest& n = out[e.Kind];
			n.Any = true;
			const float d = glm::distance(from, e.Pos);
			if (d >= n.Distance) continue;
			if (SpatialMemory::Confidence(e, m_Time, Config.Memory) <= 0.0f) continue;
			if (auto claim = m_Claims.find(name); claim != m_Claims.end() && claim->second != m.Name) continue;
			n.Distance = d;
			n.Name = name;
		}
		return out;
	}

	// Every worker has the tool of its trade: the campfire must not eat the stone a pickaxe still needs.
	bool Tribe::ToolsReady() const
	{
		for (const TribeMember& w : m_Members)
		{
			std::string tool, tag;
			ToolFor(Config.Roles, w.Role, tool, tag);
			if (!tag.empty() && !w.Dead && w.Body && w.Body->Bag().CountTagged(*m_Items, tag) == 0) return false;
		}
		return true;
	}

	bool Tribe::CanCraftTool(const TribeMember& m) const
	{
		std::string tool, tag;
		ToolFor(Config.Roles, m.Role, tool, tag);
		return !tool.empty() && m_Knowledge.CanMake(m.Name, tool) && m_Recipes->WhyNot(*m_Items, m_Store, tool, "workbench").empty();
	}

	// ------------------------------------------------------------------ knowledge (H11): the logic is in GB/Game/Knowledge; these are the hooks
	std::vector<KnowledgeAgent> Tribe::KnowledgeView(bool lesson) const
	{
		std::vector<KnowledgeAgent> out;
		out.reserve(m_Members.size());
		for (const TribeMember& m : m_Members)
		{
			KnowledgeAgent a;
			a.Name = m.Name;
			a.Alive = !m.Dead && m.Body;
			a.Pos = a.Alive ? Where(m) : glm::vec2(0.0f);
			a.AtCamp = a.Alive && glm::distance(a.Pos, Config.Camp) <= kNearCamp * 2.0f;
			a.Idle = m.Job.empty() || m.Job == "rest" || m.Job == "warm" || m.Job == "socialize" || m.Job == "regroup" || m.Job == "eat" || m.Job == "experiment";
			if (lesson && a.Alive && a.AtCamp)   // H9 + H10: a child learns faster, an elder teaches better, and each would rather learn from its parents
			{
				a.Learn = m_Society.LearnRate(Config.Society, m.Name, std::string());
				a.Teach = m_Society.TeachRate(Config.Society, m.Name);
				a.Mentors = m_Society.Mentors(Config.Society, m.Name);
			}
			out.push_back(a);
		}
		return out;
	}

	void Tribe::Tell(const std::vector<KnowledgeEvent>& events)
	{
		for (const KnowledgeEvent& e : events)
		{
			if (!e.Other.empty()) Relate(e.Who, e.Other, "teach");   // a lesson brings teacher and learner closer (H10)
			if (e.Text.rfind("learns ", 0) == 0 || e.Text.rfind("discovers ", 0) == 0) ClanEvent("learn");
			const auto m = std::find_if(m_Members.begin(), m_Members.end(), [&](const TribeMember& x) { return x.Name == e.Who; });
			if (m != m_Members.end()) { Say(*m, e.Text); continue; }
			TribeMember clan;   // the clan itself speaks
			clan.Name = "clan";
			clan.Role = "all";
			Say(clan, e.Text);
		}
	}

	void Tribe::Witnessed(const TribeMember& m, const std::string& recipe, const std::string& node)
	{
		if (!m_Knowledge.Gated()) return;
		const std::vector<KnowledgeAgent> view = KnowledgeView();
		if (!recipe.empty()) Tell(m_Knowledge.WitnessRecipe(m.Name, recipe, Where(m), view));
		if (!node.empty()) Tell(m_Knowledge.WitnessNode(m.Name, node, Where(m), view));
	}

	// How much the role likes an option: by the node's id, then by its kind from the most specific up ("Resource.Stone.Flint", "Resource.Stone"),
	// else by the option's name ("fire"); what it does not list gets the role's default (anyone can help). An unknown role gives 0.3 to everything.
	float Tribe::RoleWeight(const std::string& name, const std::string& kind) const
	{
		const auto role = Config.Roles.find(name);
		if (role == Config.Roles.end()) return TribeRole{}.Default;
		const std::map<std::string, float>& weights = role->second.Weights;
		if (const auto w = weights.find(kind); w != weights.end()) return w->second;
		if (const auto def = m_Defs.find(kind); def != m_Defs.end())
			for (std::string tag = ResourceKindTag(def->second); tag.find('.') != std::string::npos; tag.resize(tag.rfind('.')))
				if (const auto w = weights.find(tag); w != weights.end()) return w->second;
		return role->second.Default;
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
		const glm::vec2 pos = Where(m);
		const double now = m_Time;
		const bool night = IsNight(Config.Needs, Hour());
		auto weight = [&](const std::string& kind) { return RoleWeight(m, kind); };
		char buffer[160];
		std::vector<UtilityOption> opts;
		const std::map<std::string, Nearest> known = ScanMemory(m);
		ClaimJob(m, known);   // H7: the job of the board worth most to it now (it adds the "job" factor below)
		auto nearest = [&](const std::string& kind, float* distance) -> std::string
		{
			const auto it = known.find(kind);
			if (it == known.end()) return {};
			if (distance) *distance = it->second.Distance;
			return it->second.Name;
		};

		std::string tool, toolTag;
		ToolFor(Config.Roles, m.Role, tool, toolTag);
		Steer(m);   // the places it weighs below are checked with the way it walks now (CanReach)
		const bool hasTool = tool.empty() || bag.CountTagged(*m_Items, toolTag) > 0;
		const int carried = Carried(*m_Items, bag);
		const int berries = FoodIn(bag), storeBerries = FoodIn(m_Store);   // food (TribeEconomy): berries in era 1
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
			std::snprintf(buffer, sizeof buffer, "hunger %.0f/100, %d food in the %s", static_cast<double>(hunger), berries > 0 ? berries : storeBerries, berries > 0 ? "bag" : "store");
			o.Note = buffer;
			opts.push_back(o);
		}

		// drink: from the bag, else at the nearest well it knows
		{
			const int water = bag.Count("water");
			std::string well;
			float wellDistance = 0.0f;
			if (water == 0) well = nearest("well", &wellDistance);
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
			if (!m_Knowledge.CanGather(m.Name, kind)) continue;   // a node only those who know a technology work (H11)
			float distance = 0.0f;
			const std::string node = nearest(kind, &distance);
			if (node.empty()) continue;
			const MemoryEntry* e = mind.Memory.Find(node);
			const float conf = SpatialMemory::Confidence(*e, now, Config.Memory);
			const float target = StockWanted(def.Item);   // data (TribeEconomy::Stock)
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
			if (IsFood(def.Item))
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
		if (weight("fire") > 0.0f && !HasFire() && !tool.empty() && hasTool && ToolsReady() && m_Store.Count("wood") >= 4 && m_Store.Count("stone") >= 3)
		{
			UtilityOption o;
			o.Kind = "fire";
			o.Where = CampSpot(m);
			o.Score = 1.2f * weight("fire");
			Factor(o, "role", weight("fire"));
			o.Note = "the tools are ready and the store has 4 wood and 3 stone";
			opts.push_back(o);
		}

		// feed the campfire before it goes out: a piece of wood from the store (or the bag) and Stoke, as the player does
		if (weight("fire") > 0.0f && HasFire() && FireFuel() <= Config.StokeBelow)
		{
			const auto claim = m_Claims.find(Config.FireName);
			const bool can = asc && std::find(asc->Granted().begin(), asc->Granted().end(), "Stoke") != asc->Granted().end();
			if (can && (claim == m_Claims.end() || claim->second == m.Name) && (!FuelIn(*m_Items, bag).empty() || !FuelIn(*m_Items, m_Store).empty()))
			{
				UtilityOption o;
				o.Kind = "stoke";
				o.Target = Config.FireName;
				o.Where = CampSpot(m);
				const float low = FireBurning() ? 1.0f : 1.3f;   // out already: more pressing
				o.Score = 1.2f * low * weight("fire") * Falloff(glm::distance(pos, o.Where), 120.0f);
				Factor(o, "fire low", low);
				Factor(o, "role", weight("fire"));
				std::snprintf(buffer, sizeof buffer, "the campfire has %.1f fuel", static_cast<double>(FireFuel()));
				o.Note = buffer;
				opts.push_back(o);
			}
		}

		// explore: nothing it knows of the thing its trade (or its stomach) asks for
		{
			float want = 0.0f;
			std::string what;
			for (const auto& [kind, def] : m_Defs)
			{
				if (m_Store.Count(def.Item) >= Config.StockCap) continue;
				if (!def.RequiresTag.empty() && bag.CountTagged(*m_Items, def.RequiresTag) == 0) continue;
				if (!m_Knowledge.CanGather(m.Name, kind)) continue;
				if (!nearest(kind, nullptr).empty()) continue;
				float u = 0.8f * weight(kind);
				if (u < 0.5f) continue;
				if (const auto it = known.find(kind); it != known.end() && it->second.Any) u *= 0.2f;   // it knows where they are, they are only empty for now: it only goes looking for more when it has nothing better
				if (u > want) { want = u; what = kind; }
				if (lacksFood && IsFood(def.Item))
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
				for (int k = 1; k < 8 && !m.Body->CanReach(o.Where); ++k)   // not into a lake or a cliff: try the next angle
					o.Where = Config.Camp + glm::vec2(std::cos(angle + 0.7f * static_cast<float>(k)), std::sin(angle + 0.7f * static_cast<float>(k))) * radius;
				o.Score = want;
				Factor(o, "unknown", want);
				o.Note = "knows no " + what;
				opts.push_back(o);
			}
		}

		// experiment (H11, E5): the materials of a recipe of a technology it does not know yet are at hand (store or bag): it tries to work it out at the camp
		{
			std::string recipe;
			if (const std::string tech = m_Knowledge.NextExperiment(m.Name, now, m_Store, bag, *m_Recipes, &recipe); !tech.empty())
			{
				UtilityOption o;
				o.Kind = "experiment";
				o.Target = tech;
				o.Where = CampSpot(m);
				const float reach = Falloff(glm::distance(pos, o.Where), 60.0f);
				// curiosity, or need: the technology would let it make the tool of its trade, which it lacks (then it is worth as much as making it)
				const TechDef* def = m_Knowledge.Techs()->Find(tech);
				const bool trade = !hasTool && def && std::find(def->Recipes.begin(), def->Recipes.end(), tool) != def->Recipes.end();
				const float want = trade ? 1.1f : 0.2f;
				o.Score = want * reach;
				Factor(o, trade ? "needs it for its tool" : "curiosity", want);
				Factor(o, "distance", reach);
				o.Note = "has what the " + (recipe.empty() ? tech : recipe) + " needs and does not know " + m_Knowledge.Name(tech);
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

		// the agenda of the day (and of the season, and of its role) scales what it would do
		const float hourNow = Hour();
		for (UtilityOption& o : opts)
		{
			const float f = AgendaFactor(m.Role, o.Kind, hourNow, CurrentSeason());
			if (f == 1.0f) continue;
			o.Score *= f;
			Factor(o, "agenda", f);
		}
		ExploreOptions(m, opts);   // H12: going on with an expedition, walking to the new camp
		JobBonus(m, opts);   // H7: what does the job it claimed is worth more (a factor on the options, not a new chooser)
		SocietyFactors(m, opts);   // H9 + H10: its genes, personality, stage of life and the clan's culture (and the companion it likes best)

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
		Steer(m);   // the load may have changed since it decided (a deposit is on its way back empty)
		std::string error;
		if (!Run(m, orders, error)) { Say(m, "cannot: " + error); return false; }
		m.Mind.Orders = orders;
		return true;
	}

	bool Tribe::Execute(TribeMember& m, const UtilityOption& o)
	{
		char go[96];
		Inventory& bag = m.Body->Bag();
		if (o.Kind == "expedition" || o.Kind == "migrate") return ExecuteExplore(m, o);   // H12 (TribeExplore.cpp)
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
		if (o.Kind == "stoke")
		{
			// it carries a piece of wood from the store unless it has one already (the hottest, as Stoke takes them)
			std::string fuel = FuelIn(*m_Items, bag);
			if (fuel.empty())
			{
				fuel = FuelIn(*m_Items, m_Store);
				if (fuel.empty() || m_Store.Transfer(*m_Items, bag, fuel, 1) != 1) return false;
			}
			Wait(go, sizeof go, o.Where, ("wait 1 | ability Stoke " + Underscored(Config.FireName) + " | wait 1").c_str());
			if (!Order(m, go)) return false;
			m_Claims[Config.FireName] = m.Name;
			m.Job = "stoke";
			m.Detail = Config.FireName;
			Say(m, "takes " + fuel + " to feed the campfire");
			return true;
		}
		if (o.Kind == "experiment")
		{
			const TechDef* tech = m_Knowledge.Techs() ? m_Knowledge.Techs()->Find(o.Target) : nullptr;
			if (!tech) return false;
			Wait(go, sizeof go, o.Where, ("wait " + std::to_string(std::max(1, static_cast<int>(std::lround(tech->Discovery.Seconds))))).c_str());
			if (!Order(m, go)) return false;
			m.Job = "experiment";
			m.Detail = o.Target;
			Say(m, "sits down at the camp to work out " + m_Knowledge.Name(o.Target));
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
			const glm::vec2 pos = Where(m);
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
			// H7: its aptitude for the family of the node gives more gathers per trip and less time between them (data: SkillRules Yield, Speed)
			const std::string family = SkillFamilyOf(kind);
			const float skill = SkillEffect(m.Skills, Config.Work.Skills, family, IsProfession(m, family));
			const int most = std::max(1, static_cast<int>(std::lround(static_cast<float>(Config.Swings) * vigor * (1.0f + Config.Work.Skills.Yield * skill))));
			const int swings = std::max(1, std::min(most, (amount + per - 1) / per));
			const std::string ability = def != m_Defs.end() && !def->second.Ability.empty() ? def->second.Ability : AbilityFor(kind);
			char pause[32] = "wait 1";
			const float pace = WorkPace(m, family);   // H9: children and elders work slower, strong arms faster (genes)
			if (skill > 0.0f || pace != 1.0f) std::snprintf(pause, sizeof pause, "wait %.2f", 1.0 / ((1.0 + static_cast<double>(Config.Work.Skills.Speed * skill)) * static_cast<double>(pace)));
			std::string text;
			for (int i = 0; i < swings; ++i)
				text += std::string(i ? " | " : "") + "ability " + ability + " " + Underscored(o.Target) + " | " + pause;
			if (!Order(m, text)) return false;
			m.WorkKind = kind;
			m.WorkStart = def != m_Defs.end() ? bag.Count(def->second.Item) : 0;
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
		const std::string carried = FirstFood(m.Body->Bag()), stored = carried.empty() ? FirstFood(m_Store) : std::string();
		const bool ate = (!carried.empty() && m.Body->Bag().Remove(carried, 1) == 1) || (!stored.empty() && m_Store.Remove(stored, 1) == 1);   // what it carries first
		if (ate)
		{
			++m.Meals;
			if (asc)
			{
				SatisfyNeed(*asc, NeedName::Hunger, static_cast<float>(Config.Needs.FoodHunger));
				SatisfyNeed(*asc, NeedName::Thirst, static_cast<float>(Config.Needs.FoodThirst));
			}
			if (m.Hungry) Say(m, "eats again");
			if (carried.empty()) SharedFood(m);   // what another brought (H10)
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
		const std::string reason = job != "rest" && HasFailed(m) ? WhyFailed(m) : std::string();
		if (!reason.empty() && job == "gather" && reason.find("Depleted") != std::string::npos)   // someone (or the rabbits) emptied it on the way: not a fault
		{
			if (const MemoryEntry* e = m.Mind.Memory.Find(m.Detail)) m.Mind.Memory.Observe({ e->Name, e->Kind, e->Pos, 0, m_Time, false });
			Say(m, "finds " + m.Detail + " empty");
			Quarrel(m, m.Detail);   // someone else took it (H10)
		}
		else if (!reason.empty() && job == "approach")   // cannot get next to it (trees and bushes in the way): treat it as empty for a while instead of trying again at once
		{
			if (const MemoryEntry* e = m.Mind.Memory.Find(m.Detail)) m.Mind.Memory.Observe({ e->Name, e->Kind, e->Pos, 0, m_Time, false });
			Say(m, "cannot get next to " + m.Detail + ": " + reason);
		}
		else if (!reason.empty() && job == "explore") Say(m, "cannot get to " + m.Detail + " ground: " + reason);   // a lake or a cliff in the way: it tries another way next time
		else if (job != "rest" && HasFailed(m))   // (a rest has no orders: an old failure would count again every half second)
		{
			++m.Failures;
			if (job != "gather") { m.HasSpot = false; ++m.SpotTries; }   // the place it was sent to cannot be reached: choose another next time
			Say(m, "could not finish " + job + (m.Detail.empty() ? "" : " " + m.Detail) + (WhyFailed(m).empty() ? "" : " (" + WhyFailed(m) + ")"));
		}
		if (job == "gather")
		{
			m_Claims.erase(m.Detail);
			// what it gathered on this trip is what it practised (H7)
			if (const auto def = m_Defs.find(m.WorkKind); def != m_Defs.end())
			{
				const int gathered = bag.Count(def->second.Item) - m.WorkStart;
				if (gathered > 0)
				{
					Practise(m, "gather", m.WorkKind, static_cast<float>(gathered));
					m_LastWorker[m.Detail] = m.Name;
					Worked(m);   // side by side with others: closer to them (H10)
					if (const Job* held = m_Board.ClaimOf(m.Name); held && held->Kind == "gather" && held->Target == def->second.Item) m_Board.Progress(m.Name, 0.0f, m_Time);
				}
			}
			m.WorkKind.clear();
			m.WorkStart = 0;
			if (reason.empty())
				if (const MemoryEntry* e = m.Mind.Memory.Find(m.Detail)) Witnessed(m, std::string(), e->Kind);   // others see how it works a node of a technology
		}
		else if (job == "experiment")
		{
			if (!HasFailed(m))
			{
				Tell(m_Knowledge.Experiment(m.Name, m_Time, m_Store, bag, *m_Recipes));
				Practise(m, "craft", "craft", 1.0f);   // trying a recipe is crafting practice (H7)
			}
			Share(m);
		}
		else if (job == "stoke")
		{
			m_Claims.erase(Config.FireName);
			if (!HasFailed(m))
			{
				Say(m, "feeds the campfire (fuel " + std::to_string(static_cast<int>(std::lround(FireFuel()))) + ")");
				ClanEvent("stoke");
				Practise(m, "stoke", "stoke", 1.0f);
				if (const Job* held = m_Board.ClaimOf(m.Name); held && held->Kind == "stoke") m_Board.Progress(m.Name, 1.0f, m_Time);
			}
		}
		else if (job == "approach")
		{
			m_Claims.erase(m.Detail);
			Look(m);   // it is next to the node now: what it sees is what is there
		}
		else if (job == "deposit")
		{
			int moved = 0;
			const Job* held = m_Board.ClaimOf(m.Name);
			const std::string jobItem = held && held->Kind == "gather" ? held->Target : std::string();
			for (const ItemSlot& s : std::vector<ItemSlot>(bag.Stacks()))
				if (!IsTool(*m_Items, s.Id))
				{
					const int n = bag.Transfer(*m_Items, m_Store, s.Id, s.Count);
					moved += n;
					if (n > 0 && s.Id == jobItem) m_Board.Progress(m.Name, static_cast<float>(n), m_Time);   // delivered: the claim is alive
					if (n > 0 && IsFood(s.Id)) m_FoodBy = m.Name;   // who feeds the others (H10)
				}
			m.Deposited += moved;
			Say(m, "leaves " + std::to_string(moved) + " in the store");
			Share(m);
		}
		else if (job == "craft")
		{
			const std::set<std::string> known = m_Knowledge.KnownRecipes(m.Name, *m_Recipes);   // H11: only what it knows how to make
			const std::set<std::string>* gate = m_Knowledge.Gated() ? &known : nullptr;
			if (m_Recipes->Craft(*m_Items, m_Store, m.Detail, "workbench", gate))
			{
				Witnessed(m, m.Detail, std::string());
				m_Store.Transfer(*m_Items, bag, m.Detail, 1);
				if (AbilitySystemComponent* asc = m_Gameplay->Find(m.Name)) ResourceSystem::SyncTools(*asc, bag, *m_Items);   // the new tool counts at once
				++m.Made;
				Say(m, "makes a " + m.Detail + " and takes it");
				ClanEvent("craft");
				Practise(m, "craft", "craft", 1.0f);
				if (const Job* held = m_Board.ClaimOf(m.Name); held && held->Kind == "craft") { m_Board.Progress(m.Name, 1.0f, m_Time); m_Board.Release(m.Name); }
			}
			else Say(m, "cannot make the " + m.Detail + ": " + m_Recipes->WhyNot(*m_Items, m_Store, m.Detail, "workbench", gate));
			Share(m);
		}
		else if (job == "fire")
		{
			m_Store.Transfer(*m_Items, bag, "wood", 4);
			m_Store.Transfer(*m_Items, bag, "stone", 3);
			if (m_Knowledge.CanMake(m.Name, "campfire") && m_Recipes->Craft(*m_Items, bag, "campfire", ""))
			{
				Witnessed(m, "campfire", std::string());
				bag.Remove("campfire", 1);
				BuildFire(Config.FireHeat);   // the kit's wood is the first fuel: it burns FireSeconds
				++m.Made;
				Say(m, "lights the campfire");
				ClanEvent("fire");
				Practise(m, "fire", "fire", 1.0f);
				if (const Job* held = m_Board.ClaimOf(m.Name); held && held->Kind == "fire") { m_Board.Progress(m.Name, 1.0f, m_Time); m_Board.Release(m.Name); }
			}
			else
			{
				// give back what was taken
				bag.Transfer(*m_Items, m_Store, "wood", 4);
				bag.Transfer(*m_Items, m_Store, "stone", 3);
				Say(m, "cannot build the campfire: " + (m_Knowledge.CanMake(m.Name, "campfire") ? m_Recipes->WhyNot(*m_Items, bag, "campfire", "") : "does not know how"));
			}
		}
		else if (job == "eat")
		{
			Eat(m);
			Share(m);
		}
		else if (job == "drink") Drink(m);
		else if (job == "expedition" || job == "migrate") FinishExplore(m, job);   // H12 (TribeExplore.cpp)
		else if (job == "sleep" || job == "warm" || job == "regroup" || job == "socialize" || job == "explore") Share(m);
		if (job != "rest") m.Detail.clear();
	}

	// The campfire is the fire pit of the interactives (Config.FireDef), built at the camp under the name Config.FireName, lit with `fuel` and burning
	// at the tribe's pace. Its ability system is made here (a scene loaded later brings it back from its component); the host's InteractiveSystem
	// picks it up on its next update. An older one of the same name goes (the visual-only campfire of saves from before the interactive).
	void Tribe::BuildFire(float fuel)
	{
		m_Fire = true;
		if (!m_Entities || !m_Gameplay) return;
		InteractiveDef def = DefaultInteractives().at("fire_pit");
		if (m_InteractiveDefs)
			if (const auto it = m_InteractiveDefs->find(Config.FireDef); it != m_InteractiveDefs->end() && it->second.Kind == "campfire") def = it->second;
		if (const EntityId old = m_Entities->FindByName(Config.FireName); old != NullEntity) m_Entities->Destroy(old);
		const EntityId id = BuildInteractive(*m_Entities, def, Config.FireName, NullEntity, { FirePos().x, Config.CampY, FirePos().y });
		Entity& e = *m_Entities->Find(id);
		const bool lit = fuel >= 1.0f;
		GameplayComponent& g = *e.Gameplay;
		g.Tags.erase(std::remove_if(g.Tags.begin(), g.Tags.end(), [](const std::string& t) { return t == "State.Unlit" || t == "State.Lit" || t == "State.Out"; }), g.Tags.end());
		g.Tags.push_back(lit ? "State.Lit" : "State.Out");
		for (GameplayAttribute& a : g.Attributes) if (a.Name == "Fuel") a.Value = std::min(std::max(fuel, a.Min), a.Max);
		g.Attributes.push_back({ "BurnSeconds", Config.FireSeconds / std::max(Config.FireHeat, 0.01f), 0.0f, 1e6f });
		for (const char* part : { " Flame", " Light" })
			if (const EntityId p = m_Entities->FindByName(e.Name + part); p != NullEntity) m_Entities->Find(p)->Active = lit;
		AbilitySystemComponent* a = m_Gameplay->Find(e.Name);   // reused when there is one: the interactives keep their listeners on it
		if (!a) a = m_Gameplay->Create(e.Name);
		a->FromComponent(g);
		m_WasBurning = lit;
	}

	std::string Tribe::Report() const
	{
		std::string s = "store:";
		for (const std::string& id : m_Items->Ids())
			if (const int n = m_Store.Count(id)) s += " " + id + " x" + std::to_string(n);
		if (m_Store.Stacks().empty()) s += " empty";
		s += !HasFire() ? "; no campfire yet\n" : FireBurning() ? "; the campfire is lit\n" : "; the campfire is out\n";
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
			if (!m.Skills.empty())   // H7: its aptitudes, the best first
			{
				std::vector<std::pair<std::string, float>> skills(m.Skills.begin(), m.Skills.end());
				std::stable_sort(skills.begin(), skills.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
				s += ", skills";
				for (const auto& [family, level] : skills) s += " " + family + " " + std::to_string(static_cast<int>(level));
			}
			if (const Job* held = m_Board.ClaimOf(m.Name)) s += ", job: " + held->Id;
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
		j["upkeep"] = m_UpkeepTimer;
		j["spoil"] = m_SpoilTimer;
		j["cursor"] = m_Cursor;
		j["sights"] = m_SightsTime;
		j["needTimer"] = m_NeedTimer;
		j["fire"] = m_Fire ? 1 : 0;   // its state is the interactive's (tags and attributes, saved with the gameplay)
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
		j["board"] = Json::parse(m_Board.ToJson());   // H7 (an older save has none: the board starts empty and the skills at 0)
		j["boardTimer"] = m_BoardTimer;
		j["reviewTimer"] = m_ReviewTimer;
		j["explore"] = Json::parse(m_Explore.ToJson());   // H12 (an older save has none: the clan knows no region and has sent nobody)
		j["society"] = Json::parse(m_Society.ToJson());   // H9 + H10 (an older save has none: the members are founders with their ages and genes from their names)
		j["campTimer"] = m_CampTimer;
		j["foodBy"] = m_FoodBy;
		Json workers = Json::object();
		for (const auto& [node, who] : m_LastWorker) workers[node] = who;
		j["lastWorker"] = workers;
		Json members = Json::array();
		for (const TribeMember& m : m_Members)
			members.push_back(Json{ { "name", m.Name }, { "role", m.Role }, { "job", m.Job }, { "detail", m.Detail }, { "deposited", m.Deposited }, { "made", m.Made },
				{ "failures", m.Failures }, { "why", m.Why }, { "meals", m.Meals }, { "hungry", m.Hungry ? 1 : 0 }, { "dead", m.Dead ? 1 : 0 }, { "cause", m.Cause },
				{ "mind", Json::parse(m.Mind.ToJson()) }, { "skills", Json::parse(SkillsToJson(m.Skills)) }, { "workKind", m.WorkKind }, { "workStart", m.WorkStart } });
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
		JobBoard board;
		if (j.contains("board") && !board.FromJson(j["board"].dump())) return false;
		ExplorationState explore;
		if (j.contains("explore") && !explore.FromJson(j["explore"].dump())) return false;
		Society society = m_Society;   // (an older save keeps the founders AddMember made)
		if (j.contains("society") && !society.FromJson(j["society"].dump())) return false;
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
		m_UpkeepTimer = j.value("upkeep", 0.0f);
		m_SpoilTimer = j.value("spoil", 0.0f);
		m_Cursor = j.value("cursor", static_cast<size_t>(0));
		m_SightsTime = j.value("sights", -1e9);
		m_NeedTimer = j.value("needTimer", 0.0f);
		m_Board = std::move(board);
		m_Explore = std::move(explore);
		m_BoardTimer = j.value("boardTimer", 0.0f);
		m_ReviewTimer = j.value("reviewTimer", 0.0f);
		m_Society = std::move(society);
		m_CampTimer = j.value("campTimer", 0.0f);
		m_FoodBy = j.value("foodBy", std::string());
		m_LastWorker.clear();
		if (j.contains("lastWorker") && j["lastWorker"].is_object())
			for (const auto& [node, who] : j["lastWorker"].items()) if (who.is_string()) m_LastWorker[node] = who.get<std::string>();
		m_Fire = j.value("fire", 0) != 0;
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
		// A save from before the campfire was an interactive kept its own burning state: the fire pit is built from it, with the heat it had left
		// (what was put in and not burnt, and the rest of the current burn when it was burning).
		if (m_Fire && j.contains("burning") && m_Gameplay && !m_Gameplay->Find(Config.FireName))
		{
			const bool burning = j.value("burning", 0) != 0;
			const float timer = j.value("fireTimer", 0.0f), heat = j.value("fireHeat", 0.0f);
			const float left = heat + (burning ? Config.FireHeat * std::max(0.0f, 1.0f - timer / std::max(Config.FireSeconds, 0.01f)) : 0.0f);
			BuildFire(std::ceil(left - 0.001f));
		}
		m_WasBurning = FireBurning();
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
				m.Skills = jm.contains("skills") ? SkillsFromJson(jm["skills"].dump()) : SkillSet();
				m.WorkKind = jm.value("workKind", std::string());
				m.WorkStart = jm.value("workStart", 0);
				if (m.Body) m.Body->SetGrowth(m_Society.Growth(Config.Society, m.Name));   // a child's actor comes back at its size (H9)
				if (reissue && !m.Dead && m.Body && !m.Job.empty() && m.Job != "rest")
				{
					std::string error;
					if (m.Mind.Orders.empty() || !Run(m, m.Mind.Orders, error)) { m_Claims.erase(m.Detail); m.Job.clear(); m.Mind.Orders.clear(); }
				}
			}
		return true;
	}
}
