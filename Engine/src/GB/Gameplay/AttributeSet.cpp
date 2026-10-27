#include "GB/Gameplay/AttributeSet.h"
#include <algorithm>

namespace GB
{
	const AttributeSet::Attribute* AttributeSet::Find(const std::string& name) const
	{
		for (const Attribute& a : m_Attributes)
			if (a.Name == name) return &a;
		return nullptr;
	}

	AttributeSet::Attribute* AttributeSet::Find(const std::string& name)
	{
		for (Attribute& a : m_Attributes)
			if (a.Name == name) return &a;
		return nullptr;
	}

	void AttributeSet::Recompute(Attribute& a) const
	{
		float add = 0.0f, mul = 1.0f;
		bool overridden = false;
		float over = 0.0f;
		for (const Held& h : m_Modifiers)
		{
			if (h.Mod.Attribute != a.Name) continue;
			if (h.Mod.Op == ModOp::Add) add += h.Mod.Magnitude;
			else if (h.Mod.Op == ModOp::Multiply) mul *= h.Mod.Magnitude;
			else { overridden = true; over = h.Mod.Magnitude; }
		}
		const float value = overridden ? over : (a.Base + add) * mul;
		a.Current = std::clamp(value, a.Min, a.Max);
	}

	void AttributeSet::Define(const std::string& name, float base, float min, float max)
	{
		Attribute* a = Find(name);
		if (!a)
		{
			m_Attributes.push_back({});
			a = &m_Attributes.back();
			a->Name = name;
		}
		a->Min = min;
		a->Max = std::max(min, max);
		a->Base = std::clamp(base, a->Min, a->Max);
		Recompute(*a);
	}

	float AttributeSet::Base(const std::string& name) const { const Attribute* a = Find(name); return a ? a->Base : 0.0f; }
	float AttributeSet::Current(const std::string& name) const { const Attribute* a = Find(name); return a ? a->Current : 0.0f; }
	float AttributeSet::Min(const std::string& name) const { const Attribute* a = Find(name); return a ? a->Min : 0.0f; }
	float AttributeSet::Max(const std::string& name) const { const Attribute* a = Find(name); return a ? a->Max : 0.0f; }

	void AttributeSet::SetBase(const std::string& name, float value)
	{
		Attribute* a = Find(name);
		if (!a) return;
		a->Base = std::clamp(value, a->Min, a->Max);
		Recompute(*a);
	}

	void AttributeSet::ApplyInstant(const AttributeModifier& mod)
	{
		Attribute* a = Find(mod.Attribute);
		if (!a) return;
		if (mod.Op == ModOp::Add) a->Base += mod.Magnitude;
		else if (mod.Op == ModOp::Multiply) a->Base *= mod.Magnitude;
		else a->Base = mod.Magnitude;
		a->Base = std::clamp(a->Base, a->Min, a->Max);
		Recompute(*a);
	}

	void AttributeSet::AddModifier(int source, const AttributeModifier& mod)
	{
		m_Modifiers.push_back({ source, mod });
		if (Attribute* a = Find(mod.Attribute)) Recompute(*a);
	}

	void AttributeSet::RemoveModifiers(int source)
	{
		m_Modifiers.erase(std::remove_if(m_Modifiers.begin(), m_Modifiers.end(), [source](const Held& h) { return h.Source == source; }), m_Modifiers.end());
		for (Attribute& a : m_Attributes) Recompute(a);
	}

	bool AttributeSet::CanAfford(const std::vector<AttributeModifier>& mods) const
	{
		for (const AttributeModifier& m : mods)
		{
			if (m.Op != ModOp::Add || m.Magnitude >= 0.0f) continue;
			const Attribute* a = Find(m.Attribute);
			if (!a || a->Base + m.Magnitude < a->Min - 1e-6f) return false;
		}
		return true;
	}
}
