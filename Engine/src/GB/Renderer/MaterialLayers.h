#pragma once
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	struct Material;

	// Material layers: procedural effects a data material switches on and tunes ("layers": { "moss": { "amount": 0.4, "color": [r, g, b] } }
	// in visuals.json). The engine owns the names and the parameters: every layer is one entry of the registry, which says what it reads (name,
	// type, default, range), how its values land on a render Material and which shader uniforms carry them. The parser (VisualDefs) and the GL
	// side (VisualCache) only walk the registry, so a new layer is a new entry here (plus its block in the shader), not a change to either.
	//
	// This is the render frontend: data and parameters, no GL (docs: documentation/02_tecnico/motor/MATERIALES.md). Uploading the uniforms is the
	// backend's job; for now a layer writes the fields Material already has (Moss, MossColor, Grain...) and Renderer uploads those.

	enum class LayerParamType { Float, Vec3, Color };   // Color: linear RGB, each channel in the parameter's range

	struct MaterialLayerParam
	{
		const char* Name;                 // the JSON key inside the layer ("amount")
		LayerParamType Type;
		glm::vec3 Default;                // a float keeps its value in x
		float Min, Max;                   // every component must be in [Min, Max]
		const char* Uniform;              // the shader uniform that carries it ("uMoss"), for the docs and the backend
		const char* Note;
	};

	// The values a material gives a layer: parameter name -> value (a float in x). Missing parameters take their defaults.
	using MaterialLayerValues = std::map<std::string, glm::vec3>;
	using MaterialLayers = std::map<std::string, MaterialLayerValues>;   // layer name -> its values (only the layers the material switches on)

	struct MaterialLayerDef
	{
		const char* Name;                 // the key under "layers" ("moss")
		const char* Note;
		std::vector<MaterialLayerParam> Params;
		void (*Apply)(const MaterialLayerValues& values, Material& material);   // writes the layer onto a render material, nothing else
	};

	// Every layer the engine knows, in the order they are applied.
	const std::vector<MaterialLayerDef>& MaterialLayerRegistry();
	const MaterialLayerDef* FindMaterialLayer(const std::string& name);
	const MaterialLayerParam* FindLayerParam(const MaterialLayerDef& layer, const std::string& param);

	// Sets one parameter of a layer from the numbers the data gave (one for a float, three for a vec3 or a colour). Returns what is wrong (unknown
	// parameter, wrong type, out of range) or "".
	std::string SetLayerParam(const MaterialLayerDef& layer, MaterialLayerValues& values, const std::string& param, const std::vector<float>& numbers);
	// The value of a parameter: the material's, or the default.
	glm::vec3 LayerParam(const MaterialLayerDef& layer, const MaterialLayerValues& values, const std::string& param);

	// Applies every layer of `layers` onto `material` (the rest of the material is left as it is): a procedural material and a glTF kit piece go
	// the same way.
	void ApplyMaterialLayers(const MaterialLayers& layers, Material& material);
}
