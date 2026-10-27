#include "Model.h"
#include "ModelInstance.h"
#include "ModelUtil.h"
#include "GB/Log.h"
#include "GB/Renderer/Image.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <glm/gtc/matrix_transform.hpp>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

namespace GB
{
	namespace
	{
		std::string DirectoryOf(const std::string& path)
		{
			const size_t slash = path.find_last_of("/\\");
			return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
		}

		bool DecodeBase64(const char* text, size_t length, std::vector<uint8_t>& out)
		{
			auto value = [](char c) -> int
			{
				if (c >= 'A' && c <= 'Z') return c - 'A';
				if (c >= 'a' && c <= 'z') return c - 'a' + 26;
				if (c >= '0' && c <= '9') return c - '0' + 52;
				if (c == '+') return 62;
				if (c == '/') return 63;
				return -1;
			};
			uint32_t accumulator = 0;
			int bits = 0;
			for (size_t i = 0; i < length; ++i)
			{
				if (text[i] == '=')
					break;
				const int v = value(text[i]);
				if (v < 0)
					return false;
				accumulator = (accumulator << 6) | static_cast<uint32_t>(v);
				bits += 6;
				if (bits >= 8)
				{
					bits -= 8;
					out.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xFF));
				}
			}
			return true;
		}

		glm::mat4 ToMat4(const float* m)
		{
			glm::mat4 r;
			std::memcpy(&r[0][0], m, sizeof(float) * 16);   // both column-major
			return r;
		}

	}

	namespace detail
	{
		// Per-vertex normals from face normals (area weighted) for meshes without NORMAL.
		void GenerateNormals(std::vector<Vertex>& v, const std::vector<uint32_t>& idx)
		{
			for (Vertex& vert : v)
				vert.Normal = glm::vec3(0.0f);
			for (size_t i = 0; i + 2 < idx.size(); i += 3)
			{
				const glm::vec3 n = glm::cross(v[idx[i + 1]].Position - v[idx[i]].Position,
					v[idx[i + 2]].Position - v[idx[i]].Position);
				for (int k = 0; k < 3; ++k)
					v[idx[i + k]].Normal += n;
			}
			for (Vertex& vert : v)
				vert.Normal = glm::dot(vert.Normal, vert.Normal) > 0.0f ? glm::normalize(vert.Normal) : glm::vec3(0, 1, 0);
		}

		// Tangents from UV derivatives, following the glTF convention: the normal map's +Y (green)
		// points toward decreasing V, so the bitangent target is -dP/dV.
		void GenerateTangents(std::vector<Vertex>& v, const std::vector<uint32_t>& idx)
		{
			std::vector<glm::vec3> tan(v.size(), glm::vec3(0.0f)), bitan(v.size(), glm::vec3(0.0f));
			for (size_t i = 0; i + 2 < idx.size(); i += 3)
			{
				const Vertex& a = v[idx[i]];
				const Vertex& b = v[idx[i + 1]];
				const Vertex& c = v[idx[i + 2]];
				const glm::vec3 e1 = b.Position - a.Position, e2 = c.Position - a.Position;
				const glm::vec2 d1 = b.UV - a.UV, d2 = c.UV - a.UV;
				const float det = d1.x * d2.y - d2.x * d1.y;
				if (std::abs(det) < 1e-12f)
					continue;
				const float r = 1.0f / det;
				const glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;      // dP/dU
				const glm::vec3 bt = (e2 * d1.x - e1 * d2.x) * r;     // dP/dV
				for (int k = 0; k < 3; ++k)
				{
					tan[idx[i + k]] += t;
					bitan[idx[i + k]] += -bt;                          // toward decreasing V
				}
			}
			for (size_t i = 0; i < v.size(); ++i)
			{
				const glm::vec3 n = v[i].Normal;
				glm::vec3 t = tan[i] - n * glm::dot(n, tan[i]);
				if (glm::dot(t, t) < 1e-20f)
				{
					// Degenerate / missing UVs: any vector perpendicular to the normal works.
					t = std::abs(n.y) < 0.99f ? glm::cross(n, glm::vec3(0, 1, 0)) : glm::vec3(1, 0, 0);
				}
				t = glm::normalize(t);
				const float w = glm::dot(glm::cross(n, t), bitan[i]) < 0.0f ? -1.0f : 1.0f;
				v[i].Tangent = glm::vec4(t, w);
			}
		}
	}

	using detail::GenerateNormals;
	using detail::GenerateTangents;

	// Defined here (and befriended) so the cgltf details stay out of the header.
	struct ModelLoader
	{
		Model& Out;
		cgltf_data* Data;
		std::string BaseDir;
		std::map<std::pair<const cgltf_image*, int>, Texture2D*> TextureCache;

		const Texture2D* GetTexture(const cgltf_texture_view& view, TextureEncoding encoding)
		{
			const cgltf_texture* texture = view.texture;
			if (!texture || !texture->image)
				return nullptr;
			if (view.texcoord != 0)
				GB_CORE_WARN("glTF: texture uses UV set {}; only UV0 is supported", view.texcoord);

			const auto key = std::make_pair(texture->image, static_cast<int>(encoding));
			auto it = TextureCache.find(key);
			if (it != TextureCache.end())
				return it->second;

			Image image;
			const cgltf_image* src = texture->image;
			if (src->buffer_view)
			{
				const uint8_t* bytes = static_cast<const uint8_t*>(src->buffer_view->buffer->data) + src->buffer_view->offset;
				image = Image::LoadFromMemory(bytes, src->buffer_view->size, false);
			}
			else if (src->uri && std::strncmp(src->uri, "data:", 5) == 0)
			{
				const char* comma = std::strchr(src->uri, ',');
				std::vector<uint8_t> bytes;
				if (comma && DecodeBase64(comma + 1, std::strlen(comma + 1), bytes))
					image = Image::LoadFromMemory(bytes.data(), bytes.size(), false);
			}
			else if (src->uri)
			{
				std::string decoded = src->uri;
				cgltf_decode_uri(decoded.data());
				decoded.resize(std::strlen(decoded.c_str()));
				image = Image::Load(BaseDir + decoded, false);
			}

			// glTF UV (0,0) is the top-left of the image, which is row 0 of the uploaded data (no flip).
			Texture2D* result = nullptr;
			if (image.IsValid())
			{
				Out.m_Textures.push_back(std::make_unique<Texture2D>(image, encoding));
				result = Out.m_Textures.back().get();
			}
			else
				GB_CORE_WARN("glTF: could not load an image; the material will render without it");
			TextureCache[key] = result;
			return result;
		}

		Material ConvertMaterial(const cgltf_material& src)
		{
			Material m;
			if (src.has_pbr_metallic_roughness)
			{
				const cgltf_pbr_metallic_roughness& pbr = src.pbr_metallic_roughness;
				m.Albedo = { pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2] };
				m.Opacity = pbr.base_color_factor[3];
				m.Metallic = pbr.metallic_factor;
				m.Roughness = pbr.roughness_factor;
				m.AlbedoMap = GetTexture(pbr.base_color_texture, TextureEncoding::SRGB);
				m.MetallicRoughnessMap = GetTexture(pbr.metallic_roughness_texture, TextureEncoding::Linear);
			}
			else
			{
				// glTF default: white, fully metallic and rough (per spec when the block is absent)
				m.Metallic = 1.0f;
				m.Roughness = 1.0f;
			}
			m.NormalMap = GetTexture(src.normal_texture, TextureEncoding::Linear);
			m.NormalStrength = src.normal_texture.texture ? src.normal_texture.scale : 1.0f;
			m.OcclusionMap = GetTexture(src.occlusion_texture, TextureEncoding::Linear);
			m.OcclusionStrength = src.occlusion_texture.texture ? src.occlusion_texture.scale : 1.0f;
			m.EmissiveMap = GetTexture(src.emissive_texture, TextureEncoding::SRGB);
			m.Emissive = { src.emissive_factor[0], src.emissive_factor[1], src.emissive_factor[2] };
			if (src.has_emissive_strength)
				m.Emissive *= src.emissive_strength.emissive_strength;
			m.DoubleSided = src.double_sided != 0;
			m.HeightScale = 0.0f;   // glTF has no displacement
			m.CavityStrength = 0.0f;
			switch (src.alpha_mode)
			{
			case cgltf_alpha_mode_mask: m.Alpha = AlphaMode::Mask; m.AlphaCutoff = src.alpha_cutoff; break;
			case cgltf_alpha_mode_blend: m.Alpha = AlphaMode::Blend; break;
			default: m.Alpha = AlphaMode::Opaque; break;
			}
			return m;
		}

		// CPU copies kept until the bounds are computed.

		std::unique_ptr<Mesh> ConvertPrimitive(const cgltf_primitive& prim, Model::PrimitiveCPU& cpu)
		{
			if (prim.type != cgltf_primitive_type_triangles)
			{
				GB_CORE_WARN("glTF: skipping a non-triangle primitive");
				return nullptr;
			}
			const cgltf_accessor *position = nullptr, *normal = nullptr, *tangent = nullptr, *uv = nullptr;
			const cgltf_accessor *joints = nullptr, *weights = nullptr;
			for (cgltf_size i = 0; i < prim.attributes_count; ++i)
			{
				const cgltf_attribute& a = prim.attributes[i];
				if (a.type == cgltf_attribute_type_position) position = a.data;
				else if (a.type == cgltf_attribute_type_normal) normal = a.data;
				else if (a.type == cgltf_attribute_type_tangent) tangent = a.data;
				else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) uv = a.data;
				else if (a.type == cgltf_attribute_type_joints && a.index == 0) joints = a.data;
				else if (a.type == cgltf_attribute_type_weights && a.index == 0) weights = a.data;
			}
			if (!position)
			{
				GB_CORE_WARN("glTF: skipping a primitive without POSITION");
				return nullptr;
			}

			const size_t count = position->count;
			std::vector<Vertex> vertices(count);
			std::vector<float> tmp;
			auto read = [&](const cgltf_accessor* acc, size_t components, auto&& store)
			{
				tmp.assign(acc->count * components, 0.0f);
				cgltf_accessor_unpack_floats(acc, tmp.data(), tmp.size());
				for (size_t i = 0; i < std::min(count, static_cast<size_t>(acc->count)); ++i)
					store(vertices[i], &tmp[i * components]);
			};
			read(position, 3, [](Vertex& v, const float* f) { v.Position = { f[0], f[1], f[2] }; });
			if (normal) read(normal, 3, [](Vertex& v, const float* f) { v.Normal = { f[0], f[1], f[2] }; });
			if (uv) read(uv, 2, [](Vertex& v, const float* f) { v.UV = { f[0], f[1] }; });
			if (tangent) read(tangent, 4, [](Vertex& v, const float* f) { v.Tangent = { f[0], f[1], f[2], f[3] }; });

			std::vector<uint32_t> indices;
			if (prim.indices)
			{
				indices.resize(prim.indices->count);
				for (cgltf_size i = 0; i < prim.indices->count; ++i)
					indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(prim.indices, i));
			}
			else
			{
				indices.resize(count);
				for (size_t i = 0; i < count; ++i)
					indices[i] = static_cast<uint32_t>(i);
			}
			indices.resize(indices.size() / 3 * 3);
			for (uint32_t index : indices)
			{
				if (index >= count)
				{
					GB_CORE_WARN("glTF: primitive has out-of-range indices; skipped");
					return nullptr;
				}
			}

			if (!normal) GenerateNormals(vertices, indices);
			if (!tangent) GenerateTangents(vertices, indices);

			std::vector<SkinVertex> skin;
			if (joints && weights && joints->count >= count && weights->count >= count)
			{
				skin.resize(count);
				std::vector<float> w(weights->count * 4);
				cgltf_accessor_unpack_floats(weights, w.data(), w.size());
				for (size_t i = 0; i < count; ++i)
				{
					cgltf_uint j[4] = { 0, 0, 0, 0 };
					cgltf_accessor_read_uint(joints, i, j, 4);
					float sum = w[i * 4] + w[i * 4 + 1] + w[i * 4 + 2] + w[i * 4 + 3];
					for (int k = 0; k < 4; ++k)
					{
						skin[i].Joints[k] = j[k];
						skin[i].Weights[k] = sum > 1e-6f ? w[i * 4 + k] / sum : (k == 0 ? 1.0f : 0.0f);
					}
				}
			}

			cpu.Positions.resize(count);
			for (size_t i = 0; i < count; ++i)
				cpu.Positions[i] = vertices[i].Position;
			cpu.Skin = skin;

			Out.m_TriangleCount += indices.size() / 3;
			return std::make_unique<Mesh>(vertices, indices, skin.empty() ? nullptr : &skin);
		}

		static void DecomposeMatrix(const float* m, ModelNode& node)
		{
			const glm::mat4 mat = ToMat4(m);
			node.Translation = glm::vec3(mat[3]);
			glm::vec3 c0(mat[0]), c1(mat[1]), c2(mat[2]);
			glm::vec3 scale(glm::length(c0), glm::length(c1), glm::length(c2));
			if (glm::dot(glm::cross(c0, c1), c2) < 0.0f)
				scale.x = -scale.x;
			node.Scale = scale;
			if (scale.x != 0.0f && scale.y != 0.0f && scale.z != 0.0f)
				node.Rotation = glm::quat_cast(glm::mat3(c0 / scale.x, c1 / scale.y, c2 / scale.z));
		}

		void ConvertNodes()
		{
			Out.m_Nodes.resize(Data->nodes_count);
			for (cgltf_size i = 0; i < Data->nodes_count; ++i)
			{
				const cgltf_node& src = Data->nodes[i];
				ModelNode& node = Out.m_Nodes[i];
				node.Name = src.name ? src.name : "";
				node.Parent = src.parent ? static_cast<int>(cgltf_node_index(Data, src.parent)) : -1;
				for (cgltf_size c = 0; c < src.children_count; ++c)
					node.Children.push_back(static_cast<int>(cgltf_node_index(Data, src.children[c])));
				if (src.has_matrix)
					DecomposeMatrix(src.matrix, node);
				else
				{
					if (src.has_translation) node.Translation = { src.translation[0], src.translation[1], src.translation[2] };
					if (src.has_rotation) node.Rotation = glm::quat(src.rotation[3], src.rotation[0], src.rotation[1], src.rotation[2]);
					if (src.has_scale) node.Scale = { src.scale[0], src.scale[1], src.scale[2] };
				}
				node.MeshIndex = src.mesh ? static_cast<int>(cgltf_mesh_index(Data, src.mesh)) : -1;
				node.SkinIndex = src.skin ? static_cast<int>(cgltf_skin_index(Data, src.skin)) : -1;
				if (node.MeshIndex >= 0)
					++Out.m_MeshNodeCount;
			}

			if (Data->scene)
				for (cgltf_size i = 0; i < Data->scene->nodes_count; ++i)
					Out.m_RootNodes.push_back(static_cast<int>(cgltf_node_index(Data, Data->scene->nodes[i])));
			else if (Data->scenes_count > 0)
				for (cgltf_size i = 0; i < Data->scenes[0].nodes_count; ++i)
					Out.m_RootNodes.push_back(static_cast<int>(cgltf_node_index(Data, Data->scenes[0].nodes[i])));
			else
				for (size_t i = 0; i < Out.m_Nodes.size(); ++i)
					if (Out.m_Nodes[i].Parent < 0)
						Out.m_RootNodes.push_back(static_cast<int>(i));

			std::function<void(int)> visit = [&](int n)
			{
				Out.m_Order.push_back(n);
				for (int child : Out.m_Nodes[n].Children)
					visit(child);
			};
			for (int root : Out.m_RootNodes)
				visit(root);
		}

		void ConvertCameras()
		{
			for (cgltf_size i = 0; i < Data->nodes_count; ++i)
			{
				const cgltf_camera* src = Data->nodes[i].camera;
				if (!src)
					continue;
				ModelCamera cam;
				cam.Name = src->name ? src->name : (Data->nodes[i].name ? Data->nodes[i].name : "");
				cam.Node = static_cast<int>(i);
				if (src->type == cgltf_camera_type_orthographic)
				{
					cam.Orthographic = true;
					cam.OrthoHalfHeight = src->data.orthographic.ymag;
					cam.NearPlane = src->data.orthographic.znear;
					cam.FarPlane = src->data.orthographic.zfar;
				}
				else if (src->type == cgltf_camera_type_perspective)
				{
					cam.FovYDegrees = glm::degrees(src->data.perspective.yfov);
					cam.NearPlane = src->data.perspective.znear;
					if (src->data.perspective.has_zfar)
						cam.FarPlane = src->data.perspective.zfar;
				}
				else
					continue;
				Out.m_Cameras.push_back(std::move(cam));
			}
		}

		void ConvertSkins()
		{
			for (cgltf_size i = 0; i < Data->skins_count; ++i)
			{
				const cgltf_skin& src = Data->skins[i];
				ModelSkin skin;
				for (cgltf_size j = 0; j < src.joints_count; ++j)
					skin.Joints.push_back(static_cast<int>(cgltf_node_index(Data, src.joints[j])));
				skin.InverseBind.assign(src.joints_count, glm::mat4(1.0f));
				if (src.inverse_bind_matrices && src.inverse_bind_matrices->count >= src.joints_count)
				{
					std::vector<float> f(src.joints_count * 16);
					cgltf_accessor_unpack_floats(src.inverse_bind_matrices, f.data(), f.size());
					for (cgltf_size j = 0; j < src.joints_count; ++j)
						skin.InverseBind[j] = ToMat4(&f[j * 16]);
				}
				Out.m_Skins.push_back(std::move(skin));
			}
		}

		void ConvertAnimations()
		{
			for (cgltf_size i = 0; i < Data->animations_count; ++i)
			{
				const cgltf_animation& src = Data->animations[i];
				AnimationClip clip;
				clip.Name = src.name ? src.name : "Animation " + std::to_string(i);
				for (cgltf_size c = 0; c < src.channels_count; ++c)
				{
					const cgltf_animation_channel& ch = src.channels[c];
					if (!ch.target_node || !ch.sampler)
						continue;
					AnimationChannel channel;
					channel.Node = static_cast<int>(cgltf_node_index(Data, ch.target_node));
					size_t components = 3;
					switch (ch.target_path)
					{
					case cgltf_animation_path_type_translation: channel.Target = AnimationTarget::Translation; break;
					case cgltf_animation_path_type_scale: channel.Target = AnimationTarget::Scale; break;
					case cgltf_animation_path_type_rotation: channel.Target = AnimationTarget::Rotation; components = 4; break;
					default:
						GB_CORE_WARN("glTF: animation '{}' targets morph weights, which are not supported yet", clip.Name);
						continue;
					}
					switch (ch.sampler->interpolation)
					{
					case cgltf_interpolation_type_step: channel.Interpolation = AnimationInterpolation::Step; break;
					case cgltf_interpolation_type_cubic_spline: channel.Interpolation = AnimationInterpolation::CubicSpline; break;
					default: channel.Interpolation = AnimationInterpolation::Linear; break;
					}

					const size_t keys = ch.sampler->input->count;
					const size_t perKey = channel.Interpolation == AnimationInterpolation::CubicSpline ? 3 : 1;
					if (keys == 0 || ch.sampler->output->count < keys * perKey)
						continue;
					channel.Times.resize(keys);
					cgltf_accessor_unpack_floats(ch.sampler->input, channel.Times.data(), keys);
					channel.Values.resize(keys * perKey * components);
					cgltf_accessor_unpack_floats(ch.sampler->output, channel.Values.data(), channel.Values.size());
					clip.Duration = std::max(clip.Duration, channel.Times.back());
					clip.Channels.push_back(std::move(channel));
				}
				Out.m_Clips.push_back(std::move(clip));
			}
		}
	};

	std::unique_ptr<Model> Model::Load(const std::string& path)
	{
		{
			std::string ext = path.size() >= 4 ? path.substr(path.size() - 4) : "";
			for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (ext == ".fbx")
				return LoadFbx(path);
		}
		cgltf_options options = {};
		cgltf_data* data = nullptr;
		cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
		if (result != cgltf_result_success)
		{
			GB_CORE_ERROR("glTF: cannot parse '{}' (cgltf error {})", path, static_cast<int>(result));
			return nullptr;
		}
		result = cgltf_load_buffers(&options, data, path.c_str());
		if (result == cgltf_result_success)
			result = cgltf_validate(data);
		if (result != cgltf_result_success)
		{
			GB_CORE_ERROR("glTF: '{}' is invalid or has missing buffers (cgltf error {})", path, static_cast<int>(result));
			cgltf_free(data);
			return nullptr;
		}

		std::unique_ptr<Model> model(new Model());
		ModelLoader loader{ *model, data, DirectoryOf(path), {} };

		for (cgltf_size i = 0; i < data->materials_count; ++i)
			model->m_Materials.push_back(loader.ConvertMaterial(data->materials[i]));

		loader.ConvertNodes();
		loader.ConvertSkins();
		loader.ConvertCameras();
		loader.ConvertAnimations();

		model->m_Meshes.resize(data->meshes_count);
		std::vector<std::vector<Model::PrimitiveCPU>> cpu(data->meshes_count);
		for (cgltf_size m = 0; m < data->meshes_count; ++m)
		{
			const cgltf_mesh& mesh = data->meshes[m];
			for (cgltf_size p = 0; p < mesh.primitives_count; ++p)
			{
				const cgltf_primitive& prim = mesh.primitives[p];
				Model::PrimitiveCPU primitiveCPU;
				std::unique_ptr<Mesh> gpu = loader.ConvertPrimitive(prim, primitiveCPU);
				if (!gpu)
					continue;
				Primitive primitive;
				primitive.MeshData = std::move(gpu);
				primitive.MaterialIndex = prim.material ? static_cast<int>(cgltf_material_index(data, prim.material)) : -1;
				model->m_Meshes[m].push_back(std::move(primitive));
				cpu[m].push_back(std::move(primitiveCPU));
			}
		}

		model->ComputeBounds(cpu);

		cgltf_free(data);
		GB_CORE_INFO("Loaded glTF '{}': {} meshes, {} nodes, {} materials, {} textures, {} triangles, {} skins, {} animations, {} cameras",
			path, model->m_Meshes.size(), model->m_Nodes.size(), model->m_Materials.size(),
			model->m_Textures.size(), model->m_TriangleCount, model->m_Skins.size(), model->m_Clips.size(), model->m_Cameras.size());
		return model;
	}

	void Model::ComputeBounds(const std::vector<std::vector<PrimitiveCPU>>& cpu)
	{
		// Bounds in the rest pose: rigid vertices by their node's world, skinned ones through the rest palette.
		const size_t nodeCount = m_Nodes.size();
		std::vector<glm::mat4> world(nodeCount, glm::mat4(1.0f));
		{
			std::function<void(int, const glm::mat4&)> visit = [&](int n, const glm::mat4& parent)
			{
				const ModelNode& node = m_Nodes[n];
				world[n] = parent * glm::translate(glm::mat4(1.0f), node.Translation) * glm::mat4_cast(node.Rotation)
					* glm::scale(glm::mat4(1.0f), node.Scale);
				for (int child : node.Children)
					visit(child, world[n]);
			};
			for (int root : m_RootNodes)
				visit(root, glm::mat4(1.0f));
		}
		glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
		for (size_t n = 0; n < nodeCount; ++n)
		{
			const ModelNode& node = m_Nodes[n];
			if (node.MeshIndex < 0)
				continue;
			std::vector<glm::mat4> palette;
			if (node.SkinIndex >= 0)
			{
				const ModelSkin& skin = m_Skins[node.SkinIndex];
				for (size_t j = 0; j < skin.Joints.size(); ++j)
					palette.push_back(world[skin.Joints[j]] * skin.InverseBind[j]);
			}
			for (const Model::PrimitiveCPU& prim : cpu[node.MeshIndex])
			{
				for (size_t v = 0; v < prim.Positions.size(); ++v)
				{
					glm::vec4 p(prim.Positions[v], 1.0f), w4;
					if (!prim.Skin.empty() && !palette.empty())
					{
						const SkinVertex& sv = prim.Skin[v];
						glm::vec4 acc(0.0f);
						for (int k = 0; k < 4; ++k)
							if (sv.Weights[k] > 0.0f && sv.Joints[k] < palette.size())
								acc += sv.Weights[k] * (palette[sv.Joints[k]] * p);
						w4 = acc;
					}
					else
						w4 = world[n] * p;
					lo = glm::min(lo, glm::vec3(w4));
					hi = glm::max(hi, glm::vec3(w4));
				}
			}
		}
		if (lo.x <= hi.x)
		{
			m_BoundsMin = lo;
			m_BoundsMax = hi;
		}
	}

	int Model::FindClip(const std::string& name) const
	{
		for (size_t i = 0; i < m_Clips.size(); ++i)
			if (m_Clips[i].Name == name)
				return static_cast<int>(i);
		return -1;
	}

	int Model::FindNode(const std::string& name) const
	{
		for (size_t i = 0; i < m_Nodes.size(); ++i)
			if (m_Nodes[i].Name == name)
				return static_cast<int>(i);
		return -1;
	}

	Material Model::GetMaterial(int materialIndex) const
	{
		if (materialIndex >= 0 && static_cast<size_t>(materialIndex) < m_Materials.size())
			return m_Materials[materialIndex];
		Material m;   // glTF default material
		m.Metallic = 1.0f;
		m.Roughness = 1.0f;
		m.HeightScale = 0.0f;
		m.CavityStrength = 0.0f;
		return m;
	}

	void Model::AddToScene(Scene& scene, const glm::mat4& root) const
	{
		ModelInstance rest(*this);   // rest pose, no playback
		const size_t first = scene.Objects.size();
		rest.AddToScene(scene, root);
		// The palettes die with `rest`: skinned meshes are drawn unskinned (bind pose) in static scenes.
		for (size_t i = first; i < scene.Objects.size(); ++i)
			scene.Objects[i].JointMatrices = nullptr;
	}

	int Model::AddClip(AnimationClip clip)
	{
		const int existing = FindClip(clip.Name);
		if (existing >= 0)
		{
			m_Clips[static_cast<size_t>(existing)] = std::move(clip);
			return existing;
		}
		m_Clips.push_back(std::move(clip));
		return static_cast<int>(m_Clips.size()) - 1;
	}
}
