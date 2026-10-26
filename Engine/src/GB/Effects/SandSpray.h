#pragma once
#include "GB/Renderer/Camera.h"
#include "GB/Renderer/Particles.h"
#include "GB/Renderer/Renderer.h"
#include "GB/World/Terrain.h"
#include <memory>

namespace GB
{
	// Sand and dust blown off the dune crests by the wind: grains streaming downwind plus faint dust puffs.
	// Emission happens at the crest points of a dune Terrain near the camera.
	class SandSpray
	{
	public:
		explicit SandSpray(const Terrain& terrain, uint32_t seed = 1);

		float Rate = 1400.0f;        // grains per second inside the radius (at Intensity 1)
		float Radius = 80.0f;        // only crests within this distance of the camera emit
		float Intensity = 1.0f;      // 0..1

		void SetWind(const glm::vec3& velocity) { m_Wind = velocity; }
		void Update(float dt, const Camera& camera);
		void AddToScene(Scene& scene) const;

		ParticleSystem& GetGrains() { return *m_Grains; }
		ParticleSystem& GetPuffs() { return *m_Puffs; }

	private:
		float Random();

		const Terrain& m_Terrain;
		std::unique_ptr<ParticleSystem> m_Grains, m_Puffs;
		glm::vec3 m_Wind{ 8.0f, 0.0f, 3.0f };
		float m_Carry = 0.0f, m_PuffCarry = 0.0f, m_Time = 0.0f;
		uint64_t m_Rng;
	};
}
