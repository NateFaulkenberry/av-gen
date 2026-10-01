"""All You Got, art pass 3: the contemplative tableaux (03-art-pass-3-addendum.md, sections 16-19 and 13).

The mannequin never moves while we see it. Each time the camera comes back to it, it is in another pose
somewhere else in the room: a sequence of frozen tableaux, the camera providing all the motion. Each tableau is
a figure3.Figure posed in the frame of its ANCHOR furniture and checked against it (`python3 tableaux3.py`).

A tableau: name, the pose (figure3 spec, in the FIGURE's frame: it faces +Z), the anchor (a furniture builder
and its placement in the figure's frame), the anchor's category for the validator, and contact groups (which
parts must touch the anchor: seat, feet, elbows, hands, head).

    thinker      seated in the armchair, hunched, the chin on the right hand, the elbow on the knee (Rodin)
    couch_lie    lying on the sofa, the head on a pillow against one arm, the feet up on the other, staring up
    head_hands   sitting on the sofa's front edge, elbows on the knees, the face in the hands
    window       standing at a window, close, still, looking out
    table        at the kitchen table, the elbows on the table, the head resting on the hands
    mirror       at the bathroom sink, both hands on its rim, the head lowered
    toilet       on the toilet, forearms on the thighs, hands hanging, staring at the floor (deadpan)
    bed          lying in bed on its back, a forearm across the forehead, staring at the ceiling
    desk         at the desk, forearms on it, staring into the monitor's static
    bed_edge     sitting on the edge of the bed, hands on the mattress, head down
    stair        standing at the foot of the stair, looking up it
"""

from __future__ import annotations

import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tools"))

import figure3 as F  # noqa: E402
import kit as K  # noqa: E402
import liminal_space as _ls  # noqa: E402

_ls.instrument_kit(K)

import props3 as PR  # noqa: E402
from figure3 import add, mul, norm, rot_axis

FLOOR = K.X(((-3.0, 3.0), (-0.6, 0.0), (-3.0, 3.0)))


def pitch(deg):
    """The spine's up vector pitched `deg` forward (towards +Z) from vertical."""
    a = math.radians(deg)
    return [0.0, math.cos(a), math.sin(a)]


def pitch_f(deg):
    a = math.radians(deg)
    return [0.0, -math.sin(a), math.cos(a)]


def _seated_points(P, up_p, up_s):
    """Shoulders, the neck top and helpers for a seated pose (the figure's own proportions)."""
    lumbar = add(P, mul(up_p, 0.13))
    chest = add(lumbar, mul(up_s, F.CHEST_AT - 0.13))
    sh = {s: add(add(chest, [sg * F.SHOULDER_W, 0.0, 0.0]), mul(up_s, F.SH_UP)) for s, sg in (("R", -1), ("L", 1))}
    n1 = add(lumbar, mul(up_s, F.NECK1 - 0.13))
    return sh, n1


TABLEAUX = {}


def tableau(fn):
    TABLEAUX[fn.__name__] = fn
    return fn


@tableau
def thinker():
    up_p, up_s = pitch(15), pitch(58)
    P = [0.0, 0.42 + 0.097, 0.02]
    _, n1 = _seated_points(P, up_p, up_s)
    hpitch = -35.0                                    # the head lifted against the hunch: it looks ahead, down
    hy = rot_axis(up_s, [1, 0, 0], hpitch)
    head = add(n1, mul(hy, F.HEAD_OVER_NECK))
    hz = norm(rot_axis(pitch_f(58), [1, 0, 0], hpitch))
    chin = add(add(head, mul(hy, -0.105)), mul(hz, 0.045))
    hand_dir = norm([0.12, 1.0, -0.05])
    wrist_r = add(chin, add(mul(hand_dir, -0.16), [-0.02, -0.015, 0.0]))
    pose = {"P": P, "Up": up_p, "U": up_s, "F": [0, 0, 1], "head": {"pitch": hpitch},
            "hands": {"R": {"wrist": wrist_r, "elbow": [-0.1, 0.6, 0.43], "dir": hand_dir, "palm": [0.0, 0.0, -1.0]},
                      "L": {"wrist": [0.13, 0.58, 0.56], "elbow": [0.17, 0.6, 0.38], "dir": [0.0, -0.6, 1.0]}},
            "feet": {"R": {"ankle": [-0.12, 0.05, 0.54], "pole": [0, 0.5, 1]},
                     "L": {"ankle": [0.14, 0.05, 0.5], "pole": [0, 0.5, 1]}}}
    return {"pose": pose, "anchor": (PR.armchair3, (0.0, 0.0, 0.0), 0.0), "category": "armchair", "posture": "sit",
            "groups": {"seat": ["thigh", "pelvis"], "feet": ["foot"]}}


@tableau
def couch_lie():
    U_ = norm([-1.0, 0.07, 0.0])
    P = [0.12, 0.42 + 0.1 - 0.02, 0.08]
    pose = {"P": P, "U": U_, "Up": U_, "F": [0.0, 1.0, 0.0], "head": {"pitch": 40.0},
            "hands": {"R": {"wrist": add(P, [-0.24, 0.17, -0.06]), "pole": [0.2, -0.4, -1.0], "dir": [0.4, 0.0, 1.0],
                            "palm": [0.0, 1.0, 0.0]},
                      "L": {"wrist": add(P, [-0.2, 0.17, 0.08]), "pole": [0.2, -0.4, 1.0], "dir": [0.3, 0.0, -1.0],
                            "palm": [0.0, 1.0, 0.0]}},
            "feet": {"R": {"ankle": [0.93, 0.665, 0.0], "pole": [0, 1, 0], "dir": [0, 1, 0], "up": [-1, 0, 0]},
                     "L": {"ankle": [0.9, 0.665, 0.2], "pole": [0, 1, 0.2], "dir": [0, 1, 0.1], "up": [-1, 0, 0]}}}
    return {"pose": pose, "anchor": (lambda: K.U(PR.sofa3(), K.place(PR.pillow(0.5, 0.34, 0.12), (-0.66, 0.42, 0.08), 90.0)),
                                     (0.0, 0.0, 0.0), 0.0),
            "category": "sofa", "posture": "lie", "groups": {"back": ["pelvis", "chest"], "head": ["head"], "heels": ["foot"]}}


@tableau
def head_hands():
    up_p, up_s = pitch(18), pitch(52)
    P = [0.0, 0.42 + 0.097, 0.13]
    _, n1 = _seated_points(P, up_p, up_s)
    hpitch = 12.0
    hy = rot_axis(up_s, [1, 0, 0], hpitch)
    hz = norm(rot_axis(pitch_f(52), [1, 0, 0], hpitch))
    head = add(n1, mul(hy, F.HEAD_OVER_NECK))
    hands = {}
    for s, sg in (("R", -1.0), ("L", 1.0)):
        cheek = add(add(head, [sg * 0.05, 0.0, 0.0]), mul(hz, 0.1))
        wrist = add(cheek, mul(hy, -0.17))
        hands[s] = {"wrist": wrist, "elbow": [sg * 0.13, 0.62, 0.52], "dir": norm(add(hy, mul(hz, -0.25))),
                    "palm": mul(hz, -1.0)}
    pose = {"P": P, "Up": up_p, "U": up_s, "F": [0, 0, 1], "head": {"pitch": hpitch}, "hands": hands,
            "feet": {"R": {"ankle": [-0.13, 0.05, 0.6], "pole": [0, 0.4, 1]}, "L": {"ankle": [0.13, 0.05, 0.62], "pole": [0, 0.4, 1]}}}
    return {"pose": pose, "anchor": (PR.sofa3, (0.0, 0.0, 0.0), 0.0), "category": "sofa", "posture": "sit",
            "groups": {"seat": ["thigh", "pelvis"], "feet": ["foot"]}}


@tableau
def window():
    pose = {"P": [0.0, 0.935, 0.0], "U": pitch(4), "head": {"pitch": 10.0, "yaw": 6.0},
            "hands": {"R": {"wrist": [-0.2, 0.68, 0.05], "pole": [-0.3, 0, -1]},
                      "L": {"wrist": [0.23, 0.69, 0.07], "pole": [0.3, 0, -1]}},
            "feet": {"R": {"ankle": [-0.11, 0.05, 0.02]}, "L": {"ankle": [0.12, 0.05, -0.04]}}}
    # the anchor: the floor and a wall 0.5 m in front with a window in it (the room supplies the real one)
    wall = K.D(K.X(((-1.5, 1.5), (0.0, 2.6), (0.5, 0.65))), K.X(((-0.6, 0.6), (0.9, 2.2), (0.3, 0.9))))
    return {"pose": pose, "anchor": (lambda: wall, (0.0, 0.0, 0.0), 0.0), "category": "window", "posture": "stand",
            "groups": {"feet": ["foot"]}}


def table_group(eid_prefix=None, room=None):
    """The kitchen chair at the origin facing +Z and the table in front of it (near edge z = 0.35)."""
    ent = (lambda i, **kw: dict({"id": f"{eid_prefix}{i}", "room": room}, **kw)) if eid_prefix else (lambda i, **kw: None)
    return K.U(K.chair(entity=ent("Chair", anchor=f"{eid_prefix}Table")), K.place(K.table(1.1, 0.75, 0.75, entity=ent("Table")), (0.0, 0.0, 0.35 + 0.375)))


def desk_group(eid_prefix=None, room=None):
    """The office chair at the origin facing +Z; the desk, its CRT monitor and keyboard 0.6 m in front, facing it."""
    import liminal_space as ls
    ent = (lambda i, **kw: dict({"id": f"{eid_prefix}{i}", "room": room}, **kw)) if eid_prefix else (lambda i, **kw: None)
    chair = PR.office_chair()
    mon, kb = PR.crt_monitor(), PR.keyboard()
    if eid_prefix:
        ls.tag(chair, "chair", id=f"{eid_prefix}Chair", room=room, anchor=f"{eid_prefix}Desk", seatHeight=0.47)
        ls.tag(mon, "monitor", id=f"{eid_prefix}Monitor", room=room)
        ls.tag(kb, "prop", id=f"{eid_prefix}Keyboard", room=room)
    return K.U(chair, K.place(K.U(K.desk(1.3, 0.62, 0.75, entity=ent("Desk")), K.place(mon, (-0.2, 0.75, -0.06)), K.place(kb, (-0.2, 0.75, 0.16))),
                              (-0.2, 0.0, 0.6), 180.0))


@tableau
def table():
    # the chair at the origin facing +Z, pulled in; the table's near edge at z = 0.35
    up_p, up_s = pitch(10), pitch(36)
    P = [0.0, 0.46 + 0.097, 0.02]
    _, n1 = _seated_points(P, up_p, up_s)
    hpitch = -6.0
    hy = rot_axis(up_s, [1, 0, 0], hpitch)
    hz = norm(rot_axis(pitch_f(36), [1, 0, 0], hpitch))
    head = add(n1, mul(hy, F.HEAD_OVER_NECK))
    hands = {}
    for s, sg in (("R", -1.0), ("L", 1.0)):
        jaw = add(add(add(head, [sg * 0.06, 0.0, 0.0]), mul(hy, -0.07)), mul(hz, 0.05))
        hands[s] = {"wrist": add(jaw, mul(hy, -0.13)), "elbow": [sg * 0.17, 0.8, 0.42], "dir": norm(add(hy, mul(hz, -0.2))),
                    "palm": [-sg, 0.0, 0.0]}
    pose = {"P": P, "Up": up_p, "U": up_s, "F": [0, 0, 1], "head": {"pitch": hpitch}, "hands": hands,
            "feet": {"R": {"ankle": [-0.12, 0.05, 0.4], "pole": [0, 0.4, 1]}, "L": {"ankle": [0.13, 0.05, 0.36], "pole": [0, 0.4, 1]}}}
    return {"pose": pose, "anchor": (table_group, (0.0, 0.0, 0.0), 0.0), "category": "chair", "posture": "sit",
            "groups": {"seat": ["thigh", "pelvis"], "feet": ["foot"], "elbows": ["farm", "elbow"]}}


@tableau
def mirror():
    up_p, up_s = pitch(6), pitch(24)
    P = [0.0, 0.93, -0.02]
    hands = {s: {"wrist": [sg * 0.2, 0.905, 0.25], "pole": [sg * 0.6, 0.0, -1.0], "dir": [0.0, -0.05, 1.0],
                 "palm": [0.0, 1.0, 0.0]} for s, sg in (("R", -1.0), ("L", 1.0))}
    pose = {"P": P, "Up": up_p, "U": up_s, "F": [0, 0, 1], "head": {"pitch": 26.0}, "hands": hands,
            "feet": {"R": {"ankle": [-0.12, 0.05, -0.04]}, "L": {"ankle": [0.12, 0.05, 0.0]}}}
    return {"pose": pose, "anchor": (PR.vanity, (0.0, 0.0, 0.5), 180.0), "category": "sink", "posture": "stand",
            "groups": {"feet": ["foot"], "hands": ["hand"]}}


@tableau
def toilet():
    up_p, up_s = pitch(10), pitch(42)
    P = [0.0, 0.42 + 0.097, -0.03]
    hands = {s: {"wrist": [sg * 0.05, 0.36, 0.43], "elbow": [sg * 0.15, 0.55, 0.27], "dir": [0.0, -1.0, 0.25]}
             for s, sg in (("R", -1.0), ("L", 1.0))}
    pose = {"P": P, "Up": up_p, "U": up_s, "F": [0, 0, 1], "head": {"pitch": 20.0}, "hands": hands,
            "feet": {"R": {"ankle": [-0.13, 0.05, 0.43], "pole": [0, 0.4, 1]}, "L": {"ankle": [0.13, 0.05, 0.45], "pole": [0, 0.4, 1]}}}
    return {"pose": pose, "anchor": (PR.toilet, (0.0, 0.0, 0.0), 0.0), "category": "toilet", "posture": "sit",
            "groups": {"seat": ["thigh", "pelvis"], "feet": ["foot"]}}


@tableau
def bed():
    U_ = norm([0.0, 0.05, -1.0])
    P = [0.05, 0.52 + 0.1 - 0.015, 0.06]
    pose = {"P": P, "U": U_, "Up": U_, "F": [0.0, 1.0, 0.0], "head": {"pitch": 47.0},
            "hands": {"R": {"wrist": add(P, [0.03, 0.27, -0.66]), "elbow": add(P, [-0.3, 0.18, -0.55]), "dir": [1.0, 0.1, 0.0],
                            "palm": [0.0, -1.0, 0.0]},
                      "L": {"wrist": add(P, [0.27, -0.03, 0.08]), "pole": [1.0, -0.2, 0.0], "dir": [0.1, -0.1, 1.0],
                            "palm": [0.0, 1.0, 0.0]}},
            "feet": {"R": {"ankle": add(P, [-0.12, -0.02, 0.86]), "pole": [0, 1, 0], "dir": [0.0, 1.0, 0.25], "up": [0, 0.25, -1]},
                     "L": {"ankle": add(P, [0.12, -0.02, 0.86]), "pole": [0, 1, 0], "dir": [0.1, 1.0, 0.25], "up": [0, 0.25, -1]}}}
    return {"pose": pose, "anchor": (PR.bed3, (0.0, 0.0, 0.0), 0.0), "category": "bed", "posture": "lie",
            "groups": {"back": ["pelvis", "chest"], "head": ["head"], "legs": ["thigh", "shin"]}}


@tableau
def desk():
    up_p, up_s = pitch(8), pitch(16)
    P = [0.0, 0.47 + 0.097, -0.02]
    hands = {s: {"wrist": [sg * 0.14, 0.795, 0.47], "elbow": [sg * 0.21, 0.8, 0.24], "dir": [-sg * 0.25, 0.0, 1.0],
                 "palm": [0.0, 1.0, 0.0]} for s, sg in (("R", -1.0), ("L", 1.0))}
    pose = {"P": P, "Up": up_p, "U": up_s, "F": [0, 0, 1], "head": {"pitch": -2.0}, "hands": hands,
            "feet": {"R": {"ankle": [-0.13, 0.05, 0.42], "pole": [0, 0.4, 1]}, "L": {"ankle": [0.12, 0.05, 0.38], "pole": [0, 0.4, 1]}}}
    return {"pose": pose, "anchor": (desk_group, (0.0, 0.0, 0.0), 0.0), "category": "office_chair", "posture": "sit",
            "groups": {"seat": ["thigh", "pelvis"], "feet": ["foot"], "forearms": ["farm", "hand"]}}


@tableau
def bed_edge():
    up_p, up_s = pitch(8), pitch(30)
    P = [0.0, 0.52 + 0.125, 0.06]
    hands = {s: {"wrist": [sg * 0.25, 0.705, 0.06], "pole": [sg, 0.0, -0.4], "dir": [sg * 0.2, -1.0, 0.1],
                 "palm": [-sg, 0.0, 0.0]} for s, sg in (("R", -1.0), ("L", 1.0))}
    pose = {"P": P, "Up": up_p, "U": up_s, "F": [0, 0, 1], "head": {"pitch": 34.0}, "hands": hands,
            "feet": {"R": {"ankle": [-0.13, 0.05, 0.47], "pole": [0, 0.4, 1]}, "L": {"ankle": [0.13, 0.05, 0.5], "pole": [0, 0.4, 1]}}}
    # sitting on the bed's long side: the bed turned so its side faces the figure
    return {"pose": pose, "anchor": (PR.bed3, (0.22, 0.0, -0.55), 90.0), "category": "bed", "posture": "sit",
            "groups": {"seat": ["thigh", "pelvis"], "feet": ["foot"]}}


@tableau
def stair():
    pose = {"P": [0.0, 0.935, 0.0], "U": pitch(-3), "head": {"pitch": -24.0},
            "hands": {"R": {"wrist": [-0.21, 0.68, -0.02]}, "L": {"wrist": [0.22, 0.68, -0.02]}},
            "feet": {"R": {"ankle": [-0.11, 0.05, 0.0]}, "L": {"ankle": [0.11, 0.05, 0.04]}}}
    return {"pose": pose, "anchor": (lambda: FLOOR, (0.0, 0.0, 0.0), 0.0), "category": "floor", "posture": "stand",
            "groups": {"feet": ["foot"]}}


@tableau
def floor_sit():
    # on the floor, the back against a wall 0.3 m behind, knees up, forearms on the knees, the head down
    up_p, up_s = pitch(-12), pitch(-4)
    P = [0.0, 0.072, -0.12]
    pose = {"P": P, "Up": up_p, "U": up_s, "F": [0, 0, 1], "head": {"pitch": 38.0},
            "hands": {"R": {"wrist": [0.06, 0.58, 0.3], "elbow": [-0.17, 0.62, 0.2], "dir": [0.6, -0.4, 0.3]},
                      "L": {"wrist": [-0.04, 0.6, 0.33], "elbow": [0.18, 0.64, 0.22], "dir": [-0.6, -0.5, 0.3]}},
            "feet": {"R": {"ankle": [-0.13, 0.05, 0.4], "pole": [0, 1, 0.2]}, "L": {"ankle": [0.14, 0.05, 0.43], "pole": [0, 1, 0.2]}}}
    wall = K.X(((-1.5, 1.5), (0.0, 2.6), (-0.6, -0.31)))
    return {"pose": pose, "anchor": (lambda: wall, (0.0, 0.0, 0.0), 0.0), "category": "floor", "posture": "floor",
            "groups": {"seat": ["pelvis"], "feet": ["foot"], "back": ["chest", "pelvis"]}}


@tableau
def gallery():
    # bridge 2: upright on a chair in the middle of the room of objects, hands on the knees, head a little down
    up_p, up_s = pitch(2), pitch(6)
    P = [0.0, 0.46 + 0.097, -0.02]
    pose = {"P": P, "Up": up_p, "U": up_s, "F": [0, 0, 1], "head": {"pitch": 14.0},
            "hands": {s: {"wrist": [sg * 0.12, 0.6, 0.36], "pole": [sg * 0.4, 0.0, -1.0], "dir": [0.0, -0.4, 1.0],
                          "palm": [0.0, 1.0, 0.0]} for s, sg in (("R", -1.0), ("L", 1.0))},
            "feet": {"R": {"ankle": [-0.12, 0.05, 0.42], "pole": [0, 0.4, 1]}, "L": {"ankle": [0.12, 0.05, 0.42], "pole": [0, 0.4, 1]}}}
    return {"pose": pose, "anchor": (K.chair, (0.0, 0.0, 0.0), 0.0), "category": "chair", "posture": "sit",
            "groups": {"seat": ["thigh", "pelvis"], "feet": ["foot"]}}


@tableau
def hill():
    # the dawn: standing on the hilltop, facing the sunrise, arms hanging, the head lifted a little
    pose = {"P": [0.0, 0.935, 0.0], "U": pitch(-2), "head": {"pitch": -9.0},
            "hands": {"R": {"wrist": [-0.22, 0.69, 0.0]}, "L": {"wrist": [0.22, 0.69, 0.0]}},
            "feet": {"R": {"ankle": [-0.11, 0.05, 0.02]}, "L": {"ankle": [0.11, 0.05, -0.02]}}}
    return {"pose": pose, "anchor": (lambda: FLOOR, (0.0, 0.0, 0.0), 0.0), "category": "floor", "posture": "stand",
            "groups": {"feet": ["foot"]}}


# ---- building and placing ------------------------------------------------------------------------------------

def build(name, fig_name):
    """(Figure, anchor tree in the figure's frame, the tableau's spec)."""
    spec = TABLEAUX[name]()
    fig = F.Figure(spec["pose"], fig_name)
    builder, at, yaw = spec["anchor"]
    anchor = K.place(builder(), at, yaw)
    return fig, anchor, spec


VALIDATOR_POSE = {"thinker": "thinker", "couch_lie": "couchLying", "head_hands": "headInHands", "window": "window",
                  "table": "elbowsOnTable", "mirror": "mirror", "toilet": "toilet", "bed": "bedLying", "desk": "desk",
                  "bed_edge": "sit", "stair": "stand", "floor_sit": "floorSit", "gallery": "sit", "hill": "stand"}


def placed(name, fig_name, at=(0.0, 0.0, 0.0), yaw=0.0, room=None, anchor_id=None, t0=None, t1=None):
    """The tableau's figure placed in a room: (tree, bounds lo, bounds hi). The figure's frame goes to `at`,
    turned `yaw`; the anchor furniture belongs to the room (rooms3.fig_from_anchor puts the figure where its
    furniture is). The figure is tagged for the validator: its pose, its anchor, and for a seated pose the hip
    point that rests on the seat."""
    import liminal_space as ls
    fig, _, spec = build(name, fig_name)
    tree = K.place(fig.tree, at, yaw)
    lo, hi = F.placed_bounds(fig.bounds(), at, yaw)
    P = fig.points["pelvis"]
    hip = [P[0], P[1] - 0.097, P[2]] if spec["posture"] == "sit" else list(P)
    ls.tag(fig.tree, "mannequin", id=fig_name, room=room, anchor=anchor_id, pose=VALIDATOR_POSE.get(name, "stand"),
           tableau=name, hip=[round(v, 4) for v in hip], t0=t0, t1=t1)
    return tree, lo, hi


def anchor_place(name, at=(0.0, 0.0, 0.0), yaw=0.0):
    """Where the tableau's anchor furniture goes in the room when the figure's frame is at (at, yaw)."""
    spec = TABLEAUX[name]()
    _, a_at, a_yaw = spec["anchor"]
    r = math.radians(yaw)
    c, s = math.cos(r), math.sin(r)
    x, y, z = a_at
    return (at[0] + x * c + z * s, at[1] + y, at[2] - x * s + z * c), yaw + a_yaw


def report():
    ok_all = True
    for name in TABLEAUX:
        fig, anchor, spec = build(name, "m")
        tree = K.U(anchor, FLOOR)
        rows, ok = F.contacts(fig, tree, spec["groups"])
        ok_all &= ok
        n = K.count(fig.tree)[0]
        print(f"{name:<11} {spec['posture']:<5} on {spec['category']:<12} {n} nodes  {'OK' if ok else 'FIX'}")
        print("\n".join(rows))
    return ok_all


if __name__ == "__main__":
    sys.exit(0 if report() else 1)
