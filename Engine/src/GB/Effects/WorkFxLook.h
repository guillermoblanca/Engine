#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// A45: the effects of the field work and of walking through a crop, as data (visuals.json "workFx", read by GB/Visual/VisualDefs).
	// CPU only, no renderer types: the descriptions. GB/Effects/WorkEffects.h turns them into particle systems and plant pushers.

	// One layer of particles of a work (the clods a hoe throws, the dust they raise, the straw a sickle cuts). Ranges are {min, max}.
	struct WorkParticleLayer
	{
		std::string Sprite = "soft";                          // "soft" (a soft disc), "chunk" (a solid ragged bit: clods, straw) or "smoke" (a puff: dust)
		glm::vec4 Color{ 0.42f, 0.31f, 0.21f, 0.9f };        // linear RGBA at birth (the particles are unlit: the colour is what is seen)
		glm::vec4 EndColor{ 0.42f, 0.31f, 0.21f, 0.0f };     // ... and at death
		glm::vec2 Size{ 0.04f, 0.08f };                      // metres
		float EndSize = 1.0f;                                // size at death over size at birth
		glm::vec2 Life{ 0.5f, 0.9f };                        // seconds
		glm::vec2 Speed{ 1.0f, 2.5f };                       // m/s
		float Cone = 30.0f;                                  // degrees around the throw direction
		float Lean = 0.6f;                                   // the throw direction: up, leaning this much toward the worker's front (0 = straight up)
		float Gravity = 1.0f;                                // 1 = real gravity; > 0 also lands them on the ground
		float Drag = 0.5f;                                   // velocity lost per second
		float Bounce = 0.0f;                                 // restitution on the ground (0: they die where they land)
		float Spin = 0.0f;                                   // degrees per second, either way
		float Rate = 30.0f;                                  // particles per second while the work's clock is inside Window
		glm::vec2 Window{ 0.0f, 1.0f };                      // fractions of the work's cycle (its montage's run): from > to wraps round
		float Reach = 0.5f;                                  // metres in front of the worker where they come from
		float Radius = 0.15f;                                // ... spread over a disc this wide
		float Height = 0.05f;                                // ... this high above its feet
		bool operator==(const WorkParticleLayer&) const = default;
	};

	// The effect of one activity ("farm.till"): its layers, and the length of a cycle when the worker plays no montage (a debug burst).
	struct WorkFx
	{
		std::vector<WorkParticleLayer> Layers;
		float Cycle = 1.2f;
		bool operator==(const WorkFx&) const = default;
	};

	// The plants that lean away from someone walking through them (the materials with "push", lib/tree_wind): who pushes, how far and how much.
	struct PushLook
	{
		float Radius = 0.7f;       // metres around a body where the plants lean
		float Bend = 0.35f;        // metres the tip of a plant moves at full lean (at its material's windHeight)
		float Down = 0.12f;        // ... and how much it sinks with it (so a stalk bends, not stretches)
		float Lag = 0.35f;         // seconds: the lean follows the body this late, and lets go as slowly (the plants recover)
		bool operator==(const PushLook&) const = default;
	};

	struct WorkFxLook
	{
		std::map<std::string, WorkFx> Work;   // by activity key ("farm.till", "farm.sow", "farm.harvest": GB/Visual/PropVisuals ActivityKey)
		PushLook Push;
		float Distance = 60.0f;               // metres from the viewer: farther, no particles and no pushers
		uint32_t MaxParticles = 600;          // per layer
		bool operator==(const WorkFxLook&) const = default;
	};
}
