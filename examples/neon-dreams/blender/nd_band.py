#!/usr/bin/env python3
"""Neon Dreams, iteration 12 (owner msg 149): the band in the garden plaza, more jet packs, the end shot's clouds.

    python3 blender/nd_band.py [--root examples/neon-dreams]      (plain Python, no Blender; idempotent)

Edits the art data under ROOT/art, in the engine frame (Y up, x east, z = -north):
  layout.json        the garden pool -> the band stage (stage/band_stage + band_terrace, 8 floor spots, 4 searchlight
                     heads on the halo masts); the trees, lamps and planting in its footprint out; a top-level `band`
                     block for the engine (musicians, spot lights, sky beams, audio intent); the camera slowed and
                     brought close past the band (camera.waypoints, camera.look_ahead); the gondola pylons' legs
                     continued into the cloud (stage/pylon_leg_ext); the Break Light isle raised; the Lone Lamp lit.
  population.json    the band's audience (a ring on the plaza, three tiers behind the band, dancers), the figures and
                     walks that stood where the stage is out; six new jet-pack flights.
  cloud_design.json  the Break Light's seat moved with it; layered cloud/fog at the end; the ocean break as a CLOUD
                     VORTEX (`sea_design.break.vortex`, parameters for the engine, plus the shelves as tagged lobes that
                     carry their own orbit); wisp zones round the band and the end isles.
The originals of everything moved or removed are kept in population.json `edits[it=12].orig`, so a second run gives
the same files.
"""
import json, math, os, sys, random

HERE = os.path.dirname(os.path.abspath(__file__))
argv = sys.argv[1:]
ROOT = argv[argv.index("--root") + 1] if "--root" in argv else os.path.join(HERE, "..")
ART = os.path.join(ROOT, "art")
R3 = lambda v: [round(x, 3) for x in v]

# ------------------------------------------------------------------------------------------------ the stage's frame
C = (146.0, 121.6, 38.0)          # where the pool was (the plaza deck's top is 121.6)
YAW = -110.0                      # the stage's front (+z of the GLB) faces the Quay, where the lens comes from
DECK_TOP = C[1] + 1.0
MAST_R, MAST_A, HALO_Z, SPOT_Z = 6.75, (-45.0, -135.0, 45.0, 135.0), 8.8, 9.6
TERR = dict(r0=11.5, depth=1.33, rise=0.45, tiers=3, a0=20.0, a1=160.0)
FIX_R, FIX_PHI = 8.4, (-112.5, -67.5, -157.5, -22.5, 157.5, 22.5, 112.5, 67.5)


def g2e(g):
    """A point of the stage GLB (glTF: x, y up, z front) -> engine."""
    t = math.radians(YAW)
    return [C[0] + g[0] * math.cos(t) + g[2] * math.sin(t), C[1] + g[1], C[2] - g[0] * math.sin(t) + g[2] * math.cos(t)]


def b2e(bx, by, bz=0.0):
    """A point in the stage's Blender frame (x, y plan, z up; the front is -y) -> engine."""
    return g2e((bx, bz, -by))


def polar(phi_deg, r, z=0.0):
    a = math.radians(phi_deg)
    return b2e(r * math.cos(a), r * math.sin(a), z)


def yaw_to(a, b):
    return math.degrees(math.atan2(b[0] - a[0], b[2] - a[2]))


def quat_yaw(deg):
    h = math.radians(deg) / 2
    return [0.0, round(math.sin(h), 6), 0.0, round(math.cos(h), 6)]


def quat_aim(d):
    """The rotation that turns +z onto direction d (yaw about y, then pitch about the local x)."""
    yaw = math.atan2(d[0], d[2]); el = math.atan2(d[1], math.hypot(d[0], d[2]))
    qy = (0.0, math.sin(yaw / 2), 0.0, math.cos(yaw / 2))
    qx = (math.sin(-el / 2), 0.0, 0.0, math.cos(-el / 2))
    x1, y1, z1, w1 = qy; x2, y2, z2, w2 = qx
    return [round(v, 6) for v in (w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2, w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
                                  w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2, w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2)]


def norm(v):
    n = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / n for x in v]


def kicks():
    ev = json.load(open(os.path.join(ROOT, "audio", "sabertooth_events.json")))
    return [t for t, _ in ev["stems"]["kick"]["onsets"]]


KICKS = kicks()


def snap(t):
    return round(min(KICKS, key=lambda k: abs(k - t)), 3)


# ------------------------------------------------------------------------------------------------ the band
# Each performer and its props share one frame (assets/musicians/ATTRIBUTION.md): place both nodes with one transform.
# Positions in the stage's Blender frame (the riser's centre is (0, 1.5), its top 0.3 over the deck).
MUSICIANS = [
    dict(name="band_drums", asset="musicians/astronaut_drums.glb", props="musicians/drum_kit.glb", clip="Drums",
         at=(0.0, 1.95, 1.3), yaw=0.0, chest=1.05),
    dict(name="band_keys", asset="musicians/astronaut_keys.glb", props="musicians/keyboard_set.glb", clip="Piano",
         at=(-2.1, -0.9, 1.0), yaw=18.0, chest=1.15),
]


def band_block():
    mus = []
    for m in MUSICIANS:
        p = b2e(*m["at"])
        mus.append({"name": m["name"], "asset": "assets/" + m["asset"], "props": "assets/" + m["props"], "pos": R3(p),
                    "yaw_deg": round(YAW + m["yaw"], 2), "clip": m["clip"],
                    "chest": R3([p[0], p[1] + m["chest"], p[2]])})
    spots = []
    heads = []
    for i, phi in enumerate(FIX_PHI):
        base = polar(phi, FIX_R)
        piv = [base[0], base[1] + 0.95, base[2]]
        # odd fixtures light the drummer, even the keys; the axis passes 0.5 m over the chest (uplight, the beam runs
        # on up past the band into the air over the far side)
        tgt = mus[i % 2]["chest"]
        aim = [tgt[0], tgt[1] + 0.5, tgt[2]]
        d = norm([aim[k] - piv[k] for k in range(3)])
        lens = [piv[k] + d[k] * 0.45 for k in range(3)]
        key = ("sign_a", "moon_disc", "sign_b", "moon_disc")[i % 4]
        spots.append({"name": "band_spot%d" % i, "pos": R3(lens), "direction": R3(d), "aim": R3(aim),
                      "outer_deg": 15.0, "inner_deg": 8.0, "key": key, "intensity_cd": 900.0, "range_m": 30.0,
                      "volumetric": 0.6, "fixture": "spot_base + spot_head at %s" % R3(piv)})
        heads.append((base, piv, d, phi, key))
    sky = []
    for i, a in enumerate(MAST_A):
        ar = math.radians(a)
        rp = MAST_R + 0.55
        piv = b2e(rp * math.cos(ar), rp * math.sin(ar), SPOT_Z)
        # the four cross over the band and twist: each aims at a point 26 m up, 3 m out on the far side, turned 40 deg
        ao = ar + math.pi + math.radians(40.0)
        far = b2e(3.0 * math.cos(ao), 3.0 * math.sin(ao), 26.0)
        d = norm([far[k] - piv[k] for k in range(3)])
        el = math.degrees(math.asin(d[1]))
        yaw = math.degrees(math.atan2(-d[2], d[0]))       # the cloud beam's yaw: 0 = +x, 90 = -z
        sky.append({"name": "band_sky%d" % i, "origin": R3(piv), "yaw_deg": round(yaw, 2), "elevation_deg": round(el, 2),
                    "direction": R3(d), "length_m": 170.0, "half_angle_deg": 2.2, "key": ("sign_a", "sign_b")[i % 2],
                    "intensity": 1.0, "count": 1, "rpm": 0.0})
        heads.append((None, piv, d, a, ("sign_a", "sign_b")[i % 2]))
    blk = {
        "msg": 149,
        "note": "it12 (owner msg 149): the band on a round stage where the garden's pool was. The stage, terrace and "
                "fixtures are kit instances (zone 'band'); this block is what the engine places and drives: the two "
                "astronaut musicians (gitignored GLBs from the main checkout, never committed; each with its props on "
                "one transform), the floor spots as spot lights, the halo's four searchlights as cloud beams, the "
                "audio intent.",
        "stage": {"centre": R3(C), "yaw_deg": YAW, "radius_m": 6.0, "deck_y": round(DECK_TOP, 3),
                  "riser": {"centre": R3(b2e(0.0, 1.5, 1.3)), "radius_m": 1.75}, "halo_y": round(C[1] + HALO_Z, 2),
                  "front_dir": R3(norm([math.sin(math.radians(YAW)), 0.0, math.cos(math.radians(YAW))]))},
        "musicians": mus,
        "spots": spots,
        "sky_beams": sky,
        "audio": {
            "drums": "the drummer's clip on the song's beat (171.4 BPM); the Drums clip is 4.7 s",
            "keys": "the pianist on the Moog/pad phrases",
            "floor_spots": "kick: +60 % 15/250 ms; the four colours swap on each crash (a quarter turn of the key order)",
            "sky_beams": "sway +-12 deg in yaw over two bars, whip with the lighthouses on the crashes; snare: +40 % 10/180 ms",
            "halo": "the halo's lanterns chase round the ring on the hats (the kit's lantern role already chases on hh)",
            "stage_ring": "the inlaid ring (strip) on the bass; the footlights on the toms",
            "held_breath": "45.5-47.15 s: the floor spots dim to 30 %, the sky beams hold still; all back on the 47.15 s crash"},
    }
    return blk, heads


# ------------------------------------------------------------------------------------------------ the plaza cleared
CLEAR_R = 16.6          # trees, lamps, planting and figures inside this of the stage's centre go
CLEAR_EXTRA = [(130.0, 30.0, 3.0)]     # the tree on the lens's line in (x, z, r)
CLEAR_ASSETS = ("garden/umbel_tree", "garden/tree_pit", "street/street_lamp", "garden/shrub", "garden/sprig_bed",
                "garden/planter_round", "decks/pool_15m")


def cleared(p):
    if math.hypot(p[0] - C[0], p[2] - C[2]) < CLEAR_R:
        return True
    return any(math.hypot(p[0] - x, p[2] - z) < r for x, z, r in CLEAR_EXTRA)


# ------------------------------------------------------------------------------------------------ the camera
# Into the Quay (31 s) and the close pass (35.75 s) as before; then faster to the Steps (it was 12 m/s, now up to 22),
# so it can slow to 7-8 m/s low over the plaza from 44.4 to 46.25 s with the band ahead-right at 14-24 m; up into
# the Veil at 47.0 s and the reveal (56.88 / 58.95 s) as before.
CAM_BAND = [
    (36.85, [-15, 136.25, -7.0]),
    (37.75, [0, 135.5, -4.11]),
    (38.45, [15, 134.75, -0.3]),
    (39.05, [28, 134.1, 3.03]),
    (39.6, [40, 133.5, 5.59]),
    (40.3, [55, 133.05, 7.46]),
    (41.0, [70, 132.6, 7.46]),
    (41.75, [85, 130.9, 13.2]),
    (42.5, [98.5, 129.0, 19.0]),
    (43.25, [109.0, 127.7, 23.4]),
    (44.05, [118.5, 126.9, 27.6]),
    (44.95, [127.1, 126.5, 30.15]),
    (45.75, [132.9, 126.3, 31.9]),
    (46.3, [136.5, 126.4, 32.4]),
    (46.75, [145.5, 129.2, 26.8]),
]
CAM_VEIL = (47.6, [173.6, 142.6, 25.8])  # on it11's line into the Veil
                                            # gently past the band (11 m off) instead of climbing 13 m straight up
LOOK_AHEAD = [(0.0, 45.0), (42.6, 45.0), (43.6, 12.0), (44.8, 6.0), (45.3, 3.5), (46.3, 3.5), (46.9, 14.0), (47.7, 45.0), (70.2, 45.0)]


# ------------------------------------------------------------------------------------------------ the audience
CASTS = ["alien-scout", "alien-pilot", "alien-ranger", "alien-trooper", "alien-elder", "alien-diver"]


def audience(rnd, lay):
    spots, groups = [], []
    blockers = []      # (x, z, r) in the engine: fixtures, masts
    for phi in FIX_PHI:
        p = polar(phi, FIX_R); blockers.append((p[0], p[2], 1.0))
    for a in MAST_A:
        p = polar(a, MAST_R); blockers.append((p[0], p[2], 0.8))
    for i in lay["instances"]:   # whatever still stands on the plaza (keel lanterns hang under it: not blockers)
        if i["zone"] in ("garden", "plaza") and i["asset"].startswith(("garden/", "street/street_lamp", "garden/pavilion")):
            if math.hypot(i["pos"][0] - C[0], i["pos"][2] - C[2]) < 24:
                blockers.append((i["pos"][0], i["pos"][2], 1.6 if "umbel" in i["asset"] or "pit" in i["asset"] else 0.9))
    placed = []

    def free(p, r=0.55):
        if any(math.hypot(p[0] - x, p[2] - z) < rr + r for x, z, rr in blockers):
            return False
        return all(math.hypot(p[0] - q[0], p[2] - q[2]) >= 2 * r for q in placed)

    def add(p, kind, phi, r):
        face = yaw_to(p, [C[0], p[1], C[2]]) + rnd.uniform(-14, 14)
        cast = rnd.choice(CASTS)
        if kind == "dance":
            clips = ["Crazy", "Fight_idle"]
        elif kind == "bounce":
            clips = ["Fight_idle", "Idle"]
        else:
            clips = ["Idle", "Idle_turn"] if rnd.random() < 0.8 else ["Idle_turn", "Idle"]
        sid = "band_%03d" % len(spots)
        f = [math.sin(math.radians(face)), 0.0, math.cos(math.radians(face))]
        spots.append({"id": sid, "activity": "watch_band" if kind != "dance" else "dance", "pos": R3(p),
                      "facing_deg": round(face, 1), "forward": R3(f), "district": "band", "cast": cast, "clips": clips,
                      "tier": "A", "react": ["sway"], "look_at": R3([C[0], DECK_TOP + 1.3, C[2]]), "zone": "band",
                      "source": "it12_band", "note": "msg 149: the band's audience (%s, r %.1f m)" % (kind, r)})
        placed.append(p)
        return sid

    # the plaza: four loose rows round the front (the terrace's arc, behind the band, is left to the tiers)
    rows = [9.7, 10.8, 11.9, 13.0, 14.1, 15.3]
    fill = [1.0, 0.95, 0.9, 0.8, 0.6, 0.4]
    for ri, r in enumerate(rows):
        a0, a1 = TERR["a1"] + 9.0, 360.0 + TERR["a0"] - 9.0
        n = int((a1 - a0) / 360.0 * 2 * math.pi * r / 1.1)
        for k in range(n):
            if rnd.random() > fill[ri]:
                continue
            phi = a0 + (a1 - a0) * (k + 0.5 + (0.5 if ri % 2 else 0.0) * 0.5) / n + rnd.uniform(-1.5, 1.5)
            rr = r + rnd.uniform(-0.25, 0.25)
            p = polar(phi, rr)
            if not free(p):
                continue
            front = abs(((phi + 90.0 + 180.0) % 360.0) - 180.0) < 70.0
            kind = "dance" if (ri == 0 and front and rnd.random() < 0.35) else "bounce" if rnd.random() < 0.14 else "watch"
            add(p, kind, phi, rr)
    # the terrace: a standing crowd on each tier, raised so the lens sees it over the band
    for k in range(TERR["tiers"]):
        r = TERR["r0"] + k * TERR["depth"] + TERR["depth"] * 0.5
        z = (k + 1) * TERR["rise"]
        a0, a1 = TERR["a0"] + 3.0, TERR["a1"] - 3.0
        n = int((a1 - a0) / 360.0 * 2 * math.pi * r / 1.2)
        for j in range(n):
            if rnd.random() > (0.82, 0.72, 0.6)[k]:
                continue
            phi = a0 + (a1 - a0) * (j + 0.5) / n + rnd.uniform(-1.2, 1.2)
            p = polar(phi, r + rnd.uniform(-0.18, 0.18), z)
            if not free(p, 0.5):
                continue
            add(p, "bounce" if rnd.random() < 0.18 else "watch", phi, r)
    return spots, groups


# ------------------------------------------------------------------------------------------------ the flights
def flight(fid, cast, A, B, t_lift, t_touch, cruise, route, note, bob_a=None, bob_b=None, da="", db=""):
    """A flyer record in nd_population's shape: Idle at A, the jump (feet leave 0.567 s in, on a kick), the route
    (via keys), a vertical descent onto B, touching down on a kick."""
    lift = snap(t_lift)
    t0 = round(lift - 0.567, 3)
    td = snap(t_touch)
    L = sum(math.dist(route[i], route[i + 1]) for i in range(len(route) - 1))
    tc0, tc1 = t0 + 2.4, td - 2.6
    keys = [{"t": round(t0 - 1.2, 3), "pos": R3(A), "clip": "Idle"}, {"t": t0, "pos": R3(A), "clip": "Jumping"},
            {"t": round(t0 + 0.8, 3), "pos": R3([A[0], A[1] + 0.6, A[2]]), "clip": "Floating"}]
    acc = 0.0
    for i, p in enumerate(route):
        if i:
            acc += math.dist(route[i - 1], p)
        keys.append({"t": round(tc0 + (tc1 - tc0) * acc / L, 3), "pos": R3(p), "clip": "Flying_jet"})
    keys.append({"t": round(tc1, 3), "pos": R3([B[0], B[1] + 6.0, B[2]]), "clip": "Floating"})
    keys.append({"t": td, "pos": R3([B[0], B[1] + 0.4, B[2]]), "clip": "Landing"})
    keys.append({"t": round(td + 0.9, 3), "pos": R3(B), "clip": "Idle"})
    y0 = round(yaw_to(A, route[0]), 1); y1 = round(yaw_to(route[-1], B), 1)
    for k in keys:
        k["yaw_deg"] = y0 if k["t"] <= tc0 else y1
    phases, last = [], None
    for k in keys:
        if k["clip"] != last:
            phases.append({"t": k["t"], "clip": k["clip"]}); last = k["clip"]
    return {"id": fid, "cast": cast, "note": note, "t_start": keys[0]["t"], "t_takeoff": t0, "t_land": td,
            "duration_s": round(keys[-1]["t"] - keys[0]["t"], 2),
            "on_beat": {"liftoff_kick_s": lift, "touchdown_kick_s": td, "cruise_stretch": 1.0},
            "takeoff": {"pos": R3(A), "district": da, "bob": bob_a}, "landing": {"pos": R3(B), "district": db, "bob": bob_b},
            "cruise_height_m": round(cruise, 2), "speed_mps": round(L / max(tc1 - tc0, 0.1), 2), "camera_min_dist_m": None,
            "phases": phases, "keys": keys, "it": 12, "msg": 149}


def lerp(a, b, u):
    return [a[k] + (b[k] - a[k]) * u for k in range(3)]


def flights(pop, lay):
    sp = {s["id"]: s for s in pop["spots"]}
    out = []
    # 1. in from the east isle's lantern spire, down onto the terrace's top tier behind the band (seen ahead, 42-44.5 s)
    A = sp["pop_0396"]["pos"]                                   # isle_east's deck (rides isle_east)
    B = polar(118.0, TERR["r0"] + 2.5 * TERR["depth"], 3 * TERR["rise"])
    top = 150.0
    rt = [lerp(A, B, 0.15), lerp(A, B, 0.5), lerp(A, B, 0.85)]
    rt = [[p[0], top, p[2]] for p in rt]
    out.append(flight("fly_band_in_east", "alien-ranger", A, B, 36.6, 44.37, top, rt,
                      "it12 (msg 149): from the east isle's spire down onto the band's terrace, ahead of the lens as it slows",
                      bob_a="isle_east", da="isle:isle_east", db="band"))
    # 2. out of the crowd: lifts off the plaza's south side as the lens arrives, climbs over the band and away east
    A = polar(-32.0, 15.6)
    B = sp["pop_0402"]["pos"]                                   # isle_exit's deck (rides isle_exit)
    rt = [lerp(A, B, 0.2), lerp(A, B, 0.55), lerp(A, B, 0.9)]
    rt = [[rt[0][0], 140.0, rt[0][2]], [rt[1][0], 146.0, rt[1][2]], [rt[2][0], 142.0, rt[2][2]]]
    out.append(flight("fly_band_out", "alien-pilot", A, B, 43.65, 53.0, 146.0, rt,
                      "it12 (msg 149): a pilot jets out of the audience as the lens slows into the plaza (43.6 s) and "
                      "climbs away to the exit isle's mast", bob_b="isle_exit", da="band", db="isle:isle_exit"))
    # 3. in from the hanging-garden isle south of the Steps, across the frame right to left, down at the crowd's back
    A = sp["pop_0420"]["pos"]
    B = polar(-140.0, 16.0)
    rt = [lerp(A, B, 0.2), lerp(A, B, 0.55), lerp(A, B, 0.85)]
    rt = [[p[0], 133.0, p[2]] for p in rt]
    out.append(flight("fly_band_in_south", "alien-pilot", A, B, 38.0, 45.78, 133.0, rt,
                      "it12 (msg 149): from the hanging-garden isle across the lens's view (right to left) and down at "
                      "the back of the crowd", bob_a="isle_south2", da="isle:isle_south2", db="band"))
    # 4. a Quay roof to the band: leads the lens down the Quay from the close pass and lands at the crowd's west edge
    A = [30.4, 134.51, 14.76]                                   # a roof on the Quay's south side (open sky: --open-near)
    B = polar(-175.0, 15.2)
    rt = [[40.0, 146.0, 14.0], [62.0, 146.0, 15.0], [90.0, 140.0, 22.0], [B[0] - 8.0, 132.0, B[2] - 2.0]]
    out.append(flight("fly_quay_band", "alien-ranger", A, B, 33.55, 43.62, 147.0, rt,
                      "it12 (msg 149): off a Quay roof garden on the snare before the close pass, ahead of the lens down "
                      "the Quay and over the Steps to the band", da="roof", db="band"))
    # 5. the pier: off the inner2 isle as the lens comes up the pier, along it ahead, down on the deck near the root
    A = sp["pop_0408"]["pos"]
    O, D, N = (-317.0, -164.0), (0.8216, 0.5698), (0.5698, -0.8216)

    def pier(s, n, z):
        x, y = O[0] + s * D[0] + n * N[0], O[1] + s * D[1] + n * N[1]
        return [x, z, -y]
    B = pier(222.0, -1.2, 131.2)
    rt = [pier(176.0, -9.0, 140.0), pier(196.0, -6.0, 141.0), pier(214.0, -3.0, 140.0)]
    out.append(flight("fly_pier_root", "alien-ranger", A, B, 16.7, 26.74, 141.0, rt,
                      "it12 (msg 149): off the inner2 isle ahead-right of the pier run, along the pier's north-west "
                      "side and down near its root before the lens passes", bob_a="isle_inner2", da="isle:isle_inner2",
                      db="pier"))
    # 6. the end: off the Lone Lamp isle's deck, out over the cloud vortex, toward the raised Break Light
    return out


# ------------------------------------------------------------------------------------------------ the end shot
BREAK_LIGHT_DY = 95.0          # the small lighthouse isle in the break, up (owner's arrow at 62 s)
LONE = "lone_lamp"


def near_break(x):
    """the figures on the Break Light's isle (some carry no bob of their own: the ground check's fix gave them one)"""
    return math.hypot(x["pos"][0] - 1500.0, x["pos"][2] - 70.0) < 45.0 and x["pos"][1] < 140.0 or x.get("bob") == "break_light"


def lone_lamp_lights(lay):
    """msg 149: 'needs better lighting - too dark'. Warm deck lamps, a lamp over the door, garden glow, and the ocean's
    light on the rock from below (rim/under light)."""
    isl = next(i for i in lay["islands"] if i.get("bob") == LONE)
    cx, cy, cz = isl["centre"]
    house = next(i for i in lay["instances"] if i["id"] == "lone_lamp_house")
    hx, hy, hz = house["pos"]
    inst, lights = [], []
    deck_y = hy
    lamps = [(-9.5, -9.0), (-12.0, -3.5), (-6.5, -13.0), (7.0, 6.0), (4.0, -10.5)]
    for k, (dx, dz) in enumerate(lamps):
        p = [hx + dx, deck_y, hz + dz]
        inst.append({"id": "it12_lone_lamp_post%d" % k, "asset": "street/street_lamp.glb", "pos": R3(p), "rot_y_deg": 0.0,
                     "quat": [0.0, 0.0, 0.0, 1.0], "scale": [0.55, 0.55, 0.55], "zone": "lone_lamp", "bob": LONE})
        lights.append({"name": "it12_lone_deck%d" % k, "pos": R3([p[0] + 0.3, deck_y + 2.2, p[2]]), "key": "amber",
                       "radius_m": 9.0, "intensity": 1.4, "zone": "lone_lamp", "bob": LONE})
    lights.append({"name": "it12_lone_door", "pos": R3([hx, deck_y + 3.0, hz + 4.6]), "key": "amber", "radius_m": 14.0,
                   "intensity": 1.6, "zone": "lone_lamp", "bob": LONE})
    lights.append({"name": "it12_lone_garden", "pos": R3([hx - 8.0, deck_y + 1.2, hz - 6.0]), "key": "garden_a",
                   "radius_m": 12.0, "intensity": 1.2, "zone": "lone_lamp", "bob": LONE})
    lights.append({"name": "it12_lone_roof", "pos": R3([hx, deck_y + 12.0, hz]), "key": "amber", "radius_m": 18.0,
                   "intensity": 1.0, "zone": "lone_lamp", "bob": LONE})
    # the ocean's light from below: violet, wide, under the rock
    lights.append({"name": "it12_lone_under", "pos": R3([cx + 6.0, cy - 30.0, cz + 4.0]), "key": "ocean_a",
                   "radius_m": 40.0, "intensity": 1.6, "zone": "lone_lamp", "bob": LONE})
    lights.append({"name": "it12_lone_rim", "pos": R3([cx + 22.0, cy - 12.0, cz + 10.0]), "key": "ocean_b",
                   "radius_m": 30.0, "intensity": 1.3, "zone": "lone_lamp", "bob": LONE})
    # garden on the deck: shrubs and sprigs (garden role) round the house
    for k, (dx, dz, a) in enumerate([(-11.0, -6.0, "garden/shrub_a.glb"), (-8.0, -11.5, "garden/sprig_bed_3m.glb"),
                                     (6.0, 8.0, "garden/shrub_b.glb"), (-13.0, 0.5, "garden/sprig_bed_3m.glb")]):
        inst.append({"id": "it12_lone_garden%d" % k, "asset": a, "pos": R3([hx + dx, deck_y, hz + dz]), "rot_y_deg": 30.0 * k,
                     "quat": quat_yaw(30.0 * k), "scale": [1.0, 1.0, 1.0], "zone": "lone_lamp", "bob": LONE})
    return inst, lights


def end_clouds(des):
    """More layers at the end (59-70 s): torn stratus shelves below and beside the lens's drift, a cloud bank between
    the isles, collars round the isles. Lobes tagged it12_end (no bob: the sea's shelves do not float)."""
    rnd = random.Random(1201)
    lobes = []
    # (centre x, y, z), (spread x, z), n, radius, scale y, density, soft
    layers = [((760, 196, -30), (90, 70), 14, 30, 0.22, 0.55, 0.7),     # under the lens's drift, low and torn
              ((830, 205, 10), (70, 60), 10, 26, 0.2, 0.5, 0.75),       # right of the drift, under the Lone Lamp's view
              ((1000, 170, -120), (160, 90), 14, 44, 0.25, 0.6, 0.7),   # between the isles, left of the Lone Lamp
              ((1150, 150, 40), (180, 110), 14, 48, 0.22, 0.55, 0.72),  # the mid-distance layer over the vortex's near rim
              ((1350, 130, -60), (200, 120), 12, 55, 0.2, 0.5, 0.75)]   # the far layer, toward the works
    for (cx, cy, cz), (sx, sz), n, r, syy, dens, soft in layers:
        for k in range(n):
            lobes.append({"c": R3([cx + rnd.uniform(-sx, sx), cy + rnd.uniform(-6, 6), cz + rnd.uniform(-sz, sz)]),
                          "r": round(r * rnd.uniform(0.7, 1.2), 1), "scale": [round(rnd.uniform(1.4, 2.4), 2), syy, round(rnd.uniform(1.2, 2.0), 2)],
                          "density": dens, "soft": soft, "tag": "it12_end"})
    return lobes


def vortex(des):
    """The ocean break as a cloud vortex (msg 149, owner-requested): a funnel of cloud shelves stepping down into the
    glowing ocean. Parameters for the engine's renderer, plus the shelves as lobes that carry their orbit (centre and
    degrees a second, faster inward: a Keplerian shear, so the spiral winds as it turns)."""
    br = des["sea_design"]["break"]
    cx, cz = br["centre"]
    ya = br["yaw_rad"]
    rim = [br["radius"] * br["elongation"], br["radius"]]        # semi-axes along the break's yaw (x) and across (z)
    top, floor = des["sea_design"]["top_mean"] + 18.0, br["ocean_plane"]["y"] + 6.0
    shelves = 7
    params = {
        "centre": [cx, cz], "yaw_rad": ya, "rim_semi_axes": rim, "throat_semi_axes": [round(rim[0] * 0.34, 1), round(rim[1] * 0.34, 1)],
        "top_y": round(top, 1), "floor_y": round(floor, 1), "depth_m": round(top - floor, 1),
        "shelves": shelves, "shelf_thickness_m": 16.0, "shelf_overhang_m": 30.0,
        "rotation_dps_rim": 1.2, "shear_exponent": 1.5, "direction": "counter-clockwise seen from above",
        "arms": 3, "arm_pitch_deg": 16.0, "arm_contrast": 0.55,
        "rim_softness_m": 60.0,
        "underlight": {"key": "ocean_a", "rim_gain": 0.15, "throat_gain": 1.6, "falloff": "with depth below top_y, squared"},
        "eye_glow": {"position": [cx, round(floor + 10.0, 1), cz], "radius_m": 240.0, "key": "ocean_b", "intensity": 2.5},
        "audio": "the pad's phrases: the throat's light swells (900 ms in, 2.4 s out); the rotation never changes speed",
        "note": "it12 (msg 149, owner-requested; overrides the no-vortex rule for this one place). The shelves are also "
                "given as lobes (masses.vortex, tag 'vortex', each with 'orbit': {centre, dps}) so the shape stands "
                "without the renderer; spin them about centre at dps (seek-safe: angle = dps * t). The rim shelf "
                "overhangs the break's cut edge (msg 149's 59.6 s 'too CG' edge).",
    }
    rnd = random.Random(1203)
    lobes = []
    cy_, sy_ = math.cos(ya), math.sin(ya)
    for s in range(shelves):
        u = s / (shelves - 1)                                   # 0 rim .. 1 throat
        k = 1.06 - 0.72 * u                                     # semi-axis scale of the shelf's middle
        y = top - (top - floor) * (u ** 0.85) - 6.0
        dps = params["rotation_dps_rim"] * (1.0 / k) ** params["shear_exponent"]
        n = int(46 * k) + 10
        for j in range(n):
            a = 2 * math.pi * j / n + rnd.uniform(-0.04, 0.04)
            arm = 0.5 + 0.5 * math.cos(params["arms"] * (a + math.radians(params["arm_pitch_deg"]) * 8.0 * u))
            ex, ez = rim[0] * k * math.cos(a), rim[1] * k * math.sin(a)
            x, z = cx + ex * cy_ - ez * sy_, cz + ex * sy_ + ez * cy_
            r = (30.0 + 26.0 * (1 - u)) * (0.75 + 0.45 * arm) * rnd.uniform(0.85, 1.15)
            lobes.append({"c": R3([x, y + rnd.uniform(-4, 4), z]), "r": round(r, 1),
                          "scale": [round(rnd.uniform(1.6, 2.4), 2), round(0.26 + 0.1 * arm, 2), round(rnd.uniform(1.0, 1.4), 2)],
                          "density": round(0.5 + 0.4 * arm, 2), "soft": 0.62, "tag": "vortex",
                          "orbit": {"centre": [cx, cz], "dps": round(dps, 3)}})
    return params, lobes


def end_wisps():
    return [{"name": "band_plaza", "centre": [C[0], C[2]], "radii": [24.0, 22.0], "yaw": 0.0, "floor": 121.6, "below": 4.0,
             "above": 3.5, "density": 0.55},
            {"name": "end_lone_lamp", "centre": [890.0, -49.0], "radii": [46.0, 40.0], "yaw": 0.0, "floor": 206.0,
             "below": 10.0, "above": 5.0, "density": 0.7, "bob": LONE},
            {"name": "end_drift", "centre": [800.0, -40.0], "radii": [120.0, 70.0], "yaw": -0.1, "floor": 205.0,
             "below": 8.0, "above": 4.0, "density": 0.5}]


# ------------------------------------------------------------------------------------------------ the pylons
def pylon_legs(lay):
    out = []
    for i in lay["instances"]:
        if i["asset"] == "street/gondola_pylon.glb":
            p = i["pos"]
            out.append({"id": "it12_leg_" + i["id"], "asset": "stage/pylon_leg_ext.glb", "pos": R3([p[0], p[1] - 12.0, p[2]]),
                        "rot_y_deg": i["rot_y_deg"], "quat": i["quat"], "scale": [1.0, 1.0, 1.0], "zone": i["zone"],
                        **({"bob": i["bob"]} if i.get("bob") else {})})
    return out


# ------------------------------------------------------------------------------------------------ main
def main():
    lay = json.load(open(os.path.join(ART, "layout.json")))
    des = json.load(open(os.path.join(ART, "cloud_design.json")))
    pop = json.load(open(os.path.join(ART, "population.json")))
    rec = next((e for e in pop.get("edits", []) if e.get("it") == 12), None)
    if rec is None:
        rec = {"it": 12, "msg": 149, "by": "art", "script": "blender/nd_band.py", "orig": {
            "waypoints": lay["camera"]["waypoints"], "inst": [], "lights": [], "spots": [], "paths": [], "groups": [],
            "break_light": {"inst": {i["id"]: i["pos"] for i in lay["instances"] if i.get("bob") == "break_light"},
                            "lights": {l["name"]: l["pos"] for l in lay["lights"] if l.get("bob") == "break_light"},
                            "islands": {str(k): il["centre"] for k, il in enumerate(lay["islands"]) if il.get("bob") == "break_light"},
                            "beams": {b["name"]: b["origin"] for b in lay["beams"] if b["name"] == "break_light"},
                            "spots": {x["id"]: x["pos"] for x in pop["spots"] if near_break(x)},
                            "lobes": {"%s/%d" % (m, k): lb["c"] for m, ls in des["masses"].items() if isinstance(ls, list)
                                      for k, lb in enumerate(ls) if isinstance(lb, dict) and lb.get("bob") == "break_light"}}}}
        pop.setdefault("edits", []).append(rec)
    orig = rec["orig"]
    # ---- restore what an earlier run removed or moved, and drop what it added
    ids = {i["id"] for i in lay["instances"]}
    lay["instances"] += [i for i in orig["inst"] if i["id"] not in ids]
    names = {l["name"] for l in lay["lights"]}
    lay["lights"] += [l for l in orig["lights"] if l["name"] not in names]
    sids = {s["id"] for s in pop["spots"]}
    pop["spots"] += [s for s in orig["spots"] if s["id"] not in sids]
    pids = {p["id"] for p in pop["paths"]}
    pop["paths"] += [p for p in orig["paths"] if p["id"] not in pids]
    gids = {g["id"] for g in pop["groups"]}
    pop["groups"] += [g for g in orig["groups"] if g["id"] not in gids]
    lay["instances"] = [i for i in lay["instances"] if not i["id"].startswith(("it12_", "band_"))]
    lay["lights"] = [l for l in lay["lights"] if not l["name"].startswith(("it12_", "band_"))]
    pop["spots"] = [s for s in pop["spots"] if not s["id"].startswith("band_")]
    pop["groups"] = [g for g in pop["groups"] if not g["id"].startswith("grp_band")]
    pop["paths"] = [p for p in pop["paths"] if not p["id"].startswith("walk_band")]
    pop["flyers"] = [f for f in pop["flyers"] if f.get("it") != 12]
    for name in list(des["masses"]):
        if isinstance(des["masses"][name], list):
            des["masses"][name] = [lb for lb in des["masses"][name] if lb.get("tag") not in ("it12_end", "vortex")]
    des["masses"].pop("vortex", None); des["masses"].pop("end_layers", None)
    des["wisp_regions"] = [w for w in des["wisp_regions"] if not w["name"].startswith(("band_", "end_"))]
    bl = orig["break_light"]
    for i in lay["instances"]:
        if i["id"] in bl["inst"]:
            i["pos"] = bl["inst"][i["id"]]
    for l in lay["lights"]:
        if l["name"] in bl["lights"]:
            l["pos"] = bl["lights"][l["name"]]
    for k, il in enumerate(lay["islands"]):
        if str(k) in bl["islands"]:
            il["centre"] = bl["islands"][str(k)]
    for b in lay["beams"]:
        if b["name"] in bl["beams"]:
            b["origin"] = bl["beams"][b["name"]]
    for x in pop["spots"]:
        if x["id"] in bl.get("spots", {}):
            x["pos"] = bl["spots"][x["id"]]
    for m, ls in des["masses"].items():
        if isinstance(ls, list):
            for k, lb in enumerate(ls):
                if "%s/%d" % (m, k) in bl["lobes"]:
                    lb["c"] = bl["lobes"]["%s/%d" % (m, k)]

    # ---- A. the plaza cleared, the stage built
    gone_i = [i for i in lay["instances"] if i["asset"].startswith(CLEAR_ASSETS) and cleared(i["pos"]) and i["zone"] == "garden"]
    gone_l = [l for l in lay["lights"] if l.get("zone") == "garden" and cleared(l["pos"]) and l["pos"][1] > 125.0]
    gone_s = [s for s in pop["spots"] if s.get("district") == "garden" and math.hypot(s["pos"][0] - C[0], s["pos"][2] - C[2]) < 18.0]
    gone_p = [p for p in pop["paths"] if p.get("district") == "garden" and any(
        math.hypot(q[0] - C[0], q[2] - C[2]) < 17.0 for a, b in zip(p["points"], p["points"][1:])
        for q in (lerp(a, b, u / 10.0) for u in range(11)))]
    gone_ids = {s["id"] for s in gone_s}
    gone_g = [g for g in pop["groups"] if any(m in gone_ids for m in g["members"])]
    for lst, key, new in ((orig["inst"], "id", gone_i), (orig["lights"], "name", gone_l), (orig["spots"], "id", gone_s),
                          (orig["paths"], "id", gone_p), (orig["groups"], "id", gone_g)):
        have = {x[key] for x in lst}
        lst += [x for x in new if x[key] not in have]
    rm = lambda lst, gone, key: [x for x in lst if x[key] not in {g[key] for g in gone}]
    lay["instances"] = rm(lay["instances"], gone_i, "id")
    lay["lights"] = rm(lay["lights"], gone_l, "name")
    pop["spots"] = rm(pop["spots"], gone_s, "id")
    pop["paths"] = rm(pop["paths"], gone_p, "id")
    pop["groups"] = rm(pop["groups"], gone_g, "id")
    for g in pop["groups"]:
        g["members"] = [m for m in g["members"] if m not in gone_ids]

    blk, heads = band_block()
    q = quat_yaw(YAW)
    lay["instances"].append({"id": "band_stage", "asset": "stage/band_stage.glb", "pos": R3(C), "rot_y_deg": YAW, "quat": q,
                             "scale": [1, 1, 1], "zone": "band", "tint": {"strip": "sign_a", "lantern": "amber"}})
    lay["instances"].append({"id": "band_terrace", "asset": "stage/band_terrace.glb", "pos": R3(C), "rot_y_deg": YAW, "quat": q,
                             "scale": [1, 1, 1], "zone": "band", "tint": {"lantern": "amber"}})
    for k, (base, piv, d, phi, key) in enumerate(heads):
        if base is not None:
            by = yaw_to(base, [base[0] + d[0], base[1], base[2] + d[2]])
            lay["instances"].append({"id": "band_spotbase%d" % k, "asset": "stage/spot_base.glb", "pos": R3(base),
                                     "rot_y_deg": round(by, 2), "quat": quat_yaw(by), "scale": [1, 1, 1], "zone": "band"})
        lay["instances"].append({"id": "band_spothead%d" % k, "asset": "stage/spot_head.glb", "pos": R3(piv),
                                 "rot_y_deg": 0.0, "quat": quat_aim(d), "scale": [1.0, 1.0, 1.0] if base is not None else [1.7, 1.7, 1.7],
                                 "zone": "band", "tint": {"beam_lamp": key}})
    # the halo's lanterns and the stage ring cast a little light of their own (the spot lights are the engine's)
    for k, a in enumerate((-90.0, 0.0, 90.0, 180.0)):
        lay["lights"].append({"name": "band_halo%d" % k, "pos": R3(polar(a, 5.2, HALO_Z - 1.6)), "key": "amber",
                              "radius_m": 9.0, "intensity": 1.1, "zone": "band"})
    lay["lights"].append({"name": "band_wash", "pos": R3(b2e(0.0, -3.5, 3.2)), "key": "sign_a", "radius_m": 11.0,
                          "intensity": 1.0, "zone": "band"})
    lay["band"] = blk

    # ---- the audience and the flights
    rnd = random.Random(149)
    sp, gr = audience(rnd, lay)
    pop["spots"] += sp; pop["groups"] += gr
    fl = flights(pop, lay)
    pop["flyers"] += fl

    # ---- B. the camera
    keep = [w for w in orig["waypoints"] if w["t"] <= 35.75 or w["t"] > 47.0]
    lay["camera"]["waypoints"] = sorted(keep + [{"t": t, "pos": p} for t, p in CAM_BAND + ([CAM_VEIL] if CAM_VEIL else [])], key=lambda w: w["t"])
    lay["camera"]["look_ahead"] = [[t, v] for t, v in LOOK_AHEAD]
    lay["camera"]["note"] = ("the 70.2 s journey; it12 (owner msg 149): from the Quay's close pass (35.75 s) faster down "
                             "the Quay, then slow (7-8 m/s) and low over the band's plaza 44.4-46.25 s with the band ahead-"
                             "right at 14-24 m, up into the Veil at 47.0 s as before; `look_ahead` (m along the path, keyed) "
                             "shortens to 16 m over the plaza so the lens keeps looking where it travels, not up into the "
                             "Veil; it11's opening and end as before")

    # ---- D. the pylons' legs into the cloud
    lay["instances"] += pylon_legs(lay)

    # ---- E. the end
    dy = BREAK_LIGHT_DY
    for i in lay["instances"]:
        if i.get("bob") == "break_light":
            i["pos"] = R3([i["pos"][0], i["pos"][1] + dy, i["pos"][2]])
    for l in lay["lights"]:
        if l.get("bob") == "break_light":
            l["pos"] = R3([l["pos"][0], l["pos"][1] + dy, l["pos"][2]])
    for il in lay["islands"]:
        if il.get("bob") == "break_light":
            il["centre"] = R3([il["centre"][0], il["centre"][1] + dy, il["centre"][2]])
    for b in lay["beams"]:
        if b["name"] == "break_light":
            b["origin"] = R3([b["origin"][0], b["origin"][1] + dy, b["origin"][2]])
    gf = json.load(open(os.path.join(ART, "ground_fixes.json"))).get("spots", {}) if os.path.exists(os.path.join(ART, "ground_fixes.json")) else {}
    for x in pop["spots"]:
        if near_break(x):
            x["bob"] = "break_light"
            fx = gf.get(x["id"])          # the ground check's fix for it (msg 130), taken along: at the new height it would not match
            p0 = fx["pos"] if fx and math.dist(fx.get("from", [0, 0, 0]), x["pos"]) < 0.01 else x["pos"]
            x["pos"] = R3([p0[0], p0[1] + dy, p0[2]])
            if x.get("look_at"):
                x["look_at"] = R3([x["look_at"][0], x["look_at"][1] + dy, x["look_at"][2]])
    for m, ls in des["masses"].items():
        if isinstance(ls, list):
            for lb in ls:
                if isinstance(lb, dict) and lb.get("bob") == "break_light":
                    lb["c"] = R3([lb["c"][0], lb["c"][1] + dy, lb["c"][2]])
    li, ll = lone_lamp_lights(lay)
    lay["instances"] += li; lay["lights"] += ll
    des["masses"]["end_layers"] = end_clouds(des)
    vp, vl = vortex(des)
    des["masses"]["vortex"] = vl
    des["sea_design"]["break"]["vortex"] = vp
    des["wisp_regions"] += end_wisps()

    rec["applied"] = {"removed": {"instances": len(orig["inst"]), "lights": len(orig["lights"]), "spots": len(orig["spots"]),
                                  "paths": len(orig["paths"]), "groups": len(orig["groups"])},
                      "audience": len(sp), "flights": [f["id"] for f in fl], "pylon_legs": sum(1 for i in lay["instances"] if i["id"].startswith("it12_leg_")),
                      "vortex_lobes": len(vl), "end_lobes": len(des["masses"]["end_layers"])}
    json.dump(lay, open(os.path.join(ART, "layout.json"), "w"), indent=1)
    json.dump(des, open(os.path.join(ART, "cloud_design.json"), "w"), indent=None, separators=(",", ":"))
    json.dump(pop, open(os.path.join(ART, "population.json"), "w"), indent=None, separators=(",", ":"))
    print(json.dumps(rec["applied"]))


if __name__ == "__main__":
    main()
