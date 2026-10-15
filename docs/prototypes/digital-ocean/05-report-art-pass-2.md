# Digital Ocean, Art Pass 2: report

*The art agent's report on the owner's brief `04-brief-art-pass-2.md`, which was amended in flight by
`08-brief-island-simplification.md` (the island) and `09-brief-boats.md` (the boats). Branch `art/digital-ocean-2`
from main `f3851d75`. The log, with every decision and wrong turn, is in `PROGRESS.md` under "Pass 2". Media is in
`~/Desktop/av-gen-review/54-digital-ocean-2/`. `before-pass1.png` is the merged Pass 1 scene.*

## What changed

The ocean-horizon-sun scene is now an **illustrated ocean world**. The art direction is a graphic travel poster
crossed with an anime background: bold flat silhouettes for the land, the boat and the creatures; painted
gradients, luminous rays and banded cumulus for the sky; and the sea's colour grouped into flat regions.

| Element | Pass 2 |
|---|---|
| Composition | An elevated, illustrative view (14 m, 32 mm). Sky ~44 %, ocean ~56 %. The island strip is on the left third, the sun with its rays on the right third, and the yacht crosses the middle distance. |
| Island | A long, low anime-background strip, 3 % of the frame (7 % at the crowns). Five cut-paper trees in one vocabulary (windswept palms, broad low canopies). Hard two-tone land. Stable: no deformer and nothing routed to its shape. |
| Boat | A 16 m sport motor yacht with a raked rising bow, a sweeping sheer, a dark boot band, and a hardtop over a long dark glazing band. It cruises across the view and rides the swell, trailing a masked graphic V-wake. |
| Dolphins | 8.5 m (2.7x Pass 1). The leap is a complete gesture: approach, rise, airborne arc, apex, dive, splash. Pods leap every 53 s and 79 s; the hero leap is on pad 36. |
| Clouds | Illustrated cumulus banks and wind-stretched ribbons in two drifting layers (parallax), billowing slowly, lit in two bands: glowing undersides and flat violet tops. |
| Sun rays | Painted into the sky so clouds and land cover them. Two slow sines drift them, so they part and merge rather than spin. Level strengthens them, kicks flash them, onsets extend them, and their tint follows the palette. |
| Colour | Three coordinated palettes (violet sunset, golden afternoon, electric blue) on one axis that the music and the performer both move, plus a bioluminescent night. |
| Water | Banded reflection colours over the Gerstner swell. Pass 3 replaces the water material. |

## Steps, decisions and media

- **Pass A, style frames.** `passA-studies.png` covers three compositions and three palettes. E3 and violet sunset
  were chosen.
- **Pass B, silhouettes.** `passB-composition-before-after.png`.
- **Pass C, illustration treatment.** `passC-illustration-palettes-and-bands.png`. The clouds were rebuilt as
  cumulus, the island was painted, and the water banded.
- **Pass D, atmosphere.** `passD-rays-drift-2-10-20-29s.png`, `passD-30s.mp4`.
- **Island step.** `island/`:
  - `before.png`;
  - variants A (minimal), B (tropical anime) and C (cut-paper), each rendered from the same camera, sea, sun and
    clouds at 1080p.
  - Round 1 at 1.45 km vanished into the haze, so round 2 moved the strip to 900 m.
  - **C was chosen.** It has A's minimal geometry and the cleanest graphic read, and a canopy anchors each end of the
    strip. B's extra trees added clutter, and A left the left half of the strip bare.
- **Boats step.** `boats/`:
  - `before-toy-sailboat.png`;
  - candidates A (16 m motor yacht), B (10 m deep-V runabout) and C (13 m modern sloop), at 130 m and then at 70 m.
  - The reference direction was the hull profiles of modern 15-17 m express cruisers and sport motor yachts, with no
    brands.
  - **A was chosen on silhouette.** B was unreadable at live resolution, and C's sails read as a toy again.
  - The revision followed the coordinator's notes: a raked rising bow, a sheer sweeping to a low stern, a boot band,
    and a crisp masked V-wake in place of particles (`yacht-v2-crossing.png`, `yacht-v3-hull-1080p.png`).
- **Pass E, musical colour.** `passE-F-colour-and-conditions.png`, `passE-F-round2.png`.
- **Pass F, MIDI.** `passF-daybreak-pad-0-14s.png`.
- **Pass G, critique.** `passG-round1.png`, `passG-events-leap-herowave.png`, `passG-round2.png`,
  `passG-leap-round2.png`.
- **Pass H, performance and reliability.** `live-stress/`; see below.

## Palette and controls

- **Palettes**, each designed whole, with every colour of sky, water, crest light, foam, clouds, land, trees, hull,
  deck, glazing and rays set per palette:
  - *violet sunset*, the primary: cobalt water, magenta-lavender crest light, peach rays, pink-orange clouds, dark
    violet land;
  - *golden afternoon*: turquoise water, cream foam, coral sun, soft blue sky, green island;
  - *electric blue*: ultramarine and cyan water, mint surf, saturated blue sky, coral and yellow accents, navy land;
  - *midnight*, the night end.
- **`visual.mood`** = CC 1 + 0.35 x the piece's 12 s intensity + 0.75 x the daybreak envelope. It moves every colour
  together, piecewise from violet to afternoon to electric, smoothed over 1.5 s.

**MIDI**, any device and channel:

| Control | Effect |
|---|---|
| CC 1 | palette (violet -> afternoon -> electric) |
| CC 2 | night |
| CC 3 | waves (swell height) |
| CC 4 | wave energy (chop, foam, the tide's depth) |
| CC 5 | sun rays |
| CC 6 | clouds |
| CC 7 | water colour |
| CC 8 | spray |
| pad 36 | dolphin leap |
| pad 37 | hero wave |
| pad 38 | bioluminescent bloom |
| pad 39 | sun flare |
| pad 40 | spray |
| pad 41 | daybreak: sweeps to electric day over 3 s, holds 5 s, back over 4 s |

**Audio.** Each band has its own role and time constant:

| Signal | Response |
|---|---|
| intensity | the palette mood |
| bass | the tidewriting swell; deepens the water's body; lifts the crest light |
| kick | fronts roll in; crest, foam and ray flash |
| level | swell height and ray strength |
| onsets | ray reach |
| mids | wind sea |
| snare | spray |
| hats / highs | glints and mint crest light |
| sustain | a pink sky middle, warm cloud undersides, the halo |
| brightness | a slight grade drift |
| notes | sparks on the sun's path |

`sonicScene.vocabulary` lists all of them.

## Engine and tool changes

- **ADR-1371: a program's opacity cuts the depth prepass.** `fs_proc_depth` now evaluates a masked material's
  program, so program-made cutouts (the wake's foam) no longer show the cleared colour through their holes. It has a
  GPU test, which fails without the fix.
- **Kit.** `Scene.sources` are now listed before the interpret sources, so a mapping can read an envelope. Every other
  Sonic Abstract scene regenerates byte-identically.
- **Probe.** A `stress` scenario for the reliability run.

## Performance (M2 Max, live target 30 fps = 33.3 ms)

Measured with `tools/live_scene_profile.py` under `tools/gpu-lock.sh`. The reports are in
`54-digital-ocean-2/pass-H-profiles/`.

| Case | GPU median / p95 | Frame median / p95 / p99 | Status |
|---|---|---|---|
| **Real live path, 1080p** (auto quality settles at Medium, scale 0.71) | 27.3 / 28.6 | 33.3 / 33.9 / 34.2 | achieved: a steady 30 fps |
| Headless 1080p, silence, Ultra (scale 1.0) | 27.1 / 27.7 | 30.1 / 42.5 / 52.1 | achieved |
| Headless 1080p, Trench from 60 s, Ultra | 33.3 / 34.3 | 35.8 / 40.8 / 54.5 | at risk without the ladder |
| Headless 2160p, silence, Ultra | 68.2 / 70.1 | 71.7 / 79.2 / 84.7 | over: the ladder scales it |

- Pass 1 measured 22.4 ms GPU silent at 1080p Ultra, so Pass 2's world costs about 5 ms more. The likely sources are
  the sky program with its rays (46 ops on the upper half of the frame), ~320 billowing cloud puffs, and the island,
  yacht and wake. Pass 3 owes a per-feature breakdown and replaces the water, the largest cost, so the deep
  optimisation is scheduled there.
- **Reliability** (`live-stress/`): the probe's new `stress` scenario played 899 MIDI messages into the live editor
  with BlackHole input, over ~80 s:
  - every pad 36-41 hit 16 times at 4 Hz;
  - the palette CC swept fast and slow through all three palettes;
  - night, with six hero waves during it.

  GPU time stayed flat through every phase (p50 29.5-29.7 ms, p95 31.2-31.6), so nothing accumulates: particle pools
  are fixed, fields are stateless and envelopes restart. There were no GPU errors and every message arrived. The
  editor window's frame interval p50 was 33.3 ms in every phase; its p95 of ~67 ms is the editor on a Retina canvas
  and does not move with the stress.

## Remaining limitations

- **Handed to Pass 3, which replaces the water:**
  - the white slab of crest light and foam under heavy bass or at the peak;
  - the hero wave reading as a thin dark line rather than a rising, foaming swell;
  - wave speed and direction not yet on controls (a deformer's phase is `k.x + speed t`);
  - wake foam to be matched to the new water's foam language.
- At intermediate mood positions under loud music the sky can drift pale lavender (two palettes half-blended plus the
  rays).
- The yacht keeps one course (left to right, ~75 s a crossing). The daybreak and palette controls recolour it, but no
  pad sends it.
