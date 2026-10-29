// H12 · exploration, expeditions and migration: the part of the tribe that knows the world region by region (GB/World/Regions.h), sends parties
// to find what the clan lacks and moves the camp when its home runs dry. Kept apart from Tribe.cpp (the senses, the needs and the decision of each
// member) and TribeWork.cpp (who does what), as H7 was; the hooks there are few: Update calls StepExploration, Decide calls ExploreOptions, Execute
// and Finish hand "expedition" and "migrate" here, Share tells the clan's map (ShareAtlas), PostJobs posts the expedition and JobScore asks
// ExpeditionJobScore. Without regions (SetRegions) none of it runs. Deterministic like the rest: maps in id order, ties to the first, no randomness.
//
// The loop (see docs/EXPLORACION.md):
//  * Map. Every SeeEvery seconds each member marks the scout cells it has in view (vision range) as its own; at the camp they join the clan's
//    atlas with what it remembers, region by region (explored share, the nodes it knows there, the last visit).
//  * Scarcity. A gather job (the store wants an item) that nobody can take because nobody knows a node of that item, for Scarcity seconds, makes
//    the clan post an "expedition <item>" job for the region where finding it is likeliest for the trip: the chance (the clan saw some there; else
//    the share of the region's ground on which the item's node grows, by the data's spawn biomes, times what is still unexplored), the travel
//    (TravelScale) and the danger (RiskWeight). Members who can gather the item claim it like any job (role and aptitude); with MinParty (or with
//    whoever there is after Gather seconds) the party takes some food and goes: each walks the scout points and comes back, and tells at the camp.
//  * Migration. Every Every seconds the home (HomeRadius around the camp) is scored by what the clan knows grows there (SiteScore). A home poorer
//    than Poor x the best it was, without food, or without water, for After seconds, makes the clan move to a better place it knows (the site
//    chooser: H8's when it gives one, ChooseCampSite until then): the store and the campfire go with the camp and the members walk there.
#include "GB/Game/Tribe.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace GB
{
	namespace
	{
		constexpr float kAtCamp = 8.0f;   // a member back from an expedition this close to the camp has told the clan

		void Go(char* buffer, size_t size, const glm::vec2& at, const char* tail)
		{
			std::snprintf(buffer, size, "goto %.2f %.2f | %s", static_cast<double>(at.x), static_cast<double>(at.y), tail);
		}

		std::string Place(const glm::vec2& p)
		{
			return "(" + std::to_string(static_cast<int>(std::lround(p.x))) + ", " + std::to_string(static_cast<int>(std::lround(p.y))) + ")";
		}

		std::string Joined(const std::vector<std::string>& names)
		{
			std::string s;
			for (size_t i = 0; i < names.size(); ++i) s += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
			return s;
		}
	}

	// ------------------------------------------------------------------ setting up
	void Tribe::SetRegions(const RegionMap* regions, std::function<float(float, float)> height)
	{
		m_Regions = regions && !regions->Empty() ? regions : nullptr;
		m_Height = std::move(height);
		ClanAtlas& atlas = m_Explore.Atlas;
		const size_t cells = m_Regions ? static_cast<size_t>(m_Regions->CellCount()) : 0;
		if (m_Regions && atlas.Seen.size() != cells)   // another world (or a save of none): what the clan knew of the old one means nothing here
		{
			atlas.Seen.assign(cells, 0);
			atlas.Lore.clear();
		}
	}

	void Tribe::TellClan(const std::string& text)
	{
		char t[32];
		std::snprintf(t, sizeof t, "%6.1f ", m_Time);
		m_Log.push_back(t + std::string("the clan ") + text);
		if (static_cast<int>(m_Log.size()) > Config.LogLines + Config.LogLines / 4)
			m_Log.erase(m_Log.begin(), m_Log.begin() + static_cast<std::ptrdiff_t>(m_Log.size() - static_cast<size_t>(Config.LogLines)));
	}

	// ------------------------------------------------------------------ what the clan knows
	// A node is worth knowing about while it has something or grows back (an empty bush regrows; a quarried rock does not).
	bool Tribe::KnowsNodeOf(const std::string& item, bool clanOnly) const
	{
		auto knows = [&](const SpatialMemory& memory)
		{
			for (const auto& [name, e] : memory.All())
			{
				const auto def = m_Defs.find(e.Kind);
				if (def != m_Defs.end() && def->second.Item == item && (e.Amount != 0 || def->second.RegenSeconds > 0.0f)) return true;
			}
			return false;
		};
		if (knows(m_Clan)) return true;
		if (clanOnly) return false;
		for (const TribeMember& m : m_Members)
			if (!m.Dead && m.Body && knows(m.Mind.Memory)) return true;
		return false;
	}

	bool Tribe::CanGatherItem(const TribeMember& m, const std::string& item) const
	{
		if (m.Dead || !m.Body) return false;
		for (const auto& [kind, def] : m_Defs)
			if (def.Item == item && (def.RequiresTag.empty() || m.Body->Bag().CountTagged(*m_Items, def.RequiresTag) > 0)) return true;
		return false;
	}

	// The members' map joins the clan's at the camp (Share calls it): the cells it saw, the last visit of their regions and, from the clan's memory,
	// the nodes it knows in each region.
	void Tribe::ShareAtlas(TribeMember& m)
	{
		if (!m_Regions) return;
		ClanAtlas& atlas = m_Explore.Atlas;
		const auto pending = atlas.Pending.find(m.Name);
		if (pending != atlas.Pending.end())
		{
			for (const auto& [cell, when] : pending->second)
			{
				if (cell < 0 || cell >= static_cast<int>(atlas.Seen.size())) continue;
				RegionLore& lore = atlas.Lore[m_Regions->CellRegion(cell)];
				lore.LastVisit = std::max(lore.LastVisit, when);
				if (atlas.Seen[static_cast<size_t>(cell)]) continue;
				atlas.Seen[static_cast<size_t>(cell)] = 1;
				++lore.SeenCells;
			}
			atlas.Pending.erase(pending);
		}
		RefreshLore();
	}

	void Tribe::RefreshLore()
	{
		ClanAtlas& atlas = m_Explore.Atlas;
		std::map<int, std::map<std::string, int>> counts;
		for (const auto& [name, e] : m_Clan.All())
			if (m_Defs.count(e.Kind)) ++counts[m_Regions->RegionAt(e.Pos)][e.Kind];
		for (const auto& [region, kinds] : counts) atlas.Lore[region].Resources = kinds;   // a region the memory says nothing of keeps what it last knew
		for (auto& [region, lore] : atlas.Lore)
			lore.Explored = static_cast<float>(lore.SeenCells) / static_cast<float>(std::max(m_Regions->CellsIn(region), 1));
	}

	// ------------------------------------------------------------------ the clock
	void Tribe::StepExploration(float dt)
	{
		if (!m_Regions) return;
		ExplorationState& x = m_Explore;
		const ExpeditionRules& rules = Config.Explore.Expeditions;
		if (x.Atlas.Seen.size() != static_cast<size_t>(m_Regions->CellCount())) SetRegions(m_Regions, m_Height);
		x.SeeTimer += dt;
		if (x.SeeTimer >= rules.SeeEvery)
		{
			x.SeeTimer = 0.0f;
			std::vector<int> cells;
			for (const TribeMember& m : m_Members)
			{
				if (m.Dead || !m.Body) continue;
				m_Regions->CellsNear(Where(m), Config.Vision.Range, cells);
				std::map<int, double>& mine = x.Atlas.Pending[m.Name];
				for (const int c : cells) mine[c] = m_Time;
			}
		}
		x.Timer += dt;
		if (x.Timer >= rules.Every) { const float step = x.Timer; x.Timer = 0.0f; StepExpedition(step); }
		x.HomeTimer += dt;
		if (x.HomeTimer >= Config.Explore.Migration.Every) { const float step = x.HomeTimer; x.HomeTimer = 0.0f; CheckHome(step); }
	}

	// ------------------------------------------------------------------ expeditions
	bool Tribe::ChooseExpeditionRegion(const std::string& item, int& region, std::string& why) const
	{
		const ExpeditionRules& rules = Config.Explore.Expeditions;
		float best = 0.0f;
		region = -1;
		for (const RegionSummary& g : m_Regions->Regions())
		{
			if (g.Water) continue;
			if (const auto t = m_Explore.Tried.find(item + "@" + std::to_string(g.Id)); t != m_Explore.Tried.end() && m_Time - t->second < static_cast<double>(rules.Retry)) continue;
			const auto lore = m_Explore.Atlas.Lore.find(g.Id);
			int known = 0;
			float fit = 0.0f;
			for (const auto& [kind, def] : m_Defs)
			{
				if (def.Item != item) continue;
				if (lore != m_Explore.Atlas.Lore.end())
					if (const auto r = lore->second.Resources.find(kind); r != lore->second.Resources.end()) known += r->second;
				float share = 0.0f;
				if (def.Spawn.Placed())
				{
					for (const std::string& biome : def.Spawn.Biomes)
						for (int b = 0; b < 7; ++b)
							if (biome == BiomeName(static_cast<Biome>(b))) share += g.Biomes[static_cast<size_t>(b)];
				}
				else share = 0.2f;   // it grows we do not know where: any land might have it
				fit = std::max(fit, std::min(share, 1.0f));
			}
			const float explored = lore != m_Explore.Atlas.Lore.end() ? lore->second.Explored : 0.0f;
			const float chance = known > 0 ? 1.0f : fit * (1.0f - explored);
			if (chance <= 0.0f) continue;
			const float d = glm::distance(Config.Camp, g.Anchor);
			if (d > rules.MaxDistance) continue;
			const float travel = 1.0f / (1.0f + d / rules.TravelScale);
			const float risk = std::max(0.05f, 1.0f - rules.RiskWeight * g.Danger);
			const float score = chance * travel * risk;
			if (score > best + 1e-6f)
			{
				best = score;
				region = g.Id;
				char buf[200];
				if (known > 0) std::snprintf(buf, sizeof buf, "the clan saw %d nodes of it there; %.0f m away, danger %.2f", known, static_cast<double>(d), static_cast<double>(g.Danger));
				else std::snprintf(buf, sizeof buf, "%.0f%% of its ground suits it and %.0f%% is unknown; %.0f m away, danger %.2f", static_cast<double>(fit * 100.0f),
					static_cast<double>((1.0f - explored) * 100.0f), static_cast<double>(d), static_cast<double>(g.Danger));
				why = buf;
			}
		}
		return region >= 0;
	}

	void Tribe::StepExpedition(float step)
	{
		ExplorationState& x = m_Explore;
		const ExpeditionRules& rules = Config.Explore.Expeditions;
		Expedition& e = x.Current;
		if (x.Moving) return;   // a clan on the move sends nobody away
		if (!e.Active())
		{
			// what the clan wants (its gather jobs) and nobody knows where to find
			std::map<std::string, float> unmet;
			for (const auto& [id, job] : m_Board.Jobs())
			{
				if (job.Kind != "gather" || KnowsNodeOf(job.Target, false)) continue;
				bool someone = false;
				for (const TribeMember& m : m_Members) someone = someone || CanGatherItem(m, job.Target);
				if (!someone) continue;   // (copper without a pickaxe: finding it would not help yet)
				const auto was = x.Unmet.find(job.Target);
				unmet[job.Target] = (was != x.Unmet.end() ? was->second : 0.0f) + step;
			}
			x.Unmet = unmet;
			std::string item;
			float priority = 0.0f;
			for (const auto& [it, seconds] : x.Unmet)
				if (seconds >= rules.Scarcity)
					if (const Job* job = m_Board.Find("gather " + it); job && job->Priority > priority) { priority = job->Priority; item = it; }
			if (item.empty()) return;
			x.Unmet[item] = 0.0f;   // looked into: again after another Scarcity seconds
			int region = -1;
			std::string why;
			if (!ChooseExpeditionRegion(item, region, why))
			{
				TellClan("lacks " + item + " and knows of no region worth looking in for it");
				return;
			}
			const RegionSummary& g = *m_Regions->Find(region);
			e = Expedition();
			e.Id = x.NextId++;
			e.Item = item;
			e.Region = region;
			e.RegionName = g.Name;
			e.Stage = "forming";
			e.Posted = m_Time;
			e.Why = why;
			// the scout points: spread over the region (farthest first from what is chosen), then walked nearest-first from the camp
			std::vector<glm::vec2> candidates;
			for (const int t : m_Regions->TilesOf(region)) candidates.push_back(m_Regions->TileAnchor(t));
			std::vector<glm::vec2> chosen{ g.Anchor };
			while (static_cast<int>(chosen.size()) < std::max(rules.ScoutPoints, 1) && chosen.size() < candidates.size() + 1)
			{
				float far = -1.0f;
				size_t pick = 0;
				for (size_t i = 0; i < candidates.size(); ++i)
				{
					float d = 1e30f;
					for (const glm::vec2& c : chosen) d = std::min(d, glm::distance(c, candidates[i]));
					if (d > far + 1e-4f) { far = d; pick = i; }
				}
				if (far < 1.0f) break;
				chosen.push_back(candidates[pick]);
			}
			glm::vec2 from = Config.Camp;
			while (!chosen.empty())
			{
				size_t next = 0;
				for (size_t i = 1; i < chosen.size(); ++i)
					if (glm::distance(from, chosen[i]) < glm::distance(from, chosen[next]) - 1e-4f) next = i;
				from = chosen[next];
				e.Points.push_back(from);
				chosen.erase(chosen.begin() + static_cast<std::ptrdiff_t>(next));
			}
			TellClan("knows nowhere " + item + " grows: it calls for an expedition to " + g.Name + " (" + why + ")");
			PostJobs();   // on the board at once, so the members can claim it as they decide
			return;
		}

		if (e.Stage == "forming")
		{
			std::vector<std::string> party;
			if (const Job* job = m_Board.Find("expedition " + e.Item))
				for (const JobClaim& c : job->Claims)
					for (const TribeMember& m : m_Members)
						if (m.Name == c.Member && !m.Dead && m.Body && static_cast<int>(party.size()) < rules.MaxParty) party.push_back(m.Name);
			const double waited = m_Time - e.Posted;
			if (static_cast<int>(party.size()) >= rules.MinParty || (!party.empty() && waited >= static_cast<double>(rules.Gather)))
			{
				e.Stage = "out";
				e.Started = m_Time;
				e.Party = party;
				for (const std::string& who : party) e.Leg[who] = 0;
				++x.Atlas.Lore[e.Region].Expeditions;
				for (TribeMember& m : m_Members)   // some food for the road
				{
					if (std::find(party.begin(), party.end(), m.Name) == party.end()) continue;
					for (int k = 0; k < rules.Food; ++k)
					{
						const std::string food = FirstFood(m_Store);
						if (food.empty() || m_Store.Transfer(*m_Items, m.Body->Bag(), food, 1) != 1) break;
					}
				}
				TellClan("sends " + Joined(party) + " to " + e.RegionName + " to look for " + e.Item);
			}
			else if (waited >= 3.0 * static_cast<double>(rules.Gather))
			{
				TellClan("finds nobody to go to " + e.RegionName + " for " + e.Item + ": the expedition is called off");
				e = Expedition();
			}
			return;
		}

		// out: the claims stay alive while the party walks; it ends when everyone is back (or dead), or after MaxSeconds (then they come home)
		bool all = true;
		const int home = static_cast<int>(e.Points.size());
		for (const std::string& who : e.Party)
		{
			const auto m = std::find_if(m_Members.begin(), m_Members.end(), [&](const TribeMember& t) { return t.Name == who; });
			if (m == m_Members.end() || m->Dead || !m->Body) continue;
			m_Board.Progress(who, 0.0f, m_Time);
			if (e.Leg[who] <= home) all = false;
		}
		const double out = m_Time - e.Started;
		if (!all && out >= static_cast<double>(rules.MaxSeconds))
			for (auto& [who, leg] : e.Leg) leg = std::max(leg, home);   // time is up: home
		if (all || out >= 1.5 * static_cast<double>(rules.MaxSeconds)) EndExpedition();
	}

	void Tribe::EndExpedition()
	{
		ExplorationState& x = m_Explore;
		Expedition& e = x.Current;
		RefreshLore();
		int found = 0;
		std::string kinds;
		if (const auto lore = x.Atlas.Lore.find(e.Region); lore != x.Atlas.Lore.end())
			for (const auto& [kind, n] : lore->second.Resources)
				if (const auto def = m_Defs.find(kind); def != m_Defs.end() && def->second.Item == e.Item && n > 0) { found += n; kinds += (kinds.empty() ? "" : ", ") + std::to_string(n) + " " + kind; }
		const bool known = KnowsNodeOf(e.Item, true);
		ExpeditionRecord r;
		r.Id = e.Id;
		r.Item = e.Item;
		r.Region = e.Region;
		r.RegionName = e.RegionName;
		r.Found = found > 0 || known;
		r.Started = e.Started;
		r.Ended = m_Time;
		r.Party = e.Party;
		x.History.push_back(r);
		if (x.History.size() > 32) x.History.erase(x.History.begin());
		if (found > 0) TellClan("hears from the expedition to " + e.RegionName + ": " + kinds + " (" + e.Item + ")");
		else if (known) TellClan("hears from the expedition to " + e.RegionName + ": no " + e.Item + " there, but it now knows where some grows");
		else
		{
			TellClan("hears from the expedition to " + e.RegionName + ": no " + e.Item + " there");
			x.Tried[e.Item + "@" + std::to_string(e.Region)] = m_Time;
		}
		for (const std::string& who : e.Party)
			if (const Job* held = m_Board.ClaimOf(who); held && held->Kind == "expedition") m_Board.Release(who);
		x.Unmet.erase(e.Item);
		e = Expedition();
	}

	void Tribe::PostExpedition()
	{
		const Expedition& e = m_Explore.Current;
		if (!m_Regions || !e.Active()) return;
		const ExpeditionRules& rules = Config.Explore.Expeditions;
		std::string family;
		for (const auto& [kind, def] : m_Defs) if (def.Item == e.Item) { family = SkillFamilyOf(kind); break; }
		const int slots = e.Stage == "forming" ? std::max(rules.MaxParty, 1) : std::max(static_cast<int>(e.Party.size()), 1);
		m_Board.Post("expedition", e.Item, family, 1.0f, rules.Priority, slots, "to " + e.RegionName + ": " + e.Why, m_Time, Config.Work.Jobs);
	}

	// Forming: whoever can gather the item, by how much it likes that work (role and aptitude) and how fit it is. Out: the party keeps it.
	float Tribe::ExpeditionJobScore(const TribeMember& m, const Job& job) const
	{
		const Expedition& e = m_Explore.Current;
		if (!e.Active() || e.Item != job.Target || m.Dead || !m.Body) return 0.0f;
		if (e.Stage == "out") return std::find(e.Party.begin(), e.Party.end(), m.Name) != e.Party.end() ? 10.0f : 0.0f;
		if (!CanGatherItem(m, e.Item)) return 0.0f;
		float liking = 0.0f;
		for (const auto& [kind, def] : m_Defs) if (def.Item == e.Item) liking = std::max(liking, MemberWeight(m, kind));
		const AbilitySystemComponent* asc = m_Gameplay ? m_Gameplay->Find(m.Name) : nullptr;
		const float vigor = asc && asc->Attributes().Has(NeedName::Vigor) ? asc->Attribute(NeedName::Vigor) : 1.0f;
		const float health = asc && asc->Attributes().Has(NeedName::Health) ? std::clamp(asc->Attribute(NeedName::Health) / 100.0f, 0.0f, 1.0f) : 1.0f;
		return job.Priority * liking * vigor * health;
	}

	// ------------------------------------------------------------------ the options of a member
	void Tribe::ExploreOptions(TribeMember& m, std::vector<UtilityOption>& options)
	{
		if (!m_Regions) return;
		const Expedition& e = m_Explore.Current;
		char buf[160];
		if (e.Stage == "out")
			if (const auto leg = e.Leg.find(m.Name); leg != e.Leg.end() && leg->second <= static_cast<int>(e.Points.size()))
			{
				const int n = static_cast<int>(e.Points.size());
				UtilityOption o;
				o.Kind = "expedition";
				o.Target = e.RegionName;
				o.Where = leg->second < n ? e.Points[static_cast<size_t>(leg->second)] : CampSpot(m);
				o.Score = Config.Explore.Expeditions.Score;
				o.Commit = 20.0f;
				o.Factors.push_back({ "expedition", o.Score });
				if (leg->second < n) std::snprintf(buf, sizeof buf, "looks for %s in %s (place %d of %d)", e.Item.c_str(), e.RegionName.c_str(), leg->second + 1, n);
				else std::snprintf(buf, sizeof buf, "comes home from %s to tell the clan", e.RegionName.c_str());
				o.Note = buf;
				options.push_back(o);
			}
		if (m_Explore.Moving && !m_Explore.Arrived.count(m.Name))
		{
			UtilityOption o;
			o.Kind = "migrate";
			o.Target = m_Explore.Moves.empty() ? std::string() : m_Explore.Moves.back().Region;
			o.Where = CampSpot(m);
			o.Score = Config.Explore.Migration.Score;
			o.Commit = 30.0f;
			o.Factors.push_back({ "the clan moves", o.Score });
			o.Note = "the clan moves its camp to " + Place(Config.Camp);
			options.push_back(o);
		}
	}

	bool Tribe::ExecuteExplore(TribeMember& m, const UtilityOption& o)
	{
		char go[96];
		if (o.Kind == "migrate")
		{
			Go(go, sizeof go, o.Where, "wait 1");
			if (!Order(m, go)) return false;
			m.Job = "migrate";
			m.Detail = o.Target;
			return true;
		}
		Expedition& e = m_Explore.Current;
		const auto leg = e.Leg.find(m.Name);
		if (e.Stage != "out" || leg == e.Leg.end()) return false;
		const int n = static_cast<int>(e.Points.size());
		if (leg->second < n && !m.Body->CanReach(o.Where))   // a lake or a cliff in the way: the next place
		{
			Say(m, "cannot get to " + Place(o.Where) + " in " + e.RegionName + ": it goes on to the next place");
			++leg->second;
			return false;
		}
		Go(go, sizeof go, o.Where, leg->second < n ? "wait 3" : "wait 1");   // it looks around before it goes on
		if (!Order(m, go)) return false;
		m.Job = "expedition";
		m.Detail = e.RegionName;
		if (leg->second == 0 && m.Mind.Current.Kind != "expedition") Say(m, "sets off for " + e.RegionName + " to look for " + e.Item);
		return true;
	}

	void Tribe::FinishExplore(TribeMember& m, const std::string& job)
	{
		const glm::vec2 pos = Where(m);
		if (job == "migrate")
		{
			if (glm::distance(pos, Config.Camp) > Config.Explore.Migration.Arrive) return;   // (did not get there: it chooses again)
			m_Explore.Arrived.insert(m.Name);
			Say(m, "is at the new camp");
			bool all = true;
			for (const TribeMember& t : m_Members) if (!t.Dead && t.Body && !m_Explore.Arrived.count(t.Name)) all = false;
			if (all)
			{
				m_Explore.Moving = false;
				m_Explore.Arrived.clear();
				TellClan("has all its members at its new camp " + Place(Config.Camp));
			}
			return;
		}
		Expedition& e = m_Explore.Current;
		const auto leg = e.Leg.find(m.Name);
		if (e.Stage != "out" || leg == e.Leg.end()) return;
		const int n = static_cast<int>(e.Points.size());
		m_Board.Progress(m.Name, 1.0f, m_Time);
		if (leg->second < n)
		{
			if (HasFailed(m)) Say(m, "could not get to place " + std::to_string(leg->second + 1) + " in " + e.RegionName);
			++leg->second;
			if (leg->second == n) Say(m, "has seen what it could of " + e.RegionName + " and heads home");
			return;
		}
		if (leg->second == n && HasFailed(m) && glm::distance(pos, Config.Camp) > kAtCamp)   // the way home is closed (a search that ran out of its budget, a lake): not for ever
		{
			if (++m.HomeTries >= 3)
			{
				m.HomeTries = 0;
				leg->second = n + 1;
				Say(m, "cannot find the way home from " + e.RegionName + " and gives up telling the clan");
			}
			return;
		}
		if (leg->second == n && glm::distance(pos, Config.Camp) <= kAtCamp)
		{
			m.HomeTries = 0;
			leg->second = n + 1;
			m.Mind.SharedAt = -1e9;   // it tells the clan now, whatever it told last
			Share(m);
			Say(m, "is back from " + e.RegionName + " and tells the clan what it saw");
		}
	}

	// ------------------------------------------------------------------ the home and the move
	float Tribe::SiteScore(const glm::vec2& at) const
	{
		const MigrationRules& rules = Config.Explore.Migration;
		std::map<std::string, int> near;   // item -> nodes of it known near the place
		std::map<std::string, bool> items;
		for (const auto& [kind, def] : m_Defs) items[def.Item] = true;
		bool well = false;
		for (const auto& [name, e] : m_Clan.All())
		{
			if (glm::distance(e.Pos, at) > rules.HomeRadius) continue;
			if (e.Kind == "well") { well = true; continue; }
			const auto def = m_Defs.find(e.Kind);
			if (def == m_Defs.end() || (e.Amount == 0 && def->second.RegenSeconds <= 0.0f)) continue;
			++near[def->second.Item];
		}
		float sum = 0.0f, weights = 0.0f;
		for (const auto& [item, any] : items)
		{
			const float w = IsFood(item) ? 2.0f : 1.0f;   // the food weighs most: a clan lives where it can eat
			const auto n = near.find(item);
			sum += w * std::min(n != near.end() ? static_cast<float>(n->second) : 0.0f, 3.0f) / 3.0f;
			weights += w;
		}
		float score = weights > 0.0f ? sum / weights : 0.0f;
		if (well || (m_Regions && m_Regions->WaterDistanceAt(at) <= rules.WaterLost)) score += 0.3f;
		if (m_Regions)
			if (const RegionSummary* g = m_Regions->Find(m_Regions->RegionAt(at))) score -= 0.5f * Config.Explore.Expeditions.RiskWeight * g->Danger;
		return score;
	}

	bool Tribe::ChooseCampSite(const CampSiteRequest& request, CampSite& site) const
	{
		if (!m_Regions) return false;
		const TribeMember* walker = nullptr;
		for (const TribeMember& m : m_Members) if (!m.Dead && m.Body) { walker = &m; break; }
		if (!walker) return false;
		float best = -1e30f;
		bool any = false;
		for (int t = 0; t < m_Regions->TilesX() * m_Regions->TilesZ(); ++t)
		{
			const int region = m_Regions->TileRegion(t);
			const RegionSummary* g = m_Regions->Find(region);
			const auto lore = m_Explore.Atlas.Lore.find(region);
			if (!g || g->Water || lore == m_Explore.Atlas.Lore.end() || lore->second.SeenCells == 0) continue;   // only where the clan has been
			const glm::vec2 at = m_Regions->TileAnchor(t);
			const float d = glm::distance(at, request.From);
			if (d < request.HomeRadius) continue;   // a move, not a shuffle
			const float score = SiteScore(at) - d / 2000.0f;   // (nearer wins a tie)
			if (score <= best || !walker->Body->CanReach(at)) continue;
			best = score;
			site.At = at;
			site.Score = SiteScore(at);
			site.Region = region;
			char buf[96];
			std::snprintf(buf, sizeof buf, "%s, worth %.2f against %.2f at home", g->Name.c_str(), static_cast<double>(site.Score), static_cast<double>(request.HomeScore));
			site.Why = buf;
			any = true;
		}
		return any;
	}

	void Tribe::CheckHome(float step)
	{
		ExplorationState& x = m_Explore;
		const MigrationRules& rules = Config.Explore.Migration;
		bool alive = false;
		for (const TribeMember& m : m_Members) alive = alive || (!m.Dead && m.Body);
		if (!alive) return;
		if (x.Moving)
		{
			if (!x.Moves.empty() && m_Time - x.Moves.back().At > 2.0 * static_cast<double>(rules.Settle)) { x.Moving = false; x.Arrived.clear(); }   // stragglers: they find it in their own time
			return;
		}
		if (m_Time - x.CampSince < static_cast<double>(rules.Settle)) return;
		const float score = SiteScore(Config.Camp);
		x.Peak = std::max(x.Peak, score);
		bool food = false, well = false;
		for (const auto& [name, e] : m_Clan.All())
		{
			if (glm::distance(e.Pos, Config.Camp) > rules.HomeRadius) continue;
			if (e.Kind == "well") well = true;
			const auto def = m_Defs.find(e.Kind);
			if (def != m_Defs.end() && IsFood(def->second.Item) && (e.Amount != 0 || def->second.RegenSeconds > 0.0f)) food = true;
		}
		const bool dry = !well && m_Regions->WaterDistanceAt(Config.Camp) > rules.WaterLost;
		const bool poor = x.Peak > 0.0f && score < rules.Poor * x.Peak;
		std::string why;
		if (!food) why = "nothing to eat is known near the camp";
		else if (dry) why = "there is no water near the camp";
		else if (poor) why = "the land near the camp gives little now";
		if (why.empty()) { x.Poor = std::max(0.0f, x.Poor - rules.Recover * step); return; }
		if (x.Poor == 0.0f || x.PoorWhy != why) TellClan("worries: " + why);
		x.Poor += step;
		x.PoorWhy = why;
		if (x.Poor < rules.After) return;
		if (!x.Moves.empty() && m_Time - x.Moves.back().At < static_cast<double>(rules.Cooldown)) return;
		CampSiteRequest request;
		request.From = Config.Camp;
		request.HomeRadius = rules.HomeRadius;
		request.HomeScore = score;
		for (const auto& [kind, def] : m_Defs)
			if (std::find(request.Wanted.begin(), request.Wanted.end(), def.Item) == request.Wanted.end()) request.Wanted.push_back(def.Item);
		CampSite site;
		const bool ok = m_SiteChooser ? m_SiteChooser(request, site) : ChooseCampSite(request, site);
		if (ok && site.Score >= score + rules.MinGain) MoveCamp(site.At, why + "; " + site.Why);
		else
		{
			TellClan("would leave (" + why + ") but knows no better place yet");
			x.Poor = rules.After * 0.5f;   // looks again in a while (the members keep exploring)
		}
	}

	bool Tribe::MoveCamp(const glm::vec2& to, const std::string& why)
	{
		bool alive = false;
		for (const TribeMember& m : m_Members) alive = alive || (!m.Dead && m.Body);
		if (!alive) return false;
		ExplorationState& x = m_Explore;
		MigrationRecord r;
		r.From = Config.Camp;
		r.To = to;
		r.At = m_Time;
		r.Why = why;
		if (m_Regions) if (const RegionSummary* g = m_Regions->Find(m_Regions->RegionAt(to))) r.Region = g->Name;
		Config.Camp = to;
		if (m_Height) Config.CampY = m_Height(to.x, to.y);
		if (HasFire()) BuildFire(FireFuel());   // the campfire is built again at the new camp with the fuel it had (the store is the camp's: it goes too)
		for (TribeMember& m : m_Members) { m.HasSpot = false; m.SpotTries = 0; }
		if (x.Current.Active())
		{
			for (const std::string& who : x.Current.Party) m_Board.Release(who);
			TellClan("calls the expedition to " + x.Current.RegionName + " home: it is moving");
			x.Current = Expedition();
		}
		x.Moves.push_back(r);
		if (x.Moves.size() > 16) x.Moves.erase(x.Moves.begin());
		x.Moving = true;
		x.Arrived.clear();
		x.CampSince = m_Time;
		x.Peak = 0.0f;
		x.Poor = 0.0f;
		x.PoorWhy.clear();
		TellClan("moves its camp from " + Place(r.From) + " to " + Place(to) + (r.Region.empty() ? "" : " in " + r.Region) + ": " + why);
		return true;
	}

	// ------------------------------------------------------------------ for people
	std::string Tribe::ExploreReport() const
	{
		if (!m_Regions) return "no regions: the clan explores only around its camp (generate a world first)\n";
		const ExplorationState& x = m_Explore;
		char buf[256];
		std::string s;
		const int home = m_Regions->RegionAt(Config.Camp);
		std::snprintf(buf, sizeof buf, "camp %s in %s; home worth %.2f (best %.2f)%s\n", Place(Config.Camp).c_str(), m_Regions->Find(home) ? m_Regions->Find(home)->Name.c_str() : "?",
			static_cast<double>(SiteScore(Config.Camp)), static_cast<double>(x.Peak), x.Moving ? "; the clan is moving" : "");
		s += buf;
		if (x.Poor > 0.0f) { std::snprintf(buf, sizeof buf, "  poor for %.0f s of %.0f: %s\n", static_cast<double>(x.Poor), static_cast<double>(Config.Explore.Migration.After), x.PoorWhy.c_str()); s += buf; }
		const Expedition& e = x.Current;
		if (e.Active())
		{
			s += "expedition " + std::to_string(e.Id) + " (" + e.Stage + "): " + e.Item + " in " + e.RegionName + " - " + e.Why + "\n";
			for (const std::string& who : e.Party)
			{
				const auto leg = e.Leg.find(who);
				const int l = leg != e.Leg.end() ? leg->second : 0;
				const int n = static_cast<int>(e.Points.size());
				s += "  " + who + ": " + (l < n ? "place " + std::to_string(l + 1) + " of " + std::to_string(n) : l == n ? std::string("coming home") : std::string("back")) + "\n";
			}
		}
		for (const auto& [item, seconds] : x.Unmet) { std::snprintf(buf, sizeof buf, "  nobody knows where %s grows (%.0f s)\n", item.c_str(), static_cast<double>(seconds)); s += buf; }
		for (const ExpeditionRecord& r : x.History)
		{
			std::snprintf(buf, sizeof buf, "  expedition %d to %s for %s: %s (%.0f..%.0f s)\n", r.Id, r.RegionName.c_str(), r.Item.c_str(), r.Found ? "found" : "nothing", r.Started, r.Ended);
			s += buf;
		}
		for (const MigrationRecord& r : x.Moves) s += "  moved from " + Place(r.From) + " to " + Place(r.To) + " (" + r.Region + "): " + r.Why + "\n";
		s += "what the clan knows:\n";
		for (const auto& [region, lore] : x.Atlas.Lore) s += RegionLoreReport(region);
		return s;
	}

	std::string Tribe::RegionLoreReport(int region) const
	{
		if (!m_Regions || !m_Regions->Find(region)) return {};
		const auto lore = m_Explore.Atlas.Lore.find(region);
		char buf[200];
		if (lore == m_Explore.Atlas.Lore.end())
		{
			std::snprintf(buf, sizeof buf, "  %2d %s: unknown to the clan\n", region, m_Regions->Find(region)->Name.c_str());
			return buf;
		}
		const RegionLore& l = lore->second;
		std::snprintf(buf, sizeof buf, "  %2d %s: %.0f%% explored, last seen at %.0f s, %d expeditions;", region, m_Regions->Find(region)->Name.c_str(),
			static_cast<double>(l.Explored * 100.0f), l.LastVisit, l.Expeditions);
		std::string s = buf;
		if (l.Resources.empty()) s += " no nodes known";
		for (const auto& [kind, n] : l.Resources) s += " " + kind + " " + std::to_string(n);
		return s + "\n";
	}
}
