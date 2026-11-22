#!/usr/bin/env python3
"""Neon Dreams, iteration 13 (owner msgs 178, 180, 188): the Veil's faces blended; the end's rocks solid; dark sky behind the band.

    python3 blender/nd_clouds13.py [--root examples/neon-dreams] [--base-dir DIR] [--out FILE]
                                                             (plain Python, no Blender; idempotent)

Edits ROOT/art/cloud_design.json ONLY. It is a pure function of iteration 12's art data (git BASE below: its
cloud_design.json and the camera of its layout.json; --base-dir reads the two files from a folder instead), so a second
run gives the same file and a later camera edit in layout.json does not move a lobe.

msg 178 (39.8 s, the cloud right of and over the band: "a little too blobby still, need a bit more blending"; msg 105
"too blobby", msg 149 "more blended with layers of fog"). The formations are the VEIL's outside (mass `veil`, since
it12b only 250 m long and 100-250 m from the Quay's end and the garden) and the rim wraps of the two isles beside it.
What made them blobs, measured in AV Gen on the it12 build (work/art/it13):
  * every lobe outside the rooms was 30-75 m, round or a flattened slab, at the FULL erosion share. At full share the
    renderer replaces the authored soft edge by a crisp one (1.4 m at a crown, ADR-2115), and at 140-250 m the march's
    footprint has already faded the fine erosion octave: what is left is a few smooth convex swellings with a hard
    outline, each lit as one body;
  * the flat `bank` slabs are seen from below from the Quay and the garden: smooth undersides, a row of puffs over the
    roofs at 35-38 s;
  * the isles' rim wraps are 8-10 m lobes at the seat's 0.3 share: smooth eggs on the rock (44-47 s).
What blends in this renderer and what does not (each step a still against the it12 frame, work/art/it13/av):
  * a lobe at a PART share of the erosion keeps a soft band (edge = mix(soft x r, crisp, share)), and the billows
    still bend its surface: at 0.65 an edge wanders by up to 14 m and fades over 10-12 m;
  * at a LOW share (0.15-0.45, tried first) a translucent lobe's iso-surfaces stay round, the shape noise (55 m,
    slower than the lobe) cuts its thin density off along one of them, and a clean circular arc stands in the sky;
  * a translucent lobe that is SMALL (under about 40 m in plan) is only dimmed by that noise: a clean ellipse (three
    thin sheets per slab, and soft tufts on an isle's rim: both tried here and dropped);
  * more, smaller heads on a body (the brief's first idea; 4-7 of 0.3-0.5 r, and 7-14 m ones on those) are smaller
    smooth balls with crisp outlines at the full share and soft ovals against the sky at a part share: dropped.
So the hierarchy is of kinds, not of ball sizes:
  1. BODIES (the round lobes of 30-67 m): the lobe itself becomes a torn ENVELOPE (up to 1.15 r, density 0.52, share
     0.65, soft 0.95) round a solid heart of 0.62 r.
  2. MIST between them: one big thin lobe round each body (density 0.42), and a bridge across every saddle between
     neighbouring bodies, so heads run into each other and down into the sea. None reaches the garden, the band or
     the lens's path, and none lies on a line of sight from the lens to a rock island's body (a translucent lobe in
     front of a rock is what made the end's rocks look like glass: msg 180).
  3. STRATA: every flat slab that is not a roof over the rooms, at 0.62 of its density and the 0.65 share: layers of
     fog with sky between, not a puck with a smooth underside.
  4. One MIST SHEET of the engine's own kind (ADR-2119 `mist_layers`) at the bank's shoulder height: the thin strands
     between the masses. It is the one costly part (see the coordination note): `--no-sheet` leaves it out.
  5. The rooms' walls and roof slabs keep their bodies; their skin is softer (share 0.55, soft 0.8).
  6. The rim wraps of isle_exit and isle_lampwick: no lobes at the rim, a thin wisp zone instead.
  7. The renderer's lobe bins (64 to a cell in plan) are full where the Veil stands over the town's cloud: fit_bins()
     takes this iteration's own lobes out until every cell fits, so the renderer never drops one of the town's.
Untouched: the plug and its fade (the Condensery stays hidden until 56.88 s; stills at 55, 56.5 and 56.9 s), the
crown's height (msgs 96/103), the sill and the reveal, the town's own cloud.

msg 188 (39.8 s, "can we remove / alter these blobs": the pale masses above and behind the band): open_band_sky()
takes out the four dense lobes of the gate's south side that stood behind the stage's sky beams (see BAND_SKY).

msg 180 (60-70 s: the Lone Lamp's and the Break Light's rocks "partly see-through"). Found by switching things off in
AV Gen: it is my it12 wisp zones `end_lone_lamp` (rim height, 46 x 40 m round the rock) and `end_drift` (under the
lens): torn translucent sheets BETWEEN the lens and the rocks' hanging keels, the same tone as the sea behind, so the
keel reads as glass. Not a depth or compositing fault (with the zones off the keels are solid). Now: no zone at the
rim; the drift's sheet lies 50 m lower, under the keel's lowest sight line; the Lone Lamp gets a small dense collar on
its far side and the Break Light a cloudlet its keel sinks into.
"""
import json, math, os, random, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
argv = sys.argv[1:]
ROOT = argv[argv.index("--root") + 1] if "--root" in argv else os.path.join(HERE, "..")
ART = os.path.join(ROOT, "art")
BASE = "bc117750"       # iteration 12's final art data: every run starts from it
R1 = lambda v: [round(x, 1) for x in v]


def load_base(name):
    if "--base-dir" in argv:
        return json.load(open(os.path.join(argv[argv.index("--base-dir") + 1], name)))
    top = subprocess.run(["git", "-C", HERE, "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True).stdout.strip()
    return json.loads(subprocess.run(["git", "-C", top, "show", "%s:examples/neon-dreams/art/%s" % (BASE, name)],
                                     capture_output=True, text=True, check=True).stdout)


# ------------------------------------------------------------------------------------------------ the lens's path
def _cr(p0, p1, p2, p3, t, alpha=0.5):
    def tj(ti, a, b):
        return ti + max(math.dist(a, b), 1e-6) ** alpha
    t0 = 0.0; t1 = tj(t0, p0, p1); t2 = tj(t1, p1, p2); t3 = tj(t2, p2, p3); tt = t1 + (t2 - t1) * t

    def lerp(a, b, ta, tb):
        w = (tt - ta) / (tb - ta)
        return [a[i] + (b[i] - a[i]) * w for i in range(3)]
    a1 = lerp(p0, p1, t0, t1); a2 = lerp(p1, p2, t1, t2); a3 = lerp(p2, p3, t2, t3)
    return lerp(lerp(a1, a2, t0, t2), lerp(a2, a3, t1, t3), t1, t2)


def lens_path(layout, step=0.1):
    """[(t, [x, y, z])] every `step` seconds: build.py journey() (centripetal Catmull-Rom, monotone cubic of distance)."""
    wp = layout["camera"]["waypoints"]; pts = [list(w["pos"]) for w in wp]; ts = [w["t"] for w in wp]
    ext = [[2 * pts[0][i] - pts[1][i] for i in range(3)]] + pts + [[2 * pts[-1][i] - pts[-2][i] for i in range(3)]]
    dense, idx = [], []
    for k in range(len(pts) - 1):
        n = max(4, int(math.dist(pts[k], pts[k + 1]) / 4.0)); idx.append(len(dense))
        dense += [_cr(ext[k], ext[k + 1], ext[k + 2], ext[k + 3], j / n) for j in range(n)]
    idx.append(len(dense)); dense.append(pts[-1])
    cum = [0.0]
    for k in range(1, len(dense)):
        cum.append(cum[-1] + math.dist(dense[k - 1], dense[k]))
    ss = [cum[i] for i in idx]; n = len(ts)
    d = [(ss[i + 1] - ss[i]) / (ts[i + 1] - ts[i]) for i in range(n - 1)]
    m = [d[0]] + [(d[i - 1] + d[i]) / 2 if d[i - 1] * d[i] > 0 else 0.0 for i in range(1, n - 1)] + [d[-1]]
    for i in range(n - 1):
        if d[i] == 0:
            m[i] = m[i + 1] = 0; continue
        a, b = m[i] / d[i], m[i + 1] / d[i]; h = a * a + b * b
        if h > 9:
            tau = 3 / math.sqrt(h); m[i], m[i + 1] = tau * a * d[i], tau * b * d[i]

    def s_at(t):
        t = min(max(t, ts[0]), ts[-1]); i = max(k for k in range(n - 1) if ts[k] <= t)
        hh = ts[i + 1] - ts[i]; u = (t - ts[i]) / hh
        return (2 * u ** 3 - 3 * u ** 2 + 1) * ss[i] + (u ** 3 - 2 * u ** 2 + u) * hh * m[i] + (-2 * u ** 3 + 3 * u ** 2) * ss[i + 1] + (u ** 3 - u ** 2) * hh * m[i + 1]

    def at_s(s):
        s = min(max(s, 0.0), cum[-1]); lo, hi = 0, len(cum) - 1
        while hi - lo > 1:
            mid = (lo + hi) // 2
            lo, hi = (mid, hi) if cum[mid] <= s else (lo, mid)
        u = (s - cum[lo]) / max(cum[hi] - cum[lo], 1e-9)
        return [dense[lo][j] + (dense[hi][j] - dense[lo][j]) * u for j in range(3)]
    out = []; t = 0.0
    while t <= ts[-1] + 1e-6:
        out.append((round(t, 2), at_s(s_at(t)))); t += step
    return out


def reach(l, u):
    """The lobe's radius along the unit direction u."""
    rx, ry, rz = (l["r"] * s for s in l["scale"])
    return 1.0 / math.sqrt((u[0] / rx) ** 2 + (u[1] / ry) ** 2 + (u[2] / rz) ** 2)


def gap_to(l, p):
    """Metres from the lobe's surface to the point p (negative inside)."""
    v = [p[i] - l["c"][i] for i in range(3)]; d = math.sqrt(sum(q * q for q in v))
    if d < 1e-6:
        return -l["r"] * min(l["scale"])
    return d - reach(l, [q / d for q in v])


def path_gap(path, l, t0, t1):
    return min(gap_to(l, p) for t, p in path if t0 <= t <= t1)


def lobe(c, r, scale, dens, soft, tag, erode=None, bob=None):
    l = {"c": R1(c), "r": round(r, 1), "scale": [round(s, 2) for s in scale], "density": round(dens, 2), "soft": round(soft, 2), "tag": tag}
    if erode is not None:
        l["erode"] = erode
    if bob:
        l["bob"] = bob
    return l


# ------------------------------------------------------------------------------------------------ msg 178: the Veil
GARDEN = (146.0, 121.6, 38.0)       # the band's stage (blender/nd_band.py C)
LOW_TOP = 120.0
ROOF_GAP = 45.0                     # a flat lobe this near the lens's line inside the bank (49.4-56.9 s) is a room's roof: it stays
BODY_ERODE, WALL_ERODE = 0.5, 0.55
# the translucent lobes' share of the erosion. Low (0.15-0.45, tried first) leaves the lobe's own iso-surfaces round: the
# shape noise (55 m, slower than the lobe) then cuts the thin density off along one of them, and a clean circular arc
# stands in the sky (a glass bubble in front of the hamlet at 39.8 s). At 0.65 the billows bend the iso-surfaces by up to
# 14 m while the soft band is still 10-12 m wide: an edge that wanders AND fades
SHARE = float(argv[argv.index("--share") + 1]) if "--share" in argv else 0.65
STRATA = (0.62, 0.9, SHARE, 1.0)     # density, soft, erode share, thickness factor of a flat slab that is no room's roof (0.55 and 0.7 as thick: separate soft patches over the roofs at 37.5 s)
HEART = (0.62, 0.9)                 # a body's solid heart: share of the radius, density
ENVELOPE = (0.52, 0.95, SHARE)       # density, soft, erode share of a body's torn envelope
MIST = (0.42, SHARE)                 # density, erode share of a halo
MIST_SHEET = {"name": "veil_shoulder", "centre": [235.0, 70.0], "radius": 250.0, "base": 136.0, "top": 205.0, "density": 0.5,
              "scale": 34.0, "coverage": 0.5, "stretch": 2.6, "drift": [1.6, 0.0, 0.7]}
# the engine's four sheets (build.py MIST_LAYERS at BASE): a design that gives `mist_layers` replaces them, so they are
# repeated here unchanged, and the Veil's sheet is the fifth (the renderer holds six)
ENGINE_SHEETS = [
    {"name": "stilts", "centre": [0.0, 10.0], "radius": 520.0, "base": 92.0, "top": 121.0, "density": 0.9, "scale": 26.0, "coverage": 0.5, "stretch": 3.5, "drift": [1.4, 0.0, 0.5]},
    {"name": "harbour_low", "centre": [0.0, 10.0], "radius": 1400.0, "base": 40.0, "top": 95.0, "density": 0.6, "scale": 60.0, "coverage": 0.45, "stretch": 3.0, "drift": [2.2, 0.0, 0.8]},
    {"name": "open_sea", "centre": [1300.0, -30.0], "radius": 1300.0, "base": 15.0, "top": 75.0, "density": 0.5, "scale": 80.0, "coverage": 0.42, "stretch": 3.0, "drift": [2.5, 0.0, 1.0]},
    {"name": "vortex_shelf", "centre": [2090.0, -20.0], "radius": 950.0, "base": -25.0, "top": 45.0, "density": 0.7, "scale": 55.0, "coverage": 0.5, "stretch": 3.0, "drift": [0.4, 0.0, 0.2]}]


def veils_rock(l, eyes, islands, reach_m=420.0):
    """True when the lobe lies between one of the lens's places and a rock island's body: a translucent lobe there
    makes the rock read as glass (msg 180). Lines of sight to the rim, the waist and the keel of every isle in reach."""
    c = l["c"]; rad = [l["r"] * s for s in l["scale"]]
    for il in islands:
        ix, top, iz = il["centre"]; R = il["width_m"] / 2.0; dep = il["depth_m"]
        if math.dist(c, (ix, top - dep / 2.0, iz)) > reach_m + l["r"] * max(l["scale"]):
            continue
        pts = [(ix, top - dep * f, iz) for f in (0.0, 0.35, 0.7, 1.0)]
        pts += [(ix + R * 0.8 * math.cos(a), top - dep * f, iz + R * 0.8 * math.sin(a) * (1.0 - 0.6 * f)) for a in (0.0, 1.571, 3.142, 4.712) for f in (0.05, 0.4)]
        for e in eyes:
            if math.dist(e, (ix, top, iz)) > reach_m:
                continue
            for q in pts:
                a_ = [(e[i] - c[i]) / rad[i] for i in range(3)]; d_ = [(q[i] - e[i]) / rad[i] for i in range(3)]
                dd = sum(v * v for v in d_) or 1e-9
                u = min(max(-sum(a_[i] * d_[i] for i in range(3)) / dd, 0.0), 1.0)
                if sum((a_[i] + d_[i] * u) ** 2 for i in range(3)) < 1.0:
                    return True
    return False


def admit_rooms(l, rooms, bulge):
    for v in rooms:
        d = math.dist(l["c"], v["centre"])
        if d < 1e-6:
            return False
        if d < v["radius"] + reach(l, [(v["centre"][i] - l["c"][i]) / d for i in range(3)]) * (1.0 - bulge):
            return False
    return True


def blend_veil(des, path, islands, stats):
    rr = random.Random(1301)
    veil = des["masses"]["veil"]
    rooms = [v for v in des["veil"]["rooms"] if v["centre"][0] <= 262.0]
    stage = (GARDEN[0], GARDEN[1] + 6.0, GARDEN[2])
    watch = [p for t, p in path if 31.0 <= t <= 49.6 and abs(t * 2 - round(t * 2)) < 0.01]       # the lens every half second, Quay to gate

    def torn_ok(l):
        return admit_rooms(l, rooms, 0.3) and path_gap(path, l, 30.0, 54.0) >= 6.0 and gap_to(l, stage) >= 30.0 and not veils_rock(l, watch, islands)

    def mist_ok(l):
        # not over the garden or the band, not on the lens's line before the dive, not in front of a rock
        return gap_to(l, stage) >= 38.0 and path_gap(path, l, 30.0, 49.6) >= 18.0 and not veils_rock(l, watch, islands)

    out, bodies = [], []
    n = dict(bodies=0, low=0, envelopes=0, hearts=0, mist=0, bridges=0, strata=0, roof=0, walls=0)
    for l in veil:
        tag = l.get("tag")
        if tag in ("plug", "plug_fade") or l["r"] > 100.0 or l["density"] < 0.9:
            out.append(l); continue
        if tag == "wall":                                              # a room's shell: its body stays, its skin is softer
            w = dict(l); w["erode"] = WALL_ERODE; w["soft"] = max(l["soft"], 0.8); out.append(w); n["walls"] += 1; continue
        cx, cy, cz = l["c"]; r = l["r"]; sx, sy, sz = l["scale"]
        if sy <= 0.66:
            if path_gap(path, l, 49.4, 56.9) < ROOF_GAP:              # a room's roof: its body stays, its skin is softer (as the walls)
                w = dict(l); w["erode"] = WALL_ERODE; w["soft"] = max(l["soft"], 0.8); out.append(w); n["roof"] += 1; continue
            # 3. a stratum: the slab at 0.62 of its density and a part share, so the shape noise (55 m) tears holes
            #    through it and its edge fades over most of its thickness
            st = dict(l); st["density"] = STRATA[0]; st["soft"] = STRATA[1]; st["erode"] = STRATA[2]; st["tag"] = "blend_strata"
            st["scale"] = [sx, round(sy * STRATA[3], 3), sz]; st["c"] = [cx, round(cy + r * sy * (1.0 - STRATA[3]), 1), cz]      # thinner, its top where it was
            out.append(st); n["strata"] += 1; continue
        if cy + r * sy < LOW_TOP:                                      # under the decks' level: only its top shows, as the floor beyond the garden's rim
            n["low"] += 1; out.append(l); continue
        # 1. a body: a torn envelope round a solid heart
        n["bodies"] += 1; bodies.append(l)
        # the body itself becomes the torn envelope (the lobe count of a crowded bin cell does not grow); the largest
        # that keeps out of the rooms, off the band and from in front of a rock
        env = None
        for grow in (1.15, 1.06, 1.0):
            e = lobe((cx, cy, cz), r * grow, (sx, sy, sz), ENVELOPE[0], ENVELOPE[1], "blend_envelope", ENVELOPE[2])
            if grow == 1.0 or torn_ok(e):
                env = e; break
        out.append(env); n["envelopes"] += 1
        heart = lobe((cx, cy - 0.06 * r, cz), r * HEART[0], (sx, sy, sz), HEART[1], 0.85, "blend_heart", BODY_ERODE)
        out.append(heart); n["hearts"] += 1
    # 2. mist: a halo round each body, a bridge across each saddle (big, thin, torn by the shape noise)
    for l in bodies:
        r = l["r"]; cx, cy, cz = l["c"]
        near = min((p for t, p in path if 30.0 <= t <= 49.6), key=lambda p: math.dist(p, l["c"]))
        dx, dz = cx - near[0], cz - near[2]; dn = math.hypot(dx, dz) or 1.0
        jx, jz = rr.uniform(-6, 6), rr.uniform(-6, 6)
        for grow, shift in ((1.6, 0.0), (1.45, 0.2), (1.3, 0.35), (1.2, 0.5)):      # the largest that keeps off the lens, the band and the rocks, pushed away from the lens
            m = lobe((cx + jx + dx / dn * shift * r, cy - 0.2 * r, cz + jz + dz / dn * shift * r), r * grow, (1.25, 0.62, 1.25), MIST[0], 1.0, "blend_mist", MIST[1])
            if mist_ok(m) and admit_rooms(m, rooms, 0.45):
                out.append(m); n["mist"] += 1; break
    for i, a in enumerate(bodies):
        for b in bodies[i + 1:]:
            d = math.dist(a["c"], b["c"])
            if a["r"] + b["r"] - 10.0 < d < a["r"] + b["r"] + 45.0:
                mid = [(a["c"][j] + b["c"][j]) / 2 for j in range(3)]
                sc = (1.7, 0.5, 1.0) if abs(a["c"][0] - b["c"][0]) > abs(a["c"][2] - b["c"][2]) else (1.0, 0.5, 1.7)
                m = lobe((mid[0], mid[1] - rr.uniform(0, 8), mid[2]), max(34.0, d * 0.45), sc, 0.55, 0.9, "blend_bridge", SHARE)
                if mist_ok(m) and admit_rooms(m, rooms, 0.3):
                    out.append(m); n["bridges"] += 1
    des["masses"]["veil"] = out
    if "--no-sheet" not in argv:                                       # (an A/B switch for the sheet's look and cost)
        des["mist_layers"] = [dict(s) for s in ENGINE_SHEETS] + [dict(MIST_SHEET)]
    stats["veil"] = dict(n, lobes_before=len(veil), lobes_after=len(out))


# ------------------------------------------------------------------------------------------------ msg 188: the sky behind the band
# owner msg 188 (on the 39.8 s still): "can we remove / alter these blobs" -- the pale rounded masses above and behind the
# band, left of the isle's tower: the stage's sky beams should stand against dark sky. Named by projecting the lobes into
# the owner's box: the antechamber's south wall masses and the two roof slabs south of the lens's line (dense, so no
# share of the erosion tears them), 60-140 m behind the stage as the Quay's end and the garden see it. They go. The rooms
# keep their north walls, their roof over the lens's line and the plug: the lens still dives into cloud, with a window on
# the south sky beside it. BAND_SKY: x0, x1, y0, y1, z0, z1 (engine metres) of the centres that go.
BAND_SKY = (195.0, 300.0, 125.0, 185.0, 30.0, 100.0)


def open_band_sky(des, stats):
    x0, x1, y0, y1, z0, z1 = BAND_SKY
    keep, gone = [], []
    for l in des["masses"]["veil"]:
        c = l["c"]
        if l.get("tag") not in ("plug", "plug_fade") and l["density"] >= 0.9 and x0 <= c[0] <= x1 and y0 <= c[1] <= y1 and z0 <= c[2] <= z1:
            gone.append([l.get("tag"), c, l["r"]]); continue
        keep.append(l)
    des["masses"]["veil"] = keep
    stats["band_sky_removed"] = gone


# ------------------------------------------------------------------------------------------------ msg 178: two rim wraps
WRAP_ISLES = ("isle_exit", "isle_lampwick")


def soften_wraps(des, islands, stats):
    """The isles beside the Veil: the rim's lobes go (crisp eggs of 8-14 m at the seat's 0.3 share, and tatters too small
    for the 55 m shape noise to tear: clean ellipses on the rock at 44-47 s); the mist far under the keel stays, and a
    thin wisp zone lies at each rim (the engine's torn sheets, 10 m, riding the isle; 5 m deep, so the keel under it
    stays solid)."""
    n = 0
    for name in WRAP_ISLES:
        il = next(i for i in islands if i.get("bob") == name)
        keep = [l for l in des["masses"].get(name, []) if l["c"][1] < il["centre"][1] - il["depth_m"]]
        n += len(des["masses"].get(name, [])) - len(keep); des["masses"][name] = keep
        des["wisp_regions"] = [w for w in des["wisp_regions"] if w["name"] != "rim_" + name]
        des["wisp_regions"].append({"name": "rim_" + name, "centre": [il["centre"][0], il["centre"][2]], "radii": [round(il["width_m"] * 0.5 + 9.0, 1), round(il["width_m"] * 0.5 + 9.0, 1)],
                                    "yaw": 0.0, "floor": round(il["centre"][1] - 1.5, 1), "below": 5.0, "above": 1.0, "density": 0.6, "bob": name})
    stats["wrap_lobes_removed"] = n


# ------------------------------------------------------------------------------------------------ msg 180: the end's rocks
def solid_rocks(des, path, islands, stats):
    lone = next(i for i in islands if i.get("bob") == "lone_lamp"); brk = next(i for i in islands if i.get("bob") == "break_light")
    lx, ly, lz = lone["centre"]; keel = ly - lone["depth_m"]
    # the zone at the Lone Lamp's rim goes; the drift's sheet lies under the keel's lowest sight line: from the lens
    # (y 219-226) no ray to either rock's keel meets it before the rock (the Lone Lamp's keel ends at 178 and bobs 2.8 m)
    top = keel - 6.0
    wr = []
    for w in des["wisp_regions"]:
        if w["name"] == "end_lone_lamp":
            continue
        if w["name"] == "end_drift":
            w = dict(w, floor=round(top - 4.0, 1), above=4.0, below=12.0, radii=[150.0, 90.0], density=0.5)
        wr.append(w)
    des["wisp_regions"] = wr
    # the it12 strands at the lens's height round the Lone Lamp (20 flattened lobes of 16-22 m at density 0.4): at the
    # full share the erosion eats them whole, they never drew; out, so nothing translucent can come back in front of
    # the rock if the share changes
    ends = des["masses"].get("end_layers", [])
    keep = [l for l in ends if not (l["c"][0] < 720.0 and l["c"][1] > 185.0)]
    stats["end_strands_removed"] = len(ends) - len(keep); des["masses"]["end_layers"] = keep
    rr = random.Random(1303)
    # the Lone Lamp: a collar of small dense heads at its waist, on the side away from the lens and under its rim (the
    # lens passes north-west of it, 59-70 s), so the rock stands in cloud without cloud in front of it
    eye = [p for t, p in path if 59.0 <= t <= 70.2]
    ex = sum(p[0] for p in eye) / len(eye); ez = sum(p[2] for p in eye) / len(eye)
    away = math.atan2(lz - ez, lx - ex); R = lone["width_m"] / 2.0
    ll = [l for l in des["masses"]["lone_lamp"] if l.get("tag") != "seat13"]
    for k in range(7):
        a = away + (k - 3) * 0.42 + rr.uniform(-0.1, 0.1); d = R * rr.uniform(0.95, 1.25); r = rr.uniform(9.0, 14.0)
        ll.append(lobe((lx + d * math.cos(a), ly - rr.uniform(9.0, 20.0), lz + d * math.sin(a)), r, (rr.uniform(1.1, 1.4), rr.uniform(0.6, 0.8), rr.uniform(1.0, 1.3)),
                       1.0, 0.6, "seat13", 0.6, bob="lone_lamp"))
    des["masses"]["lone_lamp"] = ll
    # the Break Light, 0.9-1.1 km from the lens: a cloudlet its keel sinks into (lobes of 30 m and more: the march's step
    # there is 20 m), its top 14-22 m over the keel's tip, dense, flattened, wider than the rock
    bx, by, bz = brk["centre"]; tip = by - brk["depth_m"]
    bl = [l for l in des["masses"]["break_light"] if l.get("tag") != "seat13"]
    for (dx, dz, r, sy, up) in ((0.0, 0.0, 40.0, 0.5, 20.0), (-26.0, 10.0, 32.0, 0.55, 14.0), (24.0, -14.0, 34.0, 0.5, 16.0), (6.0, 26.0, 30.0, 0.5, 12.0), (-8.0, -28.0, 30.0, 0.55, 13.0)):
        bl.append(lobe((bx + dx, tip + up - r * sy, bz + dz), r, (rr.uniform(1.2, 1.5), sy, rr.uniform(1.1, 1.35)), 1.0, 0.6, "seat13", 0.6, bob="break_light"))
    des["masses"]["break_light"] = bl
    stats["end"] = {"drift_sheet_top": round(top, 1), "lone_lamp_collar": 7, "break_light_cloudlet": 5}


# ------------------------------------------------------------------------------------------------ bins (replica)
BIN_LIMIT = 63          # the renderer's cell holds 64


def _bin_grid(lobes, reach_of, side):
    """rendering::binCloudLobesOnce: the cells each lobe touches, at `side` cells along the longer side."""
    ext = [l["r"] * max(l["scale"][0], l["scale"][2]) + reach_of(l) for l in lobes]
    lo = (min(l["c"][0] - e for l, e in zip(lobes, ext)), min(l["c"][2] - e for l, e in zip(lobes, ext)))
    hi = (max(l["c"][0] + e for l, e in zip(lobes, ext)), max(l["c"][2] + e for l, e in zip(lobes, ext)))
    span = max(hi[0] - lo[0], hi[1] - lo[1]); cell = max(0.5 * sorted(ext)[len(ext) // 2], span / side, 1.0)
    cells = {}
    for k, (l, e) in enumerate(zip(lobes, ext)):
        for z in range(int((l["c"][2] - e - lo[1]) // cell), int((l["c"][2] + e - lo[1]) // cell) + 1):
            for x in range(int((l["c"][0] - e - lo[0]) // cell), int((l["c"][0] + e - lo[0]) // cell) + 1):
                cells.setdefault((x, z), []).append(k)
    return cells, cell, ext


def fit_bins(des, stats):
    """The renderer bins the hero lobes in plan, 64 to a cell, at 128 cells a side, then 256, then 512 when a cell is
    over (ADR-2114); past that it drops the smallest lobes of the cell, whoever they belong to. So that it never has
    to (the town's own small heads share cells with the Veil's foot): while the finest grid has a cell over the limit,
    this iteration's smallest lobe in the fullest cell goes."""
    amp = {b["name"]: max(b["amplitude"][0], b["amplitude"][2]) for b in des.get("bobs", [])}
    reach_of = lambda l: amp.get(l.get("bob"), 0.0)
    dropped = 0; drops = {}
    while True:
        lobes = [l for k, m in des["masses"].items() if k != "vortex" for l in m]
        for side in (128, 256, 512):
            cells, cell, ext = _bin_grid(lobes, reach_of, side)
            worst = max(cells.values(), key=len)
            if len(worst) <= BIN_LIMIT:
                break
        if len(worst) <= BIN_LIMIT:
            break
        by = {}
        for k in worst:
            by.setdefault(lobes[k].get("tag"), []).append(k)
        # where a dozen strata lie over each other one of them goes first (fewer sheets, more sky between them); then
        # a bridge, a stratum, a heart; the halos that do the blending go last
        order = ["blend_strata"] if len(by.get("blend_strata", [])) > 12 else []
        order += ["blend_bridge", "blend_strata", "blend_heart", "blend_mist"]
        mine = next((by[t] for t in order if by.get(t)), None)
        if not mine:
            stats["bins_unfit"] = len(worst); break
        gone = lobes[min(mine, key=lambda k: ext[k])]; drops[gone["tag"]] = drops.get(gone["tag"], 0) + 1
        for m in des["masses"].values():
            for q, l in enumerate(m):
                if l is gone:
                    del m[q]; break
        dropped += 1
    stats["bins"] = {"cells_a_side": side, "cell_m": round(cell, 1), "fullest_cell": len(worst), "dropped_to_fit": dropped, "dropped_by_tag": drops, "hero_lobes": len(lobes)}


def main():
    des = load_base("cloud_design.json"); lay = load_base("layout.json")
    path = lens_path(lay); islands = lay["islands"]; stats = {}
    blend_veil(des, path, islands, stats)
    if "--no-band-sky" not in argv:
        open_band_sky(des, stats)
    soften_wraps(des, islands, stats)
    solid_rocks(des, path, islands, stats)
    fit_bins(des, stats)
    stats["veil"]["lobes_after_fit"] = len(des["masses"]["veil"])
    des["counts"] = {k: len(v) for k, v in des["masses"].items()}
    des["it13"] = {"msgs": [178, 180, 188], "script": "blender/nd_clouds13.py", "base": BASE, "stats": stats,
                   "note": "msg 178: the Veil's outside rebuilt as torn envelopes round solid hearts, mist halos, bridges and strata (tags "
                           "blend_envelope / blend_heart / blend_mist / blend_bridge / blend_strata; each carries its own `erode` share), one mist "
                           "sheet at its shoulder (mist_layers: the engine's four repeated, veil_shoulder added), soft rim wraps and rim "
                           "wisp zones for isle_exit and isle_lampwick. msg 180: wisp zone end_lone_lamp removed, end_drift lowered under "
                           "the Lone Lamp's keel; a collar for the Lone Lamp and a cloudlet for the Break Light (tag seat13, erode 0.6)."}
    dst = argv[argv.index("--out") + 1] if "--out" in argv else os.path.join(ART, "cloud_design.json")
    json.dump(des, open(dst, "w"), indent=None, separators=(",", ":"))
    print("nd_clouds13:", json.dumps(stats))


if __name__ == "__main__":
    main()
