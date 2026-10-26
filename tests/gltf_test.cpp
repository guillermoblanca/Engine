// Loads tests/data/test_scene.glb (see tools/make_test_gltf.py) and checks the importer.
// SKIPs (77) without an OpenGL 4.5 context because meshes and textures live on the GPU.
#include "GB/Assets/Model.h"
#include "GB/Log.h"
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
	bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
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

	Check(GB::Model::Load(std::string(GB_TEST_DATA_DIR) + "/missing.glb") == nullptr, "missing file returns nullptr");

	auto model = GB::Model::Load(std::string(GB_TEST_DATA_DIR) + "/test_scene.glb");
	Check(model != nullptr, "test_scene.glb loads");
	if (!model)
		return 1;

	Check(model->GetMeshCount() == 2, "two glTF meshes");
	Check(model->GetInstanceCount() == 2, "two node instances (the parent node has no mesh)");
	Check(model->GetTriangleCount() == 13, "12 cube triangles + 1 triangle");
	Check(model->GetMaterialCount() == 1 && model->GetTextureCount() == 1, "one material, one embedded texture");

	// Cube world bounds: parent (1,2,3) * child (0,1,0) * scale 2 -> x [0,2], y [2,4], z [2,4]; triangle at x -5.
	const glm::vec3 lo = model->GetBoundsMin(), hi = model->GetBoundsMax();
	Check(Near(lo.x, -5) && Near(lo.y, 0) && Near(lo.z, 0), "bounds min includes the triangle");
	Check(Near(hi.x, 2) && Near(hi.y, 4) && Near(hi.z, 4), "bounds max follows the node hierarchy");

	const GB::Material& red = model->GetMaterials()[0];
	Check(Near(red.Albedo.g, 0.5f) && Near(red.Opacity, 0.8f), "base color factor");
	Check(Near(red.Metallic, 0.25f) && Near(red.Roughness, 0.75f), "metallic/roughness factors");
	Check(red.AlbedoMap != nullptr && red.AlbedoMap->IsValid(), "embedded PNG became a texture");
	Check(red.Alpha == GB::AlphaMode::Mask && Near(red.AlphaCutoff, 0.3f) && red.DoubleSided, "alpha mask + double sided");
	Check(Near(red.Emissive.b, 0.3f), "emissive factor");

	GB::Scene scene;
	model->AddToScene(scene, glm::translate(glm::mat4(1.0f), glm::vec3(10, 0, 0)));
	Check(scene.Objects.size() == 2, "AddToScene emits one object per primitive instance");
	if (scene.Objects.size() == 2)
	{
		const glm::vec3 cubePos(scene.Objects[0].Transform[3]);
		Check(Near(cubePos.x, 11) && Near(cubePos.y, 3) && Near(cubePos.z, 3), "cube node translation composed with the root");
		const GB::Material& def = scene.Objects[1].Mat;
		Check(Near(def.Metallic, 1.0f) && Near(def.Roughness, 1.0f) && def.AlbedoMap == nullptr, "glTF default material");
	}
	return failures;
}
