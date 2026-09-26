"""The Director compiler's chase rig (compiler.cpp:243-262) on GV3's subjects: followLocal offset
(0, 2, -4) in the body frame, lag 0.25 s, aim node + (0, 1.2, 0). Heading from the trace's yaw
(the drawn rotation adds Liveliness sway and slope tilt on top, not modelled here)."""
import math, sys, pathlib, json
import numpy as np
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import rigs as R
from analyze import gauss_lp, HZ
def run(name, t0, t1, lag=0.25, off=(0.0, 2.0, -4.0), aimoff=(0.0, 1.2, 0.0), focal=35.0):
    e = R.CAST["entities"][name]
    ts = np.array(e["t"]); P = np.array(e["position"], float); yaw = np.unwrap(np.array(e["yaw"], float))
    t = np.arange(math.ceil(t0 * HZ), math.ceil(t1 * HZ)) / HZ
    Pl = R.interp_vec(ts, P, t - lag); yl = np.interp(t - lag, ts, yaw)
    ox, oy, oz = off
    wx = ox * np.cos(yl) + oz * np.sin(yl); wz = -ox * np.sin(yl) + oz * np.cos(yl)
    eye = Pl + np.stack([wx, np.full_like(wx, oy), wz], axis=1)
    aim = R.interp_vec(ts, P, t) + np.array(aimoff)
    v = aim - eye
    vyaw = np.degrees(np.unwrap(np.arctan2(v[:, 0], v[:, 2]))); vp = np.degrees(np.arctan2(v[:, 1], np.hypot(v[:, 0], v[:, 2])))
    w = np.hypot(np.diff(vyaw), np.diff(vp)) * HZ
    sp = np.linalg.norm(np.diff(eye, axis=0), axis=1) * HZ
    yr = vyaw - gauss_lp(vyaw); pr = vp - gauss_lp(vp)
    vfov = math.degrees(2 * math.atan(12 / focal))
    return dict(yawHF=float(np.sqrt(np.mean(yr**2))), pitchHF=float(np.sqrt(np.mean(pr**2))),
                angVel_max=float(w.max()), angVel_p95=float(np.percentile(w, 95)),
                eyeSpeed_max=float(sp.max()), eyeSpeed_p95=float(np.percentile(sp, 95)),
                subjSpeed_max=float(np.max(np.linalg.norm(np.diff(R.interp_vec(ts, P, t), axis=0), axis=1)) * HZ))
for name, a, b, lbl in [("vane", 118.62, 133.39, "s19 window"), ("ember", 188.77, 192.46, "s36 window"),
                        ("vane", 199.85, 207.23, "s38 window"), ("sage", 103.85, 118.62, "s18 window")]:
    m = run(name, a, b)
    print(lbl, name, {k: round(v, 2) for k, v in m.items()})
