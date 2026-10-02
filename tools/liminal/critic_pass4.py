#!/usr/bin/env python3
"""Creative Critic inputs for All You Got, judged by the art pass 4 brief (04-art-pass-4.md, PARTS 13-16).
Pass 3's wrapper (critic_pass3.py, copied) with pass 4's questions, intent, camera language and avoid list.

It runs the engineer's `tools/liminal_critic.py inputs` and then rewrites what that writes:

  * the intent: pass 2's mood, visual language, camera language and avoid list, the owner's section-12
    questions and section-19 phase-2 asks, and the sections in the OWNER's bar numbering
    (tools/liminal/pass2_grid.py) with each section's camera language;
  * the scene's `modulation` as the LIST the Critic's schema wants. `liminal_critic.py` wrote a dict
    ({"routes": [...]}), which is why the reactivity analyzer failed in pass 1 with "'str' object has no
    attribute 'get'": it iterated the dict's keys;
  * three timeline "routes" carrying the owner's musical EVENTS (BIG CLAPs, quarter notes where the owner asks
    for the quarter pulse, eighth notes where he asks for the eighth subdivision), so the reactivity analyzer
    measures the pixels' response locked to them;
  * the BIG CLAPs as scene `events`, so their variety (time, place, framing, scale) is measured.

    python3 tools/liminal/critic_pass2.py --project examples/liminal/all-you-got.json \
        --video ~/Desktop/av-gen-review/24-liminal-space/all-you-got-final.mp4 --out <dir> [--video-start s] \
        [--submit --label pass1-final]

With --submit it sends the job (preview mode) to the local Critic (../creative-critic) and waits.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import pass2_grid as G  # noqa: E402

CRITIC = os.path.join(os.path.dirname(ROOT), "creative-critic")
SECTIONS = os.path.join(HERE, "all-you-got.sections.json")

QUESTIONS = [
    # PART 13: the arc
    "Does the film read as a progression: construction -> house and routine -> the city -> alone in the populated city -> the city alive -> the climb -> reaching the top -> dawn?",
    # PART 3 / 15: build -> lock
    "Intro (0:15-0:22): does the world visibly keep assembling itself, rhythmically, and does each piece STAY where it lands (build, lock), with nothing wobbling or twitching afterwards?",
    "Anywhere in the film: does any wall, roof, building or piece of furniture move, wobble or twitch after it has been placed (outside a deliberate one-off BIG CLAP corruption)?",
    # PART 4
    "Kitchen (~1:17): does the clap land on a quiet, mundane image of the mannequin at the stove over a steaming pot, as a satisfying camera beat, without comedy or theatre?",
    # PART 5
    "Stairs (~1:25-1:32): does the hall arrive later than the verse's line, with room for the phrase, and does the climb gather momentum to the top and the fall off the edge?",
    # PART 6
    "Basement (1:32-1:50): does it feel like a lived-in floor -- a laundry, a home gym, a lounge with a colourful (not nightclub) bar -- with the mannequin present but emotionally inactive in each?",
    # PART 7
    "Around 2:15: is any geometry distorted, clipped or cut open by the camera?",
    # PARTS 8-10
    "Let it grow (2:25-2:45): does the camera leave the house and does the world expand house -> neighbourhood -> city, growing outward (roads, lights, traffic, blocks, skyline) with the music?",
    "Is that all you (2:45-3:02): in each shot (a red light, a bar, a park bench, a crossing) is the city moving and populated while he is completely still and alone? Is the contrast clear and restrained?",
    "Bridge 3 (3:02-3:20): does the camera soar through a living city (rising, sweeping, diving, along a street, pulling up) that breathes with the music, without chaos?",
    # PART 11
    "Final chorus: when we reach the top of the stairs, is he there, upright and triumphant (not cartoonish), with a strong silhouette, elevated above everything before?",
    # PART 12
    "4:08: does the digital transition lead directly into the dawn, with no unrelated material in between?",
    # PART 16
    "Are there any visible clipping, intersections, floating objects, impossible poses, text off its wall or overlapping, premature transitions, awkward camera acceleration or excessive wobble?",
    "Does the world still breathe on the quarter note through light, colour, signage and city activity (never the camera)? Are the BIG CLAPs obvious?",
    "Do the lyrics sit on surfaces as artifacts embedded in the world, readable and not overlapping?",
]

CAMERA_LANGUAGE = {
    "intro-a": "a low road-level view rising over a street as it builds; no breathing",
    "intro-b": "gliding along the street as it fills, the city rising behind",
    "intro-c": "turning on the house and accelerating into its lit window; the cut inside lands on the downbeat",
    "release": "inside: a slow, deliberate sweep around the living room; the furniture lands around the still figure",
    "verse1-a": "the same room, swept; each return finds the mannequin in another pose",
    "verse1-b": "through the back door to the kitchen, a pan that lands on him at the stove on the clap; then the hall and up the stair",
    "pause": "over the top step, falling into the black",
    "letgo": "landing in the laundry, walking on through the gym to the lounge and its bar",
    "verse2": "upstairs, room by room; each BIG CLAP corrupts the world its own way",
    "bridge-t": "tilting up as the ceiling and the roof fly off",
    "bridge1-a": "rising out of the roofless house over the street, the neighbourhood, the city growing outward",
    "bridge1-b": "high over the complete city, then a controlled descent towards downtown",
    "bridge2": "near-still, observational shots at street level; he is still, the city moves",
    "bridge3": "the camera soars: down an avenue, round a tower, a dive, low along a street, pulled up high",
    "chorus-a": "free, sweeping over the open landscape; the city far away",
    "chorus-b": "climbing the stair into the sky towards him at the top",
    "chorus-c": "on the summit, round him: a hero's low angle",
    "crash": "impact, then dawn at once",
    "ending": "slowing to still; back on the road where it began",
}


def quarter_events() -> list[float]:
    out = []
    for sid in ("intro-a", "intro-b", "intro-c", "release", "verse1-a", "verse1-b", "letgo", "verse2", "bridge2",
                "bridge3", "chorus-a", "chorus-b", "chorus-c"):
        s = next(x for x in G.build()["sections"] if x["id"] == sid)
        bar = s["bars"][0]
        while bar <= s["bars"][1]:
            out += [round(G.t(bar, b), 4) for b in (1.0, 2.0, 3.0, 4.0)]
            bar += 1
    return sorted(set(out))


def eighth_events() -> list[float]:
    out = []
    for b0, b1 in ((5, 16), (42, 49), (75, 82), (83, 90)):
        for bar in range(b0, b1 + 1):
            out += [round(G.t(bar, b + 0.5), 4) for b in (1.0, 2.0, 3.0, 4.0)]
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--project", required=True)
    ap.add_argument("--video", required=True)
    ap.add_argument("--video-start", type=float, default=0.0)
    ap.add_argument("--out", required=True)
    ap.add_argument("--submit", action="store_true")
    ap.add_argument("--label", default="")
    ap.add_argument("--session", default="liminal-pass4")
    ap.add_argument("--track", default="")
    a = ap.parse_args()
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "liminal_critic.py"), "inputs", "--project", a.project,
                    "--sections", SECTIONS, "--video", a.video, "--video-start", str(a.video_start), "--out", a.out],
                   check=True)
    grid = G.build()
    dur = float(subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0",
                                os.path.expanduser(a.video)], check=True, capture_output=True, text=True).stdout.strip())
    lo, hi = a.video_start, a.video_start + dur

    def inside(x0, x1):
        return x1 > lo and x0 < hi

    segments = []
    for s in grid["sections"]:
        if s["id"] == "countin" or not inside(s["t0"], s["t1"]):
            continue
        segments.append({"id": s["id"], "name": f"{s['name']} (owner bars {s['bars'][0]}-{s['bars'][1]})",
                         "start": round(max(s["t0"], lo), 3), "end": round(min(s["t1"], hi), 3),
                         "intent": s["ask"], "music": s["music"], "camera_language": CAMERA_LANGUAGE.get(s["id"], "")})
    intent_path = os.path.join(a.out, "intent.json")
    intent = json.load(open(intent_path))
    intent.update({
        "project": "All You Got -- art pass 4 (a digital neighbourhood, a house and a city of luminous lines; a still mannequin; a broken simulation)",
        "mood": ["construction", "routine", "loneliness", "confusion", "wandering", "isolation", "expansion", "increasingly alive", "release"],
        "visual_language": ["luminous line-drawn geometry on dark", "early primitive 3D / simulated space",
                            "a neighbourhood constructing itself rhythmically: build, lock", "one coherent house: floors, a stair, a basement",
                            "a believable night city: roads, lamps, traffic, blocks, a skyline", "the city's people moving while he is still",
                            "a still mannequin as a recurring tableau, never animated", "lyrics as artifacts on clear wall",
                            "digital corruption at transitions, claps, lyrics and peaks", "screens showing static",
                            "BIG CLAP punctuation, each different"],
        "camera_language": ["authored", "cinematic", "the camera observes, it never breathes or pulses", "sweeps that reveal tableaux"],
        "avoid": ["camera breathing or bobbing", "objects jumping or bouncing like a malfunction", "an animated lead mannequin",
                  "structures wobbling or twitching after they are built", "generic cyberpunk spectacle", "a nightclub bar",
                  "abstract procedural intro geometry", "a literal rainbow wipe across the screen", "doubling back A-B-A between rooms",
                  "text over windows, doors or furniture", "the whole video as one glitch effect", "barren rooms",
                  "Glowmere", "generic 3D", "flat screen-space subtitles"],
        "segments": segments,
        "questions": QUESTIONS,
    })
    json.dump(intent, open(intent_path, "w"), indent=1)
    scene_path = os.path.join(a.out, "scene.json")
    scene = json.load(open(scene_path))
    routes = scene.get("modulation", {})
    routes = routes.get("routes", []) if isinstance(routes, dict) else list(routes)
    claps = [c["t"] for c in grid["bigClaps"] if lo <= c["t"] < hi]
    routes += [
        {"source": "timeline.bigclap", "target": "bigclap/exposure-emission-colour", "events": claps, "amount": 1.0,
         "enabled": True},
        {"source": "timeline.quarter", "target": "quarter/pulse-emission-intensity",
         "events": [x for x in quarter_events() if lo <= x < hi], "amount": 1.0, "enabled": True},
        {"source": "timeline.eighth", "target": "eighth/pulse-emission-intensity",
         "events": [x for x in eighth_events() if lo <= x < hi], "amount": 1.0, "enabled": True},
    ]
    scene["modulation"] = routes
    scene["events"] = [{"type": "bigclap", "t": c["t"], "entity": c["id"]} for c in grid["bigClaps"] if lo <= c["t"] < hi]
    # pass 2's own shots, when the generator wrote them beside the project (pass 1 used the timing sheet)
    sidecar = os.path.splitext(os.path.abspath(a.project))[0] + ".shots.json"
    if os.path.exists(sidecar):
        own = []
        for sh in json.load(open(sidecar)):
            s0, s1 = max(sh["start"], lo), min(sh["end"], hi)
            if s1 - s0 > 0.05:
                sec = next((x["id"] for x in grid["sections"] if x["t0"] - 1e-6 <= s0 < x["t1"]), "")
                own.append({"id": sh["id"], "start": round(s0, 3), "end": round(s1, 3), "segment": sec, "label": sh["name"]})
        json.dump(own, open(os.path.join(a.out, "shots.json"), "w"), indent=1)
        scene["shots"] = own
    scene.setdefault("timeline", {})["sections"] = [{"id": s["id"], "name": s["name"], "start": s["start"], "end": s["end"]}
                                                  for s in segments]
    json.dump(scene, open(scene_path, "w"), indent=1)
    print(f"pass 4 intent: {len(segments)} owner sections, {len(claps)} BIG CLAPs, {len(QUESTIONS)} questions")
    if a.submit:
        cmd = [os.path.join(CRITIC, ".venv", "bin", "critic"), "submit", "--inputs", os.path.join(os.path.abspath(a.out), "inputs.json"),
               "--mode", "preview", "--session", a.session, "--wait", "--json"]
        if a.track:
            cmd += ["--track", a.track]
        if a.label:
            cmd += ["--label", a.label]
        r = subprocess.run(cmd, capture_output=True, text=True, cwd=CRITIC)
        print(r.stdout[-4000:])
        print(r.stderr[-2000:])


if __name__ == "__main__":
    main()
