# Musical Geography -- Gate 1 look mock-up (Blender, headless, Eevee).
# Not engine code: a cheap proof of the art direction in 04-art-direction.md.
# usage: Blender -b --factory-startup --python gate1-mockup.py -- <shot> <out.png> [scale] [samples]
# Throwaway: the formation rules and palettes of 04-art-direction.md, for engineering to read, not to port line by line.
import bpy, sys, math
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]
SHOT = argv[0]
OUT = argv[1]
RES_SCALE = float(argv[2]) if len(argv) > 2 else 1.0

# ---------------------------------------------------------------- palettes
def hexlin(h):
    h = h.lstrip('#')
    c = np.array([int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)])
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)

PAL = {
  "day": dict(
    drift_amt=0.14,
    drift="#8E9CF2",
    sky_zen="#5C7EE0", sky_hor="#E6E0F4", fog="#D9D2EE", fog_sun="#FFF0D8",
    sun="#FFF1DC", sun_str=3.9, sun_elev=40, sun_az=235, amb="#9EAAF0", amb_str=0.45,
    water=["#1630A8", "#2448D0", "#3F70E8", "#8DB4F6"], shallow="#3FB0C8",
    land=["#8088E6", "#A890E4", "#EBA290", "#EDB872", "#F0D480"],
    wall="#EE987C", strata=0.08, river="#3A62D6",
    edge="#FFF4C8", edge_str=0.0, crest_str=0.0,
    path="#FFF5E2", path_emit=0.0, beacon="#FF8FB4", beacon_str=0.0, river_emit=0.0,
    fog_k=1.0, exposure=-0.4),
  "sunset": dict(
    drift_amt=0.28,
    drift="#6A3AA0",
    sky_zen="#33206E", sky_hor="#FF8A50", fog="#E07462", fog_sun="#FFB060",
    sun="#FF9A4A", sun_str=5.0, sun_elev=9, sun_az=262, amb="#5E46B8", amb_str=0.9,
    water=["#24164F", "#4A2380", "#8A2F8C", "#E2507A"], shallow="#9A3A9A",
    land=["#4E2A7E", "#8A2C78", "#D0405A", "#F2785A", "#FFB45A"],
    wall="#C8445E", strata=0.12, river="#5A2A8A",
    edge="#FFC27A", edge_str=0.9, crest_str=0.6,
    path="#FFD7A0", path_emit=0.6, beacon="#FFB070", beacon_str=0.0, river_emit=0.3,
    fog_k=1.35, exposure=-0.2),
  "night": dict(
    drift_amt=0.28,
    drift="#5A2CB4",
    sky_zen="#080A2C", sky_hor="#33237A", fog="#2C1F66", fog_sun="#41308E",
    sun="#C8B4FF", sun_str=1.2, sun_elev=34, sun_az=200, amb="#5A38B0", amb_str=0.6,
    water=["#0E1452", "#18227A", "#2443B0", "#3F78FF"], shallow="#2A3AA0",
    land=["#221A6C", "#36228E", "#52289E", "#7430B0", "#9C44C8"],
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
    land=["#6FB57E", "#9CCC84", "#DCE08E", "#F4D68C", "#FBE9C4"],
    wall="#E8A44A", strata=0.08, river="#7393BD",
    edge="#FFD27A", edge_str=0.5, crest_str=0.4,
    path="#F2A93B", path_emit=0.25, beacon="#FFC060", beacon_str=0.0, river_emit=0.0,
    fog_k=1.35, exposure=-0.2),
}

def lerp_pal(a, b, t):
    out = {}
    for k, va in a.items():
        vb = b[k]
        if isinstance(va, str):
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
  "01-day-low-pass-water":   dict(pal="day", cam=(-25, -165, 4.2), tgt=(10, 120, -14), f=24, R=620, t=3.0),
  "02-day-high-reveal":      dict(pal="day", cam=(-70, 120, 90), tgt=(20, 270, 0), f=35, R=700, t=3.0),
  "03-sunset-canyon":        dict(pal="sunset", cam=None, canyon_y=95, cam_z=27, look=70, f=28, R=650, t=5.0),
  "04-night-edge-light":     dict(pal="night", cam=(95, -10, 62), tgt=(-5, 175, 6), f=35, R=750, t=7.0),
  "05-dawn-terraces":        dict(pal="dawn", cam=(-125, -75, 26), tgt=(-30, 50, 3), f=35, R=650, t=2.0),
  "06-dusk-wavefront":       dict(pal=("sunset", "night", 0.55), cam=(40, -150, 7), tgt=(-30, 40, 1), f=24, R=620, t=9.0),
  "07-sunset-plateau-figures-scale": dict(pal="sunset", cam=None, plateau=True, f=50, R=500, t=5.0),
}
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

def canyon_x(y):
    return 12 + 34 * np.sin(y / 62.0 + 0.3) + 13 * np.sin(y / 27.0 + 2.1)

def path_x(y):
    return -95 + 38 * np.sin(y / 74.0 + 0.9) + 9 * np.sin(y / 31.0)

def terrain(x, y, t):
    """returns height, region id, extra fields."""
    dsh = y - shore_y(x)
    land = sstep(-2, 22, dsh)
    # rolling hills: domain-warped low-frequency fbm, broad swells not bumps
    wx = x + 40 * (fbm(x / 210, y / 210, 2, 5) - 0.5)
    wy = y + 40 * (fbm(x / 210 + 7, y / 210, 2, 6) - 0.5)
    hills = fbm(wx / 150, wy / 150, 4, 1)
    hills = 3.0 + 20.0 * sstep(0.22, 0.80, hills) + 3.0 * (fbm(wx / 45, wy / 45, 2, 9) - 0.5)
    # coastal terraces (tidal flats / paddies): quantised rise near the shore
    coast_h = np.minimum(dsh, 70) * 0.13 + 2.0 * (fbm(x / 30, y / 30, 2, 3) - 0.5)
    tidx = np.floor(np.maximum(coast_h, 0) / 1.1)
    terr = tidx * 1.1
    coastw = 1 - sstep(15 + 25 * fbm(x / 70, y / 70, 1, 2), 60 + 30 * fbm(x / 70, y / 70, 1, 2), dsh)
    h = hills * (1 - coastw) + np.minimum(terr, hills) * coastw
    # mesas: sharp-edged mask, flat tops at three levels, stepped cliffs
    mm = fbm(x / 85 + 3.3, y / 85 - 1.1, 3, 11) + 0.0011 * np.clip(y - 60, -80, 220)
    m = sstep(0.655, 0.685, mm)
    lvl = np.floor(fbm(x / 160, y / 160, 2, 21) * 3.0)
    top = 22.5 + 7 * lvl + 0.5 * (fbm(x / 25, y / 25, 1, 4) - 0.5)
    mesa_h = np.maximum(h, top)
    cliff = h + (mesa_h - h) * m
    cliff = np.where((m > 0.02) & (m < 0.98), np.floor(cliff / 2.2) * 2.2, cliff)
    h = cliff
    region = np.where(m > 0.5, 3, np.where(coastw > 0.5, 1, 2))
    # canyon: meandering, flat river floor, terraced walls
    dc = np.abs(x - canyon_x(y))
    floor = -0.6
    wf, wr = 4.0, 4.0 + 7 + 0.3 * np.clip(h, 0, 40)
    tt = np.clip((dc - wf) / (wr - wf), 0, 1)
    prof = floor + (h - floor) * np.floor(tt ** 1.25 * 7) / 7
    incanyon = (dc < wr) & (dsh > -4)
    h = np.where(incanyon, np.minimum(h, prof), h)
    river = (dc < wf) & (dsh > -4)
    region = np.where(incanyon & ~river, 4, region)
    region = np.where(river, 5, region)
    # path: a raised causeway following its own meander up the hills
    dp = np.abs(x - path_x(y))
    onpath = (dp < 1.0) & (dsh > 6) & (y < 330) & ~incanyon
    hs = hills * (1 - coastw) + np.maximum(coast_h, 0) * coastw
    h = np.where(onpath, np.minimum(h, hs) + 0.35, h)
    region = np.where(onpath, 6, region)
    # water: two crossing wave trains + a long swell (art-directed, not noise)
    k1 = 2 * np.pi / 34.0; d1 = np.array([0.82, 0.57])
    k2 = 2 * np.pi / 21.0; d2 = np.array([-0.45, 0.89])
    k3 = 2 * np.pi / 95.0; d3 = np.array([0.2, 0.98])
    w1 = np.sin(k1 * (x * d1[0] + y * d1[1]) - 1.1 * t)
    w2 = np.sin(k2 * (x * d2[0] + y * d2[1]) - 1.6 * t + 1.0)
    w3 = np.sin(k3 * (x * d3[0] + y * d3[1]) - 0.5 * t)
    wv = 0.42 * w1 + 0.26 * w2 + 0.55 * w3
    wv += 0.08 * (fbm(x / 9 + 0.3 * t, y / 9, 1, 31) - 0.5)
    water_h = -0.4 + wv
    crest = sstep(0.955, 0.998, w1) * (0.35 + 0.65 * sstep(-0.3, 0.9, w2))   # wavefront on one train, brightest where trains meet
    h = np.where(land > 0.5, h * land + (1 - land) * water_h, water_h)
    region = np.where(land > 0.5, region, 0)
    h = np.where(river, -0.5 + 0.18 * w2, h)
    return h, region, dict(crest=crest * (region == 0), wv=wv, dsh=dsh, river=river, tidx=tidx)

# ---------------------------------------------------------------- camera
def look_rot(cam, tgt):
    d = np.array(tgt, float) - np.array(cam, float)
    d /= np.linalg.norm(d)
    yaw = math.atan2(d[1], d[0]); pitch = math.asin(d[2])
    return (math.pi / 2 + pitch, 0.0, yaw - math.pi / 2)

if S.get("cam") is None and "canyon_y" in S:
    y0 = S["canyon_y"]; y1 = y0 + S["look"]
    cam = (float(canyon_x(np.array(y0))), y0, S["cam_z"])
    tgt = (float(canyon_x(np.array(y1))), y1, S["cam_z"] - 9)
elif S.get("plateau"):
    cam, tgt = None, None
else:
    cam, tgt = S["cam"], S["tgt"]

# find a mesa for the plateau shot: highest flat area near (30,170)
if S.get("plateau"):
    gx, gy = np.meshgrid(np.arange(-150, 150, 2.0), np.arange(60, 320, 2.0))
    hh, rr, _ = terrain(gx, gy, T)
    score = np.where(rr == 3, hh, -1e9)
    k = np.unravel_index(np.argmax(score), score.shape)
    px, py, ph = gx[k], gy[k], hh[k]
    angs = np.linspace(0, 2 * np.pi, 72, endpoint=False)
    hd = [terrain(np.array([px + 30 * np.cos(a)]), np.array([py + 30 * np.sin(a)]), T)[0][0] for a in angs]
    a = angs[int(np.argmin(hd))]
    dvec = np.array([np.cos(a), np.sin(a)])
    # walk toward the rim until the next tile drops
    r = 0
    while terrain(np.array([px + (r + 1) * dvec[0]]), np.array([py + (r + 1) * dvec[1]]), T)[0][0] > ph - 1.5 and r < 40:
        r += 1
    rx, ry = px + (r - 3) * dvec[0], py + (r - 3) * dvec[1]
    PLATEAU = (rx, ry, ph)
    PDIR = dvec
    cam = (rx - 11 * dvec[0] + 3 * dvec[1], ry - 11 * dvec[1] - 3 * dvec[0], ph + 3.2)
    tgt = (rx + 60 * dvec[0], ry + 60 * dvec[1], ph - 12)
    print("plateau at", PLATEAU)

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
GX = GX[keep]; GY = GY[keep]
H, REG, EX = terrain(GX, GY, T)
if S.get("plateau"):
    px, py, ph = PLATEAU
    pd = np.array([-PDIR[1], PDIR[0]])
    FIGS = []
    for (a_, b_, j_) in [(0, 0, 0.0), (-3, 2.5, 1.0), (-1.5, -2.5, 0.0)]:
        FIGS.append((round(px + a_ * PDIR[0] + b_ * pd[0]), round(py + a_ * PDIR[1] + b_ * pd[1]), j_))
    # floating-platform response under figure 0 (just landed): depressed tile, smaller ring, faint ripple
    lx, ly = FIGS[0][0], FIGS[0][1]
    rr = np.hypot(GX - lx, GY - ly)
    H = H - 0.32 * np.exp(-rr * rr / 0.5) + 0.07 * np.sin(rr * 1.9 - 1.0) * np.exp(-rr / 3.0) * (rr > 0.6)
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
flags *= (land & (H > 1.0))[:, None]
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
wall = REG == 4
band = (np.floor(H / 2.2) % 2) * 2 - 1
col[wall] = (P["wall"] * (1 + P["strata"] * band[wall][:, None]))
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
flat = (np.abs(hE - H) < 0.8) & (np.abs(hN - H) < 0.8) & (np.abs(hW - H) < 0.8) & (np.abs(hS - H) < 0.8)
bea = (REG == 3) & flat & (hash2(gi // 5, gj // 5, 5) < 0.02) & (hash2(gi, gj, 6) < 0.8)
emit[bea] = P["beacon"] * P["beacon_str"]
emit += (P["water"][3] * P["crest_str"] * 0.06) * crest[:, None]
rough = np.where(water | (REG == 5) | flood, 0.16, 0.62)

# ---------------------------------------------------------------- mesh (12 verts / 9 faces per tile)
GAP = 0.085; CH = 0.045
half = 0.5 - GAP / 2; inn = half - CH
floor_z = H.min() - 3.0
o = np.array([[-1, -1], [1, -1], [1, 1], [-1, 1]], float)
V = np.zeros((N, 12, 3))
V[:, 0:4, 0] = GX[:, None] + o[:, 0] * inn; V[:, 0:4, 1] = GY[:, None] + o[:, 1] * inn; V[:, 0:4, 2] = H[:, None]
V[:, 4:8, 0] = GX[:, None] + o[:, 0] * half; V[:, 4:8, 1] = GY[:, None] + o[:, 1] * half; V[:, 4:8, 2] = H[:, None] - CH
V[:, 8:12, 0] = V[:, 4:8, 0]; V[:, 8:12, 1] = V[:, 4:8, 1]
V[:, 8:12, 2] = np.maximum(floor_z, H[:, None] - 60)
faces = [(0, 1, 2, 3)]
for k in range(4):
    a, b = k, (k + 1) % 4
    faces.append((4 + a, 4 + b, b, a))            # chamfer
for k in range(4):
    a, b = k, (k + 1) % 4
    faces.append((8 + a, 8 + b, 4 + b, 4 + a))    # side
F = np.array(faces)
LOOPV = (F[None, :, :] + 12 * np.arange(N)[:, None, None]).reshape(-1)
me = bpy.data.meshes.new("tiles")
me.vertices.add(N * 12)
me.vertices.foreach_set("co", V.reshape(-1).astype(np.float32))
me.loops.add(N * 36)
me.loops.foreach_set("vertex_index", LOOPV.astype(np.int32))
me.polygons.add(N * 9)
me.polygons.foreach_set("loop_start", (np.arange(N * 9) * 4).astype(np.int32))
me.update(calc_edges=True)

def face_attr(name, typ, data):
    a = me.attributes.new(name, typ, 'FACE')
    key = "color" if typ == 'FLOAT_COLOR' else "value"
    a.data.foreach_set(key, data.reshape(-1).astype(np.float32))

def rep9(a):
    return np.repeat(a, 9, axis=0)
c4 = np.concatenate([col, np.ones((N, 1))], 1)
face_attr("tcol", 'FLOAT_COLOR', rep9(c4))
e4 = np.concatenate([emit, np.ones((N, 1))], 1)
face_attr("temit", 'FLOAT_COLOR', rep9(e4))
face_attr("tedge", 'FLOAT_COLOR', rep9(flags))
face_attr("trough", 'FLOAT', rep9(rough))
# per-corner: top-ness (1 at top, 0 at bottom of side) and tile UV (-1 on sides)
corner_v = np.array(faces).reshape(-1)
ao = np.where(corner_v < 8, 1.0, 0.0)
uvt = np.zeros((36, 2)) - 1.0
pos01 = (o + 1) / 2
inn01 = 0.5 + o * (inn / half) / 2
for li, vi in enumerate(corner_v):
    if li < 20:
        uvt[li] = inn01[vi] if vi < 4 else pos01[vi - 4]
aoA = me.attributes.new("tao", 'FLOAT', 'CORNER')
aoA.data.foreach_set("value", np.tile(ao, N).astype(np.float32))
uv = me.uv_layers.new(name="tuv")
uv.data.foreach_set("uv", np.tile(uvt.reshape(-1), N).astype(np.float32))
obj = bpy.data.objects.new("tiles", me)

# ---------------------------------------------------------------- scene
for o_ in list(bpy.data.objects):
    if o_ is not obj: bpy.data.objects.remove(o_)
sc = bpy.context.scene
sc.collection.objects.link(obj)

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
W = 0.11
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
k = 1.0 / (P["fog_k"] * 0.58 * R)
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
if "canyon_y" in S:
    co.rotation_euler[1] = math.radians(-4)   # a few degrees of bank into the meander
co.rotation_mode = 'XYZ'

# a few figure silhouettes for scale (plateau shot only): capsule body + head, ~0.85 tile tall
if S.get("plateau"):
    px, py, ph = PLATEAU
    fig = bpy.data.materials.new("fig"); fig.use_nodes = True
    fb = fig.node_tree.nodes["Principled BSDF"]
    fb.inputs["Base Color"].default_value = (*hexlin("#FFF3E6"), 1); fb.inputs["Roughness"].default_value = 0.45
    fb.inputs["Emission Color"].default_value = (*hexlin("#FFD2A8"), 1); fb.inputs["Emission Strength"].default_value = 0.15
    for i, (gx_, gy_, jump) in enumerate(FIGS):
        hz = float(H[(GX == gx_) & (GY == gy_)][0]) + jump
        bpy.ops.mesh.primitive_uv_sphere_add(radius=0.22, location=(gx_, gy_, hz + 0.38), segments=24, ring_count=12)
        b = bpy.context.object; b.scale = (1, 0.85, 1.55); b.data.materials.append(fig)
        bpy.ops.mesh.primitive_uv_sphere_add(radius=0.15, location=(gx_, gy_, hz + 0.86), segments=24, ring_count=12)
        bpy.context.object.data.materials.append(fig)

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
sc.view_settings.look = 'AgX - Medium High Contrast'
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
ng.links.new(rl.outputs["Image"], gl.inputs["Image"]); ng.links.new(gl.outputs["Image"], go.inputs[0])
sc.compositing_node_group = ng
sc.render.use_compositing = True

sc.render.image_settings.file_format = 'PNG'
sc.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("WROTE", OUT, "tiles", N)
