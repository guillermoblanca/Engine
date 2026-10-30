#include "GB/Game/Components/Presentation/AnimationComponent.h"
#include "GB/Assets/AssetPath.h"
#include "GB/Game/Components/Physics/RagdollComponent.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace GB
{
	namespace
	{
		ClipExists ClipsOf(const Model* model)
		{
			return model ? ClipExists([model](const std::string& c) { return model->FindClip(c) >= 0; }) : ClipExists();
		}
	}

	bool AnimationComponent::Build(Setup setup, std::string& error)
	{
		if (!m_Geometry.GetModel() || !m_Geometry.Instance()) { error = "no model"; return false; }
		m_BaseManifest = std::move(setup.Base);
		m_Bindings = std::move(setup.Bindings);
		m_Skeleton = std::move(setup.Skeleton);
		m_Steps = setup.Steps;
		m_Manifest = EffectiveManifest();
		if (setup.Rigged) return BuildAnimator(error);
		m_Animation = CharacterAnimation::ForModel(*m_Geometry.GetModel());
		m_Animation.Start(*m_Geometry.Instance());
		return true;
	}

	bool AnimationComponent::BuildAnimator(std::string& error)
	{
		const Model& model = *m_Geometry.GetModel();
		m_Profile = AnalyzeCharacter(model);
		LocomotionOptions options;
		if (m_Profile.Humanoid()) options.Analysis = m_Profile.Analysis;   // this skeleton's own bone names
		auto animator = std::make_unique<CharacterAnimator>(model, *m_Geometry.Instance(), options);
		if (animator->AddDefaultLocomotionClips() < 2 && animator->AddRoleClips(m_Profile) < 2)
		{
			error = "the character has no locomotion clips (idle, walk, run...): put the animation FBX files next to it (see character_check)";
			return false;
		}
		m_ManifestReport = animator->LoadManifest(m_Manifest);   // the sets, turns, jump and montages the folder has clips for
		// which montage each ability plays is the manifest's (its "abilities"), among those this character has clips for
		animator->SetFootsteps(m_Steps);
		if (animator->Anim().Turn().HasClips("")) m_Movement.Controller().Config.TurnInPlaceAngle = 70.0f;   // the character has a turn on the spot to show
		m_Animator = std::move(animator);
		SyncAbilities();
		ApplyFeet();
		return true;
	}

	AnimManifest AnimationComponent::EffectiveManifest() const
	{
		AnimManifest manifest = m_BaseManifest;
		if (m_Bindings && m_Bindings->Applies(m_Skeleton) && (!m_Bindings->Bindings.Bindings.empty() || !m_Bindings->Bindings.Transitions.empty() || !m_Bindings->Bindings.Locomotion.empty()))
			ApplyBindings(manifest, m_Bindings->Bindings, m_Bindings->Defaults, m_Skeleton, ClipsOf(m_Geometry.GetModel().get()));
		return manifest;
	}

	bool AnimationComponent::SetBindings(std::shared_ptr<const BindingsSetup> setup, const std::string& skeleton)
	{
		m_Bindings = std::move(setup);
		m_Skeleton = skeleton;
		AnimManifest next = EffectiveManifest();
		if (AnimManifestToJson(next) == AnimManifestToJson(m_Manifest)) return false;
		m_Manifest = std::move(next);
		if (m_Animator)
		{
			std::string error;
			RebuildAnimator(error);
		}
		return true;
	}

	void AnimationComponent::SyncAbilities()
	{
		if (!m_Gameplay || !m_Animator) return;
		ApplyManifestAbilities(m_Gameplay->Library, m_Manifest, [this](const std::string& m) { return m_Animator->Anim().Montages().Has(m); });
		RegisterAbilityMontages(m_Gameplay->Library, m_Animator->Anim(), m_Manifest.Montages);   // the same montages plus the notifies the abilities need
	}

	std::string AnimationComponent::PickMontage(const AbilityDef& ability, int use, const std::map<std::string, std::string>& context) const
	{
		if (!m_Bindings || !m_Bindings->Applies(m_Skeleton) || !m_Animator) return {};
		const ResolvedClip r = ResolveClip(m_Bindings->Bindings, m_BaseManifest, m_Bindings->Defaults, "ability:" + ability.Name, context, m_Skeleton, use, ClipsOf(m_Geometry.GetModel().get()));
		return r.FromBinding && !r.Playable.empty() && m_Animator->Anim().Montages().Has(r.Playable) ? r.Playable : std::string();
	}

	bool AnimationComponent::RebuildAnimator(std::string& error)
	{
		if (!m_Animator) return true;
		m_Animator.reset();
		if (m_Ragdoll) m_Ragdoll->Drop();   // the ragdoll lived in the animator and its physics world goes after it
		return BuildAnimator(error);
	}

	bool AnimationComponent::LoadManifestFile(const std::string& path, std::string& report)
	{
		std::ifstream in(path);
		if (!in) { report = "could not read " + path; return false; }
		std::stringstream text;
		text << in.rdbuf();
		AnimManifest manifest;
		std::string error;
		if (!AnimManifestFromJson(text.str(), manifest, &error)) { report = error; return false; }
		return SetManifest(std::move(manifest), report, path);
	}

	bool AnimationComponent::SetManifest(AnimManifest manifest, std::string& report, const std::string& source)
	{
		const std::vector<std::string> problems = ValidateManifest(manifest);
		if (!problems.empty())
		{
			report = "the manifest has problems:";
			for (const std::string& p : problems) report += "\n  " + p;
			return false;
		}
		m_BaseManifest = std::move(manifest);
		m_Manifest = EffectiveManifest();
		report = "loaded " + std::to_string(m_Manifest.Sets.size() + 1) + " set(s) and " + std::to_string(m_Manifest.Montages.size()) + " montage(s)" + (source.empty() ? "" : " from " + source);
		if (m_Animator)
		{
			std::string error2;
			if (!BuildAnimator(error2)) { report += "\n" + error2; return false; }
			report += "\n" + ManifestInfo();
		}
		return true;
	}

	bool AnimationComponent::SaveManifestFile(const std::string& path, std::string& report) const
	{
		std::ofstream out(path);
		if (!out) { report = "could not write " + path; return false; }
		out << AnimManifestToJson(m_Manifest);
		report = "saved " + path;
		return out.good();
	}

	std::string AnimationComponent::ManifestInfo() const
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

	bool AnimationComponent::Retarget(const std::string& pathIn, const std::string& assetRoot, std::string& report)
	{
		namespace fs = std::filesystem;
		if (!m_Geometry.GetModel() || !m_Animator) { report = "start the player with an FBX character first (player <character.fbx>)"; return false; }
		Model& target = *m_Geometry.GetModel();
		const std::string path = ResolveAssetPath(pathIn, assetRoot);
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
			const RetargetMap map = BuildRetargetMap(*source, target);
			if (first) report += DescribeRetargetMap(*source, target, map);
			first = false;
			if (!map.Valid()) { ++skipped; continue; }
			added += RetargetAnimations(*source, target, map, &names);
		}
		report += std::to_string(added) + " clip(s) retargeted" + (skipped ? ", " + std::to_string(skipped) + " file(s) skipped (no clips, or a skeleton that cannot be paired)" : "") + "\n";
		m_RetargetReport = report;
		if (added == 0) return false;
		// the new clips take part in the character check and the locomotion
		std::string error;
		if (!RebuildAnimator(error)) { report += error + "\n"; return false; }
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

	void AnimationComponent::ApplyFeet()
	{
		if (!m_Animator) return;
		m_Animator->Anim().Feet().SetGround(m_FeetOn ? m_Movement.World().GroundHeight : std::function<float(float, float)>());
	}

	float AnimationComponent::SpeedLimit() const
	{
		if (!m_Animator) return 1e9f;
		// carrying a box or pushing a barrow the set has a walk and no run: never go faster than its fastest clip shows
		const float fastest = m_Animator->Anim().Active().MaxSpeed();
		const bool setApplies = m_Animator->Anim().Overlay() != AnimInstance::kDefaultSet && fastest > 0.3f;
		return setApplies ? fastest * 1.1f : 1e9f;
	}

	void AnimationComponent::Animate(const MotionState& motion, float dt, float speed)
	{
		ModelInstance* instance = m_Geometry.Instance();
		if (!instance) return;
		if (m_Animator)
			m_Animator->Update(motion, dt, m_Geometry.RootMatrix());   // blend + footsteps, whoever produced the input
		else
		{
			m_Animation.Update(*instance, speed);
			instance->Update(dt);
		}
	}

	std::string AnimationComponent::MontageStatus() const
	{
		if (!m_Animator) return "";
		const MontagePlayer& m = m_Animator->Anim().Montages();
		std::string playing;
		for (const std::string& n : m.Names())
			if (m.IsPlaying(n)) playing += (playing.empty() ? "" : ", ") + n;
		return playing.empty() ? "no montage" : "montage: " + playing;
	}

	void AnimationComponent::StopMontage()
	{
		if (!m_Animator) return;
		m_Animator->Anim().Montages().StopSlot("FullBody");
		m_Animator->Anim().Montages().StopSlot("UpperBody");
	}

	std::string AnimationComponent::Describe(const FootstepSounds* steps, bool airborne, const std::string& tasks) const
	{
		std::ostringstream out;
		out << std::fixed << std::setprecision(2);
		if (m_Animator)
		{
			out << ", locomotion blend [" << m_Animator->Anim().Overlay() << "] (phase " << static_cast<double>(m_Animator->Locomotion().Phase()) << ", speed x"
				<< static_cast<double>(m_Animator->Locomotion().SpeedScale()) << ", steps " << (steps ? steps->Count() : 0) << "):";
			for (const auto& c : m_Animator->Locomotion().Contributions()) out << " " << c.Clip << "=" << static_cast<double>(c.Weight);
			out << " | " << MontageStatus() << (airborne ? ", airborne" : "");
			if (!tasks.empty()) out << " | tasks: " << tasks;
		}
		else
			out << ", " << (m_Animation.State == Locomotion::Idle ? "idle" : m_Animation.State == Locomotion::Walk ? "walking" : "running");
		return out.str();
	}
}
