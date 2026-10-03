#!/usr/bin/env python3
"""The Sonic VFX test matrix (02-brief-vfx-expansion.md §20-21): every scene through every input class, measured.

    python3 tools/sonic_vfx/matrix.py run <scene-id> [class ...] [--projects DIR] [--out DIR] [--size 480x270]
    python3 tools/sonic_vfx/matrix.py report [--out DIR] [--md docs/prototypes/sonic-garden/TEST-MATRIX.md]

`run` renders each class's clip with its audio (the file path: the same character, context and response as live,
ADR-1025), writes the pinned engine's signal trace, and runs the evaluator (tools/sonic_vfx_critic.py measure) on
it. `report` gathers every report under --out into the matrix: per scene and class, what the scene's vocabulary
expects of that input, what the picture was measured to answer (signals at z >= 3, with event latency), the
evaluator's scores, and its findings.
"""
import argparse
import csv
import glob
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, REPO)
from tools.sonic_vfx import review  # noqa: E402

# What each input class exercises: the signals the vocabulary rows for that class should answer.
CLASS_EXPECTS = {
    "pads": ["response.sustain", "notes.polyphony", "notes.tension", "notes.held"],
    "chords": ["notes.polyphony", "notes.tension", "response.note"],
    "bass": ["response.bass", "response.note", "notes.lastPitch"],
    "lead": ["response.note", "notes.lastPitch", "notes.lastVelocity"],
    "arp": ["response.note", "notes.lastPitch"],
    "edrums": ["response.kick", "response.snare", "response.hat"],
    "drumloop": ["response.kick", "response.snare", "response.hat"],
    "dense": ["response.note", "notes.polyphony", "notes.density"],          # (no drums in this class)
    "sparse": ["response.note", "notes.lastPitch"],
    "velocity": ["notes.lastVelocity", "response.note"],
    "rapid": ["response.note", "notes.lastPitch"],
    "sustained": ["response.sustain", "notes.held"],
    "full": ["response.note", "response.bass", "response.kick", "response.snare", "response.hat",
             "response.sustain"],
}


def run(a):
    classes = a.classes or review.CLASSES
    out = os.path.join(a.out, a.scene)
    os.makedirs(out, exist_ok=True)
    for cls in classes:
        proj, dur = review.variant(a.scene, cls, out, a.projects or None)
        base = os.path.join(out, cls)
        if not (a.skip_existing and os.path.exists(base + ".critic.json")):
            review.run(["--headless", "--project", proj, "--render", base + ".mp4", "--range", "0:%.2f" % dur,
                        "--size", a.size, "--fps", a.fps, "--codec", "h264", "--quality", "70",
                        "--particle-warmup", "120", "--tier", a.tier])
            subprocess.run([review.AVGEN, "--project", proj, "--sonic-trace", base + ".trace.csv"],
                           capture_output=True)
            subprocess.run([sys.executable, os.path.join(REPO, "tools", "sonic_vfx_critic.py"), "measure",
                            "--video", base + ".mp4", "--trace", base + ".trace.csv", "--project", proj, "--out",
                            base + ".critic.json"], capture_output=True)
        print(a.scene, cls, "ok" if os.path.exists(base + ".critic.json") else "MISSING")


def vocab_for(doc):
    rows = (doc.get("sonicScene") or {}).get("vocabulary") or []
    return {r[1]: r for r in rows if len(r) > 2 and isinstance(r[1], str) and "." in r[1]}


HITS = {"response.kick": "response.kickEnv", "response.snare": "response.snareEnv", "response.hat": "response.hatEnv",
        "response.note": "response.noteEnv", "response.low": "response.lowEnv", "response.onset": "response.onsetEnv"}


def played(trace_csv, signal):
    """Whether the input exercised a signal at all: a hit's envelope passed 0.3, or a level moved."""
    col = HITS.get(signal, signal)
    try:
        with open(trace_csv) as f:
            r = csv.reader(f)
            h = next(r)
            if col not in h:
                return None
            i = h.index(col)
            vals = [float(x[i]) for x in r if x]
    except (OSError, StopIteration, ValueError):
        return None
    if not vals:
        return None
    if signal in HITS:
        # a hit is an envelope's jump (fast notes keep it above any fixed level, so count rises, not crossings)
        n, last = 0, -10
        for k in range(1, len(vals)):
            if vals[k] - vals[k - 1] > 0.07 and vals[k] > 0.3 and k - last > 3:
                n, last = n + 1, k
        return n
    return max(vals) - min(vals)


def observed(rep, trace_csv, sigs):
    """For each expected signal: answered (z >= 3), weak (2 <= z < 3), silent (z < 2), or not played by this input
    (a hit the detector never fired, a level that never moved)."""
    av = rep.get("measures", {}).get("audioVisual", {})
    out = []
    for sig in sigs:
        n = played(trace_csv, sig)
        if n is not None and ((sig in HITS and n == 0) or (sig not in HITS and n < 0.05)):
            out.append("%s: not played" % sig.split(".")[-1])
            continue
        r = av.get(HITS.get(sig, sig)) or av.get(sig) or {}
        z = r.get("z", 0.0)
        e = r.get("erp") or {}
        lat = (", %.0f ms" % (1000 * e["latency"])) if e.get("latency") is not None and sig in HITS else ""
        verdict = "answered" if z >= 3 else ("weak" if z >= 2 else "silent")
        hits = (" (%d hits)" % n) if sig in HITS and n else ""
        out.append("%s: %s, z %.1f%s%s" % (sig.split(".")[-1], verdict, z, lat, hits))
    return out


KNOWN = ()   # findings left out of the problems column (none since the evaluator's 28347601 palette search)


def sheet(scene_dir, out_png, title):
    """One frame of every class's clip (at 60% of it), labelled: how the scene looks under each input."""
    from PIL import Image, ImageDraw
    tiles = []
    for cls in review.CLASSES:
        clip = os.path.join(scene_dir, cls + ".mp4")
        if not os.path.exists(clip):
            continue
        dur = float(subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0",
                                    clip], capture_output=True, text=True).stdout.strip() or 1.0)
        png = os.path.join(scene_dir, cls + "-frame.png")
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-ss", "%.2f" % (dur * 0.6), "-i", clip, "-frames:v", "1",
                        png], capture_output=True)
        if os.path.exists(png):
            tiles.append((cls, Image.open(png).convert("RGB")))
    if not tiles:
        return None
    w, h = 384, 216
    cols = 4
    rows = (len(tiles) + cols - 1) // cols
    canvas = Image.new("RGB", (cols * w, rows * (h + 18) + 24), (12, 12, 14))
    d = ImageDraw.Draw(canvas)
    d.text((8, 5), title, fill=(235, 235, 235))
    for k, (cls, im) in enumerate(tiles):
        x, y = (k % cols) * w, 24 + (k // cols) * (h + 18)
        canvas.paste(im.resize((w, h)), (x, y + 18))
        d.text((x + 6, y + 3), cls, fill=(220, 220, 220))
    canvas.save(out_png)
    return out_png


def short(text, n=70):
    """A vocabulary row's description for a table cell: up to its first parenthesis, cut at a word."""
    t = text.split(" (")[0].strip()
    if len(t) <= n:
        return t
    return t[:n].rsplit(" ", 1)[0] + "..."


def set_list():
    """The scene ids in set-list order (tools/sonic_vfx/scenes SCENES), then anything else found."""
    import importlib
    from tools.sonic_vfx.scenes import SCENES
    return [importlib.import_module("tools.sonic_vfx.scenes." + n).ID for n in SCENES]


def tally(rep, trace_csv, sigs):
    """(answered, played) over the expected signals of one clip."""
    n_ans = n_play = 0
    for o in observed(rep, trace_csv, sigs):
        if "not played" in o:
            continue
        n_play += 1
        n_ans += ": answered" in o
    return n_ans, n_play


def report(a):
    found = {d for d in os.listdir(a.out) if os.path.isdir(os.path.join(a.out, d))}
    order = set_list()
    scenes = [s for s in order if s in found] + sorted(found - set(order))
    try:
        from tools.sonic_vfx import matrix_notes
    except ImportError:  # pragma: no cover
        matrix_notes = None
    lines = ["# Sonic VFX test matrix", "",
             "Every scene through the thirteen input classes of the synthesized test material",
             "(`tools/sonic_vfx/make_test_material.py`), rendered from the file path (the same response, character and",
             "context as live, ADR-1025) and measured by the evaluator (`tools/sonic_vfx_critic.py measure`).",
             "Generated by `tools/sonic_vfx/matrix.py report`; the per-clip reports are in the review folder.", "",
             "- **Expected:** the scene's vocabulary rows that this input exercises (what the scene says the world "
             "does).",
             "- **Observed:** signals the picture measurably answers (synchrony z >= 3), strongest first, with the "
             "event latency for hits.",
             "- **Quality:** the evaluator's mean dimension score (0..1) and its lowest dimension.",
             "- **Problems:** the evaluator's findings for that clip (severity medium and up).", ""]
    if matrix_notes is not None:   # the material, the detector on it, how to read a row (tools/sonic_vfx)
        lines += [matrix_notes.PREAMBLE.rstrip(), ""]
    # the overview: per scene and input, expected signals answered / expected signals the input played
    grid = ["## The matrix at a glance", "",
            "Each cell is *answered/played*: of the vocabulary signals that input exercises and actually played, "
            "how many the picture measurably answers (z >= 3). A dash: the input exercises no row of the scene's "
            "vocabulary.", "",
            "| scene | " + " | ".join(review.CLASSES) + " |", "|---|" + "---|" * len(review.CLASSES)]
    for sc in scenes:
        cells = []
        proj = None
        for cls in review.CLASSES:
            rp = os.path.join(a.out, sc, cls + ".critic.json")
            if not os.path.exists(rp):
                cells.append(" ")
                continue
            proj = proj or os.path.join(a.out, sc, "%s--%s.json" % (sc, cls))
            doc = json.load(open(proj)) if os.path.exists(proj) else {}
            vocab = vocab_for(doc)
            sigs = [x for x in CLASS_EXPECTS.get(cls, []) if x in vocab][:4]
            n_ans, n_play = tally(json.load(open(rp)), os.path.join(a.out, sc, cls + ".trace.csv"), sigs)
            cells.append("%d/%d" % (n_ans, n_play) if n_play else "-")
        grid.append("| %s | %s |" % (sc, " | ".join(cells)))
    lines += grid + [""]
    if matrix_notes is not None and getattr(matrix_notes, "LIVE", ""):
        lines += [matrix_notes.LIVE.rstrip(), ""]
    for sc in scenes:
        reps = {}
        for p in sorted(glob.glob(os.path.join(a.out, sc, "*.critic.json"))):
            cls = os.path.basename(p).split(".")[0]
            reps[cls] = json.load(open(p))
        if not reps:
            continue
        proj = os.path.join(a.out, sc, "%s--%s.json" % (sc, next(iter(reps))))
        doc = json.load(open(proj)) if os.path.exists(proj) else {}
        vocab = vocab_for(doc)
        lines += ["## %s" % ((doc.get("sonicScene") or {}).get("title") or sc), ""]
        if (doc.get("sonicScene") or {}).get("thesis"):
            lines += ["*%s*" % doc["sonicScene"]["thesis"], ""]
        verdict = (getattr(matrix_notes, "VERDICTS", {}) or {}).get(sc)
        if verdict:
            lines += ["**Verdict.** " + verdict.strip(), ""]
        png = sheet(os.path.join(a.out, sc), os.path.join(a.out, "%s-sheet.png" % sc),
                    (doc.get("sonicScene") or {}).get("title") or sc)
        if png:
            lines += ["Every input, one frame each: `%s`." % png.replace(os.path.expanduser("~"), "~"), ""]
        lines += ["| input | expected | observed | quality | problems |", "|---|---|---|---|---|"]
        for cls in review.CLASSES:
            r = reps.get(cls)
            if r is None:
                continue
            sigs = [x for x in CLASS_EXPECTS.get(cls, []) if x in vocab][:4]
            exp_txt = "; ".join(short(vocab[x][2]) for x in sigs) or "(no row of the vocabulary)"
            obs = observed(r, os.path.join(a.out, sc, cls + ".trace.csv"), sigs)
            sc_ = r.get("scores", {})
            mean = sum(sc_.values()) / max(1, len(sc_))
            low = min(sc_.items(), key=lambda kv: kv[1]) if sc_ else ("-", 0)
            probs = [f["title"] for f in r.get("findings", [])
                     if f.get("severity") in ("medium", "high") and f["title"] not in KNOWN]
            lines.append("| %s | %s | %s | %.2f (low: %s %.2f) | %s |" % (
                cls, exp_txt.replace("|", "/"), "; ".join(obs) or "-", mean, low[0], low[1],
                "; ".join(probs[:4]) or "none"))
        lines.append("")
    open(a.md, "w").write("\n".join(lines) + "\n")
    print(a.md)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["run", "report"])
    ap.add_argument("scene", nargs="?")
    ap.add_argument("classes", nargs="*")
    ap.add_argument("--projects", default="")
    ap.add_argument("--out", default=os.path.expanduser("~/Desktop/av-gen-review/25-sonic-vfx/matrix"))
    ap.add_argument("--size", default="480x270")
    ap.add_argument("--fps", default="24")
    ap.add_argument("--skip-existing", action="store_true")
    ap.add_argument("--tier", default="realtime")
    ap.add_argument("--md", default=os.path.join(REPO, "docs", "prototypes", "sonic-garden", "TEST-MATRIX.md"))
    a = ap.parse_args()
    if a.mode == "run":
        run(a)
    else:
        report(a)


if __name__ == "__main__":
    main()
