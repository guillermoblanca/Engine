#include "PlayerMode.h"
#include "SceneEditor.h"
#include "WorldInstance.h"
#include "GB/Log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>

namespace GB::Editor
{
	bool PlayerMode::Start(const std::string& kind, WorldInstance& world, const SceneEditor& edit, const std::string& assetRoot,
		const glm::vec3& near, std::string& error)
	{
		Stop();
		if (!world.IsBuilt())
		{
			error = "generate a world first (generate temperate 5)";
			return false;
		}
		namespace fs = std::filesystem;
		const bool isFbx = kind.size() > 4 && (kind.substr(kind.size() - 4) == ".fbx" || kind.substr(kind.size() - 4) == ".FBX");
		const bool fox = kind != "cesium" && !isFbx;
		std::shared_ptr<Model> model;
		if (isFbx)
		{
			std::string path = kind;
			if (!fs::exists(path) && fs::exists(assetRoot + "/" + path)) path = assetRoot + "/" + path;
			model = Model::Load(path);
			if (!model)
			{
				error = "could not load the character from " + path;
				return false;
			}
			// the other FBX files of the folder are its animations (Mixamo ships each clip as its own file)
			std::vector<std::string> files;
			std::error_code ec;
			for (const auto& e : fs::directory_iterator(fs::path(path).parent_path().empty() ? fs::path(".") : fs::path(path).parent_path(), ec))
				if (e.is_regular_file() && e.path().extension() == ".fbx" && fs::path(path).filename() != e.path().filename()) files.push_back(e.path().generic_string());
			std::sort(files.begin(), files.end());
			for (const std::string& f : files) model->AppendAnimations(f);
		}
		else
			model = Model::Load(assetRoot + (fox ? "/models/Fox.glb" : "/models/CesiumMan.glb"));
		if (!model)
		{
			error = "could not load the character model from " + assetRoot + "/models";
			return false;
		}
		const float height = isFbx ? 1.75f : fox ? 0.75f : 1.75f;
		const float modelHeight = model->GetBoundsMax().y - model->GetBoundsMin().y;
		m_Scale = modelHeight > 1e-3f ? height / modelHeight : 1.0f;

		m_World = MakeTerrainCharacterWorld(world.GetTerrain());
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
					m_World.Obstacles.push_back({ { p.x, p.z }, 0.3f * scale });
				else if (world.PropBounds(*entity.Prop, center, radius) && radius * scale * 0.5f > 0.2f)
					m_World.Obstacles.push_back({ { p.x, p.z }, radius * scale * 0.65f });
			}
			else if (entity.Body)
			{
				const glm::vec3 h = entity.Body->HalfExtents * scale;
				m_World.Obstacles.push_back({ { p.x, p.z }, entity.Body->Shape == ColliderShape::Box ? std::max(h.x, h.z) : h.x });
			}
		}
		m_Obstacles = m_World.Obstacles.size();
		m_Controller.Config.Radius = std::max(0.2f * height, 0.25f);
		m_Controller.Config.MaxWadeDepth = 0.55f * height;
		const glm::vec2 start = FindFreeSpot(m_World, { near.x, near.z }, 2.5f);
		m_Controller.SetPosition(m_World, start.x, start.y, 0.0f);

		m_Model = std::move(model);
		m_Instance = std::make_unique<ModelInstance>(*m_Model);
		m_Animator.reset();
		m_Tracker.Reset();
		m_UseAI = false;
		if (isFbx)
		{
			auto animator = std::make_unique<CharacterAnimator>(*m_Model, *m_Instance);
			if (animator->AddDefaultLocomotionClips() < 2)
			{
				error = "the character has no locomotion clips (idle, walking, standard run...): put the animation FBX files next to it";
				m_Instance.reset();
				m_Model.reset();
				return false;
			}
			animator->AddMixamoSets();      // box, holding, wheelbarrow... when the folder has those clips
			animator->AddMixamoActions();   // farming montages and the jump clip
			animator->SetFootsteps(m_Steps);
			m_Animator = std::move(animator);
		}
		else
		{
			m_Animation = CharacterAnimation::ForModel(*m_Model);
			m_Animation.Start(*m_Instance);
		}

		m_Camera = ThirdPersonCamera();
		m_Camera.GroundHeight = m_World.GroundHeight;
		m_Camera.Distance = 3.4f + 1.4f * height;
		m_Camera.MinDistance = 1.2f;
		m_Camera.TargetHeight = 0.75f * height;
		m_Camera.Yaw = 3.14159265f;   // behind the character (it starts facing +Z)
		m_Camera.Snap(m_Controller.GetPosition());
		m_Script.clear();
		m_ScriptTime = 0.0f;
		GB_CLIENT_INFO("Player mode: {} at ({:.1f}, {:.1f}), {} obstacles{}", isFbx ? kind : fox ? "fox" : "cesium", start.x, start.y, m_Obstacles, m_Animator ? ", locomotion blend" : "");
		return true;
	}

	void PlayerMode::Stop()
	{
		m_Animator.reset();
		m_UseAI = false;
		m_Instance.reset();
		m_Model.reset();
		m_World = CharacterWorld();
		m_Script.clear();
		m_Obstacles = 0;
	}

	void PlayerMode::SetScript(const std::string& text)
	{
		m_Script = ParseCharacterScript(text);
		m_ScriptTime = 0.0f;
	}

	void PlayerMode::MoveTo(const glm::vec2& xz, bool run)
	{
		m_UseAI = true;
		m_Script.clear();
		m_AI.WalkSpeed = m_Controller.Config.WalkSpeed;
		m_AI.RunSpeed = m_Controller.Config.RunSpeed;
		m_AI.Run = run;
		m_AI.MoveTo(xz);
	}

	void PlayerMode::Update(float dt, bool acceptInput)
	{
		if (!m_Instance)
			return;
		CharacterInput in;
		in.CameraYaw = m_Camera.Yaw;
		if (!m_Script.empty())
		{
			if (const CharacterScriptStep* step = SampleCharacterScript(m_Script, m_ScriptTime))
			{
				in.Move = step->Move;
				in.Run = step->Run;
				in.Jump = step->Jump;
				m_Camera.Rotate(step->CameraTurn * m_Camera.RotateSpeed * dt, 0.0f);
				in.CameraYaw = m_Camera.Yaw;
			}
			m_ScriptTime += dt;
		}
		else if (m_UseAI && !m_AI.Arrived() && !m_AI.Stuck())
		{
			m_Camera.ReadInput(dt);   // the player can still look around while the AI walks the character
			in = m_AI.Update(m_Controller, dt);
		}
		else if (acceptInput)
		{
			m_Camera.ReadInput(dt);
			in = ReadCharacterInput(m_Camera.Yaw);
		}
		m_Controller.Step(in, m_World, dt);
		m_Motion = m_Tracker.Update(m_Controller, dt);
		if (m_Animator)
			m_Animator->Update(m_Motion, dt, RootMatrix());   // blend + footsteps, whoever produced the input
		else
		{
			m_Animation.Update(*m_Instance, m_Controller.GetSpeed());
			m_Instance->Update(dt);
		}
		m_Camera.Update(dt, m_Controller.GetPosition());
	}

	std::string PlayerMode::MontageStatus() const
	{
		if (!m_Animator) return "";
		const MontagePlayer& m = m_Animator->Anim().Montages();
		std::string playing;
		for (const std::string& n : m.Names())
			if (m.IsPlaying(n)) playing += (playing.empty() ? "" : ", ") + n;
		return playing.empty() ? "no montage" : "montage: " + playing;
	}

	glm::mat4 PlayerMode::RootMatrix() const
	{
		return glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), m_Controller.GetPosition()), m_Controller.GetYaw(), glm::vec3(0, 1, 0)), glm::vec3(m_Scale));
	}

	void PlayerMode::AddToScene(Scene& scene) const
	{
		if (m_Instance)
			m_Instance->AddToScene(scene, RootMatrix());
	}

	std::string PlayerMode::Describe() const
	{
		if (!m_Instance)
			return "player mode is off";
		const glm::vec3 p = m_Controller.GetPosition();
		if (m_Animator)
		{
			char head[200];
			std::snprintf(head, sizeof head, "player at (%.1f, %.1f, %.1f), %.2f m/s, locomotion blend [%s] (phase %.2f, speed x%.2f, steps %d):", static_cast<double>(p.x), static_cast<double>(p.y), static_cast<double>(p.z),
				static_cast<double>(m_Controller.GetSpeed()), m_Animator->Anim().Overlay().c_str(), static_cast<double>(m_Animator->Locomotion().Phase()), static_cast<double>(m_Animator->Locomotion().SpeedScale()), m_Steps ? m_Steps->Count() : 0);
			std::string out = head;
			for (const auto& c : m_Animator->Locomotion().Contributions()) { char b[96]; std::snprintf(b, sizeof b, " %s=%.2f", c.Clip.c_str(), static_cast<double>(c.Weight)); out += b; }
			out += " | " + MontageStatus() + (m_Controller.IsAirborne() ? ", airborne" : "");
			return out + (m_UseAI ? (m_AI.Arrived() ? " [AI arrived]" : m_AI.Stuck() ? " [AI stuck]" : " [AI walking]") : "");
		}
		char buf[200];
		std::snprintf(buf, sizeof buf, "player at (%.1f, %.1f, %.1f), %.2f m/s, %s, %zu obstacles", static_cast<double>(p.x), static_cast<double>(p.y), static_cast<double>(p.z),
			static_cast<double>(m_Controller.GetSpeed()), m_Animation.State == Locomotion::Idle ? "idle" : m_Animation.State == Locomotion::Walk ? "walking" : "running", m_Obstacles);
		return buf;
	}
}
