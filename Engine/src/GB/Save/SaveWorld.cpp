#include "GB/Save/SaveWorld.h"
#include "GB/Game/SceneIO.h"
#include "GB/Qa/Qa.h"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace GB::Save
{
	namespace
	{
		using Json = nlohmann::ordered_json;

		// id -> the entity as compact JSON text, for every entity of a world
		std::map<EntityId, std::string> EntityTexts(const EntityWorld& world, std::vector<Json>* objects = nullptr)
		{
			std::map<EntityId, std::string> out;
			const Json scene = Json::parse(SaveSceneToString(world), nullptr, false);
			if (scene.is_discarded() || !scene.contains("entities")) return out;
			for (const Json& e : scene["entities"])
			{
				out[e.value("id", 0u)] = e.dump();
				if (objects) objects->push_back(e);
			}
			return out;
		}

		void AddText(Qa::Fingerprint& f, const std::string& s)
		{
			f.Add(static_cast<int64_t>(s.size()));
			for (unsigned char c : s) f.Add(static_cast<int64_t>(c));
		}
		void AddVec(Qa::Fingerprint& f, const glm::vec3& v, float quantum)
		{
			f.Add(v.x, quantum);
			f.Add(v.y, quantum);
			f.Add(v.z, quantum);
		}
	}

	std::string DigestToString(uint64_t value)
	{
		static const char* digits = "0123456789abcdef";
		std::string out(16, '0');
		for (int i = 15; i >= 0; --i, value >>= 4) out[static_cast<size_t>(i)] = digits[value & 0xF];
		return out;
	}

	std::string ISaveHost::Validate(const LoadedGame& game)
	{
		const GameplayWorld* live = Gameplay();
		if (!live) return {};
		GameplayWorld scratch;   // the same library, none of the live state
		scratch.Library = live->Library;
		std::string error;
		RestoreGameplay(scratch, game.Entities, game.Gameplay, &error);
		return error;
	}

	BaselineId BaselineIdOf(const EntityWorld& baseline)
	{
		return { static_cast<uint32_t>(baseline.Count()), HashText(SaveSceneToString(baseline, SceneFloats::Short7)) };
	}

	EntityDiff DiffEntities(const EntityWorld& baseline, const EntityWorld& current)
	{
		const auto base = EntityTexts(baseline), cur = EntityTexts(current);
		EntityDiff diff;
		diff.NextId = current.NextId();
		for (const auto& [id, text] : base)
			if (!cur.count(id)) diff.Removed.push_back(id);
		for (const auto& [id, text] : cur)
		{
			auto it = base.find(id);
			if (it == base.end()) diff.Added.push_back({ id, text });
			else if (it->second != text) diff.Changed.push_back({ id, text });
		}
		return diff;
	}

	Result PatchEntities(const EntityWorld& baseline, const EntityDiff& diff, EntityWorld& out)
	{
		std::vector<Json> objects;
		EntityTexts(baseline, &objects);
		std::map<EntityId, Json> byId;
		for (Json& e : objects)
		{
			const EntityId id = e.value("id", 0u);   // (read before the move: the right side of an assignment is evaluated first)
			byId[id] = std::move(e);
		}
		for (EntityId id : diff.Removed) byId.erase(id);
		for (const auto* list : { &diff.Changed, &diff.Added })
			for (const DiffEntity& e : *list)
			{
				Json j = Json::parse(e.Json, nullptr, false);
				if (j.is_discarded()) return Result::Fail("an entity of the save is damaged (id " + std::to_string(e.Id) + ")");
				byId[e.Id] = std::move(j);
			}
		Json scene;
		scene["version"] = 1;
		Json list = Json::array();
		for (auto& [id, e] : byId) list.push_back(std::move(e));
		scene["entities"] = std::move(list);
		EntityWorld built;
		const SceneIOResult r = LoadSceneFromString(built, scene.dump());
		if (!r.Ok) return Result::Fail("the entities of the save do not load: " + r.Error);
		built.SetNextId(diff.NextId);
		out = std::move(built);
		return {};
	}

	std::vector<GameplayRuntime> CaptureGameplay(const GameplayWorld& gameplay)
	{
		std::vector<GameplayRuntime> out;
		for (const std::string& name : gameplay.Names())
			if (const AbilitySystemComponent* a = gameplay.Find(name)) out.push_back({ name, a->RuntimeState() });
		return out;
	}

	bool RestoreGameplay(GameplayWorld& gameplay, const EntityWorld& entities, const std::vector<GameplayRuntime>& runtime, std::string* error)
	{
		gameplay.Load(entities);
		for (const GameplayRuntime& r : runtime)
		{
			AbilitySystemComponent* a = gameplay.Find(r.Entity);
			if (!a)
			{
				if (error) *error = "the save has the state of an ability system for '" + r.Entity + "', which is not in the scene";
				return false;
			}
			if (!a->RestoreRuntimeState(r.State, error)) return false;
		}
		return true;
	}

	CaptureResult CaptureGame(const ISaveHost& host)
	{
		CaptureResult out;
		SaveGame& g = out.Game;
		g.World = host.World();
		g.Clock = host.Clock();
		g.Weather = host.Weather();
		EntityWorld current = host.Entities();
		if (const GameplayWorld* gp = host.Gameplay())
		{
			gp->Store(current);   // the components carry the live attributes, tags and abilities
			g.Gameplay = CaptureGameplay(*gp);
			for (const std::string& name : gp->Names())
				if (gp->Find(name)->AnyActive()) out.Warnings.push_back(name + " was in the middle of an ability: it is saved without it");
		}
		EntityWorld baseline;
		if (g.World.Generated)
		{
			std::string error;
			if (!host.BuildBaseline(g.World, baseline, error))
			{
				out.Warnings.push_back("could not build the generated world (" + error + "): saving every entity");
				g.World.Generated = false;
				baseline = EntityWorld();
			}
		}
		g.Baseline = BaselineIdOf(baseline);
		g.Entities = DiffEntities(baseline, current);
		g.Actors = host.Actors();
		g.Sections = host.Sections();
		return out;
	}

	Result ApplyGame(ISaveHost& host, const SaveGame& game, const LoadOptions& options)
	{
		Result result;
		LoadedGame loaded;
		loaded.World = game.World;
		EntityWorld baseline;
		if (game.World.Generated)
		{
			std::string error;
			if (!host.BuildBaseline(game.World, baseline, error)) return Result::Fail("could not generate the world of the save: " + error);
		}
		const BaselineId id = BaselineIdOf(baseline);
		if (id.Hash != game.Baseline.Hash || id.Entities != game.Baseline.Entities)
		{
			const std::string msg = "the seed no longer generates the world this save was made on (" + std::to_string(game.Baseline.Entities) + " objects then, " +
				std::to_string(id.Entities) + " now)";
			if (!options.AllowBaselineDrift) return Result::Fail(msg);
			result.Warnings.push_back(msg);
		}
		Result patched = PatchEntities(baseline, game.Entities, loaded.Entities);
		if (!patched.Ok) return patched;
		loaded.Clock = game.Clock;
		loaded.Weather = game.Weather;
		loaded.Gameplay = game.Gameplay;
		loaded.Actors = game.Actors;
		loaded.Sections = game.Sections;
		const std::string why = host.Validate(loaded);
		if (!why.empty()) return Result::Fail(why);
		host.Commit(std::move(loaded));
		return result;
	}

	Digest ComputeDigest(const ISaveHost& host)
	{
		Digest d;
		EntityWorld entities = host.Entities();
		const GameplayWorld* gp = host.Gameplay();
		if (gp) gp->Store(entities);
		{
			Qa::Fingerprint f;
			for (const auto& [id, e] : entities.All())
			{
				f.Add(static_cast<int64_t>(id));
				AddText(f, e.Name);
				f.Add(static_cast<int64_t>(e.Parent));
				f.Add(static_cast<int64_t>(e.Active));
				for (const std::string& t : e.Tags) AddText(f, t);
				AddVec(f, e.Transform.Position, 1e-2f);
				AddVec(f, e.Transform.Rotation, 1e-1f);
				AddVec(f, e.Transform.Scale, 1e-2f);
				if (e.Prop) { f.Add(static_cast<int64_t>(e.Prop->Kind)); f.Add(static_cast<int64_t>(e.Prop->Variant)); }
				if (e.Mesh) { AddText(f, e.Mesh->Model); f.Add(static_cast<int64_t>(e.Mesh->Shape)); f.Add(e.Mesh->Size, 1e-2f); AddVec(f, e.Mesh->Albedo, 1e-2f); }
				if (e.Gameplay)
				{
					for (const std::string& t : e.Gameplay->Tags) AddText(f, t);
					for (const std::string& a : e.Gameplay->Abilities) AddText(f, a);
					for (const std::string& a : e.Gameplay->Effects) AddText(f, a);
					for (const GameplayAttribute& a : e.Gameplay->Attributes) { AddText(f, a.Name); f.Add(a.Value, 1e-3f); }
				}
			}
			d.Parts["entities"] = f.Value();
		}
		{
			Qa::Fingerprint f;
			if (gp)
				for (const std::string& name : gp->Names())
				{
					const AbilitySystemComponent* a = gp->Find(name);
					AddText(f, name);
					for (const auto& [tag, count] : a->Tags().All()) { AddText(f, tag); f.Add(static_cast<int64_t>(count)); }
					for (const AttributeSet::Attribute& at : a->Attributes().All()) { AddText(f, at.Name); f.Add(at.Base, 1e-3f); f.Add(at.Current, 1e-3f); }
					const AbilityRuntimeState rt = a->RuntimeState();
					for (const EffectRuntime& e : rt.Effects) { AddText(f, e.Name); f.Add(e.Remaining, 1e-2f); f.Add(e.PeriodTimer, 1e-2f); }
					for (const auto& [k, v] : rt.Uses) { AddText(f, k); f.Add(static_cast<int64_t>(v)); }
					f.Add(static_cast<int64_t>(rt.Completed));
					for (const std::string& ab : a->ActiveAbilities()) AddText(f, "running:" + ab);
				}
			d.Parts["gameplay"] = f.Value();
		}
		{
			Qa::Fingerprint f;
			for (const ActorState& a : host.Actors())
			{
				AddText(f, a.Name);
				AddVec(f, a.Position, 1e-2f);
				f.Add(a.Yaw, 1e-2f);
				AddText(f, a.Set);
				if (!a.Kind.empty()) AddText(f, a.Kind);
				for (const std::string& k : a.Knowledge) AddText(f, k);
				for (const std::string& p : a.Props) AddText(f, p);
				for (const ItemStack& s : a.Inventory) { AddText(f, s.Item); f.Add(static_cast<int64_t>(s.Count)); if (s.Age != 0.0f) f.Add(s.Age, 1e-2f); }
				f.Add(a.Move.x, 1e-2f);
				f.Add(a.Move.y, 1e-2f);
				f.Add(static_cast<int64_t>(a.Run));
				for (const BodyState& b : a.Bodies)
				{
					AddText(f, b.Name);
					AddVec(f, b.Position, 1e-2f);
					AddVec(f, b.LinearVelocity, 1e-1f);
				}
			}
			d.Parts["actors"] = f.Value();
		}
		{
			const ClockState c = host.Clock();
			Qa::Fingerprint f;
			f.Add(static_cast<int64_t>(c.Tick));
			f.Add(c.Hours, 1e-2f);
			f.Add(static_cast<int64_t>(c.Day));
			d.Parts["clock"] = f.Value();
		}
		{
			Qa::Fingerprint f;
			AddText(f, host.Weather().Preset);
			d.Parts["weather"] = f.Value();
		}
		{
			Qa::Fingerprint f;
			for (const SectionBlob& s : host.Sections()) { AddText(f, s.Name); AddText(f, s.Json); }
			d.Parts["sections"] = f.Value();
		}
		Qa::Fingerprint total;
		for (const auto& [name, value] : d.Parts) { AddText(total, name); total.Add(static_cast<int64_t>(value & 0x7FFFFFFFFFFFFFFFull)); }
		d.Total = total.Value();
		return d;
	}

	std::vector<std::string> DifferingParts(const Digest& a, const Digest& b)
	{
		std::vector<std::string> out;
		for (const auto& [name, value] : a.Parts)
		{
			auto it = b.Parts.find(name);
			if (it == b.Parts.end() || it->second != value) out.push_back(name);
		}
		for (const auto& [name, value] : b.Parts)
			if (!a.Parts.count(name)) out.push_back(name);
		return out;
	}
}
