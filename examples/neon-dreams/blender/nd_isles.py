"""Neon Dreams, iteration 9 (owner msgs 78 and 80): floating rock islands for what floats on the cloud.

    Blender -b --factory-startup -P examples/neon-dreams/blender/nd_isles.py -- [--src assets/treeisle/floating-isle-bare.glb] [--out examples/neon-dreams/meshes/_thirdparty/isles]

THIRD-PARTY: the rock is the owner-supplied floating island of the Tree of Life POC (assets/treeisle/floating-isle-bare.glb;
assets/treeisle.manifest.json: "supplied by the owner and carries no licence text"). It is gitignored, and so is everything
this script derives from it: the output goes to meshes/_thirdparty/ (gitignored) and is never committed. Re-run this
script to make it.

What it derives, in the Lanternwright world (a night harbour on the cloud): the island's ROCKY BASE ONLY (the grass cap is
dropped: decks, gardens and houses stand on it), re-shaded for the night: the stone darkened toward violet slate, its
moss seams turned into faint bioluminescent veins (an emissive texture made from the base map's green seams, baked violet
and pink; the material `isle_stone` is not a light role, so the scene keeps its baked emission). Three shapes, each 1 unit
across with the top at y = 0 (scale it in the layout: 1 unit = the island's width in metres):
  isle_rock_a: the island as it is, its underside a little deeper;
  isle_rock_b: a wide plate with a shallow keel (for towns: the Condensery, Highwater, the Keep's town);
  isle_rock_c: a deep eroded spire hanging under a small top (lighthouses, the Watch).
The hanging roots and vines that go under them are the kit's own (garden/root_curtain_*.glb, nd_kit_garden.py).
Exported as .gltf with the textures beside them, so every variant shares one set of images (ADR-2110).
"""
import bpy, bmesh, sys, os, math, random
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
arg = lambda n, d: argv[argv.index(n) + 1] if n in argv else d
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = arg("--src", os.path.join(HERE, "..", "..", "..", "assets", "treeisle", "floating-isle-bare.glb"))
OUT = arg("--out", os.path.join(HERE, "..", "meshes", "_thirdparty", "isles")); os.makedirs(OUT, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SRC)
rock = [o for o in bpy.data.objects if o.type == 'MESH' and "Rocky" in o.name][0]
for o in list(bpy.data.objects):
    if o != rock:
        bpy.data.objects.remove(o)
rock.parent = None
bpy.context.view_layer.objects.active = rock; rock.select_set(True)
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
mat = rock.data.materials[0]
nt = mat.node_tree
bsdf = [n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED'][0]
base_img = None
for n in nt.nodes:
    if n.type == 'TEX_IMAGE' and "BaseMap" in n.image.name:
        base_img = n.image

# ---- the night stone and its veins: two new images from the base map
w, h = base_img.size
px = np.array(base_img.pixels[:], dtype=np.float32).reshape(h, w, 4)
r, g, b = px[..., 0], px[..., 1], px[..., 2]
green = np.clip((g - np.maximum(r, b)) * 6.0, 0.0, 1.0)                 # the moss seams
lum = 0.3 * r + 0.55 * g + 0.15 * b
stone = np.stack([lum * 1.05 + 0.02, lum * 0.92 + 0.02, lum * 1.30 + 0.035], -1)   # violet slate (the image is sRGB). Draft 1 at 0.62x read as black cut-outs at night
seam = green[..., None]
stone = stone * (1.0 - 0.6 * seam) + seam * np.array([0.10, 0.07, 0.16])         # the seams dark and wet under their glow
night = bpy.data.images.new("isle_stone_base", w, h, alpha=False)
night.pixels[:] = np.concatenate([np.clip(stone, 0, 1), np.ones((h, w, 1), np.float32)], -1).ravel()
rng = np.random.default_rng(78)
blot = np.kron(rng.random((h // 64, w // 64)), np.ones((64, 64)))[:h, :w]           # the glow comes in patches, not every seam
glowv = green * (0.35 + 0.65 * (blot > 0.45))
hue = np.kron(rng.random((h // 128, w // 128)), np.ones((128, 128)))[:h, :w][..., None]
col = (1 - hue) * np.array([0.62, 0.42, 1.0]) + hue * np.array([1.0, 0.45, 0.85])     # garden_b violet to garden_a pink
emis = bpy.data.images.new("isle_stone_glow", w, h, alpha=False)
emis.pixels[:] = np.concatenate([np.clip(col * glowv[..., None], 0, 1), np.ones((h, w, 1), np.float32)], -1).ravel()
for im, nm in ((night, "isle_stone_base.png"), (emis, "isle_stone_glow.png")):
    im.filepath_raw = os.path.join(OUT, nm); im.file_format = 'PNG'; im.save()
for n in nt.nodes:
    if n.type == 'TEX_IMAGE' and n.image == base_img:
        n.image = night
te = nt.nodes.new('ShaderNodeTexImage'); te.image = emis
nt.links.new(te.outputs[0], bsdf.inputs['Emission Color']); bsdf.inputs['Emission Strength'].default_value = 3.0          # the veins: faint at 1.0 from 300 m
mat.name = "isle_stone"

# ---- normalise: 1 unit across, top at 0
me = rock.data
co = np.array([v.co[:] for v in me.vertices])
span = max(np.ptp(co[:, 0]), np.ptp(co[:, 1])); ctr = np.array([(co[:, 0].max() + co[:, 0].min()) / 2, (co[:, 1].max() + co[:, 1].min()) / 2, co[:, 2].max()])
co = (co - ctr) / span
base_co = co.copy()


def variant(name, deep=1.0, flat=1.0, taper=0.0, wobble=0.0, seed=0):
    """deep: the underside's depth x; flat: the top's spread x (xy); taper: how much the underside narrows toward its tip;
    wobble: a low-frequency lean of the keel, so no two read as the same bowl."""
    c = base_co.copy(); zmin = c[:, 2].min()
    t = np.clip(c[:, 2] / zmin, 0.0, 1.0)                                    # 0 at the top, 1 at the keel
    c[:, 0] *= flat; c[:, 1] *= flat
    k = 1.0 - taper * t ** 1.4
    c[:, 0] *= k; c[:, 1] *= k
    c[:, 2] = np.where(c[:, 2] < 0, c[:, 2] * deep, c[:, 2])
    rr = random.Random(seed); ph = rr.uniform(0, 6.28)
    c[:, 0] += wobble * t ** 2 * math.cos(ph); c[:, 1] += wobble * t ** 2 * math.sin(ph)
    s = max(np.ptp(c[:, 0]), np.ptp(c[:, 1])); c /= s
    c[:, 2] -= c[:, 2].max()
    ob = rock.copy(); ob.data = rock.data.copy(); ob.name = name; ob.data.name = name
    bpy.context.scene.collection.objects.link(ob)
    for v, p in zip(ob.data.vertices, c):
        v.co = p
    ob.data.update()
    return ob, float(-c[:, 2].min())


bpy.context.scene.collection.objects.unlink(rock) if rock.name in bpy.context.scene.collection.objects else None
for c in list(rock.users_collection):
    c.objects.unlink(rock)
made = []
for (nm, kw) in (("isle_rock_a", dict(deep=1.3, flat=1.0, taper=0.15, wobble=0.04, seed=1)),
                 ("isle_rock_b", dict(deep=0.75, flat=1.25, taper=0.45, wobble=0.06, seed=2)),
                 ("isle_rock_c", dict(deep=2.6, flat=0.9, taper=0.72, wobble=0.12, seed=3))):
    ob, depth = variant(nm, **kw)
    bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True); bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, nm + ".gltf"), export_format='GLTF_SEPARATE', use_selection=True, export_yup=True,
                              export_apply=True, export_texture_dir="", export_image_format='AUTO')
    made.append((nm, len(ob.data.polygons), depth))
print("ISLES", made)
MM = arg("--merge-manifest", "")
if MM:                                                     # the kit manifest lists them (path, bounds, material) so the layout can place them; the files stay untracked
    import json
    doc = json.load(open(MM)); names = {"_thirdparty/isles/%s.gltf" % nm for nm, _, _ in made}
    doc["assets"] = [a for a in doc["assets"] if a["asset"] not in names] + [
        {"asset": "_thirdparty/isles/%s.gltf" % nm, "tris": tris, "height_m": 0.0, "bounds_m": [-0.5, round(-depth, 3), -0.5, 0.5, 0.0, 0.5], "materials": ["isle_stone"],
         "what": "a floating rock island, 1 unit across, top at 0 (scale = width in metres)", "third_party": "derived by blender/nd_isles.py from assets/treeisle/floating-isle-bare.glb (owner-supplied, no licence recorded): gitignored, never committed"}
        for nm, tris, depth in made]
    json.dump(doc, open(MM, "w"), indent=1)
