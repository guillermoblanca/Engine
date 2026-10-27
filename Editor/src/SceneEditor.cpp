#include "SceneEditor.h"
#include "GB/Game/SceneIO.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>

namespace GB::Editor
{
	// ------------------------------------------------------------------ geometry
	Ray ScreenRay(const Camera& camera, glm::vec2 mouse, float aspect)
	{
		const glm::mat4 inv = glm::inverse(camera.GetProjection(aspect) * camera.GetView());
		const glm::vec2 ndc(mouse.x * 2.0f - 1.0f, 1.0f - mouse.y * 2.0f);
		glm::vec4 n = inv * glm::vec4(ndc, -1.0f, 1.0f);
		glm::vec4 f = inv * glm::vec4(ndc, 1.0f, 1.0f);
		const glm::vec3 a = glm::vec3(n) / n.w, b = glm::vec3(f) / f.w;
		return { a, glm::normalize(b - a) };
	}

	namespace
	{
		bool HitBox(const glm::vec3& o, const glm::vec3& d, const glm::vec3& half, float& t)
		{
			float t0 = 0.0f, t1 = 1e30f;
			for (int i = 0; i < 3; ++i)
			{
				if (std::fabs(d[i]) < 1e-9f)
				{
					if (o[i] < -half[i] || o[i] > half[i]) return false;
					continue;
				}
				float a = (-half[i] - o[i]) / d[i], b = (half[i] - o[i]) / d[i];
				if (a > b) std::swap(a, b);
				t0 = std::max(t0, a);
				t1 = std::min(t1, b);
				if (t0 > t1) return false;
			}
			t = t0;
			return true;
		}

		bool HitSphere(const glm::vec3& o, const glm::vec3& d, float r, float& t)
		{
			const float a = glm::dot(d, d), b = 2.0f * glm::dot(o, d), c = glm::dot(o, o) - r * r;
			const float disc = b * b - 4.0f * a * c;
			if (disc < 0.0f) return false;
			const float s = std::sqrt(disc);
			float t0 = (-b - s) / (2.0f * a);
			if (t0 < 0.0f) t0 = (-b + s) / (2.0f * a);
			if (t0 < 0.0f) return false;
			t = t0;
			return true;
		}
	}

	bool RayHitEntity(const EntityWorld& world, EntityId id, const Ray& ray, float& t)
	{
		const Entity* e = world.Find(id);
		if (!e || !world.IsActive(id) || (!e->Mesh && !e->Light)) return false;
		const glm::mat4 m = world.WorldMatrix(id);
		if (!e->Mesh || !e->Mesh->Model.empty())
		{
			// lights and models: a sphere around the origin (world scale for lights)
			const float r = e->Mesh ? glm::max(glm::length(glm::vec3(m[0])), glm::max(glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])))) : 0.3f;
			return HitSphere(ray.Origin - glm::vec3(m[3]), ray.Dir, r, t);
		}
		// the ray in the entity's local space; keeping Dir unnormalized keeps `t` the same in both spaces
		const glm::mat4 inv = glm::inverse(m);
		const glm::vec3 o = glm::vec3(inv * glm::vec4(ray.Origin, 1.0f));
		const glm::vec3 d = glm::vec3(inv * glm::vec4(ray.Dir, 0.0f));
		const float h = e->Mesh->Size * 0.5f;
		switch (e->Mesh->Shape)
		{
		case MeshShape::Sphere: return HitSphere(o, d, h, t);
		case MeshShape::Plane: return HitBox(o, d, { h, 0.02f, h }, t);
		case MeshShape::Cube: return HitBox(o, d, glm::vec3(h), t);
		}
		return false;
	}

	float RayAxisParam(const Ray& ray, const glm::vec3& axisOrigin, const glm::vec3& axisDir)
	{
		const glm::vec3 w0 = ray.Origin - axisOrigin;
		const float a = glm::dot(ray.Dir, ray.Dir), b = glm::dot(ray.Dir, axisDir), c = glm::dot(axisDir, axisDir);
		const float d = glm::dot(ray.Dir, w0), e = glm::dot(axisDir, w0);
		const float denom = a * c - b * b;
		if (std::fabs(denom) < 1e-8f) return 0.0f;
		return (a * e - b * d) / denom;
	}

	bool ProjectToScreen(const glm::mat4& viewProj, const glm::vec3& p, glm::vec2 size, glm::vec2& out)
	{
		const glm::vec4 c = viewProj * glm::vec4(p, 1.0f);
		if (c.w <= 1e-5f) return false;
		out = { (c.x / c.w * 0.5f + 0.5f) * size.x, (1.0f - (c.y / c.w * 0.5f + 0.5f)) * size.y };
		return true;
	}

	// ------------------------------------------------------------------ selection and undo
	SceneEditor::SceneEditor() { m_Current = SaveSceneToString(m_World); }

	void SceneEditor::Select(EntityId id)
	{
		m_Selected = m_World.Find(id) ? id : NullEntity;
		Rebind();
	}

	EntityId SceneEditor::Pick(const Ray& ray) const
	{
		EntityId best = NullEntity;
		float bestT = 1e30f;
		for (const auto& [id, e] : m_World.All())
		{
			float t;
			if (e.Prop && m_PropBounds)   // trees and rocks: a sphere around the prop
			{
				glm::vec3 center(0.0f);
				float radius = 1.0f;
				if (!m_World.IsActive(id) || !m_PropBounds(*e.Prop, center, radius)) continue;
				const glm::mat4 m = m_World.WorldMatrix(id);
				const float scale = std::max({ glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])) });
				if (HitSphere(ray.Origin - glm::vec3(m * glm::vec4(center, 1.0f)), ray.Dir, radius * scale, t) && t < bestT) { bestT = t; best = id; }
			}
			else if (RayHitEntity(m_World, id, ray, t) && t < bestT) { bestT = t; best = id; }
		}
		return best;
	}

	EntityId SceneEditor::Resolve(const std::string& token) const
	{
		if (token == "sel") return m_Selected;
		if (!token.empty() && std::all_of(token.begin(), token.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
		{
			const EntityId id = static_cast<EntityId>(std::strtoul(token.c_str(), nullptr, 10));
			if (m_World.Find(id)) return id;
		}
		return m_World.FindByName(token);
	}

	void SceneEditor::Commit()
	{
		std::string now = SaveSceneToString(m_World);
		if (now == m_Current) return;
		m_Undo.push_back(std::move(m_Current));
		if (m_Undo.size() > 100) m_Undo.erase(m_Undo.begin());
		m_Current = std::move(now);
		m_Redo.clear();
	}

	void SceneEditor::Restore(const std::string& snapshot)
	{
		LoadSceneFromString(m_World, snapshot);
		m_Current = snapshot;
		if (!m_World.Find(m_Selected)) m_Selected = NullEntity;
		m_Dirty = false;
		Rebind();
	}

	bool SceneEditor::Undo()
	{
		Commit();   // pending edits become their own step first
		if (m_Undo.empty()) return false;
		m_Redo.push_back(m_Current);
		const std::string prev = std::move(m_Undo.back());
		m_Undo.pop_back();
		Restore(prev);
		return true;
	}

	bool SceneEditor::Redo()
	{
		if (m_Redo.empty()) return false;
		m_Undo.push_back(m_Current);
		const std::string next = std::move(m_Redo.back());
		m_Redo.pop_back();
		Restore(next);
		return true;
	}

	// ------------------------------------------------------------------ properties
	void SceneEditor::Bind(PropertyRegistry& props)
	{
		m_Props = &props;
		Rebind();
	}

	void SceneEditor::Rebind()
	{
		if (!m_Props) return;
		m_Props->RemovePrefix("sel.");
		GB::Entity* e = m_World.Find(m_Selected);
		if (!e) return;
		auto dirty = [this](Property& p) { p.OnChange = [this] { MarkDirty(); }; };
		PropertyRegistry& r = *m_Props;
		dirty(r.Vec3("sel.position", e->Transform.Position, -1000.0f, 1000.0f, "Transform", "local position"));
		dirty(r.Vec3("sel.rotation", e->Transform.Rotation, -180.0f, 180.0f, "Transform", "local rotation in degrees (pitch, yaw, roll)"));
		dirty(r.Vec3("sel.scale", e->Transform.Scale, 0.001f, 100.0f, "Transform", "local scale"));
		dirty(r.Bool("sel.active", e->Active, "Transform", "inactive entities and their children are not drawn"));
		if (e->Mesh)
		{
			MeshComponent& m = *e->Mesh;
			dirty(r.Enum("sel.mesh.shape", m.Shape, { "cube", "sphere", "plane" }, "Mesh"));
			dirty(r.Float("sel.mesh.size", m.Size, 0.01f, 100.0f, "Mesh"));
			dirty(r.Color("sel.mesh.albedo", m.Albedo, "Mesh"));
			dirty(r.Float("sel.mesh.metallic", m.Metallic, 0.0f, 1.0f, "Mesh"));
			dirty(r.Float("sel.mesh.roughness", m.Roughness, 0.0f, 1.0f, "Mesh"));
			dirty(r.Color("sel.mesh.emissive", m.Emissive, "Mesh"));
		}
		if (e->Light)
		{
			LightComponent& l = *e->Light;
			dirty(r.Color("sel.light.color", l.Color, "Light"));
			dirty(r.Float("sel.light.intensity", l.Intensity, 0.0f, 500.0f, "Light"));
			dirty(r.Float("sel.light.radius", l.Radius, 0.0f, 100.0f, "Light", "0 = no limit"));
		}
		if (e->Body)
		{
			BodyComponent& b = *e->Body;
			dirty(r.Enum("sel.body.type", b.Type, { "static", "dynamic" }, "Body"));
			dirty(r.Enum("sel.body.shape", b.Shape, { "box", "sphere", "capsule" }, "Body"));
			dirty(r.Vec3("sel.body.halfExtents", b.HalfExtents, 0.01f, 50.0f, "Body"));
			dirty(r.Float("sel.body.mass", b.Mass, 0.01f, 10000.0f, "Body"));
			dirty(r.Float("sel.body.friction", b.Friction, 0.0f, 2.0f, "Body"));
			dirty(r.Float("sel.body.restitution", b.Restitution, 0.0f, 1.0f, "Body"));
		}
	}

	// ------------------------------------------------------------------ files
	CommandResult SceneEditor::NewScene()
	{
		m_World.Clear();
		m_Selected = NullEntity;
		if (OnNewScene)
		{
			OnNewScene();
			ResetHistory();   // the generated world cannot be brought back by undo
		}
		else Commit();
		Rebind();
		return { true, "new scene" };
	}

	void SceneEditor::ResetHistory()
	{
		m_Undo.clear();
		m_Redo.clear();
		m_Dirty = false;
		m_Current = SaveSceneToString(m_World);
	}

	EntityId SceneEditor::Group(const std::string& name)
	{
		const EntityId g = m_World.Create(name);
		m_World.Find(g)->Tags.push_back("world");
		return g;
	}

	void SceneEditor::RemoveEnvironment()
	{
		for (EntityId id : m_World.FindByTag("world"))
			m_World.Destroy(id);   // a group takes its children along
		if (!m_World.Find(m_Selected)) m_Selected = NullEntity;
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
				GB::Entity* e = m_World.Find(id);
				e->Transform.Position = pl.Position;
				e->Transform.Rotation.y = glm::degrees(pl.Yaw);
				e->Transform.Scale = glm::vec3(pl.Scale);
				e->Prop = PropComponent{ kind, pl.Variant };
			}
		};
		fill("Trees", "Tree", PropKind::Tree, trees);
		fill("Rocks", "Rock", PropKind::Rock, rocks);
		Rebind();
		ResetHistory();
		return trees.size() + rocks.size();
	}

	CommandResult SceneEditor::Add(const std::vector<std::string>& a)
	{
		namespace fs = std::filesystem;
		auto fail = [](const std::string& m) { return CommandResult{ false, m }; };
		auto lower = [](std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return s; };
		if (a.empty()) return fail("usage: add <cube|sphere|plane|light|model.glb> [x y z]");
		glm::vec3 p = m_Spawn;
		if (a.size() > 1)
		{
			if (a.size() < 4) return fail("usage: add <what> [x y z]  (a position needs three numbers)");
			for (size_t i = 0; i < 3; ++i)
			{
				char* end = nullptr;
				p[static_cast<glm::length_t>(i)] = std::strtof(a[i + 1].c_str(), &end);
				if (end == a[i + 1].c_str() || *end != 0) return fail("not a number: " + a[i + 1]);
			}
		}
		const std::string what = lower(a[0]);
		EntityId id = NullEntity;
		if (what == "cube" || what == "sphere" || what == "plane" || what == "light")
		{
			id = m_World.Create(std::string(1, static_cast<char>(std::toupper(what[0]))) + what.substr(1));
			GB::Entity* e = m_World.Find(id);
			if (what == "light") e->Light.emplace().Radius = 20.0f;
			else
			{
				e->Mesh.emplace().Shape = what == "sphere" ? MeshShape::Sphere : what == "plane" ? MeshShape::Plane : MeshShape::Cube;
				if (what == "plane") e->Mesh->Size = 10.0f;
			}
			e->Transform.Position = p;
		}
		else
		{
			const std::string ext = lower(fs::path(a[0]).extension().string());
			if (ext != ".glb" && ext != ".gltf") return fail("not cube, sphere, plane, light or a .glb/.gltf model: " + a[0]);
			std::error_code ec;
			fs::path stored = a[0];
			if (stored.is_absolute())
			{
				if (!fs::exists(stored, ec)) return fail("file not found: " + a[0]);
				if (!m_AssetRoot.empty())
				{
					const fs::path rel = fs::relative(stored, m_AssetRoot, ec);
					if (!ec && !rel.empty() && *rel.begin() != "..") stored = rel;
				}
			}
			else if (!fs::exists(m_AssetRoot.empty() ? stored : fs::path(m_AssetRoot) / stored, ec))
				return fail("model not found: " + a[0] + (m_AssetRoot.empty() ? "" : " (looked in " + m_AssetRoot + ")"));
			const std::string model = stored.generic_string();
			id = m_World.Create(fs::path(model).stem().string());
			GB::Entity* e = m_World.Find(id);
			e->Mesh.emplace().Model = model;
			e->Transform.Position = p;
		}
		Commit();
		Select(id);
		return { true, "added " + Describe(id) };
	}

	CommandResult SceneEditor::Save(const std::string& path)
	{
		const SceneIOResult r = SaveSceneToFile(m_World, path);
		return { r.Ok, r.Ok ? "saved " + path + " (" + std::to_string(m_World.Count()) + " entities)" : r.Error };
	}

	CommandResult SceneEditor::Load(const std::string& path)
	{
		const SceneIOResult r = LoadSceneFromFile(m_World, path);
		if (!r.Ok) return { false, r.Error };
		if (!m_World.Find(m_Selected)) m_Selected = NullEntity;
		Commit();
		Rebind();
		return { true, "loaded " + path + " (" + std::to_string(m_World.Count()) + " entities)" };
	}

	std::string SceneEditor::Describe(EntityId id) const
	{
		const GB::Entity* e = m_World.Find(id);
		if (!e) return "";
		char buf[256];
		std::snprintf(buf, sizeof buf, "#%u %s%s pos(%.3g %.3g %.3g) rot(%.3g %.3g %.3g) scale(%.3g %.3g %.3g)%s%s%s", e->Id, e->Name.c_str(), e->Active ? "" : " [inactive]",
			static_cast<double>(e->Transform.Position.x), static_cast<double>(e->Transform.Position.y), static_cast<double>(e->Transform.Position.z),
			static_cast<double>(e->Transform.Rotation.x), static_cast<double>(e->Transform.Rotation.y), static_cast<double>(e->Transform.Rotation.z),
			static_cast<double>(e->Transform.Scale.x), static_cast<double>(e->Transform.Scale.y), static_cast<double>(e->Transform.Scale.z),
			e->Mesh ? " mesh" : "", e->Light ? " light" : "", e->Body ? " body" : "");
		return buf;
	}

	// ------------------------------------------------------------------ commands
	CommandResult SceneEditor::EntityCommand(const std::vector<std::string>& a)
	{
		auto fail = [](const std::string& m) { return CommandResult{ false, m }; };
		if (a.empty()) return fail("entity create|delete|duplicate|rename|parent|select|add|remove|model|set|info|list ...");
		const std::string& sub = a[0];
		auto target = [&](size_t index) { return index < a.size() ? Resolve(a[index]) : m_Selected; };

		if (sub == "create")
		{
			// entity create [empty|cube|sphere|plane|light] [name] [parent]
			const std::string kind = a.size() > 1 ? a[1] : "empty";
			const std::string name = a.size() > 2 ? a[2] : (kind == "empty" ? "Entity" : kind == "light" ? "Light" : std::string(1, static_cast<char>(std::toupper(kind[0]))) + kind.substr(1));
			const EntityId parent = a.size() > 3 ? Resolve(a[3]) : NullEntity;
			if (a.size() > 3 && parent == NullEntity) return fail("unknown parent '" + a[3] + "'");
			if (kind != "empty" && kind != "cube" && kind != "sphere" && kind != "plane" && kind != "light") return fail("unknown kind '" + kind + "' (empty, cube, sphere, plane, light)");
			const EntityId id = m_World.Create(name, parent);
			GB::Entity* e = m_World.Find(id);
			if (parent == NullEntity) e->Transform.Position = m_Spawn;
			if (kind == "light") { e->Light.emplace(); e->Transform.Position += glm::vec3(0.0f, 3.0f, 0.0f); }
			else if (kind != "empty")
			{
				e->Mesh.emplace().Shape = kind == "sphere" ? MeshShape::Sphere : kind == "plane" ? MeshShape::Plane : MeshShape::Cube;
				if (kind == "plane") e->Mesh->Size = 10.0f;
			}
			Commit();
			Select(id);
			return { true, "created " + Describe(id) };
		}
		if (sub == "list")
		{
			std::string out;
			for (const auto& [id, e] : m_World.All()) out += (id == m_Selected ? "* " : "  ") + Describe(id) + (e.Parent != NullEntity ? " parent=#" + std::to_string(e.Parent) : "") + "\n";
			return { true, out.empty() ? "(no entities)" : out.substr(0, out.size() - 1) };
		}

		if (sub == "add" || sub == "remove")
		{
			// entity add <mesh|light|body> [entity]
			if (a.size() < 2) return fail("entity " + sub + " <mesh|light|body> [entity]");
			const EntityId t = a.size() > 2 ? Resolve(a[2]) : m_Selected;
			GB::Entity* e = m_World.Find(t);
			if (!e) return fail("no such entity");
			const bool add = sub == "add";
			if (a[1] == "mesh") { if (add) e->Mesh.emplace(); else e->Mesh.reset(); }
			else if (a[1] == "light") { if (add) e->Light.emplace(); else e->Light.reset(); }
			else if (a[1] == "body") { if (add) e->Body.emplace(); else e->Body.reset(); }
			else return fail("unknown component '" + a[1] + "' (mesh, light, body)");
			Commit();
			Rebind();
			return { true, Describe(t) };
		}
		if (sub == "model")
		{
			// entity model <path relative to the assets> [entity]: draws a glTF model instead of a primitive
			if (a.size() < 2) return fail("entity model <path> [entity]");
			GB::Entity* e = m_World.Find(a.size() > 2 ? Resolve(a[2]) : m_Selected);
			if (!e) return fail("no such entity");
			if (!e->Mesh) e->Mesh.emplace();
			e->Mesh->Model = a[1];
			Commit();
			Rebind();
			return { true, Describe(e->Id) };
		}
		const EntityId id = target(1);
		if (sub == "info") return id ? CommandResult{ true, Describe(id) } : fail("no such entity");
		if (!id) return fail("no such entity (give a name or id, or select one)");

		if (sub == "delete")
		{
			m_World.Destroy(id);
			if (!m_World.Find(m_Selected)) m_Selected = NullEntity;
			Commit();
			Rebind();
			return { true, "deleted" };
		}
		if (sub == "duplicate")
		{
			const EntityId copy = m_World.Duplicate(id);
			Commit();
			Select(copy);
			return { true, "duplicated as " + Describe(copy) };
		}
		if (sub == "rename")
		{
			if (a.size() < 3) return fail("entity rename <entity> <new name>");
			m_World.Rename(id, a[2]);
			Commit();
			return { true, Describe(id) };
		}
		if (sub == "parent")
		{
			if (a.size() < 3) return fail("entity parent <entity> <parent|none>");
			const EntityId p = a[2] == "none" ? NullEntity : Resolve(a[2]);
			if (a[2] != "none" && p == NullEntity) return fail("unknown parent '" + a[2] + "'");
			if (!m_World.SetParent(id, p)) return fail("cannot parent an entity under itself or its descendants");
			Commit();
			return { true, Describe(id) };
		}
		if (sub == "select")
		{
			Select(id);
			return { true, Describe(id) };
		}
		if (sub == "set")
		{
			// entity set <entity> <property without 'sel.'> <value...>
			if (a.size() < 4 || !m_Props) return fail("entity set <entity> <property> <value...>   e.g. entity set Cube position 0 2 0");
			Select(id);
			std::string value;
			for (size_t i = 3; i < a.size(); ++i) value += (i > 3 ? " " : "") + a[i];
			const SetResult r = m_Props->Set("sel." + a[2], value);
			if (!r.Ok) return fail(r.Message);
			Commit();
			return { true, Describe(id) };
		}
		if (sub == "interact")
		{
			// entity interact <entity> <point> <montage|-> <x> <y> <z> <yaw degrees> [requires] [gives]   |   entity interact <entity> clear
			Entity* e = m_World.Find(id);
			if (a.size() >= 3 && a[2] == "clear")
			{
				e->Interact.reset();
				Commit();
				return { true, "interaction points removed" };
			}
			if (a.size() < 8) return fail("entity interact <entity> <point> <montage|-> <x> <y> <z> <yaw> [requires] [gives]   |   entity interact <entity> clear");
			InteractionPoint p;
			p.Name = a[2];
			p.Montage = a[3] == "-" ? "" : a[3];
			p.Position = { static_cast<float>(std::atof(a[4].c_str())), static_cast<float>(std::atof(a[5].c_str())), static_cast<float>(std::atof(a[6].c_str())) };
			p.Yaw = static_cast<float>(std::atof(a[7].c_str()));
			if (a.size() > 8 && a[8] != "-") p.Requires = a[8];
			if (a.size() > 9 && a[9] != "-") p.Gives = a[9];
			if (!e->Interact) e->Interact.emplace();
			bool replaced = false;
			for (InteractionPoint& old : e->Interact->Points)
				if (old.Name == p.Name) { old = p; replaced = true; }
			if (!replaced) e->Interact->Points.push_back(p);
			Commit();
			return { true, std::string(replaced ? "updated" : "added") + " interaction point '" + p.Name + "' on " + e->Name };
		}
		return fail("unknown entity command '" + sub + "'");
	}

	void SceneEditor::Register(CommandRunner& c)
	{
		c.Register("entity", "entity create|delete|duplicate|rename|parent|select|add|remove|set|interact|info|list ...: edits the scene's entities", [this](const std::vector<std::string>& a) { return EntityCommand(a); });
		c.Register("add", "add <cube|sphere|plane|light|file.glb|file.gltf> [x y z]: adds an object (models are relative to the assets folder)", [this](const std::vector<std::string>& a) { return Add(a); });
		c.Register("new_scene", "new_scene: an empty scene (camera and sun only, no generated world); same as `scene new`", [this](const std::vector<std::string>&) { return NewScene(); });
		c.Register("select", "select <name|id|none>: selects an entity", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty() || a[0] == "none") { Select(NullEntity); return { true, "selection cleared" }; }
			const EntityId id = Resolve(a[0]);
			if (!id) return { false, "no such entity '" + a[0] + "'" };
			Select(id);
			return { true, Describe(id) };
		});
		c.Register("scene", "scene new|save <file>|load <file>: scene files (JSON)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			if (!a.empty() && a[0] == "new") return NewScene();
			if (a.size() >= 2 && a[0] == "save") return Save(a[1]);
			if (a.size() >= 2 && a[0] == "load") return Load(a[1]);
			return { false, "scene new | scene save <file> | scene load <file>" };
		});
		c.Register("undo", "undo: reverts the last scene edit", [this](const std::vector<std::string>&) -> CommandResult { return Undo() ? CommandResult{ true, "undone" } : CommandResult{ false, "nothing to undo" }; });
		c.Register("redo", "redo: re-applies an undone scene edit", [this](const std::vector<std::string>&) -> CommandResult { return Redo() ? CommandResult{ true, "redone" } : CommandResult{ false, "nothing to redo" }; });
	}
}
