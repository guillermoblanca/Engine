#include "GB/Game/Actor.h"
#include "GB/Game/Interaction.h"
#include "GB/Log.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	namespace
	{
		constexpr float kPi = 3.14159265f;
		constexpr float kViewTurnSpeed = 2.0f;   // rad/s of a script's camera turn (the third-person camera's own rotate speed)

		float Wrap(float a)
		{
			while (a > kPi) a -= 2.0f * kPi;
			while (a < -kPi) a += 2.0f * kPi;
			return a;
		}
	}

	namespace
	{
		// Adds the time between its creation and destruction to `into` when `on`.
		struct Stopwatch
		{
			Stopwatch(bool on, double& into) : m_On(on), m_Into(into) { if (on) m_Start = std::chrono::steady_clock::now(); }
			~Stopwatch() { if (m_On) m_Into += std::chrono::duration<double>(std::chrono::steady_clock::now() - m_Start).count(); }
			bool m_On;
			double& m_Into;
			std::chrono::steady_clock::time_point m_Start;
		};
	}

	Actor::~Actor()
	{
		if (AbilitySystemComponent* a = Abilities()) { a->CancelAll(); a->SetRuntime(nullptr); }
		m_Animator.reset();   // the ragdoll first, then the world it lives in
		m_Physics.reset();
	}

	std::shared_ptr<Model> Actor::LoadCharacter(const std::string& pathIn, const std::string& assetRoot, std::string& error)
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

	void Actor::SetHeight(float height)
	{
		if (height <= 0.01f || m_Height <= 0.0f || std::fabs(height - m_Height) < 1e-4f) return;
		m_Scale *= height / m_Height;
		m_Height = height;
		m_Controller.Config.Radius = std::max(0.2f * m_Height, 0.25f);
		m_Controller.Config.MaxWadeDepth = 0.55f * m_Height;
	}

	bool Actor::Init(std::shared_ptr<Model> model, const ActorHost& host, const ActorSpec& spec, std::string& error)
	{
		if (!model) { error = "no model"; return false; }
		m_Name = spec.Name;
		m_Height = spec.Height;
		const float modelHeight = model->GetBoundsMax().y - model->GetBoundsMin().y;
		m_Scale = modelHeight > 1e-3f ? m_Height / modelHeight : 1.0f;
		m_World = host.World;
		m_StaticObstacles = m_World.Obstacles.size();
		m_Interactions = host.Interactions;
		m_Steps = host.Steps;
		if (spec.Manifest) m_Manifest = *spec.Manifest;

		m_Controller.Config.Radius = std::max(0.2f * m_Height, 0.25f);
		m_Controller.Config.MaxWadeDepth = 0.55f * m_Height;
		m_BaseWalk = m_Controller.Config.WalkSpeed;
		m_BaseRun = m_Controller.Config.RunSpeed;
		SetOthers(spec.Others);
		const glm::vec2 start = FindFreeSpot(m_World, spec.Start, 2.5f);
		m_Controller.SetPosition(m_World, start.x, start.y, spec.StartYaw);

		m_Model = std::move(model);
		m_Instance = std::make_unique<ModelInstance>(*m_Model);
		if (spec.Rigged)
		{
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
		return true;
	}

	bool Actor::BuildAnimator(std::string& error)
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
		if (m_Gameplay) RegisterAbilityMontages(m_Gameplay->Library, animator->Anim(), m_Manifest.Montages);   // the same montages plus the notifies the abilities need
		animator->SetFootsteps(m_Steps);
		if (animator->Anim().Turn().HasClips("")) m_Controller.Config.TurnInPlaceAngle = 70.0f;   // the character has a turn on the spot to show
		m_Animator = std::move(animator);
		ApplyFeet();
		return true;
	}

	bool Actor::RebuildAnimator(std::string& error)
	{
		if (!m_Animator) return true;
		m_Animator.reset();
		m_Physics.reset();
		return BuildAnimator(error);
	}

	bool Actor::LoadManifestFile(const std::string& path, std::string& report)
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

	bool Actor::SaveManifestFile(const std::string& path, std::string& report) const
	{
		std::ofstream out(path);
		if (!out) { report = "could not write " + path; return false; }
		out << AnimManifestToJson(m_Manifest);
		report = "saved " + path;
		return out.good();
	}

	std::string Actor::ManifestInfo() const
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

	bool Actor::Retarget(const std::string& pathIn, const std::string& assetRoot, std::string& report)
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

	void Actor::ApplyFeet()
	{
		if (!m_Animator) return;
		m_Animator->Anim().Feet().SetGround(m_FeetOn ? m_World.GroundHeight : std::function<float(float, float)>());
	}

	void Actor::SetOthers(const std::vector<CircleObstacle>& others)
	{
		m_World.Obstacles.resize(m_StaticObstacles);
		m_World.DynamicFrom = m_StaticObstacles;
		m_World.Obstacles.insert(m_World.Obstacles.end(), others.begin(), others.end());
	}

	void Actor::Teleport(float x, float z, float yaw)
	{
		m_Controller.SetPosition(m_World, x, z, yaw);
	}

	void Actor::BuildPhysics()
	{
		m_Physics = std::make_unique<PhysicsWorld>();
		const auto ground = m_World.GroundHeight ? m_World.GroundHeight : std::function<float(float, float)>([](float, float) { return 0.0f; });
		m_Physics->SetGround(ground, [ground](float x, float z)
		{
			const float e = 0.3f;
			return glm::normalize(glm::vec3(ground(x - e, z) - ground(x + e, z), 2.0f * e, ground(x, z - e) - ground(x, z + e)));
		});
		if (m_World.WaterLevel) m_Physics->SetWater(m_World.WaterLevel);
		for (size_t i = 0; i < m_StaticObstacles; ++i)   // trees, rocks and the bodies the user added: vertical columns (the other actors are not in this world)
		{
			const CircleObstacle& o = m_World.Obstacles[i];
			StaticCollider c;
			c.Shape = ShapeType::Capsule;
			c.Center = { o.Center.x, ground(o.Center.x, o.Center.y) + 1.5f, o.Center.y };
			c.Radius = o.Radius;
			c.HalfHeight = 1.5f;
			m_Physics->AddStatic(c);
		}
	}

	bool Actor::StartRagdoll(const glm::vec3& extraVelocity)
	{
		if (!m_Animator || Ragdolled()) return false;
		if (AbilitySystemComponent* a = Abilities()) a->CancelAll();
		BuildPhysics();
		ReleaseOrders();
		const glm::vec3 v = m_Motion.Velocity + glm::vec3(0.0f, m_Motion.VerticalSpeed, 0.0f) + extraVelocity;
		m_RestTime = 0.0f;
		if (!m_Animator->Anim().StartRagdoll(*m_Physics, RootMatrix(), v))
		{
			m_Physics.reset();
			return false;
		}
		return true;
	}

	void Actor::GetUp()
	{
		if (!Ragdolled()) return;
		Ragdoll* rag = m_Animator->Anim().GetRagdoll();
		const glm::vec3 pelvis = rag->PelvisPosition();
		m_Controller.SetPosition(m_World, pelvis.x, pelvis.z, rag->GetUpYaw());
		m_Animator->Anim().EndRagdoll(RootMatrix(), 0.9f);
		m_RestTime = 0.0f;
	}

	bool Actor::Blast(const glm::vec2& xz, float radius, float speed)
	{
		if (!m_Animator) return false;
		const glm::vec3 p = m_Controller.GetPosition();
		if (!Ragdolled() && glm::distance(glm::vec2(p.x, p.z), xz) < radius) StartRagdoll();
		if (!m_Physics) return false;
		const float y = m_World.GroundHeight ? m_World.GroundHeight(xz.x, xz.y) : 0.0f;
		m_Physics->Explode({ xz.x, y, xz.y }, radius, speed);
		return true;
	}

	void Actor::HitRagdoll(const glm::vec3& impulse)
	{
		if (!Ragdolled()) return;
		Ragdoll* rag = m_Animator->Anim().GetRagdoll();
		rag->Hit(rag->PelvisPosition() + glm::vec3(0.0f, 0.3f, 0.0f), impulse);
	}

	std::string Actor::RagdollStatus() const
	{
		if (!m_Animator) return "";
		if (Ragdolled())
		{
			const Ragdoll* rag = const_cast<Actor*>(this)->m_Animator->Anim().GetRagdoll();
			std::ostringstream s;
			s << std::fixed << std::setprecision(2) << "ragdoll (" << static_cast<double>(rag->Speed()) << " m/s, " << (rag->LyingDown() ? "lying" : "on its feet") << (rag->AtRest() ? ", at rest" : "") << ")";
			return s.str();
		}
		return m_Animator->Anim().Recovering() ? "getting up" : "";
	}

	void Actor::SetAbilities(GameplayWorld* gameplay, const std::string& name)
	{
		if (AbilitySystemComponent* old = Abilities()) { old->CancelAll(); old->SetRuntime(nullptr); }
		m_Gameplay = gameplay;
		m_AbilityName = name;
		if (m_Gameplay && m_Animator) RegisterAbilityMontages(m_Gameplay->Library, m_Animator->Anim(), m_Manifest.Montages);
	}

	void Actor::PrepareAbilities()
	{
		AbilitySystemComponent* asc = Abilities();
		if (!asc) return;
		for (const char* work : { "Chop", "Mine", "Gather", "PullFiber", "Collect", "Stoke", "DrawWater", "OpenDoor", "CloseDoor" }) asc->Grant(work);   // every actor can work resource nodes and interactives (H2, H3); a library without them ignores this
		if (!m_Instance || !m_Animator || Ragdolled())
		{
			if (asc->Runtime()) { asc->CancelAll(); asc->SetRuntime(nullptr); }
			return;
		}
		m_AbilityRt.Anim = &m_Animator->Anim();
		m_AbilityRt.Controller = &m_Controller;
		m_AbilityRt.World = &m_World;
		m_AbilityRt.Resolve = m_Interactions ? m_Interactions : TaskRunner::Resolver([](const std::string&, const std::string&, InteractionTarget&, std::string*) { return false; });
		m_AbilityRt.Equip = [this](const std::string& prop, bool on) { return EquipProp(prop, on); };
		asc->SetRuntime(&m_AbilityRt);
	}

	bool Actor::UseAbility(const std::string& ability, const std::string& entity, std::string& error)
	{
		AbilitySystemComponent* asc = Abilities();
		if (!asc) { error = "no ability system for '" + m_AbilityName + "' (garden build)"; return false; }
		PrepareAbilities();   // the character is lent to the abilities before this one starts, even in the frame the actor appeared
		ReleaseOrders();
		return asc->Activate(ability, entity.empty() ? nullptr : m_Gameplay->Find(entity), &error, entity);
	}

	TaskRunner::AbilityHandler Actor::AbilityHandler()
	{
		TaskRunner::AbilityHandler h;
		h.Start = [this](const std::string& ability, const std::string& entity, std::string& error)
		{
			AbilitySystemComponent* asc = Abilities();
			if (!asc) { error = "no ability system for '" + m_AbilityName + "' (garden build)"; return false; }
			PrepareAbilities();
			return asc->Activate(ability, entity.empty() ? nullptr : m_Gameplay->Find(entity), &error, entity);
		};
		h.Poll = [this](std::string& error)
		{
			AbilitySystemComponent* asc = Abilities();
			if (!asc) { error = "the ability system is gone"; return -1; }
			if (asc->AnyActive()) return 0;
			error = asc->LastError();
			return error.empty() ? 1 : -1;
		};
		return h;
	}

	bool Actor::EquipProp(const std::string& name, bool on)
	{
		if (!m_Animator) return false;
		AnimInstance& anim = m_Animator->Anim();
		if (on) return name == "wheelbarrow" ? anim.SetOverlay("wheelbarrow", 0.3f) : AttachProp(name);
		DetachProp(name);
		if (name == "box" || name == "wheelbarrow") anim.SetOverlay(anim.HasSet("free") ? "free" : AnimInstance::kDefaultSet, 0.3f);
		return true;
	}

	void Actor::StopMontage()
	{
		if (!m_Animator) return;
		m_Animator->Anim().Montages().StopSlot("FullBody");
		m_Animator->Anim().Montages().StopSlot("UpperBody");
	}

	// ---- orders

	void Actor::ReleaseOrders()
	{
		m_Tasks.Clear();
		m_Script.clear();
		m_AI.Stop();
		m_Source = OrderSource::Direct;
	}

	bool Actor::RunTasks(const std::string& text, std::string& error)
	{
		if (!m_Instance) { error = "the actor is not built"; return false; }
		std::vector<Task> tasks;
		if (!ParseTasks(text, tasks, &error)) return false;
		if (tasks.empty()) { error = "no tasks"; return false; }
		m_Script.clear();
		m_AI.Stop();
		m_FailLogged = false;
		m_Tasks.Set(std::move(tasks));
		m_Source = OrderSource::Tasks;
		return true;
	}

	bool Actor::Interact(const std::string& entity, const std::string& point, std::string& error)
	{
		return RunTasks("interact " + entity + (point.empty() ? "" : " " + point), error);
	}

	void Actor::StopTasks()
	{
		m_Tasks.Clear();
		if (m_Source == OrderSource::Tasks) m_Source = OrderSource::Direct;
	}

	void Actor::SetScript(const std::string& text)
	{
		m_Tasks.Clear();
		m_AI.Stop();
		m_Script = ParseCharacterScript(text);
		m_ScriptTime = 0.0f;
		m_Source = OrderSource::Script;
	}

	float Actor::ScriptLength() const
	{
		float total = 0.0f;
		for (const CharacterScriptStep& s : m_Script) total += s.Seconds;
		return total;
	}

	void Actor::MoveTo(const glm::vec2& xz, bool run)
	{
		m_Tasks.Clear();
		m_Script.clear();
		m_Source = OrderSource::AI;
		m_AI.WalkSpeed = m_Controller.Config.WalkSpeed;
		m_AI.RunSpeed = m_Controller.Config.RunSpeed;
		m_AI.Run = run;
		m_AI.MoveTo(xz);
	}

	void Actor::StartReplay(std::vector<CharacterInput> frames)
	{
		ReleaseOrders();
		m_Replay = std::move(frames);
		m_ReplayIndex = 0;
		m_Source = OrderSource::Replay;
	}

	// The input of this frame from whoever gives the orders. A source with nothing left to say (a script that ended, tasks done, an AI that
	// arrived or is stuck) leaves the actor to the host's input, as the keyboard did before.
	CharacterInput Actor::ChooseInput(float dt)
	{
		CharacterInput manual;
		manual.CameraYaw = m_ViewYaw;
		if (m_HasManual) manual = m_Manual;
		m_HasManual = false;
		switch (m_Source)
		{
		case OrderSource::Script:
			if (!m_Script.empty())
			{
				const CharacterScriptStep* step = SampleCharacterScript(m_Script, m_ScriptTime);
				m_ScriptTime += dt;
				if (step)
				{
					m_ViewYaw = Wrap(m_ViewYaw + step->CameraTurn * kViewTurnSpeed * dt);
					CharacterInput in;
					in.Move = step->Move;
					in.Run = step->Run;
					in.Jump = step->Jump;
					in.CameraYaw = m_ViewYaw;
					return in;
				}
			}
			break;
		case OrderSource::Tasks:
			if (m_Tasks.Running())
			{
				const TaskRunner::PropHandler props = [this](const std::string& prop, bool attach)
				{
					if (attach) return AttachProp(prop);
					DetachProp(prop);
					return true;
				};
				const TaskRunner::Resolver none = [](const std::string&, const std::string&, InteractionTarget&, std::string*) { return false; };
				m_Tasks.Profile = Navigation;
				const CharacterInput in = m_Tasks.Update(m_Controller, m_World, m_Animator ? &m_Animator->Anim() : nullptr, dt, m_Interactions ? m_Interactions : none, props, AbilityHandler());
				if (m_Tasks.Failed() && !m_FailLogged) { m_FailLogged = true; GB_CLIENT_WARN("{}: orders failed ({})", m_Name, m_Tasks.Describe()); }
				return in;
			}
			break;
		case OrderSource::AI:
			if (!m_AI.Arrived() && !m_AI.Stuck()) { m_AI.Profile = Navigation; return m_AI.Update(m_Controller, m_World, dt); }
			break;
		case OrderSource::Replay:
			if (m_ReplayIndex < m_Replay.size()) return m_Replay[m_ReplayIndex++];
			break;
		case OrderSource::Direct:
			break;
		}
		return manual;
	}

	// ---- the frame

	void Actor::Update(float dt)
	{
		if (!m_Instance)
			return;
		if (Ragdolled() && m_Physics)
		{
			// the actor is a ragdoll: the physics world moves it, the controller follows the pelvis (so the camera and the pose's root do too)
			Stopwatch watch(MeasureCost, m_Cost.Ragdoll);
			++m_Cost.Frames;
			m_HasManual = false;
			m_Physics->Step(dt);
			Ragdoll* rag = m_Animator->Anim().GetRagdoll();
			const glm::vec3 pelvis = rag->PelvisPosition();
			m_Controller.SetPosition(m_World, pelvis.x, pelvis.z, m_Controller.GetYaw());
			m_Motion = m_Tracker.Update(m_Controller, dt);
			m_Animator->Update(m_Motion, dt, RootMatrix());
			m_RestTime = rag->AtRest() ? m_RestTime + dt : 0.0f;
			if (AutoGetUp && m_RestTime > 1.5f) GetUp();
			return;
		}
		++m_Cost.Frames;
		std::unique_ptr<Stopwatch> control(new Stopwatch(MeasureCost, m_Cost.Control));
		CharacterInput in = ChooseInput(dt);
		if (AbilitySystemComponent* asc = Abilities(); asc && asc->Controlling()) in = asc->Input();   // an ability is walking or animating the character
		if (m_Animator && m_BaseRun > 0.0f)
		{
			// carrying a box or pushing a barrow the set has a walk and no run: never go faster than its fastest clip shows
			const float fastest = m_Animator->Anim().Active().MaxSpeed();
			const bool setApplies = m_Animator->Anim().Overlay() != AnimInstance::kDefaultSet && fastest > 0.3f;
			const float limit = setApplies ? fastest * 1.1f : 1e9f;
			m_Controller.Config.WalkSpeed = std::min(m_BaseWalk, limit);
			m_Controller.Config.RunSpeed = std::min(m_BaseRun, limit);
		}
		if (m_Recording) m_Recorded.push_back(in);
		m_Controller.Step(in, m_World, dt);
		m_Motion = m_Tracker.Update(m_Controller, dt);
		control.reset();
		Stopwatch animation(MeasureCost, m_Cost.Animation);
		if (m_Animator && RagdollLandingSpeed > 0.0f && m_Motion.Landed && m_Motion.LandingSpeed > RagdollLandingSpeed)
		{
			m_Animator->Update(m_Motion, dt, RootMatrix());   // the last animated frame: the ragdoll starts from this pose
			StartRagdoll(glm::vec3(0.0f, -0.6f * m_Motion.LandingSpeed, 0.0f));
			return;
		}
		if (m_Animator)
			m_Animator->Update(m_Motion, dt, RootMatrix());   // blend + footsteps, whoever produced the input
		else
		{
			m_Animation.Update(*m_Instance, m_Controller.GetSpeed());
			m_Instance->Update(dt);
		}
		if (!m_PendingProp.empty() && m_Animator && !m_Animator->Anim().Transitioning())
		{
			const std::string name = m_PendingProp;
			m_PendingProp.clear();
			AttachProp(name);
		}
	}

	std::string Actor::MontageStatus() const
	{
		if (!m_Animator) return "";
		const MontagePlayer& m = m_Animator->Anim().Montages();
		std::string playing;
		for (const std::string& n : m.Names())
			if (m.IsPlaying(n)) playing += (playing.empty() ? "" : ", ") + n;
		return playing.empty() ? "no montage" : "montage: " + playing;
	}

	glm::mat4 Actor::RootMatrix() const
	{
		return glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), m_Controller.GetPosition()), m_Controller.GetYaw(), glm::vec3(0, 1, 0)), glm::vec3(m_Scale));
	}

	bool Actor::AttachProp(const std::string& name)
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
		else if (name == "can")
		{
			if (!sockets.Find("hand_r")) return false;
			prop.Socket = "hand_r";
			look.Size = { 0.2f, 0.26f, 0.24f };
			look.Color = { 0.2f, 0.4f, 0.65f };
		}
		else if (name == "shovel")
		{
			if (!sockets.Find("hand_r")) return false;
			prop.Socket = "hand_r";
			look.Size = { 0.05f, 0.05f, 1.0f };
			look.Color = { 0.5f, 0.38f, 0.22f };
		}
		else if (name == "seeds")
		{
			if (!sockets.Find("hand_r")) return false;
			prop.Socket = "hand_r";
			look.Size = { 0.18f, 0.08f, 0.12f };
			look.Color = { 0.75f, 0.65f, 0.3f };
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

	void Actor::DetachProp(const std::string& name)
	{
		if (!m_Animator) return;
		if (name.empty() || name == m_PendingProp) m_PendingProp.clear();
		for (const std::string& n : m_Animator->Anim().PropNames())
			if (name.empty() || n == name) m_Animator->Anim().Detach(n);
	}

	void Actor::AddToScene(Scene& scene) const
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

	std::string Actor::Describe() const
	{
		if (!m_Instance)
			return m_Name + " is not built";
		const glm::vec3 p = m_Controller.GetPosition();
		std::ostringstream out;
		out << std::fixed << std::setprecision(1) << m_Name << " at (" << static_cast<double>(p.x) << ", " << static_cast<double>(p.y) << ", " << static_cast<double>(p.z) << "), "
			<< std::setprecision(2) << static_cast<double>(m_Controller.GetSpeed()) << " m/s";
		if (m_Animator)
		{
			out << ", locomotion blend [" << m_Animator->Anim().Overlay() << "] (phase " << static_cast<double>(m_Animator->Locomotion().Phase()) << ", speed x"
				<< static_cast<double>(m_Animator->Locomotion().SpeedScale()) << ", steps " << (m_Steps ? m_Steps->Count() : 0) << "):";
			for (const auto& c : m_Animator->Locomotion().Contributions()) out << " " << c.Clip << "=" << static_cast<double>(c.Weight);
			out << " | " << MontageStatus() << (m_Controller.IsAirborne() ? ", airborne" : "");
			if (m_Tasks.Count() > 0) out << " | tasks: " << m_Tasks.Describe();
		}
		else
			out << ", " << (m_Animation.State == Locomotion::Idle ? "idle" : m_Animation.State == Locomotion::Walk ? "walking" : "running");
		switch (m_Source)
		{
		case OrderSource::AI: out << (m_AI.Arrived() ? " [AI arrived]" : m_AI.Stuck() ? " [AI stuck]" : " [AI walking]"); break;
		case OrderSource::Tasks: out << " [tasks]"; break;
		case OrderSource::Script: out << " [script]"; break;
		case OrderSource::Replay: out << " [replay]"; break;
		case OrderSource::Direct: break;
		}
		return out.str();
	}
}
