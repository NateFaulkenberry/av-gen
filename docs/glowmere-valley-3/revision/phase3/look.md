# Phase 3, gv3-look: audio reactivity, the drop, water, wind

The stream's iteration log (brief §17). Each iteration names what changed, what was measured and
what was decided. Branch `gv3/look` in `~/Documents/GitHub/av-gen-gv3-look`, from `gv3/production`
`334c4cf6`; shared engine `040d6644`. Renders are 960×540 clips through the GPU lock; the Critic
session is `gv3-look`, one track per clip range. "Before" is always the same range rendered from
`334c4cf6`'s generator output (the first pass plus the foundation's energy arc) on the same engine.

Evidence (stills, sheets, side-by-side clips) is in
`~/Desktop/av-gen-review/18-glowmere-valley-3/revision/look/`.

## How the reactivity is made now

`tools/gv3/reactivity.py` makes the film's reactivity a generator step:

1. **Prepare** what the Director's planner reads: two wave fields from the elder (a ring on every
   downbeat for the fungi and shelf fungi; one valley-wide ring on the drop for the beacons and the
   heroes), and the heroes ranked by how much of the cut they fill.
2. **Propose:** `avgen --project <scratch copy> --propose-reactivity` (ADR-927).
3. **Edit** item by item (`EDITS`, `DROPS`, `DROP_TARGETS`, each with its reason).
4. **Install** as ADR-927 says: routes, sources, parameters, and the plan in `directingPlans`.
5. **Audit:** `avgen --audit-routes` on the installed project; the generator prints any route or
   track that is not live.

`look.py` keeps authored only what the score writes and a detector would place wrong: the crash's
flash, the four kick gaps, the kicks the elder's heartbeat reads, and the river's bass.

## Iterations

### Iteration 1: the proposal as a generator step (the configured tier)

**What changed** (commits `73202437`, `c072df70`, `31e339ad`):
- The first pass's eleven hand-written routes are replaced by the planner's proposal, edited and
  installed by `reactivity.py`, plus five authored events in `look.py` (the crash's flash and bloom,
  the two kick-gap dips, the river's bass) and one production addition (the riser's roll on the
  fungi).
- The planner's edits: the elder's five kick items read the scored kicks (`timeline.kick`, event
  mode) instead of the detector's low onsets, with the first pass's short fall (140-200 ms). Six
  macro items are dropped because the arc already keys their targets per directive (fog, ecology
  light, spores, fireflies, motes, water glow); on the fog the proposal would have thickened the air
  in the drop that the arc clears. The tears' low end is held at 0.28 (the validator's warning).
- The waves: a ring every downbeat from the elder for the fungi and shelf fungi (fading out by 45 m,
  since a ring restarted every 1.85 s never gets further than 37 m at 20 m/s), and one ring on the
  drop at 40 m/s for the beacons and the heroes' parts, so the valley relights outward on the crash.
  The planner does not look inside a compound field (it reports a compound of triggered waves as
  "on the transport clock"), so each layer names its own field: an engine limitation, reported.
- The heroes ranked by how much of the cut they fill (FEATURED): the planner hands the layers out by
  importance, and the scene's importances were a ramp in creation order, which gave the bar's swell to
  the spire (never more than 40 px at 1080p) and the lead's slow level to the umbra's one close-up
  (s35, 480 px). Now: elder kick, lantern clap, bloom downbeat, umbra breath, cairn phrase, spire
  lead, veil bass, ridge/scree/ember the section changes (relighting outward at 120 m/s).
- The first pass's shared-material kick and breath routes are gone (they put all ten heroes in
  lockstep); the scored pulses are event-mode sources.
- The dead arcs are fixed: the crowns' fireflies keyed on their layer (`layer/1/fireflies`, base 16),
  the painted ground's dropped, the tissue program's lost op dropped; `scene/ecologyLight` joins the
  arc with the valley's light.
- The aurora's own spectrum response is off (`fx/aurora/audioSensitivity` 0): measured on the
  arrival's grand wide (s14, top fifth of the frame), its raw per-frame bands made the sky jump by up
  to 44% between consecutive frames, in the first pass's v2 render and in this engine's alike.

**Measured (configured tier, `avgen --audit-routes`):**

| | first pass (334c4cf6) | iteration 1 |
|---|---|---|
| project routes | 11 | 65 (5 authored, 59 proposed, 1 added) |
| audit: routes live | 30 of 33 (3 hazards: value pulses missed at 30 fps) | 87 of 87 |
| audit: tracks live | 68 of 71 (3 dead arcs) | 72 of 72 |
| the planner's validator | 8 OVER_SATURATED (the shared-material routes) | 0 errors; 1 warning (the tears' x0.70 floor), which the edit removes |
| unknown parameters at load | 9 (18 warnings) | 0 |
| routes by level | — | micro 17, meso 34, macro 8, plus 6 authored/added |
| distinct route sources | 6 | 16 |

### Iteration 2 (v3): behavioural and meaningful tiers -- IN PROGRESS at the session handoff

**State at 12:05, 2026-09-27.** The generator is at the commit named in the next section; `v3` is
iteration 1 plus the roll (`timeline.roll` on the fungi). The field names were then changed to what a
viewer sees (`elder-rings`, `drop-ring`; the look is identical). No v3 clip has rendered yet: the GPU
lock was held by the coordinator's GPU suite, then gv3-cut's full-film render, with three other
waiters, for about 50 minutes.

**What has been measured so far:**
- One clip on the first shared engine (`040d6644`, not comparable with anything since, because the
  coordinator moved everyone to `av-gen-engine-2` `ec515c8b`): v1, 74.33-89.10 (s14 grand wide, s15
  bloom, s16 gills). Critic job `job_1a0e3635ac541e4fd` (session `gv3-look`, track `arrival-heroes`,
  preview, strict, complete). The elder's heartbeat in s16: **+8.1% of the region's luma, z 38**
  ("configured and observed"; the first pass measured +9% in s16). The lantern's clap in s14 (a
  38 px speck at 540p): no measurable response. The cairn's bass level: none (a slow level, not an
  onset). The grid found the sky answering the low band at up to +22% (lag 33 ms) -- the aurora's
  own per-frame spectrum response, which the measurement below confirmed and v2 switched off.
- The sky flicker: the top fifth of s14 jumps by up to 44% between consecutive frames (76.617 ->
  76.633 s: 0.113 -> 0.163 luma) in the first pass's `v2.mov` and in this engine's render alike.
- The first pass's hue distance, plateau against drop (`measure.py hue` on the first pass's v2.mov,
  chroma-weighted HSV hue, earth mover's distance on the hue circle): 2.5-12.2 degrees between the
  plateau's three shots and the drop's four; the plateau's own shots differ from each other by
  4.5-5.3 degrees. So the first pass's style shift shows mostly in exposure and saturation (mean
  chroma 0.23-0.31 on the plateau, 0.28-0.43 in the drop), less in hue.
- Wind, computed (a replica of `wind.cpp` for the scene's values, tip travel at speed 0.74): grass
  gust swing 4.9 cm every 9.5 s -> 12.2 cm every 3.25 s, flutter 0.9 -> 3.0 cm; ferns 6.3 -> 15.2 cm,
  flutter 1.2 -> 4.2 cm; flowers 3.2 -> 7.1 cm; canopy trees sway 3.2 cm at 0.08 Hz -> 6.5 cm at
  0.19 Hz; bushes, fungi unchanged. The drop's x1.3 wind: ferns 19.8 cm. A breeze, not a storm.

**Current values** (all in `tools/gv3/`): see `reactivity.py` (WAVE_FIELDS, WAVE_LAYERS,
HERO_DROP_RING 1.5, FEATURED, EDITS, DROPS, DROP_TARGETS, ADDS, SOURCE_PARAMETERS) and `look.py`
(BASE incl. `fx/aurora/audioSensitivity` 0; WATER; WIND; MOTION; LAYER_LIGHTS; ECOLOGY_LIGHT 1.4;
the drop's directive in `directives.py`: ev +0.15, sat 1.10, temp +0.05, fog 0.85, light 1.6,
aurora 2.4, sparkle 1.5).

**Open questions:**
1. Do the elder's rings read (fungi within 45 m of the elder, every bar from groove 2)? Judge on s08,
   s17, s19 at 1080p; raise the speed or the fade radius if they are too local to see.
2. Does the drop's ring read in s33/s34 (beacons and heroes lighting outward at 40 m/s)? The heroes
   visible in s33 are the elder, spire and ridge; the lantern, spire, cairn and ridge in s34.
3. The mushrooms' hue in the drop (-0.08 turn: teal toward green). Check the hue histograms plateau
   against drop; soften to -0.05 if the drop's palette moves.
4. The tears after the wind was turned down the valley (1.45 rad): the water stream tuned them at
   0.62 rad. Check s02, s08, s12, s14, s26; re-place with `tearSpacing`/`tearCoverage` if no seam
   reads where it should.
5. The slow level routes on the specks (spire on the lead, veil on the bass level) will read as
   "configured but no measurable response" in the Critic; keep (harmless background life) or drop.
6. Not done: the ecology light's colour (the fungi cast the source's purple while showing teal; teal
   would light the floor about 3.8x brighter at the same power -- a variant to judge, not a default).

## Resuming (the exact next steps)

Tools (in the worktree, not committed): `build/gv3/look/pair.sh` (several clips of one range under
one lock acquisition), `render.sh`, `measure.py` (hue, region, lock, motion), `sheet.py` (pairs and
strips), `chart.py`; scratch projects in `build/gv3/reactivity/examples/world/` (`v3.json` etc.,
snapshots of the generated project made with `cd tools && python3 -m gv3.reactivity snapshot NAME`);
the first pass's project as `build/gv3/look/examples/world/before.json`; Critic inputs in
`build/gv3/look/critic/` (`<name>-inputs.json` = the gv3-v2 scene, the regenerated intent
`v2-intent.json`, and `<name>-modulation.json` from `critic_modulation()`: the gv3-v2 scene's own
modulation lists only the first pass's routes, and the adapter maps only material targets to
entities).

1. Engine: `build/release` -> `/Users/natefaulkenberry/Documents/GitHub/av-gen-engine-2/build/release`.
2. Render the pairs (before and v3 of the same range, one lock each):
   ```
   cd ~/Documents/GitHub/av-gen-gv3-look; B=$PWD/build/gv3/look/examples/world/before.json
   V=$PWD/build/gv3/reactivity/examples/world/v3.json
   tools/gpu-lock.sh build/gv3/look/pair.sh 74.33:89.10 C $B before $V v3      # arrival: heroes, rings, sky
   tools/gpu-lock.sh build/gv3/look/pair.sh 177.71:192.48 D $B before $V v3    # the drop
   tools/gpu-lock.sh build/gv3/look/pair.sh 0.0:18.94 A $B before $V v3        # open: ferns, lantern, pool
   tools/gpu-lock.sh build/gv3/look/pair.sh 30.02:44.79 B $B before $V v3      # groove 2: rings begin, tears
   tools/gpu-lock.sh build/gv3/look/pair.sh 89.10:103.86 E $B before $V v3     # plateau (for the drop's hue)
   tools/gpu-lock.sh build/gv3/look/pair.sh 166.63:177.71 F $B before $V v3    # riser: the roll, s26 water
   ```
   Clips land in `build/gv3/look/clips/<before|v3>-<tag>.mov`.
3. Submit each clip (background), e.g. for C:
   ```
   ~/Documents/GitHub/creative-critic/.venv/bin/critic submit --inputs build/gv3/look/critic/v3-inputs.json \
     --video $PWD/build/gv3/look/clips/v3-C.mov --video-start 74.33 --mode preview --session gv3-look \
     --track arrival-heroes --label v3 --wait --json --strict
   ```
   and the before with `before-inputs.json` (the first pass's routes and intent). Compare with
   `critic compare <before job> <v3 job>`. Tracks: arrival-heroes (C), drop (D), open-wind (A),
   groove-water (B), plateau (E), riser (F).
4. Measure: `measure.py hue` (E against D, before and v3), `measure.py motion` on static shots (s15,
   s34, s26) for the wind, `measure.py lock` for the heroes in their feature shots (s04 lantern on
   claps.json, s15 bloom on bars.json, s35 umbra), `tools/gv3/shimmer.py` on s26 for the water.
5. 1080p stills for the small mushrooms and the tears (`--size 1920x1080`, a short range at s08
   33.7, s14 78.0, s33 179.5, s01 4.0), and the owner's reference beside them.
6. Evidence into `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/look/`; each iteration here.
7. The final report (the brief's list), including the change outside my files:
   `tools/make_glowmere_valley_3.py` passes the scene to `look.apply_base` and `look.apply_motifs`.
