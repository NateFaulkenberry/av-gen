#!/usr/bin/env python3
"""Four arms of the global height layer (ADR-568, the brief's §7).

    python3 tools/make_height_fog_arms.py

Identical in every number except `fogUpperDensity` and `fogHeightCurve`, so a difference between
two arms is the control and nothing else.

  exponential   what the model was before ADR-568: a long soft tail that never quite ends.
  upper         a haze floor under it -- 30% of the layer's density at ANY height, so the sky
                behind the island is veiled instead of clear.
  curve         the compact quadratic: the layer reaches EXACTLY zero at 2/falloff above its top,
                which at these numbers is y = 213 m. Above that line there is no fog at all, and
                the point of the arm is that you can see where the line is.
  both          a thin global haze with a defined bank inside it, which is the combination §7's
                "dense near ground -> gradually thinner -> clear atmosphere, but allow custom
                curves" is asking for and which one exponential cannot express.

No placed medium in any of them -- which is true only since ADR-578 switched OFF the vortex the
SCENE declares. It was not true when these arms were first rendered, and the comment said it was.
The fog-bank effect's own vertical profile is a different control with a different ADR (563), and
mixing the two in one arm set would make it impossible to say which one moved the picture.
"""

import json
import sys
from pathlib import Path

# ADR-578: THE SCENE CARRIES A MEDIUM OF ITS OWN AND THESE ARMS DID NOT KNOW.
#
# Before ADR-702 a project's `atmosphericEffects` MERGED with the scene's, so every arm rendered the
# scene's own medium too, and it was switched off by a parameter here. ADR-702: a project's
# `effects` array replaces the scene's list outright, so removing the medium from the project's
# list removes it from the frame. See tools/make_fog_arms.py for the whole record.

HERE = Path(__file__).resolve().parent.parent
EX = HERE / "examples" / "treeisland"
PROJECT = EX / "tree-of-life-floating-island.json"

WIDTH, HEIGHT = 1280, 720

# The layer, chosen so its structure is inside the frame. The top sits 120 m below the island and
# the falloff has an e-fold of 167 m, so the visible band from the island to the top of frame is
# where the density actually changes -- a layer whose whole gradient is off screen is a flat tint.
BASE = {
    # Optical depth, computed before the first render rather than tuned after the third: the
    # march covers 4 km at `volumeAbsorption` 0.5, and the height term averages about 0.4 over the
    # visible band, so 0.0006 gives tau ~= 0.5 and a transmittance around 0.6. The first attempt
    # used 0.0016 and rendered a white wash -- the same mistake as the fog-primitive arms, made a
    # second time, which is why the arithmetic is written down here instead of the number.
    "scene/volumeDensity": 0.0006,
    "scene/fogHeight": -120.0,
    "scene/fogHeightFalloff": 0.006,
    "scene/volumeNoise": 0.0,
    "scene/volumeEmission": 0.006,
    "scene/volumeSteps": 128,
    "post/bloom/enabled": False,
}

ARMS = {
    "exponential": {"scene/fogUpperDensity": 0.0, "scene/fogHeightCurve": 0.0},
    "upper": {"scene/fogUpperDensity": 0.30, "scene/fogHeightCurve": 0.0},
    "curve": {"scene/fogUpperDensity": 0.0, "scene/fogHeightCurve": 1.0},
    "both": {"scene/fogUpperDensity": 0.18, "scene/fogHeightCurve": 1.0},
}


def load_project() -> dict:
    text = PROJECT.read_text()
    data = json.loads(text)
    if json.dumps(data, indent=2) + "\n" != text:
        raise SystemExit(
            f"{PROJECT} no longer round-trips through json.dumps(indent=2). Refusing to rewrite it."
        )
    return data


def main() -> int:
    for name, params in ARMS.items():
        p = load_project()
        # The placed medium goes, so the only fog in the frame is the layer under test.
        p["effects"] = [e for e in p["effects"] if e.get("type") not in ("vortex", "tornado", "fog")]
        # ADR-702: orders are contiguous per owner, and the loader refuses a stack that is not.
        counters: dict = {}
        for e in p["effects"]:
            owner = (e["owner"]["kind"], e["owner"].get("name", ""))
            e["order"] = counters.get(owner, 0)
            counters[owner] = e["order"] + 1
        p["parameters"].update(BASE)
        p["parameters"].update(params)
        p["render"]["width"] = WIDTH
        p["render"]["height"] = HEIGHT
        out = EX / f"_fogheight-{name}.json"
        out.write_text(json.dumps(p, indent=2) + "\n")
        print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
