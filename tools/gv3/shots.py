"""The cut: one authored composition for every span of the Director's Song Mode cut.

**The times are not here.** When each shot starts and ends is the engine Director's (songcut.py:
`avgen_song_cut` on this project, snapped to the fitted grid). This file says what each span shows
-- the rig, and what it is for -- keyed by where the span starts on the grid ("41.1" is bar 41 beat
1; "open" is the first shot, out of the pre-roll). `build()` refuses a Director span with no
composition here, and a composition no span uses, so the two cannot drift apart silently.

Written against, in this order:
  1. the revision plan (docs/glowmere-valley-3/revision/03-revision-plan.md section 3): the film,
     section by section, and the UFO events E1-E5 at their places and times;
  2. where the cast actually is: `avgen_cast_trace` on this project. The aliens decide their own way,
     so a shot of an alien is a live follow or aim at its node, never a fixed frame it may leave;
  3. the first pass's shots (first-pass ids in brackets), kept where the evaluator and the owner
     found them good: the cold open, the pool, the flyby, the grand wides' rhyme (first pass s14 and
     s39), the suspension's locked-off wides, the riser's views of the lift, the drop's first move.

Aliens lead at most a fifth of the film (brief section 9: part of the world, not the whole of it):
they appear in the world's wides and reacting to its events, and `lead` on every shot is what
`make_glowmere_valley_3.py` totals and refuses above the budget.

Positions are world metres (x east, z south, y up). `g(x, z, h)` is h metres above the ground or the
water at (x, z), asked of the engine (ground.py). Lenses are focal lengths on a 36x24 sensor.
"""

import math

from .rig import Rig, Shot

# ---- places the cut returns to ------------------------------------------------------------------------
ELDER_CAP = [-14.5, 15.5, 52.7]      # 16 m, gold gills under a violet cap; ground 3.7 at its foot
ELDER_BODY = [-12.0, 10.0, 52.0]
LANTERN = [-44.8, 13.1, -25.6]
BLOOM_CAP = [-59.3, 11.2, 117.7]
UMBRA = [-64.2, 8.4, 166.7]
SPIRE = [67.7, 15.3, -103.7]
VEIL = [77.5, 2.9, 198.7]
CAIRN = [-150.2, 37.2, -59.2]
RIDGE = [130.5, 24.9, -189.4]
SCREE = [-175.8, 26.3, 95.6]
EMBER_CAP = [175.7, 15.2, 149.8]
EAST_MEADOW = [74.0, 9.0, 13.0]      # the herd's horses, re-homed on the 5.1 ha meadow (characters.md)
WEST_MEADOW = [-70.0, 8.3, 3.0]      # the two cows, on the flat half of the 3.9 ha meadow
E1_PLACE = [-20.0, 25.0, -190.0]     # the survey sweeps x -50..10 along z -190, 30 m up (the UFO plan)
# The river pair's region centre, 14 m up: gv3-cast's (-69, 6), radius 20 (gv3/cast 0ff69eea), where
# cow-12 and cow-23 graze after the re-homing. The plan's (-55, 40) held one animal, and E4 never
# beamed. 54.1 frames the whole region at the station's height, so the station may be anywhere in it.
E4_PLACE = [-69.0, 14.0, 6.0]

COMPOSITIONS = {}


def at(label):
    """Register the composition for the Director's span that starts at `label`."""
    def register(fn):
        if label in COMPOSITIONS:
            raise ValueError(f"two compositions for span {label}")
        COMPOSITIONS[label] = fn
        return fn
    return register


def still(focal, eye, target):
    return Rig("", focal, position=eye, target=target)


def moving(t0, t1, focal, eye0, eye1, target0, target1=None, interp="linear"):
    r = Rig("", focal)
    r.move(t0, t1, eye0, eye1, target0, target1 if target1 is not None else target0, interp=interp)
    return r


def arc(t0, t1, focal, g, x, z, h, target, degrees, rise=0.0):
    """A truck around a still subject: the eye, `h` m over the ground at (x, z), swings `degrees` about
    the vertical through `target` (positive: counter-clockwise seen from above), keeping its distance
    and rising `rise` metres; the target stays put. The parallax changes the whole frame for the
    whole shot, which a push of a few metres toward a subject 20-40 m off does not (iteration 2: the
    Critic's novelty read such pushes as static from the first frame, and 38 of 73 shots pushed in)."""
    a = math.radians(degrees)
    dx, dz = x - target[0], z - target[2]
    x1 = target[0] + dx * math.cos(a) - dz * math.sin(a)
    z1 = target[2] + dx * math.sin(a) + dz * math.cos(a)
    return moving(t0, t1, focal, g(x, z, h), g(x1, z1, h + rise), target)


def aim_at(focal, eye, node, offset, smoothing):
    return Rig("", focal, position=eye, aim=node, aim_offset=offset, smoothing=smoothing)


def follow(focal, node, offset, aim_offset, clearance=1.2, smoothing="walker", aim=None):
    return Rig("", focal, follow=node, follow_offset=offset, aim=aim or node, aim_offset=aim_offset,
               clearance=clearance, smoothing=smoothing)


# ==== 1 cold open (bars 1-4) ===========================================================================
@at("open")
def nocturne(t0, t1, g):
    # [s01] Out of black on the first kick, low among the ferns on the east bank, drifting toward the
    # elder: one 4-bar move, a contrast with everything after it.
    r = moving(t0, t1, 24.0, g(14, 88, 1.3), g(4, 74, 1.6), [-12.0, 12.5, 52.0], [-12.0, 12.0, 52.0])
    return r, dict(lead="hero", purpose="Nocturne: low through the ferns toward the elder", subject="the elder",
                   camera="24 mm, 1.3 m, east bank", movement="slow forward drift",
                   music="cut in on the first downbeat (0.480 s)", effects="heartbeat in the gills")


# ==== 2 riff groove (bars 5-14): meet the valley; E1 far up it ==========================================
@at("5.1")
def pool(t0, t1, g):
    # [s02] The elder across the pool, whose dark water carries its reflection.
    r = moving(t0, t1, 50.0, g(-62, 80, 2.5), g(-61, 71, 2.5), ELDER_BODY)
    return r, dict(lead="hero", purpose="The elder across the pool, from the west bank",
                   subject="the elder and its reflection", camera="50 mm, 2.5 m, west bank",
                   movement="lateral truck", music="the riff enters on bar 5")


@at("7.1")
def e1_far_survey(t0, t1, g):
    # E1. Far up the valley the scout settles and a thin beam lights over a field (bar 8.3), 350 m
    # beyond the elder; it sweeps east until the riff's phrase end (bar 13) and lifts nothing. From
    # the east bank, low, through a 50 mm: the elder 5 deg left of centre, the whole sweep 5-15 deg
    # right of it, never behind the cap (the scout hovers 6.7 deg up, the cap's top is at 6.8).
    r = moving(t0, t1, 50.0, g(30, 150, 3.0), g(28, 150, 3.0), [-31.8, 14.7, -40.2])
    return r, dict(lead="event", purpose="E1: the elder, and far beyond it a beam lights over a field",
                   subject="the elder; the scout's survey (E1), tiny, 350 m beyond",
                   camera="50 mm from the east bank, 107 m south-east of the elder, low", movement="slow lateral truck",
                   music="2 bars; the beam lights at bar 8.3", effects="the survey beam (E1)")


@at("9.1")
def horse_grazing(t0, t1, g):
    # [s05] The white horse -- what the valley will lose -- grazing under the elder's cap, from the
    # south-east: through a 55 mm, the horse on the lower third (0.13 of the frame) and the whole cap
    # above it. Iteration 2's 30 mm from here read as the same framing as the east-bank travel (26.1)
    # and the orbit (49.1), 35 mm lenses a few metres away (the Critic: eyes within 18 m, views within
    # 12 deg, fields of view within 8); iteration 4's first try from the north-east put the elder as near
    # the lens as the horse and hid the horse behind a rock (seen in the clip). Its path is the same on
    # both casts.
    r = aim_at(55.0, g(22, 78, 3.0), "horse-11", (0.0, 5.0, 0.0), "still")
    return r, dict(lead="animal", purpose="The white horse grazing under the elder's cap",
                   subject="horse-11 (what the valley will lose)", camera="55 mm, 3 m, south-east, fixed eye, live aim",
                   movement="still", music="2 bars")


@at("11.1")
def lantern_hollow(t0, t1, g):
    # [s04] The lantern through the ferns; Rook works this side of the valley and may cross it.
    r = moving(t0, t1, 28.0, g(-70, -41, 1.8), g(-66, -44, 1.8), LANTERN)
    return r, dict(lead="hero", purpose="The lantern from the hollow, ferns in front", subject="the lantern mushroom",
                   camera="28 mm, low, through ferns", movement="lateral truck (parallax)",
                   music="2 bars to the riff's phrase end")


@at("13.1")
def bloom_by_the_river(t0, t1, g):
    # A hero answering its own layer: the bloom's cap breathes on the bass glide.
    r = moving(t0, t1, 35.0, g(-40, 130, 2.0), g(-41, 128, 2.0), [-60.0, 9.0, 118.0])
    return r, dict(lead="hero", purpose="The bloom by the river, breathing", subject="the bloom mushroom",
                   camera="35 mm, 2 m, across the river", movement="slow drift", music="1 bar")


@at("14.1")
def cap_rim_from_the_north(t0, t1, g):
    # The elder's cap from the north, its rim against the stars, the gold beating with the kick a bar
    # before the pull-back takes the drums away. (From the east, iteration 1, horse-11 grazed in front
    # of the lens and filled the frame.) A rising move, so the frame changes in its one bar.
    r = moving(t0, t1, 20.0, g(-10, 36, 1.0), g(-9, 37, 2.2), [-13.0, 17.0, 48.0], [-13.0, 17.5, 48.0])
    return r, dict(lead="hero", purpose="The elder's cap from the north, its rim against the stars",
                   subject="the elder's cap and gills", camera="20 mm, 1-2 m, north of the stem",
                   movement="rising 1.2 m", music="1 bar, into the pull-back", modulation="heartbeat")


# ==== 3 first pull-back (bars 15-16): E2 ===============================================================
@at("15.1")
def e2_flyby(t0, t1, g):
    # [s07] E2, the flyby: from 112 m north of the elder looking south past it, a shape crosses the
    # frame over the cap while the music holds its breath -- the promise the drop keeps. Panned 16 deg
    # east of the first pass's frame (iteration 3): the saucer comes in from the east, slowly at first,
    # and was 13 deg outside the frame when its crossing began; now it is in from that instant and
    # for 1.92 s instead of 1.28, and the cap sits on the right third with the sky it comes out of on
    # the left.
    eye = g(-24, -58, 2.0)
    ahead = [-12.0 - eye[0], 110.7 * math.tan(math.radians(14.0)), 52.0 - eye[2]]
    pan = math.radians(16.0)
    r = still(24.0, eye, [eye[0] + math.cos(pan) * ahead[0] + math.sin(pan) * ahead[2], eye[1] + ahead[1],
                          eye[2] - math.sin(pan) * ahead[0] + math.cos(pan) * ahead[2]])
    return r, dict(lead="event", purpose="E2: a shape crosses the sky over the elder",
                   subject="the saucer (flyby, E2), the elder's cap",
                   camera="24 mm, 112 m north of the elder, 14 deg up, the cap on the right third", movement="still",
                   music="the pull-back: drums and top out", effects="the flyby", modulation="light drained")


# ==== 4 groove 2 (bars 17-32): the light returns ========================================================
@at("17.1")
def the_return(t0, t1, g):
    # [s08] Settling toward the pool and the elder as the drums return. Two bars now: the first
    # pass's four held 2.5 s after the evaluator's last new information.
    r = moving(t0, t1, 30.0, g(-62, 30, 6.0), g(-58, 36, 2.6), ELDER_BODY)
    return r, dict(lead="hero", purpose="The return: settling toward the pool and the elder",
                   subject="the elder across the pool", camera="30 mm, west bank", movement="crane down and in",
                   music="the drums return on bar 17")


@at("19.1")
def first_wave(t0, t1, g):
    # The first wave of light runs out from the elder through the small mushrooms on the downbeat:
    # low over the ground cover north-east of it, so the wave comes toward the lens. (Iteration 1's
    # vantage was over the pool, and the water filled half the frame.)
    r = moving(t0, t1, 35.0, g(14, 34, 1.0), g(9, 38, 1.0), [-12.0, 5.0, 52.0])
    return r, dict(lead="world", purpose="The first wave runs through the mushrooms toward us",
                   subject="the small mushrooms, the elder beyond", camera="35 mm, 1 m, north-east of the elder",
                   movement="push, 6 m", music="2 bars; the wave on the downbeat", effects="bar wave")


@at("21.1")
def rook_investigates(t0, t1, g):
    # An alien investigates a flickering cluster: Rook inspects what it finds on the ground.
    r = follow(35.0, "rook", (-4.5, 2.2, 5.5), (0.0, 1.3, 0.0))
    return r, dict(lead="alien", purpose="Rook investigates a flickering cluster", subject="rook",
                   camera="35 mm follow, beside and behind", movement="tracks", music="2 bars")


@at("23.1")
def herd_east_meadow(t0, t1, g):
    # The herd on the east meadow, the elder's gold far beyond it. Four metres up: at 1.8 m a fern
    # filled the frame (iteration 1).
    r = moving(t0, t1, 40.0, g(98, -10, 4.0), g(95, -7, 4.0), [60.0, 9.0, 20.0])
    return r, dict(lead="animal", purpose="The herd grazing on the east meadow", subject="the horses, the elder far off",
                   camera="40 mm, 4 m, north-east of the meadow", movement="slow lateral", music="2 bars")


@at("25.1")
def lantern_close(t0, t1, g):
    # The lantern's clap flare, close, from its east side, pushing in.
    r = moving(t0, t1, 35.0, g(-30, -20, 2.5), g(-34, -21.5, 2.3), [-45.0, 10.5, -26.0])
    return r, dict(lead="hero", purpose="The lantern, close, flaring on the clap", subject="the lantern mushroom",
                   camera="35 mm, 15 m east of it", movement="push, 4 m", music="1 bar")


@at("26.1")
def east_bank_travel(t0, t1, g):
    # The one longer travel: down the east bank past the ferns, looking across the river at the
    # elder, so the whole middle of the valley slides by behind it. Eight metres further east than
    # iteration 1, whose path ran into horse-11 grazing.
    r = moving(t0, t1, 35.0, g(32, 98, 2.5), g(30, 76, 2.5), [-12.0, 10.0, 52.0])
    return r, dict(lead="world", purpose="A travel down the east bank, the elder across the water",
                   subject="the valley floor, the elder", camera="35 mm, 2.5 m, east bank",
                   movement="lateral dolly, 22 m", music="3 bars")


@at("29.1")
def vane_watches(t0, t1, g):
    # [s11] Vane crosses the east meadow through the grazing herd, the elder's gold on the horizon:
    # riding 11 m north-east of her and 6 m up, looking south-west past her. Vane walks the left third,
    # the elder's cap stands upper right. (Iteration 2 rode 10 m south-east of her at 2.4 m, and
    # horse-20 walked past the lens and filled half the frame: the herd grazes within 5-8 m of her
    # path. Searched on both casts, gv3-cut's preview world and gv3-cast's, where her path is the
    # same: no animal nearer the lens than 6.7 m or in front of her, the eye 5.8 m over the ground.)
    r = follow(35.0, "vane", (7.07, 6.0, -8.43), (-12.0, 1.2, 2.0))
    return r, dict(lead="alien", purpose="Vane crosses the east meadow, the elder beyond", subject="vane, the elder beyond",
                   camera="35 mm, riding 11 m north-east of Vane and 6 m up, looking south-west past it",
                   movement="follows", music="2 bars")


@at("31.1")
def spire_east_bank(t0, t1, g):
    # A hero the first pass never showed: the spire on the east bank, the eye swinging 14 deg round
    # it at 24 m so the bank slides behind it (iteration 2's 8 m diagonal toward it read as a push,
    # static after 0.9 s).
    r = arc(t0, t1, 35.0, g, 52, -86, 2.0, [68.0, 14.5, -104.0], 14.0)
    return r, dict(lead="hero", purpose="The spire on the east bank", subject="the spire mushroom",
                   camera="35 mm, 2 m, south-west of it", movement="truck round it, 14 deg", music="2 bars to the lift")


# ==== 5 lift (bars 33-40): E3 ==========================================================================
@at("33.1")
def cairn_spur(t0, t1, g):
    # The body arrives: the cairn on its spur, its spores in the air.
    r = moving(t0, t1, 35.0, g(-125, -45, 2.0), g(-126, -48, 2.0), [-150.0, 35.0, -60.0])
    return r, dict(lead="hero", purpose="The cairn on the spur, its spores in the air", subject="the cairn mushroom",
                   camera="35 mm, 2 m, below the spur", movement="slow lateral", music="the lift: the body arrives on bar 33")


@at("35.1")
def e3_crane_reveal(t0, t1, g):
    # E3. [s12's crane] Up out of the ferns, up the valley -- and at the top, as the beam comes on
    # (bar 36.4), a column of light 290 m away, left of the river.
    r = moving(t0, t1, 24.0, g(-8, 33, 1.2), g(-8, 31, 14.0), [-10.0, 6.0, -150.0], [-10.0, 9.0, -150.0],
               interp="easeInOut")
    return r, dict(lead="event", purpose="E3: the crane rises to reveal a column of light up the valley",
                   subject="the river and the valley to the north; the scout's beam (E3)",
                   camera="24 mm, east bank", movement="crane up 1.2 -> 14 m", music="2 bars; the beam at bar 36.4",
                   effects="the scout's beam (E3)")


@at("37.1")
def e3_lift(t0, t1, g):
    # E3's lift, on bar 37: one animal rising into the scout's column, through a long lens from the
    # valley floor 200 m away.
    r = aim_at(100.0, g(-30, -70, 4.0), "scout", (0.0, -13.0, 0.0), "craft")
    return r, dict(lead="event", purpose="E3: an animal lifted into the column, far up the valley",
                   subject="the scout, its beam and the animal it lifts (E3)", camera="100 mm, 200 m off, live aim",
                   movement="still", music="the lift on bar 37", effects="the scout's beam")


@at("38.1")
def tide_sees_e3(t0, t1, g):
    # The aliens react: Tide, in the north of the valley, turned toward the column.
    r = follow(28.0, "tide", (3.0, 2.2, 5.0), (0.0, -10.0, 0.0), aim="scout", smoothing="fixed-target")
    return r, dict(lead="alien", purpose="Tide, and the column far up the valley beyond", subject="tide, E3 beyond",
                   camera="28 mm over Tide's shoulder, looking at the scout", movement="follows", music="1 bar")


@at("39.1")
def e3_taken(t0, t1, g):
    # E3's animal fades into the scout and is gone (70.4-71.9 s): a second long lens, from the west
    # slope, 125 m off.
    r = aim_at(70.0, g(-125, -150, 3.0), "scout", (0.0, -9.0, 0.0), "craft")
    return r, dict(lead="event", purpose="E3: the animal fades into the scout", subject="the scout and its beam (E3)",
                   camera="70 mm from the west slope, live aim", movement="still", music="1 bar",
                   effects="the scout's beam")


@at("40.1")
def ridge_north_east(t0, t1, g):
    # The ridge, high in the north-east, the last bar before the valley lights up: a push, so the
    # frame changes in its bar.
    r = moving(t0, t1, 35.0, g(108, -166, 3.0), g(114, -172, 3.0), [131.0, 24.0, -189.0])
    return r, dict(lead="hero", purpose="The ridge mushroom high in the north-east", subject="the ridge mushroom",
                   camera="35 mm, 3 m", movement="push, 8 m", music="1 bar into the arrival")


# ==== 6 arrival (bars 41-48): the valley lights up ======================================================
@at("41.1")
def first_grand_wide(t0, t1, g):
    # [s14] The first grand wide: the lit valley, far and low. Rhymes with the last (117.1).
    r = moving(t0, t1, 60.0, g(-6, 196, 6.0), g(2, 194.4, 6.0), ELDER_BODY)
    return r, dict(lead="world", purpose="The first grand wide: the lit valley, far and low",
                   subject="the valley, the elder, the aurora", camera="60 mm from 145 m south, 6 m up",
                   movement="slow lateral truck", music="the shimmer arrives on bar 41", effects="aurora up, sparkle")


@at("43.1")
def valley_floor_lit(t0, t1, g):
    # Down among it: the valley floor lit, low and wide, the mushrooms turning toward cyan; a truck of
    # 6.5 m across the view, so the near ferns slide past the far walls.
    r = arc(t0, t1, 24.0, g, 60, 140, 2.0, [-30.0, 8.0, 20.0], 2.5)
    return r, dict(lead="world", purpose="The valley floor lit, low and wide", subject="the valley floor, the far walls",
                   camera="24 mm, 2 m, south-east of the elder", movement="lateral truck, 6.5 m", music="2 bars")


@at("45.1")
def bloom_spores(t0, t1, g):
    # [s15] Spores falling from the bloom.
    r = still(24.0, g(-54, 125, 1.2), BLOOM_CAP)
    return r, dict(lead="hero", purpose="Spores falling from the bloom", subject="the bloom mushroom",
                   camera="24 mm, looking up", movement="still", music="1 bar", effects="spores")


@at("46.1")
def veil_at_the_water(t0, t1, g):
    # The veil at the water's edge, close enough to read (iteration 1: tiny behind the ferns), the eye
    # swinging 16 deg round it so the water behind it turns.
    r = arc(t0, t1, 35.0, g, 88, 207, 1.5, [77.5, 3.2, 198.7], 16.0)
    return r, dict(lead="hero", purpose="The veil at the water's edge", subject="the veil mushroom",
                   camera="35 mm, 1.5 m, 13 m south-east of it", movement="truck round it, 16 deg", music="1 bar")


@at("47.1")
def gold_gills(t0, t1, g):
    # [s16] Under the elder: the gold gills, the eye circling 25 deg round the stem as it rises, so the
    # gills wheel overhead in the one bar (a straight crane of 1.4 m still read as static).
    r = arc(t0, t1, 18.0, g, -15, 59, 1.0, [-12.0, 15.0, 51.0], 25.0, rise=1.2)
    return r, dict(lead="hero", purpose="Under the elder: the gold gills", subject="the elder's gills",
                   camera="18 mm, from the stem's foot", movement="circling up the stem, 25 deg", music="1 bar",
                   modulation="heartbeat")


@at("48.1")
def scree_glimpse(t0, t1, g):
    r = still(35.0, g(-150, 110, 2.0), [-177.0, 25.0, 96.0])
    return r, dict(lead="hero", purpose="The scree on the west slope, lit", subject="the scree mushroom",
                   camera="35 mm", movement="still", music="half a bar")


@at("48.3")
def ember_cap_glimpse(t0, t1, g):
    r = still(50.0, g(155, 160, 2.5), [176.0, 14.0, 150.0])
    return r, dict(lead="hero", purpose="The ember mushroom on the east terrace, lit", subject="the ember-cap mushroom",
                   camera="50 mm", movement="still", music="half a bar into the plateau")


# ==== 7 melodic plateau (bars 49-72): the valley notices its visitors; E4 ================================
@at("49.1")
def elder_orbit(t0, t1, g):
    # [s17] An orbit of the elder, 40 m out and 7.5 m up, clear of the herd.
    r = Rig("", 35.0, follow="elder-2-cap", aim="elder-2-cap", aim_offset=(0.0, 9.0, 0.0), clearance=2.0,
            smoothing="still")
    r.orbit(t0, t1, radius=40.0, height=7.5, a0_deg=20.0, a1_deg=33.0, steps=3)
    return r, dict(lead="hero", purpose="An orbit of the elder", subject="the elder",
                   camera="35 mm at 40 m, 7.5 m up", movement="orbit 13 deg", music="bar 49: the lead shifts")


@at("51.1")
def herd_west_meadow(t0, t1, g):
    # The cows on the west meadow, the lantern's glow beyond them.
    r = moving(t0, t1, 40.0, g(-100, 30, 1.8), g(-99, 27, 1.8), [-62.0, 9.0, -8.0])
    return r, dict(lead="animal", purpose="Cows on the west meadow, the lantern beyond",
                   subject="the cows, the lantern", camera="40 mm, 1.8 m, west of the meadow",
                   movement="slow lateral", music="2 bars")


@at("53.1")
def bloom_far_bank(t0, t1, g):
    # The bloom from across the river, the eye swinging 6 deg round it at 40 m (a 2 m drift).
    r = arc(t0, t1, 50.0, g, -35, 150, 2.0, [-59.0, 9.0, 118.0], 6.0)
    return r, dict(lead="hero", purpose="The bloom from across the river", subject="the bloom mushroom",
                   camera="50 mm, 2 m, south-east of it", movement="truck round it, 6 deg", music="1 bar")


@at("54.1")
def e4_arrives(t0, t1, g):
    # E4. The scout comes down over the west meadow and its beam lights (bar 56.3). Locked off, from
    # over the river south of the elder, with a slow push, aimed a quarter of the way from E4's region
    # toward the elder: the scout flies in at the upper left and settles just left of centre, and the
    # elder stands whole at the right (aimed at the region itself, its cap crossed the frame's edge).
    # The whole region is in frame at the station's height, so the station may be wherever its animals
    # are. (Iteration 2 chased the scout with a live aim: its approach and stop into the station read
    # as 0.20 deg of yaw HF.)
    aim = [p + 0.25 * (q - p) for p, q in zip(E4_PLACE, ELDER_CAP)]
    r = moving(t0, t1, 28.0, g(-24, 120, 3.0), g(-24, 115, 3.0), aim)
    return r, dict(lead="event", purpose="E4: the scout settles over the meadow beyond the elder",
                   subject="the scout (E4), the elder at the right", camera="28 mm, 3 m, from the south, locked on the region",
                   movement="slow push", music="3 bars; the beam at bar 56.3", effects="the scout's beam (E4)")


@at("57.1")
def e4_pair_lifted(t0, t1, g):
    # E4's lift, on the sub-phrase line: two animals rising together in one column, seen from 4 m over
    # the river 42 m east of the station, looking west across the water. A 35 mm aimed 10 m below the
    # scout holds the whole column on both casts: the scout's top at +0.72 (+0.66 on gv3-cast's) and the
    # pair's feet at -0.86 (-0.73) through a 35 mm; a 28 mm, so the column stays whole even where the
    # scout has no hero record and the Critic takes the saucer's larger bounds. Over water, so no undergrowth can stand in the
    # lens: iteration 3's eye on the dry ground north-east of the meadow was inside a plant whose
    # leaves filled the frame (and its 40 mm cut the scout's top).
    r = aim_at(28.0, g(-27, 17, 4.0), "scout", (0.0, -10.0, 0.0), "hover")
    return r, dict(lead="event", purpose="E4: two animals lifted together", subject="the scout's beam and the pair (E4)",
                   camera="28 mm, 4 m over the river 42 m east, live aim", movement="still", music="the lift on bar 57",
                   effects="the scout's beam")


@at("59.1")
def e4_aliens_watch(t0, t1, g):
    # The aliens watch: over Sage's shoulder at the column. Sage stops on the rise 39 m west of the
    # station as the pair lifts, turns to face it and stands, so the follow is still and only the
    # scout's sway reaches the aim. (Iteration 2 rode with Ember, who walks in bursts, and looked 33 m
    # past her: her stride was 0.24 deg of yaw HF.) If Sage goes to see the beam instead, she walks
    # away from the lens toward it and stays in frame. Offset searched on the trace: her head and
    # shoulders low on the right third, the scout and both animals in frame for the whole shot, the
    # eye 1.7 m over the rising ground behind her.
    r = follow(20.0, "sage", (-2.23, 3.6, -3.88), (0.0, -12.0, 0.0), aim="scout", smoothing="hover")
    return r, dict(lead="event", purpose="E4: Sage turns to watch the pair rise", subject="the column (E4), sage in front",
                   camera="20 mm over Sage's shoulder, looking up at the scout", movement="follows", music="2 bars")


@at("61.1")
def herd_after(t0, t1, g):
    # The valley goes on: the horses on the east meadow from its south-east, 3 m up, clear of the
    # tree that stood in the middle of iteration 1's frame.
    r = moving(t0, t1, 35.0, g(100, 30, 3.0), g(98, 26, 3.0), [70.0, 11.0, 10.0])
    return r, dict(lead="animal", purpose="The herd on the east meadow", subject="the horses",
                   camera="35 mm, 3 m, south-east of the meadow", movement="slow lateral", music="2 bars")


@at("63.1")
def umbra_listening(t0, t1, g):
    # The valley listening in close-ups: the umbra.
    r = moving(t0, t1, 24.0, g(-75, 180, 1.2), g(-73, 181, 1.2), [-64.0, 7.0, 167.0])
    return r, dict(lead="hero", purpose="The umbra, listening", subject="the umbra mushroom",
                   camera="24 mm, low, south-west of it", movement="slow lateral", music="2 bars")


@at("65.1")
def elder_spores(t0, t1, g):
    # The elder's spores drifting in its gold: the eye swinging 8 deg round the elder at 30 m and
    # rising half a metre, so the spores cross the cap (a 6 m push read as static from the first frame).
    r = arc(t0, t1, 50.0, g, -36, 70, 2.5, [-12.0, 12.0, 52.0], 8.0, rise=0.5)
    return r, dict(lead="hero", purpose="The elder's spores drifting in its gold", subject="the elder",
                   camera="50 mm, 2.5 m, south-west", movement="truck round it, 8 deg", music="a bar and a half")


@at("66.3")
def sage_grove(t0, t1, g):
    # [s18] Sage in the grove, among the fireflies, from the north-west.
    r = follow(40.0, "sage", (-4.5, 3.0, -5.0), (0.0, 1.8, 0.0))
    return r, dict(lead="alien", purpose="Sage in the grove, among the fireflies", subject="sage",
                   camera="40 mm follow", movement="tracks", music="2.5 bars", effects="fireflies")


@at("69.1")
def lantern_from_the_north(t0, t1, g):
    # The lantern from its north side, trucking (iteration 1's river-side vantage had a rock in front).
    r = moving(t0, t1, 35.0, g(-50, -48, 2.5), g(-44, -48, 2.5), [-45.0, 11.0, -26.0])
    return r, dict(lead="hero", purpose="The lantern from the north", subject="the lantern mushroom",
                   camera="35 mm, 2.5 m, 22 m north of it", movement="lateral truck, 6 m", music="2 bars")


@at("71.1")
def elder_from_the_north(t0, t1, g):
    # The elder through a long lens from the north, the southern sky over it.
    r = moving(t0, t1, 85.0, g(-20, -60, 4.0), g(-17, -60, 4.0), [-12.0, 10.0, 52.0])
    return r, dict(lead="hero", purpose="The elder through a long lens from the north", subject="the elder",
                   camera="85 mm, 112 m north", movement="slow lateral", music="2 bars to the lead")


# ==== 8 lead forward (bars 73-80): the aurora carries the lead ==========================================
@at("73.1")
def vane_aurora(t0, t1, g):
    # [s20] One alien looks up: low behind Vane, the aurora beyond.
    r = follow(20.0, "vane", (0.6, 1.7, 5.2), (0.0, 2.6, -1.0), clearance=1.0)
    return r, dict(lead="alien", purpose="Low behind Vane, the aurora beyond", subject="vane and the aurora",
                   camera="20 mm, 1.7 m", movement="follows", music="the lead comes forward on bar 73", effects="aurora")


@at("75.1")
def aurora_over_the_valley(t0, t1, g):
    # [s21] The aurora over the valley: a crane up from the ferns, tilting up the valley to the sky
    # (iteration 2 pushed north: one more push-in).
    r = moving(t0, t1, 28.0, g(-30, 150, 2.0), g(-30, 150, 6.5), [-10.0, 22.0, -80.0], [-10.0, 46.0, -80.0])
    return r, dict(lead="world", purpose="The aurora over the valley", subject="the sky up the valley",
                   camera="28 mm, from the ferns", movement="crane up 4.5 m, tilting up", music="2 bars", effects="aurora")


@at("77.1")
def valley_from_the_north(t0, t1, g):
    # The whole valley from its north end, low: the river winding down to the elder under the sky.
    r = moving(t0, t1, 50.0, g(-20, -290, 6.0), g(-18, -284, 6.0), [-10.0, 8.0, 60.0])
    return r, dict(lead="world", purpose="The valley from its north end, the river winding to the elder",
                   subject="the valley, the river, the elder far off", camera="50 mm, 6 m, the north end",
                   movement="slow push", music="2 bars", effects="aurora")


@at("79.1")
def spire_in_aurora(t0, t1, g):
    r = still(35.0, g(85, -95, 2.0), [68.0, 14.0, -104.0])
    return r, dict(lead="hero", purpose="The spire under the aurora, from the east", subject="the spire mushroom",
                   camera="35 mm, 2 m", movement="still", music="1 bar")


@at("80.1")
def tide_looks_up(t0, t1, g):
    r = follow(24.0, "tide", (2.0, 1.2, 3.0), (0.0, 3.2, 0.0), clearance=1.0)
    return r, dict(lead="alien", purpose="Tide looks up", subject="tide", camera="24 mm, low, in front",
                   movement="follows", music="1 bar, the last before the suspension")


# ==== 9 suspension (bars 81-88): E5 begins ==============================================================
@at("81.1")
def e5_over_the_rim(t0, t1, g):
    # [s22] Locked off: the saucer comes over the south rim, far, as the shimmer is cut.
    r = still(50.0, g(-26, -12, 3.0), [30.0, 70.0, 260.0])
    return r, dict(lead="event", purpose="E5: locked off, the saucer comes over the rim", subject="the saucer, far",
                   camera="50 mm, looking south over the elder", movement="still",
                   music="the shimmer is cut on bar 81", effects="the approach", modulation="the light drains")


@at("85.1")
def e5_beyond_the_cap(t0, t1, g):
    # [s23] The saucer comes on beyond the elder's cap.
    r = still(35.0, g(-8, 30, 2.5), [5.0, 30.0, 150.0])
    return r, dict(lead="event", purpose="E5: the saucer comes on beyond the elder's cap",
                   subject="the elder's cap, the saucer approaching", camera="35 mm, north of the elder",
                   movement="still", music="4 bars to the break")


# ==== 10 submerged break (bars 89-92) ====================================================================
@at("89.1")
def e5_under_the_cap(t0, t1, g):
    # [s24] From beside the elder's stem: the saucer settling over its rim. The eye swings 30 deg round
    # the stem, 11 m out, with a live aim at the saucer, so the rim slides off it: the saucer starts
    # behind the rim's edge and ends clear of it (a 5 m push read as static from the first frame).
    x, z = -19.0, 43.0
    a = math.radians(-30.0)
    x1 = -12.0 + (x + 12.0) * math.cos(a) - (z - 52.0) * math.sin(a)
    z1 = 52.0 + (x + 12.0) * math.sin(a) + (z - 52.0) * math.cos(a)
    r = Rig("", 20.0, aim="visitor", aim_offset=(0.0, -4.0, 0.0), smoothing="craft")
    r.move(t0, t1, g(x, z, 1.5), g(x1, z1, 1.9))
    return r, dict(lead="event", purpose="From under the elder: the saucer over its rim",
                   subject="the saucer, the elder's gills", camera="20 mm, beside the stem, live aim",
                   movement="swing round the stem, 30 deg", music="the low-pass break on bar 89",
                   modulation="light low, fog up")


@at("91.1")
def ember_watches(t0, t1, g):
    # [s25] Ember on the west bank as the saucer settles across the water: a fixed eye up the bank
    # behind her, 4 m over the ground, with a live aim at Ember led 15% of the way toward the saucer
    # (the offset, the same to a metre on both casts), so she walks the lower frame and the saucer
    # hangs above the far bank, beside the elder: 0.19-0.25 of the frame's height, 32 mm from 3 m up. Searched on both casts, where Ember walks different ways 26 m apart:
    # both in frame the whole shot in each. (Iteration 2 rode over her shoulder with a fixed target:
    # she stood in the bottom corner behind the ferns, and on gv3-cast's cast out of frame; an
    # over-the-shoulder that holds the saucer puts the eye on the rising bank behind her.)
    r = aim_at(32.0, g(-106, 36, 3.0), "ember", (13.0, 5.0, 6.0), "watch")
    return r, dict(lead="alien", purpose="Ember on the west bank, the saucer settling over the elder beyond",
                   subject="ember on the west bank, the saucer settling over the elder beyond",
                   camera="32 mm, up the west bank 18 m behind her, live aim", movement="still",
                   music="2 bars to the riser")


# ==== 11 riser (bars 93-96): E5's lift; the cutting compresses with the roll ================================
@at("93.1")
def e5_beam_lights(t0, t1, g):
    # [s26] The beam lights, from the west bank.
    r = aim_at(24.0, g(-58, 84, 2.2), "visitor", (0.0, -12.0, 0.0), "craft")
    return r, dict(lead="event", purpose="E5: the beam lights", subject="the beam, the horse, the elder",
                   camera="24 mm from the west bank, live aim", movement="still",
                   music="the riser begins; the beam on bar 93", effects="the beam")


@at("94.1")
def e5_up_the_beam(t0, t1, g):
    # [s27] Up the beam from beside the horse, 12 m off the station: at 6 m (iteration 1, the tuned
    # cast's station) the lens looked almost straight up and the saucer's sway read as yaw. The
    # hovering kind: at the craft's 0.6 s the sway still read 0.102 deg (iteration 2).
    r = aim_at(20.0, g(-8, 83, 1.0), "visitor", (0.0, -3.0, 0.0), "hover")
    return r, dict(lead="event", purpose="E5: up the beam from beside the horse", subject="the beam and the saucer",
                   camera="20 mm, 1 m, looking up", movement="still", music="1 bar")


@at("95.1")
def vane_sees_it(t0, t1, g):
    # [s28] Vane sees it.
    r = follow(50.0, "vane", (2.5, 2.2, 3.5), (0.0, 2.6, 0.0))
    return r, dict(lead="alien", purpose="Vane sees it", subject="vane", camera="50 mm close", movement="follows",
                   music="half a bar: the roll doubles")


@at("95.3")
def e5_horse_rises(t0, t1, g):
    # [s29] The horse rises, glowing, side on: the rise is the shot, so the aim is not smoothed.
    r = aim_at(85.0, g(-14, 75, 8.5), "horse-11", (0.0, 1.0, 0.0), "lifted")
    return r, dict(lead="event", purpose="E5: the horse rises, glowing, side on", subject="horse-11",
                   camera="85 mm, side on, 22 m, live aim", movement="still", music="half a bar")


@at("96.1")
def e5_elder_beside_the_beam(t0, t1, g):
    # [s30] The elder beside the beam.
    r = still(50.0, g(-62, 44, 1.6), [-2.0, 14.0, 60.0])
    return r, dict(lead="event", purpose="E5: the elder beside the beam", subject="the elder and the beam",
                   camera="50 mm from the west", movement="still", music="half a bar")


@at("96.3")
def e5_horse_under_the_saucer(t0, t1, g):
    # [s31] The horse under the saucer, from the south, against black sky.
    r = aim_at(70.0, g(10, 118, 3.0), "visitor", (0.0, -4.5, 0.0), "craft")
    return r, dict(lead="event", purpose="E5: the horse under the saucer", subject="the horse and the saucer",
                   camera="70 mm, from the south, 47 m", movement="still", music="one beat")


@at("96.4")
def e5_horse_fades(t0, t1, g):
    # [s32] The horse fades into the saucer on the roll's last beat, from the south-west through a
    # 50 mm (the beat before is 70 mm from the south: two frames that read as one). Aims at the craft,
    # never at the horse, which is retired on the frame of the drop.
    r = aim_at(50.0, g(-25, 95, 2.5), "visitor", (0.0, -3.5, 0.0), "craft")
    return r, dict(lead="event", purpose="E5: the horse fades into the saucer", subject="horse-11 vanishing",
                   camera="50 mm, from the south-west", movement="still", music="the last beat of the roll")


# ==== 12 drop (bars 97-120): the light rebuilt ===========================================================
@at("97.1")
def the_drop(t0, t1, g):
    # [s33] The drop: the valley lights up, wide and fast, the saucer over it.
    r = moving(t0, t1, 28.0, g(-70, 98, 2.5), g(-48, 102, 2.8), [-12.0, 11.0, 52.0], [-10.0, 12.0, 54.0])
    return r, dict(lead="world", purpose="The drop: the valley lights up, wide and fast",
                   subject="the elder, the saucer, the valley", camera="28 mm, west bank", movement="fast lateral truck",
                   music="the crash on bar 97", effects="flash")


@at("99.1")
def e5_leaves(t0, t1, g):
    # [s34] The saucer leaves over the north rim, the elder below: it lifts on bar 99.
    eye = g(15, 140, 3.0)
    r = still(24.0, eye, [-11.8, eye[1] + 32.5, 40.0])
    return r, dict(lead="event", purpose="E5: the saucer leaves over the north rim, the elder below",
                   subject="the saucer, the elder", camera="24 mm from the south bank, 18 deg up", movement="still",
                   music="bar 99: it lifts")


@at("100.1")
def umbra_lit(t0, t1, g):
    # [s35] The umbra lit, its spores in the air.
    r = moving(t0, t1, 24.0, g(-54, 178, 1.2), g(-52, 176, 1.2), UMBRA)
    return r, dict(lead="hero", purpose="The umbra lit, its spores in the air", subject="the umbra",
                   camera="24 mm, low", movement="lateral", music="1 bar", effects="sparkle")


@at("101.1")
def ember_cap_lit(t0, t1, g):
    # The ember mushroom on the east terrace, lit, pushing in. (Iteration 1 followed the saucer up into
    # the black sky: a near-black frame in the middle of the drop.)
    r = moving(t0, t1, 28.0, g(165, 158, 2.0), g(169, 155, 2.0), [175.7, 14.0, 149.8])
    return r, dict(lead="hero", purpose="The ember mushroom on the east terrace, lit", subject="ember-cap",
                   camera="28 mm, 2 m, 12 m west of it", movement="push, 5 m", music="1 bar")


@at("102.1")
def cairn_lit(t0, t1, g):
    r = still(50.0, g(-135, -78, 2.0), [-150.0, 35.0, -59.0])
    return r, dict(lead="hero", purpose="The cairn lit", subject="the cairn mushroom", camera="50 mm, below the spur",
                   movement="still", music="1 bar")


@at("103.1")
def ember_looks_up(t0, t1, g):
    # [s36] Ember looks up after it.
    r = follow(35.0, "ember", (3.0, 2.2, 4.0), (0.0, 3.0, 0.0))
    return r, dict(lead="alien", purpose="Ember looks up", subject="ember", camera="35 mm", movement="follows",
                   music="1 bar")


@at("104.1")
def spire_lit(t0, t1, g):
    # The spire lit, close from the north-west (iteration 1's 17 m left it a speck), the eye swinging
    # 20 deg round it and rising.
    r = arc(t0, t1, 28.0, g, 62, -112, 1.5, [67.7, 14.0, -103.7], 20.0, rise=0.8)
    return r, dict(lead="hero", purpose="The spire lit, from the north-west", subject="the spire mushroom",
                   camera="28 mm, low, 10 m off", movement="truck round it, 20 deg", music="1 bar to the sub-phrase")


@at("105.1")
def herd_rebuilt(t0, t1, g):
    # 2.2 m: at 1.4 m a fern leaf covered the first half of iteration 1's bar.
    r = moving(t0, t1, 28.0, g(80, 40, 2.2), g(76, 39, 2.2), [72.0, 9.0, 12.0])
    return r, dict(lead="animal", purpose="The horses in the rebuilt light", subject="the horses",
                   camera="28 mm, 2.2 m, among them", movement="lateral, 4 m", music="bar 105: the sub-phrase")


@at("106.1")
def veil_lit(t0, t1, g):
    # The veil lit, from its north-east, trucking past it.
    r = moving(t0, t1, 28.0, g(86, 190, 1.4), g(83, 188, 1.4), [77.5, 3.0, 198.7])
    return r, dict(lead="hero", purpose="The veil lit at the water", subject="the veil mushroom",
                   camera="28 mm, 1.4 m, 12 m north-east of it", movement="lateral truck, 3.6 m", music="1 bar")


@at("107.1")
def scree_lit(t0, t1, g):
    # The scree lit on the west slope, the eye swinging 14 deg round it (a 6 m push read as static).
    r = arc(t0, t1, 35.0, g, -160, 80, 2.0, [-176.0, 25.0, 96.0], 14.0)
    return r, dict(lead="hero", purpose="The scree lit on the west slope", subject="the scree mushroom",
                   camera="35 mm", movement="truck round it, 14 deg", music="1 bar")


@at("108.1")
def wave_from_the_east(t0, t1, g):
    # A wave through the mushrooms from the elder, seen low from the east.
    r = moving(t0, t1, 28.0, g(30, 70, 1.5), g(29, 68, 1.5), [-12.0, 5.0, 52.0])
    return r, dict(lead="world", purpose="A wave of light through the mushrooms, from the east",
                   subject="the small mushrooms, the elder beyond", camera="28 mm, 1.5 m, east of the elder",
                   movement="slow push", music="1 bar", effects="bar wave")


@at("109.1")
def crane_over_the_valley(t0, t1, g):
    # [s37] Crane up past the elder's cap and out over the valley.
    r = moving(t0, t1, 24.0, g(-24, 64, 1.5), g(-24, 64, 24.0), [-12.0, 10.0, 52.0], [-12.0, 8.0, -60.0],
               interp="easeInOut")
    return r, dict(lead="world", purpose="Crane up past the elder's cap and out over the valley",
                   subject="the elder, then the valley", camera="24 mm", movement="crane 1.5 -> 24 m, tilting up and out",
                   music="bar 109")


@at("111.1")
def tide_to_the_spire(t0, t1, g):
    # [s38] As the mids lift for the last push, Tide walks the north end toward the lit spire: a chase
    # 5 m behind her and 3.4 m up, looking past her at it. Tide 0.6 of the frame's height, her head
    # just right of centre, the spire clear of it upper right. Her path here is the same on both
    # casts, and the eye runs where she has just walked (76% of the shot), clear of the undergrowth.
    # (Iteration 2 followed Vane from beside her path, and the lens went through a plant for two
    # thirds of the shot; nor did Vane walk toward the horse's place on either cast.)
    r = follow(24.0, "tide", (-4.33, 3.4, 2.5), (8.0, 1.2, -6.0))
    return r, dict(lead="alien", purpose="Tide walks the north end toward the lit spire", subject="tide, the spire beyond",
                   camera="24 mm chase, 5 m behind and 3.4 m up", movement="tracks", music="the last push, bar 111")


@at("113.1")
def valley_rebuilt_from_the_north(t0, t1, g):
    # The valley rebuilt, from the north-east ridge 12 m up, 115 m from the elder: a 10 m truck along
    # the ridge, the elder held, the lit valley sliding under it. (Iteration 2 looked south from the
    # north end, as 77.1 does: the Critic found the two the same composition. The line of sight to the
    # elder's stem and cap is clear of the ground from both ends of the truck.)
    r = moving(t0, t1, 35.0, g(65.4, -37.4, 12.0), g(72.6, -30.6, 12.0), [-12.0, 10.0, 52.0])
    return r, dict(lead="world", purpose="The valley rebuilt, from high on the north-east slope", subject="the valley, the river, the elder",
                   camera="35 mm, 12 m up the north-east slope", movement="lateral truck, 10 m",
                   music="the final phrase, slightly thinner, bar 113")


@at("117.1")
def last_grand_wide(t0, t1, g):
    # [s39] The last wide: the elder and the valley rebuilt, from the first grand wide's place (41.1).
    # A rhyme, deliberately: the place first seen lit at bar 41 seen again rebuilt. That one trucks
    # sideways; this pushes in.
    r = moving(t0, t1, 50.0, g(-2, 203, 4.0), g(-4, 191, 4.1), [-12.0, 14.0, 52.0], interp="easeInOut")
    return r, dict(lead="world", purpose="The last wide: the elder and the valley rebuilt",
                   subject="the elder, the river, the valley, the aurora", camera="50 mm, 4 m, 150 m south",
                   movement="slow push-in (12 m)", music="the last 4 bars")


# ==== 13 tail (bars 121-122) =============================================================================
@at("121.1")
def elder_alone(t0, t1, g):
    # [s40] The elder alone, then black on the last hit -- now from the north-east under the cap, so it
    # does not repeat the arrival's frame at the stem's foot (the Critic's s27/s73 pair).
    r = moving(t0, t1, 20.0, g(-2, 44, 1.3), g(-3, 45, 1.2), [-12.0, 14.5, 51.0])
    return r, dict(lead="hero", purpose="The elder alone, then black", subject="the elder's gills",
                   camera="20 mm, below, north-east of the stem", movement="drifts in", music="kick and clap; black on the last hit at 224.79 s",
                   modulation="heartbeat alone")


# ---- the cut ------------------------------------------------------------------------------------------
def build(ground, spans, lead=0.0):
    """A Shot for each of the Director's spans, from the composition written for it.

    `lead` is how far ahead of its beat the cut is placed (make_glowmere_valley_3.CUT_LEAD). Each
    composition is given its span as it is on screen, from the led start to the led end, so a keyed
    move runs for the whole shot. Keyed from the beat instead, every move held still for the shot's
    first frame and then set off at full speed: invisible at a cut, but 39 "abrupt camera
    acceleration" findings in the Critic's iteration-2 job."""
    g = ground.above
    missing = [s.label for s in spans if s.label not in COMPOSITIONS]
    unused = sorted(set(COMPOSITIONS) - {s.label for s in spans})
    if missing or unused:
        raise RuntimeError(f"the Director's cut and the compositions disagree: spans with no composition "
                           f"{missing}; compositions no span uses {unused}")
    shots = []
    for i, span in enumerate(spans):
        t0 = span.start if span.start <= 0.0 else span.start - lead
        t1 = span.end if i == len(spans) - 1 else span.end - lead
        rig, notes = COMPOSITIONS[span.label](t0, t1, g)
        # Named s01, s02, ... in cut order, as the engine's evaluator hook and the Critic's adapter name
        # the scene's shots (directing_evaluate.cpp), so every report agrees on which shot is which. The
        # first pass's ids named a different cut; the docs name a span by its bar and beat instead.
        sid = f"s{i + 1:02d}"
        rig.name = f"{sid} {notes.get('purpose', '')}"[:60]
        notes.setdefault("music", "")
        notes["music"] = f"{notes['music']}; Director: {span.why}" if span.why else notes["music"]
        shots.append(Shot(sid, span.start, span.end, rig, segment=span.segment, span=span, **notes))
    return shots
