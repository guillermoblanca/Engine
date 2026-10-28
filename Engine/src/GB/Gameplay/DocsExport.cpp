#include "GB/Gameplay/DocsExport.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		const char* OpName(ModOp op)
		{
			switch (op)
			{
			case ModOp::Add: return "add";
			case ModOp::Multiply: return "multiply";
			default: return "override";
			}
		}

		const char* PolicyName(EffectPolicy p)
		{
			switch (p)
			{
			case EffectPolicy::Instant: return "instant";
			case EffectPolicy::Duration: return "duration";
			default: return "infinite";
			}
		}

		bool StartsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

		// Where an ability sits in the skill tree: by what it acts on (the tag of its first event), else by its name.
		std::string Domain(const AbilityDef& a)
		{
			const std::string ev = a.Events.empty() ? std::string() : a.Events[0].EventTag;
			if (StartsWith(ev, "Event.Resource")) return "Recursos";
			if (StartsWith(ev, "Event.Fuel") || StartsWith(ev, "Event.Water.") || StartsWith(ev, "Event.Door")) return "Interactivos";
			if (StartsWith(ev, "Event.Soil") || StartsWith(ev, "Event.Plant") || StartsWith(ev, "Event.Tree") || StartsWith(ev, "Event.Fruit")
				|| StartsWith(ev, "Event.Cow") || StartsWith(ev, "Event.Compost")) return "Huerto";
			if (a.Name.find("Box") != std::string::npos || a.Name.find("Wheelbarrow") != std::string::npos) return "Transporte";
			return "Otras";
		}

		Json Strings(const std::vector<std::string>& v)
		{
			Json j = Json::array();
			for (const std::string& s : v) j.push_back(s);
			return j;
		}

		Json Round(float v) { return std::round(static_cast<double>(v) * 1000.0) / 1000.0; }

		Json AbilityJson(const AbilityDef& a)
		{
			Json j = Json::object();
			j["name"] = a.Name;
			j["tag"] = a.Tag;
			j["domain"] = Domain(a);
			j["note"] = a.Note;
			j["needsTarget"] = a.NeedsTarget;
			j["moveToTarget"] = a.MoveToTarget;
			j["requiredTags"] = Strings(a.RequiredTags);
			j["blockedTags"] = Strings(a.BlockedTags);
			j["requiredProp"] = a.RequiredProp;
			j["targetRequiredTags"] = Strings(a.TargetRequiredTags);
			j["targetBlockedTags"] = Strings(a.TargetBlockedTags);
			j["targetRequiredAnyTags"] = Strings(a.TargetRequiredAnyTags);
			Json minAttrs = Json::array();
			for (const auto& kv : a.TargetMinAttributes) minAttrs.push_back({ { "attribute", kv.first }, { "value", Round(kv.second) } });
			j["targetMinAttributes"] = minAttrs;
			j["point"] = a.Point;
			j["cost"] = a.Cost;
			j["cooldownSeconds"] = Round(a.CooldownSeconds);
			j["montages"] = Strings(a.Montages);
			j["durationSeconds"] = Round(a.Duration);
			Json events = Json::array();
			for (const AbilityEventDef& e : a.Events)
				events.push_back({ { "notify", e.Notify }, { "time", Round(e.Time) }, { "eventTag", e.EventTag }, { "selfEffects", Strings(e.SelfEffects) }, { "targetEffects", Strings(e.TargetEffects) } });
			j["events"] = events;
			j["waitEvent"] = a.WaitEvent;
			j["activeTags"] = Strings(a.ActiveTags);
			j["activationEffects"] = Strings(a.ActivationEffects);
			j["removeEffects"] = Strings(a.RemoveEffects);
			j["endEffects"] = Strings(a.EndEffects);
			j["equipProp"] = a.EquipProp;
			j["keepProp"] = a.KeepProp;
			j["unequipProp"] = a.UnequipProp;
			return j;
		}

		Json EffectJson(const GameplayEffectDef& e)
		{
			Json mods = Json::array();
			for (const AttributeModifier& m : e.Modifiers) mods.push_back({ { "attribute", m.Attribute }, { "op", OpName(m.Op) }, { "magnitude", Round(m.Magnitude) } });
			return { { "name", e.Name }, { "policy", PolicyName(e.Policy) }, { "durationSeconds", Round(e.Duration) }, { "periodSeconds", Round(e.Period) },
				{ "modifiers", mods }, { "grantedTags", Strings(e.GrantedTags) }, { "removedTags", Strings(e.RemovedTags) }, { "unique", e.Unique } };
		}

		// What kind of thing an ability system belongs to, from what it holds.
		std::string KindOf(const AbilitySystemComponent& a)
		{
			if (a.Attributes().Has(NeedName::Hunger)) return "Agente (personaje con necesidades)";
			if (a.HasTag("Resource.Node")) return "Nodo de recurso";
			if (a.HasTag("Garden.Plot")) return "Parcela del huerto";
			if (a.HasTag("Garden.Tree")) return "Árbol del huerto";
			if (a.HasTag("Garden.Cow")) return "Vaca";
			if (a.HasTag("Animal.Rabbit")) return "Conejo";
			if (a.HasTag("Animal.Fox")) return "Zorro";
			if (a.HasTag("Interactive.Campfire")) return "Fogata";
			if (a.HasTag("Interactive.Well")) return "Pozo";
			if (a.HasTag("Interactive.Door")) return "Puerta";
			if (a.HasTag("Garden.Compost")) return "Compostador";
			if (a.Attributes().Has("Energy")) return "Personaje del huerto (jugador o ayudante)";
			return "Otro";
		}
	}

	std::string ExportDocsJson(const DocsInputs& in)
	{
		Json root = Json::object();

		// ---- abilities and effects
		Json abilities = Json::array(), effects = Json::array();
		if (in.Library)
		{
			for (const auto& kv : in.Library->Abilities()) abilities.push_back(AbilityJson(kv.second));
			for (const auto& kv : in.Library->Effects()) effects.push_back(EffectJson(kv.second));
		}
		root["abilities"] = abilities;
		root["effects"] = effects;

		// ---- attributes and tags by kind of entity
		struct Group { std::string Example; int Count = 0; std::map<std::string, std::array<float, 3>> Attributes; std::set<std::string> Tags, Abilities; };
		std::map<std::string, Group> groups;
		if (in.World)
			for (const std::string& name : in.World->Names())
			{
				const AbilitySystemComponent* a = in.World->Find(name);
				if (!a) continue;
				Group& g = groups[KindOf(*a)];
				if (g.Example.empty()) g.Example = name;
				++g.Count;
				for (const AttributeSet::Attribute& at : a->Attributes().All()) g.Attributes.emplace(at.Name, std::array<float, 3>{ at.Base, at.Min, at.Max });
				for (const std::string& t : a->Tags().Names()) g.Tags.insert(t);
				for (const std::string& ab : a->Granted()) g.Abilities.insert(ab);
			}
		Json kinds = Json::array();
		for (const auto& [kind, g] : groups)
		{
			Json attrs = Json::array();
			for (const auto& [n, v] : g.Attributes) attrs.push_back({ { "name", n }, { "base", Round(v[0]) }, { "min", Round(v[1]) }, { "max", v[2] > 1e8f ? Json(nullptr) : Json(Round(v[2])) } });
			kinds.push_back({ { "kind", kind }, { "count", g.Count }, { "example", g.Example }, { "attributes", attrs },
				{ "tags", Strings(std::vector<std::string>(g.Tags.begin(), g.Tags.end())) }, { "abilities", Strings(std::vector<std::string>(g.Abilities.begin(), g.Abilities.end())) } });
		}
		root["entityKinds"] = kinds;

		// ---- needs
		const NeedsConfig& n = in.Needs;
		root["needs"] = {
			{ "list", Json::array({ NeedName::Hunger, NeedName::Thirst, NeedName::Rest, NeedName::Warmth, NeedName::Safety, NeedName::Company }) },
			{ "derived", Json::array({ NeedName::Health, NeedName::Vigor }) },
			{ "config", { { "daySeconds", Round(n.DaySeconds) }, { "startHour", Round(n.StartHour) }, { "hungerSeconds", Round(n.HungerSeconds) }, { "thirstSeconds", Round(n.ThirstSeconds) },
				{ "restSeconds", Round(n.RestSeconds) }, { "sleepGain", Round(n.SleepGain) }, { "warmGain", Round(n.WarmGain) }, { "starveDamage", Round(n.StarveDamage) },
				{ "thirstDamage", Round(n.ThirstDamage) }, { "coldDamage", Round(n.ColdDamage) }, { "healPerSecond", Round(n.HealPerSecond) },
				{ "nightFrom", Round(n.NightFrom) }, { "nightTo", Round(n.NightTo) }, { "foodHunger", n.FoodHunger }, { "foodThirst", n.FoodThirst }, { "waterThirst", n.WaterThirst } } } };

		// ---- the senses, memory, decision and roles
		root["senses"] = { { "vision", { { "rangeMeters", Round(in.Vision.Range) }, { "halfFovDegrees", Round(in.Vision.HalfFov * 57.29578f) }, { "nearRangeMeters", Round(in.Vision.NearRange) } } },
			{ "memory", { { "lifetimeSeconds", Round(static_cast<float>(in.Memory.Lifetime)) }, { "recheckSeconds", Round(static_cast<float>(in.Memory.RecheckSeconds)) } } } };
		root["decision"] = { { "minCommitSeconds", Round(in.Chooser.MinCommit) }, { "margin", Round(in.Chooser.Margin) }, { "stickBonus", Round(in.Chooser.StickBonus) }, { "criticalScore", Round(in.Chooser.CriticalScore) } };
		Json roles = Json::object();
		for (const auto& [role, weights] : in.Roles)
		{
			Json w = Json::object();
			for (const auto& [kind, v] : weights) w[kind] = Round(v);
			roles[role] = w;
		}
		root["roles"] = roles;

		// ---- seasons
		Json seasons = Json::array();
		for (int i = 0; i < 4; ++i)
		{
			const Season s = static_cast<Season>(i);
			seasons.push_back({ { "name", SeasonClock::Name(s) }, { "growthRate", Round(SeasonClock::GrowthRate(s)) }, { "hungerRate", Round(SeasonClock::HungerRate(s)) },
				{ "foliage", { Round(SeasonClock::Foliage(s, 0.0f).x), Round(SeasonClock::Foliage(s, 0.0f).y), Round(SeasonClock::Foliage(s, 0.0f).z) } },
				{ "snowAtStart", Round(SeasonClock::SnowCover(s, 0.0f)) }, { "snowAtMiddle", Round(SeasonClock::SnowCover(s, 0.5f)) }, { "snowAtEnd", Round(SeasonClock::SnowCover(s, 1.0f)) },
				{ "weatherAtStart", SeasonClock::Weather(s, 0.05f) }, { "weatherAtMiddle", SeasonClock::Weather(s, 0.5f) }, { "weatherAtEnd", SeasonClock::Weather(s, 0.95f) } });
		}
		root["seasons"] = { { "seasonSeconds", Round(SeasonClock().SeasonSeconds) }, { "list", seasons } };

		// ---- settings of the systems
		// the rabbits' numbers as the flat "ecology" table the wiki reads, and every species as its data says
		const auto rabbit = in.Ecology.find("rabbit");
		const EcologyConfig e = rabbit != in.Ecology.end() ? rabbit->second.Life : EcologyConfig{};
		root["species"] = Json::parse(SpeciesToJson(in.Ecology));
		root["ecology"] = { { "maxAnimals", e.MaxAnimals }, { "speed", Round(e.Speed) }, { "wanderRadius", Round(e.WanderRadius) }, { "foodMax", Round(e.FoodMax) }, { "hungerPerSecond", Round(e.HungerPerSecond) },
			{ "hungryBelow", Round(e.HungryBelow) }, { "eatReach", Round(e.EatReach) }, { "eatSeconds", Round(e.EatSeconds) }, { "foodPerPlant", Round(e.FoodPerPlant) }, { "breedAbove", Round(e.BreedAbove) },
			{ "adultSeconds", Round(e.AdultSeconds) }, { "breedCooldown", Round(e.BreedCooldown) }, { "starveSeconds", Round(e.StarveSeconds) } };
		{
			// the first campfire and well for the summary line of the wiki, and every interactive as its data says
			InteractiveDef fire, well;
			for (const auto& [id, d] : in.Interactives) { if (d.Kind == "campfire" && fire.Id.empty()) fire = d; if (d.Kind == "well" && well.Id.empty()) well = d; }
			root["interactives"] = { { "burnSeconds", Round(fire.BurnSeconds) }, { "fuelPerWood", fire.FuelPerWood }, { "waterPerDraw", well.WaterPerDraw },
				{ "refillSeconds", Round(well.RefillSeconds) }, { "list", Json::parse(InteractivesToJson(in.Interactives)) } };
		}
		root["garden"] = { { "stageSeconds", Round(in.Garden.StageSeconds) }, { "moistureSeconds", Round(in.Garden.MoistureSeconds) }, { "treeSeconds", Round(in.Garden.TreeSeconds) } };
		Json commands = Json::array();
		for (const auto& c : in.Commands) commands.push_back({ { "name", c.first }, { "help", c.second } });
		root["commands"] = commands;
		return root.dump(2);
	}
}
