"""The approved hero library (the asset artist's GLBs in meshes/hero/) placed in the shots.

The organisms keep their own materials and textures: no `material` block (03-hero-assets.md). Near the lens they draw
the macro build (LOD 0); further out the scene build (LOD 1). Each species' `.emit.json` glints become an ecosystem
layer, which can mirror in the water (ADR-1281).

Species and niches (scale is relative to the authored size, which is macro-scale: 0.1-0.8 m):
  veilcap       the focal hero: alone, on a bank or a rise, very large where it is the shot's subject
  glowmound     mossy glowing mounds: the ground's skin near water and under heroes
  glowmat       flat luminous mats: between mounds, on rock tops
  lanternberry  hanging berry clusters: in groups at the foot of larger plants
  filigree      branching filament shrubs: the middle layer, in drifts
  veilfern      broad translucent fronds: at the waterline, leaning out
  bellspire     tall stalks with bell heads: rare, standing above the rest
  lattice       perforated fans: in shade, sparse
  reliquary     a caged orb on stalks: rare focal accents
  chalice       cups (being redone): unused until the redo lands
"""
from __future__ import annotations

import math
from pathlib import Path

import numpy as np

import populate
from geom import fbm2

rec = populate.rec
HERO = "meshes/hero"


# Per-scene emission dials (the artist's strengths are all 4.0). A material block with neutral factors keeps the
# asset's textures and only retunes the emissive strength (ADR-1283 keeps its cutouts and two-sidedness).
DIAL = {"glowmat": 0.8, "filigree": 1.6, "glowmound": 2.4, "veilfern": 2.6, "lattice": 2.6, "bellspire": 3.0,
        "lanternberry": 3.0, "veilcap": 4.0, "reliquary": 3.0, "glowsedge": 3.0, "riverrock_a": 2.0,
        "riverrock_b": 2.0, "riverrock_c": 2.0, "riverrock_d": 2.0, "shadowbough": 2.0, "darkfern": 0.15,
        "canopy": 1.5, "canopygrove": 2.0, "bulbstalk": 3.0}


# Base-colour factors (multiplying the textures): the pale aprons and lace read as snow under the moon at range
BASE = {"glowmat": [0.07, 0.09, 0.13], "glowmound": [0.45, 0.55, 0.6], "lattice": [0.4, 0.4, 0.55],
        "filigree": [0.15, 0.17, 0.22], "darkfern": [0.35, 0.4, 0.5]}


# Colourways by tinting the emission (the material's emissive factor multiplies the asset's emission texture, whose
# light is cyan-teal): one mesh, the reference palette's hues. Factors above 1 pull the weak channels up.
TINTS = {"cyan": [1.0, 1.0, 1.0], "emerald": [0.35, 1.0, 0.4], "chartreuse": [2.6, 1.05, 0.12],
         "gold": [4.5, 0.8, 0.06], "blue": [0.35, 0.55, 1.7], "violet": [2.6, 0.3, 1.7]}
# the glint colour of each colourway (the light the eye reads as the organism's dots)
TINT_GLOW = {"cyan": [0.1, 0.85, 1.0], "emerald": [0.15, 1.0, 0.35], "chartreuse": [0.65, 1.0, 0.1],
             "gold": [1.0, 0.7, 0.15], "blue": [0.15, 0.35, 1.0], "violet": [0.55, 0.2, 1.0]}
# per shot, per species: the hue mix (weights), chosen per region (coherent drifts of one colour)
PALETTES = {
    # panel 4 is greener than the collage as a whole: its carpet is emerald and yellow-green over blue
    "meadow": {"bulbstalk": [("chartreuse", 3), ("blue", 2), ("cyan", 2), ("gold", 1)],
               "glowmat": [("emerald", 3), ("chartreuse", 1), (None, 1)],   # the blue mat reads as frost here
               "filigree": [("blue", 2), ("cyan", 1), ("violet", 1)],
               "lanternberry": [("blue", 2), ("gold", 1), ("violet", 1)],
               "glowmound": [("blue", 2), ("emerald", 1)],
               "glowsedge": [("blue", 2), ("cyan", 1)], "veilfern": [("blue", 1), ("cyan", 1)],
               "bellspire": [("blue", 2), ("violet", 1)]},
    "riverbank": {"glowsedge": [("blue", 2), ("cyan", 2), ("violet", 2)],
                  "glowmound": [("blue", 3), ("emerald", 2), ("cyan", 1)],
                  "glowmat": [("blue", 2), ("emerald", 1)], "veilfern": [("cyan", 2), ("violet", 1), ("blue", 1)],
                  "lanternberry": [("violet", 2), ("blue", 2), ("gold", 1)], "bellspire": [("violet", 2), ("blue", 1)],
                  "filigree": [("cyan", 1), ("blue", 2)], "veilcap": [("cyan", 1)]},
}


def node(name, species, pts, lod, maxd, tint=None):
    # the artist's recoloured GLB when there is one (a material factor above 1 is clamped, so the emission tint can
    # only darken channels; the colourways recolour the texture band by band instead)
    variant = (Path(__file__).parent / HERO / f"hero_{species}_{tint}_lod{lod}.glb").exists() if tint else False
    glb = f"hero_{species}_{tint}" if variant else f"hero_{species}"
    proc = {"source": {"kind": "mesh", "asset": f"../{HERO}/{glb}_lod{lod}.glb"},
            "distribution": {"kind": "points", "points": pts}, "castsShadow": False,
            "lod": {"cull": True, "count": 1, "byScreenSize": False, "maxDistance": maxd}}
    if species in DIAL:
        proc["material"] = {"baseColor": BASE.get(species, [1.0, 1.0, 1.0]), "roughness": 1.0, "metallic": 0.0,
                            "emissiveColor": [1.0, 1.0, 1.0] if variant or not tint else
                            [min(c, 1.0) for c in TINTS.get(tint, [1.0, 1.0, 1.0])],
                            "emissiveIntensity": DIAL[species]}
    return {"name": name, "kind": "procedural", "procedural": proc}


def emit_layer(name, hosts, species, color, intensity, mirror=None, **kw):
    d = {"name": name, "hosts": hosts, "template": f"../{HERO}/hero_{species}.emit.json", "color": color,
         "intensity": intensity, "excitedColor": color, "excitedIntensity": 0.0, "size": 1.0, "breath": 0.25,
         "breathRate": 0.06, "flicker": 0.2, "flickerRate": 0.6, "sparsity": 0.3, "maxDistance": 400.0}
    d.update(kw)
    if mirror:
        d.update(mirror)
    return d


def assemble(groups, eye, near=10.0, far=400.0, palette=None, scale=12.0):
    """groups: {species: [records]} -> (nodes, {species: [host names]}). LOD 0 within `near` m of the eye. With a
    palette, each record takes a colourway chosen by a low-frequency noise at its position (drifts of one hue)."""
    nodes, hosts = [], {}
    palette = palette or {}
    for sp, recs in groups.items():
        if not recs:
            continue
        mix = palette.get(sp, [(None, 1)])
        names = [m[0] for m in mix]
        cum = np.cumsum([m[1] for m in mix], dtype=float)
        cum /= cum[-1]
        P = np.array([[r[0], r[2]] for r in recs])
        n = fbm2(P[:, 0] / scale, P[:, 1] / scale, 71 + len(sp), 2)
        n = np.clip((n - 0.25) / 0.5, 0, 0.999)                  # stretch the noise to fill 0..1
        pick = np.searchsorted(cum, n)
        for ti, tint in enumerate(names):
            sel = [recs[i] for i in range(len(recs)) if pick[i] == ti]
            near_r = [r for r in sel if math.hypot(r[0] - eye[0], r[2] - eye[2]) < near]
            far_r = [r for r in sel if math.hypot(r[0] - eye[0], r[2] - eye[2]) >= near]
            for tag, rs, lod, md in (("n", near_r, 0, near * 1.5), ("f", far_r, 1, far)):
                if rs:
                    nm = f"lib_{sp}_{tint or 'base'}_{tag}"
                    nodes.append(node(nm, sp, rs, lod, md, tint))
                    hosts.setdefault((sp, tint), []).append(nm)
    return nodes, hosts


def bank(g, eye, head, water_level, fall_foot=None, rng_seed=31):
    """The riverbank: the stream's two banks out to ~80 m upstream, a focal veilcap on the near (left) bank."""
    r = np.random.default_rng(rng_seed)
    groups = {k: [] for k in ("veilcap", "glowmound", "glowmat", "lanternberry", "filigree", "veilfern", "bellspire",
                              "lattice", "reliquary", "glowsedge", "darkfern", "riverrock_a", "riverrock_b",
                              "riverrock_c", "riverrock_d", "shadowbough", "canopy")}

    def put(sp, x, z, s, sink=0.01):
        y = float(g.height(np.array([x]), np.array([z]))[0])
        groups[sp].append(rec(x, y - sink * s, z, r.uniform(0, 6.28), s))

    ex, ez = eye[0], eye[2]
    # the forest over the stream (panel 1's back is canopy and hanging growth, not bare mountain): crowns on both
    # banks from 25 m out, tall enough to close the top of frame
    fx0, fz0 = math.sin(head), math.cos(head)
    for k in range(70):
        d = r.uniform(22.0, 110.0)
        side = r.choice([-1.0, 1.0]) * r.uniform(9.0, 40.0)
        x, z = ex + fx0 * d + fz0 * side, ez + fz0 * d - fx0 * side
        if g.wet(np.array([x]), np.array([z]))[0]:
            continue
        if fall_foot is not None:                     # keep the line of sight to the falls open
            ax, az = fall_foot[0] - ex, fall_foot[2] - ez
            L = math.hypot(ax, az)
            t = ((x - ex) * ax + (z - ez) * az) / L
            if 0 < t < L + 60 and abs((x - ex) * az - (z - ez) * ax) / L < 30.0:
                continue
        put("canopy", x, z, r.uniform(1.1, 1.9), sink=0.02)
    fx, fz = math.sin(head), math.cos(head)          # forward (x, z)
    lx, lz = fz, -fx                                  # left (checked against the projection: right-dot < 0)

    def shore(d, extra):
        """The point `extra` metres beyond the left (near) bank's waterline, `d` metres ahead."""
        for s in np.arange(0.0, 40.0, 0.25):
            x, z = ex + fx * d + lx * s, ez + fz * d + lz * s
            if not g.wet(np.array([x]), np.array([z]))[0]:
                return s + extra
        return 40.0 + extra
    # candidates over both banks ahead of the lens
    for k in range(16000):
        d = r.uniform(5.0, 90.0)
        side = r.uniform(-30.0, 30.0)
        grow = 0.55 + 0.45 * min(d / 40.0, 1.0)          # nearer plants smaller, so the lens is not blocked
        x = ex + fx * d + lx * side
        z = ez + fz * d + lz * side
        if g.wet(np.array([x]), np.array([z]))[0]:
            continue
        right = side < 0
        nw = any(g.wet(np.array([x + dx]), np.array([z + dz]))[0] for dx, dz in ((1.5, 0), (-1.5, 0), (0, 1.5),
                                                                                  (0, -1.5)))
        patch = float(fbm2(np.array([x / 7.0]), np.array([z / 7.0]), 41, 3)[0])
        u = r.random()
        if nw and u < 0.18:
            put("veilfern", x, z, grow * r.uniform(1.6, 3.2))                 # the waterline: broad fronds leaning out
        elif nw and u < 0.55:
            put("glowsedge", x, z, r.uniform(0.6, 1.0) if right else r.uniform(1.0, 2.0))   # sedge at the edge
        elif nw and u < 0.65:
            put("lanternberry", x, z, r.uniform(2.0, 3.5))
        elif nw and u < 0.8:
            put(["riverrock_a", "riverrock_b", "riverrock_c", "riverrock_d"][int(r.integers(0, 4))], x, z,
                r.uniform(0.8, 1.5), sink=0.12)                               # rocks at the waterline
        elif nw:
            put("glowmound", x, z, grow * r.uniform(0.8, 2.0), sink=0.05)
        elif u < 0.25:
            put("darkfern", x, z, r.uniform(2.0, 4.0))                         # dark contrast between the glow
        elif patch > 0.58:
            put(["glowmound", "glowmat", "glowmat"][int(r.integers(0, 3))], x, z, grow * r.uniform(0.8, 2.2), sink=0.04)
        elif patch > 0.45:
            put(["filigree", "lanternberry", "filigree"][int(r.integers(0, 3))], x, z,
                grow * r.uniform(1.0, 1.8) if right else grow * r.uniform(1.4, 3.2))
        elif patch < 0.3 and u < 0.06:
            put("lattice", x, z, grow * r.uniform(1.8, 3.5))
        elif u < 0.012 and not right:
            put("bellspire", x, z, grow * r.uniform(2.0, 3.5))
    # the focal veilcap: on the near (left) bank, 10-12 m ahead, very large (about 2.5 m tall), and a smaller pair
    for d, side, s in ((9.0, 1.5, 7.0), (12.0, 3.0, 4.0), (7.0, 3.0, 2.6)):
        side = shore(d, side)
        x, z = ex + fx * d + lx * side, ez + fz * d + lz * side
        put("veilcap", x, z, s)
    # the near-left foreground (panel 1's dark rocks, moss and glowing stalks at the lens)
    for k in range(60):
        d = r.uniform(1.6, 14.0)
        side = shore(d, r.uniform(0.3, 4.0))
        x, z = ex + fx * d + lx * side, ez + fz * d + lz * side
        if g.wet(np.array([x]), np.array([z]))[0]:
            continue
        u = r.random()
        if u < 0.3:
            put(["riverrock_a", "riverrock_b", "riverrock_c"][int(r.integers(0, 3))], x, z, r.uniform(0.7, 1.6),
                sink=0.15)
        elif u < 0.55:
            put("darkfern", x, z, r.uniform(2.0, 4.0))
        elif u < 0.75:
            put("glowmound", x, z, r.uniform(0.9, 1.8), sink=0.05)
        elif u < 0.9:
            put("lanternberry", x, z, r.uniform(1.0, 2.0))
        else:
            put("bellspire", x, z, r.uniform(1.2, 2.0))
    # stones standing in the stream to the right of the lens, with sedge on them: the water is framed, not empty
    for d, side, s in ((9.0, -5.5, 0.6), (17.0, 3.0, 0.5)):
        x, z = ex + fx * d + lx * side, ez + fz * d + lz * side
        y = float(g.water_level(np.array([x]), np.array([z]))[0])
        if y < -1e8:
            y = float(g.height(np.array([x]), np.array([z]))[0])
        sp = ["riverrock_a", "riverrock_b"][int(r.integers(0, 2))]
        groups[sp].append(rec(x, y - 0.25 * s, z, r.uniform(0, 6.28), s))

    # glowing plants at the falls' foot
    if fall_foot is not None:
        for k in range(14):
            a = r.uniform(0, 6.28)
            rr = r.uniform(3.0, 10.0)
            x, z = fall_foot[0] + rr * math.cos(a), fall_foot[2] + rr * math.sin(a)
            if g.wet(np.array([x]), np.array([z]))[0]:
                continue
            put(["glowsedge", "bellspire", "lanternberry", "glowmound"][k % 4], x, z, r.uniform(1.5, 3.0))
    # the framing bough: rooted behind-left of the lens, its +X limb aimed over the stream to the right
    bx, bz = ex - fx * 2.0 + lx * 14.0, ez - fz * 2.0 + lz * 14.0
    aim = (fx * 0.85 - lx * 0.5, fz * 0.85 - lz * 0.5)
    y = float(g.height(np.array([bx]), np.array([bz]))[0])
    groups["shadowbough"].append(rec(bx, y - 0.5, bz, math.atan2(-aim[1], aim[0]), 0.65))
    for d, side, s in ((22.0, -6.0, 3.0), (30.0, 5.0, 3.5)):
        x, z = ex + fx * d + lx * side, ez + fz * d + lz * side
        put("reliquary", x, z, s)
    return groups


def meadow(g, eye, head, rng_seed=37):
    """The meadow (panel 4): layered vegetation in drifts, not a carpet. Fine ground growth (glowmat), small
    luminous clusters (bulbstalk), medium forms (filigree, lanternberry), occasional tall heroes (bellspire), dark
    contrast (darkfern), rare bright focal specimens (veilcap, reliquary); mossy rocks in the foreground guiding the
    eye to the stream."""
    r = np.random.default_rng(rng_seed)
    groups = {k: [] for k in ("glowmat", "bulbstalk", "filigree", "lanternberry", "bellspire", "darkfern", "veilcap",
                              "reliquary", "glowmound", "riverrock_a", "riverrock_b", "riverrock_c", "riverrock_d",
                              "glowsedge", "veilfern")}
    ex, ez = eye[0], eye[2]
    fx, fz = math.sin(head), math.cos(head)
    lx, lz = fz, -fx

    def put(sp, x, z, s, sink=0.01):
        y = float(g.height(np.array([x]), np.array([z]))[0])
        groups[sp].append(rec(x, y - sink * s, z, r.uniform(0, 6.28), s))

    for k in range(30000):
        d = 6.0 + 160.0 * r.random() ** 1.7          # denser near, thinning out; the lens's first metres open
        side = r.uniform(-1.1, 1.1) * (4.0 + d * 0.9)
        x, z = ex + fx * d + lx * side, ez + fz * d + lz * side
        if g.wet(np.array([x]), np.array([z]))[0]:
            continue
        nw = any(g.wet(np.array([x + dx]), np.array([z + dz]))[0] for dx, dz in ((1.0, 0), (-1.0, 0), (0, 1.0),
                                                                                  (0, -1.0)))
        drift = float(fbm2(np.array([x / 18.0]), np.array([z / 18.0]), 51, 3)[0]) * 0.6 + \
            float(fbm2(np.array([x / 6.0]), np.array([z / 6.0]), 52, 2)[0]) * 0.4
        grow = 0.6 + 0.7 * min(d / 60.0, 1.0)
        u = r.random()
        if nw and u < 0.5:
            put(["glowsedge", "veilfern"][int(r.integers(0, 2))], x, z, r.uniform(0.6, 1.1))
        elif drift > 0.58:                                    # a drift: dense luminous clusters
            put(["bulbstalk", "bulbstalk", "bulbstalk", "glowmat", "filigree"][int(r.integers(0, 5))], x, z,
                grow * r.uniform(1.6, 3.0))
        elif drift > 0.48:                                    # the drift's edge: medium forms
            if u < 0.5:
                put(["filigree", "lanternberry", "glowmat"][int(r.integers(0, 3))], x, z, grow * r.uniform(1.6, 3.0))
        elif u < 0.35:                                        # between drifts: dark growth
            put("darkfern", x, z, grow * r.uniform(1.2, 2.6))
        if u < 0.004 and d > 25.0:
            put("bellspire", x, z, r.uniform(1.8, 2.8))
    # rare focal specimens: a veilcap pair left of the stream, a reliquary right
    for d, side, s in ((16.0, 4.5, 4.0), (18.0, 6.0, 2.5)):
        put("veilcap", ex + fx * d + lx * side, ez + fz * d + lz * side, s)
    put("reliquary", ex + fx * 24.0 - lx * 6.0, ez + fz * 24.0 - lz * 6.0, 4.0)
    # foreground: the carpet runs to the lens (panel 4's bottom is growth, not ground): mats edge to edge, stalk
    # drifts rising out of them, a few low mossy rocks and mounds half-sunk in it
    for k in range(5000):
        d = 1.0 + 8.0 * r.random() ** 0.8
        side = r.uniform(-1.0, 1.0) * (1.0 + d * 0.75)
        x, z = ex + fx * d + lx * side, ez + fz * d + lz * side
        if g.wet(np.array([x]), np.array([z]))[0]:
            continue
        drift = float(fbm2(np.array([x / 3.0]), np.array([z / 3.0]), 53, 2)[0])
        u = r.random()
        near = d < 4.5                                    # the nearest metres stay mostly dark (panel 4's bottom)
        if u < 0.45:
            put("glowmat", x, z, r.uniform(1.2, 2.4))
        elif drift > (0.66 if near else 0.56) and u < 0.55:
            put("bulbstalk", x, z, r.uniform(1.0, 2.2))
        elif u < 0.8:
            put("darkfern", x, z, r.uniform(0.6, 1.3))       # low, so the mats' light shows between
        elif u < 0.92:
            put(["filigree", "lanternberry"][int(r.integers(0, 2))], x, z, r.uniform(1.0, 1.8))
    for k in range(14):
        d = r.uniform(2.0, 9.0)
        side = r.uniform(-5.0, 5.0)
        x, z = ex + fx * d + lx * side, ez + fz * d + lz * side
        if g.wet(np.array([x]), np.array([z]))[0]:
            continue
        if r.random() < 0.5:
            put(["riverrock_a", "riverrock_b", "riverrock_c"][int(r.integers(0, 3))], x, z, r.uniform(0.3, 0.6),
                sink=0.3)
        else:
            put("glowmound", x, z, r.uniform(0.5, 0.9), sink=0.25)
    return groups


# ---- the macro (panel 3): the library at LOD 0 in the valley's own macro camera ------------------------------------
# Colourways are the artist's recoloured GLBs (hero_<species>_<colour>), weighted to the reference's glow palette:
# mostly electric blue and cyan, emerald second, violet and gold as rare accents.
MACRO_DIAL = {"veilcap": 1.9, "reliquary": 2.1, "lacefan": 0.15, "beadcluster": 1.2, "glowmound": 0.7,
              "glowmat": 1.2, "bulbstalk": 0.6, "filigree": 0.55, "bellspire": 0.45, "lanternberry": 0.9,
              "veilfern": 0.9, "chalice": 1.0, "glowsedge": 1.0, "darkfern": 0.4, "canopy": 0.0}


# the lit albedo of the supporting forms: the pale lacefan lattice and the tall stalks catch the moon as brightly as
# the heroes glow, so their reflectance is cut (they read as dark lace against the dark, lit at the rims)
MACRO_FG_DIAL = 2.2
MACRO_BASE = {"lacefan": 0.18, "bellspire": 0.45, "bulbstalk": 0.6}


def macro_node(name, glb, pts, dial):
    if "@fg" in glb:                                   # a foreground variant: same mesh, its own glow
        glb = glb.replace("@fg", "")
        name = name.replace("@fg", "_fg")
        dial = MACRO_FG_DIAL
    if glb.startswith("env:"):
        # the artist's substrate pieces: their own materials (low glow), no override
        return {"name": name.replace(":", "_"), "kind": "procedural", "procedural": {
            "source": {"kind": "mesh", "asset": f"../meshes/env/{glb[4:]}_lod0.glb"},
            "distribution": {"kind": "points", "points": pts}, "castsShadow": False,
            "lod": {"cull": True, "count": 1, "byScreenSize": False, "maxDistance": 40.0}}}
    return {"name": name, "kind": "procedural", "procedural": {
        "source": {"kind": "mesh", "asset": f"../{HERO}/hero_{glb}_lod0.glb"},
        "distribution": {"kind": "points", "points": pts}, "castsShadow": False,
        "lod": {"cull": True, "count": 1, "byScreenSize": False, "maxDistance": 40.0},
        "material": {"baseColor": [MACRO_BASE.get(glb.split("_")[0], 1.0)] * 3, "roughness": 1.0, "metallic": 0.0,
                     "emissiveColor": [1.0, 1.0, 1.0], "emissiveIntensity": 4.0 * dial}}}


def macro(g, eye, tgt, rng_seed=43):
    """-> {glb: [records]} placed in the macro camera's frame: (right, forward) metres from the lens."""
    r = np.random.default_rng(rng_seed)
    ex, ez = eye[0], eye[2]
    fx, fz = tgt[0] - ex, tgt[2] - ez
    n = math.hypot(fx, fz)
    fx, fz = fx / n, fz / n
    rx, rz = -fz, fx                                   # screen right (checked by render)
    out = {}

    def _xz_y(right, fwd, sink):
        x, z = ex + rx * right + fx * fwd, ez + rz * right + fz * fwd
        y = float(g.height(np.array([x]), np.array([z]))[0])
        return x, y - sink, z

    def put(glb, right, fwd, s, sink=0.01, yaw=None):
        x, z = ex + rx * right + fx * fwd, ez + rz * right + fz * fwd
        y = float(g.height(np.array([x]), np.array([z]))[0])
        out.setdefault(glb, []).append(rec(x, y - sink * s, z, r.uniform(0, 6.28) if yaw is None else yaw, s))

    # the heroes: a tall emerald-cyan veilcap upper left (panel 3's mushroom), the reliquary behind right of centre,
    # blue lace fans at the left edge and behind
    put("veilcap_emeraldcyan", -0.32, 1.05, 1.7)
    put("veilcap", -0.55, 1.6, 1.3)
    put("reliquary", 0.28, 1.7, 1.25)
    put("lacefan", -0.7, 1.15, 1.6)
    put("lacefan", -0.95, 1.9, 2.0)
    r.uniform(0, 6.28)        # (the violet lacefan here was the violet smudge mid-frame; removed, its draw kept)
    # out-of-focus foreground: blue bead colonies bottom left, an emerald bead mound bottom right
    for k in range(5):                                # blue bead colonies in the bottom-left corner, soft in the blur
        put("beadcluster", r.uniform(-0.36, -0.2), r.uniform(0.26, 0.42), r.uniform(0.9, 1.4))
    # the near-right mound (0.5 m from the lens) is the foreground's soft out-of-focus glow, in emerald (panel 3's
    # bottom right); at the mounds' common dial it was a dark smooth slab across the lower right third
    put("glowmound_emerald@fg", 0.24, 0.42, 0.5, sink=0.2)
    for k in range(3):                                # emerald beads over the mound, bottom-right corner
        put("beadcluster_emerald", r.uniform(0.2, 0.34), r.uniform(0.3, 0.45), r.uniform(0.9, 1.3))
    # the carpet under everything: glow mats edge to edge from the lens to 2 m, blue first, emerald second
    for k in range(90):
        right, fwd = r.uniform(-0.9, 0.9), r.uniform(0.25, 2.2)
        put(["glowmat_blue", "glowmat_blue", "glowmat_blue", "glowmat_emerald", "glowmat_emerald"][int(r.integers(0, 5))],
            right * (0.4 + fwd * 0.5), fwd, r.uniform(1.0, 2.0))
    # the mid-ground bank: a carpet of mats and mounds, stalks rising out of it, filigree and bells between
    for k in range(70):
        right, fwd = r.uniform(-1.4, 1.4), r.uniform(0.6, 3.5)
        if abs(right) < 0.12 and fwd < 0.9:
            continue
        u = r.random()
        if u < 0.3:
            put(["glowmat_blue", "glowmat_blue", "glowmat_emerald", "glowmat"][int(r.integers(0, 4))], right, fwd,
                r.uniform(0.7, 1.4))
        elif u < 0.45:
            put(["glowmound_blue", "glowmound", "glowmound_emerald"][int(r.integers(0, 3))], right, fwd,
                r.uniform(0.35, 0.7), sink=0.1)
        elif u < 0.7:
            put(["bulbstalk", "bulbstalk_blue", "bulbstalk_blue", "bulbstalk_chartreuse"][int(r.integers(0, 4))],
                right, fwd, r.uniform(1.0, 2.2))
        elif u < 0.82:
            put(["filigree_blue", "filigree", "filigree_violet"][int(r.integers(0, 3))], right, fwd, r.uniform(1.0, 1.8))
        elif u < 0.9:
            put(["bellspire_blue", "bellspire"][int(r.integers(0, 2))], right, fwd, r.uniform(0.8, 1.4))
        elif u < 0.95:
            put(["lanternberry_blue", "lanternberry_gold"][int(r.integers(0, 2))], right, fwd, r.uniform(1.0, 1.6))
        else:
            put(["chalice_blue", "chalice"][int(r.integers(0, 2))], right, fwd, r.uniform(1.0, 1.6))
    # beyond: more mounds and stalks to 8 m so the bank continues into the haze
    for k in range(120):
        right, fwd = r.uniform(-3.5, 3.5), r.uniform(3.5, 9.0)
        u = r.random()
        glb = (["bulbstalk_blue", "bulbstalk", "glowmound_blue", "glowmat_blue", "bulbstalk_blue", "veilcap_blue",
                "chalice_blue", "lacefan"][int(r.integers(0, 8))])
        put(glb, right, fwd, r.uniform(1.2, 2.2))
    near_ok = lambda right, fwd: math.hypot(right, fwd) > 1.6   # (showcase P2-3; see below)
    # the substrate (spec 07 §8): moss carpets, surface roots and litter under and between everything, so the
    # organisms stand in a forest floor rather than on bare lumps; sunk a little so they hug the ground
    for k in range(26):
        right, fwd = r.uniform(-1.3, 1.3) * (0.5 + 0.3 * k / 26), r.uniform(0.35, 3.2)
        sc_ = r.uniform(0.8, 1.4)
        if near_ok(right, fwd):
            put("env:" + ["env_mosscarpet_a", "env_mosscarpet_b", "env_mosscarpet_a"][k % 3], right, fwd, sc_,
                sink=0.03)
        else:
            r.uniform(0, 6.28)                            # keep the stream: the skipped put's yaw draw
    for k in range(10):
        put("env:env_roots", r.uniform(-1.2, 1.2), r.uniform(0.6, 3.0), r.uniform(0.8, 1.5), sink=0.05)
    # (showcase P2-3) the moss carpets are smooth cushions: seen within ~1.6 m of the lens they read as a flat dark
    # slab across the lower right (found by rendering the library alone without DOF), so near ones are not placed
    near_ok = lambda right, fwd: math.hypot(right, fwd) > 1.6
    for k in range(14):
        put("env:env_litter", r.uniform(-1.4, 1.4), r.uniform(0.3, 3.5), r.uniform(0.9, 1.6), sink=0.0)
    # the forest behind: dark trunks and crowns 12-30 m back close the top of frame (panel 3 has no sky)
    for k in range(9):
        put("canopy", r.uniform(-14, 14), r.uniform(12, 30), r.uniform(0.8, 1.3), sink=0.02)
    return out


def macro_frame(eye, tgt):
    """(right, fwd) metres in the macro camera's ground frame -> world (x, z); the frame `macro()` places in."""
    ex, ez = eye[0], eye[2]
    fx, fz = tgt[0] - ex, tgt[2] - ez
    n = math.hypot(fx, fz)
    fx, fz = fx / n, fz / n
    rx, rz = -fz, fx

    def to_world(right, fwd):
        return ex + rx * right + fx * fwd, ez + rz * right + fz * fwd
    return to_world


def macro_lights(g, eye, tgt):
    """The subjects light their own ground (spec 09 §3 local coloured illumination, and the ground's depth): a small
    emerald-cyan pool under the veilcap's cap, a blue one at the reliquary's foot, and a faint violet back light low
    behind the pair that rims the ground's undulation."""
    w = macro_frame(eye, tgt)
    out = []
    for name, (right, fwd), lift, col, inten, rng in (
            ("veilcapPool", (-0.32, 1.05), 0.12, [0.25, 1.0, 0.75], 0.16, 0.9),
            ("reliquaryPool", (0.28, 1.7), 0.12, [0.3, 0.55, 1.0], 0.2, 0.9),
            ("groundRim", (0.0, 2.8), 0.1, [0.45, 0.35, 1.0], 0.35, 3.0)):
        x, z = w(right, fwd)
        y = float(g.height(np.array([x]), np.array([z]))[0]) + lift
        out.append({"name": name, "type": "point", "position": [round(x, 3), round(y, 3), round(z, 3)],
                    "color": col, "intensity": inten, "range": rng, "radius": 0.05, "castsShadow": False})
    return out
