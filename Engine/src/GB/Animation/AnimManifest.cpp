#include "GB/Animation/AnimManifest.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <set>

namespace GB
{
	namespace
	{
		using Json = nlohmann::json;

		Json StringList(const std::vector<std::string>& v)
		{
			Json list = Json::array();
			for (const std::string& s : v) list.push_back(s);
			return list;
		}

		std::vector<std::string> ReadStrings(const Json& j, const char* key)
		{
			std::vector<std::string> out;
			const auto it = j.find(key);
			if (it != j.end() && it->is_array())
				for (const Json& e : *it)
					if (e.is_string()) out.push_back(e.get<std::string>());
			return out;
		}

		Json SetToJson(const ManifestSet& s, bool named)
		{
			Json j;
			if (named) j["name"] = s.Name;
			if (!s.Pack.empty()) j["pack"] = s.Pack;
			if (!s.Note.empty()) j["note"] = s.Note;
			j["clips"] = StringList(s.Clips);
			j["turns"] = StringList(s.Turns);
			return j;
		}

		ManifestSet SetFromJson(const Json& j)
		{
			ManifestSet s;
			s.Name = j.value("name", "");
			s.Pack = j.value("pack", "");
			s.Note = j.value("note", "");
			s.Clips = ReadStrings(j, "clips");
			s.Turns = ReadStrings(j, "turns");
			return s;
		}

		Json UsesToJson(const std::vector<ManifestUse>& uses)
		{
			Json list = Json::array();
			for (const ManifestUse& u : uses)
			{
				Json j;
				j["name"] = u.Name;
				j["montages"] = StringList(u.Montages);
				if (!u.Set.empty()) j["set"] = u.Set;
				if (u.Loop) j["loop"] = true;
				if (!u.Note.empty()) j["note"] = u.Note;
				list.push_back(std::move(j));
			}
			return list;
		}

		std::vector<ManifestUse> UsesFromJson(const Json& root, const char* key)
		{
			std::vector<ManifestUse> out;
			const auto it = root.find(key);
			if (it == root.end() || !it->is_array()) return out;
			for (const Json& j : *it)
			{
				if (!j.is_object()) continue;
				ManifestUse u;
				u.Name = j.value("name", "");
				u.Montages = ReadStrings(j, "montages");
				u.Set = j.value("set", "");
				u.Loop = j.value("loop", false);
				u.Note = j.value("note", "");
				out.push_back(std::move(u));
			}
			return out;
		}
	}

	const ManifestUse* FindUse(const std::vector<ManifestUse>& uses, const std::string& name)
	{
		for (const ManifestUse& u : uses)
			if (u.Name == name) return &u;
		return nullptr;
	}

	float StageRate(const AnimManifest& manifest, const std::string& stage)
	{
		for (const ManifestStage& s : manifest.Stages)
			if (s.Name == stage) return s.Rate;
		return 1.0f;
	}

	AnimManifest MixamoManifest()
	{
		AnimManifest m;
		m.Slots.push_back({ "UpperBody", "mixamorig:Spine" });
		m.Free.Name = AnimInstance::kDefaultSet;
		m.Free.Clips = { "idle", "walking", "standard run", "left strafe walking", "right strafe walking", "left strafe", "right strafe" };
		m.Free.Turns = { "left turn 90", "right turn 90" };
		m.Free.Pack = "locomotion";
		for (const LocomotionSetDesc& d : MixamoSets())
		{
			ManifestSet s;
			s.Name = d.Name;
			s.Clips = d.Clips;
			if (d.Name == "box") { s.Turns = { "box turn", "box turn (2)" }; s.Pack = "farming"; }
			else if (d.Name == "holding") { s.Turns = { "holding turn left", "holding turn right" }; s.Pack = "farming"; }
			else if (d.Name == "wheelbarrow") s.Pack = "farming";
			else if (d.Name == "injured") { s.Turns = { "injured turn left", "injured turn right" }; s.Pack = "injured"; }
			m.Sets.push_back(std::move(s));
		}
		m.Montages = MixamoMontages();
		return m;
	}

	std::string AnimManifestToJson(const AnimManifest& manifest)
	{
		Json root;
		root["version"] = manifest.Version;
		root["air"] = manifest.AirClip;
		Json slots = Json::array();
		for (const ManifestSlot& s : manifest.Slots) slots.push_back({ { "name", s.Name }, { "rootBone", s.RootBone } });
		root["slots"] = slots;
		root["free"] = SetToJson(manifest.Free, false);
		Json sets = Json::array();
		for (const ManifestSet& s : manifest.Sets) sets.push_back(SetToJson(s, true));
		root["sets"] = sets;
		// the montages are written by the montage code itself, so the two files never disagree about a field
		root["montages"] = Json::parse(MontagesToJson(manifest.Montages))["montages"];
		Json packs = Json::array();
		for (const ManifestPack& p : manifest.Packs) packs.push_back({ { "id", p.Id }, { "description", p.Description }, { "clips", StringList(p.Clips) } });
		root["packs"] = packs;
		// (the uses and stages only when there are some: a manifest without them writes the text it always wrote)
		if (!manifest.Abilities.empty()) root["abilities"] = UsesToJson(manifest.Abilities);
		if (!manifest.Activities.empty()) root["activities"] = UsesToJson(manifest.Activities);
		if (!manifest.Stages.empty())
		{
			Json stages = Json::array();
			for (const ManifestStage& st : manifest.Stages) stages.push_back({ { "name", st.Name }, { "rate", st.Rate } });
			root["stages"] = stages;
		}
		if (!manifest.Locomotion.Empty())
		{
			const ManifestLocomotion& l = manifest.Locomotion;
			Json loco = Json::object();
			if (l.MinSpeedScale) loco["minSpeedScale"] = *l.MinSpeedScale;
			if (l.MaxSpeedScale) loco["maxSpeedScale"] = *l.MaxSpeedScale;
			if (l.VelocitySmoothing) loco["velocitySmoothing"] = *l.VelocitySmoothing;
			if (l.MinGaitWeight) loco["minGaitWeight"] = *l.MinGaitWeight;
			if (l.SetFade) loco["setFade"] = *l.SetFade;
			Json clips = Json::array();
			for (const ManifestClipSpeed& c : l.Clips) clips.push_back({ { "set", c.Set }, { "clip", c.Clip }, { "speed", c.Speed } });
			if (!l.Clips.empty()) loco["clips"] = clips;
			root["locomotion"] = loco;
		}
		if (!manifest.Transitions.empty())
		{
			Json trs = Json::array();
			for (const MontageTransition& t : manifest.Transitions) trs.push_back({ { "from", t.From }, { "to", t.To }, { "duration", t.Duration }, { "curve", BlendCurveName(t.Curve) } });
			root["transitions"] = trs;
		}
		return root.dump(2) + "\n";
	}

	bool AnimManifestFromJson(const std::string& text, AnimManifest& out, std::string* error)
	{
		auto fail = [&](const std::string& why) { if (error) *error = why; return false; };
		const Json root = Json::parse(text, nullptr, false);
		if (root.is_discarded() || !root.is_object()) return fail("the manifest is not valid JSON");
		if (root.value("version", 0) != 1) return fail("unknown manifest version " + std::to_string(root.value("version", 0)));
		AnimManifest m;
		m.AirClip = root.value("air", "jump");
		if (const auto it = root.find("slots"); it != root.end() && it->is_array())
			for (const Json& s : *it) m.Slots.push_back({ s.value("name", ""), s.value("rootBone", "") });
		if (const auto it = root.find("free"); it != root.end() && it->is_object())
		{
			m.Free = SetFromJson(*it);
			m.Free.Name = AnimInstance::kDefaultSet;
		}
		if (const auto it = root.find("sets"); it != root.end() && it->is_array())
			for (const Json& s : *it) m.Sets.push_back(SetFromJson(s));
		if (const auto it = root.find("montages"); it != root.end() && it->is_array())
		{
			Json wrapper;
			wrapper["version"] = 1;
			wrapper["montages"] = *it;
			std::string why;
			if (!MontagesFromJson(wrapper.dump(), m.Montages, &why)) return fail("montages: " + why);
		}
		if (const auto it = root.find("packs"); it != root.end() && it->is_array())
			for (const Json& p : *it) m.Packs.push_back({ p.value("id", ""), p.value("description", ""), ReadStrings(p, "clips") });
		m.Abilities = UsesFromJson(root, "abilities");
		m.Activities = UsesFromJson(root, "activities");
		if (const auto it = root.find("stages"); it != root.end() && it->is_array())
			for (const Json& st : *it)
				if (st.is_object()) m.Stages.push_back({ st.value("name", ""), st.value("rate", 1.0f) });
		if (const auto it = root.find("locomotion"); it != root.end() && it->is_object())
		{
			auto opt = [&](const char* key) { std::optional<float> v; if (it->contains(key) && (*it)[key].is_number()) v = (*it)[key].get<float>(); return v; };
			m.Locomotion.MinSpeedScale = opt("minSpeedScale");
			m.Locomotion.MaxSpeedScale = opt("maxSpeedScale");
			m.Locomotion.VelocitySmoothing = opt("velocitySmoothing");
			m.Locomotion.MinGaitWeight = opt("minGaitWeight");
			m.Locomotion.SetFade = opt("setFade");
			if (it->contains("clips") && (*it)["clips"].is_array())
				for (const Json& c : (*it)["clips"])
					if (c.is_object()) m.Locomotion.Clips.push_back({ c.value("set", ""), c.value("clip", ""), c.value("speed", 0.0f) });
		}
		if (const auto it = root.find("transitions"); it != root.end() && it->is_array())
			for (const Json& t : *it)
				if (t.is_object()) m.Transitions.push_back({ t.value("from", ""), t.value("to", ""), t.value("duration", 0.2f), BlendCurveFromName(t.value("curve", "smooth")) });
		out = std::move(m);
		return true;
	}

	ManifestReport ApplyManifest(const AnimManifest& manifest, const Model& model, AnimInstance& anim)
	{
		ManifestReport report;
		std::set<std::string> used;
		auto has = [&](const std::string& clip) { return model.FindClip(clip) >= 0; };
		auto note = [&](const std::string& what, const std::string& clip) { report.Missing.push_back(what + ": " + clip); };

		for (const ManifestSlot& s : manifest.Slots)
			if (!s.Name.empty()) anim.Montages().AddSlot(s.Name, s.RootBone);

		// the default set: its clips go straight into the blender that always exists
		int free = 0;
		for (const std::string& clip : manifest.Free.Clips)
		{
			used.insert(clip);
			if (!has(clip)) { note("free", clip); continue; }
			free += anim.Default().AddClip(clip) ? 1 : 0;
		}
		if (free > 0) report.Sets.push_back(AnimInstance::kDefaultSet);
		for (const std::string& clip : manifest.Free.Turns)
		{
			used.insert(clip);
			if (!has(clip)) { note("free turn", clip); continue; }
			report.TurnClips += anim.Turn().AddClip("", clip) ? 1 : 0;
		}

		for (const ManifestSet& set : manifest.Sets)
		{
			LocomotionSetDesc desc;
			desc.Name = set.Name;
			for (const std::string& clip : set.Clips)
			{
				used.insert(clip);
				if (has(clip)) desc.Clips.push_back(clip); else note("set " + set.Name, clip);
			}
			const bool registered = !desc.Clips.empty() && anim.AddSet(desc);
			if (registered) report.Sets.push_back(set.Name);
			for (const std::string& clip : set.Turns)
			{
				used.insert(clip);
				if (!has(clip)) { note("turn " + set.Name, clip); continue; }
				report.TurnClips += anim.Turn().AddClip(set.Name, clip) ? 1 : 0;
			}
		}

		if (!manifest.AirClip.empty())
		{
			used.insert(manifest.AirClip);
			if (has(manifest.AirClip)) report.Air = anim.Air().SetClip(manifest.AirClip); else note("air", manifest.AirClip);
		}
		for (const MontageDesc& m : manifest.Montages)
		{
			used.insert(m.Clip);
			if (!has(m.Clip)) { note("montage " + m.Name, m.Clip); continue; }
			report.Montages += anim.Montages().Add(m) ? 1 : 0;
		}
		anim.Montages().SetTransitions(manifest.Transitions);
		// the tuning of the blend space (animation bindings): the limits and smoothing for every set, then where each named clip sits on the speed axis
		{
			const ManifestLocomotion& l = manifest.Locomotion;
			LocomotionOptions o = anim.Options();
			if (l.MinSpeedScale) o.MinSpeedScale = *l.MinSpeedScale;
			if (l.MaxSpeedScale) o.MaxSpeedScale = *l.MaxSpeedScale;
			if (l.VelocitySmoothing) o.VelocitySmoothing = *l.VelocitySmoothing;
			if (l.MinGaitWeight) o.MinGaitWeight = *l.MinGaitWeight;
			if (l.MinSpeedScale || l.MaxSpeedScale || l.VelocitySmoothing || l.MinGaitWeight) anim.SetOptions(o);
			if (l.SetFade) anim.SetFadeOverride(*l.SetFade);
			for (const ManifestClipSpeed& c : l.Clips)
				if (!anim.SetClipSpeed(c.Set, c.Clip, c.Speed)) note("locomotion speed of set " + (c.Set.empty() ? std::string("default") : c.Set), c.Clip);
		}
		for (const AnimationClip& c : model.GetClips())
			if (!used.count(c.Name)) report.Unused.push_back(c.Name);
		return report;
	}

	std::vector<std::string> ValidateManifest(const AnimManifest& manifest)
	{
		std::vector<std::string> problems;
		std::set<std::string> seen;
		auto unique = [&](const std::string& kind, const std::string& name)
		{
			if (name.empty()) { problems.push_back(kind + " without a name"); return; }
			if (!seen.insert(kind + ":" + name).second) problems.push_back(kind + " '" + name + "' is repeated");
		};
		for (const ManifestSlot& s : manifest.Slots)
		{
			unique("slot", s.Name);
			if (s.RootBone.empty()) problems.push_back("slot '" + s.Name + "' has no root bone");
		}
		if (manifest.Free.Clips.size() < 2) problems.push_back("the free set needs at least two clips");
		for (const ManifestSet& s : manifest.Sets)
		{
			unique("set", s.Name);
			if (s.Name == AnimInstance::kDefaultSet || s.Name == "free") problems.push_back("set '" + s.Name + "' is the free set, which has its own \"free\" entry, not one in \"sets\"");
			if (s.Clips.size() < 2) problems.push_back("set '" + s.Name + "' needs at least two clips (an idle and a gait)");
			for (const std::string& c : s.Clips) if (c.empty()) problems.push_back("set '" + s.Name + "' has an empty clip name");
		}
		for (const MontageDesc& m : manifest.Montages)
			if (m.TrimStart < 0.0f || m.TrimEnd > 1.0f || m.TrimEnd <= m.TrimStart) problems.push_back("montage '" + m.Name + "' has a trim that is not inside 0..1 or is empty");
		std::set<std::string> slots;
		slots.insert(MontagePlayer::kFullBody);
		for (const ManifestSlot& s : manifest.Slots) slots.insert(s.Name);
		for (const MontageDesc& m : manifest.Montages)
		{
			unique("montage", m.Name);
			if (m.Clip.empty()) problems.push_back("montage '" + m.Name + "' has no clip");
			if (!slots.count(m.Slot)) problems.push_back("montage '" + m.Name + "' uses the unknown slot '" + m.Slot + "'");
			for (const MontageNotify& n : m.Notifies)
				if (n.Time < 0.0f || n.Time > 1.0f) problems.push_back("montage '" + m.Name + "': notify '" + n.Name + "' is out of 0..1");
		}
		for (const ManifestPack& p : manifest.Packs) unique("pack", p.Id);
		std::set<std::string> montages, sets;
		for (const MontageDesc& m : manifest.Montages) montages.insert(m.Name);
		for (const ManifestSet& s : manifest.Sets) sets.insert(s.Name);
		sets.insert(AnimInstance::kDefaultSet);
		sets.insert("free");
		auto uses = [&](const char* kind, const std::vector<ManifestUse>& list)
		{
			for (const ManifestUse& u : list)
			{
				unique(kind, u.Name);
				if (u.Montages.empty() && u.Set.empty()) problems.push_back(std::string(kind) + " '" + u.Name + "' plays nothing (no montage and no set)");
				for (const std::string& m : u.Montages)
					if (!montages.count(m)) problems.push_back(std::string(kind) + " '" + u.Name + "' plays the unknown montage '" + m + "'");
				if (!u.Set.empty() && !sets.count(u.Set)) problems.push_back(std::string(kind) + " '" + u.Name + "' walks with the unknown set '" + u.Set + "'");
			}
		};
		uses("ability", manifest.Abilities);
		uses("activity", manifest.Activities);
		for (const ManifestStage& st : manifest.Stages)
		{
			unique("stage", st.Name);
			if (!(st.Rate > 0.0f)) problems.push_back("stage '" + st.Name + "' has a rate that is not positive");
		}
		return problems;
	}

	std::vector<std::string> UnusedClips(const AnimManifest& manifest, const Model& model)
	{
		std::set<std::string> used(manifest.Free.Clips.begin(), manifest.Free.Clips.end());
		used.insert(manifest.Free.Turns.begin(), manifest.Free.Turns.end());
		for (const ManifestSet& s : manifest.Sets)
		{
			used.insert(s.Clips.begin(), s.Clips.end());
			used.insert(s.Turns.begin(), s.Turns.end());
		}
		used.insert(manifest.AirClip);
		for (const MontageDesc& m : manifest.Montages) used.insert(m.Clip);
		std::vector<std::string> out;
		for (const AnimationClip& c : model.GetClips())
			if (!used.count(c.Name)) out.push_back(c.Name);
		return out;
	}
}
