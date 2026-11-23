"""Neon Dreams it18 (owner msg 271): no aimless action loops, no figures clipping through each other.

Plain Python (no Blender). Run from anywhere with the example folder as --nd (default: this file's parent):

    /usr/bin/python3 blender/nd_it18.py [--nd DIR] [--write]

It edits art/population.json in place with --write (else prints what it would do), and appends one `edits` entry.

1. Clips. The pack's short action clips -- Button_push (1.47 s), Take_from_table (1.63 s), Take_from_floor (2.3 s) and
   their mirrors -- looped for the whole song read as a figure pressing a button every 1.5 s for nothing (the owner's
   mark at 40.4 s was pop_0083, a sign-watcher on the Quay on Button_push_m). Every such loop becomes a purposeful
   11.2 s idle (watching, hands on hips, talking, looking up at the sign, crouched tending); where the figure stands at a
   real fixture (activity use / work / browse: a market stall, the station's counter, the Condensery's panels) or tends
   a bed (tend), the action stays as an occasional single press / pick-up every 7-13 s (two cues: the action from its
   first frame, the idle again one action length later), never in a loop. Walkers' shopfront stops on Button_push /
   Take_from_table become a look in the window. The stock fallbacks (`anim_clip`, cue `stock`) that name those clips
   become Idle / Idle_turn.
2. Separation (blender/nd_separation.py). Measured on the built cast:
   - two walkers on one pingpong path always meet head-on (any phase): each path with 2+ walkers is split into one
     path per walker on parallel lanes 0.8 m apart (keep-right lanes);
   - a standing figure a walker's lane passes through, or a flyer lands in, is moved clear of it (perpendicular,
     limit + 0.15 m);
   - walkers of different paths that meet at a crossing are re-timed: a whole-beat shift of the walker's phase (its
     lap is whole beats, so a shift moves its whole timetable, clip cues included), searched for the fewest close
     instants against everyone else, greedy, a few rounds.
"""
import argparse
import bisect
import copy
import json
import math
import os
import random
import re
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ACTION = {"Button_push", "Take_from_table", "Take_from_floor"}
ACTION_LEN = {"Button_push": 1.467, "Take_from_table": 1.633, "Take_from_floor": 2.3}
FIXTURE = {"use", "work", "browse"}
PALETTE = {   # base loop for a former action figure, by activity: clip family -> weight
    "use": {"ND_Watch": 3, "ND_Hips": 2, "ND_Stand": 2},
    "work": {"ND_Watch": 2, "ND_Hips": 2, "ND_Stand": 2},
    "browse": {"ND_Watch": 3, "ND_Hips": 2, "ND_Talk_a": 1.5},
    "tend": {"ND_Squat": 5, "ND_Hips": 1},
    "fish": {"ND_Fish": 1},
    "look_sign": {"ND_Look_up": 3, "ND_Watch": 2, "ND_Hips": 1},
    "_": {"ND_Watch": 2, "ND_Hips": 2, "ND_Talk_a": 1.5, "ND_Look_up": 1},
}
HAS_M = {"ND_Watch", "ND_Stand", "ND_Talk_a", "ND_Talk_b", "ND_Look_up", "ND_Squat", "ND_Fish", "ND_Lean"}
STOCK_OF = {"ND_Watch": "Idle", "ND_Hips": "Idle_turn", "ND_Stand": "Idle", "ND_Talk_a": "Idle_turn",
            "ND_Look_up": "Idle_turn", "ND_Squat": "Idle", "ND_Fish": "Idle"}
STOCK_SAFE = {"Button_push": "Idle_turn", "Take_from_table": "Idle_turn", "Take_from_floor": "Idle"}
LONG = 11.202   # the authored idles' length (16 beats)
STOP_LOOK = ["ND_Watch", "ND_Hips", "ND_Look_up", "ND_Watch_m"]
LANE_GAP = 0.8


def base(c):
    return c[:-2] if c and c.endswith("_m") else c


def fam_mirror(c, mirror):
    return c + "_m" if mirror and c in HAS_M else c


# ---------------------------------------------------------------------------------------------------------- clips
def active_clip(sp, t):
    c = sp.get("nd_clip")
    for q in sorted(sp.get("nd_cues") or [], key=lambda q: q["t"]):
        if q["t"] <= t:
            c = q["clip"]
    return c


def clip_pass(pop, rng, song, log):
    spots = pop["spots"]
    for sp in spots:
        c = sp.get("nd_clip")
        if base(c) not in ACTION:
            continue
        act = sp.get("activity", "_")
        mirror = c.endswith("_m")
        near = [base(o.get("nd_clip") or "") for o in spots if o is not sp and math.dist(o["pos"], sp["pos"]) < 9.0]
        pal = dict(PALETTE.get(act, PALETTE["_"]))
        for k in list(pal):
            if k in near and len(pal) > 1:
                pal[k] *= 0.15
        fams, w = zip(*pal.items())
        new = fam_mirror(rng.choices(fams, w)[0], mirror)
        old_cues = sorted(sp.get("nd_cues") or [], key=lambda q: q["t"])
        rec = {"id": sp["id"], "activity": act, "district": sp.get("district"), "from": c, "to": new, "presses": 0}
        sp["nd_clip"] = new
        sp["nd_rate"] = round(rng.uniform(1.0, 1.1), 3)
        sp["nd_phase"] = round(rng.uniform(0.0, LONG), 4)
        sp["nd_grid"] = "free"
        sp["anim_clip"] = STOCK_OF.get(base(new), "Idle")
        # old cues that returned to the action loop return to the new idle
        for q in old_cues:
            if base(q["clip"]) in ACTION:
                q.update(clip=new, rate=sp["nd_rate"], offset=round(rng.uniform(0.0, LONG), 3),
                         stock=sp["anim_clip"])
        if act in FIXTURE or act == "tend":
            press = c if act in FIXTURE else ("Take_from_floor_m" if mirror else "Take_from_floor")
            L = ACTION_LEN[base(press)]
            t = rng.uniform(2.0, 9.0)
            cues = list(old_cues)
            while t + L + 0.5 < song:
                if not any(t - 1.0 < q["t"] < t + L + 1.0 for q in old_cues) and \
                        base(active_clip({"nd_clip": new, "nd_cues": old_cues}, t)) == base(new):
                    cues.append({"t": round(t, 3), "clip": press, "rate": 1.0, "offset": 0.0, "blend": 0.3,
                                 "stock": STOCK_SAFE[base(press)], "why": "msg 271: one press, not a loop"})
                    cues.append({"t": round(t + L - 0.1, 3), "clip": new, "rate": sp["nd_rate"],
                                 "offset": round(rng.uniform(0.0, LONG), 3), "blend": 0.45, "stock": sp["anim_clip"]})
                    rec["presses"] += 1
                t += rng.uniform(7.0, 13.0)
            sp["nd_cues"] = sorted(cues, key=lambda q: q["t"])
        elif old_cues:
            sp["nd_cues"] = old_cues
        log["spots"].append(rec)
    # stock fallbacks that name an action clip (only read without the -nd cast files)
    for sp in spots + pop.get("riders", []):
        if sp.get("anim_clip") in STOCK_SAFE:
            sp["anim_clip"] = STOCK_SAFE[sp["anim_clip"]]
            log["fallbacks"] += 1
        for q in sp.get("nd_cues") or []:
            if q.get("stock") in STOCK_SAFE:
                q["stock"] = STOCK_SAFE[q["stock"]]
                log["fallbacks"] += 1
    for rd in pop.get("riders", []):
        if base(rd.get("nd_clip")) in ACTION:
            rd["nd_clip"] = "ND_Watch"
            log["riders"].append(rd["id"])
    for p in pop["paths"]:
        if base(p.get("stop_clip")) in ACTION:
            new = rng.choice(STOP_LOOK)
            log["stops"].append({"id": p["id"], "from": p["stop_clip"], "to": new})
            p["stop_clip"] = new
        pc = p.get("pause_clips") or []
        for i, c in enumerate(pc):
            if base(c) in ACTION:
                pc[i] = rng.choice(STOP_LOOK)
                log["stops"].append({"id": p["id"], "pause": c, "to": pc[i]})


# ---------------------------------------------------------------------------------------------------------- lanes
def offset_polyline(pts, d):
    """The polyline moved d metres to the right of its direction (horizontal), mitred at its vertices."""
    out = []
    n = len(pts)
    for i in range(n):
        nrm = []
        segs = []
        if i > 0:
            segs.append((pts[i - 1], pts[i]))
        if i < n - 1:
            segs.append((pts[i], pts[i + 1]))
        for a, b in segs:
            dx, dz = b[0] - a[0], b[2] - a[2]
            L = math.hypot(dx, dz) or 1.0
            nrm.append((-dz / L, dx / L))   # right of travel in the engine's frame (x east, z south: yaw = atan2(x, z))
        nx, nz = sum(v[0] for v in nrm), sum(v[1] for v in nrm)
        L = math.hypot(nx, nz) or 1.0
        nx, nz = nx / L, nz / L
        cosh = max(0.5, nx * nrm[0][0] + nz * nrm[0][1])
        out.append([round(pts[i][0] + nx * d / cosh, 3), pts[i][1], round(pts[i][2] + nz * d / cosh, 3)])
    return out


def path_samples(pts):
    """A walker's ground-check samples along a polyline (every 0.3 m, facing along it), as build.py makes them."""
    out = []
    for a, b in zip(pts, pts[1:]):
        L = math.dist(a, b)
        yaw = round(math.degrees(math.atan2(b[0] - a[0], b[2] - a[2])), 1)
        for k in range(int(L / 0.3) + 1):
            u = min(1.0, k * 0.3 / L) if L > 1e-6 else 0.0
            out.append([[round(a[i] + (b[i] - a[i]) * u, 3) for i in range(3)], yaw])
    out.append([list(pts[-1]), out[-1][1] if out else 0.0])
    return out


class Ground:
    """blender/nd_ground_check.py on candidate places, pass/fail only (its walker re-laying, which takes minutes per
    failing lane, is switched off in a temporary copy; spots still get its nearest-standing-point search)."""

    def __init__(self, nd, work):
        src = open(os.path.join(nd, "blender", "nd_ground_check.py")).read()
        src = src.replace('    if fails and f["kind"] == "walker" and f.get("path"):', '    if False:')
        src = src.replace("HERE = os.path.dirname(os.path.abspath(__file__))",
                          "HERE = %r" % os.path.join(os.path.abspath(nd), "blender"))
        os.makedirs(work, exist_ok=True)
        self.script = os.path.join(work, "nd_ground_probe.py")
        open(self.script, "w").write(src)
        self.work = work
        self.n = 0

    def probe(self, figs):
        import subprocess
        self.n += 1
        a = os.path.join(self.work, "probe%d_in.json" % self.n)
        b = os.path.join(self.work, "probe%d.json" % self.n)
        json.dump({"figures": figs}, open(a, "w"))
        subprocess.run([BLENDER, "-b", "--factory-startup", "-P", self.script, "--", a, b], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        rep = json.load(open(b))
        print("ground probe %d: %d places, %d fail" % (self.n, rep["summary"]["figures"], rep["summary"]["fail"]))
        return {r["id"]: r for r in rep["figures"]}


BLENDER = "/Applications/Blender.app/Contents/MacOS/Blender"
CONF2 = [(-0.4, 0.4), (-0.35, 0.35), (-0.3, 0.3), (0.0, 0.8), (-0.8, 0.0), (0.0, 0.7), (-0.7, 0.0), (0.0, 0.6),
         (-0.6, 0.0)]


def side_of(pt, pts):
    """+1 when pt lies on offset_polyline's positive side of pts, else -1."""
    best, sgn = 1e9, 1
    for a, b in zip(pts, pts[1:]):
        dx, dz = b[0] - a[0], b[2] - a[2]
        L2 = dx * dx + dz * dz or 1e-9
        u = max(0.0, min(1.0, ((pt[0] - a[0]) * dx + (pt[2] - a[2]) * dz) / L2))
        ex, ez = pt[0] - a[0] - u * dx, pt[2] - a[2] - u * dz
        d = math.hypot(ex, ez)
        if d < best:
            L = math.sqrt(L2)
            best, sgn = d, (1 if ex * (-dz / L) + ez * (dx / L) >= 0 else -1)
    return sgn


def lane_pass(pop, gfix, log, ground):
    """Two walkers on one pingpong path meet head-on whatever their phases: one path per walker, on lanes. The lane
    set is the first of CONF2 on which every lane stands (ground probe); none -> one walker stays on the line."""
    split, cands = [], []
    for p in pop["paths"]:
        n = int(p.get("walkers", 1))
        if n < 2:
            continue
        pts = p["points"]
        fp = gfix.get("paths", {}).get(p["id"])
        if fp is not None and fp.get("from") and len(fp["from"]) == len(pts) and \
                all(math.dist(a, b) < 0.01 for a, b in zip(fp["from"], pts)):
            pts = fp["points"]   # the ground check's re-laid path, baked in (the lanes are new paths)
        confs = CONF2 if n == 2 else [tuple((w - (n - 1) / 2.0) * g for w in range(n)) for g in (0.8, 0.7, 0.6)]
        split.append((p, pts, n, confs))
        for k, conf in enumerate(confs):
            for w, d in enumerate(conf):
                cands.append({"id": "%s|%d|%d" % (p["id"], k, w), "kind": "walker", "bob": p.get("bob"),
                              "samples": path_samples(offset_polyline(pts, d))})
    res = ground.probe(cands)
    lanes_of = {}
    out = []
    for p in pop["paths"]:
        hit = next((x for x in split if x[0] is p), None)
        if hit is None:
            out.append(p)
            continue
        _, pts, n, confs = hit
        k = next((k for k in range(len(confs)) if all(res["%s|%d|%d" % (p["id"], k, w)]["ok"] for w in range(n))), None)
        conf = confs[k] if k is not None else (0.0,)
        lanes_of[p["id"]] = conf
        for w, d in enumerate(conf):
            q = copy.deepcopy(p)
            q["id"] = p["id"] if w == 0 else "%s_l%d" % (p["id"], w)
            q["points"] = offset_polyline(pts, d) if d else [list(v) for v in pts]
            q["walkers"] = 1
            for key in ("phases_s", "cast", "pause_clips"):
                if q.get(key):
                    q[key] = [p[key][w % len(p[key])]]
            q["lane_m"] = round(d, 2)
            q["lane_of"] = p["id"]
            out.append(q)
        log["lanes"].append({"path": p["id"], "walkers": n, "lanes_m": list(conf),
                             "note": None if k is not None else "no lane pair stands here: one walker on the line"})
    # msg 125's Quay walkers walk a host path's line 0.5 m to one side: outside the host's lanes now (0.8 m clear of
    # the outermost lane on their side; a second one on the same side one lane further out), the nearest that stands
    hosts = {p["id"]: p for p in pop["paths"]}
    side_count = defaultdict(int)
    qcands, plan = [], []
    for q in out:
        m = re.search(r"on (walk_\d+), lane ([+-]?\d+(?:\.\d+)?) m", q.get("note", ""))
        if not q["id"].startswith("qw_") or not m or m.group(1) not in hosts:
            continue
        fp = gfix.get("paths", {}).get(q["id"])
        if fp is not None and fp.get("from") and len(fp["from"]) == len(q["points"]) and \
                all(math.dist(a, b) < 0.01 for a, b in zip(fp["from"], q["points"])):
            q["points"] = fp["points"]
        hp = hosts[m.group(1)]["points"]
        sg = side_of(q["points"][len(q["points"]) // 2], hp)
        conf = lanes_of.get(m.group(1), (0.0,))
        outer = max([0.0] + [d * sg for d in conf])
        key = (m.group(1), sg)
        target = outer + LANE_GAP * (1 + side_count[key])
        side_count[key] += 1
        now = polyline_dist(q["points"][len(q["points"]) // 2], hp)
        opts = [target + e for e in (0.0, -0.1, -0.2, 0.2, 0.4)]
        plan.append((q, sg, now, opts, m.group(1)))
        for i, t in enumerate(opts):
            qcands.append({"id": "%s|%d" % (q["id"], i), "kind": "walker", "bob": q.get("bob"),
                           "samples": path_samples(offset_polyline(q["points"], sg * (t - now)))})
    if qcands:
        res = ground.probe(qcands)
        for q, sg, now, opts, host in plan:
            i = next((i for i in range(len(opts)) if res["%s|%d" % (q["id"], i)]["ok"]), None)
            if i is None:
                log["lanes"].append({"path": q["id"], "outside_of": host, "note": "no outer lane stands: kept"})
                continue
            q["points"] = offset_polyline(q["points"], sg * (opts[i] - now))
            q["lane_m"] = round(sg * opts[i], 2)
            log["lanes"].append({"path": q["id"], "outside_of": host, "lane_m": round(sg * opts[i], 2)})
    pop["paths"] = out


def polyline_dist(pt, pts):
    best = 1e9
    for a, b in zip(pts, pts[1:]):
        dx, dz = b[0] - a[0], b[2] - a[2]
        L2 = dx * dx + dz * dz or 1e-9
        u = max(0.0, min(1.0, ((pt[0] - a[0]) * dx + (pt[2] - a[2]) * dz) / L2))
        best = min(best, math.hypot(pt[0] - a[0] - u * dx, pt[2] - a[2] - u * dz))
    return best


# ---------------------------------------------------------------------------------------------------------- eval
def load_build(nd):
    sys.path.insert(0, nd)
    sys.path.insert(0, os.path.join(nd, "blender"))
    import build as B   # noqa: E402
    import nd_separation as S   # noqa: E402
    return B, S


def evaluate(B, S, pop):
    B.POPULATION = pop
    nodes, lights, tracks = B.cast_nodes()
    figs = S.figures_from(nodes, tracks)
    return figs, S.check_figures(figs, B.SONG)


def walker_track(B, S, pop, p):
    """One path's walker as nd_cast lays it (its timetable from phases_s), built alone: a figure for nd_separation."""
    mini = {k: v for k, v in pop.items() if k not in ("spots", "groups", "paths", "riders", "flyers")}
    mini.update(spots=[], groups=[], paths=[p], riders=[], flyers=[])
    B.POPULATION = mini
    nodes, lights, tracks = B.cast_nodes()
    figs = [f for f in S.figures_from(nodes, tracks) if f["kind"] == "walker"]
    B.POPULATION = pop
    return figs[0] if figs else None


def retime_pass(B, S, pop, figs, rep, log, rounds=4, tries=80):
    """Walkers that meet other walkers / standing figures at crossings: their phase moved by whole beats (each
    candidate timetable rebuilt by nd_cast), the one with the fewest / shallowest close instants kept. Greedy."""
    beat = B.AU.BEAT
    song = B.SONG
    step = 0.1
    times = [round(i * step, 3) for i in range(int(song / step) + 1)]
    cell = 2.0
    path_of = {}
    for p in pop["paths"]:
        for pre in ("walkA_", "walk_"):
            path_of[pre + p["id"] + "_0"] = p

    def same_floor(a, b, pa, pb):
        return (a["bob"] or None) == (b["bob"] or None) and (a["parent"] or None) == (b["parent"] or None) and \
            abs(pa[1] - pb[1]) < 1.4

    for rnd in range(rounds):
        by = {f["id"]: f for f in figs}
        bad = defaultdict(float)
        for w in rep["violations"]:
            for x, y in ((w["a"], w["b"]), (w["b"], w["a"])):
                if by[x]["kind"] == "walker" and x in path_of:
                    bad[x] += 1 + (2.0 if by[y]["kind"] != "walker" else 0.0)
        if not bad:
            break
        grids = []
        for t in times:
            g = defaultdict(list)
            for f in figs:
                if f["parent"]:
                    continue
                q = f["at"](t) if "at" in f else f["pos"]
                g[(int(math.floor(q[0] / cell)), int(math.floor(q[2] / cell)))].append((f, q))
            grids.append(g)

        def cost(f, skip):
            c = 0.0
            for i, t in enumerate(times):
                pa = f["at"](t)
                gx, gz = int(math.floor(pa[0] / cell)), int(math.floor(pa[2] / cell))
                for dx in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        for b, pb in grids[i].get((gx + dx, gz + dz), []):
                            if b["id"] == skip or not same_floor(f, b, pa, pb):
                                continue
                            d = math.hypot(pa[0] - pb[0], pa[2] - pb[2])
                            lim = 0.62 * 0.5 * (f["scale"] + b["scale"]) + 0.15
                            if d < lim:
                                c += (lim - d) + 0.2
            return c

        moved = 0
        for wid, _ in sorted(bad.items(), key=lambda kv: -kv[1]):
            p = path_of[wid]
            ph0 = (p.get("phases_s") or [0.0])[0]
            f0 = walker_track(B, S, pop, p)
            if f0 is None:
                continue
            best = (cost(f0, wid), 0, f0)
            if best[0] == 0:
                continue
            for k in list(range(1, tries // 2 + 1)) + list(range(-1, -tries // 2 - 1, -1)):
                p["phases_s"] = [round(ph0 + k * beat, 4)]
                f = walker_track(B, S, pop, p)
                c = cost(f, wid)
                if c < best[0] - 1e-6:
                    best = (c, k, f)
                if c == 0:
                    break
            p["phases_s"] = [round(ph0 + best[1] * beat, 4)]
            if best[1]:
                moved += 1
                p["it18_shift_beats"] = p.get("it18_shift_beats", 0) + best[1]
                f = best[2]
                for i, t in enumerate(times):   # its new places in the grids
                    for lst in grids[i].values():
                        lst[:] = [(b, pb) for b, pb in lst if b["id"] != wid]
                    q = f["at"](t)
                    grids[i][(int(math.floor(q[0] / cell)), int(math.floor(q[2] / cell)))].append((f, q))
        log["retime_rounds"].append({"walkers_considered": len(bad), "shifted": moved})
        figs, rep = evaluate(B, S, pop)
        print("retime round %d: %d of %d shifted, %s" % (rnd + 1, moved, len(bad), json.dumps(rep["summary"])))
        if not moved:
            break
    return figs, rep


def nudge_spots(pop, gfix, figs, rep, log, song=70.18, reach=3, ground=None):
    """Standing figures a walker lane or a landing goes through: moved to the nearest place (rings of 0.25 m out to
    3 m round where they stood) that every mover passes at least its limit + 0.15 m from, all song, and that keeps
    0.9 m from every other standing figure."""
    by = {f["id"]: f for f in figs}
    spots = {"alien_" + s["id"]: s for s in pop["spots"]}
    todo = []
    for w in rep["violations"]:
        for x, y in ((w["a"], w["b"]), (w["b"], w["a"])):
            if by[x]["kind"] == "spot" and x in spots and x not in todo:
                todo.append(x)
                break
    if not todo:
        return 0
    cell = 1.0
    trail = defaultdict(list)   # every mover's places through the song
    for f in figs:
        if "at" not in f or f["parent"]:
            continue
        for i in range(int(song / 0.1) + 1):
            q = f["at"](i * 0.1)
            trail[(int(math.floor(q[0] / cell)), int(math.floor(q[2] / cell)))].append((f, q))
    static = [f for f in figs if "at" not in f and not f["parent"]]
    n = 0
    want = []
    for sid in todo:
        st, sp = by[sid], spots[sid]
        m = gfix.get("spots", {}).get(sp["id"])
        if m and m.get("from") and math.dist(m["from"], sp["pos"]) < 0.01:
            sp["pos"], sp["bob"] = m["pos"], m.get("bob")   # bake the ground fix first
        p0 = st["pos"]

        def ok(c):
            gx, gz = int(math.floor(c[0] / cell)), int(math.floor(c[2] / cell))
            for dx in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    for f, q in trail.get((gx + dx, gz + dz), []):
                        if (f["bob"] or None) != (st["bob"] or None) or abs(q[1] - c[1]) > 1.4:
                            continue
                        if math.hypot(q[0] - c[0], q[2] - c[2]) < 0.62 * 0.5 * (f["scale"] + st["scale"]) + 0.15:
                            return False
            for o in static:
                if o["id"] != sid and (o["bob"] or None) == (st["bob"] or None) and abs(o["pos"][1] - c[1]) < 1.4 \
                        and math.hypot(o["pos"][0] - c[0], o["pos"][2] - c[2]) < 0.9:
                    return False
            return True
        found = []
        for r in [0.25 * k for k in range(1, 4 * reach + 1)]:
            for k in range(24):
                a = 2 * math.pi * k / 24
                c = [round(p0[0] + r * math.cos(a), 3), p0[1], round(p0[2] + r * math.sin(a), 3)]
                if ok(c):
                    found.append(c)
            if len(found) >= 12:
                break
        want.append((sid, sp, st, found[:12]))
    if ground is not None and want:
        res = ground.probe([{"id": "%s|%d" % (sid, i), "kind": "spot", "bob": st["bob"],
                             "samples": [[c, sp.get("facing_deg", 0.0)]]}
                            for sid, sp, st, cs in want for i, c in enumerate(cs)])
    for sid, sp, st, cs in want:
        pick = next((c for i, c in enumerate(cs) if ground is None or res["%s|%d" % (sid, i)]["ok"]), None)
        if pick is None:
            log["spot_moves"].append({"id": sp["id"], "from": sp["pos"], "to": None,
                                      "why": "no clear standing place within %d m" % reach})
            continue
        sp.setdefault("pos_it17", sp["pos"])
        log["spot_moves"].append({"id": sp["id"], "from": sp["pos"], "to": pick})
        sp["pos"] = pick
        st["pos"] = pick
        n += 1
    return n


def drop_pass(pop, figs, rep, log):
    by = {f["id"]: f for f in figs}
    paths = {}
    for p in pop["paths"]:
        for pre in ("walkA_", "walk_"):
            paths[pre + p["id"] + "_0"] = p
    spots = {"alien_" + s["id"]: s for s in pop["spots"]}

    def seen(fid):
        x = paths.get(fid) or spots.get(fid) or {}
        return (x.get("visible") or {}).get("max_px", 0.0) + (1000 if x.get("tier") == "A" else 0)
    gone = []
    for w in rep["violations"]:
        if w["a"] in gone or w["b"] in gone:
            continue
        cand = [x for x in (w["a"], w["b"]) if x in paths or (x in spots and spots[x].get("tier") == "C")]
        if not cand:
            continue
        x = min(cand, key=seen)
        gone.append(x)
    for x in gone:
        if x in paths:
            pop["paths"].remove(paths[x])
            log.setdefault("dropped", []).append({"path": paths[x]["id"], "why": "no clear timetable"})
        else:
            pop["spots"].remove(spots[x])
            log.setdefault("dropped", []).append({"spot": spots[x]["id"], "why": "no clear place"})
    return gone


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nd", default=os.path.dirname(HERE))
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--work", default="/tmp/nd_it18", help="the ground probes' scratch folder")
    a = ap.parse_args()
    art = os.path.join(a.nd, "art")
    pop = json.load(open(os.path.join(art, "population.json")))
    gfix = json.load(open(os.path.join(art, "ground_fixes.json")))
    B, S = load_build(a.nd)
    rng = random.Random(271)
    log = {"it": 18, "msg": 271, "by": "art", "script": "blender/nd_it18.py", "spots": [], "riders": [], "stops": [],
           "fallbacks": 0, "lanes": [], "spot_moves": [], "retime_rounds": []}
    figs, rep0 = evaluate(B, S, pop)
    print("before:", json.dumps(rep0["summary"]))
    clip_pass(pop, rng, B.SONG, log)
    print("clips: %d spots, %d riders, %d stops, %d fallbacks" % (len(log["spots"]), len(log["riders"]),
                                                                  len(log["stops"]), log["fallbacks"]))
    ground = Ground(a.nd, a.work)
    lane_pass(pop, gfix, log, ground)
    figs, rep = evaluate(B, S, pop)
    print("lanes (%d paths split):" % len(log["lanes"]), json.dumps(rep["summary"]))
    for k in range(3):
        if not nudge_spots(pop, gfix, figs, rep, log, B.SONG, ground=ground):
            break
        figs, rep = evaluate(B, S, pop)
        print("spot nudge %d:" % (k + 1), json.dumps(rep["summary"]))
    figs, rep = retime_pass(B, S, pop, figs, rep, log)
    for k in range(2):
        if not nudge_spots(pop, gfix, figs, rep, log, B.SONG, ground=ground):
            break
        figs, rep = evaluate(B, S, pop)
        print("spot nudge after retime %d:" % (k + 1), json.dumps(rep["summary"]))
    if rep["violations"]:   # the stubborn ones: a timetable anywhere in the song, places up to 5 m away
        figs, rep = retime_pass(B, S, pop, figs, rep, log, rounds=2, tries=190)
        if nudge_spots(pop, gfix, figs, rep, log, B.SONG, reach=5, ground=ground):
            figs, rep = evaluate(B, S, pop)
    for k in range(3):   # last resort: the less-seen walker of a pair (or an unseen standing figure) leaves
        if not rep["violations"]:
            break
        drop = drop_pass(pop, figs, rep, log)
        figs, rep = evaluate(B, S, pop)
        print("dropped %s:" % drop, json.dumps(rep["summary"]))
    print("after:", json.dumps(rep["summary"]))
    for w in rep["violations"]:
        print("  left: %s %s %.2f < %.2f t %s" % (w["a"], w["b"], w["min_m"], w["limit_m"], w["t"]))
    log["separation"] = {"before": rep0["summary"], "after": rep["summary"]}
    log["what"] = ("msg 271: action clips (Button_push / Take_from_table / Take_from_floor) no longer loop -- purposeful "
                   "idles, single presses at real fixtures; 2-walker paths split into 0.8 m lanes; standing figures "
                   "moved off walker lanes / landings; walkers re-timed by whole beats at crossings "
                   "(blender/nd_separation.py)")
    pop["edits"].append(log)
    if a.write:
        json.dump(pop, open(os.path.join(art, "population.json"), "w"), indent=1)
        print("wrote", os.path.join(art, "population.json"))


if __name__ == "__main__":
    main()
