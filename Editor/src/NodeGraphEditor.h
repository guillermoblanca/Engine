#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <imgui.h>

namespace GB::Editor
{
	// Shared interaction state and geometry helpers for node graph editors. The derived editor owns
	// its graph model, node rendering and domain-specific commands.
	class NodeGraphEditor
	{
	public:
		virtual ~NodeGraphEditor() = default;

		void ResetCanvasInteraction()
		{
			SelNode.clear();
			SelEdge = -1;
			DragNode.clear();
			ConnectFrom.clear();
			Panning = false;
		}

	public:
		ImVec2 CanvasToScreen(const ImVec2& origin, float x, float y) const
		{
			return ImVec2(origin.x + Pan.x + x * Zoom, origin.y + Pan.y + y * Zoom);
		}

		ImVec2 ScreenToCanvas(const ImVec2& origin, const ImVec2& screen) const
		{
			return ImVec2((screen.x - origin.x - Pan.x) / Zoom, (screen.y - origin.y - Pan.y) / Zoom);
		}

		void ZoomAt(const ImVec2& origin, const ImVec2& screen, float factor)
		{
			const float oldZoom = Zoom;
			Zoom = std::clamp(Zoom * factor, 0.35f, 2.5f);
			const ImVec2 relative((screen.x - origin.x - Pan.x), (screen.y - origin.y - Pan.y));
			Pan.x -= relative.x * (Zoom / oldZoom - 1.0f);
			Pan.y -= relative.y * (Zoom / oldZoom - 1.0f);
		}

		static float DistanceToBezier(const ImVec2& a, const ImVec2& b, const ImVec2& p)
		{
			const float dx = std::max(40.0f, std::fabs(b.x - a.x) * 0.5f);
			const ImVec2 c1(a.x + dx, a.y), c2(b.x - dx, b.y);
			float best = 1e9f;
			ImVec2 previous = a;
			for (int i = 1; i <= 24; ++i)
			{
				const float t = static_cast<float>(i) / 24.0f, u = 1.0f - t;
				const ImVec2 q(
					u * u * u * a.x + 3.0f * u * u * t * c1.x + 3.0f * u * t * t * c2.x + t * t * t * b.x,
					u * u * u * a.y + 3.0f * u * u * t * c1.y + 3.0f * u * t * t * c2.y + t * t * t * b.y);
				const ImVec2 delta(q.x - previous.x, q.y - previous.y);
				const float lengthSquared = delta.x * delta.x + delta.y * delta.y;
				const float projection = lengthSquared > 0.0f
					? std::clamp(((p.x - previous.x) * delta.x + (p.y - previous.y) * delta.y) / lengthSquared, 0.0f, 1.0f)
					: 0.0f;
				const float ex = previous.x + delta.x * projection - p.x;
				const float ey = previous.y + delta.y * projection - p.y;
				best = std::min(best, std::sqrt(ex * ex + ey * ey));
				previous = q;
			}
			return best;
		}

	public:
		std::string SelNode;
		int SelEdge = -1;
		ImVec2 Pan{ 40.0f, 40.0f };
		float Zoom = 1.0f;
		std::string DragNode;
		std::string ConnectFrom;
		bool Panning = false;
		ImVec2 PopupAt{ 0.0f, 0.0f };
	};
}
