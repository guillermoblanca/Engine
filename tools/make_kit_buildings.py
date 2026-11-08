"""Writes the visuals of the buildings made of a modular kit (the Medieval Village MegaKit in assets/models/medieval_kit) into
assets/data/visuals.json, from the short descriptions of tools/kit_buildings.json.

A building is a grid of 2 m cells (x across the gable, z along the ridge), one or two floors of 3.12 m, a style (the walls, corners and floor
of plaster or of brick) and a door in the middle of its +z front. Its phases are the ground floor's planks, the ground floor's walls (door,
windows every other cell, corners), each upper floor (its planks and walls) and the roof (the tiled roof of its size and a chimney): the order
the clan builds it in, and what the drawing shows as the work goes on.

    python tools/make_kit_buildings.py

Each building's line in the "buildings" of visuals.json is replaced (or added at the top); nothing else in the file changes.
"""
import json
import os
import re

KIT = "models/medieval_kit/"
CELL = 2.0
STOREY = 3.12
STYLES = {
    "plaster": {"wall": "Wall_Plaster_Straight", "door": "Wall_Plaster_Door_Flat", "window": "Wall_Plaster_Window_Wide_Flat",
                "corner": "Corner_Exterior_Wood", "floor": "Floor_WoodDark", "upper": "Floor_WoodDark"},
    "brick": {"wall": "Wall_UnevenBrick_Straight", "door": "Wall_UnevenBrick_Door_Round", "window": "Wall_UnevenBrick_Window_Wide_Round",
              "corner": "Corner_Exterior_Brick", "floor": "Floor_UnevenBrick", "upper": "Floor_WoodDark"},
}
ROOFS = {(2, 2): "Roof_RoundTiles_4x4", (2, 3): "Roof_RoundTiles_4x6", (2, 4): "Roof_RoundTiles_4x8", (3, 3): "Roof_RoundTiles_6x6",
         (3, 4): "Roof_RoundTiles_6x8", (3, 5): "Roof_RoundTiles_6x10", (3, 6): "Roof_RoundTiles_6x12", (3, 7): "Roof_RoundTiles_6x14",
         (4, 4): "Roof_RoundTiles_8x8", (4, 5): "Roof_RoundTiles_8x10", (4, 6): "Roof_RoundTiles_8x12", (4, 7): "Roof_RoundTiles_8x14"}


def piece(name, model, pos, yaw=0.0):
    return {"name": name, "shape": "model", "model": KIT + model + ".gltf", "pos": [round(v, 3) for v in pos], "rot": [0, yaw, 0]}


def floor_parts(style, nx, nz, y, label):
    parts = []
    for j in range(nz):
        z = (j - (nz - 1) * 0.5) * CELL
        p = piece("%s %d" % (label, j), style, [0, y + 0.02, z])
        p.update({"count": nx, "layout": "row", "spacing": CELL})
        parts.append(p)
    return parts


def wall_parts(s, nx, nz, y, ground):
    hx, hz = nx * CELL * 0.5, nz * CELL * 0.5
    parts = []
    for i in range(nx):   # the front (+z) and the back (-z): the door in the middle of the front on the ground floor
        x = (i - (nx - 1) * 0.5) * CELL
        front = s["door"] if ground and i == nx // 2 else (s["window"] if (i + (0 if ground else 1)) % 2 == 0 else s["wall"])
        parts.append(piece("Front %d" % i, front, [x, y, hz]))
        parts.append(piece("Back %d" % i, s["window"] if i % 2 == 1 else s["wall"], [x, y, -hz], 180))
    for j in range(nz):   # the sides, a window every other cell
        z = (j - (nz - 1) * 0.5) * CELL
        side = s["window"] if j % 2 == 1 else s["wall"]
        parts.append(piece("Left %d" % j, side, [-hx, y, z], -90))
        parts.append(piece("Right %d" % j, side, [hx, y, z], 90))
    for k, (cx, cz) in enumerate(((-hx, -hz), (hx, -hz), (hx, hz), (-hx, hz))):
        parts.append(piece("Corner %d" % k, s["corner"], [cx, y, cz]))
    return parts


def building(spec):
    nx, nz = spec["cells"]
    floors = spec.get("floors", 1)
    s = STYLES[spec.get("style", "plaster")]
    roof = ROOFS.get((nx, nz))
    if not roof:
        raise SystemExit("%s: no roof of the kit for %dx%d cells (x 2..4, z x..7)" % (spec["id"], nx, nz))
    phases = [{"name": "floor", "parts": floor_parts(s["floor"], nx, nz, 0.0, "Floor")},
              {"name": "walls", "parts": wall_parts(s, nx, nz, 0.0, True)}]
    for f in range(1, floors):
        y = f * STOREY
        phases.append({"name": "storey %d" % (f + 1), "parts": floor_parts(s["upper"], nx, nz, y, "Storey %d floor" % (f + 1)) + wall_parts(s, nx, nz, y, False)})
    top = floors * STOREY
    gable = "Roof_Front_Brick%d" % (nx * 2)   # the triangles under the roof, front and back
    hz = nz * CELL * 0.5
    phases.append({"name": "roof", "parts": [piece("Gable front", gable, [0, top, hz]), piece("Gable back", gable, [0, top, -hz], 180),
                                             piece("Roof", roof, [0, top, 0]), piece("Chimney", "Prop_Chimney", [CELL * 0.75, top + 0.5, -CELL * 0.75])]})
    w, d = nx * CELL + 0.4, nz * CELL + 0.4
    return {"footprint": [w, d], "note": "made by tools/make_kit_buildings.py from tools/kit_buildings.json: %dx%d cells, %d floor(s), %s" % (nx, nz, floors, spec.get("style", "plaster")),
            "phases": phases}


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    specs = json.load(open(os.path.join(root, "tools", "kit_buildings.json"), encoding="utf-8"))["buildings"]
    path = os.path.join(root, "assets", "data", "visuals.json")
    text = open(path, encoding="utf-8").read()
    anchor = '    "buildings": {\n'
    if anchor not in text:
        raise SystemExit("visuals.json has no buildings section where it is expected")
    for spec in reversed(specs):
        line = '      "%s": %s,\n' % (spec["id"], json.dumps(building(spec)))
        text = re.sub(r'      "%s": \{.*\},\n' % re.escape(spec["id"]), "", text)   # the old one-line entry, if any
        text = text.replace(anchor, anchor + line, 1)
    json.loads(text)
    open(path, "w", encoding="utf-8").write(text)
    for spec in specs:
        print(spec["id"], "%dx%d" % tuple(spec["cells"]), spec.get("floors", 1), "floor(s)", spec.get("style", "plaster"), "->", len(building(spec)["phases"]), "phases")


if __name__ == "__main__":
    main()
