#pragma once
#include <functional>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace GB::Editor
{
	enum class PropType { Float, Int, Bool, Vec3, Color, Enum };

	// One editable value of the scene, by a path like "post.ssao" or "sun.intensity". The same entry drives the
	// property panels and the text commands (so agents and people change exactly the same things).
	struct Property
	{
		std::string Path;
		std::string Group;            // the panel section it appears in
		std::string Tooltip;
		PropType Type = PropType::Float;
		void* Ptr = nullptr;          // float*, int*, bool*, glm::vec3* (Color too) or int* for enums
		float Min = 0.0f, Max = 1.0f;
		std::vector<std::string> Names;   // Enum
		std::function<void()> OnChange;   // called after a successful Set from a command or the UI
	};

	struct SetResult
	{
		bool Ok = true;
		std::string Message;
	};

	class PropertyRegistry
	{
	public:
		Property& Float(const std::string& path, float& value, float min, float max, const std::string& group, const std::string& tooltip = "");
		Property& Int(const std::string& path, int& value, int min, int max, const std::string& group, const std::string& tooltip = "");
		Property& Bool(const std::string& path, bool& value, const std::string& group, const std::string& tooltip = "");
		Property& Vec3(const std::string& path, glm::vec3& value, float min, float max, const std::string& group, const std::string& tooltip = "");
		Property& Color(const std::string& path, glm::vec3& value, const std::string& group, const std::string& tooltip = "");
		// `value` must be an enum whose underlying type is int.
		template<class E>
		Property& Enum(const std::string& path, E& value, std::vector<std::string> names, const std::string& group, const std::string& tooltip = "")
		{
			static_assert(sizeof(E) == sizeof(int), "enum properties need an int-sized enum");
			return AddEnum(path, reinterpret_cast<int*>(&value), std::move(names), group, tooltip);
		}

		void RemovePrefix(const std::string& prefix);
		void Clear() { m_Items.clear(); }
		const std::vector<Property>& All() const { return m_Items; }
		std::vector<Property>& All() { return m_Items; }
		const Property* Find(const std::string& path) const;
		std::vector<std::string> List(const std::string& prefix = "") const;

		std::string Get(const std::string& path) const;                 // "" when it does not exist
		SetResult Set(const std::string& path, const std::string& text);   // parses by type, clamps to the range
		// Used by the UI after it changed the value itself.
		static void Notify(const Property& p) { if (p.OnChange) p.OnChange(); }
		static std::string Format(const Property& p);

	private:
		Property& Add(Property p);
		Property& AddEnum(const std::string& path, int* value, std::vector<std::string> names, const std::string& group, const std::string& tooltip);
		std::vector<Property> m_Items;
	};
}
