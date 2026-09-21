#!/usr/bin/env python3
"""Derive the Fog Bank diagnostic arms from the shipped Tree of Life project.

    python3 tools/make_fog_arms.py

Answers the fog brief's section 3 -- "is the visible artifact density noise, march undersampling,
stochastic sampling noise, temporal instability, shadow noise, excessive high-frequency detail, or
a combination" -- by rendering the same frame with ONE thing changed at a time.

Why it derives from the PROJECT and not from the scene file: ADR-264. The project's `parameters`
are applied over its scene, and the two disagree about the thing under test -- the scene says
`volumeSteps: 48` and the project overrides it to 32. A measurement taken from the scene file is a
measurement of a file nobody renders.

Why the project and not the scene is also *safe* to rewrite with json.load/json.dump, where
`make_treeisland_arms.py` and `make_vortex2_arms.py` both refuse to: the project is machine-written
and round-trips byte-identically through `json.dumps(indent=2)`, which this script asserts before it
writes anything. The scene file is hand-written and does not, which is why nothing here touches it.

Every arm is the shipped project with the "Cosmic Vortex" atmospheric effect REPLACED by a fog bank
of the same placement class -- a bank of medium under the island, filling the lower frame, which is
the region ADR-389 established the grain metric must be restricted to (the tree's foliage dominates
the high-frequency energy of the whole frame and moves by 3% across arms that move the medium's own
grain by 54%).

The arms:

  base            the fog bank as an artist gets it today: the `Valley Mist` preset's shaping
                  numbers, `cloudNoise` at its default 1. The "before".
  nonoise         section 4 D. `cloudNoise` 0: the fBM stack contributes nothing and the density is
                  the macro envelope alone. The single most important arm in the set, because the
                  brief's Definition of Done is that this arm must still look like fog.
  nojitter        section 4 E. `volumeJitter` 0 (ADR-461): the march's own per-pixel start offset.
  nonoise-nojitter  both, which is the floor this march can reach at the shipped step count.
  steps256        section 4 G. 256 steps, jitter on.
  steps256-nojitter  256 steps, jitter off: the reference the other arms are read against.
  flat            section 4 A. NO fog bank at all -- `Environment::volumeDensity` with
                  `fogHeightFalloff` 0 and `volumeNoise` 0, which IS a perfectly uniform volume and
                  needs no new code. Determines whether the volume renderer itself produces noise.
  gradient        section 4 B. The same, with a height falloff: a smooth analytical gradient with no
                  noise anywhere, which validates the renderer against a known-clean field.

Sections 4 F (temporal accumulation) and 4 H (shadowing) have no arm because the machinery they
name does not exist in this renderer: `rendering/volume_renderer.cpp` has no history buffer and no
reprojection (ADR-143 rejected it and ADR-460 re-confirmed the reopening trigger is shut), and
`shaders/volume.wgsl` says so in its own words -- "There is no shadowing in the fog".

Bloom is off in every arm and particles are disabled at the command line, so the arms differ in the
medium and in nothing else.
"""

from __future__ import annotations

import json
import shutil
import sys
from pathlib import Path

# ADR-578: THE SCENE CARRIES A MEDIUM OF ITS OWN AND THESE ARMS DID NOT KNOW.
#
# `tree-of-life-floating-island.scene.json` declares a `Cosmic Vortex`, and a project's
# `atmosphericEffects` list MERGES with the scene's rather than replacing it -- so every arm this
# file has ever written rendered the vortex as well as whatever it placed. The generator's own
# comments said otherwise.
#
# It was invisible until §39's "active volume count" reached the headless log (ADR-578): the first
# arm run after that printed `media=2` where one had been placed, and the second number was the
# scene's. Nothing else in the record would have said so.
#
# What it did and did not invalidate: the vortex was CONSTANT across every arm in a set, so a
# difference between two arms is still the parameter that differs -- the comparisons stand. What
# does not stand is any sentence claiming an arm contained one medium, or none. Those are corrected
# in place, and the vortex is switched off by parameter below.
DISABLE_SCENE_MEDIUM = {"atmos/Cosmic Vortex/enabled": False}

HERE = Path(__file__).resolve().parent.parent
EX = HERE / "examples" / "treeisland"
PROJECT = EX / "tree-of-life-floating-island.json"

# Where the bank stands and how big it is. The vortex it replaces is centred at (0, -70, 0) with a
# 200 m mouth; this is a bank of mist in the same air, sized to fill the lower frame the grain
# window is restricted to. The camera stands at (197.7, 45.3, 83.5), 215 m from the axis, so a
# 900 m bank centred 160 m below the island puts the camera above and outside it looking down into
# it -- which is a fog bank seen from outside, the case the brief's sections 9 and 10 are about.
BANK = {
    "center": [0.0, -160.0, 0.0],
    "radius": 900.0,
    "thickness": 250.0,
    # `Valley Mist`'s shaping, verbatim from `volumetric_fog_effect.cpp`'s `applyStyle`, except
    # `density` and `emission`, which are per-metre coefficients and are scaled to this scene's
    # 4 km march rather than to the preset's unstated one. ADR-374/379/381/389: a coefficient tuned
    # against a quantity is invalidated by a change to that quantity's distribution.
    "density": 0.0016,
    "emission": 0.020,
    "contrast": 1.5,
    "turbulence": 0.35,
    "turbulenceScale": 1.1,
    "smokeWarp": 0.9,
    "smokeBillow": 0.35,
    "detail": 0.25,
    "filaments": 0.0,
    "rotationSpeed": 0.012,
    "breathAmount": 0.06,
    "breathSpeed": 0.09,
    "scattering": 0.6,
    "spill": 0.8,
    "colorDeep": [0.050, 0.062, 0.085],
    "colorMid": [0.140, 0.170, 0.210],
    "colorAccent": [0.230, 0.270, 0.320],
    # What makes it a bank rather than a funnel: the four numbers `applyStyle` writes.
    "swirl": 0.0,
    "funnelDepth": 0.0,
    "throat": 1.0,
    "throatDensity": 0.0,
    "innerVoid": 0.0,
    "cometResponse": 0.0,
    # The default an artist gets, stated rather than inherited so the arms can move it.
    "cloudNoise": 1.0,
    "eyeWallWidth": 0.22,
    "eyeWallGain": 0.0,
    "bandArms": 0.0,
    "bandDepth": 0.0,
    "bandHarmonic": 0.0,
}

# ADR-389's measurement conditions, so these numbers are commensurable with ADR-389/460/461's.
WIDTH, HEIGHT = 1280, 720


def load_project() -> dict:
    text = PROJECT.read_text()
    data = json.loads(text)
    if json.dumps(data, indent=2) + "\n" != text:
        raise SystemExit(
            f"{PROJECT} no longer round-trips through json.dumps(indent=2). Rewriting it would "
            f"churn unrelated float literals and bury the change -- refusing (ADR-264's companion "
            f"lesson in tools/make_treeisland_arms.py)."
        )
    return data


def as_fog_bank(project: dict) -> None:
    """Replace the Cosmic Vortex effect with a fog bank of the same placement class."""
    for effect in project["atmosphericEffects"]:
        if effect.get("kind") == "vortex":
            effect["kind"] = "fog"
            effect["name"] = "Fog Bank"
            effect["style"] = "Valley Fog"
            effect["vortex"] = dict(BANK)
            return
    raise SystemExit("no vortex effect in the project to replace")


def no_medium(project: dict) -> None:
    """Remove the placed medium entirely, leaving `Environment`'s own fog as the only one."""
    project["atmosphericEffects"] = [
        e for e in project["atmosphericEffects"] if e.get("kind") not in ("vortex", "fog")
    ]


def write(name: str, project: dict) -> Path:
    project["post/bloom/enabled"] = None  # placeholder, replaced below
    del project["post/bloom/enabled"]
    project["parameters"]["post/bloom/enabled"] = False
    project["parameters"].update(DISABLE_SCENE_MEDIUM)  # ADR-578
    project["render"]["width"] = WIDTH
    project["render"]["height"] = HEIGHT
    out = EX / f"_fog-{name}.json"
    out.write_text(json.dumps(project, indent=2) + "\n")
    # The project names its scene by relative path and sha256; it is unchanged, and the arm sits in
    # the same directory so the relative path still resolves.
    return out


def main() -> int:
    arms: list[tuple[str, dict]] = []

    def arm(name: str, **params) -> None:
        p = load_project()
        as_fog_bank(p)
        p["parameters"].update(params)
        arms.append((name, p))

    arm("base")
    arm("nonoise")
    arms[-1][1]["atmosphericEffects"][-1]["vortex"]["cloudNoise"] = 0.0
    arm("nojitter", **{"scene/volumeJitter": 0.0})
    arm("nonoise-nojitter", **{"scene/volumeJitter": 0.0})
    arms[-1][1]["atmosphericEffects"][-1]["vortex"]["cloudNoise"] = 0.0
    arm("steps256", **{"scene/volumeSteps": 256})
    arm("steps256-nojitter", **{"scene/volumeSteps": 256, "scene/volumeJitter": 0.0})

    # Section 4 A and B need no fog bank and no new code: `Environment`'s own medium already IS a
    # constant field (falloff 0) and a smooth analytical gradient (falloff > 0), with the noise term
    # switched off by `volumeNoise` 0 -- which is what the shipped project already sets.
    flat = load_project()
    no_medium(flat)
    flat["parameters"].update({"scene/volumeDensity": 0.0016, "scene/fogHeightFalloff": 0.0,
                              "scene/volumeNoise": 0.0, "scene/volumeEmission": 0.02})
    arms.append(("flat", flat))
    gradient = load_project()
    no_medium(gradient)
    gradient["parameters"].update({"scene/volumeDensity": 0.0016, "scene/fogHeight": -160.0,
                                   "scene/fogHeightFalloff": 0.004, "scene/volumeNoise": 0.0,
                                   "scene/volumeEmission": 0.02})
    arms.append(("gradient", gradient))

    for name, project in arms:
        print(write(name, project))
    return 0


if __name__ == "__main__":
    sys.exit(main())
