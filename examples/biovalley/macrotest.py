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
    # a mossy bank climbing to the back-left, as panel 3's: the camera looks up into it
    s = np.clip((-0.55 * X + 0.85 * Z + 0.15) / 0.9, 0, 1.0)
    bank = 0.85 * s ** 1.5
    Y = h * 0.6 + bank
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


CARDS = False
LIBRARY = False
HERO_SCALE = 1.0
HX, HZ = -0.3, 0.5          # the hero: up the bank, left of centre
EYE = (0.25, 0.0, -0.3)      # the lens: low on the near slope, looking up into the bank


def build(variant, hero):
    M = valley.MESHES
    height = floor_mesh(M / "macro_floor.glb")
    r = np.random.default_rng(3)
    nodes = [node("floor", "macro_floor", [[0, 0, 0, 0, 0, 0, 1, 1, 1, 1]],
                  mat([0.01, 0.018, 0.03], 0.95, [0.1, 0.4, 1.0], 0.03, program="moss"))]
    layers = []

    # ---- the hero, at (0, 0, 0), 1.1 m from the lens
    if variant == "asset":
        # an asset artist's GLB: its own materials and textures (no material block, so nothing overrides them)
        place = [rec(HX, height(HX, HZ) - 0.005, HZ, 0.4, HERO_SCALE)]
        nodes.append({"name": "hero", "kind": "procedural", "procedural": {
            "source": {"kind": "mesh", "asset": f"../meshes/{hero}.glb"},
            "distribution": {"kind": "points", "points": place}, "castsShadow": False,
            "lod": {"cull": True, "count": 1, "byScreenSize": False, "maxDistance": 60.0}}})
        emit = hero.rsplit("_lod", 1)[0] + ".emit.json"
        if (M / emit).exists():
            layers.append(layer("hero_glints", ["hero"], emit, life.PALE, 1.2, maxDistance=60.0, sparsity=0.5,
                                flicker=0.2, flickerRate=0.6))
    elif variant == "blender":
        parts = {  # part: (material, emitter colour, emitter intensity)
            "stalk": (mat([0.02, 0.06, 0.04], 0.5, life.EMERALD, 0.3, program="bulb"), None, 0),
            "under": (mat([0.015, 0.03, 0.045], 0.5, life.CYAN, 0.04, doubleSided=True), None, 0),
            "gills": (mat([0.02, 0.04, 0.05], 0.4, [0.3, 1.0, 0.7], 0.08, doubleSided=True), life.PALE, 1.4),
            "cap": (mat([0.02, 0.05, 0.08], 0.35, [0.15, 0.85, 0.9], 0.35, program="cap", doubleSided=True),
                    life.CYAN, 1.0),
            "veil": (mat([0.02, 0.04, 0.06], 0.5, [0.3, 0.7, 1.0], 0.05, doubleSided=True), life.CYAN, 1.8),
        }
        place = [rec(HX, height(HX, HZ) - 0.01, HZ, 0.4, 1.7)]
        for part, (m, col, inten) in parts.items():
            glb = f"{hero}_{part}"
            if not (M / f"{glb}.glb").exists():
                continue
            nodes.append(node(f"hero_{part}", glb, place, m))
            if col is not None and (M / f"{glb}.emit.json").exists():
                layers.append(layer(f"hero_{part}", [f"hero_{part}"], f"{glb}.emit.json", col, inten,
                                    maxDistance=60.0, flicker=0.2, flickerRate=0.6, sparsity=0.75,
                                    wakeField="clumps", wakeGain=2.0))
    else:
        st, cap, e = life.fungus3d(4100, 0.42, 0.13, 0.09)
        st.write(M / "native_hero_stalk.glb", "s")
        cap.write(M / "native_hero_cap.glb", "c")
        life.write_emit(M / "native_hero.emit.json", e)
        place = [rec(HX, height(HX, HZ) - 0.01, HZ, 0.4, 1.0)]
        nodes.append(node("hero_stalk", "native_hero_stalk", place, mat([0.02, 0.06, 0.04], 0.5, life.EMERALD, 0.3, program="bulb")))
        nodes.append(node("hero_cap", "native_hero_cap", place,
                          mat([0.02, 0.05, 0.08], 0.35, [0.15, 0.85, 0.9], 0.35, program="cap", doubleSided=True)))
        layers.append(layer("hero_gills", ["hero_cap"], "native_hero.emit.json", life.PALE, 1.4, maxDistance=60.0))

    # ---- supporting organisms, dense and layered over the bank (panel 3 is packed edge to edge)
    sup = {"f3d_small": [], "f3d_bank": [], "tuft": {v: [] for v in range(ecosys.N_TUFT)},
           "frond": {v: [] for v in range(ecosys.N_FROND)}, "moss": {v: [] for v in range(ecosys.N_MOSS)}}
    for k in range(2600):
        x, z = r.uniform(-2.2, 1.6), r.uniform(-0.3, 2.6)
        de = math.hypot(x - EYE[0], z - EYE[2])
        if de < 0.2 or math.hypot(x - HX, z - HZ) < 0.07:
            continue
        y = height(x, z)
        roll = r.random()
        if roll < 0.55:      # a continuous skin of moss mounds of all sizes
            sup["moss"][int(r.integers(0, ecosys.N_MOSS))].append(rec(x, y - 0.02, z, r.uniform(0, 6.28),
                                                                       r.uniform(0.04, 0.16)))
        elif roll < (0.62 if LIBRARY else 0.8) and de > 0.45:
            sup["tuft"][ecosys.zone(x, z, 0.015)].append(rec(x, y - 0.01, z, r.uniform(0, 6.28),
                                                              r.uniform(0.15, 0.45)))
        elif roll < (0.7 if LIBRARY else 0.9):
            sup["f3d_small"].append(rec(x, y - 0.01, z, r.uniform(0, 6.28), r.uniform(0.4, 1.2)))
        elif roll < (0.72 if LIBRARY else 0.95):
            sup["frond"][int(r.integers(0, ecosys.N_FROND))].append(rec(x, y - 0.01, z, r.uniform(0, 6.28),
                                                                         r.uniform(0.08, 0.2)))
    if LIBRARY:
        # the approved hero library around the hero: each species in its own niche and scale (macro LOD 0)
        lib = {k: [] for k in ("lattice", "lanternberry", "filigree", "veilfern", "bellspire", "glowmound",
                               "glowmat", "reliquary", "chalice")}

        def at(dx, dz, s, yaw=None, name=None):
            x, z = HX + dx, HZ + dz
            lib[name].append(rec(x, height(x, z) - 0.01, z, r.uniform(0, 6.28) if yaw is None else yaw, s))
        # glowing mounds under and around the hero, and the big out-of-focus one bottom right
        for dx, dz, s in ((0.1, -0.05, 0.45), (-0.25, 0.1, 0.55), (0.35, 0.3, 0.5), (-0.5, 0.45, 0.6),
                          (0.55, -0.1, 0.4), (0.0, 0.55, 0.5), (-0.6, -0.2, 0.45)):
            at(dx, dz, s, name="glowmound")
        x, z = EYE[0] + 0.1, EYE[2] + 0.36
        lib["glowmound"].append(rec(x, height(x, z) - 0.06, z, 1.2, 0.42))
        for k in range(14):
            at(r.uniform(-0.9, 0.9), r.uniform(-0.4, 1.2), r.uniform(0.4, 0.9), name="glowmat")
        for dx, dz, s in ((-0.32, 0.08, 1.2), (0.42, 0.22, 1.0), (-0.75, 0.5, 1.4)):
            at(dx, dz, s, name="lattice")
        for k in range(9):
            at(r.uniform(-0.6, 0.7), r.uniform(-0.25, 0.7), r.uniform(0.8, 1.4), name="lanternberry")
        for k in range(10):
            at(r.uniform(-0.9, 0.9), r.uniform(-0.2, 1.0), r.uniform(0.8, 1.5), name="filigree")
        for dx, dz, s in ((-0.55, -0.05, 1.3), (0.6, 0.45, 1.1), (0.25, 0.75, 1.4)):
            at(dx, dz, s, name="veilfern")
        for dx, dz, s in ((0.3, 0.6, 1.3), (-0.45, 0.9, 1.6), (0.75, 1.0, 1.2)):
            at(dx, dz, s, name="bellspire")
        at(-0.9, 0.95, 1.3, name="reliquary")
        for name, pts in lib.items():
            if not pts:
                continue
            nodes.append({"name": f"lib_{name}", "kind": "procedural", "procedural": {
                "source": {"kind": "mesh", "asset": f"../meshes/hero/hero_{name}_lod0.glb"},
                "distribution": {"kind": "points", "points": pts}, "castsShadow": False,
                "lod": {"cull": True, "count": 1, "byScreenSize": False, "maxDistance": 60.0}}})
            if (M / f"hero/hero_{name}.emit.json").exists():
                layers.append(layer(f"lib_{name}", [f"lib_{name}"], f"hero/hero_{name}.emit.json", life.PALE, 1.0,
                                    maxDistance=60.0, sparsity=0.4, flicker=0.2, flickerRate=0.5))

    # the hero's company: a cluster of emerald bank fungi beside and behind it
    for dx, dz, s in (() if LIBRARY else ((0.1, 0.12, 0.55), (0.16, 0.05, 0.4), (-0.08, 0.2, 0.45), (0.22, 0.25, 0.6))):
        sup["f3d_bank"].append(rec(HX + dx, height(HX + dx, HZ + dz) - 0.01, HZ + dz, r.uniform(0, 6.28), s))
    # the big out-of-focus foreground mound, bottom right, and a smaller one bottom left
    for x, z, s in ((EYE[0] + 0.12, EYE[2] + 0.28, 0.2), (EYE[0] - 0.22, EYE[2] + 0.3, 0.12)):
        sup["moss"][0].append(rec(x, height(x, z) - 0.04, z, r.uniform(0, 6.28), s))
    nodes.append(node("sup_small_stalk", "f3d_small_stalk", sup["f3d_small"], mat([0.02, 0.05, 0.05], 0.5,
                                                                                   life.TEAL, 0.05)))
    nodes.append(node("sup_small_cap", "f3d_small_cap", sup["f3d_small"],
                      mat([0.02, 0.04, 0.09], 0.4, life.BLUE, 0.45, program="cap", doubleSided=True)))
    nodes.append(node("sup_bank_stalk", "f3d_bank_stalk", sup["f3d_bank"], mat([0.02, 0.06, 0.04], 0.5,
                                                                                 life.EMERALD, 0.2, program="bulb")))
    nodes.append(node("sup_bank_cap", "f3d_bank_cap", sup["f3d_bank"],
                      mat([0.02, 0.06, 0.05], 0.4, life.EMERALD, 0.3, program="cap", doubleSided=True)))
    layers.append(layer("sup_small", ["sup_small_cap"], "f3d_small.emit.json", life.CYAN, 0.9, maxDistance=60.0,
                        sparsity=0.4))
    layers.append(layer("sup_bank", ["sup_bank_cap"], "f3d_bank.emit.json", life.LIME, 1.2, maxDistance=60.0))
    for v, pts in sup["tuft"].items():
        if pts:
            nodes.append(node(f"tuft{v}s", f"tuft{v}_stalk", pts, mat([0.015, 0.04, 0.03], 0.6, life.EMERALD, 0.05)))
            nodes.append(node(f"tuft{v}b", f"tuft{v}_bulb", pts,
                              mat([0.02, 0.03, 0.04], 0.4, ecosys.TUFT_COLORS[v], 1.6, program="bulb")))
            layers.append(layer(f"tips{v}", [f"tuft{v}b"], f"tuft{v}.emit.json", ecosys.TUFT_COLORS[v], 0.35,
                                maxDistance=60.0, nearFade=0.35))
    for v, pts in sup["frond"].items():
        if pts:
            nodes.append(node(f"frond{v}", f"frond{v}", pts,
                              mat([0.01, 0.04, 0.03], 0.6, [life.CYAN, life.EMERALD, life.BLUE][v], 0.5,
                                  program="frond", doubleSided=True, alphaCutoff=0.5, alphaMode="mask")))
    mossc = [(life.EMERALD, [0.012, 0.05, 0.025]), (life.BLUE, [0.008, 0.02, 0.06]),
             (life.EMERALD, [0.015, 0.045, 0.03]), (life.CYAN, [0.008, 0.035, 0.05])]
    for v, pts in sup["moss"].items():
        if pts:
            col, base = mossc[v % 4]
            nodes.append(node(f"moss{v}", f"moss{v}", pts, mat(base, 0.95, col, 0.12, program="moss")))
            layers.append(layer(f"mossDots{v}", [f"moss{v}"], f"moss{v}.emit.json", col, 1.4, sparsity=0.35,
                                maxDistance=60.0, wakeField="clumps", wakeGain=2.5, size=1.6))

    # ---- dark vegetation behind: a curtain of crowns and trunks 4-9 m back, and shrubs
    back = [rec(x, height(max(min(x, 3.9), -3.9), 3.9) - 0.3, z, r.uniform(0, 6.28), r.uniform(0.3, 0.5))
            for x, z in ((-3.0, 6.0), (0.5, 7.5), (3.5, 5.5), (-6.0, 8.5), (6.0, 9.0))]
    # (a back curtain of grove meshes read as a hard-edged slab at this range: left to the haze instead)
    if CARDS:
        # Approach C (hybrid 2.5D): three depths of painted silhouette cards behind the bank, facing the lens
        import cards
        for k in range(4):
            cards.card_texture(M / f"card{k}.png", 500 + k, trunks=2 + k, strands=20 + 10 * k, canopy=0.6 + 0.2 * k,
                               glints=60 + 40 * k)
        cards.card_mesh(M / "card.glb")
        yaw = math.atan2(EYE[0] - HX, EYE[2] - HZ)
        for k, (dist, w, h, dx) in enumerate(((1.3, 3.0, 2.6, -0.5), (2.2, 4.6, 3.8, 0.6), (3.3, 6.5, 5.5, 0.0),
                                              (5.0, 11.0, 8.0, 0.3))):
            cx = HX + dx - dist * math.sin(yaw)
            cz = HZ - dist * math.cos(yaw)
            y0 = height(max(min(cx, 3.9), -3.9), max(min(cz, 3.9), -3.9)) - 0.1
            nodes.append({"name": f"card{k}", "kind": "procedural", "procedural": {
                "source": {"kind": "mesh", "asset": "../meshes/card.glb"},
                "distribution": {"kind": "points", "points": [rec(cx, y0, cz, yaw + math.pi, w, h)]},
                "castsShadow": False, "material": mat([1.0, 1.0, 1.0], 0.9, [0.1, 0.25, 0.6], 0.0,
                                                      doubleSided=True, alphaCutoff=0.4, alphaMode="mask",
                                                      baseColorTexture=f"../meshes/card{k}.png")}})
    shr = [rec(x, height(max(min(x, 3.9), -3.9), min(z, 3.9)) - 0.05, z, r.uniform(0, 6.28), r.uniform(0.25, 0.5))
           for x, z in ((-1.6, 2.6), (1.4, 3.0), (-0.4, 3.6), (2.4, 2.2), (-2.6, 3.4))]


    eye = [EYE[0], height(EYE[0], EYE[2]) + 0.14, EYE[2]]
    tgt = [HX + 0.2, height(HX, HZ) + 0.12, HZ]
    sc = valley.scene({}, eye, tgt, 50.0, 0.02, 26000.0, nodes, None, (), {"reach": 30.0, "steps": 12,
                                                                            "emission": 0.0})
    sc["nodes"] = [n for n in sc["nodes"] if n["name"] in ("moon",)] + nodes
    sc["nodes"].append({"name": "clumps", "kind": "field", "position": [0, 0, 0],
                        "field": {"kind": "noise", "frequency": 2.5, "seed": 61, "strength": 1.0,
                                  "falloff": {"kind": "none"}}})
    sc["environment"]["styledGroundAmbient"] = [0.0, 0.0, 0.0]
    sc["ecosystem"] = {"enabled": True, "spriteRadius": 1.5, "maxSprites": 65536, "layers": layers}
    sc["environment"]["volumeDensity"] = 0.02
    sc["environment"]["fogHeight"] = 0.6
    sc["environment"]["fogHeightFalloff"] = 1.5
    # the back light: low behind the hero, cyan, rimming the cap and the veil; a faint violet fill from the left
    sc["lights"] += [
        {"name": "blueFill", "type": "point", "position": [EYE[0] - 0.5, height(EYE[0], EYE[2]) + 0.8, EYE[2] - 0.6],
         "color": [0.12, 0.25, 1.0], "intensity": 0.6, "range": 5.0, "radius": 0.3, "castsShadow": False},
        {"name": "rim", "type": "point", "position": [HX + 0.2, height(HX, HZ) + 0.5, HZ + 0.5], "color": [0.35, 0.75, 1.0],
         "intensity": 2.0, "range": 3.0, "radius": 0.05, "castsShadow": False}
    ]
    name = variant + ("-cards" if CARDS else "") + ("-library" if LIBRARY else "")
    proj = valley.project(f"macrotest-{name}", f"macrotest-{name}.scene.json")
    proj["parameters"].update({"post/dof/enabled": True, "post/dof/focusDistance": 1.0,
                               "post/dof/focusRange": 0.2, "post/dof/maxRadius": 18.0})
    (OUT / f"macrotest-{name}.scene.json").write_text(json.dumps(sc))
    (OUT / f"macrotest-{name}.json").write_text(json.dumps(proj, indent=1))
    print("wrote", OUT / f"macrotest-{name}.json")


if __name__ == "__main__":
    variant = "blender"
    hero = "hero_lantern"
    if "--variant" in sys.argv:
        variant = sys.argv[sys.argv.index("--variant") + 1]
    if "--hero" in sys.argv:
        hero = sys.argv[sys.argv.index("--hero") + 1]
    CARDS = "--cards" in sys.argv
    LIBRARY = "--library" in sys.argv
    if "--scale" in sys.argv:
        HERO_SCALE = float(sys.argv[sys.argv.index("--scale") + 1])
    build(variant, hero)
