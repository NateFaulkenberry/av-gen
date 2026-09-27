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
flash, the four kick gaps, the kicks the elder's heartbeat reads, the claps the lantern reads
(iteration 3), the riser's roll, and the river's bass.

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

### Iteration 2 (v3): behavioural and meaningful tiers, measured on five clip pairs

`v3` is iteration 1 plus the roll (`timeline.roll` on the fungi); the field names were then changed
to what a viewer sees (`elder-rings`, `drop-ring`; the look is identical). The session that built it
hit its usage limit at 12:20 with the clip pairs queued; they rendered from 12:40 (below).

**Measured before the pairs rendered:**
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

**The clip pairs.** Engine-2 (`ec515c8b`), 960×540, "before" = `334c4cf6`'s generated project,
"v3" = `build/gv3/reactivity/examples/world/v3.json`; Critic session `gv3-look`, preview, strict.
Sheets are in `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/look/iteration2-v3/`.
- **C (74.33-89.10, the arrival) did not render:** its lock wait hit the lock's 3600 s limit at 12:32.
  The arrival's only clip is still v1 on engine-1 (above).
- **F ran under GPU contention:** a lock race (13:00:24) let gv3-cast render beside it (the
  coordinator's note). Its content is the same; nothing below rests on F's exact pixels.

| range | before job | sync | effects | coherence | observed | responding | v3 job | sync | effects | coherence | observed | responding |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| A 0-18.94 open | job_1a0e3cbea11efad00 | 0.72 | 0.85 | 1.00 | 3 | 0.208 | job_1a0e3cc1d3942bfd3 | 0.81 | 0.90 | 1.00 | 5 | 0.125 |
| B 30.02-44.79 groove 2 | job_1a0e3cc4b493441cb | 0.81 | 0.90 | 1.00 | 3 | 0.042 | job_1a0e3cc6a8240eea3 | 0.61 | 0.78 | 1.00 | 0 | 0.021 |
| D 177.71-192.48 drop | job_1a0e3c140b9b75972 | 0.55 | 0.74 | 0.98 | 4 | 0.062 | job_1a0e3c37e25c3e864 | 0.58 | 0.76 | 1.00 | 0 | 0.0 |
| E 89.10-103.86 plateau | job_1a0e3d34bda8778f7 | 0.69 | 0.83 | 1.00 | 0 | 0.229 | job_1a0e3d3688525f82f | 0.61 | 0.78 | 1.00 | 0 | 0.0 |
| F 166.63-177.71 riser | job_1a0e3d38b2451c141 | 0.83 | 0.91 | 0.96 | 0 | 0.042 | job_1a0e3d3bd510413cf | 0.83 | 0.91 | 0.96 | 0 | 0.0 |

(sync = musical_synchronization, coherence = visual_coherence, observed = routes "configured and
observed", responding = the meaningful grid's responding fraction.)

**What the pairs showed** (the Critic, plus `clipstats.py`/`sheet.py`/`skyband.py` in the session's
scratchpad: per-shot chroma-weighted hue histograms and their earth mover's distance on the hue
circle, sky-band luma and frame-to-frame change, beat-locked averages):
1. **The elder's heartbeat is half the first pass's.** The planner's +0.60 is scaled by the section's
   depth (0.19-1.0): x1.23 in the cold open, x1.4 in the grooves, where the first pass had x1.6 on
   every kick. The elder's region: A +4.1% (z 9.7) before, +2.1% (z 8.1) v3; s08 +3.5% (z 10.4)
   before, +1.7% (z 3.7) v3, which B no longer counts as a response. In the orbit of s17 neither
   pass registers (+0.5%): the gills are a thin gold band inside the elder's box at that distance.
2. **The lantern never answered.** `audio.onsetMid` is not the clap in the film's big sections: the
   planner's own section profiles (the proposal's `music.sections`) put it at 1.0-1.1/s in the cold
   open, riff, groove 2, lead and suspension (the claps' 1.08/s), but 3.4-3.8/s in the lift,
   arrival, plateau and drop (the 16th shakers and the mid synth), and 1.9-3.0/s in the break and
   riser, which have no clap. The song itself, band by band on the 130 BPM grid: above 8 kHz beats 2
   and 4 rise 5-19 dB more than 1 and 3 in every groove section; in the 2-6 kHz band (the snare
   detector's) 1/3 and 2/4 are alike from the lift on. The lantern in s04: z 0.3.
3. **The aurora lost its brightness and kept its kick.** "Audio response" 0 removes only the
   shader's per-frame band terms (the curtain's lift, folds, waves, filaments); the raw per-frame
   spectrum still sets every curtain's top (spectrum shape 0.75, `updateAuroraSpectrum`, unsmoothed).
   The drop's sky band fell 25-57% (s33-s36) and the plateau's 33% (s17), the curtains are nearly
   gone in the frames, and the sky still pulses on the beat (+12-16% beat-locked in s33 and s36; the
   first pass +16-61%). Frame-to-frame change in s17's sky: median 8.4% before, 4.7% v3. The default
   effect routes (`beat.pulse -> edgeBrightness` and the rest) are not installed in either project
   (the audit's `installed: false`), so they are not the cause.
4. **The "meaningful" responses the first pass had were the aurora's.** The grid's responding
   fraction was 6-23% before (low and low-mid groups at 0-100 ms) and 0-12.5% in v3: nothing that
   large moves with the music in v3's frames at 540p. The heroes' and mushrooms' routes are live and
   configured, but at 540p they are specks except in close-ups.
5. **The drop holds the hue but went dark and lost its accents.** Plateau (s17) against drop
   (s33-s36), pooled hue distance: 2.84 degrees before, 1.67 v3 (the plateau's own shots differ by
   4.5-5.3). But the drop's luma is 0.132 against the plateau's 0.175, chroma 0.231 against 0.303 (the
   first pass: 0.175/0.316 against 0.183/0.315), and the drop's pink and magenta mushrooms turned blue
   (the proposal's -0.08 turn): the pooled histogram misses small accents; the frames do not.
   The lower `hueField` (0.16 -> 0.07) is not the cause: v1's arrival (hue offset 0) keeps them.
6. **Water:** v3's base (ripple 0.1 at 2.6) gives larger, clearer ripples in the pool (s02) and
   breaks s08's moon glint into sparkle; a tear seam catches the light by the rock in s02. The seams
   are to be judged at 1080p.
7. **Wind: not measurable at 540p.** The frame change in the lower 40% of the frame is the same
   before and v3 in every shot (x0.84-1.05), and in the static s34 (the stars hold still) its
   temporal map shows the foreground plants barely moving in either. Those large plants are the
   `fan-plants` layer (tip 0.08, sensitivity 0.65), kept at the source's stiffness; the retuned grass
   and ferns are a pixel or two of swing at 540p.

**Decided** (iteration 3): the aurora a steady curtain at the first pass's average brightness; scored
claps for the lantern; the elder at the plan's +60% to +100%; the drop at the plateau's hue.

### Iteration 3 (v4): the aurora steady and bright, the lantern on the claps, the heartbeat restored

**What changed** (commit `8dc975a2`):
- **The aurora** (`look.BASE`): spectrum shape 0.75 -> 0, with audio response kept at 0, so nothing in
  the sky reads the audio frame by frame. The average of what the first pass's audio terms gave is
  put back as fixed values: the spectrum held the top at about 0.8 of the curtain and the bass lifted
  it about 1.25x, so a flat top stands as tall; the high band tripled the filaments' base
  (0.4 + 1.6 x 0.7 x ~0.5), so filaments 0.95 -> 2.3; the mid band's folds and the low-mid's waves
  likewise (turbulence 0.45 -> 0.68, wave amplitude 0.30 -> 0.38). The sky answers the lead slowly
  (`lead.aurora`) and the sections (the arc).
- **The claps** (`look.claps`, a scored event-mode source like the kicks, 20 ms lead): beats 2 and 4
  of bars 1-14, 17-88 and 97-122 (223 claps; none in the pull-back, break or roll). The lantern's
  four routes read them, at 0.75 on its parts (x1.47 in the riff, x1.75 in the drop); the beacons
  echo them (60 ms later).
- **The elder's kick** at amount 1.0 under the section depth: x1.39 in the cold open, x1.63-1.67 in
  the grooves, x1.92 at the arrival, x2.0 in the drop (the plan's +60% to +100%).
- **The drop's hue** held at the plateau's 0 (`reactivity.HUE_HELD`), in the installed source and in
  the plan's record of it, whose reason says so.

**Configured tier:** audit 87/87 routes, 44/44 non-camera tracks live (engine-2's audit leaves out the
28 camera tracks that iteration 1's 72 counted); 0 unknown parameters at load.

**Renders** (queued at 13:05, one lock acquisition, `build/gv3/look/multi.sh`): D, A, and a 7 s slice
of E (93-100) at 960×540 for the Critic and the hue check; at 1920×1080, s33 (the drop's ring),
two bars of s39 (bars 115-116: the elder's rings in the last wide) and s08 (33.2-34.4: the tears).
They render `v4.json`, the snapshot of `8dc975a2`.

**Measured** (13:42-13:55; D rendered while one other render was running, the rest alone; the
Critic's inputs regenerated with the fixed adapter `961e04c` -- "ember-cap" was mapped to the alien
Ember -- into `build/gv3/look/critic/adapter-961e04c/`, and v3's D and A re-scored on them as `v3n`
so v3 and v4 differ only in the film):
- **The sky.** The aurora is back: s33's sky band 0.061 (v3) -> 0.076 (first pass 0.081); in s39 at
  1080p the sky matches the first pass's final to within +26%/-19% by height band (the curtains a
  little taller, the horizon band a little dimmer: the drop's intensity keys 2.4, the first pass's
  style shift 3.2). Frame-to-frame change in s39's top 30%: median 6.8% (first pass) -> 1.4% (v4),
  p99 36% -> 9.5%. The beat-locked pulse in the drop's sky: s36 +66.5% (first pass), +19.5% (v3),
  +2.5% (v4); s33 keeps +11% (the elder's cap reaches into that band, and its kick is x2.0 there).
- **The heartbeat** (gold pixels only, detrended, averaged on the kicks, peak 17-67 ms after them;
  noise 0.1-0.7%): s01 +7.7% / +3.8% / +6.1% (first pass / v3 / v4), s02 +4.6 / +2.4 / +4.2,
  s17 +6.4 / +4.4 / +4.9, s33 +11.7 / +5.6 / +8.6. v4 is 0.74-0.91 of the first pass's (v3 was about
  half). The Critic's box-based number fell in A (+2.3% -> +1.3%) because the restored aurora behind
  the elder dilutes its box; the gold itself rose.
- **The lantern** on the scored claps: A "weak response (uncertain)", z 2.9, +2.0% in s01 (v3n: no
  measurable response, z 0.9). In s04 and the drop too few claps fall inside a shot at these
  lengths for the route-locked check.
- **The drop's colour.** Plateau (s17) against drop (s33-s36), pooled hue distance: 2.84 degrees
  (first pass), 1.67 (v3), 2.06 (v4), inside the plateau's own 4.5-5.3. Luma: plateau 0.186, drop
  0.146 (v3 0.175/0.132); the drop is a new state in light and activity, not in hue. The small
  mushrooms that are bluer than the first pass's at the same spots are the lowered `hueField`
  (0.16 -> 0.07, which moves single mushrooms both ways and is the same in every section), not the drop.
- **The Critic, v3n -> v4 on D:** musical_synchronization 0.55 -> 0.61, effects 0.74 -> 0.78;
  visual_coherence 1.00 -> 0.98 and pacing -2.2, both from one low finding that s35 now repeats s34's
  layout and colour (0.94 similarity, the aurora in both): a cut matter, noted for gv3-cut.
- **The rings and the drop's ring are not demonstrated.** At 1080p neither the elder's rings (two
  bars of s39, the elder 150 m away) nor the drop's ring (s33) reads as a travelling front: the fungi
  are specks at that distance, and the frames show no band moving outward. Their configuration is
  live (the audit); their meaningful tier is open.
- **The water at 1080p.** The base ripple (0.1 at 2.6) covers the near river with bold ripples, as
  in the owner's reference; but no seam crosses s08's glint any more, where the water stream placed
  one: the seams are laid in the frame of the wind's direction, which iteration 1 turned (below).

**After the snapshot** (not in the v4 renders):
- `fd8cbc5a`: the scored claps leave out the four kick-gap beats (219 claps): the clap plays there,
  but that beat is the valley's held breath, and the lantern flaring through it would break the one
  moment the valley answers as one. No v4 range contains a gap beat.
- `272a93e0`: the fan plants sway (computed, not yet seen). They are the big broad-leaved plants in
  the foreground of s04, s17, s34 and s36, and they kept the source's stiffness: by wind.cpp's
  oscillator a 2 m plant swung 0.9 cm in a gust. Now tip 0.20, sensitivity 0.9, stiffness 2.4 and
  mass 1.2 (the same 0.225 Hz resonance), gust response 1.2: about 4.6 cm of gust swing and 2.8 cm of
  flutter, a third of the ferns' 13 cm. To verify on the static s34.
- `4fce79f5`: the wind keeps the source's direction, 0.62 rad, which the tears were placed for
  (water.wgsl lays the seams on a lattice in the frame of the wind's steady direction, so turning
  the wind to 1.45 rad rotated every seam away from where the water stream put it). The rest of the
  wind's retune stays. To verify on s08 at 1080p.

**Open questions:**
1. The elder's rings do not read in the shots that show the elder from afar (s39 at 1080p). They
   need a shot near the elder's feet with the fungi in the foreground (s16, s17's low passes, s40),
   judged at 1080p; or a larger fade radius and brighter crest if they are to read in wides.
2. The drop's ring does not read as a front in s33 at 1080p. Candidates: carry it on the heroes and
   the beacons only where they are large in frame, or make it the ecology light's (the light the
   layers cast follows their `emissionGain`, not their `emissiveField`, so the ring lights the
   mushrooms but not the ground around them).
3. The tears with the wind back at 0.62 rad: verify the s08 glint seam at 1080p (queued, v5).
4. The slow level routes on the specks (spire on the lead, veil on the bass level) read as
   "configured but no measurable response" everywhere; keep (background life) or drop.
5. The wind's most visible plants, the fan plants, keep the source's stiffness (tip 0.08). A
   candidate: tip 0.14 and sensitivity 0.8, judged on a static 1080p shot with fan plants in front
   (s34).
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

Iteration 3 adds `build/gv3/look/multi.sh` (several ranges of one project under one lock
acquisition; it logs how many other renders were running at each clip's start and end) and, in
`build/gv3/look/it3/`, the analysis scripts: `clipstats.py` (per-shot hue histograms and their
distance on the hue circle, region luma locked to events), `sheet.py` (side-by-side frames at film
times, with crops), `skyband.py` (a band's luma and frame-to-frame change), `motionbands.py`
(frame change in the ground and sky bands, two clips), `claps.py` (where the clap plays, from the
song), and the hue histograms computed so far (`*.hue.json`; `fp-preview` is the first pass's
960×540 preview, from the review folder).

1. Engine: `build/release` -> `/Users/natefaulkenberry/Documents/GitHub/av-gen-engine-2/build/release`.
   **Trap:** `python3 -m gv3.reactivity snapshot|critic|audit` read the *written* project in
   `examples/world/`, which the commit rule restores to the committed (first-pass) version. Run
   the generator first, snapshot, then restore; check the snapshot's route count (65, not 11).
2. The iteration-2 pairs are in `build/gv3/look/clips/{before,v3}-{A,B,D,E,F}.mov`; C was never
   rendered. v4's clips land as `clips/v4-<tag>.mov` (tags D, A, E, s33, s39, s08); the batch log is
   `clips/v4-batch.log`.
3. Submit each clip (background), e.g. for C:
   ```
   ~/Documents/GitHub/creative-critic/.venv/bin/critic submit --inputs build/gv3/look/critic/v3-inputs.json \
     --video $PWD/build/gv3/look/clips/v3-C.mov --video-start 74.33 --mode preview --session gv3-look \
     --track arrival-heroes --label v3 --wait --json --strict
   ```
   and the before with `before-inputs.json` (the first pass's routes and intent). Compare with
   `critic compare <before job> <v3 job>`. Tracks: arrival-heroes (C), drop (D), open-wind (A),
   groove-water (B), plateau (E), riser (F).
   (`build/gv3/look/evaluate.sh <tag> <film start> <track> <name>...` does this; v4 uses
   `critic/v4-inputs.json`.)
4. Measure v4 against v3 and before: `it3/clipstats.py hue` (E slice against D), `it3/skyband.py`
   (the sky's level and its frame-to-frame change), the lantern in s04 and the elder in s01 (the
   Critic's route-locked rows), and the 1080p slices for the rings, the drop's ring and the tears
   (with the owner's reference beside them).
5. Evidence into `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/look/`; each iteration here.
6. The final report (the brief's list), including the change outside my files:
   `tools/make_glowmere_valley_3.py` passes the scene to `look.apply_base` and `look.apply_motifs`.
