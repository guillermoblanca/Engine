#include "GB/Renderer/MaterialLayers.h"
#include "GB/Renderer/Material.h"

namespace GB
{
	namespace
	{
		float Get(const MaterialLayerDef& layer, const MaterialLayerValues& values, const char* param) { return LayerParam(layer, values, param).x; }

		// Moss on the faces that look up, broken by world-space noise (PBRFS, "Procedural surface layers"): the mask uses the world position and
		// the world normal, so it changes with where a piece stands and how it is turned, and two copies of one glTF are not mossy alike.
		void ApplyMoss(const MaterialLayerValues& values, Material& m)
		{
			const MaterialLayerDef& self = *FindMaterialLayer("moss");
			m.Moss = Get(self, values, "amount");
			m.MossColor = LayerParam(self, values, "color");
		}

		// The renderer's noise bump and its albedo mottling (the same as a material's top-level "grain", which stays for the old data).
		void ApplyGrain(const MaterialLayerValues& values, Material& m)
		{
			const MaterialLayerDef& self = *FindMaterialLayer("grain");
			m.Grain = Get(self, values, "amount");
			m.GrainScale = Get(self, values, "scale");
		}

		std::vector<MaterialLayerDef> MakeRegistry()
		{
			const Material d;   // the defaults are the render material's, so a layer at its defaults draws as before
			return {
				{ "moss", "moss on top of the piece, by orientation and world-space noise",
					{ { "amount", LayerParamType::Float, glm::vec3(0.4f), 0.0f, 1.0f, "uMoss", "how much of the up-facing surface it covers (0 = none)" },
					  { "color", LayerParamType::Color, d.MossColor, 0.0f, 1.0f, "uMossColor", "linear RGB, mottled by the shader's fine noise" } },
					&ApplyMoss },
				{ "grain", "noise bump and mottling in world space (stone, bark)",
					{ { "amount", LayerParamType::Float, glm::vec3(0.35f), 0.0f, 1.0f, "uGrain", "strength of the bump and of the mottling" },
					  { "scale", LayerParamType::Float, glm::vec3(d.GrainScale), 0.1f, 50.0f, "uGrainScale", "noise frequency per metre" } },
					&ApplyGrain },
			};
		}
	}

	const std::vector<MaterialLayerDef>& MaterialLayerRegistry()
	{
		static const std::vector<MaterialLayerDef> registry = MakeRegistry();
		return registry;
	}

	const MaterialLayerDef* FindMaterialLayer(const std::string& name)
	{
		for (const MaterialLayerDef& l : MaterialLayerRegistry()) if (name == l.Name) return &l;
		return nullptr;
	}

	const MaterialLayerParam* FindLayerParam(const MaterialLayerDef& layer, const std::string& param)
	{
		for (const MaterialLayerParam& p : layer.Params) if (param == p.Name) return &p;
		return nullptr;
	}

	std::string SetLayerParam(const MaterialLayerDef& layer, MaterialLayerValues& values, const std::string& param, const std::vector<float>& numbers)
	{
		const MaterialLayerParam* p = FindLayerParam(layer, param);
		const std::string where = std::string("layer '") + layer.Name + "'";
		if (!p)
		{
			std::string known;
			for (const MaterialLayerParam& q : layer.Params) known += (known.empty() ? "" : ", ") + std::string(q.Name);
			return where + ": unknown parameter '" + param + "' (" + known + ")";
		}
		const bool scalar = p->Type == LayerParamType::Float;
		if (numbers.size() != (scalar ? 1u : 3u))
			return where + ", '" + param + "': " + (scalar ? "a number" : p->Type == LayerParamType::Color ? "a colour [r, g, b]" : "[x, y, z]");
		glm::vec3 v = scalar ? glm::vec3(numbers[0], 0.0f, 0.0f) : glm::vec3(numbers[0], numbers[1], numbers[2]);
		for (int i = 0; i < (scalar ? 1 : 3); ++i)
			if (!(v[i] >= p->Min && v[i] <= p->Max))
				return where + ", '" + param + "': out of range [" + std::to_string(p->Min) + ", " + std::to_string(p->Max) + "]";
		values[param] = v;
		return "";
	}

	glm::vec3 LayerParam(const MaterialLayerDef& layer, const MaterialLayerValues& values, const std::string& param)
	{
		if (const auto it = values.find(param); it != values.end()) return it->second;
		const MaterialLayerParam* p = FindLayerParam(layer, param);
		return p ? p->Default : glm::vec3(0.0f);
	}

	void ApplyMaterialLayers(const MaterialLayers& layers, Material& material)
	{
		for (const MaterialLayerDef& def : MaterialLayerRegistry())   // registry order, not the data's
			if (const auto it = layers.find(def.Name); it != layers.end()) def.Apply(it->second, material);
	}
}
