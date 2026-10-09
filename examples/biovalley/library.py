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

import numpy as np

import populate
from geom import fbm2

rec = populate.rec
HERO = "meshes/hero"


# Per-scene emission dials (the artist's strengths are all 4.0). A material block with neutral factors keeps the
# asset's textures and only retunes the emissive strength (ADR-1283 keeps its cutouts and two-sidedness).
DIAL = {"glowmat": 1.4, "filigree": 1.6, "glowmound": 2.4, "veilfern": 2.6, "lattice": 2.6, "bellspire": 3.0,
        "lanternberry": 3.0, "veilcap": 4.0, "reliquary": 3.0, "glowsedge": 3.0, "riverrock_a": 2.0,
        "riverrock_b": 2.0, "riverrock_c": 2.0, "riverrock_d": 2.0, "shadowbough": 2.0, "darkfern": 1.0,
        "canopy": 2.0, "canopygrove": 2.0, "bulbstalk": 3.0}


# Base-colour factors (multiplying the textures): the pale aprons and lace read as snow under the moon at range
BASE = {"glowmat": [0.3, 0.38, 0.45], "glowmound": [0.45, 0.55, 0.6], "lattice": [0.4, 0.4, 0.55],
        "filigree": [0.5, 0.55, 0.6]}


def node(name, species, pts, lod, maxd):
    proc = {"source": {"kind": "mesh", "asset": f"../{HERO}/hero_{species}_lod{lod}.glb"},
            "distribution": {"kind": "points", "points": pts}, "castsShadow": False,
            "lod": {"cull": True, "count": 1, "byScreenSize": False, "maxDistance": maxd}}
    if species in DIAL:
        proc["material"] = {"baseColor": BASE.get(species, [1.0, 1.0, 1.0]), "roughness": 1.0, "metallic": 0.0,
                            "emissiveColor": [1.0, 1.0, 1.0], "emissiveIntensity": DIAL[species]}
    return {"name": name, "kind": "procedural", "procedural": proc}


def emit_layer(name, hosts, species, color, intensity, mirror=None, **kw):
    d = {"name": name, "hosts": hosts, "template": f"../{HERO}/hero_{species}.emit.json", "color": color,
         "intensity": intensity, "excitedColor": color, "excitedIntensity": 0.0, "size": 1.0, "breath": 0.25,
         "breathRate": 0.06, "flicker": 0.2, "flickerRate": 0.6, "sparsity": 0.3, "maxDistance": 400.0}
    d.update(kw)
    if mirror:
        d.update(mirror)
    return d


def assemble(groups, eye, near=10.0, far=400.0):
    """groups: {species: [records]} -> (nodes, {species: [host names]}). LOD 0 within `near` m of the eye."""
    nodes, hosts = [], {}
    for sp, recs in groups.items():
        if not recs:
            continue
        near_r = [r for r in recs if math.hypot(r[0] - eye[0], r[2] - eye[2]) < near]
        far_r = [r for r in recs if math.hypot(r[0] - eye[0], r[2] - eye[2]) >= near]
        for tag, rs, lod, md in (("n", near_r, 0, near * 1.5), ("f", far_r, 1, far)):
            if rs:
                nm = f"lib_{sp}_{tag}"
                nodes.append(node(nm, sp, rs, lod, md))
                hosts.setdefault(sp, []).append(nm)
    return nodes, hosts


def bank(g, eye, head, water_level, rng_seed=31):
    """The riverbank: the stream's two banks out to ~80 m upstream, a focal veilcap on the near (left) bank."""
    r = np.random.default_rng(rng_seed)
    groups = {k: [] for k in ("veilcap", "glowmound", "glowmat", "lanternberry", "filigree", "veilfern", "bellspire",
                              "lattice", "reliquary", "glowsedge", "darkfern", "riverrock_a", "riverrock_b",
                              "riverrock_c", "riverrock_d", "shadowbough")}

    def put(sp, x, z, s, sink=0.01):
        y = float(g.height(np.array([x]), np.array([z]))[0])
        groups[sp].append(rec(x, y - sink * s, z, r.uniform(0, 6.28), s))

    ex, ez = eye[0], eye[2]
    fx, fz = math.sin(head), math.cos(head)          # forward (x, z)
    lx, lz = fz, -fx                                  # left... (rotate +90 about y): (z, -x) points to the left
    # candidates over both banks ahead of the lens
    for k in range(16000):
        d = r.uniform(5.0, 90.0)
        side = r.uniform(-30.0, 30.0)
        grow = 0.55 + 0.45 * min(d / 40.0, 1.0)          # nearer plants smaller, so the lens is not blocked
        x = ex + fx * d + lx * side
        z = ez + fz * d + lz * side
        if g.wet(np.array([x]), np.array([z]))[0]:
            continue
        nw = any(g.wet(np.array([x + dx]), np.array([z + dz]))[0] for dx, dz in ((1.5, 0), (-1.5, 0), (0, 1.5),
                                                                                  (0, -1.5)))
        patch = float(fbm2(np.array([x / 7.0]), np.array([z / 7.0]), 41, 3)[0])
        u = r.random()
        if nw and u < 0.18:
            put("veilfern", x, z, grow * r.uniform(1.6, 3.2))                 # the waterline: broad fronds leaning out
        elif nw and u < 0.55:
            put("glowsedge", x, z, r.uniform(1.0, 2.0))                       # sedge clumps at the water's edge
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
            put(["filigree", "lanternberry", "filigree"][int(r.integers(0, 3))], x, z, grow * r.uniform(1.4, 3.2))
        elif patch < 0.3 and u < 0.06:
            put("lattice", x, z, grow * r.uniform(1.8, 3.5))
        elif u < 0.012:
            put("bellspire", x, z, grow * r.uniform(2.0, 3.5))
    # the focal veilcap: on the near (left) bank, 10-12 m ahead, very large (about 2.5 m tall), and a smaller pair
    for d, side, s in ((7.5, 3.2, 7.0), (10.5, 5.5, 4.0), (6.5, 5.5, 2.6)):
        x, z = ex + fx * d + lx * side, ez + fz * d + lz * side
        put("veilcap", x, z, s)
    # the near-left foreground (panel 1's dark rocks, moss and glowing stalks at the lens)
    for k in range(26):
        d = r.uniform(1.6, 6.0)
        side = r.uniform(1.0, 5.5)
        x, z = ex + fx * d + lx * side, ez + fz * d + lz * side
        if g.wet(np.array([x]), np.array([z]))[0]:
            continue
        u = r.random()
        if u < 0.3:
            put(["riverrock_a", "riverrock_b", "riverrock_c"][int(r.integers(0, 3))], x, z, r.uniform(0.5, 1.0),
                sink=0.15)
        elif u < 0.55:
            put("darkfern", x, z, r.uniform(2.0, 4.0))
        elif u < 0.75:
            put("glowmound", x, z, r.uniform(0.5, 1.1), sink=0.05)
        elif u < 0.9:
            put("lanternberry", x, z, r.uniform(1.0, 2.0))
        else:
            put("bellspire", x, z, r.uniform(1.2, 2.0))
    # stones standing in the stream to the right of the lens, with sedge on them: the water is framed, not empty
    for d, side, s in ((4.5, -2.5, 0.9), (7.0, -4.5, 1.3), (11.0, -3.0, 1.1), (15.0, -6.5, 1.5), (6.0, -7.5, 0.8),
                       (19.0, -2.0, 1.2)):
        x, z = ex + fx * d + lx * side, ez + fz * d + lz * side
        y = float(g.water_level(np.array([x]), np.array([z]))[0])
        if y < -1e8:
            y = float(g.height(np.array([x]), np.array([z]))[0])
        sp = ["riverrock_a", "riverrock_b"][int(r.integers(0, 2))]
        groups[sp].append(rec(x, y - 0.25 * s, z, r.uniform(0, 6.28), s))
        groups["glowsedge"].append(rec(x + 0.2, y + 0.2 * s, z + 0.1, r.uniform(0, 6.28), r.uniform(1.0, 1.6)))
    # the framing bough: rooted behind-left of the lens, its +X limb aimed over the stream to the right
    bx, bz = ex - fx * 2.0 + lx * 14.0, ez - fz * 2.0 + lz * 14.0
    aim = (fx * 0.85 - lx * 0.5, fz * 0.85 - lz * 0.5)
    y = float(g.height(np.array([bx]), np.array([bz]))[0])
    groups["shadowbough"].append(rec(bx, y - 0.5, bz, math.atan2(-aim[1], aim[0]), 0.65))
    for d, side, s in ((22.0, -6.0, 3.0), (30.0, 5.0, 3.5)):
        x, z = ex + fx * d + lx * side, ez + fz * d + lz * side
        put("reliquary", x, z, s)
    return groups
