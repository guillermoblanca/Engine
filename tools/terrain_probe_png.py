"""Maps of a world measured by the terrain probe (TERR0, documentation/02_tecnico/motor/TERRENO_V5.md). Standard library only.

    GBSimTests terrain_graph_cache probe <preset> <type> <seed> <size> <resolution> <dir>
    python -I tools/terrain_probe_png.py <dir> <tag> [<out_dir>] [--px 640] [--box i0 j0 i1 j1]

<tag> is what the probe printed (for example highland_valley_temperate_5_1025). It writes three top-down maps, north (-z) up:
  <tag>_height.png  hillshade over a height ramp
  <tag>_flow.png    log of the D8 drainage area of the final ground (white: big channels); the water in blue
  <tag>_water.png   grey hillshade with the water: lake blue, river cyan; red = mid-channel ground above the water (the surface is cut),
                    magenta = the ribbon's edge over ground below the water (it floats), orange = the lake's drawn disc over dry ground below its level
  <tag>_lakemesh.png       (only when the probe wrote <tag>_lakemesh.f32, T105) the combined lake mesh against the lakes' cells: blue = a lake cell
                           the mesh draws, green = its skirt (a vertex past the shore, under the ground), red = a lake cell no triangle touches,
                           orange = the old disc over dry ground below its level (not drawn by the mesh), magenta = the mesh over dry ground (a defect)
  <tag>_lakemesh_zoom.png  the same, cropped to the lakes (or to the samples of --box) and enlarged (nearest sample), so single cells show
"""
import json
import math
import os
import struct
import sys
import zlib


def read_f32(path, n):
    with open(path, "rb") as f:
        data = f.read()
    return struct.unpack("<%df" % (n * n), data)


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)

    def chunk(kind, body):
        c = struct.pack(">I", len(body)) + kind + body
        return c + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def ramp(t):
    stops = [(0.0, (60, 110, 70)), (0.35, (140, 160, 90)), (0.6, (170, 140, 100)), (0.82, (140, 125, 115)), (1.0, (245, 245, 250))]
    t = min(max(t, 0.0), 1.0)
    for (a, ca), (b, cb) in zip(stops, stops[1:]):
        if t <= b:
            u = (t - a) / (b - a)
            return tuple(ca[k] + (cb[k] - ca[k]) * u for k in range(3))
    return stops[-1][1]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    px = 640
    if "--px" in sys.argv:
        px = int(sys.argv[sys.argv.index("--px") + 1])
        args = [a for a in args if a != str(px)]
    box = None
    if "--box" in sys.argv:
        k = sys.argv.index("--box")
        box = [int(v) for v in sys.argv[k + 1:k + 5]]
        args = [a for a in args if a not in sys.argv[k + 1:k + 5]]
    src, tag = args[0], args[1]
    out = args[2] if len(args) > 2 else src
    info = json.load(open(os.path.join(src, tag + "_info.json")))
    n = int(info["n"])
    height = read_f32(os.path.join(src, tag + "_height.f32"), n)
    water = read_f32(os.path.join(src, tag + "_water.f32"), n)
    flow = read_f32(os.path.join(src, tag + "_flow.f32"), n)
    spacing = info["size"] / (n - 1)
    lo, hi = info["min_h"], info["max_h"]
    step = max(1, int(n / px + 0.5))
    w = (n + step - 1) // step
    maxflow = math.log(max(flow))
    sun = (-0.6, 0.7, -0.4)
    sl = math.sqrt(sum(c * c for c in sun))
    sun = tuple(c / sl for c in sun)

    def at(i, j):
        i = min(max(i, 0), n - 1)
        j = min(max(j, 0), n - 1)
        return height[j * n + i]

    rows_h, rows_f, rows_w = [], [], []
    for j in range(0, n, step):
        rh, rf, rw = bytearray(), bytearray(), bytearray()
        for i in range(0, n, step):
            gx = (at(i + 1, j) - at(i - 1, j)) / (2 * spacing)
            gz = (at(i, j + 1) - at(i, j - 1)) / (2 * spacing)
            nl = math.sqrt(gx * gx + gz * gz + 1.0)
            shade = max(0.0, (-gx * sun[0] + sun[1] - gz * sun[2]) / nl)
            shade = 0.35 + 0.65 * shade
            c = j * n + i
            base = ramp((height[c] - lo) / max(hi - lo, 1e-3))
            rh += bytes(int(min(255, base[k] * shade)) for k in range(3))
            wc = water[c]
            g = int(min(255, 200 * shade))
            fl = math.log(max(flow[c], 1.0)) / maxflow
            fv = int(min(255, 40 + 215 * fl ** 1.6))
            if wc == 1.0 or wc == 2.0:
                rf += bytes((30, 80, 200))
            else:
                rf += bytes((fv, fv, fv))
            colours = {1.0: (40, 90, 210), 2.0: (40, 200, 230), 3.0: (230, 20, 20), 4.0: (230, 0, 230), 5.0: (255, 140, 0)}
            rw += bytes(colours.get(wc, (g, g, g)))
        rows_h.append(rh)
        rows_f.append(rf)
        rows_w.append(rw)
    hgt = len(rows_h)
    write_png(os.path.join(out, tag + "_height.png"), w, hgt, rows_h)
    write_png(os.path.join(out, tag + "_flow.png"), w, hgt, rows_f)
    write_png(os.path.join(out, tag + "_water.png"), w, hgt, rows_w)
    print("wrote %s_{height,flow,water}.png (%d x %d, 1 px = %.1f m)" % (os.path.join(out, tag), w, hgt, spacing * step))

    mesh_path = os.path.join(src, tag + "_lakemesh.f32")
    if not os.path.exists(mesh_path):
        return
    mesh = read_f32(mesh_path, n)
    mesh_colours = {1.0: (40, 90, 210), 2.0: (40, 200, 90), 3.0: (230, 20, 20), 4.0: (255, 140, 0), 5.0: (230, 0, 230)}

    def shade_at(i, j):
        gx = (at(i + 1, j) - at(i - 1, j)) / (2 * spacing)
        gz = (at(i, j + 1) - at(i, j - 1)) / (2 * spacing)
        nl = math.sqrt(gx * gx + gz * gz + 1.0)
        return 0.35 + 0.65 * max(0.0, (-gx * sun[0] + sun[1] - gz * sun[2]) / nl)

    def mesh_pixel(i, j):
        m = mesh[j * n + i]
        if m in mesh_colours:
            return mesh_colours[m]
        g = int(min(255, 200 * shade_at(i, j)))
        return (g, g, g)

    rows_m = []
    for j in range(0, n, step):
        rows_m.append(bytearray(b"".join(bytes(mesh_pixel(i, j)) for i in range(0, n, step))))
    write_png(os.path.join(out, tag + "_lakemesh.png"), w, hgt, rows_m)
    # the crop: the box of every marked sample, grown by a tenth, enlarged to about `px` pixels
    marked = [c for c in range(n * n) if mesh[c] > 0.0]
    i0, i1 = min(c % n for c in marked), max(c % n for c in marked)
    j0, j1 = min(c // n for c in marked), max(c // n for c in marked)
    pad = max(4, (max(i1 - i0, j1 - j0)) // 10)
    i0, j0, i1, j1 = max(0, i0 - pad), max(0, j0 - pad), min(n - 1, i1 + pad), min(n - 1, j1 + pad)
    if box:
        i0, j0, i1, j1 = max(0, box[0]), max(0, box[1]), min(n - 1, box[2]), min(n - 1, box[3])
    scale = max(1, px // max(i1 - i0 + 1, j1 - j0 + 1))
    rows_z = []
    for j in range(j0, j1 + 1):
        row = bytearray(b"".join(bytes(mesh_pixel(i, j)) * scale for i in range(i0, i1 + 1)))
        for _ in range(scale):
            rows_z.append(row)
    write_png(os.path.join(out, tag + "_lakemesh_zoom.png"), (i1 - i0 + 1) * scale, len(rows_z), rows_z)
    print("wrote %s_lakemesh{,_zoom}.png (zoom: samples %d..%d x %d..%d, %d px each)" % (os.path.join(out, tag), i0, i1, j0, j1, scale))


if __name__ == "__main__":
    main()
