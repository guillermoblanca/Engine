#!/usr/bin/env python3
"""Adds a demo camera rig to a .glb, like cameras and camera animations exported from Blender.

  python3 tools/add_camera_rig.py in.glb out.glb [radius]

Adds three cameras and two animations (the model is assumed to be centered at the origin):
  "Turntable"  perspective camera on a rig node that does a full 360 degree turn in 6 s  (clip "Turntable")
  "Dolly"      perspective camera that moves in on the model in 6 s                      (clip "Dolly")
  "Top"        orthographic camera looking straight down                                 (static)
Pure standard library.
"""
import json, math, struct, sys

def read_glb(path):
    d = open(path, "rb").read()
    assert d[:4] == b"glTF"
    off, js, binary = 12, None, b""
    while off < len(d):
        length, kind = struct.unpack("<II", d[off:off + 8])
        chunk = d[off + 8:off + 8 + length]
        if kind == 0x4E4F534A: js = json.loads(chunk)
        elif kind == 0x004E4942: binary = chunk
        off += 8 + length
    return js, bytearray(binary)

def write_glb(path, js, binary):
    text = json.dumps(js, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    binary += b"\x00" * (-len(binary) % 4)
    out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(binary))
    out += struct.pack("<II", len(text), 0x4E4F534A) + text + struct.pack("<II", len(binary), 0x004E4942) + bytes(binary)
    open(path, "wb").write(out)

def main():
    src, dst = sys.argv[1], sys.argv[2]
    radius = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
    js, binary = read_glb(src)

    def add_acc(values, comps, atype, mn=None, mx=None):
        data = struct.pack("<%df" % len(values), *values)
        while len(binary) % 4: binary.append(0)
        js["bufferViews"].append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(data)})
        binary.extend(data)
        acc = {"bufferView": len(js["bufferViews"]) - 1, "componentType": 5126, "count": len(values) // comps, "type": atype}
        if mn is not None: acc["min"], acc["max"] = mn, mx
        js["accessors"].append(acc)
        return len(js["accessors"]) - 1

    def sampler(times, values, comps):
        t = add_acc(times, 1, "SCALAR", [min(times)], [max(times)])
        v = add_acc(values, comps, "VEC3" if comps == 3 else "VEC4")
        return {"input": t, "output": v, "interpolation": "LINEAR"}

    js.setdefault("cameras", [])
    js["cameras"] += [
        {"name": "Turntable", "type": "perspective", "perspective": {"yfov": math.radians(35), "znear": 0.05, "zfar": 100}},
        {"name": "Dolly", "type": "perspective", "perspective": {"yfov": math.radians(32), "znear": 0.05, "zfar": 100}},
        {"name": "Top", "type": "orthographic", "orthographic": {"xmag": radius * 1.6, "ymag": radius * 1.6, "znear": 0.05, "zfar": 100}}]
    first = len(js["cameras"]) - 3

    n0 = len(js["nodes"])
    s = math.sin(-math.pi / 4)
    js["nodes"] += [
        {"name": "TurntableRig", "children": [n0 + 1]},
        {"name": "TurntableCam", "camera": first, "translation": [0, radius * 0.35, radius * 3.4]},
        {"name": "DollyCam", "camera": first + 1, "translation": [radius * 0.6, radius * 0.4, radius * 6.0]},
        {"name": "TopCam", "camera": first + 2, "translation": [0, radius * 5.0, 0], "rotation": [s, 0, 0, math.cos(-math.pi / 4)]}]

    quats, times = [], [0, 1.5, 3, 4.5, 6]
    for a in (0, 90, 180, 270, 360):
        h = math.radians(a) / 2
        quats += [0, math.sin(h), 0, math.cos(h)]
    dolly = sampler([0, 6], [radius * 0.6, radius * 0.4, radius * 6.0, radius * 0.2, radius * 0.1, radius * 2.6], 3)
    js.setdefault("animations", [])
    js["animations"] += [
        {"name": "Turntable", "samplers": [sampler(times, quats, 4)], "channels": [{"sampler": 0, "target": {"node": n0, "path": "rotation"}}]},
        {"name": "Dolly", "samplers": [dolly], "channels": [{"sampler": 0, "target": {"node": n0 + 2, "path": "translation"}}]}]

    scene = js["scenes"][js.get("scene", 0)]
    scene["nodes"] += [n0, n0 + 2, n0 + 3]
    js["buffers"][0]["byteLength"] = len(binary) + (-len(binary) % 4)
    write_glb(dst, js, binary)
    print("wrote", dst)

main()
