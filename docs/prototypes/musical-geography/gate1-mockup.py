# Musical Geography -- Gate 1 look mock-up (Blender, headless, Eevee).
# Not engine code: a cheap proof of the art direction in 04-art-direction.md.
# usage: Blender -b --factory-startup --python gate1-mockup.py -- <shot> <out.png> [scale] [samples]   (round 2: slabs, rolling hills)
# Throwaway: the formation rules and palettes of 04-art-direction.md, for engineering to read, not to port line by line.
import bpy, sys, math
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]
SHOT = argv[0]
OUT = argv[1]
RES_SCALE = float(argv[2]) if len(argv) > 2 else 1.0
CONTOURS = argv[4] if len(argv) > 4 else "on"

# ---------------------------------------------------------------- palettes
def hexlin(h):
    h = h.lstrip('#')
    c = np.array([int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)])
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)

PAL = {
  "day": dict(
    drift_amt=0.14,
    drift="#8E9CF2",
    sky_zen="#3C78FF", sky_hor="#D6E2FF", fog="#C8D6FA", fog_sun="#FFF2D6",
    sun="#FFF4E0", sun_str=4.6, sun_elev=42, sun_az=235, amb="#A6B6FF", amb_str=0.6,
    water=["#0B36D0", "#1C58F2", "#3A86FF", "#8CC6FF"], shallow="#2EC2D2",
    land=["#8A6CF2", "#C26AE4", "#FF7A8C", "#FFA64E", "#FFD84A"], under="#1A1E66",
    wall="#EE987C", strata=0.08, river="#3A62D6",
    edge="#FFF4C8", edge_str=0.0, crest_str=0.0,
    path="#FFF5E2", path_emit=0.0, beacon="#FF8FB4", beacon_str=0.0, river_emit=0.0,
    fog_k=1.7, exposure=-0.1, sat=1.3, look="AgX - Punchy"),
  "sunset": dict(
    drift_amt=0.28,
    drift="#6A3AA0",
    sky_zen="#33206E", sky_hor="#FF8A50", fog="#E07462", fog_sun="#FFB060",
    sun="#FF9A4A", sun_str=5.0, sun_elev=9, sun_az=262, amb="#5E46B8", amb_str=0.9,
    water=["#24164F", "#4A2380", "#8A2F8C", "#E2507A"], shallow="#9A3A9A",
    land=["#4E2A7E", "#8A2C78", "#D0405A", "#F2785A", "#FFB45A"], under="#1E0E2E",
    wall="#C8445E", strata=0.12, river="#5A2A8A",
    edge="#FFC27A", edge_str=2.0, crest_str=0.6,
    path="#FFD7A0", path_emit=0.6, beacon="#FFB070", beacon_str=0.0, river_emit=0.3,
    fog_k=1.35, exposure=-0.2),
  "night": dict(
    drift_amt=0.28,
    drift="#5A2CB4",
    sky_zen="#080A2C", sky_hor="#33237A", fog="#2C1F66", fog_sun="#41308E",
    sun="#C8B4FF", sun_str=1.2, sun_elev=34, sun_az=200, amb="#5A38B0", amb_str=0.6,
    water=["#0E1452", "#18227A", "#2443B0", "#3F78FF"], shallow="#2A3AA0",
    land=["#221A6C", "#36228E", "#52289E", "#7430B0", "#9C44C8"], under="#06061E",
    wall="#3A2C92", strata=0.12, river="#2E6BFF",
    edge="#5A86FF", edge_str=7.0, crest_str=5.0,
    path="#FF5FB0", path_emit=5.0, beacon="#FF4FA8", beacon_str=6.0, river_emit=0.45,
    fog_k=0.85, exposure=0.6),
  "dawn": dict(
    drift_amt=0.28,
    drift="#F4C49A",
    sky_zen="#A6BCDC", sky_hor="#FFD9A4", fog="#F2DDB9", fog_sun="#FFE2A0",
    sun="#FFC874", sun_str=4.4, sun_elev=13, sun_az=80, amb="#B8C8D8", amb_str=0.55,
    water=["#3E5E98", "#5A7EB8", "#86A6D0", "#F2DCA8"], shallow="#8CBFA8",
    land=["#6FB57E", "#9CCC84", "#DCE08E", "#F4D68C", "#FBE9C4"], under="#2A3A3A",
    wall="#E8A44A", strata=0.08, river="#7393BD",
    edge="#FFD27A", edge_str=0.5, crest_str=0.4,
    path="#F2A93B", path_emit=0.25, beacon="#FFC060", beacon_str=0.0, river_emit=0.0,
    fog_k=1.35, exposure=-0.2),
}

def lerp_pal(a, b, t):
    out = {}
    for k, va in a.items():
        vb = b.get(k, va)
        if isinstance(va, str) and not va.startswith("#"):
            out[k] = va if t < 0.5 else vb
        elif isinstance(va, str):
            out[k] = (1 - t) * hexlin(va) + t * hexlin(vb)
        elif isinstance(va, list):
            out[k] = [(1 - t) * hexlin(x) + t * hexlin(y) for x, y in zip(va, vb)]
        else:
            out[k] = (1 - t) * va + t * vb
    return out

def as_lin(p):
    return lerp_pal(p, p, 0.0)

# ---------------------------------------------------------------- shots
# x east, y north, z up; one unit = one tile pitch.
SHOTS = {
  "01-day-low-pass-water":       dict(pal="day", cam=(-25, -165, 4.2), tgt=(10, 120, -14), f=24, R=620, t=3.0),
  "02-day-high-reveal-hills":    dict(pal="day", cam=(60, 40, 85), tgt=(-120, 260, 0), f=35, R=750, t=3.0),
  "03-sunset-contours-on":       dict(pal="sunset", cam=(-40, 60, 34), tgt=(30, 230, 8), f=28, R=650, t=5.0, bank=-3.0),
  "04-night-contours-on":        dict(pal="night", cam=(95, -10, 62), tgt=(-5, 175, 6), f=35, R=750, t=7.0),
  "05-day-aliens-scale":         dict(pal="day", cam=(-89.5, 141.0, 2.3), tgt=(-99.0, 163.0, 0.2), ground=True, f=40, R=420, t=3.0,
                                      figs=[(None, 150, 0.0, "Running", 7, 160, "alien-scout.glb"),
                                            (None, 152, 0.6, "Jump_running", 12, 175, "alien-pilot.glb")]),
  "06-day-top-down":             dict(pal="day", cam=(-15, -25, 170), topdown=True, tgt=None, f=35, R=220, fogR=1100, t=3.0),
  "07-dusk-wavefront":           dict(pal=("sunset", "night", 0.55), cam=(40, -150, 7), tgt=(-30, 40, 1), f=24, R=620, t=9.0),
}
if "contours-off" in SHOT:
    CONTOURS = "off"
SHOT = SHOT.replace("contours-off", "contours-on")
S = SHOTS[SHOT]
if isinstance(S["pal"], tuple):
    P = lerp_pal(PAL[S["pal"][0]], PAL[S["pal"][1]], S["pal"][2])
else:
    P = as_lin(PAL[S["pal"]])
T = S["t"]

# ---------------------------------------------------------------- terrain
def hash2(ix, iy, seed=0):
    h = (ix * 374761393 + iy * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFFFF) / float(0xFFFFFF)

def vnoise(x, y, seed=0):
    ix = np.floor(x).astype(np.int64); iy = np.floor(y).astype(np.int64)
    fx = x - ix; fy = y - iy
    ux = fx * fx * (3 - 2 * fx); uy = fy * fy * (3 - 2 * fy)
    a = hash2(ix, iy, seed); b = hash2(ix + 1, iy, seed)
    c = hash2(ix, iy + 1, seed); d = hash2(ix + 1, iy + 1, seed)
    return (a * (1 - ux) + b * ux) * (1 - uy) + (c * (1 - ux) + d * ux) * uy

def fbm(x, y, oct=4, seed=0):
    s = 0; a = 0.5; n = 0
    for o in range(oct):
        s = s + a * vnoise(x, y, seed + o * 17); n += a
        x, y = x * 2.03 + 0.31 * y, y * 2.03 - 0.31 * x
        a *= 0.5
    return s / n

def sstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)

def shore_y(x):
    return -38 + 34 * np.sin(x / 95.0 + 0.4) + 16 * np.sin(x / 41.0 + 1.7) + 9 * np.sin(x / 17.0 + 0.2)

def river_x(y):
    return 12 + 34 * np.sin(y / 62.0 + 0.3) + 13 * np.sin(y / 27.0 + 2.1)

def path_x(y):
    return -95 + 38 * np.sin(y / 74.0 + 0.9) + 9 * np.sin(y / 31.0)

def terrain(x, y, t):
    """rolling-hill world: height, region id, extra fields. No walls, no cliffs."""
    dsh = y - shore_y(x)
    land = sstep(-2, 22, dsh)
    wx = x + 50 * (fbm(x / 240, y / 240, 2, 5) - 0.5)
    wy = y + 50 * (fbm(x / 240 + 7, y / 240, 2, 6) - 0.5)
    rise = sstep(-10, 240, dsh)                       # the land swells as it goes inland
    big = sstep(0.22, 0.82, fbm(wx / 260, wy / 260, 3, 1))
    mid = fbm(wx / 85, wy / 85, 3, 9) - 0.5
    hills = 1.5 + 30.0 * rise * big + 9.0 * mid * (0.35 + 0.65 * rise)
    # coastal flats: shallow steps no taller than a slab, every third flooded
    coast_h = np.minimum(dsh, 80) * 0.05 + 0.8 * (fbm(x / 30, y / 30, 2, 3) - 0.5)
    tidx = np.floor(np.maximum(coast_h, 0) / 0.32)
    terr = tidx * 0.32
    cw = fbm(x / 70, y / 70, 1, 2)
    coastw = 1 - sstep(15 + 25 * cw, 60 + 30 * cw, dsh)
    h = hills * (1 - coastw) + np.minimum(terr, hills) * coastw
    region = np.where(coastw > 0.5, 1, 2)
    # river valley: a smooth, wide U through the hills (no walls)
    dc = np.abs(x - river_x(y))
    vfloor = 0.1
    v = sstep(3.5, 48.0, dc)
    h = np.where(dsh > -4, vfloor + (h - vfloor) * v, h)
    river = (dc < 3.5) & (dsh > -4)
    # path: a raised causeway following its own meander up the hills
    dp = np.abs(x - path_x(y))
    onpath = (dp < 1.0) & (dsh > 6) & (y < 330) & (dc > 6)
    h = np.where(onpath, h + 0.3, h)
    region = np.where(onpath, 6, region)
    # water: two crossing wave trains + a long swell
    k1 = 2 * np.pi / 34.0; d1 = np.array([0.82, 0.57])
    k2 = 2 * np.pi / 21.0; d2 = np.array([-0.45, 0.89])
    k3 = 2 * np.pi / 95.0; d3 = np.array([0.2, 0.98])
    w1 = np.sin(k1 * (x * d1[0] + y * d1[1]) - 1.1 * t)
    w2 = np.sin(k2 * (x * d2[0] + y * d2[1]) - 1.6 * t + 1.0)
    w3 = np.sin(k3 * (x * d3[0] + y * d3[1]) - 0.5 * t)
    wv = 0.42 * w1 + 0.26 * w2 + 0.55 * w3
    wv += 0.08 * (fbm(x / 9 + 0.3 * t, y / 9, 1, 31) - 0.5)
    water_h = -0.4 + wv
    crest = sstep(0.955, 0.998, w1) * (0.35 + 0.65 * sstep(-0.3, 0.9, w2))
    h = np.where(land > 0.5, h * land + (1 - land) * water_h, water_h)
    region = np.where(land > 0.5, region, 0)
    h = np.where(river, -0.3 + 0.18 * w2, h)
    region = np.where(river, 5, region)
    return h, region, dict(crest=crest * (region == 0), wv=wv, dsh=dsh, river=river, tidx=tidx)

# ---------------------------------------------------------------- camera
def look_rot(cam, tgt):
    d = np.array(tgt, float) - np.array(cam, float)
    d /= np.linalg.norm(d)
    yaw = math.atan2(d[1], d[0]); pitch = math.asin(d[2])
    return (math.pi / 2 + pitch, 0.0, yaw - math.pi / 2)

cam, tgt = S["cam"], S["tgt"]
if S.get("ground"):
    gz = lambda p_: float(terrain(np.array([p_[0]]), np.array([p_[1]]), T)[0][0])
    cam = (cam[0], cam[1], cam[2] + gz(cam)); tgt = (tgt[0], tgt[1], tgt[2] + gz(tgt))
S["figs"] = [(int(round(float(path_x(np.array(f_[1]))))) if f_[0] is None else f_[0],) + tuple(f_[1:]) for f_ in S.get("figs", [])]
if S.get("topdown"):
    tgt = (cam[0], cam[1] + 0.02, cam[2] - 100)

# ---------------------------------------------------------------- tile set (frustum-culled)
R = S["R"]
cx, cy = cam[0], cam[1]
xs = np.arange(math.floor(cx - R), math.ceil(cx + R) + 1)
ys = np.arange(math.floor(cy - R), math.ceil(cy + R) + 1)
GX, GY = np.meshgrid(xs.astype(float), ys.astype(float))
GX = GX.ravel(); GY = GY.ravel()
fwd = np.array(tgt, float) - np.array(cam, float); fwd /= np.linalg.norm(fwd)
f2 = fwd[:2] / max(np.linalg.norm(fwd[:2]), 1e-6)
rel = np.stack([GX - cx, GY - cy], 1)
dist = np.linalg.norm(rel, axis=1)
hfov = math.atan(18.0 / S["f"])
along = rel @ f2
side = np.abs(rel @ np.array([-f2[1], f2[0]]))
keep = (dist < R) & ((along > -12) & (side < along * math.tan(hfov) * 1.25 + 30))
if S.get("topdown"):
    keep = (np.abs(rel[:, 0]) < R) & (np.abs(rel[:, 1]) < R * 0.6)
GX = GX[keep]; GY = GY[keep]
H, REG, EX = terrain(GX, GY, T)
FIGS = S.get("figs", [])
for (fx_, fy_, jump_, act_, fr_, yaw_, glb_) in FIGS:
    if jump_ == 0.0:   # standing/landing: depressed slab + faint ripple (floating-platform response)
        rr = np.hypot(GX - fx_, GY - fy_)
        H = H - 0.22 * (rr < 0.5) + 0.05 * np.sin(rr * 1.9 - 1.0) * np.exp(-rr / 3.0) * (rr > 0.6)
N = GX.size
print("tiles", N)

# neighbour heights for contour edges (E,N,W,S)
def hat(dx, dy):
    return terrain(GX + dx, GY + dy, T)[0]
hE, hN, hW, hS = hat(1, 0), hat(0, 1), hat(-1, 0), hat(0, -1)
CI = 3.0
c0 = np.floor(H / CI)
land = REG > 0
flags = np.stack([
    (np.floor(hN / CI) < c0), (np.floor(hE / CI) < c0),
    (np.floor(hS / CI) < c0), (np.floor(hW / CI) < c0)], 1).astype(float)
flags *= (land & (H > 1.0) & (REG != 6))[:, None]
if CONTOURS == "off":
    flags[:] = 0.0
crest = EX["crest"]
crest = EX["crest"]
cm = crest > 0.02
flags[cm] = np.maximum(flags[cm], crest[cm][:, None])

# ---------------------------------------------------------------- colour
def ramp(stops, t):
    t = np.clip(t, 0, 1) * (len(stops) - 1)
    i = np.minimum(np.floor(t).astype(int), len(stops) - 2)
    f = (t - i)[:, None]
    S_ = np.array(stops)
    return S_[i] * (1 - f) + S_[i + 1] * f

col = np.zeros((N, 3))
# hypsometric tint on absolute height (cartographic, not normalised per frame)
HST = np.array([0.0, 5.0, 12.0, 20.0, 30.0])
ln = np.interp(H, HST, np.linspace(0, 1, 5))
col[:] = ramp(P["land"], ln)
# directional drift: one slow world-space gradient so no two valleys share a tint
dr = 0.5 + 0.5 * np.sin((GX * 0.8 + GY * 0.6) / 260.0 + 0.7)
da = P["drift_amt"] * (np.clip(H, 0, 40) < 18 if P["drift_amt"] < 0.2 else 1.0)
da = da * dr
col = col * (1 - da[:, None]) + P["drift"] * da[:, None]
# coastal terraces: alternate paddies, every third one flooded (water colour, glossy)
coast = REG == 1
par = (EX["tidx"] % 2)[:, None] * 2 - 1
col[coast] = col[coast] * (1 + 0.07 * par[coast])
flood = coast & (EX["tidx"] % 3 == 1)
col[flood] = P["water"][2] * 0.85 + P["shallow"] * 0.15
# water: colour by wave height, shallows near the shore
wv = EX["wv"]
wcol = ramp(P["water"], (wv + 1.1) / 2.2)
sh = sstep(-9, 0, EX["dsh"])[:, None] * 0.6
wcol = wcol * (1 - 0.55 * sh) + P["shallow"] * 0.55 * sh
water = REG == 0
col[water] = wcol[water]
col[REG == 5] = P["river"]
col[REG == 6] = P["path"]
# subtle per-tile value jitter (one tile is an object, not a pixel)
jit = 1 + 0.045 * (hash2(GX.astype(np.int64), GY.astype(np.int64), 77) - 0.5)
col *= jit[:, None]

emit = np.zeros((N, 3))
emit[REG == 6] = P["path"] * P["path_emit"]
emit[REG == 5] = P["river"] * P["river_emit"]
gi, gj = GX.astype(np.int64), GY.astype(np.int64)
flat = (np.abs(hE - H) < 0.25) & (np.abs(hN - H) < 0.25) & (np.abs(hW - H) < 0.25) & (np.abs(hS - H) < 0.25)
bea = (REG == 2) & (H > 20) & flat & (hash2(gi // 5, gj // 5, 5) < 0.03) & (hash2(gi, gj, 6) < 0.8)
emit[bea] = P["beacon"] * P["beacon_str"]
emit += (P["water"][3] * P["crest_str"] * 0.06) * crest[:, None]
rough = np.where(water | (REG == 5) | flood, 0.16, 0.62)

# ---------------------------------------------------------------- mesh: thin sharp slabs (8 verts / 5 faces per tile)
GAP = 0.085; TH = 0.3
half = 0.5 - GAP / 2
o = np.array([[-1, -1], [1, -1], [1, 1], [-1, 1]], float)
V = np.zeros((N, 8, 3))
for b0, dz in ((0, 0.0), (4, -TH)):
    V[:, b0:b0 + 4, 0] = GX[:, None] + o[:, 0] * half
    V[:, b0:b0 + 4, 1] = GY[:, None] + o[:, 1] * half
    V[:, b0:b0 + 4, 2] = H[:, None] + dz
faces = [(0, 1, 2, 3)]
for k in range(4):
    a, b = k, (k + 1) % 4
    faces.append((4 + a, 4 + b, b, a))    # side
NF = len(faces)
F = np.array(faces)
LOOPV = (F[None, :, :] + 8 * np.arange(N)[:, None, None]).reshape(-1)
me = bpy.data.meshes.new("tiles")
me.vertices.add(N * 8)
me.vertices.foreach_set("co", V.reshape(-1).astype(np.float32))
me.loops.add(N * NF * 4)
me.loops.foreach_set("vertex_index", LOOPV.astype(np.int32))
me.polygons.add(N * NF)
me.polygons.foreach_set("loop_start", (np.arange(N * NF) * 4).astype(np.int32))
me.update(calc_edges=True)

def face_attr(name, typ, data):
    a = me.attributes.new(name, typ, 'FACE')
    key = "color" if typ == 'FLOAT_COLOR' else "value"
    a.data.foreach_set(key, data.reshape(-1).astype(np.float32))

def rep9(a):
    return np.repeat(a, NF, axis=0)
c4 = np.concatenate([col, np.ones((N, 1))], 1)
face_attr("tcol", 'FLOAT_COLOR', rep9(c4))
e4 = np.concatenate([emit, np.ones((N, 1))], 1)
face_attr("temit", 'FLOAT_COLOR', rep9(e4))
face_attr("tedge", 'FLOAT_COLOR', rep9(flags))
face_attr("trough", 'FLOAT', rep9(rough))
corner_v = np.array(faces).reshape(-1)
ao = np.where(corner_v < 4, 1.0, 0.0)
uvt = np.zeros((NF * 4, 2)) - 1.0
pos01 = (o + 1) / 2
for li, vi in enumerate(corner_v[:4]):
    uvt[li] = pos01[vi]
aoA = me.attributes.new("tao", 'FLOAT', 'CORNER')
aoA.data.foreach_set("value", np.tile(ao, N).astype(np.float32))
uv = me.uv_layers.new(name="tuv")
uv.data.foreach_set("uv", np.tile(uvt.reshape(-1), N).astype(np.float32))

# the under-surface seen through the gaps: one smooth dark sheet well below the slabs
US = 2.0
ux = np.arange(math.floor(GX.min()) - 2, math.ceil(GX.max()) + 3, US)
uy = np.arange(math.floor(GY.min()) - 2, math.ceil(GY.max()) + 3, US)
UX, UY = np.meshgrid(ux, uy)
UH = terrain(UX.ravel(), UY.ravel(), T)[0].reshape(UX.shape)
UH = np.minimum(UH, np.minimum(np.roll(UH, 1, 0), np.roll(UH, -1, 0)))
UH = np.minimum(UH, np.minimum(np.roll(UH, 1, 1), np.roll(UH, -1, 1))) - 1.4
ny_, nx_ = UX.shape
UV_ = np.stack([UX.ravel(), UY.ravel(), UH.ravel()], 1)
ii = np.arange(ny_ - 1)[:, None] * nx_ + np.arange(nx_ - 1)[None, :]
UF = np.stack([ii, ii + 1, ii + 1 + nx_, ii + nx_], -1).reshape(-1, 4)
ume = bpy.data.meshes.new("under")
ume.vertices.add(len(UV_)); ume.vertices.foreach_set("co", UV_.reshape(-1).astype(np.float32))
ume.loops.add(UF.size); ume.loops.foreach_set("vertex_index", UF.reshape(-1).astype(np.int32))
ume.polygons.add(len(UF)); ume.polygons.foreach_set("loop_start", (np.arange(len(UF)) * 4).astype(np.int32))
ume.update(calc_edges=True)
uobj = bpy.data.objects.new("under", ume)
obj = bpy.data.objects.new("tiles", me)

# ---------------------------------------------------------------- scene
for o_ in list(bpy.data.objects):
    if o_ is not obj and o_ is not uobj: bpy.data.objects.remove(o_)
sc = bpy.context.scene
sc.collection.objects.link(obj)
sc.collection.objects.link(uobj)

def sun_dir(el, az):
    el, az = math.radians(el), math.radians(az)
    return np.array([math.cos(el) * math.sin(az), math.cos(el) * math.cos(az), math.sin(el)])
SD = sun_dir(P["sun_elev"], P["sun_az"])

mat = bpy.data.materials.new("tile")
mat.use_nodes = True
nt = mat.node_tree; N_ = nt.nodes; L = nt.links
N_.clear()
def node(t, **kw):
    n = N_.new(t)
    for k, v in kw.items():
        setattr(n, k, v)
    return n
def attr(name, kind='GEOMETRY'):
    a = node("ShaderNodeAttribute"); a.attribute_name = name; a.attribute_type = kind; return a
def math_(op, a, b=None, c=None):
    m = node("ShaderNodeMath"); m.operation = op
    for i, v in enumerate((a, b, c)):
        if v is None: continue
        if isinstance(v, (int, float)): m.inputs[i].default_value = v
        else: L.new(v, m.inputs[i])
    return m.outputs[0]
def rgb(c):
    n = node("ShaderNodeRGB"); n.outputs[0].default_value = (*c, 1); return n.outputs[0]
def mixc(fac, a, b, mode='MIX'):
    m = node("ShaderNodeMix"); m.data_type = 'RGBA'; m.blend_type = mode
    for sock, v in ((m.inputs[0], fac), (m.inputs[6], a), (m.inputs[7], b)):
        if isinstance(v, (int, float)): sock.default_value = v
        else: L.new(v, sock)
    return m.outputs[2]

tcol = attr("tcol").outputs["Color"]; temit = attr("temit").outputs["Color"]
tedge = attr("tedge").outputs["Color"]; trough = attr("trough").outputs["Fac"]
tao = attr("tao").outputs["Fac"]
uvn = node("ShaderNodeUVMap"); uvn.uv_map = "tuv"
sep = node("ShaderNodeSeparateXYZ"); L.new(uvn.outputs[0], sep.inputs[0])
u, v = sep.outputs[0], sep.outputs[1]
sepE = node("ShaderNodeSeparateColor"); L.new(tedge, sepE.inputs[0])
W = 0.085
def band(d):  # 1 at the tile boundary, 0 at W inside
    return math_('MAXIMUM', math_('SUBTRACT', 1.0, math_('DIVIDE', d, W)), 0.0)
eN = math_('MULTIPLY', sepE.outputs[0], band(math_('SUBTRACT', 1.0, v)))
eE = math_('MULTIPLY', sepE.outputs[1], band(math_('SUBTRACT', 1.0, u)))
eS = math_('MULTIPLY', sepE.outputs[2], band(v))
eW = math_('MULTIPLY', sepE.outputs[2 if False else 2], band(u))
# W uses the 4th flag via alpha
alpha = attr("tedge").outputs["Alpha"]
eW = math_('MULTIPLY', alpha, band(u))
edge = math_('MAXIMUM', math_('MAXIMUM', eN, eE), math_('MAXIMUM', eS, eW))
edge = math_('MULTIPLY', edge, math_('GREATER_THAN', u, -0.5))
edge = math_('POWER', edge, 1.6)
# side darkening toward the tile's foot (a cheap occlusion read; the gap stays dark)
aof = math_('ADD', 0.18, math_('MULTIPLY', math_('POWER', tao, 1.0), 0.82))
base = mixc(1.0, tcol, aof, 'MULTIPLY')
bsdf = node("ShaderNodeBsdfPrincipled")
L.new(base, bsdf.inputs["Base Color"]); L.new(trough, bsdf.inputs["Roughness"])
bsdf.inputs["Specular IOR Level"].default_value = 0.5
# emission = tile emission + edge * edge colour
em_edge = node("ShaderNodeVectorMath"); em_edge.operation = 'SCALE'
L.new(rgb(P["edge"] * P["edge_str"]), em_edge.inputs[0]); L.new(edge, em_edge.inputs["Scale"])
em_sum = node("ShaderNodeVectorMath"); em_sum.operation = 'ADD'
topm = math_('GREATER_THAN', u, -0.5)
em_top = node("ShaderNodeVectorMath"); em_top.operation = 'SCALE'
L.new(temit, em_top.inputs[0]); L.new(topm, em_top.inputs["Scale"])
L.new(em_top.outputs[0], em_sum.inputs[0]); L.new(em_edge.outputs[0], em_sum.inputs[1])
L.new(em_sum.outputs[0], bsdf.inputs["Emission Color"]); bsdf.inputs["Emission Strength"].default_value = 1.0
# atmospheric perspective: exponential distance fog thinning with height, tinted to the sun
cd = node("ShaderNodeCameraData")
geo = node("ShaderNodeNewGeometry")
sepP = node("ShaderNodeSeparateXYZ"); L.new(geo.outputs["Position"], sepP.inputs[0])
hfac = math_('ADD', 0.45, math_('MULTIPLY', 0.55, math_('EXPONENT', math_('MULTIPLY', math_('MAXIMUM', sepP.outputs[2], 0.0), -0.035))))
k = 1.0 / (P["fog_k"] * 0.58 * S.get("fogR", R))
dd = math_('MULTIPLY', math_('MULTIPLY', cd.outputs["View Distance"], k), hfac)
fogf = math_('SUBTRACT', 1.0, math_('EXPONENT', math_('MULTIPLY', math_('MULTIPLY', dd, dd), -1.0)))
dsun = node("ShaderNodeVectorMath"); dsun.operation = 'DOT_PRODUCT'
L.new(geo.outputs["Incoming"], dsun.inputs[0]); dsun.inputs[1].default_value = tuple(-SD)
sunf = math_('POWER', math_('MAXIMUM', dsun.outputs["Value"], 0.0), 6.0)
fogc = mixc(sunf, rgb(P["fog"]), rgb(P["fog_sun"]))
fogE = node("ShaderNodeEmission"); L.new(fogc, fogE.inputs[0])
lpm = node("ShaderNodeLightPath")
fogf = math_('MULTIPLY', fogf, lpm.outputs["Is Camera Ray"])
mix = node("ShaderNodeMixShader"); L.new(fogf, mix.inputs[0]); L.new(bsdf.outputs[0], mix.inputs[1]); L.new(fogE.outputs[0], mix.inputs[2])
out = node("ShaderNodeOutputMaterial"); L.new(mix.outputs[0], out.inputs[0])
me.materials.append(mat)
# under-surface: same fog, flat dark colour, no emission
umat = mat.copy(); umat.name = "under"
ub = [n for n in umat.node_tree.nodes if n.bl_idname == "ShaderNodeBsdfPrincipled"][0]
for sock in ("Base Color", "Roughness", "Emission Color"):
    for l in list(ub.inputs[sock].links):
        umat.node_tree.links.remove(l)
ub.inputs["Base Color"].default_value = (*P["under"], 1)
ub.inputs["Roughness"].default_value = 0.9
ub.inputs["Emission Color"].default_value = (0, 0, 0, 1)
ume.materials.append(umat)

# world: sky gradient for the camera, a flat ambient for lighting
w = bpy.data.worlds.new("w"); sc.world = w; w.use_nodes = True
wn = w.node_tree.nodes; wl = w.node_tree.links; wn.clear()
tc = wn.new("ShaderNodeTexCoord")
sepw = wn.new("ShaderNodeSeparateXYZ"); wl.new(tc.outputs["Generated"], sepw.inputs[0])
mr = wn.new("ShaderNodeMapRange"); wl.new(sepw.outputs[2], mr.inputs[0]); mr.inputs[1].default_value = -0.02; mr.inputs[2].default_value = 0.55
pw = wn.new("ShaderNodeMath"); pw.operation = 'POWER'; wl.new(mr.outputs[0], pw.inputs[0]); pw.inputs[1].default_value = 0.6
mx = wn.new("ShaderNodeMix"); mx.data_type = 'RGBA'
wl.new(pw.outputs[0], mx.inputs[0]); mx.inputs[6].default_value = (*P["fog"], 1); mx.inputs[7].default_value = (*P["sky_zen"], 1)
dp = wn.new("ShaderNodeVectorMath"); dp.operation = 'DOT_PRODUCT'; wl.new(tc.outputs["Generated"], dp.inputs[0]); dp.inputs[1].default_value = tuple(SD)
sg = wn.new("ShaderNodeMath"); sg.operation = 'POWER'; wl.new(dp.outputs["Value"], sg.inputs[0]); sg.inputs[1].default_value = 24.0
mx2 = wn.new("ShaderNodeMix"); mx2.data_type = 'RGBA'; mx2.blend_type = 'ADD'
wl.new(sg.outputs[0], mx2.inputs[0]); wl.new(mx.outputs[2], mx2.inputs[6]); mx2.inputs[7].default_value = (*(P["fog_sun"] * 1.6), 1)
bgS = wn.new("ShaderNodeBackground"); wl.new(mx2.outputs[2], bgS.inputs[0])
bgA = wn.new("ShaderNodeBackground"); bgA.inputs[0].default_value = (*P["amb"], 1); bgA.inputs[1].default_value = P["amb_str"]
lp = wn.new("ShaderNodeLightPath")
ms = wn.new("ShaderNodeMixShader"); wl.new(lp.outputs["Is Camera Ray"], ms.inputs[0]); wl.new(bgA.outputs[0], ms.inputs[1]); wl.new(bgS.outputs[0], ms.inputs[2])
wo = wn.new("ShaderNodeOutputWorld"); wl.new(ms.outputs[0], wo.inputs[0])

sun = bpy.data.lights.new("sun", 'SUN'); sun.energy = P["sun_str"]; sun.color = tuple(np.clip(P["sun"] / max(P["sun"].max(), 1e-6), 0, 1))
sun.angle = math.radians(1.5)
so = bpy.data.objects.new("sun", sun); sc.collection.objects.link(so)
so.rotation_euler = (math.pi / 2 - math.radians(P["sun_elev"]), 0, math.radians(180 - P["sun_az"]) if False else 0)
# point the sun along -SD
from mathutils import Vector
so.rotation_euler = Vector(tuple(-SD)).to_track_quat('-Z', 'Y').to_euler()

camd = bpy.data.cameras.new("cam"); camd.lens = S["f"]; camd.sensor_width = 36; camd.clip_end = R * 2.5; camd.clip_start = 0.2
co = bpy.data.objects.new("cam", camd); sc.collection.objects.link(co); sc.camera = co
co.location = cam; co.rotation_euler = look_rot(cam, tgt)
co.rotation_euler[1] = math.radians(S.get("bank", 0.0))
co.rotation_mode = 'XYZ'

# characters: the Glowmere alien GLBs (commercial; read from assets/, never copied), ~1 tile tall
ALIENS = "/Users/natefaulkenberry/Documents/GitHub/av-gen-geography-art/assets/aliens/"
for (fx_, fy_, jump_, act_, fr_, yaw_, glb_) in FIGS:
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=ALIENS + glb_)
    new = [o_ for o_ in bpy.data.objects if o_ not in before]
    for o_ in new:
        if o_.name.startswith("Icosphere"):
            bpy.data.objects.remove(o_)
    new = [o_ for o_ in bpy.data.objects if o_ not in before]
    rig = [o_ for o_ in new if o_.type == 'ARMATURE'][0]
    act = bpy.data.actions.get(act_)
    rig.animation_data_create()
    rig.animation_data.action = act
    try:
        if len(act.slots): rig.animation_data.action_slot = act.slots[0]
    except Exception as e:
        print("slot", e)
    sc.frame_set(int(fr_))
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    zs = []
    for o_ in new:
        if o_.type == 'MESH':
            oe = o_.evaluated_get(dg)
            zs += [(o_.matrix_world @ v.co).z for v in oe.data.vertices]
    ht = max(zs) - min(zs); z0 = min(zs)
    sca = 1.0 / ht if not jump_ else 1.0 / 1.55
    hz = float(H[(GX == fx_) & (GY == fy_)][0])
    rig.scale = (sca, sca, sca)
    rig.location = (fx_, fy_, hz)
    rig.rotation_euler = (0, 0, math.radians(yaw_))
    bpy.context.view_layer.update(); dg = bpy.context.evaluated_depsgraph_get()
    pts = []
    for o_ in new:
        if o_.type == 'MESH':
            oe = o_.evaluated_get(dg)
            mw = o_.matrix_world
            pts += [tuple(mw @ v.co) for v in oe.data.vertices]
    pts = np.array(pts)
    cxy = (pts[:, :2].min(0) + pts[:, :2].max(0)) / 2
    rig.location = (fx_ + (fx_ - cxy[0]), fy_ + (fy_ - cxy[1]), hz + jump_ + (hz - pts[:, 2].min()))
    print("alien", glb_, "height", ht, "scale", sca)
FRAME_KEEP = sc.frame_current

# render settings
sc.render.engine = 'CYCLES'   # CPU path tracing: keeps the shared GPU free
sc.cycles.device = 'CPU'
sc.cycles.samples = int(argv[3]) if len(argv) > 3 else 48
sc.cycles.use_denoising = True
sc.cycles.max_bounces = 4
sc.cycles.diffuse_bounces = 2
sc.cycles.glossy_bounces = 2
sc.cycles.transmission_bounces = 0
sc.cycles.transparent_max_bounces = 2
try:
    sc.render.compositor_device = 'CPU'
except Exception as e:
    print("compositor device", e)
sc.render.resolution_x = int(1920 * RES_SCALE); sc.render.resolution_y = int(1080 * RES_SCALE)
sc.eevee.taa_render_samples = 48
sc.eevee.use_raytracing = True
sc.eevee.use_shadows = True
sc.view_settings.view_transform = 'AgX'
sc.view_settings.look = P.get("look", 'AgX - Medium High Contrast')
sc.view_settings.exposure = P["exposure"]
sc.render.film_transparent = False

ng = bpy.data.node_groups.new("comp", "CompositorNodeTree")
ng.interface.new_socket("Image", in_out='OUTPUT', socket_type='NodeSocketColor')
rl = ng.nodes.new("CompositorNodeRLayers")
gl = ng.nodes.new("CompositorNodeGlare")
gl.inputs["Type"].default_value = 'Bloom'
gl.inputs["Threshold"].default_value = 1.0
gl.inputs["Strength"].default_value = 0.6 if P["edge_str"] > 2 else 0.25
gl.inputs["Size"].default_value = 0.6
go = ng.nodes.new("NodeGroupOutput")
hs = ng.nodes.new("CompositorNodeHueSat")
print("huesat inputs", [i.name for i in hs.inputs])
hs.inputs["Saturation"].default_value = P.get("sat", 1.0)
ng.links.new(rl.outputs["Image"], gl.inputs["Image"]); ng.links.new(gl.outputs["Image"], hs.inputs["Image"])
ng.links.new(hs.outputs[0], go.inputs[0])
sc.compositing_node_group = ng
sc.render.use_compositing = True

sc.render.image_settings.file_format = 'PNG'
sc.render.filepath = OUT
sc.frame_set(FRAME_KEEP)
bpy.ops.render.render(write_still=True)
print("WROTE", OUT, "tiles", N)
