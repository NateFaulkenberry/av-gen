"""The underwater base (Art Pass 2, M3): an installation built into the continental shelf's wall, with a launch bay.

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python \
        examples/strange-attractor/pass2/build_base.py -- assets/strange-attractor/build

Writes ocean-base.glb (the structure, in metres, its front facing -y in Blender = +z after export... see FRONT) and
bay-membrane.glb (the faint energy field across the launch bay, the surface the bay portal opens in). All geometry is
made here (no downloads).

The installation reads as old and enormous:
- a stepped, faceted hull set into the cliff, ribbed, with heavy buttresses and pipe runs down to the seabed;
- the launch bay: a deep rectangular hall (60 x 30 m opening), lit inside, its frame lined with light strips;
- rows of windows (cool white), beacon lights on the towers, a warm glow deep in the bay;
- wear: the hull plates offset and pitted (a noise on the faces), dark streaks below the openings (a darker material).
"""
import bpy
import bmesh
import math
import random
import sys

out_dir = sys.argv[sys.argv.index("--") + 1]
random.seed(19)
bpy.ops.wm.read_factory_settings(use_empty=True)


def material(name, color, rough, metal=0.0, emit=None, strength=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1.0)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if emit:
        b.inputs["Emission Color"].default_value = (*emit, 1.0)
        b.inputs["Emission Strength"].default_value = strength
    return m


HULL = material("base_hull", (0.07, 0.08, 0.085), 0.55, 0.6)
HULL_DARK = material("base_hull_stained", (0.035, 0.04, 0.042), 0.7, 0.4)
WINDOW = material("base_window", (0.2, 0.25, 0.28), 0.3, 0.0, (0.55, 0.85, 1.0), 9.0)
STRIP = material("base_strip", (0.2, 0.2, 0.2), 0.3, 0.0, (0.75, 0.92, 1.0), 14.0)
BAYGLOW = material("base_bay_glow", (0.3, 0.28, 0.25), 0.4, 0.0, (1.0, 0.82, 0.6), 2.5)
BEACON = material("base_beacon", (0.3, 0.1, 0.1), 0.3, 0.0, (1.0, 0.25, 0.15), 18.0)
MEMBRANE = material("bay_membrane", (0.05, 0.12, 0.16), 0.2, 0.0, (0.25, 0.55, 0.7), 0.2)

parts = []


def box(name, center, size, mat, rot_z=0.0, noise=0.0):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=center)
    o = bpy.context.object
    o.name = name
    o.scale = size
    o.rotation_euler = (0.0, 0.0, rot_z)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    if noise > 0.0:
        rnd = random.Random(hash(name) & 0xFFFF)
        bm = bmesh.new()
        bm.from_mesh(o.data)
        bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=3, use_grid_fill=True)
        for v in bm.verts:
            v.co += v.normal * rnd.uniform(-noise, noise)
        bm.to_mesh(o.data)
        bm.free()
    o.data.materials.append(mat)
    parts.append(o)
    return o


def cyl(name, center, r, depth, mat, rot=(0.0, 0.0, 0.0), verts=16):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=depth, location=center, rotation=rot)
    o = bpy.context.object
    o.name = name
    o.data.materials.append(mat)
    parts.append(o)
    return o


# Blender axes: the cliff face is the plane y = 0, the base stands out of it toward -y (the front), z up, the seabed z=0.
# The bay's opening is centred at (0, -70, 32), 60 m wide and 30 m tall, 70 m deep into the hull.
# the main hull: three stepped tiers out of the cliff
box("hull_low", (0, -45, 22), (240, 90, 44), HULL, noise=0.6)
box("hull_mid", (10, -30, 58), (190, 60, 30), HULL, noise=0.5)
box("hull_top", (25, -18, 82), (120, 36, 20), HULL, noise=0.4)
# the bay: carve by building it as a frame around a recessed hall (the hall's back wall glows)
box("bay_back", (0, -6, 32), (58, 4, 28), BAYGLOW)
box("bay_floor", (0, -45, 16.5), (60, 80, 3), HULL_DARK)
box("bay_ceiling", (0, -45, 47.5), (60, 80, 3), HULL_DARK)
box("bay_wall_l", (-31.5, -45, 32), (3, 80, 33), HULL_DARK)
box("bay_wall_r", (31.5, -45, 32), (3, 80, 33), HULL_DARK)
# the bay's frame, proud of the hull face, with light strips
box("frame_top", (0, -92, 50), (76, 6, 6), HULL, noise=0.2)
box("frame_bot", (0, -92, 14), (76, 6, 6), HULL, noise=0.2)
box("frame_l", (-36, -92, 32), (6, 6, 42), HULL, noise=0.2)
box("frame_r", (36, -92, 32), (6, 6, 42), HULL, noise=0.2)
for z in (17.5, 46.5):
    box(f"strip_h{z}", (0, -95.2, z), (66, 0.6, 0.6), STRIP)
for x in (-32.5, 32.5):
    box(f"strip_v{x}", (x, -95.2, 32), (0.6, 0.6, 30), STRIP)
# strips receding down the bay's walls (depth)
for k in range(6):
    y = -84 + k * 13
    box(f"bay_rib_l{k}", (-29.6, y, 32), (0.5, 0.5, 26), STRIP)
    box(f"bay_rib_r{k}", (29.6, y, 32), (0.5, 0.5, 26), STRIP)
# the hull face around the bay (the bay opening is left clear: the low hull is split around it)
# (the low hull box covers x -120..120; cut a notch by overlaying dark recess boxes is not enough, so rebuild it in parts)
for o in [p for p in parts if p.name == "hull_low"]:
    parts.remove(o)
    bpy.data.objects.remove(o)
box("hull_low_l", (-78, -45, 22), (84, 90, 44), HULL, noise=0.6)
box("hull_low_r", (78, -45, 22), (84, 90, 44), HULL, noise=0.6)
box("hull_low_sill", (0, -45, 7.5), (72, 90, 15), HULL, noise=0.5)
# buttresses down to the seabed and into the cliff
for x in (-118, -60, 60, 118):
    box(f"buttress{x}", (x, -88, 18), (10, 18, 36), HULL_DARK, noise=0.4)
# towers with windows and beacons
for x, h in ((-70, 34), (70, 28), (40, 22)):
    cyl(f"tower{x}", (x, -40, 73 + h / 2), 9.0, h, HULL, verts=12)
    cyl(f"tower_cap{x}", (x, -40, 73 + h + 1.5), 6.0, 3.0, BEACON, verts=12)
# windows: rows on the tiers' fronts
for tier_y, tier_z, xs in ((-90.3, 34, list(range(-114, -44, 7)) + list(range(46, 116, 7))),
                           (-60.3, 62, list(range(-80, 100, 8))), (-36.3, 85, list(range(-25, 80, 9)))):
    for x in xs:
        if random.random() < 0.82:
            box(f"win{tier_z}_{x}", (x, tier_y, tier_z), (3.2, 0.5, 1.6), WINDOW)
# pipe runs down the face to the seabed
for x in (-100, -95, 92, 97):
    cyl(f"pipe{x}", (x, -91.5, 22), 1.4, 44, HULL_DARK)
# stains below the openings: dark plates
for x in (-36, 36):
    box(f"stain{x}", (x, -91.0, 4), (8, 0.3, 8), HULL_DARK)

# join and export the structure (Blender z-up -> glTF y-up: the front (-y) becomes +z)
for o in bpy.context.scene.objects:
    o.select_set(o in parts)
bpy.context.view_layer.objects.active = parts[0]
bpy.ops.object.join()
base = bpy.context.object
base.name = "ocean-base"
bpy.ops.object.shade_flat()
bpy.ops.export_scene.gltf(filepath=f"{out_dir}/ocean-base.glb", use_selection=True, export_apply=True)
print("BASE", f"{out_dir}/ocean-base.glb", sum(len(p.vertices) - 2 for p in base.data.polygons), "triangles")
base.hide_set(True)
base.select_set(False)

# the membrane across the bay opening: a subdivided plane (the corruption displaces its vertices)
bpy.ops.mesh.primitive_grid_add(x_subdivisions=60, y_subdivisions=30, size=1.0, location=(0, -90, 32), rotation=(math.pi / 2, 0, 0))
mem = bpy.context.object
mem.name = "bay-membrane"
mem.scale = (60, 30, 1)
bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
mem.data.materials.append(MEMBRANE)
mem.select_set(True)
bpy.context.view_layer.objects.active = mem
bpy.ops.export_scene.gltf(filepath=f"{out_dir}/bay-membrane.glb", use_selection=True, export_apply=True)
print("MEMBRANE", f"{out_dir}/bay-membrane.glb")
