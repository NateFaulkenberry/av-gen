"""The domes' rave windows (Art Pass 2, M3c): the dome's own glass panels, split into five colour groups.

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python \
        examples/strange-attractor/pass2/build_dome_rave.py -- assets/strange-attractor/proofs/dome.glb \
        assets/strange-attractor/build

The owner: no beams into the sky -- light the dome WINDOWS as if a rave is going on inside. Reads the dome (its
`dome_panel` faces), and writes dome-rave-{m,c,g,a,v}.glb: each a share of the panels, pushed a hair outward over the
dome's own glass, emissive in one colour (magenta, cyan, green, amber, violet). The groups are bands that wind round the
dome (azimuth plus height), so flashing them in turn reads as light moving inside; each is routed to its own beat
source in the project, so they strobe in different colours, never all at once.
"""
import bpy
import bmesh
import math
import sys

args = sys.argv[sys.argv.index("--") + 1:]
src, out_dir = args[0], args[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
panels = bpy.data.objects["dome_panels"]
me = panels.data
pidx = [i for i, m in enumerate(me.materials) if m.name.startswith("dome_panel")][0]

GROUPS = [("m", (1.0, 0.12, 0.75)), ("c", (0.1, 0.85, 1.0)), ("g", (0.3, 1.0, 0.2)), ("a", (1.0, 0.55, 0.08)),
          ("v", (0.55, 0.2, 1.0))]


def group_of(c):
    az = math.atan2(c.y, c.x)
    h = c.z / 100.0
    band = (az / (2.0 * math.pi) * 5.0 + h * 2.2) % 5.0
    return int(band) % 5


for gi, (tag, col) in enumerate(GROUPS):
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.faces.ensure_lookup_table()
    kill = [f for f in bm.faces if f.material_index != pidx or group_of(f.calc_center_median()) != gi]
    bmesh.ops.delete(bm, geom=kill, context="FACES")
    for v in bm.verts:
        v.co = v.co * 1.004              # a hair outside the glass
    m2 = bpy.data.meshes.new(f"dome-rave-{tag}")
    bm.to_mesh(m2)
    bm.free()
    mat = bpy.data.materials.new(f"rave_{tag}")
    mat.use_nodes = True
    b = mat.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*[c * 0.2 for c in col], 1.0)
    b.inputs["Emission Color"].default_value = (*col, 1.0)
    b.inputs["Emission Strength"].default_value = 5.0
    b.inputs["Roughness"].default_value = 0.3
    m2.materials.clear()
    m2.materials.append(mat)
    for p in m2.polygons:
        p.material_index = 0
    o = bpy.data.objects.new(f"dome-rave-{tag}", m2)
    bpy.context.scene.collection.objects.link(o)
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.ops.export_scene.gltf(filepath=f"{out_dir}/dome-rave-{tag}.glb", use_selection=True)
    print("RAVE", tag, len(m2.polygons))
