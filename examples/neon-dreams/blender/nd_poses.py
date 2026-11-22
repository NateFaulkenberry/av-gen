#!/usr/bin/env python3
"""Neon Dreams it12 (owner msg 170: "need to add posing variety to aliens in this scene"): more clips for the cast.

The Glowmere cast (assets/aliens/alien-*.glb, a purchased pack: gitignored, never committed) carries 26 clips and
none of them claps, cheers, raises an arm, sits, leans on a rail or talks; its Idle is a turned stride stance, so a
crowd of idlers is one silhouette. This tool authors the missing poses in code and writes them, with mirrored copies
of the pack's standing clips, into a derived copy of each cast file beside it:

    assets/aliens/<cast>.glb  ->  assets/aliens/<cast>-nd.glb      (same folder, same gitignore rule: never committed)

    /usr/bin/python3 examples/neon-dreams/blender/nd_poses.py            # all six (about a minute), + art/poses.json
    /usr/bin/python3 examples/neon-dreams/blender/nd_poses.py --only ND_Clap,ND_Sit --cast alien-scout --out DIR

Plain Python + numpy (no Blender, no GPU). Nothing of the pack enters the repository: the derived file is the
owner's local file with animations appended (meshes, textures, skin and the 26 stock clips byte for byte); the
repository holds only this code and art/poses.json (clip names, seconds, what each is for).

How a clip is made. The deform skeleton is flat (forearms, hands, head, the five spine joints and the shins hang
off the rig's root, not off each other), so a pose cannot be written as a few local rotations; it is solved in
model space from the bind pose (upright, symmetric, feet side by side; the fingers take Idle's curl). Each body part
gets a rigid displacement D: the hips (moved, turned), the spine joints, neck and head (turned about their own
pivots, carried up the chain), the arms and legs by two-bone IK (a hand or ankle target and the way the elbow or knee
points; feet stay where they are put when the hips move, so a dip bends the knees), and every joint of a part
follows its D. The model matrices then go back to each joint's local translation and rotation against its real
parent; a channel that never leaves the bind pose is left out. A mirrored copy reflects every joint's motion in the
body's midplane onto its partner, with the bind-pose correction that keeps each joint's own frame.
Clip lengths are whole kicks of Sabertooth (0.70 s = two beats at 171.4 BPM) and clip second 0 is ON a kick, so at
rate 1 a clip keeps the beat; keys run 0..L with the first pose repeated at L (a seamless loop).

Frame: glTF, Y up, the cast faces +Z, its left hand is on +X. Angles in degrees: rx > 0 leans forward, ry > 0 turns
to its left, rz > 0 tips its head toward its right.
"""
from __future__ import annotations

import json
import math
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
ALIENS = os.path.join(ROOT, "assets", "aliens")
ART = os.environ.get("ND_ART", os.path.join(HERE, "..", "art"))   # ND_ART: a scratch copy
CAST = ["alien-scout", "alien-elder", "alien-pilot", "alien-ranger", "alien-diver", "alien-trooper"]
SUFFIX = "-nd"
BEAT = 60.0 / 171.4
KICK = 2 * BEAT          # 0.70 s: the kick's pulse (the snare falls on every other one)
FPS = 30.0


# ------------------------------------------------------------------------------------------------ small maths
def q2m(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def m2q(R):
    t = R[0, 0] + R[1, 1] + R[2, 2]
    if t > 0:
        s = math.sqrt(t + 1.0) * 2
        q = [(R[2, 1] - R[1, 2]) / s, (R[0, 2] - R[2, 0]) / s, (R[1, 0] - R[0, 1]) / s, 0.25 * s]
    elif R[0, 0] > R[1, 1] and R[0, 0] > R[2, 2]:
        s = math.sqrt(1.0 + R[0, 0] - R[1, 1] - R[2, 2]) * 2
        q = [0.25 * s, (R[0, 1] + R[1, 0]) / s, (R[0, 2] + R[2, 0]) / s, (R[2, 1] - R[1, 2]) / s]
    elif R[1, 1] > R[2, 2]:
        s = math.sqrt(1.0 + R[1, 1] - R[0, 0] - R[2, 2]) * 2
        q = [(R[0, 1] + R[1, 0]) / s, 0.25 * s, (R[1, 2] + R[2, 1]) / s, (R[0, 2] - R[2, 0]) / s]
    else:
        s = math.sqrt(1.0 + R[2, 2] - R[0, 0] - R[1, 1]) * 2
        q = [(R[0, 2] + R[2, 0]) / s, (R[1, 2] + R[2, 1]) / s, 0.25 * s, (R[1, 0] - R[0, 1]) / s]
    q = np.array(q)
    return q / np.linalg.norm(q)


def trs(t, q, s):
    M = np.eye(4)
    M[:3, :3] = q2m(q) * np.asarray(s)[None, :]
    M[:3, 3] = t
    return M


def axis_rot(axis, deg):
    a = np.asarray(axis, float)
    n = np.linalg.norm(a)
    if n < 1e-9 or abs(deg) < 1e-9:
        return np.eye(3)
    a = a / n
    th = math.radians(deg)
    K = np.array([[0, -a[2], a[1]], [a[2], 0, -a[0]], [-a[1], a[0], 0]])
    return np.eye(3) + math.sin(th) * K + (1 - math.cos(th)) * (K @ K)


def euler(r):
    """Model-axis turns, applied yaw (Y), then pitch (X), then roll (Z)."""
    if r is None:
        return np.eye(3)
    rx, ry, rz = r
    return axis_rot([0, 1, 0], ry) @ axis_rot([1, 0, 0], rx) @ axis_rot([0, 0, 1], rz)


def rot_about(p, R):
    M = np.eye(4)
    M[:3, :3] = R
    M[:3, 3] = np.asarray(p) - R @ np.asarray(p)
    return M


def shift(v):
    M = np.eye(4)
    M[:3, 3] = v
    return M


def apply(M, p):
    return M[:3, :3] @ np.asarray(p, float) + M[:3, 3]


def unit(v):
    v = np.asarray(v, float)
    n = np.linalg.norm(v)
    return v / n if n > 1e-9 else np.array([0.0, 1.0, 0.0])


def minrot(a, b):
    """The shortest turn taking direction a to direction b."""
    a, b = unit(a), unit(b)
    c = float(np.dot(a, b))
    ax = np.cross(a, b)
    s = np.linalg.norm(ax)
    if s < 1e-8:
        if c > 0:
            return np.eye(3)
        ax = np.cross(a, [1.0, 0.0, 0.0])
        if np.linalg.norm(ax) < 1e-6:
            ax = np.cross(a, [0.0, 0.0, 1.0])
        return axis_rot(ax, 180.0)
    return axis_rot(ax, math.degrees(math.atan2(s, c)))


def two_bone(root, target, a, c, pole):
    """Mid joint and reached tip of a two-bone chain of lengths a, c from `root` toward `target`; the mid joint
    bends toward `pole` (a direction)."""
    d = np.asarray(target, float) - root
    L = np.linalg.norm(d)
    n = d / L if L > 1e-9 else np.array([0.0, -1.0, 0.0])
    L = min(max(L, abs(a - c) + 1e-4), (a + c) * 0.999)
    x = (a * a - c * c + L * L) / (2 * L)
    h = math.sqrt(max(a * a - x * x, 0.0))
    p = np.asarray(pole, float)
    p = p - np.dot(p, n) * n
    if np.linalg.norm(p) < 1e-6:
        p = np.cross(n, [1.0, 0.0, 0.0])
    p = unit(p)
    return root + n * x + p * h, root + n * L


def smooth(u):
    u = min(1.0, max(0.0, u))
    return u * u * (3 - 2 * u)


def lerp(a, b, u):
    return np.asarray(a, float) * (1 - u) + np.asarray(b, float) * u


def wave(t, period, phase=0.0):
    return math.sin(2 * math.pi * (t / period + phase))


def bump(t, period, phase=0.0, sharp=1.0):
    """0..1, peaking once a period at t = phase * period (a raised cosine, sharpened by a power)."""
    return (0.5 + 0.5 * math.cos(2 * math.pi * (t / period - phase))) ** sharp


def keyed(t, keys, period=None):
    """Smooth interpolation through (time, value) keys; with `period` the keys loop."""
    if period:
        t = t % period
        keys = list(keys) + [(keys[0][0] + period, keys[0][1])]
    if t <= keys[0][0]:
        return np.asarray(keys[0][1], float)
    for (ta, va), (tb, vb) in zip(keys, keys[1:]):
        if t <= tb:
            return lerp(va, vb, smooth((t - ta) / max(tb - ta, 1e-9)))
    return np.asarray(keys[-1][1], float)


# ------------------------------------------------------------------------------------------------ the file
class GLB:
    def __init__(self, path):
        b = open(path, "rb").read()
        n = struct.unpack("<I", b[12:16])[0]
        self.j = json.loads(b[20:20 + n])
        off = 20 + n
        m = struct.unpack("<I", b[off:off + 4])[0]
        self.bin = bytearray(b[off + 8:off + 8 + m])

    def acc(self, i):
        a = self.j["accessors"][i]
        bv = self.j["bufferViews"][a["bufferView"]]
        nc = {"SCALAR": 1, "VEC3": 3, "VEC4": 4, "MAT4": 16}[a["type"]]
        off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        return np.frombuffer(bytes(self.bin[off:off + a["count"] * nc * 4]), dtype=np.float32).reshape(a["count"], nc)

    def add(self, arr, kind, minmax=False):
        arr = np.ascontiguousarray(arr, dtype=np.float32)
        while len(self.bin) % 4:
            self.bin.append(0)
        self.j["bufferViews"].append({"buffer": 0, "byteOffset": len(self.bin), "byteLength": arr.nbytes})
        self.bin += arr.tobytes()
        a = {"bufferView": len(self.j["bufferViews"]) - 1, "componentType": 5126, "count": int(arr.shape[0]), "type": kind}
        if minmax:
            a["min"], a["max"] = [float(arr.min())], [float(arr.max())]
        self.j["accessors"].append(a)
        return len(self.j["accessors"]) - 1

    def save(self, path):
        while len(self.bin) % 4:
            self.bin.append(0)
        self.j["buffers"][0]["byteLength"] = len(self.bin)
        js = json.dumps(self.j, separators=(",", ":")).encode()
        js += b" " * ((4 - len(js) % 4) % 4)
        with open(path, "wb") as f:
            f.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(self.bin)))
            f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
            f.write(struct.pack("<II", len(self.bin), 0x004E4942) + bytes(self.bin))


class Rig:
    def __init__(self, glb):
        self.g = glb
        j = glb.j
        self.nodes = j["nodes"]
        self.n = len(self.nodes)
        self.parent = [-1] * self.n
        for i, nd in enumerate(self.nodes):
            for c in nd.get("children", []):
                self.parent[c] = i
        self.idx = {nd.get("name"): i for i, nd in enumerate(self.nodes)}
        self.joints = list(j["skins"][0]["joints"])
        self.order = []
        seen = set()

        def visit(i):
            if i in seen:
                return
            if self.parent[i] >= 0:
                visit(self.parent[i])
            seen.add(i)
            self.order.append(i)
        for i in range(self.n):
            visit(i)
        self.T0 = np.array([nd.get("translation", [0, 0, 0]) for nd in self.nodes], float)
        self.R0 = np.array([nd.get("rotation", [0, 0, 0, 1]) for nd in self.nodes], float)
        self.S0 = np.array([nd.get("scale", [1, 1, 1]) for nd in self.nodes], float)
        self.M0 = self.model(self.T0, self.R0, self.S0)
        self.clips = {}
        for a in j["animations"]:
            self.clips[a["name"]] = a
        self._cache = {}
        # the mirror partner of each node, and its rest correction (see mirror())
        def partner(name):
            if name.endswith(".l"):
                return name[:-2] + ".r"
            if name.endswith(".r"):
                return name[:-2] + ".l"
            if name.endswith("_L"):
                return name[:-2] + "_R"
            if name.endswith("_R"):
                return name[:-2] + "_L"
            return name
        self.mate = [self.idx.get(partner(nd.get("name", "")), i) for i, nd in enumerate(self.nodes)]
        Sx = np.diag([-1.0, 1.0, 1.0, 1.0])
        self.mirC = [np.linalg.inv(Sx @ self.M0[self.mate[i]] @ Sx) @ self.M0[i] for i in range(self.n)]
        hands = {self.idx.get("hand.l"), self.idx.get("hand.r")}

        def under_hand(i):
            while self.parent[i] >= 0:
                i = self.parent[i]
                if i in hands:
                    return True
            return False
        self.fingers = [i for i in range(self.n) if under_hand(i)]

    def model(self, T, R, S):
        M = np.zeros((self.n, 4, 4))
        for i in self.order:
            L = trs(T[i], R[i], S[i])
            M[i] = M[self.parent[i]] @ L if self.parent[i] >= 0 else L
        return M

    def _decode(self, name):
        if name in self._cache:
            return self._cache[name]
        a = self.clips[name]
        K = max(self.g.j["accessors"][s["input"]]["count"] for s in a["samplers"])
        times = None
        T = np.repeat(self.T0[None], K, 0)
        R = np.repeat(self.R0[None], K, 0)
        S = np.repeat(self.S0[None], K, 0)
        for ch in a["channels"]:
            s = a["samplers"][ch["sampler"]]
            node, path = ch["target"]["node"], ch["target"]["path"]
            out = self.g.acc(s["output"])
            if out.shape[0] == K:
                if times is None:
                    times = self.g.acc(s["input"])[:, 0].astype(float)
                v = out
            else:   # a held channel (two STEP keys)
                v = np.repeat(out[:1], K, 0)
            {"translation": T, "rotation": R, "scale": S}[path][:, node] = v
        if times is None:
            times = np.arange(1, K + 1) / FPS
        self._cache[name] = (times, T, R, S)
        return self._cache[name]

    def length(self, name):
        times = self._decode(name)[0]
        return float(times[-1])

    def sample(self, name, t, loop=True):
        """Local T, R, S of every node at clip second t. The pack's clips are keyed 1/30 .. L, frame 0 = frame L."""
        times, T, R, S = self._decode(name)
        K = len(times)
        dt = 1.0 / FPS
        f = t / dt - 1.0     # key index (key 0 sits at 1/30 s)
        if loop:
            f = f % K
            i0 = int(math.floor(f)) % K
            i1 = (i0 + 1) % K
        else:
            f = min(max(f, 0.0), K - 1.0)
            i0 = int(math.floor(f))
            i1 = min(i0 + 1, K - 1)
        u = f - math.floor(f)
        Tt = T[i0] * (1 - u) + T[i1] * u
        St = S[i0] * (1 - u) + S[i1] * u
        q0, q1 = R[i0], R[i1].copy()
        flip = np.sum(q0 * q1, axis=1) < 0
        q1[flip] *= -1
        Rt = q0 * (1 - u) + q1 * u
        Rt /= np.linalg.norm(Rt, axis=1)[:, None]
        return Tt, Rt, St

    def locals_of(self, M):
        """Model matrices -> local translation and rotation of every node against its real parent."""
        T = np.zeros((self.n, 3))
        R = np.zeros((self.n, 4))
        for i in range(self.n):
            L = np.linalg.inv(M[self.parent[i]]) @ M[i] if self.parent[i] >= 0 else M[i]
            A = L[:3, :3]
            U, _, Vt = np.linalg.svd(A)
            Rm = U @ Vt
            if np.linalg.det(Rm) < 0:
                U[:, -1] *= -1
                Rm = U @ Vt
            T[i] = L[:3, 3]
            R[i] = m2q(Rm)
        return T, R

    def mirror(self, M):
        """The left-right mirror image of a pose: joint j does what its partner did, reflected in the body's
        midplane (the rest correction keeps each joint's own bind frame)."""
        Sx = np.diag([-1.0, 1.0, 1.0, 1.0])
        out = np.zeros_like(M)
        for i in range(self.n):
            out[i] = Sx @ M[self.mate[i]] @ Sx @ self.mirC[i]
        return out


# ------------------------------------------------------------------------------------------------ a pose
SPINE = ["spine_01.x", "spine_02.x", "spine_03.x", "spine_04.x", "spine_05.x"]
HEAD = ["head.x", "Eye_L", "Eye_ball_L", "Eye_R", "Eye_ball_R", "Mouth", "Antenna", "Antenna.001", "Antenna.002", "Antenna.003"]
FOOT_Z = -0.044     # the bind pose's ankles (z); its shoulders are at x +-0.126 y 1.136, arm reach 0.44 m
HANG = {"at": [0.07, -0.41, 0.05], "pole": [0.4, 0.0, -1.0]}


class Pose:
    """What a clip asks of the body at one instant, over the bind pose (upright, symmetric, feet side by side).

    Sides are written once: `out` is away from the body's midline for either arm or leg.
      arm[s] = {"at": [out, up, fwd] from its own shoulder, carried by the chest ("space": "model" = an absolute
                [out, y, fwd]: a rail, a knee), "pole": [out, up, fwd] the way the elbow points,
                "hand_r": [rx, ry, rz] the hand turned on the wrist}
      leg[s] = {"at": [out, lift, fwd] the ankle on the floor plan (absent = the bind pose's), "yaw": toes out (deg),
                "pitch": heel up (deg), "pole": the way the knee points, "ride": 1 = the foot goes with the hips}
    """

    def __init__(self, bt=0.0):
        self.bt = bt                           # Idle's clip second, for the fingers (the bind pose splays them)
        self.hips_t = [0.0, 0.0, 0.0]          # hips moved (m)
        self.hips_r = [0.0, 0.0, 0.0]          # hips turned (deg)
        self.spine = [0.0, 0.0, 0.0]           # the whole spine's bend (deg), shared over its five joints
        self.neck = [0.0, 0.0, 0.0]
        self.head = [0.0, 0.0, 0.0]
        self.arm = {"l": dict(HANG), "r": dict(HANG)}
        self.leg = {"l": {}, "r": {}}
        self.antenna = 0.0                     # the antenna tipped back (deg): follow-through


def solve(rig, pose, mirror=False):
    T, R, S = rig.T0.copy(), rig.R0.copy(), rig.S0.copy()
    Ti, Ri, _ = rig.sample("Idle", pose.bt)
    for i in rig.fingers:
        R[i] = Ri[i]
    Mb = rig.model(T, R, S)
    ix = rig.idx

    def P(name):
        return Mb[ix[name]][:3, 3].copy()

    D = {}
    hp = P("root.x")
    Dh = shift(pose.hips_t) @ rot_about(hp, euler(pose.hips_r))
    D["root.x"] = Dh
    prev = Dh
    share = [0.15, 0.2, 0.25, 0.2, 0.2]
    for k, nm in enumerate(SPINE):
        p = apply(prev, P(nm))
        Rp = prev[:3, :3]
        Rk = Rp @ euler([a * share[k] for a in pose.spine]) @ Rp.T
        prev = rot_about(p, Rk) @ prev
        D[nm] = prev
    Dc = prev
    for nm in ("shoulder.l", "shoulder.r", "Backpack"):
        D[nm] = Dc
    p = apply(Dc, P("neck.x"))
    Rp = Dc[:3, :3]
    Dn = rot_about(p, Rp @ euler(pose.neck) @ Rp.T) @ Dc
    D["neck.x"] = D["subneck_twist_1.x"] = Dn
    p = apply(Dn, P("head.x"))
    Rp = Dn[:3, :3]
    Dhd = rot_about(p, Rp @ euler(pose.head) @ Rp.T) @ Dn
    for nm in HEAD:
        D[nm] = Dhd
    if pose.antenna:
        p = apply(Dhd, P("Antenna"))
        Rp = Dhd[:3, :3]
        Da = rot_about(p, Rp @ euler([-pose.antenna, 0, 0]) @ Rp.T) @ Dhd
        for nm in HEAD[6:]:
            D[nm] = Da
    for s in "lr":
        sg = 1.0 if s == "l" else -1.0
        up = ["c_arm_twist_offset." + s, "arm_twist_2." + s, "arm_stretch." + s]
        fo = ["forearm_stretch." + s, "forearm_twist." + s, "forearm_twist_2." + s]
        S0, E0, W0 = P(up[0]), P(fo[0]), P("hand." + s)
        arm = pose.arm[s]
        a, c = np.linalg.norm(E0 - S0), np.linalg.norm(W0 - E0)
        S1 = apply(Dc, S0)
        at = arm["at"]
        pole = arm.get("pole", [0.4, -0.2, -1.0])
        pole = np.array([sg * pole[0], pole[1], pole[2]])
        if arm.get("space") == "model":
            Wt = np.array([sg * at[0], at[1], at[2]])
        else:
            Wt = apply(Dc, S0 + np.array([sg * at[0], at[1], at[2]]))
            pole = Dc[:3, :3] @ pole
        E1, W1 = two_bone(S1, Wt, a, c, pole)
        Du = rot_about(S1, minrot(apply(Dc, E0) - S1, E1 - S1)) @ Dc
        Df = rot_about(E1, minrot(apply(Du, W0) - E1, W1 - E1)) @ Du
        Dha = Df
        if arm.get("hand_r"):
            hr = arm["hand_r"]
            Rf = Df[:3, :3]
            Dha = rot_about(W1, Rf @ euler([hr[0], sg * hr[1], sg * hr[2]]) @ Rf.T) @ Df
        for nm in up:
            D[nm] = Du
        for nm in fo:
            D[nm] = Df
        D["hand." + s] = Dha
        th = ["thigh_twist." + s, "thigh_stretch." + s, "thigh_twist_2." + s]
        sh = ["leg_stretch." + s, "leg_twist." + s, "leg_twist_2." + s]
        H0, K0, A0 = P(th[0]), P(sh[0]), P("foot." + s)
        leg = pose.leg[s]
        a, c = np.linalg.norm(K0 - H0), np.linalg.norm(A0 - K0)
        H1 = apply(Dh, H0)
        ride = leg.get("ride", 0.0)
        At = A0.copy()
        if "at" in leg:
            At = np.array([sg * leg["at"][0], A0[1] + leg["at"][1], leg["at"][2]])
        At = lerp(At, apply(Dh, At), ride)
        pole = leg.get("pole", [0.18, 0.0, 1.0])
        pole = Dh[:3, :3] @ np.array([sg * pole[0], pole[1], pole[2]])
        K1, A1 = two_bone(H1, At, a, c, pole)
        Dt = rot_about(H1, minrot(apply(Dh, K0) - H1, K1 - H1)) @ Dh
        Ds = rot_about(K1, minrot(apply(Dt, A0) - K1, A1 - K1)) @ Dt
        Rf = axis_rot([0, 1, 0], sg * leg.get("yaw", 8.0)) @ axis_rot([1, 0, 0], leg.get("pitch", 0.0))
        Dft = shift(A1 - A0) @ rot_about(A0, Rf)
        for nm in th:
            D[nm] = Dt
        for nm in sh:
            D[nm] = Ds
        D["foot." + s] = D["toes_01." + s] = Dft
    M = Mb.copy()
    for nm, Dm in D.items():
        M[ix[nm]] = Dm @ Mb[ix[nm]]
    # the joints no part claimed (fingers, twist children) ride their real parent
    claimed = {ix[nm] for nm in D}
    for i in rig.order:
        if i not in claimed and rig.parent[i] >= 0:
            M[i] = M[rig.parent[i]] @ trs(T[i], R[i], S[i])
    if mirror:
        M = rig.mirror(M)
    return M


# ------------------------------------------------------------------------------------------------ the clips
CLIPS = {}


def clip(name, kicks, fps=30.0, about="", use=""):
    def reg(fn):
        CLIPS[name] = {"fn": fn, "length": kicks * KICK, "fps": fps, "about": about, "use": use}
        return fn
    return reg


def new(t, kicks):
    """A pose at clip second t of a clip `kicks` long; the fingers take Idle's, a whole number of its cycles."""
    Ln = kicks * KICK
    return Pose((t / Ln) * max(1, round(Ln / 2.0)) * 2.0)


def stand(p, width=0.26, stagger=0.0, toe=10.0, drop=0.03, x=0.0, z=0.0):
    """Feet `width` apart, the left `stagger` ahead of the right; hips dropped `drop` (knees soft), moved x, z."""
    p.hips_t = [x, -drop, z]
    p.leg["l"] = {"at": [width / 2, 0.0, FOOT_Z + stagger / 2], "yaw": toe}
    p.leg["r"] = {"at": [width / 2, 0.0, FOOT_Z - stagger / 2], "yaw": toe}
    return p


def hang(swing=0.0, out=0.0):
    return {"at": [0.07 + out, -0.41, 0.05 + swing], "pole": [0.4, 0.0, -1.0]}


FOLD_L = {"at": [-0.20, -0.17, 0.115], "pole": [0.6, -0.8, 0.3]}
FOLD_R = {"at": [-0.17, -0.22, 0.135], "pole": [0.6, -0.8, 0.3]}
ON_HIP = {"at": [0.07, -0.31, -0.01], "pole": [1.0, 0.1, -0.7]}


# ---- the audience: on the kick (0.70 s). The body is lowest ON the kick (clip second 0, 0.70, ...).
@clip("ND_Clap", 4, about="claps at its chest on every kick, knees dipping, head nodding", use="audience")
def nd_clap(t):
    p = stand(new(t, 4), width=0.24)
    hit = bump(t, KICK, 0.0, 3.0)
    dip = bump(t, KICK, 0.04, 2.0)
    p.hips_t[1] -= 0.035 * dip
    p.hips_t[0] = 0.012 * wave(t, 2 * KICK)
    p.spine = [3.0 + 3.0 * dip, 0.0, 2.0 * wave(t, 2 * KICK)]
    p.head = [6.0 * dip - 3.0, 0.0, 0.0]
    for s in "lr":
        p.arm[s] = {"at": [-0.10 + 0.16 * (1 - hit), -0.10 + 0.02 * (1 - hit), 0.27], "pole": [1.0, -0.6, -0.3]}
    p.antenna = 5.0 * dip
    return p


@clip("ND_Clap_b", 4, about="a big slow clap on every second kick, arms flung wide between, leaning back, rocking", use="audience")
def nd_clap_b(t):
    p = stand(new(t, 4), width=0.38, toe=16.0)
    hit = bump(t, 2 * KICK, 0.5, 4.0)             # the clap on the second kick of each pair (the snare)
    rock = wave(t, 4 * KICK)
    p.hips_t[0] = 0.05 * rock
    p.hips_t[1] -= 0.03 * bump(t, KICK, 0.04, 2.0)
    p.spine = [-7.0 + 6.0 * hit, 0.0, -5.0 * rock]
    p.head = [-6.0 + 4.0 * hit, 0.0, -3.0 * rock]
    for s in "lr":
        p.arm[s] = {"at": lerp([0.26, 0.02, 0.16], [-0.10, 0.03, 0.30], hit), "pole": [0.8, -0.8, -0.3]}
    return p


@clip("ND_Arms_up", 4, about="both arms up in a V, pumping on the kick; bounces from the knees", use="audience")
def nd_arms_up(t):
    p = stand(new(t, 4), width=0.32, toe=14.0)
    dip = bump(t, KICK, 0.04, 2.0)
    p.hips_t[1] -= 0.06 * dip
    p.hips_t[0] = 0.02 * wave(t, 4 * KICK)
    p.spine = [-6.0 + 5.0 * dip, 0.0, 3.0 * wave(t, 4 * KICK)]
    p.head = [-10.0 + 6.0 * dip, 0.0, 0.0]
    for s, ph in (("l", 0.0), ("r", 0.07)):
        k = bump(t, KICK, 0.5 + ph, 1.5)          # highest between kicks
        p.arm[s] = {"at": [0.20 - 0.02 * k, 0.24 + 0.14 * k, 0.02 + 0.06 * (1 - k)], "pole": [1.0, -0.4, -0.2]}
    p.antenna = 8.0 * dip
    return p


@clip("ND_Fist_pump", 4, about="one fist punching the air on every kick, body turned a quarter, the other arm low", use="audience")
def nd_fist_pump(t):
    p = stand(new(t, 4), width=0.26, stagger=0.22, toe=14.0)
    hit = bump(t, KICK, 0.0, 2.0)
    dip = bump(t, KICK, 0.06, 2.0)
    p.hips_t[1] -= 0.04 * dip
    p.hips_r = [0.0, -14.0, 0.0]
    p.spine = [2.0, 8.0 * hit - 6.0, -3.0]
    p.head = [-5.0 + 5.0 * dip, 14.0, 0.0]
    p.arm["l"] = {"at": [0.10, 0.10 + 0.30 * hit, 0.20 - 0.12 * hit], "pole": [0.9, -0.6, -0.3]}
    p.arm["r"] = {"at": [0.12, -0.30 + 0.03 * dip, 0.14], "pole": [0.6, 0.0, -1.0]}
    return p


@clip("ND_Wave", 8, about="arms high, swaying side to side like a lighter wave", use="audience")
def nd_wave(t):
    p = stand(new(t, 8), width=0.30, toe=12.0)
    sw = wave(t, 4 * KICK)
    p.hips_t[0] = 0.05 * sw
    p.hips_t[1] -= 0.015 * abs(sw)
    p.spine = [-4.0, 0.0, -8.0 * sw]
    p.head = [-8.0, 0.0, -4.0 * sw]
    late = wave(t, 4 * KICK, -0.07)
    p.arm["l"] = {"at": [0.18 + 0.13 * late, 0.36 - 0.05 * abs(late), 0.02], "pole": [1.0, -0.4, -0.2]}
    p.arm["r"] = {"at": [0.18 - 0.13 * late, 0.36 - 0.05 * abs(late), 0.02], "pole": [1.0, -0.4, -0.2]}
    return p


@clip("ND_Sway", 8, about="a two-step: weight from foot to foot on the kick, elbows bent, shoulders rolling", use="audience")
def nd_sway(t):
    p = stand(new(t, 8), width=0.34, toe=14.0)
    sw = wave(t, 2 * KICK, 0.25)                  # full to one side on each kick
    dip = bump(t, KICK, 0.04, 2.0)
    p.hips_t[0] = 0.075 * sw
    p.hips_t[1] -= 0.03 * dip
    p.hips_r = [0.0, 7.0 * sw, -4.0 * sw]
    p.spine = [3.0, -12.0 * sw, 6.0 * sw]
    p.head = [3.0 * dip, 5.0 * sw, -3.0 * sw]
    for s in "lr":
        sg = 1 if s == "l" else -1
        p.leg[s]["at"][1] = 0.05 * max(0.0, -sg * sw) ** 2     # the unweighted foot lifts
        p.leg[s]["pitch"] = 14.0 * max(0.0, -sg * sw) ** 2
        p.arm[s] = {"at": [0.10, -0.20 + 0.06 * sg * sw, 0.22 + 0.07 * sg * sw], "pole": [0.7, -0.2, -1.0]}
    return p


@clip("ND_Nod", 4, about="arms folded, weight on one leg, nodding on the kick, a toe tapping", use="audience")
def nd_nod(t):
    p = stand(new(t, 4), width=0.28, stagger=0.10, toe=14.0, x=-0.04)
    nod = bump(t, KICK, 0.03, 2.5)
    p.hips_t[1] -= 0.01 * nod
    p.hips_r = [0.0, 0.0, 4.0]
    p.spine = [2.0 + 2.0 * nod, 0.0, -5.0]
    p.neck = [6.0 * nod, 0.0, 0.0]
    p.head = [10.0 * nod - 3.0, 0.0, 2.0]
    p.arm["l"], p.arm["r"] = dict(FOLD_L), dict(FOLD_R)
    p.leg["l"]["pitch"] = -16.0 * bump(t, KICK, 0.55, 2.0)    # the toe lifts between kicks, down on the kick
    p.antenna = 6.0 * nod
    return p


@clip("ND_Jump", 4, about="a bounce: a small hop between kicks, landing on each one, its arms up and loose", use="audience")
def nd_jump(t):
    # it14 (owner msgs 195, 202): the hop is 0.15 m (was 0.24) and the arms stay up and ride it on a raised cosine; it12
    # threw them from the waist to over the head in 0.11 s of every jump (hands 11 m/s), which read as a twitch
    p = stand(new(t, 4), width=0.24)
    u = (t / KICK) % 1.0                          # 0 = the kick (the landing's deepest point)
    air = math.sin(math.pi * (u - 0.2) / 0.62) if 0.2 < u < 0.82 else 0.0
    crouch = (1 - smooth(u / 0.2) if u < 0.2 else 0.0) + (smooth((u - 0.82) / 0.18) if u > 0.82 else 0.0)
    up = bump(t, KICK, 0.5, 1.0)                  # highest between kicks
    p.hips_t[1] += 0.15 * air - 0.07 * crouch
    soft = air * air                              # the head and back ease into the hop (no kink at the push-off)
    p.spine = [6.0 * crouch - 3.0 * soft, 0.0, 0.0]
    p.head = [-6.0 * soft + 3.0 * crouch, 0.0, 0.0]
    for s in "lr":
        p.leg[s]["at"][1] = 0.12 * air
        p.leg[s]["pitch"] = 24.0 * soft
        p.arm[s] = {"at": lerp([0.15, 0.16, 0.10], [0.19, 0.36, 0.03], up), "pole": [1.0, -0.4, -0.3]}
    p.antenna = -10.0 * soft + 8.0 * crouch
    return p


@clip("ND_Groove", 4, about="knees bouncing, fists pumping in turn at its chest, shoulders twisting", use="audience")
def nd_groove(t):
    p = stand(new(t, 4), width=0.30, toe=16.0)
    dip = bump(t, KICK, 0.04, 2.0)
    tw = wave(t, 2 * KICK, 0.25)
    p.hips_t[1] -= 0.055 * dip + 0.015
    p.hips_r = [0.0, -6.0 * tw, 0.0]
    p.spine = [6.0, 16.0 * tw, 0.0]
    p.head = [4.0 * dip - 2.0, -8.0 * tw, 0.0]
    for s in "lr":
        sg = 1 if s == "l" else -1
        f = 0.5 + 0.5 * sg * tw                   # this fist forward when the shoulders turn it there
        p.arm[s] = {"at": [0.06, -0.20 + 0.14 * f, 0.20 + 0.10 * f], "pole": [0.8, -0.4, -0.8]}
    p.antenna = 6.0 * dip
    return p


@clip("ND_Twist", 8, about="the twist: hips one way, shoulders the other, down low and up again over four kicks", use="audience")
def nd_twist(t):
    p = stand(new(t, 8), width=0.30, toe=4.0)
    tw = wave(t, KICK)
    low = 0.5 - 0.5 * math.cos(2 * math.pi * t / (4 * KICK))
    # it14 (msgs 195, 202): three quarters of the it12 twist (shoulders 26 deg either way, was 34) and a shallower dip
    p.hips_t[1] -= 0.02 + 0.07 * low
    p.hips_r = [0.0, 13.0 * tw, 0.0]
    p.spine = [5.0 + 6.0 * low, -26.0 * tw, 0.0]
    p.head = [-3.0 * low, 11.0 * tw, 0.0]
    for s in "lr":
        sg = 1 if s == "l" else -1
        p.leg[s]["yaw"] = 4.0 + sg * 12.0 * tw
        p.arm[s] = {"at": [0.14, -0.22, 0.16 - 0.10 * sg * tw], "pole": [0.9, -0.2, -0.7]}
    return p


def accent(t):
    """1 on every second kick, 0.6 on the ones between (a bar's strong and weak kick), smoothly."""
    return 0.8 + 0.2 * math.cos(2 * math.pi * t / (2 * KICK))


@clip("ND_Headbang", 4, about="a wide stance, fists low, bowing from the waist on every kick: a full bow, then a lighter one; smooth",
      use="audience")
def nd_headbang(t):
    # it14 (owner msg 195's mark at 44.1 s, msg 202 "excessive twitchiness in the headbanging"): a plain raised cosine
    # (it12 sharpened it to a snap), half the bow (33 deg head to waist on the strong kick, 20 on the weak; was 64 on
    # every kick): the head's peak speed is 150 deg/s (was 390)
    p = stand(new(t, 4), width=0.44, toe=20.0, drop=0.06)
    hit = bump(t, KICK, 0.02, 1.0) * accent(t)
    p.hips_t[1] -= 0.02 * hit
    p.hips_t[2] = -0.02 * hit
    p.spine = [-4.0 + 15.0 * hit, 0.0, 0.0]
    p.neck = [6.0 * hit, 0.0, 0.0]
    p.head = [-4.0 + 12.0 * hit, 0.0, 2.0 * wave(t, 2 * KICK)]
    for s in "lr":
        p.arm[s] = {"at": [0.12, -0.30 + 0.03 * hit, 0.14 + 0.04 * hit], "pole": [1.0, 0.0, -0.6]}
    p.antenna = -9.0 * hit + 3.0
    return p


@clip("ND_Rock", 4, about="rocks out: one fist held high and pulsing on the kick, the head nodding with it, the other fist low",
      use="audience")
def nd_rock(t):
    # it14 (msg 200 "aliens headbanging ..."; msg 202): a second, lighter headbanger for the figures the pack's Crazy
    # and Fight_idle had (Crazy is not a loop: it snaps every cycle)
    p = stand(new(t, 4), width=0.40, stagger=0.12, toe=18.0, drop=0.05)
    hit = bump(t, KICK, 0.02, 1.0) * accent(t)
    p.hips_t[1] -= 0.03 * hit
    p.hips_r = [0.0, -8.0, 0.0]
    p.spine = [-2.0 + 9.0 * hit, 5.0, -2.0]
    p.neck = [5.0 * hit, 0.0, 0.0]
    p.head = [-5.0 + 11.0 * hit, 6.0, 0.0]
    p.arm["l"] = {"at": [0.14, 0.34 - 0.06 * hit, 0.06 + 0.05 * hit], "pole": [1.0, -0.4, -0.3]}
    p.arm["r"] = {"at": [0.12, -0.28 + 0.03 * hit, 0.14], "pole": [0.8, 0.0, -0.8]}
    p.antenna = -7.0 * hit + 3.0
    return p


@clip("ND_Point", 8, about="points at the stage, jabbing on the kick, then turns to the one beside it and back; a hand on its hip", use="audience")
def nd_point(t):
    Ln = 8 * KICK
    p = stand(new(t, 8), width=0.28, stagger=0.14, toe=12.0)
    out, turn = keyed(t, [(0.0, [1.0, 0.0]), (2.4, [1.0, 0.0]), (3.1, [0.6, 1.0]), (4.3, [0.6, 1.0]), (5.0, [1.0, 0.0])], Ln)
    jab = 0.03 * bump(t, KICK, 0.0, 3.0) * out
    p.spine = [4.0, 8.0 - 16.0 * turn, 0.0]
    p.head = [-2.0, 4.0 - 34.0 * turn, 0.0]
    p.arm["l"] = {"at": [0.02, 0.10 * out - 0.04, 0.20 + 0.21 * out + jab], "pole": [0.5, -1.0, 0.0]}
    p.arm["r"] = dict(ON_HIP)
    return p


@clip("ND_Film", 16, fps=20.0, about="holds both hands up before its face (filming the stage), steadying, peeking round them", use="audience")
def nd_film(t):
    Ln = 16 * KICK
    p = stand(new(t, 16), width=0.24, toe=8.0)
    dx, dy = 0.012 * wave(t, Ln / 2), 0.010 * wave(t, Ln / 3, 0.2)
    peek = float(keyed(t, [(0.0, 0.0), (5.0, 0.0), (5.8, 1.0), (7.4, 1.0), (8.2, 0.0)], Ln))
    p.hips_t[0] = 0.02 * wave(t, Ln)
    p.spine = [-3.0, 3.0 * wave(t, Ln), 0.0]
    p.head = [-4.0, -10.0 * peek, 6.0 * peek]
    p.arm["l"] = {"at": [-0.05 + dx, 0.21 + dy, 0.36], "pole": [0.7, -0.8, 0.0]}
    p.arm["r"] = {"at": [-0.05 - dx, 0.21 + dy, 0.36], "pole": [0.7, -0.8, 0.0]}
    return p


@clip("ND_Cheer", 4, about="a cheer: both fists up, punching the air in turn, rising on its toes between kicks", use="audience, a crash")
def nd_cheer(t):
    # it14 (msgs 195, 202): it12 shook the fists, the shoulders and the head at 5.7 Hz and hopped 9 cm on every kick
    # (a jitter at 30 fps); now the fists go up in turn over two kicks and the body rises 4 cm between kicks
    p = stand(new(t, 4), width=0.34, toe=16.0)
    up = bump(t, KICK, 0.5, 1.0)                  # highest between kicks, down on the kick
    sw = wave(t, 2 * KICK, 0.25)                  # full to one side on each kick
    p.hips_t[1] += 0.04 * up - 0.03
    p.hips_t[0] = 0.02 * sw
    p.spine = [-8.0, 0.0, 3.0 * sw]
    p.head = [-12.0, 0.0, 2.0 * sw]
    for s in "lr":
        sg = 1 if s == "l" else -1
        k = 0.5 + 0.5 * sg * sw                   # this fist highest when the body leans its way
        p.leg[s]["pitch"] = 10.0 * up
        p.arm[s] = {"at": [0.20, 0.28 + 0.10 * k, 0.02], "pole": [1.0, -0.4, -0.2]}
    p.antenna = -5.0 * up
    return p


# ---- standing about: free-running (11.2 s loops; a rate between 1.0 and 1.15 and any phase)
@clip("ND_Stand", 16, fps=20.0, about="upright and easy: weight on one leg, arms loose, a slow look round, then the weight to the other leg", use="anywhere")
def nd_stand(t):
    Ln = 16 * KICK
    side = float(keyed(t, [(0.0, 1.0), (4.6, 1.0), (5.8, -1.0), (9.6, -1.0), (10.8, 1.0)], Ln))
    look = float(keyed(t, [(0.0, 0.0), (1.2, 0.0), (2.2, 1.0), (3.8, 1.0), (4.8, 0.0), (6.6, 0.0), (7.6, -1.0), (9.0, -1.0), (10.0, 0.0)], Ln))
    p = stand(new(t, 16), width=0.25, toe=12.0, drop=0.02, x=0.045 * side)
    p.hips_r = [0.0, 0.0, -4.0 * side]
    p.spine = [1.0 * wave(t, Ln / 4), 6.0 * look, 5.0 * side]
    p.head = [0.0, 26.0 * look, -2.0 * side]
    p.arm["l"], p.arm["r"] = hang(0.03 * side), hang(-0.03 * side)
    for s in "lr":
        sg = 1 if s == "l" else -1
        p.leg[s]["pitch"] = 8.0 * max(0.0, -sg * side)
    return p


@clip("ND_Talk_a", 16, fps=20.0, about="talking: its hands open and close the points it makes, head tilting, weight shifting", use="pairs and groups")
def nd_talk_a(t):
    Ln = 16 * KICK
    p = stand(new(t, 16), width=0.27, stagger=0.08, toe=12.0)
    g = float(keyed(t, [(0.0, 0.0), (0.6, 1.0), (1.3, 0.4), (1.9, 1.0), (2.8, 0.1), (3.6, 0.0), (4.1, 0.8), (4.9, 0.2), (6.4, 0.0), (7.0, 1.0),
                        (7.7, 0.5), (8.3, 1.0), (9.3, 0.0)], Ln))
    g2 = float(keyed(t, [(0.0, 0.0), (2.9, 0.0), (3.5, 1.0), (4.4, 0.6), (5.0, 1.0), (5.9, 0.0), (8.0, 0.0), (8.6, 0.9), (9.6, 0.0)], Ln))
    p.hips_t[0] = 0.03 * wave(t, Ln)
    p.hips_r = [0.0, 4.0 * wave(t, Ln), 0.0]
    p.spine = [2.0 + 4.0 * g, 5.0 * wave(t, Ln / 2), 0.0]
    p.head = [3.0 * wave(t, Ln / 4) + 4.0 * g, 6.0 * wave(t, Ln / 2, 0.1), 5.0 * wave(t, Ln / 3)]
    p.arm["l"] = {"at": lerp([0.07, -0.40, 0.06], [0.08, -0.08, 0.30], g), "pole": [0.6, -0.5, -0.7]}
    p.arm["r"] = {"at": lerp([0.07, -0.40, 0.06], [0.03, -0.12, 0.28], g2), "pole": [0.6, -0.5, -0.7]}
    return p


@clip("ND_Talk_b", 16, fps=20.0, about="listening: a hand on its hip, nodding now and then, laughing back once", use="pairs and groups")
def nd_talk_b(t):
    Ln = 16 * KICK
    p = stand(new(t, 16), width=0.30, toe=14.0, x=-0.04)
    nod = sum(bump(t - t0, 0.5, 0.5, 2.0) if t0 <= t <= t0 + 0.5 else 0.0 for t0 in (1.0, 1.5, 6.2, 6.7, 7.2))
    laugh = float(keyed(t, [(0.0, 0.0), (3.4, 0.0), (3.8, 1.0), (4.8, 1.0), (5.4, 0.0)], Ln))
    sh = laugh * wave(t, 0.23)
    p.hips_r = [0.0, 0.0, 4.0]
    p.spine = [1.0 - 10.0 * laugh + 2.0 * sh, 0.0, -5.0]
    p.head = [9.0 * nod - 12.0 * laugh + 3.0 * sh, 5.0 * wave(t, Ln / 2), -3.0]
    p.arm["l"] = dict(ON_HIP)
    p.arm["r"] = {"at": lerp([0.07, -0.41, 0.05], [-0.02, -0.02, 0.26], laugh * 0.9), "pole": [0.7, -0.5, -0.6]}
    return p


@clip("ND_Watch", 16, fps=20.0, about="arms folded, weight on one leg, looking slowly from one side to the other", use="anywhere")
def nd_watch(t):
    Ln = 16 * KICK
    p = stand(new(t, 16), width=0.30, stagger=-0.10, toe=16.0, x=0.05, drop=0.025)
    look = float(keyed(t, [(0.0, -1.0), (2.0, -1.0), (3.4, 1.0), (7.6, 1.0), (9.0, -1.0)], Ln))
    p.hips_r = [0.0, 0.0, -5.0]
    p.spine = [-3.0 + 1.0 * wave(t, Ln / 4), 5.0 * look, 6.0]
    p.head = [-3.0, 18.0 * look, 0.0]
    p.arm["l"], p.arm["r"] = dict(FOLD_L), dict(FOLD_R)
    p.leg["r"]["pitch"] = 10.0
    return p


@clip("ND_Hips", 16, fps=20.0, about="hands on its hips, feet apart, looking round and up, rocking on its heels", use="anywhere")
def nd_hips(t):
    Ln = 16 * KICK
    p = stand(new(t, 16), width=0.40, toe=20.0, drop=0.02)
    yaw, pit = keyed(t, [(0.0, [0.0, 0.0]), (1.5, [24.0, -6.0]), (4.0, [24.0, -6.0]), (5.5, [-20.0, -16.0]), (8.5, [-20.0, -16.0]),
                         (10.0, [0.0, 0.0])], Ln)
    rock = wave(t, Ln / 4)
    p.hips_t[2] = 0.02 * rock
    p.spine = [-4.0 - 2.0 * rock, 0.3 * yaw, 0.0]
    p.head = [pit, 0.7 * yaw, 0.0]
    p.arm["l"], p.arm["r"] = dict(ON_HIP), dict(ON_HIP)
    return p


@clip("ND_Look_up", 16, fps=20.0, about="head back, a hand shading its eyes, following something across the sky", use="moon, lantern and mast watchers")
def nd_look_up(t):
    Ln = 16 * KICK
    p = stand(new(t, 16), width=0.28, stagger=0.12, toe=12.0, z=-0.02)
    pan = wave(t, Ln)
    p.spine = [-12.0, 8.0 * pan, 0.0]
    p.neck = [-10.0, 0.0, 0.0]
    p.head = [-22.0, 14.0 * pan, 0.0]
    p.arm["l"] = {"at": [0.03, 0.27, 0.31], "pole": [1.0, -0.3, 0.0]}
    p.arm["r"] = hang(0.02 * pan)
    return p


@clip("ND_Stretch", 16, fps=20.0, about="a long stretch: arms up and back, on its toes, then down, and a look round", use="anywhere, sparingly")
def nd_stretch(t):
    Ln = 16 * KICK
    p = stand(new(t, 16), width=0.26, toe=12.0)
    up = float(keyed(t, [(0.0, 0.0), (1.0, 0.0), (2.6, 1.0), (4.6, 1.0), (5.8, 0.0)], Ln))
    look = float(keyed(t, [(0.0, 0.0), (6.6, 0.0), (7.6, 1.0), (8.8, 1.0), (9.8, -0.6), (10.6, 0.0)], Ln))
    p.hips_t[1] += 0.035 * up
    p.spine = [-14.0 * up, 6.0 * look, 0.0]
    p.head = [-16.0 * up, 24.0 * look, 0.0]
    for s in "lr":
        p.leg[s]["at"][1] = 0.03 * up
        p.leg[s]["pitch"] = 16.0 * up
        p.arm[s] = {"at": lerp([0.07, -0.41, 0.05], [0.16, 0.39, -0.06], up), "pole": lerp([0.4, 0.0, -1.0], [1.0, -0.3, -0.3], up)}
    return p


@clip("ND_Hello", 8, about="waves a raised hand at whoever is passing, the other on its hip, rising on its toes", use="near the lens's path")
def nd_hello(t):
    Ln = 8 * KICK
    p = stand(new(t, 8), width=0.28, toe=14.0)
    on = float(keyed(t, [(0.0, 0.0), (0.5, 1.0), (4.0, 1.0), (4.7, 0.0)], Ln))
    wv = wave(t, KICK) * on
    p.hips_t[1] += 0.02 * on
    p.spine = [-4.0 * on, 0.0, -4.0 * on]
    p.head = [-6.0 * on, 0.0, -3.0 * on]
    p.arm["l"] = {"at": lerp([0.07, -0.41, 0.05], [0.20 + 0.10 * wv, 0.36, 0.06], on), "pole": [1.0, -0.5, -0.2]}
    p.arm["r"] = dict(ON_HIP) if on > 0.5 else {"at": lerp([0.07, -0.41, 0.05], ON_HIP["at"], on * 2), "pole": lerp([0.4, 0.0, -1.0], ON_HIP["pole"], on * 2)}
    return p


@clip("ND_Lean", 16, fps=20.0, about="leaning on a rail (its forearms 1.0 m up, 0.35 m ahead of its spot), a knee slack; looks along the water", use="rails")
def nd_lean(t):
    Ln = 16 * KICK
    p = stand(new(t, 16), width=0.26, stagger=-0.26, toe=10.0, z=-0.10, drop=0.05)
    look = float(keyed(t, [(0.0, 0.0), (3.0, 0.0), (4.2, 1.0), (7.5, 1.0), (8.6, -0.5), (10.4, -0.5)], Ln))
    sh = wave(t, Ln)
    p.hips_t[0] = 0.02 * sh
    p.hips_r = [10.0, 0.0, 2.0 * sh]
    p.spine = [24.0, 0.0, 0.0]
    p.neck = [-12.0, 0.0, 0.0]
    p.head = [-14.0, 22.0 * look, 0.0]
    for s in "lr":
        p.arm[s] = {"at": [0.07, 1.00, 0.36], "space": "model", "pole": [1.0, -0.5, -0.3]}
    p.leg["l"]["pitch"] = 18.0
    return p


@clip("ND_Sit", 16, fps=20.0, about="sitting on the ground, knees up, hands on them, rocking a little, looking about", use="edges, quiet corners")
def nd_sit(t):
    Ln = 16 * KICK
    p = new(t, 16)
    rock = wave(t, Ln / 2)
    look = float(keyed(t, [(0.0, 0.0), (2.5, 0.0), (3.6, 1.0), (6.5, 1.0), (7.6, -0.7), (10.0, -0.7)], Ln))
    p.hips_t = [0.0, -0.665, -0.12]
    p.hips_r = [-14.0 + 2.0 * rock, 0.0, 0.0]
    p.spine = [30.0 - 3.0 * rock, 0.0, 0.0]
    p.neck = [-8.0, 0.0, 0.0]
    p.head = [-10.0, 28.0 * look, 0.0]
    for s in "lr":
        p.leg[s] = {"at": [0.15, 0.0, 0.26], "pole": [0.3, 1.0, 0.2], "yaw": 14.0}
        p.arm[s] = {"at": [0.17, 0.44, 0.17], "space": "model", "pole": [1.0, 0.1, -0.4]}
    return p


@clip("ND_Squat", 16, fps=20.0, about="squatting on its heels, forearms on its knees, poking at something on the ground now and then", use="gardeners, fishers, kids at the front")
def nd_squat(t):
    Ln = 16 * KICK
    p = stand(new(t, 16), width=0.34, toe=24.0)
    poke = float(keyed(t, [(0.0, 0.0), (2.0, 0.0), (2.8, 1.0), (3.4, 0.6), (4.0, 1.0), (5.0, 0.0)], Ln))
    look = float(keyed(t, [(0.0, 0.0), (6.0, 0.0), (7.0, 1.0), (9.0, 1.0), (10.0, 0.0)], Ln))
    p.hips_t = [0.0, -0.44, -0.16]
    p.hips_r = [18.0, 0.0, 0.0]
    p.spine = [16.0 + 10.0 * poke, 0.0, 0.0]
    p.head = [-16.0 + 14.0 * poke, 26.0 * look, 0.0]
    for s in "lr":
        p.leg[s]["pole"] = [0.5, 0.2, 1.0]
        p.leg[s]["pitch"] = 12.0
    p.arm["l"] = {"at": [0.20, 0.50, 0.22], "space": "model", "pole": [1.0, 0.2, -0.3]}
    p.arm["r"] = {"at": lerp([0.20, 0.50, 0.22], [0.08, 0.16, 0.42], poke), "space": "model", "pole": [1.0, 0.2, -0.3]}
    return p


@clip("ND_Fish", 16, fps=20.0, about="fishing: both hands hold a line out ahead, a slow lift, two tugs, a look down at the water", use="fishers at an edge")
def nd_fish(t):
    Ln = 16 * KICK
    p = stand(new(t, 16), width=0.30, stagger=0.20, toe=12.0)
    lift = float(keyed(t, [(0.0, 0.0), (4.0, 0.0), (5.2, 1.0), (5.6, 0.5), (6.0, 1.0), (6.6, 0.4), (8.0, 0.0)], Ln))
    p.spine = [10.0 - 9.0 * lift, 0.0, 0.0]
    p.head = [12.0 - 8.0 * lift, 6.0 * wave(t, Ln), 0.0]
    p.arm["l"] = {"at": [-0.06, -0.12 + 0.16 * lift, 0.36 - 0.07 * lift], "pole": [0.8, -0.7, -0.2]}
    p.arm["r"] = {"at": [-0.08, -0.20 + 0.16 * lift, 0.30 - 0.07 * lift], "pole": [0.8, -0.7, -0.2]}
    return p


# stock standing clips worth a mirrored copy (a left-handed guard, a look over the other shoulder, the other hand)
MIRRORED_STOCK = ["Idle", "Idle_turn", "Fight_idle", "Crazy", "Button_push", "Take_from_table", "Take_from_floor"]
# authored clips that are one-sided: a mirrored copy each ("_m")
MIRRORED_ND = ["ND_Fist_pump", "ND_Point", "ND_Nod", "ND_Stand", "ND_Talk_a", "ND_Talk_b", "ND_Watch", "ND_Look_up", "ND_Hello",
               "ND_Lean", "ND_Fish", "ND_Squat", "ND_Rock"]


def continuous(Rk):
    for k in range(1, Rk.shape[0]):
        flip = np.sum(Rk[k] * Rk[k - 1], axis=1) < 0
        Rk[k][flip] *= -1
    return Rk


def bake(rig, name):
    """(times, T[k, node], R[k, node]) of one new clip."""
    if name.endswith("_m") and name[:-2] in CLIPS:
        spec, mirror = CLIPS[name[:-2]], True
    elif name.endswith("_m"):
        src = name[:-2]
        times = rig._decode(src)[0]
        Tk, Rk = [], []
        for t in times:
            T, R, S = rig.sample(src, t, loop=False)
            Tl, Rl = rig.locals_of(rig.mirror(rig.model(T, R, S)))
            Tk.append(Tl)
            Rk.append(Rl)
        return times.astype(np.float32), np.array(Tk), continuous(np.array(Rk))
    else:
        spec, mirror = CLIPS[name], False
    n = int(round(spec["length"] * spec["fps"]))
    times = np.array([spec["length"] * k / n for k in range(n + 1)])
    Tk, Rk = [], []
    for t in times[:-1]:
        Tl, Rl = rig.locals_of(solve(rig, spec["fn"](float(t)), mirror))
        Tk.append(Tl)
        Rk.append(Rl)
    Tk.append(Tk[0])       # the loop closes on its first pose
    Rk.append(Rk[0])
    return times.astype(np.float32), np.array(Tk), continuous(np.array(Rk))


def all_names():
    return list(CLIPS) + [n + "_m" for n in MIRRORED_ND] + [n + "_m" for n in MIRRORED_STOCK]


def about(name):
    if name in CLIPS:
        return CLIPS[name]["about"]
    if name.endswith("_m") and name[:-2] in CLIPS:
        return CLIPS[name[:-2]]["about"] + " (mirrored: the other hand, the other foot)"
    return "the pack's %s, mirrored" % name[:-2]


def build_all(casts, out_dir=None, only=None):
    """Bake every clip once (the six files share one skeleton: checked), then append to each file by joint name."""
    names = [n for n in all_names() if not only or n in only]
    first = Rig(GLB(os.path.join(ALIENS, casts[0] + ".glb")))
    baked = {}
    for name in names:
        times, Tk, Rk = bake(first, name)
        baked[name] = (times, {first.nodes[i]["name"]: (Tk[:, i], Rk[:, i]) for i in first.joints})
    table = {n: round(float(baked[n][0][-1]), 4) for n in names}
    for cast in casts:
        g = GLB(os.path.join(ALIENS, cast + ".glb"))
        rig = Rig(g)
        for i in rig.joints:      # the same bind pose, joint for joint, or the bake is not this file's
            j = first.idx[rig.nodes[i]["name"]]
            assert np.abs(rig.M0[i] - first.M0[j]).max() < 1e-5, (cast, rig.nodes[i]["name"])
        for name in names:
            times, per = baked[name]
            ti = g.add(times.reshape(-1, 1), "SCALAR", minmax=True)
            chans, samps = [], []
            for node in rig.joints:
                Tj, Rj = per[rig.nodes[node]["name"]]
                # a channel that never leaves the bind pose is left out (the engine poses from the bind pose)
                for path, arr, kind, rest in (("translation", Tj, "VEC3", rig.T0[node]), ("rotation", Rj, "VEC4", rig.R0[node])):
                    same = np.abs(arr - rest).max() < 2e-6 or (kind == "VEC4" and np.abs(arr + rest).max() < 2e-6)
                    if same:
                        continue
                    samps.append({"input": ti, "output": g.add(arr, kind), "interpolation": "LINEAR"})
                    chans.append({"sampler": len(samps) - 1, "target": {"node": node, "path": path}})
            g.j["animations"].append({"name": name, "channels": chans, "samplers": samps})
        dst = os.path.join(out_dir or ALIENS, cast + SUFFIX + ".glb")
        g.save(dst)
        print("%s: %d clips added -> %s (%.1f MB)" % (cast, len(names), dst, os.path.getsize(dst) / 1e6))
    return table, first


def main():
    argv = sys.argv[1:]
    only = set(argv[argv.index("--only") + 1].split(",")) if "--only" in argv else None
    casts = argv[argv.index("--cast") + 1].split(",") if "--cast" in argv else CAST
    out = argv[argv.index("--out") + 1] if "--out" in argv else None
    table, rig = build_all(casts, out, only)
    if only is None and "--no-table" not in argv:
        stock = {n: round(rig.length(n), 4) for n in rig.clips}
        doc = {"_note": "Written by blender/nd_poses.py (it12, owner msg 170). The clips each cast file carries once "
                        "`/usr/bin/python3 examples/neon-dreams/blender/nd_poses.py` has made assets/aliens/<cast>-nd.glb "
                        "(gitignored, never committed): the pack's 26 (`stock`), mirrored copies of its standing clips "
                        "(`mirrored_stock`, *_m) and the authored ND_* clips (`authored`; kicks of 0.70 s = 2 beats at "
                        "171.4 BPM, so a rate of 1 sits on the grid; clip second 0 is ON a kick).",
               "suffix": SUFFIX, "kick_s": round(KICK, 5), "stock": stock,
               "authored": {n: {"seconds": table[n], "kicks": int(round(table[n] / KICK)), "about": about(n),
                                "use": CLIPS[n[:-2] if n.endswith("_m") else n]["use"]}
                            for n in table if n.startswith("ND_")},
               "mirrored_stock": {n: {"seconds": table[n], "about": about(n)} for n in table if not n.startswith("ND_")}}
        json.dump(doc, open(os.path.join(ART, "poses.json"), "w"), indent=1)
        print("art/poses.json:", len(doc["authored"]), "authored,", len(doc["mirrored_stock"]), "mirrored stock,", len(stock), "stock")


if __name__ == "__main__":
    main()
