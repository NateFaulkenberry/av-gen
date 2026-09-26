#!/usr/bin/env python3
"""Camera stability, measured from `avgen_cast_trace --camera` without rendering (ADR-911).

    tools/camera_stability.py TRACE.json [--scene S.scene.json] [--shots s03,s09,...] [--json OUT]

Reads the per-frame camera track (`"camera"`, documented in tools/cast_trace.cpp) and reports, per
shot, the numbers the GV3 camera audit set its pass bar in:

  pitchHF / yawHF   RMS of the view angle minus its zero-phase Gaussian low-pass (sigma 0.3 s, -3 dB
                    near 0.44 Hz), degrees. The wobble a viewer reads as the camera nodding or rocking.
  eyeY / eyeXZ      the same residual of the eye's height / horizontal position, centimetres.
  off rms / max     where the shot's subject (`aimPoint`: its aim node, raw, plus the aim offset) sits
                    in frame, as a percentage of the frame (|NDC| / 2 * 100); 16:9.
  travel            the eye's horizontal path length over the subject's (`followPoint`), both on a
                    20 Hz subsample so the subject's footfall jitter does not count as distance.
  angVel p95        the view direction's angular speed, degrees per second, 95th percentile.

Pass bar (GV3 camera audit): pitchHF and yawHF <= 0.1 deg, eyeY <= 2 cm, off max <= 20 %,
travel about 1. A shot is one entry of cameraDirection.shots; shots are named by their camera's slug.
Exit status 0; the verdict is in the table and the JSON, not the exit code.
"""
import argparse
import json
import math
import sys

import numpy as np

SIGMA = 0.3
BAR = dict(pitchHF=0.1, yawHF=0.1, eyeY=2.0, offMax=20.0)


def gauss_lp(x, hz, sigma_s=SIGMA):
    s = sigma_s * hz
    r = int(math.ceil(4 * s))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / s) ** 2)
    k /= k.sum()
    # Odd reflection: a straight-line trend passes the filter untouched, so the ends of a shot of
    # something walking steadily report no residual.
    pad = np.pad(x, (r, r), mode="reflect", reflect_type="odd") if len(x) > r else np.pad(x, (r, r), mode="edge")
    return np.convolve(pad, k, mode="same")[r:-r]


def hf_rms(x, hz):
    return float(np.sqrt(np.mean((x - gauss_lp(x, hz)) ** 2)))


def path_length(p, step):
    q = p[::step]
    return float(np.sum(np.linalg.norm(np.diff(q[:, [0, 2]], axis=0), axis=1)))


def measure(cam, frames, hz):
    eye = np.array([cam["eye"][i] for i in frames], dtype=float)
    tgt = np.array([cam["target"][i] for i in frames], dtype=float)
    vfov = np.array([cam["vfov"][i] for i in frames], dtype=float)
    v = tgt - eye
    yaw = np.degrees(np.unwrap(np.arctan2(v[:, 0], v[:, 2])))
    pitch = np.degrees(np.arctan2(v[:, 1], np.hypot(v[:, 0], v[:, 2])))
    out = dict(frames=len(frames), pitchHF=hf_rms(pitch, hz), yawHF=hf_rms(yaw, hz),
               eyeY=100 * hf_rms(eye[:, 1], hz),
               eyeXZ=100 * float(np.sqrt(hf_rms(eye[:, 0], hz) ** 2 + hf_rms(eye[:, 2], hz) ** 2)))
    w = np.hypot(np.diff(pitch), np.diff(yaw)) * hz
    out["angVelP95"] = float(np.percentile(w, 95)) if len(w) else 0.0
    aims = [cam["aimPoint"][i] for i in frames]
    if all(a is not None for a in aims):
        truth = np.array(aims, dtype=float)
        u = truth - eye
        fwd = v / np.linalg.norm(v, axis=1, keepdims=True)
        right = np.cross(fwd, np.array([0.0, 1.0, 0.0]))
        right /= np.linalg.norm(right, axis=1, keepdims=True)
        up = np.cross(right, fwd)
        z = np.sum(u * fwd, axis=1)
        tan_v = np.tan(np.radians(vfov) / 2)
        tan_h = tan_v * 16.0 / 9.0
        sx = np.sum(u * right, axis=1) / z / tan_h
        sy = np.sum(u * up, axis=1) / z / tan_v
        off = 100 * np.hypot(sx, sy) / 2
        out.update(offRms=float(np.sqrt(np.mean(off ** 2))), offMax=float(np.max(off)),
                   offMaxX=float(100 * np.max(np.abs(sx)) / 2), offMaxY=float(100 * np.max(np.abs(sy)) / 2))
    follows = [cam["followPoint"][i] for i in frames]
    if all(f is not None for f in follows):
        subj = np.array(follows, dtype=float)
        step = max(1, int(round(hz / 20.0)))
        s_len = path_length(subj, step)
        out["subjectTravel"] = s_len
        out["eyeTravel"] = path_length(eye, step)
        out["travel"] = out["eyeTravel"] / s_len if s_len > 0.05 else None
    return out


def verdict(m):
    fails = [k for k, bar in BAR.items() if m.get(k) is not None and m[k] > bar]
    if m.get("travel") is not None and not (0.85 <= m["travel"] <= 1.15):
        fails.append("travel")
    return fails


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("trace")
    ap.add_argument("--shots", default="", help="comma-separated camera slugs; default: every shot with a follow or aim node")
    ap.add_argument("--json", default="", help="write the numbers here too")
    args = ap.parse_args()
    doc = json.load(open(args.trace))
    cam = doc.get("camera")
    if cam is None:
        sys.exit("the trace has no camera track: run avgen_cast_trace with --camera")
    hz = float(cam["fps"])
    wanted = [s for s in args.shots.split(",") if s]
    # Group the frames by the shot on screen: a run of frames with the same (shot, camera).
    runs = {}
    for i, (shot, slug) in enumerate(zip(cam["shot"], cam["camera"])):
        if shot < 0 or not slug:
            continue
        runs.setdefault((shot, slug), []).append(i)
    results = {}
    print(f"{'shot':5s} {'frames':>6s} | {'pitchHF':>7s} {'yawHF':>7s} | {'eyeY cm':>7s} {'eyeXZ cm':>8s} | "
          f"{'off rms':>7s} {'off max':>7s} | {'travel':>6s} | {'angVel p95':>10s} | verdict")
    for (shot, slug), frames in sorted(runs.items()):
        if wanted and slug not in wanted:
            continue
        has_subject = any(cam["aimPoint"][i] is not None or cam["followPoint"][i] is not None for i in frames)
        if not wanted and not has_subject:
            continue
        if len(frames) < 8:
            continue
        m = measure(cam, frames, hz)
        m["start"] = cam["t"][frames[0]]
        m["end"] = cam["t"][frames[-1]]
        fails = verdict(m)
        m["fails"] = fails
        m["shot"] = shot
        results[slug if slug not in results else f"{slug}@{shot}"] = m
        off = (f"{m['offRms']:7.1f} {m['offMax']:7.1f}" if "offRms" in m else f"{'-':>7s} {'-':>7s}")
        trav = f"{m['travel']:6.2f}" if m.get("travel") is not None else f"{'-':>6s}"
        print(f"{slug:5s} {m['frames']:6d} | {m['pitchHF']:7.3f} {m['yawHF']:7.3f} | {m['eyeY']:7.2f} {m['eyeXZ']:8.2f} | "
              f"{off} | {trav} | {m['angVelP95']:10.1f} | {'pass' if not fails else 'FAIL ' + ','.join(fails)}")
    if args.json:
        with open(args.json, "w") as f:
            json.dump(results, f, indent=1)


if __name__ == "__main__":
    main()
