#!/usr/bin/env python3
"""Write the Glowmere material variant of the Tree of Life hero GLB.

    python3 tools/glowmere_tree_materials.py \
        --in assets/treeisle/tree-of-life-hero.glb \
        --out assets/treeisle/tree-of-life-hero-glowmere.glb

Why this exists at all. The hero export's own `validation.json` calls it a "Mature static
snapshot; simple PBR, no shader baking or growth export", and it means it. The GLB carries no
textures and no UVs; all seven materials are flat factors, six of them the same brown bark
`[0.12, 0.10, 0.08]` or the same dark green leaf `[0.09, 0.23, 0.11]`, and exactly one of the seven
has any emission -- the recessed tracery, at `[0.02, 0.15, 0.10]`. The Glowmere look in
`hero_pass/renders/Glowmere.png` -- cyan-teal canopy, cream-white where the key lands, pale
mauve-grey bark, bright specks scattered through the leaves -- is a Blender shading setup that did
not survive the export.

Nor can the scene put it back. A `"material"` block on a `kind: "gltf"` node is parsed, validated
and then dropped with a warning (`src/scene/composition.cpp`): a glTF node's surface comes from
the asset's own materials, and the only levers a scene has are `emissiveBoost` and
`roughnessScale`, both scalar and both whole-instance. Boosting emission on a tree whose leaves
emit nothing multiplies zero. So the conversion the brief's §15 allows -- "the minimum required
conversion... Preserve the intended Glowmere appearance" -- has to happen to the asset, and this
is the smallest form it can take.

**Only the JSON chunk is touched.** A GLB is a JSON chunk followed by a binary chunk, and every
vertex, normal and index lives in the binary one. This rewrites the `materials` array and copies
the binary chunk through byte for byte, then checks its SHA-256 against the input's and refuses to
write if it moved. The geometry of the variant is not similar to the hero's, it is *identical*,
and §7's "do not modify the Tree of Life model" holds in the strongest sense available. Both files
ship; the scene names one.

The numbers below were read off `renders/Glowmere.png`, whose lit highlights are sRGB (133,180,166)
on the bark, (141,195,209) on the shaded cyan canopy and (226,228,215) where the key breaks
through. They are a reading of that image, not a solve for it.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

GLB_MAGIC = 0x46546C67
CHUNK_JSON = 0x4E4F534A
CHUNK_BIN = 0x004E4942

# Keyed by the material name inside the hero GLB. Every one must match, or the tree has changed
# since this was written and the palette should be re-read rather than half-applied.
#
# baseColor and emissive are linear, as glTF requires. `emissiveStrength` is
# KHR_materials_emissive_strength, which this engine's loader already understands; it is how a
# lantern gets to be brighter than white without clamping.
GLOWMERE = {
    "GLB | simple LIFE | Trunk roots and boughs": {
        "role": "bark -- pale, cool and desaturated, the mauve-grey the reference trunk reads as",
        "baseColor": [0.108, 0.100, 0.118, 1.0],
        "roughness": 0.68,
        "metallic": 0.0,
    },
    "GLB | simple LIFE | Twigs and fine branches": {
        "role": "the same bark a shade darker, so the fine branches recede behind the canopy",
        "baseColor": [0.082, 0.078, 0.094, 1.0],
        "roughness": 0.74,
        "metallic": 0.0,
    },
    "GLB | simple HERO | Recessed living tracery": {
        "role": "the veins cut into the trunk. The one material that was already emissive; this "
                "keeps its hue and gives it the intensity to read as light rather than as paint",
        "baseColor": [0.028, 0.048, 0.056, 1.0],
        "roughness": 0.45,
        "metallic": 0.0,
        "emissive": [0.055, 0.620, 0.545],
        "emissiveStrength": 5.0,
    },
    "GLB | simple LIFE | Layered broadleaf canopy": {
        "role": "the canopy mass, 122,767 leaves. Teal rather than the export's forest green, with "
                "just enough emission to keep the shadow side of the crown off pure black",
        "baseColor": [0.130, 0.400, 0.430, 1.0],
        "roughness": 0.52,
        "metallic": 0.0,
        "emissive": [0.012, 0.070, 0.082],
        "emissiveStrength": 1.3,
    },
    "GLB | simple LIFE | Delicate peripheral leaves": {
        "role": "the 51,633 outer leaves, brighter and glossier than the mass. This is where the "
                "reference's scattered bright specks come from -- they are the leaves nearest the "
                "light, not a uniform glow over the whole crown",
        "baseColor": [0.220, 0.520, 0.560, 1.0],
        "roughness": 0.40,
        "metallic": 0.0,
        "emissive": [0.030, 0.145, 0.170],
        "emissiveStrength": 2.8,
    },
    "GLB | simple HERO | Hanging seed lanterns": {
        "role": "26 of them in the whole tree. Cream-white and genuinely bright -- the only thing "
                "here allowed to blow out, because a lantern that does not is a bead",
        "baseColor": [0.300, 0.320, 0.280, 1.0],
        "roughness": 0.35,
        "metallic": 0.0,
        "emissive": [0.850, 0.920, 0.780],
        "emissiveStrength": 9.0,
    },
    "GLB | simple HERO | Bark and root fungi": {
        "role": "80 shelf fungi on the trunk and roots, glowing the same cyan as the veins they "
                "grow out of",
        "baseColor": [0.040, 0.120, 0.110, 1.0],
        "roughness": 0.55,
        "metallic": 0.0,
        "emissive": [0.100, 0.520, 0.460],
        "emissiveStrength": 3.0,
    },
}


def read_glb(path: Path) -> tuple[dict, bytes]:
    data = path.read_bytes()
    magic, version, length = struct.unpack_from("<III", data, 0)
    if magic != GLB_MAGIC or version != 2:
        raise SystemExit(f"{path} is not a glTF 2.0 GLB")
    if length != len(data):
        raise SystemExit(f"{path}: header length {length} != file size {len(data)}")
    doc = None
    binary = b""
    offset = 12
    while offset < len(data):
        chunk_len, chunk_type = struct.unpack_from("<II", data, offset)
        payload = data[offset + 8: offset + 8 + chunk_len]
        if chunk_type == CHUNK_JSON:
            doc = json.loads(payload)
        elif chunk_type == CHUNK_BIN:
            binary = payload
        offset += 8 + chunk_len + (-chunk_len % 4)
    if doc is None:
        raise SystemExit(f"{path}: no JSON chunk")
    return doc, binary


def write_glb(path: Path, doc: dict, binary: bytes) -> None:
    js = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    js += b" " * (-len(js) % 4)
    pad = -len(binary) % 4
    total = 12 + 8 + len(js) + (8 + len(binary) + pad if binary else 0)
    with path.open("wb") as f:
        f.write(struct.pack("<III", GLB_MAGIC, 2, total))
        f.write(struct.pack("<II", len(js), CHUNK_JSON))
        f.write(js)
        if binary:
            f.write(struct.pack("<II", len(binary) + pad, CHUNK_BIN))
            f.write(binary)
            f.write(b"\0" * pad)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="glowmere_tree_materials")
    ap.add_argument("--in", dest="src", default="assets/treeisle/tree-of-life-hero.glb")
    ap.add_argument("--out", dest="dst", default="assets/treeisle/tree-of-life-hero-glowmere.glb")
    args = ap.parse_args(argv)

    src = Path(args.src)
    dst = Path(args.dst)
    doc, binary = read_glb(src)

    names = [m.get("name", "") for m in doc.get("materials", [])]
    missing = set(GLOWMERE) - set(names)
    extra = set(names) - set(GLOWMERE)
    if missing or extra:
        raise SystemExit(
            "the hero GLB's materials are not the seven this palette was written for.\n"
            f"  not in the file: {sorted(missing)}\n"
            f"  not in the palette: {sorted(extra)}\n"
            "Re-read renders/Glowmere.png and update GLOWMERE rather than applying it partially."
        )

    for mat in doc["materials"]:
        spec = GLOWMERE[mat["name"]]
        pbr = mat.setdefault("pbrMetallicRoughness", {})
        pbr["baseColorFactor"] = spec["baseColor"]
        pbr["roughnessFactor"] = spec["roughness"]
        pbr["metallicFactor"] = spec["metallic"]
        mat.pop("emissiveFactor", None)
        mat.pop("extensions", None)
        if "emissive" in spec:
            mat["emissiveFactor"] = spec["emissive"]
            mat["extensions"] = {
                "KHR_materials_emissive_strength": {"emissiveStrength": spec["emissiveStrength"]}
            }
        mat["doubleSided"] = True
        mat["name"] = mat["name"].replace("GLB | simple ", "GLOWMERE | ")

    used = set(doc.get("extensionsUsed", []))
    used.add("KHR_materials_emissive_strength")
    doc["extensionsUsed"] = sorted(used)

    write_glb(dst, doc, binary)

    # The whole claim of this tool. If the binary chunk moved, the geometry moved, and the tree
    # was modified after all.
    _, rewritten = read_glb(dst)
    if hashlib.sha256(rewritten).hexdigest() != hashlib.sha256(binary).hexdigest():
        dst.unlink()
        raise SystemExit("binary chunk changed -- refusing to ship a tree whose geometry moved")

    print(f"[glowmere] wrote {dst} ({dst.stat().st_size / 1e6:.1f} MB); "
          f"binary chunk identical to the hero's ({len(binary)} bytes, "
          f"sha256 {hashlib.sha256(binary).hexdigest()[:16]})")
    for mat in doc["materials"]:
        em = mat.get("emissiveFactor")
        strength = mat.get("extensions", {}).get(
            "KHR_materials_emissive_strength", {}).get("emissiveStrength")
        print(f"  {mat['name']:<44} base {mat['pbrMetallicRoughness']['baseColorFactor'][:3]}"
              + (f"  emissive {em} x{strength}" if em else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
