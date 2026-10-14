#!/usr/bin/env python3
"""Open every example in examples/index.json through the application and render one frame of it, for review.

    python3 tools/review_examples.py --out <dir> [--avgen build/release/src/avgen] [--at 10] [--size 640x360]
                                     [--only <substring>] [--no-lock]

Each entry is opened the way File > Examples opens it: `avgen --example "<name>" --render <dir> --range <at>:...`,
so a pass here means the menu's name resolves, the project and its scene load, the renderer draws a frame, and the
process exits 0. The frame is then checked for the two failures that exit 0 anyway: black, and flat (one colour).

Writes into <dir>:
  frames/<slug>.png          the rendered frame
  logs/<slug>.log            the app's log for that entry
  report.json, report.md     per entry: live/offline, category, readiness (the menu's: Ready / NoAudio /
                             Unavailable), exit code, seconds, mean and spread of the frame, verdict, first errors
  contact-sheet-{live,offline}.png   the frames on labelled sheets, one per submenu

Run it from the repository root on an idle GPU: by default every render takes tools/gpu-lock.sh. The verdicts are
what a script can see. A frame that is not black and not flat can still be wrong, so the sheets are for a person.
Needs Pillow.
"""
import argparse
import json
import os
import pathlib
import re
import subprocess
import sys
import time

try:
    from PIL import Image, ImageDraw, ImageStat
except ImportError:  # pragma: no cover
    print("review_examples.py needs Pillow (python3 -m pip install pillow)", file=sys.stderr)
    sys.exit(2)

ROOT = pathlib.Path(__file__).resolve().parent.parent
BLACK_MEAN = 2.0   # mean 8-bit luma below this: a black frame
FLAT_SPREAD = 1.5  # luma standard deviation below this: one colour, nothing drawn


def slug(name: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")


def resolve(stored: str, base: pathlib.Path) -> pathlib.Path:
    """app/project_paths.hpp's resolveFrom: ~/ is home, absolute is itself, else relative to the project."""
    if stored.startswith("~/"):
        return pathlib.Path(os.path.expanduser(stored))
    p = pathlib.Path(stored)
    return p if p.is_absolute() else (base / p)


def readiness(entry_file: pathlib.Path) -> tuple:
    """The menu's app::exampleStatus, for the report: (readiness, missing files)."""
    missing = []
    if not entry_file.exists():
        return "Unavailable", [str(entry_file)]
    try:
        doc = json.loads(entry_file.read_text())
    except (OSError, ValueError):
        return "Ready", []
    assets = doc.get("assets") if isinstance(doc, dict) else None
    audio_missing = []
    if isinstance(assets, dict):
        scene = assets.get("scene")
        if isinstance(scene, dict):
            path = scene.get("path")
            path = path.get("path") if isinstance(path, dict) else path
            if isinstance(path, str) and not resolve(path, entry_file.parent).exists():
                missing.append(str(resolve(path, entry_file.parent)))
        audio = assets.get("audio")
        if isinstance(audio, dict) and isinstance(audio.get("path"), str) and audio["path"]:
            song = resolve(audio["path"], entry_file.parent)
            if not song.exists():
                audio_missing.append(str(song))
    if missing:
        return "Unavailable", missing + audio_missing
    return ("NoAudio", audio_missing) if audio_missing else ("Ready", [])


def frame_stats(png: pathlib.Path) -> tuple:
    image = Image.open(png).convert("L")
    stat = ImageStat.Stat(image)
    return stat.mean[0], stat.stddev[0]


def first_errors(log: str, limit: int = 3) -> list:
    lines = [l for l in log.splitlines() if "[error]" in l or "[critical]" in l]
    return [re.sub(r"^\[[^\]]*\] ", "", l)[:300] for l in lines[:limit]]


def sheet(rows: list, out: pathlib.Path, title: str, thumb=(320, 180), columns=4) -> None:
    label_h, gutter = 34, 8
    n = max(1, len(rows))
    cols = min(columns, n)
    lines = (n + cols - 1) // cols
    w = cols * thumb[0] + (cols + 1) * gutter
    h = 30 + lines * (thumb[1] + label_h + gutter) + gutter
    canvas = Image.new("RGB", (w, h), (16, 16, 18))
    draw = ImageDraw.Draw(canvas)
    draw.text((gutter, 8), title, fill=(235, 235, 235))
    for i, r in enumerate(rows):
        x = gutter + (i % cols) * (thumb[0] + gutter)
        y = 30 + (i // cols) * (thumb[1] + label_h + gutter)
        frame = r.get("frame")
        if frame and pathlib.Path(frame).exists():
            canvas.paste(Image.open(frame).convert("RGB").resize(thumb), (x, y))
        else:
            draw.rectangle([x, y, x + thumb[0] - 1, y + thumb[1] - 1], outline=(120, 60, 60))
            draw.text((x + 8, y + thumb[1] // 2 - 6), r["verdict"], fill=(230, 120, 120))
        colour = (150, 220, 150) if r["verdict"] == "ok" else (235, 170, 90)
        draw.text((x, y + thumb[1] + 3), r["name"][:44], fill=(230, 230, 230))
        draw.text((x, y + thumb[1] + 17), f'{r["category"][:22]} | {r["verdict"]}', fill=colour)
    canvas.save(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", required=True)
    ap.add_argument("--avgen", default=str(ROOT / "build" / "release" / "src" / "avgen"))
    ap.add_argument("--at", type=float, default=10.0, help="timeline second to render (default 10)")
    ap.add_argument("--size", default="640x360")
    ap.add_argument("--only", default="", help="only entries whose name contains this")
    ap.add_argument("--no-lock", action="store_true", help="do not take tools/gpu-lock.sh")
    args = ap.parse_args()

    out = pathlib.Path(args.out).resolve()
    (out / "frames").mkdir(parents=True, exist_ok=True)
    (out / "logs").mkdir(exist_ok=True)
    index = json.loads((ROOT / "examples" / "index.json").read_text())["examples"]
    rows = []
    for e in index:
        if args.only and args.only.lower() not in e["name"].lower():
            continue
        rel = e.get("project") or e.get("scene") or e.get("recipe")
        entry_file = (ROOT / "examples" / rel).resolve()
        ready, missing = readiness(entry_file)
        s = slug(e["name"])
        row = {"name": e["name"], "category": e.get("category", ""), "live": bool(e.get("live")),
               "file": f"examples/{rel}", "readiness": ready, "missing": missing, "generate": e.get("generate", "")}
        if ready == "Unavailable":
            row.update(verdict="not generated" if e.get("generate") else "missing files", exit=None, seconds=0.0)
            rows.append(row)
            print(f"{row['verdict']:>14}  {e['name']}", flush=True)
            continue
        target = out / "frames" / s
        cmd = [args.avgen, "--example", e["name"], "--render", str(target), "--format", "png",
               "--range", f"{args.at}:{args.at + 0.02}", "--size", args.size]
        if not args.no_lock:
            cmd = [str(ROOT / "tools" / "gpu-lock.sh")] + cmd
        t0 = time.time()
        proc = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
        seconds = time.time() - t0
        log = proc.stdout + proc.stderr
        (out / "logs" / f"{s}.log").write_text(log)
        pngs = sorted(target.glob("*.png")) if target.exists() else []
        row.update(exit=proc.returncode, seconds=round(seconds, 1), errors=first_errors(log))
        if proc.returncode != 0 or not pngs:
            row["verdict"] = f"failed (exit {proc.returncode})" if proc.returncode != 0 else "no frame"
        else:
            frame = out / "frames" / f"{s}.png"
            pngs[0].replace(frame)
            for extra in target.glob("*"):
                extra.unlink()
            target.rmdir()
            mean, spread = frame_stats(frame)
            row.update(frame=str(frame), mean=round(mean, 2), spread=round(spread, 2))
            row["verdict"] = "black" if mean < BLACK_MEAN else ("flat" if spread < FLAT_SPREAD else "ok")
        rows.append(row)
        print(f"{row['verdict']:>14}  {e['name']}  ({row['seconds']} s)", flush=True)

    (out / "report.json").write_text(json.dumps(rows, indent=1))
    for live, label in ((True, "live"), (False, "offline")):
        part = [r for r in rows if r["live"] == live]
        if part:
            sheet(part, out / f"contact-sheet-{label}.png", f"Examples > {label.capitalize()} ({len(part)}) at {args.at} s")
    md = ["| Submenu | Category | Example | Readiness | Verdict | Exit | s | Mean | Spread | First error |",
          "|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        md.append("| {} | {} | {} | {} | {} | {} | {} | {} | {} | {} |".format(
            "Live" if r["live"] else "Offline", r["category"], r["name"], r["readiness"], r["verdict"],
            "" if r.get("exit") is None else r["exit"], r.get("seconds", ""), r.get("mean", ""), r.get("spread", ""),
            (r.get("errors") or [""])[0].replace("|", "/")[:120]))
    counts = {}
    for r in rows:
        counts[r["verdict"]] = counts.get(r["verdict"], 0) + 1
    (out / "report.md").write_text(f"# Example review ({len(rows)} entries, frame at {args.at} s)\n\n"
                                   + ", ".join(f"{k}: {v}" for k, v in sorted(counts.items())) + "\n\n"
                                   + "\n".join(md) + "\n")
    print(json.dumps(counts))
    return 0 if all(r["verdict"] in ("ok", "not generated") for r in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
