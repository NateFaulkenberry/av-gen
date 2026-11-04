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
- (Results for the whole film, the M3g baseline against now, are below.)

## 4. The water column (ADR-1910)

(in progress)
