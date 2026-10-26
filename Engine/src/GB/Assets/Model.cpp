#include "Model.h"
#include "GB/Log.h"
#include "GB/Renderer/Image.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <map>

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

		std::unique_ptr<Mesh> ConvertPrimitive(const cgltf_primitive& prim)
		{
			if (prim.type != cgltf_primitive_type_triangles)
			{
				GB_CORE_WARN("glTF: skipping a non-triangle primitive");
				return nullptr;
			}
			const cgltf_accessor *position = nullptr, *normal = nullptr, *tangent = nullptr, *uv = nullptr;
			for (cgltf_size i = 0; i < prim.attributes_count; ++i)
			{
				const cgltf_attribute& a = prim.attributes[i];
				if (a.type == cgltf_attribute_type_position) position = a.data;
				else if (a.type == cgltf_attribute_type_normal) normal = a.data;
				else if (a.type == cgltf_attribute_type_tangent) tangent = a.data;
				else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) uv = a.data;
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
			Out.m_TriangleCount += indices.size() / 3;
			return std::make_unique<Mesh>(vertices, indices);
		}

		void VisitNode(const cgltf_node& node, const glm::mat4& parent)
		{
			float local[16];
			cgltf_node_transform_local(&node, local);
			const glm::mat4 world = parent * ToMat4(local);
			if (node.mesh)
				Out.m_Instances.push_back({ static_cast<int>(cgltf_mesh_index(Data, node.mesh)), world });
			for (cgltf_size i = 0; i < node.children_count; ++i)
				VisitNode(*node.children[i], world);
		}
	};

	std::unique_ptr<Model> Model::Load(const std::string& path)
	{
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

		model->m_Meshes.resize(data->meshes_count);
		for (cgltf_size m = 0; m < data->meshes_count; ++m)
		{
			const cgltf_mesh& mesh = data->meshes[m];
			for (cgltf_size p = 0; p < mesh.primitives_count; ++p)
			{
				const cgltf_primitive& prim = mesh.primitives[p];
				std::unique_ptr<Mesh> gpu = loader.ConvertPrimitive(prim);
				if (!gpu)
					continue;
				Primitive primitive;
				primitive.MeshData = std::move(gpu);
				primitive.MaterialIndex = prim.material ? static_cast<int>(cgltf_material_index(data, prim.material)) : -1;
				model->m_Meshes[m].push_back(std::move(primitive));
			}
		}

		const glm::mat4 identity(1.0f);
		if (data->scene)
		{
			for (cgltf_size i = 0; i < data->scene->nodes_count; ++i)
				loader.VisitNode(*data->scene->nodes[i], identity);
		}
		else if (data->scenes_count > 0)
		{
			for (cgltf_size i = 0; i < data->scenes[0].nodes_count; ++i)
				loader.VisitNode(*data->scenes[0].nodes[i], identity);
		}
		else
		{
			for (cgltf_size i = 0; i < data->nodes_count; ++i)
				if (!data->nodes[i].parent)
					loader.VisitNode(data->nodes[i], identity);
		}

		// Bounds: transform each primitive accessor's min/max corners (cgltf provides them when present).
		glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
		for (const Instance& inst : model->m_Instances)
		{
			const cgltf_mesh& mesh = data->meshes[inst.MeshIndex];
			for (cgltf_size p = 0; p < mesh.primitives_count; ++p)
			{
				for (cgltf_size a = 0; a < mesh.primitives[p].attributes_count; ++a)
				{
					const cgltf_attribute& attr = mesh.primitives[p].attributes[a];
					if (attr.type != cgltf_attribute_type_position || !attr.data->has_min || !attr.data->has_max)
						continue;
					for (int corner = 0; corner < 8; ++corner)
					{
						const glm::vec3 c((corner & 1) ? attr.data->max[0] : attr.data->min[0],
							(corner & 2) ? attr.data->max[1] : attr.data->min[1],
							(corner & 4) ? attr.data->max[2] : attr.data->min[2]);
						const glm::vec3 w = glm::vec3(inst.Transform * glm::vec4(c, 1.0f));
						lo = glm::min(lo, w);
						hi = glm::max(hi, w);
					}
				}
			}
		}
		if (lo.x <= hi.x)
		{
			model->m_BoundsMin = lo;
			model->m_BoundsMax = hi;
		}

		cgltf_free(data);
		GB_CORE_INFO("Loaded glTF '{}': {} meshes, {} instances, {} materials, {} textures, {} triangles",
			path, model->m_Meshes.size(), model->m_Instances.size(), model->m_Materials.size(),
			model->m_Textures.size(), model->m_TriangleCount);
		return model;
	}

	void Model::AddToScene(Scene& scene, const glm::mat4& root) const
	{
		for (const Instance& inst : m_Instances)
		{
			for (const Primitive& prim : m_Meshes[inst.MeshIndex])
			{
				RenderObject obj;
				obj.MeshPtr = prim.MeshData.get();
				if (prim.MaterialIndex >= 0)
					obj.Mat = m_Materials[prim.MaterialIndex];
				else
				{
					obj.Mat.Metallic = 1.0f;   // glTF default material
					obj.Mat.Roughness = 1.0f;
					obj.Mat.HeightScale = 0.0f;
					obj.Mat.CavityStrength = 0.0f;
				}
				obj.Transform = root * inst.Transform;
				scene.Objects.push_back(std::move(obj));
			}
		}
	}
}
