// Entity world and scene files: hierarchy, transforms, names, duplication, JSON round trips and errors. CPU only.
#include "GB/Game/Entity.h"
#include "GB/Game/SceneIO.h"
#include <cmath>
#include <cstdio>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	bool Near(const glm::mat4& a, const glm::mat4& b, float eps = 1e-4f)
	{
		for (int c = 0; c < 4; ++c)
			for (int r = 0; r < 4; ++r)
				if (std::fabs(a[c][r] - b[c][r]) > eps) return false;
		return true;
	}
}

int main()
{
	using namespace GB;

	EntityWorld w;
	const EntityId a = w.Create("Crate");
	const EntityId b = w.Create("Crate");
	Check(w.Find(a)->Name == "Crate" && w.Find(b)->Name == "Crate 2", "names are made unique");
	Check(w.FindByName("Crate 2") == b, "find by name");
	Check(w.Rename(b, "Crate") && w.Find(b)->Name == "Crate 2", "rename keeps names unique");
	Check(w.Create("x", 999) == NullEntity, "unknown parent rejected");

	// the lookup by name remembers its answers; it must never return an entity that was deleted, renamed or replaced
	{
		EntityWorld n;
		const EntityId first = n.Create("Rabbit 1");
		Check(n.FindByName("Rabbit 1") == first && n.FindByName("Rabbit 1") == first, "a name is found twice in a row");
		n.Rename(first, "Hare");
		Check(n.FindByName("Rabbit 1") == NullEntity && n.FindByName("Hare") == first, "after a rename the old name is gone and the new one is found");
		n.Destroy(first);
		Check(n.FindByName("Hare") == NullEntity, "after a delete the name is gone");
		const EntityId second = n.Create("Hare");
		Check(n.FindByName("Hare") == second && second != first, "a new entity with the same name is found, not the old one");
		EntityWorld copy = n;
		copy.Destroy(second);
		const EntityId third = copy.Create("Other");
		Check(copy.FindByName("Hare") == NullEntity && copy.FindByName("Other") == third && n.FindByName("Hare") == second, "copies of a world do not share answers");
		Check(n.FindByName("") == NullEntity && n.FindByName("nothing here") == NullEntity, "unknown names are not found");
	}

	// Hierarchy and world matrices.
	w.Find(a)->Transform.Position = { 10, 0, 0 };
	w.Find(a)->Transform.Rotation = { 0, 90, 0 };
	const EntityId child = w.Create("Child", a);
	w.Find(child)->Transform.Position = { 0, 0, 2 };
	const glm::vec3 p = glm::vec3(w.WorldMatrix(child)[3]);
	Check(std::fabs(p.x - 12.0f) < 1e-4f && std::fabs(p.z) < 1e-4f, "child follows the rotated parent");

	// Reparenting keeps the world transform; cycles are rejected.
	const glm::mat4 before = w.WorldMatrix(child);
	Check(w.SetParent(child, b, true), "reparent");
	Check(Near(w.WorldMatrix(child), before), "reparent keeps world transform");
	Check(w.Find(a)->Children.empty() && w.Find(b)->Children.size() == 1, "children lists updated");
	Check(!w.SetParent(b, child), "cycle rejected");
	Check(!w.SetParent(b, b), "self parent rejected");

	// Activation inherits.
	w.Find(b)->Active = false;
	Check(!w.IsActive(child) && w.IsActive(a), "inactive parent deactivates children");
	w.Find(b)->Active = true;

	// Components and tags.
	w.Find(a)->Mesh.emplace().Shape = MeshShape::Sphere;
	w.Find(a)->Mesh->Albedo = { 0.1f, 0.2f, 0.3f };
	w.Find(a)->Light.emplace().Intensity = 12.5f;
	w.Find(child)->Body.emplace().Mass = 3.0f;
	w.Find(child)->Tags = { "loot", "wood" };
	Check(w.FindByTag("wood").size() == 1, "find by tag");

	// Duplicate and destroy.
	const size_t count = w.Count();
	const EntityId copy = w.Duplicate(b);
	Check(copy != NullEntity && w.Count() == count + 2, "duplicate copies descendants");
	Check(w.Find(copy)->Children.size() == 1 && w.Find(w.Find(copy)->Children[0])->Body.has_value(), "copy keeps components");
	Check(w.Destroy(copy) && w.Count() == count, "destroy removes the subtree");
	Check(!w.Destroy(copy), "destroy twice fails");

	// JSON round trip: second save is identical (stable, diff-friendly text).
	const std::string text = SaveSceneToString(w);
	EntityWorld loaded;
	const SceneIOResult r = LoadSceneFromString(loaded, text);
	Check(r.Ok, "load succeeds");
	Check(SaveSceneToString(loaded) == text, "save/load/save is identical");
	Check(loaded.Count() == w.Count(), "entity count preserved");
	Check(loaded.Find(child) && loaded.Find(child)->Parent == b, "ids and parents preserved");
	Check(loaded.Find(a)->Mesh && loaded.Find(a)->Mesh->Shape == MeshShape::Sphere, "mesh preserved");
	Check(loaded.Find(child)->Body && loaded.Find(child)->Body->Mass == 3.0f, "body preserved");
	Check(Near(loaded.WorldMatrix(child), w.WorldMatrix(child)), "transforms preserved");
	Check(loaded.Create("New") > child, "ids keep counting after a load");

	// Errors leave the target untouched.
	EntityWorld untouched;
	untouched.Create("Keep");
	Check(!LoadSceneFromString(untouched, "not json").Ok, "bad json rejected");
	Check(!LoadSceneFromString(untouched, R"({"version":2,"entities":[]})").Ok, "bad version rejected");
	Check(!LoadSceneFromString(untouched, R"({"version":1,"entities":[{"id":1},{"id":1}]})").Ok, "duplicate id rejected");
	Check(!LoadSceneFromString(untouched, R"({"version":1,"entities":[{"id":1,"parent":7}]})").Ok, "dangling parent rejected");
	Check(!LoadSceneFromString(untouched, R"({"version":1,"entities":[{"id":1,"mesh":{"shape":"torus"}}]})").Ok, "unknown shape rejected");
	Check(!LoadSceneFromString(untouched, R"({"version":1,"entities":[{"id":1,"position":[1,2]}]})").Ok, "bad vector rejected");
	Check(untouched.Count() == 1 && untouched.FindByName("Keep") != NullEntity, "failed load keeps the world");

	// Hand-written files need only the interesting fields.
	EntityWorld hand;
	Check(LoadSceneFromString(hand, R"({"version":1,"entities":[{"id":5,"name":"Lamp","position":[0,3,0],"light":{"intensity":40}}]})").Ok, "minimal file loads");
	Check(hand.Find(5) && hand.Find(5)->Light && hand.Find(5)->Light->Intensity == 40.0f, "minimal file values");

	// Props (generated trees and rocks) survive a round trip and a duplicate.
	EntityWorld props;
	const EntityId oak = props.Create("Oak");
	props.Find(oak)->Prop = PropComponent{ PropKind::Rock, 3 };
	EntityWorld propsLoaded;
	Check(LoadSceneFromString(propsLoaded, SaveSceneToString(props)).Ok, "prop scene loads");
	Check(propsLoaded.Find(oak)->Prop && propsLoaded.Find(oak)->Prop->Kind == PropKind::Rock && propsLoaded.Find(oak)->Prop->Variant == 3, "prop preserved");
	const EntityId oakCopy = props.Duplicate(oak);
	Check(props.Find(oakCopy)->Prop && props.Find(oakCopy)->Prop->Variant == 3, "prop duplicated");
	Check(!LoadSceneFromString(propsLoaded, R"({"version":1,"entities":[{"id":1,"prop":{"kind":"bush"}}]})").Ok, "unknown prop kind rejected");

	// a gameplay component with nothing in it (a character with no abilities yet) is still a component after a save and a load
	EntityWorld bare;
	const EntityId walker = bare.Create("walker");
	bare.Find(walker)->Gameplay.emplace();
	EntityWorld bareLoaded;
	Check(LoadSceneFromString(bareLoaded, SaveSceneToString(bare)).Ok, "a scene with an empty gameplay component loads");
	Check(bareLoaded.Find(bareLoaded.FindByName("walker")) && bareLoaded.Find(bareLoaded.FindByName("walker"))->Gameplay.has_value(), "an empty gameplay component survives a save and a load");

	if (failures) return 1;
	std::puts("entity tests passed");
	return 0;
}
