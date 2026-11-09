"""Hero asset H3 (brief 49, Phase 2.2): the three Sasquatches with the H1 fur hybrid (hero-01-audit.md §3, proof p6),
built on the CURRENT individuals -- their bodies, rigs, base poses and 24 s idles are kept exactly.

    Blender -b --factory-startup --python examples/strange-attractor/hero/build_sasquatch_v1.py -- \
        assets/strange-attractor/build assets/strange-attractor/hero/sasquatch [a,b,c]

Reads build/sasquatch-{a,b,c}.glb (from Mat Rex's "Bigfoot", CC BY 4.0) and writes hero/sasquatch/sasquatch-{x}-v1.glb
(+ .blend). Per creature: the body re-tinted to its fur colour and a baked fur albedo and normal (3-D streak noise
along gravity, a broad mottle; the silverback greys over the crown, shoulders and back); 8 clump shells for the fur's
volume (none on the face); about 9-11k soft-rooted lock cards on the head, shoulders, back and forearms, with explicit
back faces and each card weighted wholly by its root (no card spans a hand and a hip); the face bare. Every fur
material alpha-tested (MASK).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np  # noqa: E402

import fur_proof as fp  # noqa: E402

src_dir, outdir = sys.argv[sys.argv.index("--") + 1:][:2]
which = sys.argv[sys.argv.index("--") + 3].split(",") if len(sys.argv) > sys.argv.index("--") + 3 else ["a", "b", "c"]
os.makedirs(outdir, exist_ok=True)
V = {
    "a": dict(fur=(0.045, 0.028, 0.017), grey=None, furlen=1.0, cards=9000, seed=29),     # dark-brown adult
    "b": dict(fur=(0.032, 0.026, 0.022), grey=(0.105, 0.1, 0.095), furlen=1.35, cards=11000, seed=31),  # silverback
    "c": dict(fur=(0.085, 0.036, 0.016), grey=None, furlen=0.85, cards=8000, seed=37),    # young auburn
}
face = lambda c, n: c.z > 0.82 and n.y < -0.3                       # the face stays bare (rest pose, -y forward)
long_hair = lambda c, n: (c.z > 0.55 or (abs(c.x) > 0.12 and 0.3 < c.z < 0.6)) and not face(c, n)
# C sits on a stone: the buttocks and the backs of the thighs carry no fur volume and no cards (rest pose: the back
# faces +y), so the stone can touch the skin without fur passing through it (owner note, 2026-11-03)
seat = lambda c, n: 0.2 < c.z < 0.52 and n.y > 0.15 and abs(c.x) < 0.2
grey_zone = lambda c, n: (c.z > 0.6 and (n.y > -0.2 or c.z > 0.8)) and not face(c, n)   # crown, shoulders, back
for k in which:
    v = V[k]
    col = np.array(v["fur"])
    rig, body = fp.load(os.path.join(src_dir, f"sasquatch-{k}.glb"))
    fp.retint_body(body, colour=col)
    fp.bake_fur(body, rig, seed=float(ord(k)), grey=v["grey"])
    bare = seat if k == "c" else None
    _, cut = fp.make_shells(rig, body, 8, 0.05 * v["furlen"], uv_scale=2.5, clump=True, colour=col, face=0.0, bare=bare)
    region = long_hair if v["grey"] is None else (lambda c, n: long_hair(c, n) and not grey_zone(c, n))
    _, c2 = fp.make_cards(rig, body, v["cards"], 0.07 * v["furlen"], region=region, width=(0.12, 0.18), strands=70,
                          lock=True, colour=col, root_weights=True, seed=v["seed"],
                          skip=(lambda c, n: face(c, n) or seat(c, n)) if k == "c" else face)
    cut.update(c2)
    if v["grey"] is not None:
        _, c3 = fp.make_cards(rig, body, 6000, 0.08 * v["furlen"], region=grey_zone, width=(0.12, 0.18), strands=70,
                              lock=True, colour=np.array(v["grey"]), root_weights=True, skip=face, seed=v["seed"] + 1,
                              name="sasquatch_cards_grey")
        cut.update(c3)
    out = os.path.join(outdir, f"sasquatch-{k}-v1.glb")
    import bpy  # noqa: E402
    bpy.ops.wm.save_as_mainfile(filepath=out.replace(".glb", ".blend"), compress=True)
    fp.export(out, cut)
