"""Hero asset H2 (brief 49 §2.1, 47- §10): the night camp's alien -- the owner's Sketchfab alien (Graphetta, CC BY 4.0)
tinted to the classic grey, rigged with fingers and a jaw, and given a seek-safe "talk" clip: a neutral upright stance,
feet planted, talking across the fire to the Sasquatches -- the jaw working in phrases, the head turning and nodding,
the right hand pointing at them, the left opening in a gesture.

    Blender -b --factory-startup --python examples/strange-attractor/hero/rig_alien_h2.py -- \
        assets/strange-attractor/hero/sources/sketchfab-graphetta-alien-grey-mid.glb \
        assets/strange-attractor/hero/alien-grey-h2.glb

The body rig (landmarks, the 22-bone biped, bone heat) is rig_sketchfab_alien.py's, run through its two hooks:
- extra_bones: four two-segment fingers per hand (found by clustering the hand's distal vertices; the shortest is the
  thumb) and a jaw (its hinge behind the mouth, its tip at the chin, found from the face's midline profile);
- after_skin: the shoulder crease -- the weights smoothed across shoulder, upper arm, chest and neck; the jaw's weights
  set from the lower face with a soft edge.
The clip "talk" is an 8 s loop (every sine has a whole number of cycles in 8 s and every beat eases back to the rest
pose by 8 s), so it is deterministic and seek-safe. Arm poses are world directions blended by eased beats; the arm
lowering is shared between the clavicle and the upper arm (less crease); the legs are solved to planted feet.
The scene plays it with `animation: {"state": "talk", "speed": 1.0}`; position, rotation and scale as the Gray's.
"""
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Matrix, Quaternion, Vector

args = sys.argv[sys.argv.index("--") + 1:]
SRC, OUT = args[:2]
HERE = os.path.dirname(os.path.abspath(__file__))

HOOKS = r'''
def kmeans(X, k, iters=40):
    c = [X[np.argmax(np.linalg.norm(X - X.mean(0), axis=1))]]
    for _ in range(k - 1):
        d = np.min([np.linalg.norm(X - ci, axis=1) for ci in c], axis=0)
        c.append(X[np.argmax(d)])
    c = np.array(c)
    for _ in range(iters):
        lab = np.argmin(((X[:, None, :] - c[None]) ** 2).sum(-1), axis=1)
        c = np.array([X[lab == j].mean(0) if (lab == j).any() else c[j] for j in range(k)])
    return lab


def extra_bones(eb):
    global FINGERS, JAW, PALM
    FINGERS = {}
    PALM = {}
    for sd, sg in (("L", 1), ("R", -1)):
        w = np.array(L[f"wrist.{sd}"]); tipc = np.array(L[f"fingertip.{sd}"])
        ax = tipc - w; ln = np.linalg.norm(ax); axn = ax / ln
        rel = P - w
        t = rel @ axn
        perp = np.linalg.norm(rel - np.outer(t, axn), axis=1)
        hand = (t > 0.0) & (t < 1.25 * ln) & (perp < 0.7 * ln) & (sg * P[:, 0] > 0.1 * H)
        distal = hand & (np.linalg.norm(rel, axis=1) > 0.45 * ln)
        X = P[distal]
        lab = kmeans(X, 4)
        tips = []
        for j in range(4):
            Xi = X[lab == j]
            tip = Xi[np.argmax(np.linalg.norm(Xi - w, axis=1))]
            tips.append(tip)
        tips.sort(key=lambda q: np.linalg.norm(q - w))          # the shortest reach is the thumb
        thumb, rest = tips[0], tips[1:]
        rest.sort(key=lambda q: np.linalg.norm(q - thumb))      # then index, middle, ring by distance from it
        chain = []
        knuck = []
        for name, tip in zip(("thumb", "index", "middle", "ring"), [thumb] + rest):
            base = w + (tip - w) * (0.3 if name == "thumb" else 0.42)
            mid = base + (tip - base) * 0.55
            knuck.append(base)
            n1, n2 = f"{name}1.{sd}", f"{name}2.{sd}"
            b1 = eb.new(n1); b1.head, b1.tail = Vector(base), Vector(mid); b1.parent = eb[f"hand.{sd}"]
            b2 = eb.new(n2); b2.head, b2.tail = Vector(mid), Vector(tip); b2.parent = b1; b2.use_connect = True
            for b in (b1, b2):
                b.align_roll(Vector((0, -1, 0)))
            chain.append((n1, n2))
        eb[f"hand.{sd}"].tail = Vector(np.mean(knuck[1:], axis=0))
        FINGERS[sd] = chain
        Hv = P[hand & (t > 0.15 * ln)]
        cen = Hv.mean(0)
        _, _, vt = np.linalg.svd(Hv - cen)
        pn = vt[2] if (thumb - cen) @ vt[2] > 0 else -vt[2]
        PALM[sd] = Vector(pn)
    # the jaw: the face's midline profile (most forward y per height) -- chin, mouth recess, nose
    mid = (np.abs(P[:, 0]) < 0.012 * H) & (P[:, 2] > neck_z) & (P[:, 2] < skull[:, 2].max())
    zs = np.arange(neck_z, skull[:, 2].max(), 0.004 * H)
    prof = np.array([P[mid & (np.abs(P[:, 2] - z) < 0.003 * H), 1].min() if (mid & (np.abs(P[:, 2] - z) < 0.003 * H)).any() else np.nan for z in zs])
    ok = ~np.isnan(prof)
    zs, prof = zs[ok], prof[ok]
    sm = np.convolve(prof, np.ones(3) / 3, mode="same")
    i_chin = next(i for i in range(2, len(sm) - 2) if sm[i] < sm[i - 2] and sm[i] <= sm[i + 2])
    i_mouth = next(i for i in range(i_chin + 2, len(sm) - 2) if sm[i] > sm[i - 2] and sm[i] >= sm[i + 2])
    chin = Vector((0, sm[i_chin], zs[i_chin])); mouth_z = zs[i_mouth]
    hinge = Vector((0, head_c[1] + 0.01 * H, mouth_z + 0.015 * H))
    j = eb.new("jaw"); j.head, j.tail = hinge, chin; j.parent = eb["head"]; j.align_roll(Vector((1, 0, 0)))
    JAW = dict(chin=chin, mouth_z=mouth_z, hinge=hinge)
    print("H2 fingers", {k: [c[0] for c in v] for k, v in FINGERS.items()}, "jaw chin z %.3f mouth z %.3f" % (chin.z, mouth_z))


def after_skin():
    # Laplacian smoothing of the shoulder region's weights over the mesh edges (6 passes, 0.6), then renormalised
    me = skin.data
    nv = len(me.vertices)
    names = [g.name for g in skin.vertex_groups]
    W = np.zeros((nv, len(names)), np.float32)
    for v in me.vertices:
        for g in v.groups:
            W[v.index, g.group] = g.weight
    e = np.array([ed.vertices[:] for ed in me.edges])
    deg = np.bincount(e.ravel(), minlength=nv).astype(np.float32)
    cols = [names.index(n) for n in ("shoulder.L", "shoulder.R", "upperarm.L", "upperarm.R", "spine2", "neck")]
    region = W[:, cols].sum(1) > 0.02
    for _ in range(6):
        acc = np.zeros_like(W)
        np.add.at(acc, e[:, 0], W[e[:, 1]])
        np.add.at(acc, e[:, 1], W[e[:, 0]])
        avg = acc / np.maximum(deg, 1)[:, None]
        W[region] = W[region] * 0.4 + avg[region] * 0.6
    W[W < 0.005] = 0
    W /= np.maximum(W.sum(1, keepdims=True), 1e-6)
    for vi in np.where(region)[0]:
        for gi, nm in enumerate(names):
            if W[vi, gi] > 0:
                skin.vertex_groups[gi].add([int(vi)], float(W[vi, gi]), "REPLACE")
            else:
                skin.vertex_groups[gi].remove([int(vi)])
    # the jaw: the lower face below the mouth line, in front of the hinge, fading out over 2.5% of the height
    jg = skin.vertex_groups["jaw"]; hg = skin.vertex_groups["head"]
    mz, hy = JAW["mouth_z"], JAW["hinge"].y
    for v in skin.data.vertices:
        c = v.co
        if c.z < neck_z - 0.01 * H or c.z > mz + 0.01 * H or abs(c.x) > 0.075 * H or c.y > hy + 0.02 * H:
            wj = 0.0
        else:
            up = min(1.0, max(0.0, (mz + 0.005 * H - c.z) / (0.02 * H)))
            side = min(1.0, max(0.0, (0.075 * H - abs(c.x)) / (0.03 * H)))
            back = min(1.0, max(0.0, (hy + 0.02 * H - c.y) / (0.04 * H)))
            low = min(1.0, max(0.0, (c.z - (neck_z - 0.01 * H)) / (0.03 * H)))
            wj = up * side * back * low
        if wj > 0.01:
            others = [(g.group, g.weight) for g in v.groups if g.group != jg.index]
            tot = sum(w for _, w in others) or 1.0
            for gi, w in others:
                skin.vertex_groups[gi].add([v.index], w / tot * (1 - wj), "REPLACE")
            jg.add([v.index], wj, "REPLACE")
        else:
            jg.remove([v.index])
'''

ns = {"__name__": "rig_base", "__file__": os.path.join(HERE, "rig_sketchfab_alien.py")}
base = open(ns["__file__"]).read()
base = base[:base.index("# ---------------------------------------------------------------- the source")]
saved = sys.argv
sys.argv = [saved[0], "--", SRC, "unused", OUT]
exec(compile("import bpy\nimport numpy as np\n" + HOOKS, "hooks", "exec"), ns)
exec(compile(base, ns["__file__"], "exec"), ns)
sys.argv = saved
arm, skin, eyes, H, Lm, FINGERS, PALM = ns["arm"], ns["skin"], ns["eyes"], ns["H"], ns["L"], ns["FINGERS"], ns["PALM"]
scene = bpy.context.scene

# ---------------------------------------------------------------- the classic grey (a texture edit, new image)
mat = skin.data.materials[0]
tex = next(n for n in mat.node_tree.nodes if n.type == "TEX_IMAGE" and n.image and
           any(l.to_socket.name == "Base Color" for l in n.outputs["Color"].links))
im = tex.image
w, h = im.size
a = np.empty(w * h * 4, np.float32)
im.pixels.foreach_get(a)
a = a.reshape(-1, 4)
lum = a[:, 0] * 0.3 + a[:, 1] * 0.59 + a[:, 2] * 0.11
lum = lum / max(1e-4, float(np.median(lum)))
grey = np.array([0.40, 0.415, 0.425], np.float32)          # a cool, slightly blue-green grey (sRGB-ish linear)
hue = a[:, :3] / np.maximum(1e-4, a[:, :3].mean(1, keepdims=True))
out = grey[None] * lum[:, None] * (0.9 + 0.1 * hue)          # 10% of the original hue variation kept (veins, flush)
a[:, :3] = np.clip(out, 0, 1)
new = bpy.data.images.new("alien_skin_grey", w, h)
new.pixels.foreach_set(a.ravel())
new.pack()
tex.image = new
print("H2 tint: base colour re-tinted to grey", (w, h))

# ---------------------------------------------------------------- the clip "talk"
FPS, LOOP = 24, 8.0
frames = int(FPS * LOOP)
scene.render.fps = FPS
scene.frame_start, scene.frame_end = 0, frames
pb = arm.pose.bones
bones = arm.data.bones
for b in pb:
    b.rotation_mode = "QUATERNION"


def ease(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * (3 - 2 * x)


def beat(t, start, rise, hold, fall):
    return ease((t - start) / rise) * (1.0 - ease((t - start - rise - hold) / fall))


def wave(t, cycles, ph=0.0):
    return math.sin(2 * math.pi * cycles * t / LOOP + ph)


def nrm(*v):
    return Vector(v).normalized()


def turn_to(name, want):
    m = pb[name].matrix
    cur = (m.to_3x3() @ Vector((0, 1, 0))).normalized()
    q = cur.rotation_difference(want.normalized())
    hd = m.translation.copy()
    pb[name].matrix = Matrix.Translation(hd) @ q.to_matrix().to_4x4() @ Matrix.Translation(-hd) @ m
    bpy.context.view_layer.update()


def rotate_world(name, axis, deg):
    if abs(deg) < 1e-6:
        return
    m = pb[name].matrix
    hd = m.translation.copy()
    pb[name].matrix = Matrix.Translation(hd) @ Matrix.Rotation(math.radians(deg), 4, axis.normalized()) @ \
        Matrix.Translation(-hd) @ m
    bpy.context.view_layer.update()


def delta(name):
    """The bone's world rotation now relative to its rest."""
    return pb[name].matrix.to_quaternion() @ bones[name].matrix_local.to_quaternion().inverted()


def mix(*pairs):
    v = Vector((0, 0, 0))
    for wgt, d in pairs:
        v += d * wgt
    return v.normalized()


# arm poses as world directions (Blender: -y is the alien's front, +x its left); s = +1 left, -1 right
def poses(s):
    return {
        "rest": dict(up=nrm(s * 0.2, -0.1, -1), fo=nrm(s * 0.1, -0.4, -1), ha=nrm(s * 0.06, -0.3, -1)),
        "point": dict(up=nrm(s * 0.3, -1.0, -0.05), fo=nrm(s * 0.08, -1.0, 0.04), ha=nrm(s * 0.03, -1.0, 0.0)),
        "open": dict(up=nrm(s * 0.55, -0.62, -0.42), fo=nrm(s * 0.35, -1.0, 0.05), ha=nrm(s * 0.25, -1.0, 0.1)),
    }


CURL = {"rest": dict(thumb=25, index=28, middle=32, ring=36), "point": dict(thumb=45, index=2, middle=88, ring=92),
        "open": dict(thumb=10, index=6, middle=8, ring=10)}
hand_rest_q = {sd: bones[f"hand.{sd}"].matrix_local.to_quaternion() for sd in ("L", "R")}


def pose_at(t):
    for b in pb:
        b.location = (0, 0, 0)
        b.rotation_quaternion = (1, 0, 0, 0)
    bpy.context.view_layer.update()
    br = wave(t, 4)                                        # breaths: 4 in 8 s
    # beats: the right hand points at the Sasquatches (1.2-3.6 s), the left opens (4.4-6.9 s)
    pR = beat(t, 1.1, 0.6, 1.5, 0.7)
    oL = beat(t, 4.3, 0.6, 1.4, 0.7)
    jab = beat(t, 2.0, 0.15, 0.1, 0.3) + beat(t, 2.8, 0.15, 0.1, 0.3)   # two emphatic jabs while pointing
    pres = beat(t, 5.2, 0.25, 0.2, 0.35) + beat(t, 6.0, 0.25, 0.2, 0.35)  # two small presenting beats
    # the body: neutral, upright; a little rise with the breath, a lean into each gesture
    pb["hips"].matrix = Matrix.Translation((0.004 * H * wave(t, 2, 0.6), 0, 0.0015 * H * br)) @ pb["hips"].matrix
    bpy.context.view_layer.update()
    X, Y, Z = Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))
    rotate_world("spine", X, -(0.8 * br + 2.0 * pR + 1.0 * oL))          # forward is -y: a negative turn about x
    rotate_world("spine1", X, -0.8 * br)
    rotate_world("spine2", Z, -3.0 * pR + 3.0 * oL)                        # the chest turns toward the active hand
    # the head: toward the group, sweeping across them, nodding on the phrases
    yaw = 7.0 * wave(t, 1, 0.4) + 4.0 * pR - 3.0 * oL
    nod = 5.0 * (beat(t, 2.1, 0.2, 0.1, 0.3) + beat(t, 5.3, 0.2, 0.1, 0.3) + beat(t, 6.1, 0.2, 0.1, 0.3))
    rotate_world("neck", Z, yaw * 0.4)
    rotate_world("head", Z, yaw * 0.6)
    rotate_world("head", X, -nod - 2.0 * wave(t, 3, 1.1))
    rotate_world("head", Y, 3.0 * wave(t, 2, 2.0))                          # a slight tilt
    # arms
    for sd, s, g, jj in (("L", 1, oL, pres), ("R", -1, pR, jab)):
        P_ = poses(s)
        tgt = "open" if sd == "L" else "point"
        idle = 0.03 * wave(t, 2, 1.0 + s)
        up = mix((1 - g, P_["rest"]["up"]), (g, P_[tgt]["up"]))
        fo = mix((1 - g, P_["rest"]["fo"]), (g, P_[tgt]["fo"])) + Vector((0, 0, 0.12 * jj + idle))
        ha = mix((1 - g, P_["rest"]["ha"]), (g, P_[tgt]["ha"])) + Vector((0, 0, 0.1 * jj))
        # the clavicle takes a share of the arm's change (a softer shoulder, less crease)
        sh_rest = (bones[f"shoulder.{sd}"].tail_local - bones[f"shoulder.{sd}"].head_local).normalized()
        turn_to(f"shoulder.{sd}", mix((0.75, sh_rest), (0.25, up)) if g > 0.05 else sh_rest + Vector((0, 0, -0.06)))
        turn_to(f"upperarm.{sd}", up)
        turn_to(f"forearm.{sd}", fo)
        turn_to(f"hand.{sd}", ha)
        # fingers: curl about the axis across the knuckles, carried by the hand's rotation
        dq = delta(f"hand.{sd}")
        for (n1, n2) in FINGERS[sd]:
            name = n1[:-3]
            c = (1 - g) * CURL["rest"][name] + g * CURL[tgt][name]
            fdir = (bones[n1].tail_local - bones[n1].head_local).normalized()
            axis = dq @ fdir.cross(PALM[sd])         # the thumb-side normal proved to be the back of the hand
            rotate_world(n1, axis, -c * 0.55)        # (H2 preview): curl the other way, into the palm
            rotate_world(n2, axis, -c * 0.45)
    # the jaw: phrases of syllables (4.5 a second) while talking, closed between
    talk = beat(t, 0.6, 0.3, 2.8, 0.3) + beat(t, 4.1, 0.3, 2.6, 0.3)
    syl = 0.5 + 0.5 * wave(t, 36) * (0.7 + 0.3 * wave(t, 13, 0.7))
    jaw_axis = delta("head") @ X
    rotate_world("jaw", jaw_axis, 9.0 * talk * syl ** 1.5)
    # the legs: planted (two-bone solve to the rest ankles), the feet keep their rest orientation
    for sd in ("L", "R"):
        th, sh, ft = pb[f"thigh.{sd}"], pb[f"shin.{sd}"], pb[f"foot.{sd}"]
        A = bones[f"foot.{sd}"].head_local.copy()
        Hh = th.head.copy()
        l1, l2 = bones[th.name].length, bones[sh.name].length
        dv = A - Hh
        d = min(dv.length, (l1 + l2) * 0.9995)
        ang = math.acos(max(-1.0, min(1.0, (l1 * l1 + d * d - l2 * l2) / (2 * l1 * d))))
        nn = dv.cross(Vector((0, -1, 0)))           # the knee bends forward
        nn = nn.normalized() if nn.length > 1e-6 else X
        K = Hh + (Quaternion(nn, -ang) @ dv.normalized()) * l1
        if (K - Hh).dot(Vector((0, -1, 0))) < 0:
            K = Hh + (Quaternion(nn, ang) @ dv.normalized()) * l1
        turn_to(th.name, K - Hh)
        turn_to(sh.name, Hh + dv.normalized() * d - K)
        turn_to(ft.name, (bones[ft.name].tail_local - bones[ft.name].head_local))


feet = {"L": [], "R": []}
clear = []
for fr in range(0, frames + 1, 2):
    t = fr / FPS
    scene.frame_set(fr)
    pose_at(t)
    for b in pb:
        b.keyframe_insert("rotation_quaternion", frame=fr)
        if b.name == "hips":
            b.keyframe_insert("location", frame=fr)
    for sd in ("L", "R"):
        feet[sd].append((arm.matrix_world @ pb[f"foot.{sd}"].head).copy())
    # the pointing hand must clear the body: the index tip ahead of the chest
    if 1.8 <= t <= 3.4:
        tip = arm.matrix_world @ pb[FINGERS["R"][1][1]].tail
        chest = arm.matrix_world @ pb["spine2"].head
        clear.append(chest.y - tip.y)
act = arm.animation_data.action
act.name = "talk"
for a in list(bpy.data.actions):
    if a is not act:
        bpy.data.actions.remove(a)
drift = {sd: max((p - feet[sd][0]).length for p in feet[sd]) for sd in feet}
print("H2 FEET drift (glb units) %s, in the scene x0.68: %s" % ({k: round(v, 4) for k, v in drift.items()},
                                                               {k: round(v * 0.68, 4) for k, v in drift.items()}))
print("H2 pointing tip ahead of the chest: min %.3f m (glb units)" % min(clear))
bpy.ops.wm.save_as_mainfile(filepath=OUT.replace(".glb", ".blend"), compress=True)
bpy.ops.object.select_all(action="SELECT")
bpy.ops.export_scene.gltf(filepath=OUT, export_animations=True, export_animation_mode="ACTIONS",
                          export_image_format="AUTO")
print("H2 EXPORTED", OUT)
