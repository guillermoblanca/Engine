"""Real relief references for TERRENO_V5 (documentation/02_tecnico/motor/TERRENO_V5.md, "Referencias reales"). Standard library only.

Terrain Tiles (Mapzen / AWS Open Data, terrarium PNG, Web Mercator): height = R*256 + G + B/256 - 32768 m. The tiles are untrusted data: this
script only reads them as bytes from the folder it is given (decoded here with zlib, nothing in that folder is run).

    python -I tools/dem_ref.py fetch <tiles_dir> <site> [--zoom Z] [--center]      download the tiles of a site's 3 km crop (curl.exe, 1 s apart)
    python -I tools/dem_ref.py crop <tiles_dir> <site> <out_dir> [--spacing S] [--zoom Z]
        the crop as a metric grid: <out_dir>/<site>_height.f32 (float32, n*n, row major, j = 0 north: the probe's -z) and <site>_dem.json
    python -I tools/dem_ref.py resample <in.f32> <n> <size> <out_dir> <tag> --spacing S [--water <water.f32>]
        one of our worlds (the probe's <tag>_height.f32) on the same kind of grid: box-averaged (bilinear supersampling, the same rule as crop)
    python -I tools/dem_ref.py resolution <tiles_dir> <site> [--zoom 15]
        the effective resolution of the source under a fine tile: the lag of the peak of the autocorrelation of the squared second
        derivative (a source cell spans k pixels) and the residual of a box down/up at each factor
    python -I tools/dem_ref.py sheet <out.png> <px> <label>=<dir>/<tag> ...
        side by side, one column per grid (<dir>/<tag>_height.f32 + <tag>_dem.json; <tag>_flow.f32 and <tag>_water.f32 from the probe's dem
        command when present): hillshade over a height ramp with a common relief range, slope classes, drainage area. Same sun, same metres
        per pixel, a 1 km scale bar.
    python -I tools/dem_ref.py table <label>=<dir>/<tag> ... [--only key,key]   the dem command's <tag>_relief.json as a markdown table
    python -I tools/dem_ref.py closest "<real>=<base>;..." "<ours>=<base>;..."   ours ordered by distance in shape metrics to each real site
    python -I tools/dem_ref.py profile <dir>/<tag> [--mountain] [--frac 0.3] [--reach 600]   the median cross-section of the main valley
    --at=<site>=<lat>,<lon> moves a site's centre for one run (to look around before fixing it in SITES); --size <m> a larger crop.

Grid convention (the probe's): x east, z south, the crop centred on (0, 0); row j = 0 is the north edge.
"""
import json
import math
import os
import struct
import subprocess
import sys
import time
import zlib

# id: (lat, lon, zoom of the crop, spacing of the grid in m (the source's resolution), what it shows)
SITES = {
    "jerte": (40.192, -5.800, 13, 30.0, "Valle del Jerte: river valley in V with gullies on its sides, temperate"),
    "cares": (43.240, -4.870, 13, 30.0, "Garganta del Cares: V gorge, limestone walls and scree"),
    "bardenas": (42.222, -1.520, 13, 30.0, "Bardenas Reales: badlands, dense dendritic drainage, arid"),
    "deathvalley": (36.190, -116.937, 15, 10.0, "Death Valley, Hanaupah Canyon fan: alluvial fan at the range front"),
    "dolomitas": (46.525, 11.735, 13, 30.0, "Sassolungo / Langkofel: rock cliff with scree cones at its foot"),
    "panticosa": (42.775, -0.225, 13, 30.0, "Ibones de Panticosa / Bachimana: glacial tarns draining over the col"),
    "ordesa": (42.648, -0.030, 13, 30.0, "Valle de Ordesa: glacial U valley"),
    # not in the order's table: Bardenas at 30 m does not resolve its gullies, so badlands at the 3DEP resolution too
    "zabriskie": (36.415, -116.825, 15, 10.0, "Death Valley badlands by Zabriskie Point: dense dendritic gullies, arid (3DEP)"),
}
CROP = 3000.0
URL = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png"
EARTH = 6378137.0


# ---------------------------------------------------------------- PNG (8-bit RGB / RGBA, not interlaced)
def decode_png(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, w = 8, [], 0
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise ValueError("unsupported PNG (depth %d, colour type %d, interlace %d)" % (depth, ctype, interlace))
            bpp = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    raw = zlib.decompress(b"".join(idat))
    stride = w * bpp
    rows, prev, p = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[p]
        line = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        elif f != 0:
            raise ValueError("bad PNG filter %d" % f)
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def terrarium(path):
    with open(path, "rb") as fh:
        w, h, bpp, rows = decode_png(fh.read())
    out = []
    for r in rows:
        out.append([r[i * bpp] * 256.0 + r[i * bpp + 1] + r[i * bpp + 2] / 256.0 - 32768.0 for i in range(w)])
    return out


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)

    def chunk(kind, body):
        c = struct.pack(">I", len(body)) + kind + body
        return c + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


# ---------------------------------------------------------------- Web Mercator
def world_px(lat, lon, z):
    s = 256.0 * 2 ** z
    x = (lon + 180.0) / 360.0 * s
    y = (1.0 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2.0 * s
    return x, y


def metres_per_px(lat, z):
    return math.cos(math.radians(lat)) * 2.0 * math.pi * EARTH / (256.0 * 2 ** z)


def crop_tiles(site, z, center=False, size=None):
    lat, lon = SITES[site][0], SITES[site][1]
    cx, cy = world_px(lat, lon, z)
    half = 0.0 if center else (size or CROP) / 2.0 / metres_per_px(lat, z) + 4.0
    x0, x1 = int((cx - half) // 256), int((cx + half) // 256)
    y0, y1 = int((cy - half) // 256), int((cy + half) // 256)
    return [(z, x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)]


def tile_path(d, z, x, y):
    return os.path.join(d, "%d_%d_%d.png" % (z, x, y))


def fetch(args):
    d, site = args[0], args[1]
    z = int(opt(args, "--zoom", SITES[site][2]))
    os.makedirs(d, exist_ok=True)
    for (tz, x, y) in crop_tiles(site, z, "--center" in args, float(opt(args, "--size", CROP))):
        p = tile_path(d, tz, x, y)
        if os.path.exists(p) and os.path.getsize(p) > 0:
            continue
        url = URL.format(z=tz, x=x, y=y)
        r = subprocess.run(["curl.exe", "-s", "-f", "-D", p + ".hdr", "-o", p, url])
        print("%s -> %s (%s)" % (url, p, "ok" if r.returncode == 0 else "curl %d" % r.returncode))
        if r.returncode != 0:
            sys.exit(1)
        time.sleep(1.0)


def sources(d, tiles):
    out = set()
    for (z, x, y) in tiles:
        try:
            for line in open(tile_path(d, z, x, y) + ".hdr", encoding="latin-1"):
                if line.lower().startswith("x-amz-meta-x-imagery-sources:"):
                    out.update(s.strip() for s in line.split(":", 1)[1].split(","))
        except OSError:
            pass
    return sorted(out)


class Mosaic:
    def __init__(self, d, tiles):
        self.t = {}
        for (z, x, y) in tiles:
            self.t[(x, y)] = terrarium(tile_path(d, z, x, y))
        # no data (-32768 at a tile's edge, seen in a 3DEP tile of Death Valley): the mean of the valid pixels around, repeated until none is left
        self.nodata = 0
        x0, y0 = min(k[0] for k in self.t), min(k[1] for k in self.t)
        bad = [(px, py) for (tx, ty), rows in self.t.items() for j, r in enumerate(rows) for i, v in enumerate(r) if v < -1000.0
               for px, py in [(tx * 256 + i, ty * 256 + j)]]
        self.nodata = len(bad)
        while bad:
            left = []
            for (px, py) in bad:
                vals = []
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        qx, qy = px + dx, py + dy
                        if (qx // 256, qy // 256) in self.t and (dx or dy):
                            v = self.at(qx, qy)
                            if v >= -1000.0:
                                vals.append(v)
                if vals:
                    self.t[(px // 256, py // 256)][py % 256][px % 256] = sum(vals) / len(vals)
                else:
                    left.append((px, py))
            if len(left) == len(bad):
                break
            bad = left
        del x0, y0

    def at(self, px, py):   # one pixel, its centre at (px + 0.5, py + 0.5)
        tx, ty = px // 256, py // 256
        return self.t[(tx, ty)][py - ty * 256][px - tx * 256]

    def bilinear(self, x, y):
        x -= 0.5
        y -= 0.5
        i, j = int(math.floor(x)), int(math.floor(y))
        u, v = x - i, y - j
        a, b = self.at(i, j), self.at(i + 1, j)
        c, e = self.at(i, j + 1), self.at(i + 1, j + 1)
        return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + e * u) * v


def box_grid(sample, size, spacing, src):
    """A grid of n*n nodes over `size` m, each the mean of k*k bilinear samples over its footprint (spacing x spacing); k from the source's
    spacing `src` (2 samples per source cell, at least 1). sample(x, z) with x east, z south, (0, 0) the centre."""
    n = int(round(size / spacing)) + 1
    k = max(1, int(round(2.0 * spacing / src)))
    offs = [((a + 0.5) / k - 0.5) * spacing for a in range(k)] if k > 1 else [0.0]
    out = []
    for j in range(n):
        zc = -size / 2.0 + j * spacing
        for i in range(n):
            xc = -size / 2.0 + i * spacing
            s = 0.0
            for dz in offs:
                for dx in offs:
                    s += sample(xc + dx, zc + dz)
            out.append(s / (len(offs) ** 2))
    return n, out


def crop(args):
    d, site, out = args[0], args[1], args[2]
    lat, lon, zdef, sdef, what = SITES[site]
    z = int(opt(args, "--zoom", zdef))
    spacing = float(opt(args, "--spacing", sdef))
    size = float(opt(args, "--size", CROP))
    tiles = crop_tiles(site, z, False, size)
    m = Mosaic(d, tiles)
    cx, cy = world_px(lat, lon, z)
    mpp = metres_per_px(lat, z)
    n, h = box_grid(lambda x, zz: m.bilinear(cx + x / mpp, cy + zz / mpp), size, spacing, mpp)
    os.makedirs(out, exist_ok=True)
    tag = opt(args, "--tag", site if spacing == sdef else "%s_%g" % (site, spacing))
    write_f32(os.path.join(out, tag + "_height.f32"), h)
    info = {"site": site, "what": what, "lat": lat, "lon": lon, "zoom": z, "metres_per_px": mpp, "n": n, "size": size, "spacing": spacing,
            "min_h": min(h), "max_h": max(h), "sources": sources(d, tiles), "tiles": len(tiles), "nodata_px": m.nodata}
    json.dump(info, open(os.path.join(out, tag + "_dem.json"), "w"), indent=1)
    print("%s: %d^2 at %.1f m (z%d, %.2f m/px, %d tiles), %.0f..%.0f m, sources %s" % (tag, n, spacing, z, mpp, len(tiles), min(h), max(h), ", ".join(info["sources"])))


def read_f32(path, count=None):
    with open(path, "rb") as f:
        data = f.read()
    c = len(data) // 4 if count is None else count
    return list(struct.unpack("<%df" % c, data[:4 * c]))


def write_f32(path, v):
    with open(path, "wb") as f:
        f.write(struct.pack("<%df" % len(v), *v))


def grid_sampler(v, n, size):
    sp = size / (n - 1)

    def s(x, z):
        fx = min(max((x + size / 2.0) / sp, 0.0), n - 1.000001)
        fz = min(max((z + size / 2.0) / sp, 0.0), n - 1.000001)
        i, j = int(fx), int(fz)
        u, w = fx - i, fz - j
        a, b = v[j * n + i], v[j * n + i + 1]
        c, e = v[(j + 1) * n + i], v[(j + 1) * n + i + 1]
        return (a * (1 - u) + b * u) * (1 - w) + (c * (1 - u) + e * u) * w
    return s


def resample(args):
    src, n, size, out, tag = args[0], int(args[1]), float(args[2]), args[3], args[4]
    spacing = float(opt(args, "--spacing", 30.0))
    v = read_f32(src, n * n)
    sp = size / (n - 1)
    m, h = box_grid(grid_sampler(v, n, size), size, spacing, sp)
    os.makedirs(out, exist_ok=True)
    write_f32(os.path.join(out, tag + "_height.f32"), h)
    water = opt(args, "--water", None)
    if water:   # a lake where most of the footprint is lake (the probe's kind 1); the river ribbon is not a lake
        wv = [1.0 if x == 1.0 else 0.0 for x in read_f32(water, n * n)]
        _, wl = box_grid(grid_sampler(wv, n, size), size, spacing, sp)
        write_f32(os.path.join(out, tag + "_lakes.f32"), [1.0 if x > 0.5 else 0.0 for x in wl])
    info = {"site": tag, "what": "resampled from %s (%d^2, %.2f m)" % (os.path.basename(src), n, sp), "n": m, "size": size, "spacing": spacing,
            "min_h": min(h), "max_h": max(h)}
    json.dump(info, open(os.path.join(out, tag + "_dem.json"), "w"), indent=1)
    print("%s: %d^2 at %.1f m from %d^2 at %.2f m, %.0f..%.0f m" % (tag, m, spacing, n, sp, min(h), max(h)))


def resolution(args):
    d, site = args[0], args[1]
    z = int(opt(args, "--zoom", 15))
    lat = SITES[site][0]
    tiles = crop_tiles(site, z, True)
    rows = terrarium(tile_path(d, *tiles[0]))
    mpp = metres_per_px(lat, z)
    # squared second derivative along rows and columns, its autocorrelation by lag (a source cell k pixels wide upsampled bilinearly puts
    # every curvature on its nodes: a peak at lag k)
    def acf(lines):
        sums = [0.0] * 33
        cnt = [0] * 33
        for ln in lines:
            d2 = [(ln[i - 1] - 2 * ln[i] + ln[i + 1]) ** 2 for i in range(1, len(ln) - 1)]
            mu = sum(d2) / len(d2)
            d2 = [x - mu for x in d2]
            for lag in range(33):
                sums[lag] += sum(d2[i] * d2[i + lag] for i in range(len(d2) - lag))
                cnt[lag] += len(d2) - lag
        a = [s / c for s, c in zip(sums, cnt)]
        return [x / a[0] for x in a] if a[0] > 0 else a
    a = [(x + y) / 2 for x, y in zip(acf(rows), acf([[r[i] for r in rows] for i in range(len(rows[0]))]))]
    first_min = next((k for k in range(1, 32) if a[k] <= a[k + 1]), 32)
    peak = max(range(first_min, 33), key=lambda k: a[k])   # the strongest repeat after the decay of the first lags
    # the box down/up residual: how much detail is lost averaging f x f pixels
    res = []
    for f in (2, 3, 4, 6, 8, 12):
        m = 256 // f
        err, c = 0.0, 0
        for bj in range(m):
            for bi in range(m):
                block = [rows[bj * f + y][bi * f + x] for y in range(f) for x in range(f)]
                mu = sum(block) / len(block)
                err += sum((x - mu) ** 2 for x in block)
                c += len(block)
        res.append((f, f * mpp, math.sqrt(err / c)))
    print("%s z%d (%.2f m/px), tile %s, sources %s" % (site, z, mpp, tiles[0], ", ".join(sources(d, tiles))))
    print("  acf of (d2h)^2 by lag: " + " ".join("%d:%.2f" % (k, a[k]) for k in range(1, 17)))
    print("  after the first minimum (lag %d), the strongest repeat is at lag %d px = %.1f m (acf %.2f)" % (first_min, peak, peak * mpp, a[peak]))
    print("  box residual: " + ", ".join("%dpx (%.0f m) %.2f m" % r for r in res))


# ---------------------------------------------------------------- sheets
FONT = {   # 5x7, rows top to bottom, 5 bits each
    "A": "01110 10001 10001 11111 10001 10001 10001", "B": "11110 10001 10001 11110 10001 10001 11110", "C": "01110 10001 10000 10000 10000 10001 01110",
    "D": "11110 10001 10001 10001 10001 10001 11110", "E": "11111 10000 10000 11110 10000 10000 11111", "F": "11111 10000 10000 11110 10000 10000 10000",
    "G": "01110 10001 10000 10111 10001 10001 01111", "H": "10001 10001 10001 11111 10001 10001 10001", "I": "01110 00100 00100 00100 00100 00100 01110",
    "J": "00111 00010 00010 00010 00010 10010 01100", "K": "10001 10010 10100 11000 10100 10010 10001", "L": "10000 10000 10000 10000 10000 10000 11111",
    "M": "10001 11011 10101 10101 10001 10001 10001", "N": "10001 11001 10101 10011 10001 10001 10001", "O": "01110 10001 10001 10001 10001 10001 01110",
    "P": "11110 10001 10001 11110 10000 10000 10000", "Q": "01110 10001 10001 10001 10101 10010 01101", "R": "11110 10001 10001 11110 10100 10010 10001",
    "S": "01111 10000 10000 01110 00001 00001 11110", "T": "11111 00100 00100 00100 00100 00100 00100", "U": "10001 10001 10001 10001 10001 10001 01110",
    "V": "10001 10001 10001 10001 10001 01010 00100", "W": "10001 10001 10001 10101 10101 10101 01010", "X": "10001 10001 01010 00100 01010 10001 10001",
    "Y": "10001 10001 01010 00100 00100 00100 00100", "Z": "11111 00001 00010 00100 01000 10000 11111",
    "0": "01110 10001 10011 10101 11001 10001 01110", "1": "00100 01100 00100 00100 00100 00100 01110", "2": "01110 10001 00001 00010 00100 01000 11111",
    "3": "11110 00001 00001 01110 00001 00001 11110", "4": "00010 00110 01010 10010 11111 00010 00010", "5": "11111 10000 11110 00001 00001 10001 01110",
    "6": "00110 01000 10000 11110 10001 10001 01110", "7": "11111 00001 00010 00100 01000 01000 01000", "8": "01110 10001 10001 01110 10001 10001 01110",
    "9": "01110 10001 10001 01111 00001 00010 01100", " ": "00000 00000 00000 00000 00000 00000 00000", ".": "00000 00000 00000 00000 00000 01100 01100",
    ",": "00000 00000 00000 00000 01100 00100 01000", "-": "00000 00000 00000 11111 00000 00000 00000", "_": "00000 00000 00000 00000 00000 00000 11111",
    "/": "00001 00010 00010 00100 01000 01000 10000", "(": "00010 00100 01000 01000 01000 00100 00010", ")": "01000 00100 00010 00010 00010 00100 01000",
    ":": "00000 01100 01100 00000 01100 01100 00000", "%": "11001 11010 00010 00100 01000 01011 10011", "<": "00010 00100 01000 10000 01000 00100 00010",
    ">": "01000 00100 00010 00001 00010 00100 01000", "=": "00000 00000 11111 00000 11111 00000 00000", "+": "00000 00100 00100 11111 00100 00100 00000",
}


def text(img, x, y, s, colour=(255, 255, 255), scale=2):
    for ch in s.upper():
        g = FONT.get(ch, FONT[" "]).split()
        for r, bits in enumerate(g):
            for c, b in enumerate(bits):
                if b == "1":
                    for dy in range(scale):
                        for dx in range(scale):
                            px, py = x + c * scale + dx, y + r * scale + dy
                            if 0 <= py < len(img) and 0 <= px * 3 < len(img[0]):
                                img[py][px * 3:px * 3 + 3] = bytes(colour)
        x += 6 * scale


RAMP = [(0.0, (60, 110, 70)), (0.35, (140, 160, 90)), (0.6, (170, 140, 100)), (0.82, (140, 125, 115)), (1.0, (245, 245, 250))]
SLOPE = [(3, (40, 90, 60)), (14, (120, 170, 90)), (30, (225, 210, 110)), (40, (235, 140, 50)), (50, (205, 40, 40)), (999, (110, 0, 90))]


def ramp(t):
    t = min(max(t, 0.0), 1.0)
    for (a, ca), (b, cb) in zip(RAMP, RAMP[1:]):
        if t <= b:
            u = (t - a) / (b - a)
            return tuple(ca[k] + (cb[k] - ca[k]) * u for k in range(3))
    return RAMP[-1][1]


def load_panel(base):
    info = json.load(open(base + "_dem.json"))
    n = int(info["n"])
    h = read_f32(base + "_height.f32", n * n)
    flow = read_f32(base + "_flow.f32", n * n) if os.path.exists(base + "_flow.f32") else None
    wet = read_f32(base + "_water.f32", n * n) if os.path.exists(base + "_water.f32") else None
    return info, n, h, flow, wet


def sheet(args):
    out, px = args[0], int(args[1])
    panels = []
    for a in args[2:]:
        label, base = a.split("=", 1)
        panels.append((label,) + load_panel(base))
    relief = max(max(p[3]) - min(p[3]) for p in panels)
    maxflow = max(math.log(max(max(p[4]), 1.0)) for p in panels if p[4]) if any(p[4] for p in panels) else 1.0
    sun = (-0.6, 0.7, -0.4)
    sl = math.sqrt(sum(c * c for c in sun))
    sun = tuple(c / sl for c in sun)
    head, gap, rowsn = 40, 8, 3
    W = len(panels) * px + (len(panels) + 1) * gap
    H = head + rowsn * (px + gap) + gap + 56
    img = [bytearray(b"\x1c\x1c\x1c" * W) for _ in range(H)]
    for col, (label, info, n, h, flow, wet) in enumerate(panels):
        size, sp = info["size"], info["size"] / (n - 1)
        lo = min(h)
        x0 = gap + col * (px + gap)
        text(img, x0, 6, label[:px // 12], (255, 255, 255))
        text(img, x0, 24, "%.0f M CELLS, %.0f-%.0f M" % (sp, lo, max(h)), (190, 190, 190), 1)
        gx = [0.0] * (n * n)
        gz = [0.0] * (n * n)
        for j in range(n):
            for i in range(n):
                a, b = h[j * n + max(i - 1, 0)], h[j * n + min(i + 1, n - 1)]
                c, e = h[max(j - 1, 0) * n + i], h[min(j + 1, n - 1) * n + i]
                gx[j * n + i] = (b - a) / ((min(i + 1, n - 1) - max(i - 1, 0)) * sp)
                gz[j * n + i] = (e - c) / ((min(j + 1, n - 1) - max(j - 1, 0)) * sp)
        sh, sgx, sgz = grid_sampler(h, n, size), grid_sampler(gx, n, size), grid_sampler(gz, n, size)
        sf = grid_sampler(flow, n, size) if flow else None
        for y in range(px):
            zc = -size / 2 + (y + 0.5) / px * size
            r1, r2, r3 = img[head + y], img[head + px + gap + y], img[head + 2 * (px + gap) + y]
            for x in range(px):
                xc = -size / 2 + (x + 0.5) / px * size
                ax, az = sgx(xc, zc), sgz(xc, zc)
                nl = math.sqrt(ax * ax + az * az + 1.0)
                shade = min(1.3, 0.12 + 0.88 * max(0.0, (-ax * sun[0] + sun[1] - az * sun[2]) / nl) / sun[1])
                ci = min(int((xc + size / 2) / sp + 0.5), n - 1) + min(int((zc + size / 2) / sp + 0.5), n - 1) * n
                lake = wet is not None and wet[ci] == 1.0
                base = (40, 90, 210) if lake else ramp((sh(xc, zc) - lo) / max(relief, 1e-3))
                r1[(x0 + x) * 3:(x0 + x) * 3 + 3] = bytes(int(min(255, base[k] * shade)) for k in range(3))
                deg = math.degrees(math.atan(nl * 0 + math.sqrt(ax * ax + az * az)))
                sc = (40, 90, 210) if lake else next(c for lim, c in SLOPE if deg < lim)
                r2[(x0 + x) * 3:(x0 + x) * 3 + 3] = bytes(sc)
                if sf:
                    fl = math.log(max(sf(xc, zc), 1.0)) / maxflow
                    fv = int(min(255, 30 + 225 * max(fl, 0.0) ** 3))
                    r3[(x0 + x) * 3:(x0 + x) * 3 + 3] = bytes((40, 90, 210) if lake else (fv, fv, fv))
        bar = int(px * 1000.0 / size)   # 1 km
        yb = head + 3 * (px + gap) + 4
        for yy in range(yb, yb + 5):
            img[yy][x0 * 3:(x0 + bar) * 3] = b"\xff\xff\xff" * bar
        text(img, x0 + bar + 6, yb - 1, "1 KM", (255, 255, 255), 1)
    yl = head + 3 * (px + gap) + 16
    text(img, gap, yl, "ROWS: HILLSHADE (COMMON RELIEF %.0f M) / SLOPE / DRAINAGE AREA (LOG). NORTH UP." % relief, (200, 200, 200), 1)
    x = gap
    for lim, c in SLOPE:
        for yy in range(yl + 14, yl + 24):
            img[yy][x * 3:(x + 14) * 3] = bytes(c) * 14
        lab = ("<%d" % lim) if lim < 999 else ">50"
        text(img, x + 18, yl + 15, lab, (200, 200, 200), 1)
        x += 60
    text(img, x + 4, yl + 15, "DEG", (200, 200, 200), 1)
    write_png(out, W, H, img)
    print("wrote %s (%d x %d, %.1f m/px)" % (out, W, H, panels[0][1]["size"] / px))


# the measures of the probe's dem command (<tag>_relief.json) as markdown rows, and the closest of ours to each real site
MOUNTAIN = [("mountain", "montaña %", 0), ("m_slope_lt3", "< 3°", 1), ("m_slope_3_14", "3–14°", 1), ("m_slope_14_30", "14–30°", 1),
            ("m_slope_30_40", "30–40°", 1), ("m_slope_gt40", "> 40°", 1), ("m_slope_gt50", "> 50°", 2)]
COLUMNS = MOUNTAIN + [("slope_lt3", "< 3°", 1), ("slope_3_14", "3–14°", 1), ("slope_14_30", "14–30°", 1), ("slope_30_40", "30–40°", 1), ("slope_gt40", "> 40°", 1),
           ("slope_gt50", "> 50°", 2), ("pits_per_1000", "hoyos ‰", 2), ("land_to_pit", "seco a un hoyo %", 1), ("strahler", "Strahler sonda", 0),
           ("confluences", "confl. sonda", 0), ("strahler_e1", "Strahler E1", 0), ("confluences_e1", "confl. E1", 0), ("drainage_density", "km/km²", 2),
           ("floor_flat", "fondos < 3° %", 1), ("hand_30", "HAND 30 m", 1), ("hand_60", "HAND 60 m", 1), ("hand_120", "HAND 120 m", 1),
           ("hand_240", "HAND 240 m", 1), ("hand_trunk_60", "tronco 60 m", 1), ("hand_trunk_120", "tronco 120 m", 1), ("floor_width_2", "fondo ±2 m", 0),
           ("floor_width_5", "fondo ±5 m", 0), ("hand_p90", "HAND p90", 0)]
# the shape of the relief, not its size: the slopes of the mountain part (most sites are all mountain) and the profile across the channels (relative to the HAND at 240 m), the network
SHAPE = [("m_slope_3_14", 30.0), ("m_slope_14_30", 30.0), ("m_slope_30_40", 15.0), ("m_slope_gt40", 15.0), ("drainage_density", 2.0),
         ("hand_60", 20.0), ("hand_120", 40.0), ("hand_240", 80.0), ("floor_width_5", 40.0)]


def relief(base):
    return json.load(open(base + "_relief.json"))


def table(args):
    keys = [c for c in COLUMNS if not opt(args, "--only", None) or c[0] in opt(args, "--only", "").split(",")]
    rows = [a.split("=", 1) for a in args if "=" in a and not a.startswith("--")]
    print("| | " + " | ".join(k[1] for k in keys) + " |")
    print("|---" * (len(keys) + 1) + "|")
    for label, base in rows:
        r = relief(base)
        print("| %s | " % label + " | ".join(("%%.%df" % k[2]) % r[k[0]] for k in keys).replace(".", ",") + " |")


def closest(args):
    real = [a.split("=", 1) for a in args[0].split(";")]
    ours = [a.split("=", 1) for a in args[1].split(";")]
    for label, base in real:
        r = relief(base)
        ds = []
        for ol, ob in ours:
            o = relief(ob)
            ds.append((math.sqrt(sum(((r[k] - o[k]) / sc) ** 2 for k, sc in SHAPE) / len(SHAPE)), ol))
        ds.sort()
        print("%s: " % label + ", ".join("%s %.2f" % (l, d) for d, l in ds))


def profile(args):
    """The cross-section of the main valley: the channel cells draining over `frac` of the largest area that is not on the border (the
    probe's dem command writes <tag>_flow.f32), away from the border; across each one (perpendicular to the line through the main cells within
    90 m), the ground every half cell out to `reach` m on both sides, relative to the channel; the median of the two sides' mean."""
    base = args[0]
    frac = float(opt(args, "--frac", 0.3))
    reach = float(opt(args, "--reach", 600.0))
    info = json.load(open(base + "_dem.json"))
    n = int(info["n"])
    size = info["size"]
    sp = size / (n - 1)
    h = read_f32(base + "_height.f32", n * n)
    a = read_f32(base + "_flow.f32", n * n)
    margin = int(reach / sp) // 2 + 2
    inner = [c for c in range(n * n) if margin <= c % n < n - margin and margin <= c // n < n - margin]
    if "--mountain" in args:   # only the channels in the mountain part (the dem command's rule: > 100 m of relief within +-300 m)
        r = max(1, int(round(300.0 / sp)))

        def relief(c):
            i, j = c % n, c // n
            v = [h[jj * n + ii] for jj in range(max(0, j - r), min(n, j + r + 1)) for ii in range(max(0, i - r), min(n, i + r + 1))]
            return max(v) - min(v)
        big = sorted(inner, key=lambda c: -a[c])[:max(1, len(inner) // 20)]
        inner = [c for c in big if relief(c) >= 100.0]
    top = max(a[c] for c in inner)
    main = [c for c in inner if a[c] >= frac * top]
    pos = {c: (-size / 2 + (c % n) * sp, -size / 2 + (c // n) * sp) for c in main}
    sh = grid_sampler(h, n, size)
    dists = [k * 30.0 for k in range(1, int(reach / 30.0) + 1)]
    prof = [[] for _ in dists]
    for c in main:
        x, z = pos[c]
        near = [pos[m] for m in main if (pos[m][0] - x) ** 2 + (pos[m][1] - z) ** 2 <= 90.0 ** 2]
        mx, mz = sum(p[0] for p in near) / len(near), sum(p[1] for p in near) / len(near)
        sxx = sum((p[0] - mx) ** 2 for p in near)
        szz = sum((p[1] - mz) ** 2 for p in near)
        sxz = sum((p[0] - mx) * (p[1] - mz) for p in near)
        ang = 0.5 * math.atan2(2 * sxz, sxx - szz)   # the main direction of the channel
        px, pz = -math.sin(ang), math.cos(ang)
        h0 = h[c]
        for k, d in enumerate(dists):
            prof[k].append(0.5 * (sh(x + px * d, z + pz * d) + sh(x - px * d, z - pz * d)) - h0)
    med = [sorted(v)[len(v) // 2] for v in prof]
    last = med[-1] if med[-1] > 0 else 1.0
    print("%s: %d main channel cells (area >= %.2f km2), median cross-section (m above the channel):" % (base, len(main), frac * top / 1e6))
    print("  " + ", ".join("%.0f m: %.0f" % (d, v) for d, v in zip(dists, med) if d in (30, 60, 90, 120, 180, 240, 360, 480, 600)))
    i120, i240 = dists.index(120.0), dists.index(240.0)
    print("  at 120 m / at %.0f m = %.2f, at 240 m / at %.0f m = %.2f (a straight V: 0.20 and 0.40; a flat floor lowers them)" % (reach, med[i120] / last, reach, med[i240] / last))


def opt(args, name, default):
    return args[args.index(name) + 1] if name in args else default


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    cmd, args = sys.argv[1], sys.argv[2:]
    for a in args:   # --site id=lat,lon overrides a site's centre (to look around before fixing it in SITES)
        if a.startswith("--at="):
            sid, ll = a[5:].split("=")
            la, lo = (float(v) for v in ll.split(","))
            SITES[sid] = (la, lo) + SITES[sid][2:]
    args = [a for a in args if not a.startswith("--at=")]
    {"fetch": fetch, "crop": crop, "resample": resample, "resolution": resolution, "sheet": sheet, "table": table, "closest": closest, "profile": profile}[cmd](args)


if __name__ == "__main__":
    main()
