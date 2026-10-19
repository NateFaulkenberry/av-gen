# ADR-1493: In the live editor, procedural material programs are compiled to straight-line WGSL

- Status: Accepted (perf/resonator-live)
- Extends: ADR-030 and ADR-036 (material programs and their interpreter), ADR-050 (the interpreter's cost rules),
  ADR-1003 and ADR-1102 (the SDF renderer's compiled variants, built on Dawn's workers). This ADR follows the same
  pattern for materials.
- Code:
  - `src/scene/material_compiler.{hpp,cpp}`: the structure key, the generated WGSL and the splice. GPU-free, so it is
    in the core library.
  - `shaders/material.wgsl`: one `matK<Kind>` function per op kind, plus the `@@material-compiled-begin/end` block.
  - `shaders/pbr_shade.wgsl`: calls `evaluateMaterialProgramFast`.
  - `src/rendering/procedural_renderer.{hpp,cpp}`: the variant, its async build and the lit pass's pipeline choice.
  - `src/rendering/scene_renderer.{hpp,cpp}`: `setCompiledMaterials`.
  - `src/app/application.cpp`, `src/app/live_profile_run.cpp`: turned on, and the profile waits for the variant.
- Tests:
  - `tests/unit/test_material_compiler.cpp` (`[adr1493]`): the text, the key (and everything a route writes leaving it
    unchanged) and the splice;
  - `tests/unit/test_interactive_resolution.cpp` (`[adr1493]`): forgetting the backoff climbs back sooner;
  - `tests/rendering/test_material_compiler_gpu.cpp`: compiled against interpreted, and a route on a constant
    without a recompile.

## Problem

Measured with the profiler's `noprograms` arm: headless, full-resolution realtime tier, Feline Footwear from 95 s,
three interleaved pairs each. The material programs were the largest single cost in all three Resonator scenes:

| Scene | Frame | Programs' share |
|---|---|---|
| Cymatic World (`cwGround`, 41 ops) | 47.6 ms | 33.9 ms, 71% |
| Resonant Silk (`rsSilk`, 40 ops) | 48.5 ms | 18.5 ms |
| Waveform Dimension (`wdSurface`, 31 ops, and `wdDome`, 8 ops) | 41.3 ms | 14.5 ms |

The interpreter (`materialRunOps`) loops over 48 op records. For every op on every pixel it:

1. reads the record from the storage buffer;
2. tests the kind against each of the 34 kinds in turn;
3. reads three registers through a seven-select tree each, and writes one back through eight selects.

Its loop body holds every kind's code at once, which also sets the register allocation (ADR-050).
The structure of a program is fixed for the length of a scene. Only its constants move, by routes and MIDI.

## Decision

1. **Per-kind functions.** The interpreter's if-chain is split into one function per op kind (`matKRamp`,
   `matKRemap`, …). Each reads the op's constants, scalar and seed from the buffer by slot and op index. The
   interpreter now calls them (`materialEvalOpKind`), and it computes exactly what it computed before.
2. **A compiled program** (`compileMaterialProgramWgsl`) is the same evaluation with the structure written out:
   - each op is one call of its kind's function on named registers `r0`..`r7`;
   - an `input` op is a literal `materialInputValue(<input>u, local)`;
   - an op with a register out of range is left out, as the interpreter skips it;
   - the outputs and the layers are written for the registers the program names.

   Everything a route can move is still read from the buffer, so a moving parameter never needs a recompile. That
   means the constants, the scalar, the seed, the emission intensity, and the layer's intensity and blend range.
3. **The structure key** (`materialStructureKey`) covers the program count, the outputs, the base op count, the
   layer count and records, and every op's kind, input and registers. It covers nothing else. A change of key is the
   only thing that builds a new variant.
4. **The variant.** `ProceduralRenderer` splices the scene's library into `procedural.wgsl`:
   - each program's function, and
   - `evaluateMaterialProgramFast`, a switch on the slot whose default is the interpreter.

   It builds the two lit pipelines (culled and two-sided) with `CreateRenderPipelineAsync`. Until they exist, or if
   they fail, the interpreter's pipelines draw, so a structure change never stalls a frame.

   The depth pipeline keeps the interpreter. It runs a program only for a masked material's cutout (ADR-1371). The
   mesh and SDF paths call `evaluateMaterialProgramFast` and get the interpreter, because only the procedural
   renderer builds variants.
5. **Live only.** `SceneRenderer::setCompiledMaterials(true)` is called by `Application::runLive` and the headless
   live profile. The profile waits for the variant before its warm-up, as it already waits for SDF variants
   (ADR-1102).
   - Offline renders, render jobs and the tests keep the interpreter. A render that switched mid-sequence when a
     variant arrived would not be a pure function of the timeline.
   - **So live and offline can differ by 1/255 on a rare pixel** (the contraction under Consequences). That is
     accepted.
6. **Variants are kept by structure key**, up to 16 per session, so a scene visited again in a live set draws
   compiled from its first frame. Across sessions, Metal's own shader cache returns a rebuilt variant of an unchanged
   structure in under a second (measured below). Only a structure nobody has compiled on this machine pays the full
   build.
7. **The controller forgets the interpreter's backoff when the variant lands.** `Application::runLive` calls
   `InteractiveResolution::forgetBackoff` when `compiledMaterialsActive` turns true. The levels left on the
   interpreter's misses were refused on a cost the scene no longer has. The level in force is kept, and the climb
   proceeds at the ordinary hold instead of the up-to-16x backoff (ADR-1362 §6).
8. **Nothing a palette, a MIDI CC or a route writes is structure.** The key ignores every op's constants, scalar, seed
   and field slot, the emission intensity, the gate's numbers, the layers' parameters and the flags. None of those
   ever rebuilds a variant: a unit test changes each one and checks the key, and the GPU test moves a constant on a
   compiled program and checks that no compile starts.

## The switch-in period, measured

A live scene opens on the interpreter until its variant is built. This was measured live (projection, Feline from
60 s, scripted MIDI, Auto, 30 fps target, `--live-profile --no-prewarm`, which does not wait for the variant).

**Cold**, meaning a variant this machine has never compiled: the perf build adds a switch case no slot reaches, so the
generated MSL misses every cache. The build takes 13.9 s on Dawn's workers for each of the three scenes. The
controller descends on the interpreter's misses in that window (Cymatic World and Silk to Low or Emergency).

The first table is the level when the variant lands and what follows. The second is where the 100 s were spent.

| Scene | Variant at | With `forgetBackoff` | Without it |
|---|---|---|---|
| Cymatic World | 13.8 s | Medium 17.8 s, High 22.8 s, **Ultra 27.7 s**, held | Medium; High only at 78.8 s |
| Resonant Silk | 13.9 s | Low 18.7 s, Medium 23.7 s; a High probe fails at 28.6 s, back at Medium by 38.8 s | Emergency until Low at 31.7 s, then Emergency again at 37.3 s |
| Waveform Dimension | 21.8 s (its two programs) | High 25.8 s, **Ultra 30.7 s** | High 29.4 s, Ultra only at 94.3 s |

| Scene | With | Without |
|---|---|---|
| Cymatic World | U75 L13 H6 M6 | M67 H24 L9 U1 |
| Resonant Silk | M64 L24 E9 | E86 L11 |
| Waveform Dimension | U71 M23 H6 | H67 M26 U7 |

**Warm**, meaning the structure was compiled before on this machine, in any process: the variant arrives in 0.16-0.7 s
from Metal's cache. On a second launch Cymatic World was compiled at 0.7 s and at Ultra by 10.4 s. Within a session,
a scene revisited reuses its variant from the first frame.

So the window costs a first-ever compile of about 14 s of the interpreter's picture cost, plus the ordinary climb of
about 5 s a level. It recurs only after a program's structure changes: an art edit, or a build that changes the
generated code. Starting the build at project load was considered and not chosen, because the build already starts
on the scene's first frame.

## Consequences

- **The picture.**
  - Silk's capture is pixel-identical to the interpreter's.
  - Cymatic World's differs on 8 of 2,073,600 pixels by 1/255. The compiler may contract a multiply and an add that
    the interpreter's register file kept apart.
  - The GPU test holds compiled against interpreted to at most 1/255 on fewer than 64 of 65,536 pixels, on a program
    across most op families plus a layer. It holds the same after a constant moves, with no recompile.
- **Measured.** Headless A/B and the live matrix are in `docs/prototypes/resonator-quality/10-report-live-performance.md`.
- **A structure change in the editor costs a compile on Dawn's workers.**
  - About 13 s for Cymatic World's 41-op program: the whole `procedural.wgsl` module recompiles.
  - Meanwhile the interpreter draws at its old cost.
  - Opening a live scene starts in the same state until the first variant is ready.
- **Adding an op kind** now means adding its `matK` function and its name in `material_compiler.cpp`. A
  `static_assert` there fails on a new kind.
