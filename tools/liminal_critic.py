#!/usr/bin/env python3
"""Temporal audiovisual analysis for the Liminal POC, and the Creative Critic's inputs for an SDF project.

The Creative Critic (`../creative-critic`) could not describe an SDF project: its AV Gen adapter reads
Glowmere-shaped scenes (`cameraDirection`, heroes, entities) and produced no shots, no sections and no
camera for the Procedural Space POC, whose art pass hand-wrote its inputs. This tool does two things:

  inputs    Writes a Critic `inputs.json` (plus `intent.json`, `shots.json`, `scene.json`) for a Liminal
            project: sections and shots from the art direction's section map (bars -> seconds on the song's
            tempo grid), the journey (chapters, path lengths, the keyed walk as a speed curve), the palette
            timeline, the routes, and the owner's emotional criteria (the addendum) as the intent's
            questions. Submit with `critic submit --inputs <dir>/inputs.json --wait`.

  temporal  Measures what the brief's section 16 asks the analyzer to catch, from the rendered video and
            the song, per section and per phrase, and writes findings in the brief's own terms:
              * aggression-response: the music becomes more aggressive (bright, dense, loud) and the picture
                does not respond;
              * snapping: the picture jumps between states instead of evolving (isolated change spikes,
                A-B-A flicker), and whether the snaps sit on transients;
              * colour-rate: colour changes faster than the phrase warrants (a major colour move completed
                in less than a bar outside a section boundary, or several per phrase);
              * breakdown-contrast: the visual intensity stays high through a breakdown, so the following
                section has nothing to rise from.
            Everything is MEASURED from pixels and samples (no model), at a reduced resolution.

Dependencies: python3, numpy, ffmpeg (all already used by the repository's tools). Examples:

  python3 tools/liminal_critic.py temporal --video render.mp4 --audio ~/Desktop/"All You Got.wav" \\
      --sections tools/liminal/all-you-got.sections.json --out /tmp/critique
  python3 tools/liminal_critic.py inputs --project examples/liminal/liminal.json \\
      --sections tools/liminal/all-you-got.sections.json --video render.mp4 --out /tmp/critic-inputs
"""

from __future__ import annotations

import argparse
import json
import math
import os
import subprocess
import sys
from typing import Optional

import numpy as np

# ---- the song's grid --------------------------------------------------------------------------------


class Grid:
    """Bars to seconds from a section map's `grid` (bar 1 at downbeat_s; tempo changes at bars)."""

    def __init__(self, grid: dict):
        self.downbeat = float(grid.get("downbeat_s", 0.0))
        self.tempo = sorted((int(b), float(t)) for b, t in grid.get("tempo", [[1, 120.0]]))
        self.beats_per_bar = int(str(grid.get("meter", "4/4")).split("/")[0])
        self.bars = int(grid.get("bars", 0))

    def bar_seconds(self, bar: int) -> float:
        return 60.0 / self.tempo_at(bar) * self.beats_per_bar

    def tempo_at(self, bar: float) -> float:
        t = self.tempo[0][1]
        for b, bpm in self.tempo:
            if bar >= b:
                t = bpm
        return t

    def time(self, bar: float) -> float:
        """Start of (fractional) bar `bar` (1-based)."""
        t = self.downbeat
        b = 1
        while b + 1 <= bar:
            t += self.bar_seconds(b)
            b += 1
        return t + (bar - b) * self.bar_seconds(b)


def load_sections(path: str) -> tuple[Grid, list[dict], dict]:
    doc = json.load(open(os.path.expanduser(path)))
    grid = Grid(doc.get("grid", {}))
    sections = []
    for s in doc.get("sections", []):
        start = grid.time(s["bar0"])
        end = grid.time(s["bar1"] + 1)
        sections.append({**s, "start": start, "end": end})
    return grid, sections, doc


# ---- decoding ---------------------------------------------------------------------------------------

W, H = 160, 90


def video_frames(path: str, fps: float) -> np.ndarray:
    """Frames as float32 [n, H, W, 3] in 0..1, sampled at `fps`, downscaled."""
    cmd = ["ffmpeg", "-v", "error", "-i", os.path.expanduser(path), "-vf", f"fps={fps},scale={W}:{H}:flags=area",
           "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    raw = subprocess.run(cmd, check=True, capture_output=True).stdout
    frames = np.frombuffer(raw, dtype=np.uint8).reshape(-1, H, W, 3)
    return frames.astype(np.float32) / 255.0


def audio_samples(path: str, rate: int = 22050, start: float = 0.0, duration: Optional[float] = None) -> np.ndarray:
    cmd = ["ffmpeg", "-v", "error", "-ss", str(start), "-i", os.path.expanduser(path)]
    if duration is not None:
        cmd += ["-t", str(duration)]
    cmd += ["-ac", "1", "-ar", str(rate), "-f", "f32le", "-"]
    raw = subprocess.run(cmd, check=True, capture_output=True).stdout
    return np.frombuffer(raw, dtype=np.float32)


# ---- features ---------------------------------------------------------------------------------------


def srgb_to_linear(c: np.ndarray) -> np.ndarray:
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def oklab(rgb_linear: np.ndarray) -> np.ndarray:
    m1 = np.array([[0.4122214708, 0.5363325363, 0.0514459929],
                   [0.2119034982, 0.6806995451, 0.1073969566],
                   [0.0883024619, 0.2817188376, 0.6299787005]], dtype=np.float32)
    m2 = np.array([[0.2104542553, 0.7936177850, -0.0040720468],
                   [1.9779984951, -2.4285922050, 0.4505937099],
                   [0.0259040371, 0.7827717662, -0.8086757660]], dtype=np.float32)
    lms = np.cbrt(np.maximum(rgb_linear @ m1.T, 0.0))
    return lms @ m2.T


def visual_features(frames: np.ndarray) -> dict:
    lin = srgb_to_linear(frames)
    lab = oklab(lin.reshape(-1, 3)).reshape(frames.shape)
    mean_lab = lab.mean(axis=(1, 2))                         # the frame's average colour (OKLab)
    chroma = np.sqrt(lab[..., 1] ** 2 + lab[..., 2] ** 2)
    luma = lab[..., 0]
    diff = np.zeros(len(frames), dtype=np.float32)
    diff[1:] = np.abs(luma[1:] - luma[:-1]).mean(axis=(1, 2))  # picture change per frame
    skip = np.zeros(len(frames), dtype=np.float32)
    skip[2:] = np.abs(luma[2:] - luma[:-2]).mean(axis=(1, 2))  # change over two frames (A-B-A detector)
    # Fine detail energy (high-pass of the luma): what vibration, tremble and fine light add.
    hp = luma - (np.roll(luma, 1, 1) + np.roll(luma, -1, 1) + np.roll(luma, 1, 2) + np.roll(luma, -1, 2)) / 4.0
    detail = np.abs(hp).mean(axis=(1, 2))
    return {"lab": mean_lab, "luma": luma.mean(axis=(1, 2)), "chroma": chroma.mean(axis=(1, 2)), "diff": diff,
            "skip": skip, "detail": detail}


def audio_features(x: np.ndarray, rate: int, fps: float, n: int) -> dict:
    hop = rate / fps
    win = int(2 ** math.ceil(math.log2(max(hop * 2, 256))))
    freqs = np.fft.rfftfreq(win, 1.0 / rate)
    hann = np.hanning(win).astype(np.float32)
    rms = np.zeros(n, np.float32)
    high = np.zeros(n, np.float32)
    flat = np.zeros(n, np.float32)
    flux = np.zeros(n, np.float32)
    prev = None
    for i in range(n):
        c = int(i * hop)
        seg = x[max(0, c - win // 2): max(0, c - win // 2) + win]
        if len(seg) < win:
            seg = np.pad(seg, (0, win - len(seg)))
        mag = np.abs(np.fft.rfft(seg * hann)) + 1e-9
        rms[i] = math.sqrt(float(np.mean(seg ** 2)))
        band = mag[freqs > 2000.0]
        high[i] = float(np.sqrt(np.mean(band ** 2)))
        flat[i] = float(np.exp(np.mean(np.log(band))) / np.mean(band))  # noisiness of the highs
        if prev is not None:
            flux[i] = float(np.sum(np.maximum(mag - prev, 0.0))) / win
        prev = mag
    # Aggression: loud, bright and noisy highs together (a distorted synth reads as all three).
    def z(v):
        return (v - np.median(v)) / (np.std(v) + 1e-9)
    aggression = (z(np.log(rms + 1e-6)) + z(np.log(high + 1e-6)) + z(flat)) / 3.0
    onset = flux > (np.median(flux) + 2.5 * np.std(flux))
    return {"rms": rms, "high": high, "flatness": flat, "flux": flux, "aggression": aggression, "onset": onset}


def smooth(v: np.ndarray, n: int) -> np.ndarray:
    if n <= 1:
        return v
    k = np.ones(n, np.float32) / n
    return np.convolve(np.pad(v, (n // 2, n - 1 - n // 2), mode="edge"), k, mode="valid")


# ---- the checks -------------------------------------------------------------------------------------


def check_aggression(t, vis, aud, grid, sections, fps, findings, measures):
    """Windows of four bars: does visual activity follow the music's aggression?"""
    activity = smooth(vis["diff"] * 40.0 + vis["detail"] * 20.0 + vis["chroma"] * 4.0, int(fps))
    agg = smooth(aud["aggression"], int(fps))
    rows = []
    bar = 1
    while grid.time(bar + 4) <= t[-1] + 1e-6:
        a, b = grid.time(bar), grid.time(bar + 4)
        m = (t >= a) & (t < b)
        if m.sum() > 2:
            rows.append((bar, a, b, float(agg[m].mean()), float(activity[m].mean())))
        bar += 4
    if len(rows) < 3:
        return
    A = np.array([r[3] for r in rows])
    V = np.array([r[4] for r in rows])
    zA = (A - A.mean()) / (A.std() + 1e-9)
    zV = (V - V.mean()) / (V.std() + 1e-9)
    corr = float(np.corrcoef(A, V)[0, 1]) if len(rows) > 2 else float("nan")
    measures["aggression_activity_correlation"] = corr
    for i in range(1, len(rows)):
        rise = zA[i] - zA[i - 1]
        vis_rise = zV[i] - zV[i - 1]
        if rise > 0.9 and vis_rise < 0.2 * rise:
            bar, a, b = rows[i][0], rows[i][1], rows[i][2]
            findings.append({
                "rule": "aggression-unanswered", "severity": "major" if rise > 1.5 else "minor",
                "start": a, "end": b, "bars": [bar, bar + 3],
                "text": f"The music becomes substantially more aggressive at bars {bar}-{bar + 3} ({a:.1f}-{b:.1f} s; "
                        f"aggression +{rise:.2f} sd) but the visual system does not respond (activity "
                        f"{vis_rise:+.2f} sd)."})


def check_snapping(t, vis, aud, fps, findings, measures):
    d = vis["diff"]
    med = np.median(d) + 1e-6
    local = np.array([np.median(d[max(0, i - int(fps / 2)): i + int(fps / 2) + 1]) for i in range(len(d))])
    spikes = np.where((d > 6.0 * np.maximum(local, med * 0.5)) & (d > 0.01))[0]
    # A-B-A: the frame two back is closer than the frame one back (the picture flips and flips back).
    flicker = np.where((vis["skip"] < 0.35 * d) & (d > 3.0 * med) & (d > 0.006))[0]
    onset_frames = set(np.where(aud["onset"])[0].tolist())
    on_transient = [int(i) for i in spikes if any((i + k) in onset_frames for k in (-1, 0, 1))]
    measures["snaps"] = int(len(spikes))
    measures["snaps_on_transients"] = int(len(on_transient))
    measures["flicker_frames"] = int(len(flicker))
    measures["change_smoothness"] = float(np.median(np.abs(np.diff(d))) / med)
    groups = _group(spikes, int(fps))
    for g in groups:
        a, b = t[g[0]], t[g[-1]]
        tied = sum(1 for i in g if i in on_transient)
        findings.append({
            "rule": "snapping", "severity": "major" if len(g) >= 3 else "minor", "start": float(a),
            "end": float(b + 1.0 / fps),
            "text": f"The picture jumps between states at {a:.2f} s ({len(g)} isolated change spike(s)"
                    + (f", {tied} on audio transients: the deformation responds to the transient but snaps "
                       f"rather than evolving continuously" if tied else "") + ")."})
    for g in _group(flicker, int(fps)):
        if len(g) >= 2:
            findings.append({"rule": "flicker", "severity": "major", "start": float(t[g[0]]), "end": float(t[g[-1]]),
                             "text": f"The picture flips between two configurations and back ({len(g)} frames around "
                                     f"{t[g[0]]:.2f} s): stuck between states."})


def _group(idx, gap):
    groups = []
    for i in idx:
        if groups and i - groups[-1][-1] <= gap:
            groups[-1].append(int(i))
        else:
            groups.append([int(i)])
    return groups


def check_colour_rate(t, vis, grid, sections, fps, findings, measures):
    lab = vis["lab"]
    # Colour velocity in OKLab per second, smoothed over half a second.
    v = np.zeros(len(lab), np.float32)
    v[1:] = np.linalg.norm(lab[1:] - lab[:-1], axis=1) * fps
    v = smooth(v, max(1, int(fps / 2)))
    bar = grid.bar_seconds(1)
    phrase = 4 * bar
    boundaries = [s["start"] for s in sections]
    # A "move": a run where the colour velocity is above threshold; its size is the OKLab distance covered.
    thr = max(0.02, float(np.percentile(v, 90)) * 0.5)
    moves = []
    i = 0
    while i < len(v):
        if v[i] > thr:
            j = i
            while j < len(v) and v[j] > thr:
                j += 1
            dist = float(np.linalg.norm(lab[min(j, len(lab) - 1)] - lab[i]))
            if dist > 0.05:
                moves.append((t[i], t[min(j, len(t) - 1)], dist))
            i = j
        else:
            i += 1
    measures["colour_moves"] = len(moves)
    measures["colour_velocity_p95"] = float(np.percentile(v, 95))
    for a, b, dist in moves:
        near_boundary = any(abs(a - x) < bar or abs(b - x) < bar for x in boundaries)
        if (b - a) < bar and not near_boundary:
            findings.append({"rule": "colour-too-fast", "severity": "minor", "start": float(a), "end": float(b),
                             "text": f"Colour changes faster than the musical phrase warrants: a move of {dist:.2f} "
                                     f"(OKLab) in {b - a:.2f} s at {a:.1f} s, under one bar ({bar:.2f} s), away from "
                                     f"a section boundary."})
    # More than two moves inside one phrase.
    start = grid.downbeat
    while start < t[-1]:
        inside = [m for m in moves if start <= m[0] < start + phrase]
        if len(inside) > 2:
            findings.append({"rule": "colour-churn", "severity": "minor", "start": float(start),
                             "end": float(start + phrase),
                             "text": f"{len(inside)} colour moves inside one phrase ({start:.1f}-{start + phrase:.1f} "
                                     f"s): colour is changing faster than the phrasing."})
        start += phrase


def check_breakdown(t, vis, sections, findings, measures):
    intensity = vis["diff"] * 40.0 + vis["chroma"] * 4.0 + vis["luma"] + vis["detail"] * 20.0
    rows = []
    for s in sections:
        m = (t >= s["start"]) & (t < s["end"])
        if m.sum() > 0:
            rows.append((s, float(intensity[m].mean()), float(vis["diff"][m].mean())))
    measures["section_intensity"] = {r[0]["id"]: round(r[1], 4) for r in rows}
    for i, (s, level, motion) in enumerate(rows):
        text = (s.get("name", "") + " " + s.get("note", "") + " " + s["id"]).lower()
        if not any(k in text for k in ("breakdown", "let it go", "break", "pause", "silence")):
            continue
        before = rows[i - 1][1] if i > 0 else None
        after = rows[i + 1][1] if i + 1 < len(rows) else None
        ref = max(x for x in (before, after) if x is not None) if (before or after) else None
        if ref is None:
            continue
        drop = (ref - level) / (abs(ref) + 1e-9)
        measures.setdefault("breakdown_drop", {})[s["id"]] = round(drop, 3)
        if drop < 0.15:
            findings.append({"rule": "breakdown-contrast", "severity": "major", "start": s["start"], "end": s["end"],
                             "text": f"The visual intensity remains high through the breakdown '{s['id']}' "
                                     f"({s['start']:.1f}-{s['end']:.1f} s; {100 * drop:.0f}% below its neighbours) and "
                                     f"therefore does not create sufficient contrast for the following section."})


def temporal(args):
    grid, sections, doc = load_sections(args.sections)
    frames = video_frames(args.video, args.fps)
    n = len(frames)
    t = args.video_start + np.arange(n, dtype=np.float64) / args.fps
    vis = visual_features(frames)
    x = audio_samples(args.audio, 22050, start=args.video_start, duration=n / args.fps)
    aud = audio_features(x, 22050, args.fps, n)
    sections = [s for s in sections if s["end"] > t[0] and s["start"] < t[-1]]
    findings: list[dict] = []
    measures: dict = {"frames": n, "fps": args.fps, "start": float(t[0]), "end": float(t[-1])}
    check_aggression(t, vis, aud, grid, sections, args.fps, findings, measures)
    check_snapping(t, vis, aud, args.fps, findings, measures)
    check_colour_rate(t, vis, grid, sections, args.fps, findings, measures)
    check_breakdown(t, vis, sections, findings, measures)
    os.makedirs(args.out, exist_ok=True)
    report = {"tool": "liminal_critic temporal", "epistemic": "MEASURED", "video": args.video, "audio": args.audio,
              "sections": args.sections, "measures": measures, "findings": findings,
              "questions": EMOTIONAL_QUESTIONS}
    with open(os.path.join(args.out, "temporal.json"), "w") as f:
        json.dump(report, f, indent=1, default=float)
    with open(os.path.join(args.out, "temporal.md"), "w") as f:
        f.write(f"# Temporal audiovisual analysis\n\n{args.video}, {measures['start']:.1f}-{measures['end']:.1f} s, "
                f"{n} frames at {args.fps} fps (measured, {W}x{H}).\n\n## Measures\n\n")
        for k, v in measures.items():
            f.write(f"- **{k}**: {v}\n")
        f.write("\n## Findings\n\n" + ("None.\n" if not findings else ""))
        for fd in findings:
            f.write(f"- [{fd['severity']}] `{fd['rule']}` {fd['start']:.1f}-{fd['end']:.1f} s: {fd['text']}\n")
        f.write("\n## Questions for the creative evaluation (the owner's addendum)\n\n")
        for q in EMOTIONAL_QUESTIONS:
            f.write(f"- {q}\n")
    print(json.dumps({"findings": len(findings), **{k: v for k, v in measures.items() if not isinstance(v, dict)}},
                     default=float))


# ---- Creative Critic inputs -------------------------------------------------------------------------

EMOTIONAL_QUESTIONS = [
    "Does this visual sequence communicate the intended emotional state of the music?",
    "Is the procedural geometry helping communicate that emotion, or distracting from it?",
    "Loneliness: does the world feel too large, too empty, and the viewer alone in it?",
    "Wandering: does the camera feel like a searching walker (pauses, hesitations, uncertain turns), not a ride?",
    "Spatial uncertainty: does the place slowly stop obeying ordinary space, legibly rather than randomly?",
    "Emotional progression: quiet, curiosity, energy, intimacy, silence, growth, tension, release -- in order?",
    "Restraint: is the reactivity sparse and motivated; is silence a real absence of activity?",
    "Liminality and continuity: one continuous, strange-but-familiar building, never a generic sci-fi tunnel?",
    "Musical phrasing: do changes land on phrases rather than on every beat?",
    "Is the climax earned by what came before, and does the final 'let it go' feel different from the first?",
]


def _keys(track):
    return [(float(k["time"]), k["value"]) for k in track.get("keys", [])]


def inputs(args):
    project = json.load(open(args.project))
    base = os.path.dirname(os.path.abspath(args.project))
    scene_path = os.path.join(base, project["assets"]["scene"]["path"])
    scene = json.load(open(scene_path))
    grid, sections, doc = load_sections(args.sections)
    audio = os.path.expanduser(project["assets"].get("audio", {}).get("path", ""))
    if audio and not os.path.isabs(audio):
        audio = os.path.normpath(os.path.join(base, audio))
    tracks = {tr["target"]: tr for tr in project.get("timeline", {}).get("tracks", [])}
    journey = scene.get("camera", {}).get("journey", {})
    chapters = journey.get("chapters", [journey] if journey else [])
    # The walk as a speed curve (metres per second between the distance keys).
    walk = []
    if "camera/journey/distance" in tracks:
        ks = _keys(tracks["camera/journey/distance"])
        for (t0, v0), (t1, v1) in zip(ks, ks[1:]):
            walk.append({"start": t0, "end": t1, "metres": v1[0] - v0[0], "speed": (v1[0] - v0[0]) / max(t1 - t0, 1e-6)})
    segments = []
    for s in sections:
        intent = s.get("intent", {})
        seg = {"id": s["id"], "name": s.get("name", s["id"]), "start": round(s["start"], 3), "end": round(s["end"], 3),
               "intent": s.get("note", ""), "energy": float(intent.get("coupling", 0.5)),
               "targets": {}}
        if intent.get("palette"):
            seg["palette"] = intent["palette"]
        if "release" in (s.get("name", "") + s["id"]).lower() or "final" in s.get("name", "").lower():
            seg["role"] = "climax"
        segments.append(seg)
    shots = []
    doc_shots = doc.get("shots", [])
    for i, sh in enumerate(doc_shots):
        end = doc_shots[i + 1]["t"] if i + 1 < len(doc_shots) else (sections[-1]["end"] if sections else sh["t"] + 4)
        if end <= sh["t"]:
            continue
        shots.append({"id": f"s{i + 1:02d}", "start": float(sh["t"]), "end": float(end), "segment": sh.get("section", ""),
                      "label": f"bar {sh.get('bar', '?')}: {sh.get('camera', '')}; {sh.get('event', '')}"[:200]})
    intent = {
        "project": "Liminal Euclidean World -- All You Got",
        "mood": "alone in a beautiful dream that you don't quite understand; loneliness, wandering, uncertainty, "
                "introspection, gradual release, acceptance",
        "visual_language": "a strange dream of an ordinary building: plain rooms, corridors, stairs, landings, "
                           "doorways; large, empty, repetitive, subtly impossible; one continuous walk",
        "avoid": ["generic audio visualizer", "beat-synced flashing", "camera shake", "rainbow colour cycling",
                  "excessive bloom", "generic sci-fi corridors", "horror", "every beat causing a visible event",
                  "geometry that merely jitters", "spectacle that does not serve the emotion"],
        "segments": segments,
        "questions": EMOTIONAL_QUESTIONS,
    }
    scene_desc = {
        "meta": {"name": scene.get("name"), "kind": "sdf-journey", "project": os.path.abspath(args.project)},
        "cameras": [{"id": "journey", "kind": "journey", "fov": scene.get("camera", {}).get("fov"),
                     "chapters": [{"name": c.get("name", ""), "start": c.get("start", 0.0), "from": c.get("from", 0.0),
                                   "screw": c.get("screw"), "points": len(c.get("path", []))} for c in chapters],
                     "walk": walk}],
        "timeline": {"sections": [{"id": s["id"], "start": s["start"], "end": s["end"], "name": s.get("name", "")}
                                  for s in sections]},
        "modulation": {"routes": project.get("routes", [])},
        "extensions": {
            "palette": {"states": [st.get("name") for st in project.get("palette", {}).get("states", [])],
                        "timeline": _keys(tracks["palette/position"]) if "palette/position" in tracks else [],
                        "art_timeline": doc.get("palette_timeline", [])},
            "sdf": [{"name": n["name"], "compile": n["sdf"].get("compile"), "surfaces": len(n["sdf"].get("surfaces", []))}
                    for n in scene.get("nodes", []) if n.get("kind") == "sdf"],
            "synch_points": doc.get("synch_points", []),
            "vocals": doc.get("vocals", []),
        },
    }
    if args.video:
        # Clip the song's sections and shots to the span the video covers (a test render is an excerpt).
        dur = float(subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0",
                                    os.path.expanduser(args.video)], check=True, capture_output=True,
                                   text=True).stdout.strip())
        a, b = args.video_start, args.video_start + dur

        def clip(items):
            out = []
            for it in items:
                if it["end"] > a and it["start"] < b:
                    out.append({**it, "start": max(it["start"], a), "end": min(it["end"], b)})
            return out
        intent["segments"] = clip(intent["segments"])
        shots[:] = clip(shots)
        scene_desc["timeline"]["sections"] = clip(scene_desc["timeline"]["sections"])
    os.makedirs(args.out, exist_ok=True)
    for name, obj in (("intent.json", intent), ("shots.json", shots), ("scene.json", scene_desc)):
        with open(os.path.join(args.out, name), "w") as f:
            json.dump(obj, f, indent=1, default=float)
    inp = {"scene": os.path.join(os.path.abspath(args.out), "scene.json"),
           "intent": os.path.join(os.path.abspath(args.out), "intent.json"),
           "shots": os.path.join(os.path.abspath(args.out), "shots.json")}
    if args.video:
        inp["video"] = {"path": os.path.abspath(os.path.expanduser(args.video)), "start": args.video_start}
    if audio:
        inp["audio"] = {"path": audio, "start": args.video_start}
    with open(os.path.join(args.out, "inputs.json"), "w") as f:
        json.dump(inp, f, indent=1)
    print(f"wrote {args.out}: {len(segments)} segments, {len(shots)} shots, {len(chapters)} chapter(s), "
          f"{len(walk)} walk spans")


def selftest(args):
    """Each check fires on the defect it names and stays quiet on its absence (synthetic signals)."""
    fps = 15.0
    grid = Grid({"downbeat_s": 0.0, "tempo": [[1, 120.0]], "bars": 40, "meter": "4/4"})  # 2 s bars
    n = int(80 * fps)
    t = np.arange(n) / fps
    def vis_base():
        return {"lab": np.tile(np.array([0.6, 0.0, 0.0], np.float32), (n, 1)), "luma": np.full(n, 0.5, np.float32),
                "chroma": np.full(n, 0.05, np.float32), "diff": np.full(n, 0.004, np.float32) +
                0.001 * np.sin(t).astype(np.float32), "skip": np.full(n, 0.008, np.float32),
                "detail": np.full(n, 0.01, np.float32)}
    aud = {"aggression": np.zeros(n, np.float32), "onset": np.zeros(n, bool)}
    sections = [{"id": "A", "name": "verse", "start": 0.0, "end": 24.0}, {"id": "B", "name": "let it go",
                "start": 24.0, "end": 48.0}, {"id": "C", "name": "rebuild", "start": 48.0, "end": 80.0}]
    ok = True

    def expect(name, findings, rule, want):
        nonlocal ok
        got = any(f["rule"] == rule for f in findings)
        print(f"{'ok  ' if got == want else 'FAIL'} {name}: {rule} {'fires' if got else 'quiet'}")
        ok = ok and got == want

    v = vis_base(); f = []; m = {}
    check_snapping(t, v, aud, fps, f, m); expect("smooth motion", f, "snapping", False)
    v = vis_base(); v["diff"][[300, 301, 600]] = 0.2; f = []
    check_snapping(t, v, aud, fps, f, m); expect("isolated jumps", f, "snapping", True)
    v = vis_base(); v["diff"][300:320] = 0.05; v["skip"][300:320] = 0.002; f = []
    check_snapping(t, v, aud, fps, f, m); expect("A-B-A flicker", f, "flicker", True)
    v = vis_base(); v["lab"][150:] += np.array([0.0, 0.2, 0.0], np.float32); f = []
    v["lab"][145:150] += np.linspace(0, 0.2, 5, dtype=np.float32)[:, None] * np.array([0, 1, 0], np.float32)
    check_colour_rate(t, v, grid, sections, fps, f, m); expect("a colour jump mid-phrase", f, "colour-too-fast", True)
    v = vis_base(); v["lab"][:, 1] += np.linspace(0, 0.2, n, dtype=np.float32); f = []
    check_colour_rate(t, v, grid, sections, fps, f, m); expect("a slow colour drift", f, "colour-too-fast", False)
    v = vis_base(); f = []
    check_breakdown(t, v, sections, f, m); expect("a flat breakdown", f, "breakdown-contrast", True)
    v = vis_base(); v["diff"][int(24 * fps):int(48 * fps)] = 0.0005; v["luma"][int(24 * fps):int(48 * fps)] = 0.2; f = []
    check_breakdown(t, v, sections, f, m); expect("a quiet breakdown", f, "breakdown-contrast", False)
    a2 = dict(aud); a2["aggression"] = np.where(t > 40, 2.0, 0.0).astype(np.float32); v = vis_base(); f = []
    check_aggression(t, v, a2, grid, sections, fps, f, m); expect("aggression, no answer", f, "aggression-unanswered", True)
    v = vis_base(); v["diff"] = np.where(t > 40, 0.03, 0.004).astype(np.float32); f = []
    check_aggression(t, v, a2, grid, sections, fps, f, m); expect("aggression answered", f, "aggression-unanswered", False)
    print("selftest", "passed" if ok else "FAILED")
    return 0 if ok else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    tp = sub.add_parser("temporal")
    tp.add_argument("--video", required=True)
    tp.add_argument("--audio", required=True)
    tp.add_argument("--sections", required=True)
    tp.add_argument("--video-start", type=float, default=0.0, help="song time of the video's first frame")
    tp.add_argument("--fps", type=float, default=15.0, help="analysis frame rate")
    tp.add_argument("--out", required=True)
    ip = sub.add_parser("inputs")
    ip.add_argument("--project", required=True)
    ip.add_argument("--sections", required=True)
    ip.add_argument("--video")
    ip.add_argument("--video-start", type=float, default=0.0)
    ip.add_argument("--out", required=True)
    sub.add_parser("selftest")
    args = ap.parse_args(argv)
    if args.cmd == "selftest":
        return selftest(args)
    (temporal if args.cmd == "temporal" else inputs)(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
