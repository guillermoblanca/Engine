#include "GB/Game/Components/Presentation/PropsComponent.h"
#include <glm/gtc/matrix_transform.hpp>

namespace GB
{
	namespace
	{
		// Where a box (or a look held "carry") sits: between the palms, its width along the line joining them. Fills the prop's offset and returns
		// the width. Needs the sockets carry, hand_l and hand_r.
		float CarryGrip(const SocketSet& sockets, const ModelInstance& instance, Prop& prop)
		{
			const glm::mat4 carry = sockets.Matrix(instance, "carry");
			const glm::vec3 l = glm::vec3(glm::inverse(carry) * sockets.Matrix(instance, "hand_l")[3]), r = glm::vec3(glm::inverse(carry) * sockets.Matrix(instance, "hand_r")[3]);
			glm::vec3 x = l - r;
			const float width = glm::length(x);
			x = width > 1e-4f ? x / width : glm::vec3(1, 0, 0);
			glm::vec3 z = glm::cross(x, glm::vec3(0, 1, 0));
			z = glm::length(z) > 1e-4f ? glm::normalize(z) : glm::vec3(0, 0, 1);
			const glm::vec3 y = glm::cross(z, x);
			// a shallow box, held by the near edge: the thighs swing forward under a carried box and a deep one goes through them
			// (tests/box_clearance_test.cpp measures it)
			prop.Offset = glm::mat4(glm::vec4(x, 0), glm::vec4(y, 0), glm::vec4(z, 0), glm::vec4((l + r) * 0.5f + z * 0.11f, 1));
			return width;
		}
	}

	bool PropsComponent::Equip(const std::string& name, bool on)
	{
		AnimInstance* anim = m_Animation.Anim();
		if (!anim) return false;
		if (on) return name == "wheelbarrow" ? anim->SetOverlay("wheelbarrow", 0.3f) : Attach(name);
		Detach(name);
		if (name == "box" || name == "wheelbarrow") anim->SetOverlay(anim->HasSet("free") ? "free" : AnimInstance::kDefaultSet, 0.3f);
		return true;
	}

	bool PropsComponent::Attach(const std::string& name)
	{
		AnimInstance* animPtr = m_Animation.Anim();
		if (!animPtr || !m_Geometry.Instance()) return false;
		AnimInstance& anim = *animPtr;
		const SocketSet& sockets = anim.Sockets();
		Prop prop;
		prop.Name = name;
		PropLook look;
		if (name == "box")
		{
			if (!sockets.Find("carry") || !sockets.Find("hand_l") || !sockets.Find("hand_r")) return false;
			// the grips are measured from the clips that hold a box, so those must be on screen: switch first, attach once the change is done
			if (anim.Overlay() != "box" || anim.Transitioning())
			{
				if (!anim.SetOverlay("box", 0.3f)) return false;
				m_Pending = name;
				return true;
			}
			prop.Socket = "carry";
			const float width = CarryGrip(sockets, *m_Geometry.Instance(), prop);   // the box sits between the palms
			look.Size = { width + 0.04f, 0.32f, 0.26f };
			look.Color = { 0.62f, 0.44f, 0.26f };
		}
		else if (name == "bag")
		{
			if (!sockets.Find("back")) return false;
			prop.Socket = "back";
			look.Size = { 0.34f, 0.46f, 0.2f };
			look.Color = { 0.35f, 0.22f, 0.12f };
		}
		else if (name == "can")
		{
			if (!sockets.Find("hand_r")) return false;
			prop.Socket = "hand_r";
			look.Size = { 0.2f, 0.26f, 0.24f };
			look.Color = { 0.2f, 0.4f, 0.65f };
		}
		else if (name == "shovel")
		{
			if (!sockets.Find("hand_r")) return false;
			prop.Socket = "hand_r";
			look.Size = { 0.05f, 0.05f, 1.0f };
			look.Color = { 0.5f, 0.38f, 0.22f };
		}
		else if (name == "seeds")
		{
			if (!sockets.Find("hand_r")) return false;
			prop.Socket = "hand_r";
			look.Size = { 0.18f, 0.08f, 0.12f };
			look.Color = { 0.75f, 0.65f, 0.3f };
		}
		else if (name == "tool")
		{
			if (!sockets.Find("hand_r")) return false;
			prop.Socket = "hand_r";
			look.Size = { 0.04f, 0.04f, 0.9f };
			look.Color = { 0.5f, 0.5f, 0.55f };
		}
		else
			return false;
		anim.SnapGrips(prop);
		if (name != "box") prop.Grips.clear();   // a bag and a tool are held where the clip puts the hand: no IK
		if (!anim.Attach(prop)) return false;
		m_Looks[name] = look;
		return true;
	}

	bool PropsComponent::AttachLook(const std::string& name, const std::string& socket, const std::string& lookName, const glm::mat4& offset)
	{
		AnimInstance* animPtr = m_Animation.Anim();
		if (!animPtr || !m_Geometry.Instance()) return false;
		AnimInstance& anim = *animPtr;
		const SocketSet& sockets = anim.Sockets();
		if (!sockets.Find(socket)) return false;
		Prop prop;
		prop.Name = name;
		prop.Socket = socket;
		prop.Offset = offset;
		PropLook look;
		look.Look = lookName;
		look.Socket = socket;
		if (socket == "carry")
		{
			// held in both arms like the box: between the palms where the clips on screen put them, and the hands kept there with IK
			if (!sockets.Find("hand_l") || !sockets.Find("hand_r")) return false;
			const float width = CarryGrip(sockets, *m_Geometry.Instance(), prop);
			prop.Offset = prop.Offset * offset;
			look.Size = { width, 0.3f, 0.26f };
			anim.SnapGrips(prop);
		}
		if (!anim.Attach(prop)) return false;
		m_Looks[name] = look;
		return true;
	}

	void PropsComponent::Detach(const std::string& name)
	{
		AnimInstance* anim = m_Animation.Anim();
		if (!anim) return;
		if (name.empty() || name == m_Pending) m_Pending.clear();
		for (const std::string& n : anim->PropNames())
			if (name.empty() || n == name) anim->Detach(n);
	}

	void PropsComponent::ResolvePending()
	{
		AnimInstance* anim = m_Animation.Anim();
		if (m_Pending.empty() || !anim || anim->Transitioning()) return;
		const std::string name = m_Pending;
		m_Pending.clear();
		Attach(name);
	}

	void PropsComponent::AddToScene(Scene& scene) const
	{
		const AnimInstance* anim = m_Animation.Anim();
		if (!anim || !m_Visible) return;   // (H16: far away the props are not drawn)
		for (const std::string& n : anim->PropNames())
		{
			glm::mat4 world;
			const auto look = m_Looks.find(n);
			if (look == m_Looks.end() || !anim->PropMatrix(n, world)) continue;
			if (!look->second.Look.empty() && m_Drawer && m_Drawer(look->second.Look, world, scene)) continue;
			if (!m_Mesh) m_Mesh = std::make_unique<Mesh>(Mesh::CreateCube(1.0f));
			RenderObject obj;
			obj.MeshPtr = m_Mesh.get();
			obj.Mat.Albedo = look->second.Color;
			obj.Mat.Roughness = 0.7f;
			obj.Transform = glm::scale(world, look->second.Size);
			scene.Objects.push_back(std::move(obj));
		}
	}
}
