// The visual layer as data (GB/Visual, docs/VISUALES.md), CPU only: the "visuals" section of assets/data/visuals.json loads, goes and comes back
// through JSON and a broken document changes nothing; the procedural textures and meshes are deterministic, tile and stay within their budgets
// (low-res textures for `quality minimum`); every resource node of era 1 has a model whose look follows its Amount (berries go cluster by
// cluster, a felled tree leaves a stump); the props the tribe shows for a bag and a job (the axe in the hand while chopping and on the hip while
// walking, the basket on the back, a load in the arms with the carrying set on the way to the store); and the buildings' phases with the
// adapter H8 calls (BuildingVisuals::Build / Set / Read).
#include "GB/Gameplay/Buildings.h"
#include "GB/Gameplay/Items.h"
#include "GB/Gameplay/Resources.h"
#include "GB/Log.h"
#include "GB/Visual/BuildingVisuals.h"
#include "GB/Visual/ProcMesh.h"
#include "GB/Visual/ProcTexture.h"
#include "GB/Visual/PropVisuals.h"
#include "GB/Visual/SceneVisuals.h"
#include "GB/Visual/VisualDefs.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace
{
	int failures = 0;
	void Check(bool ok, const std::string& what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
	}

	int Visible(const std::vector<bool>& mask) { return static_cast<int>(std::count(mask.begin(), mask.end(), true)); }

	// Copies of the parts whose name starts with `name`, visible at `fraction`.
	int CopiesOf(const GB::VisualModel& m, const std::string& name, float fraction)
	{
		const std::vector<bool> mask = GB::VisibleCopies(m, fraction);
		int n = 0;
		size_t k = 0;
		for (const GB::VisualPart& p : m.Parts)
			for (int i = 0; i < p.Count; ++i, ++k)
				if (p.Name.rfind(name, 0) == 0 && mask[k]) ++n;
		return n;
	}

	bool HasProp(const GB::PropChoice& c, const std::string& look, const std::string& socket)
	{
		for (const GB::PropPlacement& p : c.Props) if (p.Look == look && p.Socket == socket) return true;
		return false;
	}
}

int main()
{
	using namespace GB;
	Log::Init();

	// ---- the data
	VisualLibrary lib;
	const std::string path = std::string(GB_ASSET_DIR) + "/data/visuals.json";
	const std::string error = LoadVisualsFile(path, lib);
	Check(error.empty(), "visuals.json loads: " + error);
	Check(lib.Models.size() >= 30 && lib.Buildings.size() >= 6 && lib.Materials.size() >= 20, "it has the node, prop, pile and plot models, six buildings and the materials");
	{
		VisualLibrary back;
		Check(LoadVisuals(VisualsToJson(lib), back).empty() && back == lib, "write and read back gives the same library");
		Check(VisualsToJson(back) == VisualsToJson(lib), "and the same text");
		VisualLibrary copy = lib;
		Check(!LoadVisuals("{ \"visuals\": { \"models\": { \"tree\": { \"parts\": [ { \"name\": \"x\", \"shape\": \"box\", \"material\": \"nope\" } ] } } } }", copy).empty() && copy == lib,
			"a part with an unknown material is refused and changes nothing");
		Check(!LoadVisuals("{ \"visuals\": { \"models\": { \"t2\": { \"parts\": [ { \"name\": \"x\", \"shape\": \"pyramid\", \"material\": \"bark\" } ] } } } }", copy).empty() && copy == lib,
			"an unknown shape is refused");
		Check(!LoadVisuals("{ \"visuals\": { \"props\": { \"items\": { \"stone_axe\": \"ghost\" } } } }", copy).empty() && copy == lib, "an item shown with an unknown look is refused");
		Check(!LoadVisuals("not json", copy).empty() && copy == lib, "garbage is refused");
		Check(LoadVisuals("{ \"buildings\": { \"drying_rack\": { \"phases\": [ { \"name\": \"frame\", \"work\": 6 } ] } } }", copy).empty() && copy == lib,
			"another file of the pack (H8's buildings in era1.json) is not read as visuals");
		Check(LoadVisuals("{ \"visuals\": { \"models\": { \"tree\": { \"variants\": 1, \"parts\": [ { \"name\": \"Trunk\", \"shape\": \"box\", \"material\": \"bark\" } ] } } } }", copy).empty()
			&& copy.Models.at("tree").Parts.size() == 1 && copy.Models.size() == lib.Models.size(), "a pack can redraw one model by id, keeping the rest");
	}

	// ---- procedural textures: deterministic, distinct, tileable, small for quality minimum
	{
		std::set<uint64_t> hashes;
		for (const std::string& kind : ProcTexture::Kinds())
		{
			const Image a = ProcTexture::Generate(kind, 64), b = ProcTexture::Generate(kind, 64);
			Check(a.IsValid() && a.Width == 64, kind + ": a 64 px texture");
			Check(ProcTexture::Hash(a) == ProcTexture::Hash(b), kind + ": the same pixels every time");
			hashes.insert(ProcTexture::Hash(a));
			// tiles: the last column is about as close to the first as neighbouring columns are to each other
			double seam = 0.0, inner = 0.0;
			for (uint32_t y = 0; y < a.Height; ++y)
				for (int c = 0; c < 3; ++c)
				{
					const auto px = [&](uint32_t x) { return static_cast<double>(a.Pixels[(static_cast<size_t>(y) * a.Width + x) * 4 + static_cast<size_t>(c)]); };
					seam += std::fabs(px(a.Width - 1) - px(0));
					inner += std::fabs(px(a.Width / 2) - px(a.Width / 2 - 1));
				}
			Check(seam <= inner * 2.5 + 64.0 * 3.0 * 6.0, kind + ": tiles across its edge (no seam much stronger than the texture's own detail)");
		}
		Check(hashes.size() == ProcTexture::Kinds().size(), "every kind looks different");
		Check(ProcTexture::Size(true) == 64 && ProcTexture::Size(false) == 256, "quality minimum makes them at 64 px instead of 256");
		Check(ProcTexture::Hash(ProcTexture::Generate("bark", 64, 1)) != ProcTexture::Hash(ProcTexture::Generate("bark", 64, 2)), "the seed changes the pattern");
		for (const auto& [id, m] : lib.Materials) Check(m.Texture.empty() || ProcTexture::Known(m.Texture), "material " + id + " names a texture this build makes");
	}

	// ---- procedural meshes: deterministic, sound, within budget
	{
		const MeshData a = ProcMesh::Blob(glm::vec3(1.0f), 2, 0.3f, 7), b = ProcMesh::Blob(glm::vec3(1.0f), 2, 0.3f, 7), c = ProcMesh::Blob(glm::vec3(1.0f), 2, 0.3f, 8);
		Check(a.Hash() == b.Hash() && a.Hash() != c.Hash(), "a blob is the same for the same seed and differs for another");
		bool sound = true;
		for (const MeshData& m : { a, ProcMesh::Box(glm::vec3(1, 2, 3)), ProcMesh::Cylinder(0.3f, 0.1f, 2.0f, 9, true, 0.2f), ProcMesh::Blade(0.1f, 1.0f, 0.2f) })
		{
			for (uint32_t i : m.Indices) sound = sound && i < m.Vertices.size();
			for (const Vertex& v : m.Vertices) sound = sound && std::fabs(glm::length(v.Normal) - 1.0f) < 1e-3f && std::isfinite(v.Position.x) && std::isfinite(v.Tangent.x);
		}
		Check(sound, "the shapes have indices in range, unit normals and finite tangents");
		// the outward faces of a box point out (winding and normals agree)
		const MeshData box = ProcMesh::Box(glm::vec3(1.0f));
		bool outward = true;
		for (size_t i = 0; i + 2 < box.Indices.size(); i += 3)
		{
			const glm::vec3 p0 = box.Vertices[box.Indices[i]].Position, p1 = box.Vertices[box.Indices[i + 1]].Position, p2 = box.Vertices[box.Indices[i + 2]].Position;
			outward = outward && glm::dot(glm::cross(p1 - p0, p2 - p0), (p0 + p1 + p2) / 3.0f) > 0.0f;
		}
		Check(outward, "a box's triangles wind outward");

		size_t worstNode = 0, worstProp = 0, worstBuilding = 0;
		for (const auto& [id, m] : lib.Models)
			for (int v = 0; v < m.Variants; ++v)
			{
				const std::vector<VisualPiece> one = BuildVisual(m, 1.0f, v), two = BuildVisual(m, 1.0f, v);
				Check(!one.empty(), "model " + id + " builds");
				Check(VisualTriangles(one) == VisualTriangles(two) && (one.empty() || one[0].Mesh.Hash() == two[0].Mesh.Hash()), "model " + id + " builds the same twice");
				const size_t tris = VisualTriangles(one);
				const bool prop = lib.Props.Looks.count(id) || id.rfind("axe", 0) == 0 || id.rfind("pick", 0) == 0;
				(prop ? worstProp : worstNode) = std::max(prop ? worstProp : worstNode, tris);
				Check(tris <= (prop ? 1500u : 9000u), "model " + id + " stays within its triangle budget (" + std::to_string(tris) + ")");
			}
		for (const auto& [id, b] : lib.Buildings)
		{
			std::vector<bool> mask;
			const VisualModel whole = BuildingModel(b, 1000, 1.0f, mask);
			const size_t tris = VisualTriangles(BuildVisual(whole, mask, 0));
			worstBuilding = std::max(worstBuilding, tris);
			Check(tris > 0 && tris <= 20000, "building " + id + " stays within 20000 triangles (" + std::to_string(tris) + ")");
		}
		std::printf("budgets: worst node/creature %zu, prop %zu, building %zu triangles\n", worstNode, worstProp, worstBuilding);
	}

	// ---- resource nodes: a model for every node of era 1, whose look follows the Amount
	{
		ItemLibrary items;
		RecipeBook recipes;
		std::map<std::string, ResourceNodeDef> nodes;
		RegisterEraOneItems(items, recipes, nodes);
		for (const auto& [id, def] : nodes)
		{
			const std::string model = def.Model.empty() ? id : def.Model;
			const auto it = lib.Models.find(model);
			Check(it != lib.Models.end(), "node " + id + " has a visual model");
			if (it == lib.Models.end()) continue;
			const int full = Visible(VisibleCopies(it->second, 1.0f)), half = Visible(VisibleCopies(it->second, 0.5f)), empty = Visible(VisibleCopies(it->second, 0.0f));
			Check(full > 0, "node " + id + ": drawn when full");
			Check(VisibleCopies(it->second, 0.0f) != VisibleCopies(it->second, 1.0f), "node " + id + ": a depleted node looks different from a full one");
			(void)half;
			// what is gathered goes as the node empties: never more of it at a lower amount
			int last = 1 << 30;
			bool monotone = true;
			for (int step = 10; step >= 1; --step)
			{
				const int n = CopiesOf(it->second, "Berries", static_cast<float>(step) / 10.0f) + CopiesOf(it->second, "Crown", static_cast<float>(step) / 10.0f)
					+ CopiesOf(it->second, "Flint", static_cast<float>(step) / 10.0f) + CopiesOf(it->second, "Ore", static_cast<float>(step) / 10.0f)
					+ CopiesOf(it->second, "Reeds", static_cast<float>(step) / 10.0f) + CopiesOf(it->second, "Tuft", static_cast<float>(step) / 10.0f)
					+ CopiesOf(it->second, "Mound", static_cast<float>(step) / 10.0f) + CopiesOf(it->second, "Rock", static_cast<float>(step) / 10.0f);
				monotone = monotone && n <= last;
				last = n;
			}
			Check(monotone, "node " + id + ": what is gathered never grows back as the amount falls");
			(void)empty;
		}
		const VisualModel& bush = lib.Models.at("berry_bush");
		Check(CopiesOf(bush, "Berries", 1.0f) == 13 && CopiesOf(bush, "Berries", 0.5f) > 0 && CopiesOf(bush, "Berries", 0.5f) < 13 && CopiesOf(bush, "Berries", 0.0f) == 0,
			"a berry bush loses its berries cluster by cluster: 13 full, some at half, none when empty");
		for (int a = 6; a > 0; --a)
			Check(CopiesOf(bush, "Berries", static_cast<float>(a - 1) / 6.0f) < CopiesOf(bush, "Berries", static_cast<float>(a) / 6.0f), "every berry picked (of 6) takes a cluster away");
		const VisualModel& tree = lib.Models.at("tree");
		Check(CopiesOf(tree, "Stump", 0.0f) == 1 && CopiesOf(tree, "Stump", 0.2f) == 0 && CopiesOf(tree, "Trunk", 0.0f) == 0 && CopiesOf(tree, "Trunk", 0.2f) == 1,
			"a felled tree is a stump, a standing one has its trunk");
		Check(CopiesOf(lib.Models.at("copper_vein"), "Ore", 1.0f) > 0 && CopiesOf(lib.Models.at("copper_vein"), "Ore", 0.0f) == 0 && CopiesOf(lib.Models.at("copper_vein"), "Base", 0.0f) == 1,
			"a copper vein keeps its rock and loses its green streaks");

		// the scene hook reads the node's tags and Amount (the live reader first, else the entity's own component)
		EntityWorld world;
		const ResourceLayout layout = BuildResourcePatch(world, glm::vec3(0.0f), nodes, 2);
		SceneVisuals visuals;
		visuals.Library = lib;
		visuals.SetNodes(&nodes);
		const EntityId bushId = world.FindByName("berry bush 1");
		Check(bushId != NullEntity, "the patch has a berry bush");
		SceneVisuals::Look look;
		Check(visuals.Describe(*world.Find(bushId), look) && look.Kind == "node" && look.Model == "berry_bush" && look.Fraction == 1.0f, "the hook draws a full bush as the berry_bush model");
		float live = 3.0f;
		visuals.SetAttributes([&](const std::string& entity, const std::string& attr, float& value) { if (entity == "berry bush 1" && attr == "Amount") { value = live; return true; } return false; });
		Check(visuals.Describe(*world.Find(bushId), look) && std::fabs(look.Fraction - 0.5f) < 1e-4f, "with 3 of 6 berries left it is half full");
		live = 0.0f;
		Check(visuals.Describe(*world.Find(bushId), look) && look.Fraction == 0.0f, "and empty with none");
		const EntityId part = world.Find(bushId)->Children.empty() ? NullEntity : world.Find(bushId)->Children[0];
		Check(part != NullEntity && !visuals.Describe(*world.Find(part), look), "its primitive parts are not drawn by the hook (they are skipped under it)");
		int variants = 0;
		std::set<int> seen;
		for (const std::string& n : layout.Nodes)
			if (visuals.Describe(*world.Find(world.FindByName(n)), look) && look.Model == "tree") { seen.insert(look.Variant); ++variants; }
		Check(variants >= 2 && seen.size() >= 2, "trees of a patch come in more than one variant");
	}

	// ---- props: what a member shows for its bag and its job
	{
		ItemLibrary items;
		RecipeBook recipes;
		std::map<std::string, ResourceNodeDef> nodes;
		RegisterEraOneItems(items, recipes, nodes);
		ItemDef hoe;
		hoe.Id = "stone_hoe";
		hoe.Tags = { "Item.Tool.Hoe" };
		items.Add(hoe);
		const PropRules& rules = lib.Props;
		auto choose = [&](const Inventory& bag, const std::string& activity, const std::string& ability = "")
		{
			PropContext c;
			c.Bag = &bag;
			c.Items = &items;
			c.Activity = activity;
			c.Ability = ability;
			return ChooseProps(rules, c);
		};
		Inventory bag(12);
		bag.Add(items, "stone_axe", 1);
		Check(HasProp(choose(bag, "gather"), "axe_stone_rest", "hip_r"), "a woodcutter walking to a tree has the axe on its right hip");
		const PropChoice chopping = choose(bag, "gather", "Chop");
		Check(HasProp(chopping, "axe_stone", "hand_r") && !HasProp(chopping, "axe_stone_rest", "hip_r"), "while chopping the axe is in the right hand (and not on the hip too)");
		Check(chopping.Props[0].Name == "tool", "the hand's tool is the prop called 'tool' (the one Chop's EquipProp holds: the look replaces its box)");
		bag.Add(items, "flint_axe", 1);
		Check(HasProp(choose(bag, "gather", "Chop"), "axe_flint", "hand_r"), "with a flint axe too, the better one is the one used");
		bag.Add(items, "basket_wicker", 1);
		bag.Add(items, "stone_pickaxe", 1);
		const PropChoice walking = choose(bag, "explore");
		Check(HasProp(walking, "basket", "back") && HasProp(walking, "axe_flint_rest", "hip_r") && HasProp(walking, "pick_stone_rest", "hip_l"),
			"walking: the basket on the back, the axe on the right hip, the pickaxe on the left");
		Check(HasProp(choose(bag, "gather", "Mine"), "pick_stone", "hand_r"), "mining takes the pickaxe in the hand");

		Inventory wood(12);
		wood.Add(items, "stone_axe", 1);
		wood.Add(items, "wood", 5);
		const PropChoice deposit = choose(wood, "deposit");
		Check(HasProp(deposit, "logs_bundle", "carry") && deposit.Set == "holding" && deposit.LoadUnits == 5, "five pieces of wood go to the store in the arms, with the holding set");
		Check(!HasProp(deposit, "axe_stone", "hand_r") && HasProp(deposit, "axe_stone_rest", "hip_r"), "the axe stays on the hip while the arms carry");
		Check(choose(wood, "gather").Set.empty() && !HasProp(choose(wood, "gather"), "logs_bundle", "carry"), "the same bag while gathering carries nothing in the arms");
		Inventory few(12);
		few.Add(items, "wood", 2);
		Check(choose(few, "deposit").Props.empty() && choose(few, "deposit").Set.empty(), "two pieces go in the bag: no load in the arms");
		Inventory stones(12);
		stones.Add(items, "stone", 4);
		Check(HasProp(choose(stones, "haul"), "stone_crate", "carry") && choose(stones, "haul").Set == "box", "stones are hauled in a crate with the box set");
		Inventory berries(12);
		berries.Add(items, "berry", 6);
		Check(HasProp(choose(berries, "deposit"), "berry_basket", "carry"), "berries go to the store in a basket in the arms");
		Inventory lots(30);
		lots.Add(items, "stone", 16);
		Check(HasProp(choose(lots, "deposit"), "wheelbarrow", "carry") && choose(lots, "deposit").Set == "wheelbarrow", "a big load goes in the wheelbarrow");
		Inventory fuel(12);
		fuel.Add(items, "wood", 1);
		Check(HasProp(choose(fuel, "stoke"), "firewood", "hand_l"), "on the way to stoke the fire it carries firewood in the left hand");
		Inventory farmer(12);
		farmer.Add(items, "stone_hoe", 1);
		Check(HasProp(choose(farmer, "farm.till"), "hoe_stone", "hand_r") && HasProp(choose(farmer, "rest"), "hoe_stone_rest", "hip_l"), "tilling takes the hoe in the hand; resting it hangs at the hip");
		Check(ActivityKey("farm", "till 0 - 1 2") == "farm.till" && ActivityKey("farm", "harvest 1 grain 3") == "farm.harvest" && ActivityKey("socialize", "", "elder") == "teach"
			&& ActivityKey("socialize", "", "adult") == "socialize" && ActivityKey("eat", "bag") == "eat", "the activity keys of the tribe's jobs");
		Check(static_cast<int>(choose(bag, "explore").Props.size()) <= rules.MaxShown, "never more props than MaxShown");
		std::set<std::string> sockets;
		bool unique = true;
		for (const PropPlacement& p : choose(bag, "gather", "Chop").Props) unique = unique && sockets.insert(p.Socket).second;
		Check(unique, "no two props share a socket");
		Check(choose(Inventory(12), "deposit").Props.empty(), "an empty bag shows nothing");

		// H14: the tool is the weapon in a fight, a raid or a hunt; the meat of a hunt goes home in the arms
		Inventory fighter(12);
		fighter.Add(items, "stone_pickaxe", 1);
		Check(HasProp(choose(fighter, "fight"), "pick_stone", "hand_r"), "with only a pickaxe, it fights with the pickaxe in its hand");
		fighter.Add(items, "stone_axe", 1);
		Check(HasProp(choose(fighter, "fight"), "axe_stone", "hand_r") && HasProp(choose(fighter, "fight"), "pick_stone_rest", "hip_l"), "with an axe too, the axe is the weapon and the pickaxe stays at the hip");
		Check(HasProp(choose(fighter, "hunt"), "axe_stone", "hand_r") && HasProp(choose(fighter, "raid"), "axe_stone", "hand_r"), "the same for a hunt and a raid");
		ItemDef meat;
		meat.Id = "meat";
		meat.Tags = { "Item.Food.Meat" };
		meat.MaxStack = 10;
		items.Add(meat);
		Inventory game(12);
		game.Add(items, "meat", 2);
		Check(HasProp(choose(game, "deposit"), "meat_bundle", "carry") && choose(game, "deposit").Set == "holding", "the meat of a rabbit goes to the store tied in its hide, in the arms");
	}

	// ---- buildings: phases, progress and the adapter for H8
	{
		const char* const wanted[] = { "shelter", "storage", "workbench", "drying_rack", "kiln", "palisade" };
		for (const char* id : wanted)
		{
			const auto it = lib.Buildings.find(id);
			Check(it != lib.Buildings.end(), std::string("building ") + id + " is described");
			if (it == lib.Buildings.end()) continue;
			const BuildingVisual& b = it->second;
			int total = 0, last = -1;
			bool monotone = true;
			for (int phase = 0; phase <= static_cast<int>(b.Phases.size()); ++phase)
				for (int step = 0; step <= 4; ++step)
				{
					const int n = BuildingCopies(b, phase, static_cast<float>(step) / 4.0f, &total);
					monotone = monotone && n >= last;
					last = n;
				}
			Check(monotone, std::string(id) + ": a site only grows as phases and progress go on");
			Check(BuildingCopies(b, 0, 0.0f) == 0, std::string(id) + ": an empty site at the start");
			Check(BuildingCopies(b, 1000, 0.0f, &total) == total && total > 0, std::string(id) + ": finished, every part is up");
			if (b.Phases.size() >= 2)
				Check(BuildingCopies(b, 1, 0.0f) > 0 && BuildingCopies(b, 1, 0.0f) < BuildingCopies(b, 1, 1.0f), std::string(id) + ": the second phase starts on the first and grows");
		}
		const BuildingVisual& shelter = lib.Buildings.at("shelter");
		Check(shelter.Phases.size() == 4 && shelter.Phases[0].Name == "foundation" && shelter.Phases[3].Name == "roof", "the hut goes foundation stakes, frame, walls, roof");
		Check(BuildingCopies(shelter, 0, 0.5f) == 5, "half of the foundation is five of its ten stakes (the floor comes last)");

		EntityWorld world;
		const EntityId site = BuildingVisuals::Build(world, "shelter", 1, 0.25f, "", NullEntity, glm::vec3(4.0f, 0.0f, 2.0f));
		std::string id;
		int phase = -1;
		float progress = -1.0f;
		Check(site != NullEntity && world.Find(site)->Name == "shelter site" && BuildingVisuals::Read(world, site, id, phase, progress) && id == "shelter" && phase == 1 && progress == 0.25f,
			"Build makes a building entity at its phase and progress");
		Check(BuildingVisuals::Set(world, site, 3, 0.5f) && BuildingVisuals::Read(world, site, id, phase, progress) && phase == 3 && progress == 0.5f, "Set moves it on");
		Check(BuildingVisuals::Build(world, "shelter", 4, 1.0f) == site, "building again with the same name updates the same entity");
		SceneVisuals visuals;
		visuals.Library = lib;
		SceneVisuals::Look look;
		Check(visuals.Describe(*world.Find(site), look) && look.Kind == "building" && look.Model == "shelter" && look.Phase == 4, "the scene hook draws it as the shelter at its phase");
		Check(!BuildingVisuals::Set(world, world.Create("not a building"), 1, 0.5f), "Set refuses an entity that is not a building");

		// H8's sites: "Building.Kind.<id>" with Phase and Work are drawn at the same share of the whole build in the visual phases
		const std::map<std::string, BuildingDef> defs = DefaultBuildings();
		visuals.SetBuildings(&defs);
		const BuildingDef& hut = defs.at("shelter");
		const EntityId h8 = BuildSite(world, hut, "Shelter 1", glm::vec3(10.0f, 0.0f, 0.0f));
		float phaseNow = 0.0f, workNow = 0.0f;
		visuals.SetAttributes([&](const std::string& entity, const std::string& attr, float& value)
		{
			if (entity != "Shelter 1") return false;
			if (attr == "Phase") { value = phaseNow; return true; }
			if (attr == "Work") { value = workNow; return true; }
			return false;
		});
		Check(visuals.Describe(*world.Find(h8), look) && look.Kind == "building" && look.Model == "shelter" && look.Phase == 0, "an H8 site is drawn as the shelter, at its first phase");
		std::vector<bool> mask;
		int lastUp = -1;
		bool grows = true;
		const int h8Phases = static_cast<int>(hut.Phases.size());
		for (int p = 0; p <= h8Phases; ++p)
			for (int k = 0; k <= 4; ++k)
			{
				if (p == h8Phases && k > 0) break;
				phaseNow = static_cast<float>(p);
				workNow = p < h8Phases ? hut.Phases[static_cast<size_t>(p)].Work * static_cast<float>(k) / 4.0f : 0.0f;
				visuals.Describe(*world.Find(h8), look);
				const int up = BuildingCopies(lib.Buildings.at("shelter"), look.Phase, look.Progress);
				grows = grows && up >= lastUp;
				lastUp = up;
			}
		int all = 0;
		BuildingCopies(lib.Buildings.at("shelter"), 1000, 1.0f, &all);
		Check(grows && lastUp == all, "as H8 puts work into its phases the site grows piece by piece up to the finished hut");
		Check(BuildingVisuals::PhaseCount(lib, "kiln") == 3 && BuildingVisuals::PhaseName(lib, "kiln", 1) == "dome" && BuildingVisuals::PhaseName(lib, "kiln", 3).empty(), "phases by name");
		int p2 = 0;
		float q2 = 0.0f;
		BuildingVisuals::FromOverall(lib, "shelter", BuildingVisuals::Overall(lib, "shelter", 2, 0.5f), p2, q2);
		Check(p2 == 2 && std::fabs(q2 - 0.5f) < 1e-4f && BuildingVisuals::Overall(lib, "shelter", 9, 0.0f) == 1.0f, "a whole build as one number and back");
	}

	if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
	std::printf("visuals: all checks passed\n");
	return 0;
}
