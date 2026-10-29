#include "GB/Visual/SceneVisuals.h"
#include "GB/Log.h"
#include "GB/Visual/BuildingVisuals.h"
#include "GB/Visual/ProcTexture.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	// ------------------------------------------------------------------ the cache
	const Texture2D* VisualCache::TextureFor(const std::string& kind) const
	{
		if (kind.empty()) return nullptr;
		auto it = m_Textures.find(kind);
		if (it == m_Textures.end())
		{
			const Image img = ProcTexture::Generate(kind, ProcTexture::Size(m_LowRes), 0);
			it = m_Textures.emplace(kind, std::make_unique<Texture2D>(img, TextureEncoding::SRGB)).first;
		}
		return it->second->IsValid() ? it->second.get() : nullptr;
	}

	Material VisualCache::MaterialFor(const std::string& id) const
	{
		Material m;
		const auto it = m_Library.Materials.find(id);
		if (it == m_Library.Materials.end()) { m.Albedo = glm::vec3(0.5f); m.Roughness = 0.8f; return m; }
		const VisualMaterial& v = it->second;
		m.Albedo = v.Seasonal ? v.Color * Foliage : v.Color;
		m.Roughness = v.Roughness;
		m.Metallic = v.Metallic;
		m.Emissive = v.Emissive;
		m.AlbedoMap = TextureFor(v.Texture);
		m.UVScale = v.UVScale;
		m.Grain = v.Grain;
		m.WindSway = v.Wind;
		m.WindHeight = v.Wind > 0.0f ? 3.0f : 1.0f;
		m.WindFlutter = v.Wind * 0.5f;
		m.DoubleSided = v.DoubleSided;
		return m;
	}

	bool VisualCache::Add(const std::string& key, const VisualModel& model, const std::vector<bool>& visible, int variant, const glm::mat4& world, Scene& scene, float maxDistance)
	{
		auto it = m_Meshes.find(key);
		if (it == m_Meshes.end())
		{
			Entry entry;
			glm::vec3 lo(1e9f), hi(-1e9f);
			for (VisualPiece& piece : BuildVisual(model, visible, variant))
			{
				glm::vec3 a, b;
				piece.Mesh.Bounds(a, b);
				lo = glm::min(lo, a);
				hi = glm::max(hi, b);
				entry.Pieces.emplace_back(std::make_unique<Mesh>(piece.Mesh.Vertices, piece.Mesh.Indices), piece.Material);
			}
			if (!entry.Pieces.empty()) { entry.Center = (lo + hi) * 0.5f; entry.Radius = glm::length(hi - lo) * 0.5f; }
			it = m_Meshes.emplace(key, std::move(entry)).first;
		}
		for (const auto& [mesh, material] : it->second.Pieces)
		{
			RenderObject obj;
			obj.MeshPtr = mesh.get();
			obj.Mat = MaterialFor(material);
			obj.Transform = world;
			obj.BoundingCenter = it->second.Center;
			obj.BoundingRadius = std::max(it->second.Radius, 0.05f);
			obj.MaxDistance = maxDistance;
			scene.Objects.push_back(std::move(obj));
		}
		return true;
	}

	bool VisualCache::AddModel(const std::string& model, float fraction, int variant, const glm::mat4& world, Scene& scene, float maxDistance)
	{
		const auto it = m_Library.Models.find(model);
		if (it == m_Library.Models.end()) return false;
		const std::vector<bool> mask = VisibleCopies(it->second, fraction);
		const int v = std::clamp(variant, 0, std::max(0, it->second.Variants - 1));
		return Add("m:" + model + "#" + std::to_string(v) + ":" + MaskKey(mask), it->second, mask, v, world, scene, maxDistance);
	}

	bool VisualCache::AddBuilding(const std::string& building, int phase, float progress, const glm::mat4& world, Scene& scene)
	{
		const auto it = m_Library.Buildings.find(building);
		if (it == m_Library.Buildings.end()) return false;
		std::vector<bool> mask;
		const VisualModel model = BuildingModel(it->second, phase, progress, mask);
		if (model.Parts.empty()) return true;   // an empty site: nothing to draw yet
		return Add("b:" + building + "#" + std::to_string(phase) + ":" + MaskKey(mask), model, mask, 0, world, scene);
	}

	// ------------------------------------------------------------------ the scene hook
	bool SceneVisuals::ReadAttribute(const Entity& e, const std::string& name, float& value) const
	{
		if (m_Read && m_Read(e.Name, name, value)) return true;
		if (e.Gameplay)
			for (const GameplayAttribute& a : e.Gameplay->Attributes)
				if (a.Name == name) { value = a.Value; return true; }
		return false;
	}

	bool SceneVisuals::Describe(const Entity& e, Look& look) const
	{
		auto tagged = [&](const std::string& prefix, std::string& rest)
		{
			auto scan = [&](const std::vector<std::string>& tags)
			{
				for (const std::string& t : tags)
					if (t.size() > prefix.size() && t.compare(0, prefix.size(), prefix) == 0) { rest = t.substr(prefix.size()); return true; }
				return false;
			};
			return scan(e.Tags) || (e.Gameplay && scan(e.Gameplay->Tags));
		};
		std::string id;
		if (e.Gameplay && m_Nodes && tagged("Resource.Node.", id))
		{
			const auto def = m_Nodes->find(id);
			if (def == m_Nodes->end()) return false;
			look.Kind = "node";
			look.Model = def->second.Model.empty() ? id : def->second.Model;
			const auto model = Library.Models.find(look.Model);
			if (model == Library.Models.end()) return false;
			float amount = static_cast<float>(def->second.Capacity);
			ReadAttribute(e, "Amount", amount);
			look.Fraction = std::clamp(amount / static_cast<float>(std::max(1, def->second.Capacity)), 0.0f, 1.0f);
			look.Variant = VisualVariant(model->second, e.Name);
			return true;
		}
		if (m_Buildings && tagged("Building.Kind.", id) && Library.Buildings.count(id))
		{
			// an H8 site: Phase (0..phases, = phases when done) and Work seconds into the current phase, as a share of the whole build
			const auto def = m_Buildings->find(id);
			if (def == m_Buildings->end() || def->second.Phases.empty()) return false;
			float phase = 0.0f, work = 0.0f;
			ReadAttribute(e, "Phase", phase);
			ReadAttribute(e, "Work", work);
			const int phases = static_cast<int>(def->second.Phases.size());
			const int p = std::clamp(static_cast<int>(std::floor(phase)), 0, phases);
			const float within = p >= phases ? 0.0f : std::clamp(work / std::max(def->second.Phases[static_cast<size_t>(p)].Work, 1e-3f), 0.0f, 1.0f);
			const float overall = p >= phases ? 1.0f : (static_cast<float>(p) + within) / static_cast<float>(phases);
			look.Kind = "building";
			look.Model = id;
			BuildingVisuals::FromOverall(Library, id, overall, look.Phase, look.Progress);
			// a site whose phase has not had any work yet still shows the stakes of its first pieces (the materials are there)
			if (overall <= 0.0f) { look.Phase = 0; look.Progress = 0.1f; }
			return true;
		}
		if (tagged("Building.", id) && Library.Buildings.count(id))
		{
			look.Kind = "building";
			look.Model = id;
			float phase = 1e6f, progress = 1.0f;
			ReadAttribute(e, "Phase", phase);
			ReadAttribute(e, "Progress", progress);
			look.Phase = static_cast<int>(std::floor(phase));
			look.Progress = std::clamp(progress, 0.0f, 1.0f);
			return true;
		}
		if (e.Gameplay)
			for (const std::string& t : e.Gameplay->Tags)
				if (Library.Creatures.count(t))
				{
					look.Kind = "creature";
					look.Model = t;
					return true;
				}
		return false;
	}

	bool SceneVisuals::Draw(const EntityWorld& world, EntityId id, const glm::mat4& matrix, Scene& scene)
	{
		(void)world;
		if (!Enabled) return false;
		const Entity* e = world.Find(id);
		if (!e) return false;
		Look look;
		if (!Describe(*e, look)) return false;
		if (look.Kind == "node") return m_Cache.AddModel(look.Model, look.Fraction, look.Variant, matrix, scene, NodeDrawDistance);   // (H16: culled by distance)
		if (look.Kind == "building") return m_Cache.AddBuilding(look.Model, look.Phase, look.Progress, matrix, scene);
		return DrawCreature(*e, id, Library.Creatures.at(look.Model), matrix, scene);
	}

	bool SceneVisuals::DrawCreature(const Entity& e, EntityId id, const CreatureVisual& c, const glm::mat4& matrix, Scene& scene)
	{
		(void)e;
		Creature& cr = m_Creatures[id];
		const glm::vec3 at(matrix[3]);
		if (!cr.Seen) { cr.Last = at; cr.Seen = true; }
		if (m_Dt > 0.0f)
		{
			const float moved = glm::length(glm::vec2(at.x - cr.Last.x, at.z - cr.Last.z)) / m_Dt;
			cr.Speed += (moved - cr.Speed) * std::min(1.0f, m_Dt * 6.0f);   // smoothed: the ecology moves them in steps
		}
		cr.Last = at;
		const glm::mat4 turn = glm::rotate(glm::mat4(1.0f), glm::radians(c.Yaw), glm::vec3(0, 1, 0));
		if (!c.Procedural.empty())
		{
			const auto model = Library.Models.find(c.Procedural);
			if (model == Library.Models.end()) return false;
			return m_Cache.AddModel(c.Procedural, 1.0f, VisualVariant(model->second, e.Name), matrix * turn, scene, 120.0f);
		}
		auto it = m_Models.find(c.Model);
		if (it == m_Models.end())
		{
			const std::string full = m_AssetDir.empty() || std::filesystem::path(c.Model).is_absolute() ? c.Model : m_AssetDir + "/" + c.Model;
			auto loaded = Model::Load(full);
			if (!loaded) GB_CORE_WARN("Creature model '{}' could not be loaded: the creature keeps its primitives", full);
			it = m_Models.emplace(c.Model, std::move(loaded)).first;
		}
		if (!it->second) return false;
		if (!cr.Instance) cr.Instance = std::make_unique<ModelInstance>(*it->second);
		const std::string clip = cr.Speed > c.RunSpeed ? c.Run : cr.Speed > 0.15f ? c.Walk : c.Idle;
		if (clip != cr.Clip && !clip.empty()) { cr.Instance->Play(clip, true, 0.25f); cr.Clip = clip; }
		const float scale = c.Length / std::max(2.0f * it->second->GetBoundsRadius(), 1e-4f);
		cr.Instance->AddToScene(scene, matrix * turn * glm::scale(glm::mat4(1.0f), glm::vec3(scale)));
		return true;
	}

	void SceneVisuals::Update(float dt)
	{
		m_Dt = dt;
		for (auto& [id, c] : m_Creatures)
			if (c.Instance) c.Instance->Update(dt);
	}
}
