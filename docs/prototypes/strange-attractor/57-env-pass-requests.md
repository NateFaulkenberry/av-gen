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

### H-4: the night and dusk lines -- keep the clearing's geology out of their corridors (Phase C, if it resumes)
Status: open, updated 2026-11-06 for round 2 of the paths (65-, 66-). The SA agent owns the paths.
- **Night (93.5-121.2 s):** one straight line, 182 m, about 3.2 m over the water, down the river at about 6 m/s, from
  the new entrance in the far bank's rock (25, 1205) to the moved river portal (53, 1385), in open water. The last
  few metres are the same glide into the same portal.
  - The camp passes 26.6 m off on the camera's left at 115.9 s. The bank stops the line going closer.
  - Corridor: 4 m either side, 3 m under, along the whole line and around the portal.
  - The gathering and the craft are in frame on the left until 108.5 s, readable from about 100 s.
- **Dusk (47.9-69.7 s):** one gentle curve from the far bank's exit over the river, past the gathering about 15 m
  on its river side, about 5 m up, then up over the benches to the outer cliff's portal.
  - Corridor: 4 m either side, 3 m under.
  - Knots: `strange-attractor-moon.scene.json`, `portal.flight`.

## From the hero agent to the SA agent

(none yet)
