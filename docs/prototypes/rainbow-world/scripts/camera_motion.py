#!/usr/bin/env python3
"""Rainbow World: the ride camera's motion, frame by frame, as AV Gen places it.

The camera is the spline camera (composition.cpp `evaluateMainCamera`): its eye at `splineT`, its aim at the point
`camera/lookAhead` metres further along the same spline, its up the spline's normal (`camera/splineUp`, ADR-1541).
`splineT` is the project's `ride` timeline, linear between keys. knot_path.Path is AV Gen's spline in Python, so the
poses here are the engine's poses.

Per frame it reports the camera's angular velocity split into yaw (about the camera's up), pitch (about its right)
and roll (about its line of sight), the angular jerk (the second difference of that velocity), and how far the look
direction and the up vector turn. It prints peaks, p95 and the twitchiest windows, and can plot the lap.

    python3 docs/prototypes/rainbow-world/scripts/camera_motion.py examples/rainbow-world/rainbow-world-feline.json \
        [--fps 30] [--plot out.png] [--json out.json]
"""
import argparse
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "..", "examples", "rainbow-world", "blender"))
import knot_path  # noqa: E402


def load(project_path):
    proj = json.load(open(project_path))
    base = os.path.dirname(os.path.abspath(project_path))
    scene_rel = proj["assets"]["scene"]["path"]
    scene = json.load(open(scene_rel if os.path.isabs(scene_rel) else os.path.join(base, scene_rel)))
    node = [n for n in scene["nodes"] if n.get("kind") == "spline" and n["name"] == scene["camera"]["spline"]][0]
    sp = node["spline"]
    # AV Gen Y-up back to the Z-up frame the Path was built in: (x, y, z) -> (x, -z, y)
    pts = np.array([[p["position"][0], -p["position"][2], p["position"][1]] for p in sp["points"]])
    rolls = np.array([p.get("roll", 0.0) for p in sp["points"]])
    up = sp["up"]
    path = knot_path.Path(pts, rolls, (up[0], -up[2], up[1]), per_segment=sp.get("samplesPerSegment", 24),
                          roll_turns=sp.get("rollTurns", 0))
    src = [s for s in proj["sources"] if s.get("kind") == "timeline" and s["name"] == "ride"][0]
    keys = src["settings"]["keys"]
    look_ahead = proj["parameters"].get("camera/lookAhead", scene.get("parameters", {}).get("camera/lookAhead", 2.0))
    return path, keys, float(look_ahead)


def pose(path, s, look_ahead):
    p, t, n, b = path.at(s)
    q, _, _, _ = path.at(s + look_ahead)
    f = q - p
    f /= np.linalg.norm(f)
    u = n - f * np.dot(n, f)
    u /= np.linalg.norm(u)
    r = np.cross(f, u)
    return p, np.stack([r, u, f], axis=1)    # columns: right, up, forward


def rot_vec(Ra, Rb):
    """The rotation from frame a to frame b as a vector (radians) in frame a's own axes (right, up, forward)."""
    Q = Ra.T @ Rb
    c = max(-1.0, min(1.0, (np.trace(Q) - 1.0) / 2.0))
    ang = math.acos(c)
    w = np.array([Q[2, 1] - Q[1, 2], Q[0, 2] - Q[2, 0], Q[1, 0] - Q[0, 1]])
    nw = np.linalg.norm(w)
    if nw < 1e-12:
        return np.zeros(3)
    return w / nw * ang


def analyse(project_path, fps=30.0):
    path, keys, look_ahead = load(project_path)
    times = np.array([k["time"] for k in keys])
    vals = np.array([k["value"] for k in keys])
    lap = float(times[-1]) + 1e-3
    lin = times < times[-1]   # the last key is the step back to 0
    n = int(lap * fps)
    tt = np.arange(n) / fps
    s = np.interp(tt, times[lin], vals[lin]) * path.length
    frames = [pose(path, si, look_ahead) for si in s]
    R = [fr[1] for fr in frames]
    w = np.array([rot_vec(R[k], R[(k + 1) % n]) for k in range(n)]) * fps          # rad/s: pitch, yaw, roll
    rate = np.degrees(w)                                                              # deg/s
    acc = (np.roll(rate, -1, axis=0) - rate) * fps                                    # deg/s^2
    jerk = (np.roll(acc, -1, axis=0) - acc) * fps                                     # deg/s^3
    look = np.degrees([math.acos(max(-1, min(1, np.dot(R[k][:, 2], R[(k + 1) % n][:, 2])))) for k in range(n)])
    upc = np.degrees([math.acos(max(-1, min(1, np.dot(R[k][:, 1], R[(k + 1) % n][:, 1])))) for k in range(n)])
    return dict(t=tt, s=s, rate=rate, acc=acc, jerk=jerk, look=look, up=upc, lap=lap, length=path.length)


def summary(a):
    names = ("pitch", "yaw", "roll")
    out = {}
    for i, nm in enumerate(names):
        r = np.abs(a["rate"][:, i])
        j = np.abs(a["jerk"][:, i])
        out[nm] = {"rate_peak": float(r.max()), "rate_p95": float(np.percentile(r, 95)),
                   "jerk_peak": float(j.max()), "jerk_p95": float(np.percentile(j, 95)),
                   "rate_peak_at": float(a["t"][int(r.argmax())])}
    tot = np.linalg.norm(a["rate"], axis=1)
    out["total"] = {"rate_peak": float(tot.max()), "rate_p95": float(np.percentile(tot, 95)),
                    "rate_peak_at": float(a["t"][int(tot.argmax())])}
    out["look_per_frame"] = {"peak": float(a["look"].max()), "p95": float(np.percentile(a["look"], 95))}
    out["up_per_frame"] = {"peak": float(a["up"].max()), "p95": float(np.percentile(a["up"], 95))}
    # the twitchiest 3 s windows: the summed angular jerk, so a sharp change counts, not a steady turn
    jm = np.linalg.norm(a["jerk"], axis=1)
    fps = 1.0 / (a["t"][1] - a["t"][0])
    win = int(3 * fps)
    sc = np.convolve(jm, np.ones(win), "same")
    picks = []
    for k in np.argsort(-sc):
        if all(abs(a["t"][k] - p) > 6.0 for p in picks):
            picks.append(float(a["t"][k]))
        if len(picks) == 5:
            break
    out["twitchiest_windows_s"] = picks
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("project")
    ap.add_argument("--fps", type=float, default=30.0)
    ap.add_argument("--plot")
    ap.add_argument("--json")
    ap.add_argument("--label", default="")
    o = ap.parse_args()
    a = analyse(o.project, o.fps)
    s = summary(a)
    print(json.dumps(s, indent=1))
    if o.json:
        json.dump({"summary": s, "rate": a["rate"].round(3).tolist(), "t": a["t"].round(4).tolist()}, open(o.json, "w"))
    if o.plot:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, ax = plt.subplots(4, 1, figsize=(18, 11), sharex=True)
        for i, nm in enumerate(("pitch", "yaw", "roll")):
            ax[0].plot(a["t"], a["rate"][:, i], lw=0.6, label=nm)
            ax[1].plot(a["t"], np.clip(a["jerk"][:, i], -3e4, 3e4), lw=0.5, label=nm)
        ax[0].set_ylabel("angular rate, deg/s")
        ax[1].set_ylabel("angular jerk, deg/s^3 (clipped)")
        ax[2].plot(a["t"], a["look"], lw=0.6, label="look direction")
        ax[2].plot(a["t"], a["up"], lw=0.6, label="up vector")
        ax[2].set_ylabel("turn per frame, deg")
        ax[3].plot(a["t"], np.linalg.norm(a["rate"], axis=1), lw=0.6, color="k")
        ax[3].set_ylabel("total rate, deg/s")
        ax[3].set_xlabel("song time, s (one lap %.0f s)" % a["lap"])
        for x in ax[:3]:
            x.legend(loc="upper right")
        fig.suptitle("Rainbow World ride camera %s" % o.label)
        fig.tight_layout()
        fig.savefig(o.plot, dpi=90)


if __name__ == "__main__":
    main()
