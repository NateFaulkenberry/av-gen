"""The Earth's stylised glow (Art Pass 2, M3c): three options, each a flat emissive figure in the plane facing the
viewer, radius 10 = the Earth's (scaled by the scene), set just behind the planet so it frames the disc.

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python \
        examples/strange-attractor/pass2/build_earth_halo.py -- assets/strange-attractor/build

- earth-halo-a.glb: chromatic rims -- three thin rings a step out of register (magenta, cyan, gold), with a soft blue
  atmosphere band inside them;
- earth-halo-b.glb: scanline glow -- horizontal bars stacked behind the disc out to 1.6 R, brightest at the limb, some
  bars broken and slid sideways (a glitching CRT halo);
- earth-halo-c.glb: pixel corona -- square blocks in a ring 1.03-1.45 R, sized and lit by a hash, a few displaced
  outward (a datamoshed, blocky glow).
Each is in the x-y plane, facing +z, radius 10 = the Earth's; colours are emissive. Separate objects per colour so the export keeps them.
"""
import bpy
import bmesh
import math
import random
import sys

out_dir = sys.argv[sys.argv.index("--") + 1]


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def emissive(name, col, strength=4.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (0, 0, 0, 1)
    b.inputs["Emission Color"].default_value = (*col, 1.0)
    b.inputs["Emission Strength"].default_value = strength
    return m


def obj_from(bm, name, mat):
    # radius 10 in the file (a scene node's scale is capped at 100; the scene scales it by the Earth's radius / 10)
    bmesh.ops.scale(bm, vec=(10.0, 10.0, 10.0), verts=bm.verts)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    me.materials.append(mat)
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    return o


def ring(bm, r0, r1, cx=0.0, cy=0.0, n=160, z=0.0):
    vs = []
    for i in range(n):
        a = 2 * math.pi * i / n
        vs.append((bm.verts.new((cx + r0 * math.cos(a), cy + r0 * math.sin(a), z)),
                   bm.verts.new((cx + r1 * math.cos(a), cy + r1 * math.sin(a), z))))
    for i in range(n):
        a0, b0 = vs[i]
        a1, b1 = vs[(i + 1) % n]
        bm.faces.new((a0, b0, b1, a1))


def quad(bm, x0, y0, x1, y1, z=0.0):
    v = [bm.verts.new((x0, y0, z)), bm.verts.new((x1, y0, z)), bm.verts.new((x1, y1, z)), bm.verts.new((x0, y1, z))]
    bm.faces.new(v)


def export(name):
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(filepath=f"{out_dir}/{name}.glb", use_selection=True)
    print("HALO", name)


# A: chromatic rims
reset()
for tag, col, r0, dx, dy in (("m", (1.0, 0.15, 0.7), 1.07, -0.012, 0.004), ("c", (0.1, 0.9, 1.0), 1.07, 0.012, -0.004),
                             ("y", (1.0, 0.78, 0.25), 1.15, 0.0, 0.0)):
    bm = bmesh.new()
    ring(bm, r0, r0 + 0.018, dx, dy)
    obj_from(bm, f"rim-{tag}", emissive(f"rim_{tag}", col, 5.0))
bm = bmesh.new()
ring(bm, 0.99, 1.045)
obj_from(bm, "atmos", emissive("atmos", (0.25, 0.55, 1.0), 2.2))
export("earth-halo-a")

# B: scanline glow
reset()
rnd = random.Random(7)
bm = bmesh.new()
bm2 = bmesh.new()
n = 64
for i in range(n):
    y = -1.6 + 3.2 * (i + 0.5) / n
    half = math.sqrt(max(1.6 * 1.6 - y * y, 0.0))
    if half <= 0.0:
        continue
    slide = rnd.uniform(-0.25, 0.25) if rnd.random() < 0.18 else 0.0
    target = bm2 if abs(y) < 1.0 and rnd.random() < 0.3 else bm
    quad(target, -half + slide, y - 0.012, half + slide, y + 0.012)
obj_from(bm, "scan", emissive("scan", (0.2, 0.75, 1.0), 2.6))
obj_from(bm2, "scan-hot", emissive("scan_hot", (0.95, 0.35, 1.0), 3.5))
export("earth-halo-b")

# C: pixel corona
reset()
rnd = random.Random(11)
bmc = {k: bmesh.new() for k in ("b", "c", "w")}
for i in range(900):
    a = rnd.uniform(0, 2 * math.pi)
    r = 1.03 + 0.42 * rnd.random() ** 1.8
    s = 0.012 + 0.05 * rnd.random() ** 2 * (1.6 - (r - 1.0))
    if rnd.random() < 0.08:
        r += rnd.uniform(0.1, 0.35)          # datamoshed outliers
    x, y = r * math.cos(a), r * math.sin(a)
    x, y = round(x / 0.02) * 0.02, round(y / 0.02) * 0.02   # on a pixel grid
    k = "w" if rnd.random() < 0.08 else ("c" if rnd.random() < 0.45 else "b")
    quad(bmc[k], x - s, y - s, x + s, y + s)
for k, col, st in (("b", (0.15, 0.4, 1.0), 2.6), ("c", (0.2, 0.95, 1.0), 3.2), ("w", (0.95, 0.95, 1.0), 4.0)):
    obj_from(bmc[k], f"px-{k}", emissive(f"px_{k}", col, st))
bm = bmesh.new()
ring(bm, 0.99, 1.04)
obj_from(bm, "atmos", emissive("atmos", (0.25, 0.55, 1.0), 2.0))
export("earth-halo-c")
