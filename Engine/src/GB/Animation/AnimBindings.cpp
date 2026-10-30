#include "GB/Animation/AnimBindings.h"
#include "GB/Gameplay/AbilitySystemComponent.h"
#include "GB/Gameplay/Combat.h"
#include "GB/Gameplay/GameplayAbility.h"
#include "GB/Log.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

namespace GB
{
	namespace
	{
		using Json = nlohmann::json;

		std::string Lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		bool StartsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }
		std::string After(const std::string& s, size_t n) { return s.size() > n ? s.substr(n) : std::string(); }

		const MontageDesc* FindMontage(const AnimManifest& m, const std::string& name)
		{
			for (const MontageDesc& d : m.Montages) if (d.Name == name) return &d;
			return nullptr;
		}

		bool SkeletonMatches(const std::string& scope, const std::string& skeleton) { return scope.empty() || Lower(scope) == Lower(skeleton); }

		// The binding that answers for (action, skeleton, context): the most specific, the last in the file on a tie. Null when none.
		const AnimBinding* FindBinding(const AnimBindings& b, const std::string& action, const std::map<std::string, std::string>& context, const std::string& skeleton)
		{
			const AnimBinding* best = nullptr;
			int bestScore = -1;
			for (const AnimBinding& c : b.Bindings)
			{
				if (c.Action != action || !SkeletonMatches(c.Skeleton, skeleton)) continue;
				bool ok = true;
				for (const auto& [k, v] : c.When)
				{
					const auto it = context.find(k);
					if (it == context.end() || it->second != v) { ok = false; break; }
				}
				if (!ok) continue;
				const int score = (c.Skeleton.empty() ? 0 : 100) + static_cast<int>(c.When.size());
				if (score >= bestScore) { best = &c; bestScore = score; }
			}
			return best;
		}

		const std::vector<std::string>* DefaultList(const AnimManifest& manifest, const BindingDefaults& defaults, const std::string& action)
		{
			if (StartsWith(action, "ability:"))
			{
				const auto it = defaults.Abilities.find(After(action, 8));
				if (it != defaults.Abilities.end()) return &it->second;
				if (const ManifestUse* u = FindUse(manifest.Abilities, After(action, 8))) return &u->Montages;
			}
			else if (StartsWith(action, "activity:"))
			{
				if (const ManifestUse* u = FindUse(manifest.Activities, After(action, 9))) return &u->Montages;
			}
			return nullptr;
		}

		std::string AliasName(const AnimBinding& b, int i);

		// Resolves clip number `turn` of `binding` against the manifest. `why` tells why it cannot be built (a missing clip).
		bool BuildFromEntry(const AnimBinding& binding, const AnimManifest& manifest, int turn, const ClipExists& hasClip, ResolvedClip& out, std::string& why)
		{
			if (binding.Clips.empty()) { why = "the binding has no clips"; return false; }
			const BindingClip& c = binding.Clips[static_cast<size_t>(turn) % binding.Clips.size()];
			const MontageDesc* base = FindMontage(manifest, c.Source);
			if (!base && hasClip && !hasClip(c.Source)) { why = "clip '" + c.Source + "' does not exist"; return false; }
			if (base && hasClip && !hasClip(base->Clip)) { why = "montage '" + c.Source + "' plays the clip '" + base->Clip + "', which the model lacks"; return false; }
			const float t0 = c.TrimStart.value_or(base ? base->TrimStart : 0.0f), t1 = c.TrimEnd.value_or(base ? base->TrimEnd : 1.0f);
			if (t0 < 0.0f || t1 > 1.0f || t1 <= t0) { why = "the trim " + std::to_string(t0) + ".." + std::to_string(t1) + " is not inside 0..1 or is empty"; return false; }
			if (c.Speed && !(*c.Speed > 0.0f)) { why = "the speed is not positive"; return false; }
			if ((c.BlendIn && *c.BlendIn < 0.0f) || (c.BlendOut && *c.BlendOut < 0.0f)) { why = "a blend is negative"; return false; }
			ResolvedClip r;
			if (base) r = FromMontage(*base);
			else r.Clip = c.Source;
			r.Montage = base ? base->Name : std::string();
			if (c.Loop) r.Loop = *c.Loop;
			if (c.TrimStart) r.TrimStart = *c.TrimStart;
			if (c.TrimEnd) r.TrimEnd = *c.TrimEnd;
			if (c.Speed) r.Speed = *c.Speed;
			if (c.BlendIn) r.BlendIn = *c.BlendIn;
			if (c.BlendOut) r.BlendOut = *c.BlendOut;
			if (c.Curve) r.Curve = *c.Curve;
			if (c.KeepRootMotion) r.KeepRootMotion = *c.KeepRootMotion;
			r.Found = true;
			r.FromBinding = true;
			r.Why = "binding";
			r.Playable = (!c.HasOverrides() && base) ? base->Name : AliasName(binding, static_cast<int>(static_cast<size_t>(turn) % binding.Clips.size()));
			r.Set = binding.Set;
			r.Repeat = binding.Repeat.value_or(false);
			out = std::move(r);
			return true;
		}

		ManifestUse* FindUseMutable(std::vector<ManifestUse>& uses, const std::string& name)
		{
			for (ManifestUse& u : uses) if (u.Name == name) return &u;
			return nullptr;
		}

		ManifestSet* FindSetMutable(AnimManifest& m, const std::string& name)
		{
			if (name == "default" || name == "free" || name == AnimInstance::kDefaultSet) return &m.Free;
			for (ManifestSet& s : m.Sets) if (s.Name == name) return &s;
			return nullptr;
		}

		const ManifestSet* FindSet(const AnimManifest& m, const std::string& name)
		{
			if (name == "default" || name == "free" || name == AnimInstance::kDefaultSet) return &m.Free;
			for (const ManifestSet& s : m.Sets) if (s.Name == name) return &s;
			return nullptr;
		}

		// The montage made for clip `i` of a binding: "bind:ability:Chop#0", and for one that depends on the context "bind:ability:Chop?node=tree&tool=axe#0"
		std::string AliasName(const AnimBinding& b, int i)
		{
			std::string name = "bind:" + b.Action;
			char sep = '?';
			for (const auto& [k, v] : b.When) { name += sep; name += k + "=" + v; sep = '&'; }
			return name + "#" + std::to_string(i);
		}

		// The binding answers for its own conditions (no other one of the file wins where it would), so its clips are worth making montages of.
		bool Reachable(const AnimBindings& all, const AnimBinding& b, const std::string& skeleton)
		{
			return SkeletonMatches(b.Skeleton, skeleton) && FindBinding(all, b.Action, b.When, skeleton) == &b;
		}

		bool IsUseAction(const std::string& action) { return StartsWith(action, "ability:") || StartsWith(action, "activity:"); }
	}

	std::string SkeletonKey(const std::string& modelPath)
	{
		size_t slash = modelPath.find_last_of("/\\");
		std::string name = slash == std::string::npos ? modelPath : modelPath.substr(slash + 1);
		const size_t dot = name.find_last_of('.');
		if (dot != std::string::npos) name.resize(dot);
		return Lower(name);
	}

	// ---- JSON

	std::string AnimBindingsToJson(const AnimBindings& b)
	{
		Json root;
		root["version"] = b.Version;
		root["note"] = "Animation bindings: action -> clip, with loop / trim (fractions of the clip) / speed / blend. Edited in the Editor (Animation Bindings window); see docs/EDITOR_ANIMACIONES.md. An action not listed here plays its default.";
		Json list = Json::array();
		for (const AnimBinding& a : b.Bindings)
		{
			Json j;
			j["action"] = a.Action;
			if (!a.Skeleton.empty()) j["skeleton"] = a.Skeleton;
			if (!a.When.empty()) { Json w = Json::object(); for (const auto& [k, v] : a.When) w[k] = v; j["when"] = w; }
			if (!a.Set.empty()) j["set"] = a.Set;
			if (a.Repeat) j["repeat"] = *a.Repeat;
			if (!a.Note.empty()) j["note"] = a.Note;
			Json clips = Json::array();
			for (const BindingClip& c : a.Clips)
			{
				Json k;
				k["source"] = c.Source;
				if (!c.Replaces.empty()) k["replaces"] = c.Replaces;
				if (c.Loop) k["loop"] = *c.Loop;
				if (c.TrimStart) k["trimStart"] = *c.TrimStart;
				if (c.TrimEnd) k["trimEnd"] = *c.TrimEnd;
				if (c.Speed) k["speed"] = *c.Speed;
				if (c.BlendIn) k["blendIn"] = *c.BlendIn;
				if (c.BlendOut) k["blendOut"] = *c.BlendOut;
				if (c.Curve) k["curve"] = BlendCurveName(*c.Curve);
				if (c.KeepRootMotion) k["keepRootMotion"] = *c.KeepRootMotion;
				clips.push_back(std::move(k));
			}
			j["clips"] = std::move(clips);
			list.push_back(std::move(j));
		}
		root["bindings"] = std::move(list);
		Json trs = Json::array();
		for (const BindingTransition& t : b.Transitions)
		{
			Json j;
			j["from"] = t.From;
			j["to"] = t.To;
			if (!t.Skeleton.empty()) j["skeleton"] = t.Skeleton;
			j["duration"] = t.Duration;
			j["curve"] = BlendCurveName(t.Curve);
			trs.push_back(std::move(j));
		}
		root["transitions"] = std::move(trs);
		if (!b.Locomotion.empty())
		{
			Json locos = Json::array();
			for (const LocomotionTuning& t : b.Locomotion)
			{
				Json j = Json::object();
				if (!t.Skeleton.empty()) j["skeleton"] = t.Skeleton;
				const ManifestLocomotion& l = t.Values;
				if (l.MinSpeedScale) j["minSpeedScale"] = *l.MinSpeedScale;
				if (l.MaxSpeedScale) j["maxSpeedScale"] = *l.MaxSpeedScale;
				if (l.VelocitySmoothing) j["velocitySmoothing"] = *l.VelocitySmoothing;
				if (l.MinGaitWeight) j["minGaitWeight"] = *l.MinGaitWeight;
				if (l.SetFade) j["setFade"] = *l.SetFade;
				Json clips = Json::array();
				for (const ManifestClipSpeed& c : l.Clips) clips.push_back({ { "set", c.Set }, { "clip", c.Clip }, { "speed", c.Speed } });
				if (!l.Clips.empty()) j["clips"] = clips;
				locos.push_back(std::move(j));
			}
			root["locomotion"] = std::move(locos);
		}
		return root.dump(2) + "\n";
	}

	bool AnimBindingsFromJson(const std::string& text, AnimBindings& out, std::string* error)
	{
		auto fail = [&](const std::string& why) { if (error) *error = why; return false; };
		const Json root = Json::parse(text, nullptr, false);
		if (root.is_discarded() || !root.is_object()) return fail("the animation bindings are not valid JSON");
		const int version = root.value("version", 0);
		if (version != kAnimBindingsVersion) return fail("unknown animation bindings version " + std::to_string(version));
		AnimBindings result;
		auto optF = [](const Json& j, const char* key) { std::optional<float> v; if (j.contains(key) && j[key].is_number()) v = j[key].get<float>(); return v; };
		auto optB = [](const Json& j, const char* key) { std::optional<bool> v; if (j.contains(key) && j[key].is_boolean()) v = j[key].get<bool>(); return v; };
		try
		{
			if (const auto it = root.find("bindings"); it != root.end())
			{
				if (!it->is_array()) return fail("'bindings' must be a list");
				for (const Json& j : *it)
				{
					if (!j.is_object()) return fail("a binding must be an object");
					AnimBinding a;
					a.Action = j.value("action", "");
					if (a.Action.empty()) return fail("a binding needs an 'action'");
					a.Skeleton = j.value("skeleton", "");
					a.Set = j.value("set", "");
					a.Note = j.value("note", "");
					a.Repeat = optB(j, "repeat");
					if (j.contains("when") && j["when"].is_object())
						for (auto w = j["when"].begin(); w != j["when"].end(); ++w) a.When[w.key()] = w.value().is_string() ? w.value().get<std::string>() : w.value().dump();
					if (j.contains("clips") && j["clips"].is_array())
						for (const Json& k : j["clips"])
						{
							BindingClip c;
							c.Source = k.value("source", "");
							c.Replaces = k.value("replaces", "");
							c.Loop = optB(k, "loop");
							c.TrimStart = optF(k, "trimStart");
							c.TrimEnd = optF(k, "trimEnd");
							c.Speed = optF(k, "speed");
							c.BlendIn = optF(k, "blendIn");
							c.BlendOut = optF(k, "blendOut");
							if (k.contains("curve") && k["curve"].is_string()) c.Curve = BlendCurveFromName(k["curve"].get<std::string>());
							c.KeepRootMotion = optB(k, "keepRootMotion");
							a.Clips.push_back(std::move(c));
						}
					result.Bindings.push_back(std::move(a));
				}
			}
			if (const auto it = root.find("transitions"); it != root.end())
			{
				if (!it->is_array()) return fail("'transitions' must be a list");
				for (const Json& j : *it)
				{
					BindingTransition t;
					t.From = j.value("from", "");
					t.To = j.value("to", "");
					t.Skeleton = j.value("skeleton", "");
					t.Duration = j.value("duration", 0.2f);
					t.Curve = BlendCurveFromName(j.value("curve", "smooth"));
					result.Transitions.push_back(std::move(t));
				}
			}
			if (const auto it = root.find("locomotion"); it != root.end())
			{
				if (!it->is_array()) return fail("'locomotion' must be a list");
				for (const Json& j : *it)
				{
					if (!j.is_object()) return fail("a locomotion entry must be an object");
					LocomotionTuning t;
					t.Skeleton = j.value("skeleton", "");
					t.Values.MinSpeedScale = optF(j, "minSpeedScale");
					t.Values.MaxSpeedScale = optF(j, "maxSpeedScale");
					t.Values.VelocitySmoothing = optF(j, "velocitySmoothing");
					t.Values.MinGaitWeight = optF(j, "minGaitWeight");
					t.Values.SetFade = optF(j, "setFade");
					if (j.contains("clips") && j["clips"].is_array())
						for (const Json& c : j["clips"])
							if (c.is_object()) t.Values.Clips.push_back({ c.value("set", ""), c.value("clip", ""), c.value("speed", 0.0f) });
					result.Locomotion.push_back(std::move(t));
				}
			}
		}
		catch (const Json::exception& ex)
		{
			return fail(std::string("bad value: ") + ex.what());
		}
		out = std::move(result);
		return true;
	}

	bool LoadAnimBindingsFile(const std::string& path, AnimBindings& out, std::string* error, bool* found)
	{
		std::ifstream in(path, std::ios::binary);
		if (found) *found = static_cast<bool>(in);
		if (!in) { out = {}; return true; }
		std::stringstream text;
		text << in.rdbuf();
		AnimBindings b;
		if (!AnimBindingsFromJson(text.str(), b, error)) { out = {}; return false; }
		out = std::move(b);
		return true;
	}

	bool SaveAnimBindingsFile(const std::string& path, const AnimBindings& bindings, std::string* error)
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out) { if (error) *error = "could not write " + path; return false; }
		out << AnimBindingsToJson(bindings);
		return static_cast<bool>(out);
	}

	// ---- defaults and the action list

	std::map<std::string, std::vector<std::string>> AbilityMontageLists(const GameplayLibrary& library)
	{
		std::map<std::string, std::vector<std::string>> lists;
		for (const auto& [name, def] : library.Abilities()) lists[name] = def.Montages;
		return lists;
	}

	int RestoreAbilityMontages(GameplayLibrary& library, const std::map<std::string, std::vector<std::string>>& lists)
	{
		int changed = 0;
		for (const auto& [name, montages] : lists)
		{
			const AbilityDef* def = library.Ability(name);
			if (!def || def->Montages == montages) continue;
			AbilityDef copy = *def;
			copy.Montages = montages;
			library.AddAbility(copy);
			++changed;
		}
		return changed;
	}

	BindingDefaults MakeBindingDefaults(const AnimManifest& manifest, const GameplayLibrary* library)
	{
		return MakeBindingDefaults(manifest, library ? AbilityMontageLists(*library) : std::map<std::string, std::vector<std::string>>());
	}

	BindingDefaults MakeBindingDefaults(const AnimManifest& manifest, const std::map<std::string, std::vector<std::string>>& libraryMontages)
	{
		BindingDefaults d;
		// the combat abilities (Attack.<weapon>, Block, Stagger) have their montages written in C++ (Combat.cpp): shown even where the game has not
		// registered combat (the editor without the conflict rules), so they can be bound there too
		GameplayLibrary combat;
		RegisterCombatContent(combat, DefaultCombatRules());
		std::map<std::string, std::vector<std::string>> lists = AbilityMontageLists(combat);
		for (const auto& [name, montages] : libraryMontages) lists[name] = montages;
		for (const auto& [name, montages] : lists)
		{
			d.OneShotAbilities.push_back(name);
			d.Abilities[name] = montages;
		}
		for (const ManifestUse& u : manifest.Abilities)
		{
			if (u.Montages.empty()) continue;
			d.Abilities[u.Name] = u.Montages;   // (at runtime the manifest's list wins over the library's)
			if (std::find(d.OneShotAbilities.begin(), d.OneShotAbilities.end(), u.Name) == d.OneShotAbilities.end()) d.OneShotAbilities.push_back(u.Name);
		}
		return d;
	}

	std::vector<BindableAction> ListBindableActions(const AnimManifest& manifest, const BindingDefaults& defaults)
	{
		std::vector<BindableAction> out;
		auto category = [](const std::string& name)
		{
			if (StartsWith(name, "Attack.") || name == "ShootBow") return "Attack";
			if (name == "Block" || name == "Stagger" || name == "Flee") return "Defence";
			if (name == "Chop" || name == "Mine" || name == "Gather" || name == "PullFiber" || name == "Collect" || name == "DrawWater" || name == "Stoke") return "Interaction";
			return "Other";
		};
		out.push_back({ "air", "Locomotion", { manifest.AirClip }, false });
		out.push_back({ "loco:default", "Locomotion", manifest.Free.Clips, false });
		for (const ManifestSet& s : manifest.Sets) out.push_back({ "loco:" + s.Name, "Locomotion", s.Clips, false });
		for (const auto& [name, montages] : defaults.Abilities)
			out.push_back({ "ability:" + name, category(name), montages, true });
		for (const ManifestUse& u : manifest.Abilities)
			if (!defaults.Abilities.count(u.Name)) out.push_back({ "ability:" + u.Name, category(u.Name), u.Montages, true });
		for (const ManifestUse& u : manifest.Activities)
		{
			const char* cat = u.Name == "sleep" || u.Name == "dead" || u.Name == "eat" || u.Name == "drink" || u.Name == "rest" || u.Name == "injured" || u.Name == "recover" ? "Life"
				: StartsWith(u.Name, "talk.") || StartsWith(u.Name, "rite.") ? "Social" : "Work";
			out.push_back({ "activity:" + u.Name, cat, u.Montages, false });   // (an activity may loop, and a locomotion set always does: neither is a one-shot)
		}
		return out;
	}

	// ---- context

	std::map<std::string, std::string> BindingContext(const AbilityDef& ability, const AbilitySystemComponent* owner, const AbilitySystemComponent* target)
	{
		std::map<std::string, std::string> c;
		c["ability"] = ability.Name;
		if (target)
		{
			c["target"] = target->Name();
			for (const auto& [tag, count] : target->Tags().All())
			{
				if (StartsWith(tag, "Resource.Node.")) { if (!c.count("node")) c["node"] = tag.substr(14); }
				else if (StartsWith(tag, "Resource.") && tag != "Resource.Node" && tag.find('.', 9) == std::string::npos && !c.count("kind")) c["kind"] = Lower(tag.substr(9));
			}
		}
		// the tool the ability asks for, and which of that kind the owner holds (the most specific tag: "Item.Tool.Axe.Flint" is a flint axe)
		std::string required;
		for (const std::string& t : ability.RequiredTags)
			if (StartsWith(t, "Item.Tool.")) { required = t; break; }
		c["tool"] = "none";
		if (!required.empty())
		{
			const size_t dot = required.find('.', 10);
			const std::string family = dot == std::string::npos ? required : required.substr(0, dot);   // "Item.Tool.Axe"
			c["tool"] = Lower(family.substr(10));
			std::string variant = "basic";
			if (owner)
				for (const auto& [tag, count] : owner->Tags().All())
					if (StartsWith(tag, (family + ".").c_str())) { variant = Lower(tag.substr(family.size() + 1)); break; }
			c["variant"] = variant;
		}
		return c;
	}

	// ---- resolution

	ResolvedClip FromMontage(const MontageDesc& d)
	{
		ResolvedClip r;
		r.Found = true;
		r.Montage = d.Name;
		r.Clip = d.Clip;
		r.Loop = d.Loop;
		r.TrimStart = d.TrimStart;
		r.TrimEnd = d.TrimEnd;
		r.Speed = d.PlayRate;
		r.BlendIn = d.BlendIn;
		r.BlendOut = d.BlendOut;
		r.Curve = d.Curve;
		r.KeepRootMotion = !d.StripTravel;
		return r;
	}

	MontageDesc ToMontage(const ResolvedClip& r, const AnimManifest& manifest)
	{
		MontageDesc d;
		if (const MontageDesc* base = FindMontage(manifest, r.Montage)) d = *base;   // slot, notifies, sections and hold-end of the base
		d.Name.clear();
		const float baseStart = d.TrimStart, baseEnd = d.TrimEnd;
		d.Clip = r.Clip;
		d.Loop = r.Loop;
		d.TrimStart = r.TrimStart;
		d.TrimEnd = r.TrimEnd;
		d.PlayRate = r.Speed;
		d.BlendIn = r.BlendIn;
		d.BlendOut = r.BlendOut;
		d.Curve = r.Curve;
		d.StripTravel = !r.KeepRootMotion;
		// the notifies that the new trim leaves outside would never fire: they are put inside it, in proportion to where they were in the trim of the
		// base (an ability's hit stays at the same point of the swing whatever part of the clip plays)
		const float from = std::clamp(baseStart, 0.0f, 1.0f), to = std::clamp(baseEnd, 0.0f, 1.0f);
		for (MontageNotify& n : d.Notifies)
			if (n.Time < r.TrimStart - 1e-4f || n.Time > r.TrimEnd + 1e-4f)
			{
				const float along = to > from + 1e-4f ? std::clamp((n.Time - from) / (to - from), 0.0f, 1.0f) : 0.5f;
				n.Time = r.TrimStart + along * (r.TrimEnd - r.TrimStart);
			}
		return d;
	}

	ResolvedClip ResolveClip(const AnimBindings& bindings, const AnimManifest& manifest, const BindingDefaults& defaults, const std::string& action,
		const std::map<std::string, std::string>& context, const std::string& skeleton, int turn, const ClipExists& hasClip)
	{
		std::string skipped;
		if (const AnimBinding* b = FindBinding(bindings, action, context, skeleton))
		{
			ResolvedClip r;
			std::string why;
			if (BuildFromEntry(*b, manifest, turn, hasClip, r, why))
			{
				if (r.Set.empty())
					if (const ManifestUse* u = StartsWith(action, "activity:") ? FindUse(manifest.Activities, After(action, 9)) : nullptr) r.Set = u->Set;
				return r;
			}
			skipped = why;
		}
		ResolvedClip r;
		r.Why = skipped.empty() ? "default" : "default (binding skipped: " + skipped + ")";
		if (StartsWith(action, "loco:") || action == "air")
		{
			const ManifestSet* s = action == "air" ? nullptr : FindSet(manifest, After(action, 5));
			const std::string clip = action == "air" ? manifest.AirClip : (s && !s->Clips.empty() ? s->Clips[static_cast<size_t>(turn) % s->Clips.size()] : std::string());
			r.Clip = clip;
			r.Found = !clip.empty();
			r.Loop = action != "air";
			return r;
		}
		const std::vector<std::string>* list = DefaultList(manifest, defaults, action);
		if (!list || list->empty()) { r.Why = skipped.empty() ? "nothing is bound or defined for this action" : r.Why; return r; }
		const std::string& name = (*list)[static_cast<size_t>(turn) % list->size()];
		if (const MontageDesc* base = FindMontage(manifest, name)) { r = FromMontage(*base); r.Why = skipped.empty() ? "default" : "default (binding skipped: " + skipped + ")"; }
		else { r.Found = true; r.Montage = name; r.Clip = name; }   // a library montage the manifest does not describe: its name is all we know
		if (StartsWith(action, "activity:"))
			if (const ManifestUse* u = FindUse(manifest.Activities, After(action, 9))) { r.Set = u->Set; r.Repeat = u->Loop; }
		return r;
	}

	// ---- locomotion tuning

	ManifestLocomotion LocomotionFor(const AnimBindings& bindings, const std::string& skeleton)
	{
		ManifestLocomotion out;
		for (int pass = 0; pass < 2; ++pass)   // general entries first, then the model's own
			for (const LocomotionTuning& t : bindings.Locomotion)
			{
				if (t.Skeleton.empty() != (pass == 0) || !SkeletonMatches(t.Skeleton, skeleton)) continue;
				const ManifestLocomotion& v = t.Values;
				if (v.MinSpeedScale) out.MinSpeedScale = v.MinSpeedScale;
				if (v.MaxSpeedScale) out.MaxSpeedScale = v.MaxSpeedScale;
				if (v.VelocitySmoothing) out.VelocitySmoothing = v.VelocitySmoothing;
				if (v.MinGaitWeight) out.MinGaitWeight = v.MinGaitWeight;
				if (v.SetFade) out.SetFade = v.SetFade;
				for (const ManifestClipSpeed& c : v.Clips)
				{
					bool replaced = false;
					for (ManifestClipSpeed& e : out.Clips) if (e.Set == c.Set && e.Clip == c.Clip) { e = c; replaced = true; }
					if (!replaced) out.Clips.push_back(c);
				}
			}
		return out;
	}

	// ---- applying

	BindingsReport ApplyBindings(AnimManifest& manifest, const AnimBindings& bindings, const BindingDefaults& defaults, const std::string& skeleton, const ClipExists& hasClip)
	{
		BindingsReport report;
		auto skip = [&](const std::string& text)
		{
			report.Skipped.push_back(text);
			GB_CLIENT_WARN("Animation bindings: {}", text);
		};
		for (const AnimBinding& entry : bindings.Bindings)
		{
			if (!Reachable(bindings, entry, skeleton)) continue;   // (another skeleton's, or shadowed by a more specific one)
			const AnimBinding* chosen = &entry;
			const std::string& action = chosen->Action;
			const bool contextual = !chosen->When.empty();
			if (contextual && !IsUseAction(action)) { skip(action + ": 'when' only works on abilities and activities (ignored)"); continue; }

			if (StartsWith(action, "loco:"))
			{
				ManifestSet* set = FindSetMutable(manifest, After(action, 5));
				if (!set) { skip(action + ": no such locomotion set"); continue; }
				bool changed = false;
				for (const BindingClip& c : chosen->Clips)
				{
					auto it = std::find(set->Clips.begin(), set->Clips.end(), c.Replaces);
					if (c.Replaces.empty() || it == set->Clips.end()) { skip(action + ": the set has no clip '" + c.Replaces + "' to replace"); continue; }
					if (hasClip && !hasClip(c.Source)) { skip(action + ": clip '" + c.Source + "' does not exist (default kept)"); continue; }
					*it = c.Source;
					changed = true;
				}
				if (changed) ++report.Applied;
				continue;
			}
			if (action == "air")
			{
				if (chosen->Clips.empty() || chosen->Clips[0].Source.empty()) { skip("air: no clip"); continue; }
				if (hasClip && !hasClip(chosen->Clips[0].Source)) { skip("air: clip '" + chosen->Clips[0].Source + "' does not exist (default kept)"); continue; }
				manifest.AirClip = chosen->Clips[0].Source;
				++report.Applied;
				continue;
			}
			const bool isAbility = StartsWith(action, "ability:"), isActivity = StartsWith(action, "activity:");
			if (!isAbility && !isActivity) { skip(action + ": unknown kind of action"); continue; }

			std::vector<std::string> names;
			for (size_t i = 0; i < chosen->Clips.size(); ++i)
			{
				ResolvedClip r;
				std::string why;
				if (!BuildFromEntry(*chosen, manifest, static_cast<int>(i), hasClip, r, why)) { skip(action + " #" + std::to_string(i + 1) + ": " + why + " (default kept)"); continue; }
				if (!chosen->Clips[i].HasOverrides() && !r.Montage.empty()) { names.push_back(r.Montage); continue; }   // the montage as the manifest has it
				MontageDesc d = ToMontage(r, manifest);
				d.Name = AliasName(*chosen, static_cast<int>(i));
				bool replaced = false;
				for (MontageDesc& m : manifest.Montages) if (m.Name == d.Name) { m = d; replaced = true; }
				if (!replaced) manifest.Montages.push_back(d);
				names.push_back(d.Name);
			}
			if (names.empty()) continue;
			if (contextual) { ++report.Applied; continue; }   // (the montages exist; the game picks among them by context: Actor::PickMontage)
			std::vector<ManifestUse>& uses = isAbility ? manifest.Abilities : manifest.Activities;
			const std::string name = After(action, isAbility ? 8 : 9);
			ManifestUse* use = FindUseMutable(uses, name);
			if (!use) { uses.push_back({}); use = &uses.back(); use->Name = name; }
			use->Montages = names;
			if (!chosen->Set.empty()) use->Set = chosen->Set;
			if (chosen->Repeat) use->Loop = *chosen->Repeat;
			if (!chosen->Note.empty()) use->Note = chosen->Note;
			++report.Applied;
		}

		// the tuning of the blend space
		{
			const ManifestLocomotion tuning = LocomotionFor(bindings, skeleton);
			const bool sane = (!tuning.MinSpeedScale || *tuning.MinSpeedScale > 0.0f) && (!tuning.MaxSpeedScale || *tuning.MaxSpeedScale > 0.0f)
				&& (!tuning.VelocitySmoothing || *tuning.VelocitySmoothing >= 0.0f) && (!tuning.SetFade || *tuning.SetFade >= 0.0f);
			if (!sane) skip("locomotion: a scale must be positive and a time cannot be negative (not applied)");
			else if (!tuning.Empty())
			{
				ManifestLocomotion& out = manifest.Locomotion;
				if (tuning.MinSpeedScale) out.MinSpeedScale = tuning.MinSpeedScale;
				if (tuning.MaxSpeedScale) out.MaxSpeedScale = tuning.MaxSpeedScale;
				if (tuning.VelocitySmoothing) out.VelocitySmoothing = tuning.VelocitySmoothing;
				if (tuning.MinGaitWeight) out.MinGaitWeight = tuning.MinGaitWeight;
				if (tuning.SetFade) out.SetFade = tuning.SetFade;
				for (const ManifestClipSpeed& c : tuning.Clips)
				{
					if (!(c.Speed > 0.0f)) { skip("locomotion: the speed of '" + c.Clip + "' must be positive (kept as it was)"); continue; }
					if (hasClip && !hasClip(c.Clip)) { skip("locomotion: clip '" + c.Clip + "' does not exist"); continue; }
					bool replaced = false;
					for (ManifestClipSpeed& e : out.Clips) if (e.Set == c.Set && e.Clip == c.Clip) { e = c; replaced = true; }
					if (!replaced) out.Clips.push_back(c);
				}
				report.Locomotion = true;
			}
		}

		// transitions: actions expand to their montages
		auto expand = [&](const std::string& e) -> std::vector<std::string>
		{
			if (e == "*") return { "*" };
			if (StartsWith(e, "ability:") || StartsWith(e, "activity:"))
			{
				const bool ab = StartsWith(e, "ability:");
				const ManifestUse* u = FindUse(ab ? manifest.Abilities : manifest.Activities, After(e, ab ? 8 : 9));
				if (u && !u->Montages.empty()) return u->Montages;
				if (const std::vector<std::string>* l = DefaultList(manifest, defaults, e)) return *l;
				return {};
			}
			return { e };
		};
		for (const BindingTransition& t : bindings.Transitions)
		{
			if (!SkeletonMatches(t.Skeleton, skeleton)) continue;
			const std::vector<std::string> from = expand(t.From), to = expand(t.To);
			if (from.empty() || to.empty()) { skip("transition " + t.From + " -> " + t.To + ": an end plays no montage"); continue; }
			for (const std::string& f : from)
				for (const std::string& g : to)
				{
					MontageTransition mt{ f, g, t.Duration, t.Curve };
					bool replaced = false;
					for (MontageTransition& e : manifest.Transitions) if (e.From == f && e.To == g) { e = mt; replaced = true; }
					if (!replaced) manifest.Transitions.push_back(mt);
				}
			++report.Transitions;
		}
		return report;
	}

	// ---- validation

	std::vector<BindingIssue> ValidateBindings(const AnimBindings& bindings, const AnimManifest& manifest, const BindingDefaults& defaults, const ClipExists& hasClip)
	{
		std::vector<BindingIssue> issues;
		auto error = [&](const std::string& t) { issues.push_back({ BindingIssue::Level::Error, t }); };
		auto warn = [&](const std::string& t) { issues.push_back({ BindingIssue::Level::Warning, t }); };
		std::set<std::string> seen;
		for (const AnimBinding& b : bindings.Bindings)
		{
			const std::string where = b.Action + (b.Skeleton.empty() ? "" : " [" + b.Skeleton + "]");
			const bool isAbility = StartsWith(b.Action, "ability:"), isActivity = StartsWith(b.Action, "activity:"), isLoco = StartsWith(b.Action, "loco:"), isAir = b.Action == "air";
			if (!isAbility && !isActivity && !isLoco && !isAir) { error(where + ": unknown kind of action (use ability:, activity:, loco: or air)"); continue; }
			std::string key = b.Action + "|" + Lower(b.Skeleton);
			for (const auto& [k, v] : b.When) key += "|" + k + "=" + v;
			if (!seen.insert(key).second) warn(where + ": repeated (the last one wins)");
			if (b.Clips.empty()) error(where + ": has no clips");
			if (isLoco && !FindSet(manifest, After(b.Action, 5))) error(where + ": no such locomotion set");
			if (isAbility || isActivity)
			{
				const std::string name = After(b.Action, isAbility ? 8 : 9);
				const bool known = isAbility ? (defaults.Abilities.count(name) || FindUse(manifest.Abilities, name)) : FindUse(manifest.Activities, name) != nullptr;
				if (!known) warn(where + ": the game has no such " + (isAbility ? "ability" : "activity") + " (nothing will play it)");
			}
			if (!b.Set.empty() && !isActivity) warn(where + ": 'set' only works on activities");
			if (!b.Set.empty() && !FindSet(manifest, b.Set)) error(where + ": the locomotion set '" + b.Set + "' does not exist");
			for (size_t i = 0; i < b.Clips.size(); ++i)
			{
				const BindingClip& c = b.Clips[i];
				const std::string at = where + " clip #" + std::to_string(i + 1);
				const MontageDesc* base = FindMontage(manifest, c.Source);
				if (c.Source.empty()) { error(at + ": has no source"); continue; }
				if (!base && hasClip && !hasClip(c.Source)) error(at + ": clip '" + c.Source + "' does not exist");
				else if (base && hasClip && !hasClip(base->Clip)) error(at + ": montage '" + c.Source + "' plays '" + base->Clip + "', which the model lacks");
				if (isLoco)
				{
					const ManifestSet* s = FindSet(manifest, After(b.Action, 5));
					if (s && (c.Replaces.empty() || std::find(s->Clips.begin(), s->Clips.end(), c.Replaces) == s->Clips.end()))
						error(at + ": replaces '" + c.Replaces + "', which the set does not have");
					if (c.HasOverrides()) warn(at + ": loop / trim / speed / blend do nothing on locomotion clips (the blend space plays them)");
					continue;
				}
				if (isAir && c.HasOverrides()) warn(at + ": loop / trim / speed / blend do nothing on the jump pose");
				const float t0 = c.TrimStart.value_or(0.0f), t1 = c.TrimEnd.value_or(1.0f);
				if (t0 < 0.0f || t0 > 1.0f || t1 < 0.0f || t1 > 1.0f) error(at + ": trim must be between 0 and 1");
				else if (t1 <= t0) error(at + ": trim end must be after trim start");
				else if (t1 - t0 < 0.03f) warn(at + ": the trim leaves almost nothing of the clip");
				if (c.Speed && (!(*c.Speed > 0.0f) || *c.Speed > 5.0f)) error(at + ": speed must be between 0 (excluded) and 5");
				if ((c.BlendIn && *c.BlendIn < 0.0f) || (c.BlendOut && *c.BlendOut < 0.0f)) error(at + ": a blend cannot be negative");
				if (base && t1 > t0 && t0 >= 0.0f && t1 <= 1.0f)
					for (const MontageNotify& n : base->Notifies)
						if (n.Time < t0 || n.Time > t1) warn(at + ": notify '" + n.Name + "' of montage '" + base->Name + "' falls outside the trim: it is moved inside it, in proportion (the timing changes)");
				const bool loop = c.Loop.value_or(base ? base->Loop : false);
				if (isAbility && loop) warn(at + ": loops, but an ability is a single shot (it would never reach its end)");
				if (isAbility && base && base->HoldEnd && !c.Loop) warn(at + ": the montage holds its last frame until stopped, on a single-shot ability");
			}
		}
		// locomotion tuning
		for (const LocomotionTuning& t : bindings.Locomotion)
		{
			const std::string at = "locomotion" + (t.Skeleton.empty() ? std::string() : " [" + t.Skeleton + "]");
			const ManifestLocomotion& l = t.Values;
			if (l.MinSpeedScale && (*l.MinSpeedScale <= 0.0f || *l.MinSpeedScale > 1.0f)) error(at + ": the minimum speed factor must be in (0, 1]");
			if (l.MaxSpeedScale && (*l.MaxSpeedScale < 1.0f || *l.MaxSpeedScale > 4.0f)) error(at + ": the maximum speed factor must be between 1 and 4");
			if (l.VelocitySmoothing && (*l.VelocitySmoothing < 0.0f || *l.VelocitySmoothing > 2.0f)) error(at + ": the velocity smoothing must be between 0 and 2 seconds");
			if (l.MinGaitWeight && (*l.MinGaitWeight < 0.0f || *l.MinGaitWeight > 1.0f)) error(at + ": the gait share must be between 0 and 1");
			if (l.SetFade && (*l.SetFade < 0.0f || *l.SetFade > 3.0f)) error(at + ": the set fade must be between 0 and 3 seconds");
			for (const ManifestClipSpeed& c : l.Clips)
			{
				const std::string cat = at + " clip '" + c.Clip + "'";
				const ManifestSet* s = FindSet(manifest, c.Set.empty() ? std::string("default") : c.Set);
				if (!s) { error(cat + ": no such locomotion set '" + c.Set + "'"); continue; }
				if (std::find(s->Clips.begin(), s->Clips.end(), c.Clip) == s->Clips.end()) warn(cat + ": the set does not use that clip");
				if (hasClip && !hasClip(c.Clip)) error(cat + ": the clip does not exist");
				if (!(c.Speed > 0.05f) || c.Speed > 20.0f) error(cat + ": the reference speed must be between 0.05 and 20 m/s");
			}
		}
		// transitions
		auto playsSomething = [&](const std::string& e, const std::string& skeleton) -> bool
		{
			if (e == "*") return true;
			if (StartsWith(e, "ability:") || StartsWith(e, "activity:"))
			{
				if (const AnimBinding* b = FindBinding(bindings, e, {}, skeleton)) if (!b->Clips.empty()) return true;
				return DefaultList(manifest, defaults, e) && !DefaultList(manifest, defaults, e)->empty();
			}
			return FindMontage(manifest, e) != nullptr;
		};
		std::set<std::string> pairs;
		for (const BindingTransition& t : bindings.Transitions)
		{
			const std::string at = "transition " + t.From + " -> " + t.To;
			if (t.From.empty() || t.To.empty()) { error(at + ": needs both ends"); continue; }
			if (!playsSomething(t.From, t.Skeleton)) error(at + ": '" + t.From + "' plays nothing (no way out)");
			if (!playsSomething(t.To, t.Skeleton)) error(at + ": '" + t.To + "' plays nothing (it goes nowhere)");
			if (t.From == t.To) warn(at + ": goes from an end to itself");
			if (!(t.Duration >= 0.0f)) error(at + ": the duration cannot be negative");
			else if (t.Duration > 3.0f) warn(at + ": a blend longer than 3 s");
			if (!pairs.insert(t.From + ">" + t.To + "|" + Lower(t.Skeleton)).second) warn(at + ": repeated (the last one wins)");
		}
		return issues;
	}

	std::vector<std::string> DescribeIssues(const std::vector<BindingIssue>& issues)
	{
		std::vector<std::string> out;
		for (const BindingIssue& i : issues) out.push_back((i.Severity == BindingIssue::Level::Error ? "error: " : "warning: ") + i.Text);
		return out;
	}
}
