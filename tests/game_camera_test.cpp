// CPU-only tests for the game cameras (H19): follow, overview and observe, the blend between them and their data section.
// Everything is deterministic: the same inputs give the same poses, bit for bit.
#include "GB/Game/GameCamera.h"
#include "GB/Game/GameContent.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace GB;

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
	bool Same(const Camera& a, const Camera& b)
	{
		return std::memcmp(&a.Position, &b.Position, sizeof(glm::vec3)) == 0 && std::memcmp(&a.Target, &b.Target, sizeof(glm::vec3)) == 0
			&& a.FovYDegrees == b.FovYDegrees;
	}
	constexpr float kPi = 3.14159265358979f;

	// Rolling hills with a steep ridge across z = 20: a camera behind an actor walking along it has ground in the way.
	float Hills(float x, float z)
	{
		const float ridge = 9.0f * std::exp(-((z - 20.0f) * (z - 20.0f)) / 18.0f);
		return 2.0f * std::sin(x * 0.11f) * std::cos(z * 0.07f) + ridge;
	}

	std::string ReadFile(const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}

	// A scripted session of the rig: walk an actor over the hills, switch cameras, pan, zoom and turn. Returns every pose.
	std::vector<Camera> Session(float* lowestClearance = nullptr)
	{
		GameCameraRig rig;
		rig.Configure(GameCameraParams(), Hills);
		rig.SetBounds({ -100.0f, -100.0f }, { 100.0f, 100.0f });
		std::vector<Camera> poses;
		float lowest = 1e9f;
		glm::vec3 feet(0.0f, Hills(0.0f, 0.0f), 0.0f);
		float facing = 0.0f;   // walking toward +Z: over the ridge
		rig.SetFollowSubject(feet, facing);
		rig.SetMode(GameCameraMode::Follow, Camera(), false);
		const float dt = 1.0f / 30.0f;
		for (int frame = 0; frame < 900; ++frame)
		{
			const float t = static_cast<float>(frame) * dt;
			facing = 0.4f * std::sin(t * 0.5f);
			feet.x += std::sin(facing) * 2.0f * dt;
			feet.z += std::cos(facing) * 2.0f * dt;
			feet.y = Hills(feet.x, feet.z);
			rig.SetFollowSubject(feet, facing);
			rig.SetObserveGroup({ feet, feet + glm::vec3(3.0f, 0.0f, 1.0f), feet + glm::vec3(-2.0f, 0.0f, 4.0f) });
			if (frame == 300) rig.SetMode(GameCameraMode::Overview, rig.Output());
			if (frame == 600) rig.SetMode(GameCameraMode::Observe, rig.Output());
			GameCameraInput in;
			if (frame > 320 && frame < 500) { in.Pan = { 1.0f, 0.6f }; in.Turn = 0.3f; in.Zoom = frame < 400 ? 1.0f : -1.0f; }
			if (frame > 100 && frame < 130) in.Turn = 1.0f;   // the user turns the follow view; it swings back later
			rig.Update(dt, in);
			poses.push_back(rig.Output());
			const glm::vec3 p = rig.Output().Position;
			lowest = std::min(lowest, p.y - Hills(p.x, p.z));
		}
		if (lowestClearance) *lowestClearance = lowest;
		return poses;
	}
}

int main()
{
	// ---- Data: the shipped file holds exactly the defaults of the code, and the JSON writer round-trips.
	{
		const std::string text = ReadFile(std::string(GB_ASSETS_DIR) + "/data/cameras.json");
		Check(!text.empty(), "assets/data/cameras.json is there");
		GameCameraParams fromFile;
		fromFile.Follow.Distance = 99.0f;   // the file must set it back
		Check(LoadGameCameraParams(text, fromFile).empty(), "cameras.json loads");
		Check(fromFile == GameCameraParams(), "cameras.json says exactly what the code defaults say");

		GameCameraParams changed;
		changed.Follow.PitchDegrees = 23.5f;
		changed.Overview.PanSpeed = 1.75f;
		changed.Observe.OrbitDegreesPerSecond = -4.0f;
		changed.TransitionSeconds = 0.4f;
		GameCameraParams back;
		Check(LoadGameCameraParams(GameCameraParamsToJson(changed), back).empty() && back == changed, "params -> JSON -> params gives the same");
		GameCameraParams defaults;
		Check(LoadGameCameraParams(GameCameraParamsToJson(defaults), back).empty() && back == defaults, "the defaults round-trip too");

		// Only the keys present change.
		GameCameraParams partial;
		Check(LoadGameCameraParams(R"({ "cameras": { "overview": { "panSpeed": 2 } } })", partial).empty(), "a partial document loads");
		GameCameraParams expect;
		expect.Overview.PanSpeed = 2.0f;
		Check(partial == expect, "a partial document changes only its keys");
		Check(LoadGameCameraParams(R"({ "items": {} })", partial).empty() && partial == expect, "a document without cameras changes nothing");

		// Atomic: any mistake leaves everything as it was, even keys read before the mistake.
		const char* bad[] = {
			R"({ "cameras": { "follow": { "distance": 7, "distanse": 3 } } })",              // typo
			R"({ "cameras": { "follow": { "distance": 7 }, "overview": { "fov": "wide" } } })",
			R"({ "cameras": { "follow": { "minDistance": 9, "maxDistance": 3 } } })",
			R"({ "cameras": { "observe": { "pitch": 120 } } })",
			R"({ "cameras": { "zoom": {} } })",
			R"({ "cameras": [] })",
			"not json" };
		for (const char* doc : bad)
		{
			GameCameraParams p;
			const std::string error = LoadGameCameraParams(doc, p);
			Check(!error.empty() && p == GameCameraParams(), "a bad cameras document is refused and changes nothing");
		}

		// Through the content packs (H17): the "cameras" section reads era1's files, and a bad file in a load changes nothing at all.
		GameCameraParams live;
		live.Follow.Distance = 42.0f;
		ContentPacks packs;
		AddCameraContentSections(packs, live);
		ContentReport report;
		Check(packs.Load("era1", std::string(GB_ASSETS_DIR) + "/data", &report).empty(), "era1 loads with the cameras section");
		Check(live == GameCameraParams(), "era1 (cameras.json) brings the default cameras");
		live.Observe.Framing = 3.0f;
		Check(!packs.LoadText(R"({ "cameras": { "observe": { "framing": 2 }, "follow": { "fov": 500 } } })").empty() && live.Observe.Framing == 3.0f,
			"a refused document leaves the live cameras alone");
		Check(packs.LoadText(R"({ "cameras": { "observe": { "framing": 2 } } })").empty() && live.Observe.Framing == 2.0f, "a good one changes them");
	}

	// ---- Follow: behind the actor, back behind it after the user turned the view, never in the ground.
	{
		FollowCamera follow;
		follow.GroundHeight = [](float, float) { return 0.0f; };
		follow.SetSubject({ 0, 0, 0 }, 0.0f);   // facing +Z
		follow.Snap();
		Camera c;
		follow.Apply(c);
		Check(c.Position.z < -1.0f && Near(c.Position.x, 0.0f), "follow: the camera starts behind the actor (facing +Z, camera at -Z)");
		Check(Near(c.FovYDegrees, follow.Params.FovDegrees), "follow: the lens of the params");
		GameCameraInput turn;
		turn.TurnDelta = 1.2f;
		follow.Update(1.0f / 60.0f, turn);
		const float turned = follow.Yaw();
		Check(std::fabs(std::remainder(turned - kPi - 1.2f, 2 * kPi)) < 0.05f,
			"follow: the user turns the view");
		for (int i = 0; i < 60; ++i) follow.Update(1.0f / 60.0f, {});
		Check(std::fabs(std::remainder(follow.Yaw() - turned, 2 * kPi)) < 1e-4f, "follow: the view stays where the user left it for a while");
		for (int i = 0; i < 600; ++i) follow.Update(1.0f / 60.0f, {});
		Check(std::fabs(std::remainder(follow.Yaw() - kPi, 2 * kPi)) < 0.02f, "follow: then it swings back behind the actor");

		// Steered by the player's camera: takes its angles.
		follow.SetView(0.5f, 0.3f);
		follow.Update(1.0f / 60.0f, {});
		Check(Near(follow.Yaw(), 0.5f), "follow: SetView steers the yaw for the player");

		// A wall of ground right behind the actor: the camera comes in (and up) instead of going through it.
		FollowCamera wall;
		wall.GroundHeight = [](float, float z) { return z < -2.0f ? 6.0f : 0.0f; };
		wall.SetSubject({ 0, 0, 0 }, 0.0f);
		wall.Snap();
		for (int i = 0; i < 120; ++i) wall.Update(1.0f / 60.0f, {});
		const glm::vec3 p = wall.Position();
		Check(p.y >= wall.GroundHeight(p.x, p.z) + wall.Params.GroundMargin - 1e-3f, "follow: never below the terrain behind a wall");
	}

	// ---- Overview: pans in the view's directions, clamps to the map, zooms in log steps and tilts with the zoom.
	{
		OverviewCamera ov;
		ov.GroundHeight = Hills;
		ov.SetBounds({ -50.0f, -50.0f }, { 50.0f, 50.0f });
		ov.Reset({ 0.0f, 0.0f }, 0.0f, 60.0f);
		GameCameraInput fwd;
		fwd.Pan = { 0.0f, 1.0f };
		ov.Update(0.1f, fwd);
		Check(ov.WantedFocus().y < 0.0f && Near(ov.WantedFocus().x, 0.0f), "overview: forward at yaw 0 pans toward -Z (away from the camera)");
		GameCameraInput diag;
		diag.Pan = { 1.0f, 1.0f };
		diag.Turn = 0.7f;
		for (int i = 0; i < 2000; ++i)
		{
			ov.Update(1.0f / 30.0f, diag);
			const glm::vec3 f = ov.Focus();
			if (f.x < -42.0f - 1e-3f || f.x > 42.0f + 1e-3f || f.z < -42.0f - 1e-3f || f.z > 42.0f + 1e-3f) { Check(false, "overview: the focus stays inside the map minus the margin"); break; }
		}
		GameCameraInput east;
		east.Pan = { 1.0f, 0.0f };
		ov.Reset({ 0.0f, 0.0f }, 0.0f, 60.0f);
		for (int i = 0; i < 300; ++i) ov.Update(1.0f / 30.0f, east);
		Check(Near(ov.WantedFocus().x, 42.0f) && Near(ov.Focus().x, 42.0f, 0.01f), "overview: panning long enough stops at the edge minus the margin");
		const glm::vec3 pos = ov.Position();
		Check(pos.y >= Hills(pos.x, pos.z) + ov.Params.GroundMargin - 1e-3f, "overview: above the ground");

		GameCameraInput out;
		out.ZoomDelta = 50.0f;
		for (int i = 0; i < 200; ++i) ov.Update(1.0f / 30.0f, out);
		Check(Near(ov.Distance(), ov.Params.MaxDistance, 0.5f), "overview: zoom out stops at the max distance");
		Check(Near(ov.PitchRadians(), ov.Params.PitchFarDegrees * kPi / 180.0f, 0.01f), "overview: zoomed out it looks down at the far pitch");
		GameCameraInput in;
		in.ZoomDelta = -50.0f;
		for (int i = 0; i < 200; ++i) ov.Update(1.0f / 30.0f, in);
		Check(Near(ov.Distance(), ov.Params.MinDistance, 0.5f) && Near(ov.PitchRadians(), ov.Params.PitchNearDegrees * kPi / 180.0f, 0.01f),
			"overview: zoomed in it stops at the min distance and the near pitch");

		// A map smaller than twice the margin: the focus sits in its middle.
		OverviewCamera tiny;
		tiny.SetBounds({ 0.0f, 0.0f }, { 10.0f, 10.0f });
		tiny.Reset({ 100.0f, -100.0f }, 0.0f, 50.0f);
		Check(Near(tiny.Focus().x, 5.0f) && Near(tiny.Focus().z, 5.0f), "overview: a tiny map keeps the focus in its middle");
	}

	// ---- Observe: frames the group with its lens, circles slowly, stays above the ground.
	{
		ObserveCamera ob;
		ob.GroundHeight = [](float, float) { return 0.0f; };
		ob.SetGroup({ { -4, 0, 0 }, { 4, 0, 0 } });
		ob.Update(1.0f / 30.0f, {});
		const float expect = std::clamp(ob.FramingDistance(4.0f + ob.Params.Padding), ob.Params.MinDistance, ob.Params.MaxDistance);
		Check(Near(ob.Distance(), expect, 1e-3f), "observe: the distance frames the group's radius plus padding with the lens");
		Camera c;
		ob.Apply(c);
		const float half = 0.5f * c.FovYDegrees * kPi / 180.0f;
		const glm::vec3 toEdge = glm::vec3(4, 0, 0) - c.Position;
		const float angle = std::acos(glm::dot(glm::normalize(toEdge), c.GetForward()));
		Check(angle < half, "observe: both ends of the group are in the frame");
		const glm::vec3 before = ob.Position();
		for (int i = 0; i < 30; ++i) ob.Update(1.0f / 30.0f, {});
		Check(glm::length(ob.Position() - before) > 0.1f && Near(glm::length(ob.Position() - ob.LookPoint()), ob.Distance(), 0.05f), "observe: it circles at the framing distance");
		ob.SetGroup({ { 30, 0, 0 } });
		for (int i = 0; i < 300; ++i) ob.Update(1.0f / 30.0f, {});
		Check(Near(ob.LookPoint().x, 30.0f, 0.05f), "observe: it glides to a new subject");
		ob.SetGroup({});
		ob.Update(1.0f / 30.0f, {});
		Check(Near(ob.LookPoint().x, 30.0f, 0.05f), "observe: an empty group keeps the last framing");
	}

	// ---- Rig: deterministic, blends between modes, never under the ground.
	{
		float lowest = 0.0f;
		const std::vector<Camera> a = Session(&lowest), b = Session();
		bool same = a.size() == b.size();
		for (size_t i = 0; same && i < a.size(); ++i) same = Same(a[i], b[i]);
		Check(same, "rig: the same session gives the same poses, bit for bit");
		Check(lowest >= 0.3f - 1e-3f, "rig: the camera never goes below the terrain (follow over the ridge, overview, observe, the blends)");
		char buf[96];
		std::snprintf(buf, sizeof buf, "rig: lowest clearance %.2f m", static_cast<double>(lowest));
		std::printf("%s\n", buf);

		// A blend starts where the old view was and arrives at the new one.
		GameCameraRig rig;
		rig.Configure(GameCameraParams(), nullptr);
		rig.SetFollowSubject({ 0, 0, 0 }, 0.0f);
		rig.SetMode(GameCameraMode::Follow, Camera(), false);
		rig.Update(1.0f / 60.0f, {});
		const Camera followView = rig.Output();
		rig.SetMode(GameCameraMode::Overview, followView);
		Check(rig.Transitioning(), "rig: switching starts a blend");
		rig.Update(1e-4f, {});
		Check(glm::length(rig.Output().Position - followView.Position) < 0.05f, "rig: the blend starts at the old view");
		for (int i = 0; i < 120 && rig.Transitioning(); ++i) rig.Update(1.0f / 60.0f, {});
		Check(!rig.Transitioning(), "rig: the blend ends within TransitionSeconds");
		Camera overview;
		rig.Overview().Apply(overview);
		Check(glm::length(rig.Output().Position - overview.Position) < 1e-3f, "rig: after the blend it is the overview");
		Check(glm::length(rig.Focus() - glm::vec3(followView.Target.x, 0.0f, followView.Target.z)) < 0.5f, "rig: the overview starts over what the old view looked at");
		Check(std::string(GameCameraModeName(rig.Mode())) == "overview", "rig: mode names");
	}

	if (failures == 0) std::printf("game camera tests passed\n");
	return failures == 0 ? 0 : 1;
}
