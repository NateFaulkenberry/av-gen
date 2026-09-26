"""The cut: every shot of Glowmere Valley 3, placed on the musical grid.

Written against three things, in this order:
  1. the directives (directives.py) -- what each segment should feel like;
  2. where the cast actually is: `avgen_cast_trace` on this project (build/gv3/cast-*.json). The
     aliens decide their own way, so a shot of an alien is a shot of where the simulation put it,
     aimed live at its node so a small divergence still frames it;
  3. the location scout (build/gv3/scout.mov) -- which viewpoints read as a place.

Positions are world metres (x east, z south, y up). `g(x, z, h)` is h metres above the ground or
the water at (x, z), asked of the engine (ground.py). Lenses are focal lengths on a 36x24 sensor.
"""

import math

from . import music
from .rig import Rig, Shot

B = music.bar
BEAT = music.beat

# Places the cut returns to.
ELDER = (-12.0, 52.0)            # the heart: 16 m, gold gills under a violet cap
ELDER_GILLS = [-12.0, 13.0, 52.0]
ELDER_BODY = [-12.0, 10.0, 52.0]
LANTERN = [-46.0, 11.0, -28.0]
BLOOM_CAP = [-62.0, 10.5, 118.0]
UMBRA = [-66.0, 10.0, 166.0]
STATION = [8.4, 28.6, 71.4]      # where the saucer hovers over horse-11 (cast trace, 169.5 s on)


# ---- the river's own course, for shots that travel with the water ----------------------------------
RIVER = [(-44.0, 15.0, -352.0), (-58.0, 12.8, -286.0), (-33.0, 10.7, -220.0), (6.0, 8.6, -154.0),
         (27.0, 6.4, -92.0), (5.0, 4.3, -34.0), (-32.0, 2.2, 20.0), (-41.0, 0.1, 76.0), (-13.0, -2.0, 132.0),
         (29.0, -4.2, 188.0), (45.0, -6.4, 244.0), (24.0, -8.6, 300.0), (11.0, -11.0, 352.0)]


def _chaikin(points, iterations=3):
    out = list(points)
    for _ in range(iterations):
        nxt = [out[0]]
        for a, b in zip(out, out[1:]):
            nxt.append(tuple(a[k] + 0.25 * (b[k] - a[k]) for k in range(3)))
            nxt.append(tuple(a[k] + 0.75 * (b[k] - a[k]) for k in range(3)))
        nxt.append(out[-1])
        out = nxt
    return out


_COURSE = _chaikin(RIVER)
_ARC = [0.0]
for _a, _b in zip(_COURSE, _COURSE[1:]):
    _ARC.append(_ARC[-1] + math.hypot(_b[0] - _a[0], _b[2] - _a[2]))


def river_at_z(z):
    """(x, surface y, z, arc length) of the river's centreline where it crosses z."""
    for (a, b), s in zip(zip(_COURSE, _COURSE[1:]), _ARC):
        if a[2] <= z <= b[2]:
            u = (z - a[2]) / max(b[2] - a[2], 1e-6)
            return (a[0] + u * (b[0] - a[0]), a[1] + u * (b[1] - a[1]), z, s + u * math.hypot(b[0] - a[0], b[2] - a[2]))
    raise ValueError(z)


def river_path(z0, z1, height, steps=6, side=0.0):
    """Points along the river from z0 to z1, `height` above its surface, `side` metres off-centre."""
    pts = []
    for i in range(steps + 1):
        z = z0 + (z1 - z0) * i / steps
        x, y, _, _ = river_at_z(z)
        xn, _, _, _ = river_at_z(min(z + 1.0, 351.0))
        # a perpendicular offset, so a camera can ride along one bank
        dx, dz = xn - x, 1.0
        n = math.hypot(dx, dz)
        pts.append([x + side * dz / n, y + height, z - side * dx / n])
    return pts


def _timed(t0, t1, points):
    return [(t0 + (t1 - t0) * i / (len(points) - 1), p) for i, p in enumerate(points)]


# ---- the cut -------------------------------------------------------------------------------------
# Iteration 2. Each shot's comment records what changed from iterations 0 and 1 and why
# (05-quality.md F11+).
def build(ground):
    g = ground.above
    shots = []

    def add(sid, t0, t1, rig, **notes):
        rig.name = f"{sid} {notes.get('purpose', '')}"[:60]
        shots.append(Shot(sid, t0, t1, rig, **notes))

    # 1 cold open -- Nocturne. Out of black on the first kick, low among the ferns on the east bank,
    # drifting toward the elder. v1: closer, so the elder is the frame and not a detail in it.
    r = Rig("", 24.0)
    r.move(0.0, B(5), g(14, 88, 1.3), g(4, 74, 1.6), [-12.0, 12.5, 52.0], [-12.0, 12.0, 52.0], interp="linear")
    add("s01", 0.0, B(5), r, segment="cold-open", purpose="Nocturne: low through the ferns toward the elder",
        subject="the elder", camera="24 mm, 1.3 m, east bank", movement="slow forward drift",
        music="cut in on the first downbeat (0.480 s)", effects="heartbeat in the gills", modulation="heartbeat, breath")

    # 2 riff groove -- the valley introduced, 2-bar shots.
    # v1: the water-dominated river shot is replaced by the elder across the pool, whose dark water
    # now carries its reflection (the research: at night the water's job is to reflect the lights).
    r = Rig("", 50.0)
    r.move(B(5), B(7), g(-62, 80, 2.5), g(-61, 71, 2.5), [-12.0, 10.0, 52.0], [-12.0, 10.0, 52.0], interp="linear")
    add("s02", B(5), B(7), r, segment="riff-groove", purpose="The elder across the pool, from the west bank",
        subject="the elder and its reflection", camera="50 mm, 2.5 m, west bank", movement="lateral truck",
        music="the riff enters on bar 5")

    r = Rig("", 35.0, follow="ember", follow_offset=(4.5, 2.4, 5.5), aim="ember", aim_offset=(0.0, 1.9, 0.0),
            clearance=1.2)
    add("s03", B(7), B(9), r, segment="riff-groove", purpose="Ember walks the west bank",
        subject="ember", camera="35 mm follow, eye height", movement="tracks alongside", music="2 bars")

    r = Rig("", 28.0, position=g(-70, -41, 1.8), target=LANTERN)
    r.move(B(9), B(11), g(-70, -41, 1.8), g(-66, -44, 1.8), LANTERN, LANTERN, interp="linear")
    add("s04", B(9), B(11), r, segment="riff-groove", purpose="The lantern from the hollow, ferns in front",
        subject="the lantern mushroom", camera="28 mm, low, through ferns", movement="lateral truck (parallax)",
        music="2 bars")

    # v1: the camera was 1.2 m down in the ferns and saw nothing; now 3 m up, with the elder behind
    # the horse on the same line of sight. v2: the 50 mm cut the elder's cap off at the top of the
    # frame; a 30 mm aimed 8.6 m above the horse puts the horse in the lower third under the whole cap.
    r = Rig("", 30.0, position=g(22, 78, 3.0), aim="horse-11", aim_offset=(0.0, 8.6, 0.0))
    add("s05", B(11), B(13), r, segment="riff-groove", purpose="The white horse grazing, the elder behind it",
        subject="horse-11 (what the valley will lose)", camera="30 mm, 3 m, fixed eye, live aim",
        movement="still", music="2 bars")

    r = Rig("", 85.0)
    r.move(B(13), B(15), g(-14, 165, 3.0), g(-4, 165, 3.0), ELDER_BODY, ELDER_BODY, interp="linear")
    add("s06", B(13), B(15), r, segment="riff-groove", purpose="Long-lens wide: the elder against the valley",
        subject="the elder and the valley walls", camera="85 mm from 115 m south, low", movement="slow lateral truck",
        music="2 bars, into the pull-back")

    # 3 first pull-back -- a shadow passes. v1 put the camera under the path, and the saucer went
    # over its head and out of the top of the frame (v1 review). v2: from 112 m north of the elder,
    # looking south past it; the lower, shorter path (cast.py) crosses the frame left to right above
    # the cap for about 1.4 s, 135 m out. 12 m west of the elder's meridian, because Tide stands 10 m
    # south of it through the pull-back and its head filled the bottom of the frame.
    eye = g(-24, -58, 2.0)
    r = Rig("", 24.0, position=eye, target=[-12.0, eye[1] + 110.7 * math.tan(math.radians(14.0)), 52.0])
    add("s07", B(15), B(17), r, segment="first-pullback", purpose="A shape crosses the sky over the elder",
        subject="the saucer (flyby), the elder's cap", camera="24 mm, 112 m north of the elder, 14 deg up",
        movement="still",
        music="the pull-back: drums and top out", effects="the flyby", modulation="light drained to 0.3")

    # 4 groove 2 -- waking; 4-bar shots, travelling with the aliens.
    r = Rig("", 30.0)
    r.move(B(17), B(21), g(-62, 30, 6.0), g(-57, 38, 2.2), ELDER_BODY, ELDER_BODY, interp="linear")
    add("s08", B(17), B(21), r, segment="groove-2", purpose="The return: settling toward the pool and the elder",
        subject="the elder across the pool", camera="30 mm, west bank", movement="crane down and in",
        music="the drums return on bar 17")

    r = Rig("", 35.0, follow="ember", follow_offset=(-5.0, 2.3, -4.0), aim="ember", aim_offset=(0.0, 1.9, 0.0),
            clearance=1.2)
    add("s09", B(21), B(25), r, segment="groove-2", purpose="Following Ember through the ferns",
        subject="ember", camera="35 mm follow", movement="tracks behind and beside",
        music="4 bars; the kick gap at bar 24 beat 4 before the cut", modulation="held breath at 44.3 s")

    # v1: the river travel (a frame of water) is replaced by Tide, standing still in the north: an
    # orbit gives the stillness a move.
    r = Rig("", 40.0, follow="tide", aim="tide", aim_offset=(0.0, 1.8, 0.0), clearance=1.2)
    r.orbit(B(25), B(29), radius=7.5, height=2.2, a0_deg=40.0, a1_deg=85.0, steps=6)
    add("s10", B(25), B(29), r, segment="groove-2", purpose="Tide, still, in the north",
        subject="tide", camera="40 mm, orbit at 7.5 m", movement="orbit 45 deg",
        music="4 bars from the sub-phrase at bar 25")

    # v1: closer, lower, a longer lens -- and it came out a portrait on black sky with nothing to
    # watch. v2: from Vane's north-east, so what Vane watches is in the frame: the elder's gold, 57 m
    # beyond. Vane sits idle at (10.3, -2.2) for the whole shot (cast trace).
    r = Rig("", 50.0, target=[1.4, 12.4, 19.5])
    r.move(B(29), B(33), g(22, -18, 2.0), g(20, -16, 2.0), [1.4, 12.4, 19.5], [1.4, 12.4, 19.5], interp="linear")
    add("s11", B(29), B(33), r, segment="groove-2", purpose="Vane by the river, watching the elder",
        subject="vane, the elder beyond", camera="50 mm, eye height, over Vane's shoulder", movement="slow push-in",
        music="4 bars to the lift")

    # 5 lift -- something stirs. v1: the crane looks up the valley toward the aurora instead of down
    # onto a model of it.
    r = Rig("", 24.0)
    r.move(B(33), B(37), g(-8, 33, 1.2), g(-8, 30, 16.0), [5.0, 6.0, -120.0], [5.0, 8.0, -120.0], interp="easeInOut")
    add("s12", B(33), B(37), r, segment="lift", purpose="The first crane: up out of the ferns, up the valley",
        subject="the river and the valley to the north", camera="24 mm, east bank", movement="crane up 1.2 -> 16 m",
        music="the lift: the body arrives on bar 33")

    # v1: from ahead and low, the valley falling away behind Rook, instead of Rook's back on a bare slope.
    r = Rig("", 28.0, follow="rook", follow_offset=(-4.0, 1.4, -8.0), aim="rook", aim_offset=(0.0, 1.6, 0.0),
            lag=0.3, clearance=1.0)
    add("s13", B(37), B(41), r, segment="lift", purpose="Rook climbs the west slope, the valley below",
        subject="rook", camera="28 mm, ahead and low", movement="follows", music="4 bars into the arrival")

    # 6 arrival -- the valley lights up. v1: the old vantage was behind trees; this one is the
    # scout's "elder from the south" over the river, which read as a place.
    r = Rig("", 60.0)
    r.move(B(41), B(45), g(-6, 196, 6.0), g(4, 194, 6.0), [-12.0, 10.0, 52.0], [-12.0, 10.0, 52.0], interp="linear")
    add("s14", B(41), B(45), r, segment="arrival", purpose="The first grand wide: the lit valley, far and low",
        subject="the valley, the elder, the aurora", camera="60 mm from 145 m south, 6 m up",
        movement="slow lateral truck", music="the shimmer arrives on bar 41", effects="aurora up, sparkle",
        modulation="the arc rises in a beat")

    r = Rig("", 24.0, position=g(-54, 125, 1.2), target=BLOOM_CAP)
    add("s15", B(45), B(47), r, segment="arrival", purpose="Spores falling from the bloom",
        subject="the bloom mushroom", camera="24 mm, looking up", movement="still", music="2 bars", effects="spores")

    r = Rig("", 18.0, position=g(-15, 59, 1.4), target=[-12.0, 15.0, 51.0])
    add("s16", B(47), B(49), r, segment="arrival", purpose="Under the elder: the gold gills",
        subject="the elder's gills", camera="18 mm, from the stem's foot", movement="still",
        music="2 bars", modulation="heartbeat")

    # 7 melodic plateau -- communion: 8-bar takes.
    r = Rig("", 35.0, follow="elder-2-cap", aim="elder-2-cap", aim_offset=(0.0, 9.0, 0.0), clearance=2.0)
    # v2: 4 m up, the orbit passed a grazing horse at a few metres and its rump filled a quarter of
    # the frame for seconds (96.5 s); 7.5 m clears the herd.
    r.orbit(B(49), B(57), radius=40.0, height=7.5, a0_deg=20.0, a1_deg=70.0, steps=6)
    add("s17", B(49), B(57), r, segment="melodic-plateau", purpose="A slow orbit of the elder, Vane nearby",
        subject="the elder (vane at its east side)", camera="35 mm at 40 m, 7.5 m up", movement="orbit 50 deg over 8 bars",
        music="8 bars from bar 49")

    # v1: the downstream water travel is replaced by Sage in the western grove, among the fireflies.
    # v2: from the south-east the follow passed behind a tree fern and lost Sage mid-shot (111 s), and
    # higher did not clear it. From the north-west (three offsets tried on five stills), Sage is seen
    # throughout, with the elder's gold and the aurora behind.
    r = Rig("", 40.0, follow="sage", follow_offset=(-4.5, 3.0, -5.0), aim="sage", aim_offset=(0.0, 1.8, 0.0),
            lag=0.4, clearance=1.2)
    add("s18", B(57), B(65), r, segment="melodic-plateau", purpose="Sage in the grove, among the fireflies",
        subject="sage", camera="40 mm follow", movement="tracks", music="8 bars from the sub-phrase at bar 57",
        effects="fireflies")

    # v1: the camera goes to the land side, so the background is the east bank and not the river.
    r = Rig("", 40.0, follow="vane", follow_offset=(-6.0, 2.4, 2.0), aim="vane", aim_offset=(0.0, 1.9, 0.0),
            lag=0.3, clearance=1.2)
    add("s19", B(65), B(73), r, segment="melodic-plateau", purpose="Walking with Vane under the elder",
        subject="vane", camera="40 mm follow", movement="tracks alongside", music="8 bars to the lead")

    # 8 lead forward -- looking up. v1: the aurora lives at the horizon, so the camera goes low and
    # south of Vane and looks north, with Vane against it.
    # v2: at 0.9 m the camera went through a leaf and the frame was flat blue at 137 s; 1.7 m and a
    # little closer.
    r = Rig("", 20.0, follow="vane", follow_offset=(0.6, 1.7, 5.2), aim="vane", aim_offset=(0.0, 2.6, -1.0),
            clearance=1.0)
    add("s20", B(73), B(77), r, segment="lead-forward", purpose="Low behind Vane, the aurora beyond",
        subject="vane and the aurora", camera="20 mm, 1.7 m", movement="follows",
        music="the lead comes forward on bar 73", effects="aurora")

    r = Rig("", 28.0)
    r.move(B(77), B(81), g(-30, 150, 2.5), g(-26, 130, 2.5), [-10.0, 40.0, -80.0], [-10.0, 40.0, -80.0], interp="linear")
    add("s21", B(77), B(81), r, segment="lead-forward", purpose="The aurora over the valley",
        subject="the sky up the valley", camera="28 mm, low", movement="slow push north",
        music="4 bars to the suspension", effects="aurora at its brightest before the drop")

    # 9 suspension -- the visitor. Appears over the south rim at 148.45 s, crosses to its station.
    r = Rig("", 50.0, position=g(-26, -12, 3.0), target=[30.0, 70.0, 260.0])
    add("s22", B(81), B(85), r, segment="suspension", purpose="Locked off: the saucer comes over the rim",
        subject="the saucer, far", camera="50 mm, looking south over the elder", movement="still",
        music="the shimmer is cut on bar 81", effects="the approach", modulation="the light drains")

    # v1: from north of the elder looking south, so its cap fills the foreground and the saucer comes
    # on beyond it -- the old frame was black sky and a saucer.
    r = Rig("", 35.0, position=g(-8, 30, 2.5), target=[5.0, 30.0, 150.0])
    add("s23", B(85), B(89), r, segment="suspension", purpose="The saucer comes on beyond the elder's cap",
        subject="the elder's cap, the saucer approaching", camera="35 mm, north of the elder", movement="still",
        music="4 bars to the break")

    # 10 submerged break -- dark, heavy. v1: under the elder's cap, the saucer settling over its rim.
    r = Rig("", 20.0, position=g(-18, 45, 1.5), aim="visitor", aim_offset=(0.0, -4.0, 0.0))
    add("s24", B(89), B(91), r, segment="submerged-break", purpose="From under the elder: the saucer over its rim",
        subject="the saucer, the elder's gills", camera="20 mm, under the cap, live aim", movement="still",
        music="the low-pass break on bar 89", modulation="light 0.25, fog 1.8")

    # v1: a fixed eye behind Ember with the saucer's station as the target -- and Ember was walking
    # the bank 35 deg outside the lens. v2: the camera rides 6 m behind Ember, looking at the station,
    # so the two are on one line whatever Ember does -- above Ember's head (3.3 m), or the camera looks
    # up past the body and the head is a grey dome on the bottom edge.
    r = Rig("", 28.0, follow="ember", follow_offset=(-7.0, 3.6, 2.5), target=[8.4, 8.0, 71.4], clearance=1.2)
    add("s25", B(91), B(93), r, segment="submerged-break", purpose="Ember watches from across the water",
        subject="ember, the saucer beyond", camera="28 mm over the shoulder, riding with Ember", movement="follows",
        music="2 bars to the riser")

    # 11 riser -- the lift; the cutting compresses with the roll. v1: every shot is a different view
    # of the one event, and nothing aims at the horse after it is retired (177.70 s).
    r = Rig("", 24.0, position=g(-58, 84, 2.2), target=[4.0, 16.0, 70.0])
    add("s26", B(93), B(94), r, segment="riser", purpose="The beam lights",
        subject="the beam, the horse, the elder", camera="24 mm from the west bank", movement="still",
        music="the riser begins; the beam at 170.35 s", effects="the beam")

    r = Rig("", 20.0, position=g(-1, 80, 1.0), aim="visitor", aim_offset=(0.0, -3.0, 0.0))
    add("s27", B(94), B(95), r, segment="riser", purpose="Up the beam from beside the horse",
        subject="the beam and the saucer", camera="20 mm, 1 m, looking up", movement="still", music="1 bar")

    r = Rig("", 50.0, follow="vane", follow_offset=(2.5, 2.2, 3.5), aim="vane", aim_offset=(0.0, 2.6, 0.0),
            clearance=1.2)
    add("s28", B(95), BEAT(95, 3), r, segment="riser", purpose="Vane sees it", subject="vane",
        camera="50 mm close", movement="follows", music="half a bar: the roll doubles")

    r = Rig("", 85.0, position=g(-14, 75, 8.5), aim="horse-11", aim_offset=(0.0, 1.0, 0.0))
    add("s29", BEAT(95, 3), B(96), r, segment="riser", purpose="The horse rises, glowing, side on",
        subject="horse-11", camera="85 mm, side on, 22 m, live aim", movement="still", music="half a bar")

    r = Rig("", 50.0, position=g(-62, 44, 1.6), target=[-2.0, 14.0, 60.0])
    add("s30", B(96), BEAT(96, 3), r, segment="riser", purpose="The elder beside the beam",
        subject="the elder and the beam", camera="50 mm from the west", movement="still", music="half a bar")

    # v2: from the south rather than the west. Looking east, the skybox's pale body sat beside the
    # saucer and read as debris; and the west view repeated s27.
    r = Rig("", 70.0, position=g(10, 118, 3.0), target=[8.5, 24.0, 71.4])
    add("s31", BEAT(96, 3), BEAT(96, 4), r, segment="riser", purpose="The horse under the saucer",
        subject="the horse and the saucer", camera="70 mm, from the south, 47 m", movement="still", music="one beat")

    r = Rig("", 70.0, position=g(40, 78, 6.0), target=[8.5, 25.0, 71.4])
    add("s32", BEAT(96, 4), B(97), r, segment="riser", purpose="The horse fades into the saucer",
        subject="horse-11 vanishing", camera="70 mm, side on from the east, fixed", movement="still",
        music="the last beat of the roll; gone 177.70 s")

    # 12 drop -- rebuilt. v1: a fast lateral move across the west bank with the lit valley, the elder
    # and the saucer, so the release is felt as width and speed at once.
    r = Rig("", 28.0)
    r.move(B(97), B(99), g(-70, 98, 2.5), g(-48, 102, 2.8), [-12.0, 11.0, 52.0], [-10.0, 12.0, 54.0], interp="linear")
    add("s33", B(97), B(99), r, segment="drop", purpose="The drop: the valley lights up, wide and fast",
        subject="the elder, the saucer, the valley", camera="28 mm, west bank", movement="fast lateral truck",
        music="the crash on bar 97", effects="flash", modulation="the arc steps to its brightest")

    # v1: from far, so the saucer rises through the whole frame -- but at 134 m/s straight up it was
    # out of the top in a second, and the rest was empty sky. v2: it leaves up and away over the north
    # rim (cast.py EXIT), and the camera looks north from the south bank: it lifts beside the elder's
    # cap, climbs to the left and shrinks toward the aurora for the whole shot.
    eye = g(15, 140, 3.0)
    r = Rig("", 24.0, position=eye, target=[-11.8, eye[1] + 32.5, 40.0])
    add("s34", B(99), B(101), r, segment="drop", purpose="The saucer leaves over the north rim, the elder below",
        subject="the saucer, the elder", camera="24 mm from the south bank, 18 deg up", movement="still",
        music="bar 99")

    # v2: v1 repeated s15's bloom from s15's angle. The drop is every hero lit, so it goes to one the
    # film had not shown: the umbra, 115 m south-west of the elder.
    r = Rig("", 24.0, target=UMBRA)
    r.move(B(101), B(103), g(-54, 178, 1.2), g(-50, 174, 1.2), UMBRA, UMBRA, interp="linear")
    add("s35", B(101), B(103), r, segment="drop", purpose="The umbra lit, its spores in the air", subject="the umbra",
        camera="24 mm, low", movement="lateral", music="2 bars", effects="sparkle")

    r = Rig("", 35.0, follow="ember", follow_offset=(3.0, 2.2, 4.0), aim="ember", aim_offset=(0.0, 3.0, 0.0),
            clearance=1.2)
    add("s36", B(103), B(105), r, segment="drop", purpose="Ember looks up", subject="ember",
        camera="35 mm", movement="follows", music="2 bars to the sub-phrase")

    # v1: the crane ends looking out across the valley, not down onto it.
    r = Rig("", 24.0)
    r.move(B(105), B(109), g(-24, 64, 1.5), g(-24, 64, 30.0), [-12.0, 10.0, 52.0], [-12.0, 8.0, -60.0], interp="easeInOut")
    add("s37", B(105), B(109), r, segment="drop", purpose="Crane up past the elder's cap and out over the valley",
        subject="the elder, then the valley", camera="24 mm", movement="crane 1.5 -> 30 m, tilting up and out",
        music="4 bars from the sub-phrase at bar 105")

    r = Rig("", 35.0, follow="vane", follow_offset=(-5.0, 2.4, 4.0), aim="vane", aim_offset=(0.0, 2.2, 0.0),
            lag=0.3, clearance=1.2)
    add("s38", B(109), B(113), r, segment="drop", purpose="Vane walks to where the horse was taken",
        subject="vane", camera="35 mm follow", movement="tracks", music="4 bars; the push at bar 111")

    # v1: low, south of the elder, looking north with the aurora behind it -- over the river, whose
    # ripples filled the bottom half of the frame. v2 (three vantages tried on stills): the arrival's
    # grand wide again, from 150 m south, now that the river is a mirror -- it curves up the valley to
    # the elder between the two hills, under the aurora. A rhyme, deliberately: the place first seen
    # lit at bar 41 seen again rebuilt. s14 trucks sideways; this pushes in over the last phrase.
    r = Rig("", 50.0)
    r.move(B(113), B(121), g(-2, 203, 4.0), g(-5, 183, 4.2), [-12.0, 14.0, 52.0], [-12.0, 14.0, 52.0], interp="easeInOut")
    add("s39", B(113), B(121), r, segment="drop", purpose="The last wide: the elder and the valley rebuilt",
        subject="the elder, the river, the valley, the aurora", camera="50 mm, 4 m, 150 m south", movement="slow push-in (20 m)",
        music="the final 8 bars")

    # 13 tail -- afterglow: one image, then black on the last hit.
    r = Rig("", 20.0, position=g(-17, 60, 1.3), target=[-12.0, 14.5, 51.0])
    add("s40", B(121), 225.5, r, segment="tail", purpose="The elder alone, then black",
        subject="the elder's gills", camera="20 mm, below", movement="still",
        music="kick and clap; black on the last hit at 224.79 s", modulation="heartbeat alone")

    return shots
