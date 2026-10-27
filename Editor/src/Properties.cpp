#include "Properties.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace GB::Editor
{
	namespace
	{
		std::string Lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}
		bool ParseFloat(const std::string& text, float& out)
		{
			char* end = nullptr;
			out = std::strtof(text.c_str(), &end);
			return end != text.c_str() && *end == '\0';
		}
		std::string Num(float v)
		{
			char buf[32];
			std::snprintf(buf, sizeof buf, "%g", static_cast<double>(v));
			return buf;
		}
	}

	Property& PropertyRegistry::Add(Property p)
	{
		for (Property& e : m_Items)
			if (e.Path == p.Path) { e = std::move(p); return e; }
		m_Items.push_back(std::move(p));
		return m_Items.back();
	}

	Property& PropertyRegistry::Float(const std::string& path, float& value, float min, float max, const std::string& group, const std::string& tooltip)
	{
		Property p; p.Path = path; p.Group = group; p.Tooltip = tooltip; p.Type = PropType::Float; p.Ptr = &value; p.Min = min; p.Max = max;
		return Add(std::move(p));
	}
	Property& PropertyRegistry::Int(const std::string& path, int& value, int min, int max, const std::string& group, const std::string& tooltip)
	{
		Property p; p.Path = path; p.Group = group; p.Tooltip = tooltip; p.Type = PropType::Int; p.Ptr = &value; p.Min = static_cast<float>(min); p.Max = static_cast<float>(max);
		return Add(std::move(p));
	}
	Property& PropertyRegistry::Bool(const std::string& path, bool& value, const std::string& group, const std::string& tooltip)
	{
		Property p; p.Path = path; p.Group = group; p.Tooltip = tooltip; p.Type = PropType::Bool; p.Ptr = &value;
		return Add(std::move(p));
	}
	Property& PropertyRegistry::Vec3(const std::string& path, glm::vec3& value, float min, float max, const std::string& group, const std::string& tooltip)
	{
		Property p; p.Path = path; p.Group = group; p.Tooltip = tooltip; p.Type = PropType::Vec3; p.Ptr = &value; p.Min = min; p.Max = max;
		return Add(std::move(p));
	}
	Property& PropertyRegistry::Color(const std::string& path, glm::vec3& value, const std::string& group, const std::string& tooltip)
	{
		Property p; p.Path = path; p.Group = group; p.Tooltip = tooltip; p.Type = PropType::Color; p.Ptr = &value; p.Min = 0.0f; p.Max = 1.0f;
		return Add(std::move(p));
	}
	Property& PropertyRegistry::AddEnum(const std::string& path, int* value, std::vector<std::string> names, const std::string& group, const std::string& tooltip)
	{
		Property p; p.Path = path; p.Group = group; p.Tooltip = tooltip; p.Type = PropType::Enum; p.Ptr = value; p.Names = std::move(names);
		return Add(std::move(p));
	}

	void PropertyRegistry::RemovePrefix(const std::string& prefix)
	{
		m_Items.erase(std::remove_if(m_Items.begin(), m_Items.end(), [&](const Property& p) { return p.Path.compare(0, prefix.size(), prefix) == 0; }), m_Items.end());
	}

	const Property* PropertyRegistry::Find(const std::string& path) const
	{
		for (const Property& p : m_Items)
			if (p.Path == path) return &p;
		return nullptr;
	}

	std::vector<std::string> PropertyRegistry::List(const std::string& prefix) const
	{
		std::vector<std::string> out;
		for (const Property& p : m_Items)
			if (p.Path.compare(0, prefix.size(), prefix) == 0) out.push_back(p.Path);
		std::sort(out.begin(), out.end());
		return out;
	}

	std::string PropertyRegistry::Format(const Property& p)
	{
		switch (p.Type)
		{
		case PropType::Float: return Num(*static_cast<float*>(p.Ptr));
		case PropType::Int: return std::to_string(*static_cast<int*>(p.Ptr));
		case PropType::Bool: return *static_cast<bool*>(p.Ptr) ? "true" : "false";
		case PropType::Vec3:
		case PropType::Color:
		{
			const glm::vec3& v = *static_cast<glm::vec3*>(p.Ptr);
			return Num(v.x) + " " + Num(v.y) + " " + Num(v.z);
		}
		case PropType::Enum:
		{
			const int i = *static_cast<int*>(p.Ptr);
			return i >= 0 && static_cast<size_t>(i) < p.Names.size() ? p.Names[static_cast<size_t>(i)] : std::to_string(i);
		}
		}
		return "";
	}

	std::string PropertyRegistry::Get(const std::string& path) const
	{
		const Property* p = Find(path);
		return p ? Format(*p) : "";
	}

	SetResult PropertyRegistry::Set(const std::string& path, const std::string& text)
	{
		Property* p = nullptr;
		for (Property& e : m_Items) if (e.Path == path) p = &e;
		if (!p) return { false, "unknown property '" + path + "' (try: list)" };
		std::istringstream in(text);
		std::vector<std::string> words;
		for (std::string w; in >> w;) words.push_back(w);
		if (words.empty()) return { false, "no value given for " + path };
		switch (p->Type)
		{
		case PropType::Float:
		{
			float v;
			if (!ParseFloat(words[0], v)) return { false, "'" + words[0] + "' is not a number" };
			*static_cast<float*>(p->Ptr) = std::clamp(v, p->Min, p->Max);
			break;
		}
		case PropType::Int:
		{
			float v;
			if (!ParseFloat(words[0], v)) return { false, "'" + words[0] + "' is not a number" };
			*static_cast<int*>(p->Ptr) = std::clamp(static_cast<int>(v), static_cast<int>(p->Min), static_cast<int>(p->Max));
			break;
		}
		case PropType::Bool:
		{
			const std::string w = Lower(words[0]);
			if (w == "1" || w == "true" || w == "on" || w == "yes") *static_cast<bool*>(p->Ptr) = true;
			else if (w == "0" || w == "false" || w == "off" || w == "no") *static_cast<bool*>(p->Ptr) = false;
			else return { false, "'" + words[0] + "' is not true/false" };
			break;
		}
		case PropType::Vec3:
		case PropType::Color:
		{
			glm::vec3 v;
			if (p->Type == PropType::Color && words.size() == 1 && words[0].size() == 7 && words[0][0] == '#')   // #rrggbb
			{
				const unsigned long rgb = std::strtoul(words[0].c_str() + 1, nullptr, 16);
				v = { static_cast<float>((rgb >> 16) & 255) / 255.0f, static_cast<float>((rgb >> 8) & 255) / 255.0f, static_cast<float>(rgb & 255) / 255.0f };
			}
			else
			{
				if (words.size() < 3) return { false, path + " needs three numbers" };
				for (int i = 0; i < 3; ++i)
					if (!ParseFloat(words[static_cast<size_t>(i)], v[i])) return { false, "'" + words[static_cast<size_t>(i)] + "' is not a number" };
			}
			*static_cast<glm::vec3*>(p->Ptr) = glm::clamp(v, glm::vec3(p->Min), glm::vec3(p->Max));
			break;
		}
		case PropType::Enum:
		{
			int index = -1;
			for (size_t i = 0; i < p->Names.size(); ++i) if (Lower(p->Names[i]) == Lower(words[0])) index = static_cast<int>(i);
			float f;
			if (index < 0 && ParseFloat(words[0], f) && f >= 0.0f && static_cast<size_t>(f) < p->Names.size()) index = static_cast<int>(f);
			if (index < 0)
			{
				std::string all;
				for (const std::string& n : p->Names) all += (all.empty() ? "" : ", ") + n;
				return { false, "'" + words[0] + "' is not one of: " + all };
			}
			*static_cast<int*>(p->Ptr) = index;
			break;
		}
		}
		Notify(*p);
		return { true, path + " = " + Format(*p) };
	}
}
