#include "GB/Visual/VisualContent.h"

namespace GB
{
	ContentSection VisualContentSection(VisualLibrary& library)
	{
		return StagedSection("visuals", { "visuals" },
			[](const std::string& json, VisualLibrary& l) { return LoadVisuals(json, l); },
			nullptr, library);
	}

	void AddVisualContentSection(ContentPacks& packs, VisualLibrary& library) { packs.AddSection(VisualContentSection(library)); }
}
