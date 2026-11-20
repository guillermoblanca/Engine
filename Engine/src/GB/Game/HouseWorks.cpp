// T367 (TALLERES.md 10.2 point 4, 10.4, 10.5): the sizes of a house, the closure of its yard and the works that grow or close it in its place, with
// the hooks the tribe calls (one line each) and what the clans of the AI do with them. See HouseModules.h.
#include "GB/Game/HouseModules.h"
#include "GB/Game/GroundFootprint.h"
#include "GB/Game/TechModifiers.h"
#include "GB/Game/Tribe.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		constexpr float kBout = 3.0f;   // seconds of work a member puts into a work on a house each time it goes there

		const YardRules& Rules() { return DefaultYardRules(); }

		std::string Stamp(double t)
		{
			char b[48];
			std::snprintf(b, sizeof b, "%6.1f ", t);
			return b;
		}

		std::string Fixed(float v, const char* format = "%.1f")
		{
			char b[48];
			std::snprintf(b, sizeof b, format, static_cast<double>(v));
			return b;
		}

		std::string ItemsText(const std::map<std::string, int>& items)
		{
			std::string s;
			for (const auto& [item, n] : items) s += (s.empty() ? "" : ", ") + std::to_string(n) + " " + item;
			return s.empty() ? std::string("nothing") : s;
		}

		float YawOf(const EntityWorld& w, const std::string& name)
		{
			const Entity* e = w.Find(w.FindByName(name));
			return e ? e->Transform.Rotation.y : 0.0f;
		}

		// The def of a size that is its host's own with other values. Made once and kept while the program runs (a list of placements handed out
		// may still point at an older one when the data is read again), so Placed::Def never dangles.
		const BuildingDef* Derived(const BuildingDef& base, const YardSize& z)
		{
			struct Kept { BuildingDef Base; const BuildingDef* Def = nullptr; };
			static std::map<std::string, Kept> cache;
			static std::vector<std::unique_ptr<BuildingDef>> made;
			const std::string key = base.Id + "/" + z.Id;
			if (const auto it = cache.find(key); it != cache.end() && it->second.Base == base) return it->second.Def;
			auto d = std::make_unique<BuildingDef>(base);
			if (z.Footprint.x > 0.0f && z.Footprint.y > 0.0f) d->Footprint = z.Footprint;
			for (BuildingPhase& p : d->Phases)
			{
				for (auto& [item, n] : p.Needs) n = std::max(1, static_cast<int>(std::ceil(static_cast<float>(n) * z.CostScale - 1e-4f)));
				p.Work *= z.WorkScale;
			}
			if (z.Shelter >= 0) d->Shelter = z.Shelter;
			if (z.Rest >= 0.0f) d->Rest = z.Rest;
			if (z.Warmth >= 0.0f) d->Warmth = z.Warmth;
			if (z.StoreCap >= 0) d->StoreCap = z.StoreCap;
			if (z.FoodCap >= 0) d->FoodCap = z.FoodCap;
			if (z.MaxModules > 0 && d->Yard.Any()) d->Yard.MaxModules = z.MaxModules;
			made.push_back(std::move(d));
			cache[key] = { base, made.back().get() };
			return made.back().get();
		}

		// The line of sizes a placed building is of and which of them it is (Host null: none). `size` is the one saved for it ("" = its own).
		struct LineAt
		{
			const std::string* Host = nullptr;
			const std::vector<YardSize>* Sizes = nullptr;
			size_t Index = 0;
		};
		LineAt LineOf(const std::string& id, const std::string& size)
		{
			const auto& all = Rules().Sizes;
			if (const auto it = all.find(id); it != all.end())
			{
				LineAt at{ &it->first, &it->second, 0 };
				for (size_t i = 0; i < it->second.size(); ++i)
					if (it->second[i].Id == size && it->second[i].Building.empty()) at.Index = i;
				return at;
			}
			for (const auto& [host, sizes] : all)
				for (size_t i = 0; i < sizes.size(); ++i)
					if (sizes[i].Building == id) return { &host, &sizes, i };
			return {};
		}

		// The def a size of a line stands for (null when the data lacks it): another building, the host itself (its first size) or a derived one
		const BuildingDef* SizeDef(const std::map<std::string, BuildingDef>& defs, const LineAt& line, size_t index)
		{
			const YardSize& z = (*line.Sizes)[index];
			if (!z.Building.empty()) { const auto it = defs.find(z.Building); return it == defs.end() ? nullptr : &it->second; }
			const auto it = defs.find(*line.Host);
			if (it == defs.end()) return nullptr;
			return index == 0 ? &it->second : Derived(it->second, z);
		}

		// The rectangle of a yard (the fence's bounding box, turned as the house)
		GroundRect YardGround(const BuildingDef& def, const glm::vec2& pos, float yaw, const std::string& what)
		{
			const BuildingYard& y = def.Yard;
			const float fx = 0.5f * def.Footprint.x, fz = 0.5f * def.Footprint.y;
			const float hx = fx + y.Eave + y.Margin, zb = -(fz + y.Eave + y.MarginBack), zf = fz + y.Eave + y.MarginFront;
			GroundRect r;
			r.Centre = pos + YardTurned({ 0.0f, 0.5f * (zb + zf) }, yaw);
			r.Half = { hx, 0.5f * (zf - zb) };
			r.Yaw = yaw;
			r.What = what;
			return r;
		}

		// Where a house of `to` stands when it grows from `from` at `pos`: the line of its door stays, it grows to the sides and to the back
		glm::vec2 GrownAt(const BuildingDef& from, const BuildingDef& to, const glm::vec2& pos, float yaw)
		{
			return pos + YardTurned({ 0.0f, -0.5f * (to.Footprint.y - from.Footprint.y) }, yaw);
		}

		std::map<std::string, int> Times(const std::map<std::string, int>& needs, float metres, float per)
		{
			std::map<std::string, int> out;
			for (const auto& [item, n] : needs)
				if (const int k = static_cast<int>(std::ceil(static_cast<float>(n) * metres / std::max(per, 0.1f) - 1e-4f)); k > 0) out[item] = k;
			return out;
		}

		// The substitutes of `items` in the phases of `def`
		void SubstitutesOf(const BuildingDef& def, const std::map<std::string, int>& items, std::map<std::string, std::vector<std::string>>& out)
		{
			for (const BuildingPhase& p : def.Phases)
				for (const auto& [item, list] : p.Substitutes)
					if (items.count(item))
						for (const std::string& s : list)
							if (std::find(out[item].begin(), out[item].end(), s) == out[item].end()) out[item].push_back(s);
		}

		int WithSubstitutes(const Inventory& inv, const HouseModules::Work& w, const std::string& item)
		{
			int n = inv.Count(item);
			if (const auto it = w.Substitutes.find(item); it != w.Substitutes.end()) for (const std::string& s : it->second) n += inv.Count(s);
			return n;
		}

		void Factor(UtilityOption& o, const char* name, float value) { o.Factors.push_back({ name, value }); }

		Json ItemsJson(const std::map<std::string, int>& m)
		{
			Json j = Json::object();
			for (const auto& [k, v] : m) j[k] = v;
			return j;
		}

		bool ReadItems(const Json& j, std::map<std::string, int>& out)
		{
			if (!j.is_object()) return false;
			for (const auto& [k, v] : j.items())
			{
				if (!v.is_number_integer() || v.get<int>() < 0) return false;
				out[k] = v.get<int>();
			}
			return true;
		}
	}

	// ------------------------------------------------------------------ the work
	std::map<std::string, int> HouseModules::Work::Missing() const
	{
		std::map<std::string, int> out;
		for (const auto& [item, n] : Needs)
		{
			const auto got = Got.find(item);
			if (const int left = n - (got == Got.end() ? 0 : got->second); left > 0) out[item] = left;
		}
		return out;
	}

	float HouseModules::Work::Progress() const
	{
		int need = 0, got = 0;
		for (const auto& [item, n] : Needs) { need += n; const auto g = Got.find(item); got += std::min(n, g == Got.end() ? 0 : g->second); }
		const float brought = need > 0 ? static_cast<float>(got) / static_cast<float>(need) : 1.0f;
		const float worked = Needed > 0.0f ? std::clamp(Done / Needed, 0.0f, 1.0f) : 1.0f;
		return 0.5f * brought + 0.5f * worked;
	}

	// ------------------------------------------------------------------ reading
	int HouseModules::Max(const Tribe& tribe, const std::string& host) const
	{
		for (const Tribe::Placed& p : tribe.Placements())
			if (p.Name == host)
			{
				if (!p.Def->Yard.Any()) return 0;
				const auto h = m_Houses.find(host);
				const LineAt line = LineOf(p.Def->Id, h == m_Houses.end() ? std::string() : h->second.Size);
				return line.Host && (*line.Sizes)[line.Index].MaxModules > 0 ? (*line.Sizes)[line.Index].MaxModules : p.Def->Yard.MaxModules;   // (T367: the size's)
			}
		return 0;
	}

	std::string HouseModules::SizeOf(const Tribe& tribe, const std::string& house) const
	{
		for (const Tribe::Placed& p : tribe.Placements())
			if (p.Name == house)
			{
				const auto h = m_Houses.find(house);
				const LineAt line = LineOf(p.Def->Id, h == m_Houses.end() ? std::string() : h->second.Size);
				return line.Host ? (*line.Sizes)[line.Index].Id : std::string("S");
			}
		return "S";
	}

	int HouseModules::ClosureOf(const std::string& house) const
	{
		const auto h = m_Houses.find(house);
		return h == m_Houses.end() ? 0 : h->second.Closure;
	}

	const HouseModules::Work* HouseModules::WorkOn(const std::string& house) const
	{
		const auto h = m_Houses.find(house);
		return h != m_Houses.end() && h->second.Doing.Any() ? &h->second.Doing : nullptr;
	}

	bool HouseModules::Reserved(const std::string& house) const
	{
		if (Count(house) > 0) return true;
		const auto h = m_Houses.find(house);
		return h != m_Houses.end() && (h->second.Closure > 0 || h->second.Doing.Kind == "closure");
	}

	const BuildingDef* HouseModules::Variant(const std::string& name, const BuildingDef* def) const
	{
		if (!def || m_Houses.empty()) return def;
		const auto h = m_Houses.find(name);
		if (h == m_Houses.end() || h->second.Size.empty()) return def;
		const auto line = Rules().Sizes.find(def->Id);
		if (line == Rules().Sizes.end()) return def;
		for (size_t i = 1; i < line->second.size(); ++i)
			if (line->second[i].Id == h->second.Size && line->second[i].Building.empty()) return Derived(*def, line->second[i]);
		return def;
	}

	HouseModules::Info HouseModules::InfoOf(const Tribe& tribe, const std::string& house) const
	{
		Info info;
		const Tribe::PlacedList placed = tribe.Placements();
		const Tribe::Placed* p = nullptr;
		for (const Tribe::Placed& q : placed) if (q.Name == house) p = &q;
		if (!p || !tribe.m_Buildings || !tribe.m_Entities) return info;
		info.Valid = p->State.Done;
		const auto h = m_Houses.find(house);
		const LineAt line = LineOf(p->Def->Id, h == m_Houses.end() ? std::string() : h->second.Size);
		const float yaw = YawOf(*tribe.m_Entities, house);
		info.Beds = p->Def->Shelter;
		info.HasYard = p->Def->Yard.Any();
		info.MaxModules = p->Def->Yard.MaxModules;
		info.Modules = Count(house);
		info.Reserved = Reserved(house);
		info.Closure = ClosureOf(house);
		if (const Work* w = WorkOn(house)) { info.Current = *w; info.Progress = w->Progress(); }
		const YardShape yard = info.HasYard ? ComputeYard(*p->Def, p->Pos, yaw, Rules()) : YardShape{};
		info.FenceLength = yard.Length;
		const bool busy = info.Current.Any();
		if (line.Host)
		{
			info.Line = *line.Host;
			info.Size = (*line.Sizes)[line.Index].Id;
			if ((*line.Sizes)[line.Index].MaxModules > 0 && info.HasYard) info.MaxModules = (*line.Sizes)[line.Index].MaxModules;
			if (line.Index + 1 < line.Sizes->size())
			{
				const YardSize& z = (*line.Sizes)[line.Index + 1];
				info.NextSize = z.Id;
				info.NextSizeKnown = (z.Building.empty() || tribe.CanBuild(z.Building)) && (z.Tech.empty() || tribe.ClanKnowsTech(z.Tech));
				if (!info.NextSizeKnown)
				{
					info.NextSizeTech = z.Tech;
					if (info.NextSizeTech.empty() && tribe.m_Knowledge.Techs())
						for (const auto& [id, t] : tribe.m_Knowledge.Techs()->Techs)
							if (std::find(t.Buildings.begin(), t.Buildings.end(), z.Building) != t.Buildings.end()) { info.NextSizeTech = id; break; }
					if (info.NextSizeTech.empty() && tribe.m_Buildings->count(z.Building)) info.NextSizeTech = tribe.m_Buildings->at(z.Building).Tech;
				}
				const BuildingDef* to = SizeDef(*tribe.m_Buildings, line, line.Index + 1);
				if (to)
				{
					info.NextBeds = to->Shelter;
					info.NextMaxModules = z.MaxModules > 0 ? z.MaxModules : to->Yard.MaxModules;
					const std::map<std::string, int> have = p->Def->TotalNeeds();
					for (const auto& [item, n] : to->TotalNeeds())
					{
						const auto it = have.find(item);
						if (const int more = n - (it == have.end() ? 0 : it->second); more > 0) info.NextSizeCost[item] = more;
					}
					info.NextSizeWork = std::max(to->TotalWork() - p->Def->TotalWork(), Rules().GrowMinWork * to->TotalWork());
					const glm::vec2 at = GrownAt(*p->Def, *to, p->Pos, yaw);
					if (const YardClosure* c = Rules().Closure(info.Closure); c && info.HasYard && to->Yard.Any())
						if (const float more = ComputeYard(*to, at, yaw, Rules()).Length - yard.Length; more > 0.0f)
						{
							for (const auto& [item, n] : Times(c->Needs, more, c->CostPer)) info.NextSizeCost[item] += n;
							info.NextSizeWork += c->Work * more / std::max(c->CostPer, 0.1f);
						}
					if (!info.Valid) info.GrowBlocked = house + " is not standing yet";
					else if (busy) info.GrowBlocked = house + " is being worked on already";
					else if (!info.NextSizeKnown) info.GrowBlocked = "the clan does not know how yet" + (info.NextSizeTech.empty() ? std::string() : ": it needs " + info.NextSizeTech);
					else if (const std::string room = GrowRoom(tribe, house, *to, at, yaw, info.Reserved); !room.empty()) info.GrowBlocked = "no room: " + room;
				}
				else info.GrowBlocked = "the data has no building '" + z.Building + "' for the size " + z.Id;
			}
		}
		if (info.HasYard)
		{
			if (const YardClosure* c = Rules().Closure(info.Closure)) info.ClosureId = c->Id;
			for (const YardClosure& c : Rules().Closures)
				if (c.Level > info.Closure)
				{
					info.NextClosure = c.Id;
					info.NextClosureCost = Times(c.Needs, yard.Length, c.CostPer);
					info.NextClosureWork = c.Work * yard.Length / std::max(c.CostPer, 0.1f);
					info.NextClosureKnown = c.Tech.empty() || tribe.ClanKnowsTech(c.Tech);
					if (!info.NextClosureKnown) info.NextClosureTech = c.Tech;
					if (!info.Valid) info.ClosureBlocked = house + " is not standing yet";
					else if (busy) info.ClosureBlocked = house + " is being worked on already";
					else if (!info.NextClosureKnown) info.ClosureBlocked = "the clan does not know how yet: it needs " + c.Tech;
					else if (!info.Reserved)
						if (const std::string room = GrowRoom(tribe, house, *p->Def, p->Pos, yaw, true); !room.empty()) info.ClosureBlocked = "no room for its yard: " + room;
					break;
				}
		}
		return info;
	}

	std::string HouseModules::GrowRoom(const Tribe& tribe, const std::string& house, const BuildingDef& to, const glm::vec2& at, float yaw, bool yard) const
	{
		if (!tribe.m_Entities) return {};
		std::vector<GroundRect> taken;
		for (const Tribe::Placed& p : tribe.Placements())
		{
			if (p.Name == house) continue;
			if (const Entry* e = Of(p.Name); e && e->Host == house) continue;   // (its own modules move with it)
			GroundRect r = BuildingGround(*p.Def, p.Pos, YawOf(*tribe.m_Entities, p.Name));
			r.What = p.Name;
			taken.push_back(r);
		}
		{
			const std::string keep = m_Placing;
			m_Placing = house;   // (the other houses' yards and growths, not its own)
			Ground(tribe, taken);
			m_Placing = keep;
		}
		for (const Field& f : tribe.Economy().Fields) FieldGrounds(f, taken);
		if (tribe.Outside().Ground) tribe.Outside().Ground(tribe, taken);
		const GroundRect want = yard && to.Yard.Any() ? YardGround(to, at, yaw, house) : BuildingGround(to, at, yaw);
		if (std::string on = GroundFree(want, taken); !on.empty()) return on;
		// a node or water under its new walls (a yard may hold a tree or a bush; what was under its old walls already is not in the way): only
		// when it grows, a closure leaves its walls as they are
		const Tribe::Placed* now = nullptr;
		const Tribe::PlacedList placed = tribe.Placements();
		for (const Tribe::Placed& p : placed) if (p.Name == house) now = &p;
		if (now && now->Def == &to && glm::distance(now->Pos, at) < 1e-3f) return {};
		const GroundRect walls = BuildingGround(to, at, yaw);
		const GroundRect old = now ? BuildingGround(*now->Def, now->Pos, yaw) : GroundRect{};
		const float span = glm::length(walls.Half) + 1.0f;
		for (const Tribe::Sight& s : tribe.m_Sights)
		{
			if (std::abs(s.Pos.x - walls.Centre.x) > span || std::abs(s.Pos.y - walls.Centre.y) > span) continue;
			GroundRect node;
			node.Centre = s.Pos;
			node.Half = glm::vec2(0.3f);
			if (GroundOverlaps(walls, node) && !(now && GroundOverlaps(old, node))) return "it would stand on " + s.Name;
		}
		for (const TribeMember& m : tribe.m_Members)
			if (!m.Dead && m.Body)
			{
				const ITribeBody* body = m.Body;
				if (const std::string wet = SiteInWater(to, at, [body](const glm::vec2& q) { return body->WaterDepthAt(q); }, yaw); !wet.empty()) return wet;
				break;
			}
		return {};
	}

	// ------------------------------------------------------------------ the commands
	HouseModules::Reply HouseModules::Grow(Tribe& tribe, const std::string& house, const std::string& size, bool now)
	{
		if (!tribe.m_Buildings || !tribe.m_Entities || !tribe.m_Gameplay) return { false, "the clan has no buildings" };
		const Tribe::PlacedList placed = tribe.Placements();
		const Tribe::Placed* p = nullptr;
		for (const Tribe::Placed& q : placed) if (q.Name == house) p = &q;
		if (!p) return { false, "no building of the clan called '" + house + "'" };
		if (!p->State.Done) return { false, house + " is not standing yet" };
		const auto h = m_Houses.find(house);
		const LineAt line = LineOf(p->Def->Id, h == m_Houses.end() ? std::string() : h->second.Size);
		if (!line.Host) return { false, "a " + p->Def->Name + " has no sizes: it does not grow" };
		size_t to = line.Sizes->size();
		std::string ids;
		for (size_t i = 0; i < line.Sizes->size(); ++i) { ids += (ids.empty() ? "" : ", ") + (*line.Sizes)[i].Id; if ((*line.Sizes)[i].Id == size) to = i; }
		if (to == line.Sizes->size()) return { false, "no size '" + size + "' (" + ids + ")" };
		if (to <= line.Index) return { false, house + " is size " + (*line.Sizes)[line.Index].Id + " already (a house does not shrink)" };
		if (const Work* w = WorkOn(house)) return { false, house + " is being worked on already (" + w->Kind + " to " + w->To + ")" };
		const YardSize& z = (*line.Sizes)[to];
		const bool known = (z.Building.empty() || tribe.CanBuild(z.Building)) && (z.Tech.empty() || tribe.ClanKnowsTech(z.Tech));
		if (!known)
		{
			const Info info = to == line.Index + 1 ? InfoOf(tribe, house) : Info{};
			return { false, "the clan does not know how to grow a " + p->Def->Name + " to " + z.Id + " yet" + (info.NextSizeTech.empty() ? std::string() : ": it needs " + info.NextSizeTech) };
		}
		const BuildingDef* def = SizeDef(*tribe.m_Buildings, line, to);
		if (!def) return { false, "the data has no building '" + z.Building + "' for the size " + z.Id };
		const float yaw = YawOf(*tribe.m_Entities, house);
		const glm::vec2 at = GrownAt(*p->Def, *def, p->Pos, yaw);
		if (const std::string room = GrowRoom(tribe, house, *def, at, yaw, Reserved(house)); !room.empty()) return { false, house + " cannot grow to " + z.Id + ": no room, " + room };
		Work w;
		w.Kind = "grow";
		w.To = z.Id;
		const std::map<std::string, int> have = p->Def->TotalNeeds();
		for (const auto& [item, n] : def->TotalNeeds())
		{
			const auto it = have.find(item);
			if (const int more = n - (it == have.end() ? 0 : it->second); more > 0) w.Needs[item] = more;
		}
		SubstitutesOf(*def, w.Needs, w.Substitutes);
		w.Needed = std::max(def->TotalWork() - p->Def->TotalWork(), Rules().GrowMinWork * def->TotalWork());
		// the closure's metres more, at its level's price, in the same work (TALLERES 10.4.3)
		if (const YardClosure* c = Rules().Closure(ClosureOf(house)); c && p->Def->Yard.Any() && def->Yard.Any())
			if (const float more = ComputeYard(*def, at, yaw, Rules()).Length - ComputeYard(*p->Def, p->Pos, yaw, Rules()).Length; more > 0.0f)
			{
				const std::map<std::string, int> fence = Times(c->Needs, more, c->CostPer);
				for (const auto& [item, n] : fence) w.Needs[item] += n;
				for (const auto& [item, list] : c->Substitutes)
					if (fence.count(item))
						for (const std::string& s : list)
							if (std::find(w.Substitutes[item].begin(), w.Substitutes[item].end(), s) == w.Substitutes[item].end()) w.Substitutes[item].push_back(s);
				w.Needed += c->Work * more / std::max(c->CostPer, 0.1f);
			}
		const std::string cost = ItemsText(w.Needs), work = Fixed(w.Needed, "%.0f");
		m_Houses[house].Doing = w;
		if (!now && !m_Planning) tribe.m_Designations.QueueSite(tribe, house);   // (the player's: in the queue of its sites, as a site)
		tribe.m_Log.push_back(Stamp(tribe.m_Time) + "the clan will grow " + house + " to " + z.Id + " (" + std::to_string(def->Shelter) + " beds): " + cost + ", " + work + " s of work");
		if (now)
		{
			Complete(tribe, house);
			return { true, house + " grows to " + z.Id + " at once (" + std::to_string(def->Shelter) + " beds)" };
		}
		return { true, house + " will grow to " + z.Id + " (" + std::to_string(def->Shelter) + " beds): the clan will bring " + cost + " and work it " + work + " s; it stays in use meanwhile" };
	}

	HouseModules::Reply HouseModules::SetClosure(Tribe& tribe, const std::string& house, const std::string& closure, bool now)
	{
		const YardClosure* c = Rules().ClosureById(closure);
		if (!c)
		{
			std::string ids;
			for (const YardClosure& k : Rules().Closures) ids += (ids.empty() ? "" : ", ") + k.Id;
			return { false, "no closure '" + closure + "' (" + ids + ")" };
		}
		const Info info = InfoOf(tribe, house);
		if (!info.Valid) return { false, "no standing building of the clan called '" + house + "'" };
		if (!info.HasYard) return { false, house + " has no yard" };
		if (c->Level <= info.Closure) return { false, house + " has a " + info.ClosureId + " already" + (c->Level < info.Closure ? std::string(" (a closure is not taken down to a lower one)") : std::string()) };
		if (info.Current.Any()) return { false, house + " is being worked on already (" + info.Current.Kind + " to " + info.Current.To + ")" };
		if (!c->Tech.empty() && !tribe.ClanKnowsTech(c->Tech)) return { false, "the clan does not know how to make a " + c->Id + " yet: it needs " + c->Tech };
		if (!info.ClosureBlocked.empty() && info.NextClosure == c->Id && info.ClosureBlocked.rfind("no room", 0) == 0) return { false, house + " cannot close its yard: " + info.ClosureBlocked };
		if (!info.Reserved && info.NextClosure != c->Id)   // (a level further up than the next: its yard checked the same)
			for (const Tribe::Placed& p : tribe.Placements())
				if (p.Name == house)
				{
					if (const std::string room = GrowRoom(tribe, house, *p.Def, p.Pos, tribe.m_Entities ? YawOf(*tribe.m_Entities, house) : 0.0f, true); !room.empty())
						return { false, house + " cannot close its yard: no room for its yard, " + room };
					break;
				}
		Work w;
		w.Kind = "closure";
		w.To = c->Id;
		w.Needs = Times(c->Needs, info.FenceLength, c->CostPer);
		for (const auto& [item, list] : c->Substitutes) if (w.Needs.count(item)) w.Substitutes[item] = list;
		w.Needed = c->Work * info.FenceLength / std::max(c->CostPer, 0.1f);
		const std::string cost = ItemsText(w.Needs), work = Fixed(w.Needed, "%.0f");
		m_Houses[house].Doing = w;
		m_Next = 0.0;
		if (!now && !m_Planning) tribe.m_Designations.QueueSite(tribe, house);   // (the player's: in the queue of its sites, as a site)
		tribe.m_Log.push_back(Stamp(tribe.m_Time) + "the clan will close the yard of " + house + " with a " + c->Id + " (" + Fixed(info.FenceLength) + " m): " + cost + ", " + work + " s of work");
		if (now)
		{
			Complete(tribe, house);
			return { true, house + ": its yard closed with a " + c->Id + " at once (" + Fixed(info.FenceLength) + " m)" };
		}
		return { true, house + ": a " + c->Id + " round its yard (" + Fixed(info.FenceLength) + " m): the clan will bring " + cost + " and work it " + work + " s" };
	}

	HouseModules::Reply HouseModules::CancelWork(Tribe& tribe, const std::string& house)
	{
		const auto h = m_Houses.find(house);
		if (h == m_Houses.end() || !h->second.Doing.Any()) return { false, "no work in course on " + house };
		std::map<std::string, int> back;
		for (const auto& [item, n] : h->second.Doing.Got)
			if (n > 0 && tribe.m_Items && tribe.m_Items->Find(item)) if (const int in = tribe.m_Store.Add(*tribe.m_Items, item, n); in > 0) back[item] = in;
		const std::string what = h->second.Doing.Kind + " to " + h->second.Doing.To;
		h->second.Doing = {};
		tribe.m_Designations.SiteGone(house);
		if (h->second.Empty()) m_Houses.erase(h);
		m_Next = 0.0;
		tribe.m_Log.push_back(Stamp(tribe.m_Time) + "the clan calls off the " + what + " of " + house + " (back to the store: " + ItemsText(back) + ")");
		return { true, "the " + what + " of " + house + " called off; back to the store: " + ItemsText(back) };
	}

	std::string HouseModules::YardStatus(const Tribe& tribe, const std::string& house) const
	{
		const Info i = InfoOf(tribe, house);
		if (!i.Valid) return "no standing building of the clan called '" + house + "'";
		std::string s = house + ": size " + i.Size + " (" + std::to_string(i.Beds) + " beds";
		if (!i.NextSize.empty())
			s += "; next " + i.NextSize + ": " + std::to_string(i.NextBeds) + " beds, " + ItemsText(i.NextSizeCost) + ", " + Fixed(i.NextSizeWork, "%.0f") + " s" + (i.GrowBlocked.empty() ? std::string() : " (" + i.GrowBlocked + ")");
		s += ")";
		if (i.HasYard)
		{
			s += "; closure " + (i.ClosureId.empty() ? std::string("none") : i.ClosureId) + " " + Fixed(i.FenceLength) + " m";
			if (!i.NextClosure.empty())
				s += " (next " + i.NextClosure + ": " + ItemsText(i.NextClosureCost) + ", " + Fixed(i.NextClosureWork, "%.0f") + " s" + (i.ClosureBlocked.empty() ? std::string() : "; " + i.ClosureBlocked) + ")";
			s += "; modules " + std::to_string(i.Modules) + "/" + std::to_string(i.MaxModules) + (i.Reserved ? "; yard reserved" : "; yard free");
		}
		if (i.Current.Any())
		{
			s += "; " + i.Current.Kind + " to " + i.Current.To + " " + Fixed(100.0f * i.Progress, "%.0f") + "%";
			if (const std::map<std::string, int> lack = i.Current.Missing(); !lack.empty()) s += " (lacks " + ItemsText(lack) + ")";
			else s += " (" + Fixed(i.Current.Done, "%.0f") + "/" + Fixed(i.Current.Needed, "%.0f") + " s of work)";
		}
		return s;
	}

	void HouseModules::Complete(Tribe& tribe, const std::string& house)
	{
		const auto h = m_Houses.find(house);
		if (h == m_Houses.end() || !h->second.Doing.Any()) return;
		const Work w = h->second.Doing;
		h->second.Doing = {};
		tribe.m_Designations.SiteGone(house);
		m_Next = 0.0;
		m_Fenced.erase(house);   // (made again at the next step: a new look, or a fence that moved with the house)
		if (w.Kind == "closure")
		{
			const YardClosure* c = Rules().ClosureById(w.To);
			if (c) h->second.Closure = c->Level;
			tribe.m_Log.push_back(Stamp(tribe.m_Time) + "the yard of " + house + " is closed with a " + w.To + (c && c->Defense > 0.0f ? " (defense +" + Fixed(c->Defense, "%.2f") + ")" : std::string()));
			return;
		}
		// grow: the house turns into its new size in its place
		if (!tribe.m_Buildings || !tribe.m_Entities || !tribe.m_Gameplay) return;
		const Tribe::PlacedList placed = tribe.Placements();
		const Tribe::Placed* p = nullptr;
		for (const Tribe::Placed& q : placed) if (q.Name == house) p = &q;
		if (!p) return;
		const LineAt line = LineOf(p->Def->Id, h->second.Size);
		if (!line.Host) return;
		size_t to = line.Sizes->size();
		for (size_t i = 0; i < line.Sizes->size(); ++i) if ((*line.Sizes)[i].Id == w.To) to = i;
		if (to == line.Sizes->size()) return;
		const BuildingDef* def = SizeDef(*tribe.m_Buildings, line, to);
		if (!def) return;
		const YardSize& z = (*line.Sizes)[to];
		const float yaw = YawOf(*tribe.m_Entities, house);
		const glm::vec2 at = GrownAt(*p->Def, *def, p->Pos, yaw);
		const BuildingDef& base = tribe.m_Buildings->at(p->Def->Id);
		if (!z.Building.empty() && z.Building != p->Def->Id)
		{
			if (AbilitySystemComponent* a = tribe.m_Gameplay->Find(house)) ConvertBuilding(*tribe.m_Entities, *a, house, base, *def);
			h->second.Size.clear();
		}
		else h->second.Size = to == 0 ? std::string() : z.Id;
		const BuildingDef& now = !z.Building.empty() ? *def : base;   // (the building it is now, at its own footprint)
		if (Entity* e = tribe.m_Entities->Find(tribe.m_Entities->FindByName(house)))
		{
			e->Transform.Position.x = at.x;
			e->Transform.Position.z = at.y;
			if (tribe.m_Ground) e->Transform.Position.y = GroundUnder(tribe.m_Ground, at, def->Radius());
			e->Transform.Scale = now.Footprint.x > 0.0f && now.Footprint.y > 0.0f ? glm::vec3(def->Footprint.x / now.Footprint.x, 1.0f, def->Footprint.y / now.Footprint.y) : glm::vec3(1.0f);
		}
		// its modules go with their slots, which keep their names (TALLERES 10.4.3)
		const YardShape yard = def->Yard.Any() ? ComputeYard(*def, at, yaw, Rules()) : YardShape{};
		for (const auto& [site, e] : m_Sites)
		{
			if (e.Host != house) continue;
			const auto slot = std::find_if(yard.Slots.begin(), yard.Slots.end(), [&](const YardSlot& s) { return s.Id == e.Slot; });
			Entity* m = tribe.m_Entities->Find(tribe.m_Entities->FindByName(site));
			if (slot == yard.Slots.end() || !m) { tribe.m_Log.push_back(Stamp(tribe.m_Time) + site + " keeps its place: the yard of " + house + " has no " + e.Slot + " now"); continue; }
			m->Transform.Position.x = slot->Pos.x;
			m->Transform.Position.z = slot->Pos.y;
			if (tribe.m_Ground) m->Transform.Position.y = tribe.m_Ground(slot->Pos.x, slot->Pos.y);
			m->Transform.Rotation.y = std::fabs(slot->Yaw) < 0.01f ? 360.0f : slot->Yaw;
		}
		if (h->second.Empty()) m_Houses.erase(h);
		tribe.m_PlacedAt = -1.0;
		tribe.ForgetPlacements();
		tribe.m_Log.push_back(Stamp(tribe.m_Time) + house + " grows to " + z.Id + ": " + std::to_string(def->Shelter) + " beds");
	}

	// ------------------------------------------------------------------ the tribe's hooks
	void HouseModules::Ground(const Tribe& tribe, std::vector<GroundRect>& out) const
	{
		if ((m_Sites.empty() && m_Houses.empty()) || !tribe.m_Entities || !tribe.m_Buildings) return;
		for (const Tribe::Placed& p : tribe.Placements())
		{
			if (!p.State.Done || p.Name == m_Placing || (Count(p.Name) == 0 && !m_Houses.count(p.Name))) continue;
			const float yaw = YawOf(*tribe.m_Entities, p.Name);
			if (p.Def->Yard.Any() && Reserved(p.Name)) out.push_back(YardGround(*p.Def, p.Pos, yaw, p.Name + "'s yard"));
			if (const auto base = tribe.m_Buildings->find(p.Def->Id); base != tribe.m_Buildings->end() && &base->second != p.Def)
			{
				GroundRect r = BuildingGround(*p.Def, p.Pos, yaw);   // (a size bigger than its building's footprint)
				r.What = p.Name;
				out.push_back(r);
			}
			if (const Work* w = WorkOn(p.Name); w && w->Kind == "grow")   // (the ground it grows into is taken from the start)
			{
				const auto h = m_Houses.find(p.Name);
				const LineAt line = LineOf(p.Def->Id, h->second.Size);
				if (!line.Host) continue;
				for (size_t i = 0; i < line.Sizes->size(); ++i)
					if ((*line.Sizes)[i].Id == w->To)
						if (const BuildingDef* to = SizeDef(*tribe.m_Buildings, line, i))
						{
							const glm::vec2 at = GrownAt(*p.Def, *to, p.Pos, yaw);
							out.push_back(to->Yard.Any() && Reserved(p.Name) ? YardGround(*to, at, yaw, p.Name + "'s yard") : BuildingGround(*to, at, yaw));
							out.back().What = p.Name;
						}
			}
		}
	}

	float HouseModules::ClosureDefense() const
	{
		if (m_Houses.empty()) return 0.0f;
		float d = 0.0f;
		for (const auto& [house, h] : m_Houses)
			if (const YardClosure* c = h.Closure > 0 ? Rules().Closure(h.Closure) : nullptr) d += c->Defense;
		return std::min(d, Rules().DefenseCap);
	}

	void HouseModules::WorkOptions(const Tribe& tribe, TribeMember& m, std::vector<UtilityOption>& opts) const
	{
		if (m_Houses.empty() || !tribe.m_Entities) return;
		if (std::none_of(m_Houses.begin(), m_Houses.end(), [](const auto& kv) { return kv.second.Doing.Any(); })) return;
		const glm::vec2 pos = tribe.Where(m);
		const float bw = std::max(0.6f, tribe.MemberWeight(m, "build"));
		const float day = tribe.AgendaFactor(m.Role, "gather", tribe.Hour(), tribe.CurrentSeason());
		const AbilitySystemComponent* asc = tribe.m_Gameplay ? tribe.m_Gameplay->Find(m.Name) : nullptr;
		const bool canBuild = asc && std::find(asc->Granted().begin(), asc->Granted().end(), "Build") != asc->Granted().end();
		for (const Tribe::Placed& p : tribe.Placements())
		{
			const auto h = m_Houses.find(p.Name);
			if (h == m_Houses.end() || !h->second.Doing.Any() || !p.State.Done) continue;
			const Work& w = h->second.Doing;
			const float low = w.Kind == "closure" ? 0.6f : 0.9f;   // (a closure is not pressing; a growth is beds for a household)
			const float yaw = YawOf(*tribe.m_Entities, p.Name);
			const glm::vec2 front = SiteFrontTurned(p.Pos, *p.Def, yaw) + YardTurned({ 0.7f * (static_cast<float>(tribe.IndexOf(m) % 3) - 1.0f), 0.0f }, yaw);
			const float reach = Falloff(glm::distance(pos, front), 120.0f);
			const std::string what = w.Kind == "grow" ? "its growth to " + w.To : "its " + w.To;
			int bring = 0;
			std::string lack;
			for (const auto& [item, n] : w.Missing())
			{
				int coming = 0;
				for (const TribeMember& o : tribe.m_Members) if (o.Job == "deliver" && o.Detail == p.Name && o.Body) coming += WithSubstitutes(o.Body->Bag(), w, item);
				if (const int k = std::min(n - coming, WithSubstitutes(tribe.m_Store, w, item)); k > 0) { bring += k; lack += (lack.empty() ? "" : ", ") + item; }
			}
			if (bring > 0)
			{
				UtilityOption o;
				o.Kind = "deliver";
				o.Target = p.Name;
				o.Where = front;
				o.Score = 0.85f * bw * reach * day * low;
				if (const float q = tribe.m_Designations.SiteFactor(tribe, p.Name, "deliver", &m); q != 1.0f) { o.Score *= q; Factor(o, "later in the queue", q); }
				Factor(o, "role", bw);
				Factor(o, "distance", reach);
				if (day != 1.0f) Factor(o, "agenda", day);
				Factor(o, "work on a house", low);
				o.Note = p.Name + " lacks " + lack + " for " + what + " and the store has it";
				opts.push_back(o);
			}
			int builders = 0;
			for (const TribeMember& o : tribe.m_Members) if (&o != &m && o.Job == "build" && o.Detail == p.Name) ++builders;
			if (w.Ready() && canBuild && builders < tribe.Config.Builders)
			{
				UtilityOption o;
				o.Kind = "build";
				o.Target = p.Name;
				o.Where = front;
				o.Score = 1.0f * bw * reach * day * low;
				if (const float q = tribe.m_Designations.SiteFactor(tribe, p.Name, "build", &m); q != 1.0f) { o.Score *= q; Factor(o, "later in the queue", q); }
				Factor(o, "role", bw);
				Factor(o, "distance", reach);
				if (day != 1.0f) Factor(o, "agenda", day);
				Factor(o, "work on a house", low);
				o.Note = p.Name + ": " + what + " has its materials (" + Fixed(w.Done, "%.0f") + "/" + Fixed(w.Needed, "%.0f") + " s of work)";
				opts.push_back(o);
			}
		}
	}

	int HouseModules::ExecuteWork(Tribe& tribe, TribeMember& m, const UtilityOption& o)
	{
		if ((o.Kind != "deliver" && o.Kind != "build") || m_Houses.empty()) return -1;
		const auto h = m_Houses.find(o.Target);
		if (h == m_Houses.end() || !h->second.Doing.Any()) return -1;
		Work& w = h->second.Doing;
		char go[160];
		if (o.Kind == "deliver")
		{
			// what the work lacks (and nobody is bringing yet), as much as one trip carries, from the store (T255: by the store building that holds it)
			if (!tribe.m_Items || !m.Body) return 0;
			Inventory& bag = m.Body->Bag();
			std::string what;
			int left = TechModifiers::Carry(tribe, tribe.Config.HaulAt);
			std::vector<std::string> taken;
			const bool sheds = ClanStores::Active(tribe);
			StoreLayout::Place via;
			for (const auto& [item, n] : w.Missing())
			{
				int coming = 0;
				for (const TribeMember& other : tribe.m_Members) if (other.Job == "deliver" && other.Detail == o.Target && other.Body) coming += WithSubstitutes(other.Body->Bag(), w, item);
				int k = std::min({ n - coming, WithSubstitutes(tribe.m_Store, w, item), left });
				std::vector<std::string> choices{ item };
				if (const auto sub = w.Substitutes.find(item); sub != w.Substitutes.end()) choices.insert(choices.end(), sub->second.begin(), sub->second.end());
				for (const std::string& one : choices)
				{
					if (k <= 0) break;
					const int want = std::min(k, tribe.m_Store.Count(one));
					StoreLayout::Place from;
					const std::string source = sheds && want > 0 ? tribe.m_Stores.Source(tribe, one, want, tribe.Where(m), &from) : std::string();
					const int moved = tribe.m_Store.Transfer(*tribe.m_Items, bag, one, want);
					if (moved <= 0) continue;
					if (!source.empty() && source != "-" && tribe.m_Stores.Take(source, one, moved) > 0 && via.Name.empty()) via = from;
					k -= moved;
					left -= moved;
					taken.push_back(one);
					what += (what.empty() ? "" : ", ") + std::to_string(moved) + " " + one + (one != item ? " (for " + item + ")" : std::string());
				}
			}
			if (what.empty()) return 0;
			std::snprintf(go, sizeof go, "goto %.2f %.2f | wait 1", static_cast<double>(o.Where.x), static_cast<double>(o.Where.y));
			if (!via.Name.empty())
				std::snprintf(go, sizeof go, "goto %.2f %.2f | wait 1 | goto %.2f %.2f | wait 1", static_cast<double>(via.Door.x), static_cast<double>(via.Door.y),
					static_cast<double>(o.Where.x), static_cast<double>(o.Where.y));
			if (!tribe.Order(m, go))
			{
				for (const std::string& item : taken) bag.Transfer(*tribe.m_Items, tribe.m_Store, item, bag.Count(item));
				return 0;
			}
			m.Job = "deliver";
			m.Detail = o.Target;
			tribe.Say(m, "carries " + what + " to " + o.Target + " for its " + (w.Kind == "grow" ? "growth" : w.To));
			return 1;
		}
		if (!w.Ready()) return 0;
		std::snprintf(go, sizeof go, "goto %.2f %.2f | wait %.1f", static_cast<double>(o.Where.x), static_cast<double>(o.Where.y), static_cast<double>(kBout));
		if (!tribe.Order(m, go)) return 0;
		const bool again = m.Job == "build" && m.Detail == o.Target;
		m.Job = "build";
		m.Detail = o.Target;
		if (!again) tribe.Say(m, "works on " + o.Target + (w.Kind == "grow" ? ": it grows to " + w.To : ": its " + w.To));
		return 1;
	}

	bool HouseModules::FinishWork(Tribe& tribe, TribeMember& m, const std::string& job)
	{
		if ((job != "deliver" && job != "build") || m_Houses.empty()) return false;
		const auto h = m_Houses.find(m.Detail);
		if (h == m_Houses.end() || !h->second.Doing.Any()) return false;
		Work& w = h->second.Doing;
		if (job == "deliver")
		{
			if (!m.Body) return true;
			for (const Tribe::Placed& p : tribe.Placements())
				if (p.Name == m.Detail && glm::distance(tribe.Where(m), p.Pos) > p.Def->Radius() + 3.0f) { tribe.Say(m, "could not get to " + m.Detail); return true; }
			Inventory& bag = m.Body->Bag();
			int given = 0;
			for (const auto& [item, n] : w.Missing())
			{
				int left = n;
				std::vector<std::string> choices{ item };
				if (const auto sub = w.Substitutes.find(item); sub != w.Substitutes.end()) choices.insert(choices.end(), sub->second.begin(), sub->second.end());
				for (const std::string& one : choices)
					if (const int k = std::min(left, bag.Count(one)); k > 0)
					{
						const int out = bag.Remove(one, k);
						w.Got[item] += out;
						left -= out;
						given += out;
					}
			}
			if (given > 0)
			{
				tribe.Say(m, "brings " + std::to_string(given) + " to " + m.Detail + (w.Ready() ? ": the work can start" : ""));
				tribe.Practise(m, "deliver", "deliver", static_cast<float>(given));
				if (const Job* held = tribe.m_Board.ClaimOf(m.Name); held && held->Kind == "deliver" && held->Target == m.Detail) tribe.m_Board.Progress(m.Name, static_cast<float>(given), tribe.m_Time);
			}
			return true;
		}
		if (!tribe.HasFailed(m) && w.Ready())
		{
			w.Done += kBout;
			tribe.Practise(m, "build", "build", 1.0f);
			if (const Job* held = tribe.m_Board.ClaimOf(m.Name); held && held->Kind == "build" && held->Target == m.Detail) tribe.m_Board.Progress(m.Name, 1.0f, tribe.m_Time);
		}
		if (w.Ready() && w.Done + 1e-4f >= w.Needed)
		{
			++m.Made;
			tribe.Say(m, "finishes the work on " + m.Detail);
			Complete(tribe, m.Detail);
		}
		return true;
	}

	void HouseModules::WorkDemand(const Tribe& tribe, std::map<std::string, std::map<std::string, int>>& now) const
	{
		for (const auto& [house, h] : m_Houses)
		{
			if (!h.Doing.Any()) continue;
			for (const auto& [item, n] : h.Doing.Missing())
			{
				int coming = 0;
				for (const TribeMember& o : tribe.m_Members) if (o.Job == "deliver" && o.Detail == house && o.Body) coming += WithSubstitutes(o.Body->Bag(), h.Doing, item);
				if (n - coming > 0) now["site " + house][item] = n - coming;
			}
		}
	}

	int HouseModules::Bring(const Tribe& tribe, const std::string& house, const Work& w, std::string* what) const
	{
		int bring = 0;
		for (const auto& [item, n] : w.Missing())
		{
			int coming = 0;
			for (const TribeMember& o : tribe.m_Members) if (!o.Dead && o.Job == "deliver" && o.Detail == house && o.Body) coming += WithSubstitutes(o.Body->Bag(), w, item);
			if (const int k = std::min(n - coming, WithSubstitutes(tribe.m_Store, w, item)); k > 0)
			{
				bring += k;
				if (what) *what += (what->empty() ? "" : ", ") + std::to_string(k) + " " + item;
			}
		}
		return bring;
	}

	int HouseModules::Builders(const Tribe& tribe, const std::string& house, const TribeMember* asking) const
	{
		int n = 0;
		for (const TribeMember& o : tribe.m_Members) if (&o != asking && !o.Dead && o.Job == "build" && o.Detail == house) ++n;
		return n;
	}

	void HouseModules::PostJobs(Tribe& tribe) const
	{
		if (m_Houses.empty()) return;
		const JobRules& rules = tribe.Config.Work.Jobs;
		auto priority = [&](const char* kind) { const auto p = rules.Priority.find(kind); return p != rules.Priority.end() ? p->second : 1.0f; };
		for (const auto& [house, h] : m_Houses)
		{
			if (!h.Doing.Any()) continue;
			const Work& w = h.Doing;
			const float low = w.Kind == "closure" ? 0.6f : 0.9f;   // (as its options: a closure is not pressing)
			const std::string aim = w.Kind == "grow" ? "growth to " + w.To : w.To;
			std::string what;
			if (const int bring = Bring(tribe, house, w, &what); bring > 0)
			{
				const int trip = std::max(1, TechModifiers::Carry(tribe, tribe.Config.HaulAt));
				const int slots = std::clamp((bring + trip - 1) / trip, 1, std::max(1, rules.MaxSlots));
				tribe.m_Board.Post("deliver", house, tribe.SkillFamilyOf("deliver"), static_cast<float>(bring), low * priority("deliver") * tribe.m_Designations.SiteFactor(tribe, house, "deliver", nullptr), slots,
					house + " lacks " + what + " for its " + aim + " and the store has it", tribe.m_Time, rules);
			}
			if (w.Ready())
				tribe.m_Board.Post("build", house, tribe.SkillFamilyOf("build"), std::max(0.0f, w.Needed - w.Done), low * priority("build") * tribe.m_Designations.SiteFactor(tribe, house, "build", nullptr),
					std::max(1, tribe.Config.Builders), "the " + aim + " of " + house + " has its materials (" + Fixed(w.Done, "%.0f") + "/" + Fixed(w.Needed, "%.0f") + " s of work)", tribe.m_Time, rules);
		}
	}

	float HouseModules::JobScore(const Tribe& tribe, const TribeMember& m, const Job& job) const
	{
		if (m_Houses.empty() || (job.Kind != "deliver" && job.Kind != "build")) return -1.0f;
		const auto h = m_Houses.find(job.Target);
		if (h == m_Houses.end() || !h->second.Doing.Any()) return -1.0f;
		for (const Tribe::Placed& p : tribe.Placements())
		{
			if (p.Name != job.Target) continue;
			const float distance = Falloff(glm::distance(tribe.Where(m), SiteFront(p.Pos, *p.Def)), 100.0f);
			const float liking = std::max(0.6f, tribe.MemberWeight(m, job.Kind));
			if (job.Kind == "build")
			{
				const AbilitySystemComponent* asc = tribe.m_Gameplay ? tribe.m_Gameplay->Find(m.Name) : nullptr;
				const bool can = asc && std::find(asc->Granted().begin(), asc->Granted().end(), "Build") != asc->Granted().end();
				return can && h->second.Doing.Ready() ? job.Priority * liking * distance : 0.0f;
			}
			return Bring(tribe, job.Target, h->second.Doing, nullptr) > 0 ? job.Priority * liking * distance : 0.0f;
		}
		return 0.0f;
	}

	void HouseModules::OpenWorks(std::vector<std::string>& out) const
	{
		for (const auto& [house, h] : m_Houses)
			if (h.Doing.Any() && std::find(out.begin(), out.end(), house) == out.end()) out.push_back(house);
	}

	bool HouseModules::WorkOpenFor(const Tribe& tribe, const std::string& house, const std::string& kind, const TribeMember* asking) const
	{
		const Work* w = WorkOn(house);
		if (!w) return false;
		if (kind == "build") return w->Ready() && Builders(tribe, house, asking) < tribe.Config.Builders;
		return Bring(tribe, house, *w, nullptr) > 0;
	}

	// ------------------------------------------------------------------ the AI (T367i)
	namespace
	{
		bool Absorbed(const std::string& id)
		{
			for (const auto& [host, sizes] : Rules().Sizes)
				for (const YardSize& z : sizes) if (z.Building == id && id != host) return true;
			return false;
		}
	}

	// The standing house of the clan with a yard that takes `d` now: a free slot of its kinds, room under its most, an allowed host. The one
	// with the fewest modules, then the nearest to the camp (by name on a tie).
	std::string HouseModules::FreeHost(const Tribe& tribe, const BuildingDef& d) const
	{
		std::string best;
		int fewest = 0;
		float nearest = 0.0f;
		for (const Tribe::Placed& p : tribe.Placements())
		{
			if (!p.State.Done || !p.Def->Yard.Any()) continue;
			if (!d.Module.Hosts.empty() && std::find(d.Module.Hosts.begin(), d.Module.Hosts.end(), p.Def->Id) == d.Module.Hosts.end()) continue;
			// (not a house the clan will pull down for a better one it knows: the shelter once it can raise huts; a house grown into is not that)
			bool passing = false;
			if (tribe.m_Buildings)
				for (const auto& [id, other] : *tribe.m_Buildings)
					if (!passing && !Absorbed(id) && std::find(other.Supersedes.begin(), other.Supersedes.end(), p.Def->Id) != other.Supersedes.end() && tribe.ClanKnowsBuilding(other)) passing = true;
			if (passing) continue;
			const int n = Count(p.Name);
			if (n >= Max(tribe, p.Name)) continue;
			bool room = false;
			for (const HouseModules::SlotView& s : Slots(tribe, p.Name))
				if (s.Site.empty() && std::find(d.Module.Slots.begin(), d.Module.Slots.end(), s.Slot.Kind) != d.Module.Slots.end()) room = true;
			if (!room) continue;
			const float dist = glm::distance(p.Pos, tribe.Config.Economy.Camp);
			if (best.empty() || n < fewest || (n == fewest && dist < nearest - 1e-3f)) { best = p.Name; fewest = n; nearest = dist; }
		}
		return best;
	}

	void HouseModules::Facts(const Tribe& tribe, SettlementFacts& facts) const
	{
		facts.ModuleRoom = [&tribe, this](const BuildingDef& d) { return !FreeHost(tribe, d).empty(); };
		facts.Absorbed = [](const BuildingDef& d) { return Absorbed(d.Id); };
		// the beds a growth in course will add count already (as a site's do): no other house goes up for them meanwhile
		if (m_Houses.empty() || !tribe.m_Buildings) return;
		for (const Tribe::Placed& p : tribe.Placements())
		{
			const Work* w = WorkOn(p.Name);
			if (!w || w->Kind != "grow" || !p.State.Done) continue;
			const LineAt line = LineOf(p.Def->Id, m_Houses.at(p.Name).Size);
			if (!line.Host) continue;
			for (size_t i = 0; i < line.Sizes->size(); ++i)
				if ((*line.Sizes)[i].Id == w->To)
					if (const BuildingDef* to = SizeDef(*tribe.m_Buildings, line, i)) facts.BedsComing += std::max(0, to->Shelter - p.Def->Shelter);
		}
	}

	int HouseModules::PlanNeed(Tribe& tribe, const BuildingNeed& need)
	{
		if (!tribe.m_Buildings) return 0;
		const auto it = tribe.m_Buildings->find(need.Building);
		if (it == tribe.m_Buildings->end()) return 0;
		const BuildingDef& d = it->second;
		if (d.Module.Any())
		{
			// a module the clan wants: in a free slot of a house's yard first (TALLERES 10.1); loose only if it may stand alone
			if (const std::string host = FreeHost(tribe, d); !host.empty())
			{
				m_Planning = true;
				const Reply r = Add(tribe, host, d.Id);
				m_Planning = false;
				if (r.Ok)
				{
					tribe.m_Log.push_back(Stamp(tribe.m_Time) + "the clan puts a " + d.Name + " in the yard of " + host + ": " + need.Why);
					return 1;
				}
			}
			return d.Module.Standalone ? 0 : -1;
		}
		// beds: a house grows in its place before another goes up (TALLERES 10.1, Y-1)
		if (d.Shelter > 0 && tribe.Beds() < static_cast<int>(std::count_if(tribe.m_Members.begin(), tribe.m_Members.end(), [](const TribeMember& m) { return !m.Dead; })))
			if (PlanGrow(tribe, true)) return 1;
		return 0;
	}

	bool HouseModules::Absorbs(const std::string& id) { return Absorbed(id); }

	bool HouseModules::PlanGrow(Tribe& tribe, bool any)
	{
		if (Rules().Sizes.empty() || !tribe.m_Buildings) return false;
		for (const auto& [house, h] : m_Houses) if (h.Doing.Kind == "grow") return false;   // (one growth at a time)
		auto payable = [&](const std::map<std::string, int>& cost)
		{
			for (const auto& [item, n] : cost)
			{
				bool from = tribe.m_Store.Count(item) > 0;
				for (const auto& [kind, node] : tribe.m_Defs) from = from || node.Item == item;
				if (!from) return false;
			}
			return true;
		};
		// the household that lacks the most beds in its house first, then any house of the smallest size (by name on a tie)
		std::vector<std::pair<int, std::string>> order;
		for (const Tribe::Household& hh : tribe.m_Households)
		{
			if (hh.Home.empty()) continue;
			for (const Tribe::Placed& p : tribe.Placements())
				if (p.Name == hh.Home && p.State.Done) order.emplace_back(static_cast<int>(hh.Members.size()) - p.Def->Shelter, p.Name);
		}
		std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
		for (const auto& [lack, house] : order)
		{
			if (!any && lack <= 0) break;
			const Info i = InfoOf(tribe, house);
			if (i.NextSize.empty() || !i.GrowBlocked.empty() || !payable(i.NextSizeCost)) continue;
			m_Planning = true;
			const Reply r = Grow(tribe, house, i.NextSize);
			m_Planning = false;
			if (r.Ok)
			{
				tribe.m_Log.back() += lack > 0 ? " (its household lacks " + std::to_string(lack) + " beds)" : std::string(" (the clan lacks beds)");
				return true;
			}
		}
		return false;
	}

	bool HouseModules::PlanClosure(Tribe& tribe)
	{
		if (Rules().Closures.empty() || m_Sites.empty()) return false;
		for (const auto& [house, h] : m_Houses) if (h.Doing.Any()) return false;   // (one work on a house at a time)
		std::map<std::string, int> hosts;
		for (const auto& [site, e] : m_Sites) ++hosts[e.Host];
		for (const auto& [house, n] : hosts)
		{
			if (n < Rules().AiClosureAfterModules) continue;
			const Info i = InfoOf(tribe, house);
			if (!i.Valid || i.NextClosure.empty() || !i.ClosureBlocked.empty()) continue;
			// only with plenty: twice what it costs in the store (its substitutes counted)
			const YardClosure* c = Rules().ClosureById(i.NextClosure);
			bool plenty = c != nullptr;
			for (const auto& [item, k] : i.NextClosureCost)
			{
				int have = tribe.m_Store.Count(item);
				if (const auto sub = c->Substitutes.find(item); sub != c->Substitutes.end()) for (const std::string& s : sub->second) have += tribe.m_Store.Count(s);
				plenty = plenty && have >= 2 * k;
			}
			if (!plenty) continue;
			m_Planning = true;
			const bool ok = SetClosure(tribe, house, i.NextClosure).Ok;
			m_Planning = false;
			if (ok) return true;
		}
		return false;
	}

	// ------------------------------------------------------------------ saving ("yards", version 1)
	std::string HouseModules::HousesToJson() const
	{
		Json houses = Json::object();
		for (const auto& [name, h] : m_Houses)
		{
			if (h.Empty()) continue;
			Json o = Json::object();
			if (!h.Size.empty()) o["size"] = h.Size;
			if (h.Closure > 0) o["closure"] = h.Closure;
			if (h.Doing.Any())
			{
				Json subs = Json::object();
				for (const auto& [item, list] : h.Doing.Substitutes) subs[item] = list;
				o["work"] = { { "kind", h.Doing.Kind }, { "to", h.Doing.To }, { "needs", ItemsJson(h.Doing.Needs) }, { "substitutes", subs }, { "got", ItemsJson(h.Doing.Got) },
					{ "done", h.Doing.Done }, { "needed", h.Doing.Needed } };
			}
			houses[name] = o;
		}
		return Json{ { "version", 1 }, { "houses", houses } }.dump();
	}

	bool HouseModules::HousesFromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object() || j.value("version", 0) != 1 || !j.contains("houses") || !j["houses"].is_object()) return false;
		std::map<std::string, House> houses;
		for (const auto& [name, o] : j["houses"].items())
		{
			if (!o.is_object()) return false;
			House h;
			if (o.contains("size")) { if (!o["size"].is_string()) return false; h.Size = o["size"].get<std::string>(); }
			if (o.contains("closure")) { if (!o["closure"].is_number_integer() || o["closure"].get<int>() < 0) return false; h.Closure = o["closure"].get<int>(); }
			if (o.contains("work"))
			{
				const Json& w = o["work"];
				if (!w.is_object() || !w.contains("kind") || !w["kind"].is_string() || !w.contains("to") || !w["to"].is_string()) return false;
				h.Doing.Kind = w["kind"].get<std::string>();
				h.Doing.To = w["to"].get<std::string>();
				if (h.Doing.Kind != "grow" && h.Doing.Kind != "closure") return false;
				if ((w.contains("needs") && !ReadItems(w["needs"], h.Doing.Needs)) || (w.contains("got") && !ReadItems(w["got"], h.Doing.Got))) return false;
				if (w.contains("substitutes"))
				{
					if (!w["substitutes"].is_object()) return false;
					for (const auto& [item, list] : w["substitutes"].items())
					{
						if (!list.is_array()) return false;
						for (const Json& s : list) { if (!s.is_string()) return false; h.Doing.Substitutes[item].push_back(s.get<std::string>()); }
					}
				}
				h.Doing.Done = w.value("done", 0.0f);
				h.Doing.Needed = w.value("needed", 0.0f);
			}
			if (!h.Empty()) houses[name] = std::move(h);
		}
		m_Houses = std::move(houses);
		m_Fenced.clear();
		m_Next = 0.0;
		return true;
	}
}
