# Pass 3, SA agent: Phase E (ocean), the night regression, the guard

The owner approved the plan (56-, 58-, 59-) on 2026-11-04 and chose the Moon fix (a). The coordinator added the night
regression. This file records what was done, in order. Media: `~/Desktop/av-gen-review/101-strange-attractor/pass3-env/`
`05-ocean/` and `08-regression/`.

## 1. "The ocean renders red as the root" was the camera tool, not the engine

- **Cause:** the fixed-camera renders took the film's project, routes included. The film's root is the canyon, and
  one of its audio routes drives the canyon terrain's contour (`response.high -> attractorWorld/contour`).
  - With the ocean as the root, that route landed on the ocean's attractor world.
  - Its contour emission covered the whole seabed: the emission AOV showed R 0.36 mean against G and B 0.01, and it
    stayed red with every light removed.
- **Ruled out first:** the fog, the volume, the octopus, the terrain program, the sun and the lights.
- **Fix:** in `pass3/render_cameras.py`, when a portal world (Moon, Mars, ocean) is drawn as the root, its own
  `moon/`, `mars/` or `ocean/` tracks and routes are re-addressed to it and the root's are dropped.
  - Its subs, creatures and lights now also move as in the film. They were unknown targets before.
  - The film never had the problem.
- **The baseline's six portal-world fixed cameras were re-rendered:** `e-`/`h-` for the Moon, Mars and the ocean.
  - The old ones are in `00-baseline/superseded-portal-world-fixed/`.
  - Mars and the Moon barely changed; the Moon's domes now pulse as in the film.

## 2. The night pass-by (the owner's second M3g regression)

- **Cause:** Mars's four-bar cut started the night 8 s earlier. The crossing into the sea (121.2 s) stayed put, so
  the same 25 m path had 8 s more to fill.
  - The speed profile spent them at 0.5 m/s beside the bank's boulder, from 98 to 116 s.
  - The gaze, following a velocity that small, also swung about 90 degrees.
- **The new pass:**
  - It is level, 4 m over the water.
  - From the bank's exit it goes out over the river in a wide left curve that turns the camera toward the bank.
  - From 107 to 115 s the camp, the fire, the craft and the figures are ahead across the water.
  - Then it turns right into the river portal's glide (same slope and speed; the glide's heading turned to this
    approach, and the ocean side, mapped from it through `entryUp`, is unchanged).
  - It flies at a steady 2.0-3.4 m/s, with no crawl.
- **Traces against the M3g baseline:**

  | | M3g | now |
  |---|---|---|
  | forward vs velocity, max / p95 | 20.3 / 10.0 | 20.1 / 7.3 |
  | lowest over the terrain before the dive | 2.46 m | 3.45 m |
  | nearest mesh box | 3.21 m | 16.5 m |

- **The corridor goes to the hero agent** (57- H-4) before it places the clearing's geology: 4 m either side, 3 m
  under. The camp's river face becomes the composition of 107-115 s.

## 3. A check that would have caught it: `pass3/near_track.py`

- **What it measures:** the share of the frame covered by geometry within N metres, every half second, from the
  renderer's own depth AOV. It flags a run of `--hold` seconds over `--flag`.
- **Calibrated on the M3g night:** the boulder sat at 11-17 m (median depth under 20 m from 102.5 to 114.5 s). The
  defaults are therefore 20 m, half the frame, 4 s.
- A 6 m threshold, the first guess, would have missed it.
- **The whole film at 2 fps:**
  - The M3g baseline flags exactly one stretch: `REGRESSION? 103.0-114.5 s`.
  - The new film flags nothing.
  - Files: `08-regression/02-near-geometry-whole-film-M3g.txt`, `03-...-new.txt`.

## 4. The ocean (Phase E, first pass)

**What changed (`build_ocean.py`):**
- **ADR-1910's water column is on.**
  - The surface sits 160 m over the floor, with k = (0.016, 0.0055, 0.0032) per metre.
  - The light from above is raised to arrive at about the M3g level on the floor (x1.43).
  - Effect: the floor sits in a bluer, darker layer than the shelf tops and the base's roof, and close surfaces lose
    their dry colour (`h-ocean`).
- **The water is drawn as a gradient:** brighter toward the surface and near black below, using the procedural sky as
  the water, with no sun.
  - The distance fog fades each surface toward the water in its own direction (ADR-918, `fogSky` 1, 420 m).
  - The flat navy backdrop is gone. The far shelf dissolves into the medium instead of standing in front of a colour.
- **The base belongs to the floor:**
  - a silt layer over its own materials: up-facing faces, thickest in its lowest 26 m, a darker stain at the foot,
    mottled; emission is untouched;
  - two sodium work lamps at its foot (520 cd, 45 m range). The water takes their red within about 30 m, so they
    pool, and they are the brief's warm accent.
- **Tried and reverted:** a 140 m shadowed march with drifting density, for shafts. It gave a grey veil and no
  shafts, because the light comes from straight above and nothing overhangs the open floor.

**Results** (`05-ocean/01-ocean-before-after.png`; numbers from `05-ocean/critic-E.json` against
`00-baseline/critic-baseline.json`):
- **The film's ocean frames got bluer and lost contrast.**
  - RMS contrast fell from 0.085-0.094 to 0.062-0.072. Luma is about the same.
  - The clip's saturation rose from 0.46 to 0.53. Its sharpness rose from 773 to 1105, because the floor's
    texture reads through the blue. Flicker fell from 0.028 to 0.017. There are no findings.
- **Judged by eye:**
  - The water reads as a medium: brighter above, the far floor sinking into it.
  - The floor no longer reads as dry sediment.
  - But the whole picture is hazier and flatter than M3g's. The silhouettes of the shelf walls against the water
    are softer.
- **The next step, if the owner agrees:** keep the column and the gradient, and win the contrast back:
  - fogSky from 1 to about 0.5, and a darker horizon, so the walls keep their edge;
  - the shelf tops lit a little more than the floor;
  - the seabed's geometry: the evenly scattered cone mounds noted in 59- §4.
- `e-ocean` and `h-ocean` compare against the re-rendered baseline in `00-baseline/frames/`. The Critic's baseline
  numbers for those two are from the superseded red renders, so only the film frames' numbers are like for like.

## 5. The regression guard after E

- **Re-rendered:** every regression frame, the ocean's eval cameras, and the night and ocean clips. Results are in
  `08-regression/`.
- **Luma every half second against the baseline** (night and ocean new, the other legs unchanged): no flagged
  half-second (`11-luma-after-E-vs-baseline.txt`).
- **Near geometry:** no flags over the whole film.
- **The night:** forward vs velocity 20.1 / 7.3 (M3g 20.3 / 10.0); clearance 3.45 m over the terrain before the
  dive, 16.5 m to a mesh.
- **The crossings:**
  - Mars to night is unchanged.
  - Night to ocean: the glide's new heading is mapped through the portal, so the ocean side's arrival is the same
    frame.
  - Ocean to dawn is unchanged.
- **The other legs** (canyon, Moon, dusk, Mars, dawn, and their crossings: 26 frames) were re-rendered after the
  ADR-1910 shader change. They are **bit-identical** to the baseline (mean and max difference 0), so the column is a
  true no-op where it is off (`12-other-legs-pixel-diff-vs-baseline.txt`).
- **Still open, unchanged by E:** the Moon's black 37.5-43 s (Phase F, fix (a)).

## 6. Phase F (the Moon): fix (a) cannot work -- the camera is looking at the empty sky

- **What the black frames contain:** nothing. The depth AOV of 38-40 s is the sky in every pixel.
  - From 37.4 to 43.2 s the Moon flight climbs from 8 m to 158 m over the ground (the portal block's knots).
  - M3g's gaze has `climbLook` 1.0: the camera looks along the climb, up into the black sky.
  - The Earth slides out of the bottom of the frame, and no ground is in view.
  - In M3f the aims held the Earth in frame.
- **Fix (a) tested:** earthglow raised from 0.09 to 0.6, an unshadowed fill. The frames stay 100% black (luma 0.0 at
  40 and 42 s), because there is no lit surface in them to light. Reverted.
- **Fix (b) probed, not applied:** the Moon portal's `climbLook` 0 and `lookDown` 0.12. The camera then looks along
  its horizontal travel while it climbs. The Earth, the regolith and the domes are back in frame from 37 to 43 s,
  much as in M3f.
  - It is still "looking where it travels" in heading. Only the pitch no longer follows the climb.
  - Evidence: `06-moon/01-moon-37-43s-left-now-right-probe-level-gaze-b.png` (640 px probes at 2 fps).
- **Waiting on the owner,** as asked ("show me before you add (b)").
- **A blind spot in the guard:** while the camera is in a portal world (Moon, Mars, ocean), the depth AOV is the
  root's, which is the sky. `near_track.py` therefore measures only the canyon legs. The luma track does see the
  portal worlds, and caught this one.

**The owner's decision (2026-11-04): the Moon camera as it was in M3f.**
- **The change:** the Moon portal block no longer takes M3g's GAZE. It takes the portal defaults (ADR-1909: look
  1.2 s ahead, a little down, a quarter of the climb's pitch), which is M3f's Moon gaze. The flight's knots are the
  same as in M3f.
  - The change is scoped to the Moon: `build_journey.py` pops the GAZE keys from that one block.
  - Every other leg's camera trace is unchanged. The canyon, Mars, night, ocean and dawn are identical; dusk differs
    only at the hand-over out of the Moon portal, which moved from 41.7 deg to 40.4 deg.
- **An owner-approved exception to 54- §1, for the Moon climb only:** forward vs velocity is over 25 deg from 37.40 to
  44.37 s (max 64.5, p95 50.1). The camera keeps the Earth and the domes in frame instead of the empty sky it
  climbs toward. The guard treats that span as approved, not as a regression
  (`06-moon/14-forward-vs-velocity-M3f-moon-gaze-owner-exception.txt`).
- **Luma:**
  - Against the pass-3 baseline: nothing flagged. The black 37.5-43 s is gone.
  - Against the M3f render: 36-44 s matches within a few levels (for example 40.0 s: 21.4 now against 20.9 in M3f).
    The only flags are 93.5-100.5 s, the approved Mars cut shifting the night.
- **Crossings:** canyon to Moon and Moon to dusk are unchanged (`06-moon/10-moon-regression-before-after.png`).
- Earthglow stays at 0.09.
