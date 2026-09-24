#!/usr/bin/env python3
"""One arm per local volume primitive (ADR-566, the brief's section 9).

    python3 tools/make_fog_primitive_arms.py

Six arms -- Bank, Sphere, Ellipsoid, Box, Capsule, Cylinder -- identical in every number except
the `shape` row. That is the whole design of the set: if two arms differ, the difference is the
primitive, because nothing else moved.

**Why this is the first fog work that can produce a picture.** ADR-563's acceptance arm came back a
featureless wash and ADR-560 had already said why: a 900 m bank centred under the island encloses
the camera, and a volume you are standing inside has no silhouette. A local primitive is the first
medium small enough for a camera to stand outside and look at, which is exactly what section 9
asks for -- "the artist should be able to place several fog banks around the world" -- and it is
why C is the phase where the fog stops being a number and becomes something to look at.

So these are placed to be SEEN: 140 m along the long axis, hanging in clear air 316 m down the
camera's own look direction, with the camera comfortably outside every one of them. Detail is at zero on
purpose. The brief's Definition of Done is that the fog reads as fog with every noise control off,
and a shape you can only see once noise is on is not a shape.

Density is authored as an OPTICAL DEPTH through the volume's own crossing path (ADR-564), so all
six arms are equally thick however differently they are shaped -- a box is not darker than a
sphere here because it holds more air, it is darker only where it is deeper.
"""

import json
import sys
from pathlib import Path

# ADR-578: THE SCENE CARRIES A MEDIUM OF ITS OWN AND THESE ARMS DID NOT KNOW.
#
# Before ADR-702 a project's `atmosphericEffects` MERGED with the scene's, so every arm rendered the
# scene's own medium too, and it was switched off by a parameter here. ADR-702: a project's
# `effects` array replaces the scene's list outright, so there is nothing to switch off. See
# tools/make_fog_arms.py for the whole record.

HERE = Path(__file__).resolve().parent.parent
EX = HERE / "examples" / "treeisland"
PROJECT = EX / "tree-of-life-floating-island.json"

WIDTH, HEIGHT = 1280, 720

SHAPES = ["Bank", "Sphere", "Ellipsoid", "Box", "Capsule", "Cylinder"]

# The placement. Small enough that the camera is outside it, high enough to sit against the sky
# rather than against the island, and rotated so the long axis is across the view -- an elongated
# primitive seen end-on is a sphere.
VOLUME = {
    # 316 m down the camera's own look direction and 60 m below it: far enough that the camera is
    # outside (the volume's longest half-axis is 140 m), near enough that a 36-degree lens gives it
    # most of the frame. The first placement tried was 150 m across beside the island and put the
    # camera INSIDE the soft rim -- which renders as the featureless white wash ADR-560 and
    # ADR-563 both already recorded, arrived at a third time.
    "center": [-53.9, -35.6, -85.7],
    "radius": 55.0,
    "thickness": 28.0,
    "density": 1.3,      # ADR-564: an optical depth through the crossing, not a per-metre rate
    "emission": 0.0,
    "contrast": 1.5,
    "turbulence": 0.0,
    "turbulenceScale": 1.0,
    "smokeWarp": 0.0,
    "smokeBillow": 0.0,
    "detail": 0.0,
    "filaments": 0.0,
    "rotationSpeed": 0.0,
    "breathAmount": 0.0,
    "breathSpeed": 0.0,
    "scattering": 0.6,
    "spill": 0.4,
    "colorDeep": [0.060, 0.075, 0.100],
    "colorMid": [0.180, 0.210, 0.250],
    "colorAccent": [0.300, 0.340, 0.390],
    "swirl": 0.0,
    "funnelDepth": 0.0,
    "throat": 1.0,
    "throatDensity": 0.0,
    "innerVoid": 0.0,
    "cometResponse": 0.0,
    "cloudNoise": 0.0,    # the bar: shape with no noise in it anywhere
    "eyeWallWidth": 0.0,
    "eyeWallGain": 0.0,
    "bandArms": 0.0,
    "bandDepth": 0.0,
    "bandHarmonic": 0.0,
}

FOG_ROWS = {
    "bankLength": 2.0,
    "bankRotation": 62.0,
    "edgeSoftness": 0.18,
    "groundHug": 0.5,
    "heightFalloff": 1.4,
    "domeShape": 0.0,
    "detailScale": 6.0,
    "detailDrift": 0.0,
    "heightInfluence": 0.0,
}


def load_project() -> dict:
    text = PROJECT.read_text()
    data = json.loads(text)
    if json.dumps(data, indent=2) + "\n" != text:
        raise SystemExit(
            f"{PROJECT} no longer round-trips through json.dumps(indent=2). Refusing to rewrite "
            f"it (the same guard tools/make_fog_arms.py carries, and for the same reason)."
        )
    return data


def as_primitive(project: dict, shape: str) -> None:
    # ADR-702's canonical entry. A fog bank's medium rows alias the vortex payload and are read from
    # the `vortex` block inside `parameters`, beside its own stored rows (FOG_ROWS, the shape).
    for effect in project["effects"]:
        if effect.get("type") in ("vortex", "tornado", "fog"):
            effect["type"] = "fog"
            effect["id"] = f"fog-{shape.lower()}"
            effect["name"] = f"Fog {shape}"
            effect["parameters"] = dict(FOG_ROWS, shape=shape, vortex=dict(VOLUME))
            effect.pop("ground", None)
            return
    raise SystemExit("no medium effect in the project to replace")


def main() -> int:
    for shape in SHAPES:
        p = load_project()
        as_primitive(p, shape)
        p["parameters"]["post/bloom/enabled"] = False
        # 256 steps, deliberately. These are SHAPE arms, not cost arms: at the shipped 32 the
        # medium is UNDERSAMPLED along the ray and every primitive reads as the same speckled blob,
        # which is section 48's point made against my own diagnostic --
        #
        # ADR-577 corrects what that speckle IS. It was written here as "the march's own sampling
        # grain", which reads as the per-pixel start jitter, and it is not: measured, the jitter's
        # contribution to a frame's high-frequency content at 32 steps and above is a ratio of
        # **1.000** against the jitter switched off. The speckle is the medium itself sampled too
        # coarsely along the ray -- a different artefact with a different fix (more steps, or §31's
        # adaptive sampling), and the jitter is what BREAKS UP its banding rather than what causes
        # it. A misattributed cause is worse than an unexplained one: it sends the next person to
        # tune the wrong knob.
        # a badly sampled shape is not a shape you can judge. The cost question is phase I's and
        # has its own arms under the lock.
        p["parameters"]["scene/volumeSteps"] = 256
        p["render"]["width"] = WIDTH
        p["render"]["height"] = HEIGHT
        out = EX / f"_fogshape-{shape.lower()}.json"
        out.write_text(json.dumps(p, indent=2) + "\n")
        print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
