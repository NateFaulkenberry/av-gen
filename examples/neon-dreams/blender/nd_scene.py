"""Neon Dreams: assemble the whole design in Blender from the data the engine reads, and render concept frames.

  Blender -b --factory-startup -P nd_scene.py -- --jobs approach:uv,reveal:blue --res 1280 --spp 48 --out DIR [--quick]

It reads art/layout.json (every instance of the kit, the lights, the beams), art/cloud_design.json (the lobes, voxelised
here the way the engine unites them: max over lobes, eroded at the edges by noise), art/palette.json and the kit's GLBs.
Nothing is modelled in this file except the sky, the moon, the haze and the sea. Cycles; volumes need the GPU, so run
it under tools/gpu-lock.sh, one or two frames per job.

Cameras: named views below, or tNN.N = the journey camera at that time (it looks along its path).
"""
import bpy, sys, os, math, json, time, random
import numpy as np
from mathutils import Vector, Quaternion

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import nd_assemble as AS
from nd_lib import new_mat, node, math_, vmath, mixc, rgb, attr, vnoise, billow, link, MB
import nd_world as WD

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
arg = lambda n, d: argv[argv.index(n) + 1] if n in argv else d
ROOT = os.path.join(HERE, "..")
KIT = arg("--kit", os.path.join(ROOT, "meshes"))
OUT = arg("--out", os.path.join(HERE, "out")); os.makedirs(OUT, exist_ok=True)
CACHE = arg("--cache", os.path.join(OUT, "vdb")); os.makedirs(CACHE, exist_ok=True)
RES, SPP = int(arg("--res", 1280)), int(arg("--spp", 48))
JOBS = [j.split(":") for j in arg("--jobs", "approach:uv").split(",")]
QUICK = "--quick" in argv
ZONES_OFF = [z for z in arg("--skip-zones", "").split(",") if z]
T0 = time.time()
log = lambda *a: print("[nd %.1fs]" % (time.time() - T0), *a, flush=True)
LAY = json.load(open(arg("--layout", os.path.join(ROOT, "art", "layout.json"))))
CLD = json.load(open(arg("--clouds", os.path.join(ROOT, "art", "cloud_design.json"))))
PALFILE = arg("--palette", os.path.join(ROOT, "art", "palette.json"))
BINDS = AS.BINDS
S = dict(world=2.2, amb=7.0, moon=1.0)
AS.GAIN["neon_a"] = 0.75                               # sign faces a little under the GLB's strength: the nearest blade should not clip
scene = bpy.context.scene
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o)


def bind_sock(sock, key):
    BINDS.append((lambda c, s=sock: setattr(s, "default_value", (c[0], c[1], c[2], 1.0)), key))


def P(e):                                              # engine -> Blender
    return Vector((e[0], -e[2], e[1]))


# ---------------------------------------------------------------------------------------------- the kit, placed
log("placing", len(LAY["instances"]), "instances")
_cloth = {}
for i in LAY["instances"]:
    if i["zone"] in ZONES_OFF:
        continue
    o = AS.place(KIT, i["asset"], i["pos"], i["rot_y_deg"], i["scale"], i.get("tint"), i.get("gain"), name=i["id"])
    q = i["quat"]; o.rotation_mode = 'QUATERNION'; o.rotation_quaternion = Quaternion((q[3], q[0], -q[2], q[1]))
    if "color" in i:
        for k, slot in enumerate(o.material_slots):
            r = AS.role_of(slot.material) if slot.material else ""
            if r.split("@")[0] in i["color"]:
                col = tuple(i["color"][r.split("@")[0]]); key = (r, col)
                if key not in _cloth:
                    m = AS._BASE[r.split("@")[0]].copy(); AS._principled(m).inputs['Base Color'].default_value = (*col, 1); _cloth[key] = m
                slot.link = 'OBJECT'; slot.material = _cloth[key]
for l in LAY["lights"]:
    lamp = bpy.data.lights.new(l["name"], 'POINT'); lamp.shadow_soft_size = max(0.5, l["radius_m"] * 0.06)
    lamp.energy = l["intensity"] * 2500.0 * (l["radius_m"] / 9.0) ** 1.4 * (0.3 if l["radius_m"] < 20 else 1.0)      # street lamps are near geometry: keep them from burning it out
    BINDS.append((lambda c, la=lamp: setattr(la, "color", c), l["key"]))
    link(bpy.data.objects.new(l["name"], lamp)).location = P(l["pos"])
for g in CLD["clouds_block"]["glows"]:                # the cloud glows of the engine's block, as lamps inside the cloud
    lamp = bpy.data.lights.new("glow_" + g["name"], 'POINT'); lamp.energy = g["intensity"] * 1.5e4 * (g["radius"] / 120.0) ** 1.4; lamp.shadow_soft_size = g["radius"] * 0.05
    BINDS.append((lambda c, la=lamp: setattr(la, "color", c), g["key"]))
    link(bpy.data.objects.new("glow_" + g["name"], lamp)).location = P(g["position"])
if "--no-lights" in argv:
    for o in bpy.data.objects:
        if o.type == 'LIGHT':
            o.hide_render = True
log("kit placed; meshes", len([o for o in bpy.data.objects if o.type == 'MESH']))

# ---------------------------------------------------------------------------------------------- clouds from the lobes
import openvdb as vdb


def write_vdb(name, F, Wt, origin, voxel):
    path = os.path.join(CACHE, name + ".vdb"); grids = []
    for nm, arr in (("density", F), ("weight", Wt)):
        g = vdb.FloatGrid(); g.copyFromArray(np.ascontiguousarray(arr, dtype=np.float32)); g.name = nm
        g.transform = vdb.createLinearTransform(voxelSize=voxel); g.transform.postTranslate(tuple(float(v) for v in origin)) if hasattr(g.transform, "postTranslate") else g.transform.translate(tuple(float(v) for v in origin))
        grids.append(g)
    vdb.write(path, grids=grids)
    return path


def voxelize(name, lobes, voxel, pad=12.0):
    """Union of ellipsoid lobes as the engine takes it: field = max over lobes of (1 - d / r); weight = that lobe's density."""
    c = np.array([[l["c"][0], -l["c"][2], l["c"][1]] for l in lobes], dtype=np.float32)
    rad = np.array([[l["r"] * l["scale"][0], l["r"] * l["scale"][2], l["r"] * l["scale"][1]] for l in lobes], dtype=np.float32)
    lo = (c - rad).min(0) - pad; hi = (c + rad).max(0) + pad
    n = np.ceil((hi - lo) / voxel).astype(int)
    F = np.zeros(n, dtype=np.float32); Wt = np.zeros(n, dtype=np.float32)
    for l, cc, rr_ in zip(lobes, c, rad):
        i0 = np.maximum(((cc - rr_ - lo) / voxel).astype(int), 0); i1 = np.minimum(((cc + rr_ - lo) / voxel).astype(int) + 2, n)
        xs = lo[0] + (np.arange(i0[0], i1[0]) + 0.5) * voxel; ys = lo[1] + (np.arange(i0[1], i1[1]) + 0.5) * voxel; zs = lo[2] + (np.arange(i0[2], i1[2]) + 0.5) * voxel
        d = np.sqrt(((xs[:, None, None] - cc[0]) / rr_[0]) ** 2 + ((ys[None, :, None] - cc[1]) / rr_[1]) ** 2 + ((zs[None, None, :] - cc[2]) / rr_[2]) ** 2)
        f = np.clip(1.0 - d, 0, 1) / max(l["soft"], 0.05) * 0.35                    # soft: how deep the edge is; 0.35 is the reference softness
        sub = F[i0[0]:i1[0], i0[1]:i1[1], i0[2]:i1[2]]; subw = Wt[i0[0]:i1[0], i0[1]:i1[1], i0[2]:i1[2]]
        m = f > sub; sub[m] = np.minimum(f[m], 1.0); subw[m] = l["density"]
    log("voxelised", name, len(lobes), "lobes ->", tuple(n), "voxels of", voxel, "m")
    return write_vdb(name, F, Wt, lo + voxel / 2, voxel)


def mat_cloud(name, density, noise_scale=0.022, erode=0.8, emis_key=None, emis=0.0, detail=True):
    m, nt = new_mat(name)
    out = node(nt, 'ShaderNodeOutputMaterial'); pv = node(nt, 'ShaderNodeVolumePrincipled')
    nt.links.new(pv.outputs[0], out.inputs['Volume'])
    pv.inputs['Color'].default_value = (0.93, 0.94, 1.0, 1); pv.inputs['Anisotropy'].default_value = 0.55; pv.inputs['Density Attribute'].default_value = ""
    geo = node(nt, 'ShaderNodeNewGeometry')
    fld = node(nt, 'ShaderNodeAttribute', attribute_name="density").outputs['Fac']; wgt = node(nt, 'ShaderNodeAttribute', attribute_name="weight").outputs['Fac']
    nz = node(nt, 'ShaderNodeTexNoise'); nt.links.new(geo.outputs['Position'], nz.inputs['Vector'])
    nz.inputs['Scale'].default_value = noise_scale; nz.inputs['Detail'].default_value = 5.0 if detail else 2.0; nz.inputs['Roughness'].default_value = 0.6
    mixn = nz.outputs[0]
    if detail:                                              # a second, finer octave so near cloud is not smooth pillows
        nz2 = node(nt, 'ShaderNodeTexNoise'); nt.links.new(geo.outputs['Position'], nz2.inputs['Vector'])
        nz2.inputs['Scale'].default_value = noise_scale * 3.7; nz2.inputs['Detail'].default_value = 2.0
        mixn = math_(nt, 'ADD', math_(nt, 'MULTIPLY', nz.outputs[0], 0.68), math_(nt, 'MULTIPLY', nz2.outputs[0], 0.32))
    thr = math_(nt, 'MULTIPLY', math_(nt, 'MULTIPLY_ADD', mixn, 2.2, -0.62, clamp=True), erode * 0.42)
    edge = node(nt, 'ShaderNodeMapRange', interpolation_type='SMOOTHSTEP'); nt.links.new(fld, edge.inputs[0])
    nt.links.new(thr, edge.inputs[1]); nt.links.new(math_(nt, 'ADD', thr, 0.07), edge.inputs[2])
    d = math_(nt, 'MULTIPLY', math_(nt, 'MULTIPLY', edge.outputs[0], density), wgt)
    nt.links.new(d, pv.inputs['Density'])
    if emis_key:
        bind_sock(pv.inputs['Emission Color'], emis_key); nt.links.new(math_(nt, 'MULTIPLY', d, emis / density), pv.inputs['Emission Strength'])
    return m


def add_volume(name, path, mat):
    v = bpy.data.volumes.new(name); v.filepath = path
    ob = link(bpy.data.objects.new(name, v)); v.materials.append(mat)
    return ob


vx = 1.6 if QUICK else 1.0
M_CLOUD = mat_cloud("cloud_hero", 0.11, 0.028, erode=0.85)
M_ROCK = mat_cloud("cloud_rock", 0.2, 0.03, erode=0.55)
M_FAR = mat_cloud("cloud_far", 0.05, 0.006, erode=0.85, detail=False)
M_PLUME = mat_cloud("plume", 0.03, 0.02, erode=0.9, emis_key="cond_a", emis=0.004)
if "--no-clouds" not in argv:
    for nm, lobes in CLD["masses"].items():
        big = nm in ("mid_field", "condensery_column")
        vox = (7.0 if big else 3.6) * vx
        mat = M_PLUME if "plume" in nm else (M_ROCK if nm.startswith("rock") or nm == "lone_lamp" else M_CLOUD)
        add_volume("cloud_" + nm, voxelize(nm, lobes, vox), mat)
    add_volume("cloud_far", voxelize("far", CLD["far_masses"]["far_towers"], 30.0 * vx, pad=60.0), M_FAR)
    # the sea: a height band with swells and billows, to 3.5 km; beyond it a displaced matte floor carries on to the horizon
    SEA = CLD["sea_design"]; SC = (1500.0, 200.0); SV = 14.0 * vx; half = 3000.0
    def sea_h(X, Y):
        big = vnoise(X, Y, 2400, 3)
        return SEA["top_mean"] + 62 * billow(X, Y, 460, 1) * (0.35 + 0.9 * vnoise(X, Y, 1500, 5)) + 30 * billow(X, Y, 190, 2) + 11 * billow(X, Y, 75, 4) + 80 * (big - 0.5)
    nx = int(2 * half / SV); nzv = int((70 - (-190)) / SV)
    xs = SC[0] - half + (np.arange(nx) + 0.5) * SV; ys = SC[1] - half + (np.arange(nx) + 0.5) * SV; zs = -190 + (np.arange(nzv) + 0.5) * SV
    X, Y = np.meshgrid(xs, ys, indexing='ij'); Hh = sea_h(X, Y).astype(np.float32)
    bx, by = SEA["break"]["centre"][0], -SEA["break"]["centre"][1]; el = SEA["break"]["elongation"]; yw = -SEA["break"]["yaw_rad"]
    ax = ((X - bx) * math.cos(yw) + (Y - by) * math.sin(yw)) / el; ay = -(X - bx) * math.sin(yw) + (Y - by) * math.cos(yw)
    rr_ = np.sqrt(ax * ax + ay * ay) + 240.0 * (vnoise(X, Y, 330, 9) - 0.5)
    keep = np.clip((rr_ - SEA["break"]["radius"] * 0.72) / (SEA["break"]["radius"] * 0.42), 0, 1); keep = keep * keep * (3 - 2 * keep)
    F = np.clip((Hh[:, :, None] - zs[None, None, :]) / 60.0, 0, 1) * keep[:, :, None]
    add_volume("cloud_sea", write_vdb("sea", F.astype(np.float32), np.ones_like(F, dtype=np.float32), (xs[0], ys[0], zs[0]), SV), mat_cloud("cloud_sea", 0.045, 0.011, erode=0.7))
    log("sea done")
    # far floor: a polar grid, billows growing with distance
    rings, sect = 70, 220; verts, faces = [], []
    for i in range(rings + 1):
        r_ = 2700.0 * (60000.0 / 2700.0) ** (i / rings)
        for j in range(sect):
            a = j / sect * 2 * math.pi; verts.append((SC[0] + r_ * math.cos(a), SC[1] + r_ * math.sin(a), 0.0))
    V = np.array(verts, dtype=np.float32); rad_ = np.hypot(V[:, 0] - SC[0], V[:, 1] - SC[1]); k_ = np.clip(rad_ / 9000.0, 0.4, 3.0)
    V[:, 2] = SEA["top_mean"] - 14 + (70 * billow(V[:, 0], V[:, 1], 600, 21) + 150 * k_ * billow(V[:, 0], V[:, 1], 2100, 22) + 240 * k_ * vnoise(V[:, 0], V[:, 1], 5200, 23) - 80 * k_)
    for i in range(rings):
        for j in range(sect):
            a, b = i * sect + j, i * sect + (j + 1) % sect; faces.append((a, b, b + sect, a + sect))
    me = bpy.data.meshes.new("far_floor"); me.from_pydata(V.tolist(), [], faces); me.polygons.foreach_set("use_smooth", [True] * len(me.polygons)); me.update()
    ff = link(bpy.data.objects.new("far_floor", me))
    m_far, nt = new_mat("far_floor"); o_ = node(nt, 'ShaderNodeOutputMaterial'); b_ = node(nt, 'ShaderNodeBsdfPrincipled'); nt.links.new(b_.outputs[0], o_.inputs[0])
    b_.inputs['Roughness'].default_value = 1.0; b_.inputs['Base Color'].default_value = (0.8, 0.82, 0.95, 1)
    me.materials.append(m_far)
# the cosmic ocean under the layer, seen through the break
m_oc, nt = new_mat("ocean")
o_ = node(nt, 'ShaderNodeOutputMaterial'); e_ = node(nt, 'ShaderNodeEmission'); nt.links.new(e_.outputs[0], o_.inputs[0])
geo_ = node(nt, 'ShaderNodeNewGeometry')
n1 = node(nt, 'ShaderNodeTexNoise'); nt.links.new(geo_.outputs['Position'], n1.inputs['Vector']); n1.inputs['Scale'].default_value = 0.0011; n1.inputs['Detail'].default_value = 7.0
n2 = node(nt, 'ShaderNodeTexVoronoi'); nt.links.new(geo_.outputs['Position'], n2.inputs['Vector']); n2.inputs['Scale'].default_value = 0.02
ca_, cb_ = rgb(nt), rgb(nt); bind_sock(ca_.outputs[0], "ocean_a"); bind_sock(cb_.outputs[0], "ocean_b")
neb = math_(nt, 'POWER', math_(nt, 'MULTIPLY_ADD', n1.outputs[0], 2.2, -0.6, clamp=True), 2.0)
nt.links.new(mixc(nt, neb, ca_.outputs[0], cb_.outputs[0]), e_.inputs[0])
nt.links.new(math_(nt, 'ADD', math_(nt, 'MULTIPLY_ADD', neb, 1.1, 0.06), math_(nt, 'MULTIPLY', math_(nt, 'LESS_THAN', n2.outputs[0], 0.035), 40.0)), e_.inputs[1])
bpy.ops.mesh.primitive_plane_add(size=9000, location=(1650, 100, -2400)); bpy.context.object.data.materials.append(m_oc); bpy.context.object.name = "cosmic_ocean"
bl = bpy.data.lights.new("ocean_up", 'POINT'); bl.energy = 1.2e6; bl.shadow_soft_size = 60; BINDS.append((lambda c: setattr(bl, "color", c), "ocean_b"))
link(bpy.data.objects.new("ocean_up", bl)).location = (CLD["sea_design"]["break"]["centre"][0], -CLD["sea_design"]["break"]["centre"][1], -230)

# haze: ONE medium whose density falls with height (no slab, so no line where a slab would end), glowing in the haze colour
m_hz, nt = new_mat("haze")
o_ = node(nt, 'ShaderNodeOutputMaterial'); pv = node(nt, 'ShaderNodeVolumePrincipled'); nt.links.new(pv.outputs[0], o_.inputs['Volume'])
geo_ = node(nt, 'ShaderNodeNewGeometry'); sp_ = node(nt, 'ShaderNodeSeparateXYZ'); nt.links.new(geo_.outputs['Position'], sp_.inputs[0])
hgt = math_(nt, 'MAXIMUM', math_(nt, 'ADD', sp_.outputs[2], 60.0), 0.0)
sig = math_(nt, 'ADD', math_(nt, 'MULTIPLY', math_(nt, 'POWER', 2.71828, math_(nt, 'DIVIDE', hgt, -380.0)), 7.5e-5), 4.0e-6)
pv.inputs['Color'].default_value = (0.75, 0.78, 0.95, 1); pv.inputs['Anisotropy'].default_value = 0.6; pv.inputs['Density Attribute'].default_value = ""
nt.links.new(sig, pv.inputs['Density']); bind_sock(pv.inputs['Emission Color'], "haze"); nt.links.new(math_(nt, 'MULTIPLY', sig, 0.05), pv.inputs['Emission Strength'])
bpy.ops.mesh.primitive_cube_add(size=1, location=(3000, 1500, 2700)); hz = bpy.context.object; hz.name = "haze"; hz.scale = (150000, 150000, 6000); hz.data.materials.append(m_hz)
hz.hide_render = "--no-haze" in argv

# ---------------------------------------------------------------------------------------------- the Drowned Moon, the Long Arc, the sky
CAM1 = Vector((-600, -380, 150)); MOON = CLD["moon"]
mb_ = math.radians(MOON["disc_bearing_deg"]); MD = 60000.0
moon_c = Vector((CAM1.x + MD * math.cos(mb_), CAM1.y + MD * math.sin(mb_), 150 + MD * math.tan(math.radians(MOON["disc_centre_elevation_deg"]))))
MR = MD * math.tan(math.radians(MOON["disc_angular_radius_deg"]))
bpy.ops.mesh.primitive_uv_sphere_add(radius=MR, location=moon_c, segments=128, ring_count=64)
moon = bpy.context.object; moon.name = "drowned_moon"; bpy.ops.object.shade_smooth(); moon.visible_shadow = False
m_moon, nt = new_mat("moon")
o_ = node(nt, 'ShaderNodeOutputMaterial'); e_ = node(nt, 'ShaderNodeEmission'); nt.links.new(e_.outputs[0], o_.inputs[0])
tc = node(nt, 'ShaderNodeTexCoord'); geo_ = node(nt, 'ShaderNodeNewGeometry')
mp = node(nt, 'ShaderNodeMapping'); nt.links.new(tc.outputs['Object'], mp.inputs[0]); mp.inputs['Scale'].default_value = (1 / MR, 1 / MR, 1 / MR)
n1 = node(nt, 'ShaderNodeTexNoise'); nt.links.new(mp.outputs[0], n1.inputs['Vector']); n1.inputs['Scale'].default_value = 1.7; n1.inputs['Detail'].default_value = 9.0; n1.inputs['Roughness'].default_value = 0.6
n3 = node(nt, 'ShaderNodeTexNoise'); nt.links.new(mp.outputs[0], n3.inputs['Vector']); n3.inputs['Scale'].default_value = 9.0; n3.inputs['Detail'].default_value = 6.0
cr1 = node(nt, 'ShaderNodeTexVoronoi'); nt.links.new(mp.outputs[0], cr1.inputs['Vector']); cr1.inputs['Scale'].default_value = 5.5       # large craters
cr2 = node(nt, 'ShaderNodeTexVoronoi'); nt.links.new(mp.outputs[0], cr2.inputs['Vector']); cr2.inputs['Scale'].default_value = 21.0      # small craters
bowl = lambda v, w: math_(nt, 'MULTIPLY', math_(nt, 'SUBTRACT', 1.0, math_(nt, 'MINIMUM', math_(nt, 'DIVIDE', v.outputs['Distance'], w), 1.0)), 1.0)
height = math_(nt, 'ADD', math_(nt, 'ADD', math_(nt, 'MULTIPLY', n1.outputs[0], 1.0), math_(nt, 'MULTIPLY', n3.outputs[0], 0.25)),
               math_(nt, 'ADD', math_(nt, 'MULTIPLY', bowl(cr1, 0.32), -0.55), math_(nt, 'MULTIPLY', bowl(cr2, 0.3), -0.2)))
bump = node(nt, 'ShaderNodeBump'); nt.links.new(height, bump.inputs['Height']); bump.inputs['Strength'].default_value = 1.0; bump.inputs['Distance'].default_value = MR * 0.035
LDIR = Vector((-0.1005, 0.597, 0.796)).normalized()                                    # the sun that lights the moon: high on its upper left and a little behind it: a wide crescent, the terminator across the disc
ndl = vmath(nt, 'DOT_PRODUCT', bump.outputs['Normal'], tuple(LDIR), out='Value')        # relief shows along the terminator
ndl0 = vmath(nt, 'DOT_PRODUCT', geo_.outputs['Normal'], tuple(LDIR), out='Value')
lam = math_(nt, 'POWER', math_(nt, 'MAXIMUM', math_(nt, 'MULTIPLY_ADD', ndl, 0.75, math_(nt, 'MULTIPLY', ndl0, 0.25)), 0.0), 0.8)
maria = node(nt, 'ShaderNodeMapRange', interpolation_type='SMOOTHSTEP'); nt.links.new(n1.outputs[0], maria.inputs[0])
maria.inputs[1].default_value = 0.42; maria.inputs[2].default_value = 0.6; maria.inputs[3].default_value = 0.42; maria.inputs[4].default_value = 1.0
alb = math_(nt, 'MULTIPLY', maria.outputs[0], math_(nt, 'MULTIPLY_ADD', n3.outputs[0], 0.3, 0.85))
mc = rgb(nt); bind_sock(mc.outputs[0], "moon_disc"); hzc = rgb(nt); bind_sock(hzc.outputs[0], "haze")
lit = node(nt, 'ShaderNodeVectorMath', operation='SCALE'); nt.links.new(mc.outputs[0], lit.inputs[0]); nt.links.new(math_(nt, 'MULTIPLY', math_(nt, 'MULTIPLY', lam, alb), 0.5), lit.inputs[3])
dark = node(nt, 'ShaderNodeVectorMath', operation='SCALE'); nt.links.new(hzc.outputs[0], dark.inputs[0]); nt.links.new(math_(nt, 'MULTIPLY', alb, 1.1), dark.inputs[3])     # earthshine: the night side is not black
addc = node(nt, 'ShaderNodeVectorMath', operation='ADD'); nt.links.new(lit.outputs[0], addc.inputs[0]); nt.links.new(dark.outputs[0], addc.inputs[1])
nt.links.new(addc.outputs[0], e_.inputs[0]); e_.inputs[1].default_value = 1.0
moon.data.materials.append(m_moon)
AR, ARL = MB(), MB()
arc_c = Vector((CAM1.x + 26000 * math.cos(math.radians(47)), CAM1.y + 26000 * math.sin(math.radians(47)), -9000.0))
ax_u = Vector((math.cos(math.radians(-38)), math.sin(math.radians(-38)), 0)); RA = 13200.0; prev = None
for k in range(0, 71):
    a_ = math.radians(118 - k * 0.62)
    if 54 <= k <= 57 or k in (63, 67):
        prev = None; continue
    q = arc_c + ax_u * (RA * math.cos(a_)) + Vector((0, 0, 1)) * (RA * math.sin(a_))
    if prev is not None:
        AR.beam(prev, q, 110.0, t2=60.0)
        if k % 2 == 0:
            ARL.box(q + Vector((0, 0, 70)), (24, 24, 24))
    prev = q


def emit_mat(name, key, strength):
    m, nt = new_mat(name); o_ = node(nt, 'ShaderNodeOutputMaterial'); e_ = node(nt, 'ShaderNodeEmission'); nt.links.new(e_.outputs[0], o_.inputs[0])
    bind_sock(e_.inputs[0], key); e_.inputs[1].default_value = strength
    return m


AR.build("long_arc", emit_mat("arc", "win_c", 1.6)); ARL.build("long_arc_lights", emit_mat("arc_l", "win_a", 3.0))   # the line must carry it: lights alone read as UFOs (seen twice)

w = bpy.data.worlds.new("sky"); scene.world = w; w.use_nodes = True; nt = w.node_tree
for n in list(nt.nodes):
    nt.nodes.remove(n)
wo = node(nt, 'ShaderNodeOutputWorld'); bg = node(nt, 'ShaderNodeBackground'); nt.links.new(bg.outputs[0], wo.inputs[0])
tc = node(nt, 'ShaderNodeTexCoord'); dirn = vmath(nt, 'NORMALIZE', tc.outputs['Generated'])
sp = node(nt, 'ShaderNodeSeparateXYZ'); nt.links.new(dirn, sp.inputs[0])
zen, hor, glw, na, nb, hzc = rgb(nt), rgb(nt), rgb(nt), rgb(nt), rgb(nt), rgb(nt)
for n_, k_ in ((zen, "sky_zenith"), (hor, "sky_horizon"), (glw, "horizon_glow"), (na, "nebula_a"), (nb, "nebula_b"), (hzc, "haze")):
    bind_sock(n_.outputs[0], k_)
up = math_(nt, 'MAXIMUM', sp.outputs[2], 0.0)
grad = mixc(nt, math_(nt, 'POWER', math_(nt, 'SUBTRACT', 1.0, up), 5.0), zen.outputs[0], hor.outputs[0])
band = math_(nt, 'POWER', math_(nt, 'SUBTRACT', 1.0, math_(nt, 'MINIMUM', math_(nt, 'ABSOLUTE', math_(nt, 'ADD', sp.outputs[2], 0.01)), 1.0)), 22.0)   # a soft glow band, widest at the horizon
sky = node(nt, 'ShaderNodeMix', data_type='RGBA', blend_type='ADD'); sky.inputs[0].default_value = 1.0; nt.links.new(grad, sky.inputs[6])
gl2 = node(nt, 'ShaderNodeVectorMath', operation='SCALE'); nt.links.new(glw.outputs[0], gl2.inputs[0]); nt.links.new(math_(nt, 'MULTIPLY', band, 0.8), gl2.inputs[3]); nt.links.new(gl2.outputs[0], sky.inputs[7])
nn = node(nt, 'ShaderNodeTexNoise'); nt.links.new(dirn, nn.inputs['Vector']); nn.inputs['Scale'].default_value = 2.2; nn.inputs['Detail'].default_value = 8.0; nn.inputs['Roughness'].default_value = 0.62
nn2 = node(nt, 'ShaderNodeTexNoise'); nt.links.new(dirn, nn2.inputs['Vector']); nn2.inputs['Scale'].default_value = 5.0; nn2.inputs['Detail'].default_value = 5.0
NEBDIR = Vector((math.cos(math.radians(8)) * 0.9, math.sin(math.radians(8)) * 0.9, 0.42)).normalized()
loc_ = math_(nt, 'POWER', math_(nt, 'MAXIMUM', vmath(nt, 'DOT_PRODUCT', dirn, tuple(NEBDIR), out='Value'), 0.0), 16.0)
nmask = math_(nt, 'MULTIPLY', math_(nt, 'POWER', math_(nt, 'MULTIPLY_ADD', nn.outputs[0], 2.4, -0.75, clamp=True), 1.6), loc_)
nsc = node(nt, 'ShaderNodeVectorMath', operation='SCALE'); nt.links.new(mixc(nt, nn2.outputs[0], na.outputs[0], nb.outputs[0]), nsc.inputs[0]); nt.links.new(math_(nt, 'MULTIPLY', nmask, 0.5), nsc.inputs[3])
sky2 = node(nt, 'ShaderNodeMix', data_type='RGBA', blend_type='ADD'); sky2.inputs[0].default_value = 1.0; nt.links.new(sky.outputs[2], sky2.inputs[6]); nt.links.new(nsc.outputs[0], sky2.inputs[7])
vs = node(nt, 'ShaderNodeTexVoronoi'); nt.links.new(dirn, vs.inputs['Vector']); vs.inputs['Scale'].default_value = 190.0
vs2 = node(nt, 'ShaderNodeTexVoronoi'); nt.links.new(dirn, vs2.inputs['Vector']); vs2.inputs['Scale'].default_value = 60.0
vsc = node(nt, 'ShaderNodeSeparateColor'); nt.links.new(vs.outputs[1], vsc.inputs[0])
st = math_(nt, 'MULTIPLY', math_(nt, 'LESS_THAN', vs.outputs[0], 0.05), math_(nt, 'POWER', vsc.outputs[0], 6.0))
stv = math_(nt, 'MULTIPLY', math_(nt, 'ADD', math_(nt, 'MULTIPLY', st, 3.0), math_(nt, 'MULTIPLY', math_(nt, 'LESS_THAN', vs2.outputs[0], 0.022), 1.6)), math_(nt, 'MULTIPLY_ADD', up, 3.0, 0.05, clamp=True))
stc = node(nt, 'ShaderNodeCombineColor'); nt.links.new(stv, stc.inputs[0]); nt.links.new(stv, stc.inputs[1]); nt.links.new(math_(nt, 'MULTIPLY', stv, 1.25), stc.inputs[2])
sky3 = node(nt, 'ShaderNodeMix', data_type='RGBA', blend_type='ADD'); sky3.inputs[0].default_value = 1.0; nt.links.new(sky2.outputs[2], sky3.inputs[6]); nt.links.new(stc.outputs[0], sky3.inputs[7])
below = math_(nt, 'MULTIPLY_ADD', sp.outputs[2], 14.0, 1.0, clamp=True)                                                   # under the horizon the world is the haze colour: nothing to make an edge
sky4 = mixc(nt, below, hzc.outputs[0], sky3.outputs[2])
nt.links.new(sky4, bg.inputs[0])
lp = node(nt, 'ShaderNodeLightPath')
nt.links.new(math_(nt, 'MULTIPLY_ADD', lp.outputs['Is Camera Ray'], S['world'] - S['amb'], S['amb']), bg.inputs[1])
ml = bpy.data.lights.new("moonlight", 'SUN'); ml.energy = S['moon']; ml.angle = math.radians(3.0)
BINDS.append((lambda c: setattr(ml, "color", c), "moon_light"))
mo = link(bpy.data.objects.new("moonlight", ml))
a1, a2 = math.radians(MOON["light_from_bearing_deg"]), math.radians(MOON["light_elevation_deg"])
mo.rotation_euler = (-Vector((math.cos(a1) * math.cos(a2), math.sin(a1) * math.cos(a2), math.sin(a2)))).to_track_quat('-Z', 'Y').to_euler()

# ---------------------------------------------------------------------------------------------- lighthouse beams
m_beam, nt = new_mat("beam")
o_ = node(nt, 'ShaderNodeOutputMaterial'); em_ = node(nt, 'ShaderNodeEmission'); nt.links.new(em_.outputs[0], o_.inputs['Volume'])
tcb = node(nt, 'ShaderNodeTexCoord'); spb = node(nt, 'ShaderNodeSeparateXYZ'); nt.links.new(tcb.outputs['Object'], spb.inputs[0])
bxp = math_(nt, 'MAXIMUM', spb.outputs[0], 0.0)
fall = math_(nt, 'DIVIDE', 1.0, math_(nt, 'ADD', 1.0, math_(nt, 'POWER', math_(nt, 'DIVIDE', bxp, 220.0), 1.6)))
radn = math_(nt, 'DIVIDE', math_(nt, 'SQRT', math_(nt, 'ADD', math_(nt, 'MULTIPLY', spb.outputs[1], spb.outputs[1]), math_(nt, 'MULTIPLY', spb.outputs[2], spb.outputs[2]))),
             math_(nt, 'MULTIPLY_ADD', bxp, math.tan(math.radians(1.0)), 0.6))
core = math_(nt, 'POWER', math_(nt, 'SUBTRACT', 1.0, math_(nt, 'MINIMUM', radn, 1.0, clamp=True)), 1.5)
bind_sock(em_.inputs[0], "moon_disc"); nt.links.new(math_(nt, 'MULTIPLY', math_(nt, 'MULTIPLY', fall, core), 0.014), em_.inputs[1])
BEAMS = []
for b in LAY["beams"]:
    for k in range(b["count"]):
        L_ = b["length_m"]; rr_ = L_ * math.tan(math.radians(b["half_angle_deg"])) + 0.6; n = 12; verts = [(0, 0, 0)]
        verts += [(L_, rr_ * math.cos(i / n * 6.2832), rr_ * math.sin(i / n * 6.2832)) for i in range(n)]
        faces = [(0, 1 + (i + 1) % n, 1 + i) for i in range(n)] + [tuple(range(1, n + 1))]          # outward normals: Cycles decides inside/outside from them
        me = bpy.data.meshes.new("beam"); me.from_pydata(verts, [], faces); me.update(); me.materials.append(m_beam)
        ob = link(bpy.data.objects.new("beam_%s_%d" % (b["name"], k), me)); ob.location = P(b["origin"]); ob.visible_shadow = False
        sl = bpy.data.lights.new("beamspot", 'SPOT'); sl.energy = 2.0e7; sl.spot_size = math.radians(5.0); sl.spot_blend = 0.6; sl.shadow_soft_size = 1.0
        BINDS.append((lambda c, la=sl: setattr(la, "color", c), "moon_disc"))
        so = link(bpy.data.objects.new("beamspot_%s_%d" % (b["name"], k), sl)); so.location = P(b["origin"])
        BEAMS.append((ob, so, b, k))
        ob.hide_render = so.hide_render = "--no-beams" in argv


def aim_beams(t):
    for ob, so, b, k in BEAMS:
        brg = 2 * math.pi * (b["phase"] + b["rpm"] * t / 60.0) + math.pi * k; el = math.radians(b["elevation_deg"])
        d = Vector((math.cos(brg) * math.cos(el), math.sin(brg) * math.cos(el), math.sin(el)))
        ob.rotation_euler = d.to_track_quat('X', 'Z').to_euler(); so.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()


# ---------------------------------------------------------------------------------------------- cameras and render
VIEWS = {   # name: (position, heading deg (0 = +x, counter-clockwise), pitch deg, lens mm, journey time for the beams)
    "approach": ((-330, -230, 140), 40.0, 4.0, 30, 17.0),
    "reveal": ((600, 78, 305), 7.0, -7.0, 32, 62.0),
    "top": ((20, 20, 900), 90.0, -89.0, 50, 0.0),
    "aerial": ((-260, -330, 330), 52.0, -27.0, 35, 20.0),
}
EXPO = {"approach": 0.3, "reveal": 0.25, "top": 0.0, "aerial": 0.4}


def view(name):
    if name in VIEWS:
        return VIEWS[name]
    t = float(name[1:]) if name[0] == "t" else 0.0
    a, b = Vector(WD.cam_at(t - 0.2)), Vector(WD.cam_at(t + 0.2)); d = (b - a).normalized()
    return (tuple(WD.cam_at(t)), math.degrees(math.atan2(d.y, d.x)), math.degrees(math.asin(d.z)), float(arg("--lens", 24)), t)


cam = bpy.data.cameras.new("cam"); cam.sensor_width = 36; cam.clip_start = 0.3; cam.clip_end = 400000
camo = link(bpy.data.objects.new("cam", cam)); scene.camera = camo
scene.render.engine = 'CYCLES'
prefs = bpy.context.preferences.addons['cycles'].preferences
if "--cpu" in argv:
    scene.cycles.device = 'CPU'
else:
    prefs.compute_device_type = 'METAL'; prefs.get_devices()
    for d_ in prefs.devices:
        d_.use = (d_.type == 'METAL')
    scene.cycles.device = 'GPU'
cy = scene.cycles
cy.samples = SPP; cy.use_adaptive_sampling = True; cy.adaptive_threshold = 0.02; cy.use_denoising = True; cy.denoiser = 'OPENIMAGEDENOISE'
cy.max_bounces = 6; cy.diffuse_bounces = 2; cy.glossy_bounces = 3; cy.volume_bounces = int(arg("--vbounce", 2)); cy.transmission_bounces = 2
for m_ in bpy.data.materials:                          # the city's light on the cloud comes from the lamps of layout.json, not from sampling 100k emissive panes
    m_.cycles.emission_sampling = 'NONE' 
cy.sample_clamp_indirect = 6.0; cy.sample_clamp_direct = 0.0
scene.render.resolution_x = RES; scene.render.resolution_y = int(RES * 9 / 16); scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'OPEN_EXR'; scene.render.image_settings.color_depth = '32'
try:
    scene.view_settings.view_transform = 'AgX'; scene.view_settings.look = 'AgX - Medium High Contrast'
except Exception as e:
    log("view transform:", e)


def post(exr, png, exposure):
    """Soft glare (three small radii) on scene-linear data, then the view transform. Mild on purpose."""
    img = bpy.data.images.load(exr); w_, h_ = img.size
    px = np.empty(w_ * h_ * 4, dtype=np.float32); img.pixels.foreach_get(px); px = px.reshape(h_, w_, 4)
    rgb_ = px[..., :3].copy() * (2.0 ** exposure)
    lum = rgb_ @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    hi = np.minimum(rgb_ * np.clip((lum - 0.8) / 1.6, 0, 1)[..., None], 40.0)
    fy = np.fft.fftfreq(h_)[:, None]; fx = np.fft.rfftfreq(w_)[None, :]
    out = rgb_.copy()
    for sigma, gain in ((w_ * 0.004, 0.30), (w_ * 0.018, 0.16), (w_ * 0.06, 0.05)):
        g = np.exp(-2 * (math.pi ** 2) * (sigma ** 2) * (fx ** 2 + fy ** 2))
        for c_ in range(3):
            out[..., c_] += gain * np.fft.irfft2(np.fft.rfft2(hi[..., c_]) * g, s=(h_, w_))
    res = bpy.data.images.new("post", w_, h_, float_buffer=True, alpha=False)
    res.pixels.foreach_set(np.concatenate([np.maximum(out, 0), np.ones((h_, w_, 1), dtype=np.float32)], -1).astype(np.float32).ravel())
    scene.render.image_settings.file_format = 'PNG'; scene.render.image_settings.color_depth = '8'; scene.view_settings.exposure = 0.0
    res.save_render(png, scene=scene)
    scene.render.image_settings.file_format = 'OPEN_EXR'; scene.render.image_settings.color_depth = '32'
    bpy.data.images.remove(img); bpy.data.images.remove(res)


def place_cast(cast_dir, cam_pos, maxn, radius):
    """The inhabitants in a concept frame: the Glowmere cast (third-party GLBs, read from where they live, never copied)
    posed from their own clips and frozen as static meshes at the layout's spots and along its paths."""
    import glob
    files = sorted(glob.glob(os.path.join(cast_dir, "alien-*.glb")))
    if not files:
        log("no cast found in", cast_dir); return
    rnd = random.Random(5); inh = LAY["inhabitants"]; cand = []
    for sp in inh["spots"]:
        cand.append((sp["priority"], (P(sp["pos"]) - cam_pos).length, P(sp["pos"]), sp["facing_deg"], sp["clips"][0]))
    for pth in inh["paths"]:
        pts = [P(q) for q in pth["points"]]
        for w in range(pth.get("walkers", 1) + 1):
            i = rnd.randrange(len(pts) - 1); u = rnd.random(); a, b = pts[i], pts[i + 1]; pos = a.lerp(b, u); d = (b - a) * (1 if rnd.random() < 0.5 else -1)
            cand.append((pth["priority"], (pos - cam_pos).length, pos, math.degrees(math.atan2(d.y, d.x)), pth.get("clip", "Walking")))
    cand = sorted([c for c in cand if c[1] < radius], key=lambda c: (c[0], c[1]))[:maxn]
    for k, (prio, dist, pos, face, clip) in enumerate(cand):
        before = set(bpy.data.objects); acts_before = set(bpy.data.actions)
        bpy.ops.import_scene.gltf(filepath=files[k % len(files)])
        new = [o for o in bpy.data.objects if o not in before]
        arm = next((o for o in new if o.type == 'ARMATURE'), None)
        acts = [a_ for a_ in bpy.data.actions if a_ not in acts_before]
        act = next((a_ for a_ in acts if a_.name.split(".")[0].split("_Armature")[0] == clip or a_.name.startswith(clip)), None)
        if arm and act:
            if not arm.animation_data:
                arm.animation_data_create()
            arm.animation_data.action = act
            try:
                if hasattr(arm.animation_data, "action_slot") and len(act.slots):
                    arm.animation_data.action_slot = act.slots[0]
            except Exception as e:
                log("slot:", e)
            f0, f1 = act.frame_range; scene.frame_set(int(f0 + rnd.random() * max(1.0, f1 - f0)))
        dg = bpy.context.evaluated_depsgraph_get(); dg.update()
        rz = Quaternion((0, 0, 1), math.radians(face + 90.0)).to_matrix().to_4x4()
        for o in [o for o in new if o.type == 'MESH' and any(m_.type == 'ARMATURE' for m_ in o.modifiers)]:      # not the importer's bone-shape helper
            ev = o.evaluated_get(dg); me = bpy.data.meshes.new_from_object(ev)
            st = link(bpy.data.objects.new("cast_%02d" % k, me))
            st.matrix_world = __import__("mathutils").Matrix.Translation(pos) @ rz @ ev.matrix_world
        for o in new:
            bpy.data.objects.remove(o)
    log("cast placed:", len(cand))


if "--clearance" in argv:                              # the true distance from the journey to the kit's geometry (not to bounding boxes)
    from mathutils.bvhtree import BVHTree
    trees = {}; worst = []
    insts = [o for o in bpy.data.objects if o.type == 'MESH' and not o.hide_render and o.data.users > 0 and o.name not in ("far_floor", "cosmic_ocean", "haze", "drowned_moon", "long_arc", "long_arc_lights") and not o.name.startswith("beam")]
    bpy.context.view_layer.update()
    for o in insts:
        if o.data.name not in trees:
            trees[o.data.name] = BVHTree.FromPolygons([v.co.copy() for v in o.data.vertices], [tuple(p.vertices) for p in o.data.polygons])
    inv = [(o, o.matrix_world.inverted(), max(o.scale)) for o in insts]
    best = (1e9, None, 0.0)
    for ti in range(0, 703):
        t = ti / 10.0; c = Vector(WD.cam_at(t)); near = (1e9, None)
        for o, mi, sc in inv:
            if (o.matrix_world.translation - c).length > 140.0:
                continue
            hit = trees[o.data.name].find_nearest(mi @ c, 30.0)
            if hit[0] is not None and hit[3] * sc < near[0]:
                near = (hit[3] * sc, o.name)
        if near[0] < best[0]:
            best = (near[0], near[1], t)
        if near[0] < 2.5:
            worst.append((t, round(near[0], 2), near[1]))
    log("CLEARANCE min %.2f m to %s at t=%.1f" % best); log("CLEARANCE under 2.5 m:", worst[:40])
    sys.exit(0)
if arg("--blend", ""):
    AS.apply_palette(AS.load_palette(PALFILE, "uv")); bpy.ops.wm.save_as_mainfile(filepath=arg("--blend", ""))
for camn, paln in JOBS:
    pos, hd, pt, lens, tj = view(camn)
    cam.lens = lens; camo.location = pos
    dv = Vector((math.cos(math.radians(pt)) * math.cos(math.radians(hd)), math.cos(math.radians(pt)) * math.sin(math.radians(hd)), math.sin(math.radians(pt))))
    camo.rotation_euler = dv.to_track_quat('-Z', 'Y').to_euler()
    aim_beams(tj)
    if arg("--cast", "") and not any(o.name.startswith("cast_") for o in bpy.data.objects):
        place_cast(arg("--cast", ""), Vector(pos), int(arg("--cast-n", 28)), float(arg("--cast-radius", 110)))
    AS.apply_palette(AS.load_palette(PALFILE, paln))
    scene.view_settings.exposure = 0.0
    exr = os.path.join(OUT, "%s_%s.exr" % (camn, paln)); scene.render.filepath = exr
    t = time.time(); bpy.ops.render.render(write_still=True)
    log("rendered", camn, paln, "in %.1fs" % (time.time() - t))
    _im = bpy.data.images.load(exr); _px = np.empty(_im.size[0] * _im.size[1] * 4, dtype=np.float32); _im.pixels.foreach_get(_px); _px = _px.reshape(-1, 4)[:, :3]
    log("exr stats: median", [round(float(v), 4) for v in np.median(_px, 0)], "mean", [round(float(v), 4) for v in _px.mean(0)], "p99", [round(float(v), 3) for v in np.percentile(_px, 99, 0)]); bpy.data.images.remove(_im)
    post(exr, os.path.join(OUT, "%s_%s.png" % (camn, paln)), float(arg("--ev", EXPO.get(camn, -0.3 if 29 < tj < 46 else 0.3))))
log("done")
