#!/usr/bin/env python3
"""Where each shot's subject sits in frame, from the rigs and the cast trace -- no GPU.

    python3 tools/gv3/framing.py build/gv3/cast-v1.json [--out build/gv3/framing.md]

For every shot whose subject is a traced body (an alien, an animal, the saucer) or a hero, the eye
and the aim are rebuilt the way the engine builds them (composition.cpp: a follow rig's eye is its
node plus `followOffset`; an aim rig looks at its node plus `aimOffset`; otherwise the keyed
position and target), the subject is projected through the rig's lens, and the shot is sampled
every tenth of a second. It reports how much of the shot the subject is in frame, how tall it
stands, and where. The Director's still critique (ADR-769) asks the same questions of one frame per
shot; this asks them of all of them.

The projection cannot see occlusion: a subject behind a fern is "in frame" to it. With `--video`,
each in-frame sample of a pale subject (an alien, a horse) is checked against the rendered frame:
is there anything pale where the subject should be? A subject that is in frame and not seen is
behind something. Iteration 1 lost Sage behind a tree fern and Vane behind a leaf that way, and three
frames a shot on the contact sheets caught one of them by luck.
"""
import io
import json
import math
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from gv3 import shots as film  # noqa: E402
from gv3.ground import Ground  # noqa: E402

HEIGHTS = {"rook": 3.3, "tide": 3.3, "sage": 3.3, "ember": 3.3, "vane": 3.3, "visitor": 7.1}
PALE = {"rook", "tide", "sage", "ember", "vane", "horse-11"}   # white bodies: seen = pale pixels there
SEEN_STEP = 0.5          # seconds between visibility samples
ASPECT = 16.0 / 9.0


def _ease(u, interp):
    if interp == "easeInOut":
        return u * u * (3 - 2 * u)
    if interp == "easeIn":
        return u * u
    if interp == "easeOut":
        return 1 - (1 - u) * (1 - u)
    if interp == "step":
        return 0.0
    return u


def keyed(rig, channel, t, default):
    keys = sorted(rig.keys.get(channel, []), key=lambda k: k.time)
    if not keys:
        return default
    if t <= keys[0].time:
        return keys[0].value
    for a, b in zip(keys, keys[1:]):
        if a.time <= t <= b.time:
            u = _ease((t - a.time) / max(b.time - a.time, 1e-9), a.interp)
            return [x + (y - x) * u for x, y in zip(a.value, b.value)]
    return keys[-1].value


class Trace:
    def __init__(self, path, scene):
        d = json.loads(pathlib.Path(path).read_text())
        self.e = d["entities"]
        self.static = {n["name"]: n.get("position") for n in scene["nodes"] if n.get("position")}

    def at(self, name, t):
        if name in self.e:
            tr = self.e[name]
            ts = tr["t"]
            # nearest sample (the trace is dense enough for framing)
            lo, hi = 0, len(ts) - 1
            while hi - lo > 1:
                mid = (lo + hi) // 2
                if ts[mid] <= t:
                    lo = mid
                else:
                    hi = mid
            i = lo if abs(ts[lo] - t) <= abs(ts[hi] - t) else hi
            return tr["position"][i], tr["visible"][i]
        if name in self.static:
            return self.static[name], True
        return None, False


def project(eye, aim, focal, point):
    fx = [aim[i] - eye[i] for i in range(3)]
    n = math.sqrt(sum(c * c for c in fx)) or 1.0
    f = [c / n for c in fx]
    up = [0.0, 1.0, 0.0]
    r = [f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]]
    rn = math.sqrt(sum(c * c for c in r)) or 1.0
    r = [c / rn for c in r]
    u = [r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]]
    d = [point[i] - eye[i] for i in range(3)]
    z = sum(d[i] * f[i] for i in range(3))
    if z <= 0.1:
        return None, z
    tan_v = 12.0 / focal  # half the 24 mm sensor height over the focal length
    x = sum(d[i] * r[i] for i in range(3)) / (z * tan_v * ASPECT)
    y = sum(d[i] * u[i] for i in range(3)) / (z * tan_v)
    return (x, y), z


def _frame(video, t, size=(480, 270)):
    import subprocess
    import numpy as np
    from PIL import Image
    raw = subprocess.run(["ffmpeg", "-v", "error", "-ss", f"{t:.4f}", "-i", str(video), "-frames:v", "1",
                          "-s", f"{size[0]}x{size[1]}", "-f", "image2pipe", "-vcodec", "png", "-"],
                         capture_output=True, check=True).stdout
    return np.asarray(Image.open(io.BytesIO(raw)).convert("RGB")).astype("float32") / 255.0


def seen(img, p, height):
    """Is the subject's body there where it projects? `p` is (x, y) in [-1, 1], `height` its fraction
    of the frame height. The aliens and the horses are white, so under the night light they are the
    brightest thing near them: the brightest tenth of the patch stands well above the frame's median.
    (Not "unsaturated": in the valley's cyan light a white body measures 0.6 saturation.) A fern in
    front of the body is dark, and fails."""
    import numpy as np
    h, w = img.shape[:2]
    cx, cy = (p[0] + 1.0) * 0.5 * w, (1.0 - p[1]) * 0.5 * h
    half = max(4, int(height * h * 0.35))
    x0, x1 = max(0, int(cx - half * 0.6)), min(w, int(cx + half * 0.6) + 1)
    y0, y1 = max(0, int(cy - half)), min(h, int(cy + half) + 1)
    if x1 <= x0 or y1 <= y0:
        return False
    patch = img[y0:y1, x0:x1]
    lum = 0.2126 * patch[..., 0] + 0.7152 * patch[..., 1] + 0.0722 * patch[..., 2]
    median = float(np.median(0.2126 * img[..., 0] + 0.7152 * img[..., 1] + 0.0722 * img[..., 2]))
    return float(np.percentile(lum, 90)) > max(0.25, 2.2 * median)


def main():
    trace_path = sys.argv[1]
    out = pathlib.Path(sys.argv[sys.argv.index("--out") + 1]) if "--out" in sys.argv else None
    video = pathlib.Path(sys.argv[sys.argv.index("--video") + 1]) if "--video" in sys.argv else None
    scene = json.loads((HERE.parent.parent / "examples" / "world" / "glowmere-valley-3.scene.json").read_text())
    world = next(n["world"] for n in scene["nodes"] if "world" in n)
    tr = Trace(trace_path, scene)
    shots = film.build(Ground(world))
    lines = ["| Shot | Subject | in frame | seen | height (frac of frame) | where (x, y) | flags |",
             "|---|---|---|---|---|---|---|"]
    for s in shots:
        rig = s.rig
        subject = rig.aim or rig.follow
        if not subject:
            continue
        samples, inside, heights, where = 0, 0, [], []
        looks, hits = 0, 0
        next_look = s.start + 0.1
        t = s.start + 0.05
        while t < s.end:
            samples += 1
            node, vis = tr.at(subject, t - rig.lag if rig.follow == subject else t)
            if rig.follow:
                base, _ = tr.at(rig.follow, t - rig.lag)
                off = keyed(rig, "followOffset", t, rig.follow_offset)
                eye = [base[i] + off[i] for i in range(3)] if base else keyed(rig, "position", t, rig.position)
            else:
                eye = keyed(rig, "position", t, rig.position)
            if rig.aim:
                a, _ = tr.at(rig.aim, t)
                aim = [a[i] + rig.aim_offset[i] for i in range(3)] if a else keyed(rig, "target", t, rig.target)
            else:
                aim = keyed(rig, "target", t, rig.target)
            focal = keyed(rig, "focalLength", t, [rig.focal])[0]
            if node is not None and vis:
                h = HEIGHTS.get(subject, 2.0)
                mid = [node[0], node[1] + 0.5 * h, node[2]]
                p, z = project(eye, aim, focal, mid)
                if p and abs(p[0]) <= 0.95 and abs(p[1]) <= 0.95:
                    inside += 1
                    heights.append(h / (2 * z * 12.0 / focal))
                    where.append(p)
                    if video and subject in PALE and t >= next_look and t < s.end - 0.05:
                        looks += 1
                        hits += 1 if seen(_frame(video, t), p, heights[-1]) else 0
                        next_look = t + SEEN_STEP
            t += 0.1
        frac = inside / max(samples, 1)
        flags = []
        if frac < 0.9:
            flags.append(f"OUT OF FRAME {100 * (1 - frac):.0f}% of the shot")
        seen_s = f"{100 * hits / looks:.0f}% of {looks}" if looks else "-"
        if looks and hits / looks < 0.9:
            flags.append(f"HIDDEN {100 * (1 - hits / looks):.0f}% of the time")
        if heights and min(heights) < 0.05:
            flags.append("tiny")
        if heights and max(heights) > 0.9:
            flags.append("fills the frame")
        hs = f"{min(heights):.2f}-{max(heights):.2f}" if heights else "-"
        ws = f"({sum(p[0] for p in where) / len(where):+.2f}, {sum(p[1] for p in where) / len(where):+.2f})" if where else "-"
        lines.append(f"| {s.sid} | {subject} | {100 * frac:.0f}% | {seen_s} | {hs} | {ws} | {', '.join(flags)} |")
    text = "\n".join(lines) + "\n"
    if out:
        out.write_text(text)
    print(text)


if __name__ == "__main__":
    main()
