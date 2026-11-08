"""Builds the UI icon atlas (assets/ui/icons.png + icons.json) from the generated icon sheets.

The sheets are 1024x1024 images with a grid of icons (4x4 or 8x8), drawn on a painted checkerboard (not a real alpha channel) and with a
caption under each icon. For each cell: the caption band is cut off, the checkerboard is made transparent by flooding it from the edges of
the cell (light, grey pixels; the icons' thick dark outline stops the flood), and the icon is trimmed, centred and scaled into a square
cell of the atlas. The index maps an id (an item id, a task kind, a state) to its cell.

    python tools/make_icon_atlas.py <folder with the sheets> [--preview out.png]

The sheets are named by what they hold (see SHEETS); each lists its ids left to right, top to bottom ("" = an empty cell).
"""
import json
import os
import sys
from collections import deque

from PIL import Image

CELL = 128          # the side of an icon in the atlas
COLUMNS = 16        # icons per row of the atlas

# file name -> (grid, fraction of the cell height cut at the top (the caption of the row above, when it runs over), kept down to, ids)
SHEETS = {
    "actions_work.jpg": (4, 0.0, 0.80, [
        "goto", "gather", "chop", "mine",
        "collect", "deposit", "deliver", "build",
        "make", "experiment", "explore", "expedition",
        "hunt", "fight", "flee", "care"]),
    "actions_life.jpg": (4, 0.0, 0.80, [
        "eat", "drink", "sleep", "warm",
        "stoke", "fire", "rite", "talk",
        "till", "sow", "tend", "harvest",
        "haul", "rest", "migrate", "trade"]),
    "states.jpg": (4, 0.0, 0.80, [
        "state.hungry", "state.thirsty", "state.tired", "state.cold",
        "state.wounded", "state.afraid", "state.angry", "state.happy",
        "state.cannot", "state.waiting", "state.full", "state.looking",
        "state.love", "state.birth", "state.death", "state.discovery"]),
    "items.jpg": (8, 0.07, 0.80, [
        "berry", "cooked_meat", "dried_berry", "eggs", "bone", "branch", "clay", "copper_ingot",
        "fish", "grain", "greens", "honey", "copper_ore", "cordage", "daub", "fat",
        "meat", "mushrooms", "nuts", "roast", "feather", "fiber", "fired_brick", "flint",
        "roots", "smoked_meat", "stew", "water", "granite", "herbs", "hide", "leather",
        "log", "mud_brick", "plank", "poultice", "bone_pickaxe", "copper_pickaxe", "fish_trap", "flint_pickaxe",
        "reed", "reed_mat", "resin", "salt", "flint_sickle", "stone_axe", "stone_hoe", "stone_pickaxe",
        "sinew", "snare", "stake", "stone", "flint_spear", "flint_sword", "hunting_bow", "basket_wicker",
        "wood", "acorn", "grain_seed", "campfire", "", "copper_axe", "leather_bag", ""]),
    # the technologies (the tree of knowledge, TechTreeView.cpp): one icon each, "tech.<id>"; the last row of the second sheet is left empty
    "techs_a.jpg": (4, 0.03, 0.80, [
        "tech.stone_tools", "tech.fire_making", "tech.cordage", "tech.flint_knapping",
        "tech.basketry", "tech.spear_making", "tech.hide_working", "tech.cooking",
        "tech.smoking", "tech.weaving", "tech.wattle_and_daub", "tech.timber_framing",
        "tech.palisade_building", "tech.food_storage", "tech.herb_lore", "tech.ritual"]),
    "techs_b.jpg": (4, 0.03, 0.80, [
        "tech.pottery", "tech.smelting", "tech.bone_working", "tech.tillage",
        "tech.storytelling", "tech.shamanism", "tech.trapping", "tech.fishing",
        "tech.butchery", "tech.cuisine", "tech.carpentry", "tech.masonry",
        "", "", "", ""]),
    # the extra sheet (no captions): things of the world for the selection panel
    "world.jpg": (4, 0.0, 1.0, [
        "flint_axe", "clothing", "node.campfire", "world.meat",
        "spear", "gourd", "building.shelter", "world.fish",
        "club", "node.berry_bush", "world.hide", "world.bone",
        "world.basket", "tusk", "world.campfire", "world.axe"]),
}


# icons that are themselves white and grey (a heap of salt): only the pure neutral tones of the checkerboard are background there
STRICT = {"salt"}


def is_background(p, tint=26):
    r, g, b = p[0], p[1], p[2]
    return min(r, g, b) > 170 and max(r, g, b) - min(r, g, b) < tint   # the painted checkerboard: light and grey


def clean_cell(sheet, box, tint=26):
    cell = sheet.crop(box).convert("RGBA")
    w, h = cell.size
    px = cell.load()
    seen = [[False] * w for _ in range(h)]
    queue = deque()
    for x in range(w):
        queue.append((x, 0)); queue.append((x, h - 1))
    for y in range(h):
        queue.append((0, y)); queue.append((w - 1, y))
    while queue:
        x, y = queue.popleft()
        if x < 0 or y < 0 or x >= w or y >= h or seen[y][x]:
            continue
        seen[y][x] = True
        if not is_background(px[x, y], tint):
            continue
        px[x, y] = (0, 0, 0, 0)
        queue.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))
    # checkerboard shut inside an icon (a bulb, a loop of rope, a bow): a region of background pixels with both the white and the grey squares in it
    for y0 in range(h):
        for x0 in range(w):
            if seen[y0][x0] or not is_background(px[x0, y0], tint):
                continue
            region, white, grey = [], 0, 0
            queue.append((x0, y0))
            seen[y0][x0] = True
            while queue:
                x, y = queue.popleft()
                region.append((x, y))
                r = px[x, y][0]
                if r > 243: white += 1
                elif r < 225: grey += 1
                for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                    if 0 <= nx < w and 0 <= ny < h and not seen[ny][nx] and is_background(px[nx, ny], tint):
                        seen[ny][nx] = True
                        queue.append((nx, ny))
            if len(region) > 60 and white > 0.15 * len(region) and grey > 0.15 * len(region):
                for x, y in region:
                    px[x, y] = (0, 0, 0, 0)
    # the anti-aliased rim between the outline and the checkerboard: light grey pixels next to a cleared one fade out
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a and min(r, g, b) > 150 and max(r, g, b) - min(r, g, b) < 30:
                if any(0 <= x + dx < w and 0 <= y + dy < h and px[x + dx, y + dy][3] == 0 for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    px[x, y] = (r, g, b, 0)
    return cell


def fit(icon):
    bbox = icon.getbbox()
    if not bbox:
        return Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    icon = icon.crop(bbox)
    margin = CELL // 16
    scale = (CELL - 2 * margin) / max(icon.size)
    icon = icon.resize((max(1, round(icon.size[0] * scale)), max(1, round(icon.size[1] * scale))), Image.LANCZOS)
    out = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    out.paste(icon, ((CELL - icon.size[0]) // 2, (CELL - icon.size[1]) // 2), icon)
    return out


def main():
    args = sys.argv[1:]
    preview = None
    if "--preview" in args:
        i = args.index("--preview")
        preview = args[i + 1]
        del args[i:i + 2]
    folder = args[0] if args else "."
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    icons = []
    for name, (grid, top, keep, ids) in SHEETS.items():
        path = os.path.join(folder, name)
        if not os.path.exists(path):
            print("missing sheet", path)
            continue
        sheet = Image.open(path).convert("RGB")
        step = sheet.size[0] / grid
        for k, id_ in enumerate(ids):
            if not id_:
                continue
            col, row = k % grid, k // grid
            box = (round(col * step), round(row * step + step * top), round((col + 1) * step), round(row * step + step * keep))
            icons.append((id_, fit(clean_cell(sheet, box, 7 if id_ in STRICT else 26))))
    rows = (len(icons) + COLUMNS - 1) // COLUMNS
    atlas = Image.new("RGBA", (COLUMNS * CELL, rows * CELL), (0, 0, 0, 0))
    index = {"cell": CELL, "columns": COLUMNS, "icons": {}}
    for i, (id_, icon) in enumerate(icons):
        x, y = (i % COLUMNS) * CELL, (i // COLUMNS) * CELL
        atlas.paste(icon, (x, y), icon)
        index["icons"][id_] = [x, y]
    out = os.path.join(root, "assets", "ui")
    os.makedirs(out, exist_ok=True)
    atlas.save(os.path.join(out, "icons.png"))
    with open(os.path.join(out, "icons.json"), "w", encoding="utf-8") as f:
        json.dump(index, f, indent=1)
    print(f"{len(icons)} icons, atlas {atlas.size[0]}x{atlas.size[1]}")
    if preview:
        bg = Image.new("RGBA", atlas.size, (60, 90, 60, 255))   # on a dark green: the cut-outs and any leftover checkerboard show
        bg.alpha_composite(atlas)
        bg.convert("RGB").save(preview)


if __name__ == "__main__":
    main()
