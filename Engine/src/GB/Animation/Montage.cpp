#include "GB/Animation/Montage.h"
#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace GB
{
	MontagePlayer::MontagePlayer(const Model& model, const ClipAnalysisConfig& analysis)
		: m_Model(model), m_Root(model.FindNode(analysis.RootBone))
	{
		m_Slots.push_back({ kFullBody, {} });
	}

	int MontagePlayer::Find(const std::string& name) const
	{
		for (size_t i = 0; i < m_Entries.size(); ++i)
			if (m_Entries[i].Desc.Name == name) return static_cast<int>(i);
		return -1;
	}

	int MontagePlayer::FindSlot(const std::string& name) const
	{
		for (size_t i = 0; i < m_Slots.size(); ++i)
			if (m_Slots[i].Name == name) return static_cast<int>(i);
		return -1;
	}

	bool MontagePlayer::AddSlot(const std::string& name, const std::string& rootBone)
	{
		std::vector<float> mask = MakeBoneMask(m_Model, rootBone);
		if (mask.empty()) return false;
		const int existing = FindSlot(name);
		if (existing == 0) return false;   // the full body cannot be redefined
		if (existing > 0) m_Slots[static_cast<size_t>(existing)].Mask = std::move(mask);
		else m_Slots.push_back({ name, std::move(mask) });
		return true;
	}

	bool MontagePlayer::Add(const MontageDesc& desc)
	{
		const int clip = m_Model.FindClip(desc.Clip);
		const int slot = FindSlot(desc.Slot);
		if (clip < 0 || slot < 0 || desc.Name.empty()) return false;
		Entry e;
		e.Desc = desc;
		e.Clip = clip;
		e.Slot = slot;
		e.Stats = AnalyzeClip(m_Model, clip);
		e.Stats.Duration = m_Model.GetClips()[static_cast<size_t>(clip)].Duration;
		const int existing = Find(desc.Name);
		if (existing >= 0) m_Entries[static_cast<size_t>(existing)] = e;
		else m_Entries.push_back(e);
		return true;
	}

	std::vector<std::string> MontagePlayer::Names() const
	{
		std::vector<std::string> names;
		for (const Entry& e : m_Entries) names.push_back(e.Desc.Name);
		return names;
	}

	std::vector<std::string> MontagePlayer::SlotNames() const
	{
		std::vector<std::string> names;
		for (const Slot& s : m_Slots) names.push_back(s.Name);
		return names;
	}

	float MontagePlayer::Duration(const std::string& name) const
	{
		const int i = Find(name);
		if (i < 0) return 0.0f;
		const Entry& e = m_Entries[static_cast<size_t>(i)];
		return e.Stats.Duration / std::max(e.Desc.PlayRate, 1e-3f);
	}

	void MontagePlayer::Leave(Active& a, float blendOut, bool interrupted)
	{
		if (a.Leaving) return;
		a.Leaving = true;
		a.Interrupted = interrupted;
		a.LeaveBlend = std::max(blendOut >= 0.0f ? blendOut : m_Entries[static_cast<size_t>(a.Entry)].Desc.BlendOut, 1e-3f);
		if (interrupted)
			m_Pending.push_back({ MontageEvent::Kind::Interrupted, m_Entries[static_cast<size_t>(a.Entry)].Desc.Name, {} });   // delivered by the next Update
	}

	const MontageDesc* MontagePlayer::Get(const std::string& name) const
	{
		const int i = Find(name);
		return i < 0 ? nullptr : &m_Entries[static_cast<size_t>(i)].Desc;
	}

	std::vector<MontageDesc> MontagePlayer::Descs() const
	{
		std::vector<MontageDesc> out;
		for (const Entry& e : m_Entries) out.push_back(e.Desc);
		return out;
	}

	bool MontagePlayer::Remove(const std::string& name)
	{
		const int i = Find(name);
		if (i < 0) return false;
		for (size_t k = m_Active.size(); k-- > 0;)
		{
			if (m_Active[k].Entry == i) m_Active.erase(m_Active.begin() + static_cast<std::ptrdiff_t>(k));
			else if (m_Active[k].Entry > i) --m_Active[k].Entry;
		}
		m_Entries.erase(m_Entries.begin() + i);
		return true;
	}

	bool MontagePlayer::Play(const std::string& name, const std::string& section)
	{
		const int i = Find(name);
		if (i < 0) return false;
		float start = 0.0f;
		if (!section.empty())
		{
			bool found = false;
			for (const MontageSection& s : m_Entries[static_cast<size_t>(i)].Desc.Sections)
				if (s.Name == section) { start = s.Time * m_Entries[static_cast<size_t>(i)].Stats.Duration; found = true; }
			if (!found) return false;
		}
		for (Active& a : m_Active)
			if (m_Entries[static_cast<size_t>(a.Entry)].Slot == m_Entries[static_cast<size_t>(i)].Slot) Leave(a, -1.0f, true);
		Active a;
		a.Entry = i;
		a.Time = start;
		m_Active.push_back(a);
		return true;
	}

	bool MontagePlayer::JumpToSection(const std::string& name, const std::string& section)
	{
		for (Active& a : m_Active)
		{
			const Entry& e = m_Entries[static_cast<size_t>(a.Entry)];
			if (a.Leaving || e.Desc.Name != name) continue;
			for (const MontageSection& s : e.Desc.Sections)
				if (s.Name == section) { a.Time = s.Time * e.Stats.Duration; return true; }
		}
		return false;
	}

	bool MontagePlayer::Seek(const std::string& name, float seconds)
	{
		for (Active& a : m_Active)
		{
			const Entry& e = m_Entries[static_cast<size_t>(a.Entry)];
			if (a.Leaving || e.Desc.Name != name) continue;
			a.Time = std::clamp(seconds, 0.0f, e.Stats.Duration);
			a.Weight = 1.0f;   // a scrubbed pose is shown in full
			return true;
		}
		return false;
	}

	float MontagePlayer::Time(const std::string& name) const
	{
		for (const Active& a : m_Active)
			if (!a.Leaving && m_Entries[static_cast<size_t>(a.Entry)].Desc.Name == name) return a.Time;
		return -1.0f;
	}

	void MontagePlayer::Stop(const std::string& name, float blendOut)
	{
		for (Active& a : m_Active)
			if (m_Entries[static_cast<size_t>(a.Entry)].Desc.Name == name) Leave(a, blendOut, true);
	}

	void MontagePlayer::StopSlot(const std::string& slot, float blendOut)
	{
		const int s = FindSlot(slot);
		for (Active& a : m_Active)
			if (m_Entries[static_cast<size_t>(a.Entry)].Slot == s) Leave(a, blendOut, true);
	}

	bool MontagePlayer::IsPlaying(const std::string& name) const
	{
		for (const Active& a : m_Active)
			if (!a.Leaving && m_Entries[static_cast<size_t>(a.Entry)].Desc.Name == name) return true;
		return false;
	}

	float MontagePlayer::SlotWeight(const std::string& slot) const
	{
		const int s = FindSlot(slot);
		float w = 0.0f;
		for (const Active& a : m_Active)
			if (m_Entries[static_cast<size_t>(a.Entry)].Slot == s) w = std::max(w, a.Weight);
		return w;
	}

	void MontagePlayer::Update(float dt)
	{
		m_Events.clear();
		m_Events.swap(m_Pending);
		if (m_Paused) return;   // a frozen preview: nothing advances, nothing ends
		for (Active& a : m_Active)
		{
			const Entry& e = m_Entries[static_cast<size_t>(a.Entry)];
			const float duration = std::max(e.Stats.Duration, 1e-3f);
			const float before = a.Time / duration;
			a.Time += dt * e.Desc.PlayRate;
			bool wrapped = false;
			if (a.Time >= duration)
			{
				if (e.Desc.Loop && !a.Leaving) { a.Time = std::fmod(a.Time, duration); wrapped = true; }
				else a.Time = duration;
			}
			const float after = a.Time / duration;
			if (!a.Leaving)
				for (const MontageNotify& n : e.Desc.Notifies)
				{
					const bool crossed = wrapped ? (n.Time > before || n.Time <= after) : (n.Time > before && n.Time <= after);
					if (crossed) m_Events.push_back({ MontageEvent::Kind::Notify, e.Desc.Name, n.Name });
				}
			if (!a.Leaving && !e.Desc.Loop && a.Time >= duration - e.Desc.BlendOut * e.Desc.PlayRate)
				Leave(a, -1.0f, false);   // reaches its end: the blend out starts BlendOut seconds before it
			const float rate = a.Leaving ? 1.0f / a.LeaveBlend : 1.0f / std::max(e.Desc.BlendIn, 1e-3f);
			a.Weight = a.Leaving ? std::max(a.Weight - dt * rate, 0.0f) : std::min(a.Weight + dt * rate, 1.0f);
		}
		for (size_t i = 0; i < m_Active.size();)
		{
			const Active& a = m_Active[i];
			const bool done = a.Leaving && a.Weight <= 0.0f;
			if (done)
			{
				if (!a.Interrupted) m_Events.push_back({ MontageEvent::Kind::Finished, m_Entries[static_cast<size_t>(a.Entry)].Desc.Name, {} });
				m_Active.erase(m_Active.begin() + static_cast<std::ptrdiff_t>(i));
			}
			else ++i;
		}
	}

	void MontagePlayer::Apply(Pose& pose)
	{
		for (const Active& a : m_Active)
		{
			if (a.Weight <= 0.0f) continue;
			const Entry& e = m_Entries[static_cast<size_t>(a.Entry)];
			SampleClip(m_Model, e.Clip, a.Time, m_Scratch);
			if (m_Root >= 0)
			{
				const float p = e.Stats.Duration > 0.0f ? a.Time / e.Stats.Duration : 0.0f;
				BonePose& root = m_Scratch[static_cast<size_t>(m_Root)];
				root.T.x -= e.Stats.RootStart.x + (e.Desc.StripTravel ? e.Stats.RootDelta.x * p : 0.0f);
				root.T.z -= e.Stats.RootStart.z + (e.Desc.StripTravel ? e.Stats.RootDelta.z * p : 0.0f);
			}
			const float s = a.Weight * a.Weight * (3.0f - 2.0f * a.Weight);   // ease in and out
			const std::vector<float>& mask = m_Slots[static_cast<size_t>(e.Slot)].Mask;
			if (mask.empty()) BlendPoses(pose, m_Scratch, s, pose);
			else BlendPosesMasked(pose, m_Scratch, s, mask, pose);
		}
	}

	std::string MontagesToJson(const std::vector<MontageDesc>& montages)
	{
		using Json = nlohmann::json;
		Json list = Json::array();
		for (const MontageDesc& m : montages)
		{
			Json j;
			j["name"] = m.Name;
			j["clip"] = m.Clip;
			j["slot"] = m.Slot;
			j["blendIn"] = m.BlendIn;
			j["blendOut"] = m.BlendOut;
			j["playRate"] = m.PlayRate;
			j["loop"] = m.Loop;
			j["stripTravel"] = m.StripTravel;
			Json notifies = Json::array();
			for (const MontageNotify& n : m.Notifies) notifies.push_back({ { "time", n.Time }, { "name", n.Name } });
			j["notifies"] = notifies;
			Json sections = Json::array();
			for (const MontageSection& s : m.Sections) sections.push_back({ { "time", s.Time }, { "name", s.Name } });
			j["sections"] = sections;
			list.push_back(std::move(j));
		}
		Json root;
		root["version"] = 1;
		root["montages"] = std::move(list);
		return root.dump(2) + "\n";
	}

	bool MontagesFromJson(const std::string& text, std::vector<MontageDesc>& out, std::string* error)
	{
		using Json = nlohmann::json;
		auto fail = [&](const std::string& why) { if (error) *error = why; return false; };
		const Json root = Json::parse(text, nullptr, false);
		if (root.is_discarded() || !root.is_object()) return fail("invalid JSON");
		if (root.value("version", 0) != 1) return fail("unsupported montage file version");
		const auto it = root.find("montages");
		if (it == root.end() || !it->is_array()) return fail("missing 'montages'");
		std::vector<MontageDesc> result;
		try
		{
			for (const Json& j : *it)
			{
				MontageDesc m;
				m.Name = j.value("name", "");
				m.Clip = j.value("clip", "");
				if (m.Name.empty() || m.Clip.empty()) return fail("a montage needs a name and a clip");
				m.Slot = j.value("slot", "FullBody");
				m.BlendIn = j.value("blendIn", 0.25f);
				m.BlendOut = j.value("blendOut", 0.25f);
				m.PlayRate = j.value("playRate", 1.0f);
				m.Loop = j.value("loop", false);
				m.StripTravel = j.value("stripTravel", true);
				if (j.contains("notifies"))
					for (const Json& n : j["notifies"]) m.Notifies.push_back({ n.value("time", 0.0f), n.value("name", "") });
				if (j.contains("sections"))
					for (const Json& s : j["sections"]) m.Sections.push_back({ s.value("time", 0.0f), s.value("name", "") });
				result.push_back(std::move(m));
			}
		}
		catch (const Json::exception& ex)
		{
			return fail(std::string("bad value: ") + ex.what());
		}
		out = std::move(result);
		return true;
	}

	std::vector<MontageDesc> MixamoMontages()
	{
		auto make = [](const char* name, const char* clip, std::vector<MontageNotify> notifies = {}, float in = 0.3f, float out = 0.3f)
		{
			MontageDesc d;
			d.Name = name;
			d.Clip = clip;
			d.BlendIn = in;
			d.BlendOut = out;
			d.Notifies = std::move(notifies);
			return d;
		};
		return {
			make("milk_cow", "cow milking"),
			make("dig_plant", "dig and plant seeds"),
			make("plant", "plant a plant"),
			make("plant_tree", "plant tree"),
			make("pick_fruit", "pick fruit"),
			make("pick_fruit_2", "pick fruit (2)"),
			make("pick_fruit_3", "pick fruit (3)"),
			make("pull_plant", "pull plant"),
			make("pull_plant_2", "pull plant (2)"),
			make("water", "watering"),
			make("wheelbarrow_dump", "wheelbarrow dump"),
		};
	}
}
