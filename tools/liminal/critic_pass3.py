#!/usr/bin/env python3
"""Creative Critic inputs for All You Got, judged by the art pass 3 addendum (03-art-pass-3-addendum.md, section 34).
Pass 2's wrapper (critic_pass2.py) with pass 3's questions, intent, camera language and avoid list.

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
    # rooms (section 34)
    "Do the rooms feel inhabited? Are the furniture relationships believable? Is there enough detail? Does the geometry feel intentional?",
    # character
    "Does the character feel lonely? Are his poses visually different from one return to the next?",
    "Does the camera movement reveal each tableau effectively? Does the mannequin read as a recurring visual motif?",
    "Does the mannequin ever move while seen (it must not)? Is his head readable against the dark rooms?",
    # digital world
    "Does the data corruption feel integrated with the world, and is there enough digital instability?",
    "Is the corruption overused? Is it placed at transitions, BIG CLAPs, lyric appearances and peaks rather than everywhere?",
    "Do the screens (the TV, the monitor) show static?",
    # audio reactivity
    "Does the world visibly breathe on the quarter note (geometry, light, colour, objects) while the camera does NOT?",
    "Are the quarter-note responses perceptible? Are the BIG CLAPs obvious? Are musical events driving meaningful visual changes?",
    "Does anything jump or bounce in a way that looks like a malfunction?",
    # transitions
    "Does the stair sequence (~1:28) communicate 'letting go': climbing, reaching the top, stepping off into black?",
    "Does the colour transition into the final chorus (~3:19) feel spatial, a wave of coloured light through the house, rather than a screen wipe?",
    "Does the intro land precisely on the downbeat when the camera enters the house (37.43 s)?",
    "Is the intro a turbulent, violent construction of a neighbourhood (houses, a street, trees, a city), with no abstract geometry?",
    # coherence
    "Does the house feel like one coherent place (floors, stairs, a basement) without doubling back A-B-A for no reason?",
    "Do the lyrics sit on clear wall, as artifacts embedded in the world, not subtitles?",
]

CAMERA_LANGUAGE = {
    "intro-a": "a low road-level view rising over a street as it builds; no breathing",
    "intro-b": "gliding along the street as it fills",
    "intro-c": "turning on the house and accelerating into its lit window; the cut inside lands on the downbeat",
    "release": "inside: a slow, deliberate sweep around the living room; the furniture lands around the still figure",
    "verse1-a": "the same room, swept; each return finds the mannequin in another pose",
    "verse1-b": "through the back door to the kitchen, round the table; then the hall and up the stair",
    "pause": "over the top step, falling into the black",
    "letgo": "landing in the basement, walking on through its rooms",
    "verse2": "upstairs, room by room; each BIG CLAP corrupts the world its own way",
    "bridge-t": "tilting up as the ceiling flies off",
    "bridge1-a": "soaring, rising, crane up the tree of rooms",
    "bridge1-b": "slowing, landing",
    "bridge2": "near-still, slow orbit of the room of objects and the seated figure",
    "bridge3": "gliding through the dancing house to the front door; NO camera pulse",
    "chorus-a": "free, sweeping, soaring; no camera pulse",
    "chorus-b": "climbing the stair into the sky",
    "chorus-c": "free, building",
    "crash": "impact, then hold",
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
    for b0, b1 in ((5, 16), (42, 49), (75, 82)):
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
    ap.add_argument("--session", default="liminal-pass3")
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
        "project": "All You Got -- art pass 3 (a digital house and neighbourhood of luminous lines; a still mannequin; a broken simulation)",
        "mood": ["construction", "loneliness", "contemplation", "broken", "growing", "increasingly alive", "released"],
        "visual_language": ["luminous line-drawn geometry on dark", "early primitive 3D / simulated space",
                            "a neighbourhood violently constructing itself", "one coherent house: floors, a stair, a basement",
                            "a still mannequin as a recurring tableau, never animated", "lyrics as artifacts on clear wall",
                            "digital corruption at transitions, claps, lyrics and peaks", "screens showing static",
                            "BIG CLAP punctuation, each different"],
        "camera_language": ["authored", "cinematic", "the camera observes, it never breathes or pulses", "sweeps that reveal tableaux"],
        "avoid": ["camera breathing or bobbing", "objects jumping or bouncing like a malfunction", "an animated mannequin",
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
    print(f"pass 3 intent: {len(segments)} owner sections, {len(claps)} BIG CLAPs, {len(QUESTIONS)} questions")
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
