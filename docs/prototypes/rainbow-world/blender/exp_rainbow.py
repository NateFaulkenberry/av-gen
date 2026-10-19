"""Gate-1 experiment E3: what makes a rainbow read as LIGHT rather than as a striped candy object?

Throwaway look test. One meadow, one sun, one camera; the rainbow arc is rendered four ways:
  a_flat     a flat ribbon with six hard stripes (the strawman the brief warns against)
  b_tube     a soft volumetric tube: emission-only volume, a smooth spectral ramp across the radius,
             gaussian density falloff from the tube's core, no surface at all
  c_light    b + the bright inner skirt (the sky inside a real bow is lighter), a faint reversed
             secondary arc outside with a darker gap between them (Alexander's band), supernumerary
             fringe on the violet edge
  d_spill    c + the arc's colour spilling onto the meadow under it (coloured lights along the arc)
Run: Blender -b --factory-startup -P exp_rainbow.py -- <outdir> [w h]
"""
import math
import os
import sys

import bpy
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
out = argv[0] if argv else "build/rw/exp_rainbow"
W = int(argv[1]) if len(argv) > 1 else 640
H = int(argv[2]) if len(argv) > 2 else 360
os.makedirs(out, exist_ok=True)

# a smooth spectral ramp, violet (inner, t=0) -> red (outer, t=1), linear RGB
SPECTRUM = [(0.00, (0.20, 0.02, 0.55)), (0.14, (0.10, 0.05, 0.95)), (0.30, (0.00, 0.35, 0.90)),
            (0.45, (0.00, 0.75, 0.25)), (0.60, (0.75, 0.90, 0.00)), (0.72, (1.00, 0.55, 0.00)),
            (0.86, (1.00, 0.12, 0.02)), (1.00, (0.60, 0.00, 0.02))]
R0, TUBE = 60.0, 4.0
CENTRE = Vector((0, 90, -12))


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    # meadow
    bpy.ops.mesh.primitive_grid_add(x_subdivisions=160, y_subdivisions=160, size=500)
    g = bpy.context.object
    for v in g.data.vertices:
        x, y = v.co.x, v.co.y
        v.co.z = (6 * math.sin(x * 0.03) * math.cos(y * 0.025) + 14 * math.exp(-((x + 70) ** 2 + (y - 140) ** 2) / 3000)
                  + 18 * math.exp(-((x - 90) ** 2 + (y - 170) ** 2) / 4000) - 2)
    bpy.ops.object.shade_smooth()
    m = bpy.data.materials.new("grass")
    m.use_nodes = True
    m.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.06, 0.26, 0.035, 1)
    m.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 0.9
    g.data.materials.append(m)
    # sky
    w = bpy.data.worlds.new("sky")
    sc.world = w
    w.use_nodes = True
    sky = w.node_tree.nodes.new("ShaderNodeTexSky")
    try:
        sky.sky_type = "MULTIPLE_SCATTERING"
    except Exception:
        pass
    sky.sun_elevation = math.radians(35)
    sky.sun_rotation = math.radians(180)
    w.node_tree.links.new(sky.outputs[0], w.node_tree.nodes["Background"].inputs[0])
    w.node_tree.nodes["Background"].inputs[1].default_value = 0.18
    bpy.ops.object.light_add(type="SUN", rotation=(math.radians(55), 0, math.radians(180)))
    bpy.context.object.data.energy = 3.5
    bpy.ops.object.camera_add(location=(0, -40, 6))
    cam = bpy.context.object
    sc.camera = cam
    cam.data.lens = 18
    cam.rotation_mode = "QUATERNION"
    cam.rotation_quaternion = Vector((0, 1, 0.22)).normalized().to_track_quat("-Z", "Y")
    sc.render.engine = "CYCLES"
    sc.cycles.samples = 48
    sc.cycles.use_denoising = True
    sc.cycles.volume_step_rate = 4.0
    try:
        prefs = bpy.context.preferences.addons["cycles"].preferences
        prefs.compute_device_type = "METAL"
        prefs.get_devices()
        for dv in prefs.devices:
            dv.use = True
        sc.cycles.device = "GPU"
    except Exception as e:
        print("GPU setup failed:", e)
    sc.view_settings.view_transform = "AgX"
    sc.view_settings.exposure = -1.3
    try:
        sc.view_settings.look = "AgX - Punchy"
    except Exception as e:
        print("look", e)
    sc.render.resolution_x, sc.render.resolution_y = W, H
    return sc


def ramp_node(nt, stops):
    r = nt.nodes.new("ShaderNodeValToRGB")
    els = r.color_ramp.elements
    els[0].position, els[0].color = stops[0][0], (*stops[0][1], 1)
    els[1].position, els[1].color = stops[-1][0], (*stops[-1][1], 1)
    for p, c in stops[1:-1]:
        e = els.new(p)
        e.color = (*c, 1)
    return r


def radial(nt, r_in, r_out):
    tc = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    nt.links.new(tc.outputs["Object"], sep.inputs[0])
    cmb = nt.nodes.new("ShaderNodeCombineXYZ")
    nt.links.new(sep.outputs[0], cmb.inputs[0])
    nt.links.new(sep.outputs[1], cmb.inputs[1])
    ln = nt.nodes.new("ShaderNodeVectorMath")
    ln.operation = "LENGTH"
    nt.links.new(cmb.outputs[0], ln.inputs[0])
    mr = nt.nodes.new("ShaderNodeMapRange")
    mr.inputs[1].default_value = r_in
    mr.inputs[2].default_value = r_out
    nt.links.new(ln.outputs["Value"], mr.inputs[0])
    return mr, sep, ln


def torus(name, major, minor):
    bpy.ops.mesh.primitive_torus_add(major_radius=major, minor_radius=minor, major_segments=200, minor_segments=32,
                                     location=CENTRE, rotation=(math.radians(90), 0, 0))
    o = bpy.context.object
    o.name = name
    return o


def flat_band():
    n = 200
    vs, fs = [], []
    for i in range(n):
        a = 2 * math.pi * i / n
        for r in (R0 - TUBE, R0 + TUBE):
            vs.append((r * math.cos(a), r * math.sin(a), 0.0))
    for i in range(n):
        j = (i + 1) % n
        fs.append((2 * i, 2 * i + 1, 2 * j + 1, 2 * j))
    me = bpy.data.meshes.new("flat")
    me.from_pydata(vs, [], fs)
    o = bpy.data.objects.new("flat", me)
    bpy.context.scene.collection.objects.link(o)
    o.location = CENTRE
    o.rotation_euler = (math.radians(90), 0, 0)
    m = bpy.data.materials.new("flat")
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    o_ = nt.nodes.new("ShaderNodeOutputMaterial")
    mr, _, _ = radial(nt, R0 - TUBE, R0 + TUBE)
    r = nt.nodes.new("ShaderNodeValToRGB")
    r.color_ramp.interpolation = "CONSTANT"
    six = [(0.0, (0.3, 0.0, 0.8)), (1 / 6, (0.0, 0.2, 1.0)), (2 / 6, (0.0, 0.8, 0.1)), (3 / 6, (1.0, 0.9, 0.0)),
           (4 / 6, (1.0, 0.35, 0.0)), (5 / 6, (1.0, 0.0, 0.0))]
    r.color_ramp.elements[0].position, r.color_ramp.elements[0].color = 0.0, (*six[0][1], 1)
    r.color_ramp.elements[1].position, r.color_ramp.elements[1].color = six[-1][0], (*six[-1][1], 1)
    for p, c in six[1:-1]:
        e = r.color_ramp.elements.new(p)
        e.color = (*c, 1)
    nt.links.new(mr.outputs[0], r.inputs[0])
    b = nt.nodes.new("ShaderNodeBsdfPrincipled")
    nt.links.new(r.outputs[0], b.inputs["Base Color"])
    nt.links.new(r.outputs[0], b.inputs["Emission Color"])
    b.inputs["Emission Strength"].default_value = 0.6
    nt.links.new(b.outputs[0], o_.inputs[0])
    o.data.materials.append(m)


def volume_bow(name, major, minor, stops, strength, density_sigma):
    o = torus(name, major, minor)
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    o_ = nt.nodes.new("ShaderNodeOutputMaterial")
    mr, sep, ln = radial(nt, major - minor, major + minor)
    r = ramp_node(nt, stops)
    nt.links.new(mr.outputs[0], r.inputs[0])
    # distance from the tube's core: sqrt((len - major)^2 + z^2)
    sub = nt.nodes.new("ShaderNodeMath")
    sub.operation = "SUBTRACT"
    nt.links.new(ln.outputs["Value"], sub.inputs[0])
    sub.inputs[1].default_value = major
    sq1 = nt.nodes.new("ShaderNodeMath")
    sq1.operation = "MULTIPLY"
    nt.links.new(sub.outputs[0], sq1.inputs[0])
    nt.links.new(sub.outputs[0], sq1.inputs[1])
    sq2 = nt.nodes.new("ShaderNodeMath")
    sq2.operation = "MULTIPLY"
    nt.links.new(sep.outputs[2], sq2.inputs[0])
    nt.links.new(sep.outputs[2], sq2.inputs[1])
    add = nt.nodes.new("ShaderNodeMath")
    add.operation = "ADD"
    nt.links.new(sq1.outputs[0], add.inputs[0])
    nt.links.new(sq2.outputs[0], add.inputs[1])
    g = nt.nodes.new("ShaderNodeMath")
    g.operation = "MULTIPLY"
    nt.links.new(add.outputs[0], g.inputs[0])
    g.inputs[1].default_value = -1.0 / (2 * density_sigma * density_sigma)
    ex = nt.nodes.new("ShaderNodeMath")
    ex.operation = "EXPONENT"
    nt.links.new(g.outputs[0], ex.inputs[0])
    st = nt.nodes.new("ShaderNodeMath")
    st.operation = "MULTIPLY"
    nt.links.new(ex.outputs[0], st.inputs[0])
    st.inputs[1].default_value = strength
    em = nt.nodes.new("ShaderNodeEmission")
    nt.links.new(r.outputs[0], em.inputs[0])
    nt.links.new(st.outputs[0], em.inputs[1])
    nt.links.new(em.outputs[0], o_.inputs["Volume"])
    o.data.materials.append(m)
    return o


def shoot(sc, name):
    sc.render.filepath = os.path.join(out, name)
    bpy.ops.render.render(write_still=True)


sc = reset()
flat_band()
shoot(sc, "a_flat.png")

sc = reset()
volume_bow("bow", R0, TUBE * 1.6, SPECTRUM, 1.2, TUBE * 0.75)
shoot(sc, "b_tube.png")

def full_bow(sc):
    # primary with a supernumerary echo on the violet edge
    sup = [(0.0, (0.0, 0.0, 0.0)), (0.06, (0.25, 0.05, 0.6)), (0.12, (0.0, 0.5, 0.3)), (0.18, (0.0, 0.0, 0.0))]
    volume_bow("bow", R0, TUBE * 1.6, SPECTRUM, 1.3, TUBE * 0.75)
    volume_bow("supernumerary", R0 - TUBE * 1.9, TUBE * 0.8, [(0.0, (0.2, 0.0, 0.5)), (0.5, (0.0, 0.5, 0.35)),
                                                             (1.0, (0.1, 0.2, 0.6))], 0.35, TUBE * 0.4)
    # inner skirt: a broad, faint white veil inside the bow
    volume_bow("skirt", R0 - TUBE * 5.0, TUBE * 3.6, [(0.0, (0.9, 0.95, 1.0)), (1.0, (1.0, 1.0, 1.0))], 0.10, TUBE * 2.6)
    # secondary, reversed and fainter, outside a darker gap
    rev = [(1 - p, c) for p, c in reversed(SPECTRUM)]
    volume_bow("secondary", R0 * 1.24, TUBE * 2.2, rev, 0.28, TUBE * 1.0)


sc = reset()
full_bow(sc)
shoot(sc, "c_light.png")

sc = reset()
full_bow(sc)
for k in range(9):
    a = math.radians(15 + k * 18.75)
    t = k / 8.0
    col = SPECTRUM[min(int(t * (len(SPECTRUM) - 1)), len(SPECTRUM) - 1)][1]
    p = CENTRE + Vector((math.cos(a) * R0, 0, math.sin(a) * R0 * 0.35 + 4))
    bpy.ops.object.light_add(type="POINT", location=p)
    L = bpy.context.object.data
    L.energy = 2.5e5
    L.color = col
    L.shadow_soft_size = 8
shoot(sc, "d_spill.png")
print("done", out)
