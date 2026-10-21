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
import music  # noqa: E402
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
ap.add_argument("--base-speed", type=float, default=36.0, help="the ride's base speed, m/s")
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
# (gate 4, "twitchy": the swing round the rim was an ellipse 40 m out and 11 m up -- a ~24 m radius at its side, a
# 30-degree turn every 15 m; 44 m out and 16 m up rounds it toward a circle of ~35-50 m)
RIM_X, RIM_A, RIM_LIFT = 108.0, 44.0, 16.0
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


# ---- Rainbow World 2: the ride as a roller coaster (the owner, gate 2: "flying / racing around this contorted
# distorted tracking, flipping through the air, diving back down ... not really stopping to take in the scenery").
# No holds. The ride races low over the land and, at the music's big moments, launches: an airtime arc off the band
# along its own up -- launch, airtime, dive back down -- some with a barrel roll at the apex, timed so the apex lands on
# the moment (the drop's at 47.85 s). Speed falls a little through the airtime and rushes back on the dive. Still a
# pure function of the timeline (seek equals play).
HOPS = []   # (station of the apex, length m, height m, barrel rolls), filled below once the ride's timing is known
BIG_CROSSINGS = set()   # the rim crossings a moment falls in: swung far out instead of hopped


def _circ(d):
    return abs(((d) + L / 2) % L - L / 2)


def hop_at(s):
    """(height, roll angle, airtime fraction 0..1) of the hops at band station s."""
    h, roll, frac = 0.0, 0.0, 0.0
    for sc, ln, hh, turns in HOPS:
        d = ((s - sc) + L / 2) % L - L / 2
        if abs(d) < ln / 2:
            u = (d + ln / 2) / ln
            f = math.sin(math.pi * u) ** 2
            h = max(h, hh * f)
            frac = max(frac, f)
            roll += 2 * math.pi * turns * band.smoothstep(0.3, 0.7, u)
    return h, roll, frac


def cam_lift(s):
    """Low and fast -- 4.5-7.5 m, so the grass, flowers and stones beside the line sell the speed -- plus the hops."""
    u = 0.5 + 0.5 * math.sin(2 * math.pi * s / band.per(380.0))
    return 4.5 + 3.0 * u * u + hop_at(s)[0]


def ride(s):
    """(position, up) of the ride at station s."""
    s0, s1, kind, side = seg_at(s)
    if kind in ("top", "bottom"):
        x = ride_x(s)
        n = normal_any(s, x, kind)
        p, t, up, sd = surf_any(s, x, kind)
        # (gate 4, "twitchy": the face path rose off the face's own normal, the rim's ellipse off the band's up, so the
        # two met at an angle; over the last 90 m before a crossing (and the first after) the lift turns onto the
        # band's up, and the ellipse starts from the same lift, so the line runs on without a kink)
        d_cross = min([min((s0_ - s) % L, (s - s1_) % L) for s0_, s1_, k_, sd_ in plan if k_ in ("over", "back")])
        w_up = band.smoothstep(90.0, 0.0, d_cross)
        u_face = Vector(up) if kind == "top" else -Vector(up)
        lift_dir = (n * (1.0 - w_up) + u_face * w_up).normalized()
        return p + lift_dir * cam_lift(s), n
    # a rim crossing: an ellipse round the cliff from one face to the other, its up the ellipse's outward normal
    w = band.smoothstep(s0, s1, s % L)
    phi = math.pi * w if kind == "over" else math.pi * (1.0 - w)   # 0 = on top, pi = on the bottom
    p, t, u, sd = band.frame_lerp(fr, s % L)
    xr = side * RIM_X                                              # the rim in band coordinates
    big = s0 in BIG_CROSSINGS     # Rainbow World 2: a moment's crossing swings far out round the rim (airtime)
    rim_a = 100.0 if big else RIM_A
    # the lift grows from the face path's own at either end to RIM_LIFT at the swing's widest
    lift_ = 42.0 if big else cam_lift(s) + (RIM_LIFT - cam_lift(s)) * math.sin(phi)
    b_top = band.FACE + band.face_height(s % L, xr, band.TOP_HILLS, 0.0) + lift_
    b_bot = band.FACE + band.face_height(s % L, -xr, band.BOTTOM_HILLS, 1.3) + lift_
    c = math.cos(phi)
    # (gate 4, "twitchy": b switched from the top face's height to the bottom's at the swing's midpoint, a kink of
    # up to 39 degrees where the two faces' hills differ; blended through the swing, the curve stays smooth)
    b = b_bot + (b_top - b_bot) * band.smoothstep(-1.0, 1.0, c)
    X = xr + side * rim_a * math.sin(phi)
    Y = b * c
    nrm = (sd * (side * math.sin(phi) / rim_a) + u * (c / b)).normalized()
    return p + sd * X + u * Y, nrm


CAM_STEP = 15.0
cam_s = np.arange(0.0, L, CAM_STEP)
BASE = a.base_speed


def speed_at(s_band):
    """The ride's speed factor: a surge to 1.5x round every rim crossing, airtime a little slower, the rush after."""
    w = 1.0
    for s0, s1, kind_, side in plan:
        if kind_ not in ("over", "back"):
            continue
        mid, half = 0.5 * (s0 + s1), 0.5 * (s1 - s0)
        w = max(w, 1.0 + 0.5 * (1.0 - band.smoothstep(half, half + 140.0, _circ(s_band - mid))))
    return w * (1.12 - 0.34 * hop_at(s_band)[2])


def ride_times():
    pts = [np.array(ride(s_)[0]) for s_ in cam_s]
    t_, out = 0.0, []
    for k in range(len(cam_s)):
        out.append(t_)
        nxt = pts[(k + 1) % len(pts)]
        t_ += float(np.linalg.norm(nxt - pts[k])) / (BASE * speed_at(cam_s[k]))
    return np.array(out), t_


def in_face(sc, ln):
    """The whole hop, with 50 m either side, on one face (consecutive plan entries of one face are one stretch)."""
    kind0 = seg_at(sc)[2]
    return kind0 in ("top", "bottom") and all(seg_at(sc + d_)[2] == kind0
                                               for d_ in np.arange(-ln / 2 - 30.0, ln / 2 + 30.1, 15.0))


# The owner after gate 3 (20-camera-note.md): no flips on a timer -- "only if it makes sense whilst following the
# geometry" -- and "more fluid ... more gliding". So no hops and no scheduled rolls: the camera's roll is the land's
# own (the band's twist, the rim crossings), and the ride glides: its path is the low line over the meadow, smoothed
# over about 60 m so it floats over small rises instead of tracing them, never closer than 3.5 m to the ground; its
# up is smoothed the same way, so the roll eases; and its speed breathes with the slope -- faster going down, a
# float over the crests -- smoothed over about 90 m so it never jerks.
cam_raw, cam_upraw, cam_lift_raw = [], [], []
for s in cam_s:
    p, n = ride(s)
    cam_raw.append(np.array(p))
    cam_upraw.append(np.array(n) / max(1e-9, np.linalg.norm(n)))
cam_raw, cam_upraw = np.array(cam_raw), np.array(cam_upraw)
on_face = np.array([seg_at(s_)[2] in ("top", "bottom") for s_ in cam_s])
# how far each sample is from a crossing, in samples: the smoothing narrows to nothing there, where the path swings
# round the rim and smoothing would cut the corner into the cliff
near_x = np.full(len(cam_s), 99)
idx = np.where(~on_face)[0]
for i_ in range(len(cam_s)):
    if len(idx):
        d_ = np.abs(((idx - i_) + len(cam_s) // 2) % len(cam_s) - len(cam_s) // 2).min()
        near_x[i_] = d_


def gsmooth(arr, sigma_at):
    n_ = len(arr)
    out = np.empty_like(arr)
    for i_ in range(n_):
        sg = sigma_at[i_]
        if sg < 0.3:
            out[i_] = arr[i_]
            continue
        r_ = int(3 * sg) + 1
        ks = np.arange(-r_, r_ + 1)
        w_ = np.exp(-0.5 * (ks / sg) ** 2)
        out[i_] = (arr[(i_ + ks) % n_] * w_[:, None]).sum(axis=0) / w_.sum() if arr.ndim == 2 else \
            (arr[(i_ + ks) % n_] * w_).sum() / w_.sum()
    return out


sig = np.clip((near_x - 2.0) / 3.0, 0.0, 1.0) * 4.0          # 4 samples (60 m) on the faces, 0 at a crossing
# (gate 4, "twitchy": unsmoothed, the face path met each rim's ellipse with a kink -- 40-63 degrees between two 15 m
# segments (ride() now joins them without one), and the knot's own geometry is jagged at 15 m near the rims. One sample
# (15 m) of smoothing on the faces, one and a half through the crossings, takes the rest out; RIM clearance below
# checks the line stays clear of the rim it swings round.)
sig = np.maximum(sig, np.where(on_face, 1.0, 1.5))
cam_sm = gsmooth(cam_raw, sig)
up_sm = gsmooth(cam_upraw, sig)
up_sm /= np.linalg.norm(up_sm, axis=1)[:, None]
pushed = 0
for i_ in range(len(cam_s)):
    if not on_face[i_]:
        continue
    ground = np.array(surf_any(cam_s[i_], ride_x(cam_s[i_]), seg_at(cam_s[i_])[2])[0])
    h_ = float(np.dot(cam_sm[i_] - ground, cam_upraw[i_]))
    if h_ < 3.5:
        cam_sm[i_] += cam_upraw[i_] * (3.5 - h_)
        pushed += 1
# through a crossing, smoothing rounds the swing a little into the rim's corner: hold the line RIM_MARGIN off the
# band's cross-section there (its faces at FACE plus the hills, its edge at HALF_W), pushing straight out from it
RIM_MARGIN = 4.5   # (the swing starts and ends at the face path's own lift, 4.5-7.5 m)


def rim_gap(i_, q):
    """(distance from q to the band's cross-section at sample i_, the outward unit direction)."""
    pp, tt_, uu, sdd = band.frame_lerp(fr, cam_s[i_] % L)
    d_ = np.array(q) - np.array(pp)
    Xs, Ys = float(np.dot(d_, sdd)), float(np.dot(d_, uu))
    xc = max(-band.HALF_W, min(band.HALF_W, Xs))
    hill = band.face_height(cam_s[i_] % L, xc, band.TOP_HILLS, 0.0) if Ys >= 0 else \
        band.face_height(cam_s[i_] % L, -xc, band.BOTTOM_HILLS, 1.3)
    yl = band.FACE + hill
    yc = max(-yl, min(yl, Ys))
    dx_, dy_ = Xs - xc, Ys - yc
    dist = math.hypot(dx_, dy_)
    if dist < 1e-6:     # inside the slab: out through the nearer side, the depth as a negative distance
        if band.HALF_W - abs(Xs) > yl - abs(Ys):
            return -(yl - abs(Ys)), np.array(uu) * math.copysign(1.0, Ys)
        return -(band.HALF_W - abs(Xs)), np.array(sdd) * math.copysign(1.0, Xs)
    return dist, (np.array(sdd) * dx_ + np.array(uu) * dy_) / dist


def rim_push():
    n_ = 0
    for i_ in range(len(cam_s)):
        if on_face[i_]:
            continue
        gap, out_ = rim_gap(i_, cam_sm[i_])
        if gap < RIM_MARGIN:
            cam_sm[i_] += out_ * (RIM_MARGIN - gap)
            n_ += 1
    return n_


# push and re-smooth the crossings a few times, so the line settles both clear and smooth (a push alone is a kink)
rim_zone = near_x <= 2
for _it in range(5):
    rim_pushed = rim_push()
    cam_sm[rim_zone] = gsmooth(cam_sm, np.where(rim_zone, 1.0, 0.0))[rim_zone]
rim_pushed = rim_push()
print("RIM pushed %d crossing samples out to %.0f m (after settling)" % (rim_pushed, RIM_MARGIN))
print("GLIDE pushed %d samples up to 3.5 m" % pushed)


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

# Rainbow World 2, gate 5 (the owner: "fix trees clumped together in some areas"): the scatter had trees standing
# inside each other -- 3 pairs under 1 m apart, 9 under 3 m, 55 whose crowns overlapped by more than half. A
# Poisson-disk pass over every tree of every family, in planting order (the deliberate rows and groves are planted
# first, so they keep their shape): a tree is dropped if it stands closer to an earlier one than SPACING x the sum of
# their crown radii. Crowns may still touch and overlap a little -- a grove still reads as a grove -- but no tree
# stands in another.
SPACING = 0.75
_CROWN = {"great": 20.5, "blossom": 9.6, "pine": 6.0, "weeper": 11.0}
_kept, _dropped = [], 0
for f_ in ("great", "weeper", "blossom", "pine"):          # the landmarks' family first
    keep_ = []
    for q in fam[f_]:
        c_, r_ = np.array(q[:3]), _CROWN[f_] * q[8]
        if any(np.linalg.norm(c_ - c2) < SPACING * (r_ + r2) for c2, r2 in _kept):
            _dropped += 1
            continue
        _kept.append((c_, r_))
        keep_.append(q)
    fam[f_] = keep_
print("TREES spaced: %d dropped, %d kept" % (_dropped, len(_kept)))
TREE_ASSETS = {"great": ("great_tree", "rw_canopy"), "weeper": ("weeper", "rw_weeper"), "blossom": ("blossom", "rw_blossom"),
               "pine": ("pine", "rw_pine")}
for f, pts in fam.items():
    if not pts:
        continue
    asset, prog = TREE_ASSETS[f]
    LEAF_PX, LEAF_CULL_PX = (130.0, 60.0, 28.0), 12.0     # projected radius, px, at which each leaf level is taken
    # the leaf cards (alpha-tested, the depth prepass's dearest geometry) thin with distance (ADR-1548: whole cards removed,
    # the rest grown, 35% at 22 m and 12% at 55 m; Stage 8: the near cards were the p95) and stop at 150 m; beyond it the canopy core,
    # tinted by the same program, is the tree -- at that distance the cards are smaller than their own gaps.
    # Rainbow World 2 (17-slow-frames.md): a fourth, thinner level from 90 m. The cards at 90-150 m were the arch holds'
    # GPU tail (about 7 ms: thousands of sub-pixel cards, every one alpha-tested in the prepass); stopping them at 90 m
    # left bare branches, a thinner level keeps the canopy's read for 6-10 ms less.
    # Rainbow World 2, gate 5 (the owner: "tree popping ... a lot of twitchy trees"; 25-tree-popping.md): the levels
    # switched by DISTANCE, the same for a 5 m pine and a 24 m Great Tree, and the leaves' first switch (to 35% of the
    # cards) came at 22 m, when a Great Tree is ~440 px across the frame; the leaves stopped dead at 150 m (a 65 px
    # canopy vanishing), and every tree near a threshold flipped back and forth as the ride wove past it. Now every
    # level switches by SCREEN SIZE (the same apparent change for every tree, and only once it is small), each tree at
    # its own offset (spread: a stand changes over a band, not on one frame), with a dead zone where the renderer
    # allows one (live; an offline render carries no history).
    STAB = {"spread": 0.35, "hysteresis": 0.2}
    nodes.append(node_points("%s_wood" % f, "%s_wood.glb" % asset, pts, {"baseColor": R(rw.P["bark"][:3]), "roughness": 0.9, "metallic": 0.0},
                             lod=dict({"cull": True, "minScreenRadius": 2.0, "count": 3,
                                       "byScreenSize": True, "distance1": 80.0, "distance2": 25.0}, **STAB)))
    nodes.append(node_points("%s_core" % f, "%s_core.glb" % asset, pts, {"program": prog, "roughness": 0.95, "metallic": 0.0},
                             lod=dict({"cull": True, "minScreenRadius": 1.0, "count": 3, "byScreenSize": True,
                                       "distance1": 60.0, "distance2": 20.0}, **STAB)))
    nodes.append(node_points("%s_leaves" % f, "%s_leaves.glb" % asset, pts,
                             {"program": prog, "roughness": 0.75, "metallic": 0.0, "alphaMode": "mask",
                              "alphaCutoff": 0.5, "doubleSided": True}, castsShadow=False,
                             lod=dict({"cull": True, "minScreenRadius": LEAF_CULL_PX, "count": 4,
                                       "byScreenSize": True, "distance1": LEAF_PX[0], "distance2": LEAF_PX[1],
                                       "distance3": LEAF_PX[2], "thinning": True}, **STAB)))
for k in range(0, len(outcrops), 60000):
    nodes.append(node_points("outcrops%d" % k, "rock_namaqualand_boulder_03.glb", outcrops[k:k + 60000],
                             {"program": "rw_rock", "roughness": 0.85, "metallic": 0.0},
                             lod={"cull": True, "minScreenRadius": 1.0, "count": 2, "byScreenSize": False,
                                  "distance1": 90.0}))
for k in range(0, len(fpts), 60000):
    nodes.append(node_points("flowers%02d" % (k // 60000), "flower_clump.glb", fpts[k:k + 60000],
                             {"program": "rw_flower", "roughness": 0.6, "metallic": 0.0, "doubleSided": True},
                             castsShadow=False, lod={"cull": True, "minScreenRadius": 0.8, "maxDistance": 45.0}))
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
                         lod={"cull": True, "minScreenRadius": 0.8, "maxDistance": 45.0}))
for k in range(0, len(gpts), 60000):
    nodes.append(node_points("grass%03d" % (k // 60000), "grass_lite.glb", gpts[k:k + 60000],
                             {"program": "rw_grass", "roughness": 0.95, "metallic": 0.0}, castsShadow=False,
                             lod={"cull": True, "minScreenRadius": 0.8, "maxDistance": 42.0}))
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
    # Rainbow World 2: each bow its own gltf node drawn through the rw_rainbow programs (ADR-1572), so light can travel
    # along it; the shape is the Stage 2-8 tube. 0.76: both bows' feet land inside the face (primary 87 m, secondary
    # 108 m < the rim's 112 m), never poking out of a rim where a foot seen from the strand's far side read as a
    # spectral blob
    for bow in ("p", "s"):
        rn_ = {"name": "rainbow_%d%s" % (k, bow), "kind": "gltf",
               "asset": "rainbow_%s.glb" % ("primary" if bow == "p" else "secondary"),
               "position": av(pos), "castsShadow": False,
               "scale": [0.76, 0.76, 0.76], "rotation": [round(math.degrees(v), 3) for v in eul]}
        if bow == "p":
            rn_["program"] = "rw_rainbow"   # the faint secondary keeps its textures: no program cost (17 section 6)
        rainbow_nodes.append(rn_)
nodes += rainbow_nodes
print("RAINBOWS", len(rainbow_nodes))
tpts = [q for v in fam.values() for q in v]

# ---- Rainbow World 2: the flight (the owner's second camera note, 20-camera-note.md; the design is 21-camera-research.md
# section 3). The gliding low line above is the ground line; the ride now flies over it like a plane: it soars over open
# meadow and dives back down to skim the arches, the crossings and the trees; it banks into its turns as a coordinated
# turn banks (lift = gravity + the turn's pull, gravity the land's own up) and trades speed for height; it rolls only
# where a phrase and a climb both earn it, never at a bow (in practice: not at all). A spring (second-order dynamics,
# run forward then back round the loop) gives
# the height and the bank their weight. All baked into the spline and the timeline: seek equals play.
LAP = 190.0          # the owner: "a little faster" -- 11% on the glide's 211 s lap (the ride loops past the song's end)
G = 9.81
SOAR = 30.0          # metres over the ground line at the top of a soar
ROLL_SPRING_M = 320.0   # the roll's spring: about one natural period per this many metres
ROOM = 8.0           # the owner chose "more soar" (gate 4): 8 m of room is enough to soar
n_s = len(cam_s)
sm1 = lambda i_: (i_ - 1) % n_s  # noqa: E731
sp1 = lambda i_: (i_ + 1) % n_s  # noqa: E731


def spring(x, f_per_m, zeta=1.0):
    """t3ssel8r's second-order dynamics (y + k1 y' + k2 y'' = x), semi-implicit, over the periodic samples: run three
    laps forward so the seam is settled, then the result backward the same way (zero phase: it anticipates)."""
    f_ = f_per_m * CAM_STEP
    k1, k2 = zeta / (math.pi * f_), 1.0 / (2 * math.pi * f_) ** 2

    def run(v):
        y, yd, out = v[0], 0.0, np.empty_like(v)
        for lap_ in range(3):
            for i_ in range(len(v)):
                y += yd
                yd += (v[i_] - y - k1 * yd) / k2
                if lap_ == 2:
                    out[i_] = y
        return out
    return run(run(np.asarray(x, float))[::-1])[::-1]


def slope_limit(e, per_sample):
    e = e.copy()
    for _ in range(2):
        for i_ in list(range(n_s)) * 2:
            e[i_] = min(e[i_], e[sm1(i_)] + per_sample)
        for i_ in list(reversed(range(n_s))) * 2:
            e[i_] = min(e[i_], e[sp1(i_)] + per_sample)
    return e


# 1. the room the flight has: low into the crossings (the rim's ellipse starts 11 m up), under the bows, and round
#    every tree -- under its canopy, or (in the open, when its crown is within reach) over it. Tree envelopes come from
#    the asset bounds (radius, canopy bottom, top, at scale 1). Floors and caps become cones that a 12-degree climb can
#    follow, so the flight can always meet them.
ENVELOPE = {"great": (20.5, 9.0, 27.0), "blossom": (9.6, 10.0, 23.3), "pine": (6.0, 6.6, 25.6), "weeper": (11.0, 0.0, 13.4)}
CLIMB = CAM_STEP * math.tan(math.radians(12.0))
cap = np.full(n_s, SOAR + 20.0)
cap[near_x < 6] = 14.0     # a dive into a crossing bottoms out 14 m over the low line...
cap[near_x < 2] = 0.0      # ...and meets the rim's ellipse only in the last 30 m
for sa_ in ARCH_S:          # under a bow: the primary's crown is ~66 m over the face
    for i_ in range(n_s):
        if _circ(cam_s[i_] - sa_) < 120.0:
            cap[i_] = min(cap[i_], 24.0)
over = np.zeros(n_s)       # the height that clears every crown near the line
under = np.full(n_s, 1e9)  # the height that stays under every canopy near the line
for f_, pts_ in fam.items():
    if not pts_:
        continue
    Rr, lo_, top_ = ENVELOPE[f_]
    T_ = np.array([(q[0], -q[2], q[1]) for q in pts_])       # AV Gen back to Blender
    S_ = np.array([q[8] for q in pts_])
    for i_ in range(n_s):
        d_ = cam_sm[i_] - T_
        h0 = d_ @ up_sm[i_]
        lat = np.linalg.norm(d_ - h0[:, None] * up_sm[i_][None, :], axis=1)
        near = (lat < Rr * S_ + 6.0) & (top_ * S_ + 4.0 - h0 > 0.0)
        if near.any():
            over[i_] = max(over[i_], float((top_ * S_[near] + 4.0 - h0[near]).max()))
            under[i_] = min(under[i_], max(0.0, float((lo_ * S_[near] - 4.0 - h0[near]).min())))


def cone_min(c_):
    out = c_.copy()
    for k_ in range(1, 40):
        out = np.minimum(out, np.minimum(np.roll(c_, k_), np.roll(c_, -k_)) + k_ * CLIMB)
    return out


def cone_max(f_):
    out = f_.copy()
    for k_ in range(1, 40):
        out = np.maximum(out, np.maximum(np.roll(f_, k_), np.roll(f_, -k_)) - k_ * CLIMB)
    return out


trees_ = over > 0
floor = np.where(trees_ & (over <= SOAR + 8.0), over, 0.0)       # go over the crowns the soar can reach
cap = np.where(trees_ & (over > SOAR + 8.0), np.minimum(cap, under), cap)
for _ in range(20):                                               # where over and under cannot both be met: under
    cap_c, floor_c = cone_min(cap), cone_max(floor)
    bad = np.where(floor_c > cap_c + 1e-6)[0]
    if not len(bad):
        break
    for i_ in bad:
        for k_ in range(-40, 41):
            j_ = (i_ + k_) % n_s
            if floor[j_] > 0 and floor[j_] - abs(k_) * CLIMB > cap_c[i_]:
                floor[j_] = 0.0
                cap[j_] = min(cap[j_], under[j_])
cap_c, floor_c = cone_min(cap), cone_max(floor)
print("ROOM soar-able (cap >= %.0f m) %.0f%%, over the crowns at %d samples, under the canopies at %d"
      % (ROOM, 100 * (cap_c >= ROOM).mean(), int((floor > 0).sum()), int((trees_ & (floor == 0)).sum())))

# 2. the soar's target: high wherever there is room; the spring gives it weight, the cones keep it clear
target = np.where(cap_c >= ROOM, np.minimum(SOAR, cap_c), 0.0)
target = np.maximum(target, floor_c)


def flight_times(speed):
    seg_ = np.linalg.norm(np.roll(cam_flight, -1, axis=0) - cam_flight, axis=1)
    base_ = float((seg_ / speed).sum() / LAP)
    return np.concatenate([[0.0], np.cumsum(seg_ / (base_ * speed))[:-1]]), base_


def solve_altitude(tgt):
    e_ = np.clip(spring(tgt, 1.0 / 280.0), floor_c, cap_c)
    e_ = np.clip(gsmooth(e_, np.full(n_s, 1.5)), floor_c, cap_c)
    return np.clip(e_, 0.0, None)


climb_g = np.array([np.dot(cam_sm[sp1(i_)] - cam_sm[sm1(i_)], up_sm[i_]) / (2 * CAM_STEP) for i_ in range(n_s)])
surge = np.ones(n_s)
for i_, s_ in enumerate(cam_s):
    for s0, s1, kind_, side in plan:
        if kind_ in ("over", "back"):
            mid, half = 0.5 * (s0 + s1), 0.5 * (s1 - s0)
            # (a glider's surge: 1.15x round the rim, not 1.45x -- the angular rates scale with the speed)
            surge[i_] = max(surge[i_], 1.0 + 0.15 * (1.0 - band.smoothstep(half, half + 160.0, _circ(s_ - mid))))


def speed_profile(e_, base_):
    """The land's slope (faster down, a float over crests), the energy trade on the soar with real g, the surge."""
    slope_ = np.clip(1.0 - 5.0 * climb_g, 0.78, 1.3)
    v2 = base_ ** 2 - 2.0 * G * (e_ - e_.mean())
    energy = np.sqrt(np.clip(v2, 0.45 * base_ ** 2, None)) / base_
    return gsmooth(slope_ * energy * surge, np.full(n_s, 6.0))


DROP = 47.85
e_alt = solve_altitude(target.copy())
cam_flight = cam_sm + up_sm * e_alt[:, None]
SPEED = speed_profile(e_alt, 30.0)
for _it in range(3):                           # settle the base speed on the lap
    times_f, BASE = flight_times(SPEED)
    SPEED = speed_profile(e_alt, BASE)
times_f, BASE = flight_times(SPEED)
i_drop = int(np.argmin(np.abs(times_f - DROP)))
print("FLIGHT over 10 m: %.0f%% of the loop; soar %.0f%% of the loop over 15 m, max %.1f m, mean %.1f m; at the drop (%d m) %.1f m; base %.1f m/s, "
      "speed factor %.2f-%.2f" % (100.0 * (e_alt > 10).mean(), 100.0 * (e_alt > 15).mean(), e_alt.max(), e_alt.mean(), cam_s[i_drop], e_alt[i_drop],
                                 BASE, SPEED.min(), SPEED.max()))

# 2b. never through a tree (gate 4 revision: the 158-173 s stretch flew through a Great Tree's trunk and crown -- the
#    smoothed, soaring line strays from the line the trees were planted clear of). Any sample inside a canopy's
#    envelope (radius, canopy bottom to top, from the asset bounds) is pushed out sideways to TREE_MARGIN past its
#    edge, and the line re-smoothed round it; a few rounds settle it clear and smooth.
TREE_MARGIN = 3.0
TRUNK = {"great": 3.0, "blossom": 1.5, "pine": 1.2, "weeper": 2.0}     # trunk radius at scale 1, metres
_trees = []
for f_, pts_ in fam.items():
    Rr, lo_, top_ = ENVELOPE[f_]
    for q in pts_:
        _trees.append(((q[0], -q[2], q[1]), Rr * q[8], lo_ * q[8] - 2.0, top_ * q[8] + 2.0))   # the crown
        _trees.append(((q[0], -q[2], q[1]), TRUNK[f_] * q[8], -2.0, lo_ * q[8]))               # the trunk under it
_TP = np.array([t_[0] for t_ in _trees])
_TR = np.array([t_[1] for t_ in _trees])
_TL = np.array([t_[2] for t_ in _trees])
_TT = np.array([t_[3] for t_ in _trees])


def tree_push(P_):
    n_, moved = 0, np.zeros(len(P_), bool)
    for i_ in range(len(P_)):
        d_ = P_[i_] - _TP
        h0 = d_ @ up_sm[i_]
        hv = d_ - h0[:, None] * up_sm[i_][None, :]
        lat = np.linalg.norm(hv, axis=1)
        bad = np.where((lat < _TR + TREE_MARGIN) & (h0 > _TL) & (h0 < _TT))[0]
        for j_ in bad:
            dirn = hv[j_] / max(1e-6, lat[j_])
            if lat[j_] < 1e-3:
                dirn = side_ref[i_]
            P_[i_] = P_[i_] + dirn * (_TR[j_] + TREE_MARGIN - lat[j_])
            n_ += 1
            moved[i_] = True
    return n_, moved


side_ref = np.cross(np.roll(cam_flight, -1, axis=0) - np.roll(cam_flight, 1, axis=0), up_sm)
side_ref /= np.maximum(1e-9, np.linalg.norm(side_ref, axis=1))[:, None]
_tp0 = 0
for _it in range(12):
    _n, _mv = tree_push(cam_flight)
    _tp0 = _tp0 or _n
    if not _n:
        break
    _zone = np.convolve(_mv.astype(float), np.ones(7), "same") > 0
    cam_flight[_zone] = gsmooth(cam_flight, np.where(_zone, 0.8, 0.0))[_zone]
tree_push(cam_flight)                       # the last word is the push: clear first, smooth second
_n_final, _ = tree_push(cam_flight)
print("TREES pushed: %d sample-tree overlaps at first, %d after settling" % (_tp0, _n_final))

# 3. the bank: a coordinated turn's up from the path's lateral curvature at the ride's own speed, clamped, sprung;
#    faded to the land's up over the crossings, where the rim's ellipse sets it
tan_ = np.array([cam_flight[sp1(i_)] - cam_flight[sm1(i_)] for i_ in range(n_s)])
tan_ /= np.linalg.norm(tan_, axis=1)[:, None]
side_ = np.cross(tan_, up_sm)
side_ /= np.linalg.norm(side_, axis=1)[:, None]
acc = np.array([(cam_flight[sp1(i_)] - 2 * cam_flight[i_] + cam_flight[sm1(i_)]) / CAM_STEP ** 2 for i_ in range(n_s)])
v_ = BASE * SPEED
lat_acc = v_ ** 2 * np.einsum("ij,ij->i", acc, side_)
bank = np.arctan2(lat_acc, G)
# (gate 4, the owner: "smooth out ... the twitchy camera movements" -- a big glider, not a drone: the bank capped at
# 30 degrees and sprung over twice the distance)
bank = np.clip(bank, -math.radians(30.0), math.radians(30.0))
bank *= np.clip((near_x - 2.0) / 3.0, 0.0, 1.0)
bank = spring(bank, 1.0 / 400.0)
print("BANK max %.0f deg, mean |bank| %.0f deg" % (math.degrees(np.abs(bank).max()), math.degrees(np.abs(bank).mean())))

# 4. the earned rolls: one whole aileron roll, quintic-eased over 4 s, into the turn; on the drop, then out of the
#    biggest climbs; at most three a lap, 900 m apart, never near a crossing
roll_add = np.zeros(n_s)
ROLLS = []


def smootherstep(u):
    u = min(1.0, max(0.0, u))
    return u * u * u * (u * (6 * u - 15) + 10)


# a roll is earned twice over: it lands on a phrase start (the 8-bar grid, 14-stem-analysis), and the land offers it --
# the ride has climbed well into a soar; its whole 4 s window at least 15 m up. Never into or under a rainbow's bow
# (the owner at gate 4: "no barrel rolls into the rainbows"); at most two a lap
BAR = 1.983471
PHRASES = [0.2425 + BAR * 8 * k_ for k_ in range(1, 12)]
cands = []
for tm in PHRASES:
    ic = int(np.argmin(np.abs(times_f - tm)))
    half = int(2.0 * v_[ic] / CAM_STEP) + 1
    win = [(ic + k_) % n_s for k_ in range(-half, half + 1)]
    rise = e_alt[ic] - e_alt[(ic - 16) % n_s]
    bow = any(_circ(cam_s[ic] - sa_) < 400.0 for sa_ in ARCH_S)
    ok = e_alt[win].min() >= 15.0 and rise > 12.0 and not bow
    print("ROLL candidate %.1f s at %d m: alt %.0f m, window min %.0f m, rise %.0f m%s%s"
          % (tm, cam_s[ic], e_alt[ic], e_alt[win].min(), rise, " near a bow" if bow else "", " *" if ok else ""))
    if ok:
        cands.append((rise, ic, "phrase %.1f s, out of a climb" % tm))
for score, ic, why in sorted(cands, reverse=True):
    if len(ROLLS) >= 2 or near_x[ic] < 8:
        continue
    if any(_circ(cam_s[ic] - cam_s[r_[0]]) < 1500.0 for r_ in ROLLS):
        continue
    n_r = 4.0 * v_[ic] / CAM_STEP              # samples in 4 s; the angle sampled every 15 m, under 90 degrees a step
    sgn = 1.0 if bank[ic] >= 0 else -1.0
    # 0 before the window, a whole turn after it: the same up vector, so the turn lives only in the window and the
    # spline's unwrap (ADR-1542) winds it
    for k_ in range(-int(n_r / 2) - 2, int(n_r / 2) + 3):
        roll_add[(ic + k_) % n_s] += sgn * 2 * math.pi * smootherstep((k_ + n_r / 2) / n_r)
    ROLLS.append((ic, why, sgn))
print("ROLLS", [("%.1f s" % times_f[ic], "%d m" % cam_s[ic], why, "alt %.0f m" % e_alt[ic]) for ic, why, sg in ROLLS])

ang = bank + roll_add
cam_ups_f = np.cos(ang)[:, None] * up_sm + np.sin(ang)[:, None] * side_


def speed_at(s_band):
    return float(np.interp(s_band % L, cam_s, SPEED))


cam_pts = list(cam_flight)
cam_ups = list(cam_ups_f)
cam_controls = np.array(cam_pts)
# clearance: the ride against every other part of the band (stations more than 400 m away along the loop), so a soar
# or a crossing never flies into another strand of the knot
_ring_s = np.array([f_[0] for f_ in fr])
_ring_p = np.array([np.array(f_[1]) for f_ in fr])
_worst = 1e9
for s_, p_ in zip(cam_s, cam_controls):
    far = np.abs(((_ring_s - s_) + L / 2) % L - L / 2) > 400.0
    d_ = np.linalg.norm(_ring_p[far] - p_, axis=1).min() - band.HALF_W
    _worst = min(_worst, d_)
print("CLEARANCE ride to other strands: %.0f m (band centre distance less its half width)" % _worst)
# and through the crossings, against the rim it swings round (rim_gap: the distance to the band's cross-section)
_rim = min(rim_gap(i_, cam_controls[i_])[0] for i_ in range(len(cam_s)) if not on_face[i_])
print("RIM clearance through the crossings: %.1f m" % _rim)


def canopy_clearance(P_):
    """The nearest canopy edge, across the ride's line, of any tree whose canopy spans the ride's height there."""
    best = 1e9
    for f_, pts_ in fam.items():
        if not pts_:
            continue
        Rr, lo_, top_ = ENVELOPE[f_]
        T_ = np.array([(q[0], -q[2], q[1]) for q in pts_])
        S_ = np.array([q[8] for q in pts_])
        for i_ in range(n_s):
            d_ = P_[i_] - T_
            h0 = d_ @ up_sm[i_]
            lat = np.linalg.norm(d_ - h0[:, None] * up_sm[i_][None, :], axis=1)
            inside = (h0 > lo_ * S_ - 2.0) & (h0 < top_ * S_ + 2.0)
            if inside.any():
                best = min(best, float((lat[inside] - Rr * S_[inside]).min()))
    return best


print("TREES clear: nearest canopy edge at the ride's height %.1f m (the glide's low line: %.1f m)"
      % (canopy_clearance(cam_controls), canopy_clearance(cam_sm)))
# 5. the roll, smoothed as an angle (gate 4: "smooth out ... the twitchy camera movements"; measured with
#    scripts/camera_motion.py, 16 section 7). The up the land asks for turns fast at the crossings' edges -- the face's
#    normal meets the rim ellipse's with a kink, and a flip over the rim is 180 degrees in ~110 m -- and the spline
#    interpolates roll linearly between controls, so every kink is a step in roll rate. The unwrapped roll (its whole
#    turns taken out so it is periodic) goes through the same zero-phase spring, so the camera starts to roll before a
#    crossing, finishes after it, and never snaps; the flip over the rim stays, as a long, even turn.
def build_path():
    raw_rolls, raw_path = knot_path.rolls_to_follow(cam_controls, cam_ups)
    _turn = 2 * math.pi * raw_path.roll_turns * np.arange(n_s) / n_s
    rolls_ = spring(np.asarray(raw_rolls) - _turn, 1.0 / ROLL_SPRING_M) + _turn
    return raw_rolls, raw_path, rolls_, knot_path.Path(cam_controls, rolls_, roll_turns=raw_path.roll_turns)


def dense_tree_hits(path_):
    """Every 3 m of the spline against every tree's envelope: (worst edge distance, where, and the hits to fix)."""
    worst, at, hits = 1e9, None, []
    for s_d in np.arange(0.0, path_.length, 3.0):
        q_ = path_.at(s_d)[0]
        i_ = int(np.argmin(np.linalg.norm(cam_controls - q_, axis=1)))
        d_ = q_ - _TP
        h0 = d_ @ up_sm[i_]
        hv = d_ - h0[:, None] * up_sm[i_][None, :]
        lat = np.linalg.norm(hv, axis=1)
        inside = (h0 > _TL) & (h0 < _TT)
        if not inside.any():
            continue
        gap = np.where(inside, lat - _TR, 1e9)
        k_ = int(np.argmin(gap))
        if gap[k_] < worst:
            worst, at = float(gap[k_]), (round(float(s_d)), round(float(_TR[k_]), 1), round(float(lat[k_]), 1))
        if gap[k_] < TREE_MARGIN:
            dirn = hv[k_] / lat[k_] if lat[k_] > 1e-3 else side_ref[i_]
            hits.append((i_, dirn, TREE_MARGIN - float(gap[k_])))
    return worst, at, hits


def dense_ground_hits(path_):
    """Every 3 m of the spline over a face: its height above the meadow (the face's own hills), and the hits under
    3.5 m -- the line between two controls, or a control a tree push moved sideways onto a hillside."""
    worst, at, hits = 1e9, None, []
    for s_d in np.arange(0.0, path_.length, 3.0):
        q_ = path_.at(s_d)[0]
        i_ = int(np.argmin(np.linalg.norm(cam_controls - q_, axis=1)))
        if not on_face[i_]:
            continue
        pp, tt_, uu, sdd = band.frame_lerp(fr, cam_s[i_] % L)
        d_ = q_ - np.array(pp)
        Xs, Ys = float(np.dot(d_, sdd)), float(np.dot(d_, uu))
        if abs(Xs) > band.HALF_W:
            continue
        g_ = band.FACE + (band.face_height(cam_s[i_] % L, Xs, band.TOP_HILLS, 0.0) if Ys >= 0 else
                          band.face_height(cam_s[i_] % L, -Xs, band.BOTTOM_HILLS, 1.3))
        h_ = abs(Ys) - g_
        if h_ < worst:
            worst, at = h_, round(float(s_d))
        if h_ < 3.5:
            hits.append((i_, np.array(uu) * math.copysign(1.0, Ys), 3.5 - h_))
    return worst, at, hits


# the spline runs between the controls, and a trunk or a crest can stand between two of them: check the line every
# 3 m and push the nearest controls clear of any tree or ground it would touch, then rebuild (a few rounds)
# The last pass is the plane's (the owner after the gate-4 revision: "minor corrections ... that feel unnatural";
# 21 section 6): the whole line smoothed over FINAL_SIGMA samples, so its curvature -- and with it the frame the
# spline carries (its rotation-minimising normal twists with every wiggle of the tangent) -- changes only slowly.
# Smoothing and the clearances are reconciled by planning, not by local pushes: the line is smoothed from a base, the
# smoothed line checked (trees, meadow, rim), and any hit raises a broad bump in the base, which the next smoothing
# turns into an early, gentle detour. Repeat until the smoothed line touches nothing.
FINAL_SIGMA = 2.0
_base = cam_controls.copy()
for _it in range(60):
    cam_controls = gsmooth(_base, np.full(n_s, FINAL_SIGMA))
    raw_rolls, raw_path, cam_rolls, cam_path = build_path()
    _worst_tree, _wt_at, _hits = dense_tree_hits(cam_path)
    _worst_ground, _wg_at, _ghits = dense_ground_hits(cam_path)
    _hits = _hits + _ghits
    for i_ in range(n_s):
        if not on_face[i_]:
            gap, out_ = rim_gap(i_, cam_controls[i_])
            if gap < RIM_MARGIN - 0.4:
                _hits.append((i_, out_, RIM_MARGIN - gap))
    if not _hits:
        break
    push = np.zeros((n_s, 3))
    for i_, dirn, need in _hits:
        for k_ in range(-8, 9):
            j_ = (i_ + k_) % n_s
            v_ = dirn * need * 1.4 * math.exp(-0.5 * (k_ / 3.0) ** 2)
            if np.linalg.norm(v_) > np.linalg.norm(push[j_]):
                push[j_] = v_
    _base = _base + push
print("FINAL smoothing %.1f samples: %d rounds, %d hits left" % (FINAL_SIGMA, _it + 1, len(_hits)))
print("TREES along the spline (every 3 m): nearest envelope edge %.1f m" % _worst_tree, _wt_at, "after %d rounds" % (_it + 1))
print("GROUND along the spline (every 3 m, over the faces): lowest %.1f m at %s" % (_worst_ground, _wg_at))
print("RIM clearance after the tree pushes: %.1f m" % min(rim_gap(i_, cam_controls[i_])[0] for i_ in range(n_s)
                                                       if not on_face[i_]))
_dr = np.degrees(np.abs(np.diff(np.concatenate([cam_rolls, [cam_rolls[0] + 2 * math.pi * raw_path.roll_turns]]))))
_draw = np.degrees(np.abs(np.diff(np.concatenate([raw_rolls, [raw_rolls[0] + 2 * math.pi * raw_path.roll_turns]]))))
print("ROLL per 15 m: max %.1f deg (unsmoothed %.1f), p95 %.1f deg (unsmoothed %.1f)"
      % (_dr.max(), _draw.max(), np.percentile(_dr, 95), np.percentile(_draw, 95)))
nodes.append(cam_path.to_avgen_json("ride", rw.to_avgen))
print("CAMERA path %.0f m, roll turns %d, plan %s" % (cam_path.length, cam_path.roll_turns,
                                                     [(round(a), k) for a, b_, k, sd_ in plan[:5]]))


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
            lod_ = {"cull": True, "count": 4, "byScreenSize": False, "distance1": 300.0, "distance2": 800.0,
                    "distance3": 1600.0}
        else:               # the masses and banks: 24-30k triangles each, mostly seen from far
            lod_ = {"cull": True, "count": 3, "byScreenSize": False, "distance1": 500.0, "distance2": 1200.0}
        nodes.append(node_points("cloud_" + kind, "cloud_%s.glb" % kind, pts,
                                 {"program": "rw_cloud", "roughness": 1.0, "metallic": 0.0, "unlit": True},
                                 castsShadow=False, lod=lod_))
print("CLOUDS", {k: len(v) for k, v in cpts.items()})

# ---- Rainbow World 2: the bass swell (16-effect-mapping.md section 3) ------------------------------------------------
# A world-space sine deformer on the land and everything standing on it, the same two on every node, so a meadow and
# the grass, flowers, stones and trees on it move together (each vertex is displaced by a function of where it is, not
# of what it belongs to). Their amounts rest at 0 -- the world as built -- and the bass sets them through the `swell`
# macro. It travels at about the ride's own speed, along an axis oblique to the knot, so the heave crosses the track
# rather than running with it. (Finch's sum of waves, GPU Gems ch. 1, was tried with two: measured in 17 section 4.)
# (gate 5: the owner chose to retire the heave -- 23-modulation-inventory item 5: 1.1-2.7 ms for an effect that does
# not read from the flight. The bass now drives colour and light on big surfaces instead. The machinery stays, empty.)
SWELL = []  # (axis of travel, displacement direction, wavelength m, speed rad/s, metres at a full swell)
SWELL_NODES = []
for n_ in nodes:
    nm_ = n_["name"]
    if n_.get("kind") != "procedural" or nm_.startswith(("cloud", "rainbow")) or nm_ == "ride":
        continue
    defs = []
    for axis, disp, wl, sp, _m in SWELL:
        a_ = np.array(axis) / np.linalg.norm(axis)
        d_ = np.array(disp) / np.linalg.norm(disp)
        defs.append({"kind": "sine", "space": "world", "amount": 0.0, "axis": R(rw.to_avgen(Vector(a_))),
                     "displacementAxis": R(rw.to_avgen(Vector(d_))), "frequency": round(2 * math.pi / wl, 6),
                     "speed": sp, "phase": 0.0, "center": [0.0, 0.0, 0.0]})
    n_["procedural"]["deformers"] = defs
    SWELL_NODES.append(nm_)
print("SWELL nodes", len(SWELL_NODES))

# ---- Rainbow World 2, gate 5: the scan sparkle (24-brief-scan-sparkle.md; 27-scan-sparkle.md) ----------------------
# As the scan's front passes a tree, the tree spins once about its trunk and a burst of glitter lights round its
# crown. Built from what the engine has, with no per-tree state and no events: three scene fields follow the
# front -- spheres about the camera (their centres the camera's baked position, their radii the front's) -- and
# effectors on the tree and glitter instances read them (ADR-025). A tree inside the front's sphere is turned a whole
# turn (no change); one in its soft edge is part-way round; so as the front sweeps past, each tree spins once, eased,
# nearer trees first (the cascade), and never again until the next scan. The glitter is a pool of tiny emissive
# instances in every crown, at scale 0 except inside a thin shell just behind the front, where they swell, rise and
# fade. A pure function of the transport: seek equals play.
SPIN_SOFT = 120.0         # m: the spin happens over this much of the front's run (~0.6-1 s at its speed)
SHELL_W, SHELL_SOFT = 90.0, 40.0    # m: the glitter's shell, just behind the front
GLITTER_PER_TREE = 28
REST_R = look.SCAN_REST


def field_node(name, spec):
    return {"name": name, "kind": "field", "position": [0.0, 0.0, 0.0],
            "field": dict({"strength": 1.0, "falloff": {"kind": "none"}, "position": [0.0, 0.0, 0.0]}, **spec)}


SPARKLE_FIELDS = {   # name -> (rest radius, the front's pull on it)
    "scanSpin": REST_R, "scanShellOut": REST_R - 10.0, "scanShellIn": REST_R - 10.0 - SHELL_W}
nodes.append(field_node("scanSpin", {"kind": "sphere", "radius": REST_R, "softness": SPIN_SOFT}))
nodes.append(field_node("scanShellOut", {"kind": "sphere", "radius": SPARKLE_FIELDS["scanShellOut"],
                                         "softness": SHELL_SOFT}))
nodes.append(field_node("scanShellIn", {"kind": "sphere", "radius": SPARKLE_FIELDS["scanShellIn"],
                                        "softness": SHELL_SOFT, "invert": True}))
nodes.append(field_node("scanShell", {"kind": "compound", "children": ["scanShellOut", "scanShellIn"],
                                      "combine": "multiply"}))
for n_ in nodes:
    if n_.get("kind") == "procedural" and n_["name"].split("_")[0] in fam and \
            n_["name"].endswith(("_wood", "_core", "_leaves")):
        # the spin: a whole turn about the trunk across the front's soft edge; and a bow: tipped 0.22 rad (13 deg)
        # and back up across the glitter's shell, so the tree nods as the wave goes through it
        n_["procedural"].setdefault("effectors", []).extend([
            {"field": "scanSpin", "op": "rotation", "blend": "add", "strength": round(2 * math.pi, 5),
             "axis": [0.0, 1.0, 0.0], "local": True},
            {"field": "scanShell", "op": "rotation", "blend": "add", "strength": 0.22,
             "axis": [1.0, 0.0, 0.0], "local": True}])
# the glitter: a pool per crown (avgen coordinates, each instance turned to its tree's up)
_ENV = {"great": (20.5, 9.0, 27.0), "blossom": (9.6, 10.0, 23.3), "pine": (6.0, 6.6, 25.6), "weeper": (11.0, 0.0, 13.4)}
glit = []
for f_, pts_ in fam.items():
    Rr, lo_, top_ = _ENV[f_]
    for q in pts_:
        base_ = np.array(q[:3]); qx, qy, qz, qw = q[3:7]; sc_ = q[8]
        up_ = np.array([2 * (qx * qy - qw * qz), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz + qw * qx)])
        up_ /= np.linalg.norm(up_)
        side_a = np.cross(up_, [1.0, 0.0, 0.0])
        if np.linalg.norm(side_a) < 1e-3:
            side_a = np.cross(up_, [0.0, 0.0, 1.0])
        side_a /= np.linalg.norm(side_a)
        side_b = np.cross(up_, side_a)
        mid, hh = 0.5 * (lo_ + top_) * sc_, 0.5 * (top_ - lo_) * sc_
        for _k in range(GLITTER_PER_TREE):
            while True:
                v_ = np.array([rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1)])
                if v_ @ v_ <= 1.0:
                    break
            p_ = base_ + up_ * (mid + v_[1] * hh * 1.1) + side_a * v_[0] * Rr * sc_ * 1.1 + side_b * v_[2] * Rr * sc_ * 1.1
            glit.append([round(float(c), 3) for c in p_] + [qx, qy, qz, qw, 1.0, 1.0, 1.0])
programs.append(look.glitter_program())
nodes.append({"name": "scan_glitter", "kind": "procedural", "procedural": {
    "source": {"kind": "sphere", "radius": 0.55, "segments": 6, "rings": 3},
    "distribution": {"kind": "points", "points": glit},
    "material": {"program": "rw_glitter", "roughness": 0.4, "metallic": 0.0},
    "castsShadow": False,
    "lod": {"cull": True, "minScreenRadius": 0.5, "maxDistance": 700.0},
    "effectors": [
        {"field": "scanShell", "op": "scale", "blend": "replace", "strength": 1.0, "scaleAxis": [1.0, 1.0, 1.0]},
        {"field": "scanShell", "op": "positionOffset", "blend": "add", "strength": 5.0, "axis": [0.0, 1.0, 0.0],
         "local": True}]}})
print("SPARKLE %d glitter instances in %d crowns" % (len(glit), len(glit) // GLITTER_PER_TREE))

# ---- scene and project ----------------------------------------------------------------------------------------------
env, lights = look.environment(SUN, rw.to_avgen(Vector((0, 0, 1))))
scene = {"format": "avgen-scene", "version": 1, "name": "rainbow-world",
         "camera": {"mode": 2, "spline": "ride", "fov": 106.0, "orbitSpeed": 0.0},
         "environment": env, "lights": lights, "materialPrograms": programs, "nodes": nodes}
with open(os.path.join(OUT, "rainbow-world.scene.json"), "w") as f:
    json.dump(scene, f)
params = look.post_parameters()
params.update({"camera/splineUp": 1.0, "camera/lookAhead": 90.0, "post/lens/fisheye": 1.0, "post/lens/fisheyeModel": 0.0,
               "camera/far": 30000.0})
# ---- the ride's rhythm. The owner (Stage 4): "definitely want faster camera movements". The clip covers a lot of
# structure in 8 s with quick swoops and rolls, so the base is fast (BASE m/s) and the holds read by contrast:
#   - lap A: brief holds (30% speed) under each rainbow arch, eased over ~150 m either side;
#   - lap B: the holds move to the Great Tree landmarks, and the arches are passed at full speed;
#   - both laps surge to 1.5x into and through every crossing (ramped over 140 m), so the swing round the rim is quick.
# Two laps with different holds make the loop ~2 laps long before it repeats. A looping timeline maps time to splineT:
# a pure function of the transport (seek equals play). Each lap ends with a step key back to 0 (closed spline: 1 == 0).
cam_D = [cam_path.D[k * cam_path.per] for k in range(len(cam_s))] + [cam_path.length]
# (the final smoothing shortens the line a little: the base speed is set again so a lap is still LAP seconds)
BASE = float(sum((cam_D[k + 1] - cam_D[k]) / speed_at(cam_s[k]) for k in range(len(cam_s))) / LAP)
ride_keys, t_acc = [], 0.0
for k in range(len(cam_s)):
    ride_keys.append({"time": round(t_acc, 4), "value": round(float(cam_D[k] / cam_path.length), 6), "interp": "linear"})
    t_acc += (cam_D[k + 1] - cam_D[k]) / (BASE * speed_at(cam_s[k]))
ride_keys.append({"time": round(t_acc - 1e-3, 4), "value": 1.0, "interp": "step"})
loop_seconds = round(t_acc, 4)
print("RIDE base %.0f m/s, one lap %.1f s, %d hops" % (BASE, loop_seconds, len(HOPS)))

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
    # Rainbow World 2 (gate 3: "the eight looks are too similar"): every look is now a whole world -- its own sky
    # gradient (a horizon colour carried well up the sky by the look's own haze width, `skyHaze`), its own meadow, its
    # own clouds and tree tints -- so no two read as the same green under a different filter.
    # Golden Hour: a cobalt sky burning orange at the horizon, the low sun catching the hill crests rose-gold over
    # deep emerald troughs (a yellow crest read as mustard under the warm key), peach and rose clouds, long violet shadow.
    ("Golden Hour", dict(zenith=H("#1A2C8E"), horizon=H("#FF9A4C"), ground=H("#A8A0CC"), sun=H("#FFB070"), bounce=H("#9C7CD4"),
                         haze=H("#F09A68"), cloud0=H("#5A3E90"), cloud1=H("#C47E9E"), cloud2=H("#FFAE78"), cloud3=H("#FFD49C"),
                         meadow0=H("#0E5434"), meadow1=H("#24783C"), meadow2=H("#3E9044"), meadow3=H("#8CB060"), shade=H("#2C2A86"))),
    # After the Storm: rain-dark teal land, a navy sky clearing to bright aqua low down, slate clouds with white heads
    ("After the Storm", dict(zenith=H("#081C6A"), horizon=H("#8CE2F2"), ground=H("#8E98B8"), sun=H("#FFF4DC"), bounce=H("#9FB0C8"),
                             haze=H("#6EA6D8"), cloud0=H("#2A3058"), cloud1=H("#5A6488"), cloud2=H("#E6E2DA"), cloud3=H("#FFFFFF"),
                             meadow0=H("#08403A"), meadow1=H("#12705A"), meadow2=H("#2EA070"), meadow3=H("#7AD69A"), shade=H("#0A3050"))),
    ("Lavender Dusk", dict(zenith=H("#2A1E7A"), horizon=H("#E89AD0"), ground=H("#9C90CC"), sun=H("#FFB4C4"), bounce=H("#8A78D2"),
                           haze=H("#8C70D4"), cloud0=H("#4A3A8A"), cloud1=H("#9A7AC0"), cloud2=H("#F6B6A8"), cloud3=H("#FFD6C6"),
                           meadow0=H("#1C4E66"), meadow1=H("#365E8E"), meadow2=H("#6A72BC"), meadow3=H("#AE92DC"), shade=H("#262C92"))),
    # Prism: R2's spectral land -- the meadow's value ramp runs across the hues (violet troughs, teal, mint, peach
    # crests), under a periwinkle sky going rose at the horizon
    ("Prism", dict(zenith=H("#3440C8"), horizon=H("#F4B4D8"), ground=H("#C8C0F0"), sun=H("#FFF0D0"), bounce=H("#F0B8E0"),
                   haze=H("#B0A0F0"), cloud0=H("#6058C0"), cloud1=H("#B090E0"), cloud2=H("#FFC8A0"), cloud3=H("#FFE6CE"),
                   meadow0=H("#4A2E9E"), meadow1=H("#1E9A96"), meadow2=H("#6ED0A8"), meadow3=H("#FFC49A"), shade=H("#3A2A9A"))),
    # Rainbow World 2 (19-brief-color-vfx section 4.1, and the coordinator at gate 3: "each non-default look should
    # transform the whole frame"): three bold directions, each its own hue relationship, none collapsing into
    # cyan/magenta. The meadow is still read as land by its value ramp (dark troughs, light crests), not its hue.
    # Candy: a pink-coral meadow with violet shade under a gold sky; lilac and cream clouds.
    ("Candy", dict(zenith=H("#E08A3C"), horizon=H("#FFE2A0"), ground=H("#FFD8E8"), sun=H("#FFF2C8"), bounce=H("#FF8CC0"),
                   haze=H("#FFB898"), cloud0=H("#B07CD8"), cloud1=H("#FFA8D0"), cloud2=H("#FFDCC0"), cloud3=H("#FFF6EA"),
                   meadow0=H("#B23C78"), meadow1=H("#E86A8C"), meadow2=H("#FF9C8A"), meadow3=H("#FFD2A0"), shade=H("#6A34A8"))),
    # Electric: a deep blue-violet world -- an indigo meadow with cyan crests, a near-black violet zenith, a magenta sun
    # and cyan bounce light, magenta-lit clouds.
    ("Electric", dict(zenith=H("#0A0630"), horizon=H("#6A1CC8"), ground=H("#3A2A8C"), sun=H("#FF5ADC"), bounce=H("#00C8FF"),
                      haze=H("#5A22D0"), cloud0=H("#24105C"), cloud1=H("#5A28A8"), cloud2=H("#D040C8"), cloud3=H("#FF90EE"),
                      meadow0=H("#1C1A6E"), meadow1=H("#2E3AAE"), meadow2=H("#3E78E8"), meadow3=H("#6EE8FF"), shade=H("#38086A"))),
    # Spring: the brightest morning -- a clear sky fading to white at the horizon, white clouds, a cool mint meadow
    # with cream crests (no yellow-green: that is the lime), trees tinted toward blossom
    ("Spring", dict(zenith=H("#2E8CEC"), horizon=H("#F2FBFF"), ground=H("#E0F6FF"), sun=H("#FFFBE8"), bounce=H("#C0A8FF"),
                    haze=H("#B0E4F8"), cloud0=H("#8494E8"), cloud1=H("#C4D2FF"), cloud2=H("#F8FBFF"), cloud3=H("#FFFFFF"),
                    meadow0=H("#246E52"), meadow1=H("#4CA06A"), meadow2=H("#94D0A0"), meadow3=H("#EEF2CC"), shade=H("#5A48C0"))),
]
# the cloud floor follows each look: troughs a deeper cloud0, flanks between cloud0 and cloud1, heads cloud2/3
# (Bright Noon keeps the floor program's own stops)
FLOOR_NOON = [R(rw.hexlin(h)[:3]) for h in look.FLOOR_STOPS]
# Rainbow World 2: the kick ring takes the look's own light (a colour per look, not a hue cycle); never a yellow on
# a green meadow -- gold on green grass tips was half the lime (gate 3)
RING_BY_LOOK = {"Bright Noon": "#FFF0DC", "Golden Hour": "#FF9A4A", "After the Storm": "#7FE6FF",
                "Lavender Dusk": "#FF86D2", "Prism": "#FFB0E0", "Candy": "#FF7AC8", "Electric": "#C86AFF",
                "Spring": "#FFD0E8"}
# and the stones' tints (multiplied into their ramp; the first of each pair was the trees', now FOLIAGE below)
TINTS = {"Bright Noon": ((1.0, 1.0, 1.0), (1.0, 1.0, 1.0)), "Golden Hour": ((1.15, 1.0, 0.72), (1.2, 0.95, 0.78)),
         "After the Storm": ((0.9, 1.05, 1.1), (0.85, 0.95, 1.1)), "Lavender Dusk": ((1.0, 0.82, 1.25), (1.1, 0.88, 1.3)),
         "Prism": ((1.1, 1.0, 1.15), (1.05, 0.95, 1.2)), "Candy": ((1.7, 0.75, 1.15), (1.35, 0.9, 1.05)),
         "Electric": ((0.55, 0.7, 1.8), (0.8, 0.55, 1.6)), "Spring": ((1.2, 1.0, 1.1), (1.0, 1.05, 1.05))}
TINTS.update({"Golden Hour": ((1.3, 0.95, 0.6), (1.3, 0.95, 0.7)), "After the Storm": ((0.75, 1.0, 1.05), (0.8, 0.9, 1.05))})
# each look's sky gradient: how far up the sky its horizon colour reaches (env/sky/haze, the exponential's width)
SKY_HAZE = {"Bright Noon": 0.4, "Golden Hour": 0.75, "After the Storm": 0.3, "Lavender Dusk": 0.6, "Prism": 0.55,
            "Candy": 0.7, "Electric": 0.5, "Spring": 0.6}
RING_BY_LOOK.update({"Candy": "#FFE070", "Electric": "#00FFE0"})
for n_, c_ in STATES:
    c_["ring"] = H(RING_BY_LOOK[n_])
    c_["stone"] = list(TINTS[n_][1])
SCALARS = {n_: {"skyHaze": SKY_HAZE[n_]} for n_, c_ in STATES}


# The trees follow the look (coordinator, gate 4 sheets: "a dark green tree on a pink or indigo meadow"): every
# canopy's colour turned in OKLCH by the look's own hue (look.foliage_hue_op), the turn taking the Great Tree's mid
# canopy green to the look's foliage hue -- by the look's weight, along the shorter arc. Lightness and each species'
# own value structure stay; the species keep their places relative to each other.
# look: (foliage hue in degrees, weight toward it). A partial weight lands between the two along the shorter arc
# (green part-way toward pink lands on rust): Spring's cherry blossom takes the whole turn.
FOLIAGE = {"Bright Noon": (0.0, 0.0), "Golden Hour": (110.0, 0.3), "After the Storm": (175.0, 0.55),
           "Lavender Dusk": (330.0, 0.85), "Prism": (285.0, 0.7), "Candy": (22.0, 0.9), "Electric": (200.0, 0.9),
           "Spring": (350.0, 1.0)}
GREAT_MID = (0.1022, 0.3231, 0.0452)     # rw_canopy's second stop (#5A9A3C), the canopy's body colour


def _hue_deg(c):
    r, g, b = c
    l_ = (0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b) ** (1 / 3)
    m_ = (0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b) ** (1 / 3)
    s_ = (0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b) ** (1 / 3)
    a_ = 1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_
    b_ = 0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_
    return math.degrees(math.atan2(b_, a_))


for n_, c_ in STATES:
    hue_t, w_ = FOLIAGE[n_]
    dh = ((hue_t - _hue_deg(GREAT_MID) + 180.0) % 360.0) - 180.0
    SCALARS[n_]["foliageHue"] = round(w_ * dh / 360.0, 4)       # turns
print("FOLIAGE turns", {n_: SCALARS[n_]["foliageHue"] for n_ in SCALARS})
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
            {"role": "haze", "target": "post/look/atmosphericTint"}, {"role": "skyHaze", "target": "env/sky/haze"}]
g = LOOK["cloudGain"]
for k, c in enumerate(("constant", "constant2", "constant3", "constant4")):
    bindings.append({"role": "cloud%d" % k, "target": cloud_ramp + c, "gain": g})
    bindings.append({"role": "floor%d" % k, "target": floor_ramp + c, "gain": g})
    for prog in ("rw_ground", "rw_ground_lip", "rw_grass"):
        bindings.append({"role": "meadow%d" % k, "target": op_path(prog, "oklabRamp") + c})
# the ground program's shade constant (the first `constant` op after the cavity input)
bindings.append({"role": "shade", "target": op_path("rw_ground", "constant", 1) + "constant"})
bindings.append({"role": "shade", "target": op_path("rw_ground_lip", "constant", 1) + "constant"})
for f_ in ("constant", "constant2", "constant3", "constant4"):
    bindings.append({"role": "stone", "mode": "multiply", "target": op_path("rw_rock", "oklabRamp") + f_})
for prog_, roles_ in look.FX.items():
    if "foliage_hue" in roles_:
        bindings.append({"role": "foliageHue", "target": "material/%s/op/%d/hueShift/value" % (prog_, roles_["foliage_hue"])})
    if "kick_colour" in roles_:
        bindings.append({"role": "ring", "target": "material/%s/op/%d/constant/constant" % (prog_, roles_["kick_colour"])})
# the scan glitter's four colours are the look's own, chosen to contrast: its kick colour, horizon, cloud shadow, crest
if "glitter_ramp" in look.FX.get("rw_glitter", {}):
    for role_, f_ in zip(("ring", "horizon", "cloud0", "meadow3"), ("constant", "constant2", "constant3", "constant4")):
        bindings.append({"role": role_, "target": "material/rw_glitter/op/%d/oklabRamp/%s"
                         % (look.FX["rw_glitter"]["glitter_ramp"], f_)})
palette = {"states": [{"name": n, "colors": c, "scalars": SCALARS[n]} for n, c in STATES], "bindings": bindings,
           "selector": {"select": 0, "transition": 8.0, "lock": False, "hold": 60.0,
                        "advance": {"signal": "beat.phrasePulse", "threshold": 0.5, "order": list(range(len(STATES))),
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
#   CC 9  look        Bright Noon, Golden Hour, After the Storm, Lavender Dusk, Prism, Candy, Electric, Spring
#                     (palette/select 0-7)
#   CC 10 look lock   hold the current look (palette/lock); unlocked, the look advances on phrase boundaries
#                     (beat.phrasePulse) or a big change in the music, holding each at least 60 s
# Audio (live input, no file): response.sustain and response.kick light the rainbows, response.level blooms the air,
# response.hat lifts saturation, response.bass lifts the warm bounce light from the cloud sea.
CC = [(1, "glow"), (2, "lens"), (5, "haze"), (6, "light"), (7, "colour"), (9, "palette"), (10, "paletteLock")]
control = {"midi": {"enabled": True, "filter": "*", "bindings": [
    {"source": "*", "channel": -1, "kind": "cc", "number": n, "signal": sig} for n, sig in CC]}}
ctl = {"attackMs": 150, "decayMs": 150}
routes = [{"source": "timeline.ride", "target": "camera/splineT", "amount": 1.0, "op": "replace", "polarity": "unipolar"},
          {"source": "control.palette", "target": "palette/select", "amount": float(len(STATES) - 1), "op": "add", "polarity": "unipolar", "chain": ctl},
          {"source": "control.paletteLock", "target": "palette/lock", "amount": 1.0, "op": "add", "polarity": "unipolar", "chain": ctl},
          {"source": "control.lens", "target": "post/lens/fisheye", "amount": -1.0, "op": "add", "polarity": "unipolar", "chain": ctl},
          {"source": "control.haze", "target": "post/look/atmospheric", "amount": 0.5, "op": "add", "polarity": "unipolar", "chain": ctl},
          # every control rests at 0 = the world as built (no MIDI is the default state), and turns one way
          {"source": "control.light", "target": "camera/exposure/compensation", "amount": 1.2, "op": "add", "polarity": "unipolar", "chain": ctl},
          {"source": "control.colour", "target": "post/grade/saturation", "amount": 0.5, "op": "add", "polarity": "unipolar", "chain": ctl}]
# Rainbow World 2: the music (music.py, 16-effect-mapping.md). The Stage 7 routes on response.* (the arches' breath,
# the kick's bloom flash, the hats' saturation, the bass's bounce light) are replaced by the stem-driven set; the
# rainbows keep CC 1's glow.
for rn in rainbow_nodes:
    routes.append({"source": "control.glow", "target": "nodes/%s/emissiveBoost" % rn["name"], "amount": 2.0,
                   "op": "add", "polarity": "unipolar", "chain": ctl})
routes += music.routes(look.FX, SWELL_NODES, SWELL)
# the scan sparkle's spheres are centred on the camera: its position over the lap, baked from the ride (the same
# keys as the ride's timeline, so the two never part), routed to each sphere's centre
CAM_KEYS = [[], [], []]
for k_ in ride_keys:
    q_ = rw.to_avgen(Vector(cam_path.at((k_["value"] % 1.0) * cam_path.length)[0]))
    for c_ in range(3):
        CAM_KEYS[c_].append({"time": k_["time"], "value": round(float(q_[c_]), 3), "interp": k_["interp"]})
CAM_SOURCES = [{"kind": "timeline", "name": "cam" + "XYZ"[c_], "settings": {"loopLength": loop_seconds,
                                                                            "keys": CAM_KEYS[c_]}} for c_ in range(3)]
for fld in music.SPARKLE_FIELDS:
    for c_ in range(3):
        routes.append({"source": "timeline.cam" + "XYZ"[c_], "target": "field/%s/position" % fld, "amount": 1.0,
                       "op": "replace", "polarity": "unipolar", "component": c_})
# the faint secondary bows carry no program: the synth's light and the scan reach them through the node's glow lane
for rn in rainbow_nodes:
    if rn["name"].endswith("s"):
        routes.append({"source": "macro.synth", "target": "nodes/%s/emissiveBoost" % rn["name"], "amount": 0.8,
                       "op": "add", "polarity": "unipolar"})
        routes.append({"source": "macro.scan", "target": "nodes/%s/emissiveBoost" % rn["name"], "amount": 3.0,
                       "op": "add", "polarity": "unipolar"})
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
    # Rainbow World 2 (17 section 7): the live ceilings -- in live mode only (an offline render keeps full quality):
    # coarser mesh LOD a little sooner and lighter shadow filtering. lodBias 2 (5.5 ms at Medium) dropped the trees'
    # shadows and the far grass; 1.5 keeps both and still holds High at 30 fps live (1 miss in 90 s, on AC). A 1k shadow
    # map made no visible or measured difference, so it is not set.
    "live": {"overrides": {"lodBias": 1.5, "reducedShadowFiltering": True}},
    "sources": [{"kind": "timeline", "name": "ride", "settings": {"loopLength": loop_seconds, "keys": ride_keys}},
                {"kind": "macro", "name": "macros", "settings": {"knobs": [{"name": k_, "default": d_}
                                                                          for k_, d_ in music.MACROS]}}] + CAM_SOURCES,
    "routes": routes,
    "palette": palette,
    "control": control,
    "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video", "path": "renders/rainbow-world.mp4"},
}
with open(proj_path, "w") as f:
    json.dump(project, f, indent=1)
# The Feline Footwear showcase (brief 13 section 12.5): the same world and routes, plus the mix as the playback source
# and the three stems as analysis sources (ADR-1520), for offline renders. A separate file, because the live scene
# carries no audio file. The audio is the owner's and gitignored; the project only names it.
showcase = json.loads(json.dumps(project))
showcase["app"] = {"name": "Rainbow World: Feline Footwear"}
audio_dir = os.path.relpath(os.path.join(HERE, "..", "..", "assets", "audio"), os.path.dirname(proj_path))
showcase["assets"]["audio"] = {"path": os.path.join(audio_dir, "feline-footwear.wav")}
showcase["assets"]["stems"] = [{"key": k_, "label": k_.capitalize(),
                                "file": os.path.join(audio_dir, "stems", "feline-footwear (%s).wav" % k_.capitalize())}
                               for k_ in music.STEMS]
showcase["sonic"]["live"] = False
# the showcase's world scans are baked from the song (music.SCAN_TIMES); the live project keeps the live trigger
showcase["sources"] = showcase["sources"] + music.showcase_scan_sources(210.5)
showcase["routes"] = music.showcase_scan_routes(showcase["routes"])
# the beat clock from the drum stem (ADR-1581): the kicks' own grid, not the mix's, under the clap fronts and the
# phrase pulses. The levels and hits stay on the mix (the fallbacks stand aside while the stems are active anyway).
showcase["feeds"] = {"beat": "stem.drums"}
showcase["sonicScene"]["id"] = "rainbow-world-feline"
showcase["sonicScene"]["title"] = "Rainbow World: Feline Footwear"
showcase["render"]["path"] = "renders/rainbow-world-feline.mp4"
with open(os.path.join(os.path.dirname(proj_path), "rainbow-world-feline.json"), "w") as f:
    json.dump(showcase, f, indent=1)
report = {"loopMetres": L, "cameraMetres": cam_path.length, "loopSeconds": loop_seconds, "baseSpeed": a.base_speed,
          "grass": len(gpts), "flowers": len(fpts), "trees": len(tpts), "rimRocks": sum(len(v) for v in by.values()),
          "rainbows": len(rainbow_nodes), "clouds": {k: len(v) for k, v in cpts.items()}, "bandChunks": len(band_nodes)}
with open(os.path.join(OUT, "world_report.json"), "w") as f:
    json.dump(report, f, indent=1)
print("done", json.dumps(report))
