#pragma once
#include "GB/Game/History.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace GB
{
	// V2 (docs/VISION_MUNDO_VIVO.md): what a person values, from -50 to 50: family, knowledge, craft, tradition, peace, trade, nature, power. A clan's
	// culture sets where its people stand on each (a value of the clan, the same for all its members) and each person strays from it a little. Both
	// come from a hash of the clan and the name, not from the simulation's chance, so asking never changes a run (and a loaded game gives the same).
	// What a person makes of an event is its feeling moved by the value the event touches: a warlike person minds a raid less, a person who values
	// knowledge is gladder of a discovery. The talk (V2) and the requests (V3) say it; nothing in the simulation decides by it yet.
	struct ValueTouch
	{
		std::string Value;
		float Sign = 1.0f;   // +1: who holds the value likes such events more; -1: likes them less
	};

	struct ValueRules
	{
		unsigned Seed = 0;          // the world's seed: the same clan id is another culture in another world
		std::vector<std::string> Names = { "family", "knowledge", "craft", "tradition", "peace", "trade", "nature", "power" };
		int CultureSpread = 30;   // a clan's value: -this..this
		int PersonSpread = 20;    // a person's own: its clan's -this..this (clamped to -50..50)
		float Weight = 0.01f;     // how much a value of 50 moves the feeling of an event (0.5)
		std::map<std::string, ValueTouch> Kinds;   // event kind -> the value it touches
	};
	ValueRules DefaultValueRules();

	// A clan's value and a person's (`clan`: the clan the person grew in; -1 = none, only the person's own).
	int CultureValue(const ValueRules& rules, int clan, const std::string& value);
	int PersonValue(const ValueRules& rules, int clan, const std::string& person, const std::string& value);
	// The values of a person, name -> -50..50.
	std::map<std::string, int> PersonValues(const ValueRules& rules, int clan, const std::string& person);
	// What a person of `clan` makes of an event it remembers with `feeling`: -1 bad .. 1 good.
	float Opinion(const ValueRules& rules, int clan, const std::string& person, const HistoryEvent& e, float feeling);
	// The two values a person holds most strongly (for people: "values knowledge, scorns trade").
	std::string ValuesReport(const ValueRules& rules, int clan, const std::string& person);
}
