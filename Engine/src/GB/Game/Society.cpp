// H9 + H10: the society of a clan (see Society.h). Plain CPU, deterministic: maps in name order, every chance a hash of the seed, a name and a counter.
#include "GB/Game/Society.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <nlohmann/json.hpp>

namespace GB
{
	using Json = nlohmann::json;

	namespace
	{
		// The salts of the dice, so two different questions about the same name never get the same answer.
		enum Salt : uint32_t { kAge = 1, kSex, kGene, kTrait, kBlend, kMutation, kOldAge, kBirth };

		uint64_t Mix(uint64_t h)   // splitmix64's finaliser
		{
			h ^= h >> 30; h *= 0xbf58476d1ce4e5b9ull;
			h ^= h >> 27; h *= 0x94d049bb133111ebull;
			return h ^ (h >> 31);
		}

		uint64_t HashText(const std::string& s, uint64_t h)
		{
			for (const char c : s) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ull; }   // FNV-1a
			return h;
		}

		float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
		// The centred effect of a value: 1 + strength x (2 v - 1).
		float Centred(float strength, float v) { return 1.0f + strength * (2.0f * v - 1.0f); }

		// The factor of a set of effects on an option: its kind and its family both count.
		float Effect(const OptionEffects& effects, const std::string& kind, const std::string& family, float v, bool centred)
		{
			float f = 1.0f;
			for (const std::string* key : { &kind, &family })
			{
				if (key->empty()) continue;
				const auto e = effects.find(*key);
				if (e != effects.end()) f *= centred ? Centred(e->second, v) : 1.0f + e->second;
			}
			return f;
		}

		std::pair<std::string, std::string> Key(const std::string& a, const std::string& b) { return a < b ? std::make_pair(a, b) : std::make_pair(b, a); }

		std::string Fixed(float v, int decimals = 2)
		{
			char b[32];
			std::snprintf(b, sizeof b, "%.*f", decimals, static_cast<double>(v));
			return b;
		}
	}

	void Society::Clear() { *this = Society(); }

	double Society::Roll(const SocietyRules& rules, const std::string& name, uint32_t salt, int64_t n) const
	{
		uint64_t h = HashText(name, 1469598103934665603ull ^ (static_cast<uint64_t>(rules.Seed) * 0x9e3779b97f4a7c15ull));
		h = Mix(h ^ (static_cast<uint64_t>(salt) << 40) ^ static_cast<uint64_t>(n));
		return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);   // 53 bits: 0..1
	}

	double Society::Bell(const SocietyRules& rules, const std::string& name, uint32_t salt, int64_t n) const
	{
		// three draws averaged: a bell from -1 to 1 (standard deviation about a third)
		return (Roll(rules, name, salt, n * 3) + Roll(rules, name, salt, n * 3 + 1) + Roll(rules, name, salt, n * 3 + 2)) / 1.5 - 1.0;
	}

	// ------------------------------------------------------------------ members
	const Person& Society::Join(const SocietyRules& rules, const std::string& name)
	{
		if (const auto it = m_People.find(name); it != m_People.end()) return it->second;
		Person p;
		p.Age = rules.Life.FounderMin + (rules.Life.FounderMax - rules.Life.FounderMin) * static_cast<float>(Roll(rules, name, kAge, 0));
		p.Female = m_Founders % 2 == 0;   // alternating: a clan of founders is as many women as men (give or take one)
		++m_Founders;
		int64_t i = 0;
		for (const auto& [id, g] : rules.Genes) p.Genes[id] = Clamp01(g.Mean + g.Spread * static_cast<float>(Bell(rules, name + "/" + id, kGene, i++)));
		for (const auto& [id, t] : rules.Traits)
		{
			p.Genes[id] = Clamp01(t.Mean + t.Spread * static_cast<float>(Bell(rules, name + "/" + id, kTrait, i++)));
			p.Traits[id] = p.Genes[id];   // nobody brought a founder up in this clan: its nature is all it has
		}
		return m_People.emplace(name, std::move(p)).first->second;
	}

	const Person& Society::Bear(const SocietyRules& rules, const std::string& child, const std::string& mother, const std::string& father)
	{
		const Person* m = Find(mother);
		const Person* f = Find(father);
		Person p;
		p.Age = 0.0f;
		p.Female = Roll(rules, child, kSex, 0) < 0.5;
		p.Mother = mother;
		p.Father = father;
		p.Generation = std::max(m ? m->Generation : 0, f ? f->Generation : 0) + 1;
		auto parentGene = [&](const Person* parent, const std::string& id, float mean)
		{
			if (!parent) return mean;
			const auto g = parent->Genes.find(id);
			return g == parent->Genes.end() ? mean : g->second;
		};
		auto inherit = [&](const std::string& id, float mean)
		{
			// recombination: a random blend of the two parents for each gene, then a small mutation
			const float w = static_cast<float>(Roll(rules, child + "/" + id, kBlend, 0));
			const float blend = parentGene(m, id, mean) * w + parentGene(f, id, mean) * (1.0f - w);
			return Clamp01(blend + rules.Mutation * static_cast<float>(Bell(rules, child + "/" + id, kMutation, 0)));
		};
		for (const auto& [id, g] : rules.Genes) p.Genes[id] = inherit(id, g.Mean);
		for (const auto& [id, t] : rules.Traits)
		{
			p.Genes[id] = inherit(id, t.Mean);
			// the upbringing: what its parents are like, nudged by what the clan values (the norm that follows this trait)
			auto parentTrait = [&](const Person* parent)
			{
				if (!parent) return t.Mean;
				const auto v = parent->Traits.find(id);
				return v == parent->Traits.end() ? t.Mean : v->second;
			};
			float upbringing = 0.5f * (parentTrait(m) + parentTrait(f));
			for (const auto& [norm, def] : rules.Culture.Norms)
				if (def.Trait == id) { upbringing = 0.5f * (upbringing + Norm(rules, norm)); break; }
			p.Traits[id] = Clamp01(t.Heritability * p.Genes[id] + (1.0f - t.Heritability) * upbringing);
		}
		if (Person* mm = Find(mother)) ++mm->Children;
		if (Person* ff = Find(father)) ++ff->Children;
		++m_Births;
		m_People[child] = std::move(p);
		const Person& out = m_People[child];
		// a child starts with a bond with its parents (and a family tie on top of it)
		for (const std::string& parent : { mother, father })
			if (!parent.empty() && Find(parent) && !Find(parent)->Dead) Relate(rules, child, parent, "birth");
		return out;
	}

	void Society::Admit(const std::string& name, const Person& person)
	{
		if (m_People.count(name)) return;
		Person p = person;
		p.Partner.clear();   // (a partner in the clan it left is not one here; Marry gives it one)
		m_People[name] = std::move(p);
	}

	bool Society::Marry(const SocietyRules& rules, const std::string& a, const std::string& b)
	{
		Person* pa = Find(a);
		Person* pb = Find(b);
		if (!pa || !pb || pa == pb || pa->Dead || pb->Dead) return false;
		for (Person* p : { pa, pb })
			if (!p->Partner.empty())
				if (Person* old = Find(p->Partner); old && old->Partner != a && old->Partner != b) old->Partner.clear();
		pa->Partner = b;
		pb->Partner = a;
		Relate(rules, a, b, "pair");
		return true;
	}

	void Society::Leave(const std::string& name)
	{
		Unlink(name);
		if (Person* p = Find(name); p && !p->Partner.empty())
			if (Person* partner = Find(p->Partner); partner && partner->Partner == name) partner->Partner.clear();
		m_People.erase(name);
	}

	std::vector<SocietyEvent> Society::Died(const SocietyRules& rules, const std::string& name, const std::string& cause)
	{
		std::vector<SocietyEvent> out;
		Person* p = Find(name);
		if (!p || p->Dead) return out;
		p->Dead = true;
		p->Cause = cause;
		if (Person* partner = Find(p->Partner); partner && partner->Partner == name) partner->Partner.clear();
		Unlink(name);
		++m_Deaths;
		for (SocietyEvent& e : Note(rules, "death")) out.push_back(e);
		return out;
	}

	const Person* Society::Find(const std::string& name) const
	{
		const auto it = m_People.find(name);
		return it == m_People.end() ? nullptr : &it->second;
	}

	Person* Society::Find(const std::string& name)
	{
		const auto it = m_People.find(name);
		return it == m_People.end() ? nullptr : &it->second;
	}

	int Society::Alive() const
	{
		int n = 0;
		for (const auto& [name, p] : m_People) if (!p.Dead) ++n;
		return n;
	}

	void Society::SetAge(const std::string& name, float years) { if (Person* p = Find(name)) p->Age = std::max(0.0f, years); }
	void Society::SetGene(const std::string& name, const std::string& gene, float value) { if (Person* p = Find(name)) p->Genes[gene] = Clamp01(value); }
	void Society::SetTrait(const std::string& name, const std::string& trait, float value) { if (Person* p = Find(name)) p->Traits[trait] = Clamp01(value); }

	// ------------------------------------------------------------------ life
	Society::Stage Society::StageOf(const SocietyRules& rules, const std::string& name) const
	{
		const Person* p = Find(name);
		if (!p) return Stage::Adult;
		return p->Age < rules.Life.ChildUntil ? Stage::Child : p->Age >= rules.Life.ElderFrom ? Stage::Elder : Stage::Adult;
	}

	float Society::LearnRate(const SocietyRules& rules, const std::string& name, const std::string& family) const
	{
		const Person* p = Find(name);
		if (!p) return 1.0f;
		float r = StageOf(rules, name) == Stage::Child ? rules.Life.ChildLearn : 1.0f;
		if (!family.empty())
			for (const auto& [id, g] : rules.Genes)
				if (const auto b = g.Learn.find(family); b != g.Learn.end())
					if (const auto v = p->Genes.find(id); v != p->Genes.end()) r *= Centred(b->second, v->second);
		return std::max(0.0f, r);
	}

	float Society::TeachRate(const SocietyRules& rules, const std::string& name) const
	{
		if (!Find(name)) return 1.0f;
		const Stage s = StageOf(rules, name);
		return s == Stage::Elder ? rules.Life.ElderTeach : s == Stage::Child ? rules.Life.ChildTeach : 1.0f;
	}

	float Society::WorkPace(const SocietyRules& rules, const std::string& name, const std::string& family) const
	{
		const Person* p = Find(name);
		if (!p) return 1.0f;
		const Stage s = StageOf(rules, name);
		float r = s == Stage::Elder ? rules.Life.ElderPace : s == Stage::Child ? rules.Life.ChildPace : 1.0f;
		if (!family.empty())
			for (const auto& [id, g] : rules.Genes)
				if (const auto b = g.Pace.find(family); b != g.Pace.end())
					if (const auto v = p->Genes.find(id); v != p->Genes.end()) r *= Centred(b->second, v->second);
		return std::max(0.05f, r);
	}

	float Society::Growth(const SocietyRules& rules, const std::string& name) const
	{
		const Person* p = Find(name);
		if (!p || rules.Life.ChildUntil <= 0.0f || p->Age >= rules.Life.ChildUntil) return 1.0f;
		return rules.Life.ChildHeight + (1.0f - rules.Life.ChildHeight) * p->Age / rules.Life.ChildUntil;
	}

	Society::Factors Society::OptionFactor(const SocietyRules& rules, const std::string& name, const std::string& kind, const std::string& family) const
	{
		Factors f;
		const Person* p = Find(name);
		if (!p) return f;
		for (const auto& [id, g] : rules.Genes)
			if (!g.Effects.empty())
				if (const auto v = p->Genes.find(id); v != p->Genes.end()) f.Nature *= Effect(g.Effects, kind, family, v->second, true);
		for (const auto& [id, t] : rules.Traits)
			if (const auto v = p->Traits.find(id); v != p->Traits.end()) f.Nature *= Effect(t.Effects, kind, family, v->second, true);
		const Stage s = StageOf(rules, name);
		if (s != Stage::Adult)
		{
			const bool heavy = std::any_of(rules.Life.Heavy.begin(), rules.Life.Heavy.end(), [&](const std::string& h) { return h == kind || (!family.empty() && h == family); });
			if (heavy) f.Life = s == Stage::Child ? rules.Life.ChildWork : rules.Life.ElderWork;
		}
		for (const auto& [id, n] : rules.Culture.Norms) f.Culture *= Effect(n.Effects, kind, family, Norm(rules, id), true);
		for (const std::string& id : m_Traditions)
			if (const auto t = rules.Culture.Traditions.find(id); t != rules.Culture.Traditions.end()) f.Culture *= Effect(t->second.Effects, kind, family, 1.0f, false);
		f.Nature = std::clamp(f.Nature, 0.05f, 4.0f);
		f.Culture = std::clamp(f.Culture, 0.05f, 4.0f);
		return f;
	}

	// ------------------------------------------------------------------ the days
	bool Society::Tick(const SocietyRules& rules, float dt, float daySeconds)
	{
		const double day = rules.Life.DaySeconds > 0.0f ? rules.Life.DaySeconds : daySeconds;
		if (day <= 0.0) return false;
		m_DayTimer += static_cast<double>(dt);
		if (m_DayTimer < day) return false;
		m_DayTimer -= day;
		if (m_DayTimer >= day) m_DayTimer = 0.0;   // a huge step is one day, not a backlog
		return true;
	}

	std::vector<SocietyEvent> Society::Day(const SocietyRules& rules, const ClanState& clan)
	{
		std::vector<SocietyEvent> out;
		const int64_t day = m_Days++;
		const LifeRules& life = rules.Life;
		const float year = 1.0f / std::max(life.DaysPerYear, 1e-3f);

		// growing older (and the stages it brings)
		for (auto& [name, p] : m_People)
		{
			if (p.Dead) continue;
			const float before = p.Age;
			p.Age += year;
			if (before < life.ChildUntil && p.Age >= life.ChildUntil) out.push_back({ "adult", name, "", "comes of age (" + Fixed(p.Age, 0) + ")" });
			if (before < life.ElderFrom && p.Age >= life.ElderFrom) out.push_back({ "elder", name, "", "is one of the elders now (" + Fixed(p.Age, 0) + ")" });
		}

		// old age: nobody outlives its MaxAge (moved by the longevity gene); from ElderFrom on, the chance a year grows as the square of the way there
		std::vector<std::string> dying;
		for (const auto& [name, p] : m_People)
		{
			if (p.Dead || p.Age < life.ElderFrom) continue;
			float limit = life.MaxAge;
			if (const auto g = p.Genes.find("longevity"); g != p.Genes.end()) limit += life.Longevity * (2.0f * g->second - 1.0f);
			limit = std::max(limit, life.ElderFrom + 1.0f);
			const float way = std::clamp((p.Age - life.ElderFrom) / (limit - life.ElderFrom), 0.0f, 1.0f);
			const double chance = static_cast<double>(life.OldAge * way * way * year);
			if (p.Age >= limit || Roll(rules, name, kOldAge, day) < chance) dying.push_back(name);
		}
		for (const std::string& name : dying)
		{
			out.push_back({ "death", name, "", "OldAge" });
			for (SocietyEvent& e : Died(rules, name, "OldAge")) out.push_back(e);
		}

		// pairing: of all the free couples that could be (old enough, of the other sex, not family, close in age, liking each other at least PairMin),
		// the best liked pair first (ties: by name), then the next among those still free
		const BirthRules& births = rules.Births;
		std::vector<std::pair<std::string, const Person*>> single;
		for (const auto& [name, p] : m_People)
			if (!p.Dead && p.Partner.empty() && p.Age >= births.PairAge) single.emplace_back(name, &p);
		struct Match { float Affinity; std::string A, B; };
		std::vector<Match> matches;
		for (size_t i = 0; i < single.size(); ++i)
			for (size_t k = i + 1; k < single.size(); ++k)
			{
				const Person& p = *single[i].second;
				const Person& q = *single[k].second;
				if (q.Female == p.Female || std::fabs(q.Age - p.Age) > births.AgeGap) continue;
				if (!Kin(single[i].first, single[k].first).empty()) continue;   // (neither has a partner: any tie is blood)
				const float a = Affinity(rules, single[i].first, single[k].first);
				if (a >= births.PairMin) matches.push_back({ a, single[i].first, single[k].first });
			}
		std::stable_sort(matches.begin(), matches.end(), [](const Match& x, const Match& y) { return x.Affinity > y.Affinity; });
		for (const Match& m : matches)
		{
			Person& a = m_People[m.A];
			Person& b = m_People[m.B];
			if (!a.Partner.empty() || !b.Partner.empty()) continue;
			a.Partner = m.B;
			b.Partner = m.A;
			Relate(rules, m.A, m.B, "pair");
			out.push_back({ "pair", m.A, m.B, "pairs with " + m.B });
			for (SocietyEvent& e : Note(rules, "pair")) out.push_back(e);
		}

		// births: a couple has a child when the mother is of an age, her last child is far enough, and the clan has food and a fire to spare
		const float food = clan.Alive > 0 ? clan.Food / static_cast<float>(clan.Alive) : 0.0f;
		const bool enough = food >= births.FoodPerMember && (!births.NeedsFire || clan.Shelter);
		int members = clan.Alive;
		for (auto& [name, p] : m_People)
		{
			if (!enough || members >= births.MaxMembers) break;
			if (p.Dead || !p.Female || p.Partner.empty() || p.Age < births.MinAge || p.Age > births.MaxAge || p.Age - p.LastBirth < births.Spacing) continue;
			if (Roll(rules, name, kBirth, day) >= static_cast<double>(births.Chance)) continue;
			p.LastBirth = p.Age;
			++members;
			out.push_back({ "birth", name, p.Partner, "" });
		}

		// the bonds fade (towards 0) and the weakest go
		if (rules.Relations.DecayPerDay > 0.0f)
		{
			std::vector<std::pair<std::string, std::string>> gone;
			for (auto& [key, bond] : m_Bonds)
			{
				bond *= 1.0f - rules.Relations.DecayPerDay;
				if (std::fabs(bond) < rules.Relations.Keep) gone.push_back(key);
			}
			for (const auto& key : gone)
			{
				m_Bonds.erase(key);
				m_Links[key.first].erase(key.second);
				m_Links[key.second].erase(key.first);
			}
		}

		// the norms drift towards what the living adults are like
		for (const auto& [id, n] : rules.Culture.Norms)
		{
			if (n.Trait.empty()) continue;
			float sum = 0.0f;
			int count = 0;
			for (const auto& [name, p] : m_People)
			{
				if (p.Dead || p.Age < life.ChildUntil) continue;
				const auto t = p.Traits.find(n.Trait);
				sum += t == p.Traits.end() ? 0.5f : t->second;
				++count;
			}
			if (count == 0) continue;
			const float before = Norm(rules, id);
			const float after = before + rules.Culture.Drift * (sum / static_cast<float>(count) - before);
			m_Norms[id] = after;
			auto band = [](float v) { return v >= 0.65f ? 1 : v <= 0.35f ? -1 : 0; };
			if (band(after) != band(before))
				out.push_back({ "norm", id, "", band(after) > 0 ? "the clan values " + id + " more and more (" + Fixed(after) + ")" : band(after) < 0 ? "the clan cares little for " + id + " (" + Fixed(after) + ")" : "the clan is of two minds about " + id + " (" + Fixed(after) + ")" });
		}
		return out;
	}

	// ------------------------------------------------------------------ relations
	float Society::Bond(const std::string& a, const std::string& b) const
	{
		const auto it = m_Bonds.find(Key(a, b));
		return it == m_Bonds.end() ? 0.0f : it->second;
	}

	std::string Society::Kin(const std::string& a, const std::string& b) const
	{
		const Person* p = Find(a);
		const Person* q = Find(b);
		if (!p || !q || a == b) return {};
		if (p->Partner == b) return "partner";
		if (p->Mother == b) return "mother";
		if (p->Father == b) return "father";
		if (q->Mother == a || q->Father == a) return "child";
		const bool sameMother = !p->Mother.empty() && p->Mother == q->Mother, sameFather = !p->Father.empty() && p->Father == q->Father;
		if (sameMother || sameFather) return "sibling";
		return {};
	}

	float Society::Affinity(const SocietyRules& rules, const std::string& a, const std::string& b) const
	{
		return std::clamp(Bond(a, b) + (Kin(a, b).empty() ? 0.0f : rules.Relations.Kin), -1.0f, 1.0f);
	}

	void Society::Relate(const SocietyRules& rules, const std::string& a, const std::string& b, const std::string& event, float scale)
	{
		const auto e = rules.Relations.Events.find(event);
		if (e != rules.Relations.Events.end()) Adjust(rules, a, b, e->second * scale);
	}

	void Society::Adjust(const SocietyRules& rules, const std::string& a, const std::string& b, float delta)
	{
		if (a == b || delta == 0.0f || !Find(a) || !Find(b)) return;
		const auto key = Key(a, b);
		float& bond = m_Bonds[key];
		bond = std::clamp(bond + delta, -1.0f, 1.0f);
		if (std::fabs(bond) < rules.Relations.Keep * 0.5f)   // (a quarrel that cancels a kindness leaves nothing)
		{
			m_Bonds.erase(key);
			m_Links[a].erase(b);
			m_Links[b].erase(a);
			return;
		}
		m_Links[a].insert(b);
		m_Links[b].insert(a);
		Trim(a, rules.Relations.MaxPerMember);
		Trim(b, rules.Relations.MaxPerMember);
	}

	// Keeps the `keep` strongest bonds of a member (by size; ties: the first by name) and drops the others: the graph stays sparse at any size.
	void Society::Trim(const std::string& name, int keep)
	{
		const auto links = m_Links.find(name);
		if (links == m_Links.end() || static_cast<int>(links->second.size()) <= keep) return;
		std::vector<std::pair<float, std::string>> bonds;
		for (const std::string& other : links->second) bonds.emplace_back(std::fabs(Bond(name, other)), other);
		std::stable_sort(bonds.begin(), bonds.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
		for (size_t i = static_cast<size_t>(std::max(keep, 0)); i < bonds.size(); ++i)
		{
			const std::string& other = bonds[i].second;
			m_Bonds.erase(Key(name, other));
			m_Links[other].erase(name);
			links->second.erase(other);
		}
	}

	void Society::Unlink(const std::string& name)
	{
		const auto links = m_Links.find(name);
		if (links == m_Links.end()) return;
		for (const std::string& other : links->second)
		{
			m_Bonds.erase(Key(name, other));
			if (const auto o = m_Links.find(other); o != m_Links.end()) o->second.erase(name);
		}
		m_Links.erase(links);
	}

	std::vector<std::pair<std::string, float>> Society::BondsOf(const std::string& name) const
	{
		std::vector<std::pair<std::string, float>> out;
		const auto links = m_Links.find(name);
		if (links == m_Links.end()) return out;
		for (const std::string& other : links->second) out.emplace_back(other, Bond(name, other));
		std::stable_sort(out.begin(), out.end(), [](const auto& x, const auto& y) { return std::fabs(x.second) > std::fabs(y.second); });
		return out;
	}

	std::vector<std::string> Society::Mentors(const SocietyRules& rules, const std::string& name, size_t count) const
	{
		std::vector<std::string> out;
		const Person* p = Find(name);
		if (!p) return out;
		for (const std::string* parent : { &p->Mother, &p->Father })
			if (const Person* q = Find(*parent); q && !q->Dead && out.size() < count) out.push_back(*parent);
		std::vector<std::pair<float, std::string>> liked;
		if (const auto links = m_Links.find(name); links != m_Links.end())
			for (const std::string& other : links->second)
				if (std::find(out.begin(), out.end(), other) == out.end())
					if (const float a = Affinity(rules, name, other); a > 0.0f) liked.emplace_back(a, other);
		std::stable_sort(liked.begin(), liked.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
		for (const auto& [a, other] : liked)
		{
			if (out.size() >= count) break;
			out.push_back(other);
		}
		return out;
	}

	std::string Society::Favourite(const SocietyRules& rules, const std::string& name, const std::vector<std::string>& candidates) const
	{
		std::string best;
		float bestAffinity = -2.0f;
		for (const std::string& c : candidates)
			if (const float a = Affinity(rules, name, c); a > bestAffinity) { best = c; bestAffinity = a; }
		return best;
	}

	// ------------------------------------------------------------------ culture
	float Society::Norm(const SocietyRules& rules, const std::string& norm) const
	{
		if (const auto it = m_Norms.find(norm); it != m_Norms.end()) return it->second;
		const auto def = rules.Culture.Norms.find(norm);
		return def == rules.Culture.Norms.end() ? 0.5f : def->second.Start;
	}

	std::vector<SocietyEvent> Society::Note(const SocietyRules& rules, const std::string& event)
	{
		std::vector<SocietyEvent> out;
		const int n = ++m_Counts[event];
		for (const auto& [id, t] : rules.Culture.Traditions)
		{
			if (t.Event != event || n < t.Count || std::find(m_Traditions.begin(), m_Traditions.end(), id) != m_Traditions.end()) continue;
			m_Traditions.push_back(id);
			out.push_back({ "tradition", id, "", "the clan keeps a new tradition: " + t.Name + " (" + std::to_string(n) + " times " + event + ")" });
		}
		return out;
	}

	// ------------------------------------------------------------------ for people
	std::string Society::Family(const SocietyRules& rules, const std::string& name) const
	{
		const Person* p = Find(name);
		if (!p) return name + ": not in the society";
		std::string s = name + ": " + (p->Female ? "woman" : "man") + ", " + Fixed(p->Age, 1) + " years, " + StageName(StageOf(rules, name)) + ", generation " + std::to_string(p->Generation);
		if (p->Dead) s += ", dead (" + p->Cause + ")";
		auto who = [&](const std::string& n) { const Person* q = Find(n); return n + (q && q->Dead ? " (dead)" : ""); };
		s += "; mother " + (p->Mother.empty() ? std::string("-") : who(p->Mother)) + ", father " + (p->Father.empty() ? std::string("-") : who(p->Father));
		s += "; partner " + (p->Partner.empty() ? std::string("-") : p->Partner);
		std::string children, siblings;
		for (const auto& [other, q] : m_People)
		{
			if (other == name) continue;
			const std::string kin = Kin(name, other);
			if (kin == "child") children += (children.empty() ? "" : ", ") + who(other);
			else if (kin == "sibling") siblings += (siblings.empty() ? "" : ", ") + who(other);
		}
		s += "; children " + (children.empty() ? std::string("-") : children) + "; siblings " + (siblings.empty() ? std::string("-") : siblings);
		if (!p->Dead && p->Female && p->Age >= rules.Births.MinAge && p->Age <= rules.Births.MaxAge && p->LastBirth >= 0.0f) s += "; last child at " + Fixed(p->LastBirth, 1);
		return s;
	}

	std::string Society::Character(const SocietyRules& rules, const std::string& name) const
	{
		const Person* p = Find(name);
		if (!p) return name + ": not in the society";
		std::string s = name + " genes:";
		for (const auto& [id, g] : rules.Genes)
			if (const auto v = p->Genes.find(id); v != p->Genes.end()) s += " " + id + " " + Fixed(v->second);
		s += "; personality:";
		for (const auto& [id, t] : rules.Traits)
			if (const auto v = p->Traits.find(id); v != p->Traits.end()) s += " " + id + " " + Fixed(v->second);
		return s;
	}

	std::string Society::Relations(const SocietyRules& rules, const std::string& name) const
	{
		if (!Find(name)) return name + ": not in the society";
		std::string s = name + " relations:";
		std::set<std::string> listed;
		for (const auto& [other, bond] : BondsOf(name))
		{
			const std::string kin = Kin(name, other);
			s += " " + other + " " + Fixed(Affinity(rules, name, other)) + (kin.empty() ? "" : " (" + kin + ")");
			listed.insert(other);
		}
		for (const auto& [other, q] : m_People)   // family it has no bond with (yet) still counts
			if (!q.Dead && !listed.count(other))
				if (const std::string kin = Kin(name, other); !kin.empty()) s += " " + other + " " + Fixed(Affinity(rules, name, other)) + " (" + kin + ")";
		if (s.back() == ':') s += " nobody yet";
		return s;
	}

	std::string Society::Culture(const SocietyRules& rules) const
	{
		std::string s = "norms:";
		for (const auto& [id, n] : rules.Culture.Norms) s += " " + id + " " + Fixed(Norm(rules, id));
		s += "; traditions:";
		if (m_Traditions.empty()) s += " none yet";
		for (const std::string& id : m_Traditions)
		{
			const auto t = rules.Culture.Traditions.find(id);
			s += " " + (t == rules.Culture.Traditions.end() ? id : t->second.Name) + ";";
		}
		s += " day " + std::to_string(m_Days) + ", " + std::to_string(Alive()) + " alive, " + std::to_string(m_Births) + " born, " + std::to_string(m_Deaths) + " died, " + std::to_string(m_Bonds.size()) + " bonds";
		return s;
	}

	// ------------------------------------------------------------------ saving
	std::string Society::ToJson() const
	{
		Json people = Json::object();
		for (const auto& [name, p] : m_People)
		{
			Json o = { { "age", p.Age }, { "female", p.Female }, { "generation", p.Generation }, { "children", p.Children }, { "lastBirth", p.LastBirth }, { "genes", p.Genes }, { "traits", p.Traits } };
			if (!p.Mother.empty()) o["mother"] = p.Mother;
			if (!p.Father.empty()) o["father"] = p.Father;
			if (!p.Partner.empty()) o["partner"] = p.Partner;
			if (p.Dead) { o["dead"] = true; o["cause"] = p.Cause; }
			people[name] = o;
		}
		Json bonds = Json::array();
		for (const auto& [key, v] : m_Bonds) bonds.push_back(Json::array({ key.first, key.second, v }));
		return Json{ { "people", people }, { "bonds", bonds }, { "norms", m_Norms }, { "counts", m_Counts }, { "traditions", m_Traditions }, { "founders", m_Founders },
			{ "days", m_Days }, { "births", m_Births }, { "deaths", m_Deaths }, { "dayTimer", m_DayTimer } }.dump();
	}

	bool Society::FromJson(const std::string& json)
	{
		const Json j = Json::parse(json, nullptr, false);
		if (j.is_discarded() || !j.is_object() || !j.contains("people") || !j["people"].is_object()) return false;
		Society s;
		try
		{
			for (const auto& [name, o] : j["people"].items())
			{
				if (!o.is_object()) return false;
				Person p;
				p.Age = o.value("age", 25.0f);
				p.Female = o.value("female", false);
				p.Generation = o.value("generation", 0);
				p.Children = o.value("children", 0);
				p.LastBirth = o.value("lastBirth", -100.0f);
				p.Mother = o.value("mother", std::string());
				p.Father = o.value("father", std::string());
				p.Partner = o.value("partner", std::string());
				p.Dead = o.value("dead", false);
				p.Cause = o.value("cause", std::string());
				if (o.contains("genes")) p.Genes = o["genes"].get<std::map<std::string, float>>();
				if (o.contains("traits")) p.Traits = o["traits"].get<std::map<std::string, float>>();
				s.m_People[name] = std::move(p);
			}
			if (j.contains("bonds"))
				for (const Json& b : j["bonds"])
				{
					if (!b.is_array() || b.size() != 3) return false;
					const std::string a = b[0].get<std::string>(), c = b[1].get<std::string>();
					s.m_Bonds[Key(a, c)] = b[2].get<float>();
					s.m_Links[a].insert(c);
					s.m_Links[c].insert(a);
				}
			if (j.contains("norms")) s.m_Norms = j["norms"].get<std::map<std::string, float>>();
			if (j.contains("counts")) s.m_Counts = j["counts"].get<std::map<std::string, int>>();
			if (j.contains("traditions")) s.m_Traditions = j["traditions"].get<std::vector<std::string>>();
			s.m_Founders = j.value("founders", 0);
			s.m_Days = j.value("days", 0);
			s.m_Births = j.value("births", 0);
			s.m_Deaths = j.value("deaths", 0);
			s.m_DayTimer = j.value("dayTimer", 0.0);
		}
		catch (const Json::exception&)
		{
			return false;
		}
		*this = std::move(s);
		return true;
	}
}
