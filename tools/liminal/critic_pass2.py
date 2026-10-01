#!/usr/bin/env python3
"""Creative Critic inputs for All You Got, judged by the art pass 2 brief (02-art-pass-2.md, section 12).

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
    # scene quality
    "Is the environment visually interesting? Does the scene feel intentionally composed?",
    "Is there enough visual information? Does the scene feel barren? Is geometry connected logically?",
    # art direction
    "Does the scene resemble a primitive simulated 3D universe of luminous line-drawn geometry on dark?",
    "Does it drift toward generic 3D? Does it accidentally become Glowmere (green, bioluminescent, fantasy)?",
    "Is the colour palette appropriate for this section, and does colour change when the music demands it?",
    # audio reactivity
    "Can the quarter-note relationship actually be perceived?",
    "Are eighth-note events visually distinct where requested (intro bars 5-16, let it go, bridge 2)?",
    "Are BIG CLAP events obvious, and is each one a different punctuation rather than the same effect?",
    "Do musical transitions correspond to meaningful visual transitions?",
    # camera
    "Is the camera movement purposeful and authored? Too much? Too little? Does it fit the section?",
    # composition
    "Is the frame interesting, with a focal point, foreground/middle/background, and something to look at?",
    # entity effects
    "Are effects serving the scene, musically timed, not overused; do they make the world feel alive?",
    # phase 2 asks
    "Where are the barren environments, disconnected geometry, weak compositions, excessive camera movement?",
    "Where are the opportunities for visual storytelling, stronger colour and environmental animation?",
]

CAMERA_LANGUAGE = {
    "intro-a": "near-still; breathing on the quarter note (small push, FOV)",
    "intro-b": "breathing on quarters with an eighth-note shiver",
    "intro-c": "a slow push that accelerates with the riser",
    "release": "an impact, then a journey through small rooms; settling",
    "verse1-a": "slow, purposeful; a room every ~4 bars; holds to discover details",
    "verse1-b": "slow, purposeful; a room every ~4 bars",
    "pause": "abrupt stop; still",
    "letgo": "quicker room to room, purposeful",
    "verse2": "verse pacing, slightly more energetic and restless",
    "bridge-t": "lifting",
    "bridge1-a": "soaring, rising, crane up",
    "bridge1-b": "slowing, landing",
    "bridge2": "near-still, slow orbit of the objects",
    "bridge3": "soaring, flowing, gliding; NO quarter-note camera pulse",
    "chorus-a": "free, sweeping, soaring; no quarter-note camera pulse",
    "chorus-b": "free, sweeping, exploring",
    "chorus-c": "free, building",
    "crash": "impact, then hold",
    "ending": "slowing to still",
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
    ap.add_argument("--session", default="liminal-pass2")
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
        "project": "All You Got -- art pass 2 (a primitive simulated universe of luminous line-drawn geometry)",
        "mood": ["winding up", "loneliness", "tension", "growth", "reflection", "celebration", "release", "resolution"],
        "visual_language": ["luminous line-drawn geometry on dark", "early primitive 3D / wireframe / simulated space",
                            "small dense furnished rooms", "simple line-drawn outdoor spaces", "spatial lyric typography",
                            "a slightly broken simulation", "mannequins, sparingly"],
        "camera_language": ["authored", "varies by section", "breathing only where it is asked for"],
        "avoid": ["large empty rooms", "endless halls", "giant caverns", "barren rooms", "disconnected floating geometry",
                  "Glowmere", "default green", "generic wireframe demo", "generic 3D", "flat screen-space subtitles",
                  "a generic scale multiplier on the audio", "every quarter note an obvious camera shake",
                  "random glitch for its own sake", "procedural randomness as the director"],
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
    scene.setdefault("timeline", {})["sections"] = [{"id": s["id"], "name": s["name"], "start": s["start"], "end": s["end"]}
                                                  for s in segments]
    json.dump(scene, open(scene_path, "w"), indent=1)
    print(f"pass 2 intent: {len(segments)} owner sections, {len(claps)} BIG CLAPs, {len(QUESTIONS)} questions")
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
