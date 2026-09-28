# ADR-939: The aurora's "Audio response" is the master of everything it does with the music

**Status:** Accepted
**Date:** 2026-09-27
**Follows:** ADR-230 §4 (the aurora: per-band depths on signals the frame block already carries, a curtain
top shaped by the spectrum), ADR-500 (an effect's rows, labels and tooltips live in its one file),
ADR-924 (plan routes), ADR-387 (the owner's rule: anything visible is controllable, under a name for what
it does)
**Found by:** the GV3 revision's gv3-look stream (`phase3/look.md`, iteration 2, finding 3: "'Audio
response' 0 removes only the shader's per-frame band terms ... the raw per-frame spectrum still sets every
curtain's top ... the sky still pulses on the beat (+12-16% beat-locked in s33 and s36)"; and after v5:
"the glints ... are the one sky term 'audio response' does not switch off, following the high band frame
by frame")
**Implemented by:** `AuroraAudio::glints` and the sensitised spectrum and glint depth in `packAurora`
(`src/world/atmospherics.{hpp,cpp}`); `AuroraGpu::audio3` and its WGSL twin `AtmosAurora::audio3`
(`shaders/common.wgsl`); the glint term in `atmosphereAuroraAt` (`shaders/atmosphere_fx.wgsl`); the rows,
labels and tooltips (`src/world/effects/kinds/aurora_effect.cpp`); `ui::effectRoutesOn` and the Effects
card's route list (`src/ui/effects_panel_logic.{hpp,cpp}`, `src/ui/effects_panel.cpp`, and its line in
`docs/help/reference-panels.md`); the frame-block
asserts (`src/rendering/scene_renderer.hpp`) and the conformance frame size
(`src/world/effects/effect_conformance.cpp`)
**Tests:** `tests/unit/test_aurora_audio_response.cpp` (`[adr939]`, 5 cases);
`tests/rendering/test_aurora_audio_response_gpu.cpp` (`[adr939]`, 2 cases)

## Context

An aurora answers the music three ways:

1. **its own per-frame terms**, in the shader: the bass lifts the curtain tops, the low-mid moves their
   waves, the mid churns the folds, the high thickens the filaments and brightens the glints, and the
   beat pulses the whole curtain; and the **spectrum**, sixteen bins across the sky, sets each bearing's
   top (weighted by "Spectrum shape");
2. **routes** onto its parameters: "+ Add aurora" installs six (`audio.bass -> Height`, `audio.rms ->
   Brightness`, ...), "Beat response" on its card writes one, and a Director plan adds its own (GV3's
   `lead.aurora`, which answers the lead slowly);
3. nothing else.

The card's main page has "Audio response", documented in the struct as "one multiplier over all of them".
It multiplied the five band depths and nothing more. Turned to 0 on Glowmere Valley 3:
- the spectrum still set every curtain's top, frame by frame (spectrum shape 0.75);
- the glints still followed the high band: their term was `0.3 + 2.0 x high` in the shader, with no depth
  any control reached.

So the sky kept the kick with its master off (+12-16% beat-locked in the drop). gv3-look switched the
spectrum off by hand (shape 0) and halved the glints (0.5 -> 0.3) because they "are the one sky term audio
response does not switch off". GV3 now runs with audio response 0, spectrum shape 0 and glints 0.3, and
its base glints must still draw.

The routes are a different matter: they are the project's, named in the Modulation panel, and GV3 depends
on one moving the aurora while its own response is off. But nothing on the aurora's card said any route
moved it except "Beat response".

## Decision

**1. "Audio response" multiplies every one of the aurora's own audio terms, as a distance from silence.**
- the five band depths, as before (`bass`, `lowMid`, `mid`, `high`, `beat`, each x the sensitivity);
- **each spectrum bin's distance from the neutral 0.5** -- the value the engine fills in with no music --
  so the tops follow the music less and less, and at 0 stand where silence stands them;
- **the glints' share of the high band**, which becomes a depth of its own (below).

At 0 the aurora is exactly the aurora with no music playing. At 1 every term is what its own depth says,
and **every aurora that runs at 1 packs bit for bit as before**: the bins go through `std::lerp`, which is
exact at 1, and the glints' new depth defaults to the old constant's share. Above 1 every term grows alike
(the struct's "one multiplier over all of them", now true), clamped where a bin would leave 0..1.

**2. The glints get their own depth, "High -> glints"** (`audio/glints` in the file, `fx/<id>/audioGlints`
as a parameter), under Advanced > "Audio response" beside "High -> filaments". 1 is the old constant
(`0.3 + 2.0 x high`), 0 holds the glints at their own level. The glints themselves are relabelled from
"Sparkle" to **"Glints"** (the leaf and the file keep `sparkle`), with a tooltip saying what brightens
them. Their own level -- 0.3 of "Glints" -- is what they hold at silence and at audio response 0, so GV3's
base glints still draw.

**3. Routes are not in the master, and are named where the aurora is.**
- The master's tooltip says what it covers -- "the spectrum shaping the curtain tops, the bass lifting
  them, the waves, folds, filaments and glints, and the pulse on each beat" -- that 0 is "exactly as it
  looks with no music playing", and that "Routes onto the aurora -- Beat response below, and any in the
  Modulation panel -- are separate, and keep moving it". "Spectrum shape" and "High -> glints" have
  tooltips too.
- Every effect card (not only the aurora's) lists, under "Beat response", the routes whose target is one
  of its parameters: "moved by N routes:", then "<source> -> <row label>" for each, with "(off)" and
  "[plan]" where they apply, six at most and "and N more (Modulation panel)" (`ui::effectRoutesOn`). A sky
  still moving with the master at 0 is now traceable from the aurora itself.

Gating routes by the aurora's master was rejected: a route is a separate, named control an artist made
(or a plan made, with its reason), and GV3's look depends on `lead.aurora` moving the sky while the
aurora's own response is off.

**4. The GPU record.** `AuroraGpu` grows one vec4, `audio3` (x = the glints' sensitised depth), mirrored in
`common.wgsl`, and the shader reads the glints' high share through it: `0.3 + 2.0 x audio3.x x high`.
The frame block's offsets after `auroras` move by 32 bytes (the asserts and the layout guard test follow),
and the conformance check's frame size is 2596 (it compares the aurora array whole, so it still reads all
of it).

## Consequences

- **Where an artist finds them:** World -> Inspector, with Atmosphere -> environment selected -> Effects
  -> the aurora's card: "Audio response" and "Spectrum shape" among its main controls, with their
  tooltips; Advanced > "Audio response": "Bass -> height", "Low-mid -> waves", "Mid -> folds", "High ->
  filaments", **"High -> glints"**, "Beat -> pulse"; Advanced > "Appearance": **"Glints"**; and under
  "Beat response", **"moved by N routes: ..."** (on every effect's card). The Help's Effects section
  (`docs/help/reference-panels.md`) says what the route list is and that routes are outside "Audio
  response". The Parameters panel lists them too, under `fx` > `<id>`, by their leaves (`audioGlints`,
  `sparkle`): no effect's parameters carry their card's labels there, which this ADR leaves as it was --
  a generic label would need each row's card section folded in, or the aurora's two "Brightness" rows
  (its own and the rainbow's) would read alike.
- **Every tracked aurora but one is unchanged**: GV2 multicam, the `_diag-water-*` scenes, `_pre-defects`
  and `examples/effects/ufo-stack` run at audio response 1 and pack bit for bit as before (tested lane by
  lane). **glowmere-atmospherics** runs at 1.15: its spectrum's distance from neutral and its glints' high
  share are now x1.15 where they were x1 -- a slightly livelier aurora with music, the same in silence.
- **GV3's sky changes, and is re-baselined here.** GV3 runs with audio response 0, spectrum shape 0 and
  glints 0.3, so the one term that changes is the glints: they no longer follow the high band and hold at
  their own level. Rendered at 960x540 from gv3-look's v8 snapshot, before (the engine at `983221a9`) and
  after (this branch), three 2 s windows at 60 fps (120 frames each); the sky band is the top 30% of the
  frame (gv3-look's measure):

  | Window | Sky luma before -> after | Per frame | Frame-to-frame change p50 / p99 | What followed the music |
  |---|---|---|---|---|
  | open, 12-14 s | 0.1541 -> 0.1534 (x0.995) | x0.981-1.000 | 0.30 / 1.60% -> 0.23 / 1.56% | 0.48% of the sky on average, up to 1.92% |
  | plateau, 95-97 s | 0.0839 -> 0.0821 (x0.978) | x0.888-0.998 | 0.34 / 8.90% -> 0.17 / 1.71% | 2.17%, up to 11.2% |
  | drop, 180-182 s | 0.0455 -> 0.0453 (x0.995) | x0.980-1.000 | 0.75 / 16.01% -> 0.72 / 14.15% | 0.49%, up to 2.04% |

  A third render with the glints at 0 separates them from the sky: they now add a steady 0.00042 /
  0.00104 / 0.00010 of sky luma (spread over the frames 0.00010 / 0.00038 / 0.00008), against 0.00115 /
  0.00287 / 0.00033 before (spread 0.00086 / 0.00231 / 0.00037). Following the high band, they averaged
  **x2.75 to x3.14** their own level. So GV3's sky is up to 2.2% darker on average in these windows, and
  its frame-to-frame flicker is gone where the glints carried it (the plateau's p99 8.90% -> 1.71%; the
  drop's p99 is a cut at its 83rd frame and pulses every 14 frames that both renders share, which are
  routed, not the aurora's own). Below the sky band the open window is unchanged to 0.01 of a
  level on average; in the plateau and the drop 1.9% and 3.6% of the pixels move by more than 2 levels (at
  most 63): the haze at the horizon -- GV3's fog takes the sky's radiance (ADR-918), and the glints are in
  it -- the glints' bloom along the tree line, and the glints themselves, streaks along the curtains' rays.
  All of it is this change: the same build rendering the drop window twice gives byte-identical frames
  (120 of 120). To keep v5's average glint brightness,
  now steady, set "Glints" (`fx/aurora/sparkle`) to about 0.85 (0.3 x 2.8, measured in display luma, so an
  estimate to confirm on frames); at 0.3 the sky is steadier and a little darker. Evidence:
  `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/uireach-work/gv3-sky-before-after/`.
- **Tests** (each with its control):
  - CPU: at 0 the packed record with a loud, shaped spectrum is the silent record, every lane (control: at
    1 they differ); at 1 every bin is the clamped spectrum exactly (==) and the glints' depth is 1; at 0.5
    and 2 each bin's distance from 0.5 is scaled, clamped; the file key round-trips and a file without it
    reads 1; the card's rows, labels, sections and tooltips; the route list;
  - GPU (480x300, a loud frame on the beat and a shaped spectrum against no music at all): with audio
    response 0 the two skies are **identical, 0 pixels** -- against the unchanged engine this arm failed
    with 89,011 of 144,000 pixels moved (the spectrum and the glints); control: at 1, 109,556 pixels move;
    in silence 0 and 1 are identical. GV3's configuration (0, shape 0, glints 0.3): music and silence
    identical, 0 pixels -- 40,020 moved before this change, the glints alone -- and its glints still light
    22,481 pixels against the same sky with none. "High -> glints" 0 against 2 with music: 101,845 pixels;
    in silence, and at audio response 0, 0.
