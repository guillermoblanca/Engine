// The editor's side of the visual layer (docs/VISUALES.md): the looks of resource nodes, buildings and creatures drawn by the EntityRenderer's hook
// from the "visuals" section of the packs, the tribe director for every clan (props in the hands, on the hips and the back, loads in the arms with
// the carrying sets, the activity montages, the hands on what they work, the fields and the store's piles), and the `visuals` and `building`
// commands (the second is how H8's construction sites are shown until they arrive: BuildingVisuals::Build).
#include "EditorApp.h"
#include "GB/Log.h"
#include "GB/Visual/BuildingVisuals.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace GB::Editor
{
	void EditorApp::BindVisuals()
	{
		m_Visuals.SetAssetDir(m_AssetRoot);
		m_Visuals.SetNodes(&m_NodeDefs);
		m_Visuals.SetBuildings(&m_Buildings.Defs);   // H8: the sites and buildings are drawn by phase and work
		// the live amount of a node (and phase of a building) is the gameplay world's; the entity's own component is only what was loaded
		m_Visuals.SetAttributes([this](const std::string& entity, const std::string& attribute, float& value)
		{
			const AbilitySystemComponent* a = m_Gameplay.Find(entity);
			if (!a || !a->Attributes().Has(attribute)) return false;
			value = a->Attributes().Current(attribute);
			return true;
		});
		m_EntityRenderer->SetEntityVisual([this](const EntityWorld& w, EntityId id, const glm::mat4& m, Scene& s) { return m_Visuals.Draw(w, id, m, s); });
		m_ActorRenderer.SetPropDrawer(TribeVisuals::PropDrawer(&m_Visuals));   // the looks the tribe director attaches to the actors' hands and backs
	}

	void EditorApp::UpdateVisuals(float dt)
	{
		m_Visuals.Update(dt);
		TribeVisuals::Context ctx;
		ctx.Actors = &m_Actors;
		ctx.Gameplay = &m_Gameplay;
		ctx.Items = &m_Items;
		ctx.Entities = &m_SceneEditor.World();
		ctx.Visuals = &m_Visuals;
		ctx.Viewer = &m_Camera.Position;   // U5: the outfits near the camera only
		for (const auto& clan : m_Clans.All())   // every clan (H13), not only the first tribe
			if (clan->Folk)
			{
				ctx.ClanColor = m_Clans.Count() > 0 ? m_Clans.ColorOf(clan->Id) : glm::vec3(-1.0f);   // U4
				m_TribeVisuals.Update(*clan->Folk, ctx, dt);
			}
	}

	void EditorApp::AddVisualsToScene()
	{
		TribeVisuals::Context ctx;
		ctx.Actors = &m_Actors;
		ctx.Items = &m_Items;
		ctx.Visuals = &m_Visuals;
		if (m_World.IsBuilt()) ctx.Ground = [this](float x, float z) { return m_World.GetTerrain().GetHeight(x, z); };
		ctx.Season = SeasonClock::Name(SeasonClock().At(static_cast<double>(m_SimTick) / 60.0));
		for (const auto& clan : m_Clans.All())
			if (clan->Folk) m_TribeVisuals.AddToScene(*clan->Folk, ctx, m_Scene);
	}

	void EditorApp::RegisterVisualCommands()
	{
		CommandRunner& c = m_Commands;
		c.Register("visuals", "visuals [on|off|status|props on|off|activities on|off|reach on|off|nodes [x z]]: the procedural looks of nodes, buildings, creatures and the tribe's props (assets/data/visuals.json); `nodes` lays out every node model full, half and empty",
			[this](const std::vector<std::string>& a) -> CommandResult
		{
			auto flag = [](const std::string& v) { return v == "on" || v == "1" || v == "true"; };
			if (a.empty() || a[0] == "status")
			{
				std::ostringstream out;
				out << "visuals " << (m_Visuals.Enabled ? "on" : "off") << ": " << m_Visuals.Library.Models.size() << " models, " << m_Visuals.Library.Buildings.size() << " buildings, "
					<< m_Visuals.Library.Materials.size() << " materials; cache " << m_Visuals.Cache().CachedMeshes() << " meshes, " << m_Visuals.Cache().CachedTextures() << " textures"
					<< (m_Visuals.Cache().LowRes() ? " (low-res)" : "") << "; tribe props " << (m_TribeVisuals.Props ? "on" : "off") << ", activities " << (m_TribeVisuals.Activities ? "on" : "off")
					<< ", reach " << (m_TribeVisuals.Reach ? "on" : "off");
				{
					const TribeVisuals::FieldStats f = m_TribeVisuals.Fields();   // T332
					out << "; fields " << f.Fields << " (" << f.Triangles << " ground triangles, " << f.Plants << " plant copies; " << f.MeshBuilds << " ground builds "
						<< f.MeshMs << " ms, last " << f.LastMeshMs << " ms; " << f.TintBuilds << " tint maps " << f.TintMs << " ms)";
				}
				for (const auto& clan : m_Clans.All())
					if (clan->Folk)
						for (const TribeMember& m : clan->Folk->Members())
							if (const TribeVisuals::MemberState* st = m_TribeVisuals.State(m.Name))
							{
								out << "\n  " << m.Name << " [" << st->Activity << (st->Ability.empty() ? "" : " / " + st->Ability) << "]";
								for (const PropPlacement& p : st->Applied.Props) out << " " << p.Look << "@" << p.Socket;
								if (!st->Set.empty()) out << " set " << st->Set;
								if (!st->Montage.empty()) out << " montage " << st->Montage;
								if (st->ReachWeight > 0.0f) out << " reach " << st->ReachWeight;
							}
				return { true, out.str() };
			}
			if (a[0] == "on" || a[0] == "off") { m_Visuals.Enabled = m_TribeVisuals.Enabled = a[0] == "on"; return { true, "visuals " + a[0] }; }
			if (a.size() >= 2 && a[0] == "props") { m_TribeVisuals.Props = flag(a[1]); return { true, "props " + a[1] }; }
			if (a.size() >= 2 && a[0] == "activities") { m_TribeVisuals.Activities = flag(a[1]); return { true, "activities " + a[1] }; }
			if (a.size() >= 2 && a[0] == "reach") { m_TribeVisuals.Reach = flag(a[1]); return { true, "reach " + a[1] }; }
			if (a[0] == "nodes")
			{
				// a showcase: every node kind in a row, full, half and empty (entities with the node's tags and an Amount; not in the gameplay world)
				const float x0 = a.size() >= 3 ? static_cast<float>(std::atof(a[1].c_str())) : 0.0f, z0 = a.size() >= 3 ? static_cast<float>(std::atof(a[2].c_str())) : 0.0f;
				EntityWorld& w = m_SceneEditor.World();
				if (const EntityId old = w.FindByName("Visual nodes"); old != NullEntity) w.Destroy(old);
				const EntityId root = w.Create("Visual nodes");
				int col = 0;
				for (const auto& [id, def] : m_NodeDefs)
				{
					const std::string model = def.Model.empty() ? id : def.Model;
					if (!m_Visuals.Library.Models.count(model)) continue;
					const float amounts[] = { 1.0f, 0.5f, 0.0f };
					for (int row = 0; row < 3; ++row)
					{
						const float x = x0 + static_cast<float>(col) * 5.0f, z = z0 + static_cast<float>(row) * 5.0f;
						const EntityId e = w.Create("visual " + id + " " + std::to_string(row), root);
						Entity& ent = *w.Find(e);
						ent.Transform.Position = { x, m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(x, z) : 0.0f, z };
						ent.Tags = { "visual-demo" };
						ent.Gameplay.emplace();
						ent.Gameplay->Tags = { "Resource.Node." + id };   // (the kind only: the hook reads it; `ability sync` would make them real nodes)
						ent.Gameplay->Attributes = { { "Amount", std::round(amounts[row] * static_cast<float>(def.Capacity)), 0.0f, static_cast<float>(def.Capacity) } };
					}
					++col;
				}
				return { true, std::to_string(col) + " node kinds laid out at " + std::to_string(static_cast<int>(x0)) + ", " + std::to_string(static_cast<int>(z0)) + " (rows: full, half, empty)" };
			}
			return { false, "usage: visuals [on|off|status|props on|off|activities on|off|reach on|off|nodes [x z]]" };
		});

		c.Register("building", "building <id> [phase] [progress] [x z] [name] | building <id> phases [x z]: a building site as H8 will make it (BuildingVisuals::Build); `phases` lays out every phase of it",
			[this](const std::vector<std::string>& a) -> CommandResult
		{
			if (a.empty())
			{
				std::string ids;
				for (const auto& [id, b] : m_Visuals.Library.Buildings) ids += (ids.empty() ? "" : ", ") + id + " (" + std::to_string(b.Phases.size()) + ")";
				return { true, "buildings: " + ids };
			}
			const std::string& id = a[0];
			const int phases = BuildingVisuals::PhaseCount(m_Visuals.Library, id);
			if (phases == 0) return { false, "no building '" + id + "' in the visuals" };
			EntityWorld& w = m_SceneEditor.World();
			auto ground = [this](float x, float z) { return m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(x, z) : 0.0f; };
			if (a.size() >= 2 && a[1] == "phases")
			{
				const float x0 = a.size() >= 4 ? static_cast<float>(std::atof(a[2].c_str())) : 0.0f, z0 = a.size() >= 4 ? static_cast<float>(std::atof(a[3].c_str())) : 0.0f;
				const float step = std::max(m_Visuals.Library.Buildings.at(id).Footprint.x, 2.0f) + 1.5f;
				int n = 0;
				for (int phase = 0; phase <= phases; ++phase)
					for (const float progress : { 0.5f, 1.0f })
					{
						if (phase == phases && progress < 1.0f) continue;
						const float x = x0 + static_cast<float>(n++) * step;
						BuildingVisuals::Build(w, id, phase, progress, id + " phase " + std::to_string(phase) + (progress < 1.0f ? " half" : ""), NullEntity, { x, ground(x, z0), z0 });
					}
				return { true, std::to_string(n) + " sites of " + id + " from the first stakes to the finished building" };
			}
			const int phase = a.size() >= 2 ? std::atoi(a[1].c_str()) : phases;
			const float progress = a.size() >= 3 ? static_cast<float>(std::atof(a[2].c_str())) : 1.0f;
			const float x = a.size() >= 5 ? static_cast<float>(std::atof(a[3].c_str())) : 0.0f, z = a.size() >= 5 ? static_cast<float>(std::atof(a[4].c_str())) : 0.0f;
			const std::string name = a.size() >= 6 ? a[5] : "";
			const EntityId e = BuildingVisuals::Build(w, id, phase, progress, name, NullEntity, { x, ground(x, z), z });
			return { true, w.Find(e)->Name + ": " + (phase >= phases ? std::string("finished") : BuildingVisuals::PhaseName(m_Visuals.Library, id, phase) + " " + std::to_string(static_cast<int>(progress * 100.0f)) + "%") };
		});

		c.Register("thumbs", "thumbs build [building...] | status: the pictures of the buildings for the god's hand grid (T254): every building of the content, finished, from three quarters above, to assets/ui/thumbs/<id>.png (128 px, transparent); run it with --quality=high",
			[this](const std::vector<std::string>& a) { return ThumbsCommand(a); });
	}

	// ------------------------------------------------------------------ T254: the buildings' thumbnails
	// Each building is raised finished in a world of its own (BuildSite, every phase shown) and drawn by an EntityRenderer of its own through the same
	// visual hook as the game (its model of visuals.json, or the primitive parts of its phases when it has none), so the picture is what stands in the
	// world. A renderer of its own (256 px, made for the command and dropped after) keeps the viewport's untouched. Two passes per building: the colour
	// under a neutral sky and sun, and a mask (every surface glowing white over a black sky) whose resolved MSAA gives the alpha with soft edges; the
	// first mask frames it (the bounds of what shows, not the bounding spheres, which are loose), and the colour is unblended from the sky behind.
	namespace
	{
		constexpr uint32_t kThumbRender = 256, kThumbSize = 128;

		std::vector<uint8_t> RenderPass(Renderer& renderer, const Framebuffer& fb, const Scene& scene, const Camera& camera)
		{
			renderer.Render(scene, camera, &fb);
			return fb.ReadPixels();
		}
	}

	CommandResult EditorApp::ThumbsCommand(const std::vector<std::string>& a)
	{
		namespace fs = std::filesystem;
		const std::string dir = m_AssetRoot + "/ui/thumbs";
		const std::map<std::string, BuildingDef>& defs = m_Buildings.Defs;
		if (a.empty() || a[0] == "status")
		{
			std::string missing;
			int have = 0;
			for (const auto& [id, def] : defs)
				if (fs::exists(dir + "/" + id + ".png")) ++have;
				else missing += " " + id;
			return { true, "thumbs: " + std::to_string(have) + " of " + std::to_string(defs.size()) + " buildings in " + dir + (missing.empty() ? std::string() : "; missing:" + missing) };
		}
		if (a[0] != "build") return { false, "usage: thumbs build [building...] | status" };
		std::vector<std::string> ids(a.begin() + 1, a.end());
		if (ids.empty()) for (const auto& [id, def] : defs) ids.push_back(id);
		std::error_code ec;
		fs::create_directories(dir, ec);
		Renderer renderer(kThumbRender, kThumbRender, 4, 1024);
		if (!renderer.IsValid()) return { false, "thumbs: the renderer could not be made" };
		const Framebuffer fb(kThumbRender, kThumbRender);
		EntityRenderer entities(m_AssetRoot);
		entities.SetEntityVisual([this](const EntityWorld& w, EntityId id, const glm::mat4& m, Scene& s) { return m_Visuals.Draw(w, id, m, s); });
		const size_t pixels = static_cast<size_t>(kThumbRender) * kThumbRender;
		int made = 0;
		std::string failed;
		for (const std::string& id : ids)
		{
			const auto def = defs.find(id);
			if (def == defs.end()) { failed += " " + id + " (no such building)"; continue; }
			// the building, finished, at the origin of a world of its own (a name no gameplay entity has: the visual hook reads its own attributes)
			EntityWorld world;
			const std::string name = "thumb:" + id;
			const EntityId root = BuildSite(world, def->second, name, glm::vec3(0.0f));
			const int phases = static_cast<int>(def->second.Phases.size());
			if (Entity* e = world.Find(root); e && e->Gameplay)
				for (GameplayAttribute& at : e->Gameplay->Attributes)
					if (at.Name == "Phase") at.Value = static_cast<float>(phases);
			SiteState done;
			done.Building = id;
			done.Phases = phases;
			done.Phase = std::max(phases - 1, 0);
			done.Done = true;
			SyncSiteVisuals(world, name, def->second, done);
			Scene colour;
			entities.Build(world, colour);
			if (colour.Objects.empty()) { failed += " " + id + " (nothing to draw)"; continue; }
			// a first frame from the bounding spheres (loose: everything is inside)
			glm::vec3 lo(1e9f), hi(-1e9f);
			for (const RenderObject& o : colour.Objects)
			{
				const glm::vec3 c = glm::vec3(o.Transform * glm::vec4(o.BoundingCenter, 1.0f));
				const float s = std::max({ glm::length(glm::vec3(o.Transform[0])), glm::length(glm::vec3(o.Transform[1])), glm::length(glm::vec3(o.Transform[2])) });
				const float r = std::max(o.BoundingRadius * s, 0.1f);
				lo = glm::min(lo, c - r);
				hi = glm::max(hi, c + r);
			}
			const glm::vec2 half = def->second.Footprint * 0.5f;   // (and never less than the footprint: a kit's pieces may carry no radius)
			lo = glm::min(lo, glm::vec3(-half.x, 0.0f, -half.y));
			hi = glm::max(hi, glm::vec3(half.x, 2.0f, half.y));
			const glm::vec3 centre = (lo + hi) * 0.5f;
			const float radius = std::max(glm::length(hi - lo) * 0.5f, 1.0f);
			// three quarters from above, from the front's side (+z is where the door faces) and the right
			const float az = glm::radians(40.0f), el = glm::radians(32.0f);
			const glm::vec3 toEye(std::sin(az) * std::cos(el), std::sin(el), std::cos(az) * std::cos(el));
			Camera camera;
			camera.Orthographic = true;
			camera.OrthoHalfHeight = radius * 1.05f;
			camera.Target = centre;
			camera.Position = centre + toEye * (radius * 4.0f);
			camera.NearPlane = 0.1f;
			camera.FarPlane = radius * 10.0f;
			const glm::vec3 right = glm::normalize(glm::cross(-toEye, glm::vec3(0.0f, 1.0f, 0.0f))), up = glm::normalize(glm::cross(right, -toEye));
			// the light: a sky the colour of old paper, the sun from the upper left of the view
			colour.Env = Environment{};
			colour.Env.SkyTop = glm::vec3(0.62f, 0.60f, 0.55f);
			colour.Env.SkyHorizon = glm::vec3(0.58f, 0.55f, 0.48f);
			colour.Env.SkyGround = glm::vec3(0.30f, 0.27f, 0.22f);
			colour.Env.BloomIntensity = 0.0f;
			colour.Env.Vignette = 0.0f;
			colour.Env.ShadowCenter = centre;
			colour.Env.ShadowRadius = radius * 1.5f;
			colour.Sun.Direction = -glm::normalize(-right * 0.8f + glm::vec3(0.0f, 1.6f, 0.0f) + toEye * 0.6f);
			colour.Sun.Color = glm::vec3(1.0f, 0.95f, 0.86f);
			colour.Sun.Intensity = 3.2f;
			colour.ScreenSpaceReflections = false;
			Scene mask = colour;
			for (RenderObject& o : mask.Objects)
			{
				o.Mat.Albedo = glm::vec3(0.0f);
				o.Mat.Metallic = 0.0f;
				o.Mat.Emissive = glm::vec3(1.0f);
				o.Mat.EmissiveMap = nullptr;
				o.Mat.Translucency = 0.0f;
			}
			mask.Env.SkyTop = mask.Env.SkyHorizon = mask.Env.SkyGround = glm::vec3(0.0f);
			mask.Sun.Intensity = 0.0f;
			mask.Sun.CastShadows = false;
			// the mask frames it: the box of what shows, filled to ~88 % (a box at the edge of the first frame: a wider one, again)
			std::vector<uint8_t> m;
			float peak = 0.0f;
			for (int tries = 0; tries < 4; ++tries)
			{
				m = RenderPass(renderer, fb, mask, camera);
				peak = 0.0f;
				int x0 = kThumbRender, y0 = kThumbRender, x1 = -1, y1 = -1;
				for (size_t i = 0; i < pixels; ++i) peak = std::max(peak, static_cast<float>(m[i * 4 + 1]));
				for (uint32_t y = 0; y < kThumbRender; ++y)
					for (uint32_t x = 0; x < kThumbRender; ++x)
						if (static_cast<float>(m[(static_cast<size_t>(y) * kThumbRender + x) * 4 + 1]) > peak * 0.05f)
						{
							x0 = std::min(x0, static_cast<int>(x)); x1 = std::max(x1, static_cast<int>(x));
							y0 = std::min(y0, static_cast<int>(y)); y1 = std::max(y1, static_cast<int>(y));
						}
				if (x1 < 0 || peak < 8.0f) break;
				const int edge = static_cast<int>(kThumbRender) - 1;
				const float px = 2.0f * camera.OrthoHalfHeight / static_cast<float>(kThumbRender);   // metres a pixel
				const float cx = (static_cast<float>(x0 + x1 + 1) * 0.5f - kThumbRender * 0.5f) * px, cy = (kThumbRender * 0.5f - static_cast<float>(y0 + y1 + 1) * 0.5f) * px;
				const bool clipped = x0 <= 0 || y0 <= 0 || x1 >= edge || y1 >= edge;
				camera.Position += right * cx + up * cy;
				camera.Target += right * cx + up * cy;
				const float size = static_cast<float>(std::max(x1 - x0 + 1, y1 - y0 + 1)) * px;
				camera.OrthoHalfHeight = clipped ? camera.OrthoHalfHeight * 1.6f : size * 0.5f / 0.88f;
				if (!clipped && tries > 0) { m = RenderPass(renderer, fb, mask, camera); break; }
			}
			peak = 0.0f;
			for (size_t i = 0; i < pixels; ++i) peak = std::max(peak, static_cast<float>(m[i * 4 + 1]));
			if (peak < 8.0f) { failed += " " + id + " (the mask is empty)"; continue; }
			const std::vector<uint8_t> c = RenderPass(renderer, fb, colour, camera);
			// the sky behind: the mean of the pixels the mask leaves empty
			glm::vec3 sky(0.0f);
			float skyN = 0.0f;
			for (size_t i = 0; i < pixels; ++i)
				if (m[i * 4 + 1] == 0) { sky += glm::vec3(c[i * 4], c[i * 4 + 1], c[i * 4 + 2]); skyN += 1.0f; }
			sky = skyN > 0.0f ? sky / skyN : glm::vec3(0.0f);
			// premultiplied colour at 256 px (unblended from the sky), then a 2 x 2 box down to 128
			std::vector<glm::vec4> pre(pixels);
			for (size_t i = 0; i < pixels; ++i)
			{
				const float al = std::clamp(static_cast<float>(m[i * 4 + 1]) / peak, 0.0f, 1.0f);
				const glm::vec3 seen(c[i * 4], c[i * 4 + 1], c[i * 4 + 2]);
				pre[i] = glm::vec4(glm::clamp(seen - sky * (1.0f - al), glm::vec3(0.0f), glm::vec3(255.0f)), al);
			}
			std::vector<uint8_t> out(static_cast<size_t>(kThumbSize) * kThumbSize * 4);
			const uint32_t f = kThumbRender / kThumbSize;
			for (uint32_t y = 0; y < kThumbSize; ++y)
				for (uint32_t x = 0; x < kThumbSize; ++x)
				{
					glm::vec4 sum(0.0f);
					for (uint32_t dy = 0; dy < f; ++dy)
						for (uint32_t dx = 0; dx < f; ++dx) sum += pre[static_cast<size_t>(y * f + dy) * kThumbRender + x * f + dx];
					sum /= static_cast<float>(f * f);
					const glm::vec3 rgb = sum.a > 1e-3f ? glm::clamp(glm::vec3(sum) / sum.a, glm::vec3(0.0f), glm::vec3(255.0f)) : glm::vec3(0.0f);
					uint8_t* p = &out[(static_cast<size_t>(y) * kThumbSize + x) * 4];
					p[0] = static_cast<uint8_t>(rgb.r + 0.5f);
					p[1] = static_cast<uint8_t>(rgb.g + 0.5f);
					p[2] = static_cast<uint8_t>(rgb.b + 0.5f);
					p[3] = static_cast<uint8_t>(std::clamp(sum.a, 0.0f, 1.0f) * 255.0f + 0.5f);
				}
			const std::string path = dir + "/" + id + ".png";
			if (!SavePNG(path.c_str(), kThumbSize, kThumbSize, out.data())) { failed += " " + id + " (could not write)"; continue; }
			m_BuildingThumbs.erase(id);   // (the grid loads it again)
			++made;
		}
		const bool ok = failed.empty();
		const std::string text = "thumbs: " + std::to_string(made) + " written to " + dir + (ok ? std::string() : "; failed:" + failed);
		GB_CLIENT_INFO("{}", text);
		return { ok, text };
	}

	const EditorApp::BuildingThumb* EditorApp::BuildingThumbOf(const std::string& id)
	{
		const auto known = m_BuildingThumbs.find(id);
		if (known != m_BuildingThumbs.end()) return known->second.Colour ? &known->second : nullptr;
		BuildingThumb& t = m_BuildingThumbs[id];
		Image image = Image::Load(m_AssetRoot + "/ui/thumbs/" + id + ".png", false);
		if (!image.IsValid()) return nullptr;
		t.Colour = std::make_unique<Texture2D>(image, TextureEncoding::Linear);
		for (size_t i = 0; i + 3 < image.Pixels.size(); i += 4)   // the grey copy: its luminance, a little darker
		{
			const float l = 0.80f * (0.299f * image.Pixels[i] + 0.587f * image.Pixels[i + 1] + 0.114f * image.Pixels[i + 2]);
			image.Pixels[i] = image.Pixels[i + 1] = image.Pixels[i + 2] = static_cast<uint8_t>(std::clamp(l, 0.0f, 255.0f));
		}
		t.Grey = std::make_unique<Texture2D>(image, TextureEncoding::Linear);
		return &t;
	}
}
