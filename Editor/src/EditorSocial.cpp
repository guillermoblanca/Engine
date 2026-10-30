// The editor's side of the social life (docs/VIDA_SOCIAL.md): the `social`, `rite`, `belief` and `hunt` commands, the game let loose around a
// new camp (social.hunting.wildlife) and the "Social life" section of the Tribe panel. The state lives in the tribe (Tribe::Social) and is saved
// with it; nothing here is simulation.
#include "EditorApp.h"
#include <imgui.h>
#include <cmath>

namespace GB::Editor
{
	namespace
	{
		std::string OneLine(std::string s)
		{
			while (!s.empty() && s.back() == '\n') s.pop_back();
			for (char& c : s) if (c == '\n') c = '|';   // one line: the console shows the first line of a message
			return s;
		}
	}

	void EditorApp::RegisterSocialCommands()
	{
		CommandRunner& c = m_Commands;
		c.Register("social", "social status [name]: the social life of the clan (docs/VIDA_SOCIAL.md): moods, grief, talks (chat, story, gossip, teach, console, settle, greet, share, groom, play) and meals; with a name, that member's", [this](const std::vector<std::string>& a) -> CommandResult
		{
			const Tribe& t = ActiveTribe();
			if (!t.Config.Social.On) return { false, "the clan has no social rules (load the era1 pack: social.json)" };
			if (a.size() > 1) return { true, OneLine(t.SocialReport(a[1])) };
			if (!a.empty() && a[0] != "status") return { true, OneLine(t.SocialReport(a[0])) };
			return { true, OneLine(t.SocialReport()) };
		});
		c.Register("rite", "rite list | start <rite> [subject]: the rites of the clan: the one under way, the agenda, the last held and those of the rules; start calls one now (naming, coming_of_age, pairing, funeral, solstice, spring_feast, harvest_feast, first_hunt, healing, tradition, appeasement)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			Tribe& t = ActiveTribe();
			if (a.empty() || a[0] == "list" || a[0] == "status") return { true, OneLine(t.RiteReport()) };
			if (a[0] == "start" && a.size() > 1)
			{
				std::string why;
				const std::string subject = a.size() > 2 ? a[2] : std::string();
				if (!t.StartRite(a[1], subject, &why)) return { false, "cannot call " + a[1] + ": " + why };
				return { true, "the clan is called to " + a[1] + (subject.empty() ? std::string() : " for " + subject) };
			}
			return { false, "usage: rite list | start <rite> [subject]" };
		});
		c.Register("belief", "belief status | omen <event>: the beliefs of the clan (animism, ancestors, sky), the dominant one, the sacred place, the taboos kept, the omens read and the shaman; omen reads an event as the clan would (storm, eclipse, harvest, big_kill, two_deaths)", [this](const std::vector<std::string>& a) -> CommandResult
		{
			Tribe& t = ActiveTribe();
			if (a.size() > 1 && a[0] == "omen") { t.Omen(a[1]); return { true, "the clan reads " + a[1] + ": " + OneLine(t.BeliefReport()) }; }
			std::string s;
			for (const auto& clan : m_Clans.All()) if (clan->Folk && clan->Folk != &t && !clan->Folk->Members().empty()) s += clan->Folk->BeliefReport();
			return { true, OneLine(t.BeliefReport() + s) };
		});
		c.Register("hunt", "hunt status | wildlife: what the clan has hunted, the game near its camp, its snares and pit traps and the taboo of the sacred place; wildlife lets loose the game of the rules around the camp", [this](const std::vector<std::string>& a) -> CommandResult
		{
			Tribe& t = ActiveTribe();
			if (!a.empty() && a[0] == "wildlife") return { true, SpawnWildlife(t) };
			return { true, OneLine(t.HuntReport() + m_Ecology.Describe()) };
		});
	}

	// The game of social.hunting.wildlife around a camp (on a ring 15-35 m out, one herd per species), as `ecology spawn` would.
	std::string EditorApp::SpawnWildlife(const Tribe& t)
	{
		std::string out;
		int k = 0;
		for (const auto& [species, n] : t.Config.Social.Hunting.Wildlife)
		{
			if (n <= 0 || !m_Ecology.Species.count(species)) continue;
			const float angle = 1.3f + 2.3999632f * static_cast<float>(k++);
			const float radius = 18.0f + 6.0f * static_cast<float>(k % 3);
			glm::vec3 at(t.Config.Economy.Camp.x + std::cos(angle) * radius, 0.0f, t.Config.Economy.Camp.y + std::sin(angle) * radius);
			at.y = m_World.IsBuilt() ? m_World.GetTerrain().GetHeight(at.x, at.z) : t.Config.Economy.CampY;
			const int made = m_Ecology.Spawn(at, n, species);
			if (made > 0) out += (out.empty() ? "" : ", ") + std::to_string(made) + " " + m_Ecology.Species.at(species).Plural;
		}
		if (!out.empty()) { m_SeasonVisuals = true; m_SceneEditor.Commit(); }
		return out.empty() ? std::string("no wildlife in the rules") : "game around the camp: " + out;
	}

	void EditorApp::DrawSocialSection()
	{
		const Tribe& t = ActiveTribe();
		if (!t.Config.Social.On || !ImGui::CollapsingHeader("Social life")) return;
		auto lines = [](const std::string& text)
		{
			size_t from = 0;
			while (from < text.size())
			{
				const size_t to = text.find('\n', from);
				const std::string line = text.substr(from, to == std::string::npos ? std::string::npos : to - from);
				if (!line.empty()) ImGui::TextWrapped("%s", line.c_str());
				if (to == std::string::npos) break;
				from = to + 1;
			}
		};
		lines(t.SocialReport());
		ImGui::Separator();
		lines(t.RiteReport());
		if (ImGui::SmallButton("Call a festival")) m_UiCommands.push_back("rite start spring_feast");
		ImGui::Separator();
		lines(t.BeliefReport());
		ImGui::Separator();
		lines(t.HuntReport());
		if (ImGui::SmallButton("Let the game loose")) m_UiCommands.push_back("hunt wildlife");
	}
}
