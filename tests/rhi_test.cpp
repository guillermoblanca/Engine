// T25: the render frontend on the null RHI backend, without a window or a GL context. The Renderer records a frame into the null device's
// command list; the test reads it back: the pass order, the culling, the instanced draws and the translucent objects back to front. Any GL
// call would crash here (glad is never loaded), so passing also says the frontend only talks to the RHI.
#include "GB/Renderer/Mesh.h"
#include "GB/Renderer/RHI/NullDevice.h"
#include "GB/Renderer/RHI/UniformCache.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Renderer/Scene.h"
#include <cstdio>
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
