"""Cursed Slammers: turn the downloaded face scan (H1, jtuhtan's "Gorilla Face", CC BY 4.0; see
assets/cursed-slammers.manifest.json) into the engine-ready head. The input and the output are both gitignored:
this script is ours, its products are derived from a third-party download and never enter git.

    /Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup -P tools/cursed_slammers/prep_face.py -- \
        --src assets/cursed-slammers/reference/gorilla-face --out assets/cursed-slammers/cs-face.glb \
        [--tris 80000] [--tex 4096] [--bake 4096] [--rot 0,0,0] [--previews <dir>]

The recipe (03-plan.md, 04-study.md):
1. Import every mesh under --src (.glb/.gltf/.obj/.fbx), join, apply transforms. The joined scan is HIGH.
2. Orient: the area-weighted mean normal of a front mask points at the viewer; rotate it onto Blender -Y (glTF +Z),
   keeping the scan's own up as close to +Z as possible. `--rot x,y,z` (degrees, applied after) corrects a scan whose
   up is wrong; check the preview.
3. Normalise: centre the bounding box on the origin, scale so the width (x) is 1.0.
4. LOW = HIGH duplicated and decimated (collapse, seams and UVs kept) to --tris. Photogrammetry atlases do not overlap,
   so LOW keeps HIGH's UV layout and reuses its texture.
5. Bake HIGH's detail into LOW's UVs as a tangent-space normal map (Cycles, selected-to-active, a small cage
   extrusion, 16 px margin), --bake square.
6. The colour: HIGH's base-colour image scaled to --tex (8192 -> 4096 by default) and saved as PNG.
7. LOW gets one Principled material (colour + normal map), exported as GLB with the images embedded.

Writes --out, and <out>.json beside it (statistics, the orientation applied, the bounding box).
"""
import argparse
import json
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ap = argparse.ArgumentParser()
ap.add_argument("--src", required=True)
ap.add_argument("--out", required=True)
ap.add_argument("--tris", type=int, default=80000)
ap.add_argument("--tex", type=int, default=4096)
ap.add_argument("--bake", type=int, default=4096)
ap.add_argument("--rot", default="0,0,0")
ap.add_argument("--extrusion", type=float, default=0.006, help="bake cage extrusion, in face widths")
ap.add_argument("--previews", default="")
args = ap.parse_args(argv)
SRC = os.path.abspath(os.path.expanduser(args.src))
OUT = os.path.abspath(os.path.expanduser(args.out))
os.makedirs(os.path.dirname(OUT), exist_ok=True)
WORK = os.path.splitext(OUT)[0] + "_work"
os.makedirs(WORK, exist_ok=True)


def log(*a):
    print("[prep_face]", *a, flush=True)


# ---------------------------------------------------------------- 1. import
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete()
files = []
if os.path.isdir(SRC):
    for root, _, names in os.walk(SRC):
        for n in names:
            if n.lower().endswith((".glb", ".gltf", ".obj", ".fbx")):
                files.append(os.path.join(root, n))
else:
    files = [SRC]
if not files:
    sys.exit("prep_face: no .glb/.gltf/.obj/.fbx under " + SRC)
for f in sorted(files):
    ext = f.lower().rsplit(".", 1)[1]
    if ext in ("glb", "gltf"):
        bpy.ops.import_scene.gltf(filepath=f)
    elif ext == "obj":
        bpy.ops.wm.obj_import(filepath=f)
    else:
        bpy.ops.import_scene.fbx(filepath=f)
    log("imported", f)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
if not meshes:
    sys.exit("prep_face: the source has no mesh")
bpy.ops.object.select_all(action="DESELECT")
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1:
    bpy.ops.object.join()
high = bpy.context.view_layer.objects.active
high.name = "HIGH"
# unparent (glTF imports often sit under an empty) keeping the world transform, then apply it
bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
for o in list(bpy.context.scene.objects):
    if o.type != "MESH":
        bpy.data.objects.remove(o)
log("HIGH", len(high.data.polygons), "faces")

# the base-colour image the scan carries (the first image texture feeding a Base Color)
src_img = None
for slot in high.material_slots:
    m = slot.material
    if not (m and m.use_nodes):
        continue
    for n in m.node_tree.nodes:
        if n.type == "TEX_IMAGE" and n.image is not None:
            src_img = n.image
            break
    if src_img:
        break
log("source image", src_img.name if src_img else None, tuple(src_img.size) if src_img else None)

# ---------------------------------------------------------------- 2. orient
me = high.data
acc = Vector((0, 0, 0))
for p in me.polygons:
    acc += p.normal * p.area
front = acc.normalized() if acc.length > 0 else Vector((0, -1, 0))
target = Vector((0, -1, 0))
q = front.rotation_difference(target)
R = q.to_matrix().to_4x4()
# keep the scan's up: after facing, roll about Y so that the old +Z (or the most upward axis) stays up
up_after = (R @ Vector((0, 0, 1, 0))).to_3d()
up_proj = Vector((up_after.x, 0.0, up_after.z))
if up_proj.length > 1e-3:
    roll = math.atan2(up_proj.x, up_proj.z)
    R = Matrix.Rotation(roll, 4, "Y") @ R
rx, ry, rz = (math.radians(float(v)) for v in args.rot.split(","))
R = Matrix.Rotation(rz, 4, "Z") @ Matrix.Rotation(ry, 4, "Y") @ Matrix.Rotation(rx, 4, "X") @ R
me.transform(R)
log("front normal", tuple(round(c, 3) for c in front), "-> -Y; extra rot", args.rot)

# ---------------------------------------------------------------- 3. normalise
xs = [v.co.x for v in me.vertices]
ys = [v.co.y for v in me.vertices]
zs = [v.co.z for v in me.vertices]
c = Vector(((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, (min(zs) + max(zs)) / 2))
s = 1.0 / max(1e-6, max(xs) - min(xs))
me.transform(Matrix.Scale(s, 4) @ Matrix.Translation(-c))
me.update()
bb = [[(min(xs) - c.x) * s, (min(ys) - c.y) * s, (min(zs) - c.z) * s],
      [(max(xs) - c.x) * s, (max(ys) - c.y) * s, (max(zs) - c.z) * s]]
log("normalised to width 1.0; bbox", bb)

# ---------------------------------------------------------------- 4. decimate
bpy.ops.object.select_all(action="DESELECT")
high.select_set(True)
bpy.context.view_layer.objects.active = high
bpy.ops.object.duplicate()
low = bpy.context.view_layer.objects.active
low.name = "LOW"
tris_high = sum(len(p.vertices) - 2 for p in high.data.polygons)
ratio = min(1.0, args.tris / max(1, tris_high))
d = low.modifiers.new("dec", "DECIMATE")
d.decimate_type = "COLLAPSE"
d.ratio = ratio
d.use_collapse_triangulate = True
d.delimit = {"SEAM", "UV"}
bpy.ops.object.modifier_apply(modifier="dec")
bpy.ops.object.shade_smooth()
tris_low = sum(len(p.vertices) - 2 for p in low.data.polygons)
log("LOW", tris_low, "tris (ratio %.4f)" % ratio)
if not low.data.uv_layers:
    log("no UVs on the scan: smart-projecting LOW (the colour cannot be reused; baking colour too)")
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(island_margin=0.003)
    bpy.ops.object.mode_set(mode="OBJECT")

# ---------------------------------------------------------------- 5. bake the normal map
scene = bpy.context.scene
scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = 1
nrm = bpy.data.images.new("cs_face_normal", args.bake, args.bake, alpha=False, float_buffer=False)
nrm.colorspace_settings.name = "Non-Color"
mat = bpy.data.materials.new("cs_face")
mat.use_nodes = True
nt = mat.node_tree
bsdf = nt.nodes["Principled BSDF"]
bsdf.inputs["Roughness"].default_value = 0.55
tex_n = nt.nodes.new("ShaderNodeTexImage")
tex_n.image = nrm
tex_n.interpolation = "Linear"
low.data.materials.clear()
low.data.materials.append(mat)
nt.nodes.active = tex_n                       # the bake target
bpy.ops.object.select_all(action="DESELECT")
high.select_set(True)
low.select_set(True)
bpy.context.view_layer.objects.active = low
bake = scene.render.bake
bake.use_selected_to_active = True
bake.cage_extrusion = args.extrusion
bake.max_ray_distance = args.extrusion * 4.0
bake.margin = 16
bake.normal_space = "TANGENT"
bpy.ops.object.bake(type="NORMAL")
nrm_path = os.path.join(WORK, "cs_face_normal.png")
nrm.filepath_raw = nrm_path
nrm.file_format = "PNG"
nrm.save()
log("baked", nrm_path)

# ---------------------------------------------------------------- 6. the colour
col_path = None
if src_img is not None and low.data.uv_layers:
    col = src_img.copy()
    col.name = "cs_face_color"
    if max(col.size) > args.tex:
        col.scale(args.tex, args.tex)
    col_path = os.path.join(WORK, "cs_face_color.png")
    col.filepath_raw = col_path
    col.file_format = "PNG"
    col.save()
    tex_c = nt.nodes.new("ShaderNodeTexImage")
    tex_c.image = col
    nt.links.new(tex_c.outputs["Color"], bsdf.inputs["Base Color"])
    log("colour", col_path, tuple(col.size))
else:
    bsdf.inputs["Base Color"].default_value = (0.04, 0.034, 0.03, 1.0)

# ---------------------------------------------------------------- 7. material and export
nmap = nt.nodes.new("ShaderNodeNormalMap")
nt.links.new(tex_n.outputs["Color"], nmap.inputs["Color"])
nt.links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])
bpy.data.objects.remove(high)
bpy.ops.object.select_all(action="DESELECT")
low.select_set(True)
bpy.context.view_layer.objects.active = low
bpy.ops.export_scene.gltf(filepath=OUT, export_format="GLB", use_selection=True, export_yup=True,
                          export_apply=True, export_normals=True, export_texcoords=True,
                          export_materials="EXPORT", export_image_format="AUTO")
stats = {"source": SRC, "files": files, "trisHigh": tris_high, "trisLow": tris_low, "frontNormal": list(front),
         "extraRotationDeg": args.rot, "bboxBlender": bb, "normalMap": nrm_path, "color": col_path,
         "bakeSize": args.bake, "texSize": args.tex, "frame": "glTF +Y up, face toward +Z, width 1.0"}
with open(os.path.splitext(OUT)[0] + ".json", "w") as f:
    json.dump(stats, f, indent=1)
log("wrote", OUT)

# ---------------------------------------------------------------- previews (optional): front and three-quarter
if args.previews:
    prev = os.path.expanduser(args.previews)
    os.makedirs(prev, exist_ok=True)
    scene.render.engine = "BLENDER_EEVEE_NEXT" if "BLENDER_EEVEE_NEXT" in {e.identifier for e in
                                                                          bpy.types.RenderSettings.bl_rna.properties[
                                                                              "engine"].enum_items} else "CYCLES"
    scene.cycles.samples = 32
    scene.render.resolution_x, scene.render.resolution_y = 1280, 960
    bpy.ops.object.light_add(type="AREA", location=(-1.2, -1.6, 1.4))
    key = bpy.context.active_object
    key.data.energy = 120.0
    key.rotation_euler = (math.radians(55), 0, math.radians(-35))
    bpy.ops.object.camera_add()
    cam = bpy.context.active_object
    scene.camera = cam
    cam.data.lens = 70
    for name, az in (("front", 0.0), ("three-quarter", 30.0)):
        a = math.radians(az)
        cam.location = (2.6 * math.sin(a), -2.6 * math.cos(a), 0.05)
        cam.rotation_euler = (math.radians(90), 0, a)
        scene.render.filepath = os.path.join(prev, "cs-face-%s.png" % name)
        bpy.ops.render.render(write_still=True)
        log("preview", scene.render.filepath)
