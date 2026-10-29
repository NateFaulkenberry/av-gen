#!/usr/bin/env python3
"""The glTF glass test fixture (ADR-1819), built in-tree from nothing: no downloaded asset.

    python3 tools/make_glass_test_glb.py --out <dir>

Writes two binary glTF files into <dir>:
  glass.glb  a glass sphere (KHR_materials_transmission 1.0, KHR_materials_ior 1.5, KHR_materials_volume with a
             blue-green attenuation) in front of a backdrop of red and white vertical stripes, so refraction shows as
             bent stripes and absorption as a tint;
  plain.glb  the same scene with the sphere's material carrying no extension at all (the control a model without
             these extensions is compared against);
  glass-blend.glb  the glass exported as exporters do for viewers without transmission: alphaMode BLEND, alpha 0.25.

Standard library only (struct, json, math), so it runs wherever the build does.
"""
import argparse
import json
import math
import os
import struct


def sphere(radius, rings, segments, centre):
    pos, nrm, idx = [], [], []
    for r in range(rings + 1):
        theta = math.pi * r / rings
        for s in range(segments + 1):
            phi = 2.0 * math.pi * s / segments
            n = (math.sin(theta) * math.cos(phi), math.cos(theta), math.sin(theta) * math.sin(phi))
            nrm.append(n)
            pos.append(tuple(centre[i] + radius * n[i] for i in range(3)))
    for r in range(rings):
        for s in range(segments):
            a = r * (segments + 1) + s
            b = a + segments + 1
            idx += [a, a + 1, b, a + 1, b + 1, b]
    return pos, nrm, idx


def quad(x0, x1, y0, y1, z):
    pos = [(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)]
    nrm = [(0.0, 0.0, 1.0)] * 4
    return pos, nrm, [0, 1, 2, 0, 2, 3]


def build(glass_extensions, blend_fallback=False):
    meshes_data = []  # (positions, normals, indices, material index)
    meshes_data.append(sphere(1.0, 48, 96, (0.0, 1.0, 0.0)) + (0,))
    # the backdrop: twelve vertical stripes, alternating red and white, 2.5 m behind the sphere
    for i in range(12):
        x0 = -3.0 + i * 0.5
        meshes_data.append(quad(x0, x0 + 0.5, -0.5, 3.0, -2.5) + (1 if i % 2 == 0 else 2,))

    blob = bytearray()
    views, accessors, meshes, nodes = [], [], [], []

    def add_view(data, target):
        while len(blob) % 4:
            blob.append(0)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), "target": target})
        blob.extend(data)
        return len(views) - 1

    for pos, nrm, idx, mat in meshes_data:
        p = add_view(b"".join(struct.pack("<3f", *v) for v in pos), 34962)
        n = add_view(b"".join(struct.pack("<3f", *v) for v in nrm), 34962)
        i = add_view(b"".join(struct.pack("<I", k) for k in idx), 34963)
        lo = [min(v[c] for v in pos) for c in range(3)]
        hi = [max(v[c] for v in pos) for c in range(3)]
        accessors.append({"bufferView": p, "componentType": 5126, "count": len(pos), "type": "VEC3", "min": lo,
                          "max": hi})
        accessors.append({"bufferView": n, "componentType": 5126, "count": len(nrm), "type": "VEC3"})
        accessors.append({"bufferView": i, "componentType": 5125, "count": len(idx), "type": "SCALAR"})
        a = len(accessors)
        meshes.append({"primitives": [{"attributes": {"POSITION": a - 3, "NORMAL": a - 2}, "indices": a - 1,
                                       "material": mat}]})
        nodes.append({"mesh": len(meshes) - 1})

    glass = {"name": "glass", "pbrMetallicRoughness": {"baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                                                        "metallicFactor": 0.0, "roughnessFactor": 0.05}}
    if blend_fallback:
        glass["alphaMode"] = "BLEND"
        glass["pbrMetallicRoughness"]["baseColorFactor"] = [1.0, 1.0, 1.0, 0.25]
    used = []
    if glass_extensions:
        glass["extensions"] = {
            "KHR_materials_transmission": {"transmissionFactor": 1.0},
            "KHR_materials_ior": {"ior": 1.5},
            "KHR_materials_volume": {"thicknessFactor": 1.0, "attenuationDistance": 1.0,
                                     "attenuationColor": [0.55, 0.85, 0.8]},
        }
        used = ["KHR_materials_transmission", "KHR_materials_ior", "KHR_materials_volume"]
    materials = [
        glass,
        {"name": "red", "pbrMetallicRoughness": {"baseColorFactor": [0.8, 0.05, 0.03, 1.0], "metallicFactor": 0.0,
                                                 "roughnessFactor": 0.6}},
        {"name": "white", "pbrMetallicRoughness": {"baseColorFactor": [0.85, 0.85, 0.85, 1.0], "metallicFactor": 0.0,
                                                   "roughnessFactor": 0.6}},
    ]
    gltf = {"asset": {"version": "2.0", "generator": "avgen tools/make_glass_test_glb.py"},
            "scene": 0, "scenes": [{"nodes": list(range(len(nodes)))}], "nodes": nodes, "meshes": meshes,
            "materials": materials, "accessors": accessors, "bufferViews": views,
            "buffers": [{"byteLength": len(blob)}]}
    if used:
        gltf["extensionsUsed"] = used
    text = json.dumps(gltf, separators=(",", ":")).encode()
    text += b" " * ((4 - len(text) % 4) % 4)
    while len(blob) % 4:
        blob.append(0)
    out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(blob))
    out += struct.pack("<II", len(text), 0x4E4F534A) + text
    out += struct.pack("<II", len(blob), 0x004E4942) + bytes(blob)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for name, ext, blend in (("glass.glb", True, False), ("plain.glb", False, False), ("glass-blend.glb", True, True)):
        with open(os.path.join(a.out, name), "wb") as f:
            f.write(build(ext, blend))
    print("wrote glass.glb, plain.glb and glass-blend.glb to", a.out)


if __name__ == "__main__":
    main()
