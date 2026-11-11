"""Top-down map of the settle nodes from the probe dumps: grey hillshade, water blue/cyan, trails yellow, platforms orange, site centres red.
    python -I tools/settle_map.py <dir> <tag> <out.png> [i0 j0 i1 j1] [scale]
"""
import json
import math
import os
import struct
import sys
import zlib


def read_f32(path, n):
    with open(path, "rb") as f:
        return struct.unpack("<%df" % (n * n), f.read())


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)

    def chunk(kind, body):
        c = struct.pack(">I", len(body)) + kind + body
        return c + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


src, tag, out = sys.argv[1], sys.argv[2], sys.argv[3]
info = json.load(open(os.path.join(src, tag + "_info.json")))
n = int(info["n"])
height = read_f32(os.path.join(src, tag + "_height.f32"), n)
water = read_f32(os.path.join(src, tag + "_water.f32"), n)
settle = read_f32(os.path.join(src, tag + "_settle.f32"), n)
sp = info["size"] / (n - 1)
i0, j0, i1, j1 = 0, 0, n, n
scale = 1
if len(sys.argv) >= 8:
    i0, j0, i1, j1 = (int(v) for v in sys.argv[4:8])
if len(sys.argv) >= 9:
    scale = int(sys.argv[8])
sun = (-0.6, 0.7, -0.4)
sl = math.sqrt(sum(c * c for c in sun))
sun = tuple(c / sl for c in sun)


def at(i, j):
    return height[min(max(j, 0), n - 1) * n + min(max(i, 0), n - 1)]


rows = []
for j in range(j0, j1):
    r = bytearray()
    for i in range(i0, i1):
        gx = (at(i + 1, j) - at(i - 1, j)) / (2 * sp)
        gz = (at(i, j + 1) - at(i, j - 1)) / (2 * sp)
        nl = math.sqrt(gx * gx + gz * gz + 1.0)
        shade = 0.3 + 0.7 * max(0.0, (-gx * sun[0] + sun[1] - gz * sun[2]) / nl)
        c = j * n + i
        g = int(min(255, 215 * shade))
        px = (g, g, g)
        if water[c] == 1.0:
            px = (40, 90, 210)
        elif water[c] == 2.0:
            px = (40, 190, 230)
        s = settle[c]
        if s == 1.0:
            px = (255, 220, 0)
        elif s == 2.0:
            px = (255, 130, 0)
        elif s == 3.0:
            px = (230, 0, 0)
        cell = bytes(px) * scale
        r += cell
    for _ in range(scale):
        rows.append(r)
write_png(out, (i1 - i0) * scale, len(rows), rows)
print("wrote", out, (i1 - i0) * scale, len(rows))
