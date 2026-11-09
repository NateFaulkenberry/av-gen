"""Hero asset H4 (brief 49, Phases 3.1-3.2): the night camp's campfire, as one glb that sits in the existing stone pit.

    Blender -b --factory-startup --python examples/strange-attractor/hero/build_campfire_v1.py -- \
        assets/strange-attractor/polyhaven/models assets/strange-attractor/build/ph-stone_fire_pit.glb \
        assets/strange-attractor/hero/campfire

Writes campfire-v1.glb (and campfire-v1.blend) into the out dir (gitignored, never committed).

It is built in the PIT'S OWN FRAME: place it with the pit's transform (the scene's `campfire-logs` node: same position,
rotation [0, 20, 0], scale 0.85), and it lands on the pit's ash floor. Everything sits on that floor by ray casts
against the pit scan itself.

What it holds:
- logs: Poly Haven's `dead_tree_trunk` (CC0), decimated to about 12k tris a log, five leaned into a collapsing teepee
  (feet on a 0.3 m circle, crossing about 0.6 m up), two fallen across the coals, three sticks of kindling. One 2048 px
  baked atlas for all of them: the bark where they are cold; charred, alligator-cracked black where they are near
  the heart; ash grey on the char's upper faces; and an emissive map of glowing cracks and hot char, orange to yellow
  toward the core;
- the bed: an ash patch conforming to the pit floor (alpha-masked edge, a glowing ember texture at its centre) and
  about 260 coals, a third of them glowing (three heats);
- flames: twelve flame tongues, each three crossed emissive sheets (BLEND, black base colour, the flame in the
  emissive and the alpha) from a four-variant 2048x1024 flame atlas -- a white-hot root, yellow, orange, a deep
  red at the edges and tips, wisps breaking off at the top -- each on its own four-bone chain. The clip "burn" is a
  4 s seamless loop of whole-cycle sines (height pulses, licks, sway, the upper bones moving most), so it is
  deterministic and seek-safe; AV Gen animates skins only and has no flipbook or UV scroll, so the motion is bones.
Sparks and smoke stay the scene's particles (the art agent's), and so does the fire's light.
"""
import math
import os
import random
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Euler, Matrix, Vector
from mathutils.bvhtree import BVHTree

models, pit_path, outdir = sys.argv[sys.argv.index("--") + 1:][:3]
os.makedirs(outdir, exist_ok=True)
rng = random.Random(7)
nrng = np.random.default_rng(7)
LOOP, FPS = 4.0, 24
EMIT_LOGS, EMIT_COALS, EMIT_FLAME = 5.0, 5.0, 12.0

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.render.fps = FPS


# ---------------------------------------------------------------- the pit (for ray casts only; not exported)
bpy.ops.import_scene.gltf(filepath=pit_path)
pit = next(o for o in bpy.data.objects if o.type == "MESH")
bpy.context.view_layer.update()
dg = bpy.context.evaluated_depsgraph_get()
pit_bvh = BVHTree.FromObject(pit, dg)
pit_mw = pit.matrix_world.copy()
pit_inv = pit_mw.inverted()


def floor_at(x, y):
    """The pit's surface height under (x, y) (Blender z-up; the glb's y-up is exported from this)."""
    o = pit_inv @ Vector((x, y, 2.0))
    d = (pit_inv.to_3x3() @ Vector((0, 0, -1))).normalized()
    hit = pit_bvh.ray_cast(o, d)
    if hit[0] is None:
        return 0.0
    return (pit_mw @ hit[0]).z


for o in list(bpy.data.objects):
    o.hide_render = True
pit_objs = list(bpy.data.objects)


# ---------------------------------------------------------------- noise helpers (numpy textures)
def fbm(h, w, octaves=5, base=4, seed=0, tile=True):
    r = np.random.default_rng(seed)
    out = np.zeros((h, w), np.float32)
    amp, tot = 1.0, 0.0
    for k in range(octaves):
        gy, gx = base * 2 ** k, base * 2 ** k * w // h
        g = r.random((gy + 1, gx + 1)).astype(np.float32)
        if tile:
            g[-1, :] = g[0, :]
            g[:, -1] = g[:, 0]
        ys = np.linspace(0, gy, h, endpoint=False)
        xs = np.linspace(0, gx, w, endpoint=False)
        y0, x0 = ys.astype(int), xs.astype(int)
        fy, fx = ys - y0, xs - x0
        fy, fx = fy * fy * (3 - 2 * fy), fx * fx * (3 - 2 * fx)
        a = g[y0][:, x0] * (1 - fx) + g[y0][:, x0 + 1] * fx
        b = g[y0 + 1][:, x0] * (1 - fx) + g[y0 + 1][:, x0 + 1] * fx
        out += amp * (a * (1 - fy)[:, None] + b * fy[:, None])
        tot += amp
        amp *= 0.5
    return out / tot


def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def to_image(name, rgba, srgb=True):
    h, w = rgba.shape[:2]
    im = bpy.data.images.new(name, w, h, alpha=True)
    if not srgb:
        im.colorspace_settings.name = "Non-Color"
    im.pixels.foreach_set(np.ascontiguousarray(rgba[::-1]).ravel())   # numpy row 0 = top
    im.pack()
    return im


# ---------------------------------------------------------------- logs
d = os.path.join(models, "dead_tree_trunk")
bpy.ops.import_scene.gltf(filepath=os.path.join(d, [f for f in os.listdir(d) if f.endswith(".gltf")][0]))
src = next(o for o in bpy.data.objects if o.type == "MESH" and o not in pit_objs)
src.rotation_mode = "XYZ"
bpy.context.view_layer.objects.active = src
for o in bpy.context.view_layer.objects:
    o.select_set(o is src)
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
dec = src.modifiers.new("dec", "DECIMATE")
dec.ratio = 0.12
bpy.ops.object.modifier_apply(modifier="dec")
vs = src.data.vertices
lo = Vector((min(v.co.x for v in vs), min(v.co.y for v in vs), min(v.co.z for v in vs)))
hi = Vector((max(v.co.x for v in vs), max(v.co.y for v in vs), max(v.co.z for v in vs)))
c = (lo + hi) / 2
for v in vs:          # centre; the trunk's length along x, normalised to 1 m
    v.co = (v.co - c) * (1.0 / (hi.x - lo.x))
cross = max(hi.y - lo.y, hi.z - lo.z) / (hi.x - lo.x)   # the trunk's girth per metre of length


def place_log(length, girth, foot, yaw, pitch, roll):
    """A log along +x scaled to length and girth, its foot end at `foot`, rising at `pitch` toward yaw."""
    o = src.copy()
    o.data = src.data.copy()
    scene.collection.objects.link(o)
    o.rotation_mode = "XYZ"      # the importer leaves QUATERNION, and then rotation_euler is ignored (H1 §0.1)
    o.scale = (length, girth / cross, girth / cross)
    o.rotation_euler = Euler((roll, -pitch, yaw), "XYZ")
    axis = Vector((math.cos(yaw) * math.cos(pitch), math.sin(yaw) * math.cos(pitch), math.sin(pitch)))
    o.location = foot + axis * length * 0.5
    return o


logs = []
for k in range(5):
    yaw_out = k * 2 * math.pi / 5 + rng.uniform(-0.25, 0.25)
    foot = Vector((0.31 * math.cos(yaw_out), 0.31 * math.sin(yaw_out), 0.0))
    foot.z = floor_at(foot.x, foot.y) + 0.02
    L = rng.uniform(0.66, 0.82)
    pitch = math.radians(rng.uniform(52, 64))
    logs.append(place_log(L, rng.uniform(0.085, 0.11), foot, yaw_out + math.pi + rng.uniform(-0.12, 0.12), pitch,
                          rng.uniform(0, 6.28)))
for k, (ang, L) in enumerate(((0.9, 0.7), (3.6, 0.62))):          # two fallen across the coals, burned through
    foot = Vector((0.33 * math.cos(ang), 0.33 * math.sin(ang), 0.0))
    foot.z = floor_at(foot.x, foot.y) + 0.035
    logs.append(place_log(L, rng.uniform(0.08, 0.1), foot, ang + math.pi + 0.5, math.radians(4 + 3 * k),
                          rng.uniform(0, 6.28)))
for k in range(3):                                                # kindling
    ang = rng.uniform(0, 6.28)
    foot = Vector((0.16 * math.cos(ang), 0.16 * math.sin(ang), 0.0))
    foot.z = floor_at(foot.x, foot.y) + 0.02
    logs.append(place_log(rng.uniform(0.3, 0.4), 0.035, foot, ang + math.pi + 0.3, math.radians(rng.uniform(25, 45)),
                          rng.uniform(0, 6.28)))
bpy.data.objects.remove(src, do_unlink=True)
bpy.context.view_layer.update()
for o in list(scene.objects):
    o.select_set(o in logs)
bpy.context.view_layer.objects.active = logs[0]
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
bpy.ops.object.join()
log_obj = bpy.context.active_object
log_obj.name = "campfire_logs"
me = log_obj.data
src_uv = me.uv_layers[0].name
bake_uv = me.uv_layers.new(name="bake")
me.uv_layers.active = bake_uv
bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.uv.smart_project(angle_limit=math.radians(60), island_margin=0.003)
bpy.ops.object.mode_set(mode="OBJECT")

# the bake shader: bark (its own UVs) -> char -> cracks -> glow, from world position (the logs are in place)
mat = me.materials[0]
nt = mat.node_tree
for n in list(nt.nodes):
    if n.type not in ("OUTPUT_MATERIAL",):
        nt.nodes.remove(n)
N = nt.nodes
L_ = nt.links.new
out = next(n for n in N if n.type == "OUTPUT_MATERIAL")
bsdf = N.new("ShaderNodeBsdfPrincipled")
L_(bsdf.outputs[0], out.inputs["Surface"])
uvn = N.new("ShaderNodeUVMap"); uvn.uv_map = src_uv
tex = {}
for key in ("diff", "nor_gl", "arm"):
    t = N.new("ShaderNodeTexImage")
    t.image = bpy.data.images.load(os.path.join(d, "textures", f"dead_tree_trunk_{key}_1k.jpg"))
    if key != "diff":
        t.image.colorspace_settings.name = "Non-Color"
    L_(uvn.outputs["UV"], t.inputs["Vector"])
    tex[key] = t
geo = N.new("ShaderNodeNewGeometry")


def math_node(op, a=None, b=None, v0=None, v1=None, clamp=False):
    m = N.new("ShaderNodeMath"); m.operation = op; m.use_clamp = clamp
    for i, (s, v) in enumerate(((a, v0), (b, v1))):
        if s is not None:
            L_(s, m.inputs[i])
        elif v is not None:
            m.inputs[i].default_value = v
    return m.outputs[0]


def map_range(s, a, b, c_=0.0, d_=1.0, smoothstep=True):
    m = N.new("ShaderNodeMapRange")
    m.interpolation_type = "SMOOTHSTEP" if smoothstep else "LINEAR"
    m.inputs["From Min"].default_value, m.inputs["From Max"].default_value = a, b
    m.inputs["To Min"].default_value, m.inputs["To Max"].default_value = c_, d_
    L_(s, m.inputs["Value"])
    return m.outputs[0]


heart = N.new("ShaderNodeCombineXYZ"); heart.inputs[2].default_value = 0.14
sub = N.new("ShaderNodeVectorMath"); sub.operation = "DISTANCE"
L_(geo.outputs["Position"], sub.inputs[0]); L_(heart.outputs[0], sub.inputs[1])
dist = sub.outputs["Value"]
nz = N.new("ShaderNodeTexNoise"); nz.inputs["Scale"].default_value = 7.0; nz.inputs["Detail"].default_value = 4.0
L_(geo.outputs["Position"], nz.inputs["Vector"])
dn = math_node("MULTIPLY_ADD", nz.outputs["Fac"], None, None, 0.22)
dn.node.inputs[2].default_value = -0.11
dd = math_node("ADD", dist, dn)
char = map_range(dd, 0.42, 0.24)                                  # 1 near the heart, 0 out at the logs' feet
heat = map_range(dd, 0.24, 0.06)                                  # the glowing core
# alligator cracks: Voronoi cell edges on a noise-warped position, so the cells are irregular, not a giraffe print
warp = N.new("ShaderNodeTexNoise"); warp.inputs["Scale"].default_value = 18.0
L_(geo.outputs["Position"], warp.inputs["Vector"])
wv = N.new("ShaderNodeVectorMath"); wv.operation = "MULTIPLY_ADD"
L_(warp.outputs["Color"], wv.inputs[0]); wv.inputs[1].default_value = (0.035, 0.035, 0.035)
L_(geo.outputs["Position"], wv.inputs[2])
vor = N.new("ShaderNodeTexVoronoi"); vor.feature = "DISTANCE_TO_EDGE"; vor.inputs["Scale"].default_value = 42.0
vor.inputs["Randomness"].default_value = 1.0
L_(wv.outputs["Vector"], vor.inputs["Vector"])
crack = map_range(vor.outputs["Distance"], 0.045, 0.0)           # thin crack lines
# albedo: bark -> char black, ash grey on the char's upward faces
sep = N.new("ShaderNodeSeparateXYZ"); L_(geo.outputs["Normal"], sep.inputs[0])
nz2 = N.new("ShaderNodeTexNoise"); nz2.inputs["Scale"].default_value = 22.0
L_(geo.outputs["Position"], nz2.inputs["Vector"])
ash = math_node("MULTIPLY", map_range(sep.outputs["Z"], 0.2, 0.8), map_range(nz2.outputs["Fac"], 0.45, 0.7))
ash = math_node("MULTIPLY", ash, char)
charcol = N.new("ShaderNodeMix"); charcol.data_type = "RGBA"
charcol.inputs["A"].default_value = (0.022, 0.019, 0.017, 1)
charcol.inputs["B"].default_value = (0.012, 0.01, 0.009, 1)
L_(crack, charcol.inputs["Factor"])
withash = N.new("ShaderNodeMix"); withash.data_type = "RGBA"
L_(ash, withash.inputs["Factor"]); L_(charcol.outputs["Result"], withash.inputs["A"])
withash.inputs["B"].default_value = (0.32, 0.3, 0.28, 1)
alb = N.new("ShaderNodeMix"); alb.data_type = "RGBA"
L_(char, alb.inputs["Factor"]); L_(tex["diff"].outputs["Color"], alb.inputs["A"]); L_(withash.outputs["Result"], alb.inputs["B"])
L_(alb.outputs["Result"], bsdf.inputs["Base Color"])
# roughness: the bark's, char drier
sepr = N.new("ShaderNodeSeparateColor"); L_(tex["arm"].outputs["Color"], sepr.inputs[0])
rmix = N.new("ShaderNodeMix"); rmix.data_type = "FLOAT"
L_(char, rmix.inputs["Factor"]); L_(sepr.outputs["Green"], rmix.inputs["A"]); rmix.inputs["B"].default_value = 0.93
L_(rmix.outputs["Result"], bsdf.inputs["Roughness"])
# normal: the bark's map, with the char's cracks bumped in
nm = N.new("ShaderNodeNormalMap"); nm.uv_map = src_uv
L_(tex["nor_gl"].outputs["Color"], nm.inputs["Color"])
bump = N.new("ShaderNodeBump"); bump.inputs["Distance"].default_value = 0.004
L_(math_node("MULTIPLY", crack, char, None, None), bump.inputs["Height"])
bump.invert = True
L_(nm.outputs["Normal"], bump.inputs["Normal"])
L_(bump.outputs["Normal"], bsdf.inputs["Normal"])
# emission: the cracks glow where it is hot, the char itself smoulders faintly at the core
nz3 = N.new("ShaderNodeTexNoise"); nz3.inputs["Scale"].default_value = 11.0
L_(geo.outputs["Position"], nz3.inputs["Vector"])
glow = math_node("ADD", math_node("MULTIPLY", crack, heat), math_node("MULTIPLY", heat, None, None, 0.12))
glow = math_node("MULTIPLY", glow, map_range(nz3.outputs["Fac"], 0.32, 0.6, 0.0, 1.0), clamp=True)   # only some cracks live
ramp = N.new("ShaderNodeValToRGB")
ramp.color_ramp.elements[0].color = (0, 0, 0, 1)
ramp.color_ramp.elements[1].position = 1.0
ramp.color_ramp.elements[1].color = (1.0, 0.78, 0.36, 1)
e = ramp.color_ramp.elements.new(0.35); e.color = (0.85, 0.16, 0.02, 1)
e = ramp.color_ramp.elements.new(0.7); e.color = (1.0, 0.45, 0.08, 1)
L_(glow, ramp.inputs["Fac"])
L_(ramp.outputs["Color"], bsdf.inputs["Emission Color"])
bsdf.inputs["Emission Strength"].default_value = 1.0

scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = 4
baked = {}
for kind, cs in (("DIFFUSE", "sRGB"), ("ROUGHNESS", "Non-Color"), ("NORMAL", "Non-Color"), ("EMIT", "sRGB")):
    im = bpy.data.images.new(f"campfire_logs_{kind.lower()}", 2048, 2048)
    im.colorspace_settings.name = cs
    tn = N.new("ShaderNodeTexImage"); tn.image = im
    for n in N:
        n.select = False
    tn.select = True
    N.active = tn
    for o in list(scene.objects):
        o.select_set(o is log_obj)
    bpy.context.view_layer.objects.active = log_obj
    kw = dict(margin=6)
    if kind == "DIFFUSE":
        kw["pass_filter"] = {"COLOR"}
    if kind == "NORMAL":
        kw["normal_space"] = "TANGENT"
    bpy.ops.object.bake(type=kind, **kw)
    im.pack()
    baked[kind] = im
    N.remove(tn)
# the shipped material: plain glTF maps on the bake UVs
for n in list(N):
    if n.type != "OUTPUT_MATERIAL":
        N.remove(n)
bsdf = N.new("ShaderNodeBsdfPrincipled"); L_(bsdf.outputs[0], out.inputs["Surface"])
for kind, inp in (("DIFFUSE", "Base Color"), ("ROUGHNESS", "Roughness"), ("EMIT", "Emission Color")):
    t = N.new("ShaderNodeTexImage"); t.image = baked[kind]
    L_(t.outputs["Color"], bsdf.inputs[inp])
t = N.new("ShaderNodeTexImage"); t.image = baked["NORMAL"]
nm = N.new("ShaderNodeNormalMap"); L_(t.outputs["Color"], nm.inputs["Color"]); L_(nm.outputs["Normal"], bsdf.inputs["Normal"])
bsdf.inputs["Emission Strength"].default_value = EMIT_LOGS
mat.name = "campfire_logs"
me.uv_layers.remove(me.uv_layers[src_uv])

# ---------------------------------------------------------------- the bed: ash patch and coals
S = 1024
yy, xx = np.mgrid[0:S, 0:S].astype(np.float32)
u, v = xx / S * 2 - 1, yy / S * 2 - 1
r = np.hypot(u, v)
n1, n2 = fbm(S, S, 6, 4, seed=11), fbm(S, S, 5, 16, seed=12)
alpha = smooth(1.0, 0.72, r + (n1 - 0.5) * 0.35)
greyv = 0.22 + 0.25 * n2 + 0.12 * smooth(0.5, 0.0, r)
ash_rgb = np.stack([greyv * 1.0, greyv * 0.96, greyv * 0.92], -1)
ember = smooth(0.62, 0.8, n2) * smooth(0.55, 0.1, r + (n1 - 0.5) * 0.3)
ash_rgb = ash_rgb * (1 - 0.8 * smooth(0.0, 0.5, ember))[..., None]       # embers are dark under their glow
emit = np.stack([ember * 1.0, ember * 0.36, ember * 0.06], -1)
ash_img = to_image("campfire_ash", np.concatenate([ash_rgb, alpha[..., None]], -1))
ash_emit = to_image("campfire_ash_glow", np.concatenate([emit, np.ones_like(alpha)[..., None]], -1))
bm = bmesh.new()
R_ASH = 0.42
bmesh.ops.create_circle(bm, cap_ends=True, cap_tris=False, segments=64, radius=R_ASH)
bmesh.ops.subdivide_edges(bm, edges=bm.edges[:], cuts=6, use_grid_fill=True)
bmesh.ops.triangulate(bm, faces=bm.faces[:])
uvl = bm.loops.layers.uv.new()
for f in bm.faces:
    for lp in f.loops:
        lp[uvl].uv = (lp.vert.co.x / (2 * R_ASH) + 0.5, lp.vert.co.y / (2 * R_ASH) + 0.5)
for vtx in bm.verts:
    vtx.co.z = floor_at(vtx.co.x, vtx.co.y) + 0.006
ash_me = bpy.data.meshes.new("campfire_ash")
bm.to_mesh(ash_me)
bm.free()
ash_obj = bpy.data.objects.new("campfire_ash", ash_me)
scene.collection.objects.link(ash_obj)
am = bpy.data.materials.new("campfire_ash")
am.use_nodes = True
b = am.node_tree.nodes["Principled BSDF"]
t = am.node_tree.nodes.new("ShaderNodeTexImage"); t.image = ash_img
am.node_tree.links.new(t.outputs["Color"], b.inputs["Base Color"])
am.node_tree.links.new(t.outputs["Alpha"], b.inputs["Alpha"])
t2 = am.node_tree.nodes.new("ShaderNodeTexImage"); t2.image = ash_emit
am.node_tree.links.new(t2.outputs["Color"], b.inputs["Emission Color"])
b.inputs["Emission Strength"].default_value = EMIT_COALS
b.inputs["Roughness"].default_value = 0.97
ash_me.materials.append(am)

coal_mats = []
for name, col, em in (("campfire_coal_dark", (0.02, 0.018, 0.016), None),
                      ("campfire_coal_warm", (0.03, 0.02, 0.015), (0.55, 0.12, 0.02)),
                      ("campfire_coal_hot", (0.04, 0.02, 0.01), (1.0, 0.32, 0.05)),
                      ("campfire_coal_white", (0.05, 0.03, 0.02), (1.0, 0.62, 0.22))):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = col + (1,)
    b.inputs["Roughness"].default_value = 0.9
    if em:
        b.inputs["Emission Color"].default_value = em + (1,)
        b.inputs["Emission Strength"].default_value = EMIT_COALS
    coal_mats.append(m)
bm = bmesh.new()
for k in range(260):
    rr = 0.33 * math.sqrt(rng.random())
    a = rng.uniform(0, 6.28)
    x, y = rr * math.cos(a), rr * math.sin(a)
    s = rng.uniform(0.012, 0.03) * (1.3 - rr)
    ico = bmesh.ops.create_icosphere(bm, subdivisions=1, radius=1.0)
    m_ = Matrix.Translation((x, y, floor_at(x, y) + s * 0.35)) @ Euler(
        (rng.uniform(0, 6.3), rng.uniform(0, 6.3), rng.uniform(0, 6.3))).to_matrix().to_4x4() @ Matrix.Diagonal(
        (s * rng.uniform(0.8, 1.6), s * rng.uniform(0.7, 1.2), s * rng.uniform(0.45, 0.8), 1))
    bmesh.ops.transform(bm, matrix=m_, verts=ico["verts"])
    hot = rr < 0.22 and rng.random() < 0.75
    idx = (3 if rr < 0.1 and rng.random() < 0.5 else 2 if rng.random() < 0.6 else 1) if hot else (0 if rng.random() < 0.8 else 1)
    for f in {f for vtx in ico["verts"] for f in vtx.link_faces}:
        f.material_index = idx
        f.smooth = False
coal_me = bpy.data.meshes.new("campfire_coals")
bm.to_mesh(coal_me)
bm.free()
coal_obj = bpy.data.objects.new("campfire_coals", coal_me)
scene.collection.objects.link(coal_obj)
for m in coal_mats:
    coal_me.materials.append(m)

# ---------------------------------------------------------------- flames
FW, FH, VAR = 512, 1024, 4
atlas = np.zeros((FH, FW * VAR, 4), np.float32)
vv = (1.0 - (np.arange(FH, dtype=np.float32) + 0.5) / FH)[:, None]    # 0 at the bottom row (numpy row FH-1)
uu = ((np.arange(FW, dtype=np.float32) + 0.5) / FW * 2 - 1)[None, :]
ramp_pos = np.array([0.0, 0.25, 0.5, 0.75, 1.0], np.float32)
ramp_col = np.array([[0.75, 0.16, 0.01], [1.0, 0.36, 0.04], [1.0, 0.58, 0.14], [1.0, 0.76, 0.32], [1.0, 0.9, 0.62]],
                    np.float32)    # saturated toward orange: a bright red-white tips pink through the tone map
for k in range(VAR):
    turb = fbm(FH, FW, 5, 3, seed=40 + k, tile=False)
    turb2 = fbm(FH, FW, 4, 6, seed=60 + k, tile=False)
    ph = [nrng.uniform(0, 6.28) for _ in range(3)]
    centre = (0.22 * np.sin(2 * np.pi * 1.1 * vv + ph[0]) + 0.1 * np.sin(2 * np.pi * 2.7 * vv + ph[1])) * vv ** 1.3
    width = 0.78 * np.sin(np.pi * np.clip(vv * 1.25, 0, 1)) ** 0.55 * (1 - vv) ** 0.4 + 0.02
    uprime = uu - centre + (turb - 0.5) * 0.55 * vv
    edge = np.abs(uprime) / width
    mask = smooth(1.0, 0.55, edge)
    mask *= smooth(0.25, 0.55, turb2 + (1 - vv) * 0.8)                   # wisps break away toward the top
    mask *= smooth(0.0, 0.07, vv)                                         # a soft root into the coals
    temp = np.clip((1 - edge) * (1 - vv) ** 0.6 * 1.25 + (turb - 0.5) * 0.2, 0, 1)
    rgb = np.stack([np.interp(temp, ramp_pos, ramp_col[:, i]) for i in range(3)], -1)
    atlas[:, k * FW:(k + 1) * FW, :3] = rgb
    atlas[:, k * FW:(k + 1) * FW, 3] = np.clip(mask, 0, 1) ** 0.85
flame_img = to_image("campfire_flame_atlas", atlas)
fm = bpy.data.materials.new("campfire_flame")
fm.use_nodes = True
b = fm.node_tree.nodes["Principled BSDF"]
t = fm.node_tree.nodes.new("ShaderNodeTexImage"); t.image = flame_img
fm.node_tree.links.new(t.outputs["Color"], b.inputs["Emission Color"])
fm.node_tree.links.new(t.outputs["Alpha"], b.inputs["Alpha"])
b.inputs["Base Color"].default_value = (0, 0, 0, 1)
b.inputs["Emission Strength"].default_value = EMIT_FLAME
b.inputs["Roughness"].default_value = 1.0
try:
    fm.blend_method = "BLEND"
except Exception:
    pass

arm = bpy.data.objects.new("campfire_rig", bpy.data.armatures.new("campfire_rig"))
scene.collection.objects.link(arm)
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode="EDIT")
tongues = []
base_z = floor_at(0, 0) + 0.05
bone_root = arm.data.edit_bones.new("root")
bone_root.head, bone_root.tail = (0, 0, 0), (0, 0, 0.05)
for i in range(12):
    if i < 3:
        rr, h, w = rng.uniform(0.0, 0.05), rng.uniform(0.9, 1.08), rng.uniform(0.44, 0.54)
    else:
        rr, h, w = rng.uniform(0.07, 0.19), rng.uniform(0.42, 0.78), rng.uniform(0.18, 0.3)
    a = rng.uniform(0, 6.28)
    p = Vector((rr * math.cos(a), rr * math.sin(a), floor_at(rr * math.cos(a), rr * math.sin(a)) + 0.04))
    names = []
    for s_ in range(4):
        bn = arm.data.edit_bones.new(f"t{i:02d}_{s_}")
        bn.head = p + Vector((0, 0, h * s_ / 4))
        bn.tail = p + Vector((0, 0, h * (s_ + 1) / 4))
        bn.parent = arm.data.edit_bones[names[-1]] if names else bone_root
        bn.use_connect = bool(names)
        names.append(bn.name)
    tongues.append(dict(p=p, h=h, w=w, names=names, var=i % VAR, spin=rng.uniform(0, 3.14), phase=rng.uniform(0, 6.28),
                        lean=Vector((-p.x, -p.y, 0)) * 0.6))
bpy.ops.object.mode_set(mode="OBJECT")
bm = bmesh.new()
uvl = bm.loops.layers.uv.new()
dl = bm.verts.layers.deform.verify()
groups = {}
fl_obj = bpy.data.objects.new("campfire_flames", bpy.data.meshes.new("campfire_flames"))
scene.collection.objects.link(fl_obj)
for tg in tongues:
    for nm_ in tg["names"]:
        groups[nm_] = fl_obj.vertex_groups.new(name=nm_).index
ROWS = 12
for tg in tongues:
    for pl in range(3):
        ang = tg["spin"] + pl * math.pi / 3
        side = Vector((math.cos(ang), math.sin(ang), 0)) * tg["w"] * 0.5
        grid = []
        for rw in range(ROWS + 1):
            f = rw / ROWS
            row = []
            for sgn in (-1, 1):
                vtx = bm.verts.new(tg["p"] + side * sgn + Vector((0, 0, tg["h"] * f)))
                seg = min(3.0, f * 4.0 - 0.5)
                b0 = max(0, int(math.floor(seg)))
                wgt = min(1.0, max(0.0, seg - b0))
                vtx[dl][groups[tg["names"][b0]]] = 1.0 - wgt if b0 < 3 else 1.0
                if b0 < 3 and wgt > 0:
                    vtx[dl][groups[tg["names"][b0 + 1]]] = wgt
                row.append(vtx)
            grid.append(row)
        u0 = tg["var"] / VAR
        for rw in range(ROWS):
            for side_flip in (False, True):     # explicit back faces (H1 G1), one material, not doubleSided
                q = (grid[rw][0], grid[rw][1], grid[rw + 1][1], grid[rw + 1][0])
                uvs = ((u0, rw / ROWS), (u0 + 1 / VAR, rw / ROWS), (u0 + 1 / VAR, (rw + 1) / ROWS), (u0, (rw + 1) / ROWS))
                if side_flip:
                    q = tuple(bm.verts.new(x.co) for x in q)
                    for x, src_v in zip(q, (grid[rw][0], grid[rw][1], grid[rw + 1][1], grid[rw + 1][0])):
                        for gk, gv in src_v[dl].items():
                            x[dl][gk] = gv
                    q, uvs = q[::-1], uvs[::-1]
                face = bm.faces.new(q)
                for lp, uvv in zip(face.loops, uvs):
                    lp[uvl].uv = uvv
bm.to_mesh(fl_obj.data)
bm.free()
fl_obj.data.materials.append(fm)
fl_obj.parent = arm
fl_obj.modifiers.new("Armature", "ARMATURE").object = arm

# the clip: whole cycles over LOOP seconds, so frame 0 == frame LOOP*FPS
bpy.context.view_layer.objects.active = arm
bpy.ops.object.mode_set(mode="POSE")
pb = arm.pose.bones
for b_ in pb:
    b_.rotation_mode = "XYZ"
frames = int(LOOP * FPS)
scene.frame_start, scene.frame_end = 0, frames


def wv(t, cyc, ph):
    return math.sin(2 * math.pi * cyc * t / LOOP + ph)


for fr in range(0, frames + 1, 2):
    t = fr / FPS
    for i, tg in enumerate(tongues):
        ph = tg["phase"]
        rootb = pb[tg["names"][0]]
        lick = max(0.0, wv(t, 3 + i % 3, ph)) ** 2               # a lick: the tongue jumps up and thins
        sy = 1.0 + 0.16 * wv(t, 5, ph + 1.3) + 0.1 * wv(t, 11, ph * 1.7) + 0.32 * lick
        sx = 1.0 - 0.12 * lick + 0.06 * wv(t, 7, ph + 0.4)
        rootb.scale = (sx, sy, sx)
        for s_ in (1, 2, 3):
            b_ = pb[tg["names"][s_]]
            amp = math.radians(4.0 + 5.0 * s_)
            b_.rotation_euler = (amp * (0.6 * wv(t, 7 + s_, ph + s_) + 0.4 * wv(t, 13, ph * 2 + s_)) + tg["lean"].y * 0.8,
                                 0.0,
                                 amp * (0.6 * wv(t, 9 - s_, ph + 2 * s_) + 0.4 * wv(t, 17, ph + s_)) - tg["lean"].x * 0.8)
            b_.scale = (1.0 - 0.18 * lick * s_ / 3, 1.0 + 0.1 * wv(t, 9, ph + s_), 1.0 - 0.18 * lick * s_ / 3)
            b_.keyframe_insert("rotation_euler", frame=fr)
            b_.keyframe_insert("scale", frame=fr)
        rootb.keyframe_insert("scale", frame=fr)
bpy.ops.object.mode_set(mode="OBJECT")
arm.animation_data.action.name = "burn"

# ---------------------------------------------------------------- export
for o in pit_objs:
    bpy.data.objects.remove(o)
for o in bpy.data.objects:
    o.hide_render = False
blend = os.path.join(outdir, "campfire-v1.blend")
glb = os.path.join(outdir, "campfire-v1.glb")
bpy.ops.wm.save_as_mainfile(filepath=blend, compress=True)
bpy.ops.object.select_all(action="SELECT")
bpy.ops.export_scene.gltf(filepath=glb, export_animations=True, export_animation_mode="ACTIONS",
                          export_image_format="AUTO")

# post: the flames black-based BLEND emissive; the ash MASK; the logs/coals single-sided
import json, struct  # noqa: E401
bb = open(glb, "rb").read()
n = struct.unpack("<I", bb[12:16])[0]
js = json.loads(bb[20:20 + n])
rest = bb[20 + n:]
for m in js.get("materials", []):
    if m["name"] == "campfire_flame":
        m["alphaMode"] = "BLEND"
        m.setdefault("pbrMetallicRoughness", {})["baseColorFactor"] = [0, 0, 0, 1]
        m["pbrMetallicRoughness"]["metallicFactor"] = 0.0
        m["doubleSided"] = False
        # the alpha lives in the emissive atlas; glTF reads alpha from baseColorTexture, so point it there too
        m["pbrMetallicRoughness"]["baseColorTexture"] = dict(m["emissiveTexture"])
    if m["name"] == "campfire_ash":
        m["alphaMode"] = "MASK"
        m["alphaCutoff"] = 0.5
enc = json.dumps(js, separators=(",", ":")).encode()
enc += b" " * ((-len(enc)) % 4)
open(glb, "wb").write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(enc) + len(rest)) +
                      struct.pack("<II", len(enc), 0x4E4F534A) + enc + rest)
tris = {o.name: sum(len(p.vertices) - 2 for p in o.data.polygons) for o in bpy.data.objects if o.type == "MESH"}
print("CAMPFIRE", glb, tris, "flame tongues", len(tongues), "clip burn", LOOP, "s")
