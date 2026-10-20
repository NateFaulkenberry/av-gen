"""Rainbow World: build the whole world -- the knot -- and its AV Gen scene and live project.

Run inside Blender (it uses the band's analytic surface and the prototype asset scripts):
  /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup -P examples/rainbow-world/build_world.py -- \
      <outdir> [--assets <dir with engine_assets.py output>] [--look look.json] [--quick]

The knot (03-art-direction.md section 3; Stage 4 grey-box E6): a (2,3) torus knot of land, 180 m per unit (a 5.7 km
loop), its band 260 m wide, twisting one whole turn round the loop (ADR-1542) so gravity turns as the camera travels.
The band is swept on the frames AV Gen itself computes for the same spline (knot_path.Path), and the camera rides a
second spline over the meadow whose rolls make its up the band's up (ADR-1541).

Outputs in <outdir>:
  band_<k>.glb                    the band in chunks (culled as single instances)
  rainbow-world.scene.json        the composition
  rainbow-world.json              the live project (no audio file; palette, MIDI, routes)
  world_report.json               counts, lengths, clearances
"""
import argparse
import json
import math
import os
import random
import shutil
import sys

import bpy
import numpy as np
from mathutils import Matrix, Quaternion, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "blender"))
import band  # noqa: E402
import knot_path  # noqa: E402
import look  # noqa: E402
import rw_common as rw  # noqa: E402
from look import R, av, node_points, point  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("out")
ap.add_argument("--assets", default=None)
ap.add_argument("--look", default=None)
ap.add_argument("--quick", action="store_true", help="coarse band (6 m rings), for layout iterations")
ap.add_argument("--scale", type=float, default=180.0)
ap.add_argument("--zamp", type=float, default=1.0)
ap.add_argument("--turns", type=int, default=1)
ap.add_argument("--chunks", type=int, default=48)
ap.add_argument("--base-speed", type=float, default=30.0, help="the ride's base speed, m/s")
ap.add_argument("--project", default=None, help="where to write the live project (default <out>/rainbow-world.json); "
                "its scene path is written relative to it")
a = ap.parse_args(rw.argv_after_dashes())
OUT = os.path.abspath(a.out)
ASSETS = os.path.abspath(a.assets) if a.assets else OUT
os.makedirs(OUT, exist_ok=True)
LOOK = look.load_look(a.look)
rng = random.Random(2026)

# ---- the path and the band -----------------------------------------------------------------------------------------
controls = knot_path.trefoil_controls(n=72, scale=a.scale, z_amp=a.zamp)
base = knot_path.Path(controls)
ups = []
for i in range(len(controls)):
    k = i * base.per
    ups.append(knot_path._rotate_about(base.rmf[k], base.T[k], 2 * math.pi * a.turns * base.D[k] / base.length))
rolls, path = knot_path.rolls_to_follow(controls, ups)
rw.reset()
band.configure_knot(path, ring_spacing=6.0 if a.quick else 3.0)
fr = band.frames()
L = band.L
print("KNOT length %.0f m, rings %d, roll turns %d" % (L, band.NS, path.roll_turns))


def surf(s, x, lift=0.0):
    return band.surface_point(fr, s % L, x, lift)


def surf_normal(s, x):
    p, t, u, sd = surf(s, x)
    pa, _, _, _ = surf(s + 0.6, x)
    pb, _, _, _ = surf(s, x + 0.6)
    n = (pa - p).cross(pb - p).normalized()
    return n if n.dot(u) > 0 else -n


# chunks
chunk_rings = []
step = band.NS // a.chunks
for c in range(a.chunks):
    i0 = c * step
    i1 = band.NS if c == a.chunks - 1 else (c + 1) * step
    chunk_rings.append((i0, i1))
band_nodes = []
for c, (i0, i1) in enumerate(chunk_rings):
    rw.reset()
    ob, _ = band.build_band("band_%02d" % c, rings=(i0, i1))
    m = bpy.data.materials.new("band")
    m.use_nodes = True
    ob.data.materials.append(m)
    # each chunk about its own centre: the culler's bound is the mesh's extent about its origin, and the LOD
    # selector measures to the instance, so a chunk left in world space is never culled and never simplified
    vs = ob.data.vertices
    lo_ = Vector((min(v.co.x for v in vs), min(v.co.y for v in vs), min(v.co.z for v in vs)))
    hi_ = Vector((max(v.co.x for v in vs), max(v.co.y for v in vs), max(v.co.z for v in vs)))
    ctr = (lo_ + hi_) * 0.5
    ob.data.transform(Matrix.Translation(-ctr))
    rw.export_glb([ob], os.path.join(OUT, "band_%02d.glb" % c))
    # mesh LOD with the chunk's open ends held still (ADR-1546), so a near chunk and a far one still meet
    band_nodes.append(node_points("band_%02d" % c, "band_%02d.glb" % c, [point(ctr, Quaternion(), 1.0)],
                                  {"program": "rw_ground", "roughness": 0.93, "metallic": 0.0},
                                  lod={"cull": True, "count": 4, "byScreenSize": False, "distance1": 160.0,
                                       "distance2": 420.0, "distance3": 950.0, "spread": 0.0, "lockBorder": True}))
print("BAND chunks", len(band_nodes))

# copy the shared assets (rocks, grass, flowers, trees, clouds, rainbow) beside the scene
for f in os.listdir(ASSETS):
    if f.endswith((".glb", ".png")) and not f.startswith(("band_", "lookdev")) and ASSETS != OUT:
        shutil.copy(os.path.join(ASSETS, f), os.path.join(OUT, f))

# ---- the sun and the look ---------------------------------------------------------------------------------------------
SUN_B = Vector((0.35, -0.55, 0.76)).normalized()   # the world's sun (fixed: the orientation anchor)
SUN = Vector(rw.to_avgen(SUN_B))
look.configure(SUN, look.rim_ranges(band))
nodes, programs = list(band_nodes), look.all_programs()

# ---- rim rocks ------------------------------------------------------------------------------------------------------
rw.reset()
protos = band.append_rocks(os.path.join(ASSETS, "..", "stage2", "rocks", "rocks.blend")
                           if not os.path.exists(os.path.join(ASSETS, "rocks.blend")) else os.path.join(ASSETS, "rocks.blend"))
placed = band.place_rim_rocks(fr, protos, seed=5)
ring_pts = np.array([np.array(f_[1]) for f_ in fr])
rock_info = []
for o in placed:
    k_ = int(np.argmin(np.linalg.norm(ring_pts - np.array(o.location), axis=1)))
    _, pr_, tr_, ur_, sr_ = fr[k_]
    rock_info.append((o, fr[k_][0], 1.0 if (o.location - pr_).dot(sr_) > 0 else -1.0))
by = {}
ROCKS_DEFERRED = rock_info  # filtered by the ride's crossings below, then written
for o in []:
    rid = o.name.split(".")[0]
    w, x, y, z = rw.quat_to_avgen(o.rotation_quaternion)
    p = rw.to_avgen(o.location)
    sc = o.scale
    by.setdefault(rid, []).append([round(p[0], 3), round(p[1], 3), round(p[2], 3), round(x, 5), round(y, 5), round(z, 5),
                                   round(w, 5), round(sc[0], 3), round(sc[2], 3), round(sc[1], 3)])


# ---- the two faces of the band -------------------------------------------------------------------------------------
# The band has two meadows: TOP (its up is the band's u) and BOTTOM (its up is -u: the other gravity). Hills on the
# bottom face are authored in its own x (band.build_band reads them at -x).
def surf_any(s, x, face, lift=0.0):
    p, t, u, sd = band.frame_lerp(fr, s % L)
    if face == "top":
        return p + sd * x + u * (band.FACE + band.face_height(s % L, x, band.TOP_HILLS, 0.0) + lift), t, u, sd
    return p + sd * x - u * (band.FACE + band.face_height(s % L, -x, band.BOTTOM_HILLS, 1.3) + lift), t, -u, sd


def normal_any(s, x, face):
    p, t, up, sd = surf_any(s, x, face)
    pa, _, _, _ = surf_any(s + 0.6, x, face)
    pb, _, _, _ = surf_any(s, x + 0.6, face)
    n = (pa - p).cross(pb - p).normalized()
    return n if n.dot(up) > 0 else -n


def hill_centre(h, face):
    """A hill's (s, x) in the face's own section coordinates."""
    return (h[0], h[1]) if face == "top" else (h[0], -h[1])


FACE_HILLS = {"top": band.TOP_HILLS, "bottom": band.BOTTOM_HILLS}

# ---- the ride: weaving over a meadow, and four times round the loop over a rim onto the other face and back ---------
TR = 110.0                       # metres a rim crossing takes
RIM_X, RIM_A, RIM_LIFT = 108.0, 40.0, 11.0
plan = []                        # (s0, s1, kind, side) with kind top|bottom|over (top->bottom) |back (bottom->top)
quarter = L / 4
for k in range(4):
    b0 = k * quarter
    side = 1.0 if k % 2 == 0 else -1.0
    plan += [(b0, b0 + 0.40 * quarter, "top", side), (b0 + 0.40 * quarter, b0 + 0.40 * quarter + TR, "over", side),
             (b0 + 0.40 * quarter + TR, b0 + 0.82 * quarter, "bottom", -side),
             (b0 + 0.82 * quarter, b0 + 0.82 * quarter + TR, "back", -side),
             (b0 + 0.82 * quarter + TR, b0 + quarter, "top", side)]


def seg_at(s):
    s = s % L
    for seg in plan:
        if seg[0] <= s < seg[1]:
            return seg
    return plan[-1]


def weave(s):
    return 0.40 * band.HALF_W * math.sin(2 * math.pi * s / band.per(820.0)) + 0.16 * band.HALF_W * math.sin(
        2 * math.pi * s / band.per(300.0) + 1.0)


def ride_x(s):
    """The ride's lateral position on a face: the weave, drawn out to the rim of the nearest crossing (in band
    coordinates, the same on either face) over the 220 m before and after it."""
    s = s % L
    x = weave(s)
    best_w, target = 0.0, 0.0
    for s0, s1, kind, side in plan:
        if kind not in ("over", "back"):
            continue
        for d in ((s0 - s) % L, (s - s1) % L):   # metres to the crossing's start ahead, from its end behind
            if d < 160.0:
                w = band.smoothstep(160.0, 0.0, d)
                if w > best_w:
                    best_w, target = w, side * RIM_X
    return x + (target - x) * best_w


# the arches stand at the middle of every long stretch on a face (placed below); the ride's holds are there
ARCH_S = [0.5 * (s0 + s1) for s0, s1, kind, side in plan if kind in ("top", "bottom") and s1 - s0 > 380.0]


def cam_lift(s):
    """Low and fast between landmarks -- 4.5-8 m, so the grass, flowers and stones beside the line give the
    near-field parallax that sells 30 m/s -- rising to ~12 m over the 200 m before each arch (16 m showed so much of
    the world at once that those frames were the GPU's p95, Stage 8), where the ride
    slows and the view opens (the clip's resolution beat)."""
    u = 0.5 + 0.5 * math.sin(2 * math.pi * s / band.per(380.0))
    low = 4.5 + 3.5 * u * u
    d = min([abs(((s - a_) + L / 2) % L - L / 2) for a_ in ARCH_S] + [1e9])
    rise = 1.0 - band.smoothstep(40.0, 240.0, d)
    return low + (12.0 - low) * rise


def ride(s):
    """(position, up) of the ride at station s."""
    s0, s1, kind, side = seg_at(s)
    if kind in ("top", "bottom"):
        x = ride_x(s)
        n = normal_any(s, x, kind)
        p, t, up, sd = surf_any(s, x, kind)
        return p + n * cam_lift(s), n
    # a rim crossing: an ellipse round the cliff from one face to the other, its up the ellipse's outward normal
    w = band.smoothstep(s0, s1, s % L)
    phi = math.pi * w if kind == "over" else math.pi * (1.0 - w)   # 0 = on top, pi = on the bottom
    p, t, u, sd = band.frame_lerp(fr, s % L)
    xr = side * RIM_X                                              # the rim in band coordinates
    b_top = band.FACE + band.face_height(s % L, xr, band.TOP_HILLS, 0.0) + RIM_LIFT
    b_bot = band.FACE + band.face_height(s % L, -xr, band.BOTTOM_HILLS, 1.3) + RIM_LIFT
    c = math.cos(phi)
    b = b_top if c >= 0 else b_bot
    X = xr + side * RIM_A * math.sin(phi)
    Y = b * c
    nrm = (sd * (side * math.sin(phi) / RIM_A) + u * (c / b)).normalized()
    return p + sd * X + u * Y, nrm


CAM_STEP = 15.0
cam_s = np.arange(0.0, L, CAM_STEP)
cam_pts, cam_ups = [], []
for s in cam_s:
    p, n = ride(s)
    cam_pts.append(np.array(p))
    cam_ups.append(np.array(n))
cam_controls = np.array(cam_pts)
cam_rolls, cam_path = knot_path.rolls_to_follow(cam_controls, cam_ups)
nodes.append(cam_path.to_avgen_json("ride", rw.to_avgen))
print("CAMERA path %.0f m, roll turns %d, plan %s" % (cam_path.length, cam_path.roll_turns,
                                                     [(round(a), k) for a, b_, k, sd_ in plan[:5]]))


def near_path(p, clearance):
    d = np.linalg.norm(cam_controls - np.array(p), axis=1)
    return d.min() < clearance


def path_clear(s, x, face, clearance=20.0):
    """True when the meadow point is at least `clearance` metres across from the ride where the ride is on that
    face (anything planted on the other face is clear of it, the band between)."""
    s0, s1, kind, side = seg_at(s)
    if kind != face:
        return not (kind in ("over", "back") and abs(abs(x) - RIM_X) < clearance)
    return abs(x - ride_x(s)) >= clearance


def ride_face(s):
    return seg_at(s)[2]


# ---- the crossings' rims: a vegetated lip instead of bare rock -----------------------------------------------------
CROSSINGS = [(s0, s1, side) for s0, s1, kind, side in plan if kind in ("over", "back")]


def near_crossing(s, side=None, margin=160.0):
    for s0, s1, sd_ in CROSSINGS:
        if (side is None or sd_ == side) and min((s - s1) % L, (s0 - s) % L, 0.0 if s0 <= s % L <= s1 else 1e9) < margin:
            return True
    return False


for o, s_r, side_r in ROCKS_DEFERRED:
    if near_crossing(s_r, side_r):
        continue  # the ride swings round this rim: no bare rock in its face
    rid = o.name.split(".")[0]
    w, x, y, z = rw.quat_to_avgen(o.rotation_quaternion)
    p = rw.to_avgen(o.location)
    sc = o.scale
    by.setdefault(rid, []).append([round(p[0], 3), round(p[1], 3), round(p[2], 3), round(x, 5), round(y, 5), round(z, 5),
                                   round(w, 5), round(sc[0], 3), round(sc[2], 3), round(sc[1], 3)])
for rid, pts in by.items():
    for k in range(0, len(pts), 60000):
        nodes.append(node_points("rim_%s_%d" % (rid.replace("rock_", ""), k // 60000), "rim" + rid + ".glb", pts[k:k + 60000],
                                 {"program": "rw_rock", "roughness": 0.85, "metallic": 0.0},
                                 castsShadow=False, lod={"cull": True, "minScreenRadius": 1.5}))
print("RIM ROCKS", sum(len(v) for v in by.values()), "of", len(ROCKS_DEFERRED))
# the chunks a crossing touches are drawn with the lip program (moss on the rim core)
for c, (i0, i1) in enumerate(chunk_rings):
    sa_, sb_ = L * i0 / band.NS, L * i1 / band.NS
    if any(not (s1 < sa_ - 60 or s0 > sb_ + 60) for s0, s1, sd_ in CROSSINGS):
        band_nodes[c]["procedural"]["material"]["program"] = "rw_ground_lip"

# ---- dressing: every feature of the land gets the dressing its shape asks for (deliberate clusters, rest between) ----
fam = {"great": [], "weeper": [], "blossom": [], "pine": []}
outcrops, fpts = [], []
landmarks = []


def plant(f, s, x, face, scale, sink=0.6):
    p, t, up, sd = surf_any(s, x, face, -sink)
    fam[f].append(point(p, up.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), rng.uniform(0, 6.28)), scale))


def drift(s, x, face, n, spread):
    """A flower drift: a dense core thinning outward (R1's foreground meadows), never a sprinkle."""
    for _ in range(n):
        ds, dx = rng.gauss(0, spread), rng.gauss(0, spread * 0.7)
        xx = x + dx
        if abs(xx) > 0.8 * band.HALF_W:
            continue
        p, t, up, sd = surf_any(s + ds, xx, face, -0.02)
        fpts.append(point(p, up.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), rng.uniform(0, 6.283)), rng.uniform(1.2, 2.0)))


def outcrop(s, x, face, size):
    """A cluster of 3-6 rocks breaking through a hill's shoulder."""
    for _ in range(rng.randint(3, 6)):
        ss, xx = s + rng.gauss(0, size * 0.5), x + rng.gauss(0, size * 0.5)
        p, t, up, sd = surf_any(ss, xx, face, -size * 0.25)
        q = up.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), rng.uniform(0, 6.28))
        outcrops.append(point(p, q, (size * rng.uniform(0.6, 1.2), size * rng.uniform(0.5, 1.0), size * rng.uniform(0.4, 0.8))))


for face in ("top", "bottom"):
    for h in FACE_HILLS[face]:
        hs, hx = hill_centre(h, face)
        hgt, rs, rx = h[2], h[3], h[4]
        if abs(hx) > 0.85 * band.HALF_W:
            continue
        if rx > 2.5 * rs and hgt > 6:      # a cross ridge: a line of trees along its crest, one gap
            gap = rng.uniform(-0.5, 0.5) * rx
            for k in range(rng.randint(5, 9)):
                xx = hx + rng.uniform(-0.8, 0.8) * rx
                if abs(xx - (hx + gap)) < 14 or abs(xx) > 0.8 * band.HALF_W or not path_clear(hs, xx, face, 22.0):
                    continue
                plant("great" if rng.random() < 0.8 else "blossom", hs + rng.gauss(0, 4), xx, face, rng.uniform(0.35, 0.6))
        elif rs > 2.5 * rx and hgt > 6:    # a long ridge spine: Ridge Pines strung along it
            for k in range(rng.randint(6, 10)):
                ss = hs + rng.uniform(-0.8, 0.8) * rs
                if path_clear(ss, hx, face, 16.0):
                    plant("pine", ss, hx + rng.gauss(0, 4), face, rng.uniform(0.6, 0.95))
        elif hgt >= 14:                    # a great hill: a Great Tree on the shoulder, an outcrop near the top
            xx = hx + rng.choice((-1, 1)) * rx * 0.6
            if abs(xx) < 0.78 * band.HALF_W and path_clear(hs, xx, face, 30.0):
                plant("great", hs + rng.gauss(0, 8), xx, face, rng.uniform(0.95, 1.2))
                landmarks.append((hs, xx, face))
            if path_clear(hs, hx, face, 18.0):
                outcrop(hs + rng.gauss(0, 10), hx + rng.gauss(0, 8), face, rng.uniform(5, 9))
        elif hgt < 0:                      # a hollow: a flower meadow on its floor, Weepers on its far rim
            if path_clear(hs, hx, face, 6.0) or True:
                drift(hs, hx, face, rng.randint(160, 280), rng.uniform(8, 14))
            for k in range(rng.randint(2, 4)):
                aa = rng.uniform(0, 2 * math.pi)
                ss, xx = hs + math.cos(aa) * rs * 0.9, hx + math.sin(aa) * rx * 0.9
                if abs(xx) < 0.78 * band.HALF_W and path_clear(ss, xx, face, 24.0):
                    plant("weeper", ss, xx, face, rng.uniform(0.8, 1.05))
        else:                              # a knoll: an outcrop or a Blossom Puff, now and then a drift at its foot
            r = rng.random()
            if r < 0.45 and path_clear(hs, hx, face, 14.0):
                outcrop(hs, hx, face, rng.uniform(3, 6))
            elif r < 0.7 and path_clear(hs, hx, face, 20.0):
                plant("blossom", hs, hx, face, rng.uniform(0.45, 0.6))
            if rng.random() < 0.5:
                drift(hs + rs, hx, face, rng.randint(60, 120), 5.0)
# the crossings' lips: a hanging garden on the crest the ride swings round -- drifts, Blossom Puffs and Weepers on
# both faces' edges, so what the camera passes is planted, not quarried
for s0, s1, side in CROSSINGS:
    for face in ("top", "bottom"):
        for k in range(rng.randint(4, 6)):
            ss = rng.uniform(s0 - 120, s1 + 120)
            xx = side * rng.uniform(0.62, 0.78) * band.HALF_W
            drift(ss, xx, face, rng.randint(80, 160), rng.uniform(4, 7))
        for k in range(rng.randint(1, 2)):
            ss = rng.uniform(s0 - 140, s1 + 140)
            xx = side * rng.uniform(0.6, 0.76) * band.HALF_W
            if path_clear(ss, xx, face, 26.0):
                plant(rng.choice(("blossom", "blossom", "weeper")), ss, xx, face, rng.uniform(0.5, 0.8))

# along the ride, every ~170 m one deliberate cluster near the camera (alternating sides, rest between):
# a grove of big trees, a flower field, or a rock garden ringed with flowers -- the big near shapes the frame lacked
CLUSTERS = ("grove", "field", "garden", "grove", "rest", "field")
for k, s0 in enumerate(np.arange(60.0, L, 170.0)):
    kind = ride_face(s0)
    if kind not in ("top", "bottom") or near_crossing(s0, None, 60.0):
        continue
    what = CLUSTERS[k % len(CLUSTERS)]
    side = 1.0 if k % 2 == 0 else -1.0
    xc = ride_x(s0) + side * rng.uniform(32, 55)
    if abs(xc) > 0.74 * band.HALF_W:
        xc = ride_x(s0) - side * rng.uniform(32, 55)
    if abs(xc) > 0.78 * band.HALF_W:
        continue
    if what == "grove":
        for j in range(rng.randint(3, 5)):
            ss, xx = s0 + rng.gauss(0, 16), xc + rng.gauss(0, 10)
            if abs(xx) < 0.8 * band.HALF_W and path_clear(ss, xx, kind, 24.0):
                plant(rng.choice(("great", "great", "great", "blossom", "pine")), ss, xx, kind, rng.uniform(0.7, 1.25))
    elif what == "field":
        drift(s0, xc, kind, rng.randint(450, 800), rng.uniform(11, 17))
    elif what == "garden":
        outcrop(s0, xc, kind, rng.uniform(4, 7))
        for j in range(5):
            aa = rng.uniform(0, 2 * math.pi)
            drift(s0 + 9 * math.cos(aa), xc + 9 * math.sin(aa), kind, rng.randint(40, 80), 2.5)

# streakers: the near field of a fast ride. Every 13-26 m on the fast stretches something stands 5-16 m off the
# ride line -- a tight flower clump, a stone, now and then a Blossom Puff or a Ridge Pine further out -- so at 30 m/s
# the frame's edges change every few frames (the coordinator's Stage 5 note: speed must read in the near field)
ss_ = 30.0
n_streak = 0
while ss_ < L:
    kind = ride_face(ss_)
    calm = min([abs(((ss_ - a_) + L / 2) % L - L / 2) for a_ in ARCH_S] + [1e9]) < 110.0
    if kind in ("top", "bottom") and not calm and not near_crossing(ss_, None, 40.0):
        side = rng.choice((-1.0, 1.0))
        r = rng.random()
        if r < 0.5:
            xx = ride_x(ss_) + side * rng.uniform(5.0, 11.0)
            if abs(xx) < 0.8 * band.HALF_W:
                drift(ss_, xx, kind, rng.randint(18, 34), rng.uniform(1.2, 2.2))
                n_streak += 1
        elif r < 0.78:
            xx = ride_x(ss_) + side * rng.uniform(6.0, 12.0)
            if abs(xx) < 0.8 * band.HALF_W:
                outcrop(ss_, xx, kind, rng.uniform(1.2, 2.6))
                n_streak += 1
        else:
            xx = ride_x(ss_) + side * rng.uniform(15.0, 22.0)
            if abs(xx) < 0.78 * band.HALF_W and path_clear(ss_, xx, kind, 14.0):
                plant(rng.choice(("blossom", "pine")), ss_, xx, kind, rng.uniform(0.4, 0.6))
                n_streak += 1
    ss_ += rng.uniform(13.0, 26.0)
print("STREAKERS", n_streak)

# foreground bands: drifts close beside the ride, every ~120 m, on the face the ride is on
for s0 in np.arange(0.0, L, 120.0):
    kind = ride_face(s0)
    if kind not in ("top", "bottom"):
        continue
    drift(s0 + rng.uniform(0, 60), ride_x(s0) + rng.choice((-1, 1)) * rng.uniform(7, 16), kind, rng.randint(70, 140), 4.0)

TREE_ASSETS = {"great": ("great_tree", "rw_canopy"), "weeper": ("weeper", "rw_weeper"), "blossom": ("blossom", "rw_blossom"),
               "pine": ("pine", "rw_pine")}
for f, pts in fam.items():
    if not pts:
        continue
    asset, prog = TREE_ASSETS[f]
    # the leaf cards (alpha-tested, the depth prepass's dearest geometry) thin with distance (ADR-1548: whole cards removed,
    # the rest grown, 35% at 22 m and 12% at 55 m; Stage 8: the near cards were the p95) and stop at 150 m; beyond it the canopy core,
    # tinted by the same program, is the tree -- at that distance the cards are smaller than their own gaps
    nodes.append(node_points("%s_wood" % f, "%s_wood.glb" % asset, pts, {"baseColor": R(rw.P["bark"][:3]), "roughness": 0.9, "metallic": 0.0},
                             lod={"cull": True, "minScreenRadius": 2.0, "maxDistance": 400.0, "count": 3,
                                  "byScreenSize": False, "distance1": 70.0, "distance2": 180.0}))
    nodes.append(node_points("%s_core" % f, "%s_core.glb" % asset, pts, {"program": prog, "roughness": 0.95, "metallic": 0.0},
                             lod={"cull": True, "minScreenRadius": 1.0, "count": 3, "byScreenSize": False,
                                  "distance1": 160.0, "distance2": 450.0}))
    nodes.append(node_points("%s_leaves" % f, "%s_leaves.glb" % asset, pts,
                             {"program": prog, "roughness": 0.75, "metallic": 0.0, "alphaMode": "mask",
                              "alphaCutoff": 0.5, "doubleSided": True}, castsShadow=False,
                             lod={"cull": True, "minScreenRadius": 4.0, "maxDistance": 150.0, "count": 3,
                                  "byScreenSize": False, "distance1": 22.0, "distance2": 55.0, "thinning": True}))
for k in range(0, len(outcrops), 60000):
    nodes.append(node_points("outcrops%d" % k, "rock_namaqualand_boulder_03.glb", outcrops[k:k + 60000],
                             {"program": "rw_rock", "roughness": 0.85, "metallic": 0.0},
                             lod={"cull": True, "minScreenRadius": 1.0, "count": 2, "byScreenSize": False,
                                  "distance1": 90.0}))
for k in range(0, len(fpts), 60000):
    nodes.append(node_points("flowers%02d" % (k // 60000), "flower_clump.glb", fpts[k:k + 60000],
                             {"program": "rw_flower", "roughness": 0.6, "metallic": 0.0, "doubleSided": True},
                             castsShadow=False, lod={"cull": True, "minScreenRadius": 0.8, "maxDistance": 62.0}))
print("TREES", {k: len(v) for k, v in fam.items()}, "OUTCROPS", len(outcrops), "FLOWERS", len(fpts))

# ---- grass: a corridor round the ride, on the face the ride is on ----------------------------------------------------
gpts = []
for s in np.arange(0.0, L, 1.0):
    kind = ride_face(s)
    faces = [kind] if kind in ("top", "bottom") else ["top", "bottom"]
    xc = ride_x(s) if kind in ("top", "bottom") else None
    for face in faces:
        for _ in range(int(rng.uniform(70, 90)) // len(faces)):
            x = (xc if xc is not None else seg_at(s)[3] * RIM_X) + rng.gauss(0, 18.0)
            if abs(x) > 0.8 * band.HALF_W:
                continue
            ss = s + rng.uniform(0, 1.0)
            p, t, up, sd = surf_any(ss, x, face)
            n = normal_any(ss, x, face)
            sc = rng.uniform(0.55, 0.95)
            gpts.append(point(p - n * 0.03, n.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), rng.uniform(0, 6.283)),
                              (sc, sc, sc * rng.uniform(0.8, 1.2))))
# the hanging gardens: where the ride swings round a rim, the cliff itself is planted -- turf tufts growing straight
# out of the rock face (each face's gravity is its own, so on a cliff "up" is outward) and flowers spilling over both
# lips. The camera passes this at 10-20 m, so it is what a crossing is made of.
n_hang, hang_f = 0, []
for s0, s1, side in CROSSINGS:
    right = side > 0
    for ss in np.arange(s0 - 130.0, s1 + 130.0, 0.6):
        for _ in range(5):  # Stage 8: 9 a step was the crossings' GPU p95; 5 still reads as turf on the face
            sj = ss + rng.uniform(0, 0.6)
            y = rng.uniform(-band.FACE + 0.5, band.FACE - 0.5)
            pos, nrm, t_, u_ = band.rim_point(fr, sj % L, y, right)
            i_ = max(0, min(band.NS, int(round((sj % L) / L * band.NS))))
            pos = pos + t_ * ((sj % L) - i_ * L / band.NS)
            sc = rng.uniform(0.7, 1.15)
            if abs(y) > band.FACE - 12.0 and rng.random() < 0.6:
                # trailers: under each lip the turf hangs -- long tufts pointing down the cliff (away from that
                # face's ground), so from outside the rim reads as a planted curtain, not a smooth fold
                hang = (-u_ * (1.0 if y > 0 else -1.0) + nrm * 0.35).normalized()
                gpts.append(point(pos - nrm * 0.05, hang.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), rng.uniform(0, 6.283)),
                                  (sc * 1.2, sc * 1.2, sc * rng.uniform(2.2, 3.4))))
            else:
                gpts.append(point(pos - nrm * 0.05, nrm.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), rng.uniform(0, 6.283)),
                                  (sc, sc, sc * rng.uniform(0.9, 1.3))))
            n_hang += 1
            if abs(y) > band.FACE - 9.0 and rng.random() < 0.55:
                up_ = (nrm + u_ * (1.0 if y > 0 else -1.0) * 0.6).normalized()
                hang_f.append(point(pos - nrm * 0.05, up_.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), rng.uniform(0, 6.283)),
                                  rng.uniform(1.2, 2.0)))
print("HANGING GARDEN tufts", n_hang, "flowers", len(hang_f))
nodes.append(node_points("flowers_hang", "flower_clump.glb", hang_f, {"program": "rw_flower", "roughness": 0.6, "metallic": 0.0, "doubleSided": True}, castsShadow=False,
                         lod={"cull": True, "minScreenRadius": 0.8, "maxDistance": 62.0}))
for k in range(0, len(gpts), 60000):
    nodes.append(node_points("grass%03d" % (k // 60000), "grass_lite.glb", gpts[k:k + 60000],
                             {"program": "rw_grass", "roughness": 0.95, "metallic": 0.0}, castsShadow=False,
                             lod={"cull": True, "minScreenRadius": 0.8, "maxDistance": 58.0}))
print("GRASS", len(gpts))

# ---- rainbows: arches across the band near the landmarks, on the face the ride is on there ----------------------------
rainbow_nodes = []
def far_from_crossing(s, margin=320.0):
    return all(min((s0 - s) % L, (s - s1) % L) > margin for s0, s1, kind, side in plan if kind in ("over", "back"))


# one arch at the middle of every long stretch on a face: the ride passes under it in each sequence's calm, and it is
# always as far as it can be from a rim crossing
arch_s = list(ARCH_S)
for k, sa_ in enumerate(arch_s):
    face = ride_face(sa_)
    if face not in ("top", "bottom"):
        continue
    pa, ta, ua, sda = band.frame_lerp(fr, sa_ % L)
    if face == "top":
        m = Matrix((sda, ta, ua)).transposed()
        pos = pa + ua * (band.FACE - 6.0)
    else:
        m = Matrix((-sda, ta, -ua)).transposed()
        pos = pa - ua * (band.FACE - 6.0)
    eul = (rw.Q_ZUP_TO_YUP @ m.to_quaternion() @ rw.Q_ZUP_TO_YUP.inverted()).to_euler("XYZ")
    rainbow_nodes.append({"name": "rainbow_%d" % k, "kind": "gltf", "asset": "rainbow_arch.glb", "position": av(pos), "castsShadow": False,
                          # 0.76: both bows' feet land inside the face (primary 87 m, secondary 108 m < the rim's
                          # 112 m), never poking out of a rim where a foot seen from the strand's far side read as a
                          # spectral blob
                          "scale": [0.76, 0.76, 0.76],
                          "rotation": [round(math.degrees(v), 3) for v in eul]})
nodes += rainbow_nodes
print("RAINBOWS", len(rainbow_nodes))
tpts = [q for v in fam.values() for q in v]

# ---- clouds: masses in the knot's free space (clear of the ride), a sea below, banks far out -----------------------
cpts = {"hero": [], "tower": [], "bank": [], "puff": []}
P = np.array(path.P[::6])
lo, hi = P.min(axis=0), P.max(axis=0)
centre = (lo + hi) / 2
extent = float(np.linalg.norm(hi - lo))
tries = 0
HALF = {"hero": 330.0, "tower": 290.0}   # half the mesh's largest extent (engine_assets CLOUD report), metres
while len(cpts["hero"]) + len(cpts["tower"]) < 14 and tries < 6000:
    tries += 1
    c = centre + np.array([rng.uniform(-0.75, 0.75), rng.uniform(-0.75, 0.75), rng.uniform(-0.45, 0.6)]) * extent * 0.6
    kind = "tower" if rng.random() < 0.4 else "hero"
    sc = rng.uniform(0.5, 0.9)
    reach = HALF[kind] * sc
    # never in the camera's way, never through the land: the cloud's own reach plus a margin from both
    if np.linalg.norm(P - c, axis=1).min() < reach + band.HALF_W + 60 or near_path(c, reach + 180):
        continue
    cpts[kind].append(point(Vector(c), Quaternion((0, 0, 1), rng.uniform(0, 6.28)), sc))
sea_z = lo[2] - 380.0
placed_puffs = 0
while placed_puffs < 900:  # the cloud sea: heads packed out to 9 km over the floor, never within reach of the ride
    aa, rr = rng.uniform(0, 2 * math.pi), 9000 * math.sqrt(rng.random())
    c = Vector((centre[0] + rr * math.cos(aa), centre[1] + rr * math.sin(aa), sea_z - rng.uniform(-40, 140)))
    sx_ = rng.uniform(1.6, 3.6)
    if near_path(np.array(c), 250.0 * sx_ + 120.0):
        continue
    placed_puffs += 1
    cpts["puff"].append(point(c, Quaternion((0, 0, 1), rng.uniform(0, 6.28)),
                              (sx_, sx_ * rng.uniform(0.8, 1.2), rng.uniform(0.7, 1.15))))
for k in range(16):  # banks on a ring round the knot
    aa = 2 * math.pi * k / 16 + rng.uniform(-0.1, 0.1)
    c = Vector((centre[0] + 3800 * math.cos(aa), centre[1] + 3800 * math.sin(aa), centre[2] + rng.uniform(-300, 500)))
    cpts["bank"].append(point(c, Quaternion((0, 0, 1), rng.uniform(0, 6.28)),
                              (rng.uniform(2.0, 3.0), rng.uniform(1.4, 2.2), rng.uniform(1.4, 2.4))))
# the floor of the cloud sea: one mesh of billows under the whole world (engine_assets.export_cloud_floor)
nodes.append(node_points("cloud_floor", "cloud_floor.glb",
                         [point(Vector((centre[0], centre[1], sea_z - 120.0)), Quaternion(), 1.0)],
                         {"program": "rw_cloudfloor", "roughness": 1.0, "metallic": 0.0, "unlit": True}, castsShadow=False))
programs.append(look.cloud_floor_program(sea_z - 120.0))
for kind, pts in cpts.items():
    if pts:
        lod_ = {"cull": True}
        if kind == "puff":  # 900 heads: simplified with distance (a cloud's silhouette survives the simplifier)
            lod_ = {"cull": True, "count": 4, "byScreenSize": False, "distance1": 700.0, "distance2": 1500.0,
                    "distance3": 2800.0}
        else:               # the masses and banks: 24-30k triangles each, mostly seen from far
            lod_ = {"cull": True, "count": 3, "byScreenSize": False, "distance1": 900.0, "distance2": 2200.0}
        nodes.append(node_points("cloud_" + kind, "cloud_%s.glb" % kind, pts,
                                 {"program": "rw_cloud", "roughness": 1.0, "metallic": 0.0, "unlit": True},
                                 castsShadow=False, lod=lod_))
print("CLOUDS", {k: len(v) for k, v in cpts.items()})

# ---- scene and project ----------------------------------------------------------------------------------------------
env, lights = look.environment(SUN, rw.to_avgen(Vector((0, 0, 1))))
scene = {"format": "avgen-scene", "version": 1, "name": "rainbow-world",
         "camera": {"mode": 2, "spline": "ride", "fov": 106.0, "orbitSpeed": 0.0},
         "environment": env, "lights": lights, "materialPrograms": programs, "nodes": nodes}
with open(os.path.join(OUT, "rainbow-world.scene.json"), "w") as f:
    json.dump(scene, f)
params = look.post_parameters()
params.update({"camera/splineUp": 1.0, "camera/lookAhead": 70.0, "post/lens/fisheye": 1.0, "post/lens/fisheyeModel": 0.0,
               "camera/far": 30000.0})
# ---- the ride's rhythm. The owner (Stage 4): "definitely want faster camera movements". The clip covers a lot of
# structure in 8 s with quick swoops and rolls, so the base is fast (BASE m/s) and the holds read by contrast:
#   - lap A: brief holds (30% speed) under each rainbow arch, eased over ~150 m either side;
#   - lap B: the holds move to the Great Tree landmarks, and the arches are passed at full speed;
#   - both laps surge to 1.5x into and through every crossing (ramped over 140 m), so the swing round the rim is quick.
# Two laps with different holds make the loop ~2 laps long before it repeats. A looping timeline maps time to splineT:
# a pure function of the transport (seek equals play). Each lap ends with a step key back to 0 (closed spline: 1 == 0).
def _circ(d):
    return abs(((d) + L / 2) % L - L / 2)


def speed_at(s_band, holds):
    w = 1.0
    for s0, s1, side in CROSSINGS:
        mid, half = 0.5 * (s0 + s1), 0.5 * (s1 - s0)
        w = max(w, 1.0 + 0.5 * (1.0 - band.smoothstep(half, half + 140.0, _circ(s_band - mid))))
    hold = 1.0
    for h_ in holds:
        hold = min(hold, 0.4 + 0.6 * band.smoothstep(8.0, 100.0, _circ(s_band - h_)))
    return w * hold


great_s = sorted({round(hs) for hs, xx, face in landmarks})
holds_b = []
for g_ in great_s:  # thin to one hold per ~600 m
    if not holds_b or g_ - holds_b[-1] > 600.0:
        holds_b.append(float(g_))
cam_D = [cam_path.D[k * cam_path.per] for k in range(len(cam_s))] + [cam_path.length]
ride_keys, t_acc = [], 0.0
for lap, holds in enumerate((arch_s, holds_b)):
    for k in range(len(cam_s)):
        ride_keys.append({"time": round(t_acc, 4), "value": round(float(cam_D[k] / cam_path.length), 6), "interp": "linear"})
        t_acc += (cam_D[k + 1] - cam_D[k]) / (a.base_speed * speed_at(cam_s[k], holds))
    ride_keys.append({"time": round(t_acc - 1e-3, 4), "value": 1.0, "interp": "step"})
loop_seconds = round(t_acc, 4)
print("RIDE base %.0f m/s, lap A+B %.1f s, holds A %d B %d" % (a.base_speed, loop_seconds, len(arch_s), len(holds_b)))

# ---- the live project: the ride, five looks (CC 9 selects, CC 10 locks), MIDI and audio --------------------------------
def op_path(program, kind, nth=0):
    """'material/<program>/op/<1-based index>/<kind>/' of the nth op of that kind in the program."""
    prog = [pr for pr in programs if pr["name"] == program][0]
    hits = [k for k, op in enumerate(prog["ops"]) if op["kind"] == kind]
    return "material/%s/op/%d/%s/" % (program, hits[nth] + 1, kind)


def H(h):
    return R(rw.hexlin(h)[:3])


# The looks (03-art-direction.md section 5): each a set of colours for the same roles. Bright Noon is the scene as
# built (LOOK); the others are designed from the references (R3's golden hour, R1's lavender clouds, a storm-cleared
# sky for the most vivid rainbows, and Prism -- R2's spectral land, restrained to the meadow's hues).
L0 = LOOK
STATES = [
    ("Bright Noon", dict(zenith=R(L0["zenith"]), horizon=R(L0["horizon"]), ground=R(L0["ground"]), sun=R(L0["sunColor"]),
                         bounce=R(rw.P["cloud_bounce"][:3]), haze=R(L0["atmosTint"]),
                         cloud0=R(L0["cloud"][0]), cloud1=R(L0["cloud"][1]), cloud2=R(L0["cloud"][2]), cloud3=R(L0["cloud"][3]),
                         meadow0=R(L0["meadow"][0]), meadow1=R(L0["meadow"][1]), meadow2=R(L0["meadow"][2]), meadow3=R(L0["meadow"][3]),
                         shade=R(L0["shade"]))),
    # Golden Hour and Lavender Dusk change the LIGHT and the sky, not the grass (coordinator, Stage 6): a warm key
    # but cool violet fill and ambient, so the shadows go long and blue-violet, and meadow ramps pushed toward blue-
    # green so the warm sun lands on them as green, not mustard. Measured target: the meadow's hue and saturation
    # within reach of Bright Noon's.
    ("Golden Hour", dict(zenith=H("#1E3E9E"), horizon=H("#F0A464"), ground=H("#A8A0CC"), sun=H("#FFC486"), bounce=H("#9C8CD4"),
                         haze=H("#E8A878"), cloud0=H("#5A4A96"), cloud1=H("#B888A8"), cloud2=H("#FFB880"), cloud3=H("#FFD8A8"),
                         meadow0=H("#147A44"), meadow1=H("#2CA244"), meadow2=H("#60C442"), meadow3=H("#9ED84C"), shade=H("#26408C"))),
    ("After the Storm", dict(zenith=H("#0B2A8E"), horizon=H("#6FB6D8"), ground=H("#8E98B8"), sun=H("#FFF0D0"), bounce=H("#9FB0C8"),
                             haze=H("#6E9CD8"), cloud0=H("#3A4068"), cloud1=H("#6E7698"), cloud2=H("#E6DED2"), cloud3=H("#F4EEE4"),
                             meadow0=H("#1E5E2A"), meadow1=H("#3E8E2A"), meadow2=H("#62B03A"), meadow3=H("#8ED04A"), shade=H("#10485A"))),
    ("Lavender Dusk", dict(zenith=H("#2A1E7A"), horizon=H("#C88AD8"), ground=H("#9C90CC"), sun=H("#FFB4C4"), bounce=H("#8A78D2"),
                           haze=H("#8C70D4"), cloud0=H("#4A3A8A"), cloud1=H("#9A7AC0"), cloud2=H("#F6B6A8"), cloud3=H("#FFD6C6"),
                           meadow0=H("#16703E"), meadow1=H("#2A9A48"), meadow2=H("#5CBC4C"), meadow3=H("#9AD858"), shade=H("#262C92"))),
    ("Prism", dict(zenith=H("#1238C0"), horizon=H("#60D0E8"), ground=H("#C8C0F0"), sun=H("#FFF0C0"), bounce=H("#F0B8E0"),
                   haze=H("#6AAAF0"), cloud0=H("#6058C0"), cloud1=H("#B090E0"), cloud2=H("#FFC8A0"), cloud3=H("#FFE6CE"),
                   meadow0=H("#1E7A58"), meadow1=H("#30A852"), meadow2=H("#62C848"), meadow3=H("#98DA4E"), shade=H("#3A3AA0"))),
]
# the cloud floor follows each look: troughs a deeper cloud0, flanks between cloud0 and cloud1, heads cloud2/3
# (Bright Noon keeps the floor program's own stops)
FLOOR_NOON = [R(rw.hexlin(h)[:3]) for h in look.FLOOR_STOPS]
for k_, (n_, c_) in enumerate(STATES):
    # (the sky's ground colour stays pale: it is also the ambient from below, and a deep one turned every shaded face
    # navy -- tested at #2C56C4, #5C80D8 and #8AA6E6, Stage 6. The deep sky below is the cloud floor's troughs.)
    if k_ == 0:
        fl = FLOOR_NOON
    else:
        c0, c1, c2, c3 = (np.array(c_["cloud%d" % i]) for i in range(4))
        z_, h_ = np.array(c_["zenith"]), np.array(c_["horizon"])
        # troughs are the sky showing between the heads; flanks shade toward the look's cloud shadow
        fl = [list(np.round(0.9 * z_ + 0.1 * h_, 4)), list(np.round(0.5 * z_ + 0.25 * h_ + 0.25 * c0, 4)),
              list(np.round(c2 * 0.92, 4)), list(c3)]
    for i in range(4):
        c_["floor%d" % i] = [float(v) for v in fl[i]]
cloud_ramp = op_path("rw_cloud", "oklabRamp")
floor_ramp = op_path("rw_cloudfloor", "oklabRamp")
bindings = [{"role": "zenith", "target": "env/sky/zenithColor"}, {"role": "horizon", "target": "env/sky/horizonColor"},
            {"role": "ground", "target": "env/sky/groundColor"}, {"role": "horizon", "target": "scene/fogColor"},
            {"role": "sun", "target": "lights/sun/color"}, {"role": "bounce", "target": "lights/cloudsea/color"},
            {"role": "haze", "target": "post/look/atmosphericTint"}]
g = LOOK["cloudGain"]
for k, c in enumerate(("constant", "constant2", "constant3", "constant4")):
    bindings.append({"role": "cloud%d" % k, "target": cloud_ramp + c, "gain": g})
    bindings.append({"role": "floor%d" % k, "target": floor_ramp + c, "gain": g})
    for prog in ("rw_ground", "rw_ground_lip", "rw_grass"):
        bindings.append({"role": "meadow%d" % k, "target": op_path(prog, "oklabRamp") + c})
# the ground program's shade constant (the first `constant` op after the cavity input)
bindings.append({"role": "shade", "target": op_path("rw_ground", "constant", 1) + "constant"})
bindings.append({"role": "shade", "target": op_path("rw_ground_lip", "constant", 1) + "constant"})
palette = {"states": [{"name": n, "colors": c, "scalars": {}} for n, c in STATES], "bindings": bindings,
           "selector": {"select": 0, "transition": 8.0, "lock": False, "hold": 60.0,
                        "advance": {"signal": "beat.phrasePulse", "threshold": 0.5, "order": [0, 1, 2, 3, 4],
                                    "change": {"signals": ["sonic.energy", "sonic.brightness", "sonic.density"],
                                               "fast": 3.0, "slow": 24.0, "threshold": 0.06, "rearm": 0.03,
                                               "primaryTimeout": 30.0, "maxHold": 120.0}}}}

# MIDI (the house layout: CC 9 look, CC 10 lock) and the controls this world has. Every control rests at 0 = the
# world as built, and turns one way. Documented the same in examples/index.json:
#   CC 1  glow        the rainbows' light, up to 3x (nodes/rainbow_*/emissiveBoost)
#   CC 2  lens        fisheye to rectilinear (post/lens/fisheye 1 -> 0)
#   CC 5  haze        more distance haze (post/look/atmospheric +0.5)
#   CC 6  light       brighter (camera/exposure/compensation +1.2 EV)
#   CC 7  colour      more saturated (post/grade/saturation +0.5)
#   CC 9  look        Bright Noon, Golden Hour, After the Storm, Lavender Dusk, Prism (palette/select 0-4)
#   CC 10 look lock   hold the current look (palette/lock); unlocked, the look advances on phrase boundaries
#                     (beat.phrasePulse) or a big change in the music, holding each at least 60 s
# Audio (live input, no file): response.sustain and response.kick light the rainbows, response.level blooms the air,
# response.hat lifts saturation, response.bass lifts the warm bounce light from the cloud sea.
CC = [(1, "glow"), (2, "lens"), (5, "haze"), (6, "light"), (7, "colour"), (9, "palette"), (10, "paletteLock")]
control = {"midi": {"enabled": True, "filter": "*", "bindings": [
    {"source": "*", "channel": -1, "kind": "cc", "number": n, "signal": sig} for n, sig in CC]}}
ctl = {"attackMs": 150, "decayMs": 150}
routes = [{"source": "timeline.ride", "target": "camera/splineT", "amount": 1.0, "op": "replace", "polarity": "unipolar"},
          {"source": "control.palette", "target": "palette/select", "amount": 4.0, "op": "add", "polarity": "unipolar", "chain": ctl},
          {"source": "control.paletteLock", "target": "palette/lock", "amount": 1.0, "op": "add", "polarity": "unipolar", "chain": ctl},
          {"source": "control.lens", "target": "post/lens/fisheye", "amount": -1.0, "op": "add", "polarity": "unipolar", "chain": ctl},
          {"source": "control.haze", "target": "post/look/atmospheric", "amount": 0.5, "op": "add", "polarity": "unipolar", "chain": ctl},
          # every control rests at 0 = the world as built (no MIDI is the default state), and turns one way
          {"source": "control.light", "target": "camera/exposure/compensation", "amount": 1.2, "op": "add", "polarity": "unipolar", "chain": ctl},
          {"source": "control.colour", "target": "post/grade/saturation", "amount": 0.5, "op": "add", "polarity": "unipolar", "chain": ctl}]
# audio: the rainbows breathe with the music, kicks send a pulse of light through them, the air glows with the level.
# Bounded and smoothed; with no audio and no MIDI every amount is at rest and the world is as built.
for rn in rainbow_nodes:
    nm = rn["name"]
    # Stage 7 with Feline Footwear: at 0.6/0.8 the arches' pulse measured +-3% (lost in the camera's own motion);
    # doubled, so a kick reads as a flash of the bow and a held chord as its swell
    routes += [{"source": "response.sustain", "target": "nodes/%s/emissiveBoost" % nm, "amount": 1.2, "op": "add",
                "polarity": "unipolar", "chain": {"attackMs": 600, "decayMs": 2500}},
               {"source": "response.kick", "target": "nodes/%s/emissiveBoost" % nm, "amount": 1.8, "op": "add",
                "polarity": "unipolar", "chain": {"attackMs": 15, "decayMs": 450}},
               {"source": "control.glow", "target": "nodes/%s/emissiveBoost" % nm, "amount": 2.0, "op": "add",
                "polarity": "unipolar", "chain": ctl}]
routes += [{"source": "response.level", "target": "post/bloom/intensity", "amount": 0.12, "op": "add", "polarity": "unipolar",
            "chain": {"attackMs": 300, "decayMs": 1500}},
           {"source": "response.kick", "target": "post/bloom/intensity", "amount": 0.1, "op": "add", "polarity": "unipolar",
            "chain": {"attackMs": 10, "decayMs": 350}},
           {"source": "response.hat", "target": "post/grade/saturation", "amount": 0.06, "op": "add", "polarity": "unipolar",
            "chain": {"attackMs": 20, "decayMs": 300}},
           {"source": "response.bass", "target": "lights/cloudsea/intensity", "amount": 0.5, "op": "add", "polarity": "unipolar",
            "chain": {"attackMs": 120, "decayMs": 900}}]
proj_path = os.path.abspath(a.project) if a.project else os.path.join(OUT, "rainbow-world.json")
scene_rel = os.path.relpath(os.path.join(OUT, "rainbow-world.scene.json"), os.path.dirname(proj_path))
project = {
    "format": "avgen-project", "version": 4, "app": {"name": "Rainbow World"},
    "assets": {"scene": {"kind": "composition", "path": scene_rel}},
    "sonic": {"live": True, "response": {"sensitivity": 0.5, "transient": 0.5, "sustain": 0.5, "attack": 1.0, "release": 1.0}},
    "sonicScene": {"category": "world", "id": "rainbow-world", "title": "Rainbow World",
                   "thesis": "A painterly meadow knotted through an all-round sky: the ride follows the land's own gravity "
                             "round a twisting loop and over its rims, under rainbow arches, among sculpted clouds."},
    "parameters": params,
    "sources": [{"kind": "timeline", "name": "ride", "settings": {"loopLength": loop_seconds, "keys": ride_keys}}],
    "routes": routes,
    "palette": palette,
    "control": control,
    "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video", "path": "renders/rainbow-world.mp4"},
}
with open(proj_path, "w") as f:
    json.dump(project, f, indent=1)
report = {"loopMetres": L, "cameraMetres": cam_path.length, "loopSeconds": loop_seconds, "baseSpeed": a.base_speed,
          "grass": len(gpts), "flowers": len(fpts), "trees": len(tpts), "rimRocks": sum(len(v) for v in by.values()),
          "rainbows": len(rainbow_nodes), "clouds": {k: len(v) for k, v in cpts.items()}, "bandChunks": len(band_nodes)}
with open(os.path.join(OUT, "world_report.json"), "w") as f:
    json.dump(report, f, indent=1)
print("done", json.dumps(report))
