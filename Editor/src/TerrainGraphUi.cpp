#include "TerrainGraphUi.h"
#include "GB/World/Graph/TerrainGraphLibrary.h"
#include "GB/Renderer/Texture.h"
#include "GB/Scene/Image.h"
#include "GB/World/Terrain.h"
#include "SceneEditor.h"
#include "GB/Log.h"
#include "GB/World/Graph/TgHash.h"
#include <algorithm>
#include <filesystem>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <map>
#include <set>

namespace GB::Editor
{
	namespace
	{
		constexpr float kNodeW = 190.0f, kNodeBaseH = 54.0f, kPortRow = 17.0f, kPin = 6.0f;
		ImVec2 Add(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }
		ImU32 HeaderColor(uint32_t rgb)
		{
			if (rgb == 0) rgb = 0x968C32;
			return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
		}
		ImU32 PinColor(TgPortKind kind)
		{
			switch (kind)
			{
			case TgPortKind::Field: return IM_COL32(220, 220, 230, 255);
			case TgPortKind::Placements: return IM_COL32(150, 220, 110, 255);
			case TgPortKind::Paths: return IM_COL32(240, 180, 90, 255);
			case TgPortKind::Water: return IM_COL32(90, 190, 240, 255);
			default: return IM_COL32(200, 160, 230, 255);
			}
		}
		bool Compatible(TgPortKind a, TgPortKind b) { return a == b || a == TgPortKind::Any || b == TgPortKind::Any; }

		// The panel's memo of the full world (T84): a budget keeps the 3 km valley at 1025 x 1025 from holding every output of every node (measured:
		// docs/GRAFO_TERRENO.md 12.6). The nodes that cost the most time per byte stay, so a change near the end of the graph is still instant.
		constexpr size_t kFullCacheBudget = size_t(128) << 20;
		std::shared_ptr<TerrainGraphCache> NewFullCache()
		{
			auto cache = std::make_shared<TerrainGraphCache>();
			cache->SetByteBudget(kFullCacheBudget);
			return cache;
		}
		double Megabytes(size_t bytes) { return static_cast<double>(bytes) / 1048576.0; }
	}

	TerrainGraphUi::TerrainGraphUi() : m_Graph(std::make_shared<TerrainGraph>()), m_Registry(MakeDefaultTerrainNodeRegistry())
	{
		m_Cache = NewFullCache();
		m_Graph->Id = "terrain";
		m_Graph->Name = "Terrain";
		AddNode("heightfield_noise", ImVec2(40.0f, 90.0f));
		AddNode("erosion_hydraulic", ImVec2(300.0f, 90.0f));
		AddNode("output", ImVec2(580.0f, 90.0f));
		m_Graph->Edges = { { "heightfield_noise", "erosion_hydraulic", "height", "height" }, { "erosion_hydraulic", "output", "in", "height" } };
		m_Message.clear();
	}

	TerrainGraphUi::~TerrainGraphUi() = default;

	const TgNodeSchema& TerrainGraphUi::SchemaOf(const std::string& type) const
	{
		const TgNodeSchema* s = m_Registry.Schema(type);
		return s ? *s : m_Unknown;
	}

	int TerrainGraphUi::OutputIndex(const TgNode& node, const std::string& port) const
	{
		const TgNodeSchema& s = SchemaOf(node.Type);
		const int index = s.OutputIndex(port);
		return index >= 0 ? index : (s.Outputs.empty() ? -1 : 0);
	}

	std::string TerrainGraphUi::UniqueId(const std::string& type) const
	{
		std::string base = type;
		std::replace(base.begin(), base.end(), '.', '_');
		std::string id = base;
		for (int i = 2; m_Graph->FindNode(id); ++i) id = base + std::to_string(i);
		return id;
	}

	void TerrainGraphUi::AddNode(const std::string& type, const ImVec2& position)
	{
		const TgNodeSchema& schema = SchemaOf(type);
		TgNode node;
		node.Id = UniqueId(type);
		node.Type = type;
		node.Label = type;
		node.X = position.x;
		node.Y = position.y;
		for (const TgParamInfo& p : schema.Params) node.Params[p.Name] = p.Default;
		if (schema.UsesSource) node.Source = schema.SourceDefault;
		m_Graph->Nodes.push_back(std::move(node));
		m_Message = "Added " + type;
		m_MessageError = false;
	}

	void TerrainGraphUi::LoadGraph(const TerrainGraph& graph)
	{
		*m_Graph = graph;
		std::snprintf(m_SaveId, sizeof m_SaveId, "%s", graph.Id.c_str());
		PinPreview();   // (a build from this graph may come before the panel is drawn: `terrain_graph preset`)
		ResetCanvasInteraction();
		m_Message = "Loaded " + (graph.Name.empty() ? graph.Id : graph.Name);
		m_MessageError = false;
	}

	TerrainGraphLibrary& TerrainGraphUi::Library()
	{
		if (!m_LibraryLoaded)
		{
			m_LibraryLoaded = true;
			m_Dir = std::string(GB_ASSET_DIR) + "/data/terrain_graphs";
			std::error_code ec;
			if (std::filesystem::is_directory(m_Dir, ec))
				if (const std::string e = m_Library.LoadDirectory(m_Dir); !e.empty()) { m_Message = e; m_MessageError = true; }
		}
		return m_Library;
	}

	std::string TerrainGraphUi::SaveCurrent(const std::string& id)
	{
		TerrainGraphLibrary& lib = Library();
		TerrainGraph graph = *m_Graph;
		if (!id.empty()) graph.Id = id;
		if (graph.Name.empty() || graph.Name == "Terrain") graph.Name = graph.Id;
		const std::string saved = graph.Id;
		if (std::string e = lib.Add(graph, true); !e.empty()) return e;
		if (std::string e = lib.SaveGraph(m_Dir, saved); !e.empty()) return e;
		m_Graph->Id = saved;
		m_Graph->Name = graph.Name;
		std::snprintf(m_SaveId, sizeof m_SaveId, "%s", saved.c_str());
		m_Message = "Saved " + m_Dir + "/" + saved + ".json";
		m_MessageError = false;
		return {};
	}

	std::string TerrainGraphUi::LoadNamed(const std::string& idOrPath)
	{
		TerrainGraphLibrary& lib = Library();
		std::string id = idOrPath;
		const bool file = idOrPath.size() > 5 && idOrPath.compare(idOrPath.size() - 5, 5, ".json") == 0;
		if (file)
			if (std::string e = lib.LoadFile(idOrPath, &id); !e.empty()) return e;
		const TerrainGraph* g = lib.Find(id);
		if (!g) return "no terrain graph '" + idOrPath + "' (terrain_graph library)";
		LoadGraph(*g);
		return {};
	}

	bool TerrainGraphUi::Command(const std::vector<std::string>& a, std::string& message)
	{
		TerrainGraphLibrary& lib = Library();
		const std::string verb = a.empty() ? std::string("library") : a[0];
		std::string error;
		if (verb == "library")
		{
			for (const std::string& id : lib.Ids()) message += (message.empty() ? "" : ", ") + id + (lib.IsPreset(id) ? " (preset)" : "");
			message = "terrain graphs in " + m_Dir + ": " + message;
			return true;
		}
		if (verb == "reload") { error = lib.LoadDirectory(m_Dir); message = error.empty() ? "reloaded " + m_Dir : error; return error.empty(); }
		if (verb == "load")
		{
			if (a.size() < 2) { message = "usage: terrain_graph load <id|file.json>"; return false; }
			error = LoadNamed(a[1]);
			message = error.empty() ? "loaded '" + m_Graph->Id + "' into the Terrain Graph panel (" + std::to_string(m_Graph->Nodes.size()) + " nodes)" : error;
			return error.empty();
		}
		if (verb == "save")
		{
			error = SaveCurrent(a.size() > 1 ? a[1] : std::string());
			message = error.empty() ? "saved " + m_Dir + "/" + m_Graph->Id + ".json" : error;
			return error.empty();
		}
		if (verb == "export")
		{
			if (a.size() < 3) { message = "usage: terrain_graph export <id> <file.json>"; return false; }
			error = lib.SaveFile(a[2], a[1]);
			message = error.empty() ? "exported '" + a[1] + "' to " + a[2] : error;
			return error.empty();
		}
		if (verb == "preview")
		{
			if (a.size() < 2) { message = "usage: terrain_graph preview <node> [port|-] [map|overlay|both|off]"; return false; }
			if (a[1] == "off") { SetPreview("", "", "off"); message = "preview off"; return true; }
			error = SetPreview(a[1], a.size() > 2 ? a[2] : std::string(), a.size() > 3 ? a[3] : std::string("map"));
			message = error.empty() ? "previewing " + m_Preview.Node + "." + m_Preview.Port + (m_Preview.Map ? " (map)" : "") + (m_Preview.Overlay ? " (on the terrain)" : "") : error;
			return error.empty();
		}
		if (verb == "quick")
		{
			if (a.size() > 1)
			{
				if (a[1] == "on" || a[1] == "off") m_Quick.Enabled = a[1] == "on";
				else if (a[1] == "auto") m_Quick.AutoFull = a.size() < 3 || (a[2] != "off" && a[2] != "0");
				else
				{
					const int resolution = std::atoi(a[1].c_str());
					if (resolution < 33 || resolution > 1025) { message = "usage: terrain_graph quick [on|off|<resolution 33..1025>|auto on|off]"; return false; }
					m_Quick.Resolution = static_cast<uint32_t>(resolution);
				}
			}
			message = QuickStatus();
			return true;
		}
		if (verb == "set")
		{
			if (a.size() < 4) { message = "usage: terrain_graph set <node> <param> <value>"; return false; }
			TgNode* node = m_Graph->FindNode(a[1]);
			if (!node) { message = "no node '" + a[1] + "' in the panel's graph"; return false; }
			if (!SchemaOf(node->Type).FindParam(a[2]) && !node->Params.count(a[2])) { message = "node '" + a[1] + "' has no parameter '" + a[2] + "'"; return false; }
			node->Params[a[2]] = std::atof(a[3].c_str());
			message = a[1] + "." + a[2] + " = " + a[3] + (m_Quick.Enabled ? " (the quick preview runs again)" : "");
			return true;
		}
		if (verb == "memory")
		{
			if (a.size() > 1) m_Cache->SetByteBudget(static_cast<size_t>(std::max(0.0, std::atof(a[1].c_str())) * 1048576.0));
			const std::string budget = m_Cache->ByteBudget() ? std::to_string(m_Cache->ByteBudget() >> 20) + " MB" : std::string("none");
			char text[200];
			std::snprintf(text, sizeof text, "terrain graph caches: full %.1f MB in %zu nodes (budget %s), quick %.1f MB", Megabytes(m_Cache->Bytes()), m_Cache->Size(),
				budget.c_str(), Megabytes(m_Quick.Cache->Bytes()));
			message = text;
			return true;
		}
		if (verb == "release") { ReleaseMemory(); message = "terrain graph caches released"; return true; }
		message = "terrain_graph library | load <id|file.json> | save [id] | export <id> <file.json> | reload | preview <node> [port] [map|overlay|both|off] | "
			"quick [on|off|<resolution>|auto on|off] | set <node> <param> <value> | memory [budget_mb] | release";
		return false;
	}

	void TerrainGraphUi::SetWorld(const WorldDesc& desc)
	{
		m_World = desc;
		m_World.TerrainGraphDefinition.reset();
		m_World.GraphCache.reset();
	}

	void TerrainGraphUi::ReleaseMemory()
	{
		// new, empty caches: a build or a quick evaluation still running keeps the old one alive until it ends (it holds a reference), and the
		// world it makes does not keep it (WorldGenerator::GenerateData)
		m_Cache = NewFullCache();
		m_Quick.Cache = std::make_shared<TerrainGraphCache>();
		m_Quick.LaunchedKey = m_Quick.ShownKey = 0;
		m_Quick.Done = m_Quick.FullDone = {};
		m_Quick.FullGeneration = m_Cache->Generation();
		m_Preview.Value = TgValue();
		m_Preview.Texture.reset();
		m_Preview.Valid = false;
		m_Preview.Source = nullptr;
		m_Preview.Target.clear();
		m_Preview.Generation = ~0ull;
		PinPreview();
	}

	uint64_t TerrainGraphUi::GraphKey() const
	{
		uint64_t h = kTgHashSeed;
		for (const TgNode& n : m_Graph->Nodes)
		{
			h = TgHashString(n.Source, TgHashString(n.Type, TgHashString(n.Id, h)));
			for (const auto& [name, value] : n.Params) h = TgHashBytes(&value, sizeof value, TgHashString(name, h));
		}
		for (const TgEdge& e : m_Graph->Edges) h = TgHashString(e.FromPort, TgHashString(e.Port, TgHashString(e.To, TgHashString(e.From, h))));
		// the world it is evaluated for, and at which resolution
		const float extent = m_World.Extent();
		const int type = static_cast<int>(m_World.Type);
		h = TgHashBytes(&m_World.Seed, sizeof m_World.Seed, h);
		h = TgHashBytes(&extent, sizeof extent, h);
		h = TgHashBytes(&type, sizeof type, h);
		h = TgHashBytes(&m_Quick.Resolution, sizeof m_Quick.Resolution, h);
		std::string node, port;   // and what it previews (the quick evaluation runs only what that needs)
		if (PreviewTarget(node, port)) h = TgHashString(node, h);
		return h == 0 ? 1 : h;   // (0 means "none")
	}

	std::string TerrainGraphUi::QuickStatus() const
	{
		char text[320];
		const QuickOutcome& q = m_Quick.Last;
		if (m_Quick.Runs == 0)
			std::snprintf(text, sizeof text, "quick preview %s at %u x %u, not run yet", m_Quick.Enabled ? "on" : "off", m_Quick.Resolution, m_Quick.Resolution);
		else if (!q.Ok)
			std::snprintf(text, sizeof text, "quick preview %u x %u failed: %s", q.Resolution, q.Resolution, q.Error.c_str());
		else
			std::snprintf(text, sizeof text, "quick preview %u x %u: %.2f s (%d ran, %d reused), quick cache %.1f MB, full cache %.1f MB%s", q.Resolution, q.Resolution,
				q.Seconds, q.Ran, q.Reused, Megabytes(m_Quick.Cache->Bytes()), Megabytes(m_Cache->Bytes()), m_Quick.AutoFull ? ", full build after the edits" : "");
		return text;
	}

	void TerrainGraphUi::UpdateQuick()
	{
		using Clock = Quick::Clock;
		const Clock::time_point now = Clock::now();
		if (const uint64_t full = m_Cache->Generation(); full != m_Quick.FullGeneration) { m_Quick.FullGeneration = full; m_Quick.FullDone = now; }
		if (m_Quick.Running && m_Quick.Job.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			m_Quick.Last = m_Quick.Job.get();
			m_Quick.Running = false;
			++m_Quick.Runs;
			m_Quick.ShownKey = m_Quick.LaunchedKey;   // (a graph that fails is not tried again until it changes)
			if (m_Quick.Last.Ok) m_Quick.Done = Clock::now();
			else { m_Message = "Quick preview failed: " + m_Quick.Last.Error; m_MessageError = true; }
			GB_CLIENT_INFO("terrain graph {}", QuickStatus());
		}
		const uint64_t key = GraphKey();
		if (key != m_Quick.SeenKey) { m_Quick.SeenKey = key; m_Quick.Changed = now; }
		const bool settled = now - m_Quick.Changed >= std::chrono::milliseconds(80);   // (a slider being dragged moves every frame)
		if (m_Quick.Enabled && !m_Quick.Running && key != m_Quick.LaunchedKey && settled && !HasTgErrors(ValidateTerrainGraph(*m_Graph, m_Registry)))
		{
			WorldDesc desc = m_World;
			desc.Size = m_World.Extent();
			desc.Tiles = 1;
			desc.Resolution = m_Quick.Resolution;
			// a copy (the panel keeps editing its own) of only what the previewed port needs: the published height does not wait for the biomes
			std::string node, port;
			desc.TerrainGraphDefinition = std::make_shared<const TerrainGraph>(PreviewTarget(node, port) ? TerrainGraphUpstreamOf(*m_Graph, { node }) : *m_Graph);
			desc.GraphCache = m_Quick.Cache;
			m_Quick.LaunchedKey = key;
			m_Quick.Running = true;
			m_Quick.Job = std::async(std::launch::async, [desc]()
			{
				QuickOutcome o;
				TgResult result;
				const auto t0 = Clock::now();
				o.Ok = EvaluateWorldGraph(desc, result, o.Error);
				o.Seconds = std::chrono::duration<double>(Clock::now() - t0).count();
				const TerrainGraphCache::Stats s = desc.GraphCache->LastStats();
				o.Ran = s.Ran;
				o.Reused = s.Reused;
				o.Resolution = desc.Resolution;
				return o;
			});
		}
		// the full build by itself, once the edits stopped and no slider is held (T84: "al soltar el slider con un retardo")
		if (m_Quick.AutoFull && key != m_Quick.AutoKey && now - m_Quick.Changed >= std::chrono::duration<float>(m_Quick.FullDelay) && !ImGui::IsAnyItemActive() &&
			!HasTgErrors(ValidateTerrainGraph(*m_Graph, m_Registry)))
		{
			m_Quick.AutoKey = key;
			m_ApplyRequested = true;
			m_Message = "Edits settled: full build queued";
			m_MessageError = false;
		}
	}

	void TerrainGraphUi::PinPreview()
	{
		// the budget never drops what the map shows (the published height is a pass-through, the cheapest entry per byte there is); set on the
		// caches of now, which ReleaseMemory replaces, before a build fills them
		std::string node, port;
		if (!PreviewTarget(node, port)) node.clear();
		m_Cache->SetPinned(node);
		m_Quick.Cache->SetPinned(node);
	}

	bool TerrainGraphUi::PreviewTarget(std::string& node, std::string& port) const
	{
		if (!m_Preview.Node.empty()) { node = m_Preview.Node; port = m_Preview.Port; return true; }
		for (const TgNode& n : m_Graph->Nodes)   // nothing picked: the height the graph publishes
			if (n.Type == "output" && n.Source == "height") { node = n.Id; port = "out"; return true; }
		return false;
	}

	void TerrainGraphUi::DrawLibraryBar()
	{
		TerrainGraphLibrary& lib = Library();
		ImGui::SameLine();
		if (ImGui::Button("Load")) ImGui::OpenPopup("terrain_library");
		if (ImGui::BeginPopup("terrain_library"))
		{
			ImGui::TextDisabled("%s", m_Dir.c_str());
			for (const std::string& id : lib.Ids())
			{
				const TerrainGraph* g = lib.Find(id);
				const std::string label = id + (lib.IsPreset(id) ? "  (preset)" : "");
				if (ImGui::MenuItem(label.c_str()) && g) LoadGraph(*g);
				if (g && !g->Name.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s, %zu nodes", g->Name.c_str(), g->Nodes.size());
			}
			ImGui::EndPopup();
		}
		ImGui::SameLine();
		if (m_SaveId[0] == 0) std::snprintf(m_SaveId, sizeof m_SaveId, "%s", m_Graph->Id.c_str());
		ImGui::SetNextItemWidth(160.0f);
		ImGui::InputText("##terrain_save_id", m_SaveId, sizeof m_SaveId);
		ImGui::SameLine();
		if (ImGui::Button("Save"))
			if (const std::string e = SaveCurrent(m_SaveId); !e.empty()) { m_Message = e; m_MessageError = true; }
		ImGui::SameLine();
		if (ImGui::Button("Reload folder"))
		{
			const std::string e = lib.LoadDirectory(m_Dir);
			m_Message = e.empty() ? "Reloaded " + m_Dir : e;
			m_MessageError = !e.empty();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("graph: %s", m_Graph->Id.c_str());
	}

	std::string TerrainGraphUi::SetPreview(const std::string& node, const std::string& port, const std::string& mode)
	{
		if (mode == "off") { m_Preview.Map = m_Preview.Overlay = false; return {}; }
		const TgNode* n = m_Graph->FindNode(node);
		if (!n) return "no node '" + node + "' in the panel's graph";
		const TgNodeSchema& s = SchemaOf(n->Type);
		std::string p = port;
		if (p.empty() || p == "-") p = s.Outputs.empty() ? std::string("out") : s.Outputs[0].Name;
		if (s.OutputIndex(p) < 0) return "node '" + node + "' has no output '" + p + "'";
		m_Preview.Node = node;
		m_Preview.Port = p;
		m_Preview.Map = mode.empty() || mode == "map" || mode == "both";
		m_Preview.Overlay = mode == "overlay" || mode == "both";
		m_Preview.Generation = ~0ull;   // read it again
		m_Preview.Valid = false;
		SelNode = node;
		PinPreview();
		return {};
	}

	glm::vec4 TerrainGraphUi::PreviewColour(float value) const
	{
		// a ramp from deep blue (lowest) through teal and green to yellow and white (highest)
		static const glm::vec3 kRamp[] = { { 0.10f, 0.12f, 0.45f }, { 0.10f, 0.50f, 0.60f }, { 0.25f, 0.70f, 0.30f }, { 0.95f, 0.85f, 0.25f }, { 1.0f, 1.0f, 1.0f } };
		const float span = m_Preview.Max - m_Preview.Min;
		const float t = span > 1e-12f ? std::clamp((value - m_Preview.Min) / span, 0.0f, 1.0f) : 0.5f;
		const float f = t * 4.0f;
		const int i = std::min(3, static_cast<int>(f));
		return glm::vec4(glm::mix(kRamp[i], kRamp[i + 1], f - static_cast<float>(i)), 1.0f);
	}

	bool TerrainGraphUi::RefreshPreview()
	{
		std::string node, port;
		if (!PreviewTarget(node, port)) return false;
		PinPreview();
		// the newer of the two evaluations: the quick one while the graph is being edited, the full one once a build finished after it
		const bool quick = m_Quick.ShownKey != 0 && m_Quick.Last.Ok && m_Quick.Done > m_Quick.FullDone;
		TerrainGraphCache* source = quick ? m_Quick.Cache.get() : m_Cache.get();
		const std::string target = node + "." + port;
		const uint64_t generation = source->Generation();
		const bool same = source == m_Preview.Source && target == m_Preview.Target;
		if (same && generation == m_Preview.Generation) return m_Preview.Valid;
		TgValue value;
		if (!source->TryCopyOutput(node, port, value))   // busy (try next frame) or not there
		{
			if (target != m_Preview.Target) m_Preview.Valid = false;   // (the same port keeps showing what it showed)
			return m_Preview.Valid;
		}
		m_Preview.Source = source;
		m_Preview.Target = target;
		m_Preview.Generation = generation;
		m_Preview.Value = std::move(value);
		m_Preview.Valid = true;
		source->Footprint(m_Preview.Size, m_Preview.Center);
		m_Preview.Texture.reset();
		if (m_Preview.Value.Kind == TgPortKind::Field && !m_Preview.Value.Field.Values.empty())
		{
			const TgField& f = m_Preview.Value.Field;
			const auto [lo, hi] = std::minmax_element(f.Values.begin(), f.Values.end());
			m_Preview.Min = *lo;
			m_Preview.Max = *hi;
			m_Preview.Size = f.Size;
			m_Preview.Center = f.Center;
			constexpr uint32_t kSide = 256;
			Image image;
			image.Width = image.Height = kSide;
			image.Pixels.resize(static_cast<size_t>(kSide) * kSide * 4);
			for (uint32_t y = 0; y < kSide; ++y)
				for (uint32_t x = 0; x < kSide; ++x)
				{
					// +x to the right and row 0 the smallest z: the map as the default camera (yaw 0, looking down -z) sees the world
					const uint32_t i = std::min(f.N - 1, x * f.N / kSide), j = std::min(f.N - 1, y * f.N / kSide);
					const glm::vec4 c = PreviewColour(f.At(i, j));
					uint8_t* px = &image.Pixels[(static_cast<size_t>(y) * kSide + x) * 4];
					px[0] = static_cast<uint8_t>(c.r * 255.0f); px[1] = static_cast<uint8_t>(c.g * 255.0f); px[2] = static_cast<uint8_t>(c.b * 255.0f); px[3] = 255;
				}
			m_Preview.Texture = std::make_unique<Texture2D>(image, TextureEncoding::Linear);
		}
		return true;
	}

	void TerrainGraphUi::DrawPreview()
	{
		ImGui::Separator();
		ImGui::Text("Preview");
		if (TgNode* node = m_Graph->FindNode(SelNode))
		{
			const TgNodeSchema& s = SchemaOf(node->Type);
			for (const TgPortInfo& port : s.Outputs)
			{
				const bool on = m_Preview.Node == node->Id && m_Preview.Port == port.Name && (m_Preview.Map || m_Preview.Overlay);
				if (ImGui::RadioButton((port.Name + "##preview").c_str(), on))
					SetPreview(node->Id, port.Name, m_Preview.Overlay ? (m_Preview.Map ? "both" : "overlay") : "map");
				ImGui::SameLine();
			}
			ImGui::NewLine();
		}
		ImGui::Checkbox("map", &m_Preview.Map);
		ImGui::SameLine();
		ImGui::Checkbox("on the terrain", &m_Preview.Overlay);
		// T84: the quick preview (a low resolution while editing) and the full build after the edits
		ImGui::Checkbox("quick", &m_Quick.Enabled);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Evaluate the graph again at a low resolution after every edit (same seed and footprint as the world)");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(70.0f);
		static const uint32_t kQuickSides[] = { 129, 257, 513 };
		if (ImGui::BeginCombo("##quick_res", std::to_string(m_Quick.Resolution).c_str()))
		{
			for (const uint32_t side : kQuickSides)
				if (ImGui::Selectable(std::to_string(side).c_str(), side == m_Quick.Resolution)) m_Quick.Resolution = side;
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::Checkbox("full after edits", &m_Quick.AutoFull);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Build the whole world at its resolution %.1f s after the last edit, once no slider is held", static_cast<double>(m_Quick.FullDelay));
		ImGui::TextDisabled("%s", m_Quick.Running ? "quick preview running..." : QuickStatus().c_str());
		std::string node, port;
		if (!PreviewTarget(node, port)) { ImGui::TextDisabled("pick an output of the selected node"); return; }
		if (!RefreshPreview())
		{
			ImGui::TextDisabled("%s.%s: no value yet (quick preview, or Apply / Regenerate builds the world from the panel)", node.c_str(), port.c_str());
			return;
		}
		const TgValue& v = m_Preview.Value;
		ImGui::TextDisabled("%s.%s (%s, %s)", node.c_str(), port.c_str(), TgPortKindName(v.Kind), m_Preview.Source == m_Quick.Cache.get() ? "quick" : "full");
		if (v.Kind == TgPortKind::Field) ImGui::TextDisabled("%u x %u, %.4g .. %.4g", v.Field.N, v.Field.N, static_cast<double>(m_Preview.Min), static_cast<double>(m_Preview.Max));
		if (v.Kind == TgPortKind::Placements) ImGui::TextDisabled("%zu placements", v.Placements.Items.size());
		if (v.Kind == TgPortKind::Paths) ImGui::TextDisabled("%zu paths", v.Paths.Items.size());
		if (!m_Preview.Map || m_Preview.Size <= 0.0f) return;
		const float side = std::max(64.0f, std::min(ImGui::GetContentRegionAvail().x, 300.0f));
		const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + side, a.y + side);
		ImDrawList* draw = ImGui::GetWindowDrawList();
		if (m_Preview.Texture)
			draw->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(m_Preview.Texture->GetID())), a, b);
		else draw->AddRectFilled(a, b, IM_COL32(30, 34, 40, 255));
		const glm::vec2 min = m_Preview.Center - m_Preview.Size * 0.5f;
		auto toMap = [&](const glm::vec2& p) { return ImVec2(a.x + (p.x - min.x) / m_Preview.Size * side, a.y + (p.y - min.y) / m_Preview.Size * side); };
		if (v.Kind == TgPortKind::Placements)
		{
			const size_t step = std::max<size_t>(1, v.Placements.Items.size() / 6000);
			for (size_t i = 0; i < v.Placements.Items.size(); i += step) draw->AddCircleFilled(toMap(v.Placements.Items[i].Position), 1.5f, IM_COL32(150, 230, 110, 255), 4);
		}
		if (v.Kind == TgPortKind::Paths)
			for (const TgPath& path : v.Paths.Items)
			{
				std::vector<ImVec2> points;
				for (const glm::vec2& p : path.Points) points.push_back(toMap(p));
				if (points.size() == 1) draw->AddCircle(points[0], 5.0f, IM_COL32(240, 180, 90, 255), 12, 2.0f);
				else if (points.size() > 1) draw->AddPolyline(points.data(), static_cast<int>(points.size()), IM_COL32(240, 180, 90, 255), 0, 2.0f);
			}
		if (v.Kind == TgPortKind::Water)
		{
			if (v.Water.HasLake())
			{
				const LakeBody& lake = v.Water.Lake();
				std::vector<ImVec2> shore;
				for (int i = 0; i < 64; ++i)
				{
					const float t = 6.2831853f * static_cast<float>(i) / 64.0f;
					shore.push_back(toMap(lake.Center + glm::vec2(std::cos(t), std::sin(t)) * lake.Radius * lake.Outline.RadiusFactor(t)));
				}
				draw->AddConvexPolyFilled(shore.data(), static_cast<int>(shore.size()), IM_COL32(60, 150, 230, 200));
			}
			if (const RiverData* river = v.Water.River())
			{
				std::vector<ImVec2> points;
				for (const glm::vec3& p : river->Points) points.push_back(toMap(glm::vec2(p.x, p.z)));
				if (points.size() > 1) draw->AddPolyline(points.data(), static_cast<int>(points.size()), IM_COL32(60, 150, 230, 255), 0, 2.0f);
			}
		}
		draw->AddRect(a, b, IM_COL32(90, 90, 100, 255));
		ImGui::Dummy(ImVec2(side, side));
	}

	void TerrainGraphUi::DrawViewportOverlay(ImDrawList* draw, const glm::mat4& viewProj, float ox, float oy, float w, float h, const Terrain& terrain)
	{
		if (!m_Preview.Overlay || !RefreshPreview()) return;
		const TgValue& v = m_Preview.Value;
		const float lift = 0.4f + m_Preview.Size * 0.0004f;   // over the ground, so the quads are not hidden in it
		auto screen = [&](const glm::vec2& p, ImVec2& out)
		{
			glm::vec2 s;
			if (!ProjectToScreen(viewProj, glm::vec3(p.x, terrain.GetHeight(p.x, p.y) + lift, p.y), glm::vec2(w, h), s)) return false;
			out = ImVec2(ox + s.x, oy + s.y);
			return true;
		};
		if (v.Kind == TgPortKind::Field && v.Field.N > 1)
		{
			// the field over its footprint as a grid of translucent quads (at most 72 x 72: the draw list stays small whatever the field's size)
			const TgField& f = v.Field;
			const int cells = static_cast<int>(std::min<uint32_t>(72, f.N - 1));
			const float cell = f.Size / static_cast<float>(cells);
			const glm::vec2 min = f.Min();
			std::vector<ImVec2> corner(static_cast<size_t>(cells + 1) * (cells + 1));
			std::vector<char> ok(corner.size());
			for (int j = 0; j <= cells; ++j)
				for (int i = 0; i <= cells; ++i)
				{
					const size_t k = static_cast<size_t>(j) * (cells + 1) + i;
					ok[k] = screen(min + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * cell, corner[k]) ? 1 : 0;
				}
			for (int j = 0; j < cells; ++j)
				for (int i = 0; i < cells; ++i)
				{
					const size_t k00 = static_cast<size_t>(j) * (cells + 1) + i, k10 = k00 + 1, k01 = k00 + cells + 1, k11 = k01 + 1;
					if (!ok[k00] || !ok[k10] || !ok[k01] || !ok[k11]) continue;
					const glm::vec2 centre = min + (glm::vec2(static_cast<float>(i), static_cast<float>(j)) + 0.5f) * cell;
					const glm::vec4 c = PreviewColour(f.Sample(centre.x, centre.y));
					draw->AddQuadFilled(corner[k00], corner[k10], corner[k11], corner[k01],
						IM_COL32(static_cast<int>(c.r * 255.0f), static_cast<int>(c.g * 255.0f), static_cast<int>(c.b * 255.0f), 120));
				}
			return;
		}
		if (v.Kind == TgPortKind::Placements)
		{
			const size_t step = std::max<size_t>(1, v.Placements.Items.size() / 4000);
			for (size_t i = 0; i < v.Placements.Items.size(); i += step)
			{
				ImVec2 s;
				if (screen(v.Placements.Items[i].Position, s)) draw->AddCircleFilled(s, 3.0f, IM_COL32(150, 230, 110, 230), 6);
			}
			return;
		}
		auto line = [&](const std::vector<glm::vec2>& points, ImU32 colour)
		{
			std::vector<ImVec2> run;
			for (const glm::vec2& p : points)
			{
				ImVec2 s;
				if (screen(p, s)) { run.push_back(s); continue; }
				if (run.size() > 1) draw->AddPolyline(run.data(), static_cast<int>(run.size()), colour, 0, 3.0f);
				run.clear();
			}
			if (run.size() > 1) draw->AddPolyline(run.data(), static_cast<int>(run.size()), colour, 0, 3.0f);
		};
		if (v.Kind == TgPortKind::Paths)
			for (const TgPath& path : v.Paths.Items) line(path.Points, IM_COL32(240, 180, 90, 255));
		if (v.Kind == TgPortKind::Water)
		{
			if (v.Water.HasLake())
			{
				const LakeBody& lake = v.Water.Lake();
				std::vector<glm::vec2> shore;
				for (int i = 0; i <= 96; ++i)
				{
					const float t = 6.2831853f * static_cast<float>(i) / 96.0f;
					shore.push_back(lake.Center + glm::vec2(std::cos(t), std::sin(t)) * lake.Radius * lake.Outline.RadiusFactor(t));
				}
				line(shore, IM_COL32(60, 150, 230, 255));
			}
			if (const RiverData* river = v.Water.River())
			{
				std::vector<glm::vec2> axis;
				for (const glm::vec3& p : river->Points) axis.push_back(glm::vec2(p.x, p.z));
				line(axis, IM_COL32(60, 150, 230, 255));
			}
		}
	}

	bool TerrainGraphUi::ConsumeApplyRequest()
	{
		const bool requested = m_ApplyRequested;
		m_ApplyRequested = false;
		return requested;
	}

	void TerrainGraphUi::Connect(const std::string& from, const std::string& fromPort, TgNode& to, size_t input)
	{
		const TgNodeSchema& target = SchemaOf(to.Type);
		const TgNode* source = m_Graph->FindNode(from);
		if (!source || input >= target.Inputs.size()) return;
		const TgNodeSchema& origin = SchemaOf(source->Type);
		const int out = origin.OutputIndex(fromPort);
		if (out < 0) return;
		const TgPortInfo& o = origin.Outputs[static_cast<size_t>(out)];
		const TgPortInfo& i = target.Inputs[input];
		if (!Compatible(o.Kind, i.Kind))
		{
			m_Message = std::string("Incompatible ports: ") + TgPortKindName(o.Kind) + " -> " + TgPortKindName(i.Kind);
			m_MessageError = true;
			return;
		}
		// one edge per input: a new one replaces it
		m_Graph->Edges.erase(std::remove_if(m_Graph->Edges.begin(), m_Graph->Edges.end(), [&](const TgEdge& e)
		{
			if (e.To != to.Id) return false;
			if (e.Port == i.Name) return true;
			return target.InputIndex(e.Port) < 0;   // (a version 1 edge bound by order: rewired by name from now on)
		}), m_Graph->Edges.end());
		m_Graph->Edges.push_back({ from, to.Id, i.Name, o.Name });
		m_Message = "Connected " + from + "." + o.Name + " -> " + to.Id + "." + i.Name;
		m_MessageError = false;
	}

	void TerrainGraphUi::Draw(bool* open)
	{
		ImGui::SetNextWindowPos(ImVec2(40.0f, 60.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(1280.0f, 700.0f), ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("Terrain Graph", open)) { ImGui::End(); return; }
		if (open && !*open)   // closed this frame: what it keeps goes (T84)
		{
			ReleaseMemory();
			ImGui::End();
			return;
		}
		UpdateQuick();
		if (ImGui::Button("Apply / Regenerate"))
		{
			if (!HasTgErrors(ValidateTerrainGraph(*m_Graph, m_Registry)))
			{
				m_ApplyRequested = true;
				m_Message = "Graph queued for regeneration";
				m_MessageError = false;
			}
			else
			{
				m_Message = "Fix graph validation errors before applying";
				m_MessageError = true;
			}
		}
		DrawLibraryBar();
		ImGui::TextDisabled("drag an output pin to an input | right click: add | middle drag: pan | wheel: zoom | Del: delete edge");

		const std::vector<TgIssue> issues = ValidateTerrainGraph(*m_Graph, m_Registry);
		ImGui::BeginChild("terrain_graph_canvas", ImVec2(-340.0f, 0.0f), true);
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const ImVec2 size(std::max(1.0f, available.x), std::max(1.0f, available.y));
		ImGui::InvisibleButton("##terrain_canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		const bool hovered = ImGui::IsItemHovered();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(origin, Add(origin, size), true);
		draw->AddRectFilled(origin, Add(origin, size), IM_COL32(22, 24, 30, 255));

		auto height = [&](const TgNode& node)
		{
			const TgNodeSchema& s = SchemaOf(node.Type);
			return std::max(kNodeBaseH, 38.0f + static_cast<float>(std::max(s.Inputs.size(), s.Outputs.size())) * kPortRow) * Zoom;
		};
		auto screen = [&](const TgNode& node) { return CanvasToScreen(origin, node.X, node.Y); };
		auto pin = [&](const TgNode& node, size_t index, bool output)
		{
			const TgNodeSchema& s = SchemaOf(node.Type);
			const size_t count = output ? s.Outputs.size() : s.Inputs.size();
			const float y = (count <= 1 ? height(node) / Zoom * 0.5f : 30.0f + static_cast<float>(index) * kPortRow) * Zoom;
			const ImVec2 p = screen(node);
			return ImVec2(p.x + (output ? kNodeW * Zoom : 0.0f), p.y + y);
		};
		// the input an edge lands on: by name, else (a version 1 edge) the next one in order among the node's unnamed edges
		auto inputIndex = [&](const TgEdge& edge, const TgNode& to)
		{
			const TgNodeSchema& s = SchemaOf(to.Type);
			const int named = s.InputIndex(edge.Port);
			if (named >= 0) return named;
			std::vector<bool> taken(s.Inputs.size(), false);
			for (const TgEdge& e : m_Graph->Edges)
				if (e.To == to.Id && s.InputIndex(e.Port) >= 0) taken[static_cast<size_t>(s.InputIndex(e.Port))] = true;
			int slot = -1;
			for (const TgEdge& e : m_Graph->Edges)
			{
				if (e.To != to.Id || s.InputIndex(e.Port) >= 0) continue;
				do { ++slot; } while (slot < static_cast<int>(taken.size()) && taken[static_cast<size_t>(slot)]);
				if (&e == &edge) return slot < static_cast<int>(taken.size()) ? slot : -1;
			}
			return -1;
		};
		const ImVec2 mouse = ImGui::GetIO().MousePos;
		int hoveredEdge = -1;

		for (size_t edgeIndex = 0; edgeIndex < m_Graph->Edges.size(); ++edgeIndex)
		{
			const TgEdge& edge = m_Graph->Edges[edgeIndex];
			const TgNode* fromNode = m_Graph->FindNode(edge.From);
			const TgNode* toNode = m_Graph->FindNode(edge.To);
			if (!fromNode || !toNode) continue;
			const int out = OutputIndex(*fromNode, edge.FromPort), in = inputIndex(edge, *toNode);
			if (out < 0 || in < 0) continue;
			const ImVec2 from = pin(*fromNode, static_cast<size_t>(out), true), to = pin(*toNode, static_cast<size_t>(in), false);
			const float bend = std::max(40.0f, std::fabs(to.x - from.x) * 0.5f);
			if (DistanceToBezier(from, to, mouse) <= 9.0f) hoveredEdge = static_cast<int>(edgeIndex);
			const bool lit = static_cast<int>(edgeIndex) == SelEdge || edge.From == SelNode || edge.To == SelNode;
			draw->AddBezierCubic(from, ImVec2(from.x + bend, from.y), ImVec2(to.x - bend, to.y), to,
				lit ? IM_COL32(255, 220, 100, 255) : PinColor(SchemaOf(fromNode->Type).Outputs[static_cast<size_t>(out)].Kind), 2.0f);
		}
		if (!ConnectFrom.empty())
			if (const TgNode* source = m_Graph->FindNode(ConnectFrom))
			{
				const int out = OutputIndex(*source, m_ConnectPort);
				if (out >= 0) draw->AddLine(pin(*source, static_cast<size_t>(out), true), mouse, IM_COL32(255, 220, 100, 255), 2.0f);
			}

		std::set<std::string> broken;
		for (const TgIssue& issue : issues) if (issue.Error && !issue.Node.empty()) broken.insert(issue.Node);
		TgNode* hoveredNode = nullptr;
		for (TgNode& node : m_Graph->Nodes)
		{
			const TgNodeSchema& s = SchemaOf(node.Type);
			const ImVec2 p = screen(node), q(p.x + kNodeW * Zoom, p.y + height(node));
			if (mouse.x >= p.x && mouse.x <= q.x && mouse.y >= p.y && mouse.y <= q.y) hoveredNode = &node;
			draw->AddRectFilled(p, q, IM_COL32(44, 46, 54, 245), 5.0f);
			draw->AddRectFilled(p, ImVec2(q.x, p.y + 20.0f * Zoom), HeaderColor(s.Color), 5.0f, ImDrawFlags_RoundCornersTop);
			const ImU32 border = node.Id == SelNode ? IM_COL32(255, 255, 255, 255) : broken.count(node.Id) ? IM_COL32(240, 90, 80, 255) : IM_COL32(60, 60, 70, 255);
			draw->AddRect(p, q, border, 5.0f, 0, 2.0f);
			draw->AddText(ImVec2(p.x + 8.0f, p.y + 3.0f), IM_COL32_WHITE, node.Label.empty() ? node.Type.c_str() : node.Label.c_str());
			if (s.UsesSource && !node.Source.empty())
				draw->AddText(ImVec2(p.x + 8.0f, q.y - 16.0f), IM_COL32(255, 210, 120, 255), ("-> " + node.Source).c_str());
			for (size_t i = 0; i < s.Inputs.size(); ++i)
			{
				const ImVec2 at = pin(node, i, false);
				draw->AddCircleFilled(at, kPin, PinColor(s.Inputs[i].Kind));
				if (!s.Inputs[i].Optional) draw->AddCircle(at, kPin + 2.0f, IM_COL32(255, 255, 255, 120));
				draw->AddText(ImVec2(at.x + 9.0f, at.y - 7.0f), IM_COL32(220, 220, 230, 255), s.Inputs[i].Name.c_str());
			}
			for (size_t i = 0; i < s.Outputs.size(); ++i)
			{
				const ImVec2 at = pin(node, i, true);
				const ImVec2 text(at.x - ImGui::CalcTextSize(s.Outputs[i].Name.c_str()).x - 9.0f, at.y - 7.0f);
				draw->AddCircleFilled(at, kPin, PinColor(s.Outputs[i].Kind));
				draw->AddText(text, IM_COL32(220, 220, 230, 255), s.Outputs[i].Name.c_str());
			}
		}

		if (hovered)
		{
			if (ImGui::GetIO().MouseWheel != 0.0f) ZoomAt(origin, mouse, ImGui::GetIO().MouseWheel > 0.0f ? 1.1f : 1.0f / 1.1f);
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				if (hoveredNode)
				{
					SelNode = hoveredNode->Id;
					SelEdge = -1;
					const TgNodeSchema& s = SchemaOf(hoveredNode->Type);
					bool portHit = false;
					for (size_t i = 0; i < s.Outputs.size() && !portHit; ++i)
					{
						const ImVec2 at = pin(*hoveredNode, i, true);
						const float dx = mouse.x - at.x, dy = mouse.y - at.y;
						if (dx * dx + dy * dy <= 100.0f) { ConnectFrom = hoveredNode->Id; m_ConnectPort = s.Outputs[i].Name; portHit = true; }
					}
					for (size_t i = 0; i < s.Inputs.size() && !portHit; ++i)
					{
						const ImVec2 at = pin(*hoveredNode, i, false);
						const float dx = mouse.x - at.x, dy = mouse.y - at.y;
						if (dx * dx + dy * dy <= 100.0f) portHit = true;
					}
					if (!portHit) DragNode = hoveredNode->Id;
				}
				else if (hoveredEdge >= 0) { SelEdge = hoveredEdge; SelNode.clear(); }
				else { SelNode.clear(); SelEdge = -1; }
			}
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !DragNode.empty())
				if (TgNode* node = m_Graph->FindNode(DragNode))
				{
					const ImVec2 delta = ImGui::GetIO().MouseDelta;
					node->X += delta.x / Zoom;
					node->Y += delta.y / Zoom;
				}
			if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !ConnectFrom.empty())
			{
				TgNode* targetNode = nullptr;
				size_t targetIndex = 0;
				float best = 15.0f;
				for (TgNode& candidate : m_Graph->Nodes)
				{
					if (candidate.Id == ConnectFrom) continue;
					const TgNodeSchema& s = SchemaOf(candidate.Type);
					for (size_t i = 0; i < s.Inputs.size(); ++i)
					{
						const ImVec2 at = pin(candidate, i, false);
						const float d = std::sqrt((mouse.x - at.x) * (mouse.x - at.x) + (mouse.y - at.y) * (mouse.y - at.y));
						if (d <= best) { best = d; targetNode = &candidate; targetIndex = i; }
					}
				}
				if (!targetNode && hoveredNode && hoveredNode->Id != ConnectFrom)   // dropped on a node's body: its first compatible free input
				{
					const TgNode* source = m_Graph->FindNode(ConnectFrom);
					const TgNodeSchema& s = SchemaOf(hoveredNode->Type);
					const TgNodeSchema& o = SchemaOf(source ? source->Type : std::string());
					const int out = o.OutputIndex(m_ConnectPort);
					for (size_t i = 0; i < s.Inputs.size() && !targetNode && out >= 0; ++i)
						if (Compatible(o.Outputs[static_cast<size_t>(out)].Kind, s.Inputs[i].Kind)) { targetNode = hoveredNode; targetIndex = i; }
				}
				if (targetNode) Connect(ConnectFrom, m_ConnectPort, *targetNode, targetIndex);
				else { m_Message = "Drop on an input port"; m_MessageError = true; }
				ConnectFrom.clear();
			}
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
			{
				const ImVec2 delta = ImGui::GetIO().MouseDelta;
				Pan.x += delta.x; Pan.y += delta.y;
			}
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			{
				PopupAt = ScreenToCanvas(origin, mouse);
				ImGui::OpenPopup("terrain_add_node");
			}
		}
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) { DragNode.clear(); ConnectFrom.clear(); }
		if (SelEdge >= 0 && SelEdge < static_cast<int>(m_Graph->Edges.size()) && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete))
		{
			m_Graph->Edges.erase(m_Graph->Edges.begin() + SelEdge);
			SelEdge = -1;
			m_Message = "Connection deleted";
			m_MessageError = false;
		}
		if (ImGui::BeginPopup("terrain_add_node"))
		{
			std::map<std::string, std::vector<std::string>> byCategory;
			for (const std::string& type : m_Registry.KnownTypes()) byCategory[SchemaOf(type).Category].push_back(type);
			for (const auto& [category, types] : byCategory)
				if (ImGui::BeginMenu(category.c_str()))
				{
					for (const std::string& type : types)
					{
						if (ImGui::MenuItem(type.c_str())) { AddNode(type, PopupAt); ImGui::CloseCurrentPopup(); }
						if (ImGui::IsItemHovered() && !SchemaOf(type).Summary.empty()) ImGui::SetTooltip("%s", SchemaOf(type).Summary.c_str());
					}
					ImGui::EndMenu();
				}
			ImGui::EndPopup();
		}
		draw->PopClipRect();
		ImGui::EndChild();
		ImGui::SameLine();
		DrawInspector(issues);
		ImGui::End();
	}

	void TerrainGraphUi::DrawInspector(const std::vector<TgIssue>& issues)
	{
		ImGui::BeginChild("terrain_graph_inspector", ImVec2(0.0f, 0.0f), true);
		if (TgNode* node = m_Graph->FindNode(SelNode))
		{
			const TgNodeSchema& s = SchemaOf(node->Type);
			ImGui::Text("%s", node->Id.c_str());
			ImGui::TextDisabled("%s  (%s)", node->Type.c_str(), s.Category.c_str());
			if (!s.Summary.empty()) ImGui::TextWrapped("%s", s.Summary.c_str());
			char label[128]; std::snprintf(label, sizeof label, "%s", node->Label.c_str());
			if (ImGui::InputText("label", label, sizeof label)) node->Label = label;
			if (s.UsesSource)
			{
				char source[64]; std::snprintf(source, sizeof source, "%s", node->Source.c_str());
				if (ImGui::InputText(node->Type == "output" ? "publishes as" : "reads", source, sizeof source)) node->Source = source;
			}
			ImGui::Separator();
			for (const TgPortInfo& port : s.Inputs)
			{
				ImGui::BulletText("in  %s (%s%s)", port.Name.c_str(), TgPortKindName(port.Kind), port.Optional ? ", optional" : "");
				if (!port.Tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", port.Tooltip.c_str());
			}
			for (const TgPortInfo& port : s.Outputs) ImGui::BulletText("out %s (%s)", port.Name.c_str(), TgPortKindName(port.Kind));
			ImGui::Separator();
			for (const TgParamInfo& p : s.Params)
			{
				double& value = node->Params.try_emplace(p.Name, p.Default).first->second;
				if (p.Integer)
				{
					int v = static_cast<int>(value);
					if (ImGui::SliderInt(p.Name.c_str(), &v, static_cast<int>(p.Min), static_cast<int>(p.Max))) value = v;
				}
				else
				{
					float v = static_cast<float>(value);
					const float span = static_cast<float>(p.Max - p.Min);
					if (ImGui::DragFloat(p.Name.c_str(), &v, span > 0.0f ? span / 400.0f : 0.01f, static_cast<float>(p.Min), static_cast<float>(p.Max), "%.4g")) value = v;
				}
				if (!p.Tooltip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.Tooltip.c_str());
			}
			for (auto& [name, value] : node->Params)   // parameters the schema does not list (an older graph): still editable
			{
				if (s.FindParam(name)) continue;
				float v = static_cast<float>(value);
				if (ImGui::DragFloat((name + " (?)").c_str(), &v, 0.01f)) value = v;
			}
			if (ImGui::Button("Delete node"))
			{
				const std::string id = node->Id;
				m_Graph->Edges.erase(std::remove_if(m_Graph->Edges.begin(), m_Graph->Edges.end(), [&](const TgEdge& edge) { return edge.From == id || edge.To == id; }), m_Graph->Edges.end());
				m_Graph->Nodes.erase(std::remove_if(m_Graph->Nodes.begin(), m_Graph->Nodes.end(), [&](const TgNode& item) { return item.Id == id; }), m_Graph->Nodes.end());
				ResetCanvasInteraction();
			}
		}
		else ImGui::TextDisabled("Select a node");
		DrawPreview();
		ImGui::Separator();
		ImGui::Text("Validation");
		if (issues.empty()) ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "Graph is valid");
		for (const TgIssue& issue : issues)
			ImGui::TextColored(issue.Error ? ImVec4(1, 0.45f, 0.4f, 1) : ImVec4(1, 0.85f, 0.4f, 1), "%s%s: %s", issue.Error ? "error " : "warning ", issue.Node.c_str(), issue.Message.c_str());
		if (!m_Message.empty()) ImGui::TextColored(m_MessageError ? ImVec4(1, 0.45f, 0.4f, 1) : ImVec4(0.5f, 0.9f, 0.5f, 1), "%s", m_Message.c_str());
		ImGui::EndChild();
	}
}
