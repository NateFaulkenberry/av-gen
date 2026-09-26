"""Reconstruct GV3's follow/aim camera rigs from the cast trace, the way the engine evaluates them.

Engine reference: src/scene/composition.cpp Composition::evaluateAuthoredCamera (7451-7528):
  eye   = P(t - lag) + followOffset(t)              (world axes; followLocal unused in GV3)
  eye.y = max(eye.y, surfaceAt(eye.xz) + clearance) (when clearance > 0)
  aim   = P(t) + aimOffset                          (undamped)
P is nodeWorldTransform = position param final = base + travel + motion (entity.cpp:2218-2225),
which is what cast_trace records as loco.position = state.position() + motion.position
(entity.cpp:2701, tools/cast_trace.cpp:199).
Keyed channels: clamped Catmull-Rom exactly as src/params/timeline.cpp:74-117.
"""
import json
import math
import pathlib

import numpy as np

WT = pathlib.Path("/Users/natefaulkenberry/Documents/GitHub/av-gen-gv3")
OUT = pathlib.Path(__file__).resolve().parent

CAST = json.loads((WT / "build/gv3/cast-v2.json").read_text())
SCENE = json.loads((WT / "examples/world/glowmere-valley-3.scene.json").read_text())
PROJECT = json.loads((WT / "examples/world/glowmere-valley-3.json").read_text())


def catmull_slope(keys, i, c):
    prev = i - 1 if i > 0 else i
    nxt = i + 1 if i + 1 < len(keys) else i
    dt = keys[nxt]["time"] - keys[prev]["time"]
    if dt <= 0:
        return 0.0
    return (keys[nxt]["value"][c] - keys[prev]["value"][c]) / dt


def hermite(p0, m0, p1, m1, u):
    u2, u3 = u * u, u * u * u
    return (2 * u3 - 3 * u2 + 1) * p0 + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * p1 + (u3 - u2) * m1


def eval_track(keys, t, comps=3):
    if t <= keys[0]["time"]:
        return list(keys[0]["value"][:comps])
    if t >= keys[-1]["time"]:
        return list(keys[-1]["value"][:comps])
    for i in range(len(keys) - 1):
        a, b = keys[i], keys[i + 1]
        if a["time"] <= t < b["time"]:
            span = b["time"] - a["time"]
            u = (t - a["time"]) / span
            out = []
            for c in range(comps):
                v0, v1 = a["value"][c], b["value"][c]
                it = a["interp"]
                if it == "step":
                    out.append(v0)
                elif it == "linear":
                    out.append(v0 + (v1 - v0) * u)
                elif it == "easeInOut":
                    out.append(v0 + (v1 - v0) * u * u * (3 - 2 * u))
                elif it == "smooth":
                    m0 = catmull_slope(keys, i, c) * span
                    m1 = catmull_slope(keys, i + 1, c) * span
                    raw = hermite(v0, m0, v1, m1, u)
                    out.append(min(max(raw, min(v0, v1)), max(v0, v1)))
                else:
                    raise ValueError(it)
            return out
    return list(keys[-1]["value"][:comps])


def rigs():
    cd = SCENE["cameraDirection"]
    tracks = {t["target"]: t for t in PROJECT["timeline"]["tracks"]}
    by_id = {c["id"]: c for c in cd["cameras"]}
    out = []
    for sh in cd["shots"]:
        c = by_id[sh["camera"]]
        if "followNode" not in c and "aimNode" not in c:
            continue
        r = dict(slug=c["slug"], name=c["name"], start=sh["start"], end=sh["end"],
                 focal=c.get("focalLength", 0.0), fov=c.get("fov"),
                 follow=c.get("followNode"), off=c.get("followOffset", [0, 0, 0]),
                 lag=c.get("followLagSeconds", 0.0), clr=c.get("followClearance", 0.0),
                 aim=c.get("aimNode"), aimoff=c.get("aimOffset", [0, 0, 0]),
                 position=c["position"], target=c["target"],
                 offkeys=tracks.get(f"cameras/{c['slug']}/followOffset", {}).get("keys"),
                 poskeys=tracks.get(f"cameras/{c['slug']}/position", {}).get("keys"),
                 tgtkeys=tracks.get(f"cameras/{c['slug']}/target", {}).get("keys"))
        out.append(r)
    return out


STATIC = {"elder-2-cap": [-12.0, 3.4356, 52.0]}


def track_of(name):
    if name in STATIC:
        ts = np.array([0.0, 1000.0])
        P = np.array([STATIC[name], STATIC[name]], dtype=float)
        return ts, P, np.zeros(2), ["static", "static"]
    e = CAST["entities"][name]
    return np.array(e["t"]), np.array(e["position"], dtype=float), np.array(e["speed"], dtype=float), e["activity"]


def interp_vec(ts, P, t):
    return np.array([np.interp(t, ts, P[:, k]) for k in range(3)]).T


def reconstruct(r, hz=20.0):
    """Eye (pre-clearance), aim, and time grid for one rig at `hz`."""
    t = np.arange(math.ceil(r["start"] * hz - 1e-9), math.ceil(r["end"] * hz - 1e-9)) / hz
    if r["follow"]:
        ts, P, _, _ = track_of(r["follow"])
        lagged = interp_vec(ts, P, t - r["lag"])
        if r["offkeys"]:
            off = np.array([eval_track(r["offkeys"], x) for x in t])
        else:
            off = np.tile(np.array(r["off"], dtype=float), (len(t), 1))
        eye0 = lagged + off
    else:
        if r["poskeys"]:
            eye0 = np.array([eval_track(r["poskeys"], x) for x in t])
        else:
            eye0 = np.tile(np.array(r["position"], dtype=float), (len(t), 1))
    if r["aim"]:
        ts, P, _, _ = track_of(r["aim"])
        aim = interp_vec(ts, P, t) + np.array(r["aimoff"], dtype=float)
    else:
        if r["tgtkeys"]:
            aim = np.array([eval_track(r["tgtkeys"], x) for x in t])
        else:
            aim = np.tile(np.array(r["target"], dtype=float), (len(t), 1))
    return t, eye0, aim


if __name__ == "__main__":
    pts = set()
    summary = []
    for r in rigs():
        for hz in (20.0, 60.0):
            t, eye0, aim = reconstruct(r, hz)
            if r["clr"] > 0:
                for x, z in zip(eye0[:, 0], eye0[:, 2]):
                    pts.add((round(float(x), 3), round(float(z), 3)))
        summary.append((r["slug"], r["follow"], r["aim"], r["lag"], r["clr"], round(r["start"], 2), round(r["end"], 2)))
    (OUT / "probe_points.json").write_text(json.dumps(sorted(pts)))
    for s in summary:
        print(s)
    print("probe points:", len(pts))
