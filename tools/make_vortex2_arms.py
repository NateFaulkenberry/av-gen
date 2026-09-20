#!/usr/bin/env python3
"""Derive the Vortex 2.0 comparison arms from the deliverable project.

    python3 tools/make_vortex2_arms.py

Every file it writes begins with `_vx2-` and is a *derivative*: the deliverable
(`examples/treeisland/tree-of-life-floating-island.{json,scene.json}`) with one thing changed.
Regenerate after any edit to the deliverable, or the comparison stops comparing what ships.

The edits are TEXTUAL, never json.load/json.dump: a round trip through Python's json rewrites
~140 unrelated float literals in that file and buries the change. `tools/make_treeisland_arms.py`
learned that first and says so.

The arms, and what each is evidence for:

  _vx2-macro        the brief's §5 and §50 checkpoint: the macro density field alone. The eye,
                    its wall and three nested sets of spiral arms switched on, `cloudNoise` at 0
                    so the fBM stack contributes nothing at all, and bloom off. Rendered with
                    `--disable particles`. The question it answers is §5's, and it is a yes/no:
                    "does this look like a giant cyclonic atmospheric system?"
  _vx2-macro-noise  the same storm with the noise back on, so the checkpoint image can be read
                    against what the detail stages will be modulating. NOT the deliverable: it is
                    the macro arm plus noise, so the pair isolates the noise and nothing else.
  _vx2-above        the §5 field seen from ABOVE, and the arm that exists because of a
                    measurement rather than a preference. The deliverable's hero camera stands
                    **214.6 m** from the vortex axis against a **200 m** mouth -- 14.6 m outside
                    its own lip -- pitched 8.8 degrees down with a 36 degree field, so the frame
                    spans depressions of +9.2 to -26.8 while the mouth's CENTRE is at -28.3 and
                    its near rim at -82.8. The cyclone's centre is below the bottom of the frame
                    and only the far lip is in it, edge on. An eye, an eye wall and spiral bands
                    are features of the horizontal plane, and no amount of building them makes
                    them visible from a camera that is level with that plane. This arm looks down
                    into the funnel so the field itself can be judged; the hero arms above say
                    what the shipped shot does with it. They are two different questions and
                    answering them with one image is what has gone wrong here before.
  _vx2-before       the deliverable's own vortex, which is the "before" the two above are judged
                    against, with the same bloom-off and particles-off treatment so the three
                    differ in the field and in nothing else. Without it the comparison is against
                    a frame that also has bloom and motes in it, and every difference is
                    attributable to three things at once.

The macro values are here rather than in the scene because they are the thing under test; when one
of them ships it moves into the deliverable and this script stops setting it.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
EX = HERE / "examples" / "treeisland"
PROJECT = EX / "tree-of-life-floating-island.json"

# §7-§11. The mouth radius is deliberately NOT among them: ADR-374 measured that the hero camera
# stands 232 m from the axis, so a mouth wider than that puts the camera INSIDE the funnel and
# every "make it bigger" change has made the picture worse. That is geometry, not taste.
MACRO = {
    "innerVoid": 0.20,        # §8, the eye: 20% of the mouth, so 40 m of clear air
    "eyeWallWidth": 0.10,     # §9, a tight wall -- a violent storm rather than a slow one
    "eyeWallGain": 2.0,       # §9, the crest three times the body's density
    "bandArms": 3.0,          # §10
    "bandPitchDegrees": 15.0, # §10, inside the 10-25 real rainbands run at
    "bandDepth": 0.75,        # §10
    "bandHarmonic": 0.5,      # §11, the two finer nested scales
}


def set_vortex(text: str, key: str, value: float) -> str:
    """Set one key inside the `vortex` object of the atmospheric effect.

    Anchored on the vortex's own `"radius"` line so it cannot wander into the comet's radius or
    into a light's -- there are three other `"radius"` keys in this file and the naive pattern
    found the wrong one on the first attempt.
    """
    # `[^{}]*?` and not `(?:[^{}]|\n)*?`: the alternation makes this backtrack exponentially and
    # the script hangs on a 12 kB file. `[^{}]` already matches a newline.
    pattern = rf'("vortex":\s*\{{[^{{}}]*?"{re.escape(key)}":\s*)-?[0-9.eE+]+'
    out, n = re.subn(pattern, lambda m: f"{m.group(1)}{value}", text, count=1)
    if n == 0:
        # The key is not in the file yet, so add it after the vortex's `radius`, which every
        # authored vortex has.
        anchor = re.search(r'("vortex":\s*\{\s*\n\s*)', text)
        if anchor is None:
            raise SystemExit(f"{PROJECT}: no vortex object to set '{key}' in")
        insert = anchor.end()
        indent = re.match(r'\s*', text[insert:]).group(0)
        out = text[:insert] + f'"{key}": {value},\n{indent}' + text[insert:]
    return out


# The looking-down-into-it camera. Placed from the geometry rather than by eye, against ADR-374's
# standing constraint that the camera must be OUTSIDE the mouth (a mouth wider than the camera's
# distance to the axis puts it inside, and every "make it bigger" change has made the picture worse
# for that reason). 800 m out against a 200 m mouth, 770 m above the mouth plane: the mouth's centre
# sits at 43.9 degrees of depression, its near rim at 52.1 and its far rim at 37.6, so the whole
# 14.5 degree span is inside a 45 degree field with room around it.
ABOVE_CAMERA = {
    "camera/position": [0.0, 700.0, 800.0],
    "camera/target": [0.0, -300.0, 0.0],
    "camera/fov": 45.0,
}


def set_parameter(text: str, key: str, value) -> str:
    """Set one `parameters` entry, textually. Raises if the key is not there: a silently-added
    parameter would sit in the file doing nothing, which is the failure this whole script's
    ADR-182 check at the end exists to catch."""
    literal = ("[" + ", ".join(f"{v}" for v in value) + "]") if isinstance(value, list) else f"{value}"
    pattern = rf'("{re.escape(key)}":\s*)(?:\[[^\]]*\]|-?[0-9.eE+]+)'
    out, n = re.subn(pattern, lambda m: f"{m.group(1)}{literal}", text, count=1)
    if n != 1:
        raise SystemExit(f"{PROJECT}: expected one '{key}' parameter to set, found {n}")
    return out


def set_centre(text: str, centre) -> str:
    """Move the vortex's own `center`. Anchored inside the `vortex` object, which is what keeps it
    off the comet's anchor position and the three light positions in the same file."""
    x, y, z = centre
    # `[^{}]*?` between the brace and the key, not `\s*`: `set_vortex` inserts new keys directly
    # after `"vortex": {`, so by the time this runs `center` is no longer the first member. An
    # anchored-on-the-brace pattern matched the deliverable and nothing derived from it.
    pattern = (r'("vortex":\s*\{[^{}]*?"center":\s*\[)'
               r'\s*-?[0-9.eE+]+,\s*-?[0-9.eE+]+,\s*-?[0-9.eE+]+\s*(\])')
    out, n = re.subn(pattern, lambda m: f"{m.group(1)}{x}, {y}, {z}{m.group(2)}", text,
                     count=1, flags=re.S)
    if n != 1:
        raise SystemExit(f"{PROJECT}: expected one vortex centre to move, found {n}")
    return out


def add_parameter(text: str, key: str, value: float) -> str:
    """Add a `parameters` entry that the deliverable does not have, beside one that it does."""
    anchor = '"scene/volumeSteps":'
    i = text.index(anchor)
    line_start = text.rindex("\n", 0, i) + 1
    indent = text[line_start:i]
    return text[:line_start] + f'{indent}"{key}": {value},\n' + text[line_start:]


def no_bloom(text: str) -> str:
    out, n = re.subn(r'("post/bloom/enabled":\s*)true', r'\1false', text, count=1)
    if n != 1:
        raise SystemExit(f"{PROJECT}: expected one enabled bloom to switch off, found {n}")
    return out


def main() -> int:
    src = PROJECT.read_text()
    if '"post/bloom/enabled": true' not in src:
        raise SystemExit(f"{PROJECT}: bloom is not on, so the arms would not be isolating it")

    # ADR-461: the march's start jitter off, on every arm. At the shipped 4000 m over 32 steps a
    # step is 125 metres and the funnel changes completely across one, so a full-step offset between
    # neighbouring pixels is 125 metres of uncorrelated displacement. Measured on this exact frame:
    # grain 1.457 at the default 1.0, 1.066 at 0.25, **0.436 at 0**. The default stays at 1.0 engine
    # wide so no other scene moves; it is authored here because here is where it was measured.
    src = add_parameter(src, "scene/volumeJitter", 0.0)

    before = no_bloom(src)
    (EX / "_vx2-before.json").write_text(before)

    macro = before
    for key, value in MACRO.items():
        macro = set_vortex(macro, key, value)
    noisy = macro
    macro = set_vortex(macro, "cloudNoise", 0.0)
    (EX / "_vx2-macro.json").write_text(macro)
    (EX / "_vx2-macro-noise.json").write_text(set_vortex(noisy, "cloudNoise", 1.0))

    # The placement arms. The coordinator's decision is "move the vortex, not the camera": put it
    # beyond the tree along the camera's own view direction and lower, so the tree is SILHOUETTED
    # against the cyclone instead of floating above an invisible one, radius unchanged.
    #
    # Corrected for ADR-264 before being used. The coordinator worked the placement from the SCENE
    # file's camera at (158, 6, 152) -- but the project overrides the camera to (197.7, 45.3, 83.5)
    # looking at (-16.9, 9.4, -6.0), and a project's parameters are applied over its scene. Against
    # the camera that actually renders, `(-380, -150, -365)` is **15.2 degrees off the view axis**,
    # and the mouth is only +/-15.3 wide in a +/-30 frame, so it would have sat jammed against one
    # edge. The same placement worked from the real camera is `(-491, -111, -204)`.
    #
    # And the distance is corrected too, from a measurement rather than a preference: the island and
    # tree occupy **+/-15.0 degrees** of this frame, measured off the shipped render, and the
    # coordinator's 746 m puts the mouth at 30.0 degrees -- exactly the island's own width, so the
    # island would cover the eye again, which is the whole thing the move was for. Nearer is what
    # makes the cyclone a backdrop rather than a disc behind a rock.
    #
    #   D      centre                 subtend   mouth against the frame's -18..+18
    #   746    (-491, -111, -204)      30.0     -12.0 .. +18.0   the island's own width
    #   600    (-356, -104, -147)      36.9     -13.2 .. +23.6
    #   500    (-264,  -98, -109)      43.6     -14.6 .. +29.0
    for name, centre in (("far", (-491.0, -111.0, -204.0)),
                         ("mid", (-356.0, -104.0, -147.0)),
                         ("near", (-264.0, -98.0, -109.0))):
        # `cloudNoise` written explicitly rather than left to its 1.0 default: these arms are
        # about the placement, and a reader must not have to know a default to know that the
        # noise is on in them.
        (EX / f"_vx2-place-{name}.json").write_text(
            set_centre(set_vortex(noisy, "cloudNoise", 1.0), centre))

    above = macro
    for key, value in ABOVE_CAMERA.items():
        above = set_parameter(above, key, value)
    (EX / "_vx2-above.json").write_text(above)

    # ADR-182: the arms have to be shown to differ from the thing they are derived from, or a
    # regex that matched nothing produces three identical files and a checkpoint that passes.
    for name in ("_vx2-before", "_vx2-macro", "_vx2-macro-noise", "_vx2-above",
                 "_vx2-place-far", "_vx2-place-mid", "_vx2-place-near"):
        text = (EX / f"{name}.json").read_text()
        if text == src:
            raise SystemExit(f"{name}.json is identical to the deliverable: nothing was changed")
    if (EX / "_vx2-macro.json").read_text() == (EX / "_vx2-before.json").read_text():
        raise SystemExit("_vx2-macro.json is identical to _vx2-before.json: the macro edits missed")
    print("wrote _vx2-before.json, _vx2-macro.json, _vx2-macro-noise.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
