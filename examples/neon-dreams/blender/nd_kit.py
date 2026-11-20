"""Neon Dreams -- the kit (iteration 2): every original asset of the POC, regenerated headless.

  Blender -b --factory-startup -P examples/neon-dreams/blender/nd_kit.py -- --out examples/neon-dreams/meshes \
          --atlas examples/neon-dreams/meshes/signs [--only buildings,garden,... [--merge-manifest <kit_manifest.json>]]

GLB, metres, origin at the base centre at deck level, Blender Z-up exported Y-up (front of a building = +z in the GLB).
One mesh per asset; materials are named by light role so the scene can drive each (see kit_manifest.json "materials").
THE WINDOW RULE (nd_kitcore.py) is audited on every asset; the result is written to window_audit.json and the run fails
loudly (exit code 3) if any pane breaks it.

Modules: nd_kitcore (materials, wall primitives, the rule and its audit), nd_kit_mooring (buildings, Great Lantern,
lighthouses, decks, street, craft), nd_kit_garden (plants and garden pieces), nd_kit_condensery, nd_kit_far (the Long Arc).
"""
import bpy, sys, os, json, math
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from nd_kitcore import *  # noqa
import nd_kit_mooring as M
import nd_kit_garden as G
import nd_kit_condensery as C
import nd_kit_far as F
import nd_kit_it9 as K9

ONLY = [x for x in arg("--only", "").split(",") if x]


def sign_kit():
    """Signs on the Tideglyph atlas. One grey emissive texture shared by every sign (.gltf + external PNG, so it is stored once)."""
    sd = os.path.join(OUT, "signs"); os.makedirs(sd, exist_ok=True)
    src_dir = arg("--atlas", os.path.join(OUT, "signs"))
    cells = {c["id"]: c for c in json.load(open(os.path.join(src_dir, "tideglyph_atlas.json")))["cells"]}
    src = bpy.data.images.load(os.path.join(src_dir, "tideglyph_atlas.png")); w_, h_ = src.size
    px = np.empty(w_ * h_ * 4, dtype=np.float32); src.pixels.foreach_get(px); px = px.reshape(h_, w_, 4)
    g = np.clip(px[..., 0] + 0.35 * px[..., 1] + 0.004, 0, 1)
    img = bpy.data.images.new("tideglyph_emissive", w_, h_, alpha=False); img.colorspace_settings.name = 'sRGB'
    out = np.stack([g, g, g, np.ones_like(g)], -1); img.pixels.foreach_set(out.ravel().astype(np.float32))
    img.filepath_raw = os.path.join(sd, "tideglyph_emissive.png"); img.file_format = 'PNG'; img.save()
    img = bpy.data.images.load(os.path.join(sd, "tideglyph_emissive.png"))
    m, nt = new_mat("neon_a")
    o_ = node(nt, 'ShaderNodeOutputMaterial'); b = node(nt, 'ShaderNodeBsdfPrincipled'); nt.links.new(b.outputs[0], o_.inputs[0])
    b.inputs['Base Color'].default_value = (0.008, 0.008, 0.01, 1); b.inputs['Roughness'].default_value = 0.35
    t = node(nt, 'ShaderNodeTexImage'); t.image = img
    nt.links.new(t.outputs[0], b.inputs['Emission Color']); b.inputs['Emission Strength'].default_value = STRENGTH["neon_a"]
    MATS["neon_a"] = m
    res = []

    def face(A, cell, p0, p1, p2, p3):
        u0, v0, u1, v1 = cells[cell]["uv"]
        A.m("neon_a").quad(p0, p1, p2, p3, uv=(u0, v0, u1, v1), double=False)

    for i in range(8):          # blade: hangs off a wall. Origin = wall mount; the panel runs out along +x, centred on z = 0
        A = Asset("sign_blade_%d" % i); hgt = 6.5; wid = hgt * cells["blade_%d" % i]["aspect"]; x0 = 0.3
        A.m("metal").box((x0 + wid / 2, 0, 0), (wid + 0.16, 0.22, hgt + 0.16))
        for zz in (-hgt * 0.38, hgt * 0.38):
            A.m("metal").box((x0 / 2, 0, zz), (x0, 0.08, 0.08)); A.m("metal").beam((0, 0, zz + 0.5), (x0 + wid * 0.5, 0, zz), 0.03)
        face(A, "blade_%d" % i, (x0, -0.115, -hgt / 2), (x0 + wid, -0.115, -hgt / 2), (x0 + wid, -0.115, hgt / 2), (x0, -0.115, hgt / 2))
        face(A, "blade_%d" % i, (x0 + wid, 0.115, -hgt / 2), (x0, 0.115, -hgt / 2), (x0, 0.115, hgt / 2), (x0 + wid, 0.115, hgt / 2))
        A.meta = {"size_m": [round(wid, 2), hgt], "brand": cells["blade_%d" % i]["brand"]}; A.finish(); res.append(A)
    for i in range(8):          # fascia: stands on an awning edge. Origin = bottom centre; faces -y
        A = Asset("sign_fascia_%d" % i); hgt = 1.2; wid = hgt * cells["fascia_%d" % i]["aspect"]
        A.m("metal").box((0, 0.1, hgt / 2), (wid + 0.12, 0.2, hgt + 0.12))
        face(A, "fascia_%d" % i, (-wid / 2, -0.005, 0), (wid / 2, -0.005, 0), (wid / 2, -0.005, hgt), (-wid / 2, -0.005, hgt))
        A.meta = {"size_m": [round(wid, 2), hgt], "brand": cells["fascia_%d" % i]["brand"]}; A.finish(); res.append(A)
    for i in range(10):         # roundel lightbox on a short arm. Origin = wall mount; disc centre at x = 1.0
        A = Asset("sign_roundel_%d" % i); d_ = 1.4
        A.m("metal").box((1.0, 0, 0), (d_ + 0.12, 0.19, d_ + 0.12))
        A.m("metal").box((0.15, 0, 0), (0.3, 0.06, 0.06))
        face(A, "roundel_%d" % i, (1.0 - d_ / 2, -0.1, -d_ / 2), (1.0 + d_ / 2, -0.1, -d_ / 2), (1.0 + d_ / 2, -0.1, d_ / 2), (1.0 - d_ / 2, -0.1, d_ / 2))
        face(A, "roundel_%d" % i, (1.0 + d_ / 2, 0.1, -d_ / 2), (1.0 - d_ / 2, 0.1, -d_ / 2), (1.0 - d_ / 2, 0.1, d_ / 2), (1.0 + d_ / 2, 0.1, d_ / 2))
        A.meta = {"size_m": [d_, d_], "brand": cells["roundel_%d" % i]["brand"]}; A.finish(); res.append(A)
    for i in range(4):          # rooftop letters on a scaffold. Origin = base centre on the roof; faces -y
        A = Asset("sign_roof_%d" % i); hgt = 2.2; wid = hgt * cells["roof_%d" % i]["aspect"]; lift = 2.6
        for k in range(6):
            x = -wid / 2 + k * wid / 5
            A.m("metal").box((x, 0.25, (lift + hgt) / 2), (0.1, 0.1, lift + hgt)); A.m("metal").beam((x, 0.25, lift * 0.9), (x, 1.6, 0), 0.06)
        for zz in (lift, lift + hgt):
            A.m("metal").box((0, 0.25, zz), (wid + 0.2, 0.08, 0.08))
        face(A, "roof_%d" % i, (-wid / 2, 0.18, lift), (wid / 2, 0.18, lift), (wid / 2, 0.18, lift + hgt), (-wid / 2, 0.18, lift + hgt))
        A.meta = {"size_m": [round(wid, 2), hgt], "brand": cells["roof_%d" % i]["brand"]}; A.finish(); res.append(A)
    return res, sd


for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)
mats()
families = []


def fam(name, fn):
    if not ONLY or name in ONLY:
        families.append((name, fn()))


def buildings():
    out = []
    specs = [("mooring_shophouse_a", 11, dict(floors=6, w=8.4, d=10.8, lit=0.34, warm=0.3)),                    # the five approved modules of kit v1
             ("mooring_shophouse_b", 23, dict(floors=8, w=9.6, d=10.8, lit=0.26, warm=0.22)),
             ("mooring_shophouse_c", 37, dict(floors=4, w=7.2, d=8.4, lit=0.42, warm=0.4)),
             ("mooring_stack_a", 51, dict(floors=13, w=9.6, d=9.6, lit=0.22, warm=0.18)),
             ("mooring_stack_b", 68, dict(floors=17, w=8.4, d=9.6, lit=0.16, warm=0.2)),
             ("mooring_shophouse_d", 104, dict(floors=3, w=7.2, d=8.4, lit=0.45, warm=0.45, roof="garden", garden=0.8)),   # low houses with roof terraces
             ("mooring_shophouse_e", 117, dict(floors=4, w=9.6, d=8.4, lit=0.4, warm=0.35, roof="garden", garden=0.8)),
             ("mooring_shophouse_f", 131, dict(floors=5, w=8.4, d=8.4, lit=0.36, warm=0.3, roof="garden", garden=0.7)),
             ("mooring_shophouse_g", 149, dict(floors=7, w=7.2, d=10.8, lit=0.3, warm=0.25, garden=0.6)),
             ("mooring_shophouse_h", 163, dict(floors=9, w=9.6, d=10.8, lit=0.24, warm=0.2, garden=0.5)),
             ("mooring_stack_c", 177, dict(floors=10, w=8.4, d=8.4, lit=0.2, warm=0.2, roof="garden", garden=0.7)),
             ("mooring_stack_d", 191, dict(floors=15, w=9.6, d=9.6, lit=0.18, warm=0.22, garden=0.6)),
             ("mooring_stack_e", 207, dict(floors=11, w=7.2, d=8.4, lit=0.26, warm=0.3, roof="garden", garden=0.7)),
             ("mooring_shophouse_i", 223, dict(floors=2, w=9.6, d=8.4, lit=0.5, warm=0.5, roof="garden", garden=0.0)),          # two floors: its terrace is at the camera's height
             ("mooring_shophouse_j", 239, dict(floors=2, w=7.2, d=8.4, lit=0.5, warm=0.5, roof="garden", garden=0.0))]
    for name, seed, kw in specs:
        out.append(M.shophouse(name, seed, **kw))
    out += [M.gallery_slab(), M.corner_tower(), M.capsule_tower(), M.station(), M.kiosk(), M.lone_lamp_house()]
    out += K9.towers()                                                      # iteration 9 (owner msg 85): tower variety
    return out


fam("buildings", buildings)
fam("landmarks", lambda: list(M.great_lantern()) + [M.lighthouse("lighthouse_a", 301, height=58.0, house_floors=3, base_w=7.2, lattice=20.0),
                                                     M.lighthouse("lighthouse_b", 302, height=41.0, house_floors=4, base_w=7.2, lattice=12.0),
                                                     M.lighthouse("lighthouse_c", 303, height=50.0, house_floors=2, base_w=4.8, lattice=24.0), M.beam_cone(), K9.mooring_mast()])
fam("decks", M.deck_kit)
fam("street", lambda: M.street_kit() + [M.gondola_pylon()])
fam("garden", lambda: G.garden_kit() + K9.garden_extra())
fam("vehicles", M.craft)
fam("condensery", C.condensery_kit)
fam("far", lambda: F.far_kit() + [K9.drowned_mast()])
signs = None
if not ONLY or "signs" in ONLY:
    signs, sign_dir = sign_kit()

manifest, audit_out, n_bad = [], {}, 0
for famname, assets in families:
    for A in assets:
        n_open, n_pane, bad = audit(A)
        if n_open or n_pane or bad:
            audit_out[A.name] = {"openings": n_open, "pane_polygons": n_pane, "violations": ["opening %d: %s" % b for b in bad]}
        n_bad += len(bad)
        for b in bad[:6]:
            print("AUDIT", A.name, b)
        export(A.ob, famname)
        zs = [v.co.z for v in A.ob.data.vertices]; xs = [v.co.x for v in A.ob.data.vertices]; ys = [v.co.y for v in A.ob.data.vertices]
        manifest.append({"asset": famname + "/" + A.name + ".glb", "tris": A.tris, "height_m": round(max(zs), 1),
                         "bounds_m": [round(min(xs), 2), round(min(zs), 2), round(-max(ys), 2), round(max(xs), 2), round(max(zs), 2), round(-min(ys), 2)],
                         "materials": sorted(A.mb.keys()), **A.meta})
if signs:
    for A in signs:
        export_sep(A.ob, sign_dir)
        vs = A.ob.data.vertices
        manifest.append({"asset": "signs/" + A.name + ".gltf", "tris": A.tris,
                         "bounds_m": [round(min(v.co.x for v in vs), 2), round(min(v.co.z for v in vs), 2), round(-max(v.co.y for v in vs), 2),
                                      round(max(v.co.x for v in vs), 2), round(max(v.co.z for v in vs), 2), round(-min(v.co.y for v in vs), 2)], **A.meta})
mat_doc = {"facade": "walls (atlas cell tiles; the factor is the building's hue)", "trim": "slabs, frames, sills, kerbs", "metal": "pipes, rails, stilts, inlays", "deck": "walking surfaces: boards, deck plates, pavers", "glass": "unlit panes; uv = (level, window hash)",
           "cloth": "awnings, laundry (tint per instance)", "foliage": "dark leaves", "pool": "garden pool",
           "win_warm": "lit panes and shop bays; uv = (level, window hash)", "win_cool": "lit panes; uv = (level, window hash)", "win_stair": "stairwell slot windows, always lit: do not run the lit/dark pane program on it", "strip": "eave lines, soffit lines, crown",
           "beacon": "red mast lights", "landmark": "Great Lantern bell; uv = (level, panel id)", "garden": "plant light points; uv = (level, hash)",
           "lantern": "lantern strings, street and soffit lamps", "keel": "keel lanterns under the decks", "neon_a": "sign faces on the Tideglyph atlas",
           "beam_lamp": "lighthouse lantern-room panes; uv.v = pane bearing / 360", "beam": "fallback beam cone (additive)",
           "cond_glow": "Condensery throats, louvres, flare", "cond_lamp": "Condensery sodium lamps", "cond_work": "Condensery work lights"}
if not ONLY:
    json.dump({"units": "metres, Y-up in the GLB, origin at base centre at deck level; a building's front (shop side) faces +z in the GLB",
               "window_rule": "every pane (glass, win_warm, win_cool, win_stair, beam_lamp) sits in a modelled, recessed, framed opening inside one 2.4 x 3.2 m facade cell; see window_audit.json",
               "materials": mat_doc, "emissive_strength": STRENGTH, "grid": {"floor_m": FLOOR, "bay_m": BAY},
               "surface": {"what": "iteration 8: one shared texture set on the unlit surface roles (facade, trim, metal, deck) of the assets listed; the GLBs reference it by URI, so it is stored once",
                           "files": ["tex/nd_surface_base.png", "tex/nd_surface_normal.png", "tex/nd_surface_orm.png"], "regions": "tex/nd_surface.json (written by blender/nd_surface_atlas.py)",
                           "reads_as": "the atlas MULTIPLIES each material's factors: base colour (a building's hue is its facade factor; an instance's `color.facade` in layout.json replaces it), "
                                       "roughness (G), metallic (B), occlusion (R); the normal map is tangent space, glTF convention",
                           "facade_cells": "a wall is mapped cell by cell (2.4 m bay x 3.2 m floor = one tile): rows = finish (light render, dark painted render, corrugated sheet, glazed tile on shop floors), "
                                           "columns = windowed / blank / the lowest floor of a block (grime rising)",
                           "assets": sorted(SURFACED)},
               "inhabitant_scale": {"cast": "Glowmere aliens at scale 1.0 (1.66 to 1.79 m)", "door_head_m": 2.04, "rail_m": 1.05, "step_riser_m": 0.2, "balcony_depth_m": 1.05,
                                    "gallery_depth_m": 1.5, "gangway_width_m": 2.0, "bench_seat_m": 0.43},
               "assets": manifest}, open(os.path.join(OUT, "kit_manifest.json"), "w"), indent=1)
    json.dump({"rule": "see nd_kitcore.py", "assets_with_panes": len(audit_out), "violations": n_bad, "assets": audit_out}, open(os.path.join(OUT, "window_audit.json"), "w"), indent=1)
if ONLY and arg("--merge-manifest", ""):                       # a partial run: put its assets into an existing manifest (replacing those of the same name)
    mp = arg("--merge-manifest", ""); doc = json.load(open(mp)); names = {m["asset"] for m in manifest}
    doc["assets"] = [a for a in doc["assets"] if a["asset"] not in names] + manifest
    json.dump(doc, open(mp, "w"), indent=1)
    print("MERGED", sorted(names), "into", mp, "->", len(doc["assets"]), "assets")
print("KIT assets", len(manifest), "tris", sum(m["tris"] for m in manifest), "audit: assets with panes", len(audit_out), "violations", n_bad)
print("KITSUMMARY", json.dumps([[m["asset"], m["tris"]] for m in manifest]))
if arg("--blend", ""):
    bpy.ops.wm.save_as_mainfile(filepath=arg("--blend", ""))
if n_bad:
    sys.exit(3)
