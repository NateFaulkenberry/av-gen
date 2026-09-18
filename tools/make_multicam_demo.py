#!/usr/bin/env python3
"""Glowmere Valley 2, directed by three cameras (ADR-245).

The multi-camera demonstration. It takes the Glowmere Valley 2 scene and project exactly as they
are -- the same world, the same heroes, the same abduction scenario, the same Auto-director cut --
and adds the one thing that did not exist before: a camera collection, a shot track, and the two
cameras that are not the Auto-director's.

    CAMERA DIRECTOR
      |-- Hero Free Roam  (the main camera; the Auto-director's cut, unchanged)
      |-- UFO Watch       (event driven: claims the frame while the abduction scenario is running)
      +-- Valley Wide     (authored: keyframed on the ordinary timeline, placed by authored shots)

Nothing here is Glowmere-specific in the engine. `UFO Watch` names a staging scenario by string and
rides a composition node by name; `Valley Wide` is keyframes on `cameras/valleywide/*`, which are
ordinary parameters. Point the same three fields at a different world and the same demo works.

    python3 tools/make_multicam_demo.py

writes `examples/world/glowmere-valley-2-multicam.scene.json` and `.json` beside the originals and
leaves the originals untouched.

**This generator no longer reproduces the checked-in demo.** Two things it copies have since been
changed in the files themselves and cannot be expressed here:

  * the render range and output path, which were re-saved from the app;
  * the cut itself -- `glowmere-valley-2.json` now carries a 39-shot bake where the demo carries
    41, so copying its `camera/*` tracks today would replace the film, not just re-place it; and
  * the Hero Free Roam camera's 20% dolly-in -- 328 baked keys on `camera/position`,
    `camera/target` and `camera/lens/focusDistance`, each scaled about the subject of the shot it
    belongs to. Those keys are copied wholesale from `glowmere-valley-2.json`, which was *not*
    given the same treatment, so a regeneration would put the hero camera back where it was.

What this file is still good for is the three cameras and the shot track. Re-run it only if you
mean to rebuild those, and expect to redo the dolly afterwards.
"""

from __future__ import annotations

import hashlib
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
WORLD = ROOT / "examples" / "world"
SRC_SCENE = WORLD / "glowmere-valley-2.scene.json"
SRC_PROJECT = WORLD / "glowmere-valley-2.json"
OUT_SCENE = WORLD / "glowmere-valley-2-multicam.scene.json"
OUT_PROJECT = WORLD / "glowmere-valley-2-multicam.json"

# Camera ids. 1 is always the main camera (the legacy `camera/*` block), which is why the
# Auto-director's existing cut needs no change at all: it already drives camera 1.
MAIN, VALLEY, UFO = 1, 2, 3

# ---- the shot track ------------------------------------------------------------------------------
#
# Thirty seconds, which is the length of the review render. Two of the shots are `locked`: Glowmere's
# saucer abducts something roughly every nineteen seconds, so without the author's veto the event
# camera would own the whole piece and there would be no establishing shot to see.
SHOTS = [
    # The world first. A cut in from black, held against the event.
    {"camera": VALLEY, "start": 0.0, "end": 7.0, "transition": "cut", "locked": True,
     "label": "establish"},
    # Then the Auto-director's camera, doing exactly what it did before this existed. Locked for the
    # same reason: the point of this stretch is that the old behaviour survived.
    {"camera": MAIN, "start": 7.0, "end": 13.5, "transition": "cut", "locked": True,
     "label": "hero"},
    # 13.5 s -> 26 s: nothing authored. The default camera has the frame, which is Hero Free Roam,
    # *until* the abduction claims it -- which is the whole demonstration. No shot says so; the
    # event does.
    # And back out to the world, this time as a dissolve: the return is a settling, not a cut.
    {"camera": VALLEY, "start": 26.0, "end": 31.0, "transition": "blend", "blend": 1.6,
     "locked": True, "label": "return"},
]

CAMERAS = [
    {
        "id": MAIN,
        # Renamed, not rebuilt. This is the camera AV Gen has always had; the Auto-director's baked
        # tracks still drive it and its shot spans and aim-follow table are untouched.
        "name": "Hero Free Roam",
        "autoDirector": True,
    },
    {
        "id": VALLEY,
        "name": "Valley Wide",
        "slug": "valleywide",
        "placement": "free",
        # The authored base. The timeline keys below move it; these are what it is without them,
        # which is also what a scrub before the first key gives.
        #
        # Halfway in along its own look-at axis, from the 268 m it was first placed at. The owner
        # asked for the wide shots 50% closer and this is the film's wide shot: a 24 mm view of the
        # whole valley, where every creature in it read as a speck. The *aim* does not move -- a
        # dolly is a change of distance, not of subject.
        "position": [-88.0, 51.0, -52.0],
        "target": [-8.0, 6.0, 46.0],
        "fov": 52.0,
        # A wide lens, because this camera *is* the wide lens. That used to be an Auto-director
        # setting; a camera knowing what kind of camera it is puts it where it belongs.
        "focalLength": 24.0,
        # Deliberately not available to the Auto-director: this is a composition somebody made for a
        # moment, not a viewpoint an automatic cut should wander into.
        "autoDirector": False,
    },
    {
        "id": UFO,
        "name": "UFO Watch",
        "slug": "ufowatch",
        "placement": "free",
        "fov": 40.0,
        "focalLength": 35.0,
        # It rides the saucer and looks *below* it. Aiming at the saucer's centre gives a shot of a
        # saucer; aiming seven metres under it gives a shot of an abduction -- the beam, the ground
        # and whatever is being lifted are what make the event readable. Forty-four metres out, which
        # was chosen by rendering it: at sixty-six the thing in the beam was three pixels tall --
        # and now 36 m, a fifth closer again, on the same instruction as everything else here.
        #
        # *Both* offsets are scaled, by the same 0.8, about the saucer they are offsets from. That
        # is what makes it a dolly and not a re-composition: the angle the shot looks down at the
        # beam from is unchanged, the saucer and the ground sit exactly where they sat in frame, and
        # the only thing that differs is that everything is 1.25x bigger. Scaling the camera alone
        # was tried first and rendered: it lifts the saucer 1.25x further up the frame and clips the
        # top of the dome, which is the whole argument for scaling the aim with it.
        "followNode": "visitor",
        "followOffset": [24.8, 4.0, 24.8],
        "aimNode": "visitor",
        "aimOffset": [0.0, -5.6, 0.0],
        # The event it answers, by name. The engine does not know what an abduction is.
        "eventScenario": "abduction",
        "eventLead": 0.3,
        "eventTail": 1.4,
        "eventBlend": 0.0,  # a hard cut: a dissolve onto an event that has started reads as a fault
        # The phases worth cutting to. The abduction spends seven seconds flying to its subject
        # before there is anything to see; these four are the part an audience needs to be shown.
        "eventBeats": ["aim", "beam", "abduct", "depart"],
        "priority": 10,
        "autoDirector": True,
    },
]

# ---- the establishing move ------------------------------------------------------------------------
#
# Ordinary timeline tracks on ordinary parameters. There is no camera animation system: `Valley Wide`
# is keyframed exactly the way `orb/scale` is, which is the point.
#
# A slow lateral drift with a small rise, over the seven seconds it is on screen, ending on more of
# the river and more sky. Subtle on purpose (multicam-demo section 9). Each key is the one it was
# authored with, dollied halfway in along the aim of the same instant -- so the move is the same
# move, half the stand-off.
VALLEY_TRACKS = [
    {
        "target": "cameras/valleywide/position",
        "component": -1,
        "timeBase": "seconds",
        "mode": "replace",
        "loopLength": 0.0,
        "enabled": True,
        "keys": [
            {"time": 0.0, "value": [-88.0, 51.0, -52.0], "interp": "easeInOut"},
            {"time": 7.0, "value": [-74.0, 55.0, -53.5], "interp": "easeInOut"},
            # The return at 26 s picks the move up again from where it left off rather than
            # snapping back, so the second appearance reads as the same camera.
            {"time": 26.0, "value": [-74.0, 55.0, -53.5], "interp": "easeInOut"},
            {"time": 31.0, "value": [-63.5, 58.0, -54.0], "interp": "easeInOut"},
        ],
    },
    {
        "target": "cameras/valleywide/target",
        "component": -1,
        "timeBase": "seconds",
        "mode": "replace",
        "loopLength": 0.0,
        "enabled": True,
        "keys": [
            {"time": 0.0, "value": [-8.0, 6.0, 46.0], "interp": "easeInOut"},
            {"time": 7.0, "value": [2.0, 9.0, 56.0], "interp": "easeInOut"},
            {"time": 26.0, "value": [2.0, 9.0, 56.0], "interp": "easeInOut"},
            {"time": 31.0, "value": [10.0, 12.0, 64.0], "interp": "easeInOut"},
        ],
    },
]


def digest(path: pathlib.Path) -> dict:
    data = path.read_bytes()
    return {"path": path.name, "sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}


def main() -> None:
    scene = json.loads(SRC_SCENE.read_text())
    scene["name"] = "glowmere-valley-2-multicam"
    scene["cameraDirection"] = {
        "cameras": CAMERAS,
        "shots": SHOTS,
        # Hero Free Roam when nobody claims the frame, which is what makes the stretch between the
        # authored shots behave exactly as the piece did before there were three cameras.
        "default": MAIN,
        "nextId": UFO + 1,
    }
    OUT_SCENE.write_text(json.dumps(scene, indent=1) + "\n")

    project = json.loads(SRC_PROJECT.read_text())
    project["assets"]["scene"]["path"] = digest(OUT_SCENE)
    tracks = project.setdefault("timeline", {}).setdefault("tracks", [])
    # The Auto-director's own tracks (camera/position, camera/target, camera/mode, camera/lens/*) are
    # left exactly as they are: they drive camera 1, which is still Hero Free Roam.
    tracks.extend(VALLEY_TRACKS)
    render = project.setdefault("render", {})
    render["start"] = 0.0
    render["end"] = 30.0
    render["path"] = "glowmere-valley-2-multicam.mov"
    OUT_PROJECT.write_text(json.dumps(project, indent=1, sort_keys=True) + "\n")

    print(f"wrote {OUT_SCENE.relative_to(ROOT)}")
    print(f"wrote {OUT_PROJECT.relative_to(ROOT)}")
    print(f"  {len(CAMERAS)} cameras, {len(SHOTS)} authored shots, {len(VALLEY_TRACKS)} camera tracks")


if __name__ == "__main__":
    main()
