#include "PlayerMode.h"
#include "SceneEditor.h"
#include "WorldInstance.h"
#include "GB/Game/Interaction.h"
#include "GB/Gameplay/Interactives.h"
#include "GB/Log.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace GB::Editor
{
	bool PlayerMode::IsFbx(const std::string& kind)
	{
		return kind.size() > 4 && (kind.substr(kind.size() - 4) == ".fbx" || kind.substr(kind.size() - 4) == ".FBX");
	}

	ActorHost PlayerMode::MakeHost(WorldInstance& world, const SceneEditor& edit, FootstepSounds* steps, const GameplayWorld* gameplay)
	{
		ActorHost host;
		host.World = MakeTerrainCharacterWorld(world.GetTerrain());
		host.Steps = steps;
		// Trees (trunk), rocks (a bit smaller than their bounding sphere) and anything the user gave a body.
		for (const auto& [id, entity] : edit.World().All())
		{
			if (!edit.World().IsActive(id))
				continue;
			const glm::mat4 m = edit.World().WorldMatrix(id);
			const glm::vec3 p(m[3]);
			const float scale = glm::length(glm::vec3(m[0]));
			if (entity.Prop)
			{
				glm::vec3 center;
				float radius = 0.0f;
				if (entity.Prop->Kind == PropKind::Tree)
					host.World.Obstacles.push_back({ { p.x, p.z }, 0.3f * scale });
				else if (world.PropBounds(*entity.Prop, center, radius) && radius * scale * 0.5f > 0.2f)
					host.World.Obstacles.push_back({ { p.x, p.z }, radius * scale * 0.65f });
			}
			else if (entity.Body)
			{
				const glm::vec3 h = entity.Body->HalfExtents * scale;
				host.World.Obstacles.push_back({ { p.x, p.z }, entity.Body->Shape == ColliderShape::Box ? std::max(h.x, h.z) : h.x });
			}
		}
		if (gameplay) DoorObstacles(edit.World(), *gameplay, host.World.Obstacles);   // a shut door is a wall
		const SceneEditor* scene = &edit;
		host.Interactions = [scene](const std::string& entity, const std::string& point, InteractionTarget& out, std::string* err)
		{
			return FindInteraction(scene->World(), entity, point, out, err);
		};
		return host;
	}

	bool PlayerMode::SyncNavigation(WorldInstance& world, const SceneEditor& edit, FootstepSounds* steps, std::string* error)
	{
		if (!m_Actors || !world.IsBuilt()) { if (error) *error = "generate a world first (generate temperate 5)"; return false; }
		m_Actors->SetHost(MakeHost(world, edit, steps, m_Gameplay));
		// Undergrowth by biome (NavProfile::VegetationWeight): the forest floor is slow, the reeds of the shore a little, a meadow hardly. Read once per cell
		// when the grid is built or a region is redone (BiomeAt reads the biome map baked with the world: one grid cell, H16).
		NavConfig config;
		config.MaxExpandedPerFrame = 30000;   // H16: the searches of a frame open at most this many cells in all (a fight or goals across water: long searches)
		WorldInstance* w = &world;
		config.Vegetation = [w](float x, float z)
		{
			if (!w->IsBuilt()) return 0.0f;
			switch (w->World().BiomeAt(x, z))
			{
			case Biome::Forest: return 1.0f;
			case Biome::Shore: return 0.3f;
			case Biome::Meadow: return 0.15f;
			default: return 0.0f;
			}
		};
		return m_Actors->SyncNavigation(config, error);
	}

	void PlayerMode::LoadShipped(const std::string& assetRoot)
	{
		// the manifest the assets ship (the packs' sets, turns and montages as data) replaces the built-in lists when it is there
		const std::string shipped = assetRoot + "/animation/mixamo.manifest.json";
		if (m_HasShipped && m_ShippedRoot == assetRoot) return;
		std::ifstream in(shipped);
		std::stringstream text;
		text << in.rdbuf();
		AnimManifest manifest;
		std::string why;
		if (AnimManifestFromJson(text.str(), manifest, &why) && ValidateManifest(manifest).empty())
		{
			m_Shipped = std::move(manifest);
			m_HasShipped = true;
			m_ShippedRoot = assetRoot;
		}
		else
			GB_CLIENT_WARN("Animation manifest: {}", why.empty() ? "the manifest has problems" : why);
	}

	void PlayerMode::CapturePristine()
	{
		if (m_HasPristine || !m_Gameplay) return;
		m_Pristine = AbilityMontageLists(m_Gameplay->Library);
		m_HasPristine = true;
	}

	BindingDefaults PlayerMode::Defaults() const
	{
		if (m_HasPristine) return MakeBindingDefaults(BaseManifest(), m_Pristine);
		return MakeBindingDefaults(BaseManifest(), m_Gameplay ? &m_Gameplay->Library : nullptr);
	}

	bool PlayerMode::LoadBindings(const std::string& assetRoot, std::string& report, const std::string& file)
	{
		LoadShipped(assetRoot);
		CapturePristine();
		const std::string path = file.empty() ? BindingsFile(assetRoot) : file;
		AnimBindings loaded;
		std::string error;
		bool found = false;
		if (!LoadAnimBindingsFile(path, loaded, &error, &found))
		{
			report = "animation bindings not loaded (" + path + "): " + error;
			GB_CLIENT_WARN("{}", report);
			return false;
		}
		m_Bindings = std::move(loaded);
		m_BindingsRoot = assetRoot;
		std::string applied;
		if (m_Actors && m_Actors->Count() > 0) ApplyBindingsToAll(applied);   // (a loaded file reaches the characters that are there)
		else if (m_Actors) m_Actors->SetBindings(MakeSetup());                 // (and those that will be)
		report = found ? "loaded " + std::to_string(m_Bindings.Bindings.size()) + " binding(s) and " + std::to_string(m_Bindings.Transitions.size()) + " transition(s) from " + path
			: "no " + path + ": every action plays its default";
		return true;
	}

	std::string PlayerMode::SkeletonOfControlled() const
	{
		const auto it = m_KindOf.find(m_Controlled);
		return it == m_KindOf.end() ? std::string() : SkeletonKey(it->second);
	}

	std::shared_ptr<const BindingsSetup> PlayerMode::MakeSetup() const
	{
		auto setup = std::make_shared<BindingsSetup>();
		setup->Bindings = m_Bindings;
		setup->Defaults = Defaults();
		setup->Only = m_OnlySkeleton;
		return setup;
	}

	bool PlayerMode::ApplyBindingsToAll(std::string& report)
	{
		if (!m_Actors) { report = "no actor world"; return false; }
		CapturePristine();
		if (m_HasPristine && m_Gameplay) RestoreAbilityMontages(m_Gameplay->Library, m_Pristine);   // (a binding taken off must bring the C++ lists back)
		// each character checks the bindings against the clips it really has: one that is not there is skipped (and logged), the default stays
		const int changed = m_Actors->SetBindings(MakeSetup());
		m_Actors->SyncAbilities();
		report = std::to_string(changed) + " of " + std::to_string(m_Actors->Count()) + " character(s) rebuilt with the bindings"
			+ (m_OnlySkeleton.empty() ? " (every model)" : " (only '" + m_OnlySkeleton + "')");
		return true;
	}

	Actor* PlayerMode::Spawn(const std::string& name, const std::string& kind, WorldInstance& world, const SceneEditor& edit, const std::string& assetRoot,
		const glm::vec3& near, FootstepSounds* steps, std::string& error, bool createEntity)
	{
		if (!m_Actors) { error = "no actor world"; return nullptr; }
		if (!world.IsBuilt())
		{
			error = "generate a world first (generate temperate 5)";
			return nullptr;
		}
		namespace fs = std::filesystem;
		const bool isFbx = IsFbx(kind);
		const bool fox = kind != "cesium" && !isFbx;
		m_Actors->SetAssetRoot(assetRoot);
		m_Actors->SetHost(MakeHost(world, edit, steps, m_Gameplay));
		m_Actors->SyncNavigation();   // (the actors plan their paths over the terrain; a world without bounds just walks straight)
		// the manifest the assets ship (the packs' sets, turns and montages as data) replaces the built-in lists when it is there
		const std::string shipped = assetRoot + "/animation/mixamo.manifest.json";
		if (isFbx && fs::exists(shipped))
		{
			LoadShipped(assetRoot);
			if (m_BindingsRoot != assetRoot) { std::string report; LoadBindings(assetRoot, report); }
			m_Actors->SetManifest(m_HasShipped ? &m_Shipped : nullptr);   // (the bindings are the world's: the new character is born with them)
		}
		else
			m_Actors->SetManifest(nullptr);

		ActorSpawn spawn;
		spawn.Path = isFbx ? kind : fox ? "models/Fox.glb" : "models/CesiumMan.glb";
		spawn.Rigged = isFbx;
		spawn.Height = isFbx ? 1.75f : fox ? 0.75f : 1.75f;
		spawn.Position = { near.x, near.z };
		spawn.Yaw = 0.0f;
		spawn.Kind = kind;
		spawn.CreateEntity = createEntity;
		CapturePristine();
		Actor* actor = m_Actors->Spawn(name, spawn, error);
		if (actor) m_KindOf[name] = kind;
		if (actor) GB_CLIENT_INFO("Actor {}: {} at ({:.1f}, {:.1f}){}", name, isFbx ? kind : fox ? "fox" : "cesium", actor->GetPosition().x, actor->GetPosition().z, actor->UsesLocomotionBlend() ? ", locomotion blend" : "");
		return actor;
	}

	bool PlayerMode::Start(const std::string& kind, WorldInstance& world, const SceneEditor& edit, const std::string& assetRoot,
		const glm::vec3& near, std::string& error)
	{
		Stop();
		if (!m_Actors) { error = "no actor world"; return false; }
		m_Actors->Remove(m_AbilityActor);   // the player of the garden is this name; a spawned actor of the same name makes way
		Actor* actor = Spawn(m_AbilityActor, kind, world, edit, assetRoot, near, m_Steps, error);
		if (!actor) return false;
		m_Owned = m_AbilityActor;
		return Control(m_AbilityActor);
	}

	void PlayerMode::Stop()
	{
		if (m_Actors && !m_Owned.empty()) m_Actors->Remove(m_Owned);
		m_Owned.clear();
		m_Controlled.clear();
		m_LookCamera = false;
	}

	bool PlayerMode::Control(const std::string& name)
	{
		Actor* actor = m_Actors ? m_Actors->Find(name) : nullptr;
		if (!actor) return false;
		if (Actor* old = Controlled(); old && old != actor && old->Source() == OrderSource::Direct) old->ClearLook();
		m_Controlled = name;
		if (m_Owned != name) m_Owned.clear();   // the actor Start made stays once the player moves to another
		SnapCamera(*actor);
		return true;
	}

	void PlayerMode::SnapCamera(const Actor& actor)
	{
		m_Camera = ThirdPersonCamera();
		m_Camera.GroundHeight = actor.World().GroundHeight;
		m_Camera.Distance = 3.4f + 1.4f * actor.Height();
		m_Camera.MinDistance = 1.2f;
		m_Camera.TargetHeight = 0.75f * actor.Height();
		m_Camera.Yaw = 3.14159265f;   // behind the character (it starts facing +Z)
		m_Camera.Snap(actor.GetPosition());
	}

	void PlayerMode::BeginFrame(float dt, bool acceptInput)
	{
		Actor* a = Controlled();
		if (!a) return;
		const bool scripted = a->ScriptRunning();
		// the player can look around while an AI or a task list walks the character; a script turns the view itself
		const bool aiWalking = a->Source() == OrderSource::AI && !a->AIArrived() && !a->AIStuck();
		if (!scripted && (a->Ragdolled() || a->TasksRunning() || aiWalking || acceptInput)) m_Camera.ReadInput(dt);
		if (!scripted) a->SetViewYaw(m_Camera.Yaw);
		if (acceptInput && !a->Ragdolled()) a->SetInput(ReadCharacterInput(m_Camera.Yaw));
		if (m_LookCamera)
		{
			Camera cam;
			m_Camera.Apply(cam);
			a->LookAtPoint(cam.Position);
		}
	}

	void PlayerMode::EndFrame(float dt)
	{
		Actor* a = Controlled();
		if (!a) return;
		if (a->ScriptRunning()) m_Camera.Yaw = a->ViewYaw();
		m_Camera.Update(dt, a->GetPosition());
	}

	void PlayerMode::Update(float dt, bool acceptInput)
	{
		if (!m_Actors) return;
		BeginFrame(dt, acceptInput);
		m_Actors->Update(dt);
		EndFrame(dt);
	}

	void PlayerMode::SetScript(const std::string& text)
	{
		if (Actor* a = Controlled())
		{
			a->SetViewYaw(m_Camera.Yaw);
			a->SetScript(text);
		}
	}

	bool PlayerMode::RunTasks(const std::string& text, std::string& error)
	{
		Actor* a = Controlled();
		if (!a) { error = "start the player first"; return false; }
		return a->RunTasks(text, error);
	}

	bool PlayerMode::Interact(const std::string& entity, const std::string& point, std::string& error)
	{
		Actor* a = Controlled();
		if (!a) { error = "start the player first"; return false; }
		return a->Interact(entity, point, error);
	}

	bool PlayerMode::UseAbility(const std::string& ability, const std::string& entity, std::string& error)
	{
		Actor* a = Controlled();
		if (!a) { error = "start the player first"; return false; }
		return a->UseAbility(ability, entity, error);
	}

	bool PlayerMode::LoadManifestFile(const std::string& path, std::string& report)
	{
		Actor* a = Controlled();
		if (!a) { report = "start the player first"; return false; }
		return a->LoadManifestFile(path, report);
	}

	bool PlayerMode::SaveManifestFile(const std::string& path, std::string& report) const
	{
		const Actor* a = Controlled();
		if (!a) { report = "start the player first"; return false; }
		return a->SaveManifestFile(path, report);
	}

	std::string PlayerMode::ManifestInfo() const
	{
		const Actor* a = Controlled();
		return a ? a->ManifestInfo() : "no actor under control";
	}

	bool PlayerMode::Retarget(const std::string& path, const std::string& assetRoot, std::string& report)
	{
		Actor* a = Controlled();
		if (!a) { report = "start the player with an FBX character first (player <character.fbx>)"; return false; }
		const bool ok = a->Retarget(path, assetRoot, report);
		if (ok)
		{
			// the clips were added to the model, which the actors built from the same file share: they rebuild theirs to see them
			for (const std::string& n : m_Actors->Names())
				if (Actor* other = m_Actors->Find(n); other && other != a && other->GetModel() == a->GetModel())
				{
					std::string why;
					if (!other->RebuildAnimator(why)) report += other->Name() + ": " + why + "\n";
				}
		}
		return ok;
	}

	std::string PlayerMode::Describe() const
	{
		const Actor* a = Controlled();
		return a ? a->Describe() : "player mode is off";
	}
}
