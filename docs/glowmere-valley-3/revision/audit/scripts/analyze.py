"""Camera jitter of GV3's follow/aim rigs, reconstructed from the 20 Hz cast trace.

Metrics (per shot):
  pitchHF / yawHF  : RMS of the view angle minus its zero-phase Gaussian low-pass (sigma 0.3 s,
                     -3 dB near 0.44 Hz), in degrees and as % of the frame height/width.
  angAcc           : RMS angular acceleration of the view direction (deg/s^2), 2nd difference.
  eyeHF            : RMS of the eye position minus its low-pass (cm), vertical and horizontal.
  eyeJerk          : RMS of the eye's third difference (m/s^3).
  f_dom            : dominant frequency of the pitch residual (Hz).
Attribution variants re-run the same rig with one cause removed.
"""
import json
import math
import pathlib
import sys

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import rigs as R  # noqa: E402

HERE = pathlib.Path(__file__).resolve().parent
HEIGHTS = json.loads((HERE / "heights.json").read_text())
HZ = 20.0
SIGMA = 0.3


def gauss_lp(x, sigma_s=SIGMA, hz=HZ):
    s = sigma_s * hz
    r = int(math.ceil(4 * s))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / s) ** 2)
    k /= k.sum()
    # odd (point) reflection: a linear trend passes through the filter untouched, so the edges of
    # a shot of something walking in a straight line report no residual
    if len(x) > r:
        pad = np.pad(x, (r, r), mode="reflect", reflect_type="odd")
    else:
        pad = np.pad(x, (r, r), mode="edge")
    return np.convolve(pad, k, mode="same")[r:-r]


def surface(x, z):
    h, w = HEIGHTS[f"{round(float(x), 3):.3f},{round(float(z), 3):.3f}"]
    return max(h, w) if w is not None else h


def lp_track(P, sigma):
    return np.stack([gauss_lp(P[:, k], sigma, HZ) for k in range(3)], axis=1)


def evaluate(r, *, lag=None, clearance=True, smooth_y=False, smooth_all=0.0):
    """Eye and aim at 20 Hz, optionally with a cause removed."""
    lag = r["lag"] if lag is None else lag
    t = np.arange(math.ceil(r["start"] * HZ - 1e-9), math.ceil(r["end"] * HZ - 1e-9)) / HZ

    def subject(name):
        ts, P, _, _ = R.track_of(name)
        P = P.copy()
        if smooth_y:
            P[:, 1] = gauss_lp(P[:, 1], SIGMA, 20.0) if len(ts) > 2 else P[:, 1]
        if smooth_all > 0 and len(ts) > 2:
            P = lp_track(P, smooth_all)
        return ts, P

    if r["follow"]:
        ts, P = subject(r["follow"])
        lagged = R.interp_vec(ts, P, t - lag)
        # the probed heights are at the as-authored eye positions; for variants that move the eye
        # horizontally we look up the nearest probed point on the same shot (the xz moves by cm)
        if r["offkeys"]:
            off = np.array([R.eval_track(r["offkeys"], x) for x in t])
        else:
            off = np.tile(np.array(r["off"], dtype=float), (len(t), 1))
        eye = lagged + off
    else:
        eye = (np.array([R.eval_track(r["poskeys"], x) for x in t]) if r["poskeys"]
               else np.tile(np.array(r["position"], dtype=float), (len(t), 1)))
    floor_active = np.zeros(len(t), dtype=bool)
    if r["follow"] and r["clr"] > 0 and clearance:
        # the as-authored pre-clearance eye, whose xz the heights were probed at
        t0, eye_auth, _ = R.reconstruct(r, HZ)
        assert len(t0) == len(t)
        fl = np.array([surface(x, z) for x, z in zip(eye_auth[:, 0], eye_auth[:, 2])]) + r["clr"]
        floor_active = fl > eye[:, 1]
        eye[:, 1] = np.maximum(eye[:, 1], fl)
    if r["aim"]:
        ts, P = subject(r["aim"])
        aim = R.interp_vec(ts, P, t) + np.array(r["aimoff"], dtype=float)
    else:
        aim = (np.array([R.eval_track(r["tgtkeys"], x) for x in t]) if r["tgtkeys"]
               else np.tile(np.array(r["target"], dtype=float), (len(t), 1)))
    return t, eye, aim, floor_active


def metrics(r, t, eye, aim):
    v = aim - eye
    horiz = np.hypot(v[:, 0], v[:, 2])
    yaw = np.degrees(np.unwrap(np.arctan2(v[:, 0], v[:, 2])))
    pitch = np.degrees(np.arctan2(v[:, 1], horiz))
    f = r["focal"] or 35.0
    vfov = math.degrees(2 * math.atan(12.0 / f))
    hfov = math.degrees(2 * math.atan(math.tan(math.radians(vfov) / 2) * 16 / 9))
    n = len(t)
    out = {}
    if n < 8:
        return None
    pr = pitch - gauss_lp(pitch)
    yr = yaw - gauss_lp(yaw)
    out["pitchHF_deg"] = float(np.sqrt(np.mean(pr ** 2)))
    out["yawHF_deg"] = float(np.sqrt(np.mean(yr ** 2)))
    out["pitchHF_pct"] = 100 * out["pitchHF_deg"] / vfov
    out["yawHF_pct"] = 100 * out["yawHF_deg"] / hfov
    out["pitchPP_deg"] = float(np.percentile(pr, 99) - np.percentile(pr, 1))
    # angular acceleration of the view direction
    a_p = np.diff(pitch, 2) * HZ * HZ
    a_y = np.diff(yaw, 2) * HZ * HZ
    out["angAcc_rms"] = float(np.sqrt(np.mean(a_p ** 2 + a_y ** 2)))
    w = np.hypot(np.diff(pitch), np.diff(yaw)) * HZ
    out["angVel_p95"] = float(np.percentile(w, 95))
    er = eye - np.stack([gauss_lp(eye[:, k]) for k in range(3)], axis=1)
    out["eyeHF_y_cm"] = 100 * float(np.sqrt(np.mean(er[:, 1] ** 2)))
    out["eyeHF_xz_cm"] = 100 * float(np.sqrt(np.mean(er[:, 0] ** 2 + er[:, 2] ** 2)))
    j = np.diff(eye, 3, axis=0) * HZ ** 3
    out["eyeJerk_rms"] = float(np.sqrt(np.mean(np.sum(j ** 2, axis=1))))
    # dominant frequency of the pitch residual
    spec = np.abs(np.fft.rfft(pr * np.hanning(n))) ** 2
    freqs = np.fft.rfftfreq(n, 1 / HZ)
    k = int(np.argmax(spec[1:]) + 1)
    out["f_dom_pitch"] = float(freqs[k])
    specy = np.abs(np.fft.rfft(yr * np.hanning(n))) ** 2
    out["f_dom_yaw"] = float(freqs[int(np.argmax(specy[1:]) + 1)])
    out["dist_m"] = float(np.mean(np.linalg.norm(v, axis=1)))
    out["vfov"] = vfov
    return out


def subject_stats(r):
    name = r["follow"] or r["aim"]
    if name not in R.CAST["entities"]:
        return {}
    ts, P, spd, act = R.track_of(name)
    m = (ts >= r["start"]) & (ts < r["end"])
    Pm = P[m]
    acts = [a for a, keep in zip(act, m) if keep]
    from collections import Counter
    res_y = Pm[:, 1] - gauss_lp(Pm[:, 1])
    lpxz = np.stack([gauss_lp(Pm[:, 0]), gauss_lp(Pm[:, 2])], axis=1)
    res_xz = Pm[:, [0, 2]] - lpxz
    n = len(res_y)
    spec = np.abs(np.fft.rfft(res_y * np.hanning(n))) ** 2
    freqs = np.fft.rfftfreq(n, 1 / HZ)
    yawtr = np.degrees(np.unwrap(np.array(R.CAST["entities"][name]["yaw"])[m]))
    yawrate = np.diff(yawtr) * HZ
    return dict(speed_mean=float(np.mean(spd[m])), speed_max=float(np.max(spd[m])),
                acts=dict(Counter(acts).most_common(4)),
                subjY_HF_cm=100 * float(np.sqrt(np.mean(res_y ** 2))),
                subjY_fdom=float(freqs[int(np.argmax(spec[1:]) + 1)]),
                subjXZ_HF_cm=100 * float(np.sqrt(np.mean(np.sum(res_xz ** 2, axis=1)))),
                subj_travel_m=float(np.sum(np.linalg.norm(np.diff(Pm[:, [0, 2]], axis=0), axis=1))),
                subj_yawrate_p95=float(np.percentile(np.abs(yawrate), 95)),
                subj_yaw_total=float(yawtr[-1] - yawtr[0]))


def main():
    rows = []
    for r in R.rigs():
        base = evaluate(r)
        m0 = metrics(r, base[0], base[1], base[2])
        if m0 is None:
            continue
        variants = {}
        variants["noBob"] = metrics(r, *evaluate(r, smooth_y=True)[:3])
        if r["lag"] > 0:
            variants["noLag"] = metrics(r, *evaluate(r, lag=0.0)[:3])
        if r["follow"] and r["clr"] > 0:
            variants["noClr"] = metrics(r, *evaluate(r, clearance=False)[:3])
        variants["subjSmooth0.5"] = metrics(r, *evaluate(r, smooth_all=0.5)[:3])
        rows.append(dict(slug=r["slug"], follow=r["follow"], aim=r["aim"], lag=r["lag"], clr=r["clr"],
                         focal=r["focal"], start=r["start"], end=r["end"], floor_frac=float(np.mean(base[3])),
                         floor_switches=int(np.sum(np.abs(np.diff(base[3].astype(int))))),
                         base=m0, variants=variants, subject=subject_stats(r)))
    (HERE / "results.json").write_text(json.dumps(rows, indent=1))
    hdr = ("shot  subj      lag  f   dur  | pitchHF deg (%fr) yawHF deg (%fr) | angAcc  eyeY cm eyeXZ cm jerk | "
           "fP   | floor% sw | noBob pHF | noLag pHF yHF | noClr pHF | smooth pHF yHF")
    print(hdr)
    for x in rows:
        b = x["base"]
        v = x["variants"]
        s = f"{x['slug']:4s}  {str(x['follow'] or '-')[:4]:4s}>{str(x['aim'] or 'fixed')[:5]:5s} {x['lag']:.1f} {x['focal']:3.0f} {x['end']-x['start']:4.1f} | "
        s += f"{b['pitchHF_deg']:.3f} ({b['pitchHF_pct']:.2f})   {b['yawHF_deg']:.3f} ({b['yawHF_pct']:.2f})   | "
        s += f"{b['angAcc_rms']:6.1f} {b['eyeHF_y_cm']:6.2f} {b['eyeHF_xz_cm']:6.2f} {b['eyeJerk_rms']:5.1f} | {b['f_dom_pitch']:.2f} | "
        s += f"{100*x['floor_frac']:4.0f}% {x['floor_switches']:2d} | {v['noBob']['pitchHF_deg']:.3f} | "
        if "noLag" in v:
            s += f"{v['noLag']['pitchHF_deg']:.3f} {v['noLag']['yawHF_deg']:.3f} | "
        else:
            s += "   -      -   | "
        s += (f"{v['noClr']['pitchHF_deg']:.3f} | " if "noClr" in v else "  -   | ")
        s += f"{v['subjSmooth0.5']['pitchHF_deg']:.3f} {v['subjSmooth0.5']['yawHF_deg']:.3f}"
        print(s)
    print()
    for x in rows:
        print(x["slug"], json.dumps(x["subject"]))


if __name__ == "__main__":
    main()
