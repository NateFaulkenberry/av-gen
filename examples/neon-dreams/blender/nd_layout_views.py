"""Neon Dreams: the Mooring's layout seen from above, for the owner to mark up (iteration 9, owner msg 78).

    Blender -b --factory-startup -P nd_layout_views.py -- --layout art/layout.json --kit meshes --out DIR [--res 2400]

Imports every instance of art/layout.json inside the Mooring's area (the kit's GLBs, placed by their full quaternion),
paints each by what it is (district walls, decks, gardens, landmarks, signs) in Workbench, draws the journey as a line,
and renders two views: TOP (orthographic, north up) and OBLIQUE (a high three-quarter from the south-west, where the
approach comes from). Writes <view>.png and <view>_labels.json (label -> pixel); nd_layout_labels.py draws the labels.
CPU only (Workbench): no GPU lock needed.
"""
import bpy, sys, os, json, math
from mathutils import Vector, Quaternion
from bpy_extras.object_utils import world_to_camera_view

HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import nd_world as W

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
arg = lambda n, d: argv[argv.index(n) + 1] if n in argv else d
LAY = json.load(open(arg("--layout", os.path.join(HERE, "..", "art", "layout.json"))))
KIT = arg("--kit", os.path.join(HERE, "..", "meshes")); OUT = arg("--out", os.path.join(HERE, "out_views")); os.makedirs(OUT, exist_ok=True)
RES = int(arg("--res", 2400))
BOX = (-400.0, -330.0, 360.0, 270.0)          # plan x0, y0, x1, y1 of the area drawn

bpy.ops.wm.read_factory_settings(use_empty=True)
sc = bpy.context.scene
TEMPL = {}


def template(asset):
    if asset in TEMPL:
        return TEMPL[asset]
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.join(KIT, asset))
    new = [o for o in bpy.data.objects if o not in before]; meshes = [o for o in new if o.type == 'MESH']
    if not meshes:
        TEMPL[asset] = None; return None
    ob = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.select_all(action='DESELECT')
        for o in meshes:
            o.select_set(True)
        bpy.context.view_layer.objects.active = ob; bpy.ops.object.join()
    bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True); bpy.context.view_layer.objects.active = ob
    ob.parent = None; bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for o in new:
        if o != ob and o.name in bpy.data.objects:
            bpy.data.objects.remove(o)
    for c in list(ob.users_collection):
        c.objects.unlink(ob)
    TEMPL[asset] = ob.data
    return ob.data


DISTRICT = {"quay_north": (0.80, 0.55, 0.62), "quay_south": (0.85, 0.66, 0.70), "stacks": (0.62, 0.58, 0.82), "lanternhill": (0.50, 0.62, 0.92),
            "lampmarket": (0.95, 0.62, 0.30), "eastreach": (0.55, 0.80, 0.70), "station": (0.9, 0.9, 0.5), "plaza": (0.75, 0.75, 0.75), "lampwick": (0.95, 0.80, 0.45),
            "watch": (0.95, 0.80, 0.45), "lighthouse": (0.9, 0.9, 0.9), "great_lantern": (1.0, 0.45, 0.70), "garden": (0.35, 0.75, 0.45)}


def colour(i):
    a, z = i["asset"], i["zone"]
    fam = a.split("/")[0]
    if "mooring_mast" in a or "great_lantern" in a:
        return (1.0, 0.40, 0.68)
    if fam == "buildings":
        return DISTRICT.get(z, (0.7, 0.7, 0.7))
    if fam == "garden" or z in ("roof_garden", "steps") or "pocket" in z:
        return (0.30, 0.72, 0.42)
    if fam == "signs":
        return (1.0, 0.25, 0.75)
    if fam == "street":
        return (0.95, 0.85, 0.40) if ("lantern" in a or "lamp" in a) else (0.55, 0.55, 0.6)
    if fam == "decks":
        return (0.42, 0.40, 0.46) if "pier" not in a else (0.5, 0.45, 0.4)
    if fam == "vehicles":
        return (1.0, 0.9, 0.3)
    if fam == "landmarks":
        return (0.95, 0.95, 0.95)
    return (0.6, 0.6, 0.6)


coll = bpy.data.collections.new("layout"); sc.collection.children.link(coll)
n = 0
for i in LAY["instances"]:
    x, y = i["pos"][0], -i["pos"][2]
    if not (BOX[0] <= x <= BOX[2] and BOX[1] <= y <= BOX[3]) or i["asset"].startswith("far/"):
        continue
    me = template(i["asset"])
    if me is None:
        continue
    o = bpy.data.objects.new("i%d" % n, me); coll.objects.link(o); n += 1
    q = i["quat"]                                                    # engine [x, y, z, w] -> Blender (x, -z, y)
    o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = Quaternion((q[3], q[0], -q[2], q[1]))
    o.location = (i["pos"][0], -i["pos"][2], i["pos"][1]); s = i["scale"]; o.scale = (s[0], s[2], s[1])
    o.color = (*colour(i), 1.0)
print("placed", n)
# the journey (0 to 47 s, in the town) as a tube
J = W.Journey(); pts = [J.at(t / 10.0) for t in range(0, 480, 2)]
cu = bpy.data.curves.new("journey", 'CURVE'); cu.dimensions = '3D'; cu.bevel_depth = 1.1
sp = cu.splines.new('POLY'); sp.points.add(len(pts) - 1)
for k, p in enumerate(pts):
    sp.points[k].co = (p[0], p[1], p[2], 1.0)
jo = bpy.data.objects.new("journey", cu); coll.objects.link(jo); jo.color = (1.0, 1.0, 0.1, 1.0)
# a ground plane at the cloud's level, for contrast
bpy.ops.mesh.primitive_plane_add(size=1, location=((BOX[0] + BOX[2]) / 2, (BOX[1] + BOX[3]) / 2, 40.0)); g = bpy.context.active_object
g.scale = (BOX[2] - BOX[0], BOX[3] - BOX[1], 1); g.color = (0.30, 0.32, 0.42, 1)

sc.render.engine = 'BLENDER_WORKBENCH'
sh = sc.display.shading; sh.light = 'STUDIO'; sh.color_type = 'OBJECT'; sh.show_shadows = True; sh.show_cavity = True; sh.cavity_type = 'BOTH'
sh.show_object_outline = False
sc.display.shadow_shift = 0.1
sc.render.film_transparent = False
sc.world = bpy.data.worlds.new("w"); sc.world.color = (0.20, 0.21, 0.28)

LABELS = {}
def zcent(zone):
    ps = [i["pos"] for i in LAY["instances"] if i["zone"] == zone]
    if not ps:
        return None
    return (sum(p[0] for p in ps) / len(ps), -sum(p[2] for p in ps) / len(ps), max(p[1] for p in ps) + 12)
for zone, text in (("lampmarket", "THE LAMPMARKET (new)"), ("lanternhill", "LANTERNHILL (new)"), ("eastreach", "EASTREACH (new)"), ("stacks", "THE STACKS"),
                   ("plaza", "PLAZA"), ("station", "KITE LINE STATION"), ("garden", "EAST GARDEN"), ("steps", "THE STEPS"), ("low_walk", "LOW WALK"),
                   ("lampwick", "LAMPWICK ISLE (new, floats)"), ("great_lantern", "GREAT LANTERN"), ("pier", "PIER")):
    c = zcent(zone)
    if c:
        LABELS[text] = c
for isl in LAY.get("islands", []):
    if isl["bob"].startswith("isle_"):
        c = isl["centre"]; LABELS[isl["bob"].replace("isle_", "isle: ").upper() + " (new)"] = (c[0], -c[2], c[1] + 8)
LABELS["THE QUAY (street)"] = (-10.0, W.qy(-10) + 0, 150.0)
LABELS["MOORING MAST (new)"] = W.MOORING_MAST[:2] + (W.MOORING_MAST[2] + 40,)
for t in (25.25, 35.75, 45.0):
    p = J.at(t); LABELS["journey %.2f s" % t] = (p[0], p[1], p[2] + 4)


def render(name, cam_ob):
    sc.camera = cam_ob; sh.show_shadows = name != "top"
    sc.render.resolution_x = RES; sc.render.resolution_y = int(RES * (BOX[3] - BOX[1]) / (BOX[2] - BOX[0])) if name == "top" else int(RES * 9 / 16)
    sc.render.filepath = os.path.join(OUT, name + ".png"); bpy.ops.render.render(write_still=True)
    out = {}
    for k, v in LABELS.items():
        co = world_to_camera_view(sc, cam_ob, Vector(v))
        out[k] = [co.x * sc.render.resolution_x, (1 - co.y) * sc.render.resolution_y]
    json.dump(out, open(os.path.join(OUT, name + "_labels.json"), "w"), indent=1)


cd = bpy.data.cameras.new("top"); cd.type = 'ORTHO'; cd.ortho_scale = max(BOX[2] - BOX[0], BOX[3] - BOX[1])
top = bpy.data.objects.new("top", cd); sc.collection.objects.link(top)
top.location = ((BOX[0] + BOX[2]) / 2, (BOX[1] + BOX[3]) / 2, 900.0); top.rotation_euler = (0, 0, 0)
cd.clip_end = 2000
render("top", top)
co = bpy.data.cameras.new("oblique"); co.lens = 24; co.clip_end = 5000
ob = bpy.data.objects.new("oblique", co); sc.collection.objects.link(ob)
tgt = Vector((-40.0, 50.0, 135.0)); ob.location = Vector((-300.0, -330.0, 330.0))
ob.rotation_euler = (tgt - ob.location).to_track_quat('-Z', 'Y').to_euler()
render("oblique", ob)
print("VIEWS done")
