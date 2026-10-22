# ADR-1590: A depth draw runs a material program only where it can cut

- Status: Accepted (perf/rainbow-world-live)
- Amends ADR-1371 (a program's opacity cuts the depth prepass). Behaviour-preserving: the cutout is the same, and only
  the work that could not change it is skipped.
- Code:
  - `src/scene/material_program.{hpp,cpp}`: `MaterialProgram::writesOpacity()` and the predicate
    `scene::depthDrawRunsProgram(masked, program)`;
  - `src/rendering/procedural_renderer.{hpp,cpp}`: `ProceduralUniforms::timeInfo.w` is that predicate for the draw. It
    was the instance count, which no shader read;
  - `shaders/procedural.wgsl`: `fs_proc_depth` runs the program only when `proc.timeInfo.w` is set, and `procVertex`
    asks for the depth draw's normal only then.
- Tests:
  - `tests/unit/test_depth_program_skip.cpp` (`[adr1590]`): the predicate's cases (masked or not, with or without a
    program, opacity register in range or not, layers that cannot write opacity);
  - `tests/rendering/test_procedural_gpu.cpp` (`[adr1590]`): a masked procedural whose program writes only the base
    colour is still cut in depth by its texture's alpha (the column behind shows through), and occludes when the texel
    is opaque, with and without the program;
  - ADR-1371's own test is the guard on the other side: a program that writes opacity still cuts.

## Problem

Rainbow World's live frame could not hold High late in the song (from about 2:30). The leaf cards were the largest
part of the depth prepass: hiding them took the prepass at 140 s from 7.9 to 3.5 ms.

Their material is masked and has a program (the canopy colour ramps). ADR-1371 made `fs_proc_depth` run a masked
material's program so a program-made cutout is cut in depth too. It ran the program whenever the material had one,
whether or not the program writes opacity. The canopy programs do not (`"opacity": -1`). On every leaf fragment of
the prepass, the depth entry therefore ran the whole interpreter (ADR-1493 compiles the lit pass only), to return the
material's own opacity unchanged.

The vertex stage paid for it as well. A masked depth draw asked for the normal (ADR-1492 left it on for the cutout
program), and the normal costs two more deformer-chain evaluations per vertex.

## Decision

A depth-only draw (the prepass and the shadow views) runs a material program only when:
- the material is masked, and
- the program writes opacity (its base names an opacity register in range; layers have no opacity output).

The CPU decides this per object and writes it to the draw's uniform, so the test is uniform across the draw. In
that case the vertex stage also computes the normal for the program, as before. Otherwise the depth entry tests the
material's alpha and texture alpha exactly as a masked material with no program does.

A program that writes no opacity returned `base.opacity` (the material's alpha). With a texture, that value was then
scaled back by the texture alpha over the material alpha. The cutoff therefore saw the same opacity: identically when
the material's alpha is 1, and otherwise to within a rounding of that division.

## Consequences

Measured on Rainbow World (headless High, GPU median, 14-20 s windows, two to three interleaved runs each, on AC):

| Window | Before | After | Depth pass |
|---|---|---|---|
| 140-160 s | 31.1-33.2 ms | 28.5-30.5 ms | 7.9-9.1 -> 3.7-4.3 ms |
| 185-205 s | 26.2-27.3 ms | 24.4-25.1 ms | 5.3-6.6 -> 3.0-3.2 ms |

- **Live**, 140-215 s with the auto ladder, two runs each: before, Ultra 413 / 0 frames, High 1196 / 1411, Medium
  590 / 517 and Low 0 / 269. After, Ultra 1367 / 1356, High 563 / 574, Medium 269 / 269 and Low 0.
- Any scene with masked foliage whose program only colours it gets the same saving, in the prepass and in the
  shadow passes.
- No image changes.
