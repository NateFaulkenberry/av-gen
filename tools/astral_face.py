#!/usr/bin/env python3
"""THE ASTRAL FORGE: the prototype's MASK latent (prototypes/astral-forge/shaders/latent.wgsl, archetype 0 at
coherence 1) as an AV Gen SDF tree, in the ADR-1144 vocabulary, and the prototype's face tendon curves
(faceCurve, ADR-1146) as authored point lists.

    tools/astral_face.py tree [--scale S]      -> the tree's root node as JSON on stdout
    tools/astral_face.py features [--scale S]  -> the eyes and mouth alone (the early-binding roles' latent)
    tools/astral_face.py curves [--scale S]    -> the tendon curves as JSON on stdout
    tools/astral_face.py patch <scene.json> <node>... [--scale S] [--curves <particles node>]
                                               -> rewrites those sdf nodes' trees in place

The mapping, part by part (q = the latent's local point, the prototype's units: the mask is ~6.7 tall):
  facePlate  the thick shell of an ellipsoid, faceted on the +x half (blend of the round shell and a
             24-plane facet shell, weight 0.85 smoothstep(-0.25, 0.35, x)), its rim frayed by value noise
             between radii 1.4 and 2.4, the back cut off (smax with -0.55 - z), almond eye sockets and the
             mouth slit cut through, a brow ridge (two tapered capsules) and a nose ridge smoothly added.
  faceEyes   per eye: an ellipsoid turned 0.22 rad (the right one blended 0.85 toward an octahedron) and
             three raised pupil rings (tori about z), behind a radial far-field guide beyond 1.3.
  faceMouth  the lip rim (a thin ellipsoid shell cut to a slab), 11 upper and 11 lower tapered teeth with the
             prototype's hashed lengths, behind an anisotropic far-field guide beyond 1.7.
  assembly   smin(smin(plate, eyes, 0.075), mouth, 0.075), then smin with the plate again (k = 0.25): the
             second smin thickens the plate by k / 4 where it dominates, so the plate's shell is 0.0625
             thicker and its cuts 0.06 smaller instead (a tree of 89 nodes, not 114).
Not ported: the small face inside the mouth (hidden by the slit at every T01 distance), the pupil rings'
0.02 wobble, the octahedron's 0.9 y squash, the mouth tunnel and the eye's travel through depth (folds).
"""
import json
import math
import sys

M32 = 0xFFFFFFFF


def hashu(x):
    x &= M32
    x ^= x >> 16
    x = (x * 0x7FEB352D) & M32
    x ^= x >> 15
    x = (x * 0x846CA68B) & M32
    x ^= x >> 16
    return x


def u01(h):
    return (h >> 8) / 16777216.0


def node(kind, children=None, **kw):
    n = {"kind": kind}
    for k, v in kw.items():
        n[k] = [round(c, 5) for c in v] if isinstance(v, (list, tuple)) else (round(v, 5) if isinstance(v, float) else v)
    if children:
        n["children"] = children
    return n


def T(t, child):
    return node("translate", [child], translation=t)


def R(deg, child):
    return node("rotate", [child], rotation=deg)


THICKEN = 0.0625  # smin(face, plate, 0.25): the plate dominates, so k/4 more shell
EYE_L = (-0.76, 0.72, 0.6)
EYE_R = (0.76, 0.72, 0.6)
MOUTH = (0.0, -1.45, 0.62)
ASYM = 0.85
OPEN = 0.18


def plate():
    c = (0.0, 0.15, -1.25)
    r = (1.95, 3.35, 2.0)
    shell = 2.0 * (0.15 + THICKEN)
    rnd = node("shell", [node("ellipsoid", size=r)], offset=shell)
    geo = node("shell", [node("facet", size=(r[0] * 0.97, r[1] * 0.97, r[2]), count=24, offset=0.93, speed=0.05,
                              amount=0.3)], offset=shell)
    blend = node("blend", [rnd, geo], amount=ASYM, axis=(1.0, 0.0, 0.0), offset=0.05, smooth=0.3)
    # the torn rim: the noise is read in the plate's local frame; the ramp is about the mask's centre in xy
    fray = node("fray", [blend], amount=0.22, frequency=1.7, speed=0.1, seed=61, radius=1.4, offset=2.4,
                size=(1.0, 0.75, 0.0), translation=(0.0, -c[1], 0.0))
    body = node("smoothIntersection", [T(c, fray), node("plane", axis=(0.0, 0.0, -1.0), offset=0.55)], smooth=0.25)
    s = (0.62 - 0.06, 0.36 - 0.06, 0.55 - 0.06)
    socketL = T(EYE_L, R((0.0, 0.0, -math.degrees(0.22)), node("ellipsoid", size=s)))
    socketR = T(EYE_R, R((0.0, 0.0, math.degrees(0.22)), node("ellipsoid", size=s)))
    cut = node("smoothDifference", [body, socketL, socketR], smooth=0.1)
    slit = T(MOUTH, node("ellipsoid", size=(0.95 - 0.06, 0.09 + 0.2 * OPEN - 0.06, 0.85 - 0.06)))
    cut = node("smoothDifference", [cut, slit], smooth=0.06)
    brow = node("union", [
        node("taperedCapsule", **{"from": (0.0, 1.38, 0.78)}, to=(-1.75, 1.02, 0.28), radius=0.17, radius2=0.07),
        node("taperedCapsule", **{"from": (0.0, 1.38, 0.78)}, to=(1.75, 1.02, 0.28), radius=0.17, radius2=0.07)])
    d = node("smoothUnion", [cut, brow], smooth=0.25)
    nose = node("taperedCapsule", **{"from": (0.0, 1.05, 0.86)}, to=(0.0, -0.55, 1.08), radius=0.07, radius2=0.12)
    return node("smoothUnion", [d, nose], smooth=0.2)


def rings(r_eye, step):
    out = []
    for k in (1, 2, 3):
        rr = r_eye * step * k
        z = r_eye * 0.86 - 0.03 * k
        out.append(T((0.0, z, 0.0), node("torus", radius=rr, rounding=0.028 * r_eye / 0.42)))
    # torus about local y; +90 about x turns local y onto the face's +z
    return R((90.0, 0.0, 0.0), node("union", out))


def eye(centre, sign):
    r_eye = 0.4
    ball = R((0.0, 0.0, sign * math.degrees(0.22)), node("ellipsoid", size=(r_eye * 1.25, r_eye * 0.72, r_eye)))
    if sign > 0:  # the geometric half's eye: the ball blended toward an octahedron
        ball = node("morph", [ball, node("octahedron", radius=r_eye * 1.25)], amount=min(ASYM, 1.0))
        rr = rings(r_eye, 0.22)
    else:
        rr = rings(r_eye, 0.2)
    body = T((centre[0], centre[1], centre[2] - 0.2), node("union", [ball, rr]))
    return node("farField", [body], translation=centre, size=(1.0, 1.0, 1.0), radius=1.3, offset=0.45)


def teeth():
    hy = 0.09 + 0.2 * OPEN
    upper, lower = [], []
    for cx in range(-5, 6):
        tx = cx * 0.17
        taper = 1.0 - 0.6 * abs(cx) / 5.0
        h = u01(hashu(((cx + 20) * 7919 + 13) & M32))
        len_u = (0.18 + 0.22 * h) * taper
        upper.append(node("taperedCapsule", **{"from": (tx, hy + 0.06, 0.05)}, to=(tx, hy + 0.06 - len_u, 0.12),
                          radius=0.05, radius2=0.004))
        h2 = u01(hashu(((cx + 20) * 104729 + 7) & M32))
        len_l = (0.15 + 0.25 * h2) * taper
        lower.append(node("taperedCapsule", **{"from": (tx + 0.08, -hy - 0.06, 0.05)},
                          to=(tx + 0.08, -hy - 0.06 + len_l, 0.12), radius=0.05, radius2=0.004))
    row = lambda t: node("union", [node("union", t[:8]), *t[8:]])
    return node("union", [row(upper), row(lower)])


def mouth():
    hy = 0.09 + 0.2 * OPEN
    lip = node("smoothIntersection", [node("shell", [node("ellipsoid", size=(0.97, hy + 0.03, 0.85))], offset=0.04),
                                      node("box", size=(50.0, 50.0, 0.22))], smooth=0.03)
    body = T(MOUTH, node("union", [lip, teeth()]))
    return node("farField", [body], translation=MOUTH, size=(1.0, 1.6, 1.0), radius=1.7, offset=1.0)


def features(scale=1.0):
    """The eyes and the mouth alone: the latent of the prototype's eye and mouth ROLES (sim.wgsl roles 1 and 2,
    which bind first: their thresholds are 0.06..0.42 and 0.14..0.55 against the plate's 0.28..0.93)."""
    root = node("union", [node("union", [eye(EYE_L, -1.0), eye(EYE_R, 1.0)]), mouth()])
    if abs(scale - 1.0) > 1e-6:
        root = node("scale", [root], scale=scale)
    return root


def face(scale=1.0):
    eyes = node("union", [eye(EYE_L, -1.0), eye(EYE_R, 1.0)])
    root = node("smoothUnion", [node("smoothUnion", [plate(), eyes], smooth=0.075), mouth()], smooth=0.075)
    if abs(scale - 1.0) > 1e-6:
        root = node("scale", [root], scale=scale)
    return root


# ---- tendons (prototype faceCurve; the static part: the outer curls' time term is dropped) -----------------

def plate_front_z(x, y):
    k = 1.0 - (x * x) / (1.95 * 1.95) - ((y - 0.15) ** 2) / (3.35 * 3.35)
    return -1.25 + 2.0 * math.sqrt(max(k, 0.0))


def on_plate(x, y):
    return (x, y, plate_front_z(x, y) + 0.07)


def face_curve(cid, u):
    side = 1.0 if (cid & 1) == 1 else -1.0
    if cid < 2:
        return on_plate(side * (0.12 + 1.75 * u), 1.12 + 0.42 * math.sin(math.pi * u) - 0.25 * u)
    if cid < 4:
        return on_plate(side * (0.7 + 0.85 * u), 0.2 - 2.2 * u + 0.15 * math.sin(math.pi * u))
    if cid < 6:
        return on_plate(side * (0.28 + 0.75 * u), 0.05 - 1.75 * u)
    if cid == 6:
        return on_plate(-1.35 + 2.7 * u, -1.75 - 0.9 * math.sin(math.pi * u))
    if cid == 7:
        return on_plate(0.06 * math.sin(u * 9.0), 1.35 + 1.75 * u)
    if cid < 10:
        return on_plate(side * (1.75 - 0.95 * u), 0.7 + 2.3 * u)
    k = cid - 10
    h = hashu((k * 2654435761 + 77) & M32)
    a = (k + 0.5) * 2.0 * math.pi / 8.0 + 0.6 * (u01(h) - 0.5)
    rim = (1.8 * math.cos(a), 0.15 + 3.0 * math.sin(a), -1.1)
    o = (0.45 * math.cos(a), 0.45 * math.sin(a), -1.0)
    ol = math.sqrt(sum(c * c for c in o))
    out = tuple(c / ol for c in o)
    length = 4.0 + 3.5 * u01(h >> 8)
    sd = (-out[2], 0.0, out[0] + 0.001)  # cross(out, (0, 1, 0)) + (0, 0, 0.001)
    sl = math.sqrt(sum(c * c for c in sd)) or 1.0
    sd = tuple(c / sl for c in sd)
    up = (sd[1] * out[2] - sd[2] * out[1], sd[2] * out[0] - sd[0] * out[2], sd[0] * out[1] - sd[1] * out[0])
    s = u * length
    curl = s * (0.9 + 0.6 * u01(h >> 12)) + k
    rad = 0.25 + 0.5 * s / length
    w = rad * (s / length + 0.2)
    return tuple(rim[i] + out[i] * s + (sd[i] * math.cos(curl) + up[i] * math.sin(curl)) * w for i in range(3))


def curves(scale=1.0, points=13):
    return [[[round(c * scale, 4) for c in face_curve(cid, j / (points - 1))] for j in range(points)] for cid in range(18)]


def main(argv):
    scale = 1.0
    if "--scale" in argv:
        i = argv.index("--scale")
        scale = float(argv[i + 1])
        del argv[i:i + 2]
    curves_node = None
    if "--curves" in argv:
        i = argv.index("--curves")
        curves_node = argv[i + 1]
        del argv[i:i + 2]
    if len(argv) < 2:
        print(__doc__)
        return 2
    if argv[1] == "tree":
        print(json.dumps(face(scale), indent=1))
    elif argv[1] == "features":
        print(json.dumps(features(scale), indent=1))
    elif argv[1] == "curves":
        print(json.dumps(curves(scale)))
    elif argv[1] == "patch":
        path, names = argv[2], argv[3:]
        scene = json.load(open(path))
        done = 0
        for n in scene["nodes"]:
            if n.get("kind") == "sdf" and n["name"] in names:
                n["sdf"]["tree"] = {"root": face(scale)}
                n["sdf"]["compile"] = True
                done += 1
            if curves_node and n.get("kind") == "particles" and n["name"] == curves_node:
                n["particles"].setdefault("latent", {}).setdefault("tendons", {})["curves"] = curves(scale)
        if done != len(names):
            print("not every node was found", file=sys.stderr)
            return 1
        json.dump(scene, open(path, "w"), indent=1)
        open(path, "a").write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
