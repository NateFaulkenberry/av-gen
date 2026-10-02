"""All You Got, art pass 3: the intro, owner bars 0-16 (0-37.43 s), on Builder3 (03-art-pass-3-addendum.md, sections
23-26). One continuous camera: down the road in the count-in, rising over the street as it builds, gliding along it,
then turning on the house and accelerating at its lit front window. The window fills the frame at the end of the
one-beat gap (16.4) and the cut inside lands exactly on the downbeat of bar 17 (37.4312 s on the grid: the audio's
attack measures 37.41-37.45 s, so frame 1123 at 30 fps, 37.433 s, is the first frame inside).

The music, beat by beat:
  bar 0       four road dashes stamp down, one a beat, out of the black (construction starts with the road)
  1.1         SPLASH: the street's grid bursts outward from the house's lot, the kerbs light, the first houses land
  1.2-2.3     a pair of houses on both sides of the street lands on every quarter note, lot after lot outward
  3.1-3.4     the stab: the far city shoots up, in steps
  4.1-4.4     the trees sprout; the roofs flip once
  5-8         the bass: street lamps pop up along the pavements on the eighths, alternating sides; the towers keep
              rising through the sustained note; the windows flicker on the off-beats
  9-12        the riser: the house we will enter rises out of the ground (9.1-10.1) and its parts fly in on the eighths,
              faster each bar; the neighbours mutate (roofs flip and twist, the whole row shudders)
  13-16       the 'all you got' chop: every window flashes on each bar's downbeat; corruption builds on the sixteenths
  16.4        the gap: the world freezes and goes dark but for the lit window, and the camera keeps rushing at it
  17.1        through the window: the cut on the downbeat
"""

from __future__ import annotations

import intro3 as IN
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

    # ---- the camera: one continuous glide, accelerating into the window --------------------------------------
    keys = [(0.0, (-46.0, 2.0, 12.6)), (t(1), (-45.6, 2.2, 12.8)), (t(5), (-36.0, 6.8, 18.0)), (t(9), (-22.0, 8.6, 18.4)),
            (t(13), (-9.5, 6.4, 15.0)), (t(15), (-3.2, 3.6, 9.8)), (t(16, 2.5), (-1.3, 2.3, 6.0)),
            (t(16, 4), (-0.75, 1.72, 4.0)), (ENTRY, (-0.6, 1.53, 2.65))]
    look = [(0.0, (-20.0, 0.8, 12.0)), (t(1), (-20.0, 1.0, 12.0)), (t(2), (-12.0, 2.5, 9.0)), (t(5), (-2.0, 3.5, 6.0)),
            (t(9), (0.8, 3.4, 1.5)), (t(13), (0.2, 2.6, 2.0)), (t(15), (-0.5, 1.9, 2.0)), (t(16, 3), (-0.6, 1.6, 2.0)),
            (ENTRY, (-0.6, 1.53, 0.0))]
    b.glide("street", keys, look, nodes_keys=("street",), fov=58.0)
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
        fall += [(tl - 0.2, [x, 34.0, 0.0], "step"), (tl - 0.001, [x, 0.0, 0.0], "easeIn"), (tl + 0.07, [x, -0.25, 0.0], "smooth"),
                 (tl + 0.16, [x, 0.0, 0.0], "step")]
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

    # ---- 3.1-3.4: the far city shoots up; it keeps rising through the bass's sustained note (bars 5-8) ----------
    tower = [(0.0, 0.0), (t(3) - 0.001, 0.0, "easeOut"), (t(3) + 0.22, 19.0, "smooth"), (t(3, 2), 17.0, "easeOut"),
             (t(3, 2) + 0.15, 22.0, "smooth"), (t(3, 3), 22.0, "easeOut"), (t(3, 3) + 0.15, 26.0, "smooth"),
             (t(3, 4), 26.0, "easeOut"), (t(3, 4) + 0.15, 30.0, "smooth"), (t(5), 30.0, "smooth"), (t(9), 46.0, "smooth"),
             (t(13), 52.0, "smooth"), (t(16, 4), 60.0, "step")]
    towerb = [(0.0, 0.0), (t(3, 2) - 0.001, 0.0, "easeOut"), (t(3, 2) + 0.2, 12.0, "smooth"), (t(3, 4), 12.0, "easeOut"),
              (t(3, 4) + 0.15, 18.0, "smooth"), (t(5), 18.0, "smooth"), (t(9), 30.0, "smooth"), (t(16, 4), 38.0, "step")]
    for nm_, ks, w in (("towerA", tower, 3.0), ("towerB", towerb, 2.4)):
        f.track(f"sdf/skyline/node/{nm_}/size", [(k[0], [w, k[1] / 2, w], k[2] if len(k) > 2 else "smooth") for k in ks])
        f.track(f"sdf/skyline/node/{nm_}t/translation", [(k[0], [0.0, k[1] / 2, 0.0], k[2] if len(k) > 2 else "smooth") for k in ks])
    f.track("sdf/skyline/node/towerAw/size", [(k[0], [3.1, k[1] / 2 - 0.4 if k[1] > 1 else 0.0, 3.1], k[2] if len(k) > 2 else "smooth") for k in tower])
    f.track("sdf/skyline/node/towerAwt/translation", [(k[0], [0.0, k[1] / 2, 0.0], k[2] if len(k) > 2 else "smooth") for k in tower])
    # the riser: the towers stutter on the sixteenths, harder each bar (a city that cannot hold its shape)
    for nm_ in ("towerA", "towerB"):
        f.route("grid.song.sixteenth", f"sdf/skyline/node/{nm_}/size", 4.0, component=1, depth="grid.song.riserBar")
    c = b.clap("city", t(3), release=0.8)
    f.route(c, "post/bloom/intensity", 1.2)
    f.route(c, "temporal/mosh/amount", 0.35)
    f.route(c, "sdf/skyline/look/edge/intensity", 8.0)

    # ---- 4.1-4.4: the trees sprout; the roofs flip once -----------------------------------------------------------
    f.track("sdf/lamps/node/treeGrow/scale", [(0.0, 0.0, "step"), (t(4) - 0.001, 0.0, "easeOut"), (t(4) + 0.2, 1.15, "smooth"),
                                              (t(4) + 0.45, 1.0, "step")])
    roof = [(0.0, [0.0, 0.0, 0.0], "step"), (t(4, 3) - 0.001, [0.0, 0.0, 0.0], "easeOut"), (t(4, 3) + 0.12, [0.0, 0.0, 180.0], "smooth"),
            (t(4, 4) - 0.001, [0.0, 0.0, 180.0], "easeOut"), (t(4, 4) + 0.12, [0.0, 0.0, 360.0], "step")]

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
    b.pulse("sdf/skyline/surface/5/emission", 2.0, "eighth", g_bass)
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
    b.key("nodes/heroParts/visible", 0.0, 0.0)
    b.key("nodes/heroParts/visible", t(10) - 0.35, 1.0)
    for nm_, tt in zip(order, slots):
        off = list(by[nm_][2])
        f.track(f"sdf/heroParts/node/hp_{nm_}/translation", [(0.0, off, "step"), (tt - 0.001, off, "easeOut"),
                                                              (tt + 0.22, [0.0, 0.0, 0.0], "step")])
        c = b.clap(f"part_{nm_}", tt + 0.2, release=0.25)
        f.route(c, "sdf/heroParts/look/edge/intensity", 6.0)
    f.track("sdf/heroTree/node/heroTree/scale", [(0.0, 0.0, "step"), (t(12, 3) - 0.001, 0.0, "easeOut"), (t(12, 4) + 0.1, 1.12, "smooth"),
                                                (t(13), 1.0, "step")])
    # the neighbours mutate: their roofs twist on the off-beats of bars 9-12 and flip on each bar's 4
    for bar in range(9, 13):
        for j, ang in enumerate((25.0, -25.0)):
            tt = t(bar, 2.5 + 2 * j)
            prev = roof[-1][1]
            roof += [(tt - 0.001, prev, "easeOut"), (tt + 0.09, [0.0, prev[1] + ang, prev[2]], "step")]
        tt = t(bar, 4)
        prev = roof[-1][1]
        roof += [(tt - 0.001, prev, "easeOut"), (tt + 0.12, [0.0, prev[1], prev[2] + 180.0], "step")]
    prev = roof[-1][1]
    roof += [(t(13) - 0.001, prev, "easeOut"), (t(13) + 0.15, [0.0, 0.0, 720.0], "step")]
    f.track("sdf/row/node/rowRoof/rotation", roof)
    # the whole row shudders sideways on the sixteenths of the riser's last bars (a digital tremor, not a bounce)
    g_riser = b.gate("iRiser", t(9), t(16, 4))
    f.track("sdf/row/transform/position", [(0.0, [0.0, 0.0, 0.0], "step"), (t(13), [0.0, 0.0, 0.0], "step")] +
            [(t(13) + i * EIGHTH / 2, [(0.18 if i % 3 == 0 else -0.11 if i % 3 == 1 else 0.0) * (1 + i / 24), 0.0, 0.0], "step")
             for i in range(1, 28)] + [(t(16, 4), [0.0, 0.0, 0.0], "step")])
    b.pulse("sdf/heroShell/look/edge/intensity", 4.0, "eighth", g_riser)
    b.pulse("sdf/heroParts/look/edge/intensity", 3.0, "eighth", g_riser)
    b.pulse("sdf/row/look/edge/intensity", 2.5, "quarter", g_riser)

    # ---- 13-16: the chop flashes every window; corruption builds on the sixteenths ---------------------------------
    for bar in range(13, 17):
        c = b.clap(f"chop{bar}", t(bar), release=0.9)
        f.route(c, "sdf/row/surface/4/emission", 8.0)
        f.route(c, "sdf/heroShell/surface/4/emission", 8.0)
        f.route(c, "sdf/skyline/surface/5/emission", 6.0)
        f.route(c, "post/lens/chromaticAberration", 0.4)
    for k_, bar in enumerate(range(13, 17)):
        f.event("riserBar", time=t(bar), hold=G.BAR1 - 0.06 if bar < 16 else G.BAR1 * 0.75 - 0.06, attack=0.03, release=0.03,
                curve="linear", strength=0.3 + 0.25 * k_)
    f.route("grid.song.sixteenth", "temporal/mosh/amount", 0.32, depth="grid.song.riserBar")
    f.route("grid.song.sixteenth", "temporal/mosh/shift", 9.0, depth="grid.song.riserBar")
    f.track("post/bloom/intensity", [(0.0, 0.0), (t(13), 0.0, "smooth"), (t(16, 3.5), 0.7, "step"), (t(16, 4), 0.0, "step")], mode="add")

    # ---- 16.4, the gap: the world freezes and goes dark but for the lit front window; the camera keeps rushing ----
    gap = t(16, 4)
    for o in ("street", "row", "faller", "lamps", "skyline", "heroParts", "heroTree"):
        f.track(f"sdf/{o}/look/edge/intensity", [(0.0, 1.0, "step"), (gap - 0.001, 1.0, "step"), (gap + 0.03, 0.12, "smooth"),
                                                 (ENTRY, 0.12, "step")], mode="multiply")
    f.track("sdf/heroShell/look/edge/intensity", [(0.0, 1.0, "step"), (gap, 1.0, "smooth"), (ENTRY - 0.12, 0.35, "step")], mode="multiply")
    f.track("sdf/heroShell/surface/2/emission", [(0.0, [2.0, 1.4, 0.7], "step"), (t(16), [2.0, 1.4, 0.7], "smooth"),
                                                 (gap, [4.0, 2.8, 1.4], "smooth"), (ENTRY - 0.02, [12.0, 8.4, 4.2], "step"),
                                                 (ENTRY, [2.0, 1.4, 0.7], "step")])
    b.val += [(gap - 0.02, 1.0, "step"), (gap, 0.55, "step"), (ENTRY, 1.0, "step")]
    return names
