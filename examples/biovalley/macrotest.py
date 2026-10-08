"""The VFX art pass's macro test scene (02-vfx-art-pass.md, Phase 2, "First required render").

    python3 examples/biovalley/macrotest.py [--hero <prefix>] [--variant native|blender]

One hero organism on a mossy forest floor, supporting organisms around it, dark vegetation behind, a cool key from
the moon and a back light that rims the hero, local emission, foreground and background layers and depth of field.
It is the same composition for every production method (Phase 3), so the method is the only variable:

  native   the hero is the valley's own numpy organism (life.fungus3d at hero scale)
  blender  the hero is a Blender-authored GLB set (hero_<name>_<part>.glb + .emit.json; meshes/ or meshes/hero/)

Writes shots/macrotest-<variant>.{scene,}.json. The scene is lit like the valley's macro shot, and uses the valley's
sky, programs and colour vocabulary, so what is learned here transfers.
"""
from __future__ import annotations

import json
import math
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import ecosys  # noqa: E402
import flora  # noqa: E402
import life  # noqa: E402
import populate  # noqa: E402
import valley  # noqa: E402
from geom import Mesh, fbm2, grid_faces  # noqa: E402

rec = populate.rec
mat = valley.mat
OUT = valley.SHOTS


def floor_mesh(path, size=8.0, n=160, seed=5):
    """A forest floor: low hummocks and a root ridge, so the ground itself has silhouettes at lens height."""
    xs = np.linspace(-size / 2, size / 2, n)
    X, Z = np.meshgrid(xs, xs, indexing="ij")
    h = 0.12 * (fbm2(X / 0.9 + 3, Z / 0.9, seed, 4) - 0.5) + 0.05 * (fbm2(X / 0.25, Z / 0.25, seed + 1, 3) - 0.5)
    ridge = 0.16 * np.exp(-((Z - 1.4 - 0.25 * np.sin(X * 1.3)) / 0.35) ** 2)     # a fallen root behind the hero
    Y = h + ridge
    m = Mesh()
    m.add(np.stack([X, Y, Z], -1).reshape(-1, 3), grid_faces(n, n, False),
          np.stack([np.zeros(n * n), np.clip(Y.ravel() * 4 + 0.5, 0, 1)], -1))
    m.write(path, "floor", outward_from=lambda P: np.tile([0, 1.0, 0], (len(P), 1)))

    def height(x, z):
        i = int(np.clip((x + size / 2) / size * (n - 1), 0, n - 1))
        k = int(np.clip((z + size / 2) / size * (n - 1), 0, n - 1))
        return float(Y[i, k])
    return height


def node(name, glb, pts, material, maxd=60.0):
    return {"name": name, "kind": "procedural", "procedural": {
        "source": {"kind": "mesh", "asset": f"../meshes/{glb}.glb"},
        "distribution": {"kind": "points", "points": pts}, "castsShadow": False,
        "lod": {"cull": True, "count": 1, "byScreenSize": False, "maxDistance": maxd}, "material": material}}


def layer(name, hosts, template, color, intensity, **kw):
    return ecosys.layer(name, hosts, template, color, intensity, **kw)


def build(variant, hero):
    M = valley.MESHES
    height = floor_mesh(M / "macro_floor.glb")
    r = np.random.default_rng(3)
    nodes = [node("floor", "macro_floor", [[0, 0, 0, 0, 0, 0, 1, 1, 1, 1]],
                  mat([0.01, 0.018, 0.03], 0.95, [0.1, 0.4, 1.0], 0.03, program="moss"))]
    layers = []

    # ---- the hero, at (0, 0, 0), 1.1 m from the lens
    if variant == "blender":
        parts = {  # part: (material, emitter colour, emitter intensity)
            "stalk": (mat([0.03, 0.045, 0.06], 0.5, life.EMERALD, 0.008), None, 0),
            "under": (mat([0.02, 0.04, 0.05], 0.5, life.CYAN, 0.12, doubleSided=True), None, 0),
            "gills": (mat([0.02, 0.05, 0.05], 0.4, [0.3, 1.0, 0.7], 0.35, doubleSided=True), life.PALE, 2.2),
            "cap": (mat([0.02, 0.05, 0.08], 0.35, [0.15, 0.85, 0.9], 0.35, program="cap", doubleSided=True),
                    life.CYAN, 1.0),
            "veil": (mat([0.03, 0.05, 0.07], 0.5, [0.3, 0.7, 1.0], 0.12, doubleSided=True), life.PALE, 1.4),
        }
        place = [rec(0.0, height(0, 0) - 0.01, 0.0, 0.4, 1.0)]
        for part, (m, col, inten) in parts.items():
            glb = f"{hero}_{part}"
            if not (M / f"{glb}.glb").exists():
                continue
            nodes.append(node(f"hero_{part}", glb, place, m))
            if col is not None and (M / f"{glb}.emit.json").exists():
                layers.append(layer(f"hero_{part}", [f"hero_{part}"], f"{glb}.emit.json", col, inten,
                                    maxDistance=60.0, flicker=0.2, flickerRate=0.6))
    else:
        st, cap, e = life.fungus3d(4100, 0.42, 0.13, 0.09)
        st.write(M / "native_hero_stalk.glb", "s")
        cap.write(M / "native_hero_cap.glb", "c")
        life.write_emit(M / "native_hero.emit.json", e)
        place = [rec(0.0, height(0, 0) - 0.01, 0.0, 0.4, 1.0)]
        nodes.append(node("hero_stalk", "native_hero_stalk", place, mat([0.03, 0.045, 0.06], 0.5, life.EMERALD, 0.008)))
        nodes.append(node("hero_cap", "native_hero_cap", place,
                          mat([0.02, 0.05, 0.08], 0.35, [0.15, 0.85, 0.9], 0.35, program="cap", doubleSided=True)))
        layers.append(layer("hero_gills", ["hero_cap"], "native_hero.emit.json", life.PALE, 1.4, maxDistance=60.0))

    # ---- supporting organisms: a second smaller lantern group behind-right, fungi, lattices, bulb stalks, fronds
    sup = {"f3d_small": [], "f3d_bank": [], "lattice0": [], "tuft": {v: [] for v in range(ecosys.N_TUFT)},
           "frond": {v: [] for v in range(ecosys.N_FROND)}, "moss": {v: [] for v in range(ecosys.N_MOSS)}}
    for k in range(140):
        a = r.uniform(-1.0, 1.0) * 1.1
        d = r.uniform(0.35, 3.2)
        x, z = d * math.sin(a), d * math.cos(a) - 1.1 + 1.1
        if math.hypot(x, z) < 0.18:
            continue
        y = height(x, z)
        roll = r.random()
        if roll < 0.35:
            sup["tuft"][ecosys.zone(x, z, 0.02)].append(rec(x, y - 0.01, z, r.uniform(0, 6.28), r.uniform(0.3, 0.6)))
        elif roll < 0.55:
            sup["f3d_small"].append(rec(x, y - 0.01, z, r.uniform(0, 6.28), r.uniform(0.6, 1.4)))
        elif roll < 0.62:
            sup["lattice0"].append(rec(x, y, z, r.uniform(0, 6.28), r.uniform(0.8, 1.5)))
        elif roll < 0.72:
            sup["frond"][int(r.integers(0, ecosys.N_FROND))].append(rec(x, y - 0.01, z, r.uniform(0, 6.28),
                                                                         r.uniform(0.15, 0.3)))
        else:
            sup["moss"][int(r.integers(0, ecosys.N_MOSS))].append(rec(x, y - 0.03, z, r.uniform(0, 6.28),
                                                                       r.uniform(0.12, 0.35)))
    # foreground mounds left and right, out of focus
    for x, z, s in ((-0.32, -0.62, 0.32), (0.36, -0.58, 0.28), (-0.6, -0.2, 0.4)):
        sup["moss"][1].append(rec(x, height(x, z) - 0.05, z, r.uniform(0, 6.28), s))
    sup["f3d_bank"] += [rec(0.55, height(0.55, 0.9), 0.9, 1.0, 1.1), rec(0.7, height(0.7, 1.1), 1.1, 2.0, 0.8)]
    nodes.append(node("sup_small_stalk", "f3d_small_stalk", sup["f3d_small"], mat([0.02, 0.04, 0.05], 0.5)))
    nodes.append(node("sup_small_cap", "f3d_small_cap", sup["f3d_small"],
                      mat([0.02, 0.04, 0.08], 0.4, life.BLUE, 0.25, program="cap", doubleSided=True)))
    nodes.append(node("sup_bank_stalk", "f3d_bank_stalk", sup["f3d_bank"], mat([0.02, 0.04, 0.05], 0.5)))
    nodes.append(node("sup_bank_cap", "f3d_bank_cap", sup["f3d_bank"],
                      mat([0.02, 0.05, 0.06], 0.4, life.EMERALD, 0.3, program="cap", doubleSided=True)))
    nodes.append(node("sup_lattice", "lattice0", sup["lattice0"],
                      mat([0.03, 0.03, 0.09], 0.5, [0.3, 0.22, 1.0], 0.25, program="cap", doubleSided=True)))
    layers.append(layer("sup_small", ["sup_small_cap"], "f3d_small.emit.json", life.PALE, 0.8, maxDistance=60.0))
    layers.append(layer("sup_bank", ["sup_bank_cap"], "f3d_bank.emit.json", life.LIME, 1.0, maxDistance=60.0))
    for v, pts in sup["tuft"].items():
        if pts:
            nodes.append(node(f"tuft{v}s", f"tuft{v}_stalk", pts, mat([0.015, 0.03, 0.03], 0.6)))
            nodes.append(node(f"tuft{v}b", f"tuft{v}_bulb", pts,
                              mat([0.02, 0.03, 0.04], 0.4, ecosys.TUFT_COLORS[v], 1.2, program="bulb")))
            layers.append(layer(f"tips{v}", [f"tuft{v}b"], f"tuft{v}.emit.json", ecosys.TUFT_COLORS[v], 0.35,
                                maxDistance=60.0))
    for v, pts in sup["frond"].items():
        if pts:
            nodes.append(node(f"frond{v}", f"frond{v}", pts,
                              mat([0.01, 0.03, 0.03], 0.6, [life.CYAN, life.EMERALD, life.BLUE][v], 0.35,
                                  program="frond", doubleSided=True, alphaCutoff=0.5)))
    for v, pts in sup["moss"].items():
        if pts:
            nodes.append(node(f"moss{v}", f"moss{v}", pts,
                              mat([0.008, 0.016, 0.03], 0.95, [life.BLUE, life.TEAL][v % 2], 0.08, program="moss")))
            layers.append(layer(f"mossDots{v}", [f"moss{v}"], f"moss{v}.emit.json", [life.BLUE, life.TEAL][v % 2],
                                0.4, sparsity=0.6, maxDistance=60.0))

    # ---- dark vegetation behind: a curtain of crowns and trunks 4-9 m back, and shrubs
    back = [rec(x, height(max(min(x, 3.9), -3.9), 3.9) - 0.3, z, r.uniform(0, 6.28), r.uniform(0.3, 0.5))
            for x, z in ((-3.0, 6.0), (0.5, 7.5), (3.5, 5.5), (-6.0, 8.5), (6.0, 9.0))]
    nodes.append(node("back_wood", "grovehq0_wood", back, mat(populate.BARK, 0.85), 200.0))
    nodes.append(node("back_leaf", "grovehq0_leaf", back, mat(populate.LEAF, 0.7, program="crown"), 200.0))
    shr = [rec(x, height(max(min(x, 3.9), -3.9), min(z, 3.9)) - 0.05, z, r.uniform(0, 6.28), r.uniform(0.25, 0.5))
           for x, z in ((-1.6, 2.6), (1.4, 3.0), (-0.4, 3.6), (2.4, 2.2), (-2.6, 3.4))]
    nodes.append(node("shrub_wood", "shrub0_wood", shr, mat(populate.BARK, 0.85)))
    nodes.append(node("shrub_leaf", "shrub0_leaf", shr, mat([0.01, 0.02, 0.03], 0.7)))

    eye = [0.12, height(0.12, -0.62) + 0.13, -0.62]
    tgt = [0.02, height(0, 0) + 0.26, 0.0]
    sc = valley.scene({}, eye, tgt, 50.0, 0.02, 26000.0, nodes, None, (), {"reach": 30.0, "steps": 12,
                                                                            "emission": 0.0})
    sc["nodes"] = [n for n in sc["nodes"] if n["name"] in ("moon",)] + nodes
    sc["ecosystem"] = {"enabled": True, "spriteRadius": 1.5, "maxSprites": 65536, "layers": layers}
    sc["environment"]["volumeDensity"] = 0.02
    sc["environment"]["fogHeight"] = 0.6
    sc["environment"]["fogHeightFalloff"] = 1.5
    # the back light: low behind the hero, cyan, rimming the cap and the veil; a faint violet fill from the left
    sc["lights"] += [
        {"name": "rim", "type": "point", "position": [0.25, height(0, 0) + 0.55, 0.9], "color": [0.35, 0.75, 1.0],
         "intensity": 2.0, "range": 3.0, "radius": 0.05, "castsShadow": False}
    ]
    proj = valley.project(f"macrotest-{variant}", f"macrotest-{variant}.scene.json")
    proj["parameters"].update({"post/dof/enabled": True, "post/dof/focusDistance": 0.66,
                               "post/dof/focusRange": 0.14, "post/dof/maxRadius": 18.0})
    (OUT / f"macrotest-{variant}.scene.json").write_text(json.dumps(sc))
    (OUT / f"macrotest-{variant}.json").write_text(json.dumps(proj, indent=1))
    print("wrote", OUT / f"macrotest-{variant}.json")


if __name__ == "__main__":
    variant = "blender"
    hero = "hero_lantern"
    if "--variant" in sys.argv:
        variant = sys.argv[sys.argv.index("--variant") + 1]
    if "--hero" in sys.argv:
        hero = sys.argv[sys.argv.index("--hero") + 1]
    build(variant, hero)
