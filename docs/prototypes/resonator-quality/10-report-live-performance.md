# Resonator scenes: live performance (art pass II, §7 and §8)

- **Brief:** [07-brief-art-pass-2.md](07-brief-art-pass-2.md) §7 (performance) and §8 (art and performance kept
  apart).
- **Branch:** `perf/resonator-live`, in parallel with the art agent's `art/resonator-pass-2`.
- **Decisions:**
  - [ADR-1490](../../decisions/ADR-1490-the-live-profile-measures-the-briefs-matrix-arms.md): the matrix tooling;
  - [ADR-1491](../../decisions/ADR-1491-a-depth-only-procedural-draw-evaluates-the-chain-once.md): the depth-only
    vertex entry;
  - [ADR-1492](../../decisions/ADR-1492-the-live-editor-writes-velocity-only-when-something-reads-it.md): velocity
    on demand;
  - [ADR-1493](../../decisions/ADR-1493-procedural-material-programs-compile-to-straight-line-wgsl-live.md):
    compiled material programs.

## Summary

All three scenes held 30 fps only at Low or Emergency before this work. They were GPU-bound by two costs that render
scale barely touches:

- **vertex work:** every procedural vertex ran its deformer chain four times in every pass;
- **material programs:** the per-pixel interpreter took 71% of Cymatic World's frame.

Three general engine changes remove that work and leave the picture unchanged, at most 1/255 on 10 of 2,073,600
pixels. The fixed-level GPU cost falls by 35-48%.

On the live path (projection, audio and scripted MIDI, Auto, 30 fps, 1080p), the scenes now settle here:

| Scene | Before | After (main's scenes) | After (the art agent's final scenes, §7) |
|---|---|---|---|
| Cymatic World | Low | **Ultra** | **Ultra** |
| Waveform Dimension | Low | **Ultra** | **Ultra** |
| Resonant Silk | Emergency | **High/Medium** (Ultra with the prepass, §6); Ultra and High through the three representative passages | **Ultra** |

Misses go from up to 28% (Silk) to at most 0.3%.

The projection, the audio analysis and the MIDI processing each measured under 0.4 ms of a frame, so none of them
was the bottleneck.

## 1. Conditions

| | |
|---|---|
| Machine | MacBook Pro Mac14,6, Apple M2 Max (38-core GPU), 64 GB; macOS 26.7.1 |
| Display | **One display connected:** an LG UltraFine 5K at 60 Hz, which is also the main display (`system_profiler SPDisplaysDataType`). The editor and the projection window share it. **No separate projector was connected, so "projector on" means the Live panel's projection window at 1920x1080 px, windowed, on that 60 Hz screen.** The owner's own LG projection display was not available to test. |
| Target | 30 fps, 1080p output, the frame cap on (ADR-1107), Auto quality on the paced, missed-frame law (ADR-1362/1363) |
| Path | `avgen --live-profile --mode live`: the editor's own loop with present, Fifo, the frame cap, the UI, and the projection window (or none, with `--projection off`; ADR-1490) |
| Audio | `--audio` Feline Footwear (also Trench and Hyperborea for E), playing to the output device. "Audio off" on a live scene is its live input opened on a silent device (BlackHole, nothing playing); the analysis thread still runs |
| MIDI | `avgen_sonic_probe resonator`, a CoreMIDI virtual source running a scripted, time-determined sequence: two of CC 1-8/11 sweeping at a time at 25 messages/s each, CC 9 palette steps every 10 s, CC 10 lock toggles, and note-36 bursts. About 49 messages/s, all received; 1025-1476 applied per 30 s depending on the scene's bindings |
| Builds | **Before:** `9ea008c5` (the tooling only). **After:** `12d1187b` and later. The A/B arms come from one binary with temporary environment switches, which are not committed; the "before" arm uses the shaders of `9ea008c5`. |
| Rules | Every GPU job under `tools/gpu-lock.sh`, in holds of at most about 9 minutes, shared with the art agent. A/B pairs are interleaved with the order reversed on the second repeat. Fixed-level costs are headless (the GPU busy every frame). Verdicts are live. |

The machine was shared all session, and run-to-run noise at a fixed level was up to ±3 ms. Only interleaved pairs
are compared.

## 2. The baseline matrix (§7.1)

Before: `9ea008c5`, live, Feline from 95 s, 30 s measured per run, two interleaved repeats. Level shares are of the
measured frames (U Ultra, H High, M Medium, L Low, E Emergency). Misses are frames that took a 60 Hz refresh longer
than 30 fps allows.

| Scene | Arm | Level | Frame p50 / p99 ms | GPU p50 / p95 / p99 ms | Misses |
|---|---|---|---|---|---|
| Cymatic World | A: audio off, MIDI off, no projection | L 96-98% | 33.3-33.4 / 37.6-37.9 | 26.7-30.3 / 28.4-32.6 / 35.3-36.6 | 0, 1 of 901 |
| | B: audio | L 98% | 33.2-33.3 / 38.7-39.4 | 26.8-30.7 / 28.4-32.6 / 36.5-37.0 | 1, 1 |
| | C: audio and MIDI | L 98% | 33.3 / 38.3-38.4 | 27.3-27.4 / 31.3-32.2 / 35.7-36.6 | 0, 1 |
| | D: audio, MIDI, projection | L 97-98% | 33.3 / 38.6 | 29.0-30.5 / 32.4-32.8 / 36.8-37.5 | 2, 1 |
| Resonant Silk | A | L 69-99%, E 0-30% | 33.3 / 38.4-38.8 | 29.2-29.7 / 34.7-35.8 / 37.9-39.1 | 2, 2 |
| | B | **E 51-76%**, L 23-48% | 33.1-33.3 / 39.3-40.7 | 31.5-32.1 / 36.6-37.6 / 39.8-40.1 | 4, 1 |
| | C | **E 61-86%** | 33.3-33.5 / **48.5-50.6** | 32.3-32.4 / 38.5-39.0 / 41.5-43.5 | **18, 37** |
| | D | **E 61%** | 33.5-33.6 / 43.6-44.8 | 32.4 / 38.0-38.2 / 41.7-42.7 | **16, 21** |
| Waveform Dimension | A | L 96-98% | 33.3 / 35.6-37.9 | 29.9-31.9 / 31.5-33.6 / 35.8-35.9 | 1, 0 |
| | B | L 98% | 33.3 / 36.2-38.8 | 29.6-31.2 / 31.0-32.8 / 35.8 | 0, 0 |
| | C | L 96-98% | 33.2-33.3 / 38.2-38.4 | 30.1-31.9 / 32.8-33.0 / 34.8-36.4 | 0, 0 |
| | D | L 96% | 33.3 / 38.3-38.7 | 29.8-31.9 / 32.4-33.1 / 35.9-36.2 | 0, 0 |

E (projection, MIDI, the representative passages) is in §5, before and after.

What the arms show:

- **The projection costs nothing measurable (D against C).**
  - Its present takes 0.35 ms of CPU per frame.
  - The render is the same 1920x1080 either way (ADR-1490 makes the no-projection arm render the output size).
  - It adds no pass: the window copies the frame the canvas would have shown (ADR-1026).
  - The projector configuration exposes an existing GPU bottleneck rather than causing one. Untested: a separate
    display at another refresh or resolution, and fullscreen.
- **The audio processing is not a cost (B against A).** The differences are the scene's response. A silent input
  skips the resonance rows that are silent (ADR-1406), so with music the fields have more to evaluate.
- **The MIDI processing is not a cost (C against B).**
  - The control hub's drain costs 0.006-0.011 ms per frame, and the signal bus 0.02 ms.
  - Silk's extra misses with MIDI come from what the knobs do. `foldDepth` and `waveTravel` multiply the deformation up
    to 2.5x, and `resonance` and `detail` excite more of the field. A cloth folded harder overlaps itself more, and Silk
    has no depth prepass, so each overlap shades its pixels again (§6).
- **The CPU is 2-6 ms of a 33 ms frame.** It splits into:
  - UI 1.5-2.0 ms;
  - scene update 0.6-0.9 ms;
  - pass encoding 0.3-0.5 ms;
  - projection present 0.35 ms;
  - the rest under 0.1 ms each.

  The main thread spends about 28-29 ms of every frame waiting on the GPU and the cap. **All three scenes are
  GPU-bound.**

## 3. Where the GPU time went

These are headless numbers at fixed levels (Feline from 95 s, 1080p). The GPU is busy every frame, so they are costs
rather than the paced live spans. The live spans fill the period because the clock governor slows the GPU (ADR-1362).

**The render scale moves only part of the frame.** Fitting `cost = fixed + perPixel × scale²` between High (0.85) and
Low (0.5):

| Scene | Fixed (any scale) | Per-pixel at full 1080p | What the fixed part is |
|---|---|---|---|
| Cymatic World | about 18 ms | about 32 ms | A 172k-vertex ground, its depth prepass 4.9 ms on its own (constant across scales) |
| Resonant Silk | about 22 ms | about 34 ms | A 52k-vertex cloth with 8 deformers, two of which each evaluate the full resonance field |
| Waveform Dimension | about 24 ms | about 21 ms | Four sheets (112k vertices, 5 field deformers), a depth prepass of 8.4-9.2 ms, and a 3 ms volume bake |

That fixed floor is why every ladder ended at Low. The scenes author no motion blur and no depth of field, so the
Medium and Low rungs differ only by render scale.

**The two causes, both in shared engine code:**

1. **Every procedural vertex ran its deformer chain four times in every pass.** The four evaluations are the position,
   two finite-difference points for the normal, and the previous frame's chain for the velocity target.
   - The depth prepass and the shadow passes used the lit pass's vertex stage, so they paid for a normal and a velocity
     they never write.
   - In the lit pass, nothing read the velocity target, because motion blur is off.
2. **The material-program interpreter.** Measured with the `noprograms` arm (headless, full-resolution realtime tier,
   three interleaved pairs):

   | Scene | Frame | Programs' share |
   |---|---|---|
   | Cymatic World (41 ops) | 47.6 ms | **33.9 ms, 71%** |
   | Resonant Silk (40 ops) | 48.5 ms | 18.5 ms |
   | Waveform Dimension (31 + 8 ops) | 41.3 ms | 14.5 ms |

   For each op on each pixel, the interpreter reads a 112-byte record, tests 34 kinds in turn, and moves four
   registers through select trees. Its loop body holds every kind's code at once, which also sets the register
   allocation.

Also measured and ruled out:

- post-processing, at 0.1-0.3 ms in total;
- the cluster and light passes, at under 0.15 ms;
- particles, none;
- the bake grids, at 0.8 ms for Cymatic World's five planes and 3.1 ms for Waveform Dimension's volume, which is
  scene-side and kept;
- CPU uploads, under 0.01 ms;
- pipeline compiles while measuring: none in any run.

## 4. What changed, before and after

### 4.1 The three changes

| ADR | Change | Where it applies |
|---|---|---|
| 1491 | `vs_proc_depth`: the depth prepass and shadow passes evaluate the chain once (plus the normal for a masked cutout). Same position expression, `@invariant` clip | Every renderer |
| 1492 | Velocity on demand: with the motion-blur gate closed and no aux view, procedural lit draws skip the previous-frame chain and write zero motion | Live editor and live profile |
| 1493 | Material programs compiled to straight-line WGSL: one call of the op kind's own function per op on named registers. Constants stay in the buffer. Variants are built on Dawn's workers by structure key and cached per session; the controller forgets the interpreter's backoff when one lands | Live editor and live profile |

Each is a separate commit with its ADR and tests. ADR-1490 is the tooling commit.

### 4.2 Fixed-level cost (headless, interleaved, two repeats, GPU p50 ms)

| Scene | Level | Before | 1491 + 1492 | All three | Change |
|---|---|---|---|---|---|
| Cymatic World | Low (0.5) | 29.1 | 28.5 | **15.5** | -47% |
| Cymatic World | High (0.85) | 42.6 | 41.6 | **22.1** | -48% |
| Resonant Silk | Low | 31.7 | 28.7 | **19.7** | -38% |
| Resonant Silk | High | 44.1 | 40.5 | **26.4** | -40% |
| Waveform Dimension | Low | 29.6 | 22.8 | **19.2** | -35% |
| Waveform Dimension | High | 39.6 | 32.3 | **25.4** | -36% |

By pass:

- **The depth prepass:**
  - Cymatic World 4.9 → 3.5 ms;
  - Waveform Dimension 8.6-9.2 → 4.3 ms.
- **The lit pass:**
  - Cymatic World Low 21-24 → 10.8 ms;
  - Silk Low 31.5 → 19.5 ms.
- **The refactored interpreter is not slower than the old one.** The "1491+1492" column runs it, and it sits at or
  below "before" in every row.
- **1491 alone, from the first A/B (gate 2):**
  - Cymatic World Low 26.9 → 25.2 ms;
  - Waveform Dimension Low 30.9 → 25.8 ms and High 41.5 → 34.5 ms;
  - nothing on Silk, which has no prepass.

### 4.3 The picture is unchanged

These are deterministic captures: headless live profile, warm-up 0, t = 100 s, 1080p, against the "before" build.

| Scene | Low | High |
|---|---|---|
| Cymatic World | 8 pixels differ by 1/255 | 8 pixels differ by 1/255 |
| Resonant Silk | identical | 3 pixels differ by 1/255 |
| Waveform Dimension | identical | identical |

Each frame has 2,073,600 pixels.

- 1491 and 1492 alone are pixel-identical in all six.
- The 1/255 pixels come from 1493. The compiler can contract a multiply-add that the interpreter's register file kept
  apart, so live and offline renders can differ at that level. The owner accepted this.

The tests are:

- **1491:** a deformed, overlapping column field renders hash-equal with and without a prepass. If the two vertex
  entries' positions differed by one bit, the lit pass would fail its depth test.
- **1492:**
  - colour is hash-equal with and without on-demand;
  - motion blur switched on after skipped frames gives the same frame as a never-skipping renderer, and that frame
    is visibly blurred;
  - seeks are hash-equal with and without on-demand, sharp and blurred.
  - Seek and play already differ by the renderer's own frame-to-frame state with every optimisation off. That is
    unrelated to this work and is recorded in the ADR.
- **1493:**
  - compiled against interpreted on a program across most op families with a layer: at most 1/255, on fewer than 64
    pixels;
  - moving a constant changes the picture with no recompile;
  - unit tests show that nothing a palette, a CC or a route writes changes the structure key.

### 4.4 The live verdict (D: projection, audio, MIDI; Auto; Feline from 95 s; 30 s; two interleaved repeats)

| Scene | Before: level / misses | After: level / misses | After: frame p99, GPU p95 |
|---|---|---|---|
| Cymatic World | Low (L 96-100%) / 0, 0 | **Ultra**, settled (U 64%; the rest is the climb from the start-up descent) / 0, 0 | 38.1-38.8, 28.6-30.3 ms |
| Resonant Silk | **Emergency** (E 51-100%) / **234 of 827**, 11 of 894 | **Medium, High** (M 44%, H 74%) / 1, 2 | 38.3-39.4, 32.9-35.8 ms |
| Waveform Dimension | Low (L 96-98%) / 2, 2 | **Ultra** (U 80%, U 97%) / 0, 0 | 37.6-38.1, 29.6-32.6 ms |

The after-matrix (all three changes, 30 s each, one run per arm):

| Scene | A | B | C | D (above) |
|---|---|---|---|---|
| Cymatic World | Ultra, 0 misses | Ultra, 1 | Ultra (U 100%), 0 | Ultra, 0 |
| Resonant Silk | Ultra, 1 | Ultra/High, 2 | High/Ultra (U 56%, H 44%), 3 | Medium/High, 1-2 |
| Waveform Dimension | Ultra, 0 | Ultra, 1 | Ultra, 0 | Ultra, 0 |

**Why live GPU spans read 25-32 ms on a 15-26 ms scene.** With the frame cap on, the GPU clock governor fills the idle
part of each 33 ms frame (ADR-1362). A higher level therefore shows up as the level Auto holds, not as a smaller GPU
number. The fixed-level numbers in §4.2 are the costs.

### 4.5 The switch-in period (ADR-1493)

A live scene's first frames draw through the interpreter until its variant is built. This was measured live
(D conditions with `--no-prewarm`, 100 s):

- **Cold.** On a structure this machine has never compiled, the build takes 13.9 s for each scene. Auto descends on
  the interpreter (Cymatic World to Low, Silk to Emergency).
  - With the backoff reset, it climbs back at about 5 s a level once the variant lands:

    | Scene | Variant lands | Then |
    |---|---|---|
    | Cymatic World | 13.8 s | Ultra at 27.7 s |
    | Waveform Dimension | 21.8 s (two programs) | Ultra at 30.7 s |
    | Resonant Silk | 13.9 s | Medium at 23.7 s |

  - Without the reset, the interpreter's backoff held Cymatic World at Medium until 78.8 s and Waveform Dimension at
    High until 94.3 s, and left Silk at Emergency 86% of the run.
- **Warm.** On a structure compiled before on this machine, Metal's cache returns the variant in 0.16-0.7 s. Within a
  session, a scene the set returns to reuses its variant from its first frame.

## 5. Sustained, representative passages (E: projection, audio, MIDI; Auto)

The measured windows are Feline 80-170 s (90 s), and Trench and Hyperborea from about 110 s (60 s each). Warm-up
can stretch, so each window starts after the level settles.

| Scene | Passage | Before: level / misses / frame p99 | After: level / misses / frame p99 |
|---|---|---|---|
| Cymatic World | Feline | Low (L 97%) / 4 of 2700 / 38.1 | **Ultra** (U 88%) / 0 of 2701 / 38.5 |
| | Trench | Low (L 98%) / 3 of 1800 / 38.5 | **Ultra** (U 90%) / 1 of 1801 / 38.6 |
| | Hyperborea | Low (L 93%) / 3 of 1800 / 38.3 | **Ultra** (U 82%) / 0 of 1800 / 38.5 |
| Resonant Silk | Feline | **Emergency** (E 96%) / **44 of 2656** / 43.3 | **Ultra** (U 88%) / 6 of 2697 / 39.1 |
| | Trench | **Emergency** (E 72%) / **57 of 1753** / 46.4 | **High** (H 74%) / 3 of 1800 / 38.6 |
| | Hyperborea | **Emergency** (E 71%) / 17 of 1794 / 41.6 | **Ultra** (U 90%) / 1 of 1801 / 38.1 |
| Waveform Dimension | Feline | Low (L 98%) / 1 of 2701 / 37.6 (this run received no MIDI: the probe did not connect) | **Ultra** (U 99%) / 0 of 2701 / 38.7 |
| | Trench | Low (L 97%) / 1 of 1801 / 35.6 | **Ultra** (U 90%) / 0 of 1801 / 38.1 |
| | Hyperborea | Low (L 97%) / 0 of 1801 / 35.9 | **Ultra** (U 90%) / 0 of 1800 / 38.4 |

Frame p99 is about 38 ms in every after-run. That is a frame or two landing on the next vsync, which is how a 60 Hz
display shows a capped 30 fps loop's jitter. It is not a GPU spike: the GPU p99 is 30-31 ms on Cymatic World and
Waveform Dimension, and 38-39 ms on Silk.

## 6. Scene-side findings, sent to the art agent

I sent these through the coordinator. I did not edit the scene files.

1. **Silk: `environment.depthPrepass: true`.**
   - The cloth overlaps itself, and without a prepass the 40-op material shades every overlapping layer. With ADR-1491,
     a prepass costs one chain evaluation per vertex (4.7 ms).
   - Measured headless at a fixed level, with my changes in and no prepass yet: High 47.6 → 32.8 ms, Low 28.7 →
     25.0-27.7 ms.
   - **Live:** D conditions, all three changes, two runs. Silk with the prepass settles at **Ultra** (U 80%) with 0
     and 1 misses, and GPU p95 28.5 ms. Without the prepass it was Medium or High, with 1-2 misses and GPU p95
     33-36 ms.
   - The art agent's current Silk (`5671ce1f`) has turned the prepass on (§7).
2. **Silk: the two resonance deformers.** `wallRes` and `sailRes` each multiply the full `resonator` field, so the
   resonance is evaluated twice per chain evaluation.
   - Removing both measured -13 ms at High and -6 ms at Low. That number shows what they cost; it is not a proposal
     to remove them.
   - A bake volume over the hero would evaluate more cells (about 1.4M) than the vertices do now (about 416k
     evaluations), so it is likely not cheaper.
   - Evaluating the resonator once and applying it along both axes is the cheaper option. It changes the picture
     slightly, so it is the art agent's call.

## 7. The art agent's scenes (§8)

These are the art agent's final scenes: `art/resonator-pass-2` at `5671ce1f`. Its head, `a55b49f9`, adds only its
report. I profiled copies of its three examples and meshes and did not commit them. Its changes are scene-side only;
it makes no engine changes. Three of them bear on performance:

- Cymatic World's ground program grew to 47 ops.
- Silk gained a veil shell (a second program, 14 ops) and **turned on its depth prepass** (§6).
- Waveform Dimension's surface program grew to 38 ops.

**Fixed-level cost** (headless, interleaved, two repeats, GPU p50 ms; before = the engine at `9ea008c5`, after = all
three changes):

| Scene | Low: before → after | High: before → after |
|---|---|---|
| Cymatic World | 23.8 → **15.5** (-35%) | 36.6 → **20.8** (-43%) |
| Resonant Silk | 32.4 → **18.4** (-43%) | 41.4 → **23.4** (-43%) |
| Waveform Dimension | 30.2 → **19.2** (-36%) | 40.0 → **25.4** (-37%) |

**Picture** (deterministic captures at t = 100 s): Cymatic World and Silk differ on 3-10 pixels by 1/255. Waveform
Dimension is identical.

**Live:** D (projection, audio, MIDI, Auto, Feline from 95 s, 30 s, one run each); E (Feline 80-170 s, after only).

| Scene | D before | D after | E after |
|---|---|---|---|
| Cymatic World | Medium (M 98%), 2 misses | **Ultra**, 0 misses, GPU p95 30.1 | **Ultra** (U 93%), 1 of 2700 |
| Resonant Silk | **Emergency** (E 75%) | **Ultra**, 0 misses, GPU p95 29.8 | **Ultra** (U 93%), 1 of 2700 |
| Waveform Dimension | Low (L 96%), 1 miss | **Ultra**, 0 misses, GPU p95 29.2 | **Ultra** (U 99%), 0 of 2701 |

The art agent's richer scenes, on the optimised engine, hold the top level live at 30 fps with audio and MIDI. On the
engine as it was, they held Medium, Emergency and Low.

Two caveats for the art agent's own report:

- Its back-to-back live profiles were taken on the engine without these changes, so its levels describe that
  engine.
- Its 47-op Cymatic World program is one op short of the 48-op limit (`kMaxMaterialOps`).

## 8. What remains GPU-bound, and why

- **All three scenes are still GPU-bound, now at a much higher level.**
  - After the changes, Cymatic World costs 15.5 ms at Low and 22.1 ms at High (headless), and holds Ultra live.
  - Its remaining time is the lit pass: about 17 ms at High for the compiled 41-op program over every ground pixel,
    the ground's 342k triangles and their tiling, plus the prepass (3.5 ms) and the five bake planes (0.8 ms).
- **Resonant Silk** is the heaviest.
  - Its remaining cost is the cloth's vertex work, about 19 ms at Low on its own: 8 field deformers, two of them the
    full resonance, times three chain evaluations for the position and normal.
  - Its other cost is overdraw without a prepass.
  - With MIDI pushing the folds, it holds High or Medium rather than Ultra on Feline 95-125 s, and Ultra or High
    through the longer passages.
  - Both remaining costs are scene-side (§6), and the art agent has them.
- **Waveform Dimension** holds Ultra live. Its remaining costs are the sheets' field deformers (the lit pass's three
  chain evaluations), the 4.3 ms prepass, and the 3.1 ms volume bake.
- **The switch-in period:** the first time a program structure is compiled on a machine, the scene plays its first
  ~14 s through the interpreter (§4.5).
- **Not done, and why:**
  - **Finite-difference normals.** These are still three chain evaluations per lit vertex. An analytic or topological
    normal would change the shading.
  - **Compiling the mesh and SDF paths' programs.** The Resonator scenes use only procedurals.
  - **Offline renders.** These stay on the interpreter so that a render remains a pure function of its timeline.
- **Untested:**
  - a separate projector, at its own resolution or refresh, or fullscreen;
  - the owner's own LG display as a second screen;
  - a hardware MIDI controller (the scripted virtual source stands in for it);
  - 4K output.

## 9. Checks

Final checks ran on head `45130e19` (plus this report), after a fresh reconfigure (`cmake --fresh --preset release`) and
a rebuild. Each exit code is the process's own:

- `tools/ci/run-suite.sh cpu-assets`, under the GPU lock: **PASS, exit 0**.
  - 4327 passed, 0 failed, 19 skipped, 1 expected failure; ran 4347 of 4347.
  - The one console `FAILED` line is the `[!shouldfail]` case in `test_character_lab_slopes.cpp`, counted as the
    expected failure.
- `tools/ci/run-suite.sh gpu`: **PASS, exit 0**. 675 passed, 0 failed, 1 skipped; ran 676 of 676.
- `python3 tools/check_project_integrity.py`: **0 problems, exit 0**.

The new tests are tagged `[adr1490]`, `[adr1491]`, `[adr1492]` and `[adr1493]`. They live in:

- `tests/unit/test_live_profile.cpp`
- `tests/unit/test_material_compiler.cpp`
- `tests/unit/test_interactive_resolution.cpp`
- `tests/rendering/test_procedural_gpu.cpp`
- `tests/rendering/test_material_compiler_gpu.cpp`

The data and scripts are in the worktree's `build/perf/` (gitignored). The A/B arms come from a scratch binary with
environment switches (`build/perf/knob.patch`), which is not committed.
