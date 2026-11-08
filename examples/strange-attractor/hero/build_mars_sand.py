"""Pass 3 final revisions (67- §4, the owner's M3g §3 note): the pyramids' sand drifts laid ON the Mars ground, so the
drift and the plain read as one surface.

    Blender -b --factory-startup --python examples/strange-attractor/hero/build_mars_sand.py -- \
        assets/strange-attractor/hero/mars [--sampler path/to/avgen_tests]

Writes <out>/pyramid-{a,b,c}-sand-p3.glb (gitignored; build_mars.py's nodes name them) and caches the ground heights it
sampled in <out>/ground-<name>.json. The sampler is the SA agent's temporary `[.sample]` test (not committed); without
--sampler the cached heights are used.

Why the old drift (pass2/build_pyramids.py, `<name>-sand-m3g.glb`) showed a hard edge: it is a height field in the
pyramid's own frame, and the pyramid is sunk 3 m (build_mars.py PYRAMID_SINK) into a pad whose ground is not exactly
level. Where the drift's surface crossed the ground it crossed at a slope, often several metres up its flank, so it
read as a separate slab standing on the plain with a crisp line round it.

The new drift has the same plan and profile (the same `sand()` as build_pyramids.py: the wind-banked crest, deepest to
windward, scoured at the corners, the cubic fall to the toe), but its height is measured from the real ground under
every vertex (sampled from the scene's attractorWorld) rather than from a flat plane:
    height (node frame) = ground + softmax(sand - SINK, 0) - toe   (out from the base; the old drift at the base)
  where sand - SINK is the old drift's thickness over a level pad, so the footprint stays the old one (within a metre)
- near the base (lift 0) it is exactly the old drift, so the burial line on the stones and the sand in the courses
  (build_pyramids.py's dust faces) are unchanged;
- out from the base (lift -> 1 over a quarter of a half-base) it rises the 3 m of the pyramid's sink and lies on the
  ground;
- at the toe it sinks under the ground by `toe` (0.25-0.75 m, an fBm over 18 m), so the drift meets the plain where
  it is a few tens of centimetres thick and nearly tangent to it, along a line the noise breaks up -- the "noise-broken
  edge" -- and nothing of it stands proud of the ground beyond that.
The material is the scene's (marsDrift and marsRock are made one material in build_mars.py), so the vertices carry no
UVs that matter.
"""
import json
import math
import os
import subprocess
import sys
import tempfile

import bmesh
import bpy

argv = sys.argv[sys.argv.index("--") + 1:]
out_dir = argv[0]
sampler = argv[argv.index("--sampler") + 1] if "--sampler" in argv else None
os.makedirs(out_dir, exist_ok=True)
HERE = os.path.dirname(os.path.abspath(__file__))
SCENE = os.path.join(os.path.dirname(HERE), "strange-attractor-mars.scene.json")

SLOPE = math.tan(math.radians(51.8))
SINK = 3.0                     # build_mars.py PYRAMID_SINK
NODE_Y = 32.0                  # build_mars.py PAD_LEVEL - PYRAMID_SINK (the nodes' y)
# name, base (m), burial, seed: build_pyramids.py's PYRAMIDS and STATES; x, z, yaw: build_mars.py's PYRAMIDS
PYR = [("pyramid-a", 300.0, 0.30, 11, 302.0, 1662.0, 8.0),
       ("pyramid-b", 280.0, 0.26, 23, 722.0, 1362.0, 0.0),
       ("pyramid-c", 140.0, 0.36, 37, 1032.0, 1062.0, 14.0)]
TOE = (0.25, 0.75, 18.0)       # the toe's sink under the ground: min, max, the noise's wavelength (m)
SOFT = 1.5                     # the soft maximum's width (m): how far the thin edge is flattened onto the ground


# ---- build_pyramids.py's noise and drift profile, verbatim ----------------------------------------------------------
def hash2(i, j, seed):
    h = (i * 374761393 + j * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFFFF) / float(0xFFFFFF)


def vnoise(x, y, seed):
    i, j = math.floor(x), math.floor(y)
    fx, fy = x - i, y - j
    ux, uy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a, b = hash2(i, j, seed), hash2(i + 1, j, seed)
    c, d = hash2(i, j + 1, seed), hash2(i + 1, j + 1, seed)
    return (a + (b - a) * ux) * (1 - uy) + (c + (d - c) * ux) * uy


def fbm(x, y, seed, octaves=4):
    s, amp, f, norm = 0.0, 1.0, 1.0, 0.0
    for o in range(octaves):
        s += amp * vnoise(x * f, y * f, seed + o * 17)
        norm += amp
        amp *= 0.5
        f *= 2.0
    return s / norm


def smooth(a, b, x):
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3 - 2 * t)


def make_sand(half, H, burial, seed):
    def sand(x, y):
        m = max(abs(x), abs(y))
        westness = smooth(-1.0, 1.0, -x / half)
        mid = 1.0 - min(abs(abs(x) - abs(y)) / max(m, 1e-3), 1.0)
        crest = burial * H * (0.7 + 0.45 * westness) * (0.78 + 0.44 * fbm(x / 90.0, y / 90.0, seed + 13))
        crest *= 1.0 - 0.4 * mid ** 2
        if m < half:
            return crest + (half - m) * 0.18
        out = math.hypot(max(abs(x) - half, 0.0), max(abs(y) - half, 0.0))
        run = crest * (2.4 + 2.2 * westness) * (0.85 + 0.3 * fbm(x / 70.0, y / 70.0, seed + 17))
        run = min(run, half * (0.75 + 0.9 * westness))
        t = min(out / max(run, 1.0), 1.0)
        prof = 1.0 - t * t * (3.0 - 2.0 * t)
        swell = (fbm(x / 45.0, y / 45.0, seed + 19) - 0.5) * 0.12 * crest * (1.0 - t)
        return crest * prof + swell
    return sand


def world_xz(px, pz, yaw, x, y):
    a = math.radians(yaw)
    lx, lz = x, -y
    return (px + lx * math.cos(a) + lz * math.sin(a), pz + (-lx * math.sin(a) + lz * math.cos(a)))


def ground_heights(name, pts):
    cache = os.path.join(out_dir, f"ground-{name}.json")
    if sampler:
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
            json.dump({"scene": SCENE, "points": [list(p) for p in pts]}, f)
            req = f.name
        subprocess.run([sampler, "[.sample]"], env={**os.environ, "SAMPLE_REQ": req}, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        hs = json.load(open(req + ".out"))
        json.dump({"n": len(pts), "h": [round(h, 3) for h in hs]}, open(cache, "w"))
        return hs
    c = json.load(open(cache))
    assert c["n"] == len(pts), "the cached grid does not match: re-run with --sampler"
    return c["h"]


bpy.ops.wm.read_factory_settings(use_empty=True)
for name, base, burial, seed, px, pz, yaw in PYR:
    half = base * 0.5
    H = half * SLOPE
    sand = make_sand(half, H, burial, seed)
    reach = half * 2.7
    step = max(base / 130.0, 1.6)
    n = int(2 * reach / step) + 1
    xs = [-reach + i * step for i in range(n)]
    pts = [world_xz(px, pz, yaw, x, y) for y in xs for x in xs]
    ground = ground_heights(name, pts)
    bm = bmesh.new()
    grid = []
    k = 0
    below = 0
    for j, y in enumerate(xs):
        row = []
        for i, x in enumerate(xs):
            m = max(abs(x), abs(y))
            g = ground[k] - NODE_Y                    # the ground in the node's frame
            k += 1
            s = max(sand(x, y), 0.0)
            face = H * (1.0 - m / half) if m < half else 0.0
            lift = smooth(half, half * 1.25, m)
            wx, wz = pts[j * n + i]
            toe = TOE[0] + (TOE[1] - TOE[0]) * fbm(wx / TOE[2], wz / TOE[2], seed + 41)
            if m < half:
                h = s                                  # the old drift exactly: the stones' burial line is unchanged
                if h < face - 0.2:
                    h = face - 0.6                     # tucked under the stone: hidden (build_pyramids.py's rule)
            else:
                # the old drift's thickness over the ground (the pyramid stood 3 m sunk) eased to meet it at a tangent:
                # a soft maximum of 0 and t, so a thick drift is unchanged and its thin edge flattens out onto the ground
                t = s - SINK
                eased = 0.5 * (t + math.sqrt(t * t + SOFT * SOFT))
                h = (1.0 - lift) * s + lift * (g + eased - toe)
                # where there is no sand left, well under the ground: a 2 m cell's straight edge must not cut through
                # the slope of a mesa past the pad
                h -= 3.0 * lift * (1.0 - smooth(SINK - 2.0, SINK - 1.0, s))
            if h < g:
                below += 1
            row.append(bm.verts.new((x, y, h)))
        grid.append(row)
    for j in range(n - 1):
        for i in range(n - 1):
            f = bm.faces.new((grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]))
            f.smooth = True
    me = bpy.data.meshes.new(f"{name}-sand")
    bm.to_mesh(me)
    bm.free()
    mat = bpy.data.materials.new("pyramid_sand")
    mat.use_nodes = True
    mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.46, 0.24, 0.13, 1.0)
    mat.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.95
    me.materials.append(mat)
    ob = bpy.data.objects.new(f"{name}-sand", me)
    bpy.context.scene.collection.objects.link(ob)
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    path = os.path.join(out_dir, f"{name}-sand-p3.glb")
    bpy.ops.export_scene.gltf(filepath=path, use_selection=True, export_apply=True)
    ob.hide_set(True)
    print("DRIFT", path, n, "x", n, "vertices,", below, "under the ground (hidden)")
