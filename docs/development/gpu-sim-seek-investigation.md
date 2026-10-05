# GPU simulation seek investigation (2026-10-05)

Branch `fix/gpu-sim-seek`, from main `f9f4b019`. Not merged.

## 1. The claim, and what it gets wrong

The GPU world spike (`../av-gen-gpuworld/docs/research/gpu-world-architecture-spike.md`, Phase 3,
system C) says production's stateful GPU systems "reset and warm up or catch up ≤240 steps, so past
4 s a scrubbed frame is not the played frame. That already breaks ADR-360."

**The mechanism is right. The contract it cites is not.**

- **ADR-360 is the particle *relaxation*.** In it the owner said: "I think we can ease the scrub must
  exactly replay particle animations". ADR-360's rule is that *scrub may differ from play; two
  renders of the same range may not differ*. ADR-395 already corrected the "ADR-091 = scrub must
  equal play" misquote. So for particles the behaviour is a decision, not a defect.
- **Grids are not covered by that relaxation.** ADR-360 relaxed "particles only", and ADR-032
  promises that "offline and live share the render-time model". For grids the claim is a real
  defect. ADR-581 had recorded half of it already (the `--range T:T` half) and did not fix it.
  The editor-scrub half, that a forward seek never reached the grid at all, had not been recorded.
- **Production impact today is small.** Exactly one scene in `examples/` authors a simulated grid:
  `examples/labs/grid-catchup-lab.scene.json`, the lab ADR-581 wrote to show this defect. No song
  or world uses one. Particles are everywhere.

## 2. Inventory: every piece of renderer state that crosses frames

Classes: **exact** (stateless, or restored exactly); **bounded** (converges after N frames);
**path-dependent** (a seek never matches without replay or a checkpoint).

| System | On a seek | Class | Evidence |
|---|---|---|---|
| **Simulated grids** (`rendering/simulation.cpp`, ADR-032) | Before: a forward seek was **not seen** (`resetTemporalHistory` did not reach it, and `Simulation::reset()` had no callers), so the grid lagged and repaid the gap at `maxSubSteps` a frame. A backward seek or fresh render reset the grid and granted at most 240 steps, skipping the rest. **After (ADR-1114): exact for time-invariant inputs.** | path-dependent → exact | measured, §3 |
| **GPU particles** (`particle_renderer.cpp`) | Pools emptied. An opt-in warm-up of ≤240 frames (`--particle-warmup`, off by default, never in the editor) uses the *arrival* frame's scene and camera. | path-dependent, **relaxed by ADR-360** | measured, §3 |
| Temporal ring: echo, feedback, slit, mosh (`temporal_history.cpp`, ADR-410) | Reset. FIR over ≤32 clean captures; the effect's output is never fed back. | bounded (≤32 frames) | code read |
| GTAO accumulation (`ao_renderer.cpp`) | Reset. EMA with blend 1/N. | bounded (approximately), live tiers only; Offline has no temporal shortcut | code read |
| Auto-exposure meter | Re-seeded on seek (`exposureReset`) | bounded; **no example uses auto mode** | code read |
| Focus tracker, motion-vector history, skinning previous palette | Reset or reseeded | bounded (1 frame; the landing frame has no motion blur) | code read |
| Procedural object `prevModel` (`procedural_renderer.cpp:243`) | **Not reset** | 1 frame of wrong velocity after a live seek | code read, side finding |
| Vegetation Tier-1 springs (`spatial/vegetation_sim.cpp`, ADR-056) | **Not reset** on a live seek; fresh in an offline render | path-dependent (live only) | code read, side finding |
| ISF `PERSISTENT` targets (`shader_layer.cpp`) | **Never reset** | path-dependent; **no example scene uses one** | code read |
| LOD hysteresis | Not reset | path-dependent in live tiers; off at Offline | code read |
| HistoryBank / trail effect, wave effect, sonic runtime, modulator | Restored or replayed by `Engine::seekSeconds` (ADR-700/870) | exact offline | code read |
| Fog, vortex, tornado, water, volume, SDF, ribbons, shells, wind field | Functions of time | exact | code read |

## 3. Reproduction through the product path

`tests/rendering/test_gpu_sim_seek_gpu.cpp` drives `app::Engine` + `rendering::SceneRenderer` at the
Offline tier (what `RenderJob` uses), 320x180, 30 fps, in three arms:

- **played**: one engine, every frame from 0 to T (`--range 0:T`);
- **fresh seek**: a new engine and renderer, `seekSeconds(T)` (`--range T:T`);
- **scrub**: one engine and renderer play 1 s, then seek forward through T, then back. Each seek
  goes through `resetTemporalHistory()`, as the transport does.

Metric: mean absolute RGB difference in 8-bit levels, and the share of pixels more than 2 levels
off. The stated tolerance for "the same frame" is ≤0.1% of pixels >2 levels and a mean ≤0.25
levels. The positive control is the fresh seek to 2 s (120 steps, inside the old cap), which was
byte-identical before the fix. The sensitivity control is played 2 s against played 10 s, which
differ by 140.4 levels.

### Grids: `grid-catchup-lab`

| arm | 2 s | 10 s | 30 s | 60 s |
|---|---|---|---|---|
| fresh seek, before | **0.000 / 0%** | 95.96 / 93.8% | 104.72 / 100% | 105.45 / 100% |
| forward scrub, before | 8.38 / 21.1% | 148.27 / 100% | 156.49 / 100% | 156.67 / 100% |
| backward scrub 60→10, before | | 95.96 / 93.8% | | |
| **all arms, after ADR-1114** | **0 / 0%** | **0 / 0%** | **0 / 0%** | **0 / 0%** |

**Confirmed, and fixed.** After the fix, every arm is byte-identical to the played frame. Before
it, the played frame filled the screen with smoke. The fresh seek showed a 4-second plume, and the
forward scrub showed the grid as it was before the scrub (frames in `frames/grid-*`; `grid-sheet-{before,after}.png` in the review folder are rows 10/30/60 s, columns played | fresh seek | forward scrub).

### Particles (hidden case, fails by design)

| scene | T | warm-up 0 | warm-up 240 |
|---|---|---|---|
| particle VFX lab (lifetimes 0.5–1.2 s) | 2 s | 2.57 / 4.0% (luma 0.09 vs played 2.69) | 1.89 / 6.2% (luma 3.62) |
| | 10 s | 2.60 / 4.0% | 1.88 / 6.1% |
| | 30 s | 2.63 / 4.5% | 1.83 / 6.0% |
| Tree of Life island (motes and leaves living 13–56 s) | 2 s | 0.21 / 1.0% | 0.59 / 2.4% |
| | 10 s | 0.79 / 2.8% | 1.08 / 4.1% |
| | 30 s | 0.82 / 3.1% | 1.10 / 4.2% |

**Confirmed: a seeked particle frame is not the played frame, with or without a warm-up.** This is
within ADR-360's relaxation, and it is the size of that relaxation as numbers. Three things were
found along the way:

- **With no warm-up, which is the editor's only behaviour, the field is empty.** The lab's mean
  luma is 0.09 against 2.69 played. The Tree of Life loses nearly every falling leaf.
- **A 240-frame warm-up overshoots the lab and undershoots the tree.** The lab with warm-up is
  *brighter* than play (3.62 against 2.69). Most of that is the burst system, which looks denser
  after the warm-up; the mechanism was **not** traced. The tree gets some leaves back, but
  lifetimes of 13–56 s cannot be refilled by 4 s.
- **The warm-up rolls into negative time.** `planPreRoll` steps back `n * step` from T with no
  clamp at 0, so a render that starts at 2 s simulates the 2 s *before* the song began. That is why
  warm-up 240 is worse than warm-up 0 for the tree at 2 s. It would be a contained fix, a clamp in
  `core/pre_roll.cpp`, but it is not made here, because particles are the owner's call.

### Not measured

The temporal ring, AO, the vegetation springs and persistent ISF targets were classified from the
code (§2) and not rendered.

## 4. Tests added

| test | binary / tag | before | after |
|---|---|---|---|
| `A simulated grid seeked to T draws the frame played to T` | `avgen_render_tests` `[gpu][simulation][seek]`, in the default run (~25 s) | **fails**, 8 of 10 arms | **passes**, byte-identical |
| `A particle field seeked to T draws the frame played to T` | `[.investigate][gpu][particles][seek]`, hidden | **fails**, 12 of 12 arms | fails (by design: ADR-360) |
| `What a seek costs a simulated grid` | `[.perf][seek][simulation]`, hidden | | measurement |

Set `AVGEN_SEEK_FRAMES=<dir>` to keep the frames.

One harness trap is recorded in the test. `FixedStepClock::seek(T)` makes the *next* tick land at
T + 1/fps. Only `restartAt(T)` lands on T. The first scrub arm compared T + 1/30 with T and read
the two sub-steps as a residual defect (0.32 levels at 2 s).

## 5. Fix options and their costs

### Grids

| option | exact? | memory | seek latency | scope |
|---|---|---|---|---|
| **A. Replay the whole backlog** (ADR-1114, *implemented*) | yes, for time-invariant inputs | 0 | linear in the distance. 48³: 0.057 ms/step = 36 ms at 10 s, 210 ms at 60 s, 1.0 s at 300 s. 128³ + diffusion: about 2.5 ms/step, 1.5 s at 10 s, 9.2 s at 60 s | one file, plus a hook |
| **B. GPU checkpoints** on top of A: snapshot the grid buffer every K s of play or replay, restore the nearest ≤T, replay ≤K s | same as A, bit-exact (gather-only kernels, no atomics) | one grid's floats × 4 B per checkpoint: 0.42 MB at 48³, 8 MB at 128³ (the whole shared table is 8 MB). A 4-min song at K = 5 s holds 48: 20 MB at 48³, 384 MB at 128³ | a copy (<1 ms) plus ≤K s of steps: ~17 ms at 48³, ~0.75 s at 128³ with K = 5 | contained to `Simulation`. Needs a budget, and eviction for long songs. Only an editor stall needs it |
| **C. Per-step inputs**: evaluate the field block at each sub-step's own second (the spike's per-step 256 B uniform) | makes A/B exact for animated or audio-driven fields, and makes play frame-rate independent | small | about +1 `packField` pass per sub-step on the CPU | broad: `FieldUniforms`, `fields.wgsl` binding, every kernel. Node transforms that `Engine` animates are still frame-sampled unless the engine is re-evaluated per step |

**Recommendation.** A is enough for what exists today: one lab grid with static fields. B is the
right next step the moment a real grid lands in a song, because A's scrub stall is linear in song
position. C is needed only when a grid's inputs move in time.

### Particles (owner decision; ADR-360 stands)

| option | what it buys | cost |
|---|---|---|
| P0. Clamp `planPreRoll` at t = 0 | a warm-up stops inventing pre-song seconds | a few lines. It changes `--particle-warmup` renders that start before 4 s |
| P1. ADR-395's emission-ordinal spawn hash | the same particles born at the same second whatever the pool history | five lines. **It reshuffles every particle scene in the repository** |
| P2. Exact replay: P1, plus a warm-up of `maxLifetime` frames with the scene, wind and camera re-evaluated through `Engine` at each frame | exact count *and* positions | the Tree of Life needs 56 s = 3,360 frames of Engine plus particle work per seek (not measured). The 240 cap would have to go |
| P3. GPU checkpoints of pools, taken during play | exact for any reached time | pools are cheap: 64 B per slot plus lists and trails, about 0.7 MB for the tree's two long systems per checkpoint. But a checkpoint only exists for seconds that were *played*, so a `--range T:T` render still needs P2 |

### The camera is simulation state (live and user-driven cameras)

- **Grids do not read the camera.** `SimUniforms` has no view input. Option A is exact whatever the
  camera does.
- **Particles do read it.** In live tiers, `particleCullDistance` (ADR-1098) **empties** a
  system's pool on any frame the camera is too far from it (`particle_renderer.cpp:536-560`), so
  the pool's contents depend on where the camera has been. The warm-up also runs every step with
  the *arrival* camera and view-projection.
  So a particle field's history includes where the camera was. With a timeline camera, P2 can
  re-evaluate it per step. With a live, user-driven camera (the Live panel, a projection, a hand
  on the viewport) it cannot, unless the camera is recorded per frame. That is ADR-700's
  `checkpointInputKey` problem on the GPU, as the spike found. Live audio input (`sonic.live`) is
  the same kind of input.

## 6. What was implemented

ADR-1114 (`docs/decisions/ADR-1114-a-seek-runs-a-grids-whole-backlog.md`):
`src/rendering/simulation.{hpp,cpp}` and one call in `SceneRenderer::resetTemporalHistory()`.

## 7. Suites

Run one after the other under `tools/gpu-lock.sh`, with each binary's exit code captured in the same
shell, on the branch with the fix:

- `avgen_render_tests`: **exit 0**. 588 cases: 587 passed, 1 skipped (`test_texture_share.cpp:243`,
  unrelated). 631,993 assertions.
- `avgen_tests`: **exit 0**. 4,110 cases: 4,090 passed, 19 skipped, **1 failed as expected** (the one
  shouldfail). 10,287,926 assertions.

## 8. Unverified

- Option A against a grid fed by an animated, routed or audio-driven field. There is no such scene,
  and by construction A is *not* exact there.
- The 128³ per-step cost. It comes from one sample, and 300 s came in cheaper per step than 60 s.
- Why the burst system overshoots after a warm-up.
- The temporal ring, AO, the vegetation springs and persistent ISF targets after a seek: these are
  classified from the code and were not rendered.
- The editor itself: the scrub arm reproduces the transport's calls (`seekSeconds` plus
  `resetTemporalHistory`) on a `FixedStepClock`, not through `Application` with a `RealtimeClock`.
