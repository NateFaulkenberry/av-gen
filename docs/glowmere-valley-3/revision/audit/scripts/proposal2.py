"""Shared-reference design: filter the subject ONCE (xz with Th, y with Tv, lead on xz only), then
eye = ref(t - lag) [optionally trailed further by Te on xz] + offset, aim = ref(t) + aimOffset."""
import json, math, pathlib, sys
import numpy as np
HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import rigs as R
from analyze import HZ, evaluate
from proposal import kernel, FINE, measure

def conv(ts, P, t, T):
    h = kernel(T); taus = np.arange(len(h)) / FINE
    return np.stack([np.interp(t[:, None] - taus[None, :], ts, P[:, k]) @ h for k in range(P.shape[1])], axis=1)

def ref(ts, P, t, Th, Tv, lead):
    xz = conv(ts, P[:, [0, 2]], t, Th)
    y = conv(ts, P[:, [1]], t, Tv)[:, 0]
    if lead > 0 and Th > 0:
        d = 1.0 / FINE
        v = (xz - conv(ts, P[:, [0, 2]], t - d, Th)) / d
        xz = xz + lead * Th * v
    return np.stack([xz[:, 0], y, xz[:, 1]], axis=1)

def run(r, Th, Tv, lead=1.0, Te=0.0, lag=0.0):
    t = np.arange(math.ceil(r["start"] * HZ - 1e-9), math.ceil(r["end"] * HZ - 1e-9)) / HZ
    ts, P, _, _ = R.track_of(r["follow"])
    off = (np.array([R.eval_track(r["offkeys"], x) for x in t]) if r["offkeys"]
           else np.tile(np.array(r["off"], dtype=float), (len(t), 1)))
    if Te > 0:
        # the eye trails the (already filtered) reference: filter the reference's xz again by Te
        tt = np.arange(ts[0], ts[-1], 1.0 / FINE)
        rr = ref(ts, P, tt, Th, Tv, lead)
        e = ref(tt, rr, t - lag, Te, 0.0, 0.0)
        e[:, 1] = np.interp(t - lag, tt, rr[:, 1])
    else:
        e = ref(ts, P, t - lag, Th, Tv, lead)
    eye = e + off
    if r["clr"] > 0:
        from analyze import surface
        _, eye_auth, _ = R.reconstruct(r, HZ)
        fl = np.array([surface(x, z) for x, z in zip(eye_auth[:, 0], eye_auth[:, 2])]) + r["clr"]
        s = 0.25
        eye[:, 1] = fl + s * np.log1p(np.exp((eye[:, 1] - fl) / s))
    if r["aim"]:
        aim = ref(ts, P, t, Th, Tv, lead) + np.array(r["aimoff"], dtype=float)
        truth = R.interp_vec(ts, P, t) + np.array(r["aimoff"], dtype=float)
    else:
        aim = np.tile(np.array(r["target"], dtype=float), (len(t), 1)); truth = aim
    return t, eye, aim, truth

def travel(x):
    return float(np.sum(np.linalg.norm(np.diff(x[:, [0, 2]], axis=0), axis=1)))

configs = [("A ref 0.3/0.8 lead1", dict(Th=0.3, Tv=0.8, lead=1.0)),
           ("B ref 0.3/0.8 lead1 + eye trails 0.5", dict(Th=0.3, Tv=0.8, lead=1.0, Te=0.5)),
           ("C ref 0.5/1.0 lead0.5 + eye trails 0.6", dict(Th=0.5, Tv=1.0, lead=0.5, Te=0.6))]
out = []
print("shot | config | pitchHF deg (%fr) | yawHF deg | eyeY cm | eyeXZ cm | subject off-centre rms/max % | eye travel / subject travel")
for r in R.rigs():
    if not r["follow"] or r["follow"] not in R.CAST["entities"] or r["end"] - r["start"] < 2.0:
        continue
    t0, e0, a0, _ = evaluate(r)
    ts, P, _, _ = R.track_of(r["follow"])
    subj = R.interp_vec(ts, P, t0)
    m0 = measure(r, t0, e0, a0, a0)
    print(f"{r['slug']} as built        pitch {m0['pitchHF']:.3f} ({m0['pitchHFpct']:.2f})  yaw {m0['yawHF']:.3f}  eyeY {m0['eyeY_cm']:5.2f}  eyeXZ {m0['eyeXZ_cm']:5.2f}  off-centre {m0['screen_rms']:.1f}/{m0['screen_max']:.1f}  travel {travel(e0):.1f}/{travel(subj):.1f} m")
    row = dict(slug=r["slug"], built=m0)
    for label, cfg in configs:
        t, eye, aim, truth = run(r, **cfg)
        m = measure(r, t, eye, aim, truth)
        row[label] = m
        print(f"     {label:38s} pitch {m['pitchHF']:.3f} ({m['pitchHFpct']:.2f})  yaw {m['yawHF']:.3f}  eyeY {m['eyeY_cm']:5.2f}  eyeXZ {m['eyeXZ_cm']:5.2f}  off-centre {m['screen_rms']:.1f}/{m['screen_max']:.1f}  travel {travel(eye):.1f}")
    out.append(row)
(HERE / "proposal2.json").write_text(json.dumps(out, indent=1))
