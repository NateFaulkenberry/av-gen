"""All You Got, art pass 4: the city's other people (04-art-pass-4.md, PARTS 9 and 10). The protagonist is a posed
figure (figure3) that never moves while seen; everyone else in the city is a RIGGED mannequin whose joints are named
rotates, so the beat grid can walk them, and make them talk and gesture, while he stays still.

The rig has figure3's proportions and look (sharp blocks, cubes at the joints, a faceted head) built as a hierarchy:

    <n>         the figure's placement (a named translate: key it along a path to walk the figure)
      pelvis, waist, chest, neck, head (<n>Head: a rotate, for a nod or a turn)
      <n>HipL/R  -> thigh, <n>KneeL/R -> shin, foot         (rotation x: negative swings the leg forward)
      <n>ShL/R   -> upper arm, <n>ElL/R -> forearm, hand    (rotation x: negative swings the arm forward)

`walk(f, obj, n, ...)` routes the grid's half-note wave into the joints: one stride a beat, the arms against the
legs, the rear knee bending, nothing bobbing (the half-note wave is a raised cosine: smooth, never a pulse).
`talk(...)` moves an arm and the head on slower waves. These are living things (PART 15's continuous modulation):
their node names carry 'walk'/'gest' so the builder's structural audit lets them move.
"""

from __future__ import annotations

import math

import figure3 as F
import kit as K
from kit import ACCENT, FILL, GLASS, R, S, T, U, box

P_Y = 0.96           # the pelvis centre standing (the ankles at 0.05, the soles on the floor)
HIP_Y = P_Y - 0.04


def _gem(name):
    a, b, c3 = F.HEAD_H
    return K.I(box([a, b, c3]), R([0, 45, 0], box([(a + c3) * 0.6, b + 0.05, (a + c3) * 0.6])),
               R([45, 0, 0], box([a + 0.05, (b + c3) * 0.6, (b + c3) * 0.6])),
               R([0, 0, 45], box([(a + b) * 0.6, (a + b) * 0.6, c3 + 0.05])))


def _cube(h, k=ACCENT):
    return S(R([0, 45, 0], box([h, h, h])), k)


def rig(n, stance=None, k=FILL, joint=ACCENT, head=GLASS):
    """A rigged standing mannequin facing +Z, its soles on y = 0. `stance` gives base joint angles in degrees:
    hipL, hipR, kneeL, kneeR, shL, shR (x), shSpread (z, arms away from the body), elL, elR (x), headPitch, headYaw,
    lean (the spine, + forward). Returns the tree (about 70 nodes)."""
    st = {"hipL": 0.0, "hipR": 0.0, "kneeL": 4.0, "kneeR": 4.0, "shL": 2.0, "shR": 2.0, "shSpread": 6.0, "elL": -10.0,
          "elR": -10.0, "headPitch": 4.0, "headYaw": 0.0, "lean": 3.0}
    st.update(stance or {})

    def leg(side, sgn):
        foot = T([0.0, -F.SHIN - 0.035, 0.06], box(list(F.FOOT_H)))
        shin = T([0.0, -F.SHIN / 2, 0.0], box([0.047, F.SHIN / 2 + 0.015, 0.047]))
        knee = T([0.0, -F.THIGH, 0.0], R([st[f"knee{side}"], 0.0, 0.0], U(_cube(0.048, joint), S(U(shin, foot), k)), name=f"{n}Knee{side}walk"))
        thigh = S(T([0.0, -F.THIGH / 2, 0.0], box([0.058, F.THIGH / 2 + 0.02, 0.058])), k)
        return T([sgn * F.HIP_W, HIP_Y, 0.0], R([st[f"hip{side}"], 0.0, 0.0], U(thigh, knee), name=f"{n}Hip{side}walk"))

    def arm(side, sgn, sh_y, chest_z):
        hand = T([0.0, -F.FARM - F.HAND_H[1] - 0.01, 0.0], box(list(F.HAND_H)))
        farm = T([0.0, -F.FARM / 2, 0.0], box([0.034, F.FARM / 2 + 0.012, 0.034]))
        elbow = T([0.0, -F.UARM, 0.0], R([st[f"el{side}"], 0.0, 0.0], U(_cube(0.037, joint), S(U(farm, hand), k)), name=f"{n}El{side}gest"))
        uarm = S(T([0.0, -F.UARM / 2, 0.0], box([0.04, F.UARM / 2 + 0.014, 0.04])), k)
        spread = R([0.0, 0.0, sgn * st["shSpread"]], U(_cube(0.05, joint), uarm, elbow))
        return T([sgn * F.SHOULDER_W, sh_y, chest_z], R([st[f"sh{side}"], 0.0, 0.0], spread, name=f"{n}Sh{side}gest"))

    lean = math.radians(st["lean"])
    up = [0.0, math.cos(lean), math.sin(lean)]
    P = [0.0, P_Y, 0.0]
    lumbar = F.add(P, F.mul(up, 0.13))
    chest = F.add(lumbar, F.mul(up, F.CHEST_AT - 0.13))
    n1 = F.add(lumbar, F.mul(up, F.NECK1 - 0.13))
    torso = U(S(T(F.add(P, [0.0, 0.04, 0.0]), box(list(F.PELVIS_H))), k), T(lumbar, _cube(0.068, joint)),
              S(T(chest, R([-st["lean"], 0.0, 0.0], box(list(F.CHEST_H)))), k),
              S(T(F.add(lumbar, F.mul(up, 0.47)), R([-st["lean"], 0.0, 0.0], box([0.036, 0.06, 0.036]))), k))
    head_node = T(F.add(n1, [0.0, F.HEAD_OVER_NECK, 0.0]),
                  R([st["headPitch"], st["headYaw"], 0.0], S(_gem(n), head), name=f"{n}Headgest"))
    sh_y = chest[1] + F.SH_UP
    body = U(torso, head_node, leg("L", 1.0), leg("R", -1.0), arm("L", 1.0, sh_y, chest[2]), arm("R", -1.0, sh_y, chest[2]))
    return body


def walker(n, stance=None):
    """The rig under its placement translate `<n>walk` (key its translation along the path)."""
    return T([0.0, 0.0, 0.0], rig(n, stance), name=f"{n}walk")


# ---- driving them ----------------------------------------------------------------------------------------------
def walk(f, obj, n, stride=24.0, arms=16.0, knee=22.0, phase=1.0, gate=None):
    """One stride a beat on the grid's half-note wave: the hips swing +-stride degrees (left forward on beats 1 and 3
    when phase is 1), the arms against them, the rear knee bends up to `knee`."""
    w = "grid.song.half.wave"
    s = phase
    f.route(w, f"sdf/{obj}/node/{n}HipLwalk/rotation", -stride * s, component=0, polarity="bipolar", depth=gate)
    f.route(w, f"sdf/{obj}/node/{n}HipRwalk/rotation", stride * s, component=0, polarity="bipolar", depth=gate)
    f.route(w, f"sdf/{obj}/node/{n}ShLgest/rotation", arms * s, component=0, polarity="bipolar", depth=gate)
    f.route(w, f"sdf/{obj}/node/{n}ShRgest/rotation", -arms * s, component=0, polarity="bipolar", depth=gate)
    # the rear leg's knee: (1 - wave) peaks while the left leg is back (phase 1), the wave itself for the right
    if s > 0:
        f.route(w, f"sdf/{obj}/node/{n}KneeLwalk/rotation", -knee, component=0, depth=gate)
        f.route(w, f"sdf/{obj}/node/{n}KneeRwalk/rotation", knee, component=0, depth=gate)
    else:
        f.route(w, f"sdf/{obj}/node/{n}KneeLwalk/rotation", knee, component=0, depth=gate)
        f.route(w, f"sdf/{obj}/node/{n}KneeRwalk/rotation", -knee, component=0, depth=gate)


def walk_base(s):
    """Base stance for a walker driven by walk(): the knees' rest angles that the knee routes add to (phase 1: the
    left knee rests bent and straightens as the wave rises)."""
    return {"kneeL": 22.0 + 4.0 if s > 0 else 4.0, "kneeR": 4.0 if s > 0 else 22.0 + 4.0, "elL": -14.0, "elR": -14.0}


def talk(f, obj, n, arm="R", amount=28.0, nod=7.0, div="bar", gate=None):
    """Talking: one forearm lifts and falls on a slow wave, the head nods on the half note."""
    f.route(f"grid.song.{div}.wave", f"sdf/{obj}/node/{n}El{arm}gest/rotation", -amount, component=0, depth=gate)
    f.route("grid.song.half.wave", f"sdf/{obj}/node/{n}Headgest/rotation", nod, component=0, polarity="bipolar", depth=gate)
