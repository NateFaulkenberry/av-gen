# ADR-1603: One refraction phase for water and procedural materials, with ladder levers

- Status: Accepted (eng/living-prism-p1, The Living Prism P1-4)
- Generalises ADR-1286's mid-pass frame copy. Adds two `LiveQualityRung`/`QualitySettings` levers (precedent ADR-1222's
  `astralTier`). Answers the scene agent's 06 §6 (the cinematic water's SSR had no ladder lever).
- Code:
  - `src/scene/scene_types.hpp` (`Refraction`), `src/scene/material_optics.{hpp,cpp}` (the `"refraction"` block and the
    `material/refraction/{strength,opacity,thickness}` parameters), `src/scene/composition.cpp` (an authored block --
    and thin film, anisotropy -- now reaches a GLB mesh's parts);
  - `src/rendering/optics_pack.hpp` (`packRefraction`), `ObjectUniforms::refraction0/1`;
  - `shaders/procedural.wgsl` (`fs_proc_refract`; the fragment body is `procShade`), frame bindings 18/19 (the copy and a
    clamped sampler) in `scene_renderer.cpp`'s frame layout;
  - `src/rendering/procedural_renderer.{hpp,cpp}` (`setRefractionPhase`, `drawRefractive`: a depth lay-down, then the
    refraction pipelines, built on first need);
  - `src/rendering/scene_renderer.cpp` (one split and one copy for the cinematic water and the refractive surfaces;
    `frameBindGroupRefract_`; the phase logged when it changes);
  - `src/rendering/water_renderer.{hpp,cpp}` (`applyWaterSsrScale`);
  - `src/rendering/render_quality.hpp`, `src/app/interactive_resolution.{hpp,cpp}` (the levers, the ladders, ceilings,
    `applyLeverToCeiling` levers and quality arms `norefraction`, `waterssrhalf`, `nowaterssr`).
- Tests: `tests/rendering/test_refraction_gpu.cpp` (`[adr1603]`): a clear crystal shows the backdrop through the copy,
  absorption darkens it by channel, full opacity is its own colour, the ladder off draws it opaque (identity kept) with
  no refractive draw; a scene with no refractive material has the same passes and the same pixels with the lever on or
  off, and a refractive scene adds exactly one render pass; the water lever's steps and rays. A hidden `[measure]` case
  times a small fixture. `tests/unit/test_material_optics.cpp`, `test_interactive_resolution.cpp`,
  `test_composition.cpp` (`[adr1603]`).

## Problem

The Living Prism's crystal reads as "pastel plastic" (06 §5): flat albedo and one emissive colour, no depth. Procedural
surfaces were opaque only (01 §2.1). The one see-through surface, ADR-1286's cinematic water, had its own frame copy, and
neither it nor anything else refracting had a ladder lever, so its cost was fixed at every rung (06 §6).

## Decision

**One refraction phase.** When a cinematic water material or a refractive procedural surface is in the frame, the
scene pass is split once, the lit opaque frame is copied once, the water draws (as before), then the refractive
surfaces draw through `frameBindGroupRefract_`, which binds the copy at frame binding 18. Each reads the frame as it
stood before either.

**A per-material block.** `"refraction": {"strength", "opacity", "thickness", "absorption"}` on any procedural
material. While the phase runs, a refractive object is left out of the depth prepass (so the linear depth behind it is
the scene's) and out of the lit pass; it is drawn after the copy, first laying its own depth (colour write-masked off, so
overlapping crystals shade each pixel once), then with `fs_proc_refract`:
- the copy is read at the pixel offset by `strength` along the view-space normal (a normal-offset UV);
- it is absorbed per channel by `exp(-absorption x path)`, the path being the linear depth behind minus the surface's
  own, at most `thickness` metres;
- the surface's own shaded colour (lighting, emission) covers `opacity` of it, and the transmission is scaled by
  1 - Fresnel (Schlick, f0 0.04).

**The levers.** `QualitySettings::refraction` (the ladders turn it off at Low and Emergency) and `waterSsrScale` (1 down to
Medium, 0.5 at Low, 0 at Emergency: a fraction of the water's authored SSR steps and rays, floored at the shader's 8
steps and one ray; 0 turns the SSR off and the environment reflection remains). With refraction off a refractive
surface draws in the lit pass as its opaque material, so the authored base colour and emission are its fallback
identity: author a refractive material's base colour as its opaque look. A priority ladder's `post` group carries both.

Alternatives: a weighted-blended OIT pass (defer, 03 §10: only if crystal is seen through crystal), a per-material copy
(several copies for several materials), screen-space raymarched refraction (cost).

## Evidence (honest timestamps, ADR-1600; M2 Max, AC power, 1080p, Release; `~/Desktop/av-gen-review/83-living-prism-p0/P1-4/`)

**The Living Prism blockout** (`examples/living-prism`, built with its `build.py` from `scene/living-prism`), headless
Ultra, the scene's typical (65.7 s) and heaviest (123.22 s) windows, 10 s each, interleaved. "refract" gives all 12
crystal nodes `strength 0.03, opacity 0.25, thickness 2, absorption [0.4, 0.8, 0.2]`:

| arm | typical GPU p50 / scene pass | heaviest GPU p50 / scene pass |
|---|---|---|
| base (no refraction) | 31.55 / 15.95 ms | 29.12 / 13.09 |
| refract, first version (no depth lay-down) | 38.34 / 22.80 | 38.43 / 22.52 |
| **refract, with the depth lay-down** | **32.74 / 17.23 (+1.2 ms)** | **31.58 / 15.68 (+2.5 ms)** |
| refract, `norefraction` arm (the ladder's Low) | 31.51 / 15.93 (= base) | 29.15 / 13.08 (= base) |
| base, `waterssrhalf` | 30.04 / 14.42 (**-1.5 ms**) | 28.93 / 12.97 |
| base, `nowaterssr` | 29.07 / 13.49 (**-2.5 ms**) | 28.81 / 12.85 |

- The first version cost about 7 ms: with the crystals out of the prepass, every overlapping crystal (the geode's ~190
  prisms) shaded every pixel it covered. Laying their depth first brought it to 1.2-2.5 ms.
- The water's SSR costs **2.5 ms** in the typical window (where the pool fills the lower frame), not the 0.1-0.4 ms 06 §4
  read on quantised timestamps. The lever halves or removes it.
- Stills of the same instants: 11.5% (typical) and 21.7% (heaviest) of the pixels change by more than 8/255; the crystals
  read as tinted glass over the grotto behind them (`stills-refract/`).

**A small fixture** (one crystal over about a third of a 1080p frame, 4 interleaved reps of 120 frames): GPU p50 off
4.33-4.43 ms, on 3.73-4.41 ms (no increase: the crystal leaves the prepass and its own lit draw); the scene label
2.65-2.71 vs 2.81-3.23 ms.

## Consequences

- The scene agent can author refraction on crystal materials now; the ladder takes it away at Low with the opaque look
  kept.
- One HDR copy (16.6 MB at 1080p) is allocated the first time any refraction or cinematic water draws.
- The first frame with a refractive surface builds three pipelines (synchronously, on the interpreter's module: a
  compiled-materials variant has no refraction pipeline yet, so a refractive surface with a program runs it
  interpreted).
- A refractive surface is not in the prepass, so ambient occlusion and contact shadows do not see it.
