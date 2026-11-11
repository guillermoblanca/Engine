#include "WaterRenderer.h"
#include "WaterRipples.h"
#include "Shaders.h"
#include "GB/World/Water/LakeMesh.h"
#include "GB/World/Water/WaterWaves.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace GB
{
	WaterRenderer::WaterRenderer()
		: m_Shader(
			InjectWaveSet(Shaders::Assemble(Shaders::WaterVS)),
			InjectWaveSet(Shaders::Assemble(Shaders::WaterFS)))
	{
		RHI::Device& device = RHI::GetDevice();
		RHI::PipelineDesc pipeline;
		pipeline.Program = m_Shader.GetHandle();
		pipeline.DepthTest = true;
		pipeline.DepthWrite = false;
		pipeline.CullMode = RHI::Cull::None;
		pipeline.Blend = RHI::BlendState{ true, RHI::BlendFactor::SrcAlpha, RHI::BlendFactor::OneMinusSrcAlpha, {} };
		m_Pipeline = device.CreatePipeline(pipeline);
		RHI::PipelineDesc end;
		end.DepthWrite = true;
		end.Blend = RHI::BlendState{};
		m_End = device.CreatePipeline(end);
	}

	WaterRenderer::~WaterRenderer()
	{
		ReleaseLakes();
		RHI::Device& device = RHI::GetDevice();
		device.DestroyPipeline(m_Pipeline);
		device.DestroyPipeline(m_End);
	}

	const Mesh& WaterRenderer::MeshFor(const WaterSurface& surface)
	{
		const std::shared_ptr<const WaterGeometry>& geometry = surface.Geometry();
		const auto found = m_Meshes.find(geometry.get());
		if (found != m_Meshes.end() && !found->second.Source.expired())
			return *found->second.Gpu;

		// A geometry that is gone leaves its mesh behind: drop those before adding another.
		for (auto it = m_Meshes.begin(); it != m_Meshes.end();)
			it = it->second.Source.expired() ? m_Meshes.erase(it) : std::next(it);

		CachedMesh entry;
		entry.Source = geometry;
		entry.Gpu = geometry->IsRiver()
			? std::make_unique<Mesh>(geometry->RiverVertices, geometry->RiverIndices)
			: std::make_unique<Mesh>(Mesh::CreateGrid(geometry->GridSize, geometry->GridSubdivisions));
		return *(m_Meshes[geometry.get()] = std::move(entry)).Gpu;
	}

	void WaterRenderer::ReleaseLakes()
	{
		RHI::Device& device = RHI::GetDevice();
		device.DestroyVertexLayout(std::exchange(m_Lakes.Layout, {}));
		device.DestroyBuffer(std::exchange(m_Lakes.Vertices, {}));
		device.DestroyBuffer(std::exchange(m_Lakes.Indices, {}));
		m_Lakes.Source.reset();
		m_Lakes.Key = nullptr;
	}

	const WaterRenderer::LakeGpu* WaterRenderer::LakesFor(const std::shared_ptr<const LakeMesh>& lakes)
	{
		if (!lakes || lakes->Empty())
			return nullptr;
		if (m_Lakes.Key == lakes.get() && !m_Lakes.Source.expired())
			return &m_Lakes;

		// one mesh at a time (a world has one): a new one replaces the buffers of the last
		ReleaseLakes();
		RHI::Device& device = RHI::GetDevice();
		m_Lakes.Source = lakes;
		m_Lakes.Key = lakes.get();
		m_Lakes.Vertices = device.CreateBuffer(lakes->Vertices.size() * sizeof(LakeVertex), lakes->Vertices.data(), false);
		m_Lakes.Indices = device.CreateBuffer(lakes->Indices.size() * sizeof(uint32_t), lakes->Indices.data(), false);
		RHI::VertexLayoutDesc layout;
		layout.Indices = m_Lakes.Indices;
		layout.Bindings.push_back({ 0, m_Lakes.Vertices, 0, static_cast<uint32_t>(sizeof(LakeVertex)), 0 });
		layout.Attribs.push_back({ 0, 3, RHI::AttribType::Float, static_cast<uint32_t>(offsetof(LakeVertex, Position)), 0 });
		layout.Attribs.push_back({ 1, 3, RHI::AttribType::Float, static_cast<uint32_t>(offsetof(LakeVertex, Normal)), 0 });
		layout.Attribs.push_back({ 4, 1, RHI::AttribType::UInt, static_cast<uint32_t>(offsetof(LakeVertex, Lake)), 0 });
		layout.Attribs.push_back({ 5, 1, RHI::AttribType::Float, static_cast<uint32_t>(offsetof(LakeVertex, Depth)), 0 });
		layout.Attribs.push_back({ 6, 1, RHI::AttribType::Float, static_cast<uint32_t>(offsetof(LakeVertex, ShoreDistance)), 0 });
		m_Lakes.Layout = device.CreateVertexLayout(layout);

		// The grid's spacing, from the first triangle (the mesh lies on the terrain's samples: its edges are one spacing along x and z).
		float spacing = 0.0f;
		for (size_t t = 0; t + 2 < lakes->Indices.size() && spacing <= 0.0f; t += 3)
			for (int e = 0; e < 3; ++e)
			{
				const glm::vec3& a = lakes->Vertices[lakes->Indices[t + static_cast<size_t>(e)]].Position;
				const glm::vec3& b = lakes->Vertices[lakes->Indices[t + static_cast<size_t>((e + 1) % 3)]].Position;
				const float d = std::max(std::fabs(a.x - b.x), std::fabs(a.z - b.z));
				if (d > 1e-4f) spacing = spacing > 0.0f ? std::min(spacing, d) : d;
			}
		m_Lakes.Spacing = spacing > 0.0f ? spacing : 1.0f;
		return &m_Lakes;
	}

	void WaterRenderer::SetLook(const WaterParams& p)
	{
		const glm::vec2 dir = glm::length(p.Direction) > 1e-4f ? glm::normalize(p.Direction) : glm::vec2(1.0f, 0.0f);
		m_Shader.SetVec2("uWaveDir", dir);
		m_Shader.SetFloat("uWaveHeight", p.WaveHeight);
		m_Shader.SetFloat("uWaveLength", p.WaveLength);
		m_Shader.SetFloat("uSteep", p.Steepness);
		m_Shader.SetFloat("uChop", p.Choppiness);
		m_Shader.SetVec3("uShallow", p.ShallowColor);
		m_Shader.SetVec3("uDeep", p.DeepColor);
		m_Shader.SetFloat("uClarity", p.Clarity);
		m_Shader.SetFloat("uRoughness", p.Roughness);
		m_Shader.SetFloat("uFoam", p.Foam);
		m_Shader.SetFloat("uFoamWidth", p.FoamWidth);
		m_Shader.SetFloat("uRippleStrength", p.RippleStrength);
	}

	void WaterRenderer::Render(const Scene& scene, const Framebuffer& sceneMSAA, const Framebuffer& sceneResolved, const Framebuffer& sceneDepth)
	{
		const LakeGpu* lakes = LakesFor(scene.Lakes);
		if (scene.Water.empty() && !lakes)
			return;

		// The water reads the depth of everything opaque to measure how deep it is.
		Framebuffer::BlitDepth(sceneMSAA, sceneDepth);
		Framebuffer::BlitColor(sceneMSAA, sceneResolved);   // what the water refracts
		RHI::CommandList& cmd = RHI::GetDevice().Commands();
		cmd.BeginPass(sceneMSAA.Pass("water"));
		cmd.BindPipeline(m_Pipeline);
		const bool ssr = scene.ScreenSpaceReflections;
		m_Shader.SetFloat("uSSR", ssr ? 1.0f : 0.0f);
		cmd.BindTexture(1, sceneDepth.DepthTexture());
		cmd.BindTexture(3, sceneResolved.ColorTexture());
		const bool ripples = scene.Ripples && scene.Ripples->IsValid();
		if (ripples)
		{
			m_Shader.SetVec4("uRippleRect", scene.Ripples->GetRect());
			cmd.BindTexture(2, { scene.Ripples->GetTexture() });
		}
		else
			m_Shader.SetVec4("uRippleRect", glm::vec4(0.0f));

		// Which lake surfaces the mesh stands for: a disc at a lake's centre and level (MakeLakeSurface) is that lake. Its look is the
		// one drawn (the editor tunes the surfaces' Params live); a lake with no surface keeps the look the mesh was built with.
		std::vector<const WaterParams*> looks;
		auto bodyOf = [&](const WaterSurface& water) -> int
		{
			if (!lakes || water.IsRiver()) return -1;
			const LakeMesh& mesh = *scene.Lakes;
			for (size_t k = 0; k < mesh.Bodies.size(); ++k)
			{
				const WaterBodyParams& body = mesh.Bodies[k];
				if (k < mesh.Ranges.size() && mesh.Ranges[k].IndexCount > 0 && std::fabs(body.Level - water.Position.y) < 1e-3f &&
					std::fabs(body.Center.x - water.Position.x) < 0.01f && std::fabs(body.Center.y - water.Position.z) < 0.01f)
					return static_cast<int>(k);
			}
			return -1;
		};
		if (lakes)
		{
			const LakeMesh& mesh = *scene.Lakes;
			looks.assign(mesh.Bodies.size(), nullptr);
			for (const WaterSurface* water : scene.Water)
				if (water)
					if (const int k = bodyOf(*water); k >= 0) looks[static_cast<size_t>(k)] = &water->Params;

			m_Shader.SetVec3("uOrigin", glm::vec3(0.0f));   // the mesh is in world space
			m_Shader.SetInt("uRiver", 0);
			m_Shader.SetInt("uLakeMesh", 1);
			m_Shader.SetFloat("uLakeSpacing", lakes->Spacing);
			m_Shader.SetFloat("uMask", 0.0f);
			cmd.BindVertexLayout(lakes->Layout);
			for (size_t k = 0; k < mesh.Bodies.size() && k < mesh.Ranges.size(); ++k)
			{
				const LakeMesh::Range& range = mesh.Ranges[k];
				if (range.IndexCount == 0) continue;
				const WaterBodyParams& body = mesh.Bodies[k];
				m_Shader.SetFloat("uOriginY", body.Level);
				m_Shader.SetVec3("uOriginXZ", glm::vec3(body.Center.x, body.Level, body.Center.y));
				SetLook(looks[k] ? *looks[k] : body.Look);
				cmd.DrawIndexed(range.FirstIndex, range.IndexCount);
				DrawCounter::Add(range.IndexCount / 3);
			}
		}

		m_Shader.SetInt("uLakeMesh", 0);
		for (const WaterSurface* water : scene.Water)
		{
			if (!water || bodyOf(*water) >= 0)
				continue;
			m_Shader.SetVec3("uOrigin", water->Position);
			m_Shader.SetInt("uRiver", water->IsRiver() ? 1 : 0);
			m_Shader.SetFloat("uOriginY", water->Position.y);
			m_Shader.SetVec3("uOriginXZ", water->Position);
			m_Shader.SetFloat("uMask", water->Radius);
			SetLook(water->Params);
			MeshFor(*water).Draw();
		}
		cmd.BindPipeline(m_End);
	}
}
