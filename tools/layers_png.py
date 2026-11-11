"""Top-down maps (north, -z, up) of what `GBSimTests terrain_graph_cache layers ... <dir>` wrote. Standard library only.
    python -I tools/layers_png.py <dir> <tag> <out_dir> [--px 640]
Writes <tag>_map_<layer>.png for every new layer (its weight in colour over a grey hillshade, water in blue), <tag>_map_layers.png (every
layer in its flat colour, the partition as the CPU paints it, over the hillshade) and <tag>_map_trees.png (one dot per tree by species)."""
import json
import math
import struct
import sys
import zlib


def read_f32(path):
    with open(path, "rb") as f:
        data = f.read()
    return struct.unpack("<%df" % (len(data) // 4), data)


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)

    def chunk(kind, body):
        c = struct.pack(">I", len(body)) + kind + body
        return c + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


COLORS = {  # the flat colours of Terrain::PaintPartition (roughly)
    "snow": (0.93, 0.95, 0.99), "path": (0.50, 0.42, 0.31), "cliff": (0.40, 0.38, 0.36), "scree": (0.58, 0.55, 0.50), "rock": (0.48, 0.38, 0.31),
    "gravel": (0.62, 0.58, 0.50), "mud": (0.26, 0.21, 0.16), "mud_dry": (0.47, 0.40, 0.31), "grass_wet": (0.17, 0.33, 0.12), "forest": (0.10, 0.16, 0.07),
    "dry": (0.47, 0.46, 0.22), "sand": (0.80, 0.66, 0.45), "grass": (0.30, 0.45, 0.16),
}
NEW = ["cliff", "scree", "gravel", "mud", "mud_dry", "grass_wet", "path"]
TINT = {"cliff": (0.85, 0.15, 0.10), "scree": (0.95, 0.60, 0.10), "gravel": (0.95, 0.85, 0.20), "mud": (0.55, 0.30, 0.10), "mud_dry": (0.85, 0.55, 0.35),
        "grass_wet": (0.10, 0.80, 0.30), "path": (0.90, 0.20, 0.80)}
SPECIES = [(0.95, 0.85, 0.20), (0.55, 0.85, 0.20), (0.20, 0.70, 0.95), (0.10, 0.35, 0.10), (0.95, 0.35, 0.15), (0.75, 0.75, 0.75), (0.9, 0.2, 0.9)]


def main():
    args = sys.argv[1:]
    px = 640
    if "--px" in args:
        i = args.index("--px")
        px = int(args[i + 1])
        del args[i:i + 2]
    src, tag, out = args[0], args[1], args[2]
    info = json.load(open("%s/%s_layers.json" % (src, tag)))
    n, size = info["n"], info["size"]
    sp = size / (n - 1)
    h = read_f32("%s/%s_height.f32" % (src, tag))
    wet = read_f32("%s/%s_wet.f32" % (src, tag))
    layers = {name: read_f32("%s/%s_layer_%s.f32" % (src, tag, name)) for name in info["layers"]}
    step = (n - 1) / (px - 1)

    def idx(x, y):
        i = min(int(round(x * step)), n - 1)
        j = min(int(round(y * step)), n - 1)
        return j * n + i, i, j

    shade = [0.0] * (px * px)
    for y in range(px):
        for x in range(px):
            c, i, j = idx(x, y)
            i0, i1, j0, j1 = max(i - 1, 0), min(i + 1, n - 1), max(j - 1, 0), min(j + 1, n - 1)
            dx = (h[j * n + i1] - h[j * n + i0]) / ((i1 - i0) * sp)
            dz = (h[j1 * n + i] - h[j0 * n + i]) / ((j1 - j0) * sp)
            nx, ny, nz = -dx, 1.0, -dz
            l = math.sqrt(nx * nx + ny * ny + nz * nz)
            lx, ly, lz = -0.5, 0.75, -0.43
            ll = math.sqrt(lx * lx + ly * ly + lz * lz)
            shade[y * px + x] = max(0.0, (nx * lx + ny * ly + nz * lz) / (l * ll))

    def rows_of(color_at):
        rows = []
        for y in range(px):
            row = bytearray()
            for x in range(px):
                r, g, b = color_at(x, y)
                row += bytes((max(0, min(255, int(r * 255))), max(0, min(255, int(g * 255))), max(0, min(255, int(b * 255)))))
            rows.append(row)
        return rows

    def grey(x, y):
        c, _, _ = idx(x, y)
        s = 0.25 + 0.75 * shade[y * px + x]
        if wet[c] == 1:
            return (0.15, 0.30, 0.65)
        if wet[c] == 2:
            return (0.20, 0.55, 0.85)
        return (s * 0.8, s * 0.8, s * 0.8)

    for name in NEW:
        if name not in layers:
            continue
        L = layers[name]
        t = TINT[name]

        def col(x, y, L=L, t=t):
            c, _, _ = idx(x, y)
            base = grey(x, y)
            if wet[c]:
                return base   # (the beds under the water are not drawn)
            w = max(0.0, min(1.0, L[c]))
            return tuple(base[k] * (1 - w) + t[k] * w * (0.5 + 0.5 * shade[y * px + x]) for k in range(3))
        write_png("%s/%s_map_%s.png" % (out, tag, name), px, px, rows_of(col))

    def composite(x, y):
        c, _, _ = idx(x, y)
        s = 0.35 + 0.65 * shade[y * px + x]
        r = g = b = 0.0
        for name, L in layers.items():
            w = L[c]
            if w <= 0:
                continue
            cc = COLORS.get(name, (1, 0, 1))
            r += cc[0] * w; g += cc[1] * w; b += cc[2] * w
        if wet[c]:
            r, g, b = r * 0.45 + 0.08, g * 0.45 + 0.18, b * 0.45 + 0.35
        return (r * s * 1.3, g * s * 1.3, b * s * 1.3)
    write_png("%s/%s_map_layers.png" % (out, tag), px, px, rows_of(composite))

    trees = read_f32("%s/%s_trees.f32" % (src, tag))
    recipes = info["recipes"]
    variety = max(1, len(recipes) // max(1, len({tuple(r) for r in recipes})))
    img = [list(grey(x, y)) for y in range(px) for x in range(px)]
    half = size / 2
    for k in range(0, len(trees), 3):
        x, z, v = trees[k], trees[k + 1], int(trees[k + 2])
        u = int((x + half) / size * (px - 1) + 0.5)
        w = int((z + half) / size * (px - 1) + 0.5)
        if 0 <= u < px and 0 <= w < px:
            img[w * px + u] = list(SPECIES[(v // variety) % len(SPECIES)])
    write_png("%s/%s_map_trees.png" % (out, tag), px, px, rows_of(lambda x, y: img[y * px + x]))
    print("maps of", tag, "in", out)


main()
