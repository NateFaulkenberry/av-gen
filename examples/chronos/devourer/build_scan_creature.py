"""CHRONOS, The Devourer (Gate D1): the creature from a sculptor's scan -- the scan route.

The base is Miquel Blay's study of an old man (photogrammetry of the plaster, CC BY 4.0, albert_sierra on Sketchfab;
docs/prototypes/chronos/assets.md). It already has what the MPFB route could not produce: an artist's exaggeration of
age, and the hair and beard as one sculpted, matted MASS, which is how Goya paints Saturn's. This script turns the bust
into a rigged creature:

  1. normalise (1 unit = the bust's height, the face toward -Y), cut away the pedestal;
  2. push it past human: deeper eye sockets, hollowed cheeks, a lengthened, hanging lower face;
  3. cut the mouth open along the lip line under the moustache, so the jaw can drop 60+ degrees and the beard swings
     with it like a maw;
  4. a small rig of its own -- root, neck, head, jaw, eye.L, eye.R -- with weights from landmark distances (the jaw
     region blends into the cheeks so they stretch rather than tear);
  5. bulging eyeballs in the carved sockets: dirty white, tiny dark pupils, raw rims (Goya's stare, r02);
  6. a dark throat cavity behind the mouth, and irregular, long, yellowed and worn teeth along both lips;
  7. the skin painted, not photographed: the scan's own cavities (baked ambient occlusion) drive an umber-to-ochre
     ramp, mottled with a painterly noise, over a faint trace of the plaster colour;
  8. the same looping 12 s life clip as the MPFB route (`idle`): breathing jaw with one slow 64-degree hunger opening,
     eye saccades, head drift;
  9. glTF binary export, and Blender stills under the chiaroscuro light.

    /Applications/Blender.app/Contents/MacOS/Blender --background --python examples/chronos/devourer/build_scan_creature.py \
        -- <blay.glb> <out.glb> [<stills_dir>]

Landmarks (eyes, mouth line, hinge) were read from orthographic grid renders made by landmarks.py. The outputs are
CC BY-derived and stay out of the repository (assets/chronos/ is gitignored); this script is original code.
"""
import math
import os
import random
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Euler, Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:]
SCAN, OUT = argv[0], argv[1]
STILLS = argv[2] if len(argv) > 2 else ""
os.makedirs(os.path.dirname(OUT) or ".", exist_ok=True)
# absolute: the .blend is saved beside the GLB, and a relative image path then resolves against the .blend's folder
# and goes missing (Blender's magenta), which is what the first build rendered
TEX = os.path.abspath(os.path.join(os.path.dirname(OUT), "chronos-scan-textures"))
os.makedirs(TEX, exist_ok=True)
rng = random.Random(1934)

# ---- landmarks, in the normalised frame (landmarks.py) -----------------------------------------------------------
EYE_R = Vector((-0.093, -0.200, 0.698))   # the subject's right (image left in the front view)
EYE_L = Vector((0.046, -0.205, 0.700))
MOUTH = Vector((-0.024, -0.245, 0.536))   # centre of the lip line, under the moustache
MOUTH_HALF = 0.052                        # half the mouth's width
HINGE = Vector((-0.024, -0.060, 0.600))   # the jaw's hinge, in front of the ears
CHIN = Vector((-0.024, -0.235, 0.405))    # the beard's point
NECK_CUT = 0.335                          # below this is pedestal
EYE_RADIUS = 0.0195


def smooth(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


def load_and_normalise():
    bpy.ops.wm.read_homefile(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=SCAN)
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    for o in list(bpy.context.scene.objects):
        if o.type == "EMPTY":
            bpy.data.objects.remove(o)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    co = np.empty(len(obj.data.vertices) * 3)
    obj.data.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    lo, hi = co.min(0), co.max(0)
    s = 1.0 / (hi[2] - lo[2])
    co = np.stack([(co[:, 0] - (lo[0] + hi[0]) / 2) * s, (co[:, 1] - (lo[1] + hi[1]) / 2) * s, (co[:, 2] - lo[2]) * s], 1)
    obj.data.vertices.foreach_set("co", co.ravel())
    obj.data.update()
    obj.name = "chronos-head"
    return obj


def cut_pedestal_and_mouth(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    # the pedestal
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if v.co.z < NECK_CUT], context="VERTS")
    # the mouth: a slit of faces through the front of the face along the lip line, wider at the centre, closing at
    # the corners; only the front surface (the scan is a shell)
    kill = []
    for f in bm.faces:
        c = f.calc_center_median()
        dx = (c.x - MOUTH.x) / MOUTH_HALF
        if abs(dx) > 1.0 or c.y > MOUTH.y + 0.09:
            continue
        half = 0.0065 * (1.0 - dx * dx) + 0.0015
        if abs(c.z - (MOUTH.z + 0.004 * dx * dx)) < half:
            kill.append(f)
    bmesh.ops.delete(bm, geom=kill, context="FACES")
    bm.to_mesh(obj.data)
    bm.free()
    obj.data.update()


def push_past_human(obj):
    me = obj.data
    me.update()
    eyes = (EYE_L + EYE_R) * 0.5
    for v in me.vertices:
        p = v.co.copy()
        n = v.normal
        d = Vector()
        # the sockets sink around the eyeballs that will sit in them
        for e in (EYE_L, EYE_R):
            r = (p - e).length / 0.045
            d += Vector((0, 1, 0)) * 0.006 * smooth(1.0, 0.0, r)
        # cheek hollows below the cheekbones, at the sides of the nose: pushed toward the head's own vertical axis,
        # not along the scan's vertex normals (noisy on photogrammetry: the first build folded the moustache into
        # crumpled shards that way)
        for side in (-1, 1):
            c = Vector((eyes.x + side * 0.075, -0.18, 0.605))
            r = (p - c).length / 0.055
            inward = Vector((MOUTH.x - p.x, -p.y, 0.0))
            if inward.length > 1e-6 and p.y < -0.1:
                d += inward.normalized() * 0.008 * smooth(1.0, 0.0, r)
        # the lower face lengthens and hangs below the mouth (the jaw will drop further still)
        below = smooth(MOUTH.z, MOUTH.z - 0.15, p.z) * smooth(0.2, 0.05, abs(p.x - MOUTH.x)) * smooth(0.05, -0.12, p.y)
        d += Vector((0, -0.25, -1)).normalized() * 0.03 * below
        v.co = p + d
    me.update()


def build_rig(obj):
    arm = bpy.data.armatures.new("chronos-rig")
    rig = bpy.data.objects.new("chronos-rig", arm)
    bpy.context.scene.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm.edit_bones

    def bone(name, head, tail, parent=None):
        b = eb.new(name)
        b.head, b.tail = head, tail
        b.roll = 0.0
        if parent:
            b.parent = eb[parent]
        return b
    bone("root", Vector((MOUTH.x, 0.0, NECK_CUT)), Vector((MOUTH.x, 0.0, NECK_CUT + 0.05)))
    bone("neck", Vector((MOUTH.x, 0.0, 0.40)), Vector((MOUTH.x, -0.01, 0.52)), "root")
    bone("head", Vector((MOUTH.x, -0.01, 0.52)), Vector((MOUTH.x, -0.02, 0.98)), "neck")
    bone("jaw", HINGE, CHIN, "head")
    bone("eye.L", EYE_L, EYE_L + Vector((0, -0.04, 0)), "head")
    bone("eye.R", EYE_R, EYE_R + Vector((0, -0.04, 0)), "head")
    bpy.ops.object.mode_set(mode="OBJECT")
    # weights
    for name in ("root", "neck", "head", "jaw"):
        obj.vertex_groups.new(name=name)
    gj, gh, gn = obj.vertex_groups["jaw"], obj.vertex_groups["head"], obj.vertex_groups["neck"]
    for v in obj.data.vertices:
        p = v.co
        # the jaw: below the lip line, across the mouth's width and the beard, in front of the hinge; blending out
        # toward the cheeks and the ears so they stretch
        lateral = smooth(MOUTH_HALF + 0.11, MOUTH_HALF + 0.02, abs(p.x - MOUTH.x))
        vertical = smooth(MOUTH.z + 0.004, MOUTH.z - 0.03, p.z)
        front = smooth(HINGE.y + 0.03, HINGE.y - 0.06, p.y)
        wj = lateral * vertical * front
        # the neck: the lowest part of the bust behind the beard
        wn = smooth(0.46, 0.37, p.z) * smooth(-0.12, -0.02, p.y) * (1 - wj)
        wh = max(0.0, 1.0 - wj - wn)
        if wj > 1e-4:
            gj.add([v.index], wj, "REPLACE")
        if wn > 1e-4:
            gn.add([v.index], wn, "REPLACE")
        if wh > 1e-4:
            gh.add([v.index], wh, "REPLACE")
    obj.parent = rig
    mod = obj.modifiers.new("Armature", "ARMATURE")
    mod.object = rig
    return rig


def attach(o, rig, group):
    o.vertex_groups.clear()
    g = o.vertex_groups.new(name=group)
    g.add(list(range(len(o.data.vertices))), 1.0, "REPLACE")
    o.parent = rig
    m = o.modifiers.new("Armature", "ARMATURE")
    m.object = rig


def material(name, color, rough=0.6, image=None, emission=None):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = color
    b.inputs["Roughness"].default_value = rough
    if image is not None:
        t = m.node_tree.nodes.new("ShaderNodeTexImage")
        t.image = image
        m.node_tree.links.new(t.outputs["Color"], b.inputs["Base Color"])
    if emission is not None:
        b.inputs["Emission Color"].default_value = emission[0]
        b.inputs["Emission Strength"].default_value = emission[1]
    return m


def eye_image(gaze_uv=(0.75, 0.5)):
    # sclera dirty white going to raw red at the rim, a small dark iris and pupil looking out along the UV seam's
    # opposite side (u = 0.75 faces -Y for Blender's UV sphere)
    w, h = 512, 256
    u = (np.arange(w) + 0.5) / w
    v = (np.arange(h) + 0.5) / h
    U, V = np.meshgrid(u, v)
    du = ((U - gaze_uv[0] + 0.5) % 1.0 - 0.5) * 2 * math.pi
    dv = (V - gaze_uv[1]) * math.pi
    ang = np.sqrt(du * du + dv * dv)  # angle from the gaze
    sclera = np.stack([0.80 + 0 * ang, 0.74 + 0 * ang, 0.62 + 0 * ang], -1)
    rim = np.clip((ang - 1.1) / 0.6, 0, 1)[..., None]
    red = np.array([0.55, 0.12, 0.08])
    col = sclera * (1 - rim) + red * rim
    veins = (np.sin(du * 23 + np.sin(dv * 17) * 3) * np.sin(dv * 19) > 0.93) * np.clip((ang - 0.5) / 0.9, 0, 1)
    col = col * (1 - 0.6 * veins[..., None]) + red * 0.6 * veins[..., None]
    iris = np.clip((0.2 - ang) / 0.03, 0, 1)[..., None]
    col = col * (1 - iris) + np.array([0.16, 0.10, 0.05]) * iris
    pupil = np.clip((0.11 - ang) / 0.02, 0, 1)[..., None]
    col = col * (1 - pupil) + np.array([0.01, 0.008, 0.006]) * pupil
    img = bpy.data.images.new("chronos-eye", w, h)
    rgba = np.concatenate([col, np.ones((h, w, 1))], -1).astype(np.float32)
    img.pixels.foreach_set(rgba.ravel())
    img.filepath_raw = os.path.join(TEX, "chronos-eye.png")
    img.file_format = "PNG"
    img.save()
    return img


def seat_eye(obj, guess):
    """The socket's deepest point near the landmark: cast rays toward +Y over a small grid and keep the furthest hit."""
    best = None
    for i in range(-6, 7):
        for k in range(-6, 7):
            o = Vector((guess.x + i * 0.003, -1.0, guess.z + k * 0.003))
            hit, loc, nrm, idx = obj.ray_cast(o, Vector((0, 1, 0)))
            if hit and (best is None or loc.y > best.y) and (loc - guess).length < 0.04:
                best = loc.copy()
    if best is None:
        return guess
    return Vector((best.x, best.y + 0.3 * EYE_RADIUS, best.z))


def gaze_uv():
    """Where a Blender UV sphere's UVs put the point that faces -Y (the creature's forward): measured, not assumed."""
    bpy.ops.mesh.primitive_uv_sphere_add(radius=1.0, segments=32, ring_count=16)
    o = bpy.context.active_object
    me = o.data
    uv = me.uv_layers.active.data
    best, best_uv = -2.0, (0.75, 0.5)
    for poly in me.polygons:
        for li in poly.loop_indices:
            v = me.vertices[me.loops[li].vertex_index].co
            if -v.y > best:
                best, best_uv = -v.y, tuple(uv[li].uv)
    bpy.data.objects.remove(o)
    print("eye gaze uv", best_uv)
    return best_uv


def eyes(rig, head):
    img = eye_image(gaze_uv())
    m = material("chronos-eye", (1, 1, 1, 1), 0.25, img)
    seats = {"eye.L": EYE_L, "eye.R": EYE_R}
    print("eye seats", {k: tuple(round(x, 3) for x in v) for k, v in seats.items()})
    for b in rig.data.bones:
        pass
    for name, c in seats.items():
        bpy.ops.mesh.primitive_uv_sphere_add(radius=EYE_RADIUS, segments=48, ring_count=24, location=c)
        o = bpy.context.active_object
        bpy.ops.object.shade_smooth()
        o.location = c  # seated in the socket's deepest point, bulging out of it
        o.name = "chronos-" + name
        o.data.materials.append(m)
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
        attach(o, rig, name)


def throat_and_teeth(rig):
    # the throat: a dark cavity behind the mouth, inside the head
    bpy.ops.mesh.primitive_uv_sphere_add(radius=1.0, segments=32, ring_count=16)
    t = bpy.context.active_object
    t.name = "chronos-throat"
    t.scale = (0.05, 0.055, 0.085)
    t.location = (MOUTH.x, MOUTH.y + 0.115, MOUTH.z - 0.03)  # behind the lips, never through them
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    t.data.materials.append(material("chronos-throat", (0.02, 0.003, 0.002, 1), 0.9))
    attach(t, rig, "head")
    # the teeth: irregular pegs along each lip, rooted in the gum line, pointing at the bite
    tooth_mat = material("chronos-teeth", (0.40, 0.29, 0.13, 1), 0.45)
    for row, group, sign in (("upper", "head", -1), ("lower", "jaw", 1)):
        n = 13
        bm = bmesh.new()
        for i in range(n):
            u = (i + 0.5) / n * 2 - 1  # -1..1 across the mouth
            x = MOUTH.x + u * MOUTH_HALF * 0.92
            y = MOUTH.y + 0.02 + 0.03 * u * u           # the arch curves back at the corners, under the moustache
            z = MOUTH.z + 0.004 * u * u - sign * 0.009  # rooted in the gums above and below the slit
            length = rng.uniform(0.016, 0.038) * (1.25 if abs(u) < 0.35 else 1.0)
            width = rng.uniform(0.0055, 0.0095)
            if rng.random() < 0.12:
                continue  # a missing tooth
            ret = bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=True, segments=6, radius1=width,
                                        radius2=width * rng.uniform(0.15, 0.4), depth=length)
            verts = ret["verts"]
            lean = Matrix.Rotation(math.radians(rng.uniform(-12, 12)), 4, "Y") @ Matrix.Rotation(math.radians(rng.uniform(-10, 18)), 4, "X")
            flip = Matrix.Rotation(math.pi, 4, "X") if sign < 0 else Matrix.Identity(4)
            M = Matrix.Translation((x, y, z + sign * length * 0.5)) @ lean @ flip
            bmesh.ops.transform(bm, matrix=M, verts=verts)
        me = bpy.data.meshes.new("chronos-teeth-" + row)
        bm.to_mesh(me)
        bm.free()
        for poly in me.polygons:
            poly.use_smooth = True
        o = bpy.data.objects.new("chronos-teeth-" + row, me)
        bpy.context.scene.collection.objects.link(o)
        o.data.materials.append(tooth_mat)
        attach(o, rig, group)


def paint_skin(obj):
    """The painted skin: baked ambient occlusion of the scan drives umber (deep) to ochre-bone (proud), mottled."""
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 48
    src = None
    for m in obj.data.materials:
        if m and m.use_nodes:
            for n in m.node_tree.nodes:
                if n.type == "TEX_IMAGE" and n.image:
                    src = n.image
                    break
    w = 4096
    img = bpy.data.images.new("chronos-skin", w, w)
    m = bpy.data.materials.new("chronos-skin-bake")
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    emit = nt.nodes.new("ShaderNodeEmission")
    ao = nt.nodes.new("ShaderNodeAmbientOcclusion")
    ao.inputs["Distance"].default_value = 0.03
    ao.samples = 16
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    els = ramp.color_ramp.elements
    els[0].position, els[0].color = 0.25, (0.06, 0.025, 0.015, 1)     # near-black umber in the crevices
    els[1].position, els[1].color = 0.95, (0.56, 0.41, 0.24, 1)        # dirty ochre-bone on the proud forms
    mid = els.new(0.6)
    mid.color = (0.3, 0.15, 0.08, 1)                                  # burnt sienna
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 40.0
    noise.inputs["Detail"].default_value = 8.0
    noise.inputs["Roughness"].default_value = 0.65
    mot = nt.nodes.new("ShaderNodeMapRange")
    mot.inputs["To Min"].default_value = 0.72
    mot.inputs["To Max"].default_value = 1.12
    mix = nt.nodes.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    mix.blend_type = "MULTIPLY"
    mix.inputs["Factor"].default_value = 1.0
    # a trace of the plaster's own tone variation, multiplied in faintly
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = src
    trace = nt.nodes.new("ShaderNodeMix")
    trace.data_type = "RGBA"
    trace.blend_type = "MULTIPLY"
    trace.inputs["Factor"].default_value = 0.35 if src else 0.0
    nt.links.new(ao.outputs["AO"], ramp.inputs["Fac"])
    nt.links.new(noise.outputs["Fac"], mot.inputs["Value"])
    nt.links.new(ramp.outputs["Color"], mix.inputs["A"])
    nt.links.new(mot.outputs["Result"], mix.inputs["B"])
    nt.links.new(mix.outputs["Result"], trace.inputs["A"])
    if src:
        nt.links.new(tex.outputs["Color"], trace.inputs["B"])
    nt.links.new(trace.outputs["Result"], emit.inputs["Color"])
    nt.links.new(emit.outputs["Emission"], out.inputs["Surface"])
    target = nt.nodes.new("ShaderNodeTexImage")
    target.image = img
    nt.nodes.active = target
    obj.data.materials.clear()
    obj.data.materials.append(m)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.bake(type="EMIT", margin=6)
    img.filepath_raw = os.path.join(TEX, "chronos-skin.png")
    img.file_format = "PNG"
    img.save()
    final = material("chronos-skin", (1, 1, 1, 1), 0.72, img)
    obj.data.materials.clear()
    obj.data.materials.append(final)


def life_clip(rig):
    fps = 24
    scene = bpy.context.scene
    scene.render.fps = fps
    act = bpy.data.actions.new("idle")
    rig.animation_data_create()
    rig.animation_data.action = act
    pb = rig.pose.bones
    for n in pb:
        n.rotation_mode = "XYZ"
    jaw = [(0, 4), (1.6, 10), (2.9, 5), (4.4, 13), (5.6, 7), (6.6, 22), (8.2, 62), (9.4, 66), (10.6, 30),
           (11.4, 10), (12.0, 4)]
    for t, deg in jaw:
        pb["jaw"].rotation_euler = Euler((math.radians(deg), 0, 0))
        pb["jaw"].keyframe_insert("rotation_euler", frame=int(round(t * fps)))
    look = [(0, (0, 0)), (2.0, (0, 0)), (2.15, (-4, -9)), (4.5, (-4, -9)), (4.65, (2, 8)), (7.4, (2, 8)),
            (7.55, (-6, 2)), (10.2, (-6, 2)), (10.35, (0, 0)), (12.0, (0, 0))]
    for t, (p, y) in look:
        for n in ("eye.L", "eye.R"):
            pb[n].rotation_euler = Euler((math.radians(p), 0, math.radians(y)))
            pb[n].keyframe_insert("rotation_euler", frame=int(round(t * fps)))
    head = [(0, (0, 0, 0)), (4.0, (1.2, 0, -1.5)), (8.2, (-6, 0, 1.0)), (9.4, (-7, 0, 1.5)), (12.0, (0, 0, 0))]
    for t, rot in head:
        pb["head"].rotation_euler = Euler(tuple(math.radians(v) for v in rot))
        pb["head"].keyframe_insert("rotation_euler", frame=int(round(t * fps)))
    scene.frame_start, scene.frame_end = 0, 12 * fps


def export():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.export_scene.gltf(filepath=OUT, export_format="GLB", use_selection=False, export_apply=False,
                              export_animations=True, export_skins=True, export_morph=False)
    print("exported", OUT)


def stills(rig):
    if not STILLS:
        return
    os.makedirs(STILLS, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 64
    scene.cycles.use_denoising = True
    scene.render.resolution_x, scene.render.resolution_y = 1600, 1200
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.look = "AgX - High Contrast"
    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.008, 0.002, 0.002, 1)
    scene.world = world
    c = Vector((MOUTH.x, -0.15, 0.64))

    def area(name, energy, color, loc, size):
        L = bpy.data.lights.new(name, "AREA")
        L.energy, L.color, L.size = energy, color, size
        o = bpy.data.objects.new(name, L)
        o.location = c + Vector(loc)
        o.rotation_euler = (c - o.location).to_track_quat("-Z", "Y").to_euler()
        scene.collection.objects.link(o)
    area("key", 22.0, (1.0, 0.68, 0.38), (-0.75, -0.55, 0.65), 0.12)    # hard dirty amber, high on one side
    area("under", 1.2, (0.9, 0.12, 0.05), (0.05, -0.6, -0.35), 0.15)    # crimson from the mouth's direction
    area("rim", 6.0, (0.6, 0.12, 0.2), (0.7, 0.6, 0.35), 0.2)           # burgundy rim behind
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    scene.collection.objects.link(cam)
    scene.camera = cam
    for name, frame, az, el, dist, lens in (("frontal-breath", 48, 0, 3, 1.25, 75), ("three-quarter-breath", 48, 32, 5, 1.25, 75),
                                            ("frontal-devour", 220, 0, -2, 1.25, 75), ("three-quarter-devour", 220, 30, 1, 1.2, 75)):
        scene.frame_set(frame)
        a, e = math.radians(az), math.radians(el)
        cam.data.lens = lens
        cam.location = c + Vector((math.sin(a) * dist * math.cos(e), -math.cos(a) * dist * math.cos(e), dist * math.sin(e)))
        cam.rotation_euler = (c - cam.location).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = os.path.join(STILLS, f"chronos-scan-{name}.png")
        bpy.ops.render.render(write_still=True)


obj = load_and_normalise()
cut_pedestal_and_mouth(obj)
push_past_human(obj)
paint_skin(obj)
# the eye seats are found on the sculpted surface before the rig exists, and the eye bones are placed on them
EYE_L = seat_eye(obj, EYE_L)
EYE_R = seat_eye(obj, EYE_R)
rig = build_rig(obj)
eyes(rig, obj)
throat_and_teeth(rig)
life_clip(rig)
bpy.ops.wm.save_as_mainfile(filepath=os.path.splitext(OUT)[0] + ".blend")
export()
stills(rig)
print("done")
