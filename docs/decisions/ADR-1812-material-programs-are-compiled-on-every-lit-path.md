# ADR-1812: Material programs are compiled on every lit path, and before the frame offline

- Status: Accepted (eng/render-improvements, 2026-10-28). Render audit prototype P1 (gap G4), approved by the owner.
- Extends ADR-1493 (compiled programs for the live procedural pass) and ADR-1003/1102 (compiled SDF trees).
- Code:
  - `src/rendering/compiled_materials.{hpp,cpp}`: `CompiledMaterialVariants` (a shader with the programs spliced in, keyed
    by `scene::materialStructureKey`, its pipelines built on Dawn's workers) and `CompiledMaterialsMode`
    (`Off`, `Async`, `Blocking`).
  - `src/rendering/scene_renderer.{hpp,cpp}`: the entity lit pass's variants (`entityMaterials_`; `createLitPipeline`'s
    descriptor sink), `setCompiledMaterials(mode)`, `compiledMaterialsPending`, `compiledMaterialsActive`, the
    Blocking wait, the `interpreted` diagnostic arm.
  - `src/rendering/skinning.{hpp,cpp}`: the skinned lit pipelines' variants (`describePipeline`).
  - `src/rendering/sdf_renderer.{hpp,cpp}`: the scene's programs spliced into compiled-tree variants.
  - `src/rendering/procedural_renderer.{hpp,cpp}`: `setCompiledMaterialsSuppressed` (the arm).
  - `src/rendering/render_quality.hpp`, `quality_policy.hpp`: `materialProgramsInterpreted`.
  - `src/app/render_job.cpp` (Blocking; `AVGEN_RENDER_INTERPRETED=1`), `src/app/application.cpp` (Async live,
    Blocking in the offline headless benchmark, the `--live-frame` wait), `src/app/live_profile_run.cpp` (the pre-warm
    waits for every path).
- Tests: `tests/rendering/test_material_compiler_gpu.cpp` (`[adr1812]`).

## Problem

ADR-1493 compiled material programs into straight-line WGSL for the live procedural pass only. Entity meshes, skinned
meshes, SDF surfaces and every offline render kept the op-list interpreter. The render audit measured the interpreter,
with the `noprograms` arm, at 18-31% of the GPU frame on the three reference scenes whose programs are on entity meshes:
Aurora Borealis 5.53 ms of 18.61, Talking Heads 4.48 of 14.55, Rift 7.65 of 43.51.

ADR-1493 kept offline on the interpreter so that a render could not switch from interpreted to compiled mid-sequence
when a variant landed.

## Decision

1. **One mechanism for every lit path.** `CompiledMaterialVariants` is ADR-1493's procedural variant generalised: a
   shader file, a label and a builder that starts the path's pipelines from the spliced module. The scene renderer's
   entity pass (pbr.wgsl: opaque, two-sided, blend; and the program-texture trio only when a program samples a
   texture), the skinning renderer (pbr_skinned.wgsl's three lit pipelines; only in a scene with a rig) and the
   procedural pass (ADR-1493, unchanged) each have one. Meshed SDF objects draw through the entity pipelines, so they
   take the entity variant. A raymarched SDF with `compile` gets the programs spliced into its compiled-tree variant,
   whose key now includes the programs' structure; an interpreted SDF tree keeps the interpreter.
2. **Three modes.** `Off` (the default: tests and any renderer nobody configures), `Async` (the live editor and the
   live profile, as ADR-1493: the interpreter draws until a variant lands), `Blocking` (offline: every frame waits for
   the variant of its own programs before it is drawn).
3. **Offline is compiled from frame 0.** A render job sets `Blocking`, so its first frame and every frame after a
   structure change are drawn compiled, never the interpreter on some frames and the variant on others. A render is
   still a pure function of the timeline, which was ADR-1493's reason to keep offline interpreted. The offline headless
   benchmark (`--headless` without `--live-frame`) does the same, so it measures what a render renders. Existing
   `--ab` figures quoted from before this ADR measured the interpreter; the `interpreted` arm reproduces them.
4. **The measurement arms.** `--ab interpreted` (`QualitySettings::materialProgramsInterpreted`) keeps every variant but
   draws the interpreter, so compiled and interpreted interleave in one process. `AVGEN_RENDER_INTERPRETED=1` keeps a
   render job on the interpreter (the picture before this ADR). The arm does not reach compiled SDF trees, whose
   programs are part of their variant.
5. **Nothing a route moves is structure** (ADR-1493 §8): the same key, so a constant moved by a palette, MIDI or a route
   reaches a compiled entity program with no rebuild (tested).

## Consequences

### Picture

- `[adr1812]`: three entities (single-sided, double-sided, blended) with ADR-1493's twelve-op program and a layer,
  first offline frame compiled: at most 1/255 on fewer than 64 of 65,536 pixels against the interpreter; the
  `interpreted` arm is byte-identical to the interpreter; a moved constant follows without a rebuild.
- `tools/picture_check.py` (ADR-1810), the five reference scenes, three frames each, offline, interpreted
  (`AVGEN_RENDER_INTERPRETED=1`) against compiled: **SAME or WITHIN on all 15 frames, largest change 1 level** (6
  frames byte-identical; 9 move by 1 level on a handful of pixels, the multiply-add contraction ADR-1493 recorded).
  Documented-equivalent.

### Performance

Headless, offline frame, 1920x1080, Feline from 60 s, `--frames 240 --ab-blocks 3` (interleaved pairs in one process,
the audit's protocol), M2 Max on AC power. `--ab interpreted` measures what compiling buys; `--ab noprograms` (the
audit's arm) measures the whole programs' cost against the same compiled baseline, so the in-run ceiling is
interpreted minus no-programs.

| Scene | Compiled | Interpreted | No programs | Ceiling (in run) | Recovered | Audit's bar |
|---|---|---|---|---|---|---|
| Aurora Borealis | 13.26 ms | 18.45 ms (pairs +5.2) | 12.50 ms | 5.95 ms | **5.18 ms, 87%** | >= 2.7 ms |
| Talking Heads | 10.90 ms | 14.65 ms (+3.78) | 10.52 ms | 4.69 ms | **3.78 ms, 81%** | >= 2.2 ms |
| Rift Live | 38.85 ms | 42.80 ms (+4.02) | 36.43 ms | 7.48 ms | **4.02 ms, 54%** | >= 3.8 ms |

- All three meet the acceptance (at least half the `noprograms` share, and the audit's absolute bars). The audit's own
  ceilings were 5.53, 4.48 and 7.65 ms.
- Rift recovers least: its one `rock` program is noise-heavy, and compiled noise is still noise. The remainder is the
  program's arithmetic, not the interpreter's dispatch.
- The live frame (`--live-frame`, Async, variants waited for): Rift 39.29 against 43.19 ms (+3.89). The Aurora and
  Talking Heads live-frame runs fell on a contended GPU (another agent's work; block spread 47-217%) and are not
  reported.
- Aurora and Talking Heads were measured twice. The first pass (14:33-14:39) coincided with another agent's parallel
  build and GPU work and its blocks varied by up to 217%; those numbers are void and only the quiet pass (16:37-16:40,
  load average 2-3, block spread 0.3-3.6%) is in the table. Rift's pass had a 2% floor.

### Cost

- Each variant is a full compile of its shader on Dawn's workers. Measured cold (a structure this machine had never
  compiled): entity pbr.wgsl variant 12.5 s (Aurora), 29.6 s (Talking Heads), 46.2 s (Rift); procedural 15.9-23.9 s.
  Warm (Metal's cache holds it): 12-19 ms. Live, the interpreter draws meanwhile (ADR-1493's switch-in period); offline
  the first frame waits.
- Module creation (WGSL parse and validation) runs on the calling thread before the pipelines go to the workers; it is
  logged (`compiled-materials module created in ... ms`).
