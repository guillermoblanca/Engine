// Animation tests on tests/data/test_anim.glb (see tools/make_test_anim_gltf.py): keyframe sampling
// (linear / step / cubic spline), looping, cross-fades, joint palettes and GPU skinning.
// SKIPs (77) without an OpenGL 4.5 context.
#include "GB/Assets/Model.h"
#include "GB/Assets/ModelInstance.h"
#include "GB/Log.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Window.h"
#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
	bool Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-3f)
	{
		return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
	}
	glm::vec3 Position(const GB::ModelInstance& inst, int node) { return glm::vec3(inst.GetNodeWorld(node)[3]); }
}

int main()
{
	GB::Log::Init();

	GB::WindowProps props;
	props.Visible = false;
	props.VSync = false;
	auto window = GB::Window::Create(props);
	if (!window)
	{
		std::fprintf(stderr, "SKIP: no OpenGL 4.5 context\n");
		return 77;
	}

	auto model = GB::Model::Load(std::string(GB_TEST_DATA_DIR) + "/test_anim.glb");
	Check(model != nullptr, "test_anim.glb loads");
	if (!model)
		return 1;

	Check(model->GetClips().size() == 6 && model->GetSkins().size() == 1, "6 clips, 1 skin");
	Check(model->FindClip("Cubic") == 3 && model->FindClip("nope") == -1, "clip lookup by name");
	Check(Near(model->GetClips()[0].Duration, 2.0f), "clip duration = last key time");

	GB::ModelInstance inst(*model);
	const int mover = 0;

	// --- linear translation, looping and clamping
	Check(inst.Play("Move", true, 0.0f), "play Move");
	inst.SetTime(0.5f);
	Check(Near(Position(inst, mover), { 1, 0, 0 }), "linear: halfway through the first segment");
	inst.SetTime(1.5f);
	Check(Near(Position(inst, mover), { 2, 2, 0 }), "linear: halfway through the second segment");
	inst.SetTime(5.0f);   // loops: 5 mod 2 = 1
	Check(Near(Position(inst, mover), { 2, 0, 0 }), "looping wraps the time");
	inst.Play("Move", false, 0.0f);
	inst.SetTime(5.0f);
	Check(Near(Position(inst, mover), { 2, 4, 0 }) && inst.IsFinished(), "non-looping clamps to the last key");

	// --- slerp rotation: 45 degrees about Y maps +X to (cos45, 0, -sin45)
	inst.Play("Spin", true, 0.0f);
	inst.SetTime(0.5f);
	const glm::vec3 rotated = glm::vec3(inst.GetNodeWorld(mover) * glm::vec4(1, 0, 0, 0));
	Check(Near(rotated, { std::sqrt(0.5f), 0, -std::sqrt(0.5f) }), "rotation is interpolated with slerp");

	// --- STEP and CUBICSPLINE
	inst.Play("Steps", false, 0.0f);   // not looping: t = duration must stay on the last key
	inst.SetTime(0.99f);
	Check(Near(Position(inst, mover).x, 0.0f), "step holds the previous value");
	inst.SetTime(1.0f);
	Check(Near(Position(inst, mover).x, 5.0f), "step jumps at the key");
	inst.Play("Cubic", false, 0.0f);
	inst.SetTime(0.25f);
	Check(Near(Position(inst, mover).x, 10.0f * (3 * 0.0625f - 2 * 0.015625f)), "cubic spline Hermite interpolation");

	// --- cross-fade: Move@1.0 (2,0,0) fading into Steps@0.0 over 1 s; after 0.5 s: previous=Move@1.5 (2,2,0), current=Steps@0.5 (0,0,0)
	inst.Play("Move", true, 0.0f);
	inst.SetTime(1.0f);
	inst.Play("Steps", true, 1.0f);
	Check(Near(Position(inst, mover), { 2, 0, 0 }), "fade starts from the previous pose");
	inst.Update(0.5f);
	Check(Near(Position(inst, mover), { 1, 1, 0 }), "fade blends previous and current halfway");
	inst.Update(0.6f);
	Check(Near(Position(inst, mover), { 5, 0, 0 }) || Near(Position(inst, mover), { 0, 0, 0 }), "fade ends on the new clip");

	inst.Stop();
	Check(Near(Position(inst, mover), { 0, 0, 0 }), "Stop returns to the rest pose");

	// --- skin palette. Bar = node 3 (translated by 100 on purpose: must be ignored).
	const int bar = 3;
	inst.Play("Bend", false, 0.0f);
	inst.SetTime(0.0f);
	const auto& rest = inst.GetJointMatrices(bar);
	Check(rest.size() == 2 && Near(glm::vec3(rest[1] * glm::vec4(0, 2, 0, 1)), { 0, 2, 0 }), "bind pose palette is identity");
	inst.SetTime(1.0f);
	const auto& bent = inst.GetJointMatrices(bar);
	Check(Near(glm::vec3(bent[1] * glm::vec4(0, 2, 0, 1)), { -1, 1, 0 }), "tip joint rotated 90 deg moves the top vertex to (-1,1,0)");
	Check(Near(glm::vec3(bent[0] * glm::vec4(0, 0, 0, 1)), { 0, 0, 0 }), "root joint keeps the bottom vertex");

	// --- glTF cameras: perspective + orthographic, animated with the scene
	const auto& cams = model->GetCameras();
	Check(cams.size() == 2 && cams[0].Name == "Main" && cams[1].Name == "Top", "two cameras imported");
	if (cams.size() == 2)
	{
		inst.Stop();
		GB::Camera main, top;
		Check(inst.GetCamera(0, main) && inst.GetCamera(1, top) && !inst.GetCamera(2, main), "GetCamera validates the index");
		Check(!main.Orthographic && Near(main.FovYDegrees, 0.5f * 180.0f / 3.14159265f, 1e-3f) && Near(main.NearPlane, 0.25f) && Near(main.FarPlane, 80.0f), "perspective camera lens");
		Check(Near(main.Position, { 0, 1, 5 }) && Near(main.GetForward(), { 0, 0, -1 }), "camera looks down the node's -Z");
		Check(top.Orthographic && Near(top.OrthoHalfHeight, 3.0f) && Near(top.NearPlane, 1.0f) && Near(top.FarPlane, 90.0f), "orthographic camera lens");
		inst.Play("Dolly", false, 0.0f);
		inst.SetTime(0.5f);
		inst.GetCamera(0, main, glm::translate(glm::mat4(1.0f), glm::vec3(10, 0, 0)));
		Check(Near(main.Position, { 10, 1, 4 }), "animated camera node + root transform");
	}

	// --- GPU skinning: render the bar straight (t=0) and bent (t=1) and compare covered pixels.
	inst.Play("Bend", false, 0.0f);
	constexpr uint32_t N = 128;
	GB::Renderer renderer(N, N, 1, 1024);
	Check(renderer.IsValid(), "renderer initializes");
	GB::Framebuffer target(N, N);
	GB::Camera camera;
	camera.Position = { 0, 1, 6 };
	camera.Target = { 0, 1, 0 };

	auto render = [&](float time) -> std::vector<uint8_t>
	{
		inst.SetTime(time);
		GB::Scene scene;
		scene.Sun.CastShadows = false;
		scene.Env.ShadowRadius = 4.0f;
		inst.AddToScene(scene);
		renderer.Render(scene, camera, &target);
		return target.ReadPixels();
	};
	auto isRed = [&](const std::vector<uint8_t>& px, float wx, float wy)
	{
		const float halfHeight = 6.0f * std::tan(glm::radians(22.5f));
		const int x = static_cast<int>(N / 2 + wx / halfHeight * (N / 2.0f));
		const int y = static_cast<int>(N / 2 - (wy - 1.0f) / halfHeight * (N / 2.0f));
		const uint8_t* p = &px[(static_cast<size_t>(y) * N + x) * 4];
		return p[0] > 100 && p[1] < p[0] / 2 && p[2] < p[0] / 2;
	};
	const auto straight = render(0.0f);
	Check(isRed(straight, 0.0f, 1.8f), "straight bar covers the top");
	Check(!isRed(straight, -0.8f, 1.0f), "straight bar does not reach the left");
	const auto bentPx = render(1.0f);
	Check(!isRed(bentPx, 0.0f, 1.8f), "bent bar no longer covers the top");
	Check(isRed(bentPx, -0.8f, 1.0f), "bent bar's top half now points left");
	Check(isRed(bentPx, 0.0f, 0.5f), "bent bar's lower half stays vertical");
	return failures;
}
