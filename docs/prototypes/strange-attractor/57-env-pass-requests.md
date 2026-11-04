# Pass 3: requests between the agents

The hero agent writes engine and shared-file requests here; the SA agent implements them and answers in place.
The SA agent also hands over work here.

Format: one section per item, `Status: open | doing | done (commit)`.

## The shared cameras and the Critic (both agents use these)

- `examples/strange-attractor/pass3/cameras.json`: 52 cameras.
  - `regression` (40): the film's own frames, 4 per leg (`r-<leg>-<t>`) plus every crossing at +-0.5 s
    (`x-<from>-<to>-before|after-<t>`).
  - `eval` (12): per world, an establishing (`e-`) and a hero (`h-`) fixed camera. The traversal distance is the
    regression frames plus the per-leg clips.
  - Add cameras freely (supplementary, e.g. `h2-camp-wall`); never change one once its baseline is rendered.
- Render: `python3 examples/strange-attractor/pass3/render_cameras.py <out_dir> [--only 'h-mars,r-mars-*'] [--set eval]`
  (1920x1080, supersample 2, through tools/gpu-lock.sh).
- Judge: `python3 examples/strange-attractor/pass3/critic_run.py <frames_dir> <clips_dir|-> <out.json> --label <l>
  --worlds mars` (Critic session `sa-pass3`; tracks `stills-<world>`, `clip-<leg>` keep the history).
- The baseline: `~/Desktop/av-gen-review/101-strange-attractor/pass3-env/00-baseline/` (`frames/`, `clips/`,
  `critic-baseline.json`).

## From the SA agent to the hero agent

### H-1: Mars drift edges (M3g §6 leftover, 54- §3)
Status: open, handed over.
The marsDrift sand drifts show a hard edge where a drift meets the rock in places. The sand in the pyramids' courses
was raised in e3b2d57b (`pass2/build_pyramids.py`, outputs `pyramid-*-m3g.glb`, `pyramid-*-sand-m3g.glb`); the drift
edge blending on the terrain was not done. Files: `build_mars.py` (marsRock sand smoothstep [0.45, 0.75], the
marsDrift program), now yours.

### H-2: the Mars screenshot camera before/after (M3g §6 leftover)
Status: open, handed over.
The owner's screenshot from the Mars arrival (about 70.5 s) needs a before/after from the same camera once the Mars
work lands. The M3g frames at 70.5 s are the "before".

### H-3: the seated Sasquatch (sasquatch-c) on its seat rock
Status: open, for Phase C (the camp clearing is yours).
check_contacts (hero/check_contacts.py) results:
- before the drop (node y 1.76): a 6.3 cm gap over the seat; the fur shells clip the rock in 18/193 frames, 9.2 cm deep.
- after the 3 cm drop now in `build_journey.py` (SEAT_SIT = 0.03, node y 1.73): the body mesh goes into the rock in
  94/193 frames and the shells in 166/193, up to 9 cm deep.
- the best search result (seat-search, rock moved dx 0.021, dz 0.077, sy 0.88) had 0 penetrating frames and a 3 cm gap.
The 3 cm drop is too much for the animation's lowest frames. Suggest: the seat rock reshaped or swapped for one with a
flat top in Phase C, then SEAT_SIT re-fitted with check_contacts. SEAT_SIT stays in `build_journey.py` (shared file):
request the value here and the SA agent sets it.

### H-4: the new night pass -- keep the clearing's geology out of its corridor (Phase C)
Status: open, for the hero agent's Phase C. The SA agent owns the path (`build_mars.py`, `night_flight`).
The owner found the M3g night pass crept past one boulder for 18 s (104-118 s). It is replaced by a longer level pass
(coordinator, 2026-11-04). Knots, absolute world x, z of the canyon scene, with heights over the ground (over the water,
over its surface):

| t (s) | x | z | height | where |
|---|---|---|---|---|
| 93.46 | 66.5 | 1295 | 5.0 (abs) | the exit out of the bank (unchanged) |
| 93.9 | 58.0 | 1318 | 4.0 | out over the river |
| 94.5 | 47.0 | 1326.5 | 4.0 | |
| ~97 | 34.0 | 1329 | 4.0 | the wide left curve over the water |
| ~100 | 23.5 | 1336 | 4.0 | |
| ~104 | 21.0 | 1346 | 4.0 | |
| ~107 | 28.0 | 1351 | 4.0 | heading east, straight at the camp across the water |
| ~111 | 37.0 | 1352.5 | 4.0 | the camp, the fire and the craft ahead, 30-40 m |
| ~114 | 43.0 | 1354 | 4.0 | turning right |
| 117-121.2 | 47.8 -> 51.5 | 1358.6 -> 1367 | glide | the river portal's glide (25 deg, 3.2 m/s), heading (0.4, 0.92) |

- The corridor to keep clear: 4 m either side of that line and 3 m under it. It is all over the water except its
  last 6 m into the glide.
- **What the camera sees from it:** the clearing's river face and the gathering, from the west across the water.
  That makes the clearing's river side, the bank's lip and the far wall behind the camp the composition of 107-115 s.
  Anything placed on the bank's water edge between z 1340 and 1365 is in the shot.
- `campmap3.png` (top view, M3g path red, new path green) is in `pass3-env/08-regression/`.

## From the hero agent to the SA agent

(none yet)
