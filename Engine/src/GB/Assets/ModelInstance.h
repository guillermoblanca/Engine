#pragma once
#include "Model.h"
#include <string>
#include <vector>

namespace GB
{
	// Runtime state of a Model: animation playback, the current pose and joint palettes.
	// Many instances can share one Model.
	class ModelInstance
	{
	public:
		explicit ModelInstance(const Model& model);

		// Starts a clip. `fadeSeconds` > 0 cross-fades from whatever is playing now.
		// Returns false (and keeps the current state) for an unknown clip.
		bool Play(int clip, bool loop = true, float fadeSeconds = 0.2f);
		bool Play(const std::string& clipName, bool loop = true, float fadeSeconds = 0.2f);
		void Stop();    // back to the rest pose

		void SetPaused(bool paused) { m_Paused = paused; }
		bool IsPaused() const { return m_Paused; }
		void SetSpeed(float speed) { m_Speed = speed; }
		float GetSpeed() const { return m_Speed; }
		// Jumps to a time in the current clip and re-evaluates the pose.
		void SetTime(float seconds);
		float GetTime() const { return m_Current.Time; }
		int GetCurrentClip() const { return m_Current.Clip; }
		bool IsFinished() const;   // a non-looping clip reached its end

		// Advances the animation and recomputes the pose.
		void Update(float deltaTime);

		// Appends the current pose to `scene`. The joint palettes are owned by this instance, which
		// must outlive the scene's use.
		void AddToScene(Scene& scene, const glm::mat4& root = glm::mat4(1.0f)) const;

		const Model& GetModel() const { return m_Model; }
		// Camera `index` of Model::GetCameras() in its current (possibly animated) pose, transformed by `root`.
		// glTF cameras look down their local -Z axis. Returns false for an invalid index.
		bool GetCamera(int index, Camera& out, const glm::mat4& root = glm::mat4(1.0f)) const;
		// World matrix of a node in model space (current pose).
		const glm::mat4& GetNodeWorld(int node) const { return m_World[node]; }
		// Joint palette for the skin used by `node` (joint world * inverse bind), model space.
		const std::vector<glm::mat4>& GetJointMatrices(int node) const { return m_Palettes[node]; }

	private:
		struct Pose
		{
			glm::vec3 T{ 0.0f };
			glm::quat R{ 1.0f, 0.0f, 0.0f, 0.0f };
			glm::vec3 S{ 1.0f };
		};
		struct Playhead
		{
			int Clip = -1;
			float Time = 0.0f;
			bool Loop = true;
		};

		void Advance(Playhead& head, float dt) const;
		void Sample(const Playhead& head, std::vector<Pose>& pose) const;
		void Evaluate();

		const Model& m_Model;
		Playhead m_Current, m_Previous;
		float m_FadeDuration = 0.0f, m_FadeElapsed = 0.0f;
		float m_Speed = 1.0f;
		bool m_Paused = false;

		std::vector<Pose> m_Rest, m_PoseA, m_PoseB;
		std::vector<glm::mat4> m_World;
		std::vector<std::vector<glm::mat4>> m_Palettes;   // indexed by node (empty if the node has no skin)
	};
}
