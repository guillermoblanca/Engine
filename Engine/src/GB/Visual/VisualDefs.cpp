#include "GB/Visual/VisualDefs.h"
#include "GB/Visual/ProcTexture.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>
#include <sstream>

namespace GB
{
	namespace
	{
		using Json = nlohmann::json;
		constexpr float kPi = 3.14159265358979f;

		const char* const kShapes[] = { "box", "cylinder", "blob", "blade" };
		const char* const kLayouts[] = { "ring", "row", "stack", "scatter" };
		const char* const kSockets[] = { "hand_l", "hand_r", "head", "back", "carry", "hip_l", "hip_r", "foot_l", "foot_r" };

		template <size_t N> bool OneOf(const std::string& s, const char* const (&list)[N])
		{
			for (const char* x : list) if (s == x) return true;
			return false;
		}

		Json Vec(const glm::vec3& v) { return Json::array({ v.x, v.y, v.z }); }
		Json Vec(const glm::vec2& v) { return Json::array({ v.x, v.y }); }

		// Reads [x, y, z] (or one number for all three); anything else is a mistake reported by the caller.
		bool ReadVec(const Json& j, const char* key, glm::vec3& out)
		{
			const auto it = j.find(key);
			if (it == j.end()) return true;
			if (it->is_number()) { out = glm::vec3(it->get<float>()); return true; }
			if (!it->is_array() || it->size() != 3) return false;
			for (size_t i = 0; i < 3; ++i) { if (!(*it)[i].is_number()) return false; out[static_cast<int>(i)] = (*it)[i].get<float>(); }
			return true;
		}

		bool ReadVec2(const Json& j, const char* key, glm::vec2& out)
		{
			const auto it = j.find(key);
			if (it == j.end()) return true;
			if (!it->is_array() || it->size() != 2 || !(*it)[0].is_number() || !(*it)[1].is_number()) return false;
			out = { (*it)[0].get<float>(), (*it)[1].get<float>() };
			return true;
		}

		Json PartToJson(const VisualPart& p)
		{
			const VisualPart d;
			Json j;
			j["name"] = p.Name;
			j["shape"] = p.Shape;
			if (p.Position != d.Position) j["pos"] = Vec(p.Position);
			j["size"] = Vec(p.Size);
			if (p.Rotation != d.Rotation) j["rot"] = Vec(p.Rotation);
			j["material"] = p.Material;
			if (p.Sides != d.Sides) j["sides"] = p.Sides;
			if (p.Bend != d.Bend) j["bend"] = p.Bend;
			if (p.Detail != d.Detail) j["detail"] = p.Detail;
			if (p.Rough != d.Rough) j["rough"] = p.Rough;
			if (p.Flat != d.Flat) j["flat"] = p.Flat;
			if (p.Count != d.Count) j["count"] = p.Count;
			if (p.Layout != d.Layout) j["layout"] = p.Layout;
			if (p.Radius != d.Radius) j["radius"] = p.Radius;
			if (p.Arc != d.Arc) j["arc"] = p.Arc;
			if (p.Spacing != d.Spacing) j["spacing"] = p.Spacing;
			if (p.Columns != d.Columns) j["columns"] = p.Columns;
			if (p.Jitter != d.Jitter) j["jitter"] = p.Jitter;
			if (p.Face != d.Face) j["face"] = p.Face;
			if (p.ShowFrom != d.ShowFrom || p.ShowTo != d.ShowTo) j["show"] = Json::array({ p.ShowFrom, p.ShowTo });
			if (p.Progressive) j["progressive"] = true;
			return j;
		}

		std::string PartFromJson(const Json& j, const std::map<std::string, VisualMaterial>& materials, VisualPart& p, const std::string& where)
		{
			if (!j.is_object()) return where + ": a part must be an object";
			p.Name = j.value("name", "");
			p.Shape = j.value("shape", "box");
			if (!OneOf(p.Shape, kShapes)) return where + " '" + p.Name + "': unknown shape '" + p.Shape + "' (box, cylinder, blob, blade)";
			if (!ReadVec(j, "pos", p.Position) || !ReadVec(j, "size", p.Size) || !ReadVec(j, "rot", p.Rotation)) return where + " '" + p.Name + "': pos, size and rot are [x, y, z]";
			p.Material = j.value("material", "");
			if (!materials.count(p.Material)) return where + " '" + p.Name + "': unknown material '" + p.Material + "'";
			p.Sides = j.value("sides", p.Sides);
			p.Bend = j.value("bend", p.Bend);
			p.Detail = j.value("detail", p.Detail);
			p.Rough = j.value("rough", p.Rough);
			p.Flat = j.value("flat", p.Flat);
			p.Count = j.value("count", p.Count);
			if (p.Count < 1 || p.Count > 256) return where + " '" + p.Name + "': count must be 1..256";
			p.Layout = j.value("layout", p.Layout);
			if (!OneOf(p.Layout, kLayouts)) return where + " '" + p.Name + "': unknown layout '" + p.Layout + "' (ring, row, stack, scatter)";
			p.Radius = j.value("radius", p.Radius);
			p.Arc = j.value("arc", p.Arc);
			p.Spacing = j.value("spacing", p.Spacing);
			p.Columns = std::max(1, j.value("columns", p.Columns));
			p.Jitter = std::clamp(j.value("jitter", p.Jitter), 0.0f, 1.0f);
			p.Face = j.value("face", p.Face);
			if (const auto it = j.find("show"); it != j.end())
			{
				if (!it->is_array() || it->size() != 2 || !(*it)[0].is_number() || !(*it)[1].is_number()) return where + " '" + p.Name + "': show is [from, to]";
				p.ShowFrom = (*it)[0].get<float>();
				p.ShowTo = (*it)[1].get<float>();
				if (p.ShowTo < p.ShowFrom) return where + " '" + p.Name + "': show [from, to] needs from <= to";
			}
			p.Progressive = j.value("progressive", false);
			return "";
		}

		Json PartsToJson(const std::vector<VisualPart>& parts)
		{
			Json list = Json::array();
			for (const VisualPart& p : parts) list.push_back(PartToJson(p));
			return list;
		}

		std::string PartsFromJson(const Json& j, const std::map<std::string, VisualMaterial>& materials, std::vector<VisualPart>& out, const std::string& where)
		{
			if (!j.is_array()) return where + ": parts must be a list";
			for (const Json& e : j)
			{
				VisualPart p;
				if (std::string why = PartFromJson(e, materials, p, where); !why.empty()) return why;
				out.push_back(std::move(p));
			}
			return "";
		}

		glm::mat4 Rotation(const glm::vec3& degrees)
		{
			glm::mat4 m(1.0f);
			m = glm::rotate(m, glm::radians(degrees.y), glm::vec3(0, 1, 0));
			m = glm::rotate(m, glm::radians(degrees.x), glm::vec3(1, 0, 0));
			m = glm::rotate(m, glm::radians(degrees.z), glm::vec3(0, 0, 1));
			return m;
		}

		// The shape of a part, at its own origin, for one copy (the seed varies a blob's lumps).
		MeshData Shape(const VisualPart& p, uint32_t seed)
		{
			if (p.Shape == "cylinder") return ProcMesh::Cylinder(p.Size.x * 0.5f, p.Size.z * 0.5f, p.Size.y, p.Sides, true, p.Bend);
			if (p.Shape == "blob") return ProcMesh::Blob(p.Size, p.Detail, p.Rough, seed, p.Flat);
			if (p.Shape == "blade") return ProcMesh::Blade(p.Size.x, p.Size.y, p.Size.z);
			return ProcMesh::Box(p.Size);
		}

		// Where copy `i` of a part goes (before the part's own position and rotation), with the jitter of the variant.
		glm::mat4 CopyTransform(const VisualPart& p, int i, uint32_t seed)
		{
			const float fi = static_cast<float>(i);
			glm::vec3 at(0.0f);
			float yaw = 0.0f;
			if (p.Layout == "ring" && (p.Radius > 0.0f || p.Count > 1))
			{
				const bool full = std::fabs(p.Arc - 360.0f) < 1e-3f;
				const float step = glm::radians(p.Arc) / static_cast<float>(full ? p.Count : std::max(1, p.Count - 1));
				const float a = step * fi;
				at = { std::cos(a) * p.Radius, 0.0f, std::sin(a) * p.Radius };
				if (p.Face) yaw = -a;
			}
			else if (p.Layout == "row")
				at = { (fi - static_cast<float>(p.Count - 1) * 0.5f) * p.Spacing, 0.0f, 0.0f };
			else if (p.Layout == "stack")
			{
				// rows of Columns along X, layers going up; a layer sits in the hollows of the one below (half a spacing in) and has one less
				int layer = 0, inLayer = i, width = p.Columns;
				while (inLayer >= width && width > 1) { inLayer -= width; ++layer; --width; }
				if (width <= 1 && inLayer >= 1) { layer += inLayer; inLayer = 0; }
				const float x = (static_cast<float>(inLayer) - static_cast<float>(width - 1) * 0.5f) * p.Spacing;
				at = { x, static_cast<float>(layer) * p.Spacing * 0.85f, 0.0f };
			}
			else if (p.Layout == "scatter")
			{
				const float a = 2.3999632f * fi, r = p.Radius * std::sqrt((fi + 0.5f) / static_cast<float>(p.Count));
				at = { std::cos(a) * r, 0.0f, std::sin(a) * r };
				if (p.Face) yaw = -a;
			}
			float scale = 1.0f;
			if (p.Jitter > 0.0f)
			{
				const uint32_t h = VisualHash(seed, static_cast<uint32_t>(i) * 7919u + 13u);
				scale = 1.0f + p.Jitter * (HashUnit(h) - 0.5f) * 0.6f;
				yaw += p.Jitter * (HashUnit(VisualHash(h, 3u)) - 0.5f) * 1.2f;
				const float spread = std::max(p.Radius, p.Spacing) * 0.25f * p.Jitter;
				at.x += (HashUnit(VisualHash(h, 5u)) - 0.5f) * spread;
				at.z += (HashUnit(VisualHash(h, 9u)) - 0.5f) * spread;
			}
			return glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), at), yaw, glm::vec3(0, 1, 0)), glm::vec3(scale));
		}

		bool CopyVisible(const VisualPart& p, int i, float f)
		{
			if (!p.Progressive) return f > p.ShowFrom && f <= p.ShowTo + 1e-6f;
			if (f > p.ShowTo + 1e-6f) return false;
			const float threshold = p.ShowFrom + (p.ShowTo - p.ShowFrom) * static_cast<float>(i) / static_cast<float>(std::max(1, p.Count));
			return f > threshold;
		}
	}

	std::string LoadVisuals(const std::string& json, VisualLibrary& library)
	{
		const Json doc = Json::parse(json, nullptr, false);
		if (doc.is_discarded() || !doc.is_object()) return "the visuals are not valid JSON";
		// only the "visuals" object: the other files of a pack have keys of their own with the same names ("buildings" of H8 in era1.json)
		if (!doc.contains("visuals")) return "";
		const Json& root = doc["visuals"];
		if (!root.is_object()) return "\"visuals\" must be an object";
		VisualLibrary next = library;   // all or nothing: a mistake leaves the library as it was
		try
		{
			if (const auto it = root.find("materials"); it != root.end())
			{
				if (!it->is_object()) return "materials must be an object";
				for (const auto& [id, j] : it->items())
				{
					VisualMaterial m;
					if (!ReadVec(j, "color", m.Color) || !ReadVec(j, "emissive", m.Emissive)) return "material '" + id + "': color and emissive are [r, g, b]";
					m.Texture = j.value("texture", "");
					if (!m.Texture.empty() && !ProcTexture::Known(m.Texture)) return "material '" + id + "': unknown procedural texture '" + m.Texture + "'";
					m.UVScale = j.value("uvScale", 1.0f);
					m.Roughness = j.value("roughness", 0.8f);
					m.Metallic = j.value("metallic", 0.0f);
					m.Grain = j.value("grain", 0.0f);
					m.Wind = j.value("wind", 0.0f);
					m.Seasonal = j.value("seasonal", false);
					m.DoubleSided = j.value("doubleSided", false);
					next.Materials[id] = m;
				}
			}
			if (const auto it = root.find("models"); it != root.end())
			{
				if (!it->is_object()) return "models must be an object";
				for (const auto& [id, j] : it->items())
				{
					VisualModel m;
					m.Variants = std::clamp(j.value("variants", 3), 1, 8);
					m.Note = j.value("note", "");
					if (!j.contains("parts")) return "model '" + id + "' has no parts";
					if (std::string why = PartsFromJson(j["parts"], next.Materials, m.Parts, "model '" + id + "'"); !why.empty()) return why;
					if (m.Parts.empty()) return "model '" + id + "' has no parts";
					next.Models[id] = std::move(m);
				}
			}
			if (const auto it = root.find("buildings"); it != root.end())
			{
				if (!it->is_object()) return "buildings must be an object";
				for (const auto& [id, j] : it->items())
				{
					BuildingVisual b;
					if (!ReadVec2(j, "footprint", b.Footprint)) return "building '" + id + "': footprint is [x, z]";
					b.Note = j.value("note", "");
					if (!j.contains("phases") || !j["phases"].is_array() || j["phases"].empty()) return "building '" + id + "' has no phases";
					for (const Json& ph : j["phases"])
					{
						BuildingVisualPhase phase;
						phase.Name = ph.value("name", "");
						if (!ph.contains("parts")) return "building '" + id + "', phase '" + phase.Name + "' has no parts";
						if (std::string why = PartsFromJson(ph["parts"], next.Materials, phase.Parts, "building '" + id + "', phase '" + phase.Name + "'"); !why.empty()) return why;
						if (phase.Parts.empty()) return "building '" + id + "', phase '" + phase.Name + "' has no parts";
						b.Phases.push_back(std::move(phase));
					}
					next.Buildings[id] = std::move(b);
				}
			}
			if (const auto it = root.find("creatures"); it != root.end())
				for (const auto& [tag, j] : it->items())
				{
					CreatureVisual c;
					c.Model = j.value("model", "");
					c.Procedural = j.value("procedural", "");
					c.Length = j.value("length", 0.6f);
					c.Yaw = j.value("yaw", 0.0f);
					c.Idle = j.value("idle", "");
					c.Walk = j.value("walk", "");
					c.Run = j.value("run", "");
					c.RunSpeed = j.value("runSpeed", 1.6f);
					if (c.Model.empty() == c.Procedural.empty()) return "creature '" + tag + "' needs a model or a procedural model (one of them)";
					next.Creatures[tag] = c;
				}
			if (const auto it = root.find("piles"); it != root.end())
				for (const auto& [tag, j] : it->items())
					next.Piles[tag] = { j.value("model", ""), std::max(1, j.value("full", 20)) };
			if (const auto it = root.find("reach"); it != root.end())
				for (const auto& [name, j] : it->items())
				{
					ReachRule r;
					r.Hand = j.value("hand", r.Hand);
					if (r.Hand != "right" && r.Hand != "left" && r.Hand != "both") return "reach '" + name + "': hand is right, left or both";
					r.Height = j.value("height", r.Height);
					r.Toward = j.value("toward", r.Toward);
					r.From = j.value("from", r.From);
					r.To = j.value("to", r.To);
					r.Weight = std::clamp(j.value("weight", r.Weight), 0.0f, 1.0f);
					next.Reach[name] = r;
				}
			if (const auto it = root.find("props"); it != root.end())
			{
				const Json& p = *it;
				PropRules& r = next.Props;
				if (const auto l = p.find("looks"); l != p.end())
					for (const auto& [id, j] : l->items())
					{
						PropLook look;
						look.Model = j.value("model", "");
						if (!ReadVec(j, "offset", look.Offset) || !ReadVec(j, "rot", look.Rotation)) return "prop look '" + id + "': offset and rot are [x, y, z]";
						r.Looks[id] = look;
					}
				auto strings = [&](const char* key, std::map<std::string, std::string>& out)
				{
					if (const auto m = p.find(key); m != p.end() && m->is_object())
						for (const auto& [k, v] : m->items()) if (v.is_string()) out[k] = v.get<std::string>();
				};
				strings("items", r.Items);
				strings("rest", r.RestSockets);
				strings("work", r.WorkTools);
				strings("hand", r.HandLooks);
				if (const auto c = p.find("carry"); c != p.end())
				{
					if (!c->is_array()) return "props.carry must be a list";
					r.Carry.clear();
					for (const Json& j : *c)
					{
						CarryRule rule;
						if (const auto t = j.find("tags"); t != j.end()) for (const Json& s : *t) rule.Tags.push_back(s.get<std::string>());
						rule.Look = j.value("look", "");
						rule.Set = j.value("set", "");
						rule.MinUnits = std::max(1, j.value("minUnits", 3));
						r.Carry.push_back(rule);
					}
				}
				if (const auto c = p.find("carryJobs"); c != p.end() && c->is_array())
				{
					r.CarryJobs.clear();
					for (const Json& s : *c) r.CarryJobs.push_back(s.get<std::string>());
				}
				r.MaxShown = std::clamp(p.value("maxShown", r.MaxShown), 0, 8);
			}
		}
		catch (const Json::exception& e)
		{
			return std::string("visuals: ") + e.what();
		}
		// cross checks: what the props, piles and creatures name must exist
		for (const auto& [id, look] : next.Props.Looks)
			if (!next.Models.count(look.Model)) return "prop look '" + id + "' uses the unknown model '" + look.Model + "'";
		for (const auto& [item, look] : next.Props.Items)
			if (!next.Props.Looks.count(look)) return "the item '" + item + "' is shown with the unknown look '" + look + "'";
		for (const auto& [job, look] : next.Props.HandLooks)
			if (!next.Props.Looks.count(look)) return "the activity '" + job + "' holds the unknown look '" + look + "'";
		for (const CarryRule& c : next.Props.Carry)
			if (!next.Props.Looks.count(c.Look)) return "a carry rule uses the unknown look '" + c.Look + "'";
		for (const auto& [family, socket] : next.Props.RestSockets)
			if (!OneOf(socket, kSockets)) return "'" + family + "' rests at the unknown socket '" + socket + "'";
		for (const auto& [tag, pile] : next.Piles)
			if (!next.Models.count(pile.Model)) return "the pile of '" + tag + "' uses the unknown model '" + pile.Model + "'";
		for (const auto& [tag, c] : next.Creatures)
			if (!c.Procedural.empty() && !next.Models.count(c.Procedural)) return "creature '" + tag + "' uses the unknown model '" + c.Procedural + "'";
		library = std::move(next);
		return "";
	}

	std::string LoadVisualsFile(const std::string& path, VisualLibrary& library)
	{
		std::ifstream in(path);
		if (!in) return "cannot open " + path;
		std::stringstream s;
		s << in.rdbuf();
		return LoadVisuals(s.str(), library);
	}

	std::string VisualsToJson(const VisualLibrary& lib)
	{
		Json root;
		Json materials = Json::object();
		for (const auto& [id, m] : lib.Materials)
		{
			Json j;
			j["color"] = Vec(m.Color);
			if (!m.Texture.empty()) j["texture"] = m.Texture;
			if (m.UVScale != 1.0f) j["uvScale"] = m.UVScale;
			j["roughness"] = m.Roughness;
			if (m.Metallic != 0.0f) j["metallic"] = m.Metallic;
			if (m.Emissive != glm::vec3(0.0f)) j["emissive"] = Vec(m.Emissive);
			if (m.Grain != 0.0f) j["grain"] = m.Grain;
			if (m.Wind != 0.0f) j["wind"] = m.Wind;
			if (m.Seasonal) j["seasonal"] = true;
			if (m.DoubleSided) j["doubleSided"] = true;
			materials[id] = j;
		}
		root["materials"] = materials;
		Json models = Json::object();
		for (const auto& [id, m] : lib.Models)
		{
			Json j;
			j["variants"] = m.Variants;
			if (!m.Note.empty()) j["note"] = m.Note;
			j["parts"] = PartsToJson(m.Parts);
			models[id] = j;
		}
		root["models"] = models;
		Json buildings = Json::object();
		for (const auto& [id, b] : lib.Buildings)
		{
			Json j;
			j["footprint"] = Vec(b.Footprint);
			if (!b.Note.empty()) j["note"] = b.Note;
			Json phases = Json::array();
			for (const BuildingVisualPhase& ph : b.Phases) phases.push_back({ { "name", ph.Name }, { "parts", PartsToJson(ph.Parts) } });
			j["phases"] = phases;
			buildings[id] = j;
		}
		root["buildings"] = buildings;
		Json creatures = Json::object();
		for (const auto& [tag, c] : lib.Creatures)
		{
			Json j;
			if (!c.Model.empty()) j["model"] = c.Model;
			if (!c.Procedural.empty()) j["procedural"] = c.Procedural;
			j["length"] = c.Length;
			if (c.Yaw != 0.0f) j["yaw"] = c.Yaw;
			if (!c.Idle.empty()) j["idle"] = c.Idle;
			if (!c.Walk.empty()) j["walk"] = c.Walk;
			if (!c.Run.empty()) j["run"] = c.Run;
			j["runSpeed"] = c.RunSpeed;
			creatures[tag] = j;
		}
		root["creatures"] = creatures;
		Json piles = Json::object();
		for (const auto& [tag, p] : lib.Piles) piles[tag] = { { "model", p.Model }, { "full", p.Full } };
		root["piles"] = piles;
		Json reach = Json::object();
		for (const auto& [name, r] : lib.Reach)
			reach[name] = { { "hand", r.Hand }, { "height", r.Height }, { "toward", r.Toward }, { "from", r.From }, { "to", r.To }, { "weight", r.Weight } };
		root["reach"] = reach;
		Json props;
		Json looks = Json::object();
		for (const auto& [id, l] : lib.Props.Looks)
		{
			Json j;
			j["model"] = l.Model;
			if (l.Offset != glm::vec3(0.0f)) j["offset"] = Vec(l.Offset);
			if (l.Rotation != glm::vec3(0.0f)) j["rot"] = Vec(l.Rotation);
			looks[id] = j;
		}
		props["looks"] = looks;
		props["items"] = lib.Props.Items;
		props["rest"] = lib.Props.RestSockets;
		props["work"] = lib.Props.WorkTools;
		props["hand"] = lib.Props.HandLooks;
		Json carry = Json::array();
		for (const CarryRule& c : lib.Props.Carry) carry.push_back({ { "tags", c.Tags }, { "look", c.Look }, { "set", c.Set }, { "minUnits", c.MinUnits } });
		props["carry"] = carry;
		props["carryJobs"] = lib.Props.CarryJobs;
		props["maxShown"] = lib.Props.MaxShown;
		root["props"] = props;
		Json doc;
		doc["visuals"] = root;
		return doc.dump(2) + "\n";
	}

	std::vector<bool> VisibleCopies(const VisualModel& model, float fraction)
	{
		std::vector<bool> mask;
		for (const VisualPart& p : model.Parts)
			for (int i = 0; i < p.Count; ++i) mask.push_back(CopyVisible(p, i, fraction));
		return mask;
	}

	std::string MaskKey(const std::vector<bool>& mask)
	{
		std::string key;
		key.reserve(mask.size());
		for (bool b : mask) key.push_back(b ? '1' : '0');
		return key;
	}

	std::vector<VisualPiece> BuildVisual(const VisualModel& model, const std::vector<bool>& visible, int variant)
	{
		std::vector<VisualPiece> pieces;
		const uint32_t seed = VisualHash(static_cast<uint32_t>(variant) + 1u, 0x2F6B7A3Du);
		size_t copy = 0;
		for (size_t pi = 0; pi < model.Parts.size(); ++pi)
		{
			const VisualPart& p = model.Parts[pi];
			const glm::mat4 own = glm::translate(glm::mat4(1.0f), p.Position) * Rotation(p.Rotation);
			const uint32_t partSeed = VisualHash(seed, static_cast<uint32_t>(pi) * 104729u);
			MeshData shape;   // one shape per part (blobs vary per copy below)
			for (int i = 0; i < p.Count; ++i, ++copy)
			{
				if (copy < visible.size() && !visible[copy]) continue;
				if (copy >= visible.size()) continue;
				VisualPiece* target = nullptr;
				for (VisualPiece& pc : pieces) if (pc.Material == p.Material) target = &pc;
				if (!target) { pieces.push_back({ MeshData(), p.Material }); target = &pieces.back(); }
				if (p.Shape == "blob") target->Mesh.Append(Shape(p, VisualHash(partSeed, static_cast<uint32_t>(i))), CopyTransform(p, i, partSeed) * own);
				else
				{
					if (shape.Empty()) shape = Shape(p, partSeed);
					// (ring and scatter copies sit at their spot and then take the part's own offset and turn)
					target->Mesh.Append(shape, CopyTransform(p, i, partSeed) * own);
				}
			}
		}
		pieces.erase(std::remove_if(pieces.begin(), pieces.end(), [](const VisualPiece& p) { return p.Mesh.Empty(); }), pieces.end());
		return pieces;
	}

	int VisualVariant(const VisualModel& model, const std::string& instance)
	{
		return static_cast<int>(VisualHash(instance.c_str(), 0x5EEDu) % static_cast<uint32_t>(std::max(1, model.Variants)));
	}

	VisualModel BuildingModel(const BuildingVisual& building, int phase, float progress, std::vector<bool>& visible)
	{
		VisualModel model;
		model.Variants = 1;
		visible.clear();
		if (phase < 0) return model;
		const int phases = static_cast<int>(building.Phases.size());
		const int last = std::min(phase, phases - 1);
		for (int ph = 0; ph <= last; ++ph)
		{
			const bool current = ph == phase;   // (phase >= phases: every phase is complete)
			int total = 0;
			for (const VisualPart& p : building.Phases[static_cast<size_t>(ph)].Parts) total += p.Count;
			const int up = current ? std::clamp(static_cast<int>(std::floor(std::clamp(progress, 0.0f, 1.0f) * static_cast<float>(total) + 1e-4f)), 0, total) : total;
			int k = 0;
			for (const VisualPart& p : building.Phases[static_cast<size_t>(ph)].Parts)
			{
				VisualPart copy = p;
				copy.ShowFrom = -1.0f;
				copy.ShowTo = 1.0f;
				copy.Progressive = false;
				model.Parts.push_back(copy);
				for (int i = 0; i < p.Count; ++i, ++k) visible.push_back(k < up);
			}
		}
		return model;
	}

	int BuildingCopies(const BuildingVisual& building, int phase, float progress, int* total)
	{
		std::vector<bool> mask;
		BuildingModel(building, phase, progress, mask);
		if (total) *total = static_cast<int>(mask.size());
		return static_cast<int>(std::count(mask.begin(), mask.end(), true));
	}

	size_t VisualTriangles(const std::vector<VisualPiece>& pieces)
	{
		size_t n = 0;
		for (const VisualPiece& p : pieces) n += p.Mesh.Triangles();
		return n;
	}
}
