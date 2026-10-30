#include "GB/Game/Actor.h"
#include "GB/Assets/AssetPath.h"
#include "GB/Game/Interaction.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace GB
{
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

	Actor::Actor() : GBObject(std::string())   // named by Init (spec.Name)
	{
		m_Movement = &AddComponent<MovementComponent>();
		m_Orders = &AddComponent<OrdersComponent>();
		m_Ability = &AddComponent<AbilityComponent>();
		m_Inventory = &AddComponent<InventoryComponent>();
		m_Orders->SetTaskHandlers(
			[this](const std::string& prop, bool attach)
			{
				if (attach) return AttachProp(prop);
				DetachProp(prop);
				return true;
			},
			AbilityHandler());
		m_Ability->SetHooks(
			[this](const std::string& prop, bool on) { return EquipProp(prop, on); },
			[this](const AbilityDef& d, int use, const std::map<std::string, std::string>& context) { return PickMontage(d, use, context); });
	}

	Actor::~Actor()
	{
		OnDestroy();   // (a world calls it first; this is for an actor that dies outside one) the components go while the body they read is alive
	}

	void Actor::EndPlay()
	{
		if (m_Ability) m_Ability->Detach();   // before the components end: the abilities hold the animation
		GBObject::EndPlay();
	}

	void Actor::OnDestroy()
	{
		if (State() == ObjectState::Destroyed) return;
		if (m_Ability) m_Ability->Detach();
		GBObject::OnDestroy();
		m_Movement = nullptr; m_Orders = nullptr; m_Ability = nullptr; m_Inventory = nullptr;
		m_Geometry = nullptr; m_Ragdoll = nullptr; m_Animation = nullptr; m_Props = nullptr;
	}

	std::shared_ptr<Model> Actor::LoadCharacter(const std::string& pathIn, const std::string& assetRoot, std::string& error)
	{
		namespace fs = std::filesystem;
		const std::string path = ResolveAssetPath(pathIn, assetRoot);
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

	bool Actor::Init(std::shared_ptr<Model> model, const ActorHost& host, const ActorSpec& spec, std::string& error)
	{
		Rename(spec.Name);
		m_Interactions = host.Interactions;
		m_Steps = host.Steps;
		m_Movement->Init(host.World, spec.Height, spec.Start, spec.StartYaw, spec.Others);

		if (model)
		{
			// built apart and joined to the object only if they work: the animation is the one that can fail. Declared in the order they are
			// added, so a failure frees the animation (and its ragdoll) before the world and the model
			auto geometry = std::make_unique<GeometryComponent>(m_Movement->Controller());
			if (!geometry->Build(std::move(model), spec.Height)) { error = "no model"; return false; }
			auto ragdoll = std::make_unique<RagdollComponent>(*geometry, *m_Movement);   // before the animation: the physics world outlives the animator
			auto animation = std::make_unique<AnimationComponent>(*geometry, *m_Movement);
			ragdoll->SetAnimation(animation.get());
			animation->SetRagdoll(ragdoll.get());
			animation->SetGameplay(m_Ability->Gameplay());
			AnimationComponent::Setup setup;
			if (spec.Manifest) setup.Base = *spec.Manifest;
			setup.Bindings = spec.Bindings;
			setup.Skeleton = spec.Skeleton;
			setup.Rigged = spec.Rigged;
			setup.Steps = m_Steps;
			if (!animation->Build(std::move(setup), error)) return false;
			auto props = std::make_unique<PropsComponent>(*geometry, *animation);
			m_Geometry = &Adopt(std::move(geometry));
			m_Ragdoll = &Adopt(std::move(ragdoll));
			m_Animation = &Adopt(std::move(animation));
			m_Props = &Adopt(std::move(props));
		}
		m_Built = true;
		SyncTransform();
		return true;
	}

	void Actor::SetHeight(float height)
	{
		const float factor = m_Movement->SetHeight(height);
		if (factor > 0.0f && m_Geometry) m_Geometry->ScaleBy(factor);
		SyncTransform();
	}

	void Actor::Teleport(float x, float z, float yaw)
	{
		m_Movement->Teleport(x, z, yaw);
		SyncTransform();
	}

	void Actor::SyncTransform()
	{
		if (!m_Movement) return;
		Transform.Position = m_Movement->Controller().GetPosition();
		Transform.Yaw = m_Movement->Controller().GetYaw();
		Transform.Scale = m_Geometry ? m_Geometry->Scale() : 1.0f;
	}

	// ---- save

	void Actor::CaptureState(Save::ActorState& state) const
	{
		state.Name = Name();
		state.Kind = Kind();
		state.Position = GetPosition();
		state.Yaw = GetYaw();
		ForEach<IStateSaver>([&state](const IStateSaver& saver) { saver.CaptureState(state); });
	}

	void Actor::RestoreState(const Save::ActorState& state)
	{
		// what it was doing before the load is not in the save: an actor that already existed (a replay played in the world it was recorded in)
		// would go on with the old run's tasks, or stay suspended as an abstract member, where a new one stands idle; both start like the new one
		StopTasks();
		OnResume();
		Teleport(state.Position.x, state.Position.z, state.Yaw);
		ForEach<IStateSaver>([&state](IStateSaver& saver) { saver.RestoreState(state); });
	}

	// ---- ragdoll

	bool Actor::StartRagdoll(const glm::vec3& extraVelocity)
	{
		if (!m_Ragdoll || !Anim() || Ragdolled()) return false;
		if (AbilitySystemComponent* a = Abilities()) a->CancelAll();
		ReleaseOrders();
		const MotionState& motion = m_Movement->Motion();
		return m_Ragdoll->Start(motion.Velocity + glm::vec3(0.0f, motion.VerticalSpeed, 0.0f) + extraVelocity);
	}

	bool Actor::Blast(const glm::vec2& xz, float radius, float speed)
	{
		if (!m_Ragdoll || !Anim()) return false;
		const glm::vec3 p = GetPosition();
		if (!Ragdolled() && glm::distance(glm::vec2(p.x, p.z), xz) < radius) StartRagdoll();
		return m_Ragdoll->Explode(xz, radius, speed);
	}

	// ---- abilities

	void Actor::SetAbilities(GameplayWorld* gameplay, const std::string& name)
	{
		m_Ability->Link(gameplay, name);
		if (m_Animation) { m_Animation->SetGameplay(gameplay); m_Animation->SyncAbilities(); }
	}

	void Actor::PrepareAbilities()
	{
		m_Ability->Prepare(Anim(), Ragdolled(), m_Movement->Controller(), m_Movement->World(), m_Interactions);
	}

	bool Actor::UseAbility(const std::string& ability, const std::string& entity, std::string& error)
	{
		if (!Abilities()) { error = "no ability system for '" + AbilityName() + "' (garden build)"; return false; }
		ReleaseOrders();
		return StartAbility(ability, entity, error);
	}

	bool Actor::StartAbility(const std::string& ability, const std::string& entity, std::string& error)
	{
		AbilitySystemComponent* asc = Abilities();
		if (!asc) { error = "no ability system for '" + AbilityName() + "' (garden build)"; return false; }
		PrepareAbilities();   // the character is lent to the abilities before this one starts, even in the frame the actor appeared
		return asc->Activate(ability, entity.empty() ? nullptr : m_Ability->Gameplay()->Find(entity), &error, entity);
	}

	TaskRunner::AbilityHandler Actor::AbilityHandler()
	{
		TaskRunner::AbilityHandler h;
		h.Start = [this](const std::string& ability, const std::string& entity, std::string& error) { return StartAbility(ability, entity, error); };
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

	// ---- orders

	bool Actor::RunTasks(const std::string& text, std::string& error)
	{
		if (!m_Built) { error = "the actor is not built"; return false; }
		return m_Orders->RunTasks(text, error);
	}

	bool Actor::Interact(const std::string& entity, const std::string& point, std::string& error)
	{
		return RunTasks("interact " + entity + (point.empty() ? "" : " " + point), error);
	}

	// ---- the frame

	void Actor::Update(float dt)
	{
		UpdateBody(dt);
		SyncTransform();
	}

	void Actor::UpdateBody(float dt)
	{
		if (!m_Built || !m_Movement)   // (not built, or its components are gone: destroyed)
			return;
		if (m_Ragdoll && m_Ragdoll->Simulating())
		{
			// the actor is a ragdoll: the physics world moves it, the controller follows the pelvis (so the camera and the pose's root do too)
			Stopwatch watch(MeasureCost, m_Cost.Ragdoll);
			++m_Cost.Frames;
			m_Orders->ForgetInput();
			m_Ragdoll->Step(dt);
			m_Movement->Track(dt);
			m_Animation->UpdateAnimator(m_Movement->Motion(), dt);
			if (AutoGetUp && m_Ragdoll->AdvanceRest(dt) > 1.5f) GetUp();
			return;
		}
		++m_Cost.Frames;
		std::unique_ptr<Stopwatch> control(new Stopwatch(MeasureCost, m_Cost.Control));
		const OrdersFrame frame{ m_Movement->Controller(), m_Movement->World(), Anim(), m_Interactions, KeysTakeOver, Navigation, Name() };
		CharacterInput in = m_Orders->Choose(frame, dt);
		if (AbilitySystemComponent* asc = Abilities(); asc && asc->Controlling()) in = asc->Input();   // an ability is walking or animating the character
		if (UsesLocomotionBlend() && m_Movement->HasOwnSpeeds()) m_Movement->LimitSpeeds(m_Animation->SpeedLimit());
		m_Orders->Note(in);
		m_Movement->Step(in, dt);
		control.reset();
		if (!m_Animation)
			return;
		Stopwatch animation(MeasureCost, m_Cost.Animation);
		const MotionState& motion = m_Movement->Motion();
		if (Anim() && RagdollLandingSpeed > 0.0f && motion.Landed && motion.LandingSpeed > RagdollLandingSpeed)
		{
			m_Animation->UpdateAnimator(motion, dt);   // the last animated frame: the ragdoll starts from this pose
			StartRagdoll(glm::vec3(0.0f, -0.6f * motion.LandingSpeed, 0.0f));
			return;
		}
		m_Animation->Animate(motion, dt, GetSpeed());
		m_Props->ResolvePending();
	}

	std::string Actor::Describe() const
	{
		if (!m_Built)
			return Name() + " is not built";
		const glm::vec3 p = GetPosition();
		std::ostringstream out;
		out << std::fixed << std::setprecision(1) << Name() << " at (" << static_cast<double>(p.x) << ", " << static_cast<double>(p.y) << ", " << static_cast<double>(p.z) << "), "
			<< std::setprecision(2) << static_cast<double>(GetSpeed()) << " m/s";
		if (m_Animation) out << m_Animation->Describe(m_Steps, m_Movement->Controller().IsAirborne(), m_Orders->TaskCount() > 0 ? m_Orders->TaskStatus() : std::string());
		out << m_Orders->SourceTag();
		return out.str();
	}
}
