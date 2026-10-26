#!/usr/bin/env python3
"""Generates tests/data/test_scene.glb, a tiny hand-built glTF 2.0 binary used by tests/gltf_test.cpp.

Contents: a node hierarchy (parent translated, child scaled), an indexed cube WITHOUT normals/tangents
(the loader must generate them) with a material that has an embedded PNG base color texture, MASK alpha
and double sided; plus a non-indexed triangle with no material (glTF default material).
Pure standard library; run from anywhere:  python3 tools/make_test_gltf.py
"""
import json, struct, zlib, pathlib

def png(width, height, rgba):
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + bytes(rgba) * width for _ in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))

# --- cube: 24 vertices (4 per face), positions + UV, 36 u16 indices
faces = [  # normal, u axis, v axis
    ((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
    ((0, 1, 0), (1, 0, 0), (0, 0, -1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
    ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0))]
pos, uv, idx = [], [], []
for n, u, v in faces:
    base = len(pos)
    for su, sv, tu, tv in ((-1, -1, 0, 1), (1, -1, 1, 1), (1, 1, 1, 0), (-1, 1, 0, 0)):
        pos.append(tuple(0.5 * (n[i] + su * u[i] + sv * v[i]) for i in range(3)))
        uv.append((tu, tv))
    idx += [base, base + 1, base + 2, base, base + 2, base + 3]

tri_pos = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]

blob = bytearray()
views, accessors = [], []
def add_view(data, target=None):
    while len(blob) % 4: blob.append(0)
    views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), **({"target": target} if target else {})})
    blob.extend(data)
    return len(views) - 1
def add_acc(view, ctype, count, atype, mn=None, mx=None):
    a = {"bufferView": view, "componentType": ctype, "count": count, "type": atype}
    if mn is not None: a["min"], a["max"] = list(mn), list(mx)
    accessors.append(a)
    return len(accessors) - 1

def bounds(points):
    return [min(p[i] for p in points) for i in range(3)], [max(p[i] for p in points) for i in range(3)]

a_pos = add_acc(add_view(b"".join(struct.pack("<3f", *p) for p in pos), 34962), 5126, len(pos), "VEC3", *bounds(pos))
a_uv = add_acc(add_view(b"".join(struct.pack("<2f", *t) for t in uv), 34962), 5126, len(uv), "VEC2")
a_idx = add_acc(add_view(struct.pack("<%dH" % len(idx), *idx), 34963), 5123, len(idx), "SCALAR")
a_tri = add_acc(add_view(b"".join(struct.pack("<3f", *p) for p in tri_pos), 34962), 5126, 3, "VEC3", *bounds(tri_pos))
img_view = add_view(png(2, 2, (200, 40, 40, 255)))

gltf = {
    "asset": {"version": "2.0", "generator": "tools/make_test_gltf.py"},
    "scene": 0, "scenes": [{"nodes": [0, 2]}],
    "nodes": [
        {"name": "Parent", "translation": [1, 2, 3], "children": [1]},
        {"name": "Cube", "mesh": 0, "translation": [0, 1, 0], "scale": [2, 2, 2]},
        {"name": "Triangle", "mesh": 1, "translation": [-5, 0, 0]}],
    "meshes": [
        {"primitives": [{"attributes": {"POSITION": a_pos, "TEXCOORD_0": a_uv}, "indices": a_idx, "material": 0}]},
        {"primitives": [{"attributes": {"POSITION": a_tri}}]}],
    "materials": [{"name": "RedMask", "doubleSided": True, "alphaMode": "MASK", "alphaCutoff": 0.3,
                   "pbrMetallicRoughness": {"baseColorFactor": [1, 0.5, 0.25, 0.8], "metallicFactor": 0.25,
                                            "roughnessFactor": 0.75, "baseColorTexture": {"index": 0}},
                   "emissiveFactor": [0.1, 0.2, 0.3]}],
    "textures": [{"source": 0}],
    "images": [{"bufferView": img_view, "mimeType": "image/png"}],
    "bufferViews": views, "accessors": accessors,
    "buffers": [{"byteLength": len(blob)}],
}

js = json.dumps(gltf, separators=(",", ":")).encode()
js += b" " * (-len(js) % 4)
blob += b"\x00" * (-len(blob) % 4)
glb = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(blob))
glb += struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(blob), 0x004E4942) + bytes(blob)

out = pathlib.Path(__file__).resolve().parent.parent / "tests" / "data" / "test_scene.glb"
out.write_bytes(glb)
print("wrote", out, len(glb), "bytes")
