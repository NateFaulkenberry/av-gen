#!/usr/bin/env python3
"""Derive the cosmic-key lighting arms from the deliverable project and scene.

    python3 tools/make_cosmickey_arms.py

Every file it writes begins with `_ck-` and is a *derivative*: the deliverable
(`examples/treeisland/tree-of-life-floating-island.{json,scene.json}`) with one thing changed.
Regenerate after any edit to the deliverable, or the comparison stops comparing what ships.

Camera arms -- the same lighting, seen from somewhere else (brief §15):

  _ck-view-wide     the deliverable's own camera, named so the set reads as a set.
  _ck-view-hero     the tree dominates the frame.
  _ck-view-under    below the plateau looking up: §8's underside and §15's low angle.
  _ck-view-orbit    a quarter turn the other way, onto the key's own side: the frame is mostly the
                    LIT face, which is where an over-bright key shows up.
  _ck-view-shadow   a quarter turn onto the key's opposite side, so the frame is mostly the SHADOW
                    side of the tree and the island. §6 lives or dies here.

The two turns are a quarter of a circle either way from the deliverable's camera, which stands at
world azimuth 46 degrees; the key comes from -50. So `orbit` looks down the key and `shadow` looks
into it, and between them they answer §15's "does the light still produce dimensionality when the
camera moves" in the two directions where the answer could differ.

Control arms -- ADR-182. Each removes exactly one thing, so a probe that passes on the
deliverable has something it must fail on:

  _ck-ctl-nokey     the key light disabled. Every key-to-shadow ratio must collapse toward 1.
                    Disabled through the PROJECT parameter and not the scene field: a project's
                    parameters are applied OVER the scene it loads (ADR-264), so the first version
                    of this arm wrote `"enabled": false` into the scene, had it overwritten by
                    `lights/celestial-key/enabled` a frame later, and produced a control whose
                    every statistic matched the arm it was controlling to five decimal places.
                    That is what a control which did not fire looks like, and it looks exactly
                    like an arm that does nothing.
  _ck-ctl-noshadow  the key still lit, `castsShadow` off. The shadow AOV must go uniformly white,
                    which is what tells a shadow measurement from a shading measurement.
  _ck-ctl-noglow    every emissiveBoost at 0. What the geometry looks like under the key alone --
                    brief §12's question, asked directly.
  _ck-ctl-nofill    the fill light disabled. The shadow-side-detail probe must fail on this one,
                    or it is measuring something the fill is not responsible for.

Evaluation arms -- not candidates for shipping, renders taken to answer a question in the brief:

  _ck-eval-volume   §13's volumetric beam, as far as this renderer goes: the volumetric march
                    switched on over the deliverable, with the key's `volumetric` at 1. The frame
                    is the answer, and the answer is that shaders/volume.wgsl does not sample the
                    shadow atlas -- "there is no shadowing in the fog" is a comment in it -- so a
                    directional light in-scatters uniformly and there is no shaft to be had.
  _ck-perf-noshadowrange  the deliverable with `shadowRange` back at 0. The timing control: it is
                    the shipping scene in every other respect, so the difference between the two
                    is what drawing three million triangles into four cascades costs.
"""

from __future__ import annotations

import json
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
EX = HERE / "examples" / "treeisland"
PROJECT = EX / "tree-of-life-floating-island.json"
SCENE = EX / "tree-of-life-floating-island.scene.json"

# position, target, fov. Chosen against the real bounds: the island is about 170 units across and
# hangs some 170 below its plateau; the canopy is 116 across and tops out near y = 78.
CAMERAS = {
    "wide":   None,  # the deliverable's own
    "hero":   ([104.0, 52.0, 100.0], [0.0, 46.0, 0.0], 34.0),
    "under":  ([104.0, -86.0, 100.0], [0.0, -18.0, 0.0], 44.0),
    "orbit":  ([-139.0, 24.0, 144.0], [0.0, 16.0, 0.0], 36.0),
    "shadow": ([153.0, 30.0, -129.0], [0.0, 16.0, 0.0], 36.0),
}

GLOW_NODES = ["tree-tracery", "tree-twigs", "tree-foliage", "tree-lumens"]


def disabled(project: dict, scene: dict, role: str):
    """The project with every light of `role` switched off, in both layers it can be switched off.

    Both, because either alone is a control that does not control: the scene field is what a
    reader of the scene file sees, and the project parameter is what actually reaches the frame."""
    out = json.loads(json.dumps(project))
    dark = json.loads(json.dumps(scene))
    for light in dark["lights"]:
        if light["role"] == role:
            light["enabled"] = False
            out["parameters"][f"lights/{light['name']}/enabled"] = False
            for preset in out.get("presets", []):
                preset["values"][f"lights/{light['name']}/enabled"] = [0.0]
    return out, dark


def write(stem: str, project: dict, scene: dict) -> None:
    project = json.loads(json.dumps(project))
    project["assets"]["scene"]["path"] = f"{stem}.scene.json"
    project["app"]["name"] = f"cosmickey {stem}"
    (EX / f"{stem}.json").write_text(json.dumps(project, indent=2) + "\n")
    (EX / f"{stem}.scene.json").write_text(json.dumps(scene, indent=2) + "\n")


def main() -> None:
    project = json.loads(PROJECT.read_text())
    scene = json.loads(SCENE.read_text())

    for name, cam in CAMERAS.items():
        s = json.loads(json.dumps(scene))
        if cam is not None:
            s["camera"]["position"], s["camera"]["target"], s["camera"]["fov"] = cam
        write(f"_ck-view-{name}", project, s)

    write("_ck-ctl-nokey", *disabled(project, scene, "key"))

    s = json.loads(json.dumps(scene))
    for light in s["lights"]:
        if light["role"] == "key":
            light["castsShadow"] = False
    write("_ck-ctl-noshadow", project, s)

    write("_ck-ctl-nofill", *disabled(project, scene, "fill"))

    s = json.loads(json.dumps(scene))
    s["environment"].update({
        "volumeDensity": 0.004,
        "fogHeight": 40.0,
        "fogHeightFalloff": 0.004,
        "volumeScattering": 1.0,
        "volumeAnisotropy": 0.72,
        "volumeSteps": 48,
        "volumeMaxDistance": 700.0,
    })
    for light in s["lights"]:
        light["volumetric"] = 1.0 if light["role"] == "key" else 0.0
    write("_ck-eval-volume", project, s)

    s = json.loads(json.dumps(scene))
    s["environment"]["shadowRange"] = 0.0
    write("_ck-perf-noshadowrange", project, s)

    p = json.loads(json.dumps(project))
    for node in GLOW_NODES:
        p["parameters"][f"nodes/{node}/emissiveBoost"] = 0.0
    write("_ck-ctl-noglow", p, json.loads(json.dumps(scene)))

    print(f"wrote {len(CAMERAS) + 6} arms to {EX}")


if __name__ == "__main__":
    main()
