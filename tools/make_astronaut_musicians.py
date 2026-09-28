"""Build the astronaut musician prototype's assets from the owner's source files.

Run headless (the regeneration path):

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
        --python tools/make_astronaut_musicians.py -- \
        --src ~/Desktop/musician_assets --out assets/musicians \
        [--blend <validation.blend to write>]

or exec() it inside a live Blender session and call the functions one at a time, which is how the
prototype was validated (docs/prototypes/astronaut-musicians/PROGRESS.md).

The source files are NOT in this repository and neither is anything this script writes: their
licences are unconfirmed and the repository is public. assets/musicians/ATTRIBUTION.md records what
the sources are and where they came from.

What it does, in order:

1. Appends the astronaut (`Human.rig` + its one skinned mesh `Cube`) from the source .blend, moves it
   so its soles sit on z = 0, and bakes the object transforms into the data so the armature and the
   mesh both sit at the origin with identity transforms.
2. Replaces the astronaut's one material. It samples `AstronautTexture.png`, which the published
   package does not contain (the .blend and the .fbx both point at a path on the author's machine),
   so it renders as a missing texture. The replacement is a flat suit colour, plus a dark visor
   assigned to the visor's own UV island -- found by casting a ray at the helmet front and
   flood-filling that face's island, so no face is picked by hand.
3. Retargets each Mixamo clip onto the astronaut's own skeleton (UE-mannequin bone names, different
   bone rolls) by world-space rotation deltas from the two T-pose rest poses, and bakes the result
   as a new action on the astronaut. The source actions are left untouched.
4. Imports the instruments, fixes their scale/materials, lays out the two performers, and exports
   one GLB per performer and per prop group.
"""
import bpy
import bmesh
import math
import os
import sys
from mathutils import Matrix, Quaternion, Vector

# --------------------------------------------------------------------------------------------------
# Source layout (as found in ~/Desktop/musician_assets on 2026-09-28)
# --------------------------------------------------------------------------------------------------
ASTRO_BLEND = "Rigged Low Poly Astronaut v1.0 — BLEND + FBX/Untitled.blend"
PIANO_FBX = "Piano Playing.fbx"
DRUMS_FBX = "Playing Drums.fbx"
KEYBOARD_OBJ = "Basic_Keyboard/BasicKeyboard.obj"
STAND_OBJ = "Folding_Stand/OBJ/Folding_Stand.obj"
DRUMS_BLEND = "lowpoly-drums/drums.blend"

# Astronaut bone (UE4-mannequin naming, as the rig ships) <- Mixamo bone (without "mixamorig:").
# 52 of the astronaut's 53 bones. `Root` has no Mixamo counterpart and stays at rest; the 13 Mixamo
# bones left over are all end effectors (`*4`, `*_End`), which carry no deformation.
BONE_MAP = {
    "pelvis": "Hips", "spine_01": "Spine", "spine_02": "Spine1", "spine_03": "Spine2",
    "neck_01": "Neck", "head": "Head",
}
for _side, _S in (("l", "Left"), ("r", "Right")):
    BONE_MAP.update({
        f"clavicle_{_side}": f"{_S}Shoulder", f"upperarm_{_side}": f"{_S}Arm",
        f"lowerarm_{_side}": f"{_S}ForeArm", f"hand_{_side}": f"{_S}Hand",
        f"thigh_{_side}": f"{_S}UpLeg", f"calf_{_side}": f"{_S}Leg",
        f"foot_{_side}": f"{_S}Foot", f"ball_{_side}": f"{_S}ToeBase",
    })
    for _fin, _F in (("thumb", "Thumb"), ("index", "Index"), ("middle", "Middle"),
                     ("ring", "Ring"), ("pinky", "Pinky")):
        for _i in (1, 2, 3):
            BONE_MAP[f"{_fin}_0{_i}_{_side}"] = f"{_S}Hand{_F}{_i}"


def _rot(m):
    """The rotation of a 4x4 or 3x3 matrix, with any scale removed."""
    return m.to_3x3().normalized().to_quaternion()


# --------------------------------------------------------------------------------------------------
# 1. The astronaut
# --------------------------------------------------------------------------------------------------
def load_astronaut(src_dir, collection_name="astronaut"):
    """Append the rig and its mesh, stand it on z = 0 and bake the object transforms away."""
    path = os.path.join(src_dir, ASTRO_BLEND)
    coll = bpy.data.collections.new(collection_name)
    bpy.context.scene.collection.children.link(coll)
    with bpy.data.libraries.load(path, link=False) as (data_from, data_to):
        data_to.objects = [n for n in data_from.objects if n in ("Human.rig", "Cube")]
    for o in data_to.objects:
        coll.objects.link(o)
    # The appended objects themselves, not a lookup by name: a second append into the same file
    # arrives as "Human.rig.001", and a name lookup would return the first copy.
    rig = next(o for o in data_to.objects if o.type == "ARMATURE")
    mesh = next(o for o in data_to.objects if o.type == "MESH")
    rig.name, mesh.name = "astronaut_rig", "astronaut_mesh"
    rig.data.name, mesh.data.name = "astronaut_rig", "astronaut_mesh"
    bpy.context.view_layer.update()

    # The rig ships at z = -0.92 with its soles at z = -1.078: lift the pair so the soles are on 0.
    sole = min((mesh.matrix_world @ v.co).z for v in mesh.data.vertices)
    rig.location.z -= sole
    bpy.context.view_layer.update()

    # Bake both object transforms into the data. The mesh's parent inverse and its own offset
    # (0, 0.224, 0.187) exist only because of how the author parented it; glTF would carry them as
    # node transforms, and a skinned mesh node's transform is one of the places engines disagree.
    # Done through the data API rather than transform_apply, which needs an operator context that
    # neither a headless run nor the MCP bridge reliably provides.
    mesh_world = mesh.matrix_world.copy()
    rig_world = rig.matrix_world.copy()
    mesh.parent = None
    mesh.data.transform(mesh_world)
    mesh.matrix_world = Matrix.Identity(4)
    rig.data.transform(rig_world)
    rig.matrix_world = Matrix.Identity(4)
    mesh.parent = rig
    mesh.matrix_parent_inverse = Matrix.Identity(4)
    mesh.modifiers["Armature"].object = rig
    bpy.context.view_layer.update()
    return rig, mesh


def visor_island(mesh):
    """Faces of the UV island under a ray fired at the centre of the helmet front."""
    me = mesh.data
    mw = mesh.matrix_world
    head_top = max((mw @ v.co).z for v in me.vertices)
    # The visor's centre sits 0.26 m below the top of the helmet on this model (1.90 m tall).
    origin = mw.inverted() @ Vector((0.0, -2.0, head_top - 0.16))
    ok, _loc, _nor, face = mesh.ray_cast(origin, Vector((0.0, 1.0, 0.0)))
    if not ok:
        raise RuntimeError("visor ray missed the helmet")
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.faces.ensure_lookup_table()
    uvl = bm.loops.layers.uv.active

    def key(loop):
        return (round(loop[uvl].uv.x, 5), round(loop[uvl].uv.y, 5))

    island, stack = {face}, [bm.faces[face]]
    while stack:
        f = stack.pop()
        for loop in f.loops:
            ends_f = {loop.vert.index: key(loop), loop.link_loop_next.vert.index: key(loop.link_loop_next)}
            for other in loop.edge.link_loops:
                g = other.face
                if g is f or g.index in island:
                    continue
                ends_g = {other.vert.index: key(other),
                          other.link_loop_next.vert.index: key(other.link_loop_next)}
                if ends_f == ends_g:          # the edge is not a UV seam
                    island.add(g.index)
                    stack.append(g)
    bm.free()
    return sorted(island)


def make_material(name, rgb, roughness, metallic=0.0):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    p = next(n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    p.inputs["Base Color"].default_value = (*rgb, 1.0)
    p.inputs["Roughness"].default_value = roughness
    p.inputs["Metallic"].default_value = metallic
    m.diffuse_color = (*rgb, 1.0)
    return m


def fix_astronaut_materials(mesh):
    """Suit + visor in place of the material whose texture the package does not contain."""
    faces = visor_island(mesh)
    suit = make_material("AstronautSuit", (0.80, 0.80, 0.78), 0.65)
    visor = make_material("AstronautVisor", (0.015, 0.02, 0.035), 0.12)
    me = mesh.data
    old = [m for m in me.materials]
    me.materials.clear()
    me.materials.append(suit)
    me.materials.append(visor)
    for p in me.polygons:
        p.material_index = 0
    for i in faces:
        me.polygons[i].material_index = 1
    for m in old:
        if m and m.users == 0:
            bpy.data.materials.remove(m)
    for img in list(bpy.data.images):
        if img.name.startswith("AstronautTexture") and img.users == 0:
            bpy.data.images.remove(img)
    return len(faces)


# --------------------------------------------------------------------------------------------------
# 2. Mixamo clips
# --------------------------------------------------------------------------------------------------
def import_mixamo(src_dir, fname, key):
    """Import a Mixamo 'without skin' FBX as-is. Returns (armature object, its action)."""
    before_o, before_a = set(bpy.data.objects), set(bpy.data.actions)
    coll = bpy.data.collections.new(f"mixamo_{key}")
    bpy.context.scene.collection.children.link(coll)
    bpy.context.view_layer.active_layer_collection = \
        bpy.context.view_layer.layer_collection.children[coll.name]
    bpy.ops.import_scene.fbx(filepath=os.path.join(src_dir, fname), use_anim=True,
                             automatic_bone_orientation=False, ignore_leaf_bones=False,
                             use_custom_props=False)
    bpy.context.view_layer.active_layer_collection = bpy.context.view_layer.layer_collection
    arm = next(o for o in bpy.data.objects if o not in before_o and o.type == "ARMATURE")
    act = next(a for a in bpy.data.actions if a not in before_a)
    arm.name, act.name = f"mixamo_{key}_rig", f"mixamo_{key}_src"
    act.use_fake_user = True
    return arm, act


def _hip_height(obj, bone_name, sole_z):
    return (obj.matrix_world @ obj.data.bones[bone_name].head_local).z - sole_z


def retarget(src, dst, action_name, frame_range=None, hip_scale=None, hand_height_ik=False,
             hand_offsets=None, lift=0.0, leg_turnout=None, hand_shift=None):
    """Bake `src`'s current action onto `dst` as a new action called `action_name`.

    For every mapped bone and frame, the source bone's world rotation relative to its own rest is
    applied to the destination bone's world rest rotation. Both rigs rest in a T-pose facing -Y, so
    the delta means the same motion on both, whatever each bone's roll is -- which is exactly what
    a same-name copy of local rotations gets wrong. The pelvis also gets the hips' world translation
    relative to rest, scaled by the ratio of the two hip heights.

    `hand_height_ik`: the astronaut's shoulders sit ~12 cm further forward of its spine than the
    Mixamo skeleton's (the suit), so a torso lean that barely moves the source's wrists drops or
    lifts the astronaut's by several centimetres -- enough to push a hand through a keyboard. With
    this on, a two-bone solve on each arm (upper arm + forearm swing only; the hand's and fingers'
    world rotations are kept) holds each wrist at the source's wrist height plus that hand's median
    offset, so the hands keep the source's key-contact rhythm at the astronaut's own playing height.
    `hand_height_ik` is True for both hands or a collection of sides ("l", "r"); `hand_shift`
    ({"l": (dx, dy)}) also moves that wrist sideways/forward by a constant.
    `hand_offsets` ({"l": dz, "r": dz}) adds to each hand's offset; calibrate_hands() uses it to put
    both hands' fingertips on one flat key plane.
    `lift` raises the pelvis track: the retargeted feet sink 2-4 cm through the floor (the astronaut's
    shins are longer than Mixamo's), and grounding belongs to the clip, not to every scene that uses it.
    `leg_turnout` ({"r": degrees}) turns that leg out about the vertical axis through its hip, as a
    constant offset on the whole leg (thigh, calf, foot, ball rotate together, so the foot stays on
    the floor and every pedal tap is kept). The drum clip needs it: its snare strokes land where the
    suit's 0.28 m-wide right knee is, so the knee has to make room.
    Returns (action, hip_scale, ik_stats).
    """
    sc = bpy.context.scene
    act_src = src.animation_data.action
    f0, f1 = frame_range or (int(act_src.frame_range[0]), int(act_src.frame_range[1]))
    sw, dw = src.matrix_world, dst.matrix_world
    dw_inv = dw.inverted()
    dw_rot_inv = _rot(dw).inverted()

    if hip_scale is None:
        # Soles: the source's toe bases rest on its floor (y = 0 in the FBX); the destination has
        # been stood on z = 0 by load_astronaut.
        src_sole = min((sw @ src.data.bones[f"mixamorig:{s}ToeBase"].head_local).z for s in ("Left", "Right"))
        hip_scale = _hip_height(dst, "pelvis", 0.0) / _hip_height(src, "mixamorig:Hips", src_sole)

    bones = dst.data.bones
    order = [b.name for b in bones]  # bones are stored parents-first
    rest_src = {d: _rot(sw @ src.data.bones["mixamorig:" + s].matrix_local) for d, s in BONE_MAP.items()}
    rest_dst_w = {d: _rot(dw @ bones[d].matrix_local) for d in BONE_MAP}
    rest_dst_arm = {b.name: _rot(b.matrix_local) for b in bones}
    hips_rest_w = sw @ src.data.bones["mixamorig:Hips"].head_local
    pelvis = bones["pelvis"]
    pelvis_rest_w = dw @ pelvis.head_local
    pelvis_rest_rot_inv = _rot(pelvis.matrix_local).inverted()

    # Pass 1: armature-space target rotation of every bone, the pelvis position, and the source's
    # wrist positions, frame by frame.
    frames = list(range(f0, f1 + 1))
    targets, pelvis_pos, src_wrist = [], [], []
    for f in frames:
        sc.frame_set(f)
        tgt = {}
        for b in order:
            bone = bones[b]
            if b in BONE_MAP:
                q_src = _rot(sw @ src.pose.bones["mixamorig:" + BONE_MAP[b]].matrix)
                tgt[b] = dw_rot_inv @ (q_src @ rest_src[b].inverted() @ rest_dst_w[b])
            else:
                parent_pose = tgt[bone.parent.name] if bone.parent else Quaternion()
                parent_rest = rest_dst_arm[bone.parent.name] if bone.parent else Quaternion()
                tgt[b] = parent_pose @ parent_rest.inverted() @ rest_dst_arm[b]
        for side, deg in (leg_turnout or {}).items():
            turn = Quaternion(Vector((0.0, 0.0, 1.0)), math.radians(deg if side == "l" else -deg))
            for b in (f"thigh_{side}", f"calf_{side}", f"foot_{side}", f"ball_{side}"):
                tgt[b] = turn @ tgt[b]
        targets.append(tgt)
        hips_w = sw @ src.pose.bones["mixamorig:Hips"].head
        pelvis_pos.append(dw_inv @ (pelvis_rest_w + (hips_w - hips_rest_w) * hip_scale) + Vector((0.0, 0.0, lift)))
        src_wrist.append({side: dw_inv @ (sw @ src.pose.bones[f"mixamorig:{S}Hand"].head)
                          for side, S in (("l", "Left"), ("r", "Right"))})

    def heads(tgt, pel, names):
        """Armature-space head positions of `names` (and their ancestors) under rotations `tgt`."""
        out = {}

        def head(b):
            if b in out:
                return out[b]
            bone = bones[b]
            if b == "pelvis":
                h = pel.copy()
            elif bone.parent is None:
                h = bone.head_local.copy()
            else:
                p = bone.parent
                h = head(p.name) + (tgt[p.name] @ rest_dst_arm[p.name].inverted()) @ (bone.head_local - p.head_local)
            out[b] = h
            return h
        for n in names:
            head(n)
        return out

    # Pass 2 (optional): hold each wrist at the source's wrist height plus a per-hand offset.
    ik_stats = None
    if hand_height_ik:
        ik_stats = {}
        sides = ("l", "r") if hand_height_ik is True else tuple(hand_height_ik)
        for side in sides:
            chain = (f"upperarm_{side}", f"lowerarm_{side}", f"hand_{side}")
            offsets = []
            for tgt, pel, sw_ in zip(targets, pelvis_pos, src_wrist):
                h = heads(tgt, pel, chain)
                offsets.append(h[chain[2]].z - sw_[side].z)
            offsets.sort()
            c = offsets[len(offsets) // 2] + (hand_offsets or {}).get(side, 0.0)
            moved = []
            for tgt, pel, sw_ in zip(targets, pelvis_pos, src_wrist):
                h = heads(tgt, pel, chain)
                S, E, W = h[chain[0]], h[chain[1]], h[chain[2]]
                dxy = (hand_shift or {}).get(side, (0.0, 0.0))
                T = Vector((W.x + dxy[0], W.y + dxy[1], sw_[side].z + c))
                l1, l2 = (E - S).length, (W - E).length
                d = max(abs(l1 - l2) + 1e-4, min(l1 + l2 - 1e-4, (T - S).length))
                u = (T - S).normalized()
                T = S + u * d
                pole = (E - S) - u * (E - S).dot(u)
                if pole.length < 1e-6:
                    continue
                v = pole.normalized()
                cos_a = max(-1.0, min(1.0, (l1 * l1 + d * d - l2 * l2) / (2.0 * l1 * d)))
                E2 = S + (u * cos_a + v * math.sqrt(max(0.0, 1.0 - cos_a * cos_a))) * l1
                r1 = (E - S).normalized().rotation_difference((E2 - S).normalized())
                r2 = (r1 @ (W - E)).normalized().rotation_difference((T - E2).normalized())
                tgt[chain[0]] = r1 @ tgt[chain[0]]
                tgt[chain[1]] = r2 @ r1 @ tgt[chain[1]]
                moved.append((T - W).length)
            moved.sort()
            ik_stats[side] = {"offset_m": round(c, 4), "median_shift_m": round(moved[len(moved) // 2], 4),
                              "max_shift_m": round(moved[-1], 4)}

    # Pass 3: local (pose-space) rotations and the pelvis location.
    rots = {b: [] for b in order}
    locs = []
    for tgt, pel in zip(targets, pelvis_pos):
        for b in order:
            bone = bones[b]
            if bone.parent:
                base = tgt[bone.parent.name] @ rest_dst_arm[bone.parent.name].inverted() @ rest_dst_arm[b]
            else:
                base = rest_dst_arm[b]
            q = base.inverted() @ tgt[b]
            q.normalize()
            if rots[b] and rots[b][-1].dot(q) < 0.0:
                q.negate()
            rots[b].append(q)
        locs.append(pelvis_rest_rot_inv @ (pel - pelvis.head_local))

    act = bpy.data.actions.get(action_name)
    if act:
        bpy.data.actions.remove(act)
    act = bpy.data.actions.new(action_name)
    act.use_fake_user = True
    slot = act.slots.new(id_type="OBJECT", name=dst.name)
    strip = act.layers.new("retarget").strips.new(type="KEYFRAME")
    bag = strip.channelbag(slot, ensure=True)
    linear = bpy.types.Keyframe.bl_rna.properties["interpolation"].enum_items["LINEAR"].value

    def write(path, index, group, values):
        fc = bag.fcurves.new(path, index=index, group_name=group)
        fc.keyframe_points.add(len(frames))
        co = []
        for f, v in zip(frames, values):
            co += [float(f), float(v)]
        fc.keyframe_points.foreach_set("co", co)
        fc.keyframe_points.foreach_set("interpolation", [linear] * len(frames))
        fc.update()

    for b in order:
        dst.pose.bones[b].rotation_mode = "QUATERNION"
        path = f'pose.bones["{b}"].rotation_quaternion'
        for i in range(4):
            write(path, i, b, [q[i] for q in rots[b]])
    for i in range(3):
        write('pose.bones["pelvis"].location', i, "pelvis", [v[i] for v in locs])

    if dst.animation_data is None:
        dst.animation_data_create()
    dst.animation_data.action = act
    dst.animation_data.action_slot = slot
    return act, hip_scale, ik_stats


def fingertip_heights(mesh, frames, skip=()):
    """Per hand, the lowest fingertip vertex height (world z) at each frame not in `skip`."""
    sc = bpy.context.scene
    vg = {g.index: g.name for g in mesh.vertex_groups}
    fingers = {"index", "middle", "ring", "pinky"}
    idx = {}
    for side in ("l", "r"):
        idx[side] = [v.index for v in mesh.data.vertices
                     if sum(g.weight for g in v.groups
                            if vg[g.group].endswith("_" + side) and vg[g.group].split("_")[0] in fingers) > 0.5]
    out = {"l": [], "r": []}
    for f in frames:
        if f in skip:
            continue
        sc.frame_set(f)
        vs = mesh.evaluated_get(bpy.context.evaluated_depsgraph_get()).data.vertices
        mw = mesh.matrix_world
        for side, ids in idx.items():
            out[side].append(min((mw @ vs[i].co).z for i in ids))
    return out


def ground_lift(mesh, frames):
    """How far the character's lowest foot vertex sinks below z = 0 over `frames` (>= 0)."""
    sc = bpy.context.scene
    feet = [v.index for v in mesh.data.vertices if v.co.z < 0.18]
    low = 1e9
    frames = list(frames)
    sc.frame_set(frames[-1])          # settle the depsgraph: the first read after a bake can be stale
    sc.frame_set(frames[0])
    for f in frames:
        sc.frame_set(f)
        vs = mesh.evaluated_get(bpy.context.evaluated_depsgraph_get()).data.vertices
        mw = mesh.matrix_world
        low = min(low, min((mw @ vs[i].co).z for i in feet))
    return max(0.0, -low)


def calibrate_hands(src, dst, mesh, action_name, skip=(), lift=0.0):
    """Retarget with the hand-height IK twice: once to measure where each hand's fingertips play,
    once with per-hand offsets that put both hands' median fingertip height on one plane.

    The source plays both hands' fingertips at one height (it was captured at a real, flat
    keyboard); the astronaut's gloves and finger rest angles differ by hand, which left its right
    fingertips ~3.7 cm above its left. Returns (action, key_plane_z, stats) with z relative to
    `dst`'s own origin.
    """
    act, hip_scale, _ = retarget(src, dst, action_name, hand_height_ik=True, lift=lift)
    f0, f1 = int(act.frame_range[0]), int(act.frame_range[1])
    frames = range(f0, f1 + 1, 2)
    base = dst.matrix_world.translation.z
    h = fingertip_heights(mesh, frames, skip)
    med = {s: sorted(v)[len(v) // 2] - base for s, v in h.items()}
    plane = 0.5 * (med["l"] + med["r"])
    offs = {s: plane - med[s] for s in med}
    act, hip_scale, ik = retarget(src, dst, action_name, hand_height_ik=True, hand_offsets=offs, lift=lift)
    h = fingertip_heights(mesh, frames, skip)
    after = {s: sorted(v)[len(v) // 2] - base for s, v in h.items()}
    return act, plane, {"before_median": med, "offsets": offs, "after_median": after, "ik": ik,
                        "hip_scale": hip_scale}


# --------------------------------------------------------------------------------------------------
# 3. Props
# --------------------------------------------------------------------------------------------------
def _import_obj(path, name, coll):
    before = set(bpy.data.objects)
    bpy.context.view_layer.active_layer_collection = \
        bpy.context.view_layer.layer_collection.children[coll.name]
    bpy.ops.wm.obj_import(filepath=path)
    bpy.context.view_layer.active_layer_collection = bpy.context.view_layer.layer_collection
    obj = next(o for o in bpy.data.objects if o not in before)
    obj.name = obj.data.name = name
    return obj


def _bake_object_transform(obj):
    """Fold an object's transform into its mesh, leaving the object at the origin."""
    obj.data.transform(obj.matrix_world)
    obj.matrix_world = Matrix.Identity(4)


def _set_material(mat, rgb, roughness, metallic=0.0):
    p = next(n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED")
    p.inputs["Base Color"].default_value = (*rgb, 1.0)
    p.inputs["Roughness"].default_value = roughness
    p.inputs["Metallic"].default_value = metallic
    mat.diffuse_color = (*rgb, 1.0)


def _new_collection(name):
    coll = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(coll)
    return coll


def _top(obj):
    return max((obj.matrix_world @ v.co).z for v in obj.data.vertices)


# The keyboard model has 20 white keys 0.343 units wide (6.86 units of key bed). The piano clip's
# hands travel 0.95 m across the keys -- the right hand runs up to 0.59 m right of centre four times
# a loop -- so the keyboard is scaled until its key bed spans that. At this scale its white keys are
# 4.7 cm wide, twice a real key: the clip was performed at a full-size piano and the model is a
# 32-key mini keyboard. The report says so; the alternatives are a bigger keyboard model or a clip
# without the runs.
KEYBOARD_SCALE = 0.1384


def load_keyboard(src_dir, coll):
    """The keyboard, long axis on X, keys facing +Y (the player's side), bottom at z = 0."""
    kb = _import_obj(os.path.join(src_dir, KEYBOARD_OBJ), "keyboard", coll)
    # As imported: long axis on Y, player's side toward +X. A quarter turn puts the player on +Y.
    kb.rotation_euler = (math.radians(90), 0.0, math.radians(90))
    kb.scale = (KEYBOARD_SCALE,) * 3
    bpy.context.view_layer.update()
    _bake_object_transform(kb)
    bottom = min(v.co.z for v in kb.data.vertices)
    for v in kb.data.vertices:
        v.co.z -= bottom
    # The OBJ's three materials are all 0.64 grey; the pack's own preview render
    # (BasicKeyboard_IMG.png) shows a black case, white keys and black keys.
    for m in kb.data.materials:
        if m.name.startswith("KeyBoard"):
            m.name = "KeyboardCase"
            _set_material(m, (0.018, 0.018, 0.02), 0.55)
        elif m.name.startswith("WhiteKeys"):
            m.name = "KeyboardWhiteKeys"
            _set_material(m, (0.82, 0.82, 0.80), 0.35)
        elif m.name.startswith("BlackKey"):
            m.name = "KeyboardBlackKeys"
            _set_material(m, (0.012, 0.012, 0.014), 0.3)
    return kb


def keyboard_key_top(kb):
    """Height of the white keys' top surface above the keyboard's own bottom."""
    wi = next(i for i, m in enumerate(kb.data.materials) if m.name.startswith("KeyboardWhiteKeys"))
    return max(kb.data.vertices[v].co.z for p in kb.data.polygons if p.material_index == wi
               for v in p.vertices)


def keyboard_front_edge(kb):
    """y of the white keys' front edge (the edge nearest the player)."""
    wi = next(i for i, m in enumerate(kb.data.materials) if m.name.startswith("KeyboardWhiteKeys"))
    return max(kb.data.vertices[v].co.y for p in kb.data.polygons if p.material_index == wi
               for v in p.vertices)


def load_stand(src_dir, coll, pad_top, scale=1.0):
    """The folding X-stand, raised the way a real one is: by closing the X.

    It ships with its support pads 0.535 m off the floor, and the keyboard has to sit higher than
    that. Scaling only its height would squash every tube into an oval, so instead each leg assembly
    (a leg, the foot bar at its lower end, the arm at its upper end and their caps and pads) turns
    about the pivot bolt, exactly as the real stand does. The bars and caps are cylinders along the
    pivot axis, so turning them about it leaves them looking the same. `scale` is uniform and
    applied first: it sets how far apart the arms land once the X is raised.
    """
    st = _import_obj(os.path.join(src_dir, STAND_OBJ), "stand", coll)
    st.scale = (scale,) * 3
    bpy.context.view_layer.update()
    _bake_object_transform(st)
    me = st.data
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.verts.ensure_lookup_table()
    # Loose parts
    seen = [False] * len(bm.verts)
    parts = []
    for v in bm.verts:
        if seen[v.index]:
            continue
        stack, comp = [v], []
        seen[v.index] = True
        while stack:
            x = stack.pop()
            comp.append(x.index)
            for e in x.link_edges:
                o = e.other_vert(x)
                if not seen[o.index]:
                    seen[o.index] = True
                    stack.append(o)
        parts.append(comp)
    # The pivot: the bolt at the crossing of the two legs.
    pivot_z = 0.5 * (min(v.co.z for v in bm.verts) + max(v.co.z for v in bm.verts))
    feet_z = 0.017 * scale  # foot-bar centre (the bars are 34 mm tubes lying on the floor)
    groups = {"A": [], "B": [], "fixed": []}
    for comp in parts:
        c = sum((bm.verts[i].co for i in comp), Vector()) / len(comp)
        span_x = max(bm.verts[i].co.x for i in comp) - min(bm.verts[i].co.x for i in comp)
        if span_x > 0.5 * scale:               # a leg: runs corner to corner
            groups["A" if c.y < 0 else "B"].append(comp)
        elif abs(c.x) > 0.2 * scale:           # bar, cap, pad or fitting at a corner
            groups["A" if c.x * (c.z - pivot_z) < 0 else "B"].append(comp)
        else:
            groups["fixed"].append(comp)
    # Arm-bar centres sit at |x| = 0.4, z = pivot + 0.25: that is the leg's half-length and angle.
    arm_x, arm_dz = 0.4 * scale, 0.25 * scale
    half_len = math.hypot(arm_x, arm_dz)
    alpha0 = math.atan2(arm_dz, arm_x)
    bar_to_pad_top = 0.018 * scale
    target_bar_z = pad_top - bar_to_pad_top
    alpha = math.asin((target_bar_z - feet_z) / (2.0 * half_len))
    theta = alpha - alpha0
    new_pivot_z = feet_z + half_len * math.sin(alpha)
    pivot = Vector((0.0, 0.0, pivot_z))
    for key, sign in (("A", 1.0), ("B", -1.0)):
        rot = Matrix.Rotation(sign * theta, 3, "Y")
        for comp in groups[key]:
            for i in comp:
                v = bm.verts[i]
                v.co = rot @ (v.co - pivot) + Vector((0.0, 0.0, new_pivot_z))
    for comp in groups["fixed"]:
        for i in comp:
            bm.verts[i].co.z += new_pivot_z - pivot_z
    bm.to_mesh(me)
    bm.free()
    me.update()
    for m in me.materials:
        if m.name.startswith("Chrome"):
            _set_material(m, (0.75, 0.75, 0.75), 0.25, 1.0)
        elif m.name.startswith("Painted_Metal"):
            _set_material(m, (0.06, 0.06, 0.065), 0.45, 0.4)
        else:                                   # Plastic, Rubber
            _set_material(m, (0.012, 0.012, 0.012), 0.75)
    return st, math.degrees(theta)


def make_bench(coll, name, seat_top, width, depth, rgb=(0.035, 0.035, 0.04)):
    """A plain padded bench: a cushion block on four legs. Not from any asset pack."""
    bm = bmesh.new()
    cushion = 0.07
    bmesh.ops.create_cube(bm, size=1.0, matrix=Matrix.LocRotScale(
        Vector((0.0, 0.0, seat_top - cushion / 2)), None, Vector((width, depth, cushion))))
    leg = 0.04
    for sx in (-1, 1):
        for sy in (-1, 1):
            h = seat_top - cushion
            bmesh.ops.create_cube(bm, size=1.0, matrix=Matrix.LocRotScale(
                Vector((sx * (width / 2 - 0.05), sy * (depth / 2 - 0.05), h / 2)), None,
                Vector((leg, leg, h))))
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(name, me)
    coll.objects.link(obj)
    mat = make_material(f"{name}_mat", rgb, 0.7)
    me.materials.append(mat)
    return obj


# The drum kit. Its pieces keep realistic proportions to each other (kick 4.18 units, snare 2.76, rack
# toms 2.32, floor tom 3.04, hi-hat 2.14, ride 3.08), so one scale serves all seven: the kick at 22"
# (0.559 m) makes the snare 14.5", the toms 12" and the floor tom 16". The stands' feet are at
# z = -2.16 in the source.
DRUM_SCALE = 0.1337
DRUM_FLOOR = -2.16
DRUM_PIECES = {"Cylinder": "kick", "Cylinder.002": "rack_toms", "Cylinder.005": "snare",
               "Cylinder.007": "hihat", "Cylinder.011": "ride", "Cylinder.012": "floor_tom",
               "Cylinder.017": "throne"}


def _loose_parts(me):
    """Vertex-index lists, one per connected piece of a mesh."""
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.verts.ensure_lookup_table()
    seen = [False] * len(bm.verts)
    parts = []
    for v in bm.verts:
        if seen[v.index]:
            continue
        stack, comp = [v], []
        seen[v.index] = True
        while stack:
            x = stack.pop()
            comp.append(x.index)
            for e in x.link_edges:
                o = e.other_vert(x)
                if not seen[o.index]:
                    seen[o.index] = True
                    stack.append(o)
        parts.append(comp)
    bm.free()
    return parts


def load_drum_kit(src_dir, coll):
    """The seven pieces, turned so the drummer faces -Y, at real size, standing on z = 0.

    The source kit is laid out for a drummer at +X facing -X; a quarter turn about Z puts the
    drummer at +Y facing -Y, like the astronaut. Each piece's mesh ends up in that frame with the
    object at the origin, so placing a piece is a translation."""
    with bpy.data.libraries.load(os.path.join(src_dir, DRUMS_BLEND), link=False) as (df, dt):
        dt.objects = [n for n in df.objects if n in DRUM_PIECES]
    turn = Matrix.Rotation(math.radians(90.0), 4, "Z") @ Matrix.Scale(DRUM_SCALE, 4) @ \
        Matrix.Translation((0.0, 0.0, -DRUM_FLOOR))
    pieces = {}
    for o in dt.objects:
        name = DRUM_PIECES[o.name]
        coll.objects.link(o)
        o.data = o.data.copy()
        o.name = o.data.name = f"drum_{name}"
        # matrix_basis, not matrix_world: a freshly appended object's world matrix is not evaluated
        # until the depsgraph runs, and these pieces have no parents.
        o.data.transform(turn @ o.matrix_basis)
        o.matrix_world = Matrix.Identity(4)
        pieces[name] = o
    # The source's materials are unnamed ("Material.005"); name them for what they are.
    names = {"Material": "DrumHead", "Material.001": "DrumBlack", "Material.002": "DrumHardware",
             "Material.003": "DrumChrome", "Material.005": "DrumShellRed", "Material.006": "Cymbal"}
    for m in {m for o in pieces.values() for m in o.data.materials if m}:
        p = next((n for n in m.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
        if p:
            m.diffuse_color = p.inputs["Base Color"].default_value
        if m.name in names:
            m.name = names[m.name]
    return pieces


def telescope(obj, z_cut, dz):
    """Move every vertex above z_cut by dz: a stand's upper tube slides in or out of its base."""
    for v in obj.data.vertices:
        if v.co.z > z_cut:
            v.co.z += dz
    obj.data.update()


def part_bounds(obj, pick):
    """(min, max) of the loose parts of `obj` for which pick(part_min, part_max, n) is true."""
    me = obj.data
    lo, hi = Vector((1e9,) * 3), Vector((-1e9,) * 3)
    for comp in _loose_parts(me):
        pts = [me.vertices[i].co for i in comp]
        mn, mx = Vector(map(min, *pts)), Vector(map(max, *pts))
        if pick(mn, mx, len(comp)):
            lo, hi = Vector(map(min, lo, mn)), Vector(map(max, hi, mx))
    return lo, hi


def tilt_parts(obj, pick, angle_deg, axis, pivot):
    """Rotate the loose parts chosen by pick() about `axis` through `pivot` (a tilted snare basket)."""
    me = obj.data
    rot = Matrix.Rotation(math.radians(angle_deg), 3, axis)
    for comp in _loose_parts(me):
        pts = [me.vertices[i].co for i in comp]
        mn, mx = Vector(map(min, *pts)), Vector(map(max, *pts))
        if pick(mn, mx, len(comp)):
            for i in comp:
                me.vertices[i].co = rot @ (me.vertices[i].co - pivot) + pivot
    me.update()


def make_drumsticks(rig, coll, grips, frame, length=0.42, grip_from_butt=0.11):
    """Two sticks in one mesh, each weighted 100 % to its hand bone.

    A glTF prop parented to a bone does not follow the animation in AV Gen (the loader flattens
    non-skinned meshes to their rest transform), so the sticks are skinned. `grips` maps side ->
    (direction, centre), both in the hand bone's local space: the direction was solved from the
    clip's own strike frames (solve_grip), the centre is the middle of the curled fist."""
    sc = bpy.context.scene
    sc.frame_set(frame)
    bm = bmesh.new()
    groups = {}
    for side, (d_local, c_local) in grips.items():
        pb = rig.pose.bones[f"hand_{side}"]
        pose_w = rig.matrix_world @ pb.matrix
        rest_w = rig.matrix_world @ rig.data.bones[f"hand_{side}"].matrix_local
        d_w = (pose_w.to_3x3().normalized() @ d_local).normalized()
        c_w = pose_w @ c_local
        stick_w = Matrix.LocRotScale(c_w, Vector((0, 0, 1)).rotation_difference(d_w), None)
        to_rest = rest_w @ pose_w.inverted()        # posed world -> the same point at rest
        before = len(bm.verts)
        bmesh.ops.create_cone(bm, cap_ends=True, segments=8, radius1=0.0085, radius2=0.0055,
                              depth=length, matrix=to_rest @ stick_w @
                              Matrix.Translation((0, 0, length / 2 - grip_from_butt)))
        bm.verts.ensure_lookup_table()
        groups[side] = list(range(before, len(bm.verts)))
    me = bpy.data.meshes.new("drumsticks")
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new("drumsticks", me)
    coll.objects.link(obj)
    for side, ids in groups.items():
        vg = obj.vertex_groups.new(name=f"hand_{side}")
        vg.add(ids, 1.0, "REPLACE")
    obj.parent = rig
    mod = obj.modifiers.new("Armature", "ARMATURE")
    mod.object = rig
    mat = make_material("Drumstick", (0.78, 0.60, 0.36), 0.55)
    me.materials.append(mat)
    return obj


def solve_grip(rig, side, frames, yaw_deg, pitch_deg):
    """The hand-local stick direction that points along (yaw, pitch) at the given strike frames.

    yaw is measured from straight ahead (-Y) toward the performer's left (+X); pitch < 0 is down.
    Returns (direction, fist centre, per-frame error in degrees), both vectors in hand-local space."""
    sc = bpy.context.scene
    y, p = math.radians(yaw_deg), math.radians(pitch_deg)
    want = Vector((math.sin(y) * math.cos(p), -math.cos(y) * math.cos(p), math.sin(p))).normalized()
    acc = Vector()
    for f in frames:
        sc.frame_set(f)
        R = (rig.matrix_world @ rig.pose.bones[f"hand_{side}"].matrix).to_3x3().normalized()
        acc += R.inverted() @ want
    d = acc.normalized()
    errs = []
    for f in frames:
        sc.frame_set(f)
        R = (rig.matrix_world @ rig.pose.bones[f"hand_{side}"].matrix).to_3x3().normalized()
        errs.append(round(math.degrees((R @ d).angle(want)), 1))
    sc.frame_set(frames[0])
    hw = rig.matrix_world @ rig.pose.bones[f"hand_{side}"].matrix

    def head(b):
        return rig.matrix_world @ rig.pose.bones[b].head
    centre_w = (head(f"index_02_{side}") + head(f"pinky_02_{side}") + head(f"hand_{side}")
                + head(f"middle_01_{side}")) / 4
    return d, hw.inverted() @ centre_w, errs


def stick_tip_track(sticks, rig, frames):
    """World position of each stick's tip at every frame. The tip is the stick vertex farthest from
    its hand joint (the grip sits 0.11 m from the butt of a 0.42 m stick)."""
    sc = bpy.context.scene
    me = sticks.data
    tips = {}
    for vg in sticks.vertex_groups:
        side = vg.name.split("_")[-1]
        joint = rig.data.bones[vg.name].head_local
        ids = [v.index for v in me.vertices if any(g.group == vg.index for g in v.groups)]
        tips[side] = max(ids, key=lambda i: (me.vertices[i].co - joint).length)
    out = {side: [] for side in tips}
    for f in frames:
        sc.frame_set(f)
        vs = sticks.evaluated_get(bpy.context.evaluated_depsgraph_get()).data.vertices
        for side, i in tips.items():
            out[side].append((f, sticks.matrix_world @ vs[i].co))
    return out


def strikes(track, prominence=0.04, window=5):
    """Local minima of tip height, as (frame, position), wrapping round the loop."""
    zs = [p.z for _f, p in track]
    n = len(zs)
    hits = []
    for i in range(n):
        if zs[i] < zs[i - 1] and zs[i] <= zs[(i + 1) % n]:
            if max(zs[(i + k) % n] for k in range(-window, window + 1)) - zs[i] > prominence:
                hits.append(track[i])
    return hits


def fit_plane(points):
    """Least-squares z = a + b x + c y through the points. Returns (a, b, c)."""
    import numpy as np
    A = np.array([[1.0, p.x, p.y] for p in points])
    z = np.array([p.z for p in points])
    a, b, c = np.linalg.lstsq(A, z, rcond=None)[0]
    return float(a), float(b), float(c)


def _parts(obj):
    """[(min, max, vertex ids)] per loose part."""
    me = obj.data
    out = []
    for comp in _loose_parts(me):
        pts = [me.vertices[i].co for i in comp]
        out.append((Vector(map(min, *pts)), Vector(map(max, *pts)), comp))
    return out


def place_drum_kit(pieces, t):
    """Stand each piece where the performance needs it, the way a drummer sets up a kit.

    `t` holds targets measured from the retargeted clip (see PROGRESS.md for how):
      throne_xy, seat_top         where the buttocks sit and how high
      kick_x, kick_face_y         batter head centred on the right boot, just past its toe
      snare_xy, snare_top, snare_tilt_deg   head plane through the left stick's backbeats/accents
      hihat_xy, hihat_z           top cymbal mid-surface where the right stick's on-beats land
      floor_tom_xy, ride_xy       unplayed in this clip: beside the right leg, over the kick's right
    The rack toms stay mounted on the kick, as in the source kit."""
    th, kick, snare, hh = pieces["throne"], pieces["kick"], pieces["snare"], pieces["hihat"]
    # -- throne: 1.2x (the source seat is 0.29 m across; real thrones are ~0.35 m), then telescope
    lo, hi = part_bounds(th, lambda mn, mx, n: True)
    c = Vector(((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, 0.0))
    th.data.transform(Matrix.Translation(c) @ Matrix.Scale(1.2, 4) @ Matrix.Translation(-c))
    seat_lo, seat_hi = part_bounds(th, lambda mn, mx, n: mx.z > 0.3)   # the seat + its post
    base_lo, base_hi = part_bounds(th, lambda mn, mx, n: mx.z <= 0.3)  # the tripod
    telescope(th, base_hi.z + 0.02, t["seat_top"] - seat_hi.z)
    seat_lo, seat_hi = part_bounds(th, lambda mn, mx, n: mx.z > 0.3)
    th_native = Vector(((seat_lo.x + seat_hi.x) / 2, (seat_lo.y + seat_hi.y) / 2, 0.0))
    th.location = Vector((*t["throne_xy"], 0.0)) - th_native
    # -- kick (and the toms mounted on it): the shell is its largest part
    shell = max(_parts(kick), key=lambda p: len(p[2]))
    face_native = Vector(((shell[0].x + shell[1].x) / 2, shell[1].y, 0.0))
    kick.location = Vector((t["kick_x"], t["kick_face_y"], 0.0)) - face_native
    pieces["rack_toms"].location = kick.location.copy()
    # -- snare: lower the drum on its stand, tilt it toward the player, move it between the knees
    drum = max(_parts(snare), key=lambda p: len(p[2]))
    legs_top = max(p[1].z for p in _parts(snare) if p[1].z < 0.3)
    telescope(snare, legs_top + 0.02, t["snare_top"] - drum[1].z)
    drum = max(_parts(snare), key=lambda p: len(p[2]))
    pivot = Vector(((drum[0].x + drum[1].x) / 2, (drum[0].y + drum[1].y) / 2, drum[1].z))
    # parts above the stand's tube top: the drum and its basket. Tilt about X so the near (+Y) edge drops.
    tilt_parts(snare, lambda mn, mx, n: mn.z > legs_top + 0.2 and (mx.x - mn.x) > 0.1,
               -t["snare_tilt_deg"], "X", pivot)
    snare.location = Vector((*t["snare_xy"], 0.0)) - Vector((pivot.x, pivot.y, 0.0))
    # -- hi-hat: the two cymbals are its widest parts; slide the rod down to put the top one at hihat_z
    cym = sorted([p for p in _parts(hh) if (p[1].x - p[0].x) > 0.2 and p[0].z > 0.3], key=lambda p: p[1].z)
    top = cym[-1]
    mid = (top[0].z + top[1].z) / 2
    base_top = max(p[1].z for p in _parts(hh) if p[1].z < 0.3)
    telescope(hh, base_top + 0.02, t["hihat_z"] - mid)
    cym = sorted([p for p in _parts(hh) if (p[1].x - p[0].x) > 0.2 and p[0].z > 0.3], key=lambda p: p[1].z)
    top = cym[-1]
    hh.location = Vector((*t["hihat_xy"], 0.0)) - Vector(((top[0].x + top[1].x) / 2, (top[0].y + top[1].y) / 2, 0.0))
    # -- floor tom beside the right leg, ride over the kick's right side: centred on their targets
    for name, key in (("floor_tom", "floor_tom_xy"), ("ride", "ride_xy")):
        o = pieces[name]
        widest = max(_parts(o), key=lambda p: (p[1].x - p[0].x) * (p[1].y - p[0].y) + p[1].z * 1e-3)
        c = Vector(((widest[0].x + widest[1].x) / 2, (widest[0].y + widest[1].y) / 2, 0.0))
        o.location = Vector((*t[key], 0.0)) - c
    bpy.context.view_layer.update()


# --------------------------------------------------------------------------------------------------
# 4. Validation
# --------------------------------------------------------------------------------------------------
def _world_tree(obj, depsgraph):
    """A BVH of an object's evaluated mesh in world space (armature deformation included)."""
    from mathutils.bvhtree import BVHTree
    ev = obj.evaluated_get(depsgraph)
    me = ev.to_mesh()
    mw = obj.matrix_world
    verts = [mw @ v.co for v in me.vertices]
    polys = [tuple(p.vertices) for p in me.polygons]
    ev.to_mesh_clear()
    return BVHTree.FromPolygons(verts, polys, all_triangles=False, epsilon=0.0), polys


def _poly_region(mesh_obj):
    """For each polygon of the character mesh, the bone that weighs most on it."""
    vg = {g.index: g.name for g in mesh_obj.vertex_groups}
    best = []
    for v in mesh_obj.data.vertices:
        top = max(v.groups, key=lambda g: g.weight, default=None)
        best.append(vg[top.group] if top else "-")
    out = []
    for p in mesh_obj.data.polygons:
        names = [best[i] for i in p.vertices]
        out.append(max(sorted(set(names)), key=names.count))   # sorted: ties break the same way every run
    return out


def audit_intersections(char_mesh, props, frames):
    """Triangle-level intersections between the posed character and each prop, frame by frame.

    Returns {prop: {"frames_hit": n, "worst_frame": f, "worst_count": k, "regions": {bone: frames}}}.
    Props are static, so each is built once; the character is rebuilt per frame from its evaluated
    (skinned) mesh. `BVHTree.overlap` reports polygon pairs whose surfaces cross.
    """
    sc = bpy.context.scene
    dg = bpy.context.evaluated_depsgraph_get()
    trees = {p.name: _world_tree(p, dg)[0] for p in props}
    region = _poly_region(char_mesh)
    report = {p.name: {"frames_hit": 0, "worst_frame": None, "worst_count": 0, "regions": {}} for p in props}
    for f in frames:
        sc.frame_set(f)
        dg = bpy.context.evaluated_depsgraph_get()
        tree, _ = _world_tree(char_mesh, dg)
        for name, pt in trees.items():
            pairs = tree.overlap(pt)
            if not pairs:
                continue
            r = report[name]
            r["frames_hit"] += 1
            if len(pairs) > r["worst_count"]:
                r["worst_count"], r["worst_frame"] = len(pairs), f
            for bone in sorted({region[a] for a, _b in pairs}):
                r["regions"][bone] = r["regions"].get(bone, 0) + 1
    return report


def seat_contact(mesh, frames, inside):
    """Median over frames of the lowest pelvis/thigh vertex whose (x, y) satisfies inside(x, y)."""
    sc = bpy.context.scene
    vg = {g.index: g.name for g in mesh.vertex_groups}
    ids = [v.index for v in mesh.data.vertices
           if sum(g.weight for g in v.groups if vg[g.group] in ("pelvis", "thigh_l", "thigh_r", "spine_01")) > 0.5]
    lows = []
    for f in frames:
        sc.frame_set(f)
        vs = mesh.evaluated_get(bpy.context.evaluated_depsgraph_get()).data.vertices
        mw = mesh.matrix_world
        zs = [p.z for p in (mw @ vs[i].co for i in ids) if p.z > 0.3 and inside(p.x, p.y)]
        if zs:
            lows.append(min(zs))
    lows.sort()
    return lows[len(lows) // 2], lows[0], lows[-1]


# --------------------------------------------------------------------------------------------------
# 5. The pipeline
# --------------------------------------------------------------------------------------------------
# The pianist. The clip opens with both hands lifted off the keys (frames 8-34); those frames are
# left out of every key-height measurement.
PIANO_SKIP = set(range(8, 35))
KEY_BELOW_FINGERTIPS = 0.006     # white-key tops sit this far under the fingertips' median height
# From the pianist's fingertip ranges over the clip: x -0.65 .. +0.30 m, y -0.61 .. -0.47 m.
KB_X, KB_FRONT_Y = -0.175, -0.455
# Stand: 1.3x puts its arms outside both knees (x -0.67, +0.32); y = -0.63 clears the left boot,
# which reaches under the keyboard (at y = -0.576, dead centre, the calf touches it in 16 frames).
STAND_SCALE, STAND_Y = 1.3, -0.63
BENCH_CENTRE, BENCH_W, BENCH_D = (-0.05, -0.05), 0.72, 0.36
SEAT_CLEARANCE = 0.008           # a seat's top sits this far under the median seated contact height

# The drummer. See PROGRESS.md for the search these came out of.
DRUM_TURNOUT = {"r": 40.0}       # right leg turned out 40 deg about the hip: the snare needs the room
DRUM_LEFT_RAISE = 0.08           # left wrist held 8 cm higher: its stick cleared the suit's thigh
GRIP_L = ([23, 58, 93, 129], -20.0, -8.0)        # snare backbeats; stick yaw, pitch at impact
GRIP_R = ([6, 23, 41, 58, 76, 94, 111, 129], 35.0, -10.0)  # hi-hat on-beats
SNARE_BACKBEATS, SNARE_ACCENTS = (23, 58, 93, 129), (48, 119)
DRUM_FIXED = {"throne_xy": (0.115, -0.03), "kick_x": -0.30, "kick_face_y": -0.74,
              "floor_tom_xy": (-0.63, -0.36), "ride_xy": (-0.80, -0.86)}
THRONE_R = 0.174

EXPORT_SETS = {
    "astronaut_keys": {"objects": ["keys_rig", "keys_mesh"], "action": "Piano"},
    "keyboard_set": {"objects": ["keyboard", "stand", "piano_bench"]},
    "astronaut_drums": {"objects": ["drums_rig", "drums_mesh", "drumsticks"], "action": "Drums"},
    "drum_kit": {"objects": [f"drum_{n}" for n in DRUM_PIECES.values()]},
}


def grounded_retarget(src, dst, mesh, name, **kw):
    """retarget(), with the pelvis lifted until the lowest foot vertex over the clip is on z = 0."""
    lift = 0.0
    for _ in range(4):
        act, _hs, ik = retarget(src, dst, name, lift=lift, **kw)
        f0, f1 = int(act.frame_range[0]), int(act.frame_range[1])
        sink = ground_lift(mesh, range(f0, f1 + 1))
        if sink < 0.0005:
            break
        lift += sink
    return act, lift, ik


def _duplicate_performer(rig, mesh, prefix, coll):
    r = rig.copy()
    r.data = rig.data.copy()
    r.animation_data_clear()
    r.name = r.data.name = f"{prefix}_rig"
    coll.objects.link(r)
    m = mesh.copy()
    m.data = mesh.data.copy()
    m.name = m.data.name = f"{prefix}_mesh"
    coll.objects.link(m)
    m.parent = r
    m.modifiers["Armature"].object = r
    return r, m


def build(src_dir):
    """Everything, from the source files, in performer-local coordinates. Returns a report dict."""
    bpy.ops.wm.read_homefile(use_empty=True)
    sc = bpy.context.scene
    sc.render.fps, sc.render.fps_base = 30, 1.0
    report = {}
    rig, mesh = load_astronaut(src_dir)
    report["visor_faces"] = fix_astronaut_materials(mesh)
    piano_src, _ = import_mixamo(src_dir, PIANO_FBX, "piano")
    drums_src, _ = import_mixamo(src_dir, DRUMS_FBX, "drums")
    keys = bpy.data.collections["astronaut"]
    keys.name = "keys"
    rig.name = rig.data.name = "keys_rig"
    mesh.name = mesh.data.name = "keys_mesh"
    drums = _new_collection("drums")
    drig, dmesh = _duplicate_performer(rig, mesh, "drums", drums)

    # --- pianist: ground, then hold both hands on one key plane
    _act, lift_p, _ = grounded_retarget(piano_src, rig, mesh, "Piano")
    act, plane, cal = calibrate_hands(piano_src, rig, mesh, "Piano", skip=PIANO_SKIP, lift=lift_p)
    key_top = plane - KEY_BELOW_FINGERTIPS
    kb = load_keyboard(src_dir, keys)
    xs = [v.co.x for v in kb.data.vertices]
    kb.location = (KB_X - 0.5 * (min(xs) + max(xs)), KB_FRONT_Y - keyboard_front_edge(kb),
                   key_top - keyboard_key_top(kb))
    ys = [v.co.y for v in kb.data.vertices]
    stand, theta = load_stand(src_dir, keys, kb.location.z, scale=STAND_SCALE)
    stand.location = (KB_X, STAND_Y, 0.0)
    bx, by = BENCH_CENTRE
    med, lo, hi = seat_contact(mesh, range(1, 501, 2),
                               lambda x, y: abs(x - bx) < BENCH_W / 2 and abs(y - by) < BENCH_D / 2)
    bench = make_bench(keys, "piano_bench", seat_top=med - SEAT_CLEARANCE, width=BENCH_W, depth=BENCH_D)
    bench.location = (bx, by, 0.0)
    report["pianist"] = {"lift_m": round(lift_p, 4), "key_plane_m": round(plane, 4), "key_top_m": round(key_top, 4),
                         "keyboard_bottom_m": round(kb.location.z, 4), "stand_leg_turn_deg": round(theta, 2),
                         "bench_top_m": round(med - SEAT_CLEARANCE, 4),
                         "seat_contact_m": [round(lo, 4), round(med, 4), round(hi, 4)],
                         "hands": {k: ({s: round(x, 4) for s, x in v.items()} if isinstance(v, dict) and k != "ik" else v)
                                   for k, v in cal.items() if k != "hip_scale"},
                         "hip_scale": round(cal["hip_scale"], 4)}

    # --- drummer: ground, turn the right leg out, raise the left hand; sticks; kit from the strikes
    act_d, lift_d, ik_d = grounded_retarget(drums_src, drig, dmesh, "Drums", leg_turnout=DRUM_TURNOUT,
                                            hand_height_ik=("l",), hand_offsets={"l": DRUM_LEFT_RAISE})
    gl = solve_grip(drig, "l", *GRIP_L)
    gr = solve_grip(drig, "r", *GRIP_R)
    sticks = make_drumsticks(drig, drums, {"l": gl[:2], "r": gr[:2]}, frame=GRIP_L[0][0],
                             length=0.41, grip_from_butt=0.08)
    tr = stick_tip_track(sticks, drig, range(1, 143))
    byf = {f: p for f, p in tr["l"]}
    cb = sum((byf[f] for f in SNARE_BACKBEATS), Vector()) / len(SNARE_BACKBEATS)
    ca = sum((byf[f] for f in SNARE_ACCENTS), Vector()) / len(SNARE_ACCENTS)
    hits_r = strikes(tr["r"])
    zs = sorted(p.z for _f, p in hits_r)
    onbeats = [p for _f, p in hits_r if p.z <= zs[len(zs) // 2]]
    chh = sum(onbeats, Vector()) / len(onbeats)
    tx, ty = DRUM_FIXED["throne_xy"]
    med_d, lo_d, hi_d = seat_contact(dmesh, range(1, 143),
                                     lambda x, y: (x - tx) ** 2 + (y - ty) ** 2 < THRONE_R ** 2)
    T = dict(DRUM_FIXED)
    T.update({"seat_top": med_d - SEAT_CLEARANCE,
              "snare_xy": ((cb.x + ca.x) / 2, (cb.y + ca.y) / 2), "snare_top": (cb.z + ca.z) / 2,
              "snare_tilt_deg": max(0.0, min(12.0, math.degrees(math.atan2(cb.z - ca.z, abs(cb.y - ca.y))))),
              "hihat_xy": (chh.x, chh.y), "hihat_z": chh.z})
    kit = load_drum_kit(src_dir, drums)
    place_drum_kit(kit, T)
    report["drummer"] = {"lift_m": round(lift_d, 4), "left_hand_ik": ik_d["l"] if ik_d else None,
                         "grip_error_deg": {"l": gl[2], "r": gr[2]},
                         "targets": {k: (round(v, 4) if isinstance(v, float) else [round(x, 4) for x in v])
                                     for k, v in T.items()},
                         "seat_contact_m": [round(lo_d, 4), round(med_d, 4), round(hi_d, 4)]}

    # --- audits over each whole loop
    props_k = [kb, stand, bench]
    report["audit"] = {
        "pianist_body_vs_props": audit_intersections(mesh, props_k, range(1, 501, 2)),
        "drummer_body_vs_kit": audit_intersections(dmesh, list(kit.values()), range(1, 143)),
        "sticks_vs_kit": audit_intersections(sticks, list(kit.values()), range(1, 143)),
    }
    return report


def export_set(build_blend, name, out_path):
    """Open the build, keep one set's objects (and its one action), and write a GLB."""
    import os as _os
    bpy.ops.wm.open_mainfile(filepath=build_blend)
    spec = EXPORT_SETS[name]
    keep = set(spec["objects"])
    for o in list(bpy.data.objects):
        if o.name not in keep:
            bpy.data.objects.remove(o, do_unlink=True)
    for a in list(bpy.data.actions):
        if a.name != spec.get("action"):
            bpy.data.actions.remove(a)
    for o in bpy.data.objects:
        o.hide_set(False)
        o.hide_viewport = False
        o.hide_render = False
        o.select_set(True)
    bpy.context.view_layer.objects.active = bpy.data.objects[spec["objects"][0]]
    _os.makedirs(_os.path.dirname(out_path) or ".", exist_ok=True)
    animated = "action" in spec
    bpy.ops.export_scene.gltf(
        filepath=out_path, export_format="GLB", use_selection=True,
        export_yup=True,                 # Blender -Y (where both performers face) becomes glTF +Z
        export_apply=False,              # never: it would bake the armature away
        export_animations=animated, export_animation_mode="ACTIONS", export_nla_strips=False,
        export_anim_single_armature=True, export_def_bones=False,
        export_rest_position_armature=True, export_skins=True,
        export_all_influences=False,     # AV Gen reads JOINTS_0/WEIGHTS_0 only: 4 influences
        export_morph=False, export_materials="EXPORT", export_cameras=False, export_lights=False,
        export_extras=False, export_force_sampling=True, export_optimize_animation_size=False,
        export_anim_slide_to_zero=True)
    return _os.path.getsize(out_path)


def layout_validation(build_blend, out_blend, spacing=1.5):
    """The inspection scene: both performers side by side, the Mixamo sources behind them."""
    bpy.ops.wm.open_mainfile(filepath=build_blend)
    sc = bpy.context.scene
    for name, dx in (("keys", -spacing), ("drums", spacing)):
        for o in bpy.data.collections[name].objects:
            if o.parent is None:
                o.location.x += dx
    for key, dx in (("piano", -spacing), ("drums", spacing)):
        src = bpy.data.objects[f"mixamo_{key}_rig"]
        src.location = (dx, 2.5, 0.0)
        src.hide_set(True)
    # loop both clips in Blender's own playback (the GLBs are already written)
    for a in (bpy.data.actions["Piano"], bpy.data.actions["Drums"]):
        for layer in a.layers:
            for strip in layer.strips:
                for bag in strip.channelbags:
                    for fc in bag.fcurves:
                        if not any(m.type == "CYCLES" for m in fc.modifiers):
                            fc.modifiers.new("CYCLES")
    floor = bpy.data.meshes.new("floor")
    bm = bmesh.new()
    bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=4.0)
    bm.to_mesh(floor)
    bm.free()
    fo = bpy.data.objects.new("floor", floor)
    sc.collection.objects.link(fo)
    floor.materials.append(make_material("Floor", (0.10, 0.105, 0.115), 0.6))
    cam = bpy.data.objects.new("camera", bpy.data.cameras.new("camera"))
    sc.collection.objects.link(cam)
    cam.location = (0.0, -5.6, 2.2)
    cam.rotation_euler = (math.radians(76), 0.0, 0.0)
    cam.data.lens = 32
    sc.camera = cam
    key = bpy.data.objects.new("key_light", bpy.data.lights.new("key_light", "AREA"))
    key.data.energy, key.data.size = 900.0, 3.0
    key.location = (2.5, -3.0, 4.0)
    key.rotation_euler = (math.radians(45), 0.0, math.radians(40))
    sc.collection.objects.link(key)
    fill = bpy.data.objects.new("fill_light", bpy.data.lights.new("fill_light", "AREA"))
    fill.data.energy, fill.data.size = 350.0, 4.0
    fill.location = (-3.0, -2.0, 3.0)
    fill.rotation_euler = (math.radians(55), 0.0, math.radians(-50))
    sc.collection.objects.link(fill)
    world = bpy.data.worlds.new("World")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.016, 0.018, 0.024, 1.0)
    sc.world = world
    sc.frame_start, sc.frame_end = 1, 500
    for engine in ("BLENDER_EEVEE", "BLENDER_EEVEE_NEXT"):
        try:
            sc.render.engine = engine
            break
        except TypeError:
            continue
    bpy.ops.wm.save_as_mainfile(filepath=out_blend)


def main(argv):
    import argparse
    import json
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True, help="the folder holding the owner's source assets")
    ap.add_argument("--out", required=True, help="folder for the four GLBs (e.g. assets/musicians)")
    ap.add_argument("--blend", required=True, help="where to write the validation .blend")
    ap.add_argument("--report", help="where to write the measurements as JSON")
    args = ap.parse_args(argv)
    src = os.path.expanduser(args.src)
    out = os.path.abspath(args.out)
    blend = os.path.abspath(args.blend)
    build_blend = os.path.splitext(blend)[0] + "_build.blend"
    report = build(src)
    bpy.ops.wm.save_as_mainfile(filepath=build_blend)
    report["glb_bytes"] = {}
    for name in EXPORT_SETS:
        report["glb_bytes"][name] = export_set(build_blend, name, os.path.join(out, f"{name}.glb"))
    layout_validation(build_blend, blend)
    os.remove(build_blend)
    for leftover in (build_blend + "1", blend + "1"):
        if os.path.exists(leftover):
            os.remove(leftover)
    text = json.dumps(report, indent=1, default=str, sort_keys=True)
    if args.report:
        with open(args.report, "w") as fh:
            fh.write(text)
    print("###REPORT###")
    print(text)


if __name__ == "__main__":
    main(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
