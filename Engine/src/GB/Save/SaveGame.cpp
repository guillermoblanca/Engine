#include "GB/Save/SaveGame.h"
#include "GB/Game/SceneIO.h"
#include <charconv>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace GB::Save
{
	namespace
	{
		using Json = nlohmann::ordered_json;

		// A float prints as the shortest text that reads back as the same float (see SceneIO.cpp).
		double Round(float v)
		{
			char buf[40];
			const std::to_chars_result r = std::to_chars(buf, buf + sizeof(buf) - 1, v);
			*r.ptr = '\0';
			return std::strtod(buf, nullptr);
		}

		Json Vec3(const glm::vec3& v) { return Json::array({ Round(v.x), Round(v.y), Round(v.z) }); }

		glm::vec3 ReadVec3(const Json& j, const char* key, glm::vec3 def = glm::vec3(0.0f))
		{
			auto it = j.find(key);
			if (it == j.end()) return def;
			if (!it->is_array() || it->size() != 3) throw std::runtime_error(std::string("bad vector '") + key + "'");
			return { (*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>() };
		}

		template<class T>
		void Read(const Json& j, const char* key, T& out)
		{
			auto it = j.find(key);
			if (it != j.end()) out = it->get<T>();
		}

		std::string EmptyBaselineHash() { return HashText(SaveSceneToString(EntityWorld(), SceneFloats::Short7)); }

		Json ToJson(const SaveGame& g, bool withChecksum)
		{
			Json root;
			root["format"] = g.Format;
			root["engine"] = g.Engine;
			const WorldParams& w = g.World;
			root["world"] = Json{ { "generated", w.Generated }, { "type", w.Type }, { "seed", w.Seed }, { "generator", w.GeneratorVersion }, { "size", Round(w.Size) },
				{ "resolution", w.Resolution }, { "river", w.River }, { "lake", w.Lake }, { "forest", Round(w.ForestAmount) }, { "trees", w.TreeBudget }, { "rocks", w.RockBudget } };
			if (w.WildResources != 0.0f) root["world"]["wild"] = Round(w.WildResources);
			if (w.Tiles > 1) root["world"]["tiles"] = w.Tiles;
			root["baseline"] = Json{ { "entities", g.Baseline.Entities }, { "hash", g.Baseline.Hash } };
			const ClockState& c = g.Clock;
			root["clock"] = Json{ { "tick", c.Tick }, { "dt", c.Dt }, { "hours", Round(c.Hours) }, { "dayLength", Round(c.DayLengthSeconds) },
				{ "timeOfDay", c.TimeOfDay }, { "day", c.Day } };
			root["weather"] = Json{ { "preset", g.Weather.Preset } };

			Json ents;
			ents["nextId"] = g.Entities.NextId;
			ents["removed"] = g.Entities.Removed;
			auto list = [](const std::vector<DiffEntity>& v)
			{
				Json a = Json::array();
				for (const DiffEntity& e : v) a.push_back(Json::parse(e.Json));
				return a;
			};
			ents["changed"] = list(g.Entities.Changed);
			ents["added"] = list(g.Entities.Added);
			root["entities"] = std::move(ents);

			Json gp = Json::array();
			for (const GameplayRuntime& r : g.Gameplay)
			{
				Json j{ { "entity", r.Entity }, { "completed", r.State.Completed } };
				Json fx = Json::array();
				for (const EffectRuntime& e : r.State.Effects)
					fx.push_back(Json{ { "name", e.Name }, { "remaining", Round(e.Remaining) }, { "period", Round(e.PeriodTimer) } });
				j["effects"] = std::move(fx);
				Json uses = Json::object();
				for (const auto& [k, v] : r.State.Uses) uses[k] = v;
				j["uses"] = std::move(uses);
				gp.push_back(std::move(j));
			}
			root["gameplay"] = std::move(gp);

			Json actors = Json::array();
			for (const ActorState& a : g.Actors)
			{
				Json j{ { "name", a.Name }, { "position", Vec3(a.Position) }, { "yaw", Round(a.Yaw) } };
				if (!a.Kind.empty()) j["kind"] = a.Kind;
				if (!a.Knowledge.empty()) j["knowledge"] = a.Knowledge;
				if (!a.Set.empty()) j["set"] = a.Set;
				if (!a.Props.empty()) j["props"] = a.Props;
				if (!a.Inventory.empty())
				{
					Json inv = Json::array();
					for (const ItemStack& s : a.Inventory)
					{
						Json stack{ { "item", s.Item }, { "count", s.Count } };
						if (s.Age != 0.0f) stack["age"] = Round(s.Age);
						inv.push_back(std::move(stack));
					}
					j["inventory"] = std::move(inv);
				}
				if (a.Move != glm::vec2(0.0f) || a.Run) j["input"] = Json{ { "move", Json::array({ Round(a.Move.x), Round(a.Move.y) }) }, { "run", a.Run } };
				if (a.Ragdoll)
				{
					Json bodies = Json::array();
					for (const BodyState& b : a.Bodies)
						bodies.push_back(Json{ { "name", b.Name }, { "position", Vec3(b.Position) },
							{ "rotation", Json::array({ Round(b.Rotation.x), Round(b.Rotation.y), Round(b.Rotation.z), Round(b.Rotation.w) }) },
							{ "linear", Vec3(b.LinearVelocity) }, { "angular", Vec3(b.AngularVelocity) } });
					j["ragdoll"] = std::move(bodies);
				}
				actors.push_back(std::move(j));
			}
			root["actors"] = std::move(actors);

			Json sections = Json::object();
			for (const SectionBlob& s : g.Sections)
			{
				Json data = Json::parse(s.Json, nullptr, false);
				sections[s.Name] = Json{ { "version", s.Version }, { "data", data.is_discarded() ? Json() : data } };
			}
			root["sections"] = std::move(sections);

			if (withChecksum) root["checksum"] = HashText(root.dump());
			return root;
		}

		// `j` has been checked for its checksum (or comes from a migration) and has the layout of the current format.
		void FromJson(const Json& root, SaveGame& g)
		{
			g = SaveGame();
			g.Format = root.at("format").get<int>();
			Read(root, "engine", g.Engine);
			if (auto it = root.find("world"); it != root.end())
			{
				WorldParams& w = g.World;
				Read(*it, "generated", w.Generated);
				Read(*it, "type", w.Type);
				Read(*it, "seed", w.Seed);
				Read(*it, "generator", w.GeneratorVersion);
				Read(*it, "wild", w.WildResources);
				Read(*it, "size", w.Size);
				Read(*it, "resolution", w.Resolution);
				Read(*it, "river", w.River);
				Read(*it, "lake", w.Lake);
				Read(*it, "forest", w.ForestAmount);
				Read(*it, "trees", w.TreeBudget);
				Read(*it, "rocks", w.RockBudget);
				Read(*it, "tiles", w.Tiles);
				if (w.Tiles < 1 || w.Tiles > 4) throw std::runtime_error("bad number of world tiles (1 to 4 per side)");
				if (w.GeneratorVersion < kWorldGeneratorV1 || w.GeneratorVersion > kWorldGeneratorLatest)
					throw std::runtime_error("world generator " + std::to_string(w.GeneratorVersion) + " is unknown to this engine (it knows 1 to " + std::to_string(kWorldGeneratorLatest) + ")");
				if (!(w.WildResources >= 0.0f && w.WildResources <= 10.0f)) throw std::runtime_error("bad density of the wild resources");
			}
			if (auto it = root.find("baseline"); it != root.end())
			{
				Read(*it, "entities", g.Baseline.Entities);
				Read(*it, "hash", g.Baseline.Hash);
			}
			if (auto it = root.find("clock"); it != root.end())
			{
				Read(*it, "tick", g.Clock.Tick);
				Read(*it, "dt", g.Clock.Dt);
				Read(*it, "hours", g.Clock.Hours);
				Read(*it, "dayLength", g.Clock.DayLengthSeconds);
				Read(*it, "timeOfDay", g.Clock.TimeOfDay);
				Read(*it, "day", g.Clock.Day);
			}
			if (auto it = root.find("weather"); it != root.end()) Read(*it, "preset", g.Weather.Preset);
			if (auto it = root.find("entities"); it != root.end())
			{
				Read(*it, "nextId", g.Entities.NextId);
				Read(*it, "removed", g.Entities.Removed);
				auto list = [](const Json& j, const char* key, std::vector<DiffEntity>& out)
				{
					auto a = j.find(key);
					if (a == j.end()) return;
					for (const Json& e : *a)
					{
						if (!e.is_object() || !e.contains("id")) throw std::runtime_error("an entity without id");
						out.push_back({ e.at("id").get<EntityId>(), e.dump() });
					}
				};
				list(*it, "changed", g.Entities.Changed);
				list(*it, "added", g.Entities.Added);
			}
			if (auto it = root.find("gameplay"); it != root.end())
				for (const Json& j : *it)
				{
					GameplayRuntime r;
					r.Entity = j.at("entity").get<std::string>();
					Read(j, "completed", r.State.Completed);
					if (auto fx = j.find("effects"); fx != j.end())
						for (const Json& e : *fx)
						{
							EffectRuntime er;
							er.Name = e.at("name").get<std::string>();
							Read(e, "remaining", er.Remaining);
							Read(e, "period", er.PeriodTimer);
							r.State.Effects.push_back(er);
						}
					if (auto u = j.find("uses"); u != j.end())
						for (auto kv = u->begin(); kv != u->end(); ++kv) r.State.Uses[kv.key()] = kv.value().get<int>();
					g.Gameplay.push_back(std::move(r));
				}
			if (auto it = root.find("actors"); it != root.end())
				for (const Json& j : *it)
				{
					ActorState a;
					a.Name = j.at("name").get<std::string>();
					a.Position = ReadVec3(j, "position");
					Read(j, "yaw", a.Yaw);
					Read(j, "kind", a.Kind);
					Read(j, "knowledge", a.Knowledge);
					Read(j, "set", a.Set);
					Read(j, "props", a.Props);
					if (auto inv = j.find("inventory"); inv != j.end())
						for (const Json& s : *inv) a.Inventory.push_back({ s.at("item").get<std::string>(), s.value("count", 1), s.value("age", 0.0f) });
					if (auto in = j.find("input"); in != j.end())
					{
						if (auto m = in->find("move"); m != in->end() && m->is_array() && m->size() == 2) a.Move = { (*m)[0].get<float>(), (*m)[1].get<float>() };
						Read(*in, "run", a.Run);
					}
					if (auto rd = j.find("ragdoll"); rd != j.end())
					{
						a.Ragdoll = true;
						for (const Json& b : *rd)
						{
							BodyState s;
							s.Name = b.at("name").get<std::string>();
							s.Position = ReadVec3(b, "position");
							s.LinearVelocity = ReadVec3(b, "linear");
							s.AngularVelocity = ReadVec3(b, "angular");
							if (auto q = b.find("rotation"); q != b.end() && q->is_array() && q->size() == 4)
								s.Rotation = { (*q)[0].get<float>(), (*q)[1].get<float>(), (*q)[2].get<float>(), (*q)[3].get<float>() };
							a.Bodies.push_back(std::move(s));
						}
					}
					g.Actors.push_back(std::move(a));
				}
			if (auto it = root.find("sections"); it != root.end() && it->is_object())
				for (auto kv = it->begin(); kv != it->end(); ++kv)
					g.Sections.push_back({ kv.key(), kv.value().value("version", 1), kv.value().contains("data") ? kv.value()["data"].dump() : "null" });
		}

		// ---- v0: a bare scene file (SceneIO's {"version":1,"entities":[...]}) becomes a saved game with no generated world: every entity is "added".
		bool MigrateV0(std::string& text, std::string& error)
		{
			Json scene = Json::parse(text, nullptr, false);
			if (scene.is_discarded() || !scene.is_object() || !scene.contains("entities") || !scene["entities"].is_array())
			{
				error = "not a scene file";
				return false;
			}
			EntityId next = 1;
			for (const Json& e : scene["entities"])
				next = std::max<EntityId>(next, e.value("id", 0u) + 1);
			Json root;
			root["format"] = 1;
			root["engine"] = "scene";
			root["world"] = Json{ { "generated", false } };
			root["baseline"] = Json{ { "entities", 0 }, { "hash", EmptyBaselineHash() } };
			root["entities"] = Json{ { "nextId", next }, { "removed", Json::array() }, { "changed", Json::array() }, { "added", scene["entities"] } };
			text = root.dump();
			return true;
		}

		// ---- v1 -> v2: the world gains the version of its generator. Every v1 save was made by generator 1 (the only one there was), and it
		// still makes the same world from the seed, so the save keeps loading onto the same trees and rocks.
		bool MigrateV1(std::string& text, std::string& error)
		{
			Json root = Json::parse(text, nullptr, false);
			if (root.is_discarded() || !root.is_object()) { error = "not a saved game"; return false; }
			if (auto it = root.find("world"); it != root.end() && it->is_object()) (*it)["generator"] = kWorldGeneratorV1;
			root["format"] = 2;
			text = root.dump();
			return true;
		}
	}

	std::string HashText(const std::string& text)
	{
		uint64_t h = 1469598103934665603ull;
		for (unsigned char c : text)
		{
			h ^= c;
			h *= 1099511628211ull;
		}
		static const char* digits = "0123456789abcdef";
		std::string out(16, '0');
		for (int i = 15; i >= 0; --i, h >>= 4) out[static_cast<size_t>(i)] = digits[h & 0xF];
		return out;
	}

	const std::vector<Migration>& BuiltinMigrations()
	{
		static const std::vector<Migration> chain = {
			{ 0, "a bare scene file becomes a saved game without a generated world", MigrateV0 },
			{ 1, "the world records its generator version (1 for every save made before)", MigrateV1 },
		};
		return chain;
	}

	int DetectFormat(const std::string& text)
	{
		const Json root = Json::parse(text, nullptr, false);
		if (root.is_discarded() || !root.is_object()) return -1;
		if (auto it = root.find("format"); it != root.end()) return it->is_number_integer() ? it->get<int>() : -1;
		return root.contains("entities") ? 0 : -1;
	}

	Result Migrate(std::string& json, int version, int target, const std::vector<Migration>& chain, std::vector<std::string>* log)
	{
		while (version < target)
		{
			const Migration* step = nullptr;
			for (const Migration& m : chain)
				if (m.From == version) step = &m;
			if (!step || !step->Apply) return Result::Fail("no migration from format " + std::to_string(version));
			std::string err;
			if (!step->Apply(json, err)) return Result::Fail("migration from format " + std::to_string(version) + " failed: " + err);
			if (log) log->push_back("format " + std::to_string(version) + " -> " + std::to_string(version + 1) + ": " + step->Note);
			++version;
		}
		return {};
	}

	std::string SaveGameToString(const SaveGame& game)
	{
		SaveGame g = game;
		g.Format = kFormatVersion;
		return ToJson(g, true).dump(2) + "\n";
	}

	Result ParseSaveGame(const std::string& text, SaveGame& out)
	{
		Json root = Json::parse(text, nullptr, false);
		if (root.is_discarded()) return Result::Fail("the file is not valid JSON (damaged or cut short)");
		if (!root.is_object()) return Result::Fail("not a saved game");
		const int format = DetectFormat(text);
		if (format < 0) return Result::Fail("not a saved game (no format)");
		if (format > kFormatVersion)
			return Result::Fail("saved by a newer version of the engine (format " + std::to_string(format) + ", this one reads up to " + std::to_string(kFormatVersion) + ")");
		Result result;
		if (format >= 1)
		{
			auto sum = root.find("checksum");
			if (sum == root.end() || !sum->is_string()) return Result::Fail("the file has no checksum (damaged)");
			const std::string wanted = sum->get<std::string>();
			root.erase("checksum");
			if (HashText(root.dump()) != wanted) return Result::Fail("the checksum does not match (the file is damaged)");
		}
		if (format < kFormatVersion)
		{
			std::string migrated = format >= 1 ? root.dump() : text;
			std::vector<std::string> log;
			result = Migrate(migrated, format, kFormatVersion, BuiltinMigrations(), &log);
			if (!result.Ok) return result;
			result.Warnings = log;
			root = Json::parse(migrated, nullptr, false);
			if (root.is_discarded()) return Result::Fail("a migration produced invalid JSON");
		}
		try
		{
			SaveGame g;
			FromJson(root, g);
			out = std::move(g);
		}
		catch (const std::exception& ex)
		{
			return Result::Fail(std::string("bad value: ") + ex.what());
		}
		return result;
	}
}
