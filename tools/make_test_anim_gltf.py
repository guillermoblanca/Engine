#!/usr/bin/env python3
"""Generates tests/data/test_anim.glb, used by tests/animation_test.cpp.

Nodes 4/5 are cameras (perspective "Main", orthographic "Top"); clip "Dolly" moves camera node 4.
Nodes: 0 "Mover" (animated by clips Move/Spin/Steps/Cubic), 1 "Root" -> 2 "Tip" (two joints, Tip is at
y=1 above Root), 3 "Bar" (skinned mesh, deliberately translated by (100,0,0) because glTF says the
transform of a skinned mesh node must be ignored). Clip "Bend" rotates Tip 90 degrees about Z.
The Bar is a 0.4 wide, 2 tall strip: bottom bound to Root, middle half/half, top to Tip.
Pure standard library:  python3 tools/make_test_anim_gltf.py
"""
import json, math, pathlib, struct

blob = bytearray()
views, accessors = [], []

def add_view(data, target=None):
    while len(blob) % 4: blob.append(0)
    v = {"buffer": 0, "byteOffset": len(blob), "byteLength": len(data)}
    if target: v["target"] = target
    views.append(v); blob.extend(data)
    return len(views) - 1

def add_acc(data, ctype, atype, count, target=None, mn=None, mx=None):
    a = {"bufferView": add_view(data, target), "componentType": ctype, "count": count, "type": atype}
    if mn is not None: a["min"], a["max"] = mn, mx
    accessors.append(a)
    return len(accessors) - 1

def floats(*v): return struct.pack("<%df" % len(v), *v)

# ---- skinned bar: 6 vertices, 2 triangles per segment
pos = [(-0.2, 0, 0), (0.2, 0, 0), (-0.2, 1, 0), (0.2, 1, 0), (-0.2, 2, 0), (0.2, 2, 0)]
joints = [(0, 0, 0, 0), (0, 0, 0, 0), (0, 1, 0, 0), (0, 1, 0, 0), (1, 0, 0, 0), (1, 0, 0, 0)]
weights = [(1, 0, 0, 0), (1, 0, 0, 0), (.5, .5, 0, 0), (.5, .5, 0, 0), (1, 0, 0, 0), (1, 0, 0, 0)]
idx = [0, 1, 3, 0, 3, 2, 2, 3, 5, 2, 5, 4]
a_pos = add_acc(b"".join(floats(*p) for p in pos), 5126, "VEC3", 6, 34962, [-0.2, 0, 0], [0.2, 2, 0])
a_joint = add_acc(b"".join(bytes(j) for j in joints), 5121, "VEC4", 6, 34962)
a_weight = add_acc(b"".join(floats(*w) for w in weights), 5126, "VEC4", 6, 34962)
a_idx = add_acc(struct.pack("<12H", *idx), 5123, "SCALAR", 12, 34963)
identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
inv_tip = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1]   # translate(0,-1,0), column-major
a_ibm = add_acc(floats(*identity, *inv_tip), 5126, "MAT4", 2)

# ---- animations
def sampler(times, values, interp="LINEAR", comps=3):
    t = add_acc(floats(*times), 5126, "SCALAR", len(times), None, [min(times)], [max(times)])
    count = len(values) // comps
    o = add_acc(floats(*values), 5126, "VEC3" if comps == 3 else "VEC4", count)
    return {"input": t, "output": o, "interpolation": interp}

s45 = math.sin(math.pi / 4)
clips = [
    ("Move", [sampler([0, 1, 2], [0, 0, 0, 2, 0, 0, 2, 4, 0])], [("translation", 0)]),
    ("Spin", [sampler([0, 1], [0, 0, 0, 1, 0, s45, 0, s45], "LINEAR", 4)], [("rotation", 0)]),
    ("Steps", [sampler([0, 1], [0, 0, 0, 5, 0, 0], "STEP")], [("translation", 0)]),
    # per key: in-tangent, value, out-tangent; zero tangents: Hermite reduces to smoothstep 3t^2 - 2t^3 between (0,0,0) and (10,0,0)
    ("Cubic", [sampler([0, 1], [0, 0, 0,  0, 0, 0,  0, 0, 0,   0, 0, 0,  10, 0, 0,  0, 0, 0], "CUBICSPLINE")], [("translation", 0)]),
    ("Bend", [sampler([0, 1], [0, 0, 0, 1, 0, 0, s45, s45], "LINEAR", 4)], [("rotation", 2)]),
    ("Dolly", [sampler([0, 1], [0, 1, 5, 0, 1, 3])], [("translation", 4)]),
]
animations = []
for name, samplers, targets in clips:
    animations.append({"name": name, "samplers": samplers,
                       "channels": [{"sampler": i, "target": {"node": node, "path": path}} for i, (path, node) in enumerate(targets)]})

gltf = {
    "asset": {"version": "2.0", "generator": "tools/make_test_anim_gltf.py"},
    "scene": 0, "scenes": [{"nodes": [0, 1, 3, 4, 5]}],
    "nodes": [
        {"name": "Mover"},
        {"name": "Root", "children": [2]},
        {"name": "Tip", "translation": [0, 1, 0]},
        {"name": "Bar", "mesh": 0, "skin": 0, "translation": [100, 0, 0]},
        {"name": "CamNode", "camera": 0, "translation": [0, 1, 5]},
        {"name": "OrthoNode", "camera": 1, "translation": [0, 1, 20]}],
    "meshes": [{"primitives": [{"attributes": {"POSITION": a_pos, "JOINTS_0": a_joint, "WEIGHTS_0": a_weight},
                                "indices": a_idx, "material": 0}]}],
    "materials": [{"doubleSided": True, "pbrMetallicRoughness": {"baseColorFactor": [1, 0, 0, 1], "metallicFactor": 0, "roughnessFactor": 0.8}}],
    "skins": [{"joints": [1, 2], "inverseBindMatrices": a_ibm}],
    "cameras": [
        {"name": "Main", "type": "perspective", "perspective": {"yfov": 0.5, "znear": 0.25, "zfar": 80}},
        {"name": "Top", "type": "orthographic", "orthographic": {"xmag": 6, "ymag": 3, "znear": 1, "zfar": 90}}],
    "animations": animations,
    "bufferViews": views, "accessors": accessors, "buffers": [{"byteLength": len(blob)}],
}
js = json.dumps(gltf, separators=(",", ":")).encode()
js += b" " * (-len(js) % 4)
blob += b"\x00" * (-len(blob) % 4)
glb = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(blob))
glb += struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(blob), 0x004E4942) + bytes(blob)
out = pathlib.Path(__file__).resolve().parent.parent / "tests" / "data" / "test_anim.glb"
out.write_bytes(glb)
print("wrote", out, len(glb), "bytes")
