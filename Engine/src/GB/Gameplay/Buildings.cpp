#include "GB/Gameplay/Buildings.h"
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
		constexpr const char* kSite = "Building.Site";
		constexpr const char* kDone = "Building.Done";
		constexpr const char* kReady = "Building.Ready";
		constexpr const char* kKindPrefix = "Building.Kind.";
		const char* const kParts[] = { "slab", "posts", "walls", "roof", "table", "rack" };

		double Round(float v) { return std::round(static_cast<double>(v) * 1000.0) / 1000.0; }   // 0.35f is written as 0.35

		std::string Delivered(const std::string& item) { return "Delivered." + item; }

		bool KnownPart(const std::string& p) { return std::any_of(std::begin(kParts), std::end(kParts), [&](const char* k) { return p == k; }); }

		void SetTag(AbilitySystemComponent& a, const char* tag, bool on)
		{
			const bool has = a.Tags().HasExact(tag);
			if (on && !has) a.Tags().Add(tag);
			else if (!on && has) a.Tags().RemoveAll(tag);
		}

		int PhaseOf(const AbilitySystemComponent& a) { return static_cast<int>(std::lround(a.Attributes().Base("Phase"))); }

		// The current phase's materials as attributes (Delivered.<item>, 0 of what it needs); those of an earlier phase stay at 0 so the set only grows.
		void StartPhase(AbilitySystemComponent& a, const BuildingDef& def, int phase)
		{
			for (const AttributeSet::Attribute& at : std::vector<AttributeSet::Attribute>(a.Attributes().All()))
				if (at.Name.rfind("Delivered.", 0) == 0) a.Attributes().SetBase(at.Name, 0.0f);
			a.Attributes().Define("Phase", static_cast<float>(phase), 0.0f, static_cast<float>(def.Phases.size()));
			a.Attributes().SetBase("Phase", static_cast<float>(phase));
			a.Attributes().Define("Work", 0.0f, 0.0f, 1e6f);
			a.Attributes().SetBase("Work", 0.0f);
			const bool done = phase >= static_cast<int>(def.Phases.size());
			if (!done)
				for (const auto& [item, n] : def.Phases[static_cast<size_t>(phase)].Needs)
					if (!a.Attributes().Has(Delivered(item))) a.Attributes().Define(Delivered(item), 0.0f, 0.0f, 1e6f);
			SetTag(a, kSite, !done);
			SetTag(a, kDone, done);
			SetTag(a, kReady, !done && ReadSite(a, def).Missing.empty());
		}

		// One primitive under `parent`, in the parent's space.
		EntityId Part(EntityWorld& w, EntityId parent, const std::string& name, const glm::vec3& pos, const glm::vec3& scale, const glm::vec3& color)
		{
			const EntityId id = w.Create(name, parent);
			Entity* e = w.Find(id);
			e->Transform.Position = pos;
			e->Transform.Scale = scale;
			e->Mesh.emplace();
			e->Mesh->Shape = MeshShape::Cube;
			e->Mesh->Size = 1.0f;
			e->Mesh->Albedo = color;
			e->Mesh->Roughness = 0.9f;
			return id;
		}

		// The pieces of one phase's look, under its group (which grows in height with the phase's progress). Heights are from the ground.
		void BuildPart(EntityWorld& w, EntityId group, const std::string& n, const std::string& part, const glm::vec2& f, const glm::vec3& c)
		{
			const float hx = 0.5f * f.x, hz = 0.5f * f.y;
			const glm::vec3 dark = c * 0.75f, light = glm::min(c * 1.3f, glm::vec3(1.0f));
			if (part == "slab") Part(w, group, n + " Slab", { 0.0f, 0.1f, 0.0f }, { f.x, 0.2f, f.y }, glm::vec3(0.45f, 0.43f, 0.4f));
			else if (part == "posts" || part == "rack")
			{
				int k = 0;
				for (const float x : { -hx + 0.1f, hx - 0.1f })
					for (const float z : { -hz + 0.1f, hz - 0.1f })
						Part(w, group, n + " Post " + std::to_string(++k), { x, 1.0f, z }, { 0.12f, 2.0f, 0.12f }, dark);
				if (part == "rack")
					for (int i = 0; i < 3; ++i)
						Part(w, group, n + " Bar " + std::to_string(i + 1), { 0.0f, 0.9f + 0.4f * static_cast<float>(i), 0.0f }, { f.x - 0.2f, 0.05f, 0.05f }, light);
				else
					Part(w, group, n + " Beam", { 0.0f, 1.95f, 0.0f }, { f.x, 0.1f, 0.1f }, dark);
			}
			else if (part == "walls")
			{
				Part(w, group, n + " Back", { 0.0f, 0.8f, -hz + 0.05f }, { f.x, 1.6f, 0.1f }, c);
				Part(w, group, n + " Left", { -hx + 0.05f, 0.8f, 0.0f }, { 0.1f, 1.6f, f.y }, c);
				Part(w, group, n + " Right", { hx - 0.05f, 0.8f, 0.0f }, { 0.1f, 1.6f, f.y }, c);
			}
			else if (part == "roof") Part(w, group, n + " Thatch", { 0.0f, 2.1f, 0.0f }, { f.x + 0.4f, 0.25f, f.y + 0.4f }, glm::vec3(0.62f, 0.55f, 0.3f));
			else if (part == "table")
			{
				Part(w, group, n + " Top", { 0.0f, 0.8f, 0.0f }, { f.x, 0.1f, f.y }, light);
				for (const float x : { -hx + 0.15f, hx - 0.15f }) Part(w, group, n + " Leg " + (x < 0.0f ? "L" : "R"), { x, 0.4f, 0.0f }, { 0.12f, 0.8f, f.y - 0.2f }, dark);
			}
		}

		std::string ReadDef(const std::string& id, const Json& o, BuildingDef& d)
		{
			if (!o.is_object()) return "expected an object";
			if (!GameplayTag::IsValidTagName(std::string(kKindPrefix) + id)) return "the id must be letters, digits and _";
			d.Id = id;
			d.Name = o.value("name", id);
			if (const auto f = o.find("footprint"); f != o.end())
			{
				if (!f->is_array() || f->size() != 2 || !(*f)[0].is_number() || !(*f)[1].is_number() || (*f)[0].get<float>() <= 0.0f || (*f)[1].get<float>() <= 0.0f)
					return "footprint must be [x, z] in metres, both more than 0";
				d.Footprint = { (*f)[0].get<float>(), (*f)[1].get<float>() };
			}
			if (const auto c = o.find("color"); c != o.end())
			{
				if (!c->is_array() || c->size() != 3 || !(*c)[0].is_number() || !(*c)[1].is_number() || !(*c)[2].is_number()) return "color must be [r, g, b]";
				d.Color = { (*c)[0].get<float>(), (*c)[1].get<float>(), (*c)[2].get<float>() };
			}
			for (const auto& [key, field] : std::initializer_list<std::pair<const char*, float*>>{ { "rest", &d.Rest }, { "warmth", &d.Warmth }, { "defense", &d.Defense },
				{ "healing", &d.Healing }, { "culture", &d.Culture }, { "aura", &d.Aura } })
				if (const auto v = o.find(key); v != o.end())
				{
					if (!v->is_number() || v->get<float>() < 0.0f) return std::string(key) + " must be a number of 0 or more";
					*field = v->get<float>();
				}
			for (const auto& [key, field] : std::initializer_list<std::pair<const char*, int*>>{ { "shelter", &d.Shelter }, { "storeCap", &d.StoreCap } })
				if (const auto v = o.find(key); v != o.end())
				{
					if (!v->is_number_integer() || v->get<int>() < 0) return std::string(key) + " must be a whole number of 0 or more";
					*field = v->get<int>();
				}
			for (const auto& [key, field] : std::initializer_list<std::pair<const char*, std::string*>>{ { "station", &d.Station }, { "tech", &d.Tech }, { "store", &d.Store } })
				if (const auto v = o.find(key); v != o.end())
				{
					if (!v->is_string()) return std::string(key) + " must be a string";
					*field = v->get<std::string>();
				}
			if (const auto v = o.find("keep"); v != o.end())
			{
				if (!v->is_number() || v->get<float>() <= 0.0f || v->get<float>() > 1.0f) return "keep must be a number more than 0 and at most 1";
				d.Keep = v->get<float>();
			}
			if (const auto v = o.find("supersedes"); v != o.end())
			{
				if (!v->is_array()) return "supersedes must be a list of building ids";
				for (const Json& s : *v)
				{
					if (!s.is_string() || s.get<std::string>().empty() || s.get<std::string>() == id) return "supersedes must be a list of other buildings' ids";
					d.Supersedes.push_back(s.get<std::string>());
				}
			}
			const auto phases = o.find("phases");
			if (phases == o.end() || !phases->is_array() || phases->empty()) return "phases must be a list of at least one phase";
			for (const Json& p : *phases)
			{
				BuildingPhase ph;
				if (!p.is_object() || !p.contains("name") || !p["name"].is_string() || p["name"].get<std::string>().empty()) return "every phase needs a name";
				ph.Name = p["name"].get<std::string>();
				if (const auto w = p.find("work"); w != p.end())
				{
					if (!w->is_number() || w->get<float>() <= 0.0f) return "phase " + ph.Name + ": work must be a number of seconds more than 0";
					ph.Work = w->get<float>();
				}
				if (const auto n = p.find("needs"); n != p.end())
				{
					if (!n->is_object()) return "phase " + ph.Name + ": needs must be an object of item -> units";
					for (const auto& [item, v] : n->items())
					{
						if (!v.is_number_integer() || v.get<int>() <= 0) return "phase " + ph.Name + ": the units of " + item + " must be a whole number more than 0";
						ph.Needs[item] = v.get<int>();
					}
				}
				if (const auto part = p.find("part"); part != p.end())
				{
					if (!part->is_string() || !KnownPart(part->get<std::string>())) return "phase " + ph.Name + ": part must be slab, posts, walls, roof, table or rack";
					ph.Part = part->get<std::string>();
				}
				if (std::any_of(d.Phases.begin(), d.Phases.end(), [&](const BuildingPhase& q) { return q.Name == ph.Name; })) return "two phases are called " + ph.Name;
				d.Phases.push_back(ph);
			}
			return {};
		}
	}

	std::map<std::string, int> BuildingDef::TotalNeeds() const
	{
		std::map<std::string, int> out;
		for (const BuildingPhase& p : Phases) for (const auto& [item, n] : p.Needs) out[item] += n;
		return out;
	}

	float BuildingDef::TotalWork() const
	{
		float w = 0.0f;
		for (const BuildingPhase& p : Phases) w += p.Work;
		return w;
	}

	std::map<std::string, BuildingDef> DefaultBuildings()
	{
		auto phase = [](const char* name, float work, std::map<std::string, int> needs, const char* part) { return BuildingPhase{ name, work, std::move(needs), part }; };

		// a hut of branches thatched with reeds: three sleep in it, rested and out of the cold
		BuildingDef shelter;
		shelter.Id = "shelter";
		shelter.Name = "Shelter";
		shelter.Footprint = { 3.0f, 3.0f };
		shelter.Phases = { phase("foundation", 6.0f, { { "stone", 3 } }, "slab"), phase("frame", 12.0f, { { "branch", 6 } }, "posts"),
			phase("roof", 10.0f, { { "reed", 4 }, { "fiber", 3 } }, "roof") };
		shelter.Shelter = 3;
		shelter.Rest = 0.2f;
		shelter.Warmth = 0.3f;
		shelter.Color = { 0.42f, 0.3f, 0.17f };

		// a covered store: a stockpile of the economy (H18) by the camp
		BuildingDef storage;
		storage.Id = "storage";
		storage.Name = "Storage";
		storage.Footprint = { 3.0f, 2.5f };
		storage.Phases = { phase("floor", 8.0f, { { "stone", 4 }, { "wood", 2 } }, "slab"), phase("walls", 10.0f, { { "branch", 6 } }, "walls"),
			phase("roof", 8.0f, { { "reed", 4 } }, "roof") };
		storage.Store = "stockpile";   // a secondary store of the economy (H18)
		storage.Color = { 0.5f, 0.36f, 0.2f };

		// a proper workbench: the camp's improvised one moves here
		BuildingDef bench;
		bench.Id = "workbench";
		bench.Name = "Workbench";
		bench.Footprint = { 2.0f, 1.2f };
		bench.Phases = { phase("bench", 8.0f, { { "wood", 3 }, { "stone", 2 } }, "table") };
		bench.Station = "workbench";
		bench.Color = { 0.55f, 0.4f, 0.22f };

		// a rack of branches tied with fibre: food and bricks dry on it
		BuildingDef rack;
		rack.Id = "drying_rack";
		rack.Name = "Drying rack";
		rack.Footprint = { 2.5f, 1.5f };
		rack.Phases = { phase("frame", 6.0f, { { "branch", 4 } }, "posts"), phase("rack", 6.0f, { { "fiber", 3 } }, "rack") };
		rack.Station = "drying_rack";
		rack.Color = { 0.5f, 0.38f, 0.22f };

		std::map<std::string, BuildingDef> out = { { shelter.Id, shelter }, { storage.Id, storage }, { bench.Id, bench }, { rack.Id, rack } };
		auto make = [&](const char* id, const char* name, glm::vec2 footprint, std::vector<BuildingPhase> phases, glm::vec3 color) -> BuildingDef&
		{
			BuildingDef& d = out[id];
			d.Id = id;
			d.Name = name;
			d.Footprint = footprint;
			d.Phases = std::move(phases);
			d.Color = color;
			return d;
		};

		// The Stone-Age progression (docs/PROGRESION.md); the technology that unlocks each names it (TechDef::Buildings).
		// Homes: the wattle hut and the longhouse, each better than the one before (and the clan stops raising the older one for beds).
		{
			BuildingDef& d = make("hut", "Wattle hut", { 3.6f, 3.6f }, { phase("foundation", 8.0f, { { "stone", 4 } }, "slab"), phase("frame", 12.0f, { { "branch", 6 }, { "wood", 2 } }, "posts"),
				phase("walls", 14.0f, { { "branch", 6 }, { "daub", 6 } }, "walls"), phase("roof", 10.0f, { { "reed", 8 }, { "fiber", 2 } }, "roof") }, { 0.5f, 0.38f, 0.26f });
			d.Shelter = 4; d.Rest = 0.3f; d.Warmth = 0.45f; d.Supersedes = { "shelter" };
		}
		{
			BuildingDef& d = make("longhouse", "Longhouse", { 7.0f, 4.0f }, { phase("footings", 10.0f, { { "stone", 8 } }, "slab"), phase("frame", 18.0f, { { "wood", 10 }, { "cordage", 3 } }, "posts"),
				phase("walls", 18.0f, { { "branch", 10 }, { "daub", 8 } }, "walls"), phase("roof", 14.0f, { { "reed", 10 }, { "reed_mat", 3 } }, "roof") }, { 0.46f, 0.34f, 0.22f });
			d.Shelter = 8; d.Rest = 0.35f; d.Warmth = 0.55f; d.Supersedes = { "hut", "shelter" };
		}
		// Food storage: a lined pit and a granary on stilts hold more and keep it longer.
		{
			BuildingDef& d = make("cache_pit", "Cache pit", { 2.0f, 2.0f }, { phase("pit", 12.0f, { { "stone", 2 } }, "slab"), phase("lining", 6.0f, { { "reed", 4 }, { "fiber", 2 } }, "roof") },
				{ 0.42f, 0.34f, 0.24f });
			d.StoreCap = 10; d.Keep = 0.75f;
		}
		{
			BuildingDef& d = make("granary", "Raised granary", { 2.6f, 2.6f }, { phase("stilts", 10.0f, { { "wood", 4 }, { "stone", 4 } }, "posts"), phase("floor", 8.0f, { { "wood", 3 }, { "branch", 4 } }, "slab"),
				phase("walls", 10.0f, { { "branch", 6 }, { "daub", 4 } }, "walls"), phase("roof", 8.0f, { { "reed", 6 } }, "roof") }, { 0.55f, 0.42f, 0.26f });
			d.StoreCap = 20; d.Keep = 0.5f;
		}
		// Fire and food: the communal hearth (cooking, and warmth and company around it) and the smoking rack.
		{
			BuildingDef& d = make("hearth", "Communal hearth", { 2.4f, 2.4f }, { phase("ring", 8.0f, { { "stone", 6 } }, "slab"), phase("fire", 6.0f, { { "wood", 3 } }, "rack") }, { 0.45f, 0.4f, 0.36f });
			d.Station = "hearth"; d.Warmth = 0.4f; d.Culture = 0.15f; d.Aura = 6.0f;
		}
		make("smoking_rack", "Smoking rack", { 2.2f, 1.6f }, { phase("frame", 8.0f, { { "branch", 6 } }, "posts"), phase("cover", 8.0f, { { "reed", 4 }, { "fiber", 2 } }, "roof") },
			{ 0.4f, 0.3f, 0.2f }).Station = "smoking_rack";
		// Hides: a frame to stretch and scrape them, over a soaking pit.
		make("hide_frame", "Hide frame", { 2.0f, 1.2f }, { phase("pit", 6.0f, { { "stone", 2 } }, "slab"), phase("frame", 8.0f, { { "branch", 4 }, { "cordage", 2 } }, "rack") },
			{ 0.52f, 0.4f, 0.28f }).Station = "tannery";
		// Care and rites: the healer's hut speeds the healing of the wounded near it; the totem is where the clan meets.
		{
			BuildingDef& d = make("healer_hut", "Healer's hut", { 2.6f, 2.6f }, { phase("frame", 8.0f, { { "branch", 6 } }, "posts"), phase("walls", 8.0f, { { "reed", 6 }, { "fiber", 2 } }, "walls"),
				phase("remedies", 4.0f, { { "poultice", 2 } }, "table") }, { 0.4f, 0.42f, 0.28f });
			d.Healing = 1.0f; d.Aura = 5.0f;
		}
		{
			BuildingDef& d = make("totem", "Totem", { 3.0f, 3.0f }, { phase("circle", 8.0f, { { "stone", 6 } }, "slab"), phase("pole", 12.0f, { { "wood", 2 } }, "posts") }, { 0.5f, 0.32f, 0.2f });
			d.Culture = 0.25f; d.Aura = 9.0f;
		}
		// Defence: a stretch of sharpened stakes.
		make("palisade", "Palisade", { 6.0f, 0.8f }, { phase("stakes", 14.0f, { { "stake", 12 } }, "posts"), phase("rails", 10.0f, { { "branch", 6 }, { "cordage", 3 } }, "rack") },
			{ 0.45f, 0.33f, 0.2f }).Defense = 1.0f;
		// Era 2: the kiln fires bricks; the forge, built of them, smelts copper.
		make("kiln", "Kiln", { 1.8f, 1.8f }, { phase("base", 8.0f, { { "stone", 6 } }, "slab"), phase("dome", 12.0f, { { "clay", 8 } }, "roof"), phase("chimney", 4.0f, { { "clay", 2 } }, "posts") },
			{ 0.62f, 0.42f, 0.3f }).Station = "kiln";
		make("forge", "Forge", { 2.2f, 2.0f }, { phase("hearth", 12.0f, { { "stone", 6 }, { "fired_brick", 6 } }, "slab"), phase("bellows", 8.0f, { { "hide", 2 }, { "wood", 2 } }, "table") },
			{ 0.5f, 0.3f, 0.24f }).Station = "forge";
		return out;
	}

	std::string LoadBuildings(const std::string& json, std::map<std::string, BuildingDef>& defs)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object()) return "the content is not a JSON object";
		const auto section = j.find("buildings");
		if (section == j.end()) return {};
		if (!section->is_object()) return "buildings: expected an object of id -> building";
		std::map<std::string, BuildingDef> read;
		for (const auto& [id, o] : section->items())
		{
			BuildingDef d;
			if (id.empty()) return "buildings: an empty id";
			if (const std::string e = ReadDef(id, o, d); !e.empty()) return "building '" + id + "': " + e;
			read[id] = std::move(d);
		}
		for (auto& [id, d] : read) defs[id] = std::move(d);
		return {};
	}

	std::string LoadBuildingsFile(const std::string& path, std::map<std::string, BuildingDef>& defs)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in) return "cannot open " + path;
		std::stringstream ss;
		ss << in.rdbuf();
		const std::string error = LoadBuildings(ss.str(), defs);
		return error.empty() ? error : path + ": " + error;
	}

	std::string BuildingsToJson(const std::map<std::string, BuildingDef>& defs)
	{
		const BuildingDef plain;
		Json out = Json::object();
		for (const auto& [id, d] : defs)
		{
			Json o = Json::object();
			o["name"] = d.Name;
			o["footprint"] = Json::array({ Round(d.Footprint.x), Round(d.Footprint.y) });
			Json phases = Json::array();
			for (const BuildingPhase& p : d.Phases)
			{
				Json needs = Json::object();
				for (const auto& [item, n] : p.Needs) needs[item] = n;
				phases.push_back({ { "name", p.Name }, { "work", Round(p.Work) }, { "needs", needs }, { "part", p.Part } });
			}
			o["phases"] = phases;
			if (d.Shelter) o["shelter"] = d.Shelter;
			if (d.Rest != 0.0f) o["rest"] = Round(d.Rest);
			if (d.Warmth != 0.0f) o["warmth"] = Round(d.Warmth);
			if (!d.Store.empty()) o["store"] = d.Store;
			if (!d.Station.empty()) o["station"] = d.Station;
			if (!d.Tech.empty()) o["tech"] = d.Tech;
			if (d.StoreCap) o["storeCap"] = d.StoreCap;
			if (d.Keep != 1.0f) o["keep"] = Round(d.Keep);
			if (d.Defense != 0.0f) o["defense"] = Round(d.Defense);
			if (d.Healing != 0.0f) o["healing"] = Round(d.Healing);
			if (d.Culture != 0.0f) o["culture"] = Round(d.Culture);
			if (d.Aura != 0.0f) o["aura"] = Round(d.Aura);
			if (!d.Supersedes.empty()) o["supersedes"] = d.Supersedes;
			if (d.Color != plain.Color) o["color"] = Json::array({ Round(d.Color.r), Round(d.Color.g), Round(d.Color.b) });
			out[id] = o;
		}
		return out.dump(2);
	}

	void RegisterBuildingContent(GameplayLibrary& library)
	{
		AbilityDef a;
		a.Name = "Build";
		a.Tag = "Ability.Build";
		a.NeedsTarget = true;
		a.MoveToTarget = true;
		a.BlockedTags = { "State.Busy", "State.Carrying" };
		a.ActiveTags = { "State.Busy" };
		a.TargetRequiredTags = { kSite, kReady };
		a.Montages = { "plant" };
		a.Duration = 2.0f;
		a.Events.push_back({ "build", 0.5f, "Event.Building.Work", {}, {} });
		a.CooldownSeconds = 0.2f;
		a.Note = "works a construction site whose phase has its materials (Building.Ready): its work moves the phase on";
		library.AddAbility(a);
	}

	const BuildingDef* BuildingOf(const AbilitySystemComponent& a, const std::map<std::string, BuildingDef>& defs)
	{
		if (!a.HasTag("Building")) return nullptr;
		for (const auto& [id, d] : defs)
			if (a.Tags().HasExact(kKindPrefix + id)) return &d;
		return nullptr;
	}

	SiteState ReadSite(const AbilitySystemComponent& a, const BuildingDef& def)
	{
		SiteState s;
		s.Building = def.Id;
		s.Phases = static_cast<int>(def.Phases.size());
		s.Phase = std::clamp(PhaseOf(a), 0, s.Phases);
		s.Done = s.Phase >= s.Phases;
		const float total = std::max(def.TotalWork(), 1e-3f);
		float before = 0.0f;
		for (int i = 0; i < s.Phase && i < s.Phases; ++i) before += def.Phases[static_cast<size_t>(i)].Work;
		if (s.Done) { s.Progress = 1.0f; return s; }
		const BuildingPhase& p = def.Phases[static_cast<size_t>(s.Phase)];
		s.PhaseName = p.Name;
		s.WorkNeeded = p.Work;
		s.Work = std::min(a.Attributes().Base("Work"), p.Work);
		for (const auto& [item, n] : p.Needs)
		{
			const int have = static_cast<int>(std::lround(a.Attributes().Base(Delivered(item))));
			if (have < n) s.Missing[item] = n - have;
		}
		s.Ready = s.Missing.empty();
		s.Progress = std::clamp((before + s.Work) / total, 0.0f, 1.0f);
		return s;
	}

	int DeliverToSite(AbilitySystemComponent& a, const BuildingDef& def, const std::string& item, int count)
	{
		const SiteState s = ReadSite(a, def);
		const auto it = s.Missing.find(item);
		if (s.Done || it == s.Missing.end() || count <= 0) return 0;
		const int taken = std::min(count, it->second);
		if (!a.Attributes().Has(Delivered(item))) a.Attributes().Define(Delivered(item), 0.0f, 0.0f, 1e6f);
		a.Attributes().AddBase(Delivered(item), static_cast<float>(taken));
		SetTag(a, kReady, ReadSite(a, def).Missing.empty());
		return taken;
	}

	bool WorkOnSite(AbilitySystemComponent& a, const BuildingDef& def, float seconds)
	{
		const SiteState s = ReadSite(a, def);
		if (s.Done || !s.Ready || seconds <= 0.0f) return false;
		a.Attributes().AddBase("Work", seconds);
		if (a.Attributes().Base("Work") + 1e-4f < s.WorkNeeded) return false;
		StartPhase(a, def, s.Phase + 1);
		return true;
	}

	EntityId BuildSite(EntityWorld& w, const BuildingDef& def, const std::string& name, const glm::vec3& pos, float yaw)
	{
		const EntityId id = w.Create(name);
		{
			Entity& e = *w.Find(id);
			e.Transform.Position = pos;
			e.Transform.Rotation = glm::vec3(0.0f, yaw, 0.0f);
			e.Interact.emplace();
			InteractionPoint p;
			p.Name = "work";
			p.Position = { 0.0f, 0.0f, 0.5f * def.Footprint.y + 0.8f };
			p.Yaw = 180.0f;   // facing the site
			e.Interact->Points.push_back(p);
			e.Gameplay.emplace();
			e.Gameplay->Tags = { "Building", kKindPrefix + def.Id, def.Phases.empty() ? kDone : kSite };
			e.Gameplay->Attributes.push_back({ "Phase", 0.0f, 0.0f, static_cast<float>(def.Phases.size()) });
			e.Gameplay->Attributes.push_back({ "Work", 0.0f, 0.0f, 1e6f });
			if (!def.Phases.empty())
			{
				for (const auto& [item, n] : def.Phases.front().Needs) e.Gameplay->Attributes.push_back({ Delivered(item), 0.0f, 0.0f, 1e6f });
				if (def.Phases.front().Needs.empty()) e.Gameplay->Tags.push_back(kReady);
			}
		}
		const std::string n = w.Find(id)->Name;
		for (const BuildingPhase& p : def.Phases)
		{
			const EntityId group = w.Create(n + " " + p.Name, id);
			Entity& g = *w.Find(group);
			g.Transform.Scale = { 1.0f, 0.05f, 1.0f };
			g.Active = false;
			BuildPart(w, group, n + " " + p.Name, p.Part, def.Footprint, def.Color);
		}
		return id;
	}

	void SyncSiteVisuals(EntityWorld& w, const std::string& name, const BuildingDef& def, const SiteState& s)
	{
		for (int i = 0; i < static_cast<int>(def.Phases.size()); ++i)
		{
			const EntityId id = w.FindByName(name + " " + def.Phases[static_cast<size_t>(i)].Name);
			Entity* g = id == NullEntity ? nullptr : w.Find(id);
			if (!g) continue;
			const bool shown = i <= s.Phase;
			float height = 1.0f;
			if (i == s.Phase && !s.Done)
			{
				// the materials lie there first (a fifth), then the work raises it
				const BuildingPhase& p = def.Phases[static_cast<size_t>(i)];
				int needed = 0, missing = 0;
				for (const auto& [item, k] : p.Needs) needed += k;
				for (const auto& [item, k] : s.Missing) missing += k;
				const float brought = needed > 0 ? 1.0f - static_cast<float>(missing) / static_cast<float>(needed) : 1.0f;
				height = 0.05f + 0.15f * brought + 0.8f * (s.WorkNeeded > 0.0f ? s.Work / s.WorkNeeded : 1.0f);
			}
			if (g->Active != shown) g->Active = shown;
			if (g->Transform.Scale.y != height) g->Transform.Scale.y = height;
		}
	}

	void BuildingSystem::Bind(GameplayWorld& gameplay)
	{
		m_Gameplay = &gameplay;
		// a system that is still there keeps one listener: Bind again (the editor rebinds on every scene change) must not add a second, or each Build would count twice
		for (const auto& [name, bound] : m_Bound)
			if (gameplay.Find(name) == bound.first) bound.first->RemoveListener(bound.second);
		m_Bound.clear();
		m_Shown.clear();
		for (const std::string& name : gameplay.Names()) Listen(name, gameplay.Find(name));
	}

	void BuildingSystem::Listen(const std::string& name, AbilitySystemComponent* a)
	{
		if (!a || !a->HasTag("Building")) return;
		m_Bound[name] = { a, 0 };
		m_Bound[name].second = a->AddListener("Event.Building", [this, a](const GameplayEventData& e)
		{
			if (e.Tag != "Event.Building.Work") return;
			if (const BuildingDef* def = BuildingOf(*a, Defs)) WorkOnSite(*a, *def, WorkPerUse);
		});
	}

	void BuildingSystem::Update(float, EntityWorld* entities)
	{
		if (!m_Gameplay) return;
		for (const std::string& name : m_Gameplay->Names())
		{
			AbilitySystemComponent* a = m_Gameplay->Find(name);
			if (!a || !a->HasTag("Building")) continue;
			if (const auto it = m_Bound.find(name); it == m_Bound.end() || it->second.first != a) Listen(name, a);   // a new site, or a reloaded one (a new system)
			if (!entities) continue;
			const BuildingDef* def = BuildingOf(*a, Defs);
			if (!def) continue;
			const SiteState s = ReadSite(*a, *def);
			int missing = 0;
			for (const auto& [item, k] : s.Missing) missing += k;
			const float key = static_cast<float>(s.Phase) * 1000.0f + s.Work - 0.001f * static_cast<float>(missing);
			if (const auto shown = m_Shown.find(name); shown != m_Shown.end() && shown->second == key) continue;
			m_Shown[name] = key;
			SyncSiteVisuals(*entities, name, *def, s);
		}
	}

	std::vector<std::string> BuildingSystem::Sites() const
	{
		std::vector<std::string> out;
		if (!m_Gameplay) return out;
		for (const std::string& name : m_Gameplay->Names())
			if (const AbilitySystemComponent* a = m_Gameplay->Find(name); a && a->HasTag("Building")) out.push_back(name);
		return out;
	}

	std::string BuildingSystem::Report() const
	{
		std::string s;
		for (const std::string& name : Sites())
		{
			const AbilitySystemComponent* a = m_Gameplay->Find(name);
			const BuildingDef* def = BuildingOf(*a, Defs);
			if (!def) { s += name + ": a building of unknown kind\n"; continue; }
			const SiteState st = ReadSite(*a, *def);
			if (st.Done) { s += name + " (" + def->Id + "): done\n"; continue; }
			char line[160];
			std::snprintf(line, sizeof line, "%s (%s): phase %d/%d %s, work %.0f/%.0f s, %.0f%% built", name.c_str(), def->Id.c_str(), st.Phase + 1, st.Phases, st.PhaseName.c_str(),
				static_cast<double>(st.Work), static_cast<double>(st.WorkNeeded), static_cast<double>(st.Progress * 100.0f));
			s += line;
			if (!st.Missing.empty())
			{
				s += ", lacks";
				for (const auto& [item, n] : st.Missing) s += " " + item + " x" + std::to_string(n);
			}
			s += "\n";
		}
		return s.empty() ? "no buildings or sites\n" : s;
	}
}
