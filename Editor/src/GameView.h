#pragma once
#include "Commands.h"
#include "GB/Game/ActorWorld.h"
#include "GB/Game/GameCamera.h"
#include "GB/Renderer/Camera.h"
#include <string>
#include <vector>

namespace GB::Editor
{
	class PlayerMode;
	class WorldInstance;

	// The game cameras (GB/Game/GameCamera.h) in the editor: `camera game follow|overview|observe|off`. While one is on it owns the view (the
	// orbit camera and the player's own camera are left alone underneath, so `camera game off` gives them back as they were). Following the
	// player, the player's camera still decides the angles (WASD walk relative to it); following anyone else, or in the other modes, the
	// keys go to the game camera (see ReadGameCameraInput) and, outside follow, the player's character gets no keys.
	class GameView
	{
	public:
		GameCameraParams Params;   // the "cameras" content section reads into this (assets/data/cameras.json through era1)

		bool Active() const { return m_Active; }
		GameCameraMode Mode() const { return m_Rig.Mode(); }
		// May WASD walk the player's character? Not while the overview pans with them, nor while the cinematic camera runs.
		bool CharacterKeys() const { return !m_Active || m_Rig.Mode() == GameCameraMode::Follow; }

		// `camera game <args>`; `current` is the view on screen now (the blend starts from it).
		CommandResult Command(const std::vector<std::string>& args, const ActorWorld& actors, const PlayerMode& player, WorldInstance& world, const Camera& current);
		// One frame: the subjects from the actors, the input (when `acceptInput`), the rig; writes `camera`. Nothing when not active.
		void Update(float dt, bool acceptInput, const ActorWorld& actors, const PlayerMode& player, WorldInstance& world, Camera& camera);
		glm::vec3 Focus() const { return m_Rig.Focus(); }
		std::string Describe() const;

	private:
		void Configure(WorldInstance& world);
		bool Subjects(const ActorWorld& actors, const PlayerMode& player);   // false when the followed / observed actors are gone

		GameCameraRig m_Rig;
		bool m_Active = false;
		std::string m_Follow;                // "" = whoever the player controls
		std::vector<std::string> m_Observe;  // empty = every actor
	};
}
