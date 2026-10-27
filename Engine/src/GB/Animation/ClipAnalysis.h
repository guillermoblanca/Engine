#pragma once
#include "GB/Assets/Model.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace GB
{
	struct ClipAnalysisConfig
	{
		std::string RootBone = "mixamorig:Hips";       // the bone that carries the root motion
		std::string LeftFoot = "mixamorig:LeftFoot";
		std::string RightFoot = "mixamorig:RightFoot";
		std::string LeftHip = "mixamorig:LeftUpLeg";   // with RightHip: the pelvis width gives the facing direction
		std::string RightHip = "mixamorig:RightUpLeg";
		float SampleRate = 60.0f;                      // samples per second of clip
		float PlantHeight = 0.045f;                    // a foot is planted within this height (m) of its lowest point in the clip
		float PlantSpeed = 0.4f;                       // ...and moves slower than this (m/s) over the ground
		float AirHeight = 0.08f;                       // both feet above this height = in the air
	};

	// What a locomotion clip does, measured from its animation: the numbers a blend tree, a root-motion extractor and
	// the footstep audio need (speed, stride, when the feet land).
	struct ClipStats
	{
		std::string Name;
		float Duration = 0.0f;
		glm::vec3 RootDelta{ 0.0f };        // root displacement over the clip (model space, meters)
		float RootSpeed = 0.0f;             // horizontal m/s over the clip
		float RootYawDegrees = 0.0f;        // net turn of the root around Y over the clip
		float RootHeightMin = 0.0f, RootHeightMax = 0.0f;
		float LoopError = 0.0f;             // degrees per bone between the first and the last pose (0 = seamless loop)
		std::vector<float> LeftPlants;      // normalized times (0..1) at which the left foot lands
		std::vector<float> RightPlants;
		float CycleLength = 0.0f;           // meters travelled per gait cycle (one left plant), 0 when there are no steps
		float StepsPerSecond = 0.0f;
		float AirStart = -1.0f, AirEnd = -1.0f;   // normalized time of the flight phase (jumps); -1 when always grounded
		bool Valid = false;                 // false when the bones were not found
	};

	ClipStats AnalyzeClip(const Model& model, int clip, const ClipAnalysisConfig& config = {});
	std::string FormatClipStats(const ClipStats& stats);
}
