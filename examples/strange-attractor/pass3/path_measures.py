"""Pass 3 (61- §3): the camera-path measures per leg, and a top view of each leg's path in its own world.

  python3 path_measures.py <trace.json.out> <out_dir>
The trace is the composition's camera at 30 fps from 0 ([t, eye xyz, target xyz, up xyz] rows, the root's space).
Portal worlds (Moon, Mars, ocean) are drawn in their own coordinates: the trace is fitted to the leg's flight knots
(a 2D rotation and translation, least squares over the knots' times), so the characters' positions line up.
Per leg: path length / straight A->B, heading reversals, look yaw and pitch rate (max, p95, deg/s), speed (min, max,
max/min outside the first and last second), forward vs velocity (max, p95)."""
import json, math, os, sys
import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
E = os.path.dirname(HERE)
X = [32.025, 47.87, 69.655, 93.46, 121.217, 147.017]
LEGS = [("canyon", 0.0, X[0], "root"), ("moon", X[0], X[1], "moon"), ("dusk", X[1], X[2], "root"),
        ("mars", X[2], X[3], "mars"), ("night", X[3], X[4], "root"), ("ocean", X[4], X[5], "ocean"),
        ("dawn", X[5], 211.0, "root")]


def load(p):
    return json.load(open(os.path.join(E, p)))


def fit(trace, knots):
    """2D rigid fit: trace xz at the knots' times -> the knots' xz."""
    ts = trace[:, 0]
    src, dst = [], []
    for k in knots:
        t = k[3]
        if ts[0] <= t <= ts[-1]:
            i = int(np.argmin(abs(ts - t)))
            src.append(trace[i, [1, 3]]); dst.append([k[0], k[2]])
    src, dst = np.array(src), np.array(dst)
    ms, md = src.mean(0), dst.mean(0)
    H = (src - ms).T @ (dst - md)
    U, _, Vt = np.linalg.svd(H)
    R = (U @ Vt).T
    if np.linalg.det(R) < 0:
        Vt[-1] *= -1; R = (U @ Vt).T
    return lambda p: (R @ (np.asarray(p) - ms).T).T + md, R


def measures(seg):
    t = seg[:, 0]; eye = seg[:, 1:4]; tgt = seg[:, 4:7]
    dt = np.gradient(t)
    vel = np.gradient(eye, axis=0) / dt[:, None]
    speed = np.linalg.norm(vel, axis=1)
    fwd = tgt - eye; fwd /= np.linalg.norm(fwd, axis=1)[:, None]
    # yaw and pitch in the camera's own world frame: the up vector of the trace (a portal world's ground need not be the
    # root's y, so the root's y-axis would make up pitch and yaw that are not there)
    up = seg[:, 7:10] / np.linalg.norm(seg[:, 7:10], axis=1)[:, None]
    upm = up.mean(0); upm /= np.linalg.norm(upm)
    ref = np.cross(upm, [1.0, 0.0, 0.0]) if abs(upm[0]) < 0.9 else np.cross(upm, [0.0, 0.0, 1.0])
    ref /= np.linalg.norm(ref); ref2 = np.cross(upm, ref)
    yaw = np.degrees(np.unwrap(np.arctan2(fwd @ ref2, fwd @ ref)))
    pitch = np.degrees(np.arcsin(np.clip(fwd @ upm, -1, 1)))
    yr = abs(np.gradient(yaw) / dt); pr = abs(np.gradient(pitch) / dt)
    length = float(np.sum(np.linalg.norm(np.diff(eye, axis=0), axis=1)))
    straight = float(np.linalg.norm(eye[-1] - eye[0]))
    # heading reversals: the sign of the horizontal path's turn rate, smoothed over 1 s, ignoring turns under 3 deg/s
    head = np.unwrap(np.arctan2(vel @ ref2, vel @ ref))
    k = 30
    turn = np.degrees(np.convolve(np.gradient(head) / dt, np.ones(k) / k, mode="same"))
    signs = [np.sign(v) for v in turn[k:-k] if abs(v) > 3.0]
    reversals = int(sum(1 for a, b in zip(signs, signs[1:]) if a != b))
    inner = (t > t[0] + 1.0) & (t < t[-1] - 1.0)
    vn = vel / np.maximum(speed[:, None], 1e-6)
    fv = np.degrees(np.arccos(np.clip(np.sum(fwd * vn, axis=1), -1, 1)))
    moving = speed > 0.3
    sp = speed[inner]
    return {"length_m": round(length, 1), "straight_m": round(straight, 1),
            "ratio": round(length / max(straight, 1e-6), 2), "reversals": reversals,
            "yaw_rate_max": round(float(yr.max()), 1), "yaw_rate_p95": round(float(np.percentile(yr, 95)), 1),
            "pitch_rate_max": round(float(pr.max()), 1), "pitch_rate_p95": round(float(np.percentile(pr, 95)), 1),
            "speed_min": round(float(sp.min()), 2), "speed_max": round(float(sp.max()), 2),
            "speed_ratio": round(float(sp.max() / max(sp.min(), 1e-3)), 1),
            "fwd_vel_max": round(float(fv[moving].max()), 1), "fwd_vel_p95": round(float(np.percentile(fv[moving], 95)), 1)}


def characters(world):
    out = []
    if world == "root":
        s = load("strange-attractor-flythrough.scene.json")
        for n in s["nodes"]:
            nm = n["name"]
            if any(k in nm for k in ("sasquatch", "alien", "gray", "ufo", "campfire", "fire-")) and "rim" not in nm and "position" in n:
                out.append((nm, n["position"][0], n["position"][2]))
    elif world == "mars":
        p = load("strange-attractor.json")
        for tr in p["timeline"]["tracks"]:
            if tr["target"] == "mars/nodes/rover/position":
                for k in tr["keys"][:: max(1, len(tr["keys"]) // 6)]:
                    v = k["value"]; out.append(("rover@%.0f" % k["time"], v[0], v[2]))
        s = load("strange-attractor-mars.scene.json")
        for n in s["nodes"]:
            if n["name"].startswith("pyramid-") and "sand" not in n["name"]:
                out.append((n["name"], n["position"][0], n["position"][2]))
    elif world == "ocean":
        s = load("strange-attractor-ocean.scene.json")
        for n in s["nodes"]:
            if (n["name"] in ("ocean-base",) or n["name"].startswith("octo-")) and "position" in n:
                out.append((n["name"], n["position"][0], n["position"][2]))
        p = load("strange-attractor.json")
        for tr in p["timeline"]["tracks"]:
            if tr["target"].startswith("ocean/nodes/sub-") and tr["target"].endswith("/position"):
                v = tr["keys"][len(tr["keys"]) // 2]["value"]; out.append((tr["target"].split("/")[2], v[0], v[2]))
            if tr["target"].startswith("ocean/nodes/octo") and tr["target"].endswith("/position"):
                v = tr["keys"][len(tr["keys"]) // 2]["value"]; out.append((tr["target"].split("/")[2], v[0], v[2]))
    elif world == "moon":
        s = load("strange-attractor-moon.scene.json")
        for n in s["nodes"]:
            if n["name"].startswith("dome") and "rave" not in n["name"] and len(n["name"]) <= 6:
                out.append((n["name"], n["position"][0], n["position"][2]))
    return out


def knots_for(world):
    s = load("strange-attractor-flythrough.scene.json")
    if world == "moon":
        return s["portal"]["flight"]
    for p in s["portals"]:
        if world in p["world"]:
            return p["flight"]
    return None


def proposal(name, pts):
    """The proposed line: straight A->B, or for the canyon (whose gorge bends) the current path smoothed to one curve
    (a 7 s moving average, the ends pinned)."""
    if name != "canyon":
        return np.array([pts[0], pts[-1]])
    k = 210
    pad = np.vstack([np.repeat(pts[:1], k, 0), pts, np.repeat(pts[-1:], k, 0)])
    sm = np.array([np.convolve(pad[:, i], np.ones(k) / k, mode="same") for i in (0, 1)]).T[k:-k]
    sm[0], sm[-1] = pts[0], pts[-1]
    return sm


def draw(name, xy, chars, out):
    pts = np.array(xy)
    prop = proposal(name, pts)
    allp = np.vstack([pts] + ([np.array([[c[1], c[2]] for c in chars])] if chars else []))
    lo, hi = allp.min(0), allp.max(0)
    pad = 0.08 * max(hi - lo) + 5
    lo -= pad; hi += pad
    W = 900; sc = W / max(hi - lo); H = int((hi[1] - lo[1]) * sc) + 40
    im = Image.new("RGB", (W, H), (24, 26, 30)); d = ImageDraw.Draw(im)
    P = lambda p: ((p[0] - lo[0]) * sc, 30 + (p[1] - lo[1]) * sc)
    d.line([P(p) for p in pts], fill=(255, 90, 80), width=3)
    d.line([P(p) for p in prop], fill=(90, 220, 120), width=3)
    for i in range(0, len(pts), 60):
        d.text(P(pts[i]), "%ds" % (i // 30), fill=(255, 170, 160))
    for nm, x, z in chars:
        a = P((x, z)); d.ellipse([a[0] - 4, a[1] - 4, a[0] + 4, a[1] + 4], outline=(255, 210, 60), width=2)
        d.text((a[0] + 6, a[1] - 6), nm, fill=(255, 210, 60))
    for lab, p in (("A", pts[0]), ("B", pts[-1])):
        a = P(p); d.ellipse([a[0] - 7, a[1] - 7, a[0] + 7, a[1] + 7], outline=(120, 200, 255), width=3)
        d.text((a[0] + 9, a[1] + 2), lab + " (portal)" if lab == "B" or name != "canyon" else lab, fill=(120, 200, 255))
    d.text((8, 8), f"{name}: current path (red), proposed line (green); characters (yellow); A arrival, B next portal",
           fill=(230, 230, 230))
    im.save(out)


def main():
    trace = np.array(json.load(open(sys.argv[1])))
    out = sys.argv[2]; os.makedirs(out, exist_ok=True)
    rows = {}
    for name, a, b, world in LEGS:
        seg = trace[(trace[:, 0] >= a + 0.05) & (trace[:, 0] <= b - 0.05)]
        rows[name] = measures(seg)
        xy = seg[:, [1, 3]]
        if world != "root":
            k = knots_for(world)
            f, _ = fit(seg, k)
            xy = f(xy)
        draw(name, xy, characters(world), os.path.join(out, f"path-{name}.png"))
    json.dump(rows, open(os.path.join(out, "path-measures.json"), "w"), indent=1)
    hdr = ["ratio", "reversals", "yaw_rate_max", "yaw_rate_p95", "pitch_rate_max", "pitch_rate_p95", "speed_min",
           "speed_max", "speed_ratio", "fwd_vel_max", "fwd_vel_p95"]
    print("leg     " + " ".join(f"{h:>14s}" for h in hdr))
    for n, r in rows.items():
        print(f"{n:7s} " + " ".join(f"{r[h]:>14}" for h in hdr))


if __name__ == "__main__":
    main()
