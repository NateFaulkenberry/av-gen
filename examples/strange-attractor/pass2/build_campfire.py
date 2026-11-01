"""The Sasquatches' campfire (Art Pass 2, M1b): logs leaned into a cone, a ring of stones and a glowing ember bed.

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup --python \
        examples/strange-attractor/pass2/build_campfire.py -- assets/strange-attractor/build/campfire.glb

All geometry is made here (no downloads), at real size in metres, the fire's centre at the origin on the ground. The
flames, embers and smoke are the engine's particles (build_journey.py); the light is the scene's flickering point
light. The ember bed is emissive so the fire's heart glows between the logs.
"""
import bpy
import bmesh
import math
import random
import sys

out = sys.argv[sys.argv.index("--") + 1]
random.seed(7)
bpy.ops.wm.read_factory_settings(use_empty=True)


def material(name, color, rough, emit=None, strength=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1.0)
    b.inputs["Roughness"].default_value = rough
    if emit:
        b.inputs["Emission Color"].default_value = (*emit, 1.0)
        b.inputs["Emission Strength"].default_value = strength
    return m


bark = material("log_bark", (0.075, 0.05, 0.035), 0.9)
char = material("log_char", (0.02, 0.018, 0.016), 0.95, (1.0, 0.25, 0.05), 0.25)
stones = [material("stone_a", (0.045, 0.038, 0.034), 0.9), material("stone_b", (0.066, 0.052, 0.044), 0.85), material("stone_c", (0.034, 0.031, 0.03), 0.92)]
embers = material("ember_bed", (0.05, 0.03, 0.02), 0.9, (1.0, 0.3, 0.05), 1.8)


def noisy(obj, amount, seed):
    rnd = random.Random(seed)
    for v in obj.data.vertices:
        v.co += v.normal * rnd.uniform(-amount, amount)


# logs: six leaned into a cone, charred toward the fire's heart
for i in range(6):
    a = i / 6.0 * 2.0 * math.pi + random.uniform(-0.15, 0.15)
    L = random.uniform(0.85, 1.05)
    r = random.uniform(0.055, 0.075)
    bpy.ops.mesh.primitive_cylinder_add(vertices=10, radius=r, depth=L)
    log = bpy.context.object
    log.name = f"log{i}"
    noisy(log, 0.008, i)
    tilt = math.radians(random.uniform(52, 62))
    log.rotation_euler = (0.0, -tilt, a)
    # the lower end out on the ground, the upper end over the centre
    log.location = (math.cos(a) * 0.5 * L * math.sin(tilt) * 0.82, math.sin(a) * 0.5 * L * math.sin(tilt) * 0.82,
                    0.5 * L * math.cos(tilt) + 0.04)
    log.data.materials.append(bark)
    log.data.materials.append(char)
    # the inner half charred (faces nearer the centre take the char material)
    bpy.context.view_layer.update()
    mw = log.matrix_world
    for poly in log.data.polygons:
        c = mw @ poly.center
        if math.hypot(c.x, c.y) < 0.28:
            poly.material_index = 1

# stones: a rough ring
for i in range(14):
    a = i / 14.0 * 2.0 * math.pi + random.uniform(-0.08, 0.08)
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=1.0)
    st = bpy.context.object
    st.name = f"stone{i}"
    noisy(st, 0.18, 100 + i)
    s = random.uniform(0.11, 0.17)
    st.scale = (s * random.uniform(1.0, 1.5), s * random.uniform(0.9, 1.2), s * random.uniform(0.55, 0.8))
    R = 0.72 + random.uniform(-0.04, 0.04)
    st.location = (math.cos(a) * R, math.sin(a) * R, s * 0.35)
    st.rotation_euler = (random.uniform(-0.3, 0.3), random.uniform(-0.3, 0.3), a)
    st.data.materials.append(stones[i % 3])

# the ember bed: a low noisy disc of glowing coals
bpy.ops.mesh.primitive_circle_add(vertices=40, radius=0.5, fill_type="TRIFAN")
bed = bpy.context.object
bed.name = "ember_bed"
bpy.ops.object.mode_set(mode="EDIT")
bm = bmesh.from_edit_mesh(bed.data)
bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=3, use_grid_fill=True)
for v in bm.verts:
    d = math.hypot(v.co.x, v.co.y)
    v.co.z = max(0.0, 0.07 * (1.0 - d / 0.5)) + random.uniform(0.0, 0.025)
bmesh.update_edit_mesh(bed.data)
bpy.ops.object.mode_set(mode="OBJECT")
bed.data.materials.append(embers)

# Blender is Z-up; the glTF exporter converts to Y-up
for o in bpy.data.objects:
    o.select_set(True)
bpy.ops.object.shade_smooth()
bpy.ops.export_scene.gltf(filepath=out, export_apply=True, export_animations=False)
print("CAMPFIRE", out, len(bpy.data.objects), "objects")
