#pragma once
#include "GB/Scene/AlphaMode.h"
#include "GB/Scene/MeshData.h"
#include "GB/Scene/SkinVertex.h"
#include <cstdint>
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

	// A texture of a model as the file has it: where its encoded pixels are, not decoded. The render decodes and uploads it once
	// (GB/Renderer/ModelGpu.h), so a model loaded only for its skeleton or its clips never pays for its images. The loader checks the header:
	// an image that cannot be read is not here (its material slot is -1).
	struct ModelImage
	{
		std::string File;                 // an image file next to the model (read when it is uploaded) ...
		std::vector<uint8_t> Encoded;     // ... or the encoded bytes embedded in the model (a .glb buffer, a data: URI, an FBX blob)
		bool SRGB = true;                 // colour data (base colour, emissive) or linear data (normal, metallic-roughness, occlusion)
		// Not empty for files of a glTF: the canonical path and the encoding. Every model that uses the same file shares one GPU texture (the
		// pieces of a modular kit share a dozen textures between them).
		std::string SharedKey;
	};

	// A metallic-roughness material as the file describes it, on the CPU: the factors, and the textures as indices into Model::GetImages()
	// (-1: none). The render turns it into a GB::Material with its textures (GB/Renderer/ModelGpu.h). The defaults are GB::Material's.
	struct ModelMaterial
	{
		glm::vec3 Albedo{ 1.0f };
		float Opacity = 1.0f;
		float Metallic = 0.0f;
		float Roughness = 0.5f;
		glm::vec3 Emissive{ 0.0f };
		float NormalStrength = 1.0f;
		float OcclusionStrength = 1.0f;
		float HeightScale = 0.1f;        // (glTF has no displacement: its loader sets 0)
		float CavityStrength = 0.6f;
		AlphaMode Alpha = AlphaMode::Opaque;
		float AlphaCutoff = 0.5f;
		bool DoubleSided = false;

		int AlbedoMap = -1;
		int MetallicRoughnessMap = -1;
		int NormalMap = -1;
		int OcclusionMap = -1;
		int EmissiveMap = -1;
	};

	// Whatever the render keeps for a model (GB/Renderer/ModelGpu.h: its meshes, textures and materials on the GPU). Opaque here: the model
	// does not know the render (ARQUITECTURA_REVISION.md, H5); it only keeps it alive as long as itself, so the GPU copy is made once for every
	// instance and every actor that shares the model, and dies with it, as the meshes and textures the model owned before did.
	struct ModelRenderData
	{
		virtual ~ModelRenderData() = default;
	};

	// A glTF 2.0 asset (.gltf or .glb, e.g. exported from Blender) or an FBX, on the CPU.
	//
	// Supported: meshes (triangles; positions, normals, tangents, UV0, joints/weights), node hierarchy,
	// skins, node/skeletal animations (step, linear, cubic spline), metallic-roughness materials with
	// base color / metallic-roughness / normal / occlusion / emissive textures, alpha modes (opaque,
	// mask, blend), double-sided, KHR_materials_emissive_strength.
	// Not yet: morph targets, lights, UV sets > 0, KHR_texture_transform.
	//
	// Pure CPU data: loading needs no OpenGL context. The model is immutable and shareable: play animations with a ModelInstance; draw it with
	// GB/Renderer/ModelGpu.h, which uploads it on the first draw and leaves the GPU copy here (GetRenderData). Keep the model alive while
	// Scenes built from it are rendered.
	class Model
	{
	public:
		// Returns nullptr (and logs why) if the file cannot be read or is not valid glTF.
		static std::unique_ptr<Model> Load(const std::string& path);

		// Axis-aligned bounds of the whole model in model space (rest pose, skins applied).
		glm::vec3 GetBoundsMin() const { return m_BoundsMin; }
		glm::vec3 GetBoundsMax() const { return m_BoundsMax; }
		glm::vec3 GetBoundsCenter() const { return (m_BoundsMin + m_BoundsMax) * 0.5f; }
		float GetBoundsRadius() const { return glm::length(m_BoundsMax - m_BoundsMin) * 0.5f; }

		size_t GetMeshCount() const { return m_Meshes.size(); }
		size_t GetInstanceCount() const { return m_MeshNodeCount; }   // nodes that reference a mesh
		size_t GetMaterialCount() const { return m_Materials.size(); }
		size_t GetTextureCount() const { return m_Images.size(); }
		size_t GetTriangleCount() const { return m_TriangleCount; }
		const std::vector<ModelMaterial>& GetMaterials() const { return m_Materials; }
		const std::vector<ModelImage>& GetImages() const { return m_Images; }

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

		// Adds the animation clips of another file (typically an animation-only FBX such as a Mixamo clip) to this
		// model, matching bones by node name. Returns how many clips were added. Bones the file animates that this
		// model does not have are skipped; `report` (optional) says how many.
		int AppendAnimations(const std::string& path, std::string* report = nullptr);
		// Adds (or, with the same name, replaces) a clip made in code, such as a retargeted one. Its channels refer to this model's nodes.
		int AddClip(AnimationClip clip);

		// One primitive of a mesh: its triangles, its skin weights and its material.
		struct Primitive
		{
			MeshData Geometry;               // vertices (normals and tangents filled in) and triangle indices
			std::vector<SkinVertex> Skin;    // one per vertex, or empty if not skinned
			int MaterialIndex = -1;          // -1 = default material
			bool HasSkin() const { return !Skin.empty() && Skin.size() == Geometry.Vertices.size(); }
		};
		const std::vector<Primitive>& GetPrimitives(int meshIndex) const { return m_Meshes[meshIndex]; }
		// Material to use for a primitive (the glTF default when it has none).
		ModelMaterial GetMaterial(int materialIndex) const;

		// The render's copy of this model (null until something draws it). Set by GB/Renderer/ModelGpu.h, on the thread that draws.
		ModelRenderData* GetRenderData() const { return m_RenderData.get(); }
		void SetRenderData(std::unique_ptr<ModelRenderData> data) const { m_RenderData = std::move(data); }

	private:
		Model() = default;
		friend struct ModelLoader;
		friend struct FbxModelLoader;

		// Bounds in the rest pose: rigid vertices by their node's world, skinned ones through the rest palette.
		void ComputeBounds();
		static std::unique_ptr<Model> LoadFbx(const std::string& path);

		std::vector<std::vector<Primitive>> m_Meshes;   // glTF meshes -> primitives
		std::vector<ModelNode> m_Nodes;
		std::vector<int> m_RootNodes;
		std::vector<int> m_Order;
		std::vector<ModelSkin> m_Skins;
		std::vector<ModelCamera> m_Cameras;
		std::vector<AnimationClip> m_Clips;
		std::vector<ModelMaterial> m_Materials;
		std::vector<ModelImage> m_Images;
		glm::vec3 m_BoundsMin{ 0.0f }, m_BoundsMax{ 0.0f };
		size_t m_TriangleCount = 0;
		size_t m_MeshNodeCount = 0;
		mutable std::unique_ptr<ModelRenderData> m_RenderData;   // (declared last: destroyed first, before the data it was made from)
	};
}
