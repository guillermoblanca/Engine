// V1 (docs/VISION_MUNDO_VIVO.md): the world's history. Events get growing ids; a person remembers what it did, suffered or saw, once per event and
// the closest way; the history stays bounded however long the game runs (the light old events go first, the weighty ones stay), and it is saved
// and read back as it was. CPU only.
#include "GB/Game/History.h"
#include <cstdio>
#include <string>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
}

using namespace GB;

int GBTest_history(int, char**)
{
	WorldHistory h;
	HistoryEvent e;
	e.Kind = "death";
	e.Whom = "Ana";
	e.Text = "dies of Hunger";
	const int death = h.Add(e);
	Check(death == 1 && h.Find(death) && h.Find(death)->Weight == 5, "an event gets its id and its kind's weight");
	h.Remember("Biel", death, "saw", h.FeelingOf("death", "saw"));
	h.Remember("Biel", death, "heard", 0.0f);
	Check(h.MemoriesOf("Biel") && h.MemoriesOf("Biel")->size() == 1 && h.MemoriesOf("Biel")->front().How == "saw", "a person remembers an event once, the closest way");
	Check(h.MemoriesOf("Biel")->front().Feeling < 0.0f, "seeing a death feels bad");

	// a long game: thousands of small events (a hunt every so often) and a few weighty ones (a discovery every 500)
	h.Rules.MaxEvents = 1000;
	h.Rules.MaxMemories = 50;
	std::vector<int> weighty;
	for (int i = 0; i < 20000; ++i)
	{
		HistoryEvent x;
		x.At = i * 10.0;
		x.Kind = i % 500 == 0 ? "discovery" : "hunt";
		x.Who = "Biel";
		x.Text = "event " + std::to_string(i);
		const int id = h.Add(x);
		if (x.Kind == "discovery") weighty.push_back(id);
		h.Remember("Biel", id, "did", 0.5f);
	}
	Check(h.Events().size() <= h.Rules.MaxEvents + h.Rules.MaxEvents / 8, "the history stays bounded");
	Check(h.MemoriesOf("Biel")->size() <= h.Rules.MaxMemories, "a person's memories stay bounded");
	bool kept = h.Find(death) != nullptr;
	for (int id : weighty) kept = kept && h.Find(id) != nullptr;
	Check(kept, "the weighty events outlive the light ones");
	Check(h.Events().back().Text == "event 19999", "the newest events stay");
	for (size_t i = 1; i < h.Events().size(); ++i) Check(h.Events()[i - 1].Id < h.Events()[i].Id, "the ids keep growing");
	for (const Recollection& r : *h.MemoriesOf("Biel")) Check(h.Find(r.Event) != nullptr, "a memory points at an event that is still there");
	const std::vector<const HistoryEvent*> recall = h.Recall("Biel", 3);
	Check(recall.size() == 3 && recall[0]->Kind == "death" && recall[1]->Kind == "discovery", "a person recalls the weightiest first");

	// saved and read back as it was
	WorldHistory back;
	Check(back.FromJson(h.ToJson()), "the history reads back");
	Check(back.Events() == h.Events(), "the events read back the same");
	Check(*back.MemoriesOf("Biel") == *h.MemoriesOf("Biel"), "the memories read back the same");
	HistoryEvent next;
	next.Kind = "birth";
	Check(back.Add(next) == h.Add(next), "the next id goes on where it was");
	Check(!back.FromJson("{\"events\":[{\"id\":2},{\"id\":1}]}"), "ids out of order are refused");
	Check(back.Find(death) != nullptr, "a refused read changes nothing");

	if (failures == 0) std::printf("history: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
