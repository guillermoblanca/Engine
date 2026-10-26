#pragma once
#include "GB/Renderer/Camera.h"
#include "GB/Renderer/Mesh.h"
#include "GB/Renderer/Renderer.h"
#include "GB/Renderer/Texture.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <vector>

namespace GB
{
	// One node of the glTF scene graph with its rest-pose transform.
	struct ModelNode
	{
		std::string Name;
		int Parent = -1;
		std::vector<int> Children;
		glm::vec3 Translation{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale{ 1.0f };
		int MeshIndex = -1;
		int SkinIndex = -1;
	};

	struct ModelSkin
	{
		std::vector<int> Joints;                 // node indices
		std::vector<glm::mat4> InverseBind;      // one per joint
	};

	// A camera defined in the glTF file (e.g. a Blender camera), attached to a node.
	struct ModelCamera
	{
		std::string Name;
		int Node = -1;
		bool Orthographic = false;
		float FovYDegrees = 45.0f;
		float OrthoHalfHeight = 5.0f;
		float NearPlane = 0.1f;
		float FarPlane = 1000.0f;
	};

	enum class AnimationTarget { Translation, Rotation, Scale };
	enum class AnimationInterpolation { Step, Linear, CubicSpline };

	struct AnimationChannel
	{
		int Node = -1;
		AnimationTarget Target = AnimationTarget::Translation;
		AnimationInterpolation Interpolation = AnimationInterpolation::Linear;
		std::vector<float> Times;
		// 3 floats per key (translation/scale) or 4 (rotation xyzw). CubicSpline stores
		// in-tangent, value, out-tangent per key.
		std::vector<float> Values;
	};

	struct AnimationClip
	{
		std::string Name;
		float Duration = 0.0f;
		std::vector<AnimationChannel> Channels;
	};

	// A glTF 2.0 asset (.gltf or .glb, e.g. exported from Blender) uploaded to the GPU.
	//
	// Supported: meshes (triangles; positions, normals, tangents, UV0, joints/weights), node hierarchy,
	// skins, node/skeletal animations (step, linear, cubic spline), metallic-roughness materials with
	// base color / metallic-roughness / normal / occlusion / emissive textures, alpha modes (opaque,
	// mask, blend), double-sided, KHR_materials_emissive_strength.
	// Not yet: morph targets, lights, UV sets > 0, KHR_texture_transform.
	//
	// The Model is immutable and shareable: play animations with a ModelInstance. It owns the meshes
	// and textures; keep it alive while Scenes built from it are rendered.
	class Model
	{
	public:
		// Returns nullptr (and logs why) if the file cannot be read or is not valid glTF.
		static std::unique_ptr<Model> Load(const std::string& path);

		// Appends one RenderObject per mesh primitive in the rest pose, each transformed by `root` * node
		// transform. Skinned meshes are drawn in their bind pose; use a ModelInstance to animate them.
		void AddToScene(Scene& scene, const glm::mat4& root = glm::mat4(1.0f)) const;

		// Axis-aligned bounds of the whole model in model space (rest pose, skins applied).
		glm::vec3 GetBoundsMin() const { return m_BoundsMin; }
		glm::vec3 GetBoundsMax() const { return m_BoundsMax; }
		glm::vec3 GetBoundsCenter() const { return (m_BoundsMin + m_BoundsMax) * 0.5f; }
		float GetBoundsRadius() const { return glm::length(m_BoundsMax - m_BoundsMin) * 0.5f; }

		size_t GetMeshCount() const { return m_Meshes.size(); }
		size_t GetInstanceCount() const { return m_MeshNodeCount; }   // nodes that reference a mesh
		size_t GetMaterialCount() const { return m_Materials.size(); }
		size_t GetTextureCount() const { return m_Textures.size(); }
		size_t GetTriangleCount() const { return m_TriangleCount; }
		const std::vector<Material>& GetMaterials() const { return m_Materials; }

		const std::vector<ModelNode>& GetNodes() const { return m_Nodes; }
		const std::vector<int>& GetRootNodes() const { return m_RootNodes; }
		// Parents before children, scene order: the order in which nodes are emitted to a Scene.
		const std::vector<int>& GetNodeOrder() const { return m_Order; }
		const std::vector<ModelSkin>& GetSkins() const { return m_Skins; }
		// One entry per node that carries a camera. Use ModelInstance::GetCamera for its current pose.
		const std::vector<ModelCamera>& GetCameras() const { return m_Cameras; }
		const std::vector<AnimationClip>& GetClips() const { return m_Clips; }
		// Index of the clip with this name, or -1.
		int FindClip(const std::string& name) const;
		// Index of the node with this exact name, or -1 (e.g. a foot bone).
		int FindNode(const std::string& name) const;

		struct Primitive
		{
			std::unique_ptr<Mesh> MeshData;
			int MaterialIndex = -1;   // -1 = default material
		};
		const std::vector<Primitive>& GetPrimitives(int meshIndex) const { return m_Meshes[meshIndex]; }
		// Material to use for a primitive (the glTF default when it has none).
		Material GetMaterial(int materialIndex) const;

	private:
		Model() = default;
		friend struct ModelLoader;

		std::vector<std::vector<Primitive>> m_Meshes;   // glTF meshes -> primitives
		std::vector<ModelNode> m_Nodes;
		std::vector<int> m_RootNodes;
		std::vector<int> m_Order;
		std::vector<ModelSkin> m_Skins;
		std::vector<ModelCamera> m_Cameras;
		std::vector<AnimationClip> m_Clips;
		std::vector<Material> m_Materials;
		std::vector<std::unique_ptr<Texture2D>> m_Textures;
		glm::vec3 m_BoundsMin{ 0.0f }, m_BoundsMax{ 0.0f };
		size_t m_TriangleCount = 0;
		size_t m_MeshNodeCount = 0;
	};
}
