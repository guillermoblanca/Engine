"""Writes highland_valley_showcase.json from highland_valley_look.json: E4's talus after E2, the settlement nodes after E1 and the layers on
the graded ground (GRAFO_TERRENO §15). Standard library only.
    python -I tools/make_showcase.py assets/data/terrain_graphs/highland_valley_look.json assets/data/terrain_graphs/highland_valley_showcase.json
"""
import json
import sys

src, dst = sys.argv[1], sys.argv[2]
g = json.load(open(src, encoding="utf-8"))
g["id"] = "highland_valley_showcase"
g["name"] = "Highland valley showcase: E2, talus, E1, settlements, material layers and trees (TERRENO_V5)"
nodes = {n["id"]: n for n in g["nodes"]}
edges = g["edges"]


def node(id_, type_, params=None, source=None, x=0.0, y=0.0):
    n = {"id": id_, "label": id_, "type": type_, "x": float(x), "y": float(y)}
    if params:
        n["params"] = {k: float(v) for k, v in params.items()}
    if source:
        n["source"] = source
    assert id_ not in nodes, id_
    g["nodes"].append(n)
    nodes[id_] = n


def edge(frm, from_port, to, port):
    edges.append({"from": frm, "fromPort": from_port, "port": port, "to": to})


def drop(pred):
    edges[:] = [e for e in edges if not pred(e)]


def into(to, port):
    return [e for e in edges if e["to"] == to and e["port"] == port]


# 1. E4: talus right after the incision (as highland_valley_e4.json); everything that read the incised height reads the trimmed one
node("e4_talus", "talus", {"HardAngle": 55, "HardCover": 0.08, "SoftAngle": 36}, x=1500, y=300)
for e in edges:
    if e["from"] == "e2_fluvial" and e["fromPort"] == "height":
        e["from"] = "e4_talus"
edge("e2_fluvial", "height", "e4_talus", "height")

# 2. the settlements on the final ground of E1 (as highland_valley_settle.json); path_grade's sites are route_network's (the reach filter)
node("settle_sites", "settlement_sites", x=5200, y=2600)
node("settle_routes", "route_network", x=5400, y=2600)
node("settle_grade", "path_grade", x=5600, y=2600)
for n in ("settle_sites", "settle_routes", "settle_grade"):
    edge("e1_lakes", "height", n, "height")
    edge("e1_lakes", "water", n, "water")
edge("forest_ok", "out", "settle_sites", "forest")
edge("settle_sites", "sites", "settle_routes", "sites")
for p in ("sites", "trails", "trail_info", "fords"):
    edge("settle_routes", p, "settle_grade", p)
node("settle_worked", "combine", {"Mode": 4}, x=5800, y=2600)
edge("settle_grade", "path", "settle_worked", "a")
edge("settle_grade", "platform", "settle_worked", "b")
node("settle_near", "blur", {"Passes": 1, "Radius": 1}, x=5950, y=2600)
edge("settle_worked", "out", "settle_near", "in")
node("settle_clear", "remap", {"InMax": 0.05, "InMin": 0, "OutMax": 0, "OutMin": 1}, x=6100, y=2600)
edge("settle_near", "out", "settle_clear", "in")
for out, source, port in (("settle_out_settlements", "settlements", "sites"), ("settle_out_trails", "trails", "trails"),
                          ("settle_out_trail_info", "trail_info", "trail_info"), ("settle_out_fords", "fords", "fords"),
                          ("settle_out_path", "path", "path"), ("settle_out_platform", "platform", "platform")):
    node(out, "output", source=source, x=6300, y=2600)
    edge("settle_grade", port, out, "in")
node("settle_out_cols", "output", source="cols", x=6300, y=2700)
edge("settle_routes", "cols", "settle_out_cols", "in")

# the graded ground is the world's, and what is painted, classified and scattered on it reads it (the moisture and the masks that only
# shape the forest's suitability keep the ungraded one, as in highland_valley_settle.json)
graded = {"out_height", "biomes", "faces", "snow", "stones", "trees", "look_biome", "look_slopes", "look_wet", "look_layers", "look_trees"}
for e in edges:
    if e["from"] == "e1_lakes" and e["fromPort"] == "height" and e["to"] in graded:
        e["from"], e["fromPort"] = "settle_grade", "height"

# trees and stones off the trails and platforms
node("settle_keep_trees", "combine", {"Mode": 2}, x=6550, y=700)
node("settle_keep_stones", "combine", {"Mode": 2}, x=6550, y=900)
[t] = into("trees", "density")
edge(t["from"], t["fromPort"], "settle_keep_trees", "a")
[s] = into("stones", "density")
edge(s["from"], s["fromPort"], "settle_keep_stones", "a")
drop(lambda e: e["to"] in ("trees", "stones") and e["port"] == "density")
edge("settle_clear", "out", "settle_keep_trees", "b")
edge("settle_clear", "out", "settle_keep_stones", "b")
edge("settle_keep_trees", "out", "trees", "density")
edge("settle_keep_stones", "out", "stones", "density")

# 3. the layers take E4's masks and the trails
# (E4's cliff is hard rock steeper than 45-55 degrees on the talus's ground; the valley, the lakes and the grading come after it, so it is
# kept only where the final ground is still a wall: min with a 38-44 degree slope mask)
node("look_steep", "mask_slope", {"MinDegrees": 44, "Falloff": 6}, x=6400, y=2400)
edge("settle_grade", "height", "look_steep", "height")
node("look_cliff", "combine", {"Mode": 3}, x=6550, y=2400)
edge("e4_talus", "cliff", "look_cliff", "a")
edge("look_steep", "mask", "look_cliff", "b")
edge("look_cliff", "out", "look_slopes", "cliff")
# (the talus's scree is metres of deposit: only the thick deposit is full scree, a thin one barely counts and stays rock or dry grass)
node("look_scree", "remap", {"InMin": 1, "InMax": 3, "OutMin": 0, "OutMax": 1}, x=6550, y=2480)
edge("e4_talus", "scree", "look_scree", "in")
edge("look_scree", "out", "look_slopes", "scree")
edge("e4_talus", "hardness", "look_slopes", "hardness")
node("out_hardness", "output", source="splat_hardness", x=6900, y=2760)
edge("look_slopes", "hardness", "out_hardness", "in")
# the platforms are trodden ground too, a little less than the trails: path = max(path, 0.7 platform)
node("look_platform", "remap", {"InMin": 0, "InMax": 1, "OutMin": 0, "OutMax": 0.7}, x=6400, y=2700)
edge("settle_grade", "platform", "look_platform", "in")
node("look_trodden", "combine", {"Mode": 4}, x=6550, y=2700)
edge("settle_grade", "path", "look_trodden", "a")
edge("look_platform", "out", "look_trodden", "b")
edge("look_trodden", "out", "look_layers", "path")
edge("look_trodden", "out", "look_trees", "path")
# no tree on the gravel of the channels nor on the mud of the shores
node("look_wet_bare", "combine", {"Mode": 4}, x=6700, y=2500)
edge("look_wet", "gravel", "look_wet_bare", "a")
edge("look_wet", "mud", "look_wet_bare", "b")
edge("look_wet_bare", "out", "look_trees", "exclude")

with open(dst, "w", encoding="utf-8", newline="\n") as f:
    json.dump(g, f, indent=2, sort_keys=True)
    f.write("\n")
print("wrote", dst, len(g["nodes"]), "nodes", len(edges), "edges")
