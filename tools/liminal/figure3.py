"""All You Got, art pass 3: the mannequin as a posed figure (03-art-pass-3-addendum.md, sections 13 and 16-19, 31-33).

The pass 2 mannequin (kit.mannequin) was posed by forward kinematics from hanging angles. Its head was a rounded
box: the SDF line look draws only creases, and at ordinary distances its line is a few millimetres wide, so a
rounded head drew nothing and its near-black fill read as a black blob (the owner's "the head becomes a dark black
blob", "the head is missing"). This figure is built for the tableaux:

  * every block is sharp-edged (boxes, beams of square section, small cubes at the joints), so the line look
    draws the whole body, and the head is a faceted gem (a box chamfered on all twelve edges) whose creases draw
    its outline from every side. The head is shaded with its own surface (GLASS, index 7) so the generator can
    give it a faint glow and brighter lines (section 32: readable against dark rooms);
  * it is posed by targets, not angles: the pelvis and the spine's direction, the head's turn, and where each
    wrist and ankle should be, solved by two-bone IK with a pole for the elbow or knee. A pose is written in its
    ANCHOR's frame (the chair's, the couch's, the bed's: the kit's convention, base on y = 0, front facing +Z),
    so the figure is placed with the same transform as its furniture and sits on it by construction;
  * the solved body is kept as a list of oriented boxes (`Figure.parts`), so `contacts()` can measure, with the
    CPU SDF evaluator, how far each part is from (or into) the anchor: the pose checker the tableaux are tuned by.

The figure faces +Z, up +Y; its right side is -X (right = forward x up). Lengths are the pass 2 mannequin's
(1.8 m standing).

    python3 tools/liminal/figure3.py     # every tableau's contact report against its anchor (CPU)
"""

from __future__ import annotations

import math
from typing import Optional, Sequence

import kit as K
from kit import ACCENT, FILL, GLASS, R, S, T, U, box

Vec = Sequence[float]

# ---- proportions (metres) ---------------------------------------------------------------------------------
PELVIS_H = (0.16, 0.09, 0.10)     # half extents (side, up, front)
CHEST_H = (0.185, 0.165, 0.11)
UARM, FARM = 0.29, 0.26           # upper arm, forearm (shoulder to elbow, elbow to wrist)
THIGH, SHIN = 0.44, 0.43
HAND_H = (0.024, 0.07, 0.042)     # half (width, length, palm depth): a mitten
FOOT_H = (0.045, 0.03, 0.105)     # half (width, height, length)
HEAD_H = (0.07, 0.105, 0.083)     # the faceted head's half extents before chamfering
SHOULDER_W, HIP_W = 0.215, 0.10
CHEST_AT, NECK0, NECK1 = 0.42, 0.55, 0.60   # along the spine from the pelvis centre
HEAD_OVER_NECK = 0.155            # head centre above the neck's top, along the head's own up
SH_UP = 0.11                      # shoulders above the chest centre


# ---- small vector algebra -----------------------------------------------------------------------------
def add(a, b):
    return [x + y for x, y in zip(a, b)]


def sub(a, b):
    return [x - y for x, y in zip(a, b)]


def mul(a, k):
    return [x * k for x in a]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def norm(a):
    L = math.sqrt(dot(a, a))
    return [x / L for x in a] if L > 1e-12 else [0.0, 1.0, 0.0]


def length(a):
    return math.sqrt(dot(a, a))


def rot_axis(v, axis, deg):
    """Rodrigues: v turned `deg` degrees about the unit `axis` (right-handed)."""
    a = math.radians(deg)
    k = norm(axis)
    c, s = math.cos(a), math.sin(a)
    return add(add(mul(v, c), mul(cross(k, v), s)), mul(k, dot(k, v) * (1 - c)))


def frame(y_axis, z_hint):
    """An orthonormal frame (x, y, z) with y along `y_axis` and z as close to `z_hint` as it can be."""
    y = norm(y_axis)
    z = sub(z_hint, mul(y, dot(z_hint, y)))
    if length(z) < 1e-6:
        z = sub([0.0, 0.0, 1.0], mul(y, y[2])) if abs(y[2]) < 0.9 else sub([1.0, 0.0, 0.0], mul(y, y[0]))
    z = norm(z)
    x = cross(y, z)
    return x, y, z


def euler_of(x, y, z):
    """Euler degrees (rx, ry, rz) of the rotation whose columns are x, y, z, in the engine's R = Rz Ry Rx
    convention (sdf_eval._rot_matrix)."""
    # M[row][col]: columns are the images of the local axes
    m20, m21, m22 = x[2], y[2], z[2]
    m10, m00 = x[1], x[0]
    beta = math.asin(max(-1.0, min(1.0, -m20)))
    if abs(math.cos(beta)) > 1e-6:
        alpha = math.atan2(m21, m22)
        gamma = math.atan2(m10, m00)
    else:   # gimbal: put everything in alpha
        alpha = math.atan2(-z[1], y[1])
        gamma = 0.0
    return [round(math.degrees(alpha), 4), round(math.degrees(beta), 4), round(math.degrees(gamma), 4)]


def two_bone(root, target, a, b, pole):
    """(mid, end): the joint and the end of a two-bone chain from `root` reaching for `target`, the joint
    bending towards `pole` (a direction). Out of reach, the chain points straight at the target."""
    d_vec = sub(target, root)
    d = length(d_vec)
    u = norm(d_vec)
    d = max(min(d, a + b - 1e-4), abs(a - b) + 1e-4)
    x = (a * a - b * b + d * d) / (2 * d)
    r = math.sqrt(max(a * a - x * x, 0.0))
    w = sub(pole, mul(u, dot(pole, u)))
    w = norm(w) if length(w) > 1e-6 else norm(cross(u, [1.0, 0.0, 0.0]))
    mid = add(add(root, mul(u, x)), mul(w, r))
    end = add(mid, mul(norm(sub(add(root, mul(u, d)), mid)), b))
    return mid, end


# ---- the figure ---------------------------------------------------------------------------------------------
class Figure:
    """A posed figure: `tree` (an SDF node) and `parts` [(name, centre, (x, y, z) axes, half extents)]."""

    def __init__(self, pose: dict, name: str = "man", k=FILL, joint=ACCENT, head=GLASS):
        self.name = name
        self.parts = []
        self.points = {}
        nodes = []
        P = list(map(float, pose["P"]))
        Fv = pose.get("F", [0.0, 0.0, 1.0])
        tx, ty, tz = frame(pose.get("U", [0.0, 1.0, 0.0]), Fv)          # the spine: x = the figure's LEFT
        px, py, pz = frame(pose.get("Up", ty), Fv)                     # the pelvis (tilts less than the spine)
        right = mul(tx, -1.0)
        self.points.update(pelvis=P, up=ty, front=tz, right=right)

        def block(nm, c, axes, h, surf, part=None):
            node = T(c, R(euler_of(*axes), box(list(h))))
            if part:
                node["part"] = part
            nodes.append(S(node, surf))
            self.parts.append((nm, list(c), axes, list(h)))

        def beam(nm, a, b_, half, surf, z_hint, part=None):
            L = length(sub(b_, a))
            ax = frame(sub(b_, a), z_hint)
            block(nm, mul(add(a, b_), 0.5), ax, (half, L / 2 + half * 0.35, half), surf, part)

        def cube(nm, c, h, surf, up=None, z_hint=None):
            ax = frame(up or ty, z_hint or tz)
            ax = (rot_axis(ax[0], ax[1], 45.0), ax[1], rot_axis(ax[2], ax[1], 45.0))
            block(nm, c, ax, (h, h, h), surf)

        # the torso: the pelvis on its own tilt, a waist cube at the lumbar point, the chest and neck on the spine
        block("pelvis", add(P, mul(py, 0.04)), (px, py, pz), PELVIS_H, k, "body")
        lumbar = add(P, mul(py, 0.13))
        cube("waist", lumbar, 0.068, joint)
        chest = add(lumbar, mul(ty, CHEST_AT - 0.13))
        block("chest", chest, (tx, ty, tz), CHEST_H, k, "body")
        n0, n1 = add(lumbar, mul(ty, NECK0 - 0.13)), add(lumbar, mul(ty, NECK1 - 0.13))
        # the head: turned relative to the spine (pitch + = nodding down, yaw + = towards the figure's left,
        # roll + = towards its left shoulder)
        hd = pose.get("head", {})
        hy, hz = ty, tz
        if hd.get("yaw"):
            hz = rot_axis(hz, ty, hd["yaw"])
        hx = cross(hy, hz)
        if hd.get("pitch"):
            hy, hz = rot_axis(hy, hx, hd["pitch"]), rot_axis(hz, hx, hd["pitch"])
        if hd.get("roll"):
            hy = rot_axis(hy, hz, -hd["roll"])
        hx, hy, hz = frame(hy, hz)
        head_c = add(n1, mul(hy, HEAD_OVER_NECK))
        beam("neck", n0, add(n1, mul(hy, 0.03)), 0.036, k, tz)
        self._head(nodes, head_c, (hx, hy, hz), head)
        self.points.update(chest=chest, head=head_c, head_up=hy, head_front=hz, neck=n1)
        # arms and legs: by IK towards a wrist or ankle target with a pole, or placed directly when an elbow or
        # knee target is given (the bone keeps its length and points at it)
        for side, sgn in (("R", -1.0), ("L", 1.0)):
            sh = add(add(chest, mul(tx, sgn * SHOULDER_W)), mul(ty, SH_UP))
            spec = pose.get("hands", {}).get(side) or {}
            target = spec.get("wrist") or add(add(sh, mul(ty, -UARM - FARM + 0.02)), mul(tx, sgn * 0.04))
            if spec.get("elbow"):
                el = add(sh, mul(norm(sub(spec["elbow"], sh)), UARM))
                wr = add(el, mul(norm(sub(target, el)), FARM))
            else:
                pole = spec.get("pole") or add(mul(tz, -1.0), mul(tx, sgn * 0.3))
                el, wr = two_bone(sh, target, UARM, FARM, pole)
            hdir = norm(spec.get("dir") or sub(wr, el))
            palm = spec.get("palm") or cross(hdir, mul(tx, sgn))
            cube(f"shoulder{side}", sh, 0.05, joint)
            beam(f"uarm{side}", sh, el, 0.04, k, tz)
            cube(f"elbow{side}", el, 0.037, joint)
            beam(f"farm{side}", el, wr, 0.034, k, tz)
            hand_ax = frame(hdir, palm)
            block(f"hand{side}", add(wr, mul(hdir, HAND_H[1] + 0.01)), hand_ax, HAND_H, k)
            self.points.update({f"shoulder{side}": sh, f"elbow{side}": el, f"wrist{side}": wr,
                                f"hand{side}": add(wr, mul(hdir, 2 * HAND_H[1] + 0.01))})
            hp = add(add(P, mul(px, sgn * HIP_W)), mul(py, -0.04))
            fs = pose.get("feet", {}).get(side) or {}
            ftarget = fs.get("ankle") or add(hp, mul(py, -THIGH - SHIN + 0.02))
            if fs.get("knee"):
                kn = add(hp, mul(norm(sub(fs["knee"], hp)), THIGH))
                an = add(kn, mul(norm(sub(ftarget, kn)), SHIN))
            else:
                kn, an = two_bone(hp, ftarget, THIGH, SHIN, fs.get("pole") or pz)
            fdir = norm(fs.get("dir") or ([pz[0], 0.0, pz[2]] if abs(pz[1]) < 0.95 else [0.0, 0.0, 1.0]))
            fup = norm(fs.get("up") or [0.0, 1.0, 0.0])
            beam(f"thigh{side}", hp, kn, 0.058, k, pz)
            cube(f"knee{side}", kn, 0.048, joint)
            beam(f"shin{side}", kn, an, 0.047, k, pz)
            foot_ax = frame(fup, fdir)
            block(f"foot{side}", add(add(an, mul(fup, -0.02)), mul(fdir, 0.06)), foot_ax, FOOT_H, k)
            self.points.update({f"hip{side}": hp, f"knee{side}": kn, f"ankle{side}": an})
        self.tree = U(*nodes, name=name)

    def _head(self, nodes, c, axes, surf):
        hx, hy, hz = axes
        a, b, c3 = HEAD_H
        # a box chamfered on all twelve edges: three 45-degree boxes cut the vertical, the side-top and the
        # front-top edges, so the head has a crease (a drawn line) on every silhouette
        gem = K.I(box([a, b, c3]),
                  R([0, 45, 0], box([(a + c3) * 0.6, b + 0.05, (a + c3) * 0.6])),
                  R([45, 0, 0], box([a + 0.05, (b + c3) * 0.6, (b + c3) * 0.6])),
                  R([0, 0, 45], box([(a + b) * 0.6, (a + b) * 0.6, c3 + 0.05])))
        node = T(c, R(euler_of(hx, hy, hz), R([0, 0, 0], gem, name=self.name + "Head")))
        node["part"] = "head"
        nodes.append(S(node, surf))
        self.parts.append(("head", list(c), (hx, hy, hz), [a, b, c3]))

    def bounds(self, pad=0.06):
        """The figure's axis-aligned bounds in its own (the anchor's) frame, from every solved part's corners:
        the SDF object's march box must hold all of it (a head outside the box is never drawn)."""
        lo, hi = [1e9] * 3, [-1e9] * 3
        for _, c, (x, y, z), h in self.parts:
            for sx in (-1, 1):
                for sy in (-1, 1):
                    for sz in (-1, 1):
                        p = add(add(add(c, mul(x, sx * h[0])), mul(y, sy * h[1])), mul(z, sz * h[2]))
                        lo = [min(u, v) for u, v in zip(lo, p)]
                        hi = [max(u, v) for u, v in zip(hi, p)]
        return [v - pad for v in lo], [v + pad for v in hi]


    # ---- measuring --------------------------------------------------------------------------------------
    def samples(self, part_names=None, density=3):
        """Points on the surface of the solved parts: (part name, point)."""
        out = []
        for nm_, c, (x, y, z), h in self.parts:
            if part_names and not any(nm_.startswith(p) for p in part_names):
                continue
            n = density
            for i in range(-n, n + 1):
                for j in range(-n, n + 1):
                    u, v = i / n, j / n
                    for ax, bx, cx, ha, hb, hc in ((x, y, z, h[0], h[1], h[2]), (y, z, x, h[1], h[2], h[0]),
                                                   (z, x, y, h[2], h[0], h[1])):
                        for sgn in (-1.0, 1.0):
                            p = add(add(add(c, mul(ax, sgn * ha)), mul(bx, u * hb)), mul(cx, v * hc))
                            out.append((nm_, p))
        return out



def placed_bounds(bounds, at=(0.0, 0.0, 0.0), yaw=0.0):
    """Bounds (lo, hi) of a node placed by kit.place(node, at, yaw): the turned corners' box, then moved."""
    lo, hi = bounds
    a = math.radians(yaw)
    c, s = math.cos(a), math.sin(a)
    xs, zs = [], []
    for x in (lo[0], hi[0]):
        for z in (lo[2], hi[2]):
            # R_y(yaw) applied to (x, z): x' = x c + z s, z' = -x s + z c
            xs.append(x * c + z * s)
            zs.append(-x * s + z * c)
    return ([min(xs) + at[0], lo[1] + at[1], min(zs) + at[2]], [max(xs) + at[0], hi[1] + at[1], max(zs) + at[2]])


def contacts(fig: Figure, tree, groups: dict, tol_in=0.03, tol_gap=0.03):
    """How the figure sits on its anchor (`tree`, an SDF node in the same frame): for each contact group
    {label: [part prefixes]}, the deepest penetration and the nearest gap; and the deepest penetration of any
    other part. Returns (rows, ok)."""
    import sdf_eval
    rows, ok = [], True
    every = fig.samples()
    deepest = {}
    for nm_, p in every:
        d = sdf_eval.evaluate(tree, p)
        if d < deepest.get(nm_, (1e9, None))[0]:
            deepest[nm_] = (d, p)
    for label, prefixes in groups.items():
        ds = sorted((deepest[n][0], n, deepest[n][1]) for n in deepest if any(n.startswith(p) for p in prefixes))
        near, who, at = ds[0]
        good = -tol_in <= near <= tol_gap
        ok &= good
        where = "" if good else f" ({who} at {', '.join(f'{v:.2f}' for v in at)})"
        rows.append(f"    {label:<14} nearest {near:+.3f} m {'ok' if good else '<-- ' + ('INTO' if near < 0 else 'GAP')}{where}")
    contact_parts = {n for prefixes in groups.values() for n in deepest if any(n.startswith(p) for p in prefixes)}
    worst = min(((d, n, p) for n, (d, p) in deepest.items() if n not in contact_parts), default=(1e9, None, None))
    good = worst[0] >= -tol_in
    ok &= good
    where = "" if good else f" at {', '.join(f'{v:.2f}' for v in worst[2])}"
    rows.append(f"    {'other parts':<14} deepest {worst[0]:+.3f} m ({worst[1]}{where}) {'ok' if good else '<-- INTO'}")
    return rows, ok
