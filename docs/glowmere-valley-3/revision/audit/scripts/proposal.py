"""What a trail-filtered follow would do to GV3's follow rigs, measured on the same trace.

The proposed primitive: the eye and the aim read the subject through a CAUSAL, FINITE kernel over its
recorded past -- the impulse response of a critically damped spring, h(tau) = w^2 tau exp(-w tau),
truncated at 8/w and renormalised; mean delay 2/w = T. Optional lead adds T * (filtered velocity) to
cancel the delay on straight walks. No integration state: a pure function of the trail, so it is
seek-exact wherever the trail is (HIST, ADR-703/700).
"""
import json
import math
import pathlib
import sys

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import rigs as R  # noqa: E402
from analyze import HZ, gauss_lp, surface  # noqa: E402

FINE = 60.0  # evaluate on the render's grid, reading the trail by interpolation


def kernel(T, hz=FINE):
    if T <= 0:
        return np.array([1.0])
    w = 2.0 / T
    tau = np.arange(0, int(math.ceil(8.0 / w * hz)) + 1) / hz
    h = w * w * tau * np.exp(-w * tau)
    h[0] = 0.0
    return h / h.sum()


def filtered(ts, P, t, T, lead=0.0):
    """sum_k h_k P(t - tau_k), plus lead * T * d/dt of the same, per time in t."""
    h = kernel(T)
    taus = np.arange(len(h)) / FINE
    out = np.zeros((len(t), 3))
    for k in range(3):
        grid = np.interp(t[:, None] - taus[None, :], ts, P[:, k])
        out[:, k] = grid @ h
    if lead > 0 and T > 0:
        dt = 1.0 / FINE
        ahead = np.zeros((len(t), 3))
        for k in range(3):
            g0 = np.interp(t[:, None] - taus[None, :], ts, P[:, k]) @ h
            g1 = np.interp((t - dt)[:, None] - taus[None, :], ts, P[:, k]) @ h
            ahead[:, k] = (g0 - g1) / dt
        out += lead * T * ahead
    return out


def run(r, Th, Tv, Ta, lead=0.0, lag=None):
    lag = r["lag"] if lag is None else lag
    t = np.arange(math.ceil(r["start"] * HZ - 1e-9), math.ceil(r["end"] * HZ - 1e-9)) / HZ
    ts, P, _, _ = R.track_of(r["follow"])
    off = (np.array([R.eval_track(r["offkeys"], x) for x in t]) if r["offkeys"]
           else np.tile(np.array(r["off"], dtype=float), (len(t), 1)))
    eh = filtered(ts, P, t - lag, Th, lead)
    ev = filtered(ts, P, t - lag, Tv, 0.0)
    eye = np.stack([eh[:, 0], ev[:, 1], eh[:, 2]], axis=1) + off
    if r["clr"] > 0:
        # the floor at the authored eye xz (probed); the filtered eye moves by centimetres in xz
        _, eye_auth, _ = R.reconstruct(r, HZ)
        fl = np.array([surface(x, z) for x, z in zip(eye_auth[:, 0], eye_auth[:, 2])]) + r["clr"]
        # a smooth floor (softplus over 0.25 m) rather than max()
        s = 0.25
        eye[:, 1] = fl + s * np.log1p(np.exp((eye[:, 1] - fl) / s))
    if r["aim"]:
        ta, Pa, _, _ = R.track_of(r["aim"])
        aim = filtered(ta, Pa, t, Ta, lead) + np.array(r["aimoff"], dtype=float)
        truth = R.interp_vec(ta, Pa, t) + np.array(r["aimoff"], dtype=float)
    else:
        aim = np.tile(np.array(r["target"], dtype=float), (len(t), 1))
        truth = aim
    return t, eye, aim, truth


def measure(r, t, eye, aim, truth):
    v = aim - eye
    yaw = np.degrees(np.unwrap(np.arctan2(v[:, 0], v[:, 2])))
    pitch = np.degrees(np.arctan2(v[:, 1], np.hypot(v[:, 0], v[:, 2])))
    vfov = math.degrees(2 * math.atan(12.0 / (r["focal"] or 35.0)))
    hfov = math.degrees(2 * math.atan(math.tan(math.radians(vfov) / 2) * 16 / 9))
    pr = pitch - gauss_lp(pitch)
    yr = yaw - gauss_lp(yaw)
    er = eye - np.stack([gauss_lp(eye[:, k]) for k in range(3)], axis=1)
    # where the subject's aim point sits in the frame, as a fraction of the half-frame
    u = truth - eye
    fwd = v / np.linalg.norm(v, axis=1, keepdims=True)
    up0 = np.array([0.0, 1.0, 0.0])
    right = np.cross(fwd, up0)
    right /= np.linalg.norm(right, axis=1, keepdims=True)
    up = np.cross(right, fwd)
    z = np.sum(u * fwd, axis=1)
    sx = np.sum(u * right, axis=1) / z / math.tan(math.radians(hfov) / 2)
    sy = np.sum(u * up, axis=1) / z / math.tan(math.radians(vfov) / 2)
    return dict(pitchHF=float(np.sqrt(np.mean(pr ** 2))), yawHF=float(np.sqrt(np.mean(yr ** 2))),
                pitchHFpct=100 * float(np.sqrt(np.mean(pr ** 2))) / vfov,
                eyeY_cm=100 * float(np.sqrt(np.mean(er[:, 1] ** 2))),
                eyeXZ_cm=100 * float(np.sqrt(np.mean(er[:, 0] ** 2 + er[:, 2] ** 2))),
                screen_rms=100 * float(np.sqrt(np.mean(sx ** 2 + sy ** 2))) / 2,  # % of frame
                screen_max=100 * float(np.max(np.hypot(sx, sy))) / 2)


def main():
    configs = [("as built", None), ("lag 0", dict(Th=0, Tv=0, Ta=0, lag=0.0)),
               ("vert only 0.6", dict(Th=0, Tv=0.6, Ta=0, lag=0.0)),
               ("rec 0.35/0.8/0.25", dict(Th=0.35, Tv=0.8, Ta=0.25, lag=0.0)),
               ("rec+lead", dict(Th=0.35, Tv=0.8, Ta=0.25, lead=1.0, lag=0.0))]
    rows = []
    for r in R.rigs():
        if not r["follow"] or r["follow"] not in R.CAST["entities"] or r["end"] - r["start"] < 2.0:
            continue
        line = [r["slug"]]
        for label, cfg in configs:
            if cfg is None:
                cfg = dict(Th=0, Tv=0, Ta=0, lag=r["lag"])
                t, eye, aim, truth = run(r, **cfg)
                # the as-built floor is max(), not softplus: recompute exactly
                from analyze import evaluate
                t, eye, aim, _ = evaluate(r)
                truth = aim
            else:
                t, eye, aim, truth = run(r, **cfg)
            m = measure(r, t, eye, aim, truth)
            line.append((label, m))
        rows.append(line)
    (HERE / "proposal.json").write_text(json.dumps(rows, indent=1))
    print("per shot: pitchHF deg (% frame) / yawHF deg / eyeY cm / subject off-centre rms% max%")
    for line in rows:
        print(line[0])
        for label, m in line[1:]:
            print(f"   {label:18s} pitch {m['pitchHF']:.3f} ({m['pitchHFpct']:.2f}%)  yaw {m['yawHF']:.3f}  "
                  f"eyeY {m['eyeY_cm']:5.2f}  eyeXZ {m['eyeXZ_cm']:5.2f}  subject off-centre rms {m['screen_rms']:.1f}% max {m['screen_max']:.1f}%")


if __name__ == "__main__":
    main()
