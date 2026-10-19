"""Rainbow World: shared Blender helpers (scene reset, render setup, palette, sky, materials, export).

Every production script does `sys.path.insert(0, <this dir>)` and imports this module. Nothing here
depends on an interactive session or on a machine path: outputs are passed in as arguments.

Conventions: Blender is Z-up, metres. AV Gen is Y-up; `to_avgen()` converts positions and rotations
for the JSON this pipeline writes (lobes, points). glTF export converts meshes itself (export_yup).
"""
import math
import os
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector


def argv_after_dashes():
    return sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def srgb_to_lin(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def hexlin(h, a=1.0):
    h = h.lstrip("#")
    return tuple(srgb_to_lin(int(h[i:i + 2], 16) / 255.0) for i in (0, 2, 4)) + (a,)


# The first-pass palette (docs/prototypes/rainbow-world/03-art-direction.md section 5), linear RGBA.
P = {
    "zenith": hexlin("#1C4BB8"), "upper_sky": hexlin("#2F74D6"), "mid_sky": hexlin("#4C9BE8"),
    "haze": hexlin("#A8D8F0"), "violet_sky": hexlin("#4A3FA6"),
    "cloud_top": hexlin("#FFF7EA"), "cloud_core": hexlin("#8F9CC9"), "cloud_deep": hexlin("#5D6AA3"),
    "cloud_bounce": hexlin("#E9B79E"), "cloud_sea": hexlin("#F2E4D6"),
    "meadow_sun": hexlin("#A6CF3A"), "meadow": hexlin("#56A62E"), "grass_deep": hexlin("#2F7D35"),
    "shadow_green": hexlin("#1F5E58"), "olive": hexlin("#7C912B"), "canopy_lit": hexlin("#71B83F"),
    "canopy_core": hexlin("#1E4E2D"),
    "stone_warm": hexlin("#CBA37A"), "stone_cool": hexlin("#808A98"), "stone_shadow": hexlin("#4A5068"),
    "bark": hexlin("#5B3E2C"), "soil": hexlin("#7B5536"),
    "sun": hexlin("#FFE2AE"), "fill": hexlin("#3B5196"),
    "magenta": hexlin("#E23E9C"), "coral": hexlin("#FF6F59"), "lilac": hexlin("#B07CFF"),
    "buttercup": hexlin("#FFD23A"),
}

# Spectral ramp for rainbows, inner (violet, t=0) to outer (red, t=1), linear RGB.
SPECTRUM = [(0.00, (0.16, 0.00, 0.50)), (0.12, (0.18, 0.02, 1.00)), (0.27, (0.00, 0.28, 1.00)),
            (0.42, (0.00, 0.80, 0.30)), (0.56, (0.70, 0.95, 0.00)), (0.68, (1.00, 0.62, 0.00)),
            (0.82, (1.00, 0.16, 0.01)), (1.00, (0.55, 0.00, 0.01))]


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    return bpy.context.scene


def setup_render(sc, w, h, samples=64, exposure=0.0, look="AgX - Base Contrast", gpu=True):
    sc.render.engine = "CYCLES"
    sc.cycles.samples = samples
    sc.cycles.use_denoising = True
    sc.cycles.max_bounces = 6
    sc.cycles.volume_bounces = 1
    sc.cycles.volume_step_rate = 2.0
    if gpu:
        try:
            prefs = bpy.context.preferences.addons["cycles"].preferences
            prefs.compute_device_type = "METAL"
            prefs.get_devices()
            for d in prefs.devices:
                d.use = True
            sc.cycles.device = "GPU"
        except Exception as e:  # CPU is fine for small previews
            print("GPU setup failed:", e)
    sc.view_settings.view_transform = "AgX"
    try:
        sc.view_settings.look = look
    except Exception:
        pass
    sc.view_settings.exposure = exposure
    sc.render.resolution_x, sc.render.resolution_y = w, h
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = "PNG"
    sc.render.film_transparent = False


def render(sc, path):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    print("RENDERED", path)


def node(nt, kind, loc=None, **inputs):
    n = nt.nodes.new(kind)
    for k, v in inputs.items():
        n.inputs[k].default_value = v
    return n


def ramp(nt, stops, interp="LINEAR"):
    r = nt.nodes.new("ShaderNodeValToRGB")
    r.color_ramp.interpolation = interp
    els = r.color_ramp.elements
    els[0].position, els[0].color = stops[0][0], tuple(stops[0][1])[:3] + (1,)
    els[1].position, els[1].color = stops[-1][0], tuple(stops[-1][1])[:3] + (1,)
    for p, c in stops[1:-1]:
        e = els.new(p)
        e.color = tuple(c)[:3] + (1,)
    return r


def math_node(nt, op, a=None, b=None, clamp=False):
    m = nt.nodes.new("ShaderNodeMath")
    m.operation = op
    m.use_clamp = clamp
    for i, v in enumerate((a, b)):
        if v is None:
            continue
        if isinstance(v, (int, float)):
            m.inputs[i].default_value = v
        else:
            nt.links.new(v, m.inputs[i])
    return m.outputs[0]


def sock(sockets, ident):
    for s_ in sockets:
        if s_.identifier == ident:
            return s_
    raise KeyError(ident)


def mix_rgb(nt, fac, a, b, blend="MIX"):
    """Colour mix; fac/a/b may be sockets or values. Returns the colour output socket."""
    m = nt.nodes.new("ShaderNodeMix")
    m.data_type = "RGBA"
    m.blend_type = blend
    for ident, v in (("Factor_Float", fac), ("A_Color", a), ("B_Color", b)):
        so = sock(m.inputs, ident)
        if isinstance(v, (int, float)):
            so.default_value = v
        elif isinstance(v, tuple):
            so.default_value = v if len(v) == 4 else tuple(v) + (1.0,)
        else:
            nt.links.new(v, so)
    return sock(m.outputs, "Result_Color")


def sky_world(sc, sun_dir, strength=1.0, sun_glow=0.35, palette=None):
    """The art-directed all-round sky: cobalt zenith, haze horizon, a luminous warm cloud sea below.

    Built from the view direction, so it is the same in every orientation of the camera -- the world-space
    'up' cue of 03-art-direction.md section 4."""
    pal = palette or P
    w = bpy.data.worlds.new("rw_sky")
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputWorld")
    bg = nt.nodes.new("ShaderNodeBackground")
    tc = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    nrm = nt.nodes.new("ShaderNodeVectorMath")
    nrm.operation = "NORMALIZE"
    nt.links.new(tc.outputs["Generated"], nrm.inputs[0])
    nt.links.new(nrm.outputs[0], sep.inputs[0])
    # z in -1..1 -> 0..1
    t = math_node(nt, "MULTIPLY_ADD", sep.outputs[2], 0.5)
    t.node.inputs[2].default_value = 0.5
    r = ramp(nt, [(0.0, pal["cloud_deep"]), (0.22, pal["cloud_core"]), (0.42, pal["cloud_sea"]),
                  (0.5, pal["haze"]), (0.58, pal["mid_sky"]), (0.75, pal["upper_sky"]), (1.0, pal["zenith"])])
    nt.links.new(t, r.inputs[0])
    # sun glow
    dot = nt.nodes.new("ShaderNodeVectorMath")
    dot.operation = "DOT_PRODUCT"
    nt.links.new(nrm.outputs[0], dot.inputs[0])
    dot.inputs[1].default_value = tuple(Vector(sun_dir).normalized())
    g = math_node(nt, "MAXIMUM", dot.outputs["Value"], 0.0)
    g = math_node(nt, "POWER", g, 24.0)
    g = math_node(nt, "MULTIPLY", g, sun_glow)
    res = mix_rgb(nt, g, r.outputs[0], pal["sun"], "ADD")
    nt.links.new(res, bg.inputs[0])
    bg.inputs[1].default_value = strength
    nt.links.new(bg.outputs[0], out.inputs[0])
    return w


def sun_light(direction_to_sun, strength=4.0, angle_deg=2.5, color=None):
    d = Vector(direction_to_sun).normalized()
    bpy.ops.object.light_add(type="SUN")
    s = bpy.context.object
    s.name = "sun"
    s.data.energy = strength
    s.data.angle = math.radians(angle_deg)
    s.data.color = tuple((color or P["sun"])[:3])
    # a sun light shines along its local -Z
    s.rotation_mode = "QUATERNION"
    s.rotation_quaternion = (-d).to_track_quat("-Z", "Y")
    return s


def camera(name="cam", lens=24.0, clip_end=20000.0):
    bpy.ops.object.camera_add()
    c = bpy.context.object
    c.name = name
    c.data.lens = lens
    c.data.clip_start = 0.1
    c.data.clip_end = clip_end
    c.rotation_mode = "QUATERNION"
    bpy.context.scene.camera = c
    return c


def aim(cam, eye, target, up=(0, 0, 1)):
    eye, target, up = Vector(eye), Vector(target), Vector(up).normalized()
    f = (target - eye).normalized()
    r = f.cross(up)
    if r.length < 1e-6:
        r = f.orthogonal()
    r.normalize()
    u = r.cross(f).normalized()
    m = Matrix((r, u, -f)).transposed()
    cam.location = eye
    cam.rotation_quaternion = m.to_quaternion()


def fisheye(cam, diag_deg, w, h):
    """Full-frame equisolid fisheye whose image circle is the frame diagonal (the lens of 01 section 3)."""
    cam.data.type = "PANO"
    cam.data.panorama_type = "FISHEYE_EQUISOLID"
    cam.data.fisheye_fov = math.radians(min(diag_deg + 5, 360))
    cam.data.sensor_fit = "HORIZONTAL"
    cam.data.sensor_width = 36.0
    cam.data.fisheye_lens = 18.0 / (2.0 * math.sin(math.radians(diag_deg) / 4.0)) * (math.hypot(w, h) / w)


def rectilinear(cam, lens):
    cam.data.type = "PERSP"
    cam.data.lens = lens


def export_glb(objs, path, apply_modifiers=True):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_yup=True,
                              export_apply=apply_modifiers, export_normals=True, export_materials="EXPORT")
    print("EXPORTED", path)


def to_avgen(v):
    """Blender Z-up position -> AV Gen Y-up (x, z, -y)."""
    return (v[0], v[2], -v[1])


Q_ZUP_TO_YUP = Quaternion((1, 0, 0), math.radians(-90))


def quat_to_avgen(q):
    """A Blender world rotation -> the same rotation expressed in AV Gen's Y-up frame (w, x, y, z)."""
    r = Q_ZUP_TO_YUP @ q @ Q_ZUP_TO_YUP.inverted()
    return (r.w, r.x, r.y, r.z)


def save_blend(path):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(path))
