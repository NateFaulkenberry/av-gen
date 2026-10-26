#!/usr/bin/env python3
"""A synchronised diagnostic clip for one Audio Response Lab fixture run (ADR-1720).

    python3 tools/audio_lab/compose.py <run-dir>/<fixture> --out clip.mp4 [--fixtures DIR] [--render]
                                       [--range a:b] [--avgen PATH]

--render first renders the lab scene for the run's project, headless at 1920x1080 30 fps, through the GPU lock
(`tools/gpu-lock.sh avgen --project <run>/project.json --render <run>/render.mp4`). Then the clip is the render on top
and a 1920x400 timeline strip below it, frame for frame, with the fixture's mix as the soundtrack:

  - the strip is drawn from the run's frame.csv (the same 30 fps Engine::update sequence the render ran) and the
    fixture's manifest; the playhead row IS the render frame above it (frame index = row index);
  - GROUND TRUTH is drawn only here, in the strip, labelled as such: it never reaches the project or the scene;
  - the window shows 4 s of the past and 2 s ahead of the playhead. Ground truth ahead of the playhead is visible
    (it is a diagnostic overlay); detections ahead are not drawn (they have not happened).

It also writes <out>.pixels.json: the rendered brightness of each lamp and meter (sampled from the render at the
indicator's projected rectangle) against the parameter final the trace says it had on that frame, so "the picture
shows what the parameter says, on the same frame" is measured, not eyeballed.
"""

import argparse
import json
import math
import subprocess
import sys
from pathlib import Path

try:
    import numpy as np
    from PIL import Image, ImageDraw, ImageFont
except ImportError as e:  # pragma: no cover
    sys.exit(f"compose: needs numpy and Pillow ({e})")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lab  # noqa: E402

W, H_RENDER, H_STRIP = 1920, 1080, 400
PAST, AHEAD = 4.0, 2.0
REG_COL = {"low": (255, 120, 40), "mid": (240, 215, 50), "high": (80, 215, 255), "broad": (230, 230, 230)}
SRC_COL = {"drums": (255, 140, 50), "bass": (90, 150, 255), "synth": (100, 255, 130), "pad": (190, 130, 255),
           "mix": (230, 230, 230)}


def font(size):
    for f in ("/System/Library/Fonts/Menlo.ttc", "/System/Library/Fonts/SFNSMono.ttf",
              "/Library/Fonts/Arial.ttf"):
        try:
            return ImageFont.truetype(f, size)
        except OSError:
            continue
    return ImageFont.load_default()


def project_rect(camera, rect, z=0.0):
    """World rectangle on the board (z = 0 plane) -> pixel rectangle in the 1920x1080 render."""
    cz = camera["position"][2] - z
    half_h = cz * math.tan(math.radians(camera["fov"]) / 2)
    half_w = half_h * W / H_RENDER

    def px(x, y):
        return ((x / half_w) * 0.5 + 0.5) * W, (0.5 - (y / half_h) * 0.5) * H_RENDER

    x0, y1 = px(rect[0], rect[1])
    x1, y0 = px(rect[2], rect[3])
    return int(x0), int(y0), int(x1), int(y1)


def render(avgen, project, out, rng):
    cmd = [str(lab.REPO / "tools" / "gpu-lock.sh"), str(avgen), "--project", str(project), "--render", str(out)]
    if rng:
        cmd += ["--range", rng]
    r = subprocess.run(cmd, capture_output=True, text=True)
    (out.parent / "render.log").write_text(r.stdout + r.stderr)
    if r.returncode != 0:
        sys.exit(f"compose: render failed ({r.returncode}); see {out.parent / 'render.log'}")


def video_frames(path):
    p = subprocess.Popen(["ffmpeg", "-v", "error", "-i", str(path), "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
                         stdout=subprocess.PIPE)
    size = W * H_RENDER * 3
    while True:
        buf = p.stdout.read(size)
        if len(buf) < size:
            break
        yield np.frombuffer(buf, dtype=np.uint8).reshape(H_RENDER, W, 3)
    p.wait()


class Strip:
    def __init__(self, m, tr):
        self.m, self.tr = m, tr
        self.t = tr["time"]
        self.f_small, self.f_med = font(15), font(19)
        rows = [("GROUND TRUTH (diagnostic only)", "gt"), ("response.onset", "ev:response.onset"),
                ("response.low", "ev:response.low"), ("beat: GT (grey) / beat.pulse", "beat")]
        if m.get("stems"):
            for k in m["sources"]:
                rows.append((f"stem.{k}.onset / level", f"stem:{k}"))
        rows.append(("level / bass / onsetEnv", "curves"))
        rows.append(("ONSET FLASH param (final)", "param"))
        self.rows = rows
        self.top = 46
        self.row_h = (H_STRIP - self.top - 8) / len(rows)
        self.x0, self.x1 = 250, W - 20
        self.ev = {c: lab.events_of(tr, c) for c in ("response.onset", "response.low", "beat.pulse")}
        if m.get("stems"):
            for k in m["sources"]:
                self.ev[f"stem.{k}.onset"] = lab.events_of(tr, f"stem.{k}.onset")

    def x(self, s, now):
        return self.x0 + (s - (now - PAST)) / (PAST + AHEAD) * (self.x1 - self.x0)

    def draw(self, i):
        now = self.t[i]
        img = Image.new("RGB", (W, H_STRIP), (14, 15, 17))
        d = ImageDraw.Draw(img)
        m = self.m
        sample = int(round(now * lab.RATE))
        d.text((12, 8), f"{m['name']} ({m['category']})   t = {now:7.3f} s   sample {sample:>8d}   frame {i}   "
                        f"render 30 fps | analysis hop 512 @ 48 kHz (10.7 ms), window 2048 centre-stamped | "
                        f"offline file path", fill=(220, 220, 220), font=self.f_med)
        lo, hi = now - PAST, now + AHEAD
        for s in range(math.ceil(lo), math.floor(hi) + 1):
            xx = self.x(s, now)
            d.line([(xx, self.top), (xx, H_STRIP - 6)], fill=(32, 34, 38))
            d.text((xx + 2, self.top - 14), f"{s}s", fill=(90, 90, 90), font=self.f_small)
        for r, (label, kind) in enumerate(self.rows):
            y0 = self.top + r * self.row_h
            y1 = y0 + self.row_h - 3
            d.rectangle([self.x0, y0, self.x1, y1], outline=(40, 42, 46))
            d.text((10, y0 + self.row_h / 2 - 9), label, fill=(170, 170, 170), font=self.f_small)
            if kind == "gt":
                for e in m["events"]:
                    if lo <= e["time"] <= hi:
                        xx = self.x(e["time"], now)
                        col = REG_COL[e["register"]]
                        if e["ambiguous"] or not e["onset"]:
                            d.line([(xx, y0 + 2), (xx, y1 - 2)], fill=tuple(c // 3 for c in col), width=2)
                        else:
                            d.line([(xx, y0 + 2), (xx, y1 - 2)], fill=col, width=2)
                for a, b in m["silent"]:
                    if b > lo and a < hi:
                        d.rectangle([self.x(max(a, lo), now), y1 - 4, self.x(min(b, hi), now), y1 - 1],
                                    fill=(60, 60, 140))
            elif kind.startswith("ev:") or kind.startswith("stem:"):
                col_name = kind[3:] if kind.startswith("ev:") else f"stem.{kind[5:]}.onset"
                colour = SRC_COL.get(kind[5:], (255, 255, 160)) if kind.startswith("stem:") else (
                    (255, 255, 160) if "onset" in col_name else (255, 120, 40))
                if kind.startswith("stem:"):
                    lv = self.tr.get(f"stem.{kind[5:]}.level")
                    if lv:
                        pts = [(self.x(self.t[j], now), y1 - lv[j] * (y1 - y0 - 4))
                               for j in range(max(0, i - int(PAST * 30)), i + 1)]
                        if len(pts) > 1:
                            d.line(pts, fill=tuple(c // 2 for c in colour), width=1)
                for (s, v) in self.ev.get(col_name) or []:
                    if lo <= s <= now:
                        xx = self.x(s, now)
                        d.line([(xx, y1 - 2), (xx, y1 - 2 - max(0.25, min(1.0, v)) * (y1 - y0 - 6))], fill=colour,
                               width=3)
            elif kind == "beat":
                beats = (m.get("beats") or {}).get("times", [])
                for s in beats:
                    if lo <= s <= hi:
                        xx = self.x(s, now)
                        d.line([(xx, y0 + 2), (xx, (y0 + y1) / 2)], fill=(120, 120, 120), width=2)
                for (s, _) in self.ev["beat.pulse"] or []:
                    if lo <= s <= now:
                        xx = self.x(s, now)
                        d.line([(xx, (y0 + y1) / 2), (xx, y1 - 2)], fill=(255, 80, 230), width=3)
            elif kind in ("curves", "param"):
                cols = ([("response.level", (230, 230, 230)), ("response.bass", (255, 120, 40)),
                         ("response.onsetEnv", (255, 255, 160))] if kind == "curves" else
                        [("final:procedural/pFlash/material/emissive[0]", (255, 255, 255))])
                for c, colour in cols:
                    v = self.tr.get(c)
                    if not v:
                        continue
                    scale = 4.0 if kind == "param" else 1.0
                    pts = [(self.x(self.t[j], now), y1 - min(1.0, v[j] / scale) * (y1 - y0 - 4))
                           for j in range(max(0, i - int(PAST * 30)), i + 1)]
                    if len(pts) > 1:
                        d.line(pts, fill=colour, width=2)
        px = self.x(now, now)
        d.line([(px, self.top - 4), (px, H_STRIP - 4)], fill=(255, 60, 60), width=2)
        return np.asarray(img)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run_dir", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--fixtures", type=Path, default=lab.DEFAULT_FIXTURES)
    ap.add_argument("--render", action="store_true")
    ap.add_argument("--range", default="")
    ap.add_argument("--avgen", type=Path, default=lab.DEFAULT_AVGEN)
    args = ap.parse_args()
    run = args.run_dir
    fixture = args.fixtures / run.name
    m = json.loads((fixture / "manifest.json").read_text())
    project = json.loads((run / "project.json").read_text())
    rendered = run / "render.mp4"
    if args.render or not rendered.exists():
        render(args.avgen, run / "project.json", rendered, args.range)
    tr = lab.read_trace(run / "frame.csv")
    start = float(args.range.split(":")[0]) if args.range and args.range.split(":")[0] else 0.0
    first = int(round(start * lab.FRAME_FPS))
    strip = Strip(m, tr)
    camera = project["audioLab"]["camera"]
    layout = project["audioLab"]["layout"]
    probes = {n: ind for n, ind in layout.items() if ind["kind"] in ("lamp", "meter") and "rect" in ind}
    pixels = {n: {"luma": [], "final": [], "rect": project_rect(camera, ind["rect"])} for n, ind in probes.items()}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    enc = subprocess.Popen(["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s",
                            f"{W}x{H_RENDER + H_STRIP}", "-r", "30", "-i", "-", "-ss", f"{start:.4f}", "-i",
                            str(fixture / "mix.wav"), "-map", "0:v", "-map", "1:a", "-c:v", "libx264", "-pix_fmt",
                            "yuv420p", "-crf", "18", "-c:a", "aac", "-b:a", "192k", "-shortest", str(args.out)],
                           stdin=subprocess.PIPE)
    n = 0
    for k, frame in enumerate(video_frames(rendered)):
        i = first + k
        if i >= len(tr["time"]):
            break
        for name, p in pixels.items():
            x0, y0, x1, y1 = p["rect"]
            ind = probes[name]
            p["luma"].append(round(float(frame[y0:y1, x0:x1].mean()), 2))
            col = f"final:{ind['param']}[{ind.get('component', 0)}]"
            p["final"].append(tr[col][i] if col in tr else None)
        enc.stdin.write(np.vstack([frame, strip.draw(i)]).tobytes())
        n += 1
    enc.stdin.close()
    enc.wait()
    report = {}
    for name, p in pixels.items():
        f = [x for x in p["final"] if x is not None]
        c = lab.corr(p["luma"][: len(f)], f) if len(f) == len(p["luma"]) else None
        # The same correlation with the picture one frame early and one frame late: the render shows a final on the
        # frame the trace has it only if lag 0 is the best of the three.
        lags = {}
        if c is not None:
            for lag in (-1, 1):
                a = p["luma"][max(0, lag):len(f) + min(0, lag)]
                b = f[max(0, -lag):len(f) - max(0, lag)]
                lags[lag] = lab.r3(lab.corr(a, b))
        report[name] = {"rect": p["rect"], "signal": probes[name]["signal"], "frames": len(p["luma"]),
                        "corrLumaFinal": lab.r3(c), "corrPictureOneFrameEarly": lags.get(-1),
                        "corrPictureOneFrameLate": lags.get(1),
                        "lumaMin": min(p["luma"]) if p["luma"] else None,
                        "lumaMax": max(p["luma"]) if p["luma"] else None}
    Path(str(args.out) + ".pixels.json").write_text(json.dumps({"firstFrame": first, "indicators": report,
                                                                "raw": pixels}, indent=1) + "\n")
    print(f"compose: {n} frames -> {args.out}")


if __name__ == "__main__":
    main()
