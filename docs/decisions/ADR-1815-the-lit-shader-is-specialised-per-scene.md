# ADR-1815: The entity lit shader is specialised to the features its scene uses

- Status: Accepted (eng/render-improvements, 2026-10-28). Filament evaluation borrow item A2, approved by the owner.
- Builds on ADR-1812 (the compiled-materials variants, which now also carry the specialisation).
- Code:
  - `shaders/pbr_shade.wgsl`, `shaders/lighting.wgsl`: nine `override` bools (`LIT_PROGRAMS`, `LIT_FX`, `LIT_TOON`,
    `LIT_STYLED`, `LIT_OPTICS`, `LIT_BANDS`, `LIT_WAVES`, `LIT_ATMOS_GROUND`, `LIT_AREA_LIGHTS`), each `true` by
    default and each gating the branch its feature already had (`litWavesAt`, `litAtmosphereGroundAt`).
  - `src/rendering/lit_features.hpp`: the feature bits, the constants a specialised pipeline sets, their names.
  - `src/rendering/compiled_materials.{hpp,cpp}`: variants keyed by (program structure, lit features); the builder
    receives the features.
  - `src/rendering/scene_renderer.{hpp,cpp}`: `litFeaturesInUse`, the sticky per-scene mask, `createLitPipeline`'s
    constants, `RenderStats::litFeatures`, the `generallit` arm.
  - `src/rendering/skinning.{hpp,cpp}`: the skinned lit variants take the same constants.
  - `src/rendering/render_quality.hpp`, `quality_policy.hpp`: `QualitySettings::litGeneral`.
- Test: `tests/rendering/test_lit_specialisation_gpu.cpp` (`[adr1815]`).

## Problem

The Filament evaluation measured AV Gen's lit shading at about 2.7x Filament's per pixel on a plain PBR material lab:
the lit `scene` pass was 8.38 ms of a 9.78 ms frame at 1080p (2.53 ms of it contact shadows). Every feature the lit
shader can draw (material programs, the entity effect lanes, cel and stylised lighting, thin film and anisotropy,
reflection bands, surface waves, the sky's ground light, area lights) sits behind a uniform branch. A branch that is never
taken still keeps its code, and the registers it needs, in the light loop. On Apple GPUs that costs occupancy.

## Decision

- **Override constants, not permutations.** Each feature is a WGSL `override` bool that defaults to `true`. A pipeline
  that sets none is the general shader exactly, so the procedural pass, the SDF pass, water and every test are
  unchanged. A specialised pipeline sets the unused ones to `false` and the compiler removes their code. Measured
  first: with all nine off, the material lab's lit pass fell from 8.30 to 2.72 ms and its frame was **byte-identical**.
  One module serves every variant; only the pipelines differ.
- **The mask is derived from what the GPU reads**, so a gate is closed only where the general shader would have
  skipped the same branch:
  - the frame uniforms: stylised lighting, reflection bands, waves, the sky's ground light;
  - the packed lights: any rect, disk, tube or sphere light;
  - the staged object uniforms of every entity drawn this frame: effect lanes (`fxA.z`), cel (`toon0.x`), film and
    anisotropy (`optics`), and each material's program slot.

  A scene with SDF objects keeps every feature: its meshed objects draw through the entity pipelines with uniforms the
  scan does not read.
- **The mask is sticky per scene**: the union of every feature the scene has used since it opened, reset when a
  different scene opens. A feature that comes and goes with the music (a wave on the beat) costs one more variant
  once, not a switch every time. In the measurement below Aurora's mask grew once, when its first wave appeared.
- **It rides on ADR-1812's variants**: the entity and skinned lit variants are keyed by (program structure, mask), so a
  scene has one variant for both. A scene with no program now gets a variant too, unless it uses every feature. Live,
  the general shader draws until the variant lands (Async). Offline, the frame waits for it (Blocking).
- **`--ab generallit`** draws the general shader in the same process: what this ADR buys.
- **Scope**: entity and skinned meshes. Procedurals, raymarched SDFs and water keep the general shader; the material
  lab and the Filament comparison are entity scenes.

## Consequences

### Performance

Headless, offline frame (specialised variant against `generallit`), 1080p, `--frames 240 --ab-blocks 3`, interleaved:

| Scene | Lit features in use | Specialised | General | Difference |
|---|---|---|---|---|
| **Material lab (S1)** | none | **4.01 ms** (lit pass **2.70**) | 9.61 ms (lit pass **8.30**) | **-5.59 ms; lit pass -67%** (pairs -5.58, -5.62, -5.59) |
| Rift Live | programs | 35.96 ms | 41.64 ms | **-6.03 ms** (16.8%) |
| Event Horizon | none | 6.05 ms | 6.68 ms | -0.64 ms |
| Aurora Borealis | programs, atmosphere ground, waves | 12.81 ms | 13.29 ms | -0.48 ms |
| Talking Heads | programs | 10.92 ms | 10.94 ms | not resolved: two runs VOID (another agent's load; blocks varied up to 26%), both near 0 |
| Rainbow World | programs | 26.90 ms | 27.60 ms | -0.60 ms (re-run on a quieter machine; most of its lit cost is procedural, which this ADR does not touch) |

**Acceptance: the lab's lit pass falls 67% against the bar of 30%** (8.30 -> 2.70 ms, bar 5.9 ms). With A6
(ADR-1816) left aside, contact shadows are still on in these numbers.

### Picture

- The lab, all nine gates off against the general shader: byte-identical (2,073,600 pixels).
- `[adr1815]`: plain PBR, a cel material, an anisotropic material and a rect light each give exactly the expected mask,
  and the specialised frame is within 1/255 of the general one.
- The five reference scenes, offline, 960x540, three frames each (ADR-1810, `--tol 1`), against the capture before this
  ADR: **SAME or WITHIN on all 15 frames, no pixel beyond 1 level** (9 byte-identical; Rift's three frames and one Event
  Horizon frame move by 1 level on a handful of pixels).

### Variants and warm-up

- **Variant count:** one entity variant per scene per mask, plus a skinned one in a scene with a rig. Scenes with
  programs already had a variant (ADR-1812), so specialisation adds none there. Scenes without programs (the lab, Event
  Horizon) gain one. Aurora built two because its mask grew once (a wave appeared); the sticky mask stops it flipping
  back.
- **Cold** (a variant this machine had never compiled; offline, so the frame waited for it): lab 1.2 s, Event Horizon
  9.8 s, Aurora 18.2 s and then 10.5 s for the grown mask, Talking Heads 20.9 s, Rainbow World 21.4 s, Rift 37.3 s. That
  is the same order as ADR-1812's compiled variants (12.5-46 s), which these replace for scenes with programs.
- **Warm** (Metal's cache holds it): 14-20 ms per variant.
- **Start-up, warm, before and after** (`--live-profile --mode headless --quality ultra`, load + first frame + pre-warm,
  the second run of each; the Part 2 binary built from `8262c034` in a temporary worktree, the two builds interleaved
  scene by scene; load average 7-43 from another agent, which moves these by a few hundred ms):

  | Scene | Part 2 (8262c034) | This ADR | First frame, before -> after |
  |---|---|---|---|
  | Material lab | 301 ms | 333 ms | 71 -> 81 ms |
  | Aurora Borealis | 4,501 ms | 4,450 ms | 109 -> 124 ms |
  | Talking Heads | 4,293 ms | 4,333 ms | 147 -> 149 ms |
  | Rift Live | 22,358 ms | 22,367 ms | 1,006 -> 986 ms |
  | Event Horizon | 4,249 ms | 4,674 ms | 126 -> 183 ms |
  | Rainbow World | 7,288 ms | 7,226 ms | 1,635 -> 1,623 ms |

  Warm start-up is unchanged within the noise; Event Horizon, which gained a variant it did not have, pays about
  60 ms on its first frame.
- **Live first frame** (material lab, `--live-profile`): 64 ms before; 82 ms with a warm variant (the extra module
  creation); **about 1.1 s with a cold one** (twice: 1,128 and 1,072 ms), once per variant per machine. The variant is
  built on Dawn's workers and the general shader is meant to draw meanwhile, yet that first frame took as long as the
  compile. Why it waits was not established here; after that frame the general shader draws until the variant lands.
