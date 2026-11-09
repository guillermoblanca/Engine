#pragma once
#include "Environment.h"
#include "Light.h"
#include "PostSettings.h"
#include "RenderObject.h"
#include "RenderStats.h"
#include "GB/Renderer/Particles.h"
#include "GB/World/Water.h"
#include <vector>

namespace GB
{
	class GrassGpu;            // GB/Renderer/GrassGpu.h: the GPU side of a GrassField
	class GroundInteraction;   // GB/Renderer/GroundInteraction.h: the marks feet and wheels leave in the ground
	class WaterRipples;        // GB/Renderer/WaterRipples.h: the ripples on the water around the camera

	struct Scene
	{
		PostSettings Post;
		std::vector<RenderObject> Objects;
		DirectionalLight Sun;
		std::vector<PointLight> PointLights;
		std::vector<const ParticleSystem*> Particles;
		std::vector<ParticleBatch> ParticleBatches;
		const GroundInteraction* Ground = nullptr;
		std::vector<const GrassGpu*> Grass;
		std::vector<const WaterSurface*> Water;
		const WaterRipples* Ripples = nullptr;
		bool ScreenSpaceReflections = true;
		// Quality knobs for slow GPUs (the editor's `quality minimum`): how far things are drawn. The defaults change nothing.
		float DrawDistance = 0.0f;                      // > 0: objects with bounds (trees, rocks, props) farther than this are skipped, on top of their own MaxDistance
		float GrassDistanceScale = 1.0f;                // multiplies the draw distance of every grass field
		Environment Env;
	};
}
