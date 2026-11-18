#pragma once
// T271 (MODO_COLONIA §4.2, C1-b): the colony's selection of several people and its right-click radial menu. The state lives here; the code is
// EditorApp's (RadialMenu.cpp). Only the god's screen of the colony has them (EditorApp::ColonyScreen): never the editor, never the player's game.
// The radial only queues `game order`, `game designate`, `game site`, `director ...` and `observer ...` commands, as every button of the game does;
// `colony select` and `colony radial` drive the same code without a mouse (the tests and the captures).
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace GB::Editor
{
	// One bubble of the radial: an id (the console's word), the icon of the atlas, its short label (English: Lang translates it), and the commands it
	// queues. A bubble that applies but cannot be used now is Off, with why (English, translated) in its tooltip.
	struct RadialOption
	{
		std::string Id, Icon, Label, Tip;
		std::vector<std::string> Commands;
		bool Off = false;
		std::string Why;
		// the people selected who cannot take the order now (C-3: too hungry, thirsty, tired or low; fighting, fleeing, away), with why (English:
		// a line of the clan's log, es.json's phrases translate it); when nobody of the selection can, the bubble is Off
		std::vector<std::pair<std::string, std::string>> Unfit;
		bool Arrow = false;          // drawn with a small arrow over its icon (the priorities: Up, or down when Down)
		bool Down = false;
	};

	// What the radial was opened over: the ground (a point), a resource node, a person of the player's clan, a person of another clan, an animal,
	// a site of the player's clan or a building (a finished one, or another clan's site).
	struct RadialTarget
	{
		enum class Kind { None, Ground, Node, Own, Stranger, Animal, Site, Building };
		Kind What = Kind::None;
		std::string Name;            // the scene's name ("" on the ground)
		glm::vec3 At{ 0.0f };        // where it stands (the menu's centre is drawn over it)
	};

	struct ColonyUiState
	{
		std::vector<std::string> Selection;   // the people of the player's clan selected (click, Shift+click, a drag's box): `sel` of `game order`
		// a left drag over the view draws a box (the camera does not turn meanwhile: Alt+drag still turns it)
		bool Boxing = false;
		glm::vec2 BoxFrom{ 0.0f }, BoxTo{ 0.0f };
		// the right button: where it went down (a click opens the radial, a drag pans the camera)
		bool RightDown = false;
		glm::vec2 RightAt{ 0.0f };
		// the radial open now (Open false: none) and the bubble a script holds hovered (`colony radial hover <id>`, for the captures)
		bool Open = false;
		RadialTarget Target;
		std::string Hover;
		// the cut orders of the player's clan already told as news (ColonyOrders::Notices: the last 32, oldest first; the key of the newest told) and
		// the news made of them (English lines of the clan's log, with the real time they came)
		std::string NoticeKey;
		struct Note { std::string Text; double At = 0.0; };
		std::vector<Note> Notes;
		// T308 (ColonyTabs.cpp): the panel open over the god's bar ("work", "stock"; "": none), the categories unfolded into their jobs in the Work
		// table, the thing and the number a new stock goal is being made of, and where the panel was drawn
		std::string Tab;
		std::set<std::string> Unfolded;
		std::string StockPick;
		int StockCount = 5;
		bool OpenPicker = false;   // `colony stock picker`: the picker of things opens on the next frame, as its click opens it
		struct Box { float X0 = 0.0f, Y0 = 0.0f, X1 = 0.0f, Y1 = 0.0f; };
		Box TabBox;
	};
}
