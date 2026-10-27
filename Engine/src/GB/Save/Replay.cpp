#include "GB/Save/Replay.h"
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace GB::Save
{
	namespace
	{
		using Json = nlohmann::ordered_json;

		uint64_t ParseHex(const std::string& s) { return std::strtoull(s.c_str(), nullptr, 16); }

		Json DigestJson(const Digest& d)
		{
			Json parts = Json::object();
			for (const auto& [k, v] : d.Parts) parts[k] = DigestToString(v);
			return Json{ { "total", DigestToString(d.Total) }, { "parts", parts } };
		}
	}

	std::vector<std::string> SplitCommand(const std::string& line)
	{
		std::vector<std::string> out;
		std::string cur;
		bool quoted = false, any = false;
		for (char c : line)
		{
			if (c == '"') { quoted = !quoted; any = true; continue; }
			if (!quoted && (c == ' ' || c == '\t'))
			{
				if (any) { out.push_back(cur); cur.clear(); any = false; }
				continue;
			}
			cur += c;
			any = true;
		}
		if (any) out.push_back(cur);
		return out;
	}

	std::string ReplayToString(const Replay& replay)
	{
		Json root;
		root["replay"] = replay.Format;
		root["ticks"] = replay.Ticks;
		Json events = Json::array();
		for (const ReplayEvent& e : replay.Events) events.push_back(Json{ { "tick", e.Tick }, { "command", e.Command } });
		root["events"] = std::move(events);
		if (replay.HasFinal) root["final"] = DigestJson(replay.Final);
		root["start"] = Json::parse(SaveGameToString(replay.Start));
		return root.dump(2) + "\n";
	}

	Result ParseReplay(const std::string& text, Replay& out)
	{
		const Json root = Json::parse(text, nullptr, false);
		if (root.is_discarded() || !root.is_object() || !root.contains("replay")) return Result::Fail("not a replay file");
		if (root["replay"].get<int>() != 1) return Result::Fail("unsupported replay format");
		Replay r;
		try
		{
			r.Ticks = root.value("ticks", 0ull);
			for (const Json& e : root.at("events")) r.Events.push_back({ e.at("tick").get<uint64_t>(), e.at("command").get<std::string>() });
			if (auto it = root.find("final"); it != root.end())
			{
				r.HasFinal = true;
				r.Final.Total = ParseHex(it->at("total").get<std::string>());
				for (auto kv = it->at("parts").begin(); kv != it->at("parts").end(); ++kv) r.Final.Parts[kv.key()] = ParseHex(kv.value().get<std::string>());
			}
		}
		catch (const std::exception& ex)
		{
			return Result::Fail(std::string("bad replay: ") + ex.what());
		}
		Result s = ParseSaveGame(root.at("start").dump(), r.Start);
		if (!s.Ok) return Result::Fail("the start state of the replay: " + s.Error);
		out = std::move(r);
		return s;
	}

	Result ReplayRecorder::Begin(const ISimulation& sim)
	{
		CaptureResult c = CaptureGame(sim);
		m_Replay = Replay();
		m_Replay.Start = std::move(c.Game);
		m_StartTick = sim.Clock().Tick;
		m_Active = true;
		Result r;
		r.Warnings = std::move(c.Warnings);
		return r;
	}

	void ReplayRecorder::Record(uint64_t tick, const std::string& command)
	{
		if (m_Active) m_Replay.Events.push_back({ tick, command });
	}

	Replay ReplayRecorder::End(const ISimulation& sim)
	{
		m_Replay.Ticks = sim.Clock().Tick - m_StartTick;
		m_Replay.Final = ComputeDigest(sim);
		m_Replay.HasFinal = true;
		m_Active = false;
		Replay out = std::move(m_Replay);
		m_Replay = Replay();
		return out;
	}

	ReplayRun PlayReplay(ISimulation& sim, const Replay& replay)
	{
		ReplayRun run;
		run.Status = ApplyGame(sim, replay.Start);
		if (!run.Status.Ok) return run;
		const uint64_t t0 = sim.Clock().Tick;
		size_t next = 0;
		auto issue = [&](uint64_t upTo)
		{
			while (next < replay.Events.size() && replay.Events[next].Tick <= upTo)
			{
				std::string message;
				if (!sim.Command(replay.Events[next].Command, message)) ++run.FailedCommands;
				++next;
			}
		};
		for (uint64_t i = 0; i < replay.Ticks; ++i)
		{
			issue(t0 + i);
			sim.Step();
			++run.TicksRun;
		}
		issue(t0 + replay.Ticks);
		run.Actual = ComputeDigest(sim);
		if (replay.HasFinal)
		{
			run.Differing = DifferingParts(replay.Final, run.Actual);
			run.Matches = run.Differing.empty() && replay.Final.Total == run.Actual.Total;
		}
		return run;
	}
}
