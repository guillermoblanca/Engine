// FBX import (ufbx): characters with skins, rigid meshes, PBR-ish materials and animation clips, converted to the same
// Model data the glTF loader produces. Everything is converted to meters, Y up, right handed.
#include "Model.h"
#include "ModelUtil.h"
#include "GB/Log.h"
#include "GB/Renderer/Image.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <ufbx.h>

namespace GB
{
	namespace
	{
		// the take's name without the exporter's scope ("Armature|Walk_Loop" -> "Walk_Loop")
		std::string TakeName(const char* name)
		{
			const std::string n = name;
			const size_t bar = n.find_last_of('|');
			return bar != std::string::npos && bar + 1 < n.size() ? n.substr(bar + 1) : n;
		}

		glm::vec3 ToVec3(const ufbx_vec3& v) { return { static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z) }; }
		glm::quat ToQuat(const ufbx_quat& q) { return glm::quat(static_cast<float>(q.w), static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z)); }
		glm::mat4 ToMat4(const ufbx_matrix& m)
		{
			glm::mat4 r(1.0f);
			for (int c = 0; c < 4; ++c)
				for (int row = 0; row < 3; ++row)
					r[c][row] = static_cast<float>(m.cols[c].v[row]);
			return r;
		}

		std::string DirectoryOf(const std::string& path)
		{
			const size_t slash = path.find_last_of("/\\");
			return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
		}

		std::string StemOf(const std::string& path)
		{
			const size_t slash = path.find_last_of("/\\");
			std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
			const size_t dot = name.find_last_of('.');
			return dot == std::string::npos ? name : name.substr(0, dot);
		}

		ufbx_scene* LoadScene(const std::string& path, ufbx_error& error)
		{
			ufbx_load_opts opts = {};
			opts.target_axes = ufbx_axes_right_handed_y_up;
			opts.target_unit_meters = 1.0f;
			// meters and Y up in the data itself: node transforms keep unit scale, vertices and bind matrices are converted
			opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
			opts.generate_missing_normals = true;
			return ufbx_load_file(path.c_str(), &opts, &error);
		}

		// Clips shorter than this are single-frame poses (Mixamo files carry a T-pose "take"); they are not animations.
		constexpr double kMinClipSeconds = 0.05;

		struct BakedClips
		{
			std::vector<AnimationClip> Clips;
			size_t SkippedBones = 0;   // animated bones the target does not have
		};

		// Bakes every animation stack at 30 fps into per-node channels. `nodeMap` maps ufbx node typed ids to model nodes (-1: none).
		BakedClips BakeClips(const ufbx_scene* scene, const std::vector<int>& nodeMap, const std::vector<ModelNode>* restNodes, const std::string& fileStem)
		{
			BakedClips out;
			size_t usable = 0;
			for (size_t s = 0; s < scene->anim_stacks.count; ++s)
				if (scene->anim_stacks.data[s]->time_end - scene->anim_stacks.data[s]->time_begin >= kMinClipSeconds) ++usable;

			for (size_t s = 0; s < scene->anim_stacks.count; ++s)
			{
				const ufbx_anim_stack* stack = scene->anim_stacks.data[s];
				const double begin = stack->time_begin, duration = stack->time_end - stack->time_begin;
				if (duration < kMinClipSeconds) continue;

				ufbx_bake_opts bo = {};
				bo.resample_rate = 30.0;
				bo.key_reduction_enabled = false;
				ufbx_error err;
				ufbx_baked_anim* baked = ufbx_bake_anim(scene, stack->anim, &bo, &err);
				if (!baked)
				{
					GB_CORE_WARN("FBX: could not bake animation '{}': {}", stack->name.data, err.description.data);
					continue;
				}

				AnimationClip clip;
				// a file with a single take is named after the file ("walking.fbx" -> "walking"), whatever the exporter called the take
				clip.Name = usable == 1 ? fileStem : (stack->name.length ? TakeName(stack->name.data) : fileStem + " " + std::to_string(s));
				clip.Duration = static_cast<float>(duration);
				for (size_t b = 0; b < baked->nodes.count; ++b)
				{
					const ufbx_baked_node& bn = baked->nodes.data[b];
					const int target = bn.typed_id < nodeMap.size() ? nodeMap[bn.typed_id] : -1;
					if (target < 0)
					{
						if (bn.translation_keys.count || bn.rotation_keys.count) ++out.SkippedBones;
						continue;
					}
					auto times = [&](const auto& keys) { std::vector<float> t; for (const auto& k : keys) t.push_back(static_cast<float>(std::max(k.time - begin, 0.0))); return t; };
					if (bn.translation_keys.count)
					{
						AnimationChannel ch;
						ch.Node = target;
						ch.Target = AnimationTarget::Translation;
						ch.Times = times(bn.translation_keys);
						for (const ufbx_baked_vec3& k : bn.translation_keys) { ch.Values.push_back(static_cast<float>(k.value.x)); ch.Values.push_back(static_cast<float>(k.value.y)); ch.Values.push_back(static_cast<float>(k.value.z)); }
						clip.Channels.push_back(std::move(ch));
					}
					if (bn.rotation_keys.count)
					{
						AnimationChannel ch;
						ch.Node = target;
						ch.Target = AnimationTarget::Rotation;
						ch.Times = times(bn.rotation_keys);
						for (const ufbx_baked_quat& k : bn.rotation_keys) { ch.Values.push_back(static_cast<float>(k.value.x)); ch.Values.push_back(static_cast<float>(k.value.y)); ch.Values.push_back(static_cast<float>(k.value.z)); ch.Values.push_back(static_cast<float>(k.value.w)); }
						clip.Channels.push_back(std::move(ch));
					}
					if (bn.scale_keys.count)
					{
						// scale only when it is not the rest scale (most skeletons never animate it)
						bool animated = !bn.constant_scale;
						if (!animated && restNodes)
						{
							const glm::vec3 k0 = ToVec3(bn.scale_keys.data[0].value);
							animated = glm::length(k0 - (*restNodes)[static_cast<size_t>(target)].Scale) > 1e-4f;
						}
						if (animated)
						{
							AnimationChannel ch;
							ch.Node = target;
							ch.Target = AnimationTarget::Scale;
							ch.Times = times(bn.scale_keys);
							for (const ufbx_baked_vec3& k : bn.scale_keys) { ch.Values.push_back(static_cast<float>(k.value.x)); ch.Values.push_back(static_cast<float>(k.value.y)); ch.Values.push_back(static_cast<float>(k.value.z)); }
							clip.Channels.push_back(std::move(ch));
						}
					}
				}
				ufbx_free_baked_anim(baked);
				if (!clip.Channels.empty()) out.Clips.push_back(std::move(clip));
			}
			return out;
		}
	}

	struct FbxModelLoader
	{
		Model& Out;
		ufbx_scene* Scene;
		std::string BaseDir;
		std::map<const ufbx_texture*, Texture2D*> TextureCache;
		std::vector<int> NodeMap;   // ufbx node typed id -> model node

		Texture2D* GetTexture(const ufbx_texture* tex, TextureEncoding encoding)
		{
			if (!tex) return nullptr;
			auto it = TextureCache.find(tex);
			if (it != TextureCache.end()) return it->second;
			Image image;
			if (tex->content.size > 0)
				image = Image::LoadFromMemory(static_cast<const uint8_t*>(tex->content.data), tex->content.size, false);
			if (!image.IsValid())
			{
				// the path as written in the file, relative to the model, or just the file name next to the model
				for (const std::string& candidate : { BaseDir + std::string(tex->relative_filename.data), std::string(tex->absolute_filename.data),
					BaseDir + std::string(tex->filename.data).substr(std::string(tex->filename.data).find_last_of("/\\") + 1) })
				{
					image = Image::Load(candidate, false);
					if (image.IsValid()) break;
				}
			}
			Texture2D* result = nullptr;
			if (image.IsValid())
			{
				Out.m_Textures.push_back(std::make_unique<Texture2D>(image, encoding));
				result = Out.m_Textures.back().get();
			}
			else
				GB_CORE_WARN("FBX: could not load texture '{}'; the material renders without it", tex->filename.data);
			TextureCache[tex] = result;
			return result;
		}

		Material ConvertMaterial(const ufbx_material& src)
		{
			Material m;
			const ufbx_material_map& base = src.pbr.base_color;
			if (base.has_value) m.Albedo = { static_cast<float>(base.value_vec4.x), static_cast<float>(base.value_vec4.y), static_cast<float>(base.value_vec4.z) };
			if (src.pbr.base_factor.has_value) m.Albedo *= static_cast<float>(src.pbr.base_factor.value_real);
			m.Metallic = src.pbr.metalness.has_value ? static_cast<float>(src.pbr.metalness.value_real) : 0.0f;
			m.Roughness = src.pbr.roughness.has_value ? static_cast<float>(src.pbr.roughness.value_real) : 0.6f;
			if (src.pbr.opacity.has_value && src.pbr.opacity.value_real < 0.999f) { m.Opacity = static_cast<float>(src.pbr.opacity.value_real); m.Alpha = AlphaMode::Blend; }
			if (src.pbr.emission_color.has_value)
			{
				const float f = src.pbr.emission_factor.has_value ? static_cast<float>(src.pbr.emission_factor.value_real) : 1.0f;
				m.Emissive = f * glm::vec3(static_cast<float>(src.pbr.emission_color.value_vec3.x), static_cast<float>(src.pbr.emission_color.value_vec3.y), static_cast<float>(src.pbr.emission_color.value_vec3.z));
			}
			m.AlbedoMap = GetTexture(base.texture, TextureEncoding::SRGB);
			m.NormalMap = GetTexture(src.pbr.normal_map.texture, TextureEncoding::Linear);
			m.OcclusionMap = GetTexture(src.pbr.ambient_occlusion.texture, TextureEncoding::Linear);
			m.EmissiveMap = GetTexture(src.pbr.emission_color.texture, TextureEncoding::SRGB);
			return m;
		}

		void ConvertNodes()
		{
			NodeMap.assign(Scene->nodes.count, -1);
			for (size_t i = 0; i < Scene->nodes.count; ++i)
			{
				const ufbx_node* n = Scene->nodes.data[i];
				if (n->is_root) continue;
				NodeMap[n->typed_id] = static_cast<int>(Out.m_Nodes.size());
				Out.m_Nodes.emplace_back();
			}
			for (size_t i = 0; i < Scene->nodes.count; ++i)
			{
				const ufbx_node* n = Scene->nodes.data[i];
				if (n->is_root) continue;
				ModelNode& node = Out.m_Nodes[static_cast<size_t>(NodeMap[n->typed_id])];
				node.Name = n->name.data;
				node.Parent = n->parent && !n->parent->is_root ? NodeMap[n->parent->typed_id] : -1;
				node.Translation = ToVec3(n->local_transform.translation);
				node.Rotation = ToQuat(n->local_transform.rotation);
				node.Scale = ToVec3(n->local_transform.scale);
				if (node.Parent >= 0) Out.m_Nodes[static_cast<size_t>(node.Parent)].Children.push_back(NodeMap[n->typed_id]);
				else Out.m_RootNodes.push_back(NodeMap[n->typed_id]);
			}
			std::function<void(int)> visit = [&](int n)
			{
				Out.m_Order.push_back(n);
				for (int child : Out.m_Nodes[static_cast<size_t>(n)].Children) visit(child);
			};
			for (int root : Out.m_RootNodes) visit(root);
		}

		void ConvertSkins()
		{
			for (size_t i = 0; i < Scene->skin_deformers.count; ++i)
			{
				const ufbx_skin_deformer* src = Scene->skin_deformers.data[i];
				ModelSkin skin;
				for (size_t c = 0; c < src->clusters.count; ++c)
				{
					const ufbx_skin_cluster* cl = src->clusters.data[c];
					skin.Joints.push_back(cl->bone_node ? NodeMap[cl->bone_node->typed_id] : -1);
					skin.InverseBind.push_back(ToMat4(cl->geometry_to_bone));
				}
				Out.m_Skins.push_back(std::move(skin));
			}
		}

		struct Corner
		{
			float Pos[3], Normal[3], UV[2];
			uint32_t Joints[4];
			float Weights[4];
		};

		// One glTF-style mesh per FBX mesh, one primitive per material slot.
		void ConvertMesh(size_t meshIndex, const ufbx_mesh& mesh, std::vector<Model::PrimitiveCPU>& cpu)
		{
			const ufbx_skin_deformer* skin = mesh.skin_deformers.count ? mesh.skin_deformers.data[0] : nullptr;
			// rigid meshes carry their node's geometry offset in the vertices; skinned ones are placed by the joints
			glm::mat4 rigid(1.0f);
			if (!skin)
				for (size_t n = 0; n < mesh.instances.count; ++n) { rigid = ToMat4(mesh.instances.data[n]->geometry_to_node); break; }
			const glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(rigid)));

			const size_t slots = std::max<size_t>(mesh.materials.count, 1);
			std::vector<std::vector<Corner>> corners(slots);
			std::vector<uint32_t> tri(mesh.max_face_triangles * 3);
			for (size_t f = 0; f < mesh.faces.count; ++f)
			{
				const ufbx_face face = mesh.faces.data[f];
				if (face.num_indices < 3) continue;
				const size_t slot = mesh.face_material.count ? std::min<size_t>(mesh.face_material.data[f], slots - 1) : 0;
				const uint32_t triangles = ufbx_triangulate_face(tri.data(), tri.size(), &mesh, face);
				for (uint32_t t = 0; t < triangles * 3; ++t)
				{
					const uint32_t ix = tri[t];
					Corner c = {};
					const ufbx_vec3 p = mesh.vertex_position.values.data[mesh.vertex_position.indices.data[ix]];
					glm::vec3 pos = glm::vec3(rigid * glm::vec4(ToVec3(p), 1.0f));
					c.Pos[0] = pos.x; c.Pos[1] = pos.y; c.Pos[2] = pos.z;
					if (mesh.vertex_normal.exists)
					{
						const glm::vec3 n = glm::normalize(normalMat * ToVec3(mesh.vertex_normal.values.data[mesh.vertex_normal.indices.data[ix]]));
						c.Normal[0] = n.x; c.Normal[1] = n.y; c.Normal[2] = n.z;
					}
					if (mesh.vertex_uv.exists)
					{
						const ufbx_vec2 uv = mesh.vertex_uv.values.data[mesh.vertex_uv.indices.data[ix]];
						c.UV[0] = static_cast<float>(uv.x);
						c.UV[1] = 1.0f - static_cast<float>(uv.y);   // FBX UV origin is the bottom-left; the engine's is the top-left
					}
					c.Weights[0] = 1.0f;
					if (skin)
					{
						const ufbx_skin_vertex sv = skin->vertices.data[mesh.vertex_indices.data[ix]];
						float sum = 0.0f;
						const uint32_t used = std::min<uint32_t>(sv.num_weights, 4);   // weights come sorted, strongest first
						for (uint32_t k = 0; k < used; ++k)
						{
							const ufbx_skin_weight w = skin->weights.data[sv.weight_begin + k];
							c.Joints[k] = w.cluster_index;
							c.Weights[k] = static_cast<float>(w.weight);
							sum += c.Weights[k];
						}
						if (sum > 1e-6f) for (uint32_t k = 0; k < 4; ++k) c.Weights[k] /= sum;
						else { c.Weights[0] = 1.0f; for (uint32_t k = 1; k < 4; ++k) c.Weights[k] = 0.0f; }
					}
					corners[slot].push_back(c);
				}
			}

			for (size_t slot = 0; slot < slots; ++slot)
			{
				std::vector<Corner>& cs = corners[slot];
				if (cs.empty()) continue;
				std::vector<uint32_t> indices(cs.size());
				ufbx_vertex_stream stream = { cs.data(), cs.size(), sizeof(Corner) };
				ufbx_error err;
				const size_t unique = ufbx_generate_indices(&stream, 1, indices.data(), indices.size(), nullptr, &err);
				if (err.type != UFBX_ERROR_NONE || unique == 0) { GB_CORE_WARN("FBX: could not index mesh '{}'", mesh.name.data); continue; }

				std::vector<Vertex> vertices(unique);
				std::vector<SkinVertex> skinVerts;
				if (skin) skinVerts.resize(unique);
				Model::PrimitiveCPU prim;
				prim.Positions.resize(unique);
				for (size_t v = 0; v < unique; ++v)
				{
					vertices[v].Position = { cs[v].Pos[0], cs[v].Pos[1], cs[v].Pos[2] };
					vertices[v].Normal = { cs[v].Normal[0], cs[v].Normal[1], cs[v].Normal[2] };
					vertices[v].UV = { cs[v].UV[0], cs[v].UV[1] };
					prim.Positions[v] = vertices[v].Position;
					if (skin) for (int k = 0; k < 4; ++k) { skinVerts[v].Joints[k] = cs[v].Joints[k]; skinVerts[v].Weights[k] = cs[v].Weights[k]; }
				}
				if (!mesh.vertex_normal.exists) detail::GenerateNormals(vertices, indices);
				detail::GenerateTangents(vertices, indices);
				prim.Skin = skinVerts;
				Out.m_TriangleCount += indices.size() / 3;

				Model::Primitive primitive;
				primitive.MeshData = std::make_unique<Mesh>(vertices, indices, skin ? &skinVerts : nullptr);
				primitive.MaterialIndex = slot < mesh.materials.count ? static_cast<int>(mesh.materials.data[slot]->typed_id) : -1;
				Out.m_Meshes[meshIndex].push_back(std::move(primitive));
				cpu.push_back(std::move(prim));
			}
		}
	};

	std::unique_ptr<Model> Model::LoadFbx(const std::string& path)
	{
		ufbx_error err;
		ufbx_scene* scene = LoadScene(path, err);
		if (!scene)
		{
			GB_CORE_ERROR("FBX: cannot load '{}': {}", path, err.description.data);
			return nullptr;
		}

		std::unique_ptr<Model> model(new Model());
		FbxModelLoader loader{ *model, scene, DirectoryOf(path), {}, {} };
		for (size_t i = 0; i < scene->materials.count; ++i)
			model->m_Materials.push_back(loader.ConvertMaterial(*scene->materials.data[i]));
		loader.ConvertNodes();
		loader.ConvertSkins();

		model->m_Meshes.resize(scene->meshes.count);
		std::vector<std::vector<PrimitiveCPU>> cpu(scene->meshes.count);
		for (size_t m = 0; m < scene->meshes.count; ++m)
			loader.ConvertMesh(m, *scene->meshes.data[m], cpu[m]);
		for (size_t i = 0; i < scene->nodes.count; ++i)
		{
			const ufbx_node* n = scene->nodes.data[i];
			if (n->is_root) continue;
			ModelNode& node = model->m_Nodes[static_cast<size_t>(loader.NodeMap[n->typed_id])];
			if (n->mesh)
			{
				node.MeshIndex = static_cast<int>(n->mesh->typed_id);
				if (n->mesh->skin_deformers.count) node.SkinIndex = static_cast<int>(n->mesh->skin_deformers.data[0]->typed_id);
				++model->m_MeshNodeCount;
			}
		}

		BakedClips baked = BakeClips(scene, loader.NodeMap, &model->m_Nodes, StemOf(path));
		model->m_Clips = std::move(baked.Clips);
		model->ComputeBounds(cpu);

		ufbx_free_scene(scene);
		GB_CORE_INFO("Loaded FBX '{}': {} meshes, {} nodes, {} materials, {} textures, {} triangles, {} skins, {} animations",
			path, model->m_Meshes.size(), model->m_Nodes.size(), model->m_Materials.size(), model->m_Textures.size(), model->m_TriangleCount,
			model->m_Skins.size(), model->m_Clips.size());
		return model;
	}

	int Model::AppendAnimations(const std::string& path, std::string* report)
	{
		ufbx_error err;
		ufbx_scene* scene = LoadScene(path, err);
		if (!scene)
		{
			GB_CORE_ERROR("FBX: cannot load '{}': {}", path, err.description.data);
			if (report) *report = err.description.data;
			return 0;
		}
		std::vector<int> map(scene->nodes.count, -1);
		for (size_t i = 0; i < scene->nodes.count; ++i)
			if (!scene->nodes.data[i]->is_root) map[scene->nodes.data[i]->typed_id] = FindNode(scene->nodes.data[i]->name.data);

		BakedClips baked = BakeClips(scene, map, &m_Nodes, StemOf(path));
		const int added = static_cast<int>(baked.Clips.size());
		for (AnimationClip& clip : baked.Clips)
		{
			const int existing = FindClip(clip.Name);
			if (existing >= 0) m_Clips[static_cast<size_t>(existing)] = std::move(clip);   // reloading a file replaces its clip
			else m_Clips.push_back(std::move(clip));
		}
		ufbx_free_scene(scene);
		if (report) *report = std::to_string(added) + " clip(s), " + std::to_string(baked.SkippedBones) + " animated bone(s) not in this model";
		if (baked.SkippedBones > 0)
			GB_CORE_WARN("FBX '{}': {} animated bone(s) do not exist in the model and were skipped", path, baked.SkippedBones);
		return added;
	}
}
