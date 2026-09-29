# GV3 targeted art pass: progress notes

The brief is `00-brief.md` beside this file (the owner's words; it governs). One agent, branch `gv3/art-pass`,
worktree `../av-gen-art`, from qa/coord `b8e391ca`. ADR block **980-989** (coordinator, 2026-09-28; the range row is
in `docs/decisions/README.md`). Don't push, don't merge: the coordinator merges.

## Resume here

- **Status (2026-09-29, 15:30): REVISION ROUND 1 in progress** (the owner's words and the coordinator's notes are
  at the end of `00-brief.md`; the tracking is the section "Revision round 1" below). Items 1-7 done and committed
  (`9c86d2b1`, `e6e7719f`, `d18b64cf`, `1fa60913`); Critic passes 1 and 2 done. Investigating s50 found an engine
  determinism defect (below, "Found: a render is not the film its seeks and traces describe"); the owner chose
  option A, which the coordinator committed as `bce61fe9` (ADR-990): renders, seeks and traces are one film again.
  s50 is back on its original rig; s49 gets the elder's gold gills (the second bland shot). Next: commit, Critic
  pass 3 (rendered from 0), the full-vs-12 s render check with the new binary, frame costs, both suites (after a
  reconfigure and full rebuild), `GV3-art-pass-r1.mp4`, REPORT.md. No before/after comparisons of old film
  against new (the owner waived them).
  Items 6 and 7 came later from the coordinator (relayed as the owner's); recording them in `00-brief.md` was
  refused by the permission system, so they are described here only -- the coordinator or the owner can add them
  to the brief. Round 0 (the pass) is complete: its final is `GV3-art-pass-final.mp4` from `6aa3701d`, kept as it
  is. Round 1 delivers `GV3-art-pass-r1.mp4`, stills in `~/Desktop/av-gen-review/20-gv3-art-pass/r1/`, a "Revision
  round 1" section in that folder's `REPORT.md`; the Critic runs up to about three passes this round.
- **Tracing the rendered film:** `avgen_cast_trace ... --render-clock 1920x1080` (added this round; the default still
  steps a constant 1/fps, which is the seek's film, not the render's). Same for `avgen_foot_probe --render-clock
  1920x1080` (and `--fixed-clock`, `--warm-up`, `--viewport`, `--instants`, `--delta-difference` to take it apart).
  Any measurement that is to describe the film uses it.
- **Variant projects** for renders live in `examples/world/_art-*.json` (made by `scratchpad/art/artvariant.py`,
  never committed): DELETE them (`artvariant.py --clean`) before running the suites or committing.
- **Build:** `cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
  -DCPM_SOURCE_CACHE=/Users/natefaulkenberry/Documents/GitHub/av-gen/.cache/cpm`, then `cmake --build build/release`.
  Assets are linked (`../av-gen/tools/link-worktree-assets.sh ../av-gen-art`, 1,803 links).
- **The project is generated.** `python3 tools/make_glowmere_valley_3.py && python3 tools/gv3/ufo.py --no-trace`
  reproduces the committed r7b (`examples/world/glowmere-valley-3.{json,scene.json}`, README "The committed project is
  r7b"). Every change in this pass goes into `tools/gv3/` (data) or the engine, then the project is regenerated, so
  the project stays the generator's output. Never round-trip the project through Python `json` by hand, and never
  clean it with a headless load-and-save.

## The owner's morning render (coordinator, 2026-09-28 evening): a firm deliverable

- **When the pass is complete** (after the fixes and the Critic): the WHOLE film with the song muxed in, review
  quality, 1080p if it fits overnight (else 720p, and say why), saved as
  `~/Desktop/av-gen-review/20-gv3-art-pass/GV3-art-pass-final.mp4`. This replaces step 6's low-res preview.
- **Safety net:** if the pass will not be finished by about 06:00 local, render the whole film from the latest
  COMMITTED state first, as `GV3-art-pass-WIP-<short sha>.mp4` in the same folder, with a short `README.md` beside
  it saying what is and is not in it yet.
- Under `tools/gpu-lock.sh`; local only (licensed assets and the song: never upload). Record path, settings and
  duration here and in the final message.

## Rules for this pass (from the launch prompt and the brief)

- Targeted refinement, not a redesign; the fewest renders; the Creative Critic ONCE, at the end; then stop.
- Licences: never commit, cache or upload the purchased assets, the musician GLBs, their sources, or the song. Renders
  are fine. `git status` before every commit; the musicians' GLBs are ignored by `/assets/musicians/*.glb`.
- All GPU work under `tools/gpu-lock.sh`. CPU-only runs (the CPU suite, cast traces) outside the lock.
- Before finishing: `build/release/tests/avgen_tests` and `tools/gpu-lock.sh build/release/tests/avgen_render_tests`,
  judged by exit code (one `[!shouldfail]` FAILED line is expected). Reconfigure CMake after adding test files.
- Review material goes in `~/Desktop/av-gen-review/20-gv3-art-pass/`.

## What I found while reading (the facts the plan rests on)

### 1. Water (the hill is the north head, the hill water is `glowmere-falls`)
- `tools/gv3/world.py close_ends` built the north head and the falls: a river feature 16 m wide, bed 4 m, flatten
  0.85, from (-58, 48, -352) down to the river's new head (-58, 12.8, -286); the river `glowmere-run-2` begins there.
  The two surfaces blend by weight where both reach (ADR-894, `WorldMap::waterSurface`).
- The water mesh is `buildChunkWater` (`src/world/terrain.cpp`): the terrain's own grid (chunk 48 m, resolution 40, so
  1.2 m cells); a quad is drawn where any corner is wet; a dry corner takes the MAX of its wet neighbours' levels so
  a flat sheet reaches the bank and the terrain's depth test cuts the shoreline. On a steep course the max picks the
  uphill neighbour, so each edge corner stands up to one cell's descent above the local water: the suspect for the
  staircase edges (reference `water-2`). To be measured, not assumed.
- `avgen_world_preview --seams` reports 0 water seams on r7b, so the existing check does not see this defect.

### 2. Hero pulses
- GV3 fires each hero's `groundPulse` on cue markers "hero pulse <hero>" on every downbeat of a shot it leads
  (`look.py apply_hero_pulses`): lifetime 4.5 s, a new marker every bar (1.846 s at 130 BPM).
- `resolveWave` (`src/world/wave_effect.cpp`) draws ONE front per instance, from the latest trigger only
  (`resolveActivationWindow`), so each new marker replaces the ring 1.85 s into its 4.5 s life: the reset the owner
  sees. `effectEventTimes` (`effect_trigger.cpp`) already returns several recent fronts; Shockwave and Ripple draw
  one front each.
- GPU budget: `kMaxGpuWaves = 8` records a frame, shared with the travel beam.

### 3. Foot sliding
- Gait: `src/entity/gait.{hpp,cpp}` (hysteresis `moveEnter`/`moveExit`, `minDwell`, `accel`), the locomotion plan
  (`locomotion_plan.cpp`, STARTING/STOPPING with a stride ramp), clip requests in `scene/composition.cpp
  AnimationSink::setLocomotion`. Aliens: `moveEnter` 0.651 m/s, `accel` 3.472, `minDwell` 0.6 s, a real Idle clip.
  Farm animals: one `Walk` clip for everything, `idleRate` 0, rate matched to speed.
- Not yet measured. Plan: measure planted-foot drift at idle-to-move starts on the cast actually filmed.

### 4. UFO warp
- The effect library already has `SpaceWarp` (12), `VelocityDistortion` (30), `GravitationalLens` (42),
  `HeatShimmer` (41) (the DF family, `distortion_frame.hpp`, `shaders/distortion.wgsl`).
- Crafts: `visitor` (the saucer) and `scout`; beams `visitor-beam`, `scout-beam` (staging actors).

### 5. Hero mushrooms
- Ten heroes, each four procedural nodes `<name>-{cap,under,stem,gills}` from one generated `mushroom` source
  (18 values, `src/organism/mushroom.cpp`), a `<name>-spores` particle node, a `heroes` entry, a Hero Pulse effect,
  and reactivity routes. GV2's generator made them (`tools/make_glowmere_valley_2.py` "Phase 5", from
  `examples/organisms/glowmere2-heroes.json`).

### Aurora addendum
- `look.py BASE`: `fx/aurora/audioSensitivity` 0, `spectrumShape` 0, `audioBeat` 0 (zeroed because the per-frame
  response jumped the sky up to 44% between frames). The duplicated block values are in
  `docs/qa-pass/gv3-state-audit.md` item 1.

### Musicians addendum
- E5 (the riser centrepiece, `tools/gv3/ufo.plan.json`) lifts `horse-11` at (-4, 66), beam on bar 93, gone on the
  drop. The drummer replaces the horse.

## Water: what the probe found, and the fix (item 1)

**The instrument:** `tools/water_probe.cpp` -> `build/release/tools/avgen_water_probe <scene> <prefix> --region x0 z0
x1 z1 --step s`. It rebuilds each chunk's water mesh and its ground at every LOD exactly as a load does and
compares them on a fine grid with `WorldMap`: false water (sheet drawn above dry drawn ground; its height is the
"slab"), false dry (drawn ground through wet water), the drawn surface against `waterSurface`, steepness. Maps
per LOD. CPU only, ~2 s for the falls, ~9 s for the whole river at 0.3 m.

**r7b, the falls (x -84..-32, z -320..-250, 0.1 m):**
- LOD 0: 41 m2 false water, 24 m2 of it more than 30 cm proud, worst 1.61 m; the drawn surface up to 1.54 m off the
  world's; steepest drawn water 74 deg. LOD 3: 47 m2 false dry, worst 1.86 m.
- Whole river (0.3 m): the valley river is clean at LOD 0 (the 24 m2 over 30 cm are all the falls); LOD 3 has 143 m2
  of coarse ground showing through along the whole river.
- Cause 1, the stepped slab (reference `water-2`): `buildChunkWater` gave a DRY corner the MAX of its wet
  neighbours' levels. On the falls (a metre of descent per 1.2 m cell) that is the upstream neighbour, so every edge
  corner stood a cell's descent above its own row: the sheet's edge rose 1-1.6 m clear of the bank, row by row.
- Cause 2, the neck and the "disconnection" (reference `water-1`): the river started at the falls' foot and a river
  reaches `width` (26 m) past its first point in every direction, up the head's face to z -312. Where the two reach,
  `waterSurface` is the weight-averaged level, which dragged the falls' water 1.6-3.8 m below their own level from
  z -308 to -292 (cross-sections: `xsec.py` in the scratchpad; the falls' own smoothed level is 28.5 at z -305, the
  blend 25.5). At z -305 that left water 1 m deep and 8 m wide: a neck the 1.6 m shore fade turns half
  transparent, between the falls above and a basin below. That neck is the 74 deg wall.
- Cause 3, far chunks: the ground drops to LOD 3 (9.6 m cells) while water is always built at 1.2 m, and the coarse
  bank pokes through the river. (The 4K final already turns terrain LOD off; previews and the editor do not.)

**The fix (ADR-980; engine + one data change):**
1. `buildChunkWater`: a dry corner's level is the plane the water around it lies in (weighted least squares over
   the 5x5 window's wet points, ridge on the slopes, never above the highest wet neighbour). The grid is sampled 2
   cells past the chunk so both chunks sharing a border corner fit the same neighbourhood. A lake is bit for bit
   the old answer (one level all round).
2. `world::kWaterChunkMaxLod = 1`: a chunk that carries water never draws its ground coarser than LOD 1
   (`Composition`'s per-frame LOD pick).
3. `tools/gv3/world.py` `JUNCTION_ALONG = 0.25`: the river starts a quarter of the way down its own first segment,
   at (-51.75, 12.275, -269.5) on the valley floor, and the falls run on to meet it there, level for level.

**After (0.1 m falls / 0.3 m river):** falls LOD 0 false water 1.0 m2, worst 0.11 m, drawn surface within 0.11 m of
the world's, steepest 43.6 deg (the falls' own gradient); the falls a steady 16-20 m wide, ~4 m deep channel from the
top of the head into the river. Whole river: LOD 0 1.71 m2 (worst 0.11 m), LOD 1 1.71 m2 (worst 0.16 m), LOD 2 5.4
m2 false water (worst 0.23 m) and 2.4 m2 false dry (worst 0.35 m, in transparent shallows), no false dry at 0-1.
**Checks:** `world.py --check`: survey min/max unchanged; seams 0/0; water edge to edge (falls -> river); ground in
the filmed band z -190..170 moved 0.020 m at z -190 only (0.002 at -180, 0 from -170: P0 reshapes the smoothed
course to about z -203 and the river's 26 m reach carries it to -177; everything over 5 cm is at z -308..-198).
The check's 13 one-column "open end" views in s03, s24, s26, s65, s66 are PRE-EXISTING: r7b itself fails the same
way (checked on the committed files). Nav grid trusted (982 sampled walks). Audit 105/105 routes live.
**Side effect to report:** any change of the ground or water near the head changes which plants the northern scatter
rows accept, and a plant's hue jitter/glow is keyed on its index in its layer (world.py's own note), so plants south
of the head get their jitter re-dealt. Positions, sizes and yaws do not move.

## Hero pulses (item 2): ADR-981, done in code

- `resolveWaveFronts` (`src/world/wave_effect.cpp`): a Trigger activation with a lifetime gives each event its own
  front with its own fades (up to `kMaxWaveFronts` 4); the newest front is bit for bit the old one. `resolveWaves`
  places every instance's newest front first, then the older ones. `kMaxGpuWaves` 8 -> 16 (`common.wgsl`,
  `scene_renderer.hpp` offsets +1152, layout guards pass).
- GV3 needs no data change: its pulses are already trigger-activated with a 4.5 s lifetime. Worst live fronts in the
  film, counted from the project's own markers: 7 (of 16).
- Tests `[adr981]` in `test_wave_effects.cpp` (3 cases, 55 assertions) and the existing wave tests pass.

## Foot sliding (item 3): ADR-982, done in code

- **The instrument:** `tools/foot_probe.cpp` -> `avgen_foot_probe --project P [--start S --seconds N] [--names a,b]
  [--dump name:t0:t1] [--profile name[:clip]] --out f.json`. Offline engine, every rig posed every frame (run it on a
  copy of the project with `animation.updateHz 0`, as the final is: `scratchpad/probe_copy.py <name>` writes
  `examples/world/<name>.json`; DELETE the copy afterwards).
- **Aliens:** the gait held `Idle`/`Idle_turn` until `moveEnter` (0.1875 s of each alien's own acceleration), and the
  dwell refused the walk after a turn: planted feet slid a median 0.256 m per foot (p90 0.88, max 1.34 m) over 0.37 s
  at the 81 starts. ADR-982: a standing gait that has rested since its last step walks once over `moveExit`, past
  the dwell. After: median 0.018 m, p90 0.022, max 0.082 m, 0.083 s. Paths bit-identical (the gait picks the clip).
- **Animals:** unchanged by design (their idle is their walk, rate matched from the first frame). Residual start slide
  p90 0.12 m per hoof; 1.9 cm per hoof per metre over the film: the farm pack's stance speed varies 6x within a
  stance, which rate matching cannot follow. Needs foot locking on quadrupeds: recorded, not done.
- **Found, not changed (for the owner):** the aliens' ground layers plant each foot at its REST height every frame, so
  the swing foot never lifts in the drawn pose (Rook's feet hold 0.267 m through `Walking`; the clip lifts them
  ~0.24 m). Global foot-IK behaviour; out of an art pass's scope. Check a walking alien in the verification stills.
- Tests `[adr982]` in `test_entity_action.cpp` (3 cases); `[gait]` 24 cases pass.

## Aurora addendum: done in data (`look.py`), measurement pending

- Two smoothed routes, appended AFTER the reactivity proposal (`apply_motifs`; the proposer skips a target something
  already routes, so authored first they cost the aurora the lead's `lead.aurora` -- checked: the proposal is 60 of
  97 again and the route diff against r7b is exactly the two new routes):
  `audio.bass -> fx/aurora/intensity` multiply, remapped 0..1 -> x1.00..x1.16, attack 160 ms, decay 1100 ms; and
  `audio.bass -> fx/aurora/curtainHeight` add, 0..140 m on 2300 m, attack 240 ms, decay 1500 ms. The shader's own
  per-frame audio response and spectrum shape stay 0 (look.py BASE, the 44% flicker).
- Block/parameter duplicates: `look.sync_effect_blocks` (called last in `make_glowmere_valley_3.py`) writes every
  effect block value that has a flat `fx/<id>/<field>` parameter from that parameter, with the registry's own
  field -> block path tables (aurora, travel beam, ground pulse; a stored-value type keeps the field's name). 12
  values rewritten (the aurora's `shape/curtainHeight` 2600 -> 2300, `audio/sensitivity` 1.0 -> 0.0, ...), 0 left.
- **To measure (render batch):** the top fifth's mean luma frame to frame at 60 fps over s14 (the arrival's grand
  wide, the 76.634 s beat) and the drop (s33, s36), r7b against this pass; state max and p99 of the consecutive-frame
  change and the swell's size over a bar.

## UFO warp (item 4): ADR-983 + data, done; look to verify in the render batch

- **Data** (`tools/gv3/cast.py` CRAFT_WARP / CRAFT_WAKE, `craft_warps`): on each craft (`visitor`, `scout`) a Space
  Warp ("Spacetime warp": strength 0.4, lens pull 0.15, bow 1.0, swirl 0.06, turbulence 0.1 at 0.35, chroma 0.06, no
  rim, 2.2x the owner's bounds, stretch 1.0, full at 18 m/s) and a Velocity Distortion ("Spacetime wake": strength
  0.7, 1.2 s, width 1.2x, ripples 1.2 at 0.6 Hz, chroma 0.06, from 6 m/s, full at 40 m/s). At rest only the lens,
  curl and twist act; in flight the bow, stretch and wake (all weighted by the craft's HIST speed; deliberately NO
  speed route: `entity.<name>.speed` reads the hidden transit).
- **Beams:** `tools/gv3/ufo.py warp_cues` adds to the compiled plan (`build/gv3/ufo.plan.compiled.json`), for every
  set piece with a beam (survey, abduction), two cues per field: strength -> 0 on `beam` (0.5 s), back to the
  effect's own strength on `depart` (1.2 s); plus subject aliases `visitor`, `scout`. 16 cues; the horse's two stay.
- **Engine (ADR-983):** `NodeView::visible` (Composition records it per frame); Space Warp, Gravitational Lens, Heat
  Shimmer and the wake draw nothing around a hidden owner; HIST velocity/acceleration never difference across a
  placement (the craft read ~5,000 m/s on the frame it appeared); `HistoryBank::placementStart` +
  `EffectSceneQuery::nodePlacedSince` stop a wake at the placement. Tests `[adr983]` (4 cases) pass; `[hist]`,
  `[distortion]`, `[shockwave]`, `[follow]`, `[xform]`, `*placement*` pass.
- **To verify (render batch):** E2 flyby (s08, 26.6-29.9), E5 approach (s47-s50), E5 beam and lift quiet (s51-s57),
  E5 depart (s58-s59), one scout moment (E4 approach, s31-s33). Tune once if needed, then leave it to the Critic.

## Hero mushrooms (item 5): done in data, to verify in the batch

- `examples/organisms/glowmere2-heroes.json` now 12 winners (the search with `kWinners` 12: first ten byte-identical).
  New: **opal** (#251, thickest cap, glowing dome, emission structure 2) and **sail** (#707, thin tilted plate, one
  lobe, under-glow, structure 1). Sheet: `~/Desktop/av-gen-review/20-gv3-art-pass/mushroom-search-12-winners.png`.
- `tools/gv3/heroes.py` builds them with GV2's recipe (4 generated parts, cool materials, spores at the probed gill
  anchor, crown hero record, a Hero Pulse, under/gills `emissiveBoost` 0.2 like the ten) at opal (80, -5) 6.8 m and
  sail (42, 43) 7.4 m: the central east had no hero. Sites scored by `scratchpad/hero_sites.py` (shots that see
  them at 25-130 m, and the pulse rule's steals) and cleared of tall plants with `avgen_scatter_probe`. FEATURED
  gets them last (no existing route changed: route diff = new targets only). Hero pulses: they add rings in
  s11/s12 (herd) and elsewhere; the elder keeps its 53 rings; the lantern gives 2 (s03, s04) to the elder.

## Musicians (addendum): done in data and engine (ADR-984), to verify in the batch

- GLBs rebuilt with Blender 5.2 (the prototype's command + `--blend` into the review folder); `Flail` clip
  authored in `make_astronaut_musicians.py` (`make_flail`: the drum clip's first pose, swings on incommensurate
  rhythms, amplitude ramping 0.3 -> 1 over 2.5 s, 5 s, not looped). `astronaut_drums.glb` carries Drums + Flail.
- `tools/gv3/musicians.py`: drummer at (-1.7, 70.9) facing north, keyboardist (-1.7, 59.9) facing south (11 m),
  each group one transform x1.94 tilted to a fitted ground plane (2.3 / 4.3 deg); grass clearings under the
  footprints; entities (clips: drummer walk/run/fall -> Flail; gait matchRate off) and heroes; aura = a rainbow
  Ground Pulse as their hero pulse (own places beside the 2-nearest heroes, `look.PERFORMER_PULSE_NEAR` 90 m) +
  Bioluminescence photophores on the suits (hue variation 0.5), routes: drummer <- timeline.kick, keyboardist <-
  audio.mid, both x section.energy. Placement searched by `scratchpad/place_search.py` (no tall plant in a
  footprint, >= 4 m from every camera eye; s01's path passes 4.2 m from the kit: WATCH s01 in the render).
- E5: `ufo.plan.json` lifts `drummer` with `returnSeconds` 6.0; the trace: lifted 172.783, gone 177.700 (the drop
  frame), back 183.733 (inside s60, which does not see the kit), 1 mm from his seat. horse-11 re-homed to the east
  meadow (70, 34); the horse's Glow and its cues removed. s04 re-aimed at the pair (55 mm, fixed), s54 aims at the
  drummer. Pulses are blacked out in the abduction (bar 93 -> drop) for the new heroes and both performers, and for
  the drummer until he is back (`look.pulse_blackouts`), so E5's rings are r7b's exactly.
- Framing check (`framing.py build/gv3/cast-ufo.json`): identical to r7b except s04 (no tracked subject now) and
  s54 (drummer, 100% in frame, 0.31-0.33 of the frame).
- Stage light: evaluated as a variant only (two soft 220 cd spots 9 m over the performers, no shadows); decide from
  the A/B stills.

## Verification batch 1 (2026-09-28 21:06-21:16): what it showed, and what was done

Outputs in `~/Desktop/av-gen-review/20-gv3-art-pass/verify/` (11 stills, 5 clips, 2 r7b clips; all rc 0).
- **Falls:** edges and junction right (ADR-980 holds), but still a flat textured panel ("slab"): the part above the
  camera's height shaded as if from under the water, depth read straight down, tears' lattice as bricks. Fixed in
  the shader: **ADR-985** (below).
- **UFO warp:** at 0.4 invisible round a stationary saucer at 80 m -> strength 0.8, livelier curl (`cast.py`). The
  wake refracted the climbing saucer into a colour-fringed crescent (s59, 182.4 s) -> wake removed.
- **E5:** the drummer read as a pale ghost in the beam -> the horse's gold lift Glow moved to him (`musicians.py`
  LIFT_GLOW, `ufo.plan.json` drummer-glow/rim cues), gain 0.5 (the cues clipped 25.7% of the frame at full).
- **Musicians:** rings too strong -> own kick pump 1.5 (`look.PERFORMER_PULSE_KICK`); performers' rings blacked
  out in the cold open (`look.pulse_blackouts`); s01's drift passed 4 m from the kit -> moved 6 m east (12 m); s04
  hid them behind ferns -> 40 mm from 5 m up (`shots.py`).
- **Stage light:** the variant's cones were written in radians; the loader reads DEGREES, so no light reached the
  performers. Fixed in `artvariant.py` (inner 14, outer 29); A/B again in batch 2.
- Committed as `7cb99c07` (the generator's output, byte-compared after a fresh make + ufo).

## Falls: ADR-985, water on a steep course is a cascade (item 1, second half)

- `shaders/water.wgsl`: a surface steeper than 12 degrees (fully past 30) is shaded about its own plane: face up from
  the derivatives, eye's side from the plane, depth across the sheet, ripples turned onto the slope, tears faded.
  New `WaterSettings::cascade` (control `nodes/<terrain>/water/cascade`): whitewater streaks combed along the flow
  (12 samples, 0.2 m apart, 0.45 m grain, 3x the water's speed, bounded advection, fading to the mean when too fine).
  All in `cascade (ADR-985)` marker blocks. GV3: `look.WATER["cascade"] = 0.35`.
- Probe: the valley's water is 7.8 deg at its steepest (so it never takes the path); the falls 607 m2 > 30 deg.
- Tests `[cascade]` (new file `test_water_cascade_gpu.cpp`, 5 cases, 152 assertions) pass: flat water byte-identical
  to the stripped shader (8/8, control differs); QA river changed only on 3 steep bank pixels (a flag arm proves
  it); the eye's-height seam log ratio 0.007 vs 1.023; tears on a 35-deg slope identical vs 0.0041; streaks +0.50
  luminance, 3.3x gradient across vs along. All `[water]` GPU tests pass (21 cases, 675 assertions).
- Batch 2 stills `still-water-a/b`: the falls read as a streaked cascade into the river; the lattice is gone.

## Verification batch 2 (2026-09-28 21:40-21:56): what it showed

Outputs in `~/Desktop/av-gen-review/20-gv3-art-pass/verify2/` (10 stills, 5 clips; all rc 0).
- **Falls (ADR-985):** `still-water-a/b` read as a streaked cascade into the river; from the valley's wides
  (`still-falls-s20`, 300 m) a small pale ribbon between the hills, no more prominent than before.
- **Stage light (addendum §8), with the cones in degrees:** a real pool (the difference image shows it round the
  drummer and his kit), but in the frame it hardly reads: the ground there is already lit by the ferns' glow and
  the rings. In s04, the shot that sees them in the film, it changes 13k pixels by a mean of 0.38/765. It does not
  genuinely improve the composition, and anything brighter would be the conventional light the addendum warns
  flattens the look. **Decision: omitted.** A/B stills kept: `still-{drums,keys,s04}{,-stage}`.
- **E5 (`clip-e5-s51-s60`):** the drummer rises out of the beam flailing, reads as a figure with limbs (s54,
  175.0 s), small in the beam in the wide (s55), gone at the drop; no crescent on the departure (the wake is out).
  The gold lift light barely shows inside the beam's own light (reads mint-white); readable, so left.
- **s01:** the drift passes the pair at 12 m; the cold open's teal wash is r7b's (the elder's heartbeat).
- **s04:** the elder's hero ring swept through the pair on every bar and washed them teal-white; their rainbow
  never read. Fixed in data: a shot whose subject is the musicians is theirs (`look.hero_pulse_plan`: the
  mushrooms give up their rings in it). To verify in batch 3.
- **Aurora (s72, the drop's last wide, 214.62-222.0, top fifth, 60 fps):** art pass: mean luma 0.1384,
  frame-to-frame max 2.88%, p99 2.26%, median 0.430%. r7b (same engine): mean 0.1117, max 2.85%, p99 2.26%,
  median 0.414%. So no flicker added (p99 identical); the sky is 24% brighter on average with the bass swell.
  (Batch 1, the arrival wide s23-s24: max 1.22% vs 1.33%, p99 1.20% vs 1.24%, mean +15%. The drop's s58 was
  confounded by the saucer: 54.98% vs 43.88%, at the white flash of 180.967 s, the same in both.)

## Frame cost (matched A/B under the GPU lock, 2026-09-28 21:57-22:01)

`tools/perf_sweep.py points ... --arm cur=build/release/src/avgen --size 1280x720 --frames 42 --fps 30 --reps 3`
(42 runs, interleaved; raw `scratchpad/art/perf-ab.json`, table `perf-ab.md`). Medians of each run's p50, ms:

| point | variant | wall | GPU | engine update | draws | tris |
|---|---|---:|---:|---:|---:|---:|
| s08 flyby @27.5 (craft moving) | full / no warp | 29.7 / 30.4 | 20.2 / 20.3 | 4.1 / 4.7 | 328 / 328 | 570k / 570k |
| s49 @164 (craft hovering) | full / no warp | 23.2 / 23.2 | 13.6 / 13.7 | 4.0 / 4.1 | 292 / 292 | 448k / 448k |
| s04 @16 (the musicians' shot) | full / no musicians | 35.1 / 33.8 | 26.0 / 25.0 | 3.7 / 3.7 | 266 / 232 | 576k / 517k |
| s04 @16 | no aura / stage light | 34.5 / 35.3 | 25.3 / 25.8 | 3.7 / 3.7 | 266 / 266 | 576k / 576k |
| s01 @3 (cold open) | full / no musicians | 26.8 / 26.4 | 17.6 / 17.4 | 3.8 / 3.7 | 312 / 278 | 580k / 522k |
| s24 @78.9 (wide) | full / r7b's project | 32.9 / 30.8 | 23.0 / 23.1 | 3.6 / 2.9 | 350 / 306 | 667k / 595k |
| s66 @194.7 (worst wide) | full / r7b's project | 32.4 / 32.2 | 22.9 / 24.1 | 3.5 / 2.9 | 370 / 326 | 697k / 627k |

- **UFO warp:** nothing measurable (spread 2-4%).
- **Musicians:** +1.3 ms wall, +1.0 ms GPU in their own close shot (34 draws, 58k tris); +0.4 / +0.2 ms in s01.
  Their aura (rings + suit photophores) is +0.6 ms wall, +0.7 ms GPU of that.
- **Stage light:** nothing measurable (and omitted on the look).
- **The whole pass's data** against r7b's project on this engine: +2.1 ms wall at s24 (CPU: engine update +0.7,
  renderer CPU +0.9; the GPU the same), +0.2 ms at s66; +44 draws, +70k tris.
- **The engine's changes** (ADR-980-985), cross-run against the QA pass's clean baseline (gate951, r7b, same seconds
  and settings, `docs/qa-pass/perf.md`): s24 30.8 ms both (GPU 22.8 -> 23.1), s66 32.2 ms both (GPU 23.5 -> 24.1);
  +10-13k tris from the water LOD floor. Not a matched A/B: read it as "within half a millisecond".

## Verification batch 3 (2026-09-28 22:13): s04 and the warp

Outputs in `verify3/` (8 stills, all rc 0).
- **s04** (16.0, 17.3): both performers read -- the drummer violet with his kit, the keyboardist white with his
  photophores under the cap -- with their own rainbow rings (teal, violet, white bands) round them. Fixed.
- **Warp A/B** (full vs `_art-nowarp`, pixels changing by more than 12/765): the flyby at 27.9 s 62 px (a black
  sky: nothing to bend); the hover at 164 s 1,142 px (subtle, round the craft); the departure at the drop, 179.5 s,
  89,571 px -- the hills and the aurora behind the leaving saucer bend over a wide field (the elder in front is
  untouched: DF bends only what is behind the owner). It reads where there is something behind the craft; over
  black sky it cannot.

## The first final and the Critic (ONCE), 2026-09-28 22:16-22:56

- **First final:** `GV3-art-pass-final.mp4` at `f2a01447`, 1920x1080, supersample 2, 60 fps, 0-225.5 s, h264 q90
  with the song (AAC 48 kHz stereo); 13,530 frames, 927.6 MB; rendered in 2,076 s (34.6 min), rc 0, no errors.
- **The Critic** (`scratchpad/art/critic.sh`: the adapter with the whole-film cast trace, then `critic submit
  --mode preview --strict`): job `job_1a0eb13123079bf2e`, complete (not partial), 287.7 s. 152 issues (0
  critical, 11 high, 90 medium, 51 low), 63 strengths. Dimensions: composition 0.711, cinematography 0.853,
  lighting 0.437, color 0.927, depth 1.0, visual hierarchy 0.736, motion 0.881, temporal coherence 0.966, effects
  0.951, environment 0.909, character staging 0.938, visual coherence 0.888, pacing 1.0, musical sync 0.904,
  creative intent 0.927, technical quality 0.327.
- **Against the revision's last whole-film preview** (`job_1a0e6cc7f6ba208ae`, r7; `critic compare`, a diff of the
  two reports, not another evaluation): 45 findings resolved (the horse's E5 occlusions among them), 40 new, 99
  persisting. Improved: motion +11.6, visual hierarchy +6.3, cinematography +6.1, composition +3.6, character
  staging +2.1. Degraded: technical quality -10.5, lighting -6.9, musical sync -4.7, color -3.0, creative
  intent -3.0, effects -2.4, environment -2.1.
- **What the new findings were, traced:** the degraded lighting and technical quality are clipping and "pale and
  washed out" in shots that were 20-40% brighter than r7b in mean luma (s12 0.16 -> 0.46, s36 0.21 -> 0.38, s65
  0.22 -> 0.48). Stills A/B'd against r7b's project on this engine (`verify4/`) found two causes, both mine:
  (1) the hero rings, which ADR-981 lets live their whole 4.5 s at GV3's old timing, ran on at full strength
  through foreground the old cut never let them reach; (2) the opal, whose ring swept the near foreground of s12
  (15 m from the lens) and s36 (35 m): with its pulse disabled s36 and s65 fall to r7b's luma exactly (0.203 vs
  0.201, 0.229 vs 0.222). This is the brief's "small obvious correction necessary to satisfy a requirement"
  (item 2: the pulse's appearance intact, the wave fading as it travels; item 5: composition and hierarchy kept).
- **The correction (data, `look.py`):** `HERO_PULSE_TIMING` 4.5 s / fade 1.2 -> 2.6 s / fade 1.1 (full to 1.5 s, 77%
  when the next ring leaves, gone at 34 m; ADR-981 amended); `NEW_HERO_LENS_CLEARANCE` 40 m: the two new
  mushrooms do not ring in a shot whose camera passes that close (opal keeps 6 rings in s33/34, s48/49, s63-65;
  sail keeps its 25). Checked at seven moments (1280x720 stills, mean luma / clipped): s12 42.2 s r7b 0.174 ->
  0.195; s36 113.3 s 0.201 -> 0.202; s65 193.5 s 0.222 -> 0.269 (the opal's one ring, a band in the mid-ground);
  s17 61.0 s 0.348 -> 0.348; s05 20.5 s 0.356 -> 0.357; s14 50.5 s 0.361 -> 0.254; s04 17.3 s 0.268.
- **Not acted on (not necessary for a requirement, or not small):** "nearly the same framing" s04/s14, s04/s30,
  s05/s01, s26/s05 (variety); s31/s15/s42 an alien or a horse outweighing or hiding the subject (the walkers' paths
  differ now that the new heroes and performers are obstacles; the subjects stay in frame); the persisting
  findings the r7 job already had (clipping in the drop, routes that do not visibly move umbra/veil, camera
  jitter). The Critic runs once; the corrected final is not re-judged by it.

## The final (the owner's morning render), after the correction

- **`~/Desktop/av-gen-review/20-gv3-art-pass/GV3-art-pass-final.mp4`**, rendered at **`6aa3701d`** (clean tree; the
  commit both suites passed on) by `scratchpad/art/finish.sh` under `tools/gpu-lock.sh`: `avgen --headless --project
  examples/world/glowmere-valley-3.json --render <file> --size 1920x1080 --fps 60 --range 0:225.5 --supersample 2`,
  the project's own h264 q90 with the song muxed. 1920x1080, 60 fps, 13,530 frames, 225.5 s, AAC 48 kHz stereo,
  926.0 MB (32.9 Mb/s). Rendered 2026-09-29 01:55:14-02:29:31, 2,057 s (34.3 min), rc 0, no error lines (the log's
  warnings are r7b's own: lodCount, the nav-grid pieces, the farm animals' foot-slip notes, the dead-route false
  positives at load). 1080p because it fits easily: 35 minutes a pass.
- It replaced the same render from `c20c182c` (23:13-23:48); the suite-driven fixes between the two change the
  film's measured look by at most 0.001 of a shot's mean luma (`shotluma-final.json` vs `shotluma-final3.json`).
- The Critic's input is kept beside it as `GV3-art-pass-critic-input-f2a01447.mp4` (same settings, 2,076 s).
- **The correction at film scale** (`scratchpad/art/shotluma.py`: 6 frames a shot, Rec. 709 luma of the decoded
  frames and the share with a channel >= 250; on the Critic's input it tracks the Critic's own per-shot mean luma
  at r = 0.995): film mean luma r7 (the Critic's r7 job) 0.231, first final 0.272, final 0.244; mean clipped share
  3.74%, 2.96%, 2.32%; shots more than 20% brighter than r7: 23 -> 7 (s65 +31%, the opal's ring, and the
  performers' shots s02/s04/s05/s10); shots clipping more than 3%: 36, 27, 21.
- The ring transition at a retrigger (s16, 57.55-58.80 s, `scratchpad/art/frames2/ring-transition.png`): the old
  ring's light is still on the ferns as the new one leaves the spire, then fades; nothing is cut.

## The suites, and what they found (2026-09-29 00:08-01:45)

- **First run** (`scratchpad/art/suites.sh` at `c20c182c`): GPU rc 42 (554 cases, 2 failed), CPU rc 42 (3,862 cases,
  3 failed + the expected `[!shouldfail]`). Each traced and fixed:
  1. GPU `a non-finite water setting is refused rather than floored`: the hand-kept poison list is counted
     against `sizeof(WaterSettings)`, and `cascade` had none. Added (`kScalarFloats` 32).
  2. GPU `the water surface and its bed keep their pixels under a small camera move` (water6_2): 1 pixel
     strobing. NOT the cascade (it fails the same with the ADR-985 blocks stripped); ADR-980's fit on the QA
     river's gentle banks (a diagnostic build with the old dry-corner answer passes). ADR-980 amended: the old
     answer on water that does not descend, the fit only where the 5x5 window spans 0.6-1.5 m of descent. Re-probed:
     the falls still fixed (none over 30 cm, worst 0.13 m).
  3. CPU `removing a terrain node unregisters every water parameter it registered`: `water/cascade` was not on
     the removal list. Added.
  4. CPU `Rook's feet stay on the ground through the turn at 3 s` (ADR-829's guard, GV2): ADR-982 handed Rook his
     walk mid-pivot (0.128 per posed frame). ADR-982 amended: from a turn only once it is over. GV3 re-measured:
     median 0.018 m, p90 0.023 m per foot.
  5. CPU `with the key absent, Glowmere behaves exactly as it did before ADR-623`: GV2's trace digest moved with
     ADR-982 (a diagnostic build with the rule off gives the old digest exactly); re-pinned with the reason, as
     three ADRs did before.
- Meanwhile the shader's cascade rule became "20 degrees down its own flow" (bank slivers tilt across the flow;
  ADR-985 amended), after the stripped-shader experiment above showed the slivers were not the strobing pixel
  but were still not cascades.
- Targeted re-runs pass: CPU `[adr982],[gait],[adr980],[water],[stride]` 81 cases; GPU `[cascade],[water],
  [forensics]` 93 cases.
- **Second full run** (`scratchpad/art/finish.sh`, under the lock, at `6aa3701d`, clean tree):
  `build/release/tests/avgen_render_tests` **rc 0** (554 cases: 553 passed, 1 skipped -- the NDI runtime is not
  installed; 617,466 assertions; 1,015 s). `build/release/tests/avgen_tests` **rc 0** (3,862 cases: 3,842 passed,
  19 skipped, 1 failed as expected -- the one FAILED line is the `[!shouldfail]` slope lean,
  `test_character_lab_slopes.cpp:187`; 9,143,399 assertions; 2,149 s).

## Revision round 1 (owner, 2026-09-29): tracking

The owner's five items and the Critic addendum, verbatim, with the coordinator's notes: `00-brief.md`, last section.
ADRs 986-989 remain for it. The order: stems (item 1) and the musicians' class (item 5, proved on its own commit),
then foot locking (2), the warp rim (3), the rainbow lift (4), one verification batch, frame costs, then the Critic
passes (up to about three) on the bland shots, both suites, the r1 render.

1. **Stems and caps -- done in code (ADR-986), to see in the verification batch.** Measured with geometry
   (`avgen_tests "probe: hero stem-to-cap gaps"`): ALL TWELVE heroes had a gap between the stem's top and the
   underside's opening, 0.06-0.52 m at GV3's scale (sail 0.52, ridge 0.39, elder 0.27 even with GV2's hand-raised
   stem, bloom 0.23, scree 0.22, lantern 0.18, umbra 0.18, cairn 0.11, opal 0.09, ember 0.08, veil 0.06, spire 0.06).
   Cause: a curved stem's top ring is perpendicular to its tangent (up to 43 deg of lean) while the opening lies in
   the cap's tilted plane; the alignment check compared centres only. Not the wind (sub-millimetre at the heroes'
   motion values) and not per-instance variation (one `single` instance per part, all four at one transform but
   the elder's stem). Fix in the generator: the stem's last three rings turn to the cap's axis onto the opening's
   centre, and one more ring carries it inside the cap. After: 0.6-4 cm (0.04-0.08 stem radii), none through the
   top; population median 0.45 -> 0.056 radii. GV3's elder stem joined to its cap (`heroes.join_parts`, after
   seating: the cap keeps its height). New test `[adr986]`. The search no longer reproduces the record (its
   population changed), so its test writes the canonical record only with `MUSHROOM_WRITE_RECORD=1`.

3. **UFO warp rim -- done (data).** A/B stills in `r1/batch/rim-*`: at the field's edge, 0.08 of the radius wide, the
   rim is a line -- an ellipse eight saucers across over the black sky (s08, 27.9 s), a big arc over the elder at
   E5's hover (164 s) and an arc across the lit valley after the drop (179.5 s), even at 0.12. 0.4 wide it is a soft
   halo about the craft. Chosen: rimIntensity 0.08, rimWidth 0.4, keyed on only in s08 (the flyby) and s47 (E5's
   approach), where the saucer is small against the black sky, 0 everywhere else (`cast.warp_rim`, a step track at
   the cuts, installed after the cut). The beam cues would quiet a rim too (`ufo.warp_cues`: the rim scales with the
   envelope, not the strength), though none is on under a beam.
4. **Rainbow lift -- done (data); the rainbow chosen.** A Color Cycling on the drummer's emission (`drummer-rainbow`,
   speed 0.34 = his pulse's rainbow speed, 1.5 bands up his body), range 0 at rest and raised on the lift with the
   light by a plan cue (same ramp and hold). A/B at 175.4/175.7 s (`r1/batch/lift-*`): gold; rainbow paled to the
   pulse's 0.55 saturation (read as tinted white in a <1 s shot); rainbow at the gold's saturation (chosen:
   unmistakably his rainbow, and a body against the cyan beam); 2.5 bands (striped him).

5. **Musician class -- done (data), proved unchanged.** What the round-0 report got wrong: the musicians were never
   tagged "animal" (their tags were `musician`, `astronaut`); the Critic called them animals because its adapter
   (`creative-critic/adapters/avgen/avgen_adapter.py`, `entity_kind`) defaults any body with no tag it knows to
   "animal". And the abduction never piggybacked on a tag: E5 names its subject (`ufo.plan.json`
   `"animals": ["drummer"]`), and a named subject is bound by name (`setpiece.cpp gather/abduction`: `q.name`, no
   tag). The decision: the musicians' class is `performer`, with `character` beside it (the adapter's word for a
   person; already every body's perception bit in the engine's tag mask), appended after their old tags so no
   other tag's bit moves (`musicians.py`). No engine change; nothing in the engine asks for either tag.
   **Proof, before (`9c86d2b1`) and after, same code:** `avgen_cast_trace` over the whole film (23 entities at 20 Hz,
   5 set pieces): identical; E5 lifted 172.783 s, gone 177.700, back 183.750 at (-1.7, 5.162, 70.9) against
   (-1.7, 5.163, 70.9) at the lift (1 mm); the drummer's whole pose (54 joints in the world, every frame 168-200 s,
   1,921 frames, `avgen_foot_probe --dump drummer:168:200 --pose-out`, new): bit-identical; seven rendered frames
   (172.9, 173.5, 175.2, 176.2, 177.3, 183.8, 198.8 s, 1280x720): byte-identical PNGs. Evidence in
   `scratchpad/r1/class/`.

2. **Foot locking for four-legged rigs -- done in code (ADR-987).** A `Stance` foot lock (`footLockMode: "stance"`):
   from the first posed frame of a contact span the hoof stays at the point in the world where the clip had it,
   handed back to the clip over the span's last 30%; the point is remembered and trusted only by the next posed
   frame of the same stance (a seek, cull or new stance holds from the clip's foot). A `sweep` contact mode (the
   backward stroke). `AnimationPlayer::setSpeed(0)` now holds the clip where it is (it snapped every stop of an
   `idleRate`-0 body to frame zero: hooves 0.37-1.37 m in one frame). GV3's farm animals: locked, walk speed their
   mean stroke speed (horse 2.194, cow 3.286, bull 3.345 m/s, rateMax 1.6), posed every frame (`updateHz` 0; at
   30 Hz a held hoof is drawn a frame's travel ahead and back). Whole film, the ten animals not taken, r0 film vs r1
   film: held part of each stance 2.048 -> 0.032 m per metre walked; release part 0.629 -> 0.636; standing 16.95 ->
   2.66 mm/frame (worst one-frame move 1.946 -> 0.206 m); starts 0.086 -> 0.045 m per metre of body; horse-2 straight
   13.95 -> 0.18, turning 21.59 -> 0.26 mm/frame. Aliens identical; paths identical; GV2 digest 7e0ba810e539e375
   unchanged. Cost: +0.48 ms/frame CPU (probe timing, whole film). The first, derived design (touchdown point moved
   by authored speed x elapsed, turned by turn rate) threw a bull's hoof 1.16 m at a turn onset: rejected.
   Instruments: `avgen_foot_probe` (stance, standing, jumps, `--feet-out`), `scratchpad/r1/probe_copy3.py`
   (`--head`, `--no-lock`, `--no-swing`, `--keep-hz`), `scratchpad/r1/foot/table.py`, `split.py`.
6. **Alien foot height (from the coordinator; round 0's decision 2) -- done in code (ADR-988).** Cause: the ground
   foot layer plants every frame at the rest height with the sole laid flat (not the clips, not the scale).
   `keepSwing` keeps the clip's foot in the air carried onto the ground (height above standing, measured across the
   plane and in the body as the reach solve moved it; the sole turned by the plane's tilt), with a `toe` floor (the
   foot taken back toward laid only as far as keeps the toe at its standing height). GV3's aliens: ankle median
   step peak 0.267 -> 0.385-0.398 m (p90 0.49), toes 0.04 -> 0.09-0.10 (p90 0.30-0.32); frames with a foot > 2 cm
   under the terrain 3,585 -> 5; on the flat the drawn foot is the clip's (walk: ankle 0.24 m, toes 0.26; run 0.65,
   0.62 -- they never run in the film). Planted-ankle slide on the same frames +0.3-0.9%; ankle path median 2 mm.
   Opt-in: GV2 digest unchanged. Drummer's pose 168-200 s bit-identical (every rig posed every frame, as the class
   proof). Instruments: `[.probe][alien][swing]`, the probe's swing clearance.
7. **Alien head turns (from the coordinator) -- done in code (ADR-989).** Cause: the look layer aims at the attended
   subject every frame (attention moves in a step), and its `maxYaw` clamp flips across the body's back. An
   entity `gaze`: a critically damped spring on the aim direction in the world (settle 0.35 s, 200 deg/s, eyes 2.6
   m, within 70 deg of the facing, sided near the back), in the entity tier (checkpointed, both publish paths: a seek
   lands it where a play does, test). Aliens' head in the world, posed steps: p99 671 -> 211 deg/s, worst 4,587 ->
   1,653 (an authored startle), steps over 400 deg/s 405 -> 28 (15 startle, 4 a walking turn, 9 the film's first
   0.17 s); accel p99 61,800 -> 3,050 deg/s^2. Feet and paths unchanged by it. `scratchpad/r1/head/headstats.py`.

### Critic pass 1 (whole film from `1fa60913`, rendered from 0 at the final's settings)

- Render `scratchpad/r1/critic/p1.mp4` (render.sh: 1920x1080, 60 fps, supersample 2, 0-225.5, rc 0, 2,112 s).
  Baseline for the round: the Critic re-run on round 0's final (`GV3-art-pass-final.mp4`), job
  `job_1a0edfe96ef980150`: 146 issues (10 high, 82 medium, 54 low), 70 strengths; composition 0.717,
  cinematography 0.833, lighting 0.501, color 0.951, visual hierarchy 0.747, motion 0.849, temporal 0.936,
  technical 0.407.
- Pass 1, job `job_1a0edf926f31b1a6b`: 143 issues (10 high, 81 medium, 52 low), 68 strengths; motion 0.873 (+0.024),
  cinematography 0.845 (+0.012), the rest unchanged to the third decimal; 3 wobble findings resolved, none new
  (`critic compare`, `scratchpad/r1/critic/compare-p0-p1.json`). Film: mean luma 0.2438, mean clipped 2.32% (r0's
  final: the same), `shotluma-p1.json`.
- **The bland shots** (`scratchpad/r1/critic/bland.py`: per shot, camera and image motion, novelty, luma, contrast,
  subject coverage, findings): **s50** (166.62-170.31, "Ember on the west bank, the saucer settling over the elder
  beyond": static eye, novelty 0.087, luma 0.074, contrast 0.120, its subject 1% of the frame) and **s29**
  (88.16-89.08, "the ember mushroom on the east terrace, lit": the film's lowest novelty 0.048 and contrast 0.042,
  luma 0.082, image motion 0.013; no downbeat inside it, so its hero never pulses in it).
- NOTE: the adapter was given `build/gv3/cast-ufo.json`, the constant-delta trace: its character data describes the
  step world, not the film (the audit below). From pass 2 the adapter gets the render-clock trace.

### Found: a render is not the film its seeks and traces describe (the render's frame delta)

Investigating s50 (in the film a dark far field; in every seek still and in the cast trace, the authored
composition: Ember lit in the lower left, the saucer upper right, the elder between) found an engine determinism
defect. Not fixed this round (the coordinator's decision: option C); for the owner to decide between A and B.

- **The measurement.** The same project and binary, the whole film on the CPU (`avgen_foot_probe`, then
  `avgen_cast_trace`), stepped five ways: constant `dt = 1/60` (the trace tools' way); the instant spelled `i/60`
  with constant dt; `setViewport(1920, 1080)` before each update; the render job's warm-up frame and second seek;
  `FixedStepClock::tick` (the render's clock). The first three and the warm-up are bit-identical to the constant
  step. The clock is not, and neither is a constant-step run given `dt = i/60 - (i-1)/60`: **the delta alone** does
  it. `FixedStepClock::tick` hands `update` `next - current_.renderTime`, the difference of two rounded instants,
  which is not 1/60 in its last bits. `EntityWorld::seekExact` (ADR-700) steps exactly `1/rate`.
- **The divergence** (whole film, render-clock trace against the constant-step trace, `scratchpad/r1/audit/`):
  the first body to part is Tide, 0.7 mm at 6.28 s (Vane 7.25 s, Rook 11.9 s, Sage 14.0 s, Ember 62.6 s). The
  aliens' largest distance: 0.03 m at 7 s, 0.1 m at 30 s, 15 m at 45 s, 21 m at 60 s, 68 m at 90 s, 80 m at 150 s,
  **114 m at 168.5 s**. Farm animals: millimetres until ~120 s, then up to 12.5 m. The crafts, the beams, the
  performers and every set piece's beats are identical to the frame in both (director-driven, nothing decides).
- **Confirmed against the renders.** The pass-1 film's s50 frames put Rook, Ember and Vane exactly where the render-
  clock trace projects them (x 657/743/859 px against 655/740/860 at 168.5 s), by the lantern 80 m off; a range
  render of 168.5 s (which seeks) shows the constant-step trace's world.
- **Which paths use which delta.** The render's (`next - previous`): `RenderJob` (every video and still render),
  `FrameRange` (frame sequences), the headless benchmark -- and a range render plays with it from its start, having
  SEEKED to its start (so a range render is a third film: the step world at its first frame, drifting after).
  Constant `1/fps`: the seek replay (`EntityWorld::seekExact`, what a scrub and a range render's start land on),
  `avgen_cast_trace`, `avgen_behavior_trace`, `avgen_character_quality`, `avgen_overlay_shot`, `avgen_foot_probe`,
  and the test harness (`tests/support/project_round_trip.hpp stepFrames`) -- so every "a seek lands where a play
  does" test holds, and no test runs the render's clock against a seek. (Live play integrates the wall clock and
  is not reproducible by design; this is the offline path.)
- **Why it matters.** ADR-091's two tiers and ADR-700's "a seek lands where a play does" hold for the constant
  step only; the deliverable does not use it. Every tool that says where a character will be in the film (the cast
  trace the generator composes on, `framing.py`, `ufo-beats.json`'s watches, the Critic adapter's cast, the editor's
  scrub) describes the step world; a chaotic body amplifies one ulp into 100 m within three minutes.
- **Options (the owner's decision):**
  - **A. `FixedStepClock::tick` reports `1/fps`** after its first frame (one line). Every render then agrees with
    seeks, range renders, traces and tests. Every existing film with deciding characters changes from its first
    divergence: GV3's aliens from ~6 s, its animals from ~120 s -- toward the paths the cut was composed on (s50 as
    authored), but the film the owner reviewed is re-dealt; GV2 the same kind of change. Nothing else changes.
  - **B. The replay and the tools take the render's delta** (`k/fps - (k-1)/fps`). Every film stays as rendered;
    seeks, range renders and traces then describe it. Wider: `seekExact`, the harness's `stepFrames` and every tool
    loop change together (a test stepping the old constant would part from the replay), and checkpoints are
    re-recorded. Invisible in any film.
  - Either way: a test that renders (or ticks `FixedStepClock`) against a seek to the same instant, which is the
    test that was missing.
- **This round (option C):** tools only, defaults unchanged. `avgen_cast_trace --render-clock WxH` steps as
  `RenderJob` does and writes `"clock": "render"` (the default writes `"step"`); the probe has the same option and
  its parts. Used for the audit below, the s50 reframe and the Critic's cast from pass 2.
- **The owner's decision, relayed by the coordinator (2026-09-29, 15:1x), in their words:** "Ah just go with
  option A. Don't worry about before and after comparisons, let just move towards getting a render ready for me to
  review". (Recorded here, not in `00-brief.md`: the permission system refused that kind of write for items 6
  and 7.) The coordinator assigned ADR block 990-999 for it (ADR-990). **Not implemented:** the edit to
  `src/core/time.cpp` (`FixedStepClock::tick`: `deltaTime = 1.0 / fps_`) was DENIED by the permission system
  ("Modify Shared Resources"); not worked around, reported to the coordinator. It needs the owner's own approval.
  **Then (15:10):** the owner confirmed it to the coordinator directly, and the coordinator made the change on this
  branch: `bce61fe9` (ADR-990, `[adr990]` tests, the README row), and put items 6, 7 and the option-A words into
  `00-brief.md` (`a073da7b`). My ADRs now go from 991.
- **After ADR-990** (reconfigured, full rebuild): the default cast trace and a `--render-clock` trace of the same
  project agree to the bit (every body, the camera), and the new film is exactly the old step world. So every
  step-world measurement (the ADR tables of items 2, 6 and 7, `framing.py`, the generator's cast-based framing)
  describes the film as rendered, and seeks and range renders are the film again. s50 went back to its original
  rig (`git checkout 1fa60913 -- tools/gv3/shots.py`; the scene is byte-identical to `1fa60913`'s): in the new film
  Ember walks toward the lens in her own lit ring, 0.15-0.25 of the frame, the elder and the saucer beyond; Sage
  passes out at the left edge in the shot's first 1.5 s (a large partial figure, 6-7 m off). Seek stills
  `scratchpad/r1/gen3/s50/sheet.png`.
- **Searched for other deltas taken as differences of instants** (the coordinator's ask; none changed):
  `MusicalEventDetector::update` (analysis-frame instants; the replay feeds it the same frames), `signals/source.cpp`
  (a window start, `renderTime - deltaTime`), the particle spawn count and the procedural `prevInfo` (render only),
  the live transport (wall clock or audio position; not reproducible by design). `RenderJob`, `FrameRange` and the
  headless benchmark all take their delta from `FixedStepClock`. GV2's fingerprint traces a constant 1/60: A would
  not move it.
- **Also found (one observation, not investigated to a cause):** the pass-2 full render (`662bd4b6`) differs from a
  0-8.25 s render of the same project and binary from frame 481 (8.017 s) on, across the frame (max 90-136 levels at
  scattered pixels, means within 0.5%). Two 0-12 s renders are identical to each other, and the pass-1 film matches
  the short renders until their last three frames (a range's end is drained differently). So one full render left an
  otherwise repeatable path. Evidence: `scratchpad/r1/det/`, `scratchpad/r1/critic/p{1,2}-all.md5`.

### Audit: what rounds 0 and 1 derived from the step world (the coordinator's ask)

Re-checked with the render-clock trace and probe. Sound unless said otherwise.
- **Unaffected (nothing that decides is involved):** water and falls (ADR-980, 985), hero pulses (ADR-981; the
  markers and the pulse plan use keyed camera positions, never a trace), the aurora, the warp and its rim (ADR-983,
  item 3: the saucer's track is identical in both worlds, so the rim stills at 27.9/164/179.5 s show the film's
  saucer), the stems (item 1), the rainbow lift (item 4: the drummer and the saucer identical), E5's timing (lifted
  172.783, gone 177.700, back 183.733: every set piece's beats identical), the hero sites and the new heroes' lens
  clearance and the musicians' placement (`hero_sites.py`, `place_search.py`: keyed eyes, never a trace; the closest
  camera to either performer, 12.1 m, and to opal/sail, 15.1/26.3 m, are keyed shots, the same in both worlds),
  and the vegetation clearings (under the performers' footprints and round the new heroes: static placements,
  checked with `avgen_scatter_probe`, nothing that moves).
- **Round 1's A/B clips** (`r1/clips/`, range renders): each pair seeks into the same world, so each is a valid
  A/B of its item, but a range render starts in the step world: s36's herd (111 s) is the film's (the animals
  part after ~120 s), s65's horses (192 s) and the aliens' clips show the step world's positions, not the film's.
- **Item 5 (the class):** renders against renders, and the performers' paths are identical in both worlds: sound.
- **Measured in the step world, re-taken in the render's** (`scratchpad/r1/audit/m-*`; r0 = `19c215d3`'s project
  with the old player emulated, which reproduces round 0's step-world numbers exactly):
  - ADR-982 (round 0, alien starts): standing slide per foot median 0.019 m, p90 0.022 (step world 0.018/0.023).
  - Item 2 (ADR-987), r0 -> r1: held part 2.051 -> 0.032 m/m; release 0.630 -> 0.638; standing 17.06 -> 2.90
    mm/frame (worst 1.946 -> 0.214 m); starts 0.083 -> 0.045 m per metre of body. (Step world: 2.048 -> 0.032,
    0.629 -> 0.636, 16.95 -> 2.66, 0.086 -> 0.045.) Horse-2's first minute is identical in both worlds.
  - Item 6 (ADR-988), r0 -> r1: ankle step peak median 0.265-0.267 -> 0.387-0.393 m (p90 0.48-0.49), toes
    0.038-0.042 -> 0.093-0.098 (p90 0.28-0.33), frames with a foot >2 cm under 3,117 -> 9, lowest -0.127 -> -0.051.
  - Item 7 (ADR-989), r0 -> r1: head speed p50 18.1 -> 21.2, p99 691 -> 212, worst 4,590 -> 1,653 deg/s; steps over
    400 deg/s 373 -> 25; accel p99 50,400 -> 3,060 deg/s^2.
- **Affected:**
  - **s50:** its fixed eye aims live at Ember, placed from the step trace; in the film she is 80 m off. The bland
    shot. Reframed against the render-clock trace (below).
  - **`framing.py`** (the generator's check and round 0's run): against the render trace only s50's verdict changes
    (Ember 0.20 -> 0.06 of the frame, the saucer and the elder out of it). Follow shots keep their subject to the
    pixel in both worlds; their backgrounds differ.
  - **s35** ("E4: Sage watches the cow rise"): in the step world Sage is never in frame; in the film she stands
    in front of the lens, head and shoulders, watching the cow go up -- it reads as intended, by luck. Left.
  - **The Critic's cast** (round 0's job and pass 1): the video was the film (rendered from 0), the adapter's
    character tracks the step world -- so its per-shot subject coverage and occlusion findings for aliens were
    judged against positions the film does not have (e.g. round 0's "s31/s15/s42 an alien or a horse outweighing
    or hiding the subject" and pass 1's s50 coverage). From pass 2 the adapter gets `--render-clock` traces.
  - **Seek stills of the cast** (verification batches, the s50 debug stills): valid as A/B of one moment (both arms
    seek to the same world) but not the film's moment for aliens; the A/B evidence for items 6/7 was measured over
    the whole film instead, now in both worlds.

### The bland shots, after pass 1

- **s50, reframed on the film as rendered** (`shots.py ember_watches`; searched with `scratchpad/r1/s50/search*.py`
  on the render-clock trace, heights from the engine): in the film Ember walks south at 3 m/s from (-39, -18) to
  (-45, -10), Rook 6 m behind her, the saucer settling from (1, 34, 84) to (-2, 28.5, 71). New rig: 50 mm, eye
  (-69, ground + 3.5, -36), live aim at Ember + (8.1, 5.3, 17.2) ("watch" smoothing, as before); `target` set to
  the aim's mid-shot point so the pulse plan frames the view the film has. Through the traced camera: Ember x
  -0.42 -> -0.22, 0.19-0.20 of the frame's height, Rook to her left (x -0.60 to -0.69, 0.22-0.26), apart; the
  saucer (0.68, 0.56) -> (0.23, 0.46), the elder (0.64, 0.04) -> (0.25, 0.06); a 10-degree pan with the walkers.
  Every body's path identical to the frame (the camera and the pulse markers do not reach the simulation: render-
  clock trace before/after). The pulse plan now rings the elder (in frame) instead of the lantern (no longer in
  frame) on s50's two downbeats; Ember's own ring stays. Label/purpose: "Ember and Rook walk toward the elder as
  the saucer settles over it". Preview from 0 (640x360, `scratchpad/r1/gen2/preview-a.mp4`, s50 frames): luma
  0.17-0.24 (was 0.074), rms contrast 0.21-0.31 (0.12), clipped 0.04-1.2%.
- (Superseded by ADR-990: the reframe above was for the film the old clock rendered; s50's original rig frames the
  film as it now renders, and is back.)
- **s49 (pass 2's pick, the Critic's "near-black even for a dark passage", 3.7 s):** the submerged break dims
  every emission to a quarter and the exposure by 2 EV; the shot is "the saucer, the elder's gills" and the gills
  read dull grey-green. `look.SHOT_EMPHASIS` / `shot_emphasis`: the elder's gills and underside `emissiveBoost`
  0.1 -> 0.8 for exactly s49, stepped at its cuts. Seek stills at 0.1/0.3/0.5/0.8 (`scratchpad/r1/s49/gills-sheet
  .png`; nothing in s49 decides): at 0.8 the gold canopy reads over the dark frame; luma 0.037-0.054 ->
  0.060-0.075, rms contrast 0.058-0.093 -> 0.101-0.133, nothing new clipped (0.18% is the saucer's disc, as before).
- **s29, tried and not kept:** a rule firing a hero's ring on beat 3 in a hero shot with no downbeat (only s29
  qualified) made the ember's ring flood the frame -- the camera is 23 m from it at 50 mm, the ring's 20 m trail
  covers the lower frame: clipped 9.4-14.7% (film mean 2.32%) at the ring's full strength and still 6.8-9.5% at a
  fifth of it (`scratchpad/r1/s29/strength-sheet.png`) -- the same reason round 0 kept the new heroes' rings off
  the near foreground. Reverted; s29 is left for pass 2 to re-rank with the corrected cast data.

### Critic pass 2 (`662bd4b6`, the old clock's film, the adapter given a render-clock cast)

- Render `scratchpad/r1/critic/p2.mp4` from 0 (2,109 s, rc 0). Job `job_1a0ee7558f804aa57`: 140 issues (10 high,
  81 medium, 49 low), 69 strengths. Against pass 1: composition 0.717 -> 0.725, visual hierarchy 0.747 -> 0.756,
  motion 0.873 -> 0.895, environment 0.932 -> 0.937, character staging 0.938 -> 0.940; cinematography 0.845 ->
  0.840, effects 0.951 -> 0.940, visual coherence 0.885 -> 0.877; lighting, technical quality, colour unchanged.
  7 findings resolved, 5 new (s50 "5 effects visible at once", s13/s63 "nearly the same framing", "4 consecutive
  static shots", two wobbles). Film: mean luma 0.2458, clipped 2.329% (r0's final 2.318%, pass 1 2.321%): s50,
  from near-black to lit, clipped 0.84% of its frame.
- Its film is superseded by ADR-990 (the cast now walks the step world's paths). It chose the next bland shot:
  s49, the Critic's only "near-black even for a dark passage" (3.7 s); s08 is the black-sky flyby, s29 is 0.92 s.

## Plan (the rest, in order)

1. ~~Milestone 1 (water, pulses, gait, probes, ADRs 980-982)~~ `cdbcf63f`. ~~Aurora~~, ~~UFO warp~~, ~~heroes~~,
   ~~musicians + E5 (ADR-983, 984)~~, ~~batch 1 + its fixes~~ `7cb99c07`, ~~falls cascade (ADR-985)~~ `84258eeb`,
   ~~batch 2~~, ~~frame-cost A/B~~, ~~s04's rings~~ `f2a01447`, ~~batch 3~~.
2. ~~The first final and the Critic, once~~ (job `job_1a0eb13123079bf2e`); ~~its one correction~~ `c20c182c`,
   checked with stills against r7b (`verify4/`).
3. ~~The owner's final~~ from `c20c182c` (`GV3-art-pass-final.mp4`), measured shot by shot against r7.
4. ~~Both suites~~: failed once (five defects, fixed in `6aa3701d`), then both rc 0 on `6aa3701d`; ~~the final~~
   re-rendered from `6aa3701d`. Done; the coordinator merges.

## Log

- 2026-09-28: read the brief, the prototype's notes and report, the perf summary, the state audit, the generator.
  ADR block 980-989 assigned. Worktree build started.
