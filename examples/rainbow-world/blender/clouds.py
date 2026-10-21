"""Rainbow World, Stage 2 prototype: a hero cumulus mass, authored as lobes (ADR-1289's format).

A designed silhouette, built in three passes as a cloud painter would block it in:
  1. the BASE: a row of broad, flattened lobes on one shared floor (cumulus have flat bottoms);
  2. the TOWERS: three leaning stacks of shrinking lobes, of unequal height (one dominant crown);
  3. the CAULIFLOWER: two generations of smaller lobes budded onto the upper, sun-facing skin of the
     bigger ones, big -> medium -> small, so edges read as layered billows, not as one blob.
Nothing is placed below the floor. The lobes are written in AV Gen's Y-up frame for the `clouds` block.

Outputs (args: <outdir> [w h samples]):
  <outdir>/hero_cloud.lobes.json   {"masses": {"hero_a": [...]}} -- consumed by AV Gen (E4)
  <outdir>/cloud_<view>.png        Cycles volume previews (the look target; AV Gen is the truth)
"""
import json
import math
import os
import random
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rw_common as rw  # noqa: E402

FLOOR = 0.0


def author_cloud(seed=8, width=620.0):
    rng = random.Random(seed)
    lobes = []  # (centre Vector, radius, scale (x, y, z), density, soft)

    def add(c, r, sc=(1, 1, 1), dens=1.0, soft=0.35):
        # keep the floor flat: a lobe may not reach below it by more than a sliver
        # (gate 8: a lobe may round 30% of its own height under the floor, so the base is a soft, rounded belly and
        # not a flat cut with a hard rim -- the owner saw the undersides jagged when the ride turns upside down)
        sink = 4.0 + 0.3 * r * sc[2]
        lowest = c.z - r * sc[2]
        if lowest < FLOOR - sink:
            c = Vector((c.x, c.y, FLOOR - sink + r * sc[2]))
        lobes.append((c, r, sc, dens, soft))

    # 1. base
    n = 9
    for i in range(n):
        x = (i / (n - 1) - 0.5) * width * 0.85 + rng.uniform(-20, 20)
        r = rng.uniform(70, 95) * (1.0 - 0.35 * abs(i / (n - 1) - 0.5) * 2)
        add(Vector((x, rng.uniform(-40, 40), r * 0.55)), r, (1.25, 1.0, 0.6), 1.0, 0.3)
    # 2. towers: (x position, height, lean)
    towers = [(-130.0, 230.0, 0.15), (40.0, 340.0, -0.08), (190.0, 180.0, 0.25)]
    for tx, th, lean in towers:
        z, r = 60.0, 85.0
        x, y = tx, rng.uniform(-25, 25)
        while z < th:
            add(Vector((x, y, z)), r, (1.0, 0.95, 0.9), 1.0, 0.33)
            z += r * 0.75
            x += lean * r * 0.9
            r *= 0.84
    # 3. cauliflower, two generations on the upper, sun-facing skin
    sun = Vector((0.5, -0.45, 0.75)).normalized()
    for gen, (count, rmin, rmax) in enumerate(((140, 22.0, 42.0), (260, 9.0, 20.0))):
        parents = list(lobes)
        for _ in range(count):
            c, r, sc, _, _ = rng.choice(parents)
            if r < rmin * 1.4:
                continue
            d = Vector((rng.gauss(0, 1), rng.gauss(0, 1), abs(rng.gauss(0, 1)) * 1.3 + 0.2)).normalized()
            if d.z < 0.05:
                continue
            p = c + Vector((d.x * r * sc[0], d.y * r * sc[1], d.z * r * sc[2])) * 0.92
            rr = rng.uniform(rmin, rmax) * (1.0 + 0.25 * max(0.0, d.dot(sun)))
            add(p, rr, (1.0, 1.0, 0.92), 0.9, 0.38 if gen == 0 else 0.45)
    return lobes


def write_lobes(lobes, path, name="hero_a", offset=Vector((0, 0, 0))):
    arr = []
    for c, r, sc, dens, soft in lobes:
        p = rw.to_avgen(c + offset)
        # AV Gen's scale is per axis in its own (Y-up) frame: x, z(up), -y
        arr.append({"c": [round(v, 2) for v in p], "r": round(r, 2),
                    "scale": [round(sc[0], 3), round(sc[2], 3), round(sc[1], 3)],
                    "density": round(dens, 3), "soft": round(soft, 3)})
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w") as f:
        json.dump({"masses": {name: arr}}, f, indent=0)
    print("LOBES", len(arr), path)


def preview_volume(lobes):
    """Cycles preview: metaball union of the lobes -> mesh -> volume, eroded by noise in the shader."""
    mb = bpy.data.metaballs.new("cloud_mb")
    mb.resolution = 6.0
    mb.render_resolution = 5.0
    mb.threshold = 0.6
    for c, r, sc, dens, soft in lobes:
        e = mb.elements.new()
        e.type = "ELLIPSOID"
        e.co = c
        e.radius = r * 1.15
        e.size_x, e.size_y, e.size_z = sc[0], sc[1], sc[2]
    ob = bpy.data.objects.new("cloud_mb", mb)
    bpy.context.scene.collection.objects.link(ob)
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    bpy.ops.object.convert(target="MESH")
    mesh = bpy.context.view_layer.objects.active
    m = bpy.data.materials.new("cloud")
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    pv = nt.nodes.new("ShaderNodeVolumePrincipled")
    pv.inputs["Color"].default_value = (1, 1, 1, 1)
    pv.inputs["Anisotropy"].default_value = 0.55
    nz = nt.nodes.new("ShaderNodeTexNoise")
    nz.inputs["Scale"].default_value = 0.012
    nz.inputs["Detail"].default_value = 5.0
    nz.inputs["Roughness"].default_value = 0.55
    tc = nt.nodes.new("ShaderNodeTexCoord")
    nt.links.new(tc.outputs["Object"], nz.inputs["Vector"])
    d = rw.math_node(nt, "MULTIPLY_ADD", nz.outputs["Fac"], 2.2)
    d.node.inputs[2].default_value = -0.75
    d = rw.math_node(nt, "MAXIMUM", d, 0.0)
    d = rw.math_node(nt, "MULTIPLY", d, 0.14)
    nt.links.new(d, pv.inputs["Density"])
    nt.links.new(pv.outputs[0], out.inputs["Volume"])
    mesh.data.materials.append(m)
    return mesh


def main():
    args = rw.argv_after_dashes()
    out = os.path.abspath(args[0] if args else "build/rw/stage2/cloud")
    W = int(args[1]) if len(args) > 1 else 960
    H = int(args[2]) if len(args) > 2 else 540
    S = int(args[3]) if len(args) > 3 else 64
    os.makedirs(out, exist_ok=True)
    lobes = author_cloud()
    write_lobes(lobes, os.path.join(out, "hero_cloud.lobes.json"))
    sc = rw.reset()
    sun = Vector((0.5, -0.45, 0.75)).normalized()
    rw.sky_world(sc, sun, strength=0.95)
    rw.sun_light(sun, strength=5.0)
    fill = rw.sun_light(Vector((-0.2, 0.3, -0.9)), strength=1.0, angle_deg=40, color=rw.P["cloud_bounce"])
    fill.data.use_shadow = False
    preview_volume(lobes)
    rw.setup_render(sc, W, H, samples=S)
    sc.cycles.volume_bounces = 2
    sc.cycles.volume_step_rate = 1.0
    cam = rw.camera(lens=28)
    rw.aim(cam, (150, -1000, -120), (20, 0, 200))
    rw.render(sc, os.path.join(out, "cloud_front.png"))
    rw.aim(cam, (-800, 700, -60), (20, 0, 200))
    rw.render(sc, os.path.join(out, "cloud_side_backlit.png"))
    rw.save_blend(os.path.join(out, "hero_cloud.blend"))
    # E4b: the same mass as a mesh (the cheap representation to compare against the lobe march)
    mesh = [o for o in bpy.data.objects if o.type == "MESH" and o.name.startswith("cloud_mb")][0]
    mesh.data.materials.clear()
    dec = mesh.modifiers.new("dec", "DECIMATE")
    dec.ratio = min(1.0, 24000 / max(1, len(mesh.data.polygons)))
    bpy.context.view_layer.objects.active = mesh
    bpy.ops.object.modifier_apply(modifier="dec")
    bpy.ops.object.shade_smooth()
    rw.export_glb([mesh], os.path.join(out, "hero_cloud_mesh.glb"))
    print("CLOUD MESH TRIS", len(mesh.data.polygons))
    print("done")


if __name__ == "__main__":
    main()
