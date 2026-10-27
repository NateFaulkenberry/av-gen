#!/usr/bin/env python3
"""Cross-scene response transfer (Audio Response Lab, brief §8; ADR-1720).

    python3 tools/audio_lab/transfer.py [--scenes event-horizon,rainbow-world,sacred-flight] [--fixtures DIR] [--out DIR]
                                        [--only sparse,dense,dynamic]

Drives existing live scenes with the lab's fixtures and asks of every parameter an audio route moves: does the control
behave the way its signal means, in this scene's mapping?

Each scene runs TWICE per fixture through the production engine (`avgen --sonic-trace --trace-finals`, 30 fps, under
the GPU lock): once with the fixture as its audio, once with digital silence of the same length and format. A final's
AUDIO-DRIVEN part is the difference of the two runs, which removes the scene's own timeline, camera keys and
time-driven sources. Per parameter it then measures:

  stale   the largest audio-driven deviation left once a silence has lasted 2.5 s (any release in either scene is
          shorter), as a share of the parameter's audio-driven range: a stale control does not come back;
  attack  the share of the fixture's firm attacks after which the parameter moves by at least 5% of its range within
          120 ms: a transient control answers attacks;
  release the time from an isolated attack's peak deviation to 10% of it (sparse fixture): the control's own decay;
  level   the correlation of the deviation with `response.level` (continuous controls).

Every parameter is classed by the sources of the routes that write it:
  transient    response.low / onset / note and their *Env;
  level        response.bass / level / sustain / mid / high / levelSlow / intensity;
  beat         beat.*;
  interpreted  visual.* / sonic.* / notes.* (the scene's interpret mappings and character);
  scene-specific  signal.*, macro.*, control.*, timeline.*, lfo.* ... conditioned or authored per project.
A scene-specific control is reported as such: it is not expected to transfer.
"""

import argparse
import json
import math
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lab  # noqa: E402

SCENES = {
    "event-horizon": lab.REPO / "examples" / "sonic-vfx" / "event-horizon.json",
    "rainbow-world": lab.REPO / "examples" / "rainbow-world" / "rainbow-world.json",
    # the beat clock pass: a live scene with a dozen beat.* routes
    "sacred-flight": lab.REPO / "examples" / "sonic-abstract" / "sacred-flight.json",
}
TRANSIENT = ("response.low", "response.onset", "response.note")
LEVEL = ("response.bass", "response.level", "response.sustain", "response.mid", "response.high",
         "response.levelSlow", "response.intensity", "response.transient", "response.flux")


def family(source):
    if source.startswith(TRANSIENT):
        return "transient"
    if source in LEVEL:
        return "level"
    if source.startswith("beat."):
        return "beat"
    if source.split(".")[0] in ("visual", "sonic", "notes", "timbre"):
        return "interpreted"
    return "scene-specific"


def silent_wav(like, out):
    raw = like.read_bytes()
    data = raw.index(b"data")
    n = struct.unpack("<I", raw[data + 4:data + 8])[0]
    out.write_bytes(raw[:data + 8] + bytes(n))  # same header, all-zero float samples


def generated(base, rel, roots):
    """A generated scene file (Rainbow World's is written by Blender and is not in git): this checkout's, else the
    first of `roots` (other checkouts, read only) that has it."""
    here = base / rel
    if here.exists():
        return here
    for root in roots:
        there = root / base.relative_to(lab.REPO) / rel
        if there.exists():
            return there
    return here


def scene_project(scene, audio, out, roots=()):
    p = json.loads(scene.read_text())
    base = scene.parent
    sc = p["assets"]["scene"]
    if isinstance(sc.get("path"), str):
        sc["path"] = str(generated(base, sc["path"], roots))
    elif isinstance(sc.get("path"), dict):
        sc["path"]["path"] = str(generated(base, sc["path"]["path"], roots))
    p["assets"]["audio"] = str(audio)
    p["assets"].pop("stems", None)
    p["sonic"]["live"] = False
    notes = p["sonic"].get("notes")
    if isinstance(notes, str):
        p["sonic"]["notes"] = str(base / notes)
    elif isinstance(notes, dict) and isinstance(notes.get("path"), str):
        notes["path"] = str(base / notes["path"])
    out.write_text(json.dumps(p) + "\n")
    return p


def analyse(m, scene_json, tr, quiet):
    t = tr["time"]
    writers = {}
    for r in scene_json["routes"]:
        writers.setdefault(r["target"], []).append(r["source"])
    gt = [c[0] for c in lab.cluster([e["time"] for e in m["events"] if e["onset"] and not e["ambiguous"]])]
    isolated = [g for k, g in enumerate(gt) if (k + 1 >= len(gt) or gt[k + 1] - g > 1.0)]
    out = []
    for col in tr:
        if not col.startswith("final:") or col not in quiet:
            continue
        path = col[len("final:"):].rsplit("[", 1)[0]
        d = [a - b for a, b in zip(tr[col], quiet[col])]
        span = max(d) - min(d)
        if span < 1e-4:
            continue  # nothing audio-driven reached this component
        srcs = sorted(set(writers.get(path, [])))
        fams = sorted({family(s) for s in srcs})
        settled = [(a + 2.5, b - lab.QUIET_GUARD_S) for a, b in m["silent"] if b - a > 2.5 + lab.QUIET_GUARD_S]
        rest = [abs(d[i]) for i, x in enumerate(t) if lab.in_any(x, settled)]
        stale = max(rest) / span if rest else None
        answered = 0
        for g in gt:
            i0 = max(0, lab.bisect.bisect_left(t, g - 0.034) - 1)
            i1 = lab.bisect.bisect_right(t, g + 0.120)
            if i1 > i0 + 1 and max(d[i0:i1]) - min(d[i0:i1]) >= 0.05 * span:
                answered += 1
        releases = []
        for g in isolated:
            i0 = lab.bisect.bisect_left(t, g - 0.034)
            i1 = lab.bisect.bisect_right(t, g + 1.0)
            if i1 - i0 < 3:
                continue
            seg = [abs(x) for x in d[i0:i1]]
            k = max(range(len(seg)), key=lambda j: seg[j])
            if seg[k] < 0.05 * span:
                continue
            j = next((j for j in range(k, len(seg)) if seg[j] <= 0.1 * seg[k]), None)
            releases.append(None if j is None else round(1000 * (t[i0 + j] - t[i0 + k]), 1))
        lvl = lab.corr(d, tr["response.level"]) if "response.level" in tr else None
        out.append({"parameter": col[len("final:"):], "sources": srcs, "families": fams, "range": round(span, 5),
                    "staleShare": None if stale is None else round(stale, 4),
                    "attacksAnswered": round(answered / len(gt), 3) if gt else None,
                    "releaseMs": releases, "releaseMedianMs": lab.r3(lab.pct([r for r in releases if r], 0.5)),
                    "corrWithLevel": lab.r3(lvl)})
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scenes", default="event-horizon,rainbow-world,sacred-flight")
    ap.add_argument("--fixtures", type=Path, default=lab.DEFAULT_FIXTURES)
    ap.add_argument("--out", type=Path, default=lab.REPO / "build" / "audio-lab-transfer")
    ap.add_argument("--only", default="sparse,dense,dynamic")
    ap.add_argument("--avgen", type=Path, default=lab.DEFAULT_AVGEN)
    ap.add_argument("--generated-root", type=Path, action="append", default=[],
                    help="another checkout to read a generated scene from (read only), e.g. ../av-gen")
    args = ap.parse_args()
    results = []
    for name in [s for s in args.scenes.split(",") if s]:
        for fx in [f for f in args.only.split(",") if f]:
            fdir = args.fixtures / fx
            m = json.loads((fdir / "manifest.json").read_text())
            rdir = args.out / name / fx
            rdir.mkdir(parents=True, exist_ok=True)
            silent_wav(fdir / "mix.wav", rdir / "silence.wav")
            sj = scene_project(SCENES[name], fdir / "mix.wav", rdir / "audio.json", args.generated_root)
            scene_project(SCENES[name], rdir / "silence.wav", rdir / "silence.json", args.generated_root)
            if not Path(json.loads((rdir / "audio.json").read_text())["assets"]["scene"]["path"]
                        if isinstance(sj["assets"]["scene"]["path"], str) else "").exists():
                print(f"transfer: {name}: its scene is not built here (pass --generated-root); skipped")
                continue
            lab.trace(args.avgen, rdir / "audio.json", rdir / "audio.csv", lab.FRAME_FPS, True)
            lab.trace(args.avgen, rdir / "silence.json", rdir / "silence.csv", lab.FRAME_FPS, True)
            rows = analyse(m, sj, lab.read_trace(rdir / "audio.csv"), lab.read_trace(rdir / "silence.csv"))
            results.append({"scene": name, "fixture": fx, "parameters": rows})
            print(f"transfer: {name} x {fx}: {len(rows)} audio-driven parameter components")
    # Per family and scene: the summary the brief asks for (consistent meaning, predictable behaviour).
    summary = {}
    for r in results:
        for p in r["parameters"]:
            fam = "+".join(p["families"]) or "unrouted"
            k = (r["scene"], fam)
            s = summary.setdefault(k, {"scene": r["scene"], "family": fam, "components": 0, "stale": [], "answered": [],
                                       "release": [], "corrLevel": []})
            s["components"] += 1
            if p["staleShare"] is not None:
                s["stale"].append(p["staleShare"])
            if r["fixture"] == "sparse":
                if p["attacksAnswered"] is not None:
                    s["answered"].append(p["attacksAnswered"])
                s["release"] += [x for x in p["releaseMs"] if x]
            if p["corrWithLevel"] is not None:
                s["corrLevel"].append(p["corrWithLevel"])
    table = []
    for s in summary.values():
        table.append({"scene": s["scene"], "family": s["family"], "components": s["components"],
                      "staleShareMax": max(s["stale"]) if s["stale"] else None,
                      "staleComponents": sum(1 for x in s["stale"] if x > 0.05),
                      "attacksAnsweredMedian": lab.r3(lab.pct(s["answered"], 0.5)),
                      "releaseMedianMs": lab.r3(lab.pct(s["release"], 0.5)),
                      "releaseP95Ms": lab.r3(lab.pct(s["release"], 0.95)),
                      "corrWithLevelMedian": lab.r3(lab.pct(s["corrLevel"], 0.5))})
    (args.out / "transfer.json").write_text(json.dumps({"summary": table, "runs": results}, indent=1) + "\n")
    lines = ["# Cross-scene response transfer", "",
             "Audio-driven part of each final = fixture run minus silent run. Stale: deviation left after 2.5 s of "
             "silence (share of range). Answered: share of firm attacks the control moves on within 120 ms (sparse). "
             "Release: peak to 10% after an isolated attack (sparse).", "",
             "| scene | family | components | stale > 5% (max share) | attacks answered (median) | release median / p95 ms | corr with response.level |",
             "|---|---|---|---|---|---|---|"]
    for x in sorted(table, key=lambda x: (x["family"], x["scene"])):
        lines.append(f"| {x['scene']} | {x['family']} | {x['components']} | {x['staleComponents']} ({x['staleShareMax']}) | "
                     f"{x['attacksAnsweredMedian']} | {x['releaseMedianMs']} / {x['releaseP95Ms']} | {x['corrWithLevelMedian']} |")
    # The same signal in both scenes: one row per (source, scene), for parameters a single source writes.
    by = {}
    for r in results:
        for p in r["parameters"]:
            if len(p["sources"]) != 1:
                continue
            k = (p["sources"][0], r["scene"])
            b = by.setdefault(k, {"stale": [], "answered": [], "release": [], "corr": [], "n": 0})
            b["n"] += 1
            if p["staleShare"] is not None:
                b["stale"].append(p["staleShare"])
            if r["fixture"] == "sparse":
                if p["attacksAnswered"] is not None:
                    b["answered"].append(p["attacksAnswered"])
                b["release"] += [x for x in p["releaseMs"] if x]
            if p["corrWithLevel"] is not None:
                b["corr"].append(p["corrWithLevel"])
    lines += ["", "## By source (parameters one source writes)", "",
              "| source | family | scene | components | stale > 5% | attacks answered (median) | release median ms | corr with level |",
              "|---|---|---|---|---|---|---|---|"]
    for (src, scene), b in sorted(by.items()):
        lines.append(f"| `{src}` | {family(src)} | {scene} | {b['n']} | {sum(1 for x in b['stale'] if x > 0.05)} | "
                     f"{lab.r3(lab.pct(b['answered'], 0.5))} | {lab.r3(lab.pct(b['release'], 0.5))} | "
                     f"{lab.r3(lab.pct(b['corr'], 0.5))} |")
    (args.out / "transfer.md").write_text("\n".join(lines) + "\n")
    print(f"transfer: {args.out / 'transfer.md'}")


if __name__ == "__main__":
    main()
