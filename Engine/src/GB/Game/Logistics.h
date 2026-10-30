#pragma once
// Who goes where to fetch what (the first piece of the logistics apart from the Tribe): the claims on the nodes. A member that sets out to a node (or to
// the campfire) claims it so that two never walk to the same one; the claim goes when the job ends, the member leaves or is taken from the tribe.
// Header only and free of the Tribe, so the hauler role (F3 of docs/PLAN_JUGABILIDAD.md) can claim stockpiles and sites with the same rule.
// The demands for materials and the deposits (TribeEconomy.cpp) stay in the Tribe for now: they read its store, board and bags (see PLAN_JUGABILIDAD.md, section 6).
#include <map>
#include <string>

namespace GB
{
	class NodeClaims
	{
	public:
		// `worker` goes to `node` (a claim already there is replaced).
		void Claim(const std::string& node, const std::string& worker) { m_Held[node] = worker; }
		// The node is free again.
		void Release(const std::string& node) { m_Held.erase(node); }
		// Everything a member held (it left, died or changed jobs).
		void ReleaseBy(const std::string& worker)
		{
			for (auto it = m_Held.begin(); it != m_Held.end();) it = it->second == worker ? m_Held.erase(it) : std::next(it);
		}
		void Clear() { m_Held.clear(); }
		// Someone other than `worker` is going there.
		bool HeldByOther(const std::string& node, const std::string& worker) const
		{
			const auto it = m_Held.find(node);
			return it != m_Held.end() && it->second != worker;
		}
		const std::map<std::string, std::string>& All() const { return m_Held; }   // node -> worker that goes there (what the tribe saves)
		bool Empty() const { return m_Held.empty(); }

	private:
		std::map<std::string, std::string> m_Held;
	};
}
