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
ap.add_argument("--chunks", type=int, default=24)
ap.add_argument("--loop-seconds", type=float, default=600.0)
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
    rw.export_glb([ob], os.path.join(OUT, "band_%02d.glb" % c))
    band_nodes.append(node_points("band_%02d" % c, "band_%02d.glb" % c, [point(Vector((0, 0, 0)), Quaternion(), 1.0)],
                                  {"program": "rw_ground", "roughness": 0.93, "metallic": 0.0},
                                  lod={"cull": True}))
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
by = {}
for o in placed:
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
print("RIM ROCKS", sum(len(v) for v in by.values()))

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
TR = 170.0                       # metres a rim crossing takes
RIM_X, RIM_A, RIM_LIFT = 108.0, 62.0, 12.0
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
    return 0.40 * band.HALF_W * math.sin(2 * math.pi * s / band.per(1150.0)) + 0.16 * band.HALF_W * math.sin(
        2 * math.pi * s / band.per(410.0) + 1.0)


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
            if d < 220.0:
                w = band.smoothstep(220.0, 0.0, d)
                if w > best_w:
                    best_w, target = w, side * RIM_X
    return x + (target - x) * best_w


def cam_lift(s):
    return 4.5 + 2.5 * (0.5 + 0.5 * math.sin(2 * math.pi * s / band.per(700.0)))


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
        fpts.append(point(p, up.to_track_quat("Z", "Y") @ Quaternion((0, 0, 1), rng.uniform(0, 6.283)), rng.uniform(0.9, 1.5)))


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
    # the leaf cards (alpha-tested, the depth prepass's dearest geometry) stop at 240 m; beyond it the canopy core,
    # tinted by the same program, is the tree -- at that distance the cards are smaller than their own gaps
    nodes.append(node_points("%s_wood" % f, "%s_wood.glb" % asset, pts, {"baseColor": R(rw.P["bark"][:3]), "roughness": 0.9, "metallic": 0.0},
                             lod={"cull": True, "minScreenRadius": 1.5, "maxDistance": 500.0}))
    nodes.append(node_points("%s_core" % f, "%s_core.glb" % asset, pts, {"program": prog, "roughness": 0.95, "metallic": 0.0},
                             lod={"cull": True, "minScreenRadius": 1.0}))
    nodes.append(node_points("%s_leaves" % f, "%s_leaves.glb" % asset, pts,
                             {"program": prog, "roughness": 0.75, "metallic": 0.0, "alphaMode": "mask",
                              "alphaCutoff": 0.5, "doubleSided": True}, castsShadow=False,
                             lod={"cull": True, "minScreenRadius": 2.0, "maxDistance": 240.0}))
for k in range(0, len(outcrops), 60000):
    nodes.append(node_points("outcrops%d" % k, "rock_namaqualand_boulder_03.glb", outcrops[k:k + 60000],
                             {"program": "rw_rock", "roughness": 0.85, "metallic": 0.0},
                             lod={"cull": True, "minScreenRadius": 1.0}))
for k in range(0, len(fpts), 60000):
    nodes.append(node_points("flowers%02d" % (k // 60000), "flower_clump.glb", fpts[k:k + 60000],
                             {"program": "rw_flower", "roughness": 0.6, "metallic": 0.0, "doubleSided": True},
                             castsShadow=False, lod={"cull": True, "minScreenRadius": 0.8, "maxDistance": 75.0}))
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
for k in range(0, len(gpts), 60000):
    nodes.append(node_points("grass%03d" % (k // 60000), "grass_lite.glb", gpts[k:k + 60000],
                             {"program": "rw_grass", "roughness": 0.95, "metallic": 0.0}, castsShadow=False,
                             lod={"cull": True, "minScreenRadius": 0.8, "maxDistance": 70.0}))
print("GRASS", len(gpts))

# ---- rainbows: arches across the band near the landmarks, on the face the ride is on there ----------------------------
rainbow_nodes = []
def far_from_crossing(s, margin=320.0):
    return all(min((s0 - s) % L, (s - s1) % L) > margin for s0, s1, kind, side in plan if kind in ("over", "back"))


# one arch at the middle of every long stretch on a face: the ride passes under it in each sequence's calm, and it is
# always as far as it can be from a rim crossing
arch_s = [0.5 * (s0 + s1) for s0, s1, kind, side in plan if kind in ("top", "bottom") and s1 - s0 > 380.0]
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
    rainbow_nodes.append({"name": "rainbow_%d" % k, "kind": "gltf", "asset": "rainbow_arch.glb", "position": av(pos),
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
while placed_puffs < 260:  # the cloud sea, out to 6 km, never within reach of the ride
    aa, rr = rng.uniform(0, 2 * math.pi), 6000 * math.sqrt(rng.random())
    c = Vector((centre[0] + rr * math.cos(aa), centre[1] + rr * math.sin(aa), sea_z - rng.uniform(0, 160)))
    sx_ = rng.uniform(2.6, 4.2)
    if near_path(np.array(c), 250.0 * sx_ + 200.0):
        continue
    placed_puffs += 1
    cpts["puff"].append(point(c, Quaternion((0, 0, 1), rng.uniform(0, 6.28)),
                              (sx_, rng.uniform(2.6, 4.2), rng.uniform(0.6, 1.0))))
for k in range(16):  # banks on a ring round the knot
    aa = 2 * math.pi * k / 16 + rng.uniform(-0.1, 0.1)
    c = Vector((centre[0] + 3800 * math.cos(aa), centre[1] + 3800 * math.sin(aa), centre[2] + rng.uniform(-300, 500)))
    cpts["bank"].append(point(c, Quaternion((0, 0, 1), rng.uniform(0, 6.28)),
                              (rng.uniform(2.0, 3.0), rng.uniform(1.4, 2.2), rng.uniform(1.4, 2.4))))
for kind, pts in cpts.items():
    if pts:
        nodes.append(node_points("cloud_" + kind, "cloud_%s.glb" % kind, pts,
                                 {"program": "rw_cloud", "roughness": 1.0, "metallic": 0.0, "unlit": True},
                                 castsShadow=False, lod={"cull": True}))
print("CLOUDS", {k: len(v) for k, v in cpts.items()})

# ---- scene and project ----------------------------------------------------------------------------------------------
env, lights = look.environment(SUN, rw.to_avgen(Vector((0, 0, 1))))
scene = {"format": "avgen-scene", "version": 1, "name": "rainbow-world",
         "camera": {"mode": 2, "spline": "ride", "fov": 106.0, "orbitSpeed": 0.0},
         "environment": env, "lights": lights, "materialPrograms": programs, "nodes": nodes}
with open(os.path.join(OUT, "rainbow-world.scene.json"), "w") as f:
    json.dump(scene, f)
params = look.post_parameters()
params.update({"camera/splineUp": 1.0, "camera/lookAhead": 40.0, "post/lens/fisheye": 1.0, "post/lens/fisheyeModel": 0.0,
               "camera/far": 30000.0, "sources/ride/rate": 1.0 / a.loop_seconds})
project = {
    "format": "avgen-project", "version": 4, "app": {"name": "Rainbow World"},
    "assets": {"scene": {"kind": "composition", "path": "rainbow-world.scene.json"}},
    "sonic": {"live": True},
    "parameters": params,
    "sources": [{"kind": "lfo", "name": "ride", "settings": {"shape": "saw"}}],
    "routes": [{"source": "lfo.ride", "target": "camera/splineT", "amount": 1.0, "op": "replace",
                "polarity": "unipolar"}],
    "render": {"width": 1920, "height": 1080, "fps": 30, "output": "video", "path": "renders/rainbow-world.mp4"},
}
with open(os.path.join(OUT, "rainbow-world.json"), "w") as f:
    json.dump(project, f, indent=1)
report = {"loopMetres": L, "cameraMetres": cam_path.length, "loopSeconds": a.loop_seconds,
          "grass": len(gpts), "flowers": len(fpts), "trees": len(tpts), "rimRocks": sum(len(v) for v in by.values()),
          "rainbows": len(rainbow_nodes), "clouds": {k: len(v) for k, v in cpts.items()}, "bandChunks": len(band_nodes)}
with open(os.path.join(OUT, "world_report.json"), "w") as f:
    json.dump(report, f, indent=1)
print("done", json.dumps(report))
