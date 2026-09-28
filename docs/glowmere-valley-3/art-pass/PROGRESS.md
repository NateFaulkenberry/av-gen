# GV3 targeted art pass: progress notes

The brief is `00-brief.md` beside this file (the owner's words; it governs). One agent, branch `gv3/art-pass`,
worktree `../av-gen-art`, from qa/coord `b8e391ca`. ADR block **980-989** (coordinator, 2026-09-28; the range row is
in `docs/decisions/README.md`). Don't push, don't merge: the coordinator merges.

## Resume here

- **Status (2026-09-28, start):** reading and planning. Nothing changed in GV3 yet.
- **Build:** `cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
  -DCPM_SOURCE_CACHE=/Users/natefaulkenberry/Documents/GitHub/av-gen/.cache/cpm`, then `cmake --build build/release`.
  Assets are linked (`../av-gen/tools/link-worktree-assets.sh ../av-gen-art`, 1,803 links).
- **The project is generated.** `python3 tools/make_glowmere_valley_3.py && python3 tools/gv3/ufo.py --no-trace`
  reproduces the committed r7b (`examples/world/glowmere-valley-3.{json,scene.json}`, README "The committed project is
  r7b"). Every change in this pass goes into `tools/gv3/` (data) or the engine, then the project is regenerated, so
  the project stays the generator's output. Never round-trip the project through Python `json` by hand, and never
  clean it with a headless load-and-save.

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

## Plan (the rest, in order)

1. Commit milestone 1 (water, pulses, gait, the two probes, ADRs 980-982).
2. Aurora bass pulse + resolve the aurora's duplicate block values (`look.py`; data only).
3. UFO warp: Space Warp ("UFO Warp" style) + a wake on `visitor` and `scout`, speed-routed; a hidden owner warps
   nothing (engine: `NodeView` visibility, ADR-983); suppressed during every beam/lift (timeline keys from the plan's
   moments).
4. Hero mushrooms: the search's two next farthest-point picks (`test_mushroom_search.cpp` kWinners 10 -> 12; the
   record gains two, the first ten byte-identical); placed by GV3's generator with GV2's hero recipe (four parts,
   spores, a `heroes` entry, a Hero Pulse, their reactivity lanes); shots adjusted where they belong.
5. Musicians: GLBs rebuilt from `~/Desktop/musician_assets` (+ a procedural "Flail" clip on the drummer, authored in
   the build script); placed by the river at E5's lift point (the drummer where the horse was lifted, (-0.7, 73.9)),
   facing each other, x1.94 per group; entities + heroes + a rainbow aura (Ground Pulse "Rainbow" + a suit glow),
   audio-routed; E5's subject -> the drummer with a flail and a return (engine: the abduction template's return,
   ADR-984); horse-11 re-homed; shots re-aimed; stage light evaluated A/B.
6. Verification renders (minimal), perf A/B under the GPU lock, one whole-film preview, the Critic once.
7. Both suites; final report.

## Log

- 2026-09-28: read the brief, the prototype's notes and report, the perf summary, the state audit, the generator.
  ADR block 980-989 assigned. Worktree build started.
