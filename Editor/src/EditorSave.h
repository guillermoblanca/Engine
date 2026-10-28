#pragma once
#include "GB/Save/Replay.h"
#include "GB/Save/SaveFiles.h"

namespace GB::Editor
{
	class EditorApp;

	// The editor as a saved-game host (see GB/Save): what to read from the session, and how a loaded game is put back in place.
	//
	// The generated trees and rocks are entities of the editor's scene, but their ids depend on what was done before (a garden built first,
	// a world generated twice), so a save does not use them. The save sees the scene in two parts: the generated objects renumbered the way the
	// seed numbers them (Entities(), diffed against the baseline) and everything else (a "user-entities" section, numbered from 1).
	class EditorSaveHost : public Save::ISimulation
	{
	public:
		explicit EditorSaveHost(EditorApp& app) : m_App(app) {}
		// A system with characters (the player, NPCs) lends its state through this; without one, no actors are saved.
		void SetActorSource(Save::IActorStateSource* source) { m_Actors = source; }
		// The baseline is cached by its WorldParams; the resource node data it plans the wild nodes with is not in them, so a change of data forgets it.
		void ForgetBaseline() { m_HaveBaseline = false; }

		// ISaveHost
		const EntityWorld& Entities() const override;
		Save::WorldParams World() const override;
		bool BuildBaseline(const Save::WorldParams& world, EntityWorld& out, std::string& error) const override;
		Save::ClockState Clock() const override;
		Save::WeatherState Weather() const override;
		const GameplayWorld* Gameplay() const override;
		std::vector<Save::ActorState> Actors() const override;
		std::vector<Save::SectionBlob> Sections() const override;
		std::string Validate(const Save::LoadedGame& game) override;
		void Commit(Save::LoadedGame&& game) override;
		// ISimulation
		void Step() override;
		bool Command(const std::string& line, std::string& message) override;

	private:
		// Splits the live scene: `env` has the generated objects with the baseline's ids, `user` everything else renumbered from 1.
		void Split(const EntityWorld& baseline, EntityWorld& env, EntityWorld& user) const;
		// Env + user section -> the scene as it will be.
		std::string Assemble(const Save::LoadedGame& game, EntityWorld& out) const;

		EditorApp& m_App;
		Save::IActorStateSource* m_Actors = nullptr;
		mutable EntityWorld m_View;
		mutable EntityWorld m_Baseline;
		mutable Save::WorldParams m_BaselineKey;
		mutable bool m_HaveBaseline = false;
	};
}
