"""Neon Dreams, iteration 10 (owner msg 117): the jet-pack flyers' exhaust, in the Lanternwright language.

    Blender -b --factory-startup -P examples/neon-dreams/blender/nd_kit_jet.py -- [--out examples/neon-dreams/meshes] [--preview DIR]

The two cast variants that fly (alien-pilot, alien-ranger) already wear a twin-nozzle pack (`Backpack_Jet`, skinned to
the `Backpack` joint); what they lack is thrust you can see at 50-300 m. This prop is that thrust, original work:

  * from each nozzle a chain of three paper-lantern bulbs (ribbed, elongated spheres, 5.0 / 3.8 / 2.7 cm in radius),
    each smaller, dimmer and further apart than the last, 0.52 m in all: up close a stylised lantern exhaust, at distance a warm flame trail;
  * a lantern collar (a ribbed ring) at each nozzle's lip;
  * materials `jet_flame` (the bulbs) and `jet_collar` (the rings): white emissive colour over the kit's 16-step level
    ramp (u = brightness, bright at the nozzle, dim at the tip), so the scene sets colour and gain per flight phase
    (cruise bright, hover softer, idle off) and can scale the node along -Y with the thrust.

GLB frame: metres, Y up, origin at the midpoint between the two nozzle exits, the plume pointing down -Y, nozzles at
x = +-0.082. Attach: a socket on joint `Backpack` at joint-local (0, 0.31, 0.01) (the nozzle exits measured in the
joint's frame from the pilot GLB's inverse bind matrix), rotated so GLB -Y lies along the joint's +Y (180 degrees about
X): the joint's +Y runs down the pack toward the nozzles. Writes meshes/props/jet_plume.glb and its manifest entry.
"""
import bpy, bmesh, sys, os, math, json
from mathutils import Vector, Matrix

HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
arg = lambda n, d: argv[argv.index(n) + 1] if n in argv else d
OUT = arg("--out", os.path.join(HERE, "..", "meshes"))
PREVIEW = arg("--preview", None)
NOZZLE_X = 0.082

bpy.ops.wm.read_factory_settings(use_empty=True)
lv = bpy.data.images.load(os.path.join(OUT, "win_levels.png"))


def lvl(x):
    return (min(15, max(0, int(x * 15.99))) + 0.5) / 16.0


def mat(name, strength):
    m = bpy.data.materials.new(name); m.use_nodes = True; nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (0.02, 0.012, 0.008, 1); b.inputs["Roughness"].default_value = 0.5
    b.inputs["Emission Color"].default_value = (1, 1, 1, 1); b.inputs["Emission Strength"].default_value = strength
    t = nt.nodes.new("ShaderNodeTexImage"); t.image = lv; t.interpolation = 'Closest'
    nt.links.new(t.outputs[0], b.inputs["Emission Color"])
    return m


M_FLAME, M_COLLAR = mat("jet_flame", 16.0), mat("jet_collar", 10.0)


def lantern_bulb(bm, uvl, c, rad, length, level, ribs=8, rings=7):
    """A paper lantern: a sphere squashed to `length` along Y with `ribs` shallow creases, every vertex at one level."""
    verts = []
    for i in range(rings + 1):
        th = math.pi * i / rings; y = -math.cos(th) * length / 2; rr = math.sin(th) * rad
        row = []
        for j in range(ribs * 2):
            ph = 2 * math.pi * j / (ribs * 2); k = 1.0 if j % 2 == 0 else 0.86          # the creases between ribs
            row.append(bm.verts.new((c[0] + rr * k * math.cos(ph), c[1] + y, c[2] + rr * k * math.sin(ph))))
        verts.append(row)
    u = lvl(level)
    for i in range(rings):
        for j in range(ribs * 2):
            a, b_, cc, d = verts[i][j], verts[i][(j + 1) % (ribs * 2)], verts[i + 1][(j + 1) % (ribs * 2)], verts[i + 1][j]
            try:
                f = bm.faces.new((a, d, cc, b_))
            except ValueError:
                continue
            for lp in f.loops:
                lp[uvl].uv = (u, 0.5)


def collar(bm, uvl, c, r0, r1, h, level, n=16):
    """A ribbed ring at a nozzle's lip (a short tube, outer wall and bottom face)."""
    u = lvl(level)
    top_o = [bm.verts.new((c[0] + r1 * math.cos(2 * math.pi * k / n), c[1], c[2] + r1 * math.sin(2 * math.pi * k / n))) for k in range(n)]
    bot_o = [bm.verts.new((c[0] + r1 * 1.08 * math.cos(2 * math.pi * k / n), c[1] - h, c[2] + r1 * 1.08 * math.sin(2 * math.pi * k / n))) for k in range(n)]
    bot_i = [bm.verts.new((c[0] + r0 * math.cos(2 * math.pi * k / n), c[1] - h, c[2] + r0 * math.sin(2 * math.pi * k / n))) for k in range(n)]
    for k in range(n):
        k2 = (k + 1) % n
        for quad in ((top_o[k], top_o[k2], bot_o[k2], bot_o[k]), (bot_o[k], bot_o[k2], bot_i[k2], bot_i[k])):
            f = bm.faces.new(quad)
            for lp in f.loops:
                lp[uvl].uv = (u, 0.5)


def build():
    me = bpy.data.meshes.new("jet_plume"); ob = bpy.data.objects.new("jet_plume", me); bpy.context.scene.collection.objects.link(ob)
    ob.data.materials.append(M_FLAME); ob.data.materials.append(M_COLLAR)
    bm = bmesh.new(); uvl = bm.loops.layers.uv.new("UVMap")
    for sx in (-1, 1):
        x = sx * NOZZLE_X
        n0 = len(bm.faces)
        collar(bm, uvl, (x, 0.0, 0.0), 0.05, 0.068, 0.035, 0.85)
        for f in bm.faces[n0:] if hasattr(bm.faces, "__getitem__") else []:
            pass
        bm.faces.ensure_lookup_table()
        for f in list(bm.faces)[n0:]:
            f.material_index = 1
        # three bulbs, each smaller, dimmer and further apart than the last (a flame that tears into lanterns)
        for (cy, rad, ln, lev) in ((-0.11, 0.05, 0.15, 1.0), (-0.29, 0.038, 0.12, 0.7), (-0.47, 0.027, 0.09, 0.42)):
            m0 = len(bm.faces)
            lantern_bulb(bm, uvl, (x, cy, 0.0), rad, ln, lev)
            bm.faces.ensure_lookup_table()
            for f in list(bm.faces)[m0:]:
                f.material_index = 0
    # GLB frame: Blender Z-up is exported as Y-up. Build in a Y-down frame: rotate so the authored -Y (down the plume)
    # becomes Blender -Z (exported as GLB -Y)
    bm.transform(Matrix.Rotation(math.radians(90), 4, 'X'))
    bm.normal_update()
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(me); bm.free()
    for p in me.polygons:
        p.use_smooth = True
    return ob


ob = build()
tris = sum(len(p.vertices) - 2 for p in ob.data.polygons)
d = os.path.join(OUT, "props"); os.makedirs(d, exist_ok=True)
bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True); bpy.context.view_layer.objects.active = ob
path = os.path.join(d, "jet_plume.glb")
bpy.ops.export_scene.gltf(filepath=path, export_format='GLB', use_selection=True, export_apply=True, export_yup=True)
bb = [ob.matrix_world @ Vector(c) for c in ob.bound_box]
lo = [min(v[i] for v in bb) for i in range(3)]; hi = [max(v[i] for v in bb) for i in range(3)]
bounds = [round(lo[0], 3), round(lo[2], 3), round(-hi[1], 3), round(hi[0], 3), round(hi[2], 3), round(-lo[1], 3)]   # GLB x, y, z
man_p = os.path.join(OUT, "kit_manifest.json"); man = json.load(open(man_p))
man["assets"] = [a for a in man["assets"] if a["asset"] != "props/jet_plume.glb"]
man["assets"].append({"asset": "props/jet_plume.glb", "tris": tris, "bounds_m": bounds, "materials": ["jet_collar", "jet_flame"],
                      "socket": {"joint": "Backpack", "position": [0.0, 0.31, 0.01], "rotation_deg": [180.0, 0.0, 0.0],
                                 "note": "cast variants alien-pilot and alien-ranger (Backpack_Jet); GLB -Y along the joint's +Y"},
                      "note": "iteration 10 (msg 117): lantern-bulb exhaust for the jet-pack flyers; emissive roles jet_flame / jet_collar"})
json.dump(man, open(man_p, "w"), indent=1)
print("wrote", path, "tris", tris, "bounds", bounds)

if PREVIEW:
    # a quick look: the plume under the pilot's pack in the Flying_jet pose's neighbour (Floating), lit by its own glow
    os.makedirs(PREVIEW, exist_ok=True)
    src = arg("--cast", None)
    sc = bpy.context.scene
    if src:
        bpy.ops.import_scene.gltf(filepath=src)
        arm = [o for o in bpy.data.objects if o.type == 'ARMATURE'][0]
        for o in bpy.data.objects:
            if o.name.startswith("Icosphere"):
                o.hide_render = True
        if arm.animation_data is None:
            arm.animation_data_create()
        arm.animation_data.action = bpy.data.actions.get(arg("--clip", "Floating"))
        sc.frame_set(int(arg("--frame", "40")))
        pb = arm.pose.bones["Backpack"]
        # socket: joint frame, then (0, 0.31, 0.01), then 180 degrees about X; the exported plume's -Y is Blender -Z
        J = arm.matrix_world @ pb.matrix
        S = J @ Matrix.Translation((0.0, 0.31, 0.01)) @ Matrix.Rotation(math.radians(180), 4, 'X')
        # GLB(-Y) == Blender(-Z) for the plume object; the joint frame here is Blender's bone frame (Y along the bone)
        ob.matrix_world = S @ Matrix.Rotation(math.radians(-90), 4, 'X')
    sc.render.engine = 'BLENDER_EEVEE_NEXT' if 'BLENDER_EEVEE_NEXT' in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties['engine'].enum_items] else 'BLENDER_EEVEE'
    sc.render.resolution_x, sc.render.resolution_y = 900, 900
    w = bpy.data.worlds.new("w"); sc.world = w; w.use_nodes = True; w.node_tree.nodes['Background'].inputs[0].default_value = (0.03, 0.02, 0.06, 1)
    try:
        sc.eevee.use_bloom = True
    except AttributeError:
        pass
    for m_, col in ((M_FLAME, (1.0, 0.55, 0.25)), (M_COLLAR, (1.0, 0.35, 0.6))):
        b = m_.node_tree.nodes["Principled BSDF"]; b.inputs["Emission Strength"].default_value = 3.0       # preview only: see the hue
        mix = m_.node_tree.nodes.new("ShaderNodeMix"); mix.data_type = 'RGBA'; mix.blend_type = 'MULTIPLY'; mix.inputs[0].default_value = 1.0
        tex = [n for n in m_.node_tree.nodes if n.type == 'TEX_IMAGE'][0]
        m_.node_tree.links.new(tex.outputs[0], mix.inputs[6]); mix.inputs[7].default_value = (*col, 1)
        m_.node_tree.links.new(mix.outputs[2], b.inputs["Emission Color"])
    key = bpy.data.objects.new("key", bpy.data.lights.new("key", 'AREA')); key.data.energy = 60; key.location = (2, -2, 3); sc.collection.objects.link(key)
    key.rotation_euler = (math.radians(50), 0, math.radians(45))
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); sc.collection.objects.link(cam); sc.camera = cam
    tgt = Vector(ob.matrix_world.translation)
    for i, (az, el, dist) in enumerate(((200, 10, 2.4), (150, 5, 1.2), (250, 25, 6.0))):
        a, e = math.radians(az), math.radians(el)
        cam.location = tgt + Vector((dist * math.cos(e) * math.sin(a), -dist * math.cos(e) * math.cos(a), dist * math.sin(e) + 0.2))
        cam.rotation_euler = (tgt - cam.location).to_track_quat('-Z', 'Y').to_euler()
        sc.render.filepath = os.path.join(PREVIEW, "jet_%d.png" % i)
        bpy.ops.render.render(write_still=True)
