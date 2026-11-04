"""Pass 3: runs the Creative Critic over a camera-set render (render_cameras.py) and per-leg clips, and writes one
summary, so every phase is judged the same way (55- §2, 56- "Cinematic Critic").

  python3 examples/strange-attractor/pass3/critic_run.py <frames_dir> <clips_dir|-> <out.json> --label <label>
      [--worlds canyon,moon,...] [--compare <earlier out.json>]

- Stills (deep mode, one job per world): image statistics plus three learned aesthetic/quality models
  (pyiqa topiq_iaa, clipiqa+, laion_aes; "experimental" in the Critic, 0.55-0.65 agreement with a production's
  own preferences, MODEL_BENCHMARKS.md §3: weak evidence, read as a trend across many frames, never one number).
- Clips (preview mode, one job per leg, film time via --video-start, the film's own audio): the 16 dimensions,
  findings, motion/shimmer/temporal measurements.
Session "sa-pass3", tracks "stills-<world>" and "clip-<leg>": the Critic keeps each track's history, so a later
run's job compares to the previous one automatically. The Critic is evidence; the owner's eye decides.
"""
import argparse, json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
CRITIC = os.path.expanduser("~/Documents/GitHub/creative-critic/.venv/bin/critic")
MODELS = ["pyiqa:topiq_iaa", "pyiqa:clipiqa+", "pyiqa:laion_aes"]
LEGS = {"canyon": 0.0, "moon": 32.025, "dusk": 47.87, "mars": 69.655, "night": 93.46, "ocean": 121.217, "dawn": 147.017}
# the camera set's worlds: a still belongs to a world by its id (r-<leg>-, x-<a>-<b>-, e-/h-<world>)
WORLDS = ["canyon", "moon", "dusk", "mars", "night", "ocean", "dawn", "crossings"]


def world_of(cid):
    p = cid.split("-")
    return "crossings" if p[0] == "x" else p[1]   # r-<leg>-t, e-/h-<world> ("canyon-day" -> "canyon")


def submit(args):
    out = subprocess.run([CRITIC, "submit", *args, "--session", "sa-pass3", "--wait", "--json", "--timeout", "3600"],
                         capture_output=True, text=True)
    if out.returncode != 0:
        print(out.stderr[-2000:], file=sys.stderr)
        raise SystemExit(f"critic failed ({out.returncode})")
    s = json.loads(out.stdout)
    return s, json.load(open(s["report_json"]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("frames"); ap.add_argument("clips"); ap.add_argument("out")
    ap.add_argument("--label", required=True)
    ap.add_argument("--worlds", default=",".join(WORLDS))
    ap.add_argument("--no-stills", action="store_true")
    ap.add_argument("--merge", help="an earlier out.json whose entries this run keeps unless it replaces them")
    a = ap.parse_args()
    want = a.worlds.split(",")
    cams = {c["id"]: c for c in json.load(open(os.path.join(HERE, "cameras.json")))["cameras"]}
    res = {"label": a.label, "stills": {}, "clips": {}}
    if a.merge:
        old = json.load(open(a.merge))
        res["stills"].update(old.get("stills", {})); res["clips"].update(old.get("clips", {}))
    for w in ([] if a.no_stills else want):
        ids = sorted(i for i in cams if os.path.exists(os.path.join(a.frames, i + ".png")) and world_of(i) == w)
        if not ids:
            continue
        s, r = submit(["--mode", "deep", "--images", *[os.path.join(a.frames, i + ".png") for i in ids],
                       "--options", json.dumps({"models": MODELS}), "--track", f"stills-{w}", "--label", a.label])
        shots = r["measurements"]["shots"]
        rows = {}
        for k, cid in enumerate(ids):
            sid = f"img{k + 1:02d}"
            im = (shots.get(sid) or {}).get("image") or {}
            row = {m: im[m]["mean"] for m in ("luma_mean", "contrast_rms", "sat_mean", "warmth", "sharpness",
                                               "dynamic_range", "clip_frac", "crush_frac") if m in im}
            for n, mo in (r.get("model_outputs") or {}).items():
                v = (mo.get("per_shot") or {}).get(sid)
                if v:
                    row[n] = v["mean"]
            row["findings"] = [f"{f['severity']}: {f['title']}" for f in r["findings"] if f.get("shot") == sid]
            rows[cid] = row
        res["stills"][w] = {"job": s["job_id"], "images": rows}
        print(f"stills {w}: {s['job_id']} ({len(ids)} images)", flush=True)
    if a.clips != "-":
        intent = os.path.join(HERE, "intent.json")
        for leg, t0 in LEGS.items():
            if leg not in want:
                continue
            clip = os.path.join(a.clips, f"leg-{leg}.mp4")
            if not os.path.exists(clip):
                continue
            s, r = submit(["--mode", "preview", "--video", clip, "--video-start", str(t0), "--intent", intent,
                           "--track", f"clip-{leg}", "--label", a.label])
            meas = {}
            for sh in r["measurements"]["shots"].values():
                for grp, keys in (("image", ("luma_mean", "contrast_rms", "sat_mean", "sharpness", "crush_frac")),
                                  ("motion", ("shimmer", "jitter")), ("temporal", ("flicker", "change_mean"))):
                    g = sh.get(grp) or {}
                    for k in keys:
                        v = g.get(k)
                        v = v.get("mean") if isinstance(v, dict) else v
                        if isinstance(v, (int, float)):
                            meas.setdefault(k, []).append(v)
            res["clips"][leg] = {"job": s["job_id"], "headline": s["headline"], "dimensions": s["dimensions"],
                                 "measured": {k: round(sum(v) / len(v), 4) for k, v in meas.items()},
                                 "findings": [f"{f['severity']}: {f['title']} ({f.get('shot')})" for f in r["findings"]][:15]}
            print(f"clip {leg}: {s['job_id']}", flush=True)
    json.dump(res, open(a.out, "w"), indent=1)
    print("wrote", a.out)


if __name__ == "__main__":
    main()
