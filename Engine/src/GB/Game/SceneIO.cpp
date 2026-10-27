#include "GB/Game/SceneIO.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace GB
{
	namespace
	{
		using Json = nlohmann::ordered_json;

		// Floats print with 7 significant digits so files stay readable (0.1, not 0.10000000149).
		double Round(float v)
		{
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%.7g", static_cast<double>(v));
			return std::strtod(buf, nullptr);
		}

		Json ToJson(const glm::vec3& v) { return Json::array({ Round(v.x), Round(v.y), Round(v.z) }); }

		bool ReadVec3(const Json& j, const char* key, glm::vec3& out)
		{
			auto it = j.find(key);
			if (it == j.end()) return true;
			if (!it->is_array() || it->size() != 3) return false;
			for (int i = 0; i < 3; ++i)
			{
				if (!(*it)[i].is_number()) return false;
				out[i] = (*it)[i].get<float>();
			}
			return true;
		}

		template<class T>
		void ReadValue(const Json& j, const char* key, T& out)
		{
			auto it = j.find(key);
			if (it != j.end()) out = it->get<T>();
		}

		const char* ShapeName(MeshShape s) { return s == MeshShape::Sphere ? "sphere" : s == MeshShape::Plane ? "plane" : "cube"; }
		const char* ColliderName(ColliderShape s) { return s == ColliderShape::Sphere ? "sphere" : s == ColliderShape::Capsule ? "capsule" : "box"; }

		Json MeshToJson(const MeshComponent& m)
		{
			Json j;
			if (m.Model.empty()) { j["shape"] = ShapeName(m.Shape); j["size"] = Round(m.Size); }
			else j["model"] = m.Model;
			j["albedo"] = ToJson(m.Albedo);
			j["metallic"] = Round(m.Metallic);
			j["roughness"] = Round(m.Roughness);
			if (m.Emissive != glm::vec3(0.0f)) j["emissive"] = ToJson(m.Emissive);
			return j;
		}

		bool MeshFromJson(const Json& j, MeshComponent& m, std::string& err)
		{
			const std::string shape = j.value("shape", "cube");
			if (shape == "cube") m.Shape = MeshShape::Cube;
			else if (shape == "sphere") m.Shape = MeshShape::Sphere;
			else if (shape == "plane") m.Shape = MeshShape::Plane;
			else { err = "unknown mesh shape '" + shape + "'"; return false; }
			ReadValue(j, "size", m.Size);
			ReadValue(j, "model", m.Model);
			ReadValue(j, "metallic", m.Metallic);
			ReadValue(j, "roughness", m.Roughness);
			if (!ReadVec3(j, "albedo", m.Albedo) || !ReadVec3(j, "emissive", m.Emissive)) { err = "bad mesh color"; return false; }
			return true;
		}

		Json BodyToJson(const BodyComponent& b)
		{
			Json j;
			j["type"] = b.Type == BodyType::Static ? "static" : "dynamic";
			j["shape"] = ColliderName(b.Shape);
			j["halfExtents"] = ToJson(b.HalfExtents);
			j["mass"] = Round(b.Mass);
			j["friction"] = Round(b.Friction);
			j["restitution"] = Round(b.Restitution);
			return j;
		}

		bool BodyFromJson(const Json& j, BodyComponent& b, std::string& err)
		{
			const std::string type = j.value("type", "dynamic");
			if (type == "static") b.Type = BodyType::Static;
			else if (type == "dynamic") b.Type = BodyType::Dynamic;
			else { err = "unknown body type '" + type + "'"; return false; }
			const std::string shape = j.value("shape", "box");
			if (shape == "box") b.Shape = ColliderShape::Box;
			else if (shape == "sphere") b.Shape = ColliderShape::Sphere;
			else if (shape == "capsule") b.Shape = ColliderShape::Capsule;
			else { err = "unknown collider shape '" + shape + "'"; return false; }
			ReadValue(j, "mass", b.Mass);
			ReadValue(j, "friction", b.Friction);
			ReadValue(j, "restitution", b.Restitution);
			if (!ReadVec3(j, "halfExtents", b.HalfExtents)) { err = "bad body halfExtents"; return false; }
			return true;
		}
	}

	std::string SaveSceneToString(const EntityWorld& world)
	{
		Json root;
		root["version"] = 1;
		Json list = Json::array();
		for (const auto& [id, e] : world.All())
		{
			Json j;
			j["id"] = id;
			j["name"] = e.Name;
			if (e.Parent != NullEntity) j["parent"] = e.Parent;
			if (!e.Tags.empty()) j["tags"] = e.Tags;
			if (!e.Active) j["active"] = false;
			j["position"] = ToJson(e.Transform.Position);
			j["rotation"] = ToJson(e.Transform.Rotation);
			j["scale"] = ToJson(e.Transform.Scale);
			if (e.Mesh) j["mesh"] = MeshToJson(*e.Mesh);
			if (e.Light)
			{
				Json l;
				l["color"] = ToJson(e.Light->Color);
				l["intensity"] = Round(e.Light->Intensity);
				l["radius"] = Round(e.Light->Radius);
				j["light"] = l;
			}
			if (e.Body) j["body"] = BodyToJson(*e.Body);
			if (e.Prop) j["prop"] = Json{ { "kind", e.Prop->Kind == PropKind::Rock ? "rock" : "tree" }, { "variant", e.Prop->Variant } };
			if (e.Interact)
			{
				Json points = Json::array();
				for (const InteractionPoint& p : e.Interact->Points)
				{
					Json jp{ { "name", p.Name }, { "position", ToJson(p.Position) }, { "yaw", Round(p.Yaw) } };
					if (!p.Montage.empty()) jp["montage"] = p.Montage;
					if (!p.Requires.empty()) jp["requires"] = p.Requires;
					if (!p.Gives.empty()) jp["gives"] = p.Gives;
					if (p.WarpSeconds != 0.4f) jp["warp"] = Round(p.WarpSeconds);
					points.push_back(std::move(jp));
				}
				j["interact"] = std::move(points);
			}
			if (e.Gameplay)
			{
				Json g = Json::object();   // (an empty component is still a component: a null would read back as none)
				if (!e.Gameplay->Tags.empty()) g["tags"] = e.Gameplay->Tags;
				if (!e.Gameplay->Abilities.empty()) g["abilities"] = e.Gameplay->Abilities;
				if (!e.Gameplay->Effects.empty()) g["effects"] = e.Gameplay->Effects;
				Json attrs = Json::array();
				for (const GameplayAttribute& a : e.Gameplay->Attributes)
					attrs.push_back(Json{ { "name", a.Name }, { "value", Round(a.Value) }, { "min", Round(a.Min) }, { "max", Round(a.Max) } });
				if (!attrs.empty()) g["attributes"] = std::move(attrs);
				j["gameplay"] = std::move(g);
			}
			list.push_back(std::move(j));
		}
		root["entities"] = std::move(list);
		return root.dump(2) + "\n";
	}

	SceneIOResult LoadSceneFromString(EntityWorld& world, const std::string& text)
	{
		auto fail = [](const std::string& msg) { return SceneIOResult{ false, msg }; };
		Json root = Json::parse(text, nullptr, false);
		if (root.is_discarded() || !root.is_object()) return fail("invalid JSON");
		if (root.value("version", 0) != 1) return fail("unsupported scene version");
		auto list = root.find("entities");
		if (list == root.end() || !list->is_array()) return fail("missing 'entities'");

		EntityWorld loaded;
		std::vector<std::pair<EntityId, EntityId>> links;
		try
		{
			for (const Json& j : *list)
			{
				const EntityId id = j.value("id", 0u);
				Entity* e = loaded.Insert(id, j.value("name", "Entity"));
				if (!e) return fail("bad or duplicate entity id " + std::to_string(id));
				if (j.contains("parent")) links.emplace_back(id, j["parent"].get<EntityId>());
				if (j.contains("tags")) e->Tags = j["tags"].get<std::vector<std::string>>();
				ReadValue(j, "active", e->Active);
				if (!ReadVec3(j, "position", e->Transform.Position) || !ReadVec3(j, "rotation", e->Transform.Rotation) ||
					!ReadVec3(j, "scale", e->Transform.Scale))
					return fail("bad transform on '" + e->Name + "'");
				std::string err;
				if (auto it = j.find("mesh"); it != j.end())
				{
					e->Mesh.emplace();
					if (!MeshFromJson(*it, *e->Mesh, err)) return fail(err + " on '" + e->Name + "'");
				}
				if (auto it = j.find("light"); it != j.end())
				{
					e->Light.emplace();
					ReadValue(*it, "intensity", e->Light->Intensity);
					ReadValue(*it, "radius", e->Light->Radius);
					if (!ReadVec3(*it, "color", e->Light->Color)) return fail("bad light color on '" + e->Name + "'");
				}
				if (auto it = j.find("prop"); it != j.end())
				{
					e->Prop.emplace();
					const std::string kind = it->value("kind", "tree");
					if (kind == "tree") e->Prop->Kind = PropKind::Tree;
					else if (kind == "rock") e->Prop->Kind = PropKind::Rock;
					else return fail("unknown prop kind '" + kind + "' on '" + e->Name + "'");
					ReadValue(*it, "variant", e->Prop->Variant);
				}
				if (auto it = j.find("interact"); it != j.end() && it->is_array())
				{
					e->Interact.emplace();
					for (const Json& jp : *it)
					{
						InteractionPoint p;
						p.Name = jp.value("name", "use");
						if (!ReadVec3(jp, "position", p.Position)) return fail("bad interaction point position on '" + e->Name + "'");
						ReadValue(jp, "yaw", p.Yaw);
						p.Montage = jp.value("montage", "");
						p.Requires = jp.value("requires", "");
						p.Gives = jp.value("gives", "");
						ReadValue(jp, "warp", p.WarpSeconds);
						e->Interact->Points.push_back(std::move(p));
					}
				}
				if (auto it = j.find("gameplay"); it != j.end() && it->is_object())
				{
					e->Gameplay.emplace();
					ReadValue(*it, "tags", e->Gameplay->Tags);
					ReadValue(*it, "abilities", e->Gameplay->Abilities);
					ReadValue(*it, "effects", e->Gameplay->Effects);
					if (auto at = it->find("attributes"); at != it->end() && at->is_array())
						for (const Json& ja : *at)
						{
							GameplayAttribute a;
							a.Name = ja.value("name", "");
							ReadValue(ja, "value", a.Value);
							ReadValue(ja, "min", a.Min);
							ReadValue(ja, "max", a.Max);
							if (a.Name.empty()) return fail("gameplay attribute without a name on '" + e->Name + "'");
							e->Gameplay->Attributes.push_back(std::move(a));
						}
				}
				if (auto it = j.find("body"); it != j.end())
				{
					e->Body.emplace();
					if (!BodyFromJson(*it, *e->Body, err)) return fail(err + " on '" + e->Name + "'");
				}
			}
		}
		catch (const Json::exception& ex)
		{
			return fail(std::string("bad value: ") + ex.what());
		}
		for (const auto& [id, parent] : links)
			if (!loaded.Link(id, parent)) return fail("bad parent " + std::to_string(parent) + " for entity " + std::to_string(id));
		world = std::move(loaded);
		return {};
	}

	SceneIOResult SaveSceneToFile(const EntityWorld& world, const std::string& path)
	{
		std::ofstream f(path, std::ios::binary);
		if (!f) return { false, "cannot write " + path };
		f << SaveSceneToString(world);
		return f.good() ? SceneIOResult{} : SceneIOResult{ false, "write failed: " + path };
	}

	SceneIOResult LoadSceneFromFile(EntityWorld& world, const std::string& path)
	{
		std::ifstream f(path, std::ios::binary);
		if (!f) return { false, "cannot read " + path };
		std::stringstream ss;
		ss << f.rdbuf();
		return LoadSceneFromString(world, ss.str());
	}
}
