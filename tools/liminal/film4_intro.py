"""All You Got, art pass 4: the intro, owner bars 0-16 (0-37.43 s), on Builder4 (04-art-pass-4.md PART 3: more growth,
rhythmically, and BUILD -> LOCK: nothing structural moves after its construction event; pass 3's roof flips and
twists, the row's shudder and the towers' stutter are gone). Pass 3's design otherwise (film3_intro.py, 03 sections
23-26). One continuous camera: down the road in the count-in, rising over the street as it builds, gliding along it,
then turning on the house and accelerating at its lit front window. The window fills the frame at the end of the
one-beat gap (16.4) and the cut inside lands exactly on the downbeat of bar 17 (37.4312 s on the grid: the audio's
attack measures 37.41-37.45 s, so frame 1123 at 30 fps, 37.433 s, is the first frame inside).

The music, beat by beat:
  bar 0       four road dashes stamp down, one a beat, out of the black (construction starts with the road)
  1.1         SPLASH: the street's grid bursts outward from the house's lot, the kerbs light, the first houses land
  1.2-2.3     a pair of houses on both sides of the street lands on every quarter note, lot after lot outward
  3.1-3.4     the stab: the far city shoots up, in steps
  4.1         every roof slams down onto its house at once (they land without them) and stays; 4.3 the trees sprout
  5-6         the bass: street lamps pop up along the pavements on the eighths, alternating sides
  7           a car lands in every driveway on the eighths, outward from the hero lot
  7-9         behind the houses the city's first blocks rise a storey a beat (the podiums in three, the slabs in nine)
  9-12        the riser: the house we will enter rises out of the ground (9.1-10.1) and its parts fly in, faster each
              bar; the towers climb a storey a beat, the downtown core a storey an eighth from 13 (build -> lock)
  13-16       the 'all you got' chop: every window flashes on each bar's downbeat; corruption builds on the sixteenths
              (in the picture, never in the geometry)
  16.4        the gap: the world freezes and goes dark but for the lit window, and the camera keeps rushing at it
  17.1        through the window: the cut on the downbeat
"""

from __future__ import annotations

import city4 as C
import intro4 as IN
import pass2_grid as G
from film2 import srgb

t = G.t
BEAT1 = G.BAR1 / 4.0
EIGHTH = BEAT1 / 2.0
ENTRY = t(17)                    # 37.4312 s


def build(b):
    f = b.f
    world = {"objects": IN.objects(), "lights": []}
    b.world("street", world)
    names = b.worlds["street"]["names"]
    inner = [o for o in C.objects() if o[0].startswith("cityIn")]
    b.world("cityIn", {"objects": inner, "lights": []})

    # ---- the camera: one continuous glide, accelerating into the window --------------------------------------
    keys = [(0.0, (-46.0, 2.0, 12.6)), (t(1), (-45.6, 2.2, 12.8)), (t(5), (-36.0, 6.8, 18.0)), (t(9), (-22.0, 8.6, 18.4)),
            (t(13), (-9.5, 6.4, 15.0)), (t(15), (-3.2, 3.6, 9.8)), (t(16, 2.5), (-1.3, 2.3, 6.0)),
            (t(16, 4), (-0.75, 1.72, 4.0)), (ENTRY, (-0.6, 1.53, 2.65))]
    look = [(0.0, (-20.0, 0.8, 12.0)), (t(1), (-20.0, 1.0, 12.0)), (t(2), (-12.0, 2.5, 9.0)), (t(5), (-2.0, 3.5, 6.0)),
            (t(9), (0.8, 3.4, 1.5)), (t(13), (0.2, 2.6, 2.0)), (t(15), (-0.5, 1.9, 2.0)), (t(16, 3), (-0.6, 1.6, 2.0)),
            (ENTRY, (-0.6, 1.53, 0.0))]
    b.glide("street", keys, look, nodes_keys=("street", "cityIn"), fov=58.0)
    f.shots[-1]["fov"] = [(0.0, 54.0), (t(9), 58.0), (t(16, 2), 60.0), (ENTRY, 66.0)]

    # ---- bar 0: the dashes, one a beat ------------------------------------------------------------------------
    # the dash clip is a box centred at x = -42 (dashes every 5 m at x = 5k, 2.2 m long): a half size of x_end + 42
    # reveals the road's centre line up to x_end (the half behind the camera is never seen)
    f.track("sdf/street/node/dashClip/size", [(0.0, [0.0, 0.5, 0.5], "step")] +
            [(t(0, 1 + i), [x_end + 42.0, 0.5, 0.5], "step") for i, x_end in enumerate((-38.6, -33.6, -28.6, -23.6))] +
            [(t(1), [260.0, 0.5, 0.5], "step")])
    for i in range(4):
        c = b.clap(f"dash{i}", t(0, 1 + i), release=0.35)
        f.route(c, "sdf/street/surface/2/emission", 6.0)

    # ---- 1.1 the splash: the grid bursts out from the house's lot, the street's lines come on -------------------
    f.track("sdf/street/node/gridClip/size", [(0.0, [0.0, 0.5, 0.0], "step"), (t(1) - 0.001, [0.0, 0.5, 0.0], "easeOut"),
                                              (t(1) + 0.9, [190.0, 0.5, 190.0], "step")])
    f.track("sdf/street/look/edge/intensity", [(0.0, 0.0, "step"), (t(1), 9.0, "step"), (t(1, 3), 3.5, "smooth"), (37.0, 3.5)])
    c = b.clap("splash", t(1), release=0.9)
    f.route(c, "camera/exposure/compensation", 2.0)
    f.route(c, "post/bloom/intensity", 1.6)
    f.route(c, "post/lens/chromaticAberration", 0.7)
    f.route(c, "temporal/mosh/amount", 0.45)
    f.route(c, "temporal/mosh/shift", 10.0)

    # ---- 1.1-2.3: the neighbours land, a pair of lots a beat -----------------------------------------------------
    lands = [t(1, 1), t(1, 2), t(1, 3), t(1, 4), t(2, 1), t(2, 2), t(2, 3)]
    clip = [(0.0, [0.0, 0.0, 0.0], "step")]      # closed: a point in the road (a zero-width box still cuts a plane)
    fall = [(0.0, [IN.LOT, 60.0, 0.0], "step")]
    for k, tl in enumerate(lands, start=1):
        x = IN.LOT * k
        # a fall from 34 m accelerating into the lot, landing ON the beat, no bounce (build -> lock). (A key's interp
        # shapes the span after it: pass 3's step key held the house in the air and dropped it in a frame.)
        fall += [(tl - 0.2, [x, 34.0, 0.0], "easeIn"), (tl, [x, 0.0, 0.0], "step")]
        # the row takes the lot over the instant before the faller leaves it (the next drop starts 0.2 s early)
        nxt = lands[k] - 0.2 if k < len(lands) else tl + 0.6
        clip.append((nxt - 0.005, [x + 3.8, 8.0, 30.0], "step"))
        fall.append((nxt - 0.004, [x, 60.0, 0.0], "step"))
        c = b.clap(f"land{k}", tl, release=0.32)
        f.route(c, "post/bloom/intensity", 0.7)
        f.route(c, "sdf/row/look/edge/intensity", 5.0)
        f.route(c, "sdf/street/look/edge/intensity", 3.0)
        f.route(c, "post/lens/chromaticAberration", 0.35)
        f.route(c, "temporal/mosh/amount", 0.22)
    clip.append((t(2, 3) + 0.6, [IN.LOT * IN.LOTS + 4.0, 8.0, 30.0], "step"))
    # each landing throws up debris of light at the two lots nearest the camera (the -x copies, both sides)
    for side, z in (("N", IN.ROAD_Z - IN.NEAR_C), ("F", IN.ROAD_Z + IN.NEAR_C)):
        nm_ = f"debris{side}"
        f.nodes.append({"name": nm_, "kind": "particles", "particles": {
            "capacity": 4000, "spawnRate": 0.0, "burst": 0.0, "shape": "box", "position": [IN.HERO_X - IN.LOT, 0.3, z],
            "extent": [3.5, 0.2, 3.5], "lifetimeMin": 0.5, "lifetimeMax": 1.3, "direction": [0.0, 1.0, 0.0], "spread": 0.9,
            "speedMin": 3.0, "speedMax": 9.0, "gravity": [0.0, -9.0, 0.0], "drag": 0.6, "sizeStart": 0.09, "sizeEnd": 0.0,
            "blend": "additive", "colorStart": [0.6, 0.95, 1.0, 1.0], "colorEnd": [1.0, 0.3, 0.9, 0.0], "emissive": 3.0,
            "velocityStretch": 0.05, "stretchMax": 5.0}})
        b.worlds["street"]["names"].append(nm_)
        f.track(f"particles/{nm_}/position", [(0.0, [IN.HERO_X - IN.LOT, 0.3, z], "step")] +
                [(tl - 0.03, [IN.HERO_X - IN.LOT * k, 0.3, z], "step") for k, tl in enumerate(lands, start=1)])
        for k, tl in enumerate(lands, start=1):
            cb = b.clap(f"debris{side}{k}", tl, release=0.06)
            f.route(cb, f"particles/{nm_}/burst", 700.0)
    f.track("sdf/row/node/rowClip/size", clip)
    f.track("sdf/faller/node/fall/translation", sorted(fall, key=lambda k: k[0]))
    b.key("nodes/faller/visible", 0.0, 0.0)
    b.key("nodes/faller/visible", t(1) - 0.25, 1.0)

    # ---- 3.1-3.4: the far city shoots up, a step a beat, and holds -----------------------------------------------
    # every set of the city's inner blocks rises out of the ground through its `<set>Rise` clip: one storey (3.2 m) a
    # beat, each step a fast ease into the new height and then a hold (build -> lock); the rings are open
    for nm_ in ("Low", "Mid", "TowerA", "TowerB", "Sky"):
        f.track(f"sdf/cityIn{nm_}/node/{nm_}Ring/size", [(0.0, [600.0, 500.0, 600.0], "step"), (f.end, [600.0, 500.0, 600.0], "step")])
    S_ = C.STOREY
    steps = {nm_: [] for nm_ in ("Low", "Mid", "TowerA", "TowerB", "Sky")}
    for j, n_st in enumerate((4, 6, 8, 10)):                        # 3.1-3.4: the towers' first storeys
        for nm_ in ("TowerA", "TowerB", "Sky"):
            steps[nm_].append((t(3, 1 + j), n_st))
    for j in range(3):                                              # 7.1-7.3: the podiums
        steps["Low"].append((t(7, 1 + j), j + 1))
    for j in range(9):                                              # 7.1-9.1: the slabs, a storey a quarter
        steps["Mid"].append((t(7, 1 + j), j + 1))
    for j in range(11):                                             # 9.1-11.3: the towers to 21 storeys
        for nm_ in ("TowerA", "TowerB"):
            steps[nm_].append((t(9, 1 + j), 11 + j))
    for j in range(16):                                             # 9.1-12.4: the core to 26
        steps["Sky"].append((t(9, 1 + j), 11 + j))
    for j in range(12):                                             # 13.1-14.2.5: the core on the eighths to 38
        steps["Sky"].append((t(13, 1 + 0.5 * j), 27 + j))
    for nm_, ev in steps.items():
        h_final = C.SETS[nm_][2]
        ev = [(tt, [2000.0, C.rise_size(min(n * S_, h_final + 0.6)), 2000.0]) for tt, n in ev]
        b.lock(f"sdf/cityIn{nm_}/node/{nm_}Rise/size", ev, [2000.0, C.rise_size(0.0), 2000.0], ease=0.07)
    for i, tt in enumerate(sorted({tt for ev in steps.values() for tt, _ in ev})):
        c = b.clap(f"storey{i:03d}", tt + 0.05, release=0.22)
        for nm_ in ("Low", "Mid", "TowerA", "TowerB", "Sky"):
            if any(abs(tt - t2) < 1e-6 for t2, _ in steps[nm_]):
                f.route(c, f"sdf/cityIn{nm_}/look/edge/intensity", 4.0)
    c = b.clap("city", t(3), release=0.8)
    f.route(c, "post/bloom/intensity", 1.2)
    f.route(c, "temporal/mosh/amount", 0.35)

    # ---- 4.1: every roof slams down (the houses landed without them) and stays; 4.3: the trees ------------------------
    f.track("sdf/row/node/rowRoof/translation", [(0.0, [0.0, 200.0, 0.0], "step"), (t(4) - 0.14, [0.0, 3.2, 0.0], "easeIn"),
                                               (t(4), [0.0, 0.0, 0.0], "step")])
    f.track("sdf/faller/node/fallRoof/translation", [(0.0, [0.0, 200.0, 0.0], "step"), (f.end, [0.0, 200.0, 0.0], "step")])
    c = b.clap("roofs", t(4), release=0.45)
    f.route(c, "sdf/row/look/edge/intensity", 8.0)
    f.route(c, "post/bloom/intensity", 0.9)
    f.route(c, "camera/exposure/compensation", 0.8)
    f.route(c, "temporal/mosh/amount", 0.25)
    f.track("sdf/lamps/node/treeGrow/scale", [(0.0, 0.0, "step"), (t(4, 3) - 0.001, 0.0, "easeOut"), (t(4, 3) + 0.16, 1.0, "step")])

    # ---- 5-8: the lamps on the eighths, alternating sides ---------------------------------------------------------
    lamp = [(0.0, [0.0, 0.0, 0.0], "step")]
    for i in range(14):
        lamp.append((t(5, 1 + i * 0.5), [IN.LOT * (i // 2 + 1) - 2.0 + (i % 2) * 6.5, 6.0, 30.0], "step"))
    lamp.append((t(9), [IN.LOT * IN.LOTS + 8.0, 6.0, 30.0], "step"))
    f.track("sdf/lamps/node/lampClip/size", lamp)
    c = b.clap("bassIn", t(5), release=0.7)          # the bass enters: a stab of light and the lamps' first pop
    f.route(c, "camera/exposure/compensation", 1.0)
    f.route(c, "post/bloom/intensity", 0.8)
    f.route(c, "sdf/street/look/edge/intensity", 5.0)
    g_bass = b.gate("iBass", t(5), t(16, 4))
    b.pulse("sdf/lamps/surface/2/emission", 2.5, "eighth", g_bass)
    b.pulse("sdf/row/surface/4/emission", 1.5, "eighth", g_bass)
    for nm_ in ("Low", "Mid", "TowerA", "TowerB", "Sky"):
        b.pulse(f"sdf/cityIn{nm_}/surface/5/emission", 1.6, "eighth", g_bass)

    # ---- 7: a car lands in every driveway on the eighths, outward from the hero lot -----------------------------------
    cars = [(0.0, [0.0, 0.0, 0.0], "step")]
    for k in range(1, IN.LOTS + 1):
        tk = t(7, 1 + 0.5 * (k - 1))
        cars.append((tk, [IN.LOT * k + 2.0, 3.0, 30.0], "step"))
        c = b.clap(f"carLand{k}", tk, release=0.25)
        f.route(c, "sdf/parked/look/edge/intensity", 6.0)
        f.route(c, "sdf/parked/surface/2/emission", 5.0)
    f.track("sdf/parked/node/carClip/size", cars)
    b.pulse("sdf/street/look/edge/intensity", 2.0, "quarter", b.gate("iQuarter", t(1), t(16, 4)))

    # ---- 9-12: the hero house rises, then its parts fly in, faster each bar; the neighbours mutate --------------
    rise = [(0.0, [0.0, 0.0, 0.0], "step")]
    for i in range(4):
        tb = t(9, 1 + i)
        rise += [(tb - 0.001, [6.0, 1.45 * i, 6.0], "easeOut"), (tb + 0.14, [6.0, 1.45 * (i + 1), 6.0], "step")]
    rise.append((t(10), [6.0, 6.2, 6.0], "step"))
    f.track("sdf/heroShell/node/heroRise/size", rise)
    f.track("sdf/heroShell/node/heroRiseAt/translation", [(k[0], [1.175, k[1][1], -2.1], k[2]) for k in rise])
    order = ["path", "fence", "mailbox", "porch", "frontDoor", "livWin", "bedWin", "roofL", "roofR", "chimney"]
    slots = [t(10, 1), t(10, 3), t(11, 1), t(11, 2), t(11, 3), t(11, 4), t(12, 1), t(12, 1.5), t(12, 2), t(12, 3)]
    by = {p[0]: p for p in IN.HERO_PARTS}
    for o in ("heroParts", "heroRoof"):
        b.key(f"nodes/{o}/visible", 0.0, 0.0)
        b.key(f"nodes/{o}/visible", t(10) - 0.35, 1.0)
    for nm_, tt in zip(order, slots):
        off = list(by[nm_][2])
        obj = "heroRoof" if nm_ in IN.ROOF_PARTS else "heroParts"
        f.track(f"sdf/{obj}/node/hp_{nm_}/translation", [(0.0, off, "step"), (tt - 0.001, off, "easeOut"),
                                                          (tt + 0.22, [0.0, 0.0, 0.0], "step")])
        c = b.clap(f"part_{nm_}", tt + 0.2, release=0.25)
        f.route(c, f"sdf/{obj}/look/edge/intensity", 6.0)
    f.track("sdf/heroTree/node/heroTree/scale", [(0.0, 0.0, "step"), (t(12, 3) - 0.001, 0.0, "easeOut"), (t(12, 3) + 0.2, 1.0, "step")])
    g_riser = b.gate("iRiser", t(9), t(16, 4))
    b.pulse("sdf/heroShell/look/edge/intensity", 4.0, "eighth", g_riser)
    b.pulse("sdf/heroParts/look/edge/intensity", 3.0, "eighth", g_riser)
    b.pulse("sdf/heroRoof/look/edge/intensity", 3.0, "eighth", g_riser)
    b.pulse("sdf/row/look/edge/intensity", 2.5, "quarter", g_riser)

    # ---- 13-16: the chop flashes every window; corruption builds on the sixteenths ---------------------------------
    for bar in range(13, 17):
        c = b.clap(f"chop{bar}", t(bar), release=0.9)
        f.route(c, "sdf/row/surface/4/emission", 8.0)
        f.route(c, "sdf/heroShell/surface/4/emission", 8.0)
        for nm_ in ("Low", "Mid", "TowerA", "TowerB", "Sky"):
            f.route(c, f"sdf/cityIn{nm_}/surface/5/emission", 5.0)
        f.route(c, "post/lens/chromaticAberration", 0.4)
    for k_, bar in enumerate(range(13, 17)):
        f.event("riserBar", time=t(bar), hold=G.BAR1 - 0.06 if bar < 16 else G.BAR1 * 0.75 - 0.06, attack=0.03, release=0.03,
                curve="linear", strength=0.3 + 0.25 * k_)
    f.route("grid.song.sixteenth", "temporal/mosh/amount", 0.32, depth="grid.song.riserBar")
    f.route("grid.song.sixteenth", "temporal/mosh/shift", 9.0, depth="grid.song.riserBar")
    f.track("post/bloom/intensity", [(0.0, 0.0), (t(13), 0.0, "smooth"), (t(16, 3.5), 0.7, "step"), (t(16, 4), 0.0, "step")], mode="add")

    # ---- 16.4, the gap: the world freezes and goes dark but for the lit front window; the camera keeps rushing ----
    gap = t(16, 4)
    for o in ("street", "row", "faller", "lamps", "parked", "heroParts", "heroRoof", "heroTree") + tuple(f"cityIn{nm_}" for nm_ in ("Low", "Mid", "TowerA", "TowerB", "Sky")):
        f.track(f"sdf/{o}/look/edge/intensity", [(0.0, 1.0, "step"), (gap - 0.001, 1.0, "step"), (gap + 0.03, 0.12, "smooth"),
                                                 (ENTRY, 0.12, "step")], mode="multiply")
    f.track("sdf/heroShell/look/edge/intensity", [(0.0, 1.0, "step"), (gap, 1.0, "smooth"), (ENTRY - 0.12, 0.35, "step")], mode="multiply")
    f.track("sdf/heroShell/surface/2/emission", [(0.0, [2.0, 1.4, 0.7], "step"), (t(16), [2.0, 1.4, 0.7], "smooth"),
                                                 (gap, [4.0, 2.8, 1.4], "smooth"), (ENTRY - 0.02, [12.0, 8.4, 4.2], "step"),
                                                 (ENTRY, [2.0, 1.4, 0.7], "step")])
    b.val += [(gap - 0.02, 1.0, "step"), (gap, 0.55, "step"), (ENTRY, 1.0, "step")]
    return names
