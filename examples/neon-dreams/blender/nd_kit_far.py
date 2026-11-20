"""Neon Dreams -- the far kit (iteration 6): what stands beyond the journey.

    Blender -b --factory-startup -P examples/neon-dreams/blender/nd_kit.py -- --out <dir> --only far --merge-manifest examples/neon-dreams/meshes/kit_manifest.json

THE LONG ARC (art direction section 8): a broken span of something city-sized. A segment of a circle 18 km across,
standing on edge 22 km east of the journey: it leaves the cloud sea steeply, bends over toward the south through 37
degrees of its circle (5.9 km of span, 3.5 km high at the break) and stops; three detached pieces carry its line on. A
main chord 50 m deep, a thinner lower chord 170 m inside it, struts between them, and a node light every 470 m. From the
journey the chord is one to two pixels wide: a hairline with points on it. The line must carry it: bright lights alone
read as a string of lamps (or as craft).

The asset's frame: origin where the arc leaves the sea, +x along the ground under it (the layout turns that to the
south), z up. The origin of the circle is ARC["centre"] in that frame. Its two materials are its own (`arc_line`, a dim
violet emission; `arc_light`, its node lights): no light role of the scene repaints them, and neither is in the
window audit. Rank 6 in the art direction's value ladder: if it competes with the Great Lantern it is too bright.
"""
import math
from nd_kitcore import *  # noqa

ARC = dict(radius=9061.0, centre=(7551.0, 0.0, -5010.0), a0_deg=33.6, a1_deg=71.0, pieces=((73.0, 75.6), (77.2, 78.6), (80.3, 81.0)))


def _mat(name, base, emit, strength, rough=0.6):
    m, nt = new_mat(name)
    out = node(nt, 'ShaderNodeOutputMaterial'); b = node(nt, 'ShaderNodeBsdfPrincipled'); nt.links.new(b.outputs[0], out.inputs[0])
    b.inputs['Base Color'].default_value = (*base, 1); b.inputs['Roughness'].default_value = rough
    b.inputs['Emission Color'].default_value = (*emit, 1); b.inputs['Emission Strength'].default_value = strength
    MATS[name] = m


def long_arc():
    _mat("arc_line", (0.004, 0.004, 0.008), (0.42, 0.36, 1.0), 0.22)
    _mat("arc_light", (0.01, 0.01, 0.01), (0.80, 0.74, 1.0), 0.9)
    A = Asset("long_arc"); R = ARC["radius"]; cx, cy, cz = ARC["centre"]

    def P(a_deg, r=None):
        a = math.radians(a_deg); r = R if r is None else r
        return (cx - r * math.cos(a), 0.0, cz + r * math.sin(a))
    step = 0.5; k = 0
    for (a0, a1) in ((ARC["a0_deg"] - 1.5, ARC["a1_deg"]),) + ARC["pieces"]:        # it starts 240 m under the sea's top
        a = a0
        while a < a1 - 1e-6:
            b = min(a + step, a1)
            A.m("arc_line").beam(P(a), P(b), 36.0, t2=50.0)
            A.m("arc_line").beam(P(a, R - 170.0), P(b, R - 170.0), 22.0, t2=22.0)
            if k % 5 == 0:
                A.m("arc_line").beam(P(a), P(a, R - 170.0), 14.0)
                A.m("arc_line").beam(P(a, R - 170.0), P(min(a + step * 2.5, a1)), 10.0)
            if k % 6 == 2:
                q = P(a, R + 30.0); A.m("arc_light").box(q, (40.0, 40.0, 40.0))
            a = b; k += 1
        q = P(a1)                                                                   # a broken end: a stub of the lower chord past it
        A.m("arc_line").beam(P(a1, R - 170.0), P(a1 + 0.35, R - 205.0), 16.0)
    A.meta = {"what": "the Long Arc: a broken span 5.9 km long standing 22 km east of the journey", "frame": "origin where it leaves the sea, +x along the ground under it, y up in the GLB",
              "circle": {"radius_m": R, "centre_m": [cx, cz, -cy], "from_deg": ARC["a0_deg"], "to_deg": ARC["a1_deg"], "detached_deg": [list(p) for p in ARC["pieces"]]},
              "materials_note": "arc_line and arc_light keep the emission baked into the GLB (0.22 and 0.9): the scene has no role for them. At 0.55 and 9 (first test in AV Gen) it was a string of fairy lights brighter than the Condensery, and showed through the Veil"}
    A.finish()
    return A


def far_kit():
    return [long_arc()]
