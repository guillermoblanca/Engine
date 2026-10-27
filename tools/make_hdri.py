#!/usr/bin/env python3
"""Writes a small procedural HDR sky (Radiance .hdr, equirectangular) for image based lighting.

  python3 tools/make_hdri.py assets/hdri/sunset_sky.hdr [--sun-elevation 12] [--width 512]
"""
import argparse
import math
import numpy as np


def sky(w, h, sun_elev_deg, sun_az_deg=40.0):
    u = (np.arange(w) + 0.5) / w
    v = (np.arange(h) + 0.5) / h
    az = (u - 0.5) * 2 * math.pi
    pol = v * math.pi                       # 0 at the zenith
    azg, polg = np.meshgrid(az, pol)
    d = np.stack([np.sin(polg) * np.cos(azg), np.cos(polg), np.sin(polg) * np.sin(azg)], axis=-1)
    elev = np.radians(sun_elev_deg)
    az0 = math.radians(sun_az_deg)
    sun = np.array([math.cos(elev) * math.cos(az0), math.sin(elev), math.cos(elev) * math.sin(az0)])
    y = d[..., 1]
    top = np.array([0.10, 0.22, 0.55]); hor = np.array([0.95, 0.55, 0.32]); gnd = np.array([0.10, 0.09, 0.08])
    up = hor + (top - hor) * np.clip(y, 0, 1)[..., None] ** 0.45
    down = hor * 0.35 + (gnd - hor * 0.35) * np.clip(-y, 0, 1)[..., None] ** 0.4
    col = np.where((y >= 0)[..., None], up, down)
    cs = np.clip(d @ sun, 0, 1)
    col += np.array([1.0, 0.6, 0.3]) * (cs ** 8)[..., None] * 1.2       # glow around the sun
    col += np.array([1.0, 0.85, 0.6]) * (cs ** 900)[..., None] * 400.0   # the disc, far above 1.0 (HDR)
    return col.astype(np.float32)


def rgbe(img):
    m = img.max(axis=-1)
    e = np.where(m > 1e-32, np.floor(np.log2(np.maximum(m, 1e-32))) + 1, 0).astype(np.int32)
    scale = np.where(m > 1e-32, 256.0 / np.power(2.0, e), 0.0)
    rgb = np.clip(img * scale[..., None], 0, 255).astype(np.uint8)
    return np.dstack([rgb, np.clip(e + 128, 0, 255).astype(np.uint8)])


def write_hdr(path, img):
    h, w, _ = img.shape
    data = rgbe(img)
    with open(path, 'wb') as f:
        f.write(b'#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n')
        f.write(f'-Y {h} +X {w}\n'.encode())
        for row in data:
            f.write(bytes([2, 2, w >> 8, w & 255]))
            for c in range(4):
                ch = row[:, c].tobytes()
                i = 0
                while i < w:                      # literal runs only (valid RLE, no compression)
                    n = min(128, w - i)
                    f.write(bytes([n]) + ch[i:i + n])
                    i += n


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--sun-elevation', type=float, default=12.0)
    ap.add_argument('--width', type=int, default=512)
    a = ap.parse_args()
    write_hdr(a.out, sky(a.width, a.width // 2, a.sun_elevation))
