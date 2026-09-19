#!/usr/bin/env python3
"""Build the foot IK lab scenes (ADR-359).

Two scenes, identical in every respect but one: a bull standing across a Glowmere hillside, with
and without the four `foot` pose layers. That pair is the arm. A single frame of the "after" proves
nothing on its own -- a bull standing on a slope looks like a bull standing on a slope -- and the
difference between the two is the only thing that shows whether the hooves found the ground.

The terrain is lifted verbatim out of `examples/world/glowmere-valley-2.scene.json` so the hillside
is the shipped one and not a ramp built to flatter the solver.
"""
import json
import os
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "examples" / "world" / "glowmere-valley-2.scene.json"
OUT = ROOT / "examples" / "labs" / "footik"

# The four legs, and no pole on any of them -- which is a correction, not an omission.
#
# The first version of this file authored a knee hint per leg, +Z on the front and -Z on the rear,
# from the anatomy. It rendered a bull with both hind legs folded up through its own body: every
# one of the four reported `solved`, every hoof was on its target to four decimal places, and the
# animal was inside out. A pole overrides the plane the *clip* is bending the knee in, and the clip
# is right. The default -- keep the animated bend plane -- cannot be wrong here, and the pole is
# for the one case that has no plane to keep: a leg the clip leaves dead straight.
CHAINS = [
    ("rear-left", ["UpperLegB.L", "LowerLegB.L", "HoofB.L"], None),
    ("rear-right", ["UpperLegB.R", "LowerLegB.R", "HoofB.R"], None),
    ("front-left", ["UpperLegF.L", "LowerLegF.L", "HoofF.L"], None),
    ("front-right", ["UpperLegF.R", "LowerLegF.R", "HoofF.R"], None),
]

# Where the bull stands and where the camera looks from. Chosen off a render rather than off a
# number: the point is a genuine bank with the animal across it, which is the case the whole unit
# is for and the one a flat lawn would hide.
BULL_XZ = (float(os.environ.get("FOOTIK_X", -222.96)), float(os.environ.get("FOOTIK_Z", -135.02)))
CAM_DIST = float(os.environ.get("FOOTIK_DIST", 3.6))
CAM_HEIGHT = float(os.environ.get("FOOTIK_CAMY", 0.35))
BULL_Y = float(os.environ.get("FOOTIK_Y", 51.8))
# -10.7 degrees: along the contour of this particular hillside, not a round number.
#
# It was 90 (straight across the fall line, nose down the hill) and the render settled the matter.
# A bull is 2.65 m long; a 19-degree bank drops 0.9 m over that, and its hind legs are 1.1 m long
# with four millimetres of straightening in them. Standing head-down that slope is not a pose this
# animal can make, and the solver said so honestly -- three of four hooves clamped -- while looking
# like a bug. Turned along the contour only the animal's 0.76 m width is on the slope, which is
# both what a real animal does on a hill and what this rig can reach.
BULL_YAW = float(os.environ.get("FOOTIK_YAW", -10.7))
# The camera looks along +X at the animal's near side, low, so the frame is mostly hooves and
# ground. A three-quarter hero shot of a bull is a picture of a bull; the thing under test is where
# four hooves meet a hillside, and it has to be big enough in frame to argue about.
CAM_DX = float(os.environ.get("FOOTIK_CAMDX", 0.983))
CAM_DZ = float(os.environ.get("FOOTIK_CAMDZ", 0.185))
CAMERA = [BULL_XZ[0] + CAM_DIST * CAM_DX, BULL_Y + CAM_HEIGHT, BULL_XZ[1] + CAM_DIST * CAM_DZ]
LOOK = [BULL_XZ[0], BULL_Y + 0.55, BULL_XZ[1]]


def layers():
    out = []
    for name, chain, pole in CHAINS:
        entry = {
            "name": name,
            "kind": "foot",
            "drive": "ground",
            "chain": chain,
            # No `groundOffset`: the plant preserves the height each hoof joint already stands at on
            # the flat (0.120 rig units on the rear leg, 0.097 on the front), read out of the rest
            # pose. "Stand on this slope the way you stand on the flat."
            #
            # 1.0: the sole lies on the slope. The per-foot twin of the body's `slopeAlign`, and
            # unlike the body's it wants to be all the way -- a hoof on a hillside is flat on the
            # hillside, it is the animal above it that only leans part of the way.
            "footAlign": 1.0,
        }
        if pole is not None:
            entry["poleDirection"] = pole
        out.append(entry)
    return out


def deepen(value):
    """Re-root every relative asset path one directory further down.

    The terrain, the environment and the material programs are copied out of `examples/world/`, and
    this lab lives in `examples/labs/footik/` -- one level deeper. A path that is not re-rooted does
    not fail loudly; it fails as a missing material program, which is the shape of an afternoon.
    """
    if isinstance(value, str):
        return "../" + value if value.startswith("../") else value
    if isinstance(value, list):
        return [deepen(v) for v in value]
    if isinstance(value, dict):
        return {k: deepen(v) for k, v in value.items()}
    return value


def build(with_layers):
    src = json.loads(SRC.read_text())
    terrain = deepen(next(n for n in src["nodes"] if n.get("kind") == "terrain"))

    bull = {
        "name": "bull",
        "kind": "gltf",
        "asset": "../../../assets/farm/bull.glb",
        "position": [BULL_XZ[0], BULL_Y, BULL_XZ[1]],
        "rotation": [0.0, BULL_YAW, 0.0],
        "scale": [1.94, 1.94, 1.94],
        "visible": True,
        "animation": {
            "state": "Walk",
            "speed": 0.0,
            "blend": 0.3,
            "nearDistance": 60.0,
            "cullDistance": 400.0,
        },
    }
    if with_layers:
        bull["animation"]["layers"] = layers()

    scene = {
        "format": "avgen-scene",
        "version": 1,
        "name": "foot-ik-lab" + ("" if with_layers else "-off"),
        "camera": {"mode": 1, "position": CAMERA, "target": LOOK, "fov": 40.0, "orbitSpeed": 0.0},
        "lightRig": "../../lightrigs/glowmere-valley.rig.json",
        "environment": deepen(src["environment"]),
        "nodes": [terrain, bull],
        "entities": [{
            "name": "bull",
            "node": "bull",
            "seed": 344,
            "cullDistance": 400.0,
            # `ground` and not `explore`: the bull stands. A wanderer would be somewhere else by the
            # time the frame is taken and the two renders would differ for a reason that has nothing
            # to do with the layers.
            "behaviors": [{
                "kind": "ground",
                "slopeAlign": 0.55,
                "bodyRadius": 1.2,
                "maxTilt": 34.0,
                # The bull is 2.65 m long and 0.76 m wide; the footprint has to be the animal's
                # span, not a person's, or the body reads a bank it does not actually stand on.
                "footprint": float(os.environ.get("FOOTIK_FP", 1.3)),
                # ADR-359, the other half of the move, and the reason the "off" scene sets it too:
                # the body drop is not part of what the layers do, so leaving it out of the control
                # would make the pair a comparison of two different things.
                "footDrop": float(os.environ.get("FOOTIK_FD", 0.75)),
            }],
        }],
    }
    if "post" in src:
        scene["post"] = deepen(src["post"])
    if "materialPrograms" in src:
        scene["materialPrograms"] = deepen(src["materialPrograms"])
    return scene


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for on, name in ((True, "foot-ik-lab.scene.json"), (False, "foot-ik-lab-off.scene.json")):
        path = OUT / name
        path.write_text(json.dumps(build(on), indent=1) + "\n")
        print("wrote", path.relative_to(ROOT))


if __name__ == "__main__":
    sys.exit(main())
