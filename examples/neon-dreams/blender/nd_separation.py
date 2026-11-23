"""Neon Dreams it18 (owner msg 271): figure-to-figure separation over the whole song. Plain Python (no Blender).

"keep aliens from clipping through each other": every pair of figures in a BUILT project (standing spots, groups,
the band's audience, walkers along their timed tracks, flyers before take-off / after landing, riders in one cabin)
must keep a minimum distance between body centres (horizontal, on the same floor) at every instant of the song.

    python3 blender/nd_separation.py PROJECT.json [--out report.json] [--step 0.1] [--static 0.75] [--moving 0.62]

Rules:
- two figures clash when they stand on the same floor (|dy| < 1.4 m) and ride the same bob (or neither does), and
  their centres come closer than the limit, scaled by the mean of the two figures' scales;
- the limit is `--static` (m) when both stand still at that instant (heads are 0.5 m wide, arms gesture: two idles
  0.6 m apart interpenetrate), `--moving` when either is walking / flying (a brush-by in passing);
- riders are compared in their cabin's own frame (same parent node), everyone else in world space.
Exit code 0 when nothing clashes, 5 otherwise (like build.py's --check-ground 4 and --check-flights 3).
"""
import argparse
import bisect
import json
import math
import os
import sys
from collections import defaultdict


def lerp_track(keys):
    ts = [k["time"] for k in keys]
    vs = [k["value"] for k in keys]

    def at(t):
        i = bisect.bisect_right(ts, t)
        if i <= 0:
            return vs[0]
        if i >= len(ts):
            return vs[-1]
        t0, t1 = ts[i - 1], ts[i]
        if t1 - t0 < 1e-9:
            return vs[i]
        u = (t - t0) / (t1 - t0)
        a, b = vs[i - 1], vs[i]
        return [a[j] + (b[j] - a[j]) * u for j in range(3)]
    return at


def figures(project_path):
    proj = json.load(open(project_path))
    scene_path = os.path.join(os.path.dirname(project_path), proj["assets"]["scene"]["path"])
    scene = json.load(open(scene_path))
    return figures_from(scene["nodes"], proj["timeline"]["tracks"]), float(proj["sequence"].get("duration") or 70.0)


def figures_from(nodes, track_list):
    """Every alien figure of a scene's nodes, with its timed position (a function of t) where a track moves it."""
    tracks = {t["target"]: t["keys"] for t in track_list}
    figs = []
    for n in nodes:
        if n.get("kind") != "gltf" or "aliens/" not in n.get("asset", ""):
            continue
        nm = n["name"]
        pk = tracks.get("nodes/%s/position" % nm)
        f = {"id": nm, "bob": n.get("bob"), "parent": n.get("parent"), "scale": n.get("scale", [1.0])[0],
             "pos": n["position"], "kind": "walker" if nm.startswith("walk") else
             ("flyer" if nm.startswith("flyer") else ("rider" if n.get("parent") else "spot"))}
        if pk and len(pk) > 1:
            f["at"] = lerp_track(pk)
            f["keys"] = pk
        figs.append(f)
    return figs


def moving_at(f, t, dt):
    if "at" not in f:
        return False
    a, b = f["at"](t - dt), f["at"](t + dt)
    return math.dist(a, b) > 0.02


def check(project_path, step=0.1, static=0.75, moving=0.62, floor=1.4):
    figs, song = figures(project_path)
    rep = check_figures(figs, song, step, static, moving, floor)
    rep["project"] = project_path
    return rep


def check_figures(figs, song, step=0.1, static=0.75, moving=0.62, floor=1.4):
    """build.py / nd_it18.py entry: the same check on figures already in memory (figures_from)."""
    fixed = [f for f in figs if "at" not in f]
    mov = [f for f in figs if "at" in f]
    worst = {}

    def note(a, b, d, lim, t, mv):
        key = tuple(sorted((a["id"], b["id"])))
        w = worst.get(key)
        if w is None:
            w = worst[key] = {"a": key[0], "b": key[1], "min_m": d, "limit_m": lim, "t": t, "t_first": t, "t_last": t,
                              "moving": mv, "kinds": sorted((a["kind"], b["kind"]))}
        w["t_last"] = t
        if d < w["min_m"]:
            w.update(min_m=d, limit_m=lim, t=t, moving=mv)

    def same_floor(a, b, pa, pb):
        if (a["bob"] or None) != (b["bob"] or None):
            return False
        if (a["parent"] or None) != (b["parent"] or None):
            return False
        return abs(pa[1] - pb[1]) < floor

    # static pairs: once
    cell = 2.0
    grid = defaultdict(list)
    for f in fixed:
        grid[(f["parent"], int(math.floor(f["pos"][0] / cell)), int(math.floor(f["pos"][2] / cell)))].append(f)
    for (par, gx, gz), lst in grid.items():
        for dx in (-1, 0, 1):
            for dz in (-1, 0, 1):
                for b in grid.get((par, gx + dx, gz + dz), []):
                    for a in lst:
                        if a["id"] >= b["id"] or not same_floor(a, b, a["pos"], b["pos"]):
                            continue
                        lim = static * 0.5 * (a["scale"] + b["scale"])
                        d = math.hypot(a["pos"][0] - b["pos"][0], a["pos"][2] - b["pos"][2])
                        if d < lim:
                            note(a, b, round(d, 3), round(lim, 3), None, False)
    # moving figures against everyone, sampled through the song
    n = int(song / step) + 1
    for i in range(n):
        t = round(i * step, 3)
        pos = {f["id"]: f["at"](t) for f in mov}
        mg = defaultdict(list)
        for f in mov:
            p = pos[f["id"]]
            mg[(int(math.floor(p[0] / cell)), int(math.floor(p[2] / cell)))].append(f)
        seen = set()
        for (gx, gz), lst in mg.items():
            for a in lst:
                pa = pos[a["id"]]
                mva = moving_at(a, t, step * 0.5)
                for dx in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        cands = [(b, pos[b["id"]]) for b in mg.get((gx + dx, gz + dz), [])] + \
                                [(b, b["pos"]) for b in grid.get((None, gx + dx, gz + dz), [])]
                        for b, pb in cands:
                            if b is a or (b["id"], a["id"]) in seen:
                                continue
                            if not same_floor(a, b, pa, pb):
                                continue
                            mv = mva or ("at" in b and moving_at(b, t, step * 0.5))
                            lim = (moving if mv else static) * 0.5 * (a["scale"] + b["scale"])
                            d = math.hypot(pa[0] - pb[0], pa[2] - pb[2])
                            if d < lim:
                                note(a, b, round(d, 3), round(lim, 3), t, mv)
                            seen.add((a["id"], b["id"]))
    viol = sorted(worst.values(), key=lambda w: w["min_m"])
    by = defaultdict(int)
    for w in viol:
        by["-".join(w["kinds"])] += 1
    return {"figures": len(figs), "moving": len(mov), "step_s": step,
            "limits_m": {"static": static, "moving": moving, "floor": floor},
            "summary": {"pairs": len(viol), "by_kind": dict(by)}, "violations": viol}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("project")
    ap.add_argument("--out", default="")
    ap.add_argument("--step", type=float, default=0.1)
    ap.add_argument("--static", type=float, default=0.75)
    ap.add_argument("--moving", type=float, default=0.62)
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    rep = check(a.project, a.step, a.static, a.moving)
    if a.out:
        json.dump(rep, open(a.out, "w"), indent=1)
    if not a.quiet:
        for w in rep["violations"]:
            print("separation: %-26s %-26s %.2f m < %.2f  %s  t %s (%s..%s)" % (
                w["a"], w["b"], w["min_m"], w["limit_m"], "/".join(w["kinds"]), w["t"], w["t_first"], w["t_last"]))
    print("separation check: %d figures (%d moving), %d pair(s) too close %s" % (
        rep["figures"], rep["moving"], rep["summary"]["pairs"], json.dumps(rep["summary"]["by_kind"])))
    sys.exit(5 if rep["summary"]["pairs"] else 0)


if __name__ == "__main__":
    main()
