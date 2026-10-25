#!/usr/bin/env python3
"""Talking Heads review renders: the live project as a FILE project driven by a recording (tools/sonic_vfx/variant.py,
ADR-1025), with or without the Feline stems, every render through tools/gpu-lock.sh, one job per hold.

    python3 tools/talking_heads/review.py still  --at 62 [--wav feline] [--stems] [--size 1920x1080] --out F.png
    python3 tools/talking_heads/review.py clip   --range 60:75 [--wav feline|trench|<path>] [--stems] --out F.mp4
    python3 tools/talking_heads/review.py clip   --wav none --range 0:12 --out F.mp4   # the silent world, moving
    python3 tools/talking_heads/review.py silent --at 4 --out F.png          # no audio, no routes: the dormant world
    python3 tools/talking_heads/review.py clip   --range 68:80 --only mouth --out F.mp4   # one feature isolated
    python3 tools/talking_heads/review.py bench  --range 60: --frames 360 --out F.json [--wav ...] [--stems]

`--project` (default examples/sonic-abstract/talking-heads.json) points at a look-development build. `AVGEN` names the
engine (default build/release/src/avgen of this tree).
"""
import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, REPO)
from tools.sonic_vfx.variant import make_variant, make_silent  # noqa: E402

AVGEN = os.environ.get("AVGEN") or os.path.join(REPO, "build", "release", "src", "avgen")
LOCK = os.path.join(REPO, "tools", "gpu-lock.sh")
AUDIO = os.path.join(REPO, "assets", "audio")
WAVS = {"feline": "feline-footwear.wav", "trench": "trench.wav", "night": "night-shift.wav",
        "hyperborea": "hyperborea.wav"}
FELINE_STEMS = [("drums", "Drums", "drums"), ("bass", "Bass", "bass"), ("synth", "Synth", "synth")]


def wav_path(name):
    return os.path.join(AUDIO, WAVS[name]) if name in WAVS else os.path.abspath(name)


FEATURES = ("beams", "copies", "tear", "chroma")
CLOSE = ([-0.34, 0.37, 0.80], [0.0, 0.30, 0.22])   # three-quarter from the right, on the middle head's mouth and jaw


def variant(project, wav, stems, work, only=None, close=None, shut=False, slow_mouth="", no_atmos=False):
    out = os.path.join(work, "variant.json")
    make_variant(project, wav_path(wav), None, out)
    with open(out) as f:
        doc = json.load(f)
    if no_atmos:
        # the stop-3 room, for the atmosphere's cost: no mist, only the middle motes, no sky tones
        doc["effects"] = [e for e in doc["effects"] if e["id"] != "mist"]
        doc["routes"] = [r for r in doc["routes"] if not r["target"].startswith(("fx/mist/", "env/sky/"))]
        sp = doc["assets"]["scene"]["path"]
        with open(sp) as f:
            sc = json.load(f)
        sc["nodes"] = [n for n in sc["nodes"] if n["name"] not in ("motesFar", "motesNear")]
        base = os.path.dirname(sp)
        for n in sc["nodes"]:
            src = n.get("procedural", {}).get("source", {})
            if "asset" in src and not os.path.isabs(src["asset"]):
                src["asset"] = os.path.join(base, src["asset"])
            if "asset" in n and not os.path.isabs(n["asset"]):
                n["asset"] = os.path.join(base, n["asset"])
        nsp = os.path.join(work, "noatmos.scene.json")
        with open(nsp, "w") as f:
            json.dump(sc, f)
        doc["assets"]["scene"] = {"kind": "composition", "path": nsp}
    if slow_mouth:
        # one head's mouth on a slow sine (0.2 Hz: 2.5 s open, 2.5 s shut) instead of its gate; its neck and look
        # held still and its flashes, copies and wash off, so the clip shows the lips and the rays alone
        P = slow_mouth
        doc["sources"].append({"kind": "lfo", "name": "slowMouth", "settings": {"shape": "sine"}})
        doc["parameters"]["sources/slowMouth/rate"] = 0.2
        hold = {P + "Pose": [{"source": "lfo.slowMouth"}], P + "Live": [{"constant": 0.0}],
                P + "Open": [{"constant": 0.0}], P + "NeckY": [{"constant": 0.0}], P + "NeckP": [{"constant": 0.0}],
                P + "NeckLeadY": [{"constant": 0.0}], P + "NeckLeadP": [{"constant": 0.0}],
                P + "Theta": [{"constant": 0.0}]}
        for src in doc.get("sources", []):
            if src.get("kind") == "signal" and src["name"] in hold:
                src["settings"]["inputs"] = hold[src["name"]]
                src["settings"].pop("chain", None)
        doc["routes"] = [r for r in doc["routes"] if not r["target"].startswith("nodes/%s-bust/rotation" % P)]
    if shut:
        for src in doc.get("sources", []):
            if src.get("name") == "padShutHold":
                src["settings"]["inputs"] = [{"constant": 1.0}]
    if only:
        # isolate features: every other feature's control channel rests at 0 (its knob turned down)
        keep = set(only.split(","))
        for ch in doc.get("control", {}).get("channels", []):
            if ch["name"] in FEATURES and ch["name"] not in keep:
                ch["rest"] = 0.0
    if close:
        # a static close camera on the face (the jaw close-up): a copy of the scene, the camera tracks removed
        sp = doc["assets"]["scene"]["path"]
        with open(sp) as f:
            sc = json.load(f)
        sc["camera"] = {"mode": 1, "position": close[0], "target": close[1], "fov": 40.0, "orbitSpeed": 0.0}
        base = os.path.dirname(sp)
        for n in sc["nodes"]:
            src = n.get("procedural", {}).get("source", {})
            if "asset" in src and not os.path.isabs(src["asset"]):
                src["asset"] = os.path.join(base, src["asset"])
            if "asset" in n and not os.path.isabs(n["asset"]):
                n["asset"] = os.path.join(base, n["asset"])
        csp = os.path.join(work, "close.scene.json")
        with open(csp, "w") as f:
            json.dump(sc, f)
        doc["assets"]["scene"] = {"kind": "composition", "path": csp}
        doc["timeline"]["tracks"] = [t for t in doc["timeline"]["tracks"] if not t["target"].startswith("camera/")]
    if stems:
        doc["assets"]["stems"] = [{"key": k, "label": lbl, "role": role,
                                   "file": os.path.join(AUDIO, "stems", "feline-footwear (%s).wav" % lbl)}
                                  for k, lbl, role in FELINE_STEMS]
    with open(out, "w") as f:
        json.dump(doc, f, indent=1)
    return out


def cam(a):
    if a.cam:
        v = [float(x) for x in a.cam.split(",")]
        return (v[:3], v[3:])
    return CLOSE if a.close else None


def run(args):
    r = subprocess.run([LOCK, AVGEN] + args, capture_output=True, text=True)
    lines = (r.stdout + r.stderr).splitlines()
    for l in lines:
        if "render complete" in l or "[error]" in l or ("warning" in l and "route" in l):
            print(l)
    if r.returncode != 0:
        print("\n".join(lines[-20:]))
        sys.exit("avgen exited %d" % r.returncode)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["still", "clip", "silent", "bench"])
    ap.add_argument("--project", default=os.path.join(REPO, "examples", "sonic-abstract", "talking-heads.json"))
    ap.add_argument("--wav", default="feline")
    ap.add_argument("--stems", action="store_true")
    ap.add_argument("--close", action="store_true", help="a static close camera on the face")
    ap.add_argument("--shut", action="store_true", help="every gate held shut (pad 36 held): only the scans and eyes move")
    ap.add_argument("--cam", default="", help="a static camera: px,py,pz,tx,ty,tz (implies --close)")
    ap.add_argument("--only", default="", help="comma-separated features to keep (mouth, eyes, vibration, tear, "
                                                "chroma); the others' knobs rest at 0")
    ap.add_argument("--no-atmos", action="store_true", help="without stop 4's mist, extra motes and sky tones")
    ap.add_argument("--slow-mouth", default="", help="a head's prefix: its mouth on a slow sine, its look held")
    ap.add_argument("--at", type=float, default=60.0)
    ap.add_argument("--range", default="60:75")
    ap.add_argument("--size", default="1920x1080")
    ap.add_argument("--fps", default="30")
    ap.add_argument("--frames", default="360")
    ap.add_argument("--tier", default="realtime")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    work = tempfile.mkdtemp(prefix="th-review-")
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    try:
        if a.mode in ("still", "silent"):
            proj = (make_silent(a.project, os.path.join(work, "silent.json")) if a.mode == "silent"
                    else variant(a.project, a.wav, a.stems, work, a.only, cam(a), a.shut, a.slow_mouth, a.no_atmos))
            d = os.path.join(work, "frames")
            # a still lands on the instant after 2 s of the music before it, so the envelopes are warm
            run(["--headless", "--project", proj, "--render", d, "--format", "png", "--range",
                 "%.3f:%.3f" % (max(0.0, a.at - 2.0), a.at + 0.01), "--size", a.size, "--fps", "30",
                 "--particle-warmup", "240", "--tier", a.tier])
            got = sorted(glob.glob(os.path.join(d, "*.png")))
            shutil.copy(got[-1], a.out)
        elif a.mode == "clip":
            proj = (make_silent(a.project, os.path.join(work, "silent.json")) if a.wav == "none"
                    else variant(a.project, a.wav, a.stems, work, a.only, cam(a), a.shut, a.slow_mouth, a.no_atmos))
            run(["--headless", "--project", proj, "--render", a.out, "--range", a.range, "--size", a.size, "--fps",
                 a.fps, "--codec", "h264", "--quality", "85", "--particle-warmup", "240", "--tier", a.tier])
        else:
            proj = (make_silent(a.project, os.path.join(work, "silent.json")) if a.wav == "none"
                    else variant(a.project, a.wav, a.stems, work, a.only, cam(a), a.shut, a.slow_mouth, a.no_atmos))
            run(["--headless", "--project", proj, "--range", a.range, "--frames", a.frames, "--bench-json", a.out,
                 "--size", a.size])
        print(a.out)
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
