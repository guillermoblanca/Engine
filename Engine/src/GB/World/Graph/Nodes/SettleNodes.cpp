#include "GB/World/Graph/Nodes/SettleNodes.h"
#include "GB/World/Graph/TerrainGraph.h"
#include "GB/World/Settle.h"

namespace GB
{
	namespace
	{
		constexpr uint32_t kSettle = 0x9a7a3c;

		std::string Who(const TgNode& node) { return node.Type + " node '" + node.Id + "'"; }

		const TgValue* Input(const std::vector<const TgValue*>& inputs, size_t slot, TgPortKind kind)
		{
			return inputs.size() > slot && inputs[slot] && inputs[slot]->Kind == kind ? inputs[slot] : nullptr;
		}

		bool CheckRevision(const TgNode& node, std::string& error)
		{
			const double revision = TgParam(node, "Revision", 1.0);
			if (revision == 1.0) return true;
			error = Who(node) + ": Revision " + std::to_string(revision) + " is unknown (this engine has 1)";
			return false;
		}

		TgField& FieldOut(std::map<std::string, TgValue>& outputs, const char* name, const TgField& like, std::vector<float> values)
		{
			TgValue& v = outputs[name];
			v.Kind = TgPortKind::Field;
			v.Field.N = like.N; v.Field.Size = like.Size; v.Field.Center = like.Center;
			v.Field.Values = std::move(values);
			return v.Field;
		}

		void PathsOut(std::map<std::string, TgValue>& outputs, const char* name, TgPaths paths)
		{
			TgValue& v = outputs[name];
			v.Kind = TgPortKind::Paths;
			v.Paths = std::move(paths);
		}

		float F(const TgNode& node, const char* key, float fallback) { return static_cast<float>(TgParam(node, key, fallback)); }
		int I(const TgNode& node, const char* key, int fallback) { return static_cast<int>(TgParam(node, key, fallback)); }

		const TgValue* Height(const TgNode& node, const std::vector<const TgValue*>& inputs, std::string& error)
		{
			const TgValue* h = Input(inputs, 0, TgPortKind::Field);
			if (!h || h->Field.N < 3) { error = Who(node) + ": input 'height' is not connected"; return nullptr; }
			return h;
		}
	}

	// ------------------------------------------------------------------ settlement_sites
	TgNodeSchema SettlementSitesNode::Describe() const
	{
		const SiteParams d;
		TgNodeSchema s;
		s.Category = "Settlement";
		s.Summary = "Where to settle: sites scored by water, slope, flat ground, flood height, border, forest and stone; the best, far apart.";
		s.Color = kSettle;
		s.Inputs = { { "height", TgPortKind::Field }, { "water", TgPortKind::Water, true, "lakes and river (the water near, the flood height)" },
			{ "forest", TgPortKind::Field, true, "0..1, forest from 0.5 (resources near)" } };
		s.Outputs = { { "sites", TgPortKind::Paths }, { "score", TgPortKind::Field }, { "water_distance", TgPortKind::Field } };
		s.Params = {
			{ "Count", static_cast<double>(d.Count), 1, 16, true, "the most sites" },
			{ "Separation", d.Separation, 0, 1500, false, "metres between two sites (R2)" },
			{ "Radius", d.Radius, 5, 80, false, "metres: the village and its platform" },
			{ "CampRadius", d.CampRadius, 1, 20, false, "metres: the camp's ground (S2)" },
			{ "CampMaxSlope", d.CampMaxSlope, 1, 45, false, "degrees, nowhere on the camp's ground" },
			{ "CampMeanSlope", d.CampMeanSlope, 1, 45, false, "degrees, the mean on the camp's ground" },
			{ "WaterMin", d.WaterMin, 0, 200, false, "metres: nearer is the bank (S1)" },
			{ "WaterBest", d.WaterBest, 0, 300, false, "metres: the best distance to the water" },
			{ "WaterMax", d.WaterMax, 0, 1000, false, "metres: farther is refused (S1, X1)" },
			{ "FloodHeight", d.FloodHeight, 0, 10, false, "metres above the nearest water (X4)" },
			{ "Edge", d.Edge, 0, 500, false, "metres from the border (S7)" },
			{ "MaxRough", d.MaxRough, 0, 20, false, "metres the ground may depart from the centre's RoughRadius away (the game's picker)" },
			{ "RoughRadius", d.RoughRadius, 1, 60, false, "metres" },
			{ "SteepShareMax", d.SteepShareMax, 0, 1, false, "of the disc over 14 degrees (X3)" },
			{ "WallDistance", d.WallDistance, 0, 60, false, "metres: no ground over 40 degrees this close (X3)" },
			{ "FlatShareMin", d.FlatShareMin, 0, 1, false, "of the disc at 8 degrees or less" },
			{ "MaxRelativeHeight", d.MaxRelativeHeight, 0, 1, false, "of the height range: above is snow" },
			{ "Step", d.Step, 1, 50, false, "metres between candidates" },
			{ "ForestRadius", d.ForestRadius, 0, 600, false, "metres" },
			{ "StoneRadius", d.StoneRadius, 0, 1000, false, "metres" },
			{ "StoneSlope", d.StoneSlope, 10, 80, false, "degrees: bare rock (stone)" },
			{ "WWater", d.WWater, 0, 4, false, "weight" }, { "WFlat", d.WFlat, 0, 4, false, "weight" }, { "WArea", d.WArea, 0, 4, false, "weight" },
			{ "WDry", d.WDry, 0, 4, false, "weight" }, { "WLow", d.WLow, 0, 4, false, "weight" }, { "WForest", d.WForest, 0, 4, false, "weight" },
			{ "WStone", d.WStone, 0, 4, false, "weight" },
			{ "Revision", 1, 1, 1, true, "behaviour version (1)" },
		};
		return s;
	}

	bool SettlementSitesNode::Run(const TgNode& node, const TgContext&, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		if (!CheckRevision(node, error)) return false;
		const TgValue* height = Height(node, inputs, error);
		if (!height) return false;
		const TgValue* water = Input(inputs, 1, TgPortKind::Water);
		const TgValue* forest = Input(inputs, 2, TgPortKind::Field);
		if (forest && forest->Field.N != height->Field.N) { error = Who(node) + ": 'forest' is not the size of the height field"; return false; }
		const SiteParams d;
		SiteParams p;
		p.Count = I(node, "Count", d.Count); p.Separation = F(node, "Separation", d.Separation); p.Radius = F(node, "Radius", d.Radius);
		p.CampRadius = F(node, "CampRadius", d.CampRadius); p.CampMaxSlope = F(node, "CampMaxSlope", d.CampMaxSlope); p.CampMeanSlope = F(node, "CampMeanSlope", d.CampMeanSlope);
		p.WaterMin = F(node, "WaterMin", d.WaterMin); p.WaterBest = F(node, "WaterBest", d.WaterBest); p.WaterMax = F(node, "WaterMax", d.WaterMax);
		p.FloodHeight = F(node, "FloodHeight", d.FloodHeight); p.Edge = F(node, "Edge", d.Edge); p.MaxRough = F(node, "MaxRough", d.MaxRough);
		p.RoughRadius = F(node, "RoughRadius", d.RoughRadius); p.SteepShareMax = F(node, "SteepShareMax", d.SteepShareMax);
		p.WallDistance = F(node, "WallDistance", d.WallDistance); p.FlatShareMin = F(node, "FlatShareMin", d.FlatShareMin);
		p.MaxRelativeHeight = F(node, "MaxRelativeHeight", d.MaxRelativeHeight); p.Step = F(node, "Step", d.Step);
		p.ForestRadius = F(node, "ForestRadius", d.ForestRadius); p.StoneRadius = F(node, "StoneRadius", d.StoneRadius); p.StoneSlope = F(node, "StoneSlope", d.StoneSlope);
		p.WWater = F(node, "WWater", d.WWater); p.WFlat = F(node, "WFlat", d.WFlat); p.WArea = F(node, "WArea", d.WArea); p.WDry = F(node, "WDry", d.WDry);
		p.WLow = F(node, "WLow", d.WLow); p.WForest = F(node, "WForest", d.WForest); p.WStone = F(node, "WStone", d.WStone);
		SiteResult r = FindSettlementSites(height->Field, water ? &water->Water : nullptr, forest ? &forest->Field : nullptr, p);
		PathsOut(outputs, "sites", EncodeSites(r.Sites));
		FieldOut(outputs, "score", height->Field, std::move(r.Score));
		FieldOut(outputs, "water_distance", height->Field, std::move(r.WaterDistance));
		return true;
	}

	// ------------------------------------------------------------------ route_network
	TgNodeSchema RouteNetworkNode::Describe() const
	{
		const RouteParams d;
		TgNodeSchema s;
		s.Category = "Settlement";
		s.Summary = "Least-cost trails between the sites (spanning tree plus extra links) and to the water, the cols and the fords.";
		s.Color = kSettle;
		s.Inputs = { { "height", TgPortKind::Field }, { "water", TgPortKind::Water, true, "lakes (forbidden) and river (only at a ford)" },
			{ "sites", TgPortKind::Paths, false, "settlement_sites: sites" } };
		s.Outputs = { { "trails", TgPortKind::Paths }, { "trail_info", TgPortKind::Paths }, { "path", TgPortKind::Field },
			{ "fords", TgPortKind::Paths }, { "cols", TgPortKind::Paths } };
		s.Params = {
			{ "Cell", d.Cell, 1, 30, false, "metres between the samples of the routing grid" },
			{ "MaxGrade", d.MaxGrade, 0.02, 0.5, false, "rise per run of a trail" },
			{ "GradeWeight", d.GradeWeight, 0, 20, false, "cost of climbing at MaxGrade" },
			{ "SteepWeight", d.SteepWeight, 0, 200, false, "cost per unit of grade over MaxGrade" },
			{ "CliffGrade", d.CliffGrade, 0.1, 2, false, "never steeper along a step" },
			{ "SideFree", d.SideFree, 0, 1, false, "cross slope (tan) that costs nothing" },
			{ "SideWeight", d.SideWeight, 0, 50, false, "cost per unit of cross slope over SideFree" },
			{ "SideMax", d.SideMax, 0, 1, false, "cross slope (tan) a talus cannot meet" },
			{ "SideMaxWeight", d.SideMaxWeight, 0, 500, false, "cost per unit of cross slope over SideMax" },
			{ "FordDepth", d.FordDepth, 0, 3, false, "metres: a reach this shallow is a natural ford" },
			{ "FordSpacing", d.FordSpacing, 10, 2000, false, "metres between fords" },
			{ "MaxFords", static_cast<double>(d.MaxFords), 0, 32, true, "" },
			{ "FordCost", d.FordCost, 1, 50, false, "x the distance in the water" },
			{ "ShapeFords", static_cast<double>(d.ShapeFords), 0, 1, true, "1: the shallowest sections are fords even when deeper (path_grade raises the bed)" },
			{ "ExtraRatio", d.ExtraRatio, 1, 5, false, "an extra link when the network's way is this many times its cost" },
			{ "ExtraLinks", static_cast<double>(d.ExtraLinks), 0, 16, true, "" },
			{ "WaterSpurMax", d.WaterSpurMax, 0, 2000, false, "metres: a trail to the nearest water up to this long" },
			{ "ColBasinShare", d.ColBasinShare, 0, 0.5, false, "basins smaller than this share have no col" },
			{ "MaxCols", static_cast<double>(d.MaxCols), 0, 16, true, "" },
			{ "ColSpurMax", d.ColSpurMax, 0, 5000, false, "metres" },
			{ "FordSpurMax", d.FordSpurMax, 0, 5000, false, "metres" },
			{ "Spacing", d.Spacing, 0.5, 20, false, "metres between trail points" },
			{ "Width", d.Width, 0.5, 20, false, "metres of tread" },
			{ "Fade", d.Fade, 0, 10, false, "metres the mask fades over" },
			{ "Revision", 1, 1, 1, true, "behaviour version (1)" },
		};
		return s;
	}

	bool RouteNetworkNode::Run(const TgNode& node, const TgContext&, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		if (!CheckRevision(node, error)) return false;
		const TgValue* height = Height(node, inputs, error);
		if (!height) return false;
		const TgValue* water = Input(inputs, 1, TgPortKind::Water);
		const TgValue* sitesIn = Input(inputs, 2, TgPortKind::Paths);
		std::vector<SettlementSite> sites;
		if (!sitesIn || !DecodeSites(sitesIn->Paths, sites)) { error = Who(node) + ": input 'sites' is not a settlement_sites' output"; return false; }
		const RouteParams d;
		RouteParams p;
		p.Cell = F(node, "Cell", d.Cell); p.MaxGrade = F(node, "MaxGrade", d.MaxGrade); p.GradeWeight = F(node, "GradeWeight", d.GradeWeight);
		p.SteepWeight = F(node, "SteepWeight", d.SteepWeight); p.CliffGrade = F(node, "CliffGrade", d.CliffGrade); p.SideFree = F(node, "SideFree", d.SideFree);
		p.SideWeight = F(node, "SideWeight", d.SideWeight); p.SideMax = F(node, "SideMax", d.SideMax); p.SideMaxWeight = F(node, "SideMaxWeight", d.SideMaxWeight);
		p.FordDepth = F(node, "FordDepth", d.FordDepth); p.FordSpacing = F(node, "FordSpacing", d.FordSpacing); p.MaxFords = I(node, "MaxFords", d.MaxFords);
		p.FordCost = F(node, "FordCost", d.FordCost); p.ShapeFords = I(node, "ShapeFords", d.ShapeFords); p.ExtraRatio = F(node, "ExtraRatio", d.ExtraRatio);
		p.ExtraLinks = I(node, "ExtraLinks", d.ExtraLinks); p.WaterSpurMax = F(node, "WaterSpurMax", d.WaterSpurMax); p.ColBasinShare = F(node, "ColBasinShare", d.ColBasinShare);
		p.MaxCols = I(node, "MaxCols", d.MaxCols); p.ColSpurMax = F(node, "ColSpurMax", d.ColSpurMax); p.FordSpurMax = F(node, "FordSpurMax", d.FordSpurMax);
		p.Spacing = F(node, "Spacing", d.Spacing); p.Width = F(node, "Width", d.Width); p.Fade = F(node, "Fade", d.Fade);
		RouteResult r = PlanRoutes(height->Field, water ? &water->Water : nullptr, sites, p);
		TgPaths geometry, info;
		EncodeTrails(r.Trails, geometry, info);
		PathsOut(outputs, "trails", std::move(geometry));
		PathsOut(outputs, "trail_info", std::move(info));
		FieldOut(outputs, "path", height->Field, std::move(r.Path));
		PathsOut(outputs, "fords", EncodeFords(r.Fords));
		PathsOut(outputs, "cols", EncodeCols(r.Cols));
		return true;
	}

	// ------------------------------------------------------------------ path_grade
	TgNodeSchema PathGradeNode::Describe() const
	{
		const GradeParams d;
		TgNodeSchema s;
		s.Category = "Settlement";
		s.Summary = "Levels the trails (grade held, light cut and fill, talus) and a platform per site; culverts, no pit after it.";
		s.Color = kSettle;
		s.Inputs = { { "height", TgPortKind::Field }, { "water", TgPortKind::Water, true, "not graded; its cells are the outlets of the final fill" },
			{ "sites", TgPortKind::Paths, false, "settlement_sites: sites" }, { "trails", TgPortKind::Paths, false, "route_network: trails" },
			{ "trail_info", TgPortKind::Paths, false, "route_network: trail_info" }, { "fords", TgPortKind::Paths, true, "route_network: fords" } };
		s.Outputs = { { "height", TgPortKind::Field }, { "path", TgPortKind::Field }, { "platform", TgPortKind::Field }, { "earthwork", TgPortKind::Field }, { "drain_fill", TgPortKind::Field },
			{ "sites", TgPortKind::Paths }, { "trails", TgPortKind::Paths }, { "trail_info", TgPortKind::Paths }, { "fords", TgPortKind::Paths } };
		s.Params = {
			{ "MaxGrade", d.MaxGrade, 0.02, 0.5, false, "rise per run of a trail" },
			{ "Width", d.Width, 0.5, 20, false, "metres of tread" },
			{ "Talus", d.Talus, 0.05, 1.5, false, "tan of the side slopes" },
			{ "SmoothLength", d.SmoothLength, 0, 200, false, "metres the profile is averaged over" },
			{ "MaxCut", d.MaxCut, 0, 10, false, "metres" },
			{ "MaxFill", d.MaxFill, 0, 10, false, "metres" },
			{ "CulvertArea", d.CulvertArea, 0, 1e6, false, "m2 drained: no fill over a channel this big" },
			{ "NoCutNearWater", d.NoCutNearWater, 0, 50, false, "metres" },
			{ "FordDepth", d.FordDepth, 0, 2, false, "metres of water over a built ford" },
			{ "Fade", d.Fade, 0, 10, false, "metres the masks fade over" },
			{ "Platforms", static_cast<double>(d.Platforms), 0, 1, true, "1: a level platform per site" },
			{ "Revision", 1, 1, 1, true, "behaviour version (1)" },
		};
		return s;
	}

	bool PathGradeNode::Run(const TgNode& node, const TgContext&, const std::vector<const TgValue*>& inputs, std::map<std::string, TgValue>& outputs, std::string& error) const
	{
		if (!CheckRevision(node, error)) return false;
		const TgValue* height = Height(node, inputs, error);
		if (!height) return false;
		const TgValue* water = Input(inputs, 1, TgPortKind::Water);
		const TgValue* sitesIn = Input(inputs, 2, TgPortKind::Paths);
		const TgValue* trailsIn = Input(inputs, 3, TgPortKind::Paths);
		const TgValue* infoIn = Input(inputs, 4, TgPortKind::Paths);
		const TgValue* fordsIn = Input(inputs, 5, TgPortKind::Paths);
		std::vector<SettlementSite> sites;
		std::vector<Trail> trails;
		std::vector<Ford> fords;
		if (!sitesIn || !DecodeSites(sitesIn->Paths, sites)) { error = Who(node) + ": input 'sites' is not a settlement_sites' output"; return false; }
		if (!trailsIn || !infoIn || !DecodeTrails(trailsIn->Paths, infoIn->Paths, trails)) { error = Who(node) + ": inputs 'trails' and 'trail_info' are not a route_network's"; return false; }
		if (fordsIn && !DecodeFords(fordsIn->Paths, fords)) { error = Who(node) + ": input 'fords' is not a route_network's"; return false; }
		const GradeParams d;
		GradeParams p;
		p.MaxGrade = F(node, "MaxGrade", d.MaxGrade); p.Width = F(node, "Width", d.Width); p.Talus = F(node, "Talus", d.Talus);
		p.SmoothLength = F(node, "SmoothLength", d.SmoothLength); p.MaxCut = F(node, "MaxCut", d.MaxCut); p.MaxFill = F(node, "MaxFill", d.MaxFill);
		p.CulvertArea = F(node, "CulvertArea", d.CulvertArea); p.NoCutNearWater = F(node, "NoCutNearWater", d.NoCutNearWater);
		p.FordDepth = F(node, "FordDepth", d.FordDepth); p.Fade = F(node, "Fade", d.Fade); p.Platforms = I(node, "Platforms", d.Platforms);
		GradeResult r = GradePaths(height->Field, water ? &water->Water : nullptr, sites, trails, fords, p);
		std::vector<float> earth(r.Height.size());
		for (size_t c = 0; c < earth.size(); ++c) earth[c] = r.Height[c] - height->Field.Values[c];
		FieldOut(outputs, "height", height->Field, std::move(r.Height));
		FieldOut(outputs, "path", height->Field, std::move(r.Path));
		FieldOut(outputs, "platform", height->Field, std::move(r.Platform));
		FieldOut(outputs, "earthwork", height->Field, std::move(earth));
		FieldOut(outputs, "drain_fill", height->Field, std::move(r.DrainFill));
		PathsOut(outputs, "sites", EncodeSites(r.Sites));
		TgPaths geometry, info;
		EncodeTrails(r.Trails, geometry, info);
		PathsOut(outputs, "trails", std::move(geometry));
		PathsOut(outputs, "trail_info", std::move(info));
		PathsOut(outputs, "fords", EncodeFords(r.Fords));
		return true;
	}
}
