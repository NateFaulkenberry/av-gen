#!/usr/bin/env python3
"""Glowmere Valley 2, directed by a song (ADR-249).

The Song Mode demonstration. It takes the three-camera demo exactly as it is -- the same world, the
same heroes, the same abduction scenario, the same three cameras -- and changes two things:

  1. **Every camera is made available to the Auto-director**, and the authored shot track is
     removed. Nobody says which camera is on screen when; the director decides.
  2. **The project gains a song plan**: eight sections, each carrying a shot *intent* -- six numbers
     and a name -- and nothing else. No camera ids, no positions, no lens, no durations.

    SONG PLAN                         AUTO-DIRECTOR (Song)
      Intro        Atmospheric Establishing   -> chooses a camera, a subject, a framing, a move,
      Verse        Hero Performance              and how many cuts, for every section
      Pre-Chorus   Building Tension
      Chorus       Dynamic Hero Coverage
      Ocean Ambience  Slow Environmental Exploration   <- a section type no analyzer produces
      Verse        Hero Performance                    <- the same intent, a second time round
      Final Chorus Peak Multi-Camera Hero
      Outro        Slow Pullback

**Nothing in the engine has heard of any of those names.** They are display strings on the plan; the
director reads the six numbers beside them. `tests/unit/test_song_director.cpp` proves it by
scrambling every one of them and checking the film does not move.

`Ocean Ambience` is in the list for the reason the brief asks: it is a section type nobody could
have hard-coded, and it is directed by exactly the same code path as `Chorus`.

The two `Verse` sections carry a **byte-identical intent** and differ only in `occurrence`. They come
out as different films of the same music, which is the evidence that Song Mode is not shot playback.

    python3 tools/make_song_demo.py

writes `examples/world/glowmere-valley-2-song.scene.json` and `.json` beside the originals and
leaves them untouched.
"""

from __future__ import annotations

import hashlib
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
WORLD = ROOT / "examples" / "world"
SRC_SCENE = WORLD / "glowmere-valley-2-multicam.scene.json"
SRC_PROJECT = WORLD / "glowmere-valley-2-multicam.json"
OUT_SCENE = WORLD / "glowmere-valley-2-song.scene.json"
OUT_PROJECT = WORLD / "glowmere-valley-2-song.json"


# ---- the song plan ---------------------------------------------------------------------------
#
# Thirty seconds, which is the length of the review render, laid out as a music video's shape rather
# than as a set of equal blocks. Every section is `(label, start, end, intent, energy, density,
# transition, occurrence)`.
#
# The intent is the whole of what the director is told:
#
#   hero       0 the environment  ..  1 the subject
#   distance   0 intimate         ..  1 the widest this world offers
#   movement   0 locked off       ..  1 constantly travelling
#   variation  0 one setup held   ..  1 keep finding new ones
#   cutRate    0 the longest hold ..  1 the shortest
#   cameras    how many viewpoints the section wants used
#
# `energy` and `density` are the section's *measured* loudness and busyness -- in a real project they
# come from the analyzer; here they are authored because this demo has to be reproducible without a
# particular audio file being present. `transition` is how hard the boundary into the section lands,
# which is what decides whether the section opens on a cut or a dissolve.


def intent(name, hero, distance, movement, variation, cut_rate, cameras):
    return {
        "id": name,
        "hero": hero,
        "distance": distance,
        "movement": movement,
        "variation": variation,
        "cutRate": cut_rate,
        "cameras": cameras,
    }


SECTIONS = [
    # The world, before the piece has committed to anything. One camera, held, as wide as it goes.
    ("Intro", 0.0, 4.0, intent("Atmospheric Establishing", 0.12, 0.95, 0.20, 0.15, 0.05, 1),
     0.18, 0.15, 0.0, 0),
    # The subject, seen properly. Two viewpoints, moderate holds. **This intent appears twice in
    # the plan and is byte-identical both times** -- see the second Verse below.
    ("Verse", 4.0, 11.0, intent("Hero Performance", 0.82, 0.38, 0.45, 0.55, 0.50, 2),
     0.45, 0.38, 0.20, 0),
    # The run-up: the camera starts going somewhere.
    ("Pre-Chorus", 11.0, 14.0, intent("Building Tension", 0.60, 0.55, 0.85, 0.55, 0.65, 2),
     0.65, 0.70, 0.16, 0),
    # The payoff. Three cameras, short holds, the hero owning the frame.
    ("Chorus", 14.0, 19.5, intent("Dynamic Hero Coverage", 0.90, 0.58, 0.88, 0.85, 0.85, 3),
     0.95, 0.88, 0.34, 0),
    # The custom section. Nothing in the engine has ever heard of "Ocean Ambience"; it is directed by
    # the same code as everything above it, and only its six numbers differ.
    ("Ocean Ambience", 19.5, 23.0,
     intent("Slow Environmental Exploration", 0.08, 0.88, 0.55, 0.25, 0.10, 2),
     0.22, 0.18, 0.30, 0),
    # The same intent as the first Verse -- the same six numbers, the same name -- one occurrence
    # later. Different cameras, different subjects, different framing. That difference is the whole
    # of the claim that Song Mode is not shot playback, and it costs nothing in determinism: it is a
    # function of `occurrence`, not of a clock or a stream.
    ("Verse", 23.0, 30.0, intent("Hero Performance", 0.82, 0.38, 0.45, 0.55, 0.50, 2),
     0.45, 0.38, 0.22, 1),
    # The last payoff: everything the world has.
    ("Final Chorus", 30.0, 34.0, intent("Peak Multi-Camera Hero", 0.92, 0.60, 0.92, 0.90, 0.90, 3),
     0.99, 0.92, 0.40, 1),
    # ...and out.
    ("Outro", 34.0, 36.0, intent("Slow Pullback", 0.30, 0.96, 0.28, 0.15, 0.05, 1),
     0.14, 0.10, 0.36, 0),
]


def song_plan():
    sections = []
    for label, start, end, shot_intent, energy, density, transition, occurrence in SECTIONS:
        sections.append({
            "start": start,
            "end": end,
            "label": label,
            "intent": shot_intent,
            "energy": energy,
            "density": density,
            "transition": transition,
            # Guided by default, which is what a first-pass music video wants: the intent is
            # respected and the execution is the director's. The film-wide ceiling in
            # `autoDirector.autonomy` is what lets a person turn the whole thing down to Locked.
            "autonomy": "guided",
            "occurrence": occurrence,
        })
    return {"name": "glowmere-song", "sections": sections}


def main() -> None:
    scene = json.loads(SRC_SCENE.read_text())
    project = json.loads(SRC_PROJECT.read_text())

    # ---- the cameras ---------------------------------------------------------------------------
    direction = scene["cameraDirection"]
    for camera in direction["cameras"]:
        # Every camera is the director's to use. In the multicam demo `Valley Wide` was deliberately
        # withheld -- it was a composition somebody made for one moment. Here the point is the
        # opposite: the director is being handed a camera library and asked to shoot a song with it.
        camera["autoDirector"] = True
    # The authored shot track goes. Whatever appears on screen in this render, the Auto-director
    # decided -- which is the only way the demonstration means anything.
    direction["shots"] = []

    # `UFO Watch` keeps its event scenario, so the world's own events can still take the frame from
    # the director's cut (ADR-245's rule 1). A Song Mode shot is locked -- an author's veto against
    # an event -- only when its section is Locked, and none of these are: an abduction happening
    # during a Chorus is the world, and the director's cut should give way to it.
    scene["name"] = "glowmere-valley-2-song"

    OUT_SCENE.write_text(json.dumps(scene, indent=1) + "\n")

    # ---- the project ---------------------------------------------------------------------------
    project["assets"]["scene"]["path"] = {
        "path": OUT_SCENE.name,
        "sha256": hashlib.sha256(OUT_SCENE.read_bytes()).hexdigest(),
        "size": OUT_SCENE.stat().st_size,
    }
    project["songPlan"] = song_plan()
    project["autoDirector"] = {
        "mode": "song",
        # The most freedom any section gets. Every section here asks for `guided`, so this is the
        # control a person would reach for to make the film more faithful, not less.
        "autonomy": "expressive",
        "minShot": 1.6,
        "minBuildShot": 1.0,
        "maxShot": 5.0,
        "maxSpeed": 8.0,
        "maxSwing": 24.0,
        "dwell": 1,
        "seed": 1,
    }
    # The Auto-director writes its own camera tracks and its own shot track, so the multicam demo's
    # baked camera automation goes with the authored shots. What stays is everything that is not the
    # camera, which is the rule `installSequence` follows anyway.
    project["timeline"]["tracks"] = [
        track for track in project["timeline"]["tracks"]
        if not track.get("target", "").startswith("camera")
    ]
    project.pop("cameraAimFollow", None)
    project.pop("cameraShotSpans", None)
    project["render"]["path"] = "glowmere-valley-2-song.mov"
    project["render"]["end"] = 36.0

    OUT_PROJECT.write_text(json.dumps(project, indent=1) + "\n")

    print(f"wrote {OUT_SCENE.relative_to(ROOT)}")
    print(f"wrote {OUT_PROJECT.relative_to(ROOT)}")
    print()
    print("direct it, persisting the camera track into the scene:")
    print("  ./build/release/src/avgen --headless --frames 1 \\")
    print(f"      --project {OUT_PROJECT.relative_to(ROOT)} --direct \\")
    print(f"      --save-project {OUT_PROJECT.relative_to(ROOT)} \\")
    print(f"      --save-scene {OUT_SCENE.relative_to(ROOT)}")
    print()
    print("then render it, through the GPU lock (ADR-170):")
    print("  tools/gpu-lock.sh ./build/release/src/avgen --headless --render \\")
    print(f"      --project {OUT_PROJECT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
