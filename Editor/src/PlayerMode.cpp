#include "PlayerMode.h"
#include "SceneEditor.h"
#include "WorldInstance.h"
#include "GB/Log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <glm/gtc/matrix_transform.hpp>

namespace GB::Editor
{
	std::shared_ptr<Model> PlayerMode::LoadCharacter(const std::string& pathIn, const std::string& assetRoot, std::string& error)
	{
		namespace fs = std::filesystem;
		std::string path = pathIn;
		if (!fs::exists(path) && fs::exists(assetRoot + "/" + path)) path = assetRoot + "/" + path;
		std::shared_ptr<Model> model = Model::Load(path);
		if (!model)
		{
			error = "could not load the character from " + path;
			return nullptr;
		}
		// the other FBX files of the folder are its animations (Mixamo ships each clip as its own file)
		std::vector<std::string> files;
		std::error_code ec;
		for (const auto& e : fs::directory_iterator(fs::path(path).parent_path().empty() ? fs::path(".") : fs::path(path).parent_path(), ec))
			if (e.is_regular_file() && e.path().extension() == ".fbx" && fs::path(path).filename() != e.path().filename()) files.push_back(e.path().generic_string());
		std::sort(files.begin(), files.end());
		for (const std::string& f : files) model->AppendAnimations(f);
		return model;
	}

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
			model = LoadCharacter(kind, assetRoot, error);
			if (!model) return false;
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
		m_BaseWalk = m_Controller.Config.WalkSpeed;
		m_BaseRun = m_Controller.Config.RunSpeed;
		const glm::vec2 start = FindFreeSpot(m_World, { near.x, near.z }, 2.5f);
		m_Controller.SetPosition(m_World, start.x, start.y, 0.0f);

		m_Edit = &edit;
		m_Tasks.Clear();
		m_Model = std::move(model);
		m_Instance = std::make_unique<ModelInstance>(*m_Model);
		m_Animator.reset();
		m_Physics.reset();
		m_Tracker.Reset();
		m_UseAI = false;
		if (isFbx)
		{
			// the manifest the assets ship (the packs' sets, turns and montages as data) replaces the built-in lists when it is there
			if (const std::string shipped = assetRoot + "/animation/mixamo.manifest.json"; fs::exists(shipped))
			{
				std::string report;
				if (!LoadManifestFile(shipped, report)) GB_CLIENT_WARN("Animation manifest: {}", report);
			}
			if (!BuildAnimator(error))
			{
				m_Instance.reset();
				m_Model.reset();
				return false;
			}
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

	bool PlayerMode::BuildAnimator(std::string& error)
	{
		m_Profile = AnalyzeCharacter(*m_Model);
		LocomotionOptions options;
		if (m_Profile.Humanoid()) options.Analysis = m_Profile.Analysis;   // this skeleton's own bone names
		auto animator = std::make_unique<CharacterAnimator>(*m_Model, *m_Instance, options);
		if (animator->AddDefaultLocomotionClips() < 2 && animator->AddRoleClips(m_Profile) < 2)
		{
			error = "the character has no locomotion clips (idle, walk, run...): put the animation FBX files next to it (see character_check)";
			return false;
		}
		m_ManifestReport = animator->LoadManifest(m_Manifest);   // the sets, turns, jump and montages the folder has clips for
		animator->SetFootsteps(m_Steps);
		if (animator->Anim().Turn().HasClips("")) m_Controller.Config.TurnInPlaceAngle = 70.0f;   // the character has a turn on the spot to show
		m_Animator = std::move(animator);
		ApplyFeet();
		return true;
	}

	bool PlayerMode::LoadManifestFile(const std::string& path, std::string& report)
	{
		std::ifstream in(path);
		if (!in) { report = "could not read " + path; return false; }
		std::stringstream text;
		text << in.rdbuf();
		AnimManifest manifest;
		std::string error;
		if (!AnimManifestFromJson(text.str(), manifest, &error)) { report = error; return false; }
		const std::vector<std::string> problems = ValidateManifest(manifest);
		if (!problems.empty())
		{
			report = "the manifest has problems:";
			for (const std::string& p : problems) report += "\n  " + p;
			return false;
		}
		m_Manifest = std::move(manifest);
		report = "loaded " + std::to_string(m_Manifest.Sets.size() + 1) + " set(s) and " + std::to_string(m_Manifest.Montages.size()) + " montage(s) from " + path;
		if (m_Model && m_Instance && m_Animator)
		{
			std::string error2;
			if (!BuildAnimator(error2)) { report += "\n" + error2; return false; }
			report += "\n" + ManifestInfo();
		}
		return true;
	}

	bool PlayerMode::SaveManifestFile(const std::string& path, std::string& report) const
	{
		std::ofstream out(path);
		if (!out) { report = "could not write " + path; return false; }
		out << AnimManifestToJson(m_Manifest);
		report = "saved " + path;
		return out.good();
	}

	std::string PlayerMode::ManifestInfo() const
	{
		std::string out = "manifest: " + std::to_string(m_Manifest.Sets.size() + 1) + " set(s), " + std::to_string(m_Manifest.Montages.size()) + " montage(s), "
			+ std::to_string(m_Manifest.Packs.size()) + " pack(s)";
		if (!m_Animator) return out + " (no FBX character to apply it to)";
		out += "\nthis character: sets";
		for (const std::string& s : m_ManifestReport.Sets) out += " " + s;
		out += "; " + std::to_string(m_ManifestReport.Montages) + " montage(s), " + std::to_string(m_ManifestReport.TurnClips) + " turn clip(s), jump " + (m_ManifestReport.Air ? "yes" : "no");
		out += "\nmissing clips: " + std::to_string(m_ManifestReport.Missing.size()) + ", clips nothing uses: " + std::to_string(m_ManifestReport.Unused.size());
		for (size_t i = 0; i < m_ManifestReport.Missing.size() && i < 8; ++i) out += "\n  missing " + m_ManifestReport.Missing[i];
		for (size_t i = 0; i < m_ManifestReport.Unused.size() && i < 8; ++i) out += "\n  unused " + m_ManifestReport.Unused[i];
		return out;
	}

	bool PlayerMode::Retarget(const std::string& pathIn, const std::string& assetRoot, std::string& report)
	{
		namespace fs = std::filesystem;
		if (!m_Model || !m_Instance || !m_Animator) { report = "start the player with an FBX character first (player <character.fbx>)"; return false; }
		std::string path = pathIn;
		if (!fs::exists(path) && fs::exists(assetRoot + "/" + path)) path = assetRoot + "/" + path;
		std::vector<std::string> files;
		std::error_code ec;
		if (fs::is_directory(path, ec))
		{
			for (const auto& e : fs::directory_iterator(path, ec))
				if (e.is_regular_file() && (e.path().extension() == ".fbx" || e.path().extension() == ".FBX")) files.push_back(e.path().generic_string());
			std::sort(files.begin(), files.end());
		}
		else if (fs::exists(path)) files.push_back(path);
		if (files.empty()) { report = "no FBX files at " + path; return false; }

		int added = 0, skipped = 0;
		std::vector<std::string> names;
		report.clear();
		bool first = true;
		for (const std::string& file : files)
		{
			auto source = Model::Load(file);
			if (!source || source->GetClips().empty()) { ++skipped; continue; }
			const RetargetMap map = BuildRetargetMap(*source, *m_Model);
			if (first) report += DescribeRetargetMap(*source, *m_Model, map);
			first = false;
			if (!map.Valid()) { ++skipped; continue; }
			added += RetargetAnimations(*source, *m_Model, map, &names);
		}
		report += std::to_string(added) + " clip(s) retargeted" + (skipped ? ", " + std::to_string(skipped) + " file(s) skipped (no clips, or a skeleton that cannot be paired)" : "") + "\n";
		m_RetargetReport = report;
		if (added == 0) return false;
		// the new clips take part in the character check and the locomotion
		m_Animator.reset();
		m_Physics.reset();
		std::string error;
		if (!BuildAnimator(error)) { report += error + "\n"; return false; }
		// every retargeted clip is playable as an action (player_montage <clip>)
		for (const std::string& name : names)
		{
			MontageDesc d;
			d.Name = name;
			d.Clip = name;
			m_Animator->Anim().Montages().Add(d);
		}
		return true;
	}

	void PlayerMode::ApplyFeet()
	{
		if (!m_Animator) return;
		m_Animator->Anim().Feet().SetGround(m_FeetOn ? m_World.GroundHeight : std::function<float(float, float)>());
	}

	void PlayerMode::BuildPhysics()
	{
		m_Physics = std::make_unique<PhysicsWorld>();
		const auto ground = m_World.GroundHeight ? m_World.GroundHeight : std::function<float(float, float)>([](float, float) { return 0.0f; });
		m_Physics->SetGround(ground, [ground](float x, float z)
		{
			const float e = 0.3f;
			return glm::normalize(glm::vec3(ground(x - e, z) - ground(x + e, z), 2.0f * e, ground(x, z - e) - ground(x, z + e)));
		});
		if (m_World.WaterLevel) m_Physics->SetWater(m_World.WaterLevel);
		for (const CircleObstacle& o : m_World.Obstacles)   // trees, rocks and the bodies the user added: vertical columns
		{
			StaticCollider c;
			c.Shape = ShapeType::Capsule;
			c.Center = { o.Center.x, ground(o.Center.x, o.Center.y) + 1.5f, o.Center.y };
			c.Radius = o.Radius;
			c.HalfHeight = 1.5f;
			m_Physics->AddStatic(c);
		}
	}

	bool PlayerMode::StartRagdoll(const glm::vec3& extraVelocity)
	{
		if (!m_Animator || Ragdolled()) return false;
		BuildPhysics();
		m_Tasks.Clear();
		m_Script.clear();
		m_UseAI = false;
		glm::vec3 v = m_Motion.Velocity + glm::vec3(0.0f, m_Motion.VerticalSpeed, 0.0f) + extraVelocity;
		m_RestTime = 0.0f;
		if (!m_Animator->Anim().StartRagdoll(*m_Physics, RootMatrix(), v))
		{
			m_Physics.reset();
			return false;
		}
		return true;
	}

	void PlayerMode::GetUp()
	{
		if (!Ragdolled()) return;
		Ragdoll* rag = m_Animator->Anim().GetRagdoll();
		const glm::vec3 pelvis = rag->PelvisPosition();
		m_Controller.SetPosition(m_World, pelvis.x, pelvis.z, rag->GetUpYaw());
		m_Animator->Anim().EndRagdoll(RootMatrix(), 0.9f);
		m_RestTime = 0.0f;
	}

	bool PlayerMode::Blast(const glm::vec2& xz, float radius, float speed)
	{
		if (!m_Animator) return false;
		const glm::vec3 p = m_Controller.GetPosition();
		if (!Ragdolled() && glm::distance(glm::vec2(p.x, p.z), xz) < radius) StartRagdoll();
		if (!m_Physics) return false;
		const float y = m_World.GroundHeight ? m_World.GroundHeight(xz.x, xz.y) : 0.0f;
		m_Physics->Explode({ xz.x, y, xz.y }, radius, speed);
		return true;
	}

	void PlayerMode::HitRagdoll(const glm::vec3& impulse)
	{
		if (!Ragdolled()) return;
		Ragdoll* rag = m_Animator->Anim().GetRagdoll();
		rag->Hit(rag->PelvisPosition() + glm::vec3(0.0f, 0.3f, 0.0f), impulse);
	}

	std::string PlayerMode::RagdollStatus() const
	{
		if (!m_Animator) return "";
		if (Ragdolled())
		{
			const Ragdoll* rag = const_cast<PlayerMode*>(this)->m_Animator->Anim().GetRagdoll();
			char b[120];
			std::snprintf(b, sizeof b, "ragdoll (%.2f m/s, %s%s)", static_cast<double>(rag->Speed()), rag->LyingDown() ? "lying" : "on its feet", rag->AtRest() ? ", at rest" : "");
			return b;
		}
		return m_Animator->Anim().Recovering() ? "getting up" : "";
	}

	void PlayerMode::Stop()
	{
		m_Animator.reset();   // the ragdoll first, then the world it lives in
		m_Physics.reset();
		m_PropLooks.clear();
		m_PendingProp.clear();
		m_Animator.reset();
		m_UseAI = false;
		m_Instance.reset();
		m_Model.reset();
		m_World = CharacterWorld();
		m_Script.clear();
		m_Obstacles = 0;
	}

	bool PlayerMode::RunTasks(const std::string& text, std::string& error)
	{
		if (!m_Instance) { error = "start the player first"; return false; }
		std::vector<Task> tasks;
		if (!ParseTasks(text, tasks, &error)) return false;
		if (tasks.empty()) { error = "no tasks"; return false; }
		m_UseAI = false;
		m_Script.clear();
		m_Tasks.Set(std::move(tasks));
		return true;
	}

	bool PlayerMode::Interact(const std::string& entity, const std::string& point, std::string& error)
	{
		return RunTasks("interact " + entity + (point.empty() ? "" : " " + point), error);
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
		if (Ragdolled() && m_Physics)
		{
			// the character is a ragdoll: the physics world moves it, the controller follows the pelvis (so the camera and the pose's root do too)
			m_Physics->Step(dt);
			m_Camera.ReadInput(dt);
			Ragdoll* rag = m_Animator->Anim().GetRagdoll();
			const glm::vec3 pelvis = rag->PelvisPosition();
			m_Controller.SetPosition(m_World, pelvis.x, pelvis.z, m_Controller.GetYaw());
			m_Motion = m_Tracker.Update(m_Controller, dt);
			m_Animator->Update(m_Motion, dt, RootMatrix());
			m_RestTime = rag->AtRest() ? m_RestTime + dt : 0.0f;
			if (AutoGetUp && m_RestTime > 1.5f) GetUp();
			m_Camera.Update(dt, pelvis);
			return;
		}
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
		else if (m_Tasks.Running())
		{
			m_Camera.ReadInput(dt);
			const TaskRunner::Resolver resolver = [this](const std::string& entity, const std::string& point, InteractionTarget& out, std::string* err)
			{
				return m_Edit && FindInteraction(m_Edit->World(), entity, point, out, err);
			};
			const TaskRunner::PropHandler props = [this](const std::string& prop, bool attach)
			{
				if (attach) return AttachProp(prop);
				DetachProp(prop);
				return true;
			};
			in = m_Tasks.Update(m_Controller, m_World, m_Animator ? &m_Animator->Anim() : nullptr, dt, resolver, props);
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
		if (m_Animator && m_BaseRun > 0.0f)
		{
			// carrying a box or pushing a barrow the set has a walk and no run: never go faster than its fastest clip shows
			const float fastest = m_Animator->Anim().Active().MaxSpeed();
			const bool setApplies = m_Animator->Anim().Overlay() != AnimInstance::kDefaultSet && fastest > 0.3f;
			const float limit = setApplies ? fastest * 1.1f : 1e9f;
			m_Controller.Config.WalkSpeed = std::min(m_BaseWalk, limit);
			m_Controller.Config.RunSpeed = std::min(m_BaseRun, limit);
		}
		m_Controller.Step(in, m_World, dt);
		m_Motion = m_Tracker.Update(m_Controller, dt);
		if (m_Animator && RagdollLandingSpeed > 0.0f && m_Motion.Landed && m_Motion.LandingSpeed > RagdollLandingSpeed)
		{
			m_Animator->Update(m_Motion, dt, RootMatrix());   // the last animated frame: the ragdoll starts from this pose
			StartRagdoll(glm::vec3(0.0f, -0.6f * m_Motion.LandingSpeed, 0.0f));
			m_Camera.Update(dt, m_Controller.GetPosition());
			return;
		}
		if (m_Animator)
			m_Animator->Update(m_Motion, dt, RootMatrix());   // blend + footsteps, whoever produced the input
		else
		{
			m_Animation.Update(*m_Instance, m_Controller.GetSpeed());
			m_Instance->Update(dt);
		}
		if (m_LookCamera && m_Animator)
		{
			Camera cam;
			m_Camera.Apply(cam);
			m_Animator->Anim().Look().SetTarget(cam.Position);
		}
		if (!m_PendingProp.empty() && m_Animator && !m_Animator->Anim().Transitioning())
		{
			const std::string name = m_PendingProp;
			m_PendingProp.clear();
			AttachProp(name);
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

	bool PlayerMode::AttachProp(const std::string& name)
	{
		if (!m_Animator) return false;
		AnimInstance& anim = m_Animator->Anim();
		const SocketSet& sockets = anim.Sockets();
		Prop prop;
		prop.Name = name;
		PropLook look;
		if (name == "box")
		{
			if (!sockets.Find("carry") || !sockets.Find("hand_l") || !sockets.Find("hand_r")) return false;
			// the grips are measured from the clips that hold a box, so those must be on screen: switch first, attach once the change is done
			if (anim.Overlay() != "box" || anim.Transitioning())
			{
				if (!anim.SetOverlay("box", 0.3f)) return false;
				m_PendingProp = name;
				return true;
			}
			prop.Socket = "carry";
			const glm::mat4 carry = sockets.Matrix(*m_Instance, "carry");
			const glm::vec3 l = glm::vec3(glm::inverse(carry) * sockets.Matrix(*m_Instance, "hand_l")[3]), r = glm::vec3(glm::inverse(carry) * sockets.Matrix(*m_Instance, "hand_r")[3]);
			// the box sits between the palms, its width along the line joining them
			glm::vec3 x = l - r;
			const float width = glm::length(x);
			x = width > 1e-4f ? x / width : glm::vec3(1, 0, 0);
			glm::vec3 z = glm::cross(x, glm::vec3(0, 1, 0));
			z = glm::length(z) > 1e-4f ? glm::normalize(z) : glm::vec3(0, 0, 1);
			const glm::vec3 y = glm::cross(z, x);
			// a shallow box, held by the near edge: the thighs swing forward under a carried box and a deep one goes through them
			// (tests/box_clearance_test.cpp measures it)
			prop.Offset = glm::mat4(glm::vec4(x, 0), glm::vec4(y, 0), glm::vec4(z, 0), glm::vec4((l + r) * 0.5f + z * 0.11f, 1));
			look.Size = { width + 0.04f, 0.32f, 0.26f };
			look.Color = { 0.62f, 0.44f, 0.26f };
		}
		else if (name == "bag")
		{
			if (!sockets.Find("back")) return false;
			prop.Socket = "back";
			look.Size = { 0.34f, 0.46f, 0.2f };
			look.Color = { 0.35f, 0.22f, 0.12f };
		}
		else if (name == "tool")
		{
			if (!sockets.Find("hand_r")) return false;
			prop.Socket = "hand_r";
			look.Size = { 0.04f, 0.04f, 0.9f };
			look.Color = { 0.5f, 0.5f, 0.55f };
		}
		else
			return false;
		anim.SnapGrips(prop);
		if (name != "box") prop.Grips.clear();   // a bag and a tool are held where the clip puts the hand: no IK
		if (!anim.Attach(prop)) return false;
		m_PropLooks[name] = look;
		return true;
	}

	void PlayerMode::DetachProp(const std::string& name)
	{
		if (!m_Animator) return;
		if (name.empty() || name == m_PendingProp) m_PendingProp.clear();
		for (const std::string& n : m_Animator->Anim().PropNames())
			if (name.empty() || n == name) m_Animator->Anim().Detach(n);
	}

	void PlayerMode::AddToScene(Scene& scene) const
	{
		if (!m_Instance)
			return;
		m_Instance->AddToScene(scene, RootMatrix());
		if (!m_Animator) return;
		for (const std::string& n : m_Animator->Anim().PropNames())
		{
			glm::mat4 world;
			const auto look = m_PropLooks.find(n);
			if (look == m_PropLooks.end() || !m_Animator->Anim().PropMatrix(n, world)) continue;
			if (!m_PropMesh) m_PropMesh = std::make_unique<Mesh>(Mesh::CreateCube(1.0f));
			RenderObject obj;
			obj.MeshPtr = m_PropMesh.get();
			obj.Mat.Albedo = look->second.Color;
			obj.Mat.Roughness = 0.7f;
			obj.Transform = glm::scale(world, look->second.Size);
			scene.Objects.push_back(std::move(obj));
		}
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
			if (m_Tasks.Count() > 0) out += " | tasks: " + m_Tasks.Describe();
			return out + (m_UseAI ? (m_AI.Arrived() ? " [AI arrived]" : m_AI.Stuck() ? " [AI stuck]" : " [AI walking]") : "");
		}
		char buf[200];
		std::snprintf(buf, sizeof buf, "player at (%.1f, %.1f, %.1f), %.2f m/s, %s, %zu obstacles", static_cast<double>(p.x), static_cast<double>(p.y), static_cast<double>(p.z),
			static_cast<double>(m_Controller.GetSpeed()), m_Animation.State == Locomotion::Idle ? "idle" : m_Animation.State == Locomotion::Walk ? "walking" : "running", m_Obstacles);
		return buf;
	}
}
