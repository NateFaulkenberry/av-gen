"""The UFO craft (Art Pass 2, M3): BrunoLatour's "UFO" (Sketchfab, CC BY 4.0; assets-pass2.md) made usable here.

    python3 examples/strange-attractor/pass2/prepare_ufo.py

Writes assets/strange-attractor/build/ufo-craft.glb from the download: every material opaque (the model ships them all
alpha-blended, so the hull drew as a glowing ghost) and its emission brought down to its windows' level (the emission
textures were at full strength, up to 4.9x). Geometry and textures are unchanged.
"""
import json
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "..", "assets", "strange-attractor", "sketchfab", "ufo", "ufo-brunolatour", "ufo.glb")
OUT = os.path.join(HERE, "..", "..", "..", "assets", "strange-attractor", "build", "ufo-craft.glb")
EMISSION = 0.12

b = open(SRC, "rb").read()
n = struct.unpack("<I", b[12:16])[0]
js = json.loads(b[20:20 + n])
rest = b[20 + n:]
for m in js.get("materials", []):
    m["alphaMode"] = "OPAQUE"
    m.pop("alphaCutoff", None)
    m["emissiveFactor"] = [c * EMISSION for c in m.get("emissiveFactor", [1.0, 1.0, 1.0])]
    ext = m.get("extensions", {})
    ext.pop("KHR_materials_emissive_strength", None)
    if not ext:
        m.pop("extensions", None)
used = js.get("extensionsUsed", [])
js["extensionsUsed"] = [e for e in used if e != "KHR_materials_emissive_strength"]
if not js["extensionsUsed"]:
    js.pop("extensionsUsed")
enc = json.dumps(js, separators=(",", ":")).encode()
enc += b" " * ((-len(enc)) % 4)
open(OUT, "wb").write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(enc) + len(rest)) +
                      struct.pack("<II", len(enc), 0x4E4F534A) + enc + rest)
print(OUT)
