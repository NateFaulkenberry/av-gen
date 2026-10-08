"""CP3: the living layer. Ground carpets, the organisms built for close range, the materials that make them read as
translucent living tissue, and the bioluminescence (ADR-1200 emitter layers) in three tiers.

  TIER 1  subtle    most organisms: moss dots, crown specks, ledge specks. Texture, barely visible.
  TIER 2  active    stalk tips, understorey glow in the forest, fungus gills and rims, reed tips. The rhythm.
  TIER 3  hero      the veil fungus's beads, the macro's hero caps, a few bright clusters. Rare focal points.

Colour (spec §12): emerald and yellow-green for the stalks (fungal light is 520-530 nm), cyan and electric blue for
moss and caps, violet only in the lattice fungi and as reflections, never mixed toward grey.

The organisms' meshes carry emitter templates (`<mesh>.emit.json`, [x, y, z, v, u, radius] in source space). Their
bodies are ordinary procedurals; the light is the ecosystem's, accumulated in compute, so millions of points cost
about a millisecond (ADR-1200), and the body meshes can stop drawing at a few tens of metres while their light
carries to the far side of the valley.
"""
from __future__ import annotations

import json
import math

import numpy as np

import flora
from geom import Mesh, grid_faces

# ---- palette (linear) -----------------------------------------------------------------------------------------------
EMERALD = [0.12, 1.0, 0.38]
LIME = [0.45, 1.0, 0.16]
CYAN = [0.05, 0.85, 1.0]
TEAL = [0.0, 0.95, 0.7]
BLUE = [0.08, 0.35, 1.0]
VIOLET = [0.42, 0.18, 1.0]
PALE = [0.65, 0.95, 1.0]


def write_emit(path, pts):
    pts = [[round(float(c), 4) for c in p] for p in pts]
    path.write_text(json.dumps({"points": pts}, separators=(",", ":")))
    return len(pts)


# ---- material programs (8 per scene on the GPU; keep them short: they run per fragment) -------------------------------


def op(kind, dst, **kw):
    d = {"kind": kind, "dst": dst}
    d.update(kw)
    return d


GLOW_FIELDS = ("glowFloorF", "glowBankF", "glowMeadowF")


def _pool(dst, tmp, color, gain):
    """Ops leaving in `dst` the living light pooled at this point: the three glow maps summed, tinted."""
    return [op("field", dst, field=GLOW_FIELDS[0]), op("field", tmp, field=GLOW_FIELDS[1]),
            op("add", dst, srcA=dst, srcB=tmp), op("field", tmp, field=GLOW_FIELDS[2]),
            op("add", dst, srcA=dst, srcB=tmp),
            op("constant", tmp, constant=[c * gain for c in color] + [1.0]), op("multiply", dst, srcA=dst, srcB=tmp)]


def programs():
    rock = {"name": "rock", "ops": [
        # macro value variation in world space, bedding streaks down the faces, moss on the ledges (uv.x = flatness)
        op("input", 0, input="worldPosition"),
        op("noise", 1, srcA=0, value=0.012, seed=3),                                # 80 m patches
        op("triplanar", 2, srcA=0, value=0.09, seed=5),                             # 10 m grain
        op("multiply", 1, srcA=1, srcB=2),
        op("remap", 1, srcA=1, value=1, constant=[0.05, 0.55, 0.45, 1.6]),
        op("constant", 3, constant=[0.052, 0.058, 0.082, 1.0]),                     # cold blue-grey stone
        op("multiply", 3, srcA=3, srcB=1),
        op("input", 4, input="uv"),
        op("smoothstep", 4, srcA=4, constant=[0.45, 0.85, 0, 0]),                   # ledge mask from flatness
        op("constant", 5, constant=[0.012, 0.03, 0.026, 1.0]),                      # moss: dark green-black
        op("mixBy", 3, srcA=3, srcB=5, srcC=4),
        op("constant", 6, constant=[0.92, 0.92, 0.92, 1.0]),
        *_pool(7, 5, [0.15, 0.45, 1.0], 0.008),                                      # the light pooled on the rock
    ], "baseColor": 3, "metallic": -1, "roughness": 6, "emission": 7, "emissionIntensity": 1.0, "opacity": -1}

    crown = {"name": "crown", "ops": [
        # a crown reads as a dome: dark underneath, catching the sky on top; lit from below where the undergrowth
        # glows (the floor's glow map). Kept short: it runs on most of the valley's pixels.
        op("input", 0, input="uv"),
        op("swizzle", 1, srcA=0, constant=[1, 1, 1, 1]),                            # v: height in the clump
        op("remap", 2, srcA=1, value=1, constant=[0.0, 1.0, 0.25, 1.35]),
        op("input", 3, input="instanceRandom"),
        op("remap", 3, srcA=3, value=1, constant=[0.0, 1.0, 0.7, 1.25]),
        op("swizzle", 3, srcA=3, constant=[0, 0, 0, 0]),
        op("multiply", 2, srcA=2, srcB=3),
        op("constant", 4, constant=[0.010, 0.024, 0.030, 1.0]),
        op("multiply", 4, srcA=4, srcB=2),
        op("field", 5, field=GLOW_FIELDS[0]),
        op("remap", 1, srcA=1, value=1, constant=[0.0, 1.0, 1.0, 0.15]),           # underside most
        op("multiply", 5, srcA=5, srcB=1),
        op("constant", 6, constant=[0.006, 0.02, 0.045, 1.0]),
        op("multiply", 6, srcA=6, srcB=5),
    ], "baseColor": 4, "metallic": -1, "roughness": -1, "emission": 6, "emissionIntensity": 1.0, "opacity": -1}

    cap = {"name": "cap", "ops": [
        # translucent cap: light gathers in the core and the gills, the body is a dimmer glow, the rim catches it
        # again; face-on (looking through) is brighter than grazing. CORE -> BODY -> RIM (spec §10).
        op("input", 0, input="uv"),
        op("swizzle", 1, srcA=0, constant=[1, 1, 1, 1]),                            # v: 0 centre .. 1 rim
        op("remap", 2, srcA=1, value=1, constant=[0.0, 0.6, 1.9, 0.45]),            # core to body
        op("smoothstep", 3, srcA=1, constant=[0.82, 1.0, 0, 0]),
        op("remap", 3, srcA=3, value=1, constant=[0.0, 1.0, 0.0, 1.4]),             # rim
        op("add", 2, srcA=2, srcB=3),
        op("fresnel", 4, value=1.5),
        op("remap", 4, srcA=4, value=1, constant=[0.0, 1.0, 1.0, 0.45]),
        op("multiply", 2, srcA=2, srcB=4),
        op("input", 5, input="instanceRandom"),
        op("swizzle", 5, srcA=5, constant=[2, 2, 2, 2]),
        op("remap", 5, srcA=5, value=1, constant=[0.0, 1.0, 0.45, 1.5]),            # each one its own brightness
        op("multiply", 2, srcA=2, srcB=5),
        op("constant", 3, constant=[140.0, 6.0, 0.0, 0.0]),                         # radial striations (gill lines
        op("multiply", 3, srcA=0, srcB=3),                                          # seen through the flesh)
        op("noise", 3, srcA=3, value=1.0, seed=13),
        op("remap", 3, srcA=3, value=1, constant=[0.25, 0.75, 0.45, 1.5]),
        op("multiply", 2, srcA=2, srcB=3),
        op("input", 6, input="materialEmission"),
        op("multiply", 6, srcA=6, srcB=2),
        op("constant", 0, constant=[0.015, 0.04, 0.05, 1.0]),
        op("remap", 7, srcA=1, value=1, constant=[0.0, 1.0, 0.92, 0.6]),           # thinner, more see-through at rim
    ], "baseColor": 0, "metallic": -1, "roughness": -1, "emission": 6, "emissionIntensity": 1.0, "opacity": 7}

    falls = {"name": "falls", "ops": [
        # falling water: streaks that travel down the strand (v runs lip -> foot), brightest in the top third,
        # broken into gaps; it catches light rather than being lit white
        op("input", 0, input="uv"),
        op("input", 1, input="time"),
        op("constant", 2, constant=[0.0, -1.6, 0.0, 0.0]),
        op("multiply", 2, srcA=2, srcB=1),                                          # (0, -1.6 t)
        op("swizzle", 3, srcA=0, constant=[0, 1, 0, 0]),                            # (u, v, u)
        op("constant", 4, constant=[17.0, 9.0, 0.0, 0.0]),
        op("multiply", 3, srcA=3, srcB=4),
        op("add", 3, srcA=3, srcB=2),
        op("noise", 3, srcA=3, value=1.0, seed=7),
        op("smoothstep", 3, srcA=3, constant=[0.35, 0.75, 0, 0]),                   # streaks with gaps
        op("swizzle", 5, srcA=0, constant=[1, 1, 1, 1]),
        op("remap", 5, srcA=5, value=1, constant=[0.0, 1.0, 1.0, 0.35]),
        op("multiply", 3, srcA=3, srcB=5),
        op("input", 6, input="materialEmission"),
        op("multiply", 6, srcA=6, srcB=3),
        op("remap", 7, srcA=3, value=1, constant=[0.0, 1.0, 0.15, 0.9]),           # gaps are see-through
        op("constant", 0, constant=[0.10, 0.14, 0.2, 1.0]),
    ], "baseColor": 0, "metallic": -1, "roughness": -1, "emission": 6, "emissionIntensity": 1.0, "opacity": 7}

    moss = {"name": "moss", "ops": [
        op("input", 0, input="worldPosition"),
        op("noise", 1, srcA=0, value=3.0, seed=11),
        op("remap", 1, srcA=1, value=1, constant=[0.2, 0.8, 0.5, 1.5]),
        op("input", 2, input="instanceRandom"),
        op("constant", 3, constant=[0.008, 0.02, 0.035, 1.0]),                     # moss: blue-black
        op("constant", 4, constant=[0.012, 0.035, 0.03, 1.0]),                     # to deep teal
        op("mixBy", 3, srcA=3, srcB=4, srcC=2),
        op("multiply", 3, srcA=3, srcB=1),
        # a soft luminous gradient in patches over the cushion's top, its colour the material's (per species)
        op("noise", 5, srcA=0, value=1.4, seed=17),
        op("smoothstep", 5, srcA=5, constant=[0.45, 0.8, 0, 0]),
        op("input", 7, input="uv"),
        op("swizzle", 7, srcA=7, constant=[1, 1, 1, 1]),
        op("multiply", 5, srcA=5, srcB=7),
        op("input", 6, input="materialEmission"),
        op("multiply", 6, srcA=6, srcB=5),
    ], "baseColor": 3, "metallic": -1, "roughness": -1, "emission": 6, "emissionIntensity": 1.0, "opacity": -1}

    bulb = {"name": "bulb", "ops": [
        # a translucent bulb lit from inside: brightest face-on (the core), falling toward the limb; each its own
        # brightness. Its colour is the material's.
        op("fresnel", 1, value=1.0),
        op("remap", 1, srcA=1, value=1, constant=[0.0, 1.0, 1.6, 0.25]),
        op("input", 2, input="instanceRandom"),
        op("swizzle", 2, srcA=2, constant=[1, 1, 1, 1]),
        op("remap", 2, srcA=2, value=1, constant=[0.0, 1.0, 0.25, 1.6]),
        op("multiply", 1, srcA=1, srcB=2),
        op("input", 3, input="materialEmission"),
        op("multiply", 6, srcA=3, srcB=1),
        op("constant", 0, constant=[0.02, 0.03, 0.04, 1.0]),
    ], "baseColor": 0, "metallic": -1, "roughness": -1, "emission": 6, "emissionIntensity": 1.0, "opacity": -1}

    frond = {"name": "frond", "ops": [
        # leaflets cut into the edge (opacity), a glowing midrib and a brighter curl at the tip
        op("input", 0, input="uv"),
        op("swizzle", 1, srcA=0, constant=[0, 0, 0, 0]),                            # u across
        op("constant", 2, constant=[-0.5, -0.5, -0.5, 0]),
        op("add", 1, srcA=1, srcB=2),
        op("multiply", 1, srcA=1, srcB=1),                                          # (u - .5)^2
        op("swizzle", 3, srcA=0, constant=[1, 1, 1, 1]),                            # v along
        op("constant", 4, constant=[22.0, 22.0, 22.0, 0]),
        op("multiply", 4, srcA=3, srcB=4),
        op("quantize", 4, srcA=4, value=1.0),
        op("constant", 5, constant=[-1, -1, -1, 0]),
        op("multiply", 4, srcA=4, srcB=5),
        op("swizzle", 5, srcA=0, constant=[1, 1, 1, 1]),
        op("constant", 7, constant=[22.0, 22.0, 22.0, 0]),
        op("multiply", 5, srcA=5, srcB=7),
        op("add", 4, srcA=4, srcB=5),                                               # fract(22 v): leaflet phase
        op("remap", 4, srcA=4, value=1, constant=[0.0, 1.0, 0.25, 0.04]),           # leaflet notch depth
        op("constant", 5, constant=[0.25, 0.25, 0.25, 0]),
        op("add", 5, srcA=4, srcB=5),
        op("multiply", 5, srcA=5, srcB=5),
        op("remap", 7, srcA=1, value=0, constant=[0.0, 1.0, 1.0, 0.0]),
        op("smoothstep", 7, srcA=1, constant=[0.0, 0.004, 0, 0]),
        op("remap", 7, srcA=7, value=1, constant=[0.0, 1.0, 1.0, 0.08]),           # the midrib glows
        op("remap", 2, srcA=3, value=1, constant=[0.6, 1.0, 0.0, 1.5]),            # the curl glows
        op("add", 7, srcA=7, srcB=2),
        op("input", 2, input="materialEmission"),
        op("multiply", 6, srcA=2, srcB=7),
        op("constant", 2, constant=[0.25, 0.25, 0.25, 0]),
        op("multiply", 2, srcA=2, srcB=1),
        op("threshold", 2, srcA=4, value=0.12),
        op("constant", 0, constant=[0.012, 0.03, 0.03, 1.0]),
    ], "baseColor": 0, "metallic": -1, "roughness": -1, "emission": 6, "emissionIntensity": 1.0, "opacity": 2}

    ground = {"name": "ground", "ops": [
        # the valley's ground: dark soil and moss in patches, wetter (darker) low down, catching the pooled light
        op("input", 0, input="worldPosition"),
        op("noise", 1, srcA=0, value=0.05, seed=21),
        op("triplanar", 2, srcA=0, value=0.6, seed=23),
        op("multiply", 1, srcA=1, srcB=2),
        op("remap", 1, srcA=1, value=1, constant=[0.05, 0.5, 0.5, 1.4]),
        op("constant", 3, constant=[0.016, 0.018, 0.034, 1.0]),                     # soil: indigo-black
        op("constant", 4, constant=[0.010, 0.024, 0.030, 1.0]),                     # moss
        op("noise", 5, srcA=0, value=0.11, seed=29),
        op("smoothstep", 5, srcA=5, constant=[0.4, 0.65, 0, 0]),
        op("mixBy", 3, srcA=3, srcB=4, srcC=5),
        op("multiply", 3, srcA=3, srcB=1),
        *_pool(6, 7, [0.15, 0.5, 1.0], 0.006),
    ], "baseColor": 3, "metallic": -1, "roughness": -1, "emission": 6, "emissionIntensity": 1.0, "opacity": -1}
    return [rock, crown, cap, falls, moss, ground, bulb, frond]


# ---- organisms ------------------------------------------------------------------------------------------------------


def tuft(seed, n=(5, 11), height=(0.2, 0.8)):
    """A tuft of thin stalks, each ending in a translucent bulb (the reference's tip-stalks). Two meshes with the same
    placements: the stalks (dark) and the bulbs (self-lit, shaded by the `bulb` program: a bright core, a dimmer rim).
    Emitters: one per bulb, as its soft halo; a faint one partway up some stalks."""
    r = np.random.default_rng(seed)
    stalks, bulbs = Mesh(), Mesh()
    emit = []
    k = int(r.integers(*n))
    for i in range(k):
        h = r.uniform(*height)
        base = np.array([r.normal(0, 0.05), 0.0, r.normal(0, 0.05)])
        lean = np.array([r.normal(0, 0.25), 1.0, r.normal(0, 0.25)])
        lean /= np.linalg.norm(lean)
        bend = np.array([r.normal(0, 0.14), 0, r.normal(0, 0.14)])
        ts = np.linspace(0, 1, 7)
        pts = base + np.outer(ts * h, lean) + np.outer(ts ** 2 * h, bend) - np.outer(ts ** 3 * h * 0.15, [0, 1, 0])
        u = (i * 0.618 + seed * 0.13) % 1.0
        flora.tube(stalks, pts, np.linspace(0.005, 0.002, 7), 3, 0.0, 1.0, u=u)
        rad = r.uniform(0.008, 0.02) * (0.6 + h)
        tip = pts[-1] + (pts[-1] - pts[-2]) / np.linalg.norm(pts[-1] - pts[-2]) * rad * 0.8
        flora.clump(bulbs, tip, rad, seed * 50 + i, level=2, squash=1.15, lump=0.05, u=u)
        emit.append([*tip, 1.0, u, rad * 1.6])
        if r.random() < 0.25:
            emit.append([*pts[4], 0.6, (u + 0.5) % 1, 0.004])
    return stalks, bulbs, emit


def frond(seed, length=(0.5, 1.2), n=(3, 6)):
    """A clump of curled fronds (fern-like): a ribbon whose width follows a leaf profile, curling at the tip, with
    leaflets cut into its edge by the program (u across, v along). The `frond` program glows along the midrib and
    the tip of the curl: glowing fronds, not dots."""
    r = np.random.default_rng(seed)
    m = Mesh()
    emit = []
    for i in range(int(r.integers(*n))):
        L = r.uniform(*length)
        a = r.uniform(0, 2 * math.pi)
        d = np.array([math.cos(a), 0, math.sin(a)])
        side = np.array([-d[2], 0, d[0]])
        s = np.linspace(0, 1, 24)
        rise = 0.55 + 0.25 * r.random()
        # an arching spine: up and out, then the tip curls back (a fiddlehead unrolling)
        ang = rise * math.pi / 2 * (1 - s) - s ** 3 * 1.6
        dx = np.cumsum(np.cos(ang)) / len(s) * L
        dy = np.cumsum(np.sin(ang)) / len(s) * L
        spine = np.outer(dx, d) + np.outer(dy, [0, 1, 0])
        w = L * 0.16 * np.sin(np.clip(s, 0, 1) * math.pi) ** 0.7 * (1 - 0.6 * s)
        twist = 0.35 * s
        nrm = np.outer(np.cos(twist), side) + np.outer(np.sin(twist), [0, 1, 0])
        V = np.concatenate([spine - nrm * w[:, None], spine + nrm * w[:, None]])
        F = []
        N = len(s)
        for j in range(N - 1):
            F += [[j, j + 1, N + j], [j + 1, N + j + 1, N + j]]
        UV = np.concatenate([np.stack([np.zeros(N), s], -1), np.stack([np.ones(N), s], -1)])
        m.add(V, np.array(F), UV)
        for j in range(4, N, 4):
            emit.append([*spine[j], s[j], (i * 0.618 + j * 0.07) % 1, 0.012])
    return m, emit


def grass(seed, n=(14, 26), height=(0.15, 0.45)):
    """A clump of dark blades (the carpet's body between the tips)."""
    r = np.random.default_rng(seed)
    m = Mesh()
    for i in range(int(r.integers(*n))):
        h = r.uniform(*height)
        a = r.uniform(0, 2 * math.pi)
        lean = r.uniform(0.1, 0.5)
        d = np.array([math.cos(a), 0, math.sin(a)])
        base = d * r.uniform(0, 0.06)
        s = np.linspace(0, 1, 4)
        spine = base[None, :] + np.outer(s * h, [0, 1, 0]) + np.outer((s ** 2) * h * lean, d)
        side = np.array([-d[2], 0, d[0]]) * 0.008
        w = (1 - s)[:, None]
        V = np.concatenate([spine - side * w, spine + side * w])
        F = [[0, 1, 4], [1, 5, 4], [1, 2, 5], [2, 6, 5], [2, 3, 6], [3, 7, 6]]
        m.add(V, np.array(F), np.stack([np.zeros(8), np.tile(s, 2)], -1))
    return m


def moss(seed, size=1.0, dots=600):
    """A moss cushion: a soft lumpy mound, with a skin of tiny luminous dots (the macro and riverbank carpet)."""
    r = np.random.default_rng(seed)
    V, F = flora.icosphere(3)
    n = flora.fbm3(V * 2.0 + seed, seed, 3)
    fine = flora.noise3(V * 9.0 + seed * 0.3, seed + 1)
    P = V * (1.0 + 0.35 * (n - 0.5) + 0.12 * (fine - 0.5))[:, None]
    P *= np.array([size * r.uniform(0.9, 1.3), size * r.uniform(0.35, 0.6), size * r.uniform(0.8, 1.2)])
    P[:, 1] = np.maximum(P[:, 1], -0.05 * size)
    m = Mesh()
    m.add(P, F, np.stack([np.zeros(len(P)), np.clip(P[:, 1] / size + 0.2, 0, 1)], -1))
    # dots on the upper surface: sample faces by area
    a, b, c = P[F[:, 0]], P[F[:, 1]], P[F[:, 2]]
    area = np.linalg.norm(np.cross(b - a, c - a), axis=1)
    up = ((a + b + c) / 3)[:, 1] > 0.0
    w = area * up
    w = w / w.sum()
    idx = r.choice(len(F), dots, p=w)
    s1, s2 = r.random(dots), r.random(dots)
    q = np.sqrt(s1)
    pts = a[idx] * (1 - q)[:, None] + b[idx] * (q * (1 - s2))[:, None] + c[idx] * (q * s2)[:, None]
    pts[:, 1] += 0.01 * size
    emit = [[*p, 0.5, float(r.random()), 0.006 * size * r.uniform(0.6, 1.6)] for p in pts]
    return m, emit


def lattice(seed, radius=0.12):
    """A lacy lattice fungus (Clathrus-like): the edges of a jittered icosphere as tubes, a hollow cage."""
    r = np.random.default_rng(seed)
    V, F = flora.icosphere(1)
    V = V * (1.0 + 0.12 * r.normal(size=(len(V), 1)))
    V[:, 1] = V[:, 1] * 0.85 + 0.85
    V *= radius
    edges = set()
    for f in F:
        for i in range(3):
            a, b = sorted((f[i], f[(i + 1) % 3]))
            edges.add((a, b))
    m = Mesh()
    emit = []
    for k, (a, b) in enumerate(edges):
        if V[a][1] < 0.15 * radius and V[b][1] < 0.15 * radius:
            continue
        pts = np.linspace(V[a], V[b], 5)
        mid = (V[a] + V[b]) / 2
        pts += np.outer(np.sin(np.linspace(0, math.pi, 5)), mid / np.linalg.norm(mid)) * radius * 0.06
        u = (k * 0.618) % 1
        flora.tube(m, pts, radius * 0.07, 5, 0.0, 1.0, u=u)
        emit.append([*mid, 0.5, u, radius * 0.06])
    return m, emit


def bulbs(seed, n=(3, 7)):
    """Round glowing bulbs on short stalks (the macro's foreground orbs)."""
    r = np.random.default_rng(seed)
    stalks, heads = Mesh(), Mesh()
    emit = []
    for i in range(int(r.integers(*n))):
        h = r.uniform(0.03, 0.12)
        base = np.array([r.normal(0, 0.04), 0, r.normal(0, 0.04)])
        top = base + np.array([r.normal(0, 0.02), h, r.normal(0, 0.02)])
        flora.tube(stalks, np.linspace(base, top, 4), 0.004, 4)
        rad = r.uniform(0.008, 0.02)
        flora.clump(heads, top, rad, seed * 9 + i, level=2, squash=1.0, lump=0.05, u=(i * 0.618) % 1)
        emit.append([*top, 1.0, (i * 0.618) % 1, rad * 1.1])
    return stalks, heads, emit


def reeds(seed, n=(5, 11)):
    """Reeds at the water's edge: tall thin blades, a luminous bead near each top."""
    r = np.random.default_rng(seed)
    m = Mesh()
    emit = []
    for i in range(int(r.integers(*n))):
        h = r.uniform(0.6, 1.6)
        base = np.array([r.normal(0, 0.08), 0, r.normal(0, 0.08)])
        lean = np.array([r.normal(0, 0.12), 1.0, r.normal(0, 0.12)])
        pts = base + np.outer(np.linspace(0, h, 6), lean / np.linalg.norm(lean))
        u = (i * 0.618) % 1
        flora.tube(m, pts, np.linspace(0.008, 0.002, 6), 3, u=u)
        emit.append([*pts[-1], 1.0, u, 0.02])
        emit.append([*pts[-3], 0.7, (u + 0.3) % 1, 0.008])
    return m, emit


def fungus3d(seed, height, cap_r, ruffle=0.08):
    """A fungus with real form: a domed cap with thickness (a top shell and an underside), a ruffled rim, gills
    under it, a stalk with a ring and a swollen foot. Parts: stalk, cap (top + underside + gills, one material),
    and an emitter template on the gills and the rim."""
    r = np.random.default_rng(seed)
    stalk, cap = Mesh(), Mesh()
    emit = []
    n = 16
    lean = r.normal(0, 0.1, 2)
    ts = np.linspace(0, 1, n)
    pts = np.stack([lean[0] * height * ts ** 2, height * ts, lean[1] * height * ts ** 2], -1)
    rad = height * 0.032 * (1.0 + 0.7 * np.exp(-ts / 0.08)) * (1.0 - 0.2 * ts)
    rad = rad * (1.0 + 0.35 * np.exp(-((ts - 0.78) / 0.025) ** 2))                # the ring
    flora.tube(stalk, pts, rad, 12, 0.0, 1.0)
    top = pts[-1]
    rings, segs = 18, 120
    th = np.linspace(0, 2 * math.pi, segs, endpoint=False)
    k1, k2, k3 = r.uniform(0, 6, 3)
    frill = 1.0 + ruffle * (0.6 * np.sin(6 * th + k1) + 0.3 * np.sin(11 * th + k2) + 0.2 * np.sin(19 * th + k3))
    lift = ruffle * cap_r * (np.sin(9 * th + k2) + 0.5 * np.sin(17 * th + k1))
    rr = np.linspace(0.0, 1.0, rings)
    dome = 0.42
    T = np.zeros((rings, segs, 3))
    B = np.zeros((rings, segs, 3))
    for i, t in enumerate(rr):
        R = cap_r * t * frill
        yt = cap_r * dome * (1.0 - t ** 2) ** 0.75 + lift * t ** 3
        yb = yt - cap_r * 0.22 * (1.0 - t ** 1.5)                                  # thickness: thick centre, thin rim
        for A, y in ((T, yt), (B, yb)):
            A[i, :, 0] = top[0] + R * np.cos(th)
            A[i, :, 2] = top[2] + R * np.sin(th)
            A[i, :, 1] = top[1] + y - cap_r * 0.1
    uv = np.stack([np.tile(th / (2 * math.pi), rings), np.repeat(rr, segs)], -1)
    ft = grid_faces(rings, segs, True)
    cap.add(T.reshape(-1, 3), ft[:, [0, 2, 1]], uv)                                 # top faces up
    cap.add(B.reshape(-1, 3), ft, uv)                                               # underside faces down
    # gills: thin radial fins hanging under the underside
    G = 64
    for g in range(G):
        a = g * 2 * math.pi / G + r.normal(0, 0.01)
        j = int(round(a / (2 * math.pi) * segs)) % segs
        line = B[2:, j, :]
        drop = np.linspace(cap_r * 0.12, 0.0, len(line))[:, None] * np.array([0, 1.0, 0])
        V = np.concatenate([line, line - drop])
        m = len(line)
        F = []
        for i in range(m - 1):
            F += [[i, i + 1, m + i], [i + 1, m + i + 1, m + i]]
        cap.add(V, np.array(F), np.stack([np.full(2 * m, a / 6.283), np.concatenate([rr[2:], rr[2:]]) * 0.7], -1))
        for i in range(3, m, 2):
            emit.append([*(line[i] - drop[i] * 0.6), 0.6, (g * 0.618 + i * 0.1) % 1, cap_r * 0.012])
    for j in range(0, segs, 2):
        emit.append([*T[-1, j, :], 1.0, (j * 0.618) % 1, cap_r * 0.016])           # the rim
    return stalk, cap, emit
