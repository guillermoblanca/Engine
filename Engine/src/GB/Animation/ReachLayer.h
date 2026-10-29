#pragma once
#include "GB/Animation/AnimLayer.h"
#include "GB/Animation/Rig.h"
#include <glm/glm.hpp>

namespace GB
{
	class Model;

	// Hands that reach a point of the world during an action: the campfire's embers when stoking, the well's rope, the door's handle, the trunk
	// where the axe lands, the top of the workbench. A game layer (AnimInstance::AddLayer) over the clips and montages: each hand with a target
	// is pulled onto it with two-bone IK (the elbow bends the way the clip had it), the weight easing in and out so the hand leaves the clip
	// smoothly and comes back to it. The target is in the model's space (the host converts it from the world with the actor's matrix).
	class ReachLayer : public IAnimLayer
	{
	public:
		ReachLayer(const Model& model, const HumanoidRig& rig) : m_Model(model), m_Rig(rig) {}

		// `weight` 0..1 is where the hand's weight goes (it eases there at Speed per second); 0 lets go.
		void Reach(bool right, const glm::vec3& target, float weight);
		void Release() { m_Hand[0].Wanted = m_Hand[1].Wanted = 0.0f; }
		float Weight(bool right) const { return m_Hand[right ? 1 : 0].Weight; }
		float Speed = 3.0f;

		void Update(const MotionState& state, float dt, const std::string& set) override;
		bool Active() const override { return m_Hand[0].Weight > 0.0f || m_Hand[1].Weight > 0.0f; }
		void Apply(Pose& pose) override;
		float LegsWeight() const override { return 0.0f; }
		bool Procedural() const override { return true; }   // (IK: skipped with the IK at a lower level of detail, H16)

	private:
		struct Hand { glm::vec3 Target{ 0.0f }; float Wanted = 0.0f, Weight = 0.0f; };
		const Model& m_Model;
		HumanoidRig m_Rig;
		Hand m_Hand[2];   // left, right
		std::vector<glm::mat4> m_World;
	};
}
