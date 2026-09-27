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
E4_PLACE = [-55.0, 12.0, 40.0]       # the river pair's region centre (radius 30)

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
def lantern_hollow(t0, t1, g):
    # [s04] The lantern through the ferns; Rook works this side of the valley and may cross it.
    r = moving(t0, t1, 28.0, g(-70, -41, 1.8), g(-66, -44, 1.8), LANTERN)
    return r, dict(lead="hero", purpose="The lantern from the hollow, ferns in front", subject="the lantern mushroom",
                   camera="28 mm, low, through ferns", movement="lateral truck (parallax)", music="2 bars")


@at("9.1")
def horse_grazing(t0, t1, g):
    # [s05] The white horse -- what the valley will lose -- under the whole of the elder's cap.
    r = aim_at(30.0, g(22, 78, 3.0), "horse-11", (0.0, 8.6, 0.0), "still")
    return r, dict(lead="animal", purpose="The white horse grazing, the elder behind it",
                   subject="horse-11 (what the valley will lose)", camera="30 mm, 3 m, fixed eye, live aim",
                   movement="still", music="2 bars")


@at("11.1")
def e1_far_survey(t0, t1, g):
    # E1. Far up the valley a thin beam sweeps a field and lifts nothing, 340 m beyond the elder;
    # it goes out on the riff's phrase end (bar 13, this shot's cut). A long lens from the river's
    # south reach, low, so the survey sits to the right of the elder's cap.
    eye0, eye1 = g(8, 150, 3.0), g(3, 150, 3.0)
    r = moving(t0, t1, 50.0, eye0, eye1, [-10.0, 13.0, 20.0])
    return r, dict(lead="event", purpose="E1: the elder, and far beyond it a beam sweeping a field",
                   subject="the elder; the scout's survey (E1), tiny, 340 m beyond",
                   camera="50 mm from 100 m south of the elder, low", movement="slow lateral truck",
                   music="2 bars; the beam goes out on the phrase end, bar 13", effects="the survey beam (E1)")


@at("13.1")
def bloom_by_the_river(t0, t1, g):
    # A hero answering its own layer: the bloom's cap breathes on the bass glide.
    r = moving(t0, t1, 35.0, g(-40, 130, 2.0), g(-41, 128, 2.0), [-60.0, 9.0, 118.0])
    return r, dict(lead="hero", purpose="The bloom by the river, breathing", subject="the bloom mushroom",
                   camera="35 mm, 2 m, across the river", movement="slow drift", music="1 bar")


@at("14.1")
def gills_from_the_east(t0, t1, g):
    # Under the elder's cap from its east side: the gold that beats with the kick, a bar before the
    # pull-back takes the drums away.
    r = still(20.0, g(6, 60, 2.0), [-12.0, 14.0, 50.0])
    return r, dict(lead="hero", purpose="Under the elder's cap, from the east", subject="the elder's gills",
                   camera="20 mm, 2 m, east of the stem", movement="still", music="1 bar, into the pull-back",
                   modulation="heartbeat")


# ==== 3 first pull-back (bars 15-16): E2 ===============================================================
@at("15.1")
def e2_flyby(t0, t1, g):
    # [s07] E2, the flyby: from 112 m north of the elder looking south past it, a shape crosses the
    # frame over the cap while the music holds its breath -- the promise the drop keeps.
    eye = g(-24, -58, 2.0)
    r = still(24.0, eye, [-12.0, eye[1] + 110.7 * math.tan(math.radians(14.0)), 52.0])
    return r, dict(lead="event", purpose="E2: a shape crosses the sky over the elder",
                   subject="the saucer (flyby, E2), the elder's cap",
                   camera="24 mm, 112 m north of the elder, 14 deg up", movement="still",
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
    # low over the ground cover, so it comes toward the lens.
    r = moving(t0, t1, 35.0, g(-42, 92, 0.9), g(-40, 89, 0.9), [-12.0, 6.0, 52.0])
    return r, dict(lead="world", purpose="The first wave runs through the mushrooms toward us",
                   subject="the small mushrooms, the elder beyond", camera="35 mm, 0.9 m, south-west of the elder",
                   movement="slow push", music="2 bars; the wave on the downbeat", effects="bar wave")


@at("21.1")
def rook_investigates(t0, t1, g):
    # An alien investigates a flickering cluster: Rook inspects what it finds on the ground.
    r = follow(35.0, "rook", (-4.5, 2.2, 5.5), (0.0, 1.3, 0.0))
    return r, dict(lead="alien", purpose="Rook investigates a flickering cluster", subject="rook",
                   camera="35 mm follow, beside and behind", movement="tracks", music="2 bars")


@at("23.1")
def herd_east_meadow(t0, t1, g):
    # The herd on the east meadow, the elder's gold far beyond it.
    r = moving(t0, t1, 40.0, g(100, -15, 1.8), g(98, -12, 1.8), [58.0, 9.0, 22.0])
    return r, dict(lead="animal", purpose="The herd grazing on the east meadow", subject="the horses, the elder far off",
                   camera="40 mm, 1.8 m, north-east of the meadow", movement="slow lateral", music="2 bars")


@at("25.1")
def lantern_close(t0, t1, g):
    # The lantern's clap flare, close, from its east side.
    r = still(35.0, g(-30, -20, 2.5), [-45.0, 10.5, -26.0])
    return r, dict(lead="hero", purpose="The lantern, close, flaring on the clap", subject="the lantern mushroom",
                   camera="35 mm, 15 m east of it", movement="still", music="1 bar")


@at("26.1")
def east_bank_travel(t0, t1, g):
    # The one longer travel: down the east bank past the ferns, looking across the river at the
    # elder, so the whole middle of the valley slides by behind it.
    r = moving(t0, t1, 35.0, g(24, 98, 2.5), g(22, 76, 2.5), [-12.0, 10.0, 52.0])
    return r, dict(lead="world", purpose="A travel down the east bank, the elder across the water",
                   subject="the valley floor, the elder", camera="35 mm, 2.5 m, east bank",
                   movement="lateral dolly, 22 m", music="3 bars")


@at("29.1")
def vane_watches(t0, t1, g):
    # [s11] Vane by the river, watching the elder: riding 10 m behind Vane, looking at the elder's
    # gold, so the two stay on one line whatever Vane does.
    r = Rig("", 28.0, follow="vane", follow_offset=(3.8, 2.6, -9.2), target=[-12.0, 12.0, 52.0], clearance=1.2,
            smoothing="fixed-target")
    return r, dict(lead="alien", purpose="Vane by the river, watching the elder", subject="vane, the elder beyond",
                   camera="28 mm, riding 10 m behind Vane", movement="follows", music="2 bars")


@at("31.1")
def spire_east_bank(t0, t1, g):
    # A hero the first pass never showed: the spire on the east bank.
    r = moving(t0, t1, 35.0, g(50, -90, 2.0), g(52, -91, 2.0), [68.0, 14.5, -104.0])
    return r, dict(lead="hero", purpose="The spire on the east bank", subject="the spire mushroom",
                   camera="35 mm, 2 m, south-west of it", movement="slow push", music="2 bars to the lift")


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
def elder_warming(t0, t1, g):
    # Back in the valley, the elder's gold warming for the arrival.
    r = moving(t0, t1, 35.0, g(-48, 60, 2.0), g(-46, 59, 2.0), [-12.0, 12.0, 52.0])
    return r, dict(lead="hero", purpose="The elder from the west, its gold warming", subject="the elder",
                   camera="35 mm, 2 m, west bank", movement="slow push", music="1 bar")


@at("40.1")
def ridge_north_east(t0, t1, g):
    # The ridge, high in the north-east, the last bar before the valley lights up.
    r = moving(t0, t1, 35.0, g(110, -170, 3.0), g(112, -171, 3.0), [131.0, 24.0, -189.0])
    return r, dict(lead="hero", purpose="The ridge mushroom high in the north-east", subject="the ridge mushroom",
                   camera="35 mm, 3 m", movement="slow push", music="1 bar into the arrival")


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
    # Down among it: the valley floor lit, low and wide, the mushrooms turning toward cyan.
    r = moving(t0, t1, 24.0, g(60, 140, 2.0), g(57, 137, 2.0), [-30.0, 8.0, 20.0])
    return r, dict(lead="world", purpose="The valley floor lit, low and wide", subject="the valley floor, the far walls",
                   camera="24 mm, 2 m, south-east of the elder", movement="slow push", music="2 bars")


@at("45.1")
def bloom_spores(t0, t1, g):
    # [s15] Spores falling from the bloom.
    r = still(24.0, g(-54, 125, 1.2), BLOOM_CAP)
    return r, dict(lead="hero", purpose="Spores falling from the bloom", subject="the bloom mushroom",
                   camera="24 mm, looking up", movement="still", music="1 bar", effects="spores")


@at("46.1")
def veil_at_the_water(t0, t1, g):
    r = still(35.0, g(95, 210, 2.0), [78.0, 2.5, 198.5])
    return r, dict(lead="hero", purpose="The veil at the water's edge", subject="the veil mushroom",
                   camera="35 mm, 2 m", movement="still", music="1 bar")


@at("47.1")
def gold_gills(t0, t1, g):
    # [s16] Under the elder: the gold gills. One bar now; the first pass's two showed nothing new
    # after their first frame.
    r = still(18.0, g(-15, 59, 1.4), [-12.0, 15.0, 51.0])
    return r, dict(lead="hero", purpose="Under the elder: the gold gills", subject="the elder's gills",
                   camera="18 mm, from the stem's foot", movement="still", music="1 bar", modulation="heartbeat")


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
    r = moving(t0, t1, 50.0, g(-35, 150, 2.0), g(-36, 148, 2.0), [-59.0, 9.0, 118.0])
    return r, dict(lead="hero", purpose="The bloom from across the river", subject="the bloom mushroom",
                   camera="50 mm, 2 m, south-east of it", movement="slow drift", music="1 bar")


@at("54.1")
def e4_arrives(t0, t1, g):
    # E4. The scout comes down over the river 47 m west of the elder and its beam lights (bar 56.3):
    # a middle-distance frame from the south with the elder at its right, so the event is in the
    # valley, beside the film's heart, not in a sky of its own.
    r = moving(t0, t1, 35.0, g(-30, 125, 3.0), g(-29, 121, 3.0), [-33.0, 16.0, 40.0])
    return r, dict(lead="event", purpose="E4: the scout settles over the river beside the elder",
                   subject="the scout (E4), the elder at the right", camera="35 mm, 3 m, 90 m south",
                   movement="slow push", music="3 bars; the beam at bar 56.3", effects="the scout's beam (E4)")


@at("57.1")
def e4_pair_lifted(t0, t1, g):
    # E4's lift, on the sub-phrase line: two animals rising together in one column, from 55 m.
    r = aim_at(50.0, g(-25, 85, 2.0), "scout", (0.0, -12.0, 0.0), "craft")
    return r, dict(lead="event", purpose="E4: two animals lifted together", subject="the scout's beam and the pair (E4)",
                   camera="50 mm, 55 m off, live aim", movement="still", music="the lift on bar 57",
                   effects="the scout's beam")


@at("59.1")
def e4_aliens_watch(t0, t1, g):
    # The aliens walk toward it and watch: riding with Vane, looking past it at the column.
    r = follow(28.0, "vane", (6.0, 2.4, 3.0), (0.0, -10.0, 0.0), aim="scout", smoothing="fixed-target")
    return r, dict(lead="event", purpose="E4: Vane watches the pair rise", subject="the column (E4), vane in front",
                   camera="28 mm, riding beside Vane, looking at the scout", movement="follows", music="2 bars")


@at("61.1")
def herd_after(t0, t1, g):
    # The valley goes on: the horses on the east meadow from the south, and in the sky beyond them
    # the scout climbing away north.
    r = moving(t0, t1, 35.0, g(95, 45, 2.0), g(94, 42, 2.0), [70.0, 12.0, 10.0])
    return r, dict(lead="animal", purpose="The herd on the east meadow; the scout leaving beyond",
                   subject="the horses", camera="35 mm, 2 m, south-east of the meadow", movement="slow lateral",
                   music="2 bars")


@at("63.1")
def umbra_listening(t0, t1, g):
    # The valley listening in close-ups: the umbra.
    r = moving(t0, t1, 24.0, g(-75, 180, 1.2), g(-73, 181, 1.2), [-64.0, 7.0, 167.0])
    return r, dict(lead="hero", purpose="The umbra, listening", subject="the umbra mushroom",
                   camera="24 mm, low, south-west of it", movement="slow lateral", music="2 bars")


@at("65.1")
def elder_spores(t0, t1, g):
    r = moving(t0, t1, 50.0, g(-36, 70, 2.5), g(-35, 68, 2.5), [-12.0, 12.0, 52.0])
    return r, dict(lead="hero", purpose="The elder's spores drifting in its gold", subject="the elder",
                   camera="50 mm, 2.5 m, south-west", movement="slow push", music="a bar and a half")


@at("66.3")
def sage_grove(t0, t1, g):
    # [s18] Sage in the grove, among the fireflies, from the north-west.
    r = follow(40.0, "sage", (-4.5, 3.0, -5.0), (0.0, 1.8, 0.0))
    return r, dict(lead="alien", purpose="Sage in the grove, among the fireflies", subject="sage",
                   camera="40 mm follow", movement="tracks", music="2.5 bars", effects="fireflies")


@at("69.1")
def lantern_pool_side(t0, t1, g):
    r = moving(t0, t1, 50.0, g(-20, -5, 2.0), g(-22, -4, 2.0), [-45.0, 11.0, -26.0])
    return r, dict(lead="hero", purpose="The lantern from the river side", subject="the lantern mushroom",
                   camera="50 mm, 2 m, south-east of it", movement="slow lateral", music="2 bars")


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
    # [s21] The aurora over the valley, pushing north.
    r = moving(t0, t1, 28.0, g(-30, 150, 2.5), g(-27, 135, 2.5), [-10.0, 40.0, -80.0])
    return r, dict(lead="world", purpose="The aurora over the valley", subject="the sky up the valley",
                   camera="28 mm, low", movement="slow push north", music="2 bars", effects="aurora")


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
    # [s24] From under the elder: the saucer settling over its rim. A slow push now: the first pass's
    # fixed frame showed nothing new after its first frame.
    r = Rig("", 20.0, aim="visitor", aim_offset=(0.0, -4.0, 0.0), smoothing="craft")
    r.move(t0, t1, g(-18, 45, 1.5), g(-16.6, 46.6, 1.8))
    return r, dict(lead="event", purpose="From under the elder: the saucer over its rim",
                   subject="the saucer, the elder's gills", camera="20 mm, under the cap, live aim",
                   movement="slow push", music="the low-pass break on bar 89", modulation="light low, fog up")


@at("91.1")
def ember_watches(t0, t1, g):
    # [s25] Ember watches from across the water: riding 7 m behind and 4.8 m over Ember (ADR-913), looking
    # at the saucer, so the two are on one line whatever Ember does.
    r = follow(28.0, "ember", (-7.0, 4.8, 2.5), (0.0, -6.0, 0.0), aim="visitor", smoothing="fixed-target")
    return r, dict(lead="alien", purpose="Ember watches from across the water", subject="ember, the saucer beyond",
                   camera="28 mm over the shoulder, riding with Ember", movement="follows", music="2 bars to the riser")


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
    # [s27] Up the beam from beside the horse.
    r = aim_at(20.0, g(-1, 80, 1.0), "visitor", (0.0, -3.0, 0.0), "craft")
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
    # [s32] The horse fades into the saucer on the roll's last beat. Aims at the craft, never at the
    # horse, which is retired on the frame of the drop.
    r = aim_at(70.0, g(40, 78, 6.0), "visitor", (0.0, -3.5, 0.0), "craft")
    return r, dict(lead="event", purpose="E5: the horse fades into the saucer", subject="horse-11 vanishing",
                   camera="70 mm, side on from the east", movement="still", music="the last beat of the roll")


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
def e5_far_away(t0, t1, g):
    # The saucer small now over the north rim, climbing toward the aurora.
    r = aim_at(50.0, g(-20, 40, 3.0), "visitor", (0.0, 0.0, 0.0), "craft")
    return r, dict(lead="event", purpose="E5: the saucer, small, climbing away north", subject="the saucer",
                   camera="50 mm, from the elder, live aim", movement="still", music="1 bar")


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
    r = moving(t0, t1, 28.0, g(56, -118, 1.6), g(58, -118, 1.6), [68.0, 14.0, -104.0])
    return r, dict(lead="hero", purpose="The spire lit, from the north-west", subject="the spire mushroom",
                   camera="28 mm, low", movement="lateral", music="1 bar to the sub-phrase")


@at("105.1")
def herd_rebuilt(t0, t1, g):
    r = moving(t0, t1, 28.0, g(80, 40, 1.4), g(77, 39, 1.4), [72.0, 9.0, 12.0])
    return r, dict(lead="animal", purpose="The horses in the rebuilt light", subject="the horses",
                   camera="28 mm, low, among them", movement="lateral", music="bar 105: the sub-phrase")


@at("106.1")
def veil_lit(t0, t1, g):
    r = still(35.0, g(60, 215, 1.5), [78.0, 2.5, 199.0])
    return r, dict(lead="hero", purpose="The veil lit at the water", subject="the veil mushroom",
                   camera="35 mm, 1.5 m, west of it", movement="still", music="1 bar")


@at("107.1")
def scree_lit(t0, t1, g):
    r = still(35.0, g(-160, 80, 2.0), [-176.0, 25.0, 96.0])
    return r, dict(lead="hero", purpose="The scree lit on the west slope", subject="the scree mushroom",
                   camera="35 mm", movement="still", music="1 bar")


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
def vane_where_the_horse_was(t0, t1, g):
    # [s38] Vane walks to where the horse was taken, as the mids lift for the last push.
    r = follow(35.0, "vane", (-5.0, 2.4, 4.0), (0.0, 2.2, 0.0))
    return r, dict(lead="alien", purpose="Vane walks to where the horse was taken", subject="vane",
                   camera="35 mm follow", movement="tracks", music="the last push, bar 111")


@at("113.1")
def valley_rebuilt_from_the_north(t0, t1, g):
    # The valley rebuilt, from the north, low: the river winding down to the elder.
    r = moving(t0, t1, 35.0, g(-35, -130, 3.0), g(-33, -121, 3.2), [-12.0, 10.0, 52.0], interp="easeInOut")
    return r, dict(lead="world", purpose="The valley rebuilt, from the north", subject="the valley, the river, the elder",
                   camera="35 mm, 3 m, 180 m north of the elder", movement="slow push (9 m)",
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
    # [s40] The elder alone, then black on the last hit.
    r = still(20.0, g(-17, 60, 1.3), [-12.0, 14.5, 51.0])
    return r, dict(lead="hero", purpose="The elder alone, then black", subject="the elder's gills",
                   camera="20 mm, below", movement="still", music="kick and clap; black on the last hit at 224.79 s",
                   modulation="heartbeat alone")


# ---- the cut ------------------------------------------------------------------------------------------
def build(ground, spans):
    """A Shot for each of the Director's spans, from the composition written for it."""
    g = ground.above
    missing = [s.label for s in spans if s.label not in COMPOSITIONS]
    unused = sorted(set(COMPOSITIONS) - {s.label for s in spans})
    if missing or unused:
        raise RuntimeError(f"the Director's cut and the compositions disagree: spans with no composition "
                           f"{missing}; compositions no span uses {unused}")
    shots = []
    for i, span in enumerate(spans):
        rig, notes = COMPOSITIONS[span.label](span.start, span.end, g)
        sid = f"c{i + 1:02d}"
        rig.name = f"{sid} {notes.get('purpose', '')}"[:60]
        notes.setdefault("music", "")
        notes["music"] = f"{notes['music']}; Director: {span.arc}, {span.why}" if span.why else notes["music"]
        shots.append(Shot(sid, span.start, span.end, rig, segment=span.segment, span=span, **notes))
    return shots
