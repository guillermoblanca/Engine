#include "SceneEditor.h"
#include "GB/Game/SceneIO.h"
#include "TestScenarios.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>

namespace GB::Editor
{
	namespace
	{
		namespace fs = std::filesystem;

		std::string Lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		bool ParseFloat(const std::string& s, float& out)
		{
			if (s.empty()) return false;
			char* end = nullptr;
			const float v = std::strtof(s.c_str(), &end);
			if (end != s.c_str() + s.size() || !std::isfinite(v)) return false;
			out = v;
			return true;
		}

		// Reads `count` numbers starting at args[first]; false if any is missing or not a number.
		bool ParseFloats(const std::vector<std::string>& args, size_t first, size_t count, float* out)
		{
			if (args.size() < first + count) return false;
			for (size_t i = 0; i < count; ++i)
				if (!ParseFloat(args[first + i], out[i])) return false;
			return true;
		}

		// Optional trailing numbers: reads up to `count` values after `first` (as many as there are).
		bool ParseOptionalFloats(const std::vector<std::string>& args, size_t first, size_t count, float* out)
		{
			if (args.size() <= first) return true;
			return ParseFloats(args, first, std::min(count, args.size() - first), out);
		}

		std::string Join(const std::vector<std::string>& args, size_t first)
		{
			std::string s;
			for (size_t i = first; i < args.size(); ++i) s += (i > first ? " " : "") + args[i];
			return s;
		}

		bool IsModelPath(const std::string& p)
		{
			const std::string ext = Lower(fs::path(p).extension().string());
			return ext == ".glb" || ext == ".gltf";
		}

		const char* ShapeName(MeshShape s) { return s == MeshShape::Sphere ? "sphere" : s == MeshShape::Plane ? "plane" : "cube"; }
	}

	// ------------------------------------------------------------------ scene content
	EntityId SceneEditor::AddPrimitive(MeshShape shape, const glm::vec3& position)
	{
		const char* name = shape == MeshShape::Sphere ? "Sphere" : shape == MeshShape::Plane ? "Plane" : "Cube";
		const EntityId id = m_World.Create(name);
		Entity* e = m_World.Find(id);
		e->Transform.Position = position;
		e->Mesh.emplace().Shape = shape;
		m_Selected = id;
		return id;
	}

	EntityId SceneEditor::AddLight(const glm::vec3& position)
	{
		const EntityId id = m_World.Create("Light");
		Entity* e = m_World.Find(id);
		e->Transform.Position = position;
		e->Light.emplace().Radius = 20.0f;
		m_Selected = id;
		return id;
	}

	EntityId SceneEditor::AddModel(const std::string& path, const glm::vec3& position, std::string* error)
	{
		auto fail = [&](const std::string& msg) { if (error) *error = msg; return NullEntity; };
		if (!IsModelPath(path)) return fail("'" + path + "' is not a .glb/.gltf file");
		std::error_code ec;
		fs::path stored = path;
		if (stored.is_absolute())
		{
			if (!fs::exists(stored, ec)) return fail("file not found: " + path);
			if (!m_AssetRoot.empty())
			{
				const fs::path rel = fs::relative(stored, m_AssetRoot, ec);
				if (!ec && !rel.empty() && *rel.begin() != "..") stored = rel;
			}
		}
		else
		{
			const fs::path full = m_AssetRoot.empty() ? stored : fs::path(m_AssetRoot) / stored;
			if (!fs::exists(full, ec)) return fail("model not found: " + path + (m_AssetRoot.empty() ? "" : " (looked in " + m_AssetRoot + ")"));
		}
		const std::string model = stored.generic_string();
		const std::string name = fs::path(model).stem().string();
		const EntityId id = m_World.Create(name.empty() ? "Model" : name);
		Entity* e = m_World.Find(id);
		e->Transform.Position = position;
		e->Mesh.emplace().Model = model;
		m_Selected = id;
		return id;
	}

	EntityId SceneEditor::Duplicate(EntityId id)
	{
		const EntityId copy = m_World.Duplicate(id);
		if (copy != NullEntity) m_Selected = copy;
		return copy;
	}

	bool SceneEditor::Delete(EntityId id)
	{
		// the selection survives only if it is not inside the deleted subtree
		for (const Entity* e = m_World.Find(m_Selected); e; e = m_World.Find(e->Parent))
			if (e->Id == id) { m_Selected = NullEntity; break; }
		return m_World.Destroy(id);
	}

	void SceneEditor::Clear()
	{
		m_World.Clear();
		m_Selected = NullEntity;
	}

	void SceneEditor::RemoveEnvironment()
	{
		for (EntityId id : m_World.FindByTag("world"))
			if (m_World.Find(id)) Delete(id);   // a group takes its children along
	}

	EntityId SceneEditor::Group(const std::string& name)
	{
		const EntityId g = m_World.Create(name);
		m_World.Find(g)->Tags.push_back("world");
		return g;
	}

	size_t SceneEditor::PopulateEnvironment(const std::vector<TreePlacement>& trees, const std::vector<RockPlacement>& rocks)
	{
		RemoveEnvironment();
		// Names are numbered explicitly: letting EntityWorld disambiguate thousands of equal names is quadratic.
		auto fill = [&](const char* groupName, const char* prefix, PropKind kind, const auto& placements)
		{
			if (placements.empty()) return;
			const EntityId group = Group(groupName);
			size_t n = 0;
			for (const auto& pl : placements)
			{
				const EntityId id = m_World.Create(std::string(prefix) + " " + std::to_string(++n), group);
				Entity* e = m_World.Find(id);
				e->Transform.Position = pl.Position;
				e->Transform.Rotation.y = glm::degrees(pl.Yaw);
				e->Transform.Scale = glm::vec3(pl.Scale);
				e->Prop = PropComponent{ kind, pl.Variant };
			}
		};
		fill("Trees", "Tree", PropKind::Tree, trees);
		fill("Rocks", "Rock", PropKind::Rock, rocks);
		return trees.size() + rocks.size();
	}

	EntityId SceneEditor::Resolve(const std::string& token) const
	{
		if (token.empty()) return m_Selected;
		if (std::all_of(token.begin(), token.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
		{
			const EntityId id = static_cast<EntityId>(std::strtoul(token.c_str(), nullptr, 10));
			if (m_World.Find(id)) return id;
		}
		return m_World.FindByName(token);
	}

	std::vector<EntityId> SceneEditor::Find(const std::string& filter) const
	{
		std::vector<EntityId> out;
		const std::string needle = Lower(filter);
		for (const auto& [id, e] : m_World.All())
			if (needle.empty() || Lower(e.Name).find(needle) != std::string::npos) out.push_back(id);
		return out;
	}

	// ------------------------------------------------------------------ picking
	bool SceneEditor::WorldBounds(EntityId id, Sphere& out) const
	{
		const Entity* e = m_World.Find(id);
		if (!e) return false;
		glm::vec3 center(0.0f);
		float radius = 0.0f;
		if (e->Mesh && e->Mesh->Model.empty())
		{
			const float size = e->Mesh->Size;
			radius = e->Mesh->Shape == MeshShape::Sphere ? size * 0.5f : e->Mesh->Shape == MeshShape::Plane ? size * 0.7071f : size * 0.866f;
		}
		else if (e->Mesh || e->Prop)
		{
			radius = 1.0f;
			if (m_Bounds && !m_Bounds(id, *e, center, radius)) { center = glm::vec3(0.0f); radius = 1.0f; }
		}
		else if (e->Light) radius = 0.4f;
		else return false;   // groups have nothing to pick

		const glm::mat4 m = m_World.WorldMatrix(id);
		const float scale = std::max({ glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])) });
		out.Center = glm::vec3(m * glm::vec4(center, 1.0f));
		out.Radius = std::max(radius * scale, 0.05f);
		return true;
	}

	EntityId SceneEditor::Pick(const glm::vec3& origin, const glm::vec3& direction) const
	{
		const float len = glm::length(direction);
		if (len < 1e-8f) return NullEntity;
		const glm::vec3 d = direction / len;
		EntityId best = NullEntity;
		float bestT = 1e30f;
		for (const auto& [id, e] : m_World.All())
		{
			Sphere s;
			if (!m_World.IsActive(id) || !WorldBounds(id, s)) continue;
			const glm::vec3 oc = origin - s.Center;
			const float b = glm::dot(oc, d), c = glm::dot(oc, oc) - s.Radius * s.Radius;
			const float disc = b * b - c;
			if (disc < 0.0f) continue;
			float t = -b - std::sqrt(disc);
			if (t < 0.0f) t = -b + std::sqrt(disc);
			if (t < 0.0f) continue;
			// A floor's bounding sphere covers everything on it: it only wins when nothing else was hit.
			if (e.Mesh && e.Mesh->Model.empty() && e.Mesh->Shape == MeshShape::Plane) t += 1000.0f;
			if (t < bestT) { bestT = t; best = id; }
		}
		return best;
	}

	std::string SceneEditor::Describe(const Entity& e)
	{
		char buf[192];
		std::snprintf(buf, sizeof buf, "[%u] %s at %.2f %.2f %.2f", e.Id, e.Name.c_str(), static_cast<double>(e.Transform.Position.x),
			static_cast<double>(e.Transform.Position.y), static_cast<double>(e.Transform.Position.z));
		std::string s = buf;
		if (e.Mesh) s += e.Mesh->Model.empty() ? std::string(" ") + ShapeName(e.Mesh->Shape) : " model " + e.Mesh->Model;
		if (e.Light) s += " light";
		if (e.Prop) s += e.Prop->Kind == PropKind::Tree ? " tree" : " rock";
		if (!e.Active) s += " (inactive)";
		return s;
	}

	// ------------------------------------------------------------------ commands
	void RegisterSceneCommands(CommandRunner& cmd, SceneEditor& ed, SceneHooks hooks)
	{
		// Position for `add`: three numbers after the thing being added, or the spawn point.
		auto where = [hooks](const std::vector<std::string>& a, size_t first, glm::vec3& p) -> bool
		{
			p = hooks.SpawnPoint ? hooks.SpawnPoint() : glm::vec3(0.0f);
			if (a.size() <= first) return true;
			float v[3];
			if (!ParseFloats(a, first, 3, v)) return false;
			p = { v[0], v[1], v[2] };
			return true;
		};
		auto noTarget = [](const std::string& t) { return CommandResult{ false, t.empty() ? "nothing is selected (select <name|id>)" : "no entity '" + t + "'" }; };

		cmd.Register("new_scene", "new_scene: an empty scene (camera and sun only, no generated world)", [&ed, hooks](const std::vector<std::string>&) -> CommandResult
		{
			ed.Clear();
			if (hooks.ClearWorld) hooks.ClearWorld();
			if (hooks.SceneReplaced) hooks.SceneReplaced();
			return { true, "new empty scene" };
		});
		cmd.Register("add", "add <cube|sphere|plane|light|file.glb|file.gltf> [x y z]: adds an object (models are relative to the assets folder)",
			[&ed, where](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: add <cube|sphere|plane|light|model.glb> [x y z]" };
			glm::vec3 p;
			if (!where(a, 1, p)) return { false, "usage: add <what> [x y z]  (position needs three numbers)" };
			const std::string what = Lower(a[0]);
			EntityId id = NullEntity;
			if (what == "cube") id = ed.AddPrimitive(MeshShape::Cube, p);
			else if (what == "sphere") id = ed.AddPrimitive(MeshShape::Sphere, p);
			else if (what == "plane") id = ed.AddPrimitive(MeshShape::Plane, p);
			else if (what == "light") id = ed.AddLight(p);
			else
			{
				std::string err;
				id = ed.AddModel(a[0], p, &err);
				if (id == NullEntity) return { false, err };
			}
			return { true, "added " + SceneEditor::Describe(*ed.World().Find(id)) };
		});
		cmd.Register("objects", "objects [filter]: lists the entities (name contains filter)", [&ed](const std::vector<std::string>& a) -> CommandResult
		{
			const std::vector<EntityId> ids = ed.Find(a.empty() ? "" : a[0]);
			std::string out = std::to_string(ids.size()) + " entities";
			constexpr size_t kMax = 40;
			for (size_t i = 0; i < ids.size() && i < kMax; ++i)
				out += std::string("\n") + (ids[i] == ed.Selected() ? "* " : "  ") + SceneEditor::Describe(*ed.World().Find(ids[i]));
			if (ids.size() > kMax) out += "\n  ... " + std::to_string(ids.size() - kMax) + " more (use a filter)";
			return { true, out };
		});
		cmd.Register("select", "select <name|id|none>: picks the entity the other commands act on", [&ed, noTarget](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: select <name|id|none>" };
			const std::string t = Join(a, 0);
			if (t == "none") { ed.Select(NullEntity); return { true, "selection cleared" }; }
			const EntityId id = ed.Resolve(t);
			if (id == NullEntity) return noTarget(t);
			ed.Select(id);
			return { true, "selected " + SceneEditor::Describe(*ed.World().Find(id)) };
		});
		cmd.Register("delete", "delete [name|id]: removes an entity (default: the selection) and its children", [&ed, noTarget](const std::vector<std::string>& a) -> CommandResult
		{
			const std::string t = Join(a, 0);
			const EntityId id = ed.Resolve(t);
			if (id == NullEntity) return noTarget(t);
			const std::string name = ed.World().Find(id)->Name;
			ed.Delete(id);
			return { true, "deleted " + name };
		});
		cmd.Register("duplicate", "duplicate [name|id]: copies an entity (default: the selection) and selects the copy", [&ed, noTarget](const std::vector<std::string>& a) -> CommandResult
		{
			const std::string t = Join(a, 0);
			const EntityId id = ed.Resolve(t);
			if (id == NullEntity) return noTarget(t);
			const EntityId copy = ed.Duplicate(id);
			return { true, "duplicated as " + SceneEditor::Describe(*ed.World().Find(copy)) };
		});

		// move / rotate / scale: numbers first, then an optional target (default: the selection).
		auto transformCommand = [&cmd, &ed, noTarget](const char* name, const char* help, size_t minNumbers, size_t maxNumbers, auto apply)
		{
			cmd.Register(name, help, [&ed, noTarget, minNumbers, maxNumbers, apply](const std::vector<std::string>& a) -> CommandResult
			{
				float v[3] = {};
				size_t n = 0;
				while (n < maxNumbers && n < a.size() && ParseFloat(a[n], v[n])) ++n;
				if (n < minNumbers || (n != minNumbers && n != maxNumbers)) return { false, "wrong number of values (see help)" };
				const std::string t = Join(a, n);
				const EntityId id = ed.Resolve(t);
				if (id == NullEntity) return noTarget(t);
				Entity* e = ed.World().Find(id);
				apply(*e, v, n);
				return { true, SceneEditor::Describe(*e) };
			});
		};
		transformCommand("move", "move <x> <y> <z> [name|id]: puts the entity at a position (relative to its parent)", 3, 3,
			[](Entity& e, const float* v, size_t) { e.Transform.Position = { v[0], v[1], v[2] }; });
		transformCommand("rotate", "rotate <pitch> <yaw> <roll> [name|id]: Euler angles in degrees", 3, 3,
			[](Entity& e, const float* v, size_t) { e.Transform.Rotation = { v[0], v[1], v[2] }; });
		transformCommand("scale", "scale <s> | <sx> <sy> <sz> [name|id]: uniform or per-axis scale", 1, 3,
			[](Entity& e, const float* v, size_t n) { e.Transform.Scale = n == 1 ? glm::vec3(v[0]) : glm::vec3(v[0], v[1], v[2]); });

		cmd.Register("color", "color <r> <g> <b> [metallic roughness]: material of the selected object", [&ed](const std::vector<std::string>& a) -> CommandResult
		{
			Entity* e = ed.World().Find(ed.Selected());
			if (!e || !e->Mesh) return { false, "select an object with a mesh first" };
			float v[5] = { 0, 0, 0, e->Mesh->Metallic, e->Mesh->Roughness };
			if (!ParseFloats(a, 0, 3, v) || !ParseOptionalFloats(a, 3, 2, v + 3))
				return { false, "usage: color <r> <g> <b> [metallic roughness]" };
			e->Mesh->Albedo = { v[0], v[1], v[2] };
			e->Mesh->Metallic = std::clamp(v[3], 0.0f, 1.0f);
			e->Mesh->Roughness = std::clamp(v[4], 0.0f, 1.0f);
			return { true, e->Name + " material updated" };
		});
		cmd.Register("emissive", "emissive <r> <g> <b>: glow of the selected object (values above 1 bloom)", [&ed](const std::vector<std::string>& a) -> CommandResult
		{
			Entity* e = ed.World().Find(ed.Selected());
			float v[3];
			if (!e || !e->Mesh) return { false, "select an object with a mesh first" };
			if (!ParseFloats(a, 0, 3, v)) return { false, "usage: emissive <r> <g> <b>" };
			e->Mesh->Emissive = { v[0], v[1], v[2] };
			return { true, e->Name + " emissive updated" };
		});
		cmd.Register("light", "light <r> <g> <b> [intensity [radius]]: color of the selected light", [&ed](const std::vector<std::string>& a) -> CommandResult
		{
			Entity* e = ed.World().Find(ed.Selected());
			if (!e || !e->Light) return { false, "select a light first (add light)" };
			float v[5] = {};
			if (!ParseFloats(a, 0, 3, v) || !ParseOptionalFloats(a, 3, 2, v + 3))
				return { false, "usage: light <r> <g> <b> [intensity [radius]]" };
			e->Light->Color = { v[0], v[1], v[2] };
			if (a.size() > 3) e->Light->Intensity = v[3];
			if (a.size() > 4) e->Light->Radius = v[4];
			return { true, e->Name + " light updated" };
		});
		cmd.Register("drop", "drop [name|id]: sets the entity on the terrain surface", [&ed, hooks, noTarget](const std::vector<std::string>& a) -> CommandResult
		{
			if (!hooks.Ground) return { false, "there is no terrain (generate a world first)" };
			const std::string t = Join(a, 0);
			const EntityId id = ed.Resolve(t);
			if (id == NullEntity) return noTarget(t);
			Entity* e = ed.World().Find(id);
			const glm::vec3 w = glm::vec3(ed.World().WorldMatrix(id)[3]);
			e->Transform.Position.y += hooks.Ground(w.x, w.z) - w.y;   // the difference in world height (parents may transform)
			return { true, SceneEditor::Describe(*e) };
		});

		cmd.Register("scene_save", "scene_save <file.json>: saves the entities (generated trees and rocks included)", [&ed](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: scene_save <file.json>" };
			const SceneIOResult r = SaveSceneToFile(ed.World(), a[0]);
			return { r.Ok, r.Ok ? "saved " + std::to_string(ed.World().Count()) + " entities to " + a[0] : r.Error };
		});
		cmd.Register("scene_load", "scene_load <file.json>: replaces the entities with a saved scene", [&ed, hooks](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: scene_load <file.json>" };
			const SceneIOResult r = LoadSceneFromFile(ed.World(), a[0]);
			ed.Select(NullEntity);
			if (r.Ok && hooks.SceneReplaced) hooks.SceneReplaced();
			return { r.Ok, r.Ok ? "loaded " + std::to_string(ed.World().Count()) + " entities from " + a[0] : r.Error };
		});

		cmd.Register("tests", "tests: lists the Sandbox test scenes you can run with `test <name>`", [](const std::vector<std::string>&) -> CommandResult
		{
			std::string out = "test scenes:";
			for (const TestScenario& s : TestScenarios())
				out += "\n  " + s.Name + (s.Approximate ? " (approximation)" : "") + ": " + s.Description;
			out += "\n`test <name>` builds it in the viewport; `test <name> sandbox` opens the original in its own window";
			return { true, out };
		});
		cmd.Register("test", "test <name> [sandbox]: builds a Sandbox test scene in the viewport (sandbox = run the original in its own window)",
			[hooks](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty()) return { false, "usage: test <name> [sandbox]  (see tests)" };
			const TestScenario* s = FindTestScenario(Lower(a[0]));
			if (!s) return { false, "unknown test scene '" + a[0] + "' (see tests)" };
			if (a.size() > 1 && Lower(a[1]) == "sandbox")
				return hooks.RunExternal ? hooks.RunExternal(s->Name) : CommandResult{ false, "the Sandbox executable is not available" };
			if (!hooks.RunScript) return { false, "cannot run scripts here" };
			hooks.RunScript(s->Script);
			return { true, "test scene '" + s->Name + "': " + s->Title };
		});
	}
}
