// T25: the render frontend on the null RHI backend, without a window or a GL context. The Renderer records a frame into the null device's
// command list; the test reads it back: the pass order, the culling, the instanced draws and the translucent objects back to front. Any GL
// call would crash here (glad is never loaded), so passing also says the frontend only talks to the RHI.
#include "GB/Renderer/Mesh.h"
#include "GB/Renderer/RHI/NullDevice.h"
#include "GB/Renderer/RHI/UniformCache.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Renderer/Scene.h"
#include "GB/Renderer/ShadowView.h"
#include "GB/Renderer/Visibility.h"
#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
		else std::fprintf(stderr, "ok: %s\n", what);
	}

	using GB::RHI::NullCommand;
	using GB::RHI::NullOp;

	GB::RenderObject Object(const GB::Mesh& mesh, const glm::vec3& at)
	{
		GB::RenderObject o;
		o.MeshPtr = &mesh;
		o.Transform = glm::translate(glm::mat4(1.0f), at);
		o.BoundingRadius = 0.9f;
		return o;
	}

	// The draws of one pass of the null device's log: the translation of each uModel set in it (one per draw) and the copies of each
	// instanced draw, in order. The pass runs from its BeginPass to the next BeginPass or colour blit.
	struct PassDraws
	{
		std::vector<glm::vec3> Models;
		std::vector<uint32_t> Copies;
		bool Has(const glm::vec3& at) const { for (const glm::vec3& m : Models) if (m == at) return true; return false; }
	};
	PassDraws DrawsOf(const GB::RHI::NullDevice& device, const char* pass)
	{
		PassDraws out;
		bool inside = false;
		for (const NullCommand& c : device.List.Log)
		{
			if (c.Op == NullOp::BeginPass) { if (inside) break; inside = c.Name == pass; continue; }
			if (!inside) continue;
			if (c.Op == NullOp::BlitColor) break;
			if (c.Op == NullOp::DrawIndexedInstanced) out.Copies.push_back(c.C);
			else if (c.Op == NullOp::SetUniform && device.UniformName({ c.A }, c.Location) == "uModel" && c.Values.size() == 16)
				out.Models.push_back({ c.Values[12], c.Values[13], c.Values[14] });
		}
		return out;
	}

	// T78: the culling shared by the scene and the shadow pass (RenderList) keeps their criteria apart, and the instanced batches come out by
	// mesh address with the first object of each (in scene order) as its head, as the std::map of before did.
	void ShadowAndBatches(GB::RHI::NullDevice& device, GB::Renderer& renderer, const GB::Mesh& cube)
	{
		const GB::Mesh small = GB::Mesh::CreateCube(0.5f), large = GB::Mesh::CreateCube(0.7f);
		GB::Scene scene;
		scene.Sun.Direction = { 0.0f, -1.0f, 0.0f };   // straight down: the shadow box is x and z in [-12, 12] around the origin
		scene.Env.ShadowCenter = { 0.0f, 0.0f, 0.0f };
		scene.Env.ShadowRadius = 12.0f;
		const glm::vec3 inView(0, 0, 0), behind(0, 0, 10), pastDistance(4, 0, -6), outOfBox(0, 0, -40), glass(1, 0, -2), unbounded(50, 0, 50);
		scene.Objects.push_back(Object(cube, inView));                   // both passes
		scene.Objects.push_back(Object(cube, behind));                   // behind the camera, in the box: shadow only
		GB::RenderObject far = Object(cube, pastDistance);              // past its draw distance, in the box: shadow only
		far.MaxDistance = 2.0f;
		scene.Objects.push_back(far);
		scene.Objects.push_back(Object(cube, outOfBox));                 // in view, out of the box: scene only
		GB::RenderObject translucent = Object(cube, glass);             // translucent: scene only, no shadow
		translucent.Mat.Alpha = GB::AlphaMode::Blend;
		scene.Objects.push_back(translucent);
		GB::RenderObject noBounds = Object(cube, unbounded);            // no bounding sphere: never culled
		noBounds.BoundingRadius = 0.0f;
		scene.Objects.push_back(noBounds);
		// two instanced meshes, their copies interleaved: one batch each, the large one's first copy recorded first
		const glm::vec3 largeAt[3] = { { -1, 0, -4 }, { 0, 0, -4 }, { 1, 0, -4 } }, smallAt[2] = { { -2, 0, -3 }, { 2, 0, -3 } };
		auto copy = [&](const GB::Mesh& mesh, const glm::vec3& at) { GB::RenderObject o = Object(mesh, at); o.Instanced = true; scene.Objects.push_back(o); };
		copy(large, largeAt[0]); copy(small, smallAt[0]); copy(large, largeAt[1]); copy(small, smallAt[1]); copy(large, largeAt[2]);

		GB::Camera camera;   // at (0, 2, 8) looking at the origin
		device.List.Log.clear();
		renderer.Render(scene, camera);
		const GB::RenderStats& stats = renderer.GetStats();
		const PassDraws shadow = DrawsOf(device, "shadows"), lit = DrawsOf(device, "scene");

		Check(stats.ObjectsDrawn == 9 && stats.ObjectsCulled == 2, "shared culling: 9 objects drawn, 2 culled (behind, past their distance)");
		Check(lit.Has(inView) && !lit.Has(behind) && !lit.Has(pastDistance) && lit.Has(outOfBox) && lit.Has(glass) && lit.Has(unbounded),
			"scene pass: the camera's frustum and draw distance");
		Check(shadow.Has(inView) && shadow.Has(behind) && shadow.Has(pastDistance) && !shadow.Has(outOfBox) && !shadow.Has(glass) && shadow.Has(unbounded),
			"shadow pass: the light's box alone (objects out of view still cast), no translucent objects");
		Check(stats.ShadowCalls == 6, "shadow pass: 4 single draws and 2 batches");

		// The batches by mesh address, each headed by its first copy in scene order (its uModel), after the single draws.
		const bool smallFirst = std::less<const GB::Mesh*>()(&small, &large);
		const std::vector<uint32_t> copies = smallFirst ? std::vector<uint32_t>{ 2, 3 } : std::vector<uint32_t>{ 3, 2 };
		const glm::vec3 heads[2] = { smallFirst ? smallAt[0] : largeAt[0], smallFirst ? largeAt[0] : smallAt[0] };
		Check(lit.Copies == copies && shadow.Copies == copies, "instanced batches in mesh address order, in both passes");
		Check(lit.Models.size() == 6 && lit.Models[3] == heads[0] && lit.Models[4] == heads[1] && lit.Models[5] == glass,
			"scene pass: the single draws, each batch with its first copy, then the translucent object");
		Check(shadow.Models.size() == 6 && shadow.Models[0] == inView && shadow.Models[1] == behind && shadow.Models[2] == pastDistance &&
			shadow.Models[3] == unbounded && shadow.Models[4] == heads[0] && shadow.Models[5] == heads[1],
			"shadow pass: the single draws in scene order, then each batch with its first copy");
	}

	// T78: a random scene against the loops of before T78, written out here as they were (each pass walking scene.Objects with BoundsOf, the
	// instanced objects grouped in a std::map by mesh): both passes must draw the same objects in the same order, two frames running (the
	// second one reuses the renderer's buffers).
	void SameAsBefore(GB::RHI::NullDevice& device, GB::Renderer& renderer)
	{
		std::vector<GB::Mesh> meshes;
		for (int i = 0; i < 4; ++i) meshes.push_back(GB::Mesh::CreateCube(0.4f + 0.2f * static_cast<float>(i)));
		const std::vector<glm::mat4> joints(1, glm::mat4(1.0f));
		std::mt19937 rng(78);
		auto uniform = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
		auto chance = [&](float p) { return uniform(0.0f, 1.0f) < p; };

		GB::Scene scene;
		scene.DrawDistance = 35.0f;
		scene.Env.ShadowCenter = { 5.0f, 0.0f, -5.0f };
		scene.Env.ShadowRadius = 20.0f;
		for (int i = 0; i < 600; ++i)
		{
			GB::RenderObject o;
			o.MeshPtr = chance(0.03f) ? nullptr : &meshes[static_cast<size_t>(uniform(0.0f, 3.999f))];
			o.Transform = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), { uniform(-45, 45), uniform(-3, 3), uniform(-45, 45) }),
				uniform(0.0f, 6.28f), glm::vec3(0, 1, 0)), glm::vec3(uniform(0.5f, 2.0f)));
			o.BoundingCenter = { uniform(-0.2f, 0.2f), uniform(0.0f, 0.5f), 0.0f };
			o.BoundingRadius = chance(0.05f) ? 0.0f : uniform(0.3f, 1.5f);
			o.MaxDistance = chance(0.4f) ? uniform(5.0f, 50.0f) : 0.0f;
			o.Instanced = chance(0.6f);
			if (chance(0.08f)) o.Mat.Alpha = GB::AlphaMode::Blend;
			else if (chance(0.2f)) o.Mat.Alpha = GB::AlphaMode::Mask;
			if (chance(0.05f)) o.JointMatrices = &joints;
			if (chance(0.05f)) { o.IndexFirst = 6; o.IndexCount = 12; }
			scene.Objects.push_back(o);
		}
		GB::Camera camera;
		camera.Position = { 0.0f, 6.0f, 25.0f };
		camera.Target = { 0.0f, 0.0f, -10.0f };

		// The expected draws of each pass, as the loops of before T78 made them.
		PassDraws litBefore, shadowBefore;
		auto add = [](PassDraws& to, const GB::RenderObject& o, uint32_t copies)
		{
			to.Models.push_back(glm::vec3(o.Transform[3]));
			if (copies > 0) to.Copies.push_back(copies);
		};
		auto flush = [&](PassDraws& to, const std::map<const GB::Mesh*, std::vector<const GB::RenderObject*>>& batches)
		{
			for (const auto& [mesh, objects] : batches)
				add(to, *objects.front(), objects.size() == 1 ? 0u : static_cast<uint32_t>(objects.size()));
		};
		{
			const GB::Frustum light = GB::MakeFrustum(GB::ShadowViewProj(scene.Env.ShadowCenter, scene.Sun.Direction, scene.Env.ShadowRadius, 2048));
			std::map<const GB::Mesh*, std::vector<const GB::RenderObject*>> batches;
			for (const GB::RenderObject& o : scene.Objects)
			{
				if (!o.MeshPtr || o.Mat.Alpha == GB::AlphaMode::Blend) continue;
				glm::vec3 c; float r;
				if (GB::BoundsOf(o, c, r) && !GB::InFrustum(light, c, r)) continue;
				if (o.Instanced && !o.JointMatrices && o.IndexCount == 0) batches[o.MeshPtr].push_back(&o);
				else add(shadowBefore, o, 0);
			}
			flush(shadowBefore, batches);
		}
		{
			const GB::Frustum view = GB::MakeFrustum(camera.GetProjection(320.0f / 180.0f) * camera.GetView());
			std::map<const GB::Mesh*, std::vector<const GB::RenderObject*>> batches;
			std::vector<const GB::RenderObject*> translucent;
			for (const GB::RenderObject& o : scene.Objects)
			{
				if (!o.MeshPtr) continue;
				glm::vec3 c; float r;
				if (GB::BoundsOf(o, c, r))
				{
					float maxDistance = o.MaxDistance;
					if (scene.DrawDistance > 0.0f) maxDistance = maxDistance > 0.0f ? std::min(maxDistance, scene.DrawDistance) : scene.DrawDistance;
					const bool tooFar = maxDistance > 0.0f && glm::distance(c, camera.Position) - r > maxDistance;
					if (tooFar || !GB::InFrustum(view, c, r)) continue;
				}
				if (o.Mat.Alpha == GB::AlphaMode::Blend) translucent.push_back(&o);
				else if (o.Instanced && !o.JointMatrices && o.IndexCount == 0) batches[o.MeshPtr].push_back(&o);
				else add(litBefore, o, 0);
			}
			flush(litBefore, batches);
			auto distance2 = [&](const GB::RenderObject* o) { const glm::vec3 d = glm::vec3(o->Transform[3]) - camera.Position; return glm::dot(d, d); };
			std::sort(translucent.begin(), translucent.end(), [&](const GB::RenderObject* a, const GB::RenderObject* b) { return distance2(a) > distance2(b); });
			for (const GB::RenderObject* o : translucent) add(litBefore, *o, 0);
		}

		for (int frame = 0; frame < 2; ++frame)
		{
			device.List.Log.clear();
			renderer.Render(scene, camera);
			const PassDraws shadow = DrawsOf(device, "shadows"), lit = DrawsOf(device, "scene");
			const bool sameShadow = shadow.Models == shadowBefore.Models && shadow.Copies == shadowBefore.Copies;
			const bool sameLit = lit.Models == litBefore.Models && lit.Copies == litBefore.Copies;
			std::fprintf(stderr, "  frame %d: shadow %zu draws (%zu batches), scene %zu draws (%zu batches)\n", frame, shadow.Models.size(), shadow.Copies.size(),
				lit.Models.size(), lit.Copies.size());
			Check(sameShadow && shadowBefore.Models.size() > 50 && shadowBefore.Copies.size() >= 3,
				"random scene: the shadow pass draws what the loop of before T78 drew, in its order");
			Check(sameLit && litBefore.Models.size() > 50 && litBefore.Copies.size() >= 3,
				"random scene: the scene pass draws what the loop of before T78 drew, in its order");
		}
	}

	int Run()
	{
		GB::RHI::NullDevice device;
		GB::RHI::SetDevice(&device);
		Check(std::string(GB::RHI::GetDevice().Name()) == "Null", "the null device is the current one");

		int result = 0;
		{
			GB::Renderer renderer(320, 180);
			Check(renderer.IsValid(), "the renderer initializes on the null device");
			const GB::Mesh cube = GB::Mesh::CreateCube(1.0f);

			GB::Scene scene;
			scene.Objects.push_back(Object(cube, { 0, 0, 0 }));      // in view
			scene.Objects.push_back(Object(cube, { 0, 0, 30 }));     // behind the camera: culled
			GB::RenderObject far = Object(cube, { 0, 0, -40 });     // in the frustum but past its own draw distance: culled
			far.MaxDistance = 10.0f;
			scene.Objects.push_back(far);
			for (int i = 0; i < 3; ++i)                              // three copies of one mesh: one instanced draw
			{
				GB::RenderObject copy = Object(cube, { -2.0f + 2.0f * static_cast<float>(i), 0, -3 });
				copy.Instanced = true;
				scene.Objects.push_back(copy);
			}
			GB::RenderObject nearGlass = Object(cube, { 1, 0, -5 }), farGlass = Object(cube, { -1, 0, -15 });
			nearGlass.Mat.Alpha = farGlass.Mat.Alpha = GB::AlphaMode::Blend;
			scene.Objects.push_back(nearGlass);                      // recorded first, drawn last (back to front)
			scene.Objects.push_back(farGlass);

			GB::Camera camera;   // at (0, 2, 8) looking at the origin
			device.List.Log.clear();
			renderer.Render(scene, camera);
			const GB::RenderStats& stats = renderer.GetStats();
			const std::vector<NullCommand>& log = device.List.Log;

			Check(stats.ObjectsDrawn == 6 && stats.ObjectsCulled == 2, "culling: 6 objects drawn, 2 culled (behind, too far)");
			Check(stats.InstancedDraws == 1, "the three copies go in one instanced draw");

			// The passes, in order.
			std::vector<std::string> passes;
			for (const NullCommand& c : log)
				if (c.Op == NullOp::BeginPass) passes.push_back(c.Name);
			Check(passes.size() >= 3 && passes.front() == "shadows" && passes[1] == "scene" && passes.back() == "composite",
				"passes: shadows, scene, ..., composite");
			bool bloomBetween = false;
			for (size_t i = 2; i + 1 < passes.size(); ++i) bloomBetween = bloomBetween || passes[i] == "bloom down";
			Check(bloomBetween, "the bloom runs between the scene and the composite");
			const NullCommand* last = nullptr;
			for (const NullCommand& c : log) if (c.Op == NullOp::BeginPass && c.Name == "composite") last = &c;
			Check(last && last->A == 0 && last->B == 320 && last->C == 180, "the composite draws to the back buffer at the renderer's size");

			// The scene pass: its draws and the order of the translucent objects.
			size_t begin = 0, end = log.size();
			for (size_t i = 0; i < log.size(); ++i)
				if (log[i].Op == NullOp::BeginPass && log[i].Name == "scene") { begin = i; break; }
			for (size_t i = begin + 1; i < log.size(); ++i)
				if (log[i].Op == NullOp::BeginPass || log[i].Op == NullOp::BlitColor) { end = i; break; }
			int indexed = 0, instanced = 0, fullscreen = 0;
			uint32_t copies = 0;
			std::vector<float> modelZ;   // the z of each uModel set in the scene pass, in order
			for (size_t i = begin; i < end; ++i)
			{
				const NullCommand& c = log[i];
				if (c.Op == NullOp::DrawIndexed) ++indexed;
				else if (c.Op == NullOp::DrawIndexedInstanced) { ++instanced; copies = c.C; }
				else if (c.Op == NullOp::Draw) ++fullscreen;
				else if (c.Op == NullOp::SetUniform && device.UniformName({ c.A }, c.Location) == "uModel" && c.Values.size() == 16)
					modelZ.push_back(c.Values[14]);
			}
			Check(fullscreen == 1, "scene pass: one full-screen draw (the sky)");
			Check(indexed == 3 && instanced == 1 && copies == 3, "scene pass: 3 single draws (1 opaque, 2 translucent) and 1 draw of 3 copies");
			Check(modelZ.size() >= 2 && modelZ[modelZ.size() - 2] == -15.0f && modelZ.back() == -5.0f, "translucent objects back to front");

			// The GPU timers: every mark of the frame is a timestamp in the stream, and the frame is flushed.
			int stamps = 0, flushes = 0;
			for (const NullCommand& c : log)
			{
				if (c.Op == NullOp::WriteTimestamp) ++stamps;
				if (c.Op == NullOp::Flush) ++flushes;
			}
			Check(stamps == 12 && flushes == 1, "12 GPU timestamps (TimerMark) and one flush per frame");

			// The stats come from the draws the frontend counted.
			Check(stats.DrawCalls > 0 && stats.SceneCalls == static_cast<uint32_t>(indexed + instanced), "draw-call stats count the scene's draws");

			// T70: the RHI counters of the frame. The null backend never filters: every uniform and state call reaches it.
			uint32_t uniforms = 0, pipelines = 0;
			for (const NullCommand& c : log)
			{
				if (c.Op == NullOp::SetUniform) ++uniforms;
				if (c.Op == NullOp::BindPipeline) ++pipelines;
			}
			const GB::RHI::FrameCounters& f = stats.Submit, &o = stats.ObjectSubmit;
			Check(f.UniformCalls == uniforms && f.PipelineBinds == pipelines, "the frame's counters match the commands recorded");
			Check(f.UniformsSent == f.UniformCalls && f.StateChanges == f.StateCalls, "the null backend sends every uniform and state call");
			Check(o.UniformCalls > 0 && o.UniformCalls < f.UniformCalls && o.StateCalls > 0 && o.UploadBytes == 3 * sizeof(glm::mat4),
				"the object loop's share: its uniforms and state, and one batch of 3 matrices uploaded");
			Check(stats.CpuObjectsMs >= 0.0f, "the object loop is timed");

			ShadowAndBatches(device, renderer, cube);
			SameAsBefore(device, renderer);
			result = failures;
		}
		GB::RHI::SetDevice(nullptr);
		return result;
	}

	// T70: the GL backend's uniform cache, on its own (it is plain CPU code).
	int RunUniformCache()
	{
		using Kind = GB::RHI::UniformCache::Kind;
		GB::RHI::UniformCache cache;
		const float a[3] = { 1.0f, 2.0f, 3.0f }, b[3] = { 1.0f, 2.0f, 4.0f };
		const int one = 1;
		const float negZero = -0.0f, zero = 0.0f;
		Check(cache.Update(5, 7, Kind::Float3, a, sizeof a), "cache: the first set is sent");
		Check(!cache.Update(5, 7, Kind::Float3, a, sizeof a), "cache: the same value again is not");
		Check(cache.Update(5, 7, Kind::Float3, b, sizeof b), "cache: another value is");
		Check(cache.Update(6, 7, Kind::Float3, b, sizeof b), "cache: the same location of another program is another uniform");
		Check(cache.Update(5, 7, Kind::Int, &one, sizeof one) && cache.Update(5, 7, Kind::Float1, &one, sizeof one),
			"cache: the same bits as another kind are another value");
		Check(cache.Update(5, 8, Kind::Float1, &zero, sizeof zero) && cache.Update(5, 8, Kind::Float1, &negZero, sizeof negZero),
			"cache: values compare bit for bit (0 and -0 differ)");
		cache.Forget(5, 8);
		Check(cache.Update(5, 8, Kind::Float1, &negZero, sizeof negZero), "cache: a forgotten location is sent again (an array set)");
		cache.ForgetProgram(5);
		Check(cache.Update(5, 8, Kind::Float1, &negZero, sizeof negZero), "cache: a destroyed program's values are forgotten");
		Check(!cache.Update(6, 7, Kind::Float3, b, sizeof b), "cache: ... and only that program's");
		Check(cache.Update(5, -1, Kind::Int, &one, sizeof one) && cache.Update(5, -1, Kind::Int, &one, sizeof one), "cache: no location is never cached");
		cache.Clear();
		Check(cache.Update(6, 7, Kind::Float3, b, sizeof b), "cache: cleared (turned on again), everything is sent");
		return failures;
	}
}

int main()
{
	Run();
	const int result = RunUniformCache();
	if (result) std::fprintf(stderr, "%d check(s) failed\n", result);
	return result ? 1 : 0;
}
