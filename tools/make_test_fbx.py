#!/usr/bin/env python3
"""Writes the small ASCII FBX files used by tests/fbx_test.cpp (no external tools needed).

  tests/data/test_rig.fbx   a Mixamo-named skeleton (centimeters, Y up) with a one-second "walk" take:
                            the hips travel 120 cm along +Z and the left hip swings.
  tests/data/test_clip.fbx  the same skeleton with a different take ("run": 300 cm in 0.5 s) plus a bone
                            the rig does not have.

Run from the repository root:  python3 tools/make_test_fbx.py
"""
import os

KTIME = 46186158000  # FBX time units per second

BONES = [  # name, parent, translation in centimeters
    ("mixamorig:Hips", None, (0, 100, 0)),
    ("mixamorig:Spine", "mixamorig:Hips", (0, 10, 0)),
    ("mixamorig:Head", "mixamorig:Spine", (0, 50, 0)),
    ("mixamorig:LeftUpLeg", "mixamorig:Hips", (10, -5, 0)),
    ("mixamorig:LeftFoot", "mixamorig:LeftUpLeg", (0, -90, 0)),
    ("mixamorig:RightUpLeg", "mixamorig:Hips", (-10, -5, 0)),
    ("mixamorig:RightFoot", "mixamorig:RightUpLeg", (0, -90, 0)),
]


def header():
    return """; FBX 7.4.0 project file
FBXHeaderExtension:  {
\tFBXHeaderVersion: 1003
\tFBXVersion: 7400
\tCreator: "GB Engine test generator"
}
GlobalSettings:  {
\tVersion: 1000
\tProperties70:  {
\t\tP: "UpAxis", "int", "Integer", "",1
\t\tP: "UpAxisSign", "int", "Integer", "",1
\t\tP: "FrontAxis", "int", "Integer", "",2
\t\tP: "FrontAxisSign", "int", "Integer", "",1
\t\tP: "CoordAxis", "int", "Integer", "",0
\t\tP: "CoordAxisSign", "int", "Integer", "",1
\t\tP: "OriginalUpAxis", "int", "Integer", "",1
\t\tP: "OriginalUpAxisSign", "int", "Integer", "",1
\t\tP: "UnitScaleFactor", "double", "Number", "",1
\t\tP: "OriginalUnitScaleFactor", "double", "Number", "",1
\t\tP: "TimeMode", "enum", "", "",6
\t}
}
"""


def build(bones, takes, extra_bones=()):
    ids = {}
    next_id = [1000]

    def new_id():
        next_id[0] += 1
        return next_id[0]

    objects, connections = [], []
    all_bones = list(bones) + list(extra_bones)
    for name, parent, t in all_bones:
        ids[name] = new_id()
        objects.append(
            f'\tModel: {ids[name]}, "Model::{name}", "LimbNode" {{\n\t\tVersion: 232\n\t\tProperties70:  {{\n'
            f'\t\t\tP: "Lcl Translation", "Lcl Translation", "", "A",{t[0]},{t[1]},{t[2]}\n\t\t}}\n\t\tShading: Y\n\t\tCulling: "CullingOff"\n\t}}')
        connections.append(f'\tC: "OO",{ids[name]},{ids[parent] if parent else 0}')

    for take_name, seconds, anims in takes:
        stack, layer = new_id(), new_id()
        objects.append(
            f'\tAnimationStack: {stack}, "AnimStack::{take_name}", "" {{\n\t\tProperties70:  {{\n'
            f'\t\t\tP: "LocalStart", "KTime", "Time", "",0\n\t\t\tP: "LocalStop", "KTime", "Time", "",{int(seconds * KTIME)}\n'
            f'\t\t\tP: "ReferenceStart", "KTime", "Time", "",0\n\t\t\tP: "ReferenceStop", "KTime", "Time", "",{int(seconds * KTIME)}\n\t\t}}\n\t}}')
        objects.append(f'\tAnimationLayer: {layer}, "AnimLayer::BaseLayer", "" {{\n\t}}')
        connections.append(f'\tC: "OO",{layer},{stack}')
        for bone, axis, keys in anims:   # keys: [(seconds, value)] for one translation axis
            node = new_id()
            rest = next(b[2] for b in all_bones if b[0] == bone)   # channels without a curve keep the bone's own value
            objects.append(
                f'\tAnimationCurveNode: {node}, "AnimCurveNode::T", "" {{\n\t\tProperties70:  {{\n'
                f'\t\t\tP: "d|X", "Number", "", "A",{rest[0]}\n\t\t\tP: "d|Y", "Number", "", "A",{rest[1]}\n\t\t\tP: "d|Z", "Number", "", "A",{rest[2]}\n\t\t}}\n\t}}')
            connections.append(f'\tC: "OO",{node},{layer}')
            connections.append(f'\tC: "OP",{node},{ids[bone]}, "Lcl Translation"')
            curve = new_id()
            times = ",".join(str(int(k[0] * KTIME)) for k in keys)
            values = ",".join(repr(float(k[1])) for k in keys)
            objects.append(
                f'\tAnimationCurve: {curve}, "AnimCurve::", "" {{\n\t\tDefault: 0\n\t\tKeyVer: 4008\n'
                f'\t\tKeyTime: *{len(keys)} {{\n\t\t\ta: {times}\n\t\t}}\n\t\tKeyValueFloat: *{len(keys)} {{\n\t\t\ta: {values}\n\t\t}}\n'
                f'\t\tKeyAttrFlags: *1 {{\n\t\t\ta: 24836\n\t\t}}\n\t\tKeyAttrDataFloat: *4 {{\n\t\t\ta: 0,0,218434821,0\n\t\t}}\n'
                f'\t\tKeyAttrRefCount: *1 {{\n\t\t\ta: {len(keys)}\n\t\t}}\n\t}}')
            connections.append(f'\tC: "OP",{curve},{node}, "d|{axis}"')

    return header() + "Objects:  {\n" + "\n".join(objects) + "\n}\nConnections:  {\n" + "\n".join(connections) + "\n}\n"


def main():
    out = os.path.join(os.path.dirname(__file__), "..", "tests", "data")
    walk = [("walk", 1.0, [
        ("mixamorig:Hips", "Z", [(0.0, 0.0), (1.0, 120.0)]),
        ("mixamorig:LeftFoot", "Y", [(0.0, -90.0), (0.5, -80.0), (1.0, -90.0)]),
    ])]
    with open(os.path.join(out, "test_rig.fbx"), "w", newline="\n") as f:
        f.write(build(BONES, walk))
    sword = ("Prop:Sword", "mixamorig:Hips", (5, 0, 0))   # a bone the rig does not have
    run = [("run", 0.5, [
        ("mixamorig:Hips", "Z", [(0.0, 0.0), (0.5, 150.0)]),
        ("Prop:Sword", "X", [(0.0, 5.0), (0.5, 9.0)]),
    ])]
    with open(os.path.join(out, "test_clip.fbx"), "w", newline="\n") as f:
        f.write(build(BONES, run, extra_bones=[sword]))


if __name__ == "__main__":
    main()
