#!/usr/bin/env python3
"""Neon Dreams, iteration 11 (owner msgs 130, 132, 136): judge the journey by the aliens it shows.

    python3 blender/nd_camera_eval.py [--root examples/neon-dreams] [--name it11] [--no-build] [--tiers]
                                      [--waypoints CANDIDATE.json] [--out DIR]

Builds the scene with ROOT/build.py (or reads ROOT/scene/NAME.*), samples the camera (as the engine rides the spline: lens at splineT, aim 45 m on) and every alien as placed (spots,
walkers' keys, riders on their cabins, flyers' keys), asks Blender (blender/nd_camera_plan.py) which are hidden by the
town and how near the lens comes to it, then prints readability per 0.5 s and per segment. Figures count from 12 px
at 1080p (lens and fov as the engine rides them); "readable" means 60 px or more (a 1.72 m alien within about 32 m at
the 52 degree lens). With --tiers it writes each figure's LOD tier (population.json schema: A seen within 40 m, B 40-150 m,
C only beyond, or never) and `visible` (max_px, min_dist_m, windows) from what this journey sees. CPU only.
"""
import json, math, os, subprocess, sys, bisect

HERE = os.path.dirname(os.path.abspath(__file__))
BL = "/Applications/Blender.app/Contents/MacOS/Blender"
PLAN = os.path.join(HERE, "nd_camera_plan.py")
DT = 0.25


def interp(keys, t):
    ts = [k["time"] for k in keys]
    i = bisect.bisect_right(ts, t)
    if i <= 0:
        return keys[0]["value"]
    if i >= len(keys):
        return keys[-1]["value"]
    a, b = keys[i - 1], keys[i]
    if a.get("interp") == "step":
        return a["value"]
    u = (t - a["time"]) / max(b["time"] - a["time"], 1e-9)
    va, vb = a["value"], b["value"]
    if isinstance(va, list):
        return [va[j] + (vb[j] - va[j]) * u for j in range(len(va))]
    return va + (vb - va) * u


class Ride:
    def __init__(self, scene, proj):
        sp = next(n for n in scene["nodes"] if n["name"] == "ride")["spline"]["points"]
        self.P = [p["position"] for p in sp]
        self.cum = [0.0]
        for a, b in zip(self.P, self.P[1:]):
            self.cum.append(self.cum[-1] + math.dist(a, b))
        self.L = self.cum[-1]
        tr = {t["target"]: t["keys"] for t in proj["timeline"]["tracks"]}
        self.T = tr["camera/splineT"]; self.F = tr["camera/fov"]
        self.ahead = proj["parameters"].get("camera/lookAhead", 45.0)

    def at_s(self, s):
        s = max(0.0, s)
        if s >= self.L:
            a, b = self.P[-2], self.P[-1]; d = math.dist(a, b)
            return [b[j] + (b[j] - a[j]) / d * (s - self.L) for j in range(3)]
        i = max(1, bisect.bisect_right(self.cum, s))
        a, b = self.P[i - 1], self.P[i]; u = (s - self.cum[i - 1]) / max(self.cum[i] - self.cum[i - 1], 1e-9)
        return [a[j] + (b[j] - a[j]) * u for j in range(3)]

    def cam(self, t):
        s = interp(self.T, t) * self.L
        return self.at_s(s), self.at_s(s + self.ahead), interp(self.F, t), s


def project(lens, aim, fov, p, h=1.72):
    f = [aim[j] - lens[j] for j in range(3)]; fl = math.sqrt(sum(v * v for v in f)); f = [v / fl for v in f]
    r = [-f[2], 0.0, f[0]]; rl = math.hypot(r[0], r[2]) or 1.0; r = [r[0] / rl, 0.0, r[2] / rl]
    # engine frame: y up; right = f x up = (-fz, 0, fx) normalised (sign irrelevant for |sx|)
    u = [r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]]
    v = [p[0] - lens[0], p[1] + h * 0.5 - lens[1], p[2] - lens[2]]
    d = sum(v[j] * f[j] for j in range(3))
    if d < 0.6:
        return 0.0, d
    foc = 540.0 / math.tan(math.radians(fov) / 2)
    sx = abs(sum(v[j] * r[j] for j in range(3)) / d * foc); sy = abs(sum(v[j] * u[j] for j in range(3)) / d * foc)
    if sx > 960 + 10 or sy > 540 + 10:
        return 0.0, d
    return h * foc / d, d


def roty(v, deg):
    a = math.radians(deg); c, s = math.cos(a), math.sin(a)
    return [v[0] * c + v[2] * s, v[1], -v[0] * s + v[2] * c]


def figures_at(scene, tracks, t, nodes_by):
    out = []
    for n in scene["nodes"]:
        if n.get("kind") != "gltf" or "aliens/" not in n.get("asset", ""):
            continue
        nm = n["name"]
        pk = tracks.get("nodes/%s/position" % nm)
        pos = interp(pk, t) if pk else n["position"]
        if n.get("parent", "").startswith("vehicle"):
            vn = n["parent"]; vp = interp(tracks["nodes/%s/position" % vn], t); vr = interp(tracks["nodes/%s/rotation" % vn], t)
            lp = roty(n["position"], vr[1])
            pos = [vp[j] + lp[j] for j in range(3)]
        out.append((nm, pos))
    return out


def segment(t):
    for name, a, b in SEGS:
        if a <= t < b:
            return name
    return SEGS[-1][0]


SEGS = [("opening", 0, 31), ("quay", 31, 44), ("garden+veil", 44, 56.85), ("reveal+end", 56.85, 70.2)]


def main():
    a = sys.argv[1:]
    opt = lambda n, d: a[a.index(n) + 1] if n in a else d
    root = os.path.abspath(opt("--root", os.path.join(HERE, "..")))
    tag = opt("--name", "it11")
    SCN = os.path.join(root, "scene")
    outd = opt("--out", os.path.join(root, "scene", "camera_eval"))   # scene/ is gitignored
    os.makedirs(outd, exist_ok=True)
    lay_p = os.path.join(root, "art", "layout.json")
    if "--waypoints" in a:
        lay = json.load(open(lay_p))
        lay["camera"]["waypoints"] = json.load(open(opt("--waypoints", "")))["waypoints"]
        json.dump(lay, open(lay_p, "w"), indent=1)
    if "--no-build" not in a:
        r = subprocess.run([sys.executable, os.path.join(root, "build.py"), "--out", SCN, "--name", tag], capture_output=True, text=True)
        if r.returncode:
            print(r.stderr[-3000:]); sys.exit(1)
    scene = json.load(open(os.path.join(SCN, tag + ".scene.json"))); proj = json.load(open(os.path.join(SCN, tag + ".json")))
    ride = Ride(scene, proj)
    tracks = {t["target"]: t["keys"] for t in proj["timeline"]["tracks"]}
    song = 70.2
    cams, figs, clear, meta = [], {}, [], {}
    t = 0.0
    while t <= song + 1e-6:
        lens, aim, fov, s = ride.cam(t)
        cams.append([round(t, 3), lens, aim, fov])
        lst = []
        for nm, pos in figures_at(scene, tracks, t, None):
            px, d = project(lens, aim, fov, pos)
            if px >= 12:
                lst.append([nm, [round(v, 3) for v in pos], round(px, 1)])
                meta.setdefault(nm, []).append((round(t, 2), round(d, 1)))
        figs["%.3f" % t] = lst
        t += DT
    t = 0.0
    while t <= song + 1e-6:
        clear.append([round(t, 2), ride.cam(t)[0]]); t += 0.1
    req = os.path.join(outd, "req_%s.json" % tag); out = os.path.join(outd, "vis_%s.json" % tag)
    json.dump({"cams": cams, "clear": clear, "figs": figs}, open(req, "w"))
    r = subprocess.run([BL, "-b", "--factory-startup", "-P", PLAN, "--", req, out, "--layout", lay_p,
                        "--cache", os.path.join(outd, "bvh_cache")], capture_output=True, text=True)
    if r.returncode or not os.path.exists(out):
        print(r.stdout[-3000:], r.stderr[-3000:]); sys.exit(1)
    res = json.load(open(out))
    # ---- report
    rows = []
    for c in cams:
        t = c[0]; v = res["vis"]["%.3f" % t]
        pxs = sorted((p for _, p in v), reverse=True)
        top = max(v, key=lambda x: x[1]) if v else ["-", 0]
        rows.append({"t": t, "n20": sum(p >= 20 for p in pxs), "n40": sum(p >= 40 for p in pxs), "n60": sum(p >= 60 for p in pxs),
                     "max": pxs[0] if pxs else 0.0, "top": top[0], "pos": c[1], "speed": 0.0})
    for i in range(len(rows)):
        j0, j1 = max(0, i - 1), min(len(rows) - 1, i + 1)
        rows[i]["speed"] = math.dist(rows[j0]["pos"], rows[j1]["pos"]) / max((rows[j1]["t"] - rows[j0]["t"]), 1e-6)
    cl = res["clear"]
    print("%-5s %6s %5s %4s %4s %4s %6s  %s" % ("t", "speed", "clr", "n20", "n40", "n60", "max", "top"))
    for i in range(0, len(rows), 2):
        rw = rows[i]
        c_near = min(d for tt, d in cl if abs(tt - rw["t"]) <= 0.26)
        print("%5.1f %6.1f %5.1f %4d %4d %4d %6.0f  %s" % (rw["t"], rw["speed"], c_near, rw["n20"], rw["n40"], rw["n60"], rw["max"], rw["top"]))
    print("\nsegment        sec  mean n20  mean n60  sec with a figure >=60 px  min clearance (m)")
    summ = {}
    for name, a0, b0 in SEGS:
        rs = [r for r in rows if a0 <= r["t"] < b0]
        cs = [d for tt, d in cl if a0 <= tt < b0]
        s60 = sum(DT for r in rs if r["n60"] > 0)
        summ[name] = {"n20": round(sum(r["n20"] for r in rs) / len(rs), 1), "n60": round(sum(r["n60"] for r in rs) / len(rs), 2),
                      "sec60": s60, "dur": b0 - a0, "clear": min(cs)}
        print("%-12s %5.1f %9.1f %9.2f %10.1f %24.2f" % (name, b0 - a0, summ[name]["n20"], summ[name]["n60"], s60, min(cs)))
    worst = sorted(cl, key=lambda x: x[1])[:5]
    print("nearest surfaces:", worst)
    json.dump({"rows": rows, "summary": summ, "clear": cl, "meta": {k: v for k, v in meta.items()}}, open(os.path.join(outd, "rep_%s.json" % tag), "w"))
    if "--tiers" in a:
        tiers(root, res, meta)


def node_owner(nm):
    """Scene node name -> (population list, id)."""
    if nm.startswith("alien_ride_"):
        return "riders", nm[len("alien_"):]
    if nm.startswith("alien_"):
        return "spots", nm[len("alien_"):]
    if nm.startswith("flyer_"):
        return "flyers", nm[len("flyer_"):]
    for pre in ("walkA_", "walk_"):
        if nm.startswith(pre):
            return "paths", nm[len(pre):].rsplit("_", 1)[0]
    return None, None


def tiers(root, res, meta):
    """Each figure's tier and visibility from this journey (occluded samples left out)."""
    pp = os.path.join(root, "art", "population.json")
    pop = json.load(open(pp))
    seen = {}
    for ts, lst in res["vis"].items():
        t = float(ts)
        for nm, px in lst:
            d = dict(meta.get(nm, [])).get(round(t, 2))
            if d is None:
                continue
            kind, fid = node_owner(nm)
            if kind is None:
                continue
            v = seen.setdefault((kind, fid), {"max_px": 0.0, "min_dist_m": 1e9, "ts": []})
            v["max_px"] = max(v["max_px"], px); v["min_dist_m"] = min(v["min_dist_m"], d); v["ts"].append(t)
    changes = {}
    for kind in ("spots", "paths", "riders"):
        for f in pop.get(kind, []):
            v = seen.get((kind, f["id"]))
            new = "C" if v is None else ("A" if v["min_dist_m"] < 40 else ("B" if v["min_dist_m"] < 150 else "C"))
            if f.get("tier") != new:
                changes["%s->%s" % (f.get("tier"), new)] = changes.get("%s->%s" % (f.get("tier"), new), 0) + 1
            f["tier"] = new
            if kind != "riders":
                wins = []
                for t in sorted(v["ts"]) if v else []:
                    if wins and t - wins[-1][1] <= DT + 1e-6:
                        wins[-1][1] = t
                    else:
                        wins.append([t, t])
                f["visible"] = {"max_px": round(v["max_px"], 1) if v else 0.0, "min_dist_m": round(v["min_dist_m"], 1) if v else None,
                                "windows": wins, "samples": len(v["ts"]) if v else 0, "journey": "it11"}
    json.dump(pop, open(pp, "w"), indent=None, separators=(",", ":"))
    n = {k: sum(1 for kind in ("spots", "paths", "riders") for f in pop.get(kind, []) if f["tier"] == k) for k in "ABC"}
    print("tiers:", n, "changes:", changes)


if __name__ == "__main__":
    main()
