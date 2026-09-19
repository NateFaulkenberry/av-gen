#!/usr/bin/env python3
"""The Image/Look Phase 0 baseline: eight captures of the image formation chain, into
`examples/imagelook/`.

    tools/make_imagelook_captures.py            # write every arm
    tools/make_imagelook_captures.py --clean    # remove them again
    tools/make_imagelook_captures.py --list     # names and what each one pins, no writes

**These eight are mine, not the owner's.** The Image/Look spec (`docs/image-look-spec.md` section 3)
asks for "the eight captures section 51.2 lists". Section 51.2's text is not in this repository --
only the revision, which names the deliverable without enumerating it -- so inventing eight and
attributing them to the owner would have been worse than saying so. This is a proposal. If the
original list turns up and disagrees, this file is the thing to change.

The arms are *data*, not code: every one is the same engine binary reading a different project, so
a difference between two of them cannot be a compiler flag, a shader edit or a stale `src/avgen`.
They are generated rather than committed for the reason `tools/make_mcperf_arms.py` gives -- the
repository has already decided not to carry near-identical scene files -- and because the recipe is
the part worth keeping.

Each arm isolates ONE decision in the chain documented in `docs/image-formation.md`, so a frame that
moves says which stage moved it. They are deliberately self-contained: procedural geometry and a
flat background, no external assets, so they render anywhere and a missing asset cannot be mistaken
for a regression.

The eighth is the one that matters most. `disabled` turns every optional stage off, and its job is
to stay byte-identical across any change that claims to be off by default. That is section 60/87 as
a standing tripwire rather than a one-off measurement (ADR-368).

Render one:

    tools/gpu-lock.sh ./build/release/src/avgen --project examples/imagelook/il-grey-ramp.json \\
        --render /tmp/il/grey-ramp --format png --range 2:2.04
"""

import argparse
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "examples" / "imagelook"
PREFIX = "il-"

# (name, what it pins, scene overrides, post/camera parameter overrides)
ARMS = [
    (
        "grey-ramp",
        "the tone curve: ten scene-linear grey patches from 0.002 to 50, the range the operator has "
        "to resolve. Moves when the curve, the encode or the exposure normalisation moves.",
        {"patches": "grey"},
        {},
    ),
    (
        "hue-wheel",
        "hue through AgX's inset matrix: six saturated patches at matched radiance. This is the "
        "measurement `post/tonemap/chroma-retention` exists to answer, so it moves when either does.",
        {"patches": "hue"},
        {},
    ),
    (
        "exposure",
        "the exposure stage and its 1e-3 deadband: the grey ramp at EV-2, where the scale is far "
        "enough from 1 that the pass is definitely encoded.",
        {"patches": "grey"},
        {"camera/exposure/compensation": -2.0},
    ),
    (
        "bloom",
        "the bloom pyramid's energy: one bright emitter on black at the shipped threshold. Moves "
        "when the prefilter, the level count or the energy-conserving upsample moves.",
        {"patches": "emitter"},
        {"post/bloom/enabled": True, "post/bloom/intensity": 0.6, "post/bloom/threshold": 1.0},
    ),
    (
        "defocus",
        "the shared defocus gather: depth of field and the ADR-079 tilt-shift band on together, "
        "which is the case the pass takes the larger circle for and the one no other arm covers.",
        {"patches": "depth"},
        {
            "post/dof/enabled": True,
            "post/dof/focusDistance": 8.0,
            "post/dof/maxRadius": 10.0,
            "post/tiltShift/enabled": True,
            "post/tiltShift/bandWidth": 0.25,
        },
    ),
    (
        "wide-tier",
        "halation and the anamorphic streak, the two tiers that share the wide texture. Both are "
        "off by default, so nothing else in this set would notice them changing.",
        {"patches": "emitter"},
        {
            "post/bloom/enabled": True,
            "post/halation/enabled": True,
            "post/halation/intensity": 0.7,
            "post/anamorphic/enabled": True,
            "post/anamorphic/intensity": 0.5,
        },
    ),
    (
        "look",
        "the ADR-368 cinematic integration, all four controls at a defined non-zero setting. The "
        "only arm in which post/look/* does anything, and the counterpart to `disabled`.",
        {"patches": "depth"},
        {
            "post/look/atmospheric": 0.45,
            "post/look/colour": 0.35,
            "post/look/localContrast": 0.5,
            "post/look/lightWrap": 0.3,
        },
    ),
    (
        "disabled",
        "THE TRIPWIRE. Every optional stage off and a unit exposure, so the chain is the composite "
        "alone. This frame must not move when a feature that claims to be off by default is added "
        "or changed -- section 60/87, ADR-368. A change here is either a real regression or a "
        "deliberate re-baseline, and there is no third case.",
        {"patches": "depth"},
        {
            "post/bloom/enabled": False,
            "post/bloom/intensity": 0.0,
            "post/halation/enabled": False,
            "post/anamorphic/enabled": False,
            "post/dof/enabled": False,
            "post/tiltShift/enabled": False,
            "post/motionBlur/amount": 0.0,
            "post/output/antialias": 0.0,
            "post/output/sharpen": 0.0,
            "post/output/vignette": 0.0,
            "post/output/grain": 0.0,
            "post/look/atmospheric": 0.0,
            "post/look/colour": 0.0,
            "post/look/localContrast": 0.0,
            "post/look/lightWrap": 0.0,
        },
    ),
]

# Scene-linear grey patches spanning the range the curve has to resolve, and saturated hues at a
# matched radiance. Both are laid out as a row of boxes at a fixed distance so a patch's screen
# position is stable across arms and a diff is read position by position.
GREY = [0.002, 0.0056, 0.018, 0.045, 0.18, 0.5, 1.0, 4.0, 16.0, 50.0]
HUE = [(2, 0, 0), (0, 2, 0), (0, 0, 2), (0, 2, 2), (2, 2, 0), (1, 0, 2)]


def patch_nodes(kind):
    """A row of emissive boxes. Emissive rather than lit, so a patch's value is the authored number
    and not the product of a light rig -- an arm that measures the curve must not also measure the
    lighting."""
    nodes = []
    if kind == "grey":
        for i, v in enumerate(GREY):
            nodes.append(_patch(f"grey{i}", (i - (len(GREY) - 1) / 2) * 1.25, (v, v, v)))
    elif kind == "hue":
        for i, c in enumerate(HUE):
            nodes.append(_patch(f"hue{i}", (i - (len(HUE) - 1) / 2) * 1.25, c))
    elif kind == "emitter":
        nodes.append(_patch("emitter", 0.0, (24.0, 18.0, 8.0), size=0.8))
    elif kind == "depth":
        # Three boxes at three distances, so depth-aware stages have something to separate.
        for i, (z, c) in enumerate([(2.0, (0.6, 0.25, 0.2)), (-6.0, (0.15, 0.55, 0.35)),
                                    (-22.0, (0.2, 0.3, 0.7))]):
            n = _patch(f"depth{i}", (i - 1) * 1.8, c, size=1.2)
            n["position"] = [(i - 1) * 1.8, 0.0, z]
            n["procedural"]["material"]["emissiveIntensity"] = 0.0
            n["procedural"]["material"]["baseColor"] = list(c)
            nodes.append(n)
        nodes.append({"name": "key", "kind": "light", "light": {
            "kind": "directional", "direction": [-0.4, -0.8, -0.45], "color": [1.0, 0.96, 0.9],
            "intensity": 3.0}})
    return nodes


def _patch(name, x, colour, size=1.0):
    return {
        "name": name, "kind": "procedural", "position": [x, 0.0, 0.0],
        "procedural": {
            "source": {"kind": "box", "size": [size, size, size], "subdivisions": 1},
            "distribution": {"kind": "single"},
            "material": {
                "baseColor": [0.0, 0.0, 0.0], "emissiveColor": list(colour),
                "emissiveIntensity": 1.0, "roughness": 0.5, "metallic": 0.0,
            },
        },
    }


def scene_doc(name, overrides):
    return {
        "format": "avgen-scene", "version": 1, "name": f"{PREFIX}{name}",
        # A fixed camera, never an orbit: an arm whose framing depends on the clock cannot be
        # compared frame to frame.
        "camera": {"mode": 0, "position": [0, 0, 12], "target": [0, 0, 0], "fov": 42.0,
                   "orbitSpeed": 0.0},
        "environment": {"intensity": 0.0, "background": [0.01, 0.012, 0.02], "fogDensity": 0.0},
        "nodes": patch_nodes(overrides.get("patches", "grey")),
    }


def project_doc(name, params):
    return {
        "format": "avgen-project", "version": 4, "app": {"name": f"Image/Look capture: {name}"},
        "assets": {"scene": {"kind": "composition", "path": f"{PREFIX}{name}.scene.json"}},
        "parameters": {k: v for k, v in params.items()},
        "routes": [], "sources": [], "presets": [], "timeline": {},
        "render": {"width": 1280, "height": 720, "fps": 30, "output": "sequence"},
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--clean", action="store_true", help="remove the arms instead of writing them")
    ap.add_argument("--list", action="store_true", help="print the set and exit")
    args = ap.parse_args()

    if args.list:
        for name, why, _, _ in ARMS:
            print(f"{PREFIX}{name}\n    {why}\n")
        return 0

    if args.clean:
        n = 0
        if OUT.is_dir():
            for f in sorted(OUT.glob(f"{PREFIX}*.json")):
                f.unlink()
                n += 1
            if not any(OUT.iterdir()):
                OUT.rmdir()
        print(f"removed {n} file(s)")
        return 0

    OUT.mkdir(parents=True, exist_ok=True)
    for name, _why, overrides, params in ARMS:
        (OUT / f"{PREFIX}{name}.scene.json").write_text(
            json.dumps(scene_doc(name, overrides), indent=2) + "\n")
        (OUT / f"{PREFIX}{name}.json").write_text(
            json.dumps(project_doc(name, params), indent=2) + "\n")
    print(f"wrote {len(ARMS)} arm(s) to {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
