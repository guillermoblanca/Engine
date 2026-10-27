#pragma once
#include <string>
#include <vector>

namespace GB
{
	enum class ModOp { Add, Multiply, Override };

	struct AttributeModifier
	{
		std::string Attribute;
		ModOp Op = ModOp::Add;
		float Magnitude = 0.0f;
	};

	// The numbers of a character or an object (energy, water, seeds, fruit...). Each has a base value, a minimum and a maximum. Instant
	// effects change the base for good; effects with a duration add modifiers that count while they last:
	//   current = clamp((base + sum of Add) * product of Multiply), or the last Override when there is one.
	class AttributeSet
	{
	public:
		struct Attribute
		{
			std::string Name;
			float Base = 0.0f, Min = 0.0f, Max = 1e9f;
			float Current = 0.0f;
		};

		// Adds the attribute (or redefines it). The base is clamped into [min, max].
		void Define(const std::string& name, float base, float min = 0.0f, float max = 1e9f);
		bool Has(const std::string& name) const { return Find(name) != nullptr; }
		float Base(const std::string& name) const;
		float Current(const std::string& name) const;   // 0 for an unknown attribute
		float Min(const std::string& name) const;
		float Max(const std::string& name) const;

		void SetBase(const std::string& name, float value);
		void AddBase(const std::string& name, float delta) { SetBase(name, Base(name) + delta); }
		// What an instant effect does: add to, scale or replace the base.
		void ApplyInstant(const AttributeModifier& mod);

		// Modifiers that last: `source` identifies the effect that owns them, so they can be taken away together.
		void AddModifier(int source, const AttributeModifier& mod);
		void RemoveModifiers(int source);
		// Can the negative Add modifiers of `mods` be paid for (no attribute would go under its minimum)?
		bool CanAfford(const std::vector<AttributeModifier>& mods) const;

		const std::vector<Attribute>& All() const { return m_Attributes; }

	private:
		struct Held { int Source; AttributeModifier Mod; };
		const Attribute* Find(const std::string& name) const;
		Attribute* Find(const std::string& name);
		void Recompute(Attribute& a) const;

		std::vector<Attribute> m_Attributes;   // in the order they were defined
		std::vector<Held> m_Modifiers;
	};
}
