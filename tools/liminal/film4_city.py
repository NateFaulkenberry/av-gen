"""All You Got, art pass 4: the city, owner bars 66-90 (145.32-199.73 s), on Builder4 (04-art-pass-4.md PARTS 8-10, 13).

  66.3-67   the study's ceiling splits and flies (film4_upper) and the house's roof goes with it: the roof that flew
            in during the intro flies back out the way it came
  67-74     LET IT GROW: house -> neighbourhood -> city. One camera rises out of the roofless study, over the house,
            the street, the neighbourhood (as the intro built it), and out over the city as it grows outward: roads
            ring by ring on the GROW beats, street lamps popping on the eighths behind them, the traffic starting to
            flow, the blocks rising a storey a beat, the far towers last; 73-74 (the drums out, the synths ringing)
            the growth is complete and the camera descends towards downtown
  75-82     IS THAT ALL YOU: the city is full of people moving; he does not move.
              75-76  stopped at a red light, alone in his car; the cross traffic streams past, people cross in front
              77-78  alone at the bar while the others talk; GOT? (C17) the bar's lights burst
              79-80  on a park bench; people walk past both ways
              81-82  standing still in the middle of the crossing while the crowd flows round him; 82.3 the pulse stops
                     and the city freezes with it; C18 (82.4) the lights go out
  83-90     the city alive: the camera is the energetic element now -- down an avenue, round a tower, diving at a
            junction, low along a street, then pulled up and up until the whole grid breathes on the beat; 90.3 the
            colour wave runs through the city towards us (ADR-1055) and the cut to the open lands on 91.1

Structures never move after they are built (PART 15): the city grows by clips stepping on the beat and locking.
What keeps moving is alive: the traffic (`traffic*Fwd/Back` translates at a steady speed), the walkers' joints (the
grid's half-note wave: one stride a beat), lights, signage, windows, the colour of the air.
"""

from __future__ import annotations

import math

import city4 as C
import intro4 as IN
import kit as K
import pass2_grid as G
import props4 as P4
import tableaux3 as TB
import tableaux4 as T4
import walkers4 as WK
from film3_house import lyric_glitch

t = G.t
BEAT1 = G.BAR1 / 4.0
BEAT2 = G.BAR2 / 4.0
EIGHTH2 = BEAT2 / 2.0
SETS = ("Low", "Mid", "TowerA", "TowerB", "Sky")
FAR = {"max_distance": 460.0}
CAR_SPEED = 11.0          # m/s, the generic traffic
REG_ALL = ((-420.0, 420.0), (-420.0, 300.0))
HERO_CUT = K.X(((C.XI - 60.0, C.XI + 60.0), (-1.0, 10.0), (C.ZK - 60.0, C.ZK + 60.0)))

# the lead's car, stopped at the line south of the downtown crossing (his lane is northbound: facing -Z)
CROSSWALK_Z = C.ZK + C.CROSS_OFF                                    # the south crossing's centre line (z)
CAR = (C.XI + C.LANE, 0.0, CROSSWALK_Z + 1.0 + 0.5 + 2.15)           # its centre: the nose half a metre behind the crossing
CAR_YAW = 180.0
KERB_W, KERB_E = C.XI - 4.5, C.XI + 4.5                               # the z-road's carriageway edges (x)


def fig_in_car():
    """Where the car_sit figure's frame goes for the car at CAR, CAR_YAW."""
    a = math.radians(CAR_YAW)
    cx, cz = T4.CAR_AT[0], T4.CAR_AT[2]
    ox, oz = cx * math.cos(a) + cz * math.sin(a), -cx * math.sin(a) + cz * math.cos(a)
    return (CAR[0] - ox, 0.0, CAR[2] - oz), CAR_YAW


# =============================================================================================================
# THE WORLDS
# =============================================================================================================

def worlds(b):
    f = b.f
    objs = C.objects()
    b.world("cityOut", {"objects": [o for o in objs if o[0].startswith("cityOut")], "lights": []})
    b.world("cityGround", {"objects": [o for o in objs if o[0] in ("cityKerbs", "cityMarks", "cityLamps")], "lights": []})
    b.world("cityTraffic", {"objects": [
        ("cityTrafX", C.traffic("x", REG_ALL, name="trafX"), "furn", (-420, -0.2, -420), (420, 2.0, 300), dict(FAR, edge_pixels=1.3)),
        ("cityTrafZ", C.traffic("z", REG_ALL, name="trafZ"), "furn", (-420, -0.2, -420), (420, 2.0, 300), dict(FAR, edge_pixels=1.3))],
        "lights": []})
    b.world("cityTraffic2", {"objects": [
        ("cityTrafX2", C.traffic("x", REG_ALL, name="trafX2", cut=HERO_CUT), "furn", (-420, -0.2, -420), (420, 2.0, 300), dict(FAR, edge_pixels=1.3)),
        ("cityTrafZ2", C.traffic("z", REG_ALL, name="trafZ2", cut=HERO_CUT), "furn", (-420, -0.2, -420), (420, 2.0, 300), dict(FAR, edge_pixels=1.3))],
        "lights": []})
    ns, ew = C.signal_heads()
    off = {K.CANVAS: [0.05, 0.0, 0.0], K.CANVAS2: [0.05, 0.03, 0.0], K.SCREEN: [0.0, 0.05, 0.02]}
    b.world("citySet", {"objects": [
        ("cityPark", C.park(), "furn", (C.HX - 15, -0.2, -103), (C.HX + 15, 9, -73), FAR),
        ("cityBenches", C.benches(), "furn2", (C.HX - 8, -0.2, -96), (C.HX + 8, 2, -80), FAR),
        ("cityBar", C.bar_building(), "furn2", (C.HX + 26, -0.2, -102), (C.HX + 55, 14, -74), FAR),
        ("cityCross", C.intersection(), "furn", (C.XI - 9, -0.2, C.ZK - 9), (C.XI + 15, 5, C.ZK + 20), FAR),
        ("citySigNS", ns, "furn", (C.XI - 8, 2.0, C.ZK - 10), (C.XI + 10, 4.5, C.ZK + 10), dict(FAR, emission=dict(off))),
        ("citySigEW", ew, "furn", (C.XI - 10, 2.0, C.ZK - 8), (C.XI + 10, 4.5, C.ZK + 8), dict(FAR, emission=dict(off)))],
        "lights": []})


def set_signals(b, t0, t1, ns="red", ew="green"):
    """Key the two signal groups' lamps for a span: the lit lamp at full strength, the others nearly dark."""
    f = b.f
    lit = {"red": (K.CANVAS, [4.0, 0.15, 0.1]), "amber": (K.CANVAS2, [4.0, 2.0, 0.2]), "green": (K.SCREEN, [0.2, 3.6, 1.0])}
    dark = {K.CANVAS: [0.05, 0.0, 0.0], K.CANVAS2: [0.05, 0.03, 0.0], K.SCREEN: [0.0, 0.05, 0.02]}
    for obj, state in (("citySigNS", ns), ("citySigEW", ew)):
        k_on, rgb = lit[state]
        for k_, d in dark.items():
            v = rgb if k_ == k_on else d
            tgt = f"sdf/{obj}/surface/{k_}/emission"
            if not any(k[0] == 0.0 for k in getattr(b, "_keys", {}).get(tgt, [])):
                b.key(tgt, 0.0, d)
            b.key(tgt, t0, v)
            b.key(tgt, t1, d)


def steady(b, obj, node, t0, t1, vec, speed, base=(0.0, 0.0, 0.0)):
    """A living thing's steady motion: key a named translate linearly from `base` at t0 along `vec` (a unit
    direction) at `speed` m/s until t1 (a replace track; it holds after t1)."""
    d = [v * speed * (t1 - t0) for v in vec]
    b.f.track(f"sdf/{obj}/node/{node}/translation", [(0.0, list(base), "step"), (t0, list(base), "linear"),
                                                     (t1, [a + c for a, c in zip(base, d)], "step")])


# =============================================================================================================
# 66.3: THE ROOF GOES
# =============================================================================================================

def roof_flight(b):
    f = b.f
    b.key("nodes/stuCeil/visible", t(67) - 0.02, 0.0)
    b.key("nodes/heroRoof/visible", t(67) - 0.02, 0.0)
    b.key("nodes/faller/visible", t(3), 0.0)
    by = {p[0]: p for p in IN.HERO_PARTS}
    t_split, t_fly = t(66, 3), t(66, 4)
    for nm_ in ("roofL", "roofR", "chimney"):
        off = list(by[nm_][2])
        far = [v * 4.0 + (40.0 if i == 1 else 0.0) for i, v in enumerate(off)]
        f.track(f"sdf/heroRoof/node/hp_{nm_}/translation", [(t_split, [0.0, 0.0, 0.0], "easeOut"), (t_split + 0.3, [0.0, 0.5, 0.0], "step"),
                                                              (t_fly, [0.0, 0.5, 0.0], "easeIn"), (t(67) - 0.05, far, "step")])


# =============================================================================================================
# 67-74: LET IT GROW
# =============================================================================================================

def grow_keys(b):
    """The outer city's construction, every step on the grid and then locked."""
    f = b.f
    S_ = C.STOREY
    grow_beats = [t(bar, beat) for bar in range(67, 73) for beat in (2.0, 4.0)]          # the GROW beats
    ring_sizes = [70.0 + 32.0 * k for k in range(len(grow_beats))]                      # a block a step, outward
    # the roads first (kerbs and markings on the GROW beat itself), the lamps an eighth later
    b.lock("sdf/cityKerbs/node/kerbsRing/size", [(tb, [r, 5.0, r]) for tb, r in zip(grow_beats, ring_sizes)], [50.0, 5.0, 50.0], ease=0.05)
    b.lock("sdf/cityMarks/node/marksRing/size", [(tb, [r, 5.0, r]) for tb, r in zip(grow_beats, ring_sizes)], [50.0, 5.0, 50.0], ease=0.05)
    b.lock("sdf/cityLamps/node/lightsRing/size", [(tb + BEAT1 / 2, [r, 50.0, r]) for tb, r in zip(grow_beats, ring_sizes)],
           [50.0, 50.0, 50.0], ease=0.03)
    # traffic follows the roads (a quarter behind) and flows from 67.1; bridge 2's traffic is all there
    for obj, node in (("cityTrafX", "trafXRing"), ("cityTrafZ", "trafZRing")):
        b.lock(f"sdf/{obj}/node/{node}/size", [(tb + BEAT1, [r, 20.0, r]) for tb, r in zip(grow_beats, ring_sizes)], [50.0, 20.0, 50.0], ease=0.03)
    for obj, node in (("cityTrafX2", "trafX2Ring"), ("cityTrafZ2", "trafZ2Ring")):
        b.key(f"sdf/{obj}/node/{node}/size", 0.0, [600.0, 20.0, 600.0])
    # the buildings: each set's ring a beat behind the roads, its storeys on the quarters / eighths
    lag = {"Low": BEAT1, "Mid": BEAT1 * 1.5, "TowerA": BEAT1 * 2.0, "TowerB": BEAT1 * 2.0, "Sky": BEAT1 * 2.5}
    rise_start = {"Low": t(67, 3), "Mid": t(68), "TowerA": t(69), "TowerB": t(69, 1.5), "Sky": t(70)}
    rise_step = {"Low": BEAT1, "Mid": BEAT1, "TowerA": BEAT1 / 2, "TowerB": BEAT1 / 2, "Sky": BEAT1 / 2}
    rise_per = {"Low": 1, "Mid": 1, "TowerA": 1, "TowerB": 1, "Sky": 2}
    for nm_ in SETS:
        obj = f"cityOut{nm_}"
        b.lock(f"sdf/{obj}/node/{nm_}Ring/size", [(tb + lag[nm_], [r, 500.0, r]) for tb, r in zip(grow_beats, ring_sizes)],
               [50.0, 500.0, 50.0], ease=0.05)
        h_final = C.SETS[nm_][2]
        n_storeys = int(round(h_final / S_))
        ev = []
        n, k = 0, 0
        while n < n_storeys:
            n = min(n_storeys, n + rise_per[nm_])
            ev.append((rise_start[nm_] + k * rise_step[nm_], [2000.0, C.rise_size(min(n * S_, h_final + 0.6)), 2000.0]))
            k += 1
        b.lock(f"sdf/{obj}/node/{nm_}Rise/size", ev, [2000.0, C.rise_size(0.0), 2000.0], ease=0.06)
        # every step lights the set's lines for an instant: the lock is seen
        for i, (tt, _) in enumerate(ev):
            if i % 2 == 0:
                c = b.clap(f"rise{nm_}{i:02d}", tt + 0.04, release=0.18)
                f.route(c, f"sdf/{obj}/look/edge/intensity", 3.0)
    for i, tb in enumerate(grow_beats):
        c = b.clap(f"grow{i:02d}", tb, release=0.35)
        f.route(c, "sdf/cityKerbs/look/edge/intensity", 6.0)
        f.route(c, "sdf/cityMarks/look/edge/intensity", 6.0)
        f.route(c, "post/bloom/intensity", 0.5)


def traffic_flow(b, t0, t1):
    """The generic traffic streams at a steady speed (living things: PART 15)."""
    for obj, trf, ax in (("cityTrafX", "trafX", 0), ("cityTrafZ", "trafZ", 2), ("cityTrafX2", "trafX2", 0), ("cityTrafZ2", "trafZ2", 2)):
        fwd = [0.0, 0.0, 0.0]
        fwd[ax] = 1.0 if ax == 0 else -1.0
        back = [-v for v in fwd]
        steady(b, obj, f"{trf}Fwd", t0, t1, fwd, CAR_SPEED)
        steady(b, obj, f"{trf}Back", t0, t1, back, CAR_SPEED)


def rise_shot(b):
    f = b.f
    keys = [(t(67), (-0.6, 4.6, -4.4)), (148.6, (-0.62, 8.2, -4.2)), (149.9, (-1.0, 15.5, -1.5)), (t(69), (-0.5, 30.0, 21.0)),
            (t(70), (8.0, 52.0, 47.0)), (t(71), (22.0, 80.0, 70.0)), (t(72), (42.0, 108.0, 60.0)), (t(73), (56.0, 116.0, 18.0)),
            (163.0, (44.0, 72.0, -58.0)), (t(75), (30.0, 34.0, -88.0))]
    look = [(t(67), (-0.6, 12.0, -4.4)), (148.6, (-0.4, 9.0, 4.0)), (149.9, (0.6, 0.5, 3.0)), (t(69), (4.0, 4.0, -40.0)),
            (t(70), (14.0, 9.0, -72.0)), (t(71), (20.0, 18.0, -112.0)), (t(72), (24.0, 28.0, -132.0)), (t(73), (21.0, 16.0, -112.0)),
            (163.0, (21.0, 4.0, -108.0)), (t(75), (21.2, 1.5, -106.0))]
    nodes = ("street", "cityIn", "cityOut", "cityGround", "cityTraffic", "citySet", "bed", "bath", "stu")
    b.glide("rise", keys, look, nodes_keys=nodes, extra=["stars"], fov=66.0)
    f.shots[-1]["fov"] = [(t(67), 76.0), (149.9, 70.0), (t(69), 64.0), (t(73), 60.0), (t(75), 56.0)]


def grow_words(b):
    """FEEL IT GROW written on the city's roads as it grows (the words are its markings, read from the air):
    twice a bar, each word on the stretch of road nearest where the camera looks."""
    f = b.f
    pats = [(-0.4, -0.25), (0.0, -0.45), (0.4, -0.25)]
    for bar in range(67, 73):
        for half, b0 in ((0, 1.0), (1, 3.0)):
            for j, wd in enumerate(("FEEL", "IT", "GROW")):
                tw = t(bar, b0 + 0.5 * j)
                sx, sy = pats[j]
                road_word(b, wd, tw, tw + G.BAR1 * 0.9, sx, sy + 0.08 * half, name=f"grow{bar}{half}{j}")


def road_word(b, text, t0, t1, sx, sy, name):
    """A word on the nearest road where the view ray through (sx, sy) meets the ground: snapped to the road's centre
    line, lying flat, read from the camera, as big as the distance needs."""
    f = b.f
    eye, d, fwd = f.ray(t0 + 0.12, sx, sy)
    if d[1] >= -0.02:
        return None
    s_ = -eye[1] / d[1]
    p = [e + v * s_ for e, v in zip(eye, d)]
    # snap to the nearest road: x-roads at z = 12 + 40k, z-roads at x = XI + 40i
    zk = 12.0 + 40.0 * round((p[2] - 12.0) / 40.0)
    xi = C.XI + 40.0 * round((p[0] - C.XI) / 40.0)
    if abs(p[2] - zk) < abs(p[0] - xi):
        p = [p[0], 0.02, zk]
    else:
        p = [xi, 0.02, p[2]]
    dist = math.dist(eye, p)
    h = min(max(dist * 0.06, 1.2), 4.2)
    e_, fw, rt, upv, fov = f.basis(t0 + 0.12)
    dd = [a - c for a, c in zip(p, e_)]
    z = sum(a * c for a, c in zip(dd, fw))
    ty = math.tan(math.radians(fov / 2))
    if z <= 1.0 or abs(sum(a * c for a, c in zip(dd, rt)) / z) > ty * 16 / 9 * 0.8 or abs(sum(a * c for a, c in zip(dd, upv)) / z) > ty * 0.8:
        p = [e + v * s_ for e, v in zip(eye, d)]        # the snap left the frame: the word stays where the ray met the ground
        p[1] = 0.02
    tilt = f.flat_tilt([0, 1, 0], fwd)
    e = b.word(text, t0, t1, p, [0.0, 1.0, 0.0], h, style="rise", role="word", tilt=tilt, name=name, intensity=4.0, tin=0.18,
               tout=0.3, category="floorText")
    return e


# =============================================================================================================
# 75-82: IS THAT ALL YOU -- he is still; the city moves
# =============================================================================================================

def walker_group(rn, stance, count=0, spacing=2.4):
    """A walker group's tree, walking +Z in its own frame: the placement translate `<rn>walk` (the film keys it along
    the path) holding a single file of `2 count + 1` rigs `spacing` apart (the repeat is inside the walk, so the file
    moves as one; each rig's joints are the same named rotates, so they step together)."""
    rig = WK.rig(rn, stance)
    if count:
        rig = K.repeat([0.0, 0.0, spacing], count, rig)
    return K.T([0.0, 0.0, 0.0], rig, name=f"{rn}walk")


def add_walkers(b, key, specs, kerbs=None):
    """specs: [(object name, rig name, start (x, y, z), yaw, t0, t1, speed, phase, count, spacing)] -> one world of
    walker groups; each walks a straight line at `speed` (1.25 m/s is one stride a beat at 111 BPM). `kerbs`: for
    walkers crossing the z-road along x, the feet step down 12 cm off the pavement and up again on the far side."""
    f = b.f
    objs = []
    for (obj, rn, at, yaw, t0, t1, speed, phase, count, spacing) in specs:
        tree = K.T(list(at), K.R([0.0, yaw, 0.0], walker_group(rn, WK.walk_base(phase), count, spacing)))
        a = math.radians(yaw)
        dx, dz = math.sin(a), math.cos(a)        # the rig walks +Z in its own frame; turned by yaw
        L = speed * (t1 - t0)
        back = spacing * count + 1.0
        xs = [at[0] - dx * back, at[0] + dx * (L + back)]
        zs = [at[2] - dz * back, at[2] + dz * (L + back)]
        objs.append((obj, tree, "furn", (min(xs) - 1.2, at[1] - 0.3, min(zs) - 1.2), (max(xs) + 1.2, at[1] + 2.2, max(zs) + 1.2),
                     dict(FAR)))
    b.world(key, {"objects": objs, "lights": []})
    for (obj, rn, at, yaw, t0, t1, speed, phase, count, spacing) in specs:
        WK.walk(f, obj, rn, phase=phase, gate=b.gate(f"g{obj}", t0, t1, fade=0.02))
        keys = [(0.0, [0.0, 0.0, 0.0], "step"), (t0, [0.0, 0.0, 0.0], "linear")]
        if kerbs:
            a = math.radians(yaw)
            dx = math.sin(a)
            # the times the group's lead crosses each kerb line (x = kx): its local z there
            cross = sorted(((kx - at[0]) / dx, dy) for kx, dy in kerbs if dx and 0.0 < (kx - at[0]) / dx < speed * (t1 - t0))
            y = 0.0
            for zl, dy in cross:
                tk = t0 + zl / speed
                keys.append((tk - 0.05, [0.0, y, zl - 0.06], "linear"))
                y += dy
                keys.append((tk + 0.05, [0.0, y, zl + 0.06], "linear"))
            keys.append((t1, [0.0, y, speed * (t1 - t0)], "step"))
        else:
            keys.append((t1, [0.0, 0.0, speed * (t1 - t0)], "step"))
        f.track(f"sdf/{obj}/node/{rn}walk/translation", keys)
    return [s[0] for s in specs]


def isolation(b):
    f = b.f
    # ---- the lead's car at the red light, and the lead in it -----------------------------------------------------
    import liminal_space as ls
    car_tree = K.place(ls.tag(P4.car(open_cabin=True), "carSeat", id="HeroCarSeat", seatHeight=0.4), CAR, CAR_YAW)
    fig_at, fig_yaw = fig_in_car()
    man_tree, lo, hi = T4.placed("car_sit", "carMan", fig_at, fig_yaw, room=None, anchor_id="HeroCarSeat")
    b.room_world("heroCar", {"objects": [("heroCar", car_tree, "furn2", (CAR[0] - 3, -0.1, CAR[2] - 3), (CAR[0] + 3, 2.0, CAR[2] + 3), FAR),
                                         ("carMan", man_tree, "figure", lo, hi, dict(FAR, figure=True))], "lights": []})
    # the cross traffic in front of him: two streams through the crossing on the green
    cross = K.U(K.T([0.0, 0.0, 0.0], K.T([C.XI, 0.0, C.ZK + C.LANE], K.I(K.repeat([16.0, 0.0, 0.0], 0, K.R([0, 90, 0], P4.car())),
                                                                           K.X(((-70.0, 70.0), (-1.0, 3.0), (-3.0, 3.0))))), name="crossFwd"),
                K.T([0.0, 0.0, 0.0], K.T([C.XI + 8.0, 0.0, C.ZK - C.LANE], K.I(K.repeat([16.0, 0.0, 0.0], 0, K.R([0, -90, 0], P4.car())),
                                                                                 K.X(((-70.0, 70.0), (-1.0, 3.0), (-3.0, 3.0))))), name="crossBack"))
    clip = K.X(((C.XI - 60.0, C.XI + 60.0), (-1.0, 3.0), (C.ZK - 5.0, C.ZK + 5.0)))
    b.world("crossTraffic", {"objects": [("crossTraffic", K.I(cross, clip), "furn", (C.XI - 61, -0.2, C.ZK - 6), (C.XI + 61, 2.0, C.ZK + 6), FAR)],
                             "lights": []})
    steady(b, "crossTraffic", "crossFwd", t(75) - 2.0, t(83), [1.0, 0.0, 0.0], 9.5)
    steady(b, "crossTraffic", "crossBack", t(75) - 2.0, t(83), [-1.0, 0.0, 0.0], 9.5)
    # people crossing in front of his car (on the walk signal): two groups, each way
    cz = CROSSWALK_Z
    pav = 0.12
    kerbs_e = [(KERB_W, -pav), (KERB_E, pav)]          # walking east: off the west kerb, onto the east one
    kerbs_w = [(KERB_E, -pav), (KERB_W, pav)]
    # individuals (a single file would share one kerb step): each steps off the kerb and onto the far one itself
    walkers_a = add_walkers(b, "walkA", [
        ("walkA1", "wa1", (KERB_W - 5.0, pav, cz + 0.3), 90.0, t(75), t(77), 1.25, 1.0, 0, 0.0),
        ("walkA2", "wa2", (KERB_W - 8.2, pav, cz + 0.65), 90.0, t(75) + 0.3, t(77), 1.3, -1.0, 0, 0.0)], kerbs=kerbs_e)
    walkers_a += add_walkers(b, "walkA2", [
        ("walkA3", "wa3", (KERB_E + 4.0, pav, cz - 0.3), -90.0, t(75), t(77), 1.25, -1.0, 0, 0.0),
        ("walkA4", "wa4", (KERB_E + 7.6, pav, cz - 0.65), -90.0, t(75) + 0.5, t(77), 1.2, 1.0, 0, 0.0)], kerbs=kerbs_w)
    # ---- the bar's interior, with the others in it ----------------------------------------------------------------
    bar = bar_room(b)
    # ---- the park: the lead on his bench, people walking past both ways ----------------------------------------------
    bx, by, bz = C.BENCH_AT
    a_ = math.radians(C.BENCH_YAW)
    man_tree, lo, hi = T4.placed("bench_sit", "benchMan", (bx, by, bz), C.BENCH_YAW, room=None, anchor_id="ParkBench")
    b.room_world("parkMan", {"objects": [("benchMan", man_tree, "figure", lo, hi, dict(FAR, figure=True))], "lights": []})
    pz = -88.0
    walkers_c = add_walkers(b, "walkC", [
        ("walkC1", "wc1", (C.HX - 9.0, 0.16, pz + 0.55), 90.0, t(79) - 1.0, t(81), 1.25, 1.0, 1, 3.4),
        ("walkC2", "wc2", (C.HX + 13.0, 0.16, pz - 0.55), -90.0, t(79) - 0.5, t(81), 1.3, -1.0, 0, 2.0),
        ("walkC3", "wc3", (C.HX + 0.55, 0.16, pz - 13.0), 0.0, t(79), t(81), 1.2, 1.0, 0, 2.0)])
    # ---- the crossing: he stands still in the middle of it; the crowd flows round him -------------------------------------
    man_tree, lo, hi = T4.placed("crowd_stand", "crowdMan", (C.XI + 0.4, 0.0, cz), 0.0, room=None)
    b.room_world("crowdMan", {"objects": [("crowdMan", man_tree, "figure", lo, hi, dict(FAR, figure=True))], "lights": []})
    walkers_d = add_walkers(b, "walkD", [
        ("walkD1", "wd1", (KERB_W - 2.0, pav, cz + 0.62), 90.0, t(81), t(82, 3), 1.25, 1.0, 0, 0.0),
        ("walkD2", "wd2", (KERB_W - 4.6, pav, cz + 0.5), 90.0, t(81) + 0.2, t(82, 3), 1.3, -1.0, 0, 0.0),
        ("walkD3", "wd3", (KERB_W - 7.4, pav, cz + 0.7), 90.0, t(81) + 0.5, t(82, 3), 1.2, 1.0, 0, 0.0),
        ("walkD4", "wd4", (KERB_W - 10.5, pav, cz + 0.88), 90.0, t(81) + 0.1, t(82, 3), 1.35, -1.0, 0, 0.0)], kerbs=kerbs_e)
    walkers_d += add_walkers(b, "walkD2", [
        ("walkD5", "wd5", (KERB_E + 1.6, pav, cz - 0.6), -90.0, t(81), t(82, 3), 1.25, -1.0, 0, 0.0),
        ("walkD6", "wd6", (KERB_E + 4.4, pav, cz - 0.5), -90.0, t(81) + 0.3, t(82, 3), 1.3, 1.0, 0, 0.0),
        ("walkD7", "wd7", (KERB_E + 7.0, pav, cz - 0.75), -90.0, t(81) + 0.15, t(82, 3), 1.2, -1.0, 0, 0.0),
        ("walkD8", "wd8", (KERB_E + 10.2, pav, cz - 0.9), -90.0, t(81) + 0.45, t(82, 3), 1.35, 1.0, 0, 0.0)], kerbs=kerbs_w)

    # ---- the shots -----------------------------------------------------------------------------------------------------------
    base = ("street", "cityIn", "cityOut", "cityGround", "cityTraffic2", "citySet", "crossTraffic")
    fx, _, fz = fig_at
    # A: the red light (75-76)
    # (from the southbound lane behind him: his profile in the car, the crossing ahead with the cross traffic, the bar's
    # lit window to the right, the words on the podium across the junction)
    eyeA = [(t(75), (C.XI - 0.75, 1.45, fz + 4.85)), (167.3, (C.XI - 0.65, 1.42, fz + 3.9)), (t(77), (C.XI - 0.55, 1.4, fz + 2.9))]
    lookA = [(t(75), (fx + 1.7, 1.25, fz - 3.6)), (t(77), (fx + 1.3, 1.2, fz - 2.7))]
    b.glide("redlight", eyeA, lookA, nodes_keys=base + ("heroCar", "walkA", "walkA2", "barPeople"), extra=["barBack", "barFurn", "barTables"], fov=48.0)
    b.show("carMan", t(75), t(77))
    set_signals(b, t(74, 4), t(77), ns="red", ew="green")
    # B: the bar (77-78)
    eyeB, lookB = bar["camera"]
    b.glide("bar", eyeB, lookB, nodes_keys=("barRoom", "barPeople", "street", "cityIn", "cityGround", "cityTraffic2", "crossTraffic"), fov=56.0)
    b.show("barMan2", t(77), t(79))
    # C: the park bench (79-80)
    eyeC = [(t(79), (bx - 2.4, 1.42, pz - 3.8)), (176.0, (bx - 0.6, 1.38, pz - 3.6)), (t(81), (bx + 1.4, 1.35, pz - 3.3))]
    lookC = [(t(79), (bx, 0.85, bz)), (t(81), (bx + 0.2, 0.85, bz))]
    b.glide("bench", eyeC, lookC, nodes_keys=base + ("parkMan", "walkC"), fov=50.0)
    b.show("benchMan", t(79), t(81))
    # D: the crossing (81-82): from above, slowly rising; 82.3 the pulse stops and everything with it; 82.4 black
    eyeD = [(t(81), (C.XI - 3.2, 9.0, cz + 9.0)), (t(82, 3), (C.XI - 2.6, 14.0, cz + 7.5)), (t(83), (C.XI - 2.5, 14.5, cz + 7.3))]
    lookD = [(t(81), (C.XI + 0.4, 0.6, cz)), (t(83), (C.XI + 0.4, 0.6, cz - 0.3))]
    b.glide("crossing", eyeD, lookD, nodes_keys=base + ("crowdMan", "walkD", "walkD2"), fov=54.0)
    b.show("crowdMan", t(81), t(83))
    set_signals(b, t(80, 4), t(83), ns="red", ew="green")
    # cars waiting at his lines in the crossing shot: the hero car again (empty now) stays where it stopped
    f.shots[-1]["nodes"].append("heroCar")

    # ---- the city moves on the beat; he does not -----------------------------------------------------------------------------
    traffic_flow(b, t(66), t(91) + 1.0)
    g2 = b.gate("gB2city", t(75), t(82, 3))
    for nm_ in SETS:
        for io in ("In", "Out"):
            b.pulse(f"sdf/city{io}{nm_}/surface/{K.CANVAS2}/emission", 1.4, "eighth", g2)
    b.pulse("sdf/cityLamps/surface/2/emission", 1.6, "quarter", g2)
    # 82.3: the pulse stops -- the walkers stop where they are (their gates and paths end there)
    # the words: IS THAT ALL YOU, once a bar, as signs in the city he is looking at
    bridge2_words(b)
    # C17 (78.2.5) GOT?: the bar's lights burst into colour; C18 (82.4): the city's lights go out for a beat
    c = b.clap("c17", t(78, 2.5), release=1.2)
    f.route(c, "post/grade/hueShift", 2.2)
    f.route(c, "post/bloom/intensity", 2.4)
    f.route(c, "palette/saturation", 0.8)
    f.route(c, "temporal/mosh/shift", 16.0)
    for k_ in (K.CANVAS, K.CANVAS2, K.SCREEN, K.GLOW, K.FLOOR):
        f.route(c, f"sdf/barBack/surface/{k_}/emission", 6.0)
    cb = b.clap("gotSpark", t(78, 2.5), release=0.08)
    f.route(cb, "particles/barSpark/burst", 2600.0)
    c = b.clap("c18", t(82, 4), release=0.2, attack=BEAT2 * 0.5)
    f.route(c, "post/bloom/intensity", 2.0)
    f.route(c, "temporal/mosh/amount", 0.5)
    b.val += [(t(82, 4) + 0.02, 1.0, "step"), (t(82, 4) + 0.06, 0.0, "step"), (t(83) - 0.001, 0.0, "step"), (t(83), 1.0, "step")]


def bridge2_words(b):
    """IS THAT ALL YOU, once a bar (IS on the previous bar's 4.5, so it is sung, and seen, in the shot before the cut):
    each word placed in whichever shot it appears in -- on the bar building's front behind him at the red light, on
    the bar's back wall over the bottles, in the dark between the park's trees over his bench, on the road round him
    at the crossing; the descent's IS floats ahead of the camera."""
    f = b.f
    seq = []
    for bar in range(75, 83):
        seq.append(("IS", t(bar - 1, 4.5), bar, 0))
        seq.append(("THAT", t(bar, 1.0), bar, 1))
        seq.append(("ALL", t(bar, 1.5), bar, 2))
        seq.append(("YOU" + ("..." if bar == 82 else ""), t(bar, 2.0), bar, 3))
        if bar == 78:
            seq.append(("GOT?", t(bar, 2.5), bar, 4))
    bb = C.BAR
    bx, _, bz = C.BENCH_AT
    for i, (wd, te, bar, j) in enumerate(seq):
        shot = f.camera_at(te + 0.12)[3]
        second = bar % 2 == 0
        if shot == "rise":
            e = b.word_at(wd, te, te + G.BAR2 * 0.85, ("view", 16.0), 0.0, 0.1, k=0.08, style="flash", role="word", name=f"isth{i:02d}",
                          intensity=3.8, tin=0.08, tout=0.3, category="floatingText")
            continue
        if shot == "redlight":       # the podium across the junction (its south face, z = -115), above the cross traffic
            pos, nrm, h, cat = (C.HX + 40.0 - 13.0 + 1.6 + 2.2 * j, 5.6 - 0.5 * j + (0.3 if second else 0.0), -88.0 - 40.0 + 13.0 + 0.01), (0, 0, 1), \
                1.1, "floatingText"   # on a podium's face: the city's blocks are SDF repeats, not rooms the validator knows
        elif shot == "bar":          # the back wall over the bottles
            zz = (bb["z0"] + bb["z1"]) / 2 - 2.4 + 1.25 * j
            pos, nrm, h, cat = (bb["x1"] - 0.02, 0.12 + 2.85 - (0.85 if wd == "GOT?" else 0.0), zz if wd != "GOT?" else (bb["z0"] + bb["z1"]) / 2 + 1.6), (-1, 0, 0), \
                0.3 if wd != "GOT?" else 0.55, "wallText"
        elif shot == "bench":        # in the dark between the trees, over him
            pos, nrm, h, cat = (bx - 2.4 + 1.6 * j, 3.6 - 0.35 * j, bz + 2.6), (0, 0, -1), 0.62, "floatingText"
        else:                        # the crossing: on the road round him, read from above
            pos, nrm, h, cat = (C.XI - 3.0 + 2.0 * j, 0.02, C.ZK + C.CROSS_OFF - 3.2 - (1.9 if second else 0.0)), (0, 1, 0), 0.9, "floorText"
        tilt = f.flat_tilt([0, 1, 0], f.basis(te + 0.12)[1]) if nrm[1] > 0.5 else 0.0
        b.word(wd, te, te + G.BAR2 * 0.85, pos, nrm, h, style="flash" if wd != "GOT?" else "pop", role="word", name=f"isth{i:02d}",
               intensity=3.8 if wd != "GOT?" else 6.0, tin=0.08, tout=0.3, category=cat, tilt=tilt)


def bar_room(b):
    """The bar's interior (the corner unit of the building south-east of the crossing): the room's own shell with the
    building's windows and door, the counter along the east wall with three stools (he sits on the middle one), the
    back bar, two tables by the window, and the others: a bartender wiping the counter, two people talking at the
    far table, a couple at the near one. Returns the camera keys for the bar shot."""
    import rooms3 as R3
    f = b.f
    bb = C.BAR
    ext = ((bb["x0"], bb["x1"]), (0.12, 0.12 + bb["h"]), (bb["z0"], bb["z1"]))
    (x0, x1), (y0, y1), (z0, z1) = ext
    rid = "barRoom"
    win_w = ((z0 + 1.0) + (z1 - 1.4)) / 2, (z1 - 1.4) - (z0 + 1.0)
    win_n = ((x0 + 1.4) + (x1 - 1.0)) / 2, (x1 - 1.0) - (x0 + 1.4)
    shell = K.U(*R3.shell_with(ext, rid, doors=[("-x", z1 - 0.75, 0.9, "BarDoor")],
                               windows=[("-x", win_w[0], win_w[1], 2.1, 0.78), ("-z", win_n[0], win_n[1], 2.1, 0.78)], tiled=True, dado=False))
    counter_at = (x1 - 1.55, y0, (z0 + z1) / 2)
    back = K.U(K.place(P4.back_bar(2.4), (x1, y0, (z0 + z1) / 2), -90.0), K.place(P4.bar_counter(2.6), counter_at, -90.0))
    import liminal_space as ls
    stools = K.U(*[K.place(ls.tag(P4.bar_stool(), "barStool", id=f"BarRoomStool{i}", room=rid), (counter_at[0] - T4.BAR_AT[2], y0, counter_at[2] + dz), 90.0)
                   for i, dz in enumerate((-0.85, 0.0, 0.85))])
    tables = K.U(K.place(K.round_table(0.45, 0.72), (x0 + 2.4, y0, z0 + 1.6)), K.place(K.round_table(0.45, 0.72), (x0 + 5.6, y0, z0 + 1.6)),
                 K.place(K.chair(), (x0 + 1.75, y0, z0 + 1.6), 90.0), K.place(K.chair(), (x0 + 3.05, y0, z0 + 1.6), -90.0),
                 K.place(P4.neon_sign(1.1, 0.36), (x0 + 0.02, y0 + 2.45, win_w[0]), 90.0))
    a_ = math.radians(-90.0)
    fig_at, fig_yaw = (counter_at[0] - T4.BAR_AT[2], y0, counter_at[2]), 90.0
    man_tree, lo, hi = T4.placed("bar_sit", "barMan2", fig_at, fig_yaw, room=rid, anchor_id="BarRoomStool1")
    glass = K.place(K.S(K.D(K.CY([0, 1.08 + 0.06, 0.58], 0.035, 0.12), K.CY([0, 1.08 + 0.08, 0.58], 0.028, 0.12)), K.GLASS), fig_at, fig_yaw)
    bright = {K.CANVAS: [1.5, 0.25, 0.85], K.SCREEN: [1.6, 0.85, 0.25], K.CANVAS2: [0.25, 1.3, 1.5], K.GLOW: [1.6, 0.9, 0.35],
              K.FLOOR: [0.3, 1.6, 1.9]}
    room = {"id": rid, "interior": ext, "objects": [
        ("barShell", shell, "wall", (x0 - 0.5, y0 - 0.3, z0 - 0.5), (x1 + 0.5, y1 + 0.3, z1 + 0.5), FAR),
        ("barBack", back, "furn2", (x1 - 2.2, y0 - 0.05, z0), (x1 + 0.05, y0 + 2.5, z1), dict(FAR, emission=dict(bright))),
        ("barFurn", K.U(stools, glass), "furn", (x0, y0 - 0.05, z0), (x1, y0 + 1.4, z1), FAR),
        ("barTables", tables, "furn", (x0 - 0.1, y0 - 0.05, z0), (x0 + 7.0, y0 + 2.9, z1), FAR),
        ("barMan2", man_tree, "figure", lo, hi, dict(FAR, figure=True))],
        "lights": [("barRoomGlow", (x1 - 0.6, y0 + 1.3, (z0 + z1) / 2), "lamp"), ("barRoomLamp", (x0 + 3.5, y1 - 0.4, z0 + 1.8), "lamp")]}
    b.room_world("barRoom", room)
    # the others: a bartender (wiping), two talking at the far table (standing), a couple at the near one (sitting)
    others = [("barTender", "bt", (counter_at[0] + 0.62, y0, counter_at[2] - 0.5), -90.0, {"shR": -45.0, "elR": -70.0, "lean": 8.0}),
              ("barTalkA", "ta", (x0 + 5.1, y0, z0 + 2.45), 160.0, {"elR": -60.0, "shR": -20.0}),
              ("barTalkB", "tb", (x0 + 6.1, y0, z0 + 2.3), -110.0, {"elL": -55.0, "headPitch": -4.0}),
              ("barSitA", "sa", (x0 + 1.75, y0 - 0.38, z0 + 1.6), 90.0, {"hipL": -88.0, "hipR": -88.0, "kneeL": 88.0, "kneeR": 88.0, "elR": -50.0}),
              ("barSitB", "sb", (x0 + 3.05, y0 - 0.38, z0 + 1.6), -90.0, {"hipL": -88.0, "hipR": -88.0, "kneeL": 88.0, "kneeR": 88.0, "elL": -45.0})]
    objs = []
    for obj, rn, at, yaw, stance in others:
        objs.append((obj, K.T(list(at), K.R([0, yaw, 0], WK.walker(rn, stance))), "figure2", (at[0] - 1.2, at[1] - 0.2, at[2] - 1.2),
                     (at[0] + 1.2, at[1] + 2.2, at[2] + 1.2), dict(FAR, own_line=True)))
    b.world("barPeople", {"objects": objs, "lights": []})
    for obj, rn, *_ in others:
        f.bind("figure", f"sdf/{obj}/look/edge/color")
    g = b.gate("gBarPeople", t(77) - 0.5, t(79))
    f.route("grid.song.half.wave", "sdf/barTender/node/btShRgest/rotation", 26.0, component=0, polarity="bipolar", depth=g)
    WK.talk(f, "barTalkA", "ta", arm="R", amount=34.0, gate=g)
    WK.talk(f, "barTalkB", "tb", arm="L", amount=26.0, div="half", gate=g)
    WK.talk(f, "barSitA", "sa", arm="R", amount=30.0, gate=g)
    WK.talk(f, "barSitB", "sb", arm="L", amount=22.0, div="half", gate=g)
    # the sparks of GOT? over the back bar
    f.nodes.append({"name": "barSpark", "kind": "particles", "particles": {
        "capacity": 6000, "spawnRate": 0.0, "burst": 0.0, "shape": "box", "position": [x1 - 0.5, y0 + 1.6, (z0 + z1) / 2],
        "extent": [0.3, 0.5, 1.2], "lifetimeMin": 0.8, "lifetimeMax": 1.6, "direction": [-1.0, 0.6, 0.0], "spread": 1.0,
        "speedMin": 1.0, "speedMax": 3.4, "gravity": [0.0, -1.2, 0.0], "drag": 1.2, "sizeStart": 0.035, "sizeEnd": 0.0,
        "blend": "additive", "colorStart": [1.0, 0.5, 1.0, 1.0], "colorEnd": [0.3, 0.9, 1.0, 0.0], "emissive": 4.0}})
    b.worlds["barRoom"]["names"].append("barSpark")
    for k_ in (K.CANVAS, K.CANVAS2, K.SCREEN):
        f.route("grid.song.eighth", f"sdf/barBack/surface/{k_}/emission", 1.0, depth=g)
    # the camera: in from the window side along the room towards him, his back to us, the bar's light on him
    eye = [(t(77), (x0 + 1.2, y0 + 1.5, z1 - 1.1)), (171.6, (x0 + 2.6, y0 + 1.45, z1 - 1.4)), (t(79), (x0 + 3.9, y0 + 1.42, z1 - 1.75))]
    look = [(t(77), (fig_at[0] + 0.3, y0 + 1.1, fig_at[2])), (t(79), (fig_at[0] + 0.6, y0 + 1.2, fig_at[2] - 0.2))]
    return {"camera": (eye, look), "man": fig_at}


# =============================================================================================================
# 83-90: THE CITY ALIVE
# =============================================================================================================

def alive(b):
    f = b.f
    keys = [(t(83), (C.XI, 4.0, C.ZK - 10.0)), (t(84), (C.XI - 0.5, 6.5, C.ZK - 48.0)), (t(85), (C.XI - 9.0, 24.0, C.ZK - 92.0)),
            (t(85, 3), (C.XI - 28.0, 40.0, C.ZK - 110.0)), (t(86), (C.XI - 42.0, 46.0, C.ZK - 132.0)), (t(87), (C.XI - 22.0, 22.0, C.ZK - 166.0)),
            (t(87, 3), (C.XI - 2.0, 9.0, C.ZK - 176.0)), (t(88), (C.XI + 30.0, 7.0, C.ZK - 180.0)), (t(88, 3), (C.XI + 62.0, 8.0, C.ZK - 180.0)),
            (t(89), (C.XI + 80.0, 40.0, C.ZK - 170.0)), (t(89, 3), (C.XI + 96.0, 110.0, C.ZK - 140.0)), (t(90), (C.XI + 100.0, 165.0, C.ZK - 100.0)),
            (t(90, 3), (C.XI + 96.0, 185.0, C.ZK - 70.0)), (t(91), (C.XI + 90.0, 190.0, C.ZK - 55.0))]
    look = [(t(83), (C.XI, 6.0, C.ZK - 60.0)), (t(84), (C.XI - 2.0, 10.0, C.ZK - 110.0)), (t(85), (C.XI - 30.0, 30.0, C.ZK - 135.0)),
            (t(85, 3), (C.XI - 14.0, 26.0, C.ZK - 140.0)), (t(86), (C.XI - 6.0, 18.0, C.ZK - 150.0)), (t(87), (C.XI, 2.0, C.ZK - 180.0)),
            (t(87, 3), (C.XI + 30.0, 4.0, C.ZK - 180.0)), (t(88), (C.XI + 80.0, 6.0, C.ZK - 180.0)), (t(88, 3), (C.XI + 110.0, 14.0, C.ZK - 175.0)),
            (t(89), (C.XI + 90.0, 30.0, C.ZK - 150.0)), (t(89, 3), (C.XI + 40.0, 0.0, C.ZK - 120.0)), (t(90), (C.XI, 0.0, C.ZK - 140.0)),
            (t(90, 3), (C.XI - 20.0, 0.0, C.ZK - 170.0)), (t(91), (C.XI - 25.0, 0.0, C.ZK - 180.0))]
    b.glide("alive", keys, look, nodes_keys=("street", "cityIn", "cityOut", "cityGround", "cityTraffic", "citySet"), extra=["stars"], fov=68.0)
    f.shots[-1]["fov"] = [(t(83), 72.0), (t(85), 66.0), (t(87, 3), 74.0), (t(88, 3), 70.0), (t(89, 3), 62.0), (t(91), 58.0)]
    c = b.clap("aliveIn", t(83), release=0.5)
    f.route(c, "camera/exposure/compensation", 1.4)
    f.route(c, "post/bloom/intensity", 1.4)
    g3 = b.gate("gB3city", t(83), t(90, 3))
    # the city breathes: each set's windows on its own division, the lamps on the quarter, the crowns on the half
    pattern = {"Low": ("quarter", 1.6), "Mid": ("eighth", 1.4), "TowerA": ("half", 2.0), "TowerB": ("quarter", 1.6), "Sky": ("eighth", 1.8)}
    for nm_, (div, amt) in pattern.items():
        for io in ("In", "Out"):
            b.pulse(f"sdf/city{io}{nm_}/surface/{K.CANVAS2}/emission", amt, div, g3)
            b.pulse(f"sdf/city{io}{nm_}/surface/{K.GLOW}/emission", 2.5, "half", g3)
            b.pulse(f"sdf/city{io}{nm_}/look/edge/intensity", 1.2, "quarter", g3)
    b.pulse("sdf/cityLamps/surface/2/emission", 2.2, "quarter", g3)
    b.pulse("sdf/cityMarks/look/edge/intensity", 2.0, "eighth", g3)
    b.pulse("sdf/cityKerbs/look/edge/intensity", 1.6, "quarter", g3)
    f.route("grid.song.bar.wave", "post/grade/hueShift", 0.18, depth=g3, polarity="bipolar")
    f.route("grid.song.half.wave", "scene/volumeDensity", 0.002, depth=g3)
    # IS THAT ALL? as signs on the towers the camera passes
    alive_words(b)
    city_wave(b)


def alive_words(b):
    f = b.f
    for bar in range(83, 91):
        tt = t(bar - 1, 4.5)
        cam = f.camera_at(min(tt + 0.25, t(91) - 0.01))
        if cam is None:
            continue
        eye, fwd, right, up, fov = f.basis(tt + 0.25)
        dist = 18.0 + 4.0 * (bar % 3)
        p = [e + d * dist + r * (4.0 if bar % 2 else -4.0) for e, d, r in zip(eye, fwd, right)]
        text = "IS THAT ALL YOU GOT?" if bar in (84, 88) else "IS THAT ALL?"
        h = dist * 0.07
        nrm = [-v for v in fwd]
        nrm[1] = 0.0
        L = math.sqrt(sum(v * v for v in nrm)) or 1.0
        b.word(text, tt, t(bar, 4.0), p, [v / L for v in nrm], h, style="pop", role="accent" if bar % 2 else "word",
               name=f"b3w{bar}", tin=0.12, intensity=4.2, category="floatingText")


def city_wave(b):
    """Section 30's wave (ADR-1055) through the city: the front runs from the far north towards the camera and past
    it (90.3 -> 91.1); inside the band every line takes the rainbow's hue, behind it they are left in the open's
    blue; the cut to the open lands on 91.1."""
    f = b.f
    t0, t1 = t(90, 3), t(91)
    end = f.end
    origin, direction = [C.XI, 0.0, -460.0], [0.0, 0.0, 1.0]
    f.track("post/wave/origin", [(0.0, origin, "step"), (end, origin, "step")])
    f.track("post/wave/direction", [(0.0, direction, "step"), (end, direction, "step")])
    f.track("post/wave/progress", [(0.0, 0.0, "step"), (t0 - 0.2, 0.0, "linear"), (t0 + 0.3, 140.0, "linear"),
                                   (t(90, 4) + 0.2, 330.0, "linear"), (t1 + 0.05, 520.0, "step")])
    f.track("post/wave/width", [(0.0, 14.0, "step"), (end, 14.0, "step")])
    f.track("post/wave/hue", [(0.0, 0.0, "step"), (end, 0.0, "step")])
    f.track("post/wave/hueSpan", [(0.0, 1.0, "step"), (end, 1.0, "step")])
    f.track("post/wave/intensity", [(0.0, 0.0, "step"), (t0 - 0.22, 0.0, "smooth"), (t0 - 0.05, 1.1, "smooth"),
                                    (t1 - 0.05, 1.1, "smooth"), (t1 + 0.1, 0.0, "step")])
    f.track("post/wave/edgeTint", [(0.0, 0.0, "step"), (t0 - 0.22, 0.0, "smooth"), (t0 - 0.12, 1.0, "smooth"),
                                   (t1 + 0.05, 1.0, "smooth"), (t1 + 0.2, 0.0, "step")])
    f.track("post/wave/trail", [(0.0, 0.0, "step"), (t0 - 0.2, 0.0, "smooth"), (t0 - 0.05, 1.0, "smooth"),
                                (t1 - 0.01, 1.0, "step"), (t1, 0.0, "step")])
    trail = [0.1, 0.58, 1.0]
    f.track("post/wave/trailColor", [(0.0, trail, "step"), (end, trail, "step")])
    for nm_ in SETS:
        c = b.clap(f"wave{nm_}", t(90, 4) - 0.1 * SETS.index(nm_), attack=0.05, release=0.4)
        f.route(c, f"sdf/cityOut{nm_}/look/edge/intensity", 6.0)
        f.route(c, f"sdf/cityIn{nm_}/look/edge/intensity", 6.0)
    c = b.clap("sweep", t(91), attack=0.3, release=0.6, curve="smooth")
    f.route(c, "camera/exposure/compensation", 2.2)
    f.route(c, "post/bloom/intensity", 1.8)
    f.route(c, "palette/saturation", 0.8)
    b.val += [(t(90, 4), 1.0, "smooth"), (t(91) - 0.02, 2.4, "step"), (t(91), 1.0, "smooth")]


# =============================================================================================================

def build(b):
    f = b.f
    worlds(b)
    roof_flight(b)
    grow_keys(b)
    rise_shot(b)
    grow_words(b)
    isolation(b)
    alive(b)
    # the hero house grows 0.6 % about the origin for the whole film so its walls never coincide with the upstairs
    # rooms' walls when both are seen together from the air (67-74); its window and door move by under 2 cm
    b.key("sdf/heroShell/transform/scale", 0.0, [1.006, 1.006, 1.006])
    # the city's fog thins for the wide shots (the air of the open city), and comes back for the street
    b.key("scene/volumeDensity", 0.0, 0.022)
    b.key("scene/volumeDensity", t(67), 0.022, "smooth")
    b.key("scene/volumeDensity", t(69), 0.007, "step")
    b.key("scene/volumeDensity", t(74, 4), 0.007, "smooth")
    b.key("scene/volumeDensity", t(75), 0.016, "step")
    b.key("scene/volumeDensity", t(82, 4), 0.016, "smooth")
    b.key("scene/volumeDensity", t(83), 0.008, "step")
    b.key("scene/volumeDensity", t(91) - 0.01, 0.008, "step")
    b.key("scene/volumeDensity", t(91), 0.022, "step")
