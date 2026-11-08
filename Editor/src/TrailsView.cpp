// T5c: the clans' trails and paved paths in the editor (the logic is GB/Game/Trails.h). What the clans wore down is drawn on the ground: the
// terrain's macro colour map is tinted towards dirt (and stone where it is paved) under the trails, and the grass is thinned on them (the
// trodden ground of WorldInstance). The navigation grid sees a trail as trampled undergrowth (WorldInstance::TrailEase, read by the grid's
// Vegetation): its cells are read again when their tier changes. All of it only where a tier changed, and at most every few seconds.
// `trails` in the console.
#include "EditorApp.h"
#include "GB/Renderer/Texture.h"
#include <glad/gl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace GB::Editor
{
	namespace
	{
		constexpr int kTile = 32;           // the untinted macro texels are kept in tiles of this many texels a side, read the first time one is tinted
		constexpr float kMerge = 24.0f;     // changed areas closer than this are redrawn as one rectangle

		bool TrailsOn(const Tribe& t) { return t.Config.Trails.Enabled && t.Trails().Ready(); }
		using Clock = std::chrono::steady_clock;
		double Ms(Clock::time_point since) { return std::chrono::duration<double, std::milli>(Clock::now() - since).count(); }
	}

	struct EditorApp::TrailsView
	{
		bool Hooked = false;
		// the macro map: the texels it had before they were tinted, by tile, for the world build and texture they belong to
		uint64_t Build = 0;
		Texture2D* Macro = nullptr;
		std::map<std::pair<int, int>, std::vector<uint8_t>> Tiles;
		// what changed since the last refresh: rectangles of the world (min x, min z, max x, max z), and the navigation blocks to read again
		std::vector<glm::vec4> Dirty;
		std::map<const Tribe*, glm::vec4> Grids;   // each clan's grid as last seen (origin x, z, side and whether its trails are on): a new one is drawn whole
		Clock::time_point LastRefresh{};
		long NavCalls = 0, NavSkipped = 0;   // the tribes' calls, and those that found no grid
		// for `trails view`
		int Refreshes = 0, NavUpdates = 0, GrassRegrows = 0, Rects = 0;
		long NavCells = 0, Texels = 0;
		double LastMs = 0.0, TotalMs = 0.0, PeakMs = 0.0, TintMs = 0.0, GrassMs = 0.0, NavMs = 0.0;

		void Touch(const glm::vec2& lo, const glm::vec2& hi)
		{
			for (glm::vec4& r : Dirty)
				if (lo.x <= r.z + kMerge && lo.y <= r.w + kMerge && hi.x >= r.x - kMerge && hi.y >= r.y - kMerge)
				{
					r = glm::vec4(std::min(r.x, lo.x), std::min(r.y, lo.y), std::max(r.z, hi.x), std::max(r.w, hi.y));
					return;
				}
			Dirty.emplace_back(lo.x, lo.y, hi.x, hi.y);
		}
	};

	void EditorApp::UpdateTrailsView()
	{
		if (!m_TrailsView) m_TrailsView = std::make_shared<TrailsView>();
		TrailsView& v = *m_TrailsView;
		if (!v.Hooked)
		{
			// the grass and the navigation ask the clans' grids directly (the same answer as the drawing, whenever they read it)
			m_World.TrailTrodden = [this](float x, float z)
			{
				float best = 0.0f;
				for (const auto& c : m_Clans.All())
					if (c->Folk && TrailsOn(*c->Folk))
					{
						const TrailVisualRules& r = c->Folk->Config.Trails.Visual;
						best = std::max(best, c->Folk->Trails().Smooth({ x, z }, c->Folk->Config.Trails, r.GrassTrail, r.GrassWorn, r.GrassPaved));
					}
				return best;
			};
			m_World.TrailEase = [this](float x, float z)
			{
				float best = 0.0f;
				for (const auto& c : m_Clans.All())
					if (c->Folk && TrailsOn(*c->Folk)) best = std::max(best, c->Folk->Trails().Ease({ x, z }, c->Folk->Config.Trails));
				return best;
			};
			v.Hooked = true;
		}
		if (!m_World.IsBuilt()) { v.Macro = nullptr; v.Tiles.clear(); return; }

		// what changed: the cells whose tier moved, a grid made anew (a new clan, a migration, a load) and a clan's trails switched on or off
		for (const auto& clan : m_Clans.All())
		{
			if (!clan->Folk) continue;
			Tribe& tribe = *clan->Folk;
			// the navigation is told by the tribe itself, within its tick (Tribe::SetTrailNavigation): the grid reads the ground again there
			tribe.SetTrailNavigation([this](const glm::vec2& lo, const glm::vec2& hi)
			{
				if (!m_TrailsView) return;
				TrailsView& tv = *m_TrailsView;
				++tv.NavCalls;
				NavGrid& nav = m_Actors.Navigation();
				if (!nav.Ready() || !m_World.IsBuilt()) { ++tv.NavSkipped; return; }
				// only the undergrowth is read again (the grid's ground may be a world being replaced: a replay rebuilds it within one command)
				const auto t0 = Clock::now();
				const int cells = nav.UpdateVegetation(lo, hi);
				tv.NavCells += cells;
				++tv.NavUpdates;
				tv.NavMs += Ms(t0);
			});
			TrailState& s = tribe.Trails();
			const glm::vec4 id = s.Ready() ? glm::vec4(s.Origin(), static_cast<float>(s.Side()), tribe.Config.Trails.Enabled ? 1.0f : 0.0f) : glm::vec4(0.0f);
			const auto known = v.Grids.find(&tribe);
			const bool fresh = known == v.Grids.end() || known->second != id;
			if (fresh && known != v.Grids.end() && known->second.z > 0.0f && glm::vec3(known->second) != glm::vec3(id))   // the old grid's ground goes back to how it was
			{
				const glm::vec2 o(known->second.x, known->second.y);
				v.Touch(o, o + glm::vec2(known->second.z * s.Cell()));
			}
			v.Grids[&tribe] = id;
			if (!s.Ready()) continue;
			bool all = false;
			const std::vector<int> cells = s.TakeChanged(all);
			const float h = s.Cell();
			if (fresh || all)   // the whole grid: where it has trails (a grid of untouched ground changes nothing)
			{
				for (int k = 0; k < s.Side() * s.Side(); ++k)
					if (s.Wear(k) > 0.0f || s.Paved(k)) { const glm::vec2 c = s.Centre(k); v.Touch(c - glm::vec2(h), c + glm::vec2(h)); }
				continue;
			}
			for (int k : cells) { const glm::vec2 c = s.Centre(k); v.Touch(c - glm::vec2(h), c + glm::vec2(h)); }
		}

		const TrailVisualRules& vis = m_Tribe.Config.Trails.Visual;
		const auto now = Clock::now();
		const auto since = [&](Clock::time_point t) { return std::chrono::duration<double>(now - t).count(); };
		if (!v.Dirty.empty() && since(v.LastRefresh) >= static_cast<double>(vis.RefreshSeconds))
		{
			v.LastRefresh = now;
			const auto t0 = Clock::now();
			std::vector<glm::vec4> rects;
			rects.swap(v.Dirty);
			for (glm::vec4& r : rects) r += glm::vec4(-1.0f, -1.0f, 1.0f, 1.0f);
			// 1. the ground: the macro colour map under the trails, from the texels it had before
			TerrainGpu& terrain = m_World.GetTerrainGpu();
			if (Texture2D* tex = terrain.MacroTexture(); tex && tex->GetWidth() > 0)
			{
				if (v.Macro != tex || v.Build != m_World.BuildCount()) { v.Macro = tex; v.Build = m_World.BuildCount(); v.Tiles.clear(); }
				const glm::vec4 mr = terrain.GetMaterial().MacroRect;   // min x, min z, size x, size z
				const int W = static_cast<int>(tex->GetWidth()), H = static_cast<int>(tex->GetHeight());
				const float tx = mr.z / static_cast<float>(W), tz = mr.w / static_cast<float>(H);
				std::vector<const Tribe*> tribes;
				for (const auto& c : m_Clans.All()) if (c->Folk && TrailsOn(*c->Folk)) tribes.push_back(c->Folk);
				std::vector<uint8_t> pixels;
				for (const glm::vec4& r : rects)
				{
					const int x0 = std::clamp(static_cast<int>(std::floor((r.x - mr.x) / tx)), 0, W), y0 = std::clamp(static_cast<int>(std::floor((r.y - mr.y) / tz)), 0, H);
					const int x1 = std::clamp(static_cast<int>(std::ceil((r.z - mr.x) / tx)), 0, W), y1 = std::clamp(static_cast<int>(std::ceil((r.w - mr.y) / tz)), 0, H);
					if (x1 <= x0 || y1 <= y0) continue;
					// the untinted texels of every tile the rectangle touches (read once: a tile is kept before anything is written into it)
					for (int ty = y0 / kTile; ty <= (y1 - 1) / kTile; ++ty)
						for (int txi = x0 / kTile; txi <= (x1 - 1) / kTile; ++txi)
						{
							auto& tile = v.Tiles[{ txi, ty }];
							if (!tile.empty()) continue;
							const int w = std::min(kTile, W - txi * kTile), h = std::min(kTile, H - ty * kTile);
							tile.assign(static_cast<size_t>(kTile) * kTile * 4u, 0);
							glPixelStorei(GL_PACK_ROW_LENGTH, kTile);
							glGetTextureSubImage(tex->GetID(), 0, txi * kTile, ty * kTile, 0, w, h, 1, GL_RGBA, GL_UNSIGNED_BYTE, static_cast<GLsizei>(tile.size()), tile.data());
							glPixelStorei(GL_PACK_ROW_LENGTH, 0);
						}
					const int w = x1 - x0, h = y1 - y0;
					pixels.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 0);
					for (int y = 0; y < h; ++y)
						for (int x = 0; x < w; ++x)
						{
							const int gx = x0 + x, gy = y0 + y;
							const std::vector<uint8_t>& tile = v.Tiles[{ gx / kTile, gy / kTile }];
							const size_t from = (static_cast<size_t>(gy % kTile) * kTile + static_cast<size_t>(gx % kTile)) * 4u;
							const size_t to = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u;
							const glm::vec2 p(mr.x + (static_cast<float>(gx) + 0.5f) * tx, mr.y + (static_cast<float>(gy) + 0.5f) * tz);
							float dirt = 0.0f, stone = 0.0f;
							for (const Tribe* t : tribes)
							{
								const TrailRules& rules = t->Config.Trails;
								dirt = std::max(dirt, t->Trails().Smooth(p, rules, rules.Visual.TintTrail, rules.Visual.TintWorn, 1.0f));
								stone = std::max(stone, t->Trails().Smooth(p, rules, 0.0f, 0.0f, 1.0f));
							}
							for (int k = 0; k < 3; ++k)
							{
								float c = static_cast<float>(tile[from + static_cast<size_t>(k)]) / 255.0f;
								c += (vis.Dirt[k] - c) * dirt;
								c += (vis.Stone[k] - c) * 0.9f * stone;
								pixels[to + static_cast<size_t>(k)] = static_cast<uint8_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f);
							}
							pixels[to + 3] = tile[from + 3];
						}
					tex->Update(static_cast<uint32_t>(x0), static_cast<uint32_t>(y0), static_cast<uint32_t>(w), static_cast<uint32_t>(h), pixels.data());
					v.Texels += static_cast<long>(w) * h;
					++v.Rects;
				}
			}
			v.TintMs += Ms(t0);
			// 2. the grass: the chunks under what changed grow again (thinned by TrailTrodden); nothing when none of it is in the field round the camera
			const auto t1 = Clock::now();
			if (m_World.RegrowGrassAreas(rects)) ++v.GrassRegrows;
			v.GrassMs += Ms(t1);
			v.LastMs = Ms(t0);
			v.TotalMs += v.LastMs;
			v.PeakMs = std::max(v.PeakMs, v.LastMs);
			++v.Refreshes;
		}
	}

	void EditorApp::RegisterTrailCommands()
	{
		m_Commands.Register("trails", "trails [<clan>] [report|on|off|pave on|off|view|rules]: the trails the clans wear by walking and the paths they pave (T5c, assets/data/trails.json)",
			[this](const std::vector<std::string>& a) -> CommandResult
		{
			std::vector<std::string> args = a;
			const Clan* only = nullptr;
			if (!args.empty() && args[0] != "report" && args[0] != "on" && args[0] != "off" && args[0] != "pave" && args[0] != "view" && args[0] != "rules" && args[0] != "probe")
			{
				only = m_Clans.Find(args[0]);
				if (!only || !only->Folk) return { false, "no clan " + args[0] };
				args.erase(args.begin());
			}
			const std::string what = args.empty() ? std::string("report") : args[0];
			auto forEach = [&](const std::function<void(Tribe&)>& f)
			{
				if (!only) f(m_Tribe);
				for (const auto& c : m_Clans.All()) if (c->Folk && c->Folk != &m_Tribe && (!only || c.get() == only)) f(*c->Folk);
			};
			if (what == "on" || what == "off")
			{
				const bool on = what == "on";
				forEach([&](Tribe& t) { t.Config.Trails.Enabled = on; });
				return { true, std::string("trails ") + what };
			}
			if (what == "pave")
			{
				if (args.size() < 2 || (args[1] != "on" && args[1] != "off")) return { false, "trails pave on|off" };
				const bool on = args[1] == "on";
				forEach([&](Tribe& t) { t.Config.Trails.Pave.Enabled = on; });
				return { true, std::string("paving ") + args[1] };
			}
			if (what == "rules")
			{
				const TrailRules& r = only ? only->Folk->Config.Trails : m_Tribe.Config.Trails;
				return { true, TrailRulesToJson(r) + (r == DefaultTrailRules() ? "\n(the same as the defaults in code)" : "\n(differs from the defaults in code)") };
			}
			const TrailsView* v = m_TrailsView.get();
			char line[600];
			if (what == "view")
			{
				if (!v) return { true, "trails view: not drawn yet" };
				std::snprintf(line, sizeof line, "trails view: %d refreshes (last %.2f ms, mean %.2f ms, peak %.2f ms; ground %.1f ms and grass %.1f ms in all), %d rectangles, %ld texels tinted, "
					"%d grass regrows, %zu macro tiles kept; navigation: %d block reads, %ld cells, %.2f ms in all (%ld calls, %ld without a grid)",
					v->Refreshes, v->LastMs, v->Refreshes ? v->TotalMs / v->Refreshes : 0.0, v->PeakMs, v->TintMs, v->GrassMs, v->Rects, v->Texels, v->GrassRegrows, v->Tiles.size(),
					v->NavUpdates, v->NavCells, v->NavMs, v->NavCalls, v->NavSkipped);
				return { true, line };
			}
			if (what == "probe" && args.size() >= 3 && m_World.IsBuilt())   // what the trails and the ground are at a place (a check of the drawing)
			{
				const glm::vec2 p(std::stof(args[1]), std::stof(args[2]));
				std::ostringstream out;
				out << "trails at (" << p.x << ", " << p.y << "): trodden " << (m_World.TrailTrodden ? m_World.TrailTrodden(p.x, p.y) : -1.0f) << ", ease "
					<< (m_World.TrailEase ? m_World.TrailEase(p.x, p.y) : -1.0f) << ", grass trodden " << m_World.TroddenAt(p.x, p.y);
				for (const auto& c : m_Clans.All())
					if (c->Folk && c->Folk->Trails().Ready())
					{
						const TrailState& s = c->Folk->Trails();
						int i = 0, j = 0;
						if (s.CellOf(p, i, j)) out << "\n  " << c->Name << ": wear " << s.Wear(s.Index(i, j)) << ", tier " << static_cast<int>(s.TierAt(s.Index(i, j), c->Folk->Config.Trails));
					}
				TerrainGpu& terrain = m_World.GetTerrainGpu();
				if (Texture2D* tex = terrain.MacroTexture())
				{
					const glm::vec4 mr = terrain.GetMaterial().MacroRect;
					const int x = static_cast<int>((p.x - mr.x) / mr.z * static_cast<float>(tex->GetWidth())), y = static_cast<int>((p.y - mr.y) / mr.w * static_cast<float>(tex->GetHeight()));
					uint8_t now[4] = {};
					glGetTextureSubImage(tex->GetID(), 0, x, y, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, 4, now);
					out << "\n  macro texel (" << x << ", " << y << ") of " << tex->GetWidth() << ": now " << int(now[0]) << " " << int(now[1]) << " " << int(now[2]);
					if (v)
						if (const auto t = v->Tiles.find({ x / kTile, y / kTile }); t != v->Tiles.end())
						{
							const size_t o = (static_cast<size_t>(y % kTile) * kTile + static_cast<size_t>(x % kTile)) * 4u;
							out << ", was " << int(t->second[o]) << " " << int(t->second[o + 1]) << " " << int(t->second[o + 2]);
						}
					out << "; material macro " << (terrain.GetMaterial().MacroMap == tex ? "is" : "is NOT") << " this texture";
				}
				return { true, out.str() };
			}
			if (what != "report") return { false, "trails [<clan>] [report|on|off|pave on|off|view|rules|probe x z]" };
			std::ostringstream out;
			bool any = false;
			for (const auto& c : m_Clans.All())
			{
				if (!c->Folk || (only && c.get() != only)) continue;
				out << (any ? "\n" : "") << c->Name << ": " << c->Folk->TrailsReport();
				any = true;
			}
			if (!any) out << m_Tribe.TrailsReport();
			return { true, out.str() };
		});
	}
}
