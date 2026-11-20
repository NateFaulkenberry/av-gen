"""Neon Dreams kit core: materials, the Asset builder, wall primitives and THE WINDOW RULE.

THE WINDOW RULE (owner, iteration 2). Every pane in the kit is made by `pane_opening` (or `porthole`):
  * the pane sits in a modelled opening: recessed behind the wall plane, with reveals, a frame and (for windows) a sill;
  * the opening lies inside one facade cell (a 2.4 m bay x 3.2 m floor), so it is aligned to floors and bays and can never
    cross a module edge, a corner, a pipe or another window;
  * nothing emissive is ever a quad floating in front of, or behind, a wall.
`audit(asset)` proves it on the finished mesh: every opening is inside its wall cell with a margin, no two openings on a
plane overlap, rays from the opening outward hit nothing (no pipe, slab, unit or other block in front of a window), a ray
into the opening lands on the pane at the recess depth, and every polygon carrying a pane material belongs to an opening.

Pane UVs are collapsed to a point: u = brightness level (16 steps of win_levels.png), v = a hash unique to the window.
A drawn blind or curtains are the same pane split into parts with lower levels and the SAME v, so a program that lights
panes by hashing v lights the whole window together.
"""
import bpy, math, random, sys, os, json
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from nd_lib import MB, new_mat, node, hex2lin, link  # noqa

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
arg = lambda n, d: argv[argv.index(n) + 1] if n in argv else d
OUT = arg("--out", os.path.join(HERE, "out"))
os.makedirs(OUT, exist_ok=True)
FLOOR, BAY = 3.2, 2.4
Z = Vector((0, 0, 1))

# ------------------------------------------------------------------------------------------- the surface atlas (iteration 8)
# One shared texture set (meshes/tex/nd_surface_*.png, written by nd_surface_atlas.py) on the four unlit surface roles.
# The GLBs reference it by URI (stored once); export() attaches it and each asset's base colour factors after Blender
# has written the file. The atlas multiplies the factor: the HUE of a building is its factor (SCHEME), the atlas carries
# value, grime, joints and relative tint. Facade walls are mapped cell by cell (a 2.4 m bay x a 3.2 m floor is one tile).
TEX = arg("--tex", os.path.join(OUT, "tex"))
SURF = json.load(open(os.path.join(TEX, "nd_surface.json"))) if os.path.exists(os.path.join(TEX, "nd_surface.json")) else None
SURF_MATS = {"facade": "roof", "trim": "trim", "metal": "metal", "deck": "plate_a"}      # role -> the patch its boxes and cylinders use
FCTX = {"cell": None, "win": False, "row": None, "base_z": None}
# muted: in the engine's albedo the first plum was pink. Second pass: at 27 s (60 to 200 m, violet ambient) the first
# factors all read one mauve, so the chroma is 1.4x and bone a step lighter; the atlas rows are near neutral.
PLUM, SLATE, RUST, BONE, INK = (0.066, 0.036, 0.062), (0.032, 0.047, 0.078), (0.094, 0.044, 0.028), (0.104, 0.094, 0.080), (0.030, 0.029, 0.046)
TIMBER, STEEL, STONE = (0.064, 0.050, 0.046), (0.046, 0.048, 0.060), (0.060, 0.054, 0.064)
# per asset: the facade's base colour factor (linear), the finish rows its blocks take in turn above the shop floor
# (0 light render, 1 dark painted render, 2 corrugated sheet; the shop floor is always 3, glazed tile), the deck's factor
SCHEME = {"_default": dict(color=INK, rows=[0, 1], deck=STEEL),
          "mooring_shophouse_a": dict(color=PLUM, rows=[0, 1]), "mooring_shophouse_b": dict(color=SLATE, rows=[1, 2, 0]),
          "mooring_shophouse_c": dict(color=BONE, rows=[0]), "mooring_shophouse_d": dict(color=RUST, rows=[2]),
          "mooring_shophouse_e": dict(color=SLATE, rows=[1, 0]), "mooring_shophouse_f": dict(color=PLUM, rows=[0, 2]),
          "mooring_shophouse_g": dict(color=BONE, rows=[2, 0, 1]), "mooring_shophouse_h": dict(color=INK, rows=[1, 0, 2]),
          "mooring_shophouse_i": dict(color=RUST, rows=[0]), "mooring_shophouse_j": dict(color=BONE, rows=[2]),
          "mooring_stack_a": dict(color=SLATE, rows=[1, 0, 1, 2]), "mooring_stack_b": dict(color=PLUM, rows=[0, 1, 2, 1]),
          "mooring_stack_c": dict(color=INK, rows=[2, 1, 0]), "mooring_stack_d": dict(color=RUST, rows=[1, 2, 0, 1]),
          "mooring_stack_e": dict(color=BONE, rows=[0, 1]), "mooring_gallery_slab": dict(color=(0.066, 0.061, 0.060), rows=[0]),
          "mooring_corner_tower": dict(color=(0.050, 0.035, 0.050), rows=[1, 0]), "mooring_capsule_tower": dict(color=SLATE, rows=[2]),
          "mooring_station": dict(color=RUST, rows=[2], deck=TIMBER), "kiosk": dict(color=BONE, rows=[2]), "lone_lamp_house": dict(color=INK, rows=[1]),
          "lighthouse_a": dict(color=BONE, rows=[0]), "lighthouse_b": dict(color=BONE, rows=[0]), "lighthouse_c": dict(color=BONE, rows=[0]),
          "pavilion": dict(color=PLUM, rows=[1], deck=TIMBER),
          "deck_quay_6m": dict(deck=TIMBER), "deck_stair_3m2": dict(deck=TIMBER), "gangway_10m": dict(deck=TIMBER), "gangway_garden_10m": dict(deck=TIMBER),
          "catwalk_stair": dict(deck=STEEL), "alley_stair": dict(deck=STEEL), "pier_span_22m": dict(deck=TIMBER), "deck_lowwalk_6m": dict(deck=TIMBER),
          "deck_pad_6m": dict(deck=STEEL), "deck_round_27m": dict(deck=STONE), "deck_round_14m": dict(deck=STONE), "terrace_step_6m": dict(deck=STONE)}
SURF_FACTORS = {"facade": dict(rough=0.95, metal=0.0), "trim": dict(color=(0.060, 0.055, 0.068), rough=0.70, metal=0.0),
                "metal": dict(color=(0.060, 0.062, 0.070), rough=0.56, metal=0.85), "deck": dict(rough=0.56, metal=0.0)}


def scheme(name):
    return {**SCHEME["_default"], **SCHEME.get(name, {})}


def hh01(*a):
    f = math.sin(sum(x * k for x, k in zip(a, (12.9898, 78.233, 37.719, 4.581, 9.173, 21.37)))) * 43758.5453
    return f - math.floor(f)


def region_uv(name, a, b):
    """Blender UV of the point (a, b) metres from the bottom-left corner of an atlas region."""
    x, y, w, h = SURF["regions"][name]["px"]; W, H = SURF["size"]; ppm = SURF["px_per_m"]
    return ((x + a * ppm) / W, 1.0 - (y + h - b * ppm) / H)


def tile_rect(row, col, cell, u0, u1, v0, v1, mirror=False):
    """(U0, V0, U1, V1) of a wall rectangle inside its facade cell on the atlas tile (row = finish, col = variant)."""
    tw, th = SURF["cell_px"]; W, H = SURF["size"]; ppm = SURF["px_per_m"]
    c0, c1, d0, d1 = cell
    k = min(1.0, 2.4 / max(c1 - c0, 1e-6), 3.2 / max(d1 - d0, 1e-6)) * ppm           # a cell larger than a tile is shrunk to fit
    e = 0.35                                                                           # stay a third of a texel inside the tile
    U = lambda u: (col * tw + min(tw - e, max(e, (tw - (u - c0) * k) if mirror else (u - c0) * k))) / W
    V = lambda v: 1.0 - (row * th + min(th - e, max(e, th - (v - d0) * k))) / H
    return (U(u0), V(v0), U(u1), V(v1))
PANE_MATS = ("glass", "win_warm", "win_cool", "win_stair", "beam_lamp")
MATS = {}
# Emissive strengths written into the GLBs (KHR_materials_emissive_strength). Scene-linear with white colour and the
# brightest level; the scene multiplies by a palette colour. Large surfaces stay low so they keep their hue under the
# tone curve (the bell was near-white at 6); only points a few pixels across go high.
STRENGTH = dict(win_warm=3.0, win_cool=3.0, win_stair=3.5, strip=10.0, beacon=40.0, landmark=1.6, garden=18.0, lantern=14.0, keel=12.0,
                cond_glow=5.0, cond_lamp=22.0, cond_work=22.0, neon_a=20.0, beam_lamp=30.0, beam=1.0)


def make_levels():
    img = bpy.data.images.new("win_levels", 16, 1, alpha=False)
    px = []
    for i in range(16):
        v = 0.12 + 0.88 * (i / 15.0) ** 1.6
        px += [v, v, v, 1.0]
    img.pixels = px
    img.filepath_raw = os.path.join(OUT, "win_levels.png")
    img.file_format = 'PNG'
    img.save()
    return img


def lvl(x):
    """Brightness 0..1 -> the u of that step of the ramp."""
    return (min(15, max(0, int(x * 15.99))) + 0.5) / 16.0


def mats():
    lv = make_levels()

    def pbr(name, col, rough, metal=0.0, emit=None, tex=None):
        m, nt = new_mat(name)
        out = node(nt, 'ShaderNodeOutputMaterial'); b = node(nt, 'ShaderNodeBsdfPrincipled')
        nt.links.new(b.outputs[0], out.inputs[0])
        b.inputs['Base Color'].default_value = (*col, 1); b.inputs['Roughness'].default_value = rough; b.inputs['Metallic'].default_value = metal
        if emit:
            b.inputs['Emission Color'].default_value = (*emit, 1); b.inputs['Emission Strength'].default_value = STRENGTH[name]
            if tex:
                t = node(nt, 'ShaderNodeTexImage'); t.image = tex; t.interpolation = 'Closest'
                nt.links.new(t.outputs[0], b.inputs['Emission Color'])
        MATS[name] = m

    W = (1.0, 1.0, 1.0)
    pbr("facade", (0.036, 0.033, 0.046), 0.72)
    pbr("trim", (0.070, 0.064, 0.080), 0.55)
    pbr("metal", (0.045, 0.047, 0.055), 0.42, 0.8)
    pbr("deck", (0.05, 0.045, 0.06), 0.34)
    pbr("glass", (0.008, 0.009, 0.014), 0.28)
    pbr("cloth", (0.16, 0.14, 0.17), 0.85)                      # awnings, laundry, coats: the engine may tint per instance
    pbr("figure", (0.012, 0.011, 0.016), 0.8)
    pbr("pool", (0.004, 0.005, 0.010), 0.04)
    pbr("foliage", (0.014, 0.027, 0.036), 0.78)                 # dark leaves; the light is in the buds (garden)
    # emissive roles: white colour, grey level texture; the scene supplies colour and gain per role and per instance
    pbr("win_warm", (0.01, 0.008, 0.006), 0.3, emit=W, tex=lv)
    pbr("win_cool", (0.006, 0.007, 0.012), 0.3, emit=W, tex=lv)
    pbr("win_stair", (0.006, 0.007, 0.012), 0.3, emit=W, tex=lv)  # stairwell slots: ALWAYS lit (the town's light at distance); no lit/dark program
    pbr("strip", (0.01, 0.01, 0.01), 0.4, emit=W)
    pbr("beacon", (0.01, 0.0, 0.0), 0.4, emit=W)
    pbr("landmark", (0.02, 0.012, 0.018), 0.5, emit=W, tex=lv)
    pbr("garden", (0.01, 0.006, 0.012), 0.5, emit=W, tex=lv)
    pbr("lantern", (0.02, 0.012, 0.008), 0.5, emit=W, tex=lv)    # lantern strings, street lamps, hand lanterns
    pbr("keel", (0.012, 0.008, 0.02), 0.5, emit=W, tex=lv)       # keel lanterns under the decks
    pbr("cond_glow", (0.02, 0.01, 0.005), 0.6, emit=W, tex=lv)   # Condensery: throats, flare (always amber)
    pbr("cond_lamp", (0.02, 0.01, 0.005), 0.5, emit=W, tex=lv)   # Condensery: sodium lamps
    pbr("cond_work", (0.01, 0.012, 0.02), 0.5, emit=W, tex=lv)   # Condensery: blue-white work lights
    pbr("beam_lamp", (0.02, 0.02, 0.02), 0.3, emit=W, tex=lv)    # lighthouse lantern-room panes (v = bearing / 360)
    pbr("beam", (0.0, 0.0, 0.0), 1.0, emit=W)                    # fallback beam cone (additive in the scene)


class Asset:
    def __init__(self, name):
        self.name = name
        self.mb = {}
        self.open = []
        self.meta = {}

    def m(self, mat):
        mb = self.mb.get(mat)
        if mb is None:
            mb = self.mb[mat] = MB()
            if SURF and mat in SURF_MATS:
                x, y, w, h = SURF["regions"][SURF_MATS[mat]]["px"]
                mb.patch = (x, y, w, h, SURF["size"][0], SURF["size"][1], SURF["px_per_m"])
                mb.def_uv = region_uv("neutral", 1.6, 1.6)
        return mb

    def finish_row(self, shop=False):
        """The finish of the next block: glazed tile on a shop floor, then the asset's rows in turn."""
        if shop:
            return 3
        rows = scheme(self.name)["rows"]; i = getattr(self, "_nfin", 0); self._nfin = i + 1
        return rows[i % len(rows)]

    def finish(self):
        objs = []
        for mat, mb in self.mb.items():
            o = mb.build(self.name + "_" + mat, MATS[mat])
            if o:
                objs.append(o)
        bpy.ops.object.select_all(action='DESELECT')
        for o in objs:
            o.select_set(True)
        bpy.context.view_layer.objects.active = objs[0]
        if len(objs) > 1:
            bpy.ops.object.join()
        ob = bpy.context.view_layer.objects.active
        ob.name = self.name; ob.data.name = self.name
        self.ob = ob
        self.tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
        return ob, self.tris


# ------------------------------------------------------------------------------------------- wall primitives
def wq(A, mat, O, T, Nn, u0, u1, v0, v1, n=0.0, uv=None, flip=False):
    """Quad on a wall plane: origin O, tangent T, outward normal Nn; (u along wall, v up, n out)."""
    if u1 - u0 < 1e-5 or v1 - v0 < 1e-5:
        return
    if uv is None and SURF and mat == "facade" and not flip:
        row = FCTX["row"] if FCTX["row"] is not None else scheme(A.name)["rows"][0]
        if FCTX["cell"] is not None:                                   # a piece of one facade cell (fill_cell)
            pieces = [(u0, u1, v0, v1, FCTX["cell"], FCTX["win"])]
        else:                                                           # a plain wall: cut into cells along the floors and the bays
            pieces = []
            vb = v0
            while vb < v1 - 1e-5:
                k = math.floor((vb + 1e-4) / FLOOR); vt = min(v1, (k + 1) * FLOOR)
                ub = u0
                while ub < u1 - 1e-5:
                    ut = min(u1, ub + BAY)
                    pieces.append((ub, ut, vb, vt, (ub, ub + BAY, k * FLOOR, (k + 1) * FLOOR), False))
                    ub = ut
                vb = vt
        for (a0, a1, b0, b1, cell, win) in pieces:
            base = FCTX["base_z"] is not None and abs(cell[2] - FCTX["base_z"]) < 1e-3
            hh = hh01(O.x, O.y, cell[0], cell[2], row, T.x)
            col = (3 if base else 0) if win else (4 if base else (1 if hh < 0.5 else 2))
            p = [O + T * a0 + Z * b0 + Nn * n, O + T * a1 + Z * b0 + Nn * n, O + T * a1 + Z * b1 + Nn * n, O + T * a0 + Z * b1 + Nn * n]
            A.m(mat).quad(*[tuple(q) for q in p], uv=tile_rect(row, col, cell, a0, a1, b0, b1, mirror=hh01(O.y, O.x, cell[2], cell[0], 3.3) < 0.5), double=False)
        return
    p = [O + T * u0 + Z * v0 + Nn * n, O + T * u1 + Z * v0 + Nn * n, O + T * u1 + Z * v1 + Nn * n, O + T * u0 + Z * v1 + Nn * n]
    if flip:
        p.reverse()
    mb = A.m(mat)
    if uv is None and mb.patch:
        f2 = [(0.0, 0.0), (u1 - u0, 0.0), (u1 - u0, v1 - v0), (0.0, v1 - v0)]
        if flip:
            f2.reverse()
        mb._add([tuple(q) for q in p], [(0, 1, 2, 3)], None, mb.puv(f2, u1 - u0, v1 - v0, O.x * 1.37 + O.y * 2.11 + u0 * 3.1 + v0 * 0.77))
        return
    mb.quad(*[tuple(q) for q in p], uv=uv or (u0 / 4, v0 / 4, u1 / 4, v1 / 4), double=False)


def wbox(A, mat, O, T, Nn, u0, u1, v0, v1, n0, n1):
    """Box in wall coordinates (n0..n1 out of the wall)."""
    c = O + T * ((u0 + u1) / 2) + Z * ((v0 + v1) / 2) + Nn * ((n0 + n1) / 2)
    yaw = math.atan2(T.y, T.x)
    A.m(mat).box(tuple(c), (abs(u1 - u0), abs(n1 - n0), abs(v1 - v0)), yaw)


def wall(O, T):
    """(O, T, N) of a vertical wall from its origin and tangent; the normal points to the right of T seen from above."""
    O, T = Vector(O), Vector(T).normalized()
    return O, T, T.cross(Z)


def fill_cell(A, O, T, Nn, cell, rects, mat="facade"):
    """Wall surface of a cell around any number of non-overlapping rectangular openings."""
    c0, c1, d0, d1 = cell
    xs = sorted(set([c0, c1] + [x for r_ in rects for x in r_[:2]])); ys = sorted(set([d0, d1] + [y for r_ in rects for y in r_[2:]]))
    # the atlas tile of this cell: the windowed variant (drips under the sill ends) only where the kit's standard window stands
    FCTX["cell"] = cell
    FCTX["win"] = len(rects) == 1 and abs(rects[0][0] - c0 - 0.575) < 0.02 and abs(rects[0][2] - d0 - 0.95) < 0.02 and abs(c1 - c0 - BAY) < 0.02
    for i in range(len(xs) - 1):
        j0 = None
        for j in range(len(ys)):                                       # merge the free cells of a column into strips
            free = j < len(ys) - 1 and not any(r_[0] - 1e-6 <= (xs[i] + xs[i + 1]) / 2 <= r_[1] + 1e-6 and r_[2] - 1e-6 <= (ys[j] + ys[j + 1]) / 2 <= r_[3] + 1e-6 for r_ in rects)
            if free and j0 is None:
                j0 = j
            if not free and j0 is not None:
                wq(A, mat, O, T, Nn, xs[i], xs[i + 1], ys[j0], ys[j]); j0 = None
    FCTX["cell"] = None; FCTX["win"] = False


def door_leaf(A, O, T, Nn, rect, depth=0.1, mat="trim"):
    """A solid door in an opening (not a pane): reveals, leaf, a rail and a handle. The cell fill is the caller's."""
    a0, a1, b0, b1 = rect
    for (p0, p1) in (((a0, b0), (a1, b0)), ((a1, b0), (a1, b1)), ((a1, b1), (a0, b1)), ((a0, b1), (a0, b0))):
        q = [O + T * p0[0] + Z * p0[1], O + T * p1[0] + Z * p1[1], O + T * p1[0] + Z * p1[1] - Nn * depth, O + T * p0[0] + Z * p0[1] - Nn * depth]
        A.m("trim").quad(*[tuple(x) for x in q], uv=(0, 0, 0.1, 0.1), double=False)
    wq(A, mat, O, T, Nn, a0, a1, b0, b1, n=-depth)
    wbox(A, "metal", O, T, Nn, a0 + 0.08, a1 - 0.08, b0 + 1.0, b0 + 1.04, -depth, -depth + 0.03)
    wbox(A, "metal", O, T, Nn, a1 - 0.16, a1 - 0.1, b0 + 0.95, b0 + 1.1, -depth, -depth + 0.06)


def pane_opening(A, O, T, Nn, rect, cell=None, depth=0.14, mat="glass", level=0.5, wid=0.5, sill=True, mullions=1,
                 transom=None, dress=None, kind="window", clear=2.5, frame_mat="trim", fill_mat="facade", reg_cell=None):
    """THE way a pane enters the kit. rect = (a0, a1, b0, b1) on the wall; cell = the wall rectangle it is cut from
    (filled around the opening when given; pass reg_cell instead when the caller fills a cell with several openings).
    dress: None | ("blind", frac) | ("curtain", frac)."""
    a0, a1, b0, b1 = rect
    if cell:
        fill_cell(A, O, T, Nn, cell, [rect], fill_mat)
    cell = cell or reg_cell
    for (p0, p1) in (((a0, b0), (a1, b0)), ((a1, b0), (a1, b1)), ((a1, b1), (a0, b1)), ((a0, b1), (a0, b0))):       # reveals
        q = [O + T * p0[0] + Z * p0[1], O + T * p1[0] + Z * p1[1], O + T * p1[0] + Z * p1[1] - Nn * depth, O + T * p0[0] + Z * p0[1] - Nn * depth]
        A.m(frame_mat).quad(*[tuple(x) for x in q], uv=(0, 0, 0.1, 0.1), double=False)
    u = lvl(level)
    parts = [(a0, a1, b0, b1, u)]
    if dress and dress[0] == "blind":
        bz = b1 - (b1 - b0) * dress[1]
        parts = [(a0, a1, b0, bz, u), (a0, a1, bz, b1, lvl(level * 0.42))]
    elif dress and dress[0] == "curtain":
        cw = (a1 - a0) * dress[1]
        parts = [(a0, a0 + cw, b0, b1, lvl(level * 0.5)), (a0 + cw, a1 - cw, b0, b1, u), (a1 - cw, a1, b0, b1, lvl(level * 0.5))]
    for (x0, x1, y0, y1, uu) in parts:
        wq(A, mat, O, T, Nn, x0, x1, y0, y1, n=-depth, uv=(uu, wid, uu, wid))
    for k in range(1, mullions + 1):
        uu = a0 + (a1 - a0) * k / (mullions + 1)
        wbox(A, frame_mat, O, T, Nn, uu - 0.025, uu + 0.025, b0, b1, -depth, -depth + 0.05)
    if transom is not None:
        wbox(A, frame_mat, O, T, Nn, a0, a1, transom - 0.02, transom + 0.02, -depth, -depth + 0.04)
    if sill:
        wbox(A, frame_mat, O, T, Nn, a0 - 0.08, a1 + 0.08, b0 - 0.07, b0, -0.02, 0.09)
    A.open.append(dict(O=tuple(O), T=tuple(T), N=tuple(Nn), rect=(a0, a1, b0, b1), cell=cell, depth=depth, kind=kind, clear=clear, round=False))


def porthole(A, C, T, Nn, radius, half_w, half_h, depth=0.16, mat="glass", level=0.5, wid=0.5, n=16, face_mat="trim", rim_mat="metal"):
    """A round window in a rectangular face (the capsule tower): the face is built around the hole, the pane is
    recessed. C = centre of the face on the wall plane."""
    C = Vector(C)
    ring = [C + T * (radius * math.cos(i / n * 6.2832)) + Z * (radius * math.sin(i / n * 6.2832)) for i in range(n)]

    def edge_pt(a):                                             # the face's border in the direction of angle a
        c, s = math.cos(a), math.sin(a)
        k = min(half_w / abs(c) if abs(c) > 1e-6 else 1e9, half_h / abs(s) if abs(s) > 1e-6 else 1e9)
        return C + T * (c * k) + Z * (s * k)
    corners = [math.atan2(sy * half_h, sx * half_w) % 6.2832 for sx, sy in ((1, 1), (-1, 1), (-1, -1), (1, -1))]
    for i in range(n):
        a0, a1 = i / n * 6.2832, (i + 1) / n * 6.2832
        outer = [edge_pt(a0)] + [edge_pt(c) for c in sorted(corners) if a0 < c < a1] + [edge_pt(a1)]
        poly = [ring[i]] + outer + [ring[(i + 1) % n]]
        A.m(face_mat)._add([tuple(p) for p in poly], [tuple(range(len(poly)))])
        j = (i + 1) % n                                         # the recess tube, then a thin rim proud of the face
        A.m(rim_mat)._add([tuple(ring[i]), tuple(ring[j]), tuple(ring[j] - Nn * depth), tuple(ring[i] - Nn * depth)], [(0, 1, 2, 3)])
    u = (lvl(level), wid)
    A.m(mat)._add([tuple(p - Nn * depth) for p in ring], [tuple(range(n))], None, [u] * n)
    wbox(A, rim_mat, C, T, Nn, -0.025, 0.025, -radius, radius, -depth, -depth + 0.05)       # cross bars set in the recess
    wbox(A, rim_mat, C, T, Nn, -radius, radius, -0.025, 0.025, -depth, -depth + 0.05)
    A.open.append(dict(O=tuple(C), T=tuple(T), N=tuple(Nn), rect=(-radius, radius, -radius, radius),
                       cell=(-half_w, half_w, -half_h, half_h), depth=depth, kind="porthole", clear=2.5, round=True))


# ------------------------------------------------------------------------------------------- facade cells
def window_cell(A, r, r2, O, T, Nn, u, v, lit_p, warm_p, wid, ac_p=0.18, balcony=False, extras=True):
    """One 2.4 x 3.2 m facade cell with a recessed, mullioned window (or a balcony door). u, v = cell origin on the
    wall. r is the layout stream (kept draw-for-draw from kit v1 so approved modules keep their shape), r2 the detail stream."""
    a0, a1 = u + 0.575, u + 1.825
    lit = r.random() < lit_p
    mat = ("win_warm" if r.random() < warm_p else "win_cool") if lit else "glass"
    level = r.random() ** 1.5
    d = r2.random()
    dress = None if d < 0.4 else (("blind", r2.uniform(0.25, 0.6)) if d < 0.72 else ("curtain", r2.uniform(0.16, 0.3)))
    cell = (u, u + BAY, v, v + FLOOR)
    if balcony:
        pane_opening(A, O, T, Nn, (a0, a1, v + 0.14, v + 2.30), cell, 0.14, mat, level, wid, sill=False, transom=v + 2.0, dress=dress,
                     kind="balcony_door", clear=0.85)
        wbox(A, "trim", O, T, Nn, u + 0.2, u + BAY - 0.2, v + 0.0, v + 0.12, 0.0, 1.05)                 # slab
        for k in range(9):
            uu = u + 0.25 + k * (BAY - 0.5) / 8
            wbox(A, "metal", O, T, Nn, uu - 0.015, uu + 0.015, v + 0.12, v + 1.05, 1.0, 1.03)
        wbox(A, "metal", O, T, Nn, u + 0.2, u + BAY - 0.2, v + 1.02, v + 1.07, 0.98, 1.05)
        for uu in (u + 0.2, u + BAY - 0.23):
            wbox(A, "metal", O, T, Nn, uu, uu + 0.03, v + 0.12, v + 1.05, 0.0, 1.03)
            wbox(A, "metal", O, T, Nn, uu, uu + 0.03, v + 1.02, v + 1.07, 0.0, 1.03)
        if r2.random() < 0.6:                                                                            # a pot in the corner
            c = O + T * (u + 0.45) + Z * (v + 0.3) + Nn * 0.75
            A.m("trim").cyl((c.x, c.y, c.z - 0.18), 0.16, 0.3, n=7, r_top=0.2); A.m("facade").sphere((c.x, c.y, c.z + 0.32), 0.26, n=7, m=5)
        return
    pane_opening(A, O, T, Nn, (a0, a1, v + 0.95, v + 2.30), cell, 0.14, mat, level, wid, sill=True, transom=v + 0.95 + 0.84, dress=dress)
    if r.random() < ac_p:                                                                               # condenser under the sill
        wbox(A, "metal", O, T, Nn, a0 + 0.2, a0 + 1.0, v + 0.3, v + 0.78, 0.0, 0.34)
        wbox(A, "metal", O, T, Nn, a0 + 0.25, a0 + 0.95, v + 0.24, v + 0.3, 0.02, 0.3)
        c = O + T * (a0 + 0.6) + Z * (v + 0.54) + Nn * 0.34                                              # fan grille
        yaw = math.atan2(T.y, T.x)
        A.m("trim").box(tuple(c + Nn * 0.006), (0.44, 0.012, 0.36), yaw); A.m("facade").box(tuple(c + Nn * 0.012), (0.3, 0.012, 0.3), yaw)
        wbox(A, "metal", O, T, Nn, a0 + 1.03, a0 + 1.07, v + 0.02, v + 0.5, 0.02, 0.06)                  # its drain line, inside the cell
    elif extras and r2.random() < 0.07:                                                                 # a laundry pole under the sill
        wbox(A, "metal", O, T, Nn, a0 + 0.3, a0 + 0.33, v + 0.78, v + 0.81, 0.0, 1.7)
        for k in range(r2.choice([2, 3])):
            nn = 0.45 + k * 0.45; hgt = r2.uniform(0.35, 0.62)
            A.m("cloth").quad(*[tuple(O + T * (a0 + 0.315 + du) + Z * (v + 0.78 - dz) + Nn * (nn + dn)) for (du, dz, dn) in
                                ((0, hgt, 0), (0, hgt, 0.34), (0, 0, 0.34), (0, 0, 0))], uv=(0, 0, 1, 1), double=True)


def shop_bay(A, r, O, T, Nn, u, v, door=False):
    a0, a1 = u + 0.16, u + BAY - 0.16
    b0, b1 = (v + 0.06, v + 2.5) if door else (v + 0.7, v + 2.5)
    level = 0.45 + 0.55 * r.random()
    pane_opening(A, O, T, Nn, (a0, a1, b0, b1), (u, u + BAY, v, v + FLOOR), 0.1, "win_warm", level, 0.5, sill=False,
                 mullions=1 if door else 2, transom=v + 2.045, kind="shop", clear=1.2)


def pipe(A, O, T, Nn, u, v0, v1, rad=0.055):
    """A vertical pipe on a wall at a CELL BOUNDARY (the pier between windows), with brackets."""
    c = O + T * u + Nn * (rad + 0.05)
    A.m("metal").cyl((c.x, c.y, v0), rad, v1 - v0, n=6, cap=False)
    k = v0 + 0.5
    while k < v1 - 0.2:
        wbox(A, "metal", O, T, Nn, u - 0.09, u + 0.09, k, k + 0.05, 0.0, 2 * rad + 0.07)
        k += FLOOR


def stair_slot(A, O, T, Nn, u, v, fl, wid):
    """A stairwell's slot window: tall, narrow, always lit. A column of them is a building's light at a distance."""
    pane_opening(A, O, T, Nn, (u + 0.85, u + 1.55, v + 0.5, v + 2.75), (u, u + BAY, v, v + FLOOR), 0.14, "win_stair", 0.72 if fl % 3 else 0.92, wid,
                 sill=False, mullions=0, transom=v + 1.6, kind="stair")


def block(A, r, r2, cx, cy, z0, w, d, floors, lit_p, warm_p, wbase, balcony_side=None, shop=False, blank_p=0.1, pipes=True,
          sides_on=(0, 1, 2, 3), special=None, stair=None):
    """One stacked block: four facades of window cells. special(si, fl, b, O, T, Nn, u, v) -> True if it built the cell."""
    sides = [(Vector((cx - w / 2, cy - d / 2, 0)), Vector((1, 0, 0)), w), (Vector((cx + w / 2, cy - d / 2, 0)), Vector((0, 1, 0)), d),
             (Vector((cx + w / 2, cy + d / 2, 0)), Vector((-1, 0, 0)), w), (Vector((cx - w / 2, cy + d / 2, 0)), Vector((0, -1, 0)), d)]
    bal = {}
    frow = A.finish_row(shop)                                           # iteration 8: this block's finish on the surface atlas
    FCTX["row"] = frow; FCTX["base_z"] = z0
    for si, (O, T, L) in enumerate(sides):
        Nn = T.cross(Z)
        nb = int(L / BAY + 1e-6); mg = (L - nb * BAY) / 2
        if frow in (0, 1):                                              # rendered walls carry a string course at every floor line (crisp at 3 m, where the atlas's joint is soft)
            for fl in range(1, floors):
                wbox(A, "trim", O, T, Nn, 0.0, L, z0 + fl * FLOOR - 0.03, z0 + fl * FLOOR + 0.03, 0.0, 0.035)
        bal_col = r.randrange(nb) if (balcony_side == si and nb > 0) else -1
        bal[si] = bal_col
        if si not in sides_on:
            wq(A, "facade", O, T, Nn, 0, L, z0, z0 + floors * FLOOR)
            continue
        for fl in range(floors):
            v = z0 + fl * FLOOR
            if mg > 1e-4:
                wq(A, "facade", O, T, Nn, 0, mg, v, v + FLOOR); wq(A, "facade", O, T, Nn, L - mg, L, v, v + FLOOR)
            for b in range(nb):
                u = mg + b * BAY
                if special and special(si, fl, b, O, T, Nn, u, v):
                    continue
                is_stair = stair is not None and si == stair and b == nb // 2 and bal_col != b
                wid = (wbase * 0.137 + si * 0.251 + fl * 0.0613 + b * 0.0171) % 1.0
                if shop and si == 0 and fl == 0:
                    shop_bay(A, r, O, T, Nn, u, v, door=(b == nb // 2))
                elif r.random() < blank_p or (si in (1, 3) and r.random() < 0.25):
                    if is_stair:
                        stair_slot(A, O, T, Nn, u, v, fl, wid)
                    else:
                        wq(A, "facade", O, T, Nn, u, u + BAY, v, v + FLOOR)
                elif is_stair:                                    # draw the cell's random numbers on a scratch asset, so every other cell stays as approved
                    window_cell(Asset("_scratch"), r, r2, O, T, Nn, u, v, lit_p, warm_p, wid, balcony=False)
                    stair_slot(A, O, T, Nn, u, v, fl, wid)
                else:
                    window_cell(A, r, r2, O, T, Nn, u, v, lit_p, warm_p, wid, balcony=(b == bal_col and fl > 0))
        if pipes and nb >= 2 and not (shop and si == 0):
            # pipes live on cell boundaries only; a conduit runs in the band above the window heads (v + 2.3 .. 3.2)
            if si in (1, 3) or (si == 2 and r2.random() < 0.7):
                for b in r2.sample(range(1, nb), 1 if nb < 4 else 2):
                    if bal_col in (b - 1, b):
                        continue
                    pipe(A, O, T, Nn, mg + b * BAY, z0 + 0.05, z0 + floors * FLOOR - 0.12)
            if special is None and r2.random() < 0.5:
                vv = z0 + (floors - 1) * FLOOR + 2.72
                wbox(A, "metal", O, T, Nn, 0.15, L - 0.15, vv, vv + 0.06, 0.03, 0.09)
    A.m("facade").box((cx, cy, z0 + floors * FLOOR - 0.01), (w - 0.02, d - 0.02, 0.02))       # top cap
    A.m("facade").box((cx, cy, z0 + 0.01), (w - 0.02, d - 0.02, 0.02))                        # bottom cap
    FCTX["row"] = None; FCTX["base_z"] = None
    return bal


def roof_kit(A, r2, cx, cy, z, w, d, mast_h=9.0, tanks=1):
    """Roof furniture in fixed slots so nothing intersects on the smallest (6 x 6 m) roof."""
    for (sx, sy, lw, ld) in ((0, -d / 2 + 0.1, w, 0.2), (0, d / 2 - 0.1, w, 0.2), (-w / 2 + 0.1, 0, 0.2, d - 0.4), (w / 2 - 0.1, 0, 0.2, d - 0.4)):   # side pieces fit between the others: no coincident faces
        A.m("trim").box((cx + sx, cy + sy, z + 0.45), (lw, ld, 0.9))
    for t in range(tanks):                                                # water tank on legs, back left
        tx, ty = cx - w / 2 + 1.75 + t * 3.1, cy + d / 2 - 1.75
        for sx in (-0.85, 0.85):
            for sy in (-0.85, 0.85):
                A.m("metal").box((tx + sx, ty + sy, z + 0.8), (0.12, 0.12, 1.6))
        A.m("metal").cyl((tx, ty, z + 1.6), 1.3, 2.3, n=14); A.m("metal").cyl((tx, ty, z + 3.9), 1.3, 0.55, n=14, r_top=0.12)
        for k in range(4):
            a = k / 4 * 6.283 + 0.4
            A.m("metal").beam((tx + 1.31 * math.cos(a), ty + 1.31 * math.sin(a), z + 1.7), (tx + 1.31 * math.cos(a), ty + 1.31 * math.sin(a), z + 3.8), 0.05)
    bx, by = cx + w / 2 - 1.5, cy - d / 2 + 1.7                             # stair bulkhead, front right, with a lit door in an opening
    bw, bd, bh = 2.4, 2.8, 2.7
    O, T, Nn = wall((bx - bw / 2, by + bd / 2, 0), (0, -1, 0))             # its west wall faces the roof
    pane_opening(A, O, T, Nn, (0.95, 1.85, z + 0.06, z + 2.1), (0, bd, z, z + bh), 0.1, "win_warm", 0.6, 0.31, sill=False, mullions=0,
                 transom=z + 1.75, kind="door", clear=1.5)
    for (o_, t_, L_) in (((bx - bw / 2, by - bd / 2, 0), (1, 0, 0), bw), ((bx + bw / 2, by - bd / 2, 0), (0, 1, 0), bd), ((bx + bw / 2, by + bd / 2, 0), (-1, 0, 0), bw)):
        O2, T2, N2 = wall(o_, t_)
        wq(A, "facade", O2, T2, N2, 0, L_, z, z + bh)
    A.m("trim").box((bx, by, z + bh + 0.06), (bw + 0.3, bd + 0.3, 0.12))
    n_c = int(max(0, min(3, (w - 2.7 - 0.2 - 0.35) // 1.2)))               # condenser units, front left
    for k in range(n_c):
        ux, uy = cx - w / 2 + 0.95 + k * 1.2, cy - d / 2 + 0.8
        A.m("metal").box((ux, uy, z + 0.55), (1.0, 0.75, 0.9)); A.m("metal").cyl((ux, uy, z + 1.0), 0.32, 0.06, n=10)
    mx, my = cx + w / 2 - 0.7, cy + d / 2 - 0.7                             # mast with beacon, back right
    A.m("metal").cyl((mx, my, z), 0.07, mast_h, n=6)
    A.m("metal").box((mx, my, z + mast_h * 0.7), (1.5, 0.05, 0.05)); A.m("metal").box((mx, my, z + mast_h * 0.84), (0.05, 1.0, 0.05))
    A.m("beacon").sphere((mx, my, z + mast_h + 0.12), 0.14, n=8, m=5)
    if w >= 8.4 and d >= 8.4 and r2.random() < 0.6:                          # a dish in the middle of bigger roofs
        A.m("metal").cyl((cx + 0.2, cy - 0.3, z), 0.06, 1.3, n=6); A.m("metal").cyl((cx + 0.2, cy - 0.3, z + 1.25), 0.15, 0.45, n=10, r_top=0.9)


def stilts(A, cx, cy, w, d, depth=14.0, nx=3):
    xs = [cx + (i / (nx - 1) - 0.5) * 2 * (w / 2 - 0.5) for i in range(nx)]
    for x in xs:
        for sy in (-1, 1):
            y = cy + sy * (d / 2 - 0.5)
            A.m("metal").cyl((x, y, -depth), 0.26, depth, n=8, cap=False)
            A.m("metal").cyl((x, y, -1.0), 0.45, 0.5, n=8)                 # the pile cap, under the deck plate
    for sy in (-1, 1):
        y = cy + sy * (d / 2 - 0.5)
        for xa_, xb_ in zip(xs[:-1], xs[1:]):
            A.m("metal").beam((xa_, y, -1.0), (xb_, y, -6.5), 0.1); A.m("metal").beam((xb_, y, -1.0), (xa_, y, -6.5), 0.1)
            A.m("metal").beam((xa_, y, -6.8), (xb_, y, -6.8), 0.14)
    A.m("trim").box((cx, cy, -0.25), (w + 0.6, d + 0.6, 0.5))            # deck plate under the building


# ------------------------------------------------------------------------------------------- decks on the surface atlas
def deck_top(A, x0, x1, y0, y1, z, kind="planks", along="y", mat="deck"):
    """The walking surface of a rectangular deck at height z, mapped onto the atlas: "planks" (0.2 m boards running
    along `along`, cut across into bands of at most thirty boards), "plates" (3 m steel deck plates with bolted seams,
    two designs turned four ways) or "pavers" (0.8 x 0.4 m slabs in half bond)."""
    mb = A.m(mat)

    def quad(xa, xb, ya, yb, uvs):
        mb._add([(xa, ya, z), (xb, ya, z), (xb, yb, z), (xa, yb, z)], [(0, 1, 2, 3)], None, uvs)
    if not SURF:
        return quad(x0, x1, y0, y1, [mb.def_uv] * 4)
    if kind == "planks":
        P = (y1 - y0) if along == "y" else (x1 - x0)
        q0, q1 = (x0, x1) if along == "y" else (y0, y1)
        k = min(1.0, 12.6 / P); qa = q0; i = 0
        while qa < q1 - 1e-6:
            qb = min(q1, qa + 6.0)
            s0 = int(hh01(x0, y0, z, i, 1.0) * (32 - math.ceil((qb - qa) / 0.2 - 1e-6) + 0.999)) * 0.2
            pu = 0.1 + hh01(y0, x0, z, i, 2.0) * (12.6 - P * k)
            if along == "y":
                quad(qa, qb, y0, y1, [region_uv("planks", pu + p * k, s0 + q) for (q, p) in ((0, 0), (qb - qa, 0), (qb - qa, P), (0, P))])
            else:
                quad(x0, x1, qa, qb, [region_uv("planks", pu + p * k, s0 + q) for (p, q) in ((0, 0), (P, 0), (P, qb - qa), (0, qb - qa))])
            qa = qb; i += 1
    elif kind == "plates":
        nx, ny = max(1, round((x1 - x0) / 3.0)), max(1, round((y1 - y0) / 3.0))
        for i in range(nx):
            for j in range(ny):
                xa, xb = x0 + (x1 - x0) * i / nx, x0 + (x1 - x0) * (i + 1) / nx
                ya, yb = y0 + (y1 - y0) * j / ny, y0 + (y1 - y0) * (j + 1) / ny
                cs = [(0.0, 0.0), (3.2, 0.0), (3.2, 3.2), (0.0, 3.2)]; rot = int(hh01(xa, ya, z, 5.0) * 4) % 4
                quad(xa, xb, ya, yb, [region_uv("plate_a" if (i + j) % 2 == 0 else "plate_b", *c) for c in cs[rot:] + cs[:rot]])
    else:
        ya = y0
        while ya < y1 - 1e-6:
            yb = min(y1, ya + 3.2); xa = x0
            while xa < x1 - 1e-6:
                xb = min(x1, xa + 6.4); ou = hh01(xa, ya, z, 7.0) * (6.4 - (xb - xa))
                quad(xa, xb, ya, yb, [region_uv("pavers", ou + a_, b_) for (a_, b_) in ((0, 0), (xb - xa, 0), (xb - xa, yb - ya), (0, yb - ya))])
                xa = xb
            ya = yb


def deck_box(A, c, size, kind="planks", along="y", mat="deck"):
    """A deck slab: MB.box without its top, and the top as deck_top."""
    A.m(mat).box(c, size, skip=(1,))
    deck_top(A, c[0] - size[0] / 2, c[0] + size[0] / 2, c[1] - size[1] / 2, c[1] + size[1] / 2, c[2] + size[2] / 2, kind, along, mat)


def ring_band(A, r0, r1, z, a0=0.0, a1=2 * math.pi, n=48, kind="pavers", mat="deck", c=(0.0, 0.0)):
    """An annular piece of deck (r0..r1, angles a0..a1) at height z: `kind` runs round the arc, its courses radially."""
    mb = A.m(mat); um = 0.0
    rw, rh = SURF["regions"][kind]["m"] if SURF else (1.0, 1.0)
    v0 = int(hh01(r0, z, a0, 3.0) * max(1, (rh - (r1 - r0)) / 0.2)) * 0.2 if kind == "planks" else 0.0
    for i in range(n):
        t0, t1 = a0 + (a1 - a0) * i / n, a0 + (a1 - a0) * (i + 1) / n
        pts = [(c[0] + r0 * math.cos(t0), c[1] + r0 * math.sin(t0), z), (c[0] + r1 * math.cos(t0), c[1] + r1 * math.sin(t0), z),
               (c[0] + r1 * math.cos(t1), c[1] + r1 * math.sin(t1), z), (c[0] + r0 * math.cos(t1), c[1] + r0 * math.sin(t1), z)]
        if not SURF:
            mb._add(pts, [(0, 1, 2, 3)]); continue
        arc = (r0 + r1) / 2 * (t1 - t0); k = min(1.0, (rw - 0.2) / arc)
        if um + arc * k > rw - 0.1:
            um = 0.1 + hh01(i, r0, z, 9.0) * 0.6
        mb._add(pts, [(0, 1, 2, 3)], None, [region_uv(kind, um, v0), region_uv(kind, um, v0 + r1 - r0), region_uv(kind, um + arc * k, v0 + r1 - r0), region_uv(kind, um + arc * k, v0)])
        um += arc * k


def wall_ring(A, mat, r, z0, z1, a0=0.0, a1=2 * math.pi, n=48, inward=False, c=(0.0, 0.0)):
    """A cylindrical wall as flat quads (each mapped into the material's patch on its own, so nothing smears round it)."""
    mb = A.m(mat)
    for i in range(n):
        t0, t1 = a0 + (a1 - a0) * i / n, a0 + (a1 - a0) * (i + 1) / n
        pa, pb = (c[0] + r * math.cos(t0), c[1] + r * math.sin(t0)), (c[0] + r * math.cos(t1), c[1] + r * math.sin(t1))
        L = math.hypot(pb[0] - pa[0], pb[1] - pa[1])
        pts = [(pa[0], pa[1], z0), (pb[0], pb[1], z0), (pb[0], pb[1], z1), (pa[0], pa[1], z1)]; f2 = [(0, 0), (L, 0), (L, z1 - z0), (0, z1 - z0)]
        if inward:
            pts.reverse(); f2.reverse()
        mb._add(pts, [(0, 1, 2, 3)], None, mb.puv(f2, L, z1 - z0, r * 1.7 + z0 * 3.1 + i * 0.37) if mb.patch else None)


# ------------------------------------------------------------------------------------------- attaching the atlas to a GLB
SURF_FAMILIES = ("buildings", "decks")
SURF_ASSETS = ("terrace_step_6m", "pavilion", "tree_pit", "lighthouse_a", "lighthouse_b", "lighthouse_c")
SURF_SKIP = ("rail_6m", "pool_15m")
SURFACED = []


def surface_attach(path, name, rel="../tex/"):
    """Point the surface roles of an exported GLB at the shared atlas BY URI (three images stored once for the whole
    kit, not embedded in every file) and write the asset's factors. Blender wrote plain factors; this edits the JSON
    chunk only. The atlas multiplies the factors: base colour, roughness (G), metallic (B), occlusion (R)."""
    import struct
    data = open(path, "rb").read()
    magic, ver, _total = struct.unpack_from("<III", data, 0); jlen, jtype = struct.unpack_from("<II", data, 12)
    assert magic == 0x46546C67 and jtype == 0x4E4F534A, path
    doc = json.loads(data[20:20 + jlen].decode("utf-8")); rest = data[20 + jlen:]
    mats_ = [m for m in doc.get("materials", []) if m.get("name") in SURF_MATS]
    if not mats_:
        return
    sch = scheme(name)
    doc.setdefault("samplers", []).append({"magFilter": 9729, "minFilter": 9987, "wrapS": 33071, "wrapT": 33071}); si = len(doc["samplers"]) - 1
    ids = {}
    for k in ("base", "normal", "orm"):
        doc.setdefault("images", []).append({"uri": rel + SURF["files"][k], "name": "nd_surface_" + k})
        doc.setdefault("textures", []).append({"sampler": si, "source": len(doc["images"]) - 1}); ids[k] = len(doc["textures"]) - 1
    for m in mats_:
        f = SURF_FACTORS[m["name"]]; pbr = m.setdefault("pbrMetallicRoughness", {})
        col = sch["color"] if m["name"] == "facade" else sch["deck"] if m["name"] == "deck" else f["color"]
        if m["name"] == "trim":                                          # slabs and frames lean a little toward the building's own hue
            col = tuple(0.62 * a_ + 0.38 * 1.25 * b_ for a_, b_ in zip(col, sch["color"]))
        pbr["baseColorFactor"] = [round(c_, 4) for c_ in col] + [1.0]; pbr["roughnessFactor"] = f["rough"]; pbr["metallicFactor"] = f["metal"]
        pbr["baseColorTexture"] = {"index": ids["base"]}; pbr["metallicRoughnessTexture"] = {"index": ids["orm"]}
        m["normalTexture"] = {"index": ids["normal"], "scale": 1.0}; m["occlusionTexture"] = {"index": ids["orm"], "strength": 1.0}
    js = json.dumps(doc, separators=(",", ":")).encode("utf-8"); js += b" " * ((4 - len(js) % 4) % 4)
    open(path, "wb").write(struct.pack("<III", magic, ver, 20 + len(js) + len(rest)) + struct.pack("<II", len(js), jtype) + js + rest)
    SURFACED.append(name)


def lvl_uv(r, lo=0.0):
    u = (int((lo + (1 - lo) * r.random()) * 15.99) + 0.5) / 16.0
    return (u, r.random(), u, r.random())


def octa(mb, c, s_, uv):
    """Tiny emissive point: an octahedron of 8 triangles with collapsed UVs."""
    x, y, z = c
    t, b = (x, y, z + s_), (x, y, z - s_)
    ring = [(x + s_, y, z), (x, y + s_, z), (x - s_, y, z), (x, y - s_, z)]
    u = (uv[0], uv[1])
    for i in range(4):
        a_, b_ = ring[i], ring[(i + 1) % 4]
        mb._add([a_, b_, t], [(0, 1, 2)], None, [u, u, u]); mb._add([b_, a_, b], [(0, 1, 2)], None, [u, u, u])


def lamp(mb, c, rad, level, hsh, n=8, m=5, squash=1.0):
    """An emissive ball with collapsed UVs (u = level, v = hash)."""
    b0 = len(mb.uv)
    mb.sphere(c, rad, n=n, m=m, squash=squash)
    u = (lvl(level), hsh)
    for i in range(b0, len(mb.uv)):
        mb.uv[i] = u


# ------------------------------------------------------------------------------------------- the audit
def audit(A):
    """Check THE WINDOW RULE on the finished mesh. Returns (n_openings, n_pane_polys, [violations])."""
    ob = A.ob; me = ob.data
    verts = [v.co.copy() for v in me.vertices]
    polys = [tuple(p.vertices) for p in me.polygons]
    bvh = BVHTree.FromPolygons(verts, polys)
    names = [s.material.name if s.material else "" for s in ob.material_slots]
    pm = [names[p.material_index] in PANE_MATS for p in me.polygons]
    bad = []
    planes = {}
    for i, o in enumerate(A.open):
        O, T, Nn = Vector(o["O"]), Vector(o["T"]), Vector(o["N"])
        a0, a1, b0, b1 = o["rect"]
        if o["depth"] < 0.06:
            bad.append((i, "pane is not recessed"))
        if o["cell"]:
            c0, c1, d0, d1 = o["cell"]
            mgn = min(a0 - c0, c1 - a1, b0 - d0, d1 - b1)
            if mgn < 0.055:
                bad.append((i, "opening reaches the edge of its wall cell (margin %.3f)" % mgn))
        key = (round(Nn.x, 3), round(Nn.y, 3), round(O.dot(Nn), 2))
        planes.setdefault(key, []).append((i, O, T, a0, a1, b0, b1))
        # rays out of the opening: nothing may stand in front of a window
        hits_front = 0; pane_ok = 0; tot = 0
        for fu in (0.04, 0.31, 0.69, 0.96):
            for fv in (0.04, 0.35, 0.72, 0.96):
                du, dv = a0 + (a1 - a0) * fu, b0 + (b1 - b0) * fv
                if o["round"] and (du * du + dv * dv) > (0.8 * a1) ** 2:
                    continue
                P = O + T * du + Z * dv
                tot += 1
                h = bvh.ray_cast(P + Nn * 0.03, Nn, o["clear"])
                if h[0] is not None:
                    hits_front += 1
                h = bvh.ray_cast(P + Nn * 0.03, -Nn, 3.0)
                if h[0] is not None and abs(h[3] - (o["depth"] + 0.03)) < 0.004 and pm[h[2]]:
                    pane_ok += 1
        if hits_front:
            bad.append((i, "%d of %d rays out of the opening hit geometry within %.1f m" % (hits_front, tot, o["clear"])))
        if pane_ok < tot:
            bad.append((i, "only %d of %d rays into the opening land on the pane at the recess depth" % (pane_ok, tot)))
    for key, lst in planes.items():                                   # two openings on one plane must not overlap
        for x in range(len(lst)):
            for y in range(x + 1, len(lst)):
                i, O, T, a0, a1, b0, b1 = lst[x]; j, O2, T2, c0, c1, d0, d1 = lst[y]
                off = (O2 - O).dot(T); offv = O2.z - O.z
                if a0 < c1 + off - 1e-4 and c0 + off < a1 - 1e-4 and b0 < d1 + offv - 1e-4 and d0 + offv < b1 - 1e-4:
                    bad.append((i, "overlaps opening %d" % j))
    # every pane polygon must belong to an opening
    n_pane = 0; stray = 0
    for pi, p in enumerate(me.polygons):
        if not pm[pi]:
            continue
        n_pane += 1
        c = p.center; found = False
        for o in A.open:
            O, T, Nn = Vector(o["O"]), Vector(o["T"]), Vector(o["N"])
            rel = c - O
            if abs(rel.dot(Nn) + o["depth"]) < 0.003:
                a0, a1, b0, b1 = o["rect"]
                if a0 - 1e-3 <= rel.dot(T) <= a1 + 1e-3 and b0 - 1e-3 <= rel.z <= b1 + 1e-3:
                    found = True; break
        if not found:
            stray += 1
    if stray:
        bad.append((-1, "%d pane polygons are not in a modelled opening" % stray))
    return len(A.open), n_pane, bad


def export(ob, sub):
    d = os.path.join(OUT, sub); os.makedirs(d, exist_ok=True)
    bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True); bpy.context.view_layer.objects.active = ob
    path = os.path.join(d, ob.name + ".glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=True, export_yup=True)
    if SURF and (sub in SURF_FAMILIES or ob.name in SURF_ASSETS) and ob.name not in SURF_SKIP:
        surface_attach(path, ob.name)
    return path


def export_sep(ob, d):
    bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True); bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=os.path.join(d, ob.name + ".gltf"), export_format='GLTF_SEPARATE', use_selection=True, export_apply=True,
                              export_yup=True, export_keep_originals=True)
