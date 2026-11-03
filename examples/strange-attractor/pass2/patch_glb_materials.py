"""Copies a .glb under a new name with its materials patched (Art Pass 2, M3e; the hero agent's audit, H1):

    python3 examples/strange-attractor/pass2/patch_glb_materials.py <src.glb> <dst.glb> <kind>

kinds:
- fur:  every material named sasquatch_fur* alpha-tested (MASK, cutoff 0.45) -- sasquatch-b's grey fur stayed BLEND
        (build_sasquatch.py's mask_fur matched only `sasquatch_fur`; fixed there too)
- matte: every material metallic 0 and no emissive -- the Gray's metallic 0.445 and faint green emission read as
         chrome or plastic
The source is never overwritten (assets are gitignored; build/ keeps M3d's files).
"""
import json
import struct
import sys

src, dst, kind = sys.argv[1:4]
b = open(src, "rb").read()
n = struct.unpack("<I", b[12:16])[0]
js = json.loads(b[20:20 + n])
rest = b[20 + n:]
for m in js.get("materials", []):
    if kind == "fur" and m.get("name", "").startswith("sasquatch_fur"):
        m["alphaMode"] = "MASK"
        m["alphaCutoff"] = 0.45
    elif kind == "matte":
        m.setdefault("pbrMetallicRoughness", {})["metallicFactor"] = 0.0
        m["emissiveFactor"] = [0.0, 0.0, 0.0]
        m.pop("emissiveTexture", None)
        m.get("extensions", {}).pop("KHR_materials_emissive_strength", None)
    print(dst, m.get("name"), m.get("alphaMode"), m.get("pbrMetallicRoughness", {}).get("metallicFactor"))
enc = json.dumps(js, separators=(",", ":")).encode()
enc += b" " * ((-len(enc)) % 4)
open(dst, "wb").write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(enc) + len(rest)) +
                      struct.pack("<II", len(enc), 0x4E4F534A) + enc + rest)
