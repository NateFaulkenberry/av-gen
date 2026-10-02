"""All You Got, art pass 4: the new tableaux (04-art-pass-4.md, PARTS 4, 6, 9 and 11). Registered into tableaux3's
table at import, so `tableaux3.placed(name, ...)` places them exactly as pass 3's (pass 3 never names these, so its
output is unchanged). Each is posed in the frame of its ANCHOR and checked against it:

    python3 tools/liminal/tableaux4.py        # every new tableau's contact report (CPU); exit 0 when all sit right

  stove        standing at the stove, a spoon in his right hand resting in the pot, looking down into it (dinner)
  fold         at the dryer, folding a towel on its top, head down (the laundry)
  gym_stand    standing in the gym with a dumbbell hanging from his right hand, not lifting it (present, inactive)
  bar_sit      on a bar stool, forearms on the bar either side of a glass, looking at it
  lounge_sit   slumped back on the lounge sofa, hands in his lap, facing the television's static
  car_sit      at the wheel of a car stopped at a red light, both hands on the wheel, looking ahead
  bench_sit    on a park bench, leaning forward, forearms on his thighs, hands loosely together, head low
  crowd_stand  standing still in the street while everyone moves past him
  triumph      at the top of the stairs: upright, the chest lifted, the head raised, the arms a little open
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tools"))

import figure3 as F  # noqa: E402
import kit as K  # noqa: E402
import liminal_space as _ls  # noqa: E402

_ls.instrument_kit(K)

import props3 as PR  # noqa: E402
import props4 as P4  # noqa: E402
import tableaux3 as TB  # noqa: E402
from tableaux3 import FLOOR, pitch, tableau  # noqa: E402

NEW = []


def new(fn):
    NEW.append(fn.__name__)
    return tableau(fn)


def standing(P=(0.0, 0.935, 0.0), lean=0.0):
    return {"P": list(P), "U": pitch(lean)}


# ---- the kitchen -------------------------------------------------------------------------------------------
STOVE_AT = (-0.17, 0.0, 0.67)     # the stove's centre in the figure's frame (turned 180: its front faces him)


def stove_anchor():
    st = K.place(P4.stove(), STOVE_AT, 180.0)
    pot = K.place(P4.pot(), (0.0, 0.9, STOVE_AT[2] - 0.12))
    return K.U(st, pot)


@new
def stove():
    # the pot's centre (0, 0.9, 0.55) in his frame; the spoon's bowl rests in it, its handle in his right hand
    pose = dict(standing(lean=7.0), head={"pitch": 38.0},
                hands={"R": {"wrist": [-0.05, 1.1, 0.4], "pole": [-0.6, -0.2, -1.0], "dir": [0.12, -0.75, 0.6], "palm": [1.0, 0.0, 0.0]},
                       "L": {"wrist": [0.22, 0.7, 0.06], "pole": [0.3, 0.0, -1.0]}},
                feet={"R": {"ankle": [-0.12, 0.05, 0.02]}, "L": {"ankle": [0.12, 0.05, -0.02]}})
    return {"pose": pose, "anchor": (stove_anchor, (0.0, 0.0, 0.0), 0.0), "category": "stove", "posture": "stand",
            "groups": {"feet": ["foot"]}, "props": [("spoon", spoon_in_hand)]}


def spoon_in_hand(fig):
    """The spoon from the right hand down into the pot (in the figure's frame)."""
    wr, hand = fig.points["wristR"], fig.points["handR"]
    d = F.norm(F.sub(hand, wr))
    tip = [0.0, 0.93, 0.55]
    a = F.add(wr, F.mul(d, 0.05))
    return K.S(K.SEG(a, tip, 0.008, box_section=(0.012, 0.006)), K.ACCENT)


# ---- the laundry ---------------------------------------------------------------------------------------------
DRYER_AT = (0.0, 0.0, 0.62)


def dryer_anchor():
    return K.U(K.place(PR.dryer(), DRYER_AT, 180.0), towel_on_dryer())


def towel_on_dryer():
    """A towel being folded on the dryer's top (0.87 m): a folded slab and the hanging half."""
    top = 0.872
    return K.S(K.U(K.X(((-0.24, 0.24), (top, top + 0.025), (0.36, 0.62))), K.X(((-0.24, 0.24), (top + 0.025, top + 0.04), (0.36, 0.5)))), K.ACCENT)


@new
def fold():
    pose = dict(standing(lean=16.0), head={"pitch": 32.0},
                hands={"R": {"wrist": [-0.16, 0.95, 0.42], "pole": [-0.7, -0.2, -1.0], "dir": [0.1, -0.25, 1.0], "palm": [0.0, -1.0, 0.0]},
                       "L": {"wrist": [0.16, 0.95, 0.42], "pole": [0.7, -0.2, -1.0], "dir": [-0.1, -0.25, 1.0], "palm": [0.0, -1.0, 0.0]}},
                feet={"R": {"ankle": [-0.12, 0.05, -0.02]}, "L": {"ankle": [0.13, 0.05, 0.03]}})
    return {"pose": pose, "anchor": (dryer_anchor, (0.0, 0.0, 0.0), 0.0), "category": "appliance", "posture": "stand",
            "groups": {"feet": ["foot"], "hands": ["hand"]}}


# ---- the gym ----------------------------------------------------------------------------------------------------
@new
def gym_stand():
    pose = dict(standing(lean=-1.0), head={"pitch": 14.0, "yaw": -8.0},
                hands={"R": {"wrist": [-0.24, 0.66, 0.03], "pole": [-0.3, 0.0, -1.0], "dir": [0.0, -1.0, 0.05], "palm": [1.0, 0.0, 0.0]},
                       "L": {"wrist": [0.22, 0.69, 0.0]}},
                feet={"R": {"ankle": [-0.13, 0.05, 0.0]}, "L": {"ankle": [0.12, 0.05, 0.05]}})
    return {"pose": pose, "anchor": (lambda: FLOOR, (0.0, 0.0, 0.0), 0.0), "category": "floor", "posture": "stand",
            "groups": {"feet": ["foot"]}, "props": [("dumbbell", dumbbell_in_hand)]}


def dumbbell_in_hand(fig):
    h = fig.points["handR"]
    c = [h[0], h[1] + 0.03, h[2]]
    return K.S(K.U(K.T(F.add(c, [0, 0, -0.1]), K.R([90, 0, 0], K.cyl(0.048, 0.055))), K.T(F.add(c, [0, 0, 0.1]), K.R([90, 0, 0], K.cyl(0.048, 0.055))),
                   K.T(c, K.R([90, 0, 0], K.cyl(0.014, 0.2)))), K.ACCENT)


# ---- the lounge's bar --------------------------------------------------------------------------------------------
BAR_AT = (0.0, 0.0, 0.74)          # the counter's centre (turned 180: its customer side faces him)


def bar_anchor():
    glass = K.S(K.D(K.CY([0, 1.08 + 0.06, 0.58], 0.035, 0.12), K.CY([0, 1.08 + 0.08, 0.58], 0.028, 0.12)), K.GLASS)
    return K.U(P4.bar_stool(), K.place(P4.bar_counter(2.6), BAR_AT, 180.0), glass)


@new
def bar_sit():
    up_p, up_s = pitch(10), pitch(30)
    P = [0.0, 0.76 + 0.097, 0.0]
    pose = {"P": P, "Up": up_p, "U": up_s, "F": [0, 0, 1], "head": {"pitch": 22.0},
            "hands": {"R": {"wrist": [-0.06, 1.115, 0.53], "elbow": [-0.21, 1.115, 0.38], "dir": [0.45, 0.0, 1.0], "palm": [0.0, -1.0, 0.0]},
                      "L": {"wrist": [0.07, 1.115, 0.55], "elbow": [0.22, 1.115, 0.4], "dir": [-0.5, 0.0, 1.0], "palm": [0.0, -1.0, 0.0]}},
            "feet": {"R": {"ankle": [-0.11, 0.33, 0.17], "pole": [0, 0.3, 1]}, "L": {"ankle": [0.11, 0.33, 0.18], "pole": [0, 0.3, 1]}}}
    return {"pose": pose, "anchor": (bar_anchor, (0.0, 0.0, 0.0), 0.0), "category": "barStool", "posture": "sit",
            "groups": {"seat": ["thigh", "pelvis"], "forearms": ["farm", "elbow"]}}


@new
def lounge_sit():
    up_p, up_s = pitch(-6), pitch(-16)
    P = [0.0, 0.53, 0.0]
    pose = {"P": P, "Up": up_p, "U": up_s, "F": [0, 0, 1], "head": {"pitch": 10.0},
            "hands": {"R": {"wrist": [-0.06, 0.6, 0.33], "pole": [-0.5, 0.0, -1.0], "dir": [0.5, -0.2, 0.6], "palm": [0.0, -1.0, 0.0]},
                      "L": {"wrist": [0.07, 0.6, 0.31], "pole": [0.5, 0.0, -1.0], "dir": [-0.5, -0.2, 0.6], "palm": [0.0, -1.0, 0.0]}},
            "feet": {"R": {"ankle": [-0.15, 0.05, 0.55], "pole": [0, 0.6, 1]}, "L": {"ankle": [0.15, 0.05, 0.54], "pole": [0, 0.6, 1]}}}
    return {"pose": pose, "anchor": (PR.sofa3, (0.0, 0.0, 0.0), 0.0), "category": "sofa", "posture": "sit",
            "groups": {"seat": ["thigh", "pelvis"], "feet": ["foot"]}}


# ---- the city ------------------------------------------------------------------------------------------------------
CAR_AT = (-0.38, 0.0, 0.25)        # the car's centre in the figure's frame: the driver's seat (car x +0.38) is his origin


def car_anchor():
    return K.place(P4.car(open_cabin=True), CAR_AT, 0.0)


@new
def car_sit():
    up_p, up_s = pitch(-8), pitch(-12)
    P = [0.0, 0.4 + 0.097, -0.02]
    wheel = [0.0, 0.92, CAR_AT[2] + 2.15 - 1.6]
    pose = {"P": P, "Up": up_p, "U": up_s, "F": [0, 0, 1], "head": {"pitch": -2.0},
            "hands": {"R": {"wrist": [wheel[0] - 0.14, wheel[1] + 0.04, wheel[2] - 0.05], "pole": [-0.6, -0.4, -1.0], "dir": [0.2, 0.6, 0.3], "palm": [1.0, 0.0, 0.0]},
                      "L": {"wrist": [wheel[0] + 0.14, wheel[1] + 0.04, wheel[2] - 0.05], "pole": [0.6, -0.4, -1.0], "dir": [-0.2, 0.6, 0.3], "palm": [-1.0, 0.0, 0.0]}},
            "feet": {"R": {"ankle": [-0.13, 0.42, 0.78], "pole": [0, 1, 0.3]}, "L": {"ankle": [0.13, 0.42, 0.76], "pole": [0, 1, 0.3]}}}
    return {"pose": pose, "anchor": (car_anchor, (0.0, 0.0, 0.0), 0.0), "category": "carSeat", "posture": "sit",
            "groups": {"seat": ["thigh", "pelvis"]}}


@new
def bench_sit():
    up_p, up_s = pitch(14), pitch(30)
    P = [0.0, 0.47 + 0.097, -0.04]
    pose = {"P": P, "Up": up_p, "U": up_s, "F": [0, 0, 1], "head": {"pitch": 22.0},
            "hands": {"R": {"wrist": [-0.03, 0.55, 0.42], "elbow": [-0.17, 0.62, 0.27], "dir": [0.6, -0.3, 0.5], "palm": [0.0, -0.4, 1.0]},
                      "L": {"wrist": [0.04, 0.56, 0.44], "elbow": [0.18, 0.63, 0.29], "dir": [-0.6, -0.4, 0.5], "palm": [0.0, -0.4, -1.0]}},
            "feet": {"R": {"ankle": [-0.14, 0.05, 0.46], "pole": [0, 0.4, 1]}, "L": {"ankle": [0.15, 0.05, 0.43], "pole": [0, 0.4, 1]}}}
    return {"pose": pose, "anchor": (P4.park_bench, (0.0, 0.0, 0.0), 0.0), "category": "parkBench", "posture": "sit",
            "groups": {"seat": ["thigh", "pelvis"], "feet": ["foot"]}}


@new
def crowd_stand():
    pose = dict(standing(lean=1.0), head={"pitch": 6.0},
                hands={"R": {"wrist": [-0.22, 0.69, 0.01]}, "L": {"wrist": [0.22, 0.69, 0.01]}},
                feet={"R": {"ankle": [-0.11, 0.05, 0.0]}, "L": {"ankle": [0.11, 0.05, 0.02]}})
    return {"pose": pose, "anchor": (lambda: FLOOR, (0.0, 0.0, 0.0), 0.0), "category": "floor", "posture": "stand",
            "groups": {"feet": ["foot"]}}


@new
def triumph():
    # upright, the chest lifted, the head raised to the open sky, the arms opened a little from the body with the palms
    # forward: someone who has finally reached somewhere (not a victory pose)
    pose = {"P": [0.0, 0.945, 0.0], "U": pitch(-7), "Up": pitch(-2), "head": {"pitch": -22.0},
            "hands": {"R": {"wrist": [-0.47, 0.84, 0.1], "pole": [-0.5, 0.1, -1.0], "dir": [-0.45, -1.0, 0.2], "palm": [0.0, 0.0, 1.0]},
                      "L": {"wrist": [0.47, 0.84, 0.1], "pole": [0.5, 0.1, -1.0], "dir": [0.45, -1.0, 0.2], "palm": [0.0, 0.0, 1.0]}},
            "feet": {"R": {"ankle": [-0.18, 0.05, 0.03]}, "L": {"ankle": [0.18, 0.05, -0.04]}}}
    return {"pose": pose, "anchor": (lambda: FLOOR, (0.0, 0.0, 0.0), 0.0), "category": "floor", "posture": "stand",
            "groups": {"feet": ["foot"]}}


TB.VALIDATOR_POSE.update({"stove": "stand", "fold": "stand", "gym_stand": "stand", "bar_sit": "sit", "lounge_sit": "sit",
                          "car_sit": "sit", "bench_sit": "sit", "crowd_stand": "stand", "triumph": "stand"})


def placed(name, fig_name, at=(0.0, 0.0, 0.0), yaw=0.0, room=None, anchor_id=None, t0=None, t1=None):
    """tableaux3.placed, plus the tableau's hand-held props (a spoon, a dumbbell) built from the solved figure and
    joined to it (they move with him, they are his)."""
    tree, lo, hi = TB.placed(name, fig_name, at, yaw, room=room, anchor_id=anchor_id, t0=t0, t1=t1)
    spec = TB.TABLEAUX[name]()
    props = spec.get("props") or []
    if not props:
        return tree, lo, hi
    fig = F.Figure(spec["pose"], fig_name)
    extra = [fn(fig) for _, fn in props]
    # the figure's own tree is tree's child under the placement; add the props in the same frame
    body = K.U(fig.tree, *extra)
    _ls.tag(body, "mannequin", id=fig_name, room=room, anchor=anchor_id, pose=TB.VALIDATOR_POSE.get(name, "stand"), tableau=name,
            hip=[round(v, 4) for v in fig.points["pelvis"]], t0=t0, t1=t1)
    fig.tree = body
    tree = K.place(body, at, yaw)
    lo2, hi2 = F.placed_bounds(fig.bounds(), at, yaw)
    pad = 0.15
    return tree, [min(a, b) - pad for a, b in zip(lo, lo2)], [max(a, b) + pad for a, b in zip(hi, hi2)]


def report():
    ok_all = True
    for name in NEW:
        fig, anchor, spec = TB.build(name, "m")
        tree = K.U(anchor, FLOOR)
        rows, ok = F.contacts(fig, tree, spec["groups"])
        ok_all &= ok
        n = K.count(fig.tree)[0]
        print(f"{name:<12} {spec['posture']:<5} on {spec['category']:<10} {n} nodes  {'OK' if ok else 'FIX'}")
        print("\n".join(rows))
    return ok_all


if __name__ == "__main__":
    sys.exit(0 if report() else 1)
