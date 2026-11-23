"""Neon Dreams iteration 19 (owner msg 283: "the glowing underneath the UFOs looks a little CG -- make it look more
professional").

The UFO asset (assets/imported/ufo.gltf, CC0) has a flat light-grey disc under its hull; lit by the fly-by's point light
from below it read as a white plate, and the halo sprites round it as a CG glow. This builds a belly that covers the
disc, modelled so the light comes from fittings in the hull: a gunmetal plate just under the disc, a raised trim ring,
a ring of twelve small emissive panels, a dim frosted lens in the middle and a thin rim strip at the plate's edge.
One GLB per glow colour (nd_ufo.BELLY). Units are the saucer's own after its glTF root (x100: the hull's radius is
230); nd_ufo gives the belly the saucer's node scale, position and rotation tracks.

    Blender -b --factory-startup -P nd_ufo_belly.py
writes ../meshes/vehicles/ufo_belly_<colour>.glb
"""
import math
import os

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "meshes", "vehicles"))
COLOURS = {"cyan": (0.35, 0.85, 1.0), "pink": (1.0, 0.45, 0.9), "green": (0.6, 1.0, 0.7), "amber": (1.0, 0.7, 0.35)}
DISC_Z = -30.4      # the asset's belly disc (z -0.304 x 100), radius 117
PLATE_R = 121.0


def mat(name, base, metal, rough, emit=None, strength=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*base, 1.0)
    b.inputs["Metallic"].default_value = metal
    b.inputs["Roughness"].default_value = rough
    if emit is not None:
        b.inputs["Emission Color"].default_value = (*emit, 1.0)
        b.inputs["Emission Strength"].default_value = strength
    return m


def add(ob, m):
    ob.data.materials.append(m)
    bpy.ops.object.shade_smooth()
    return ob


def build(colour, glow):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    pale = tuple(0.55 + 0.45 * c for c in glow)
    hull = mat("belly_hull", (0.045, 0.047, 0.055), 0.85, 0.38)
    trim = mat("belly_trim", (0.11, 0.11, 0.12), 0.9, 0.3)
    panel = mat("belly_panel", tuple(0.2 * c for c in glow), 0.0, 0.35, glow, 6.0)
    lens = mat("belly_lens", tuple(0.12 * c for c in pale), 0.0, 0.25, pale, 1.1)
    strip = mat("belly_strip", tuple(0.2 * c for c in pale), 0.0, 0.3, pale, 3.0)
    obs = []
    # the plate: a shallow cone so it is not a flat disc (thicker at the middle)
    bpy.ops.mesh.primitive_cone_add(vertices=96, radius1=PLATE_R, radius2=PLATE_R * 0.55, depth=3.0,
                                    location=(0, 0, DISC_Z - 1.5 - 0.2), rotation=(math.pi, 0, 0))
    obs.append(add(bpy.context.object, hull))
    # trim ring round the lens and a second one inside the panels
    for R, r in ((44.0, 1.6), (70.0, 1.0)):
        bpy.ops.mesh.primitive_torus_add(major_radius=R, minor_radius=r, major_segments=96, minor_segments=8,
                                         location=(0, 0, DISC_Z - 3.4))
        obs.append(add(bpy.context.object, trim))
    # the frosted lens: a low dome
    bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=16, radius=42.0, location=(0, 0, DISC_Z - 3.0))
    ob = bpy.context.object
    ob.scale = (1.0, 1.0, 0.09)
    obs.append(add(ob, lens))
    # twelve panels, tangential, set slightly proud of the plate's underside
    for i in range(12):
        a = 2.0 * math.pi * (i + 0.5) / 12.0
        R = 92.0
        bpy.ops.mesh.primitive_cube_add(size=1.0, location=(R * math.cos(a), R * math.sin(a), DISC_Z - 3.0),
                                        rotation=(0, 0, a + math.pi / 2))
        ob = bpy.context.object
        ob.scale = (17.0, 6.0, 1.2)
        obs.append(add(ob, panel))
    # the rim strip at the plate's edge
    bpy.ops.mesh.primitive_torus_add(major_radius=PLATE_R - 3.0, minor_radius=0.9, major_segments=128, minor_segments=8,
                                     location=(0, 0, DISC_Z - 1.0))
    obs.append(add(bpy.context.object, strip))
    bpy.ops.object.select_all(action="DESELECT")
    for ob in obs:
        ob.select_set(True)
    bpy.context.view_layer.objects.active = obs[0]
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    bpy.ops.object.join()
    bpy.context.object.name = "ufo_belly_" + colour
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "ufo_belly_%s.glb" % colour)
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_apply=True,
                              export_yup=True)
    print("wrote", path)


for c, g in COLOURS.items():
    build(c, g)
