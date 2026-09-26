# ADR-904: An instance's emission variation is applied once, as a rotation of the hue it shows

**Status:** Accepted
**Date:** 2026-09-26
**Follows:** ADR-054 (colour that clusters: the per-instance multiplier), ADR-057 (the living
chromatic field), ADR-179 (a program that asserts emission owns it)
**Found by:** the GV3 revision's audit of the valley's small mushrooms (`mushrooms-wind.md` §1): the
multiplier `m` was applied twice, and its hue offsets did not rotate the colour on screen.
**Implemented by:** `MaterialProgram::writesEmission` / `emissionReadsInstance` and the packed
`MaterialProgramGpu::flags` (`src/scene/material_program.*`, `shaders/material.wgsl`);
`emissionVariationOf` (`shaders/chroma.wgsl`, CPU reference `color::emissionVariationOf` in
`src/core/color.cpp`); `vs_proc`/`fs_proc` (`shaders/procedural.wgsl`); the emission-lane block of
`shadeSurface` (`shaders/pbr_shade.wgsl`); `ProceduralUniforms::chroma.w`
(`src/rendering/procedural_renderer.cpp`); the `materialEmission` input. The library programs
`glowmere-tissue`, `frond-glow`, `bush-glow`, `canopy-fireflies` and `pine-fireflies`, and the
inline `glowmereTissue` in `glowmere-valley-2-song.scene.json`.
**Tests:** `tests/rendering/test_emission_lanes_gpu.cpp` "A procedural instance's variation is
applied once, as a rotation of the hue it shows"; `tests/rendering/test_material_gpu.cpp` (the new
input's CPU/GPU parity); `tests/unit/test_emission_lanes.cpp` "A program says whether it writes
emission and whether its emission reads the instance's", "materialEmission is the material's own
emission as the instance shows it", "The library's scatter programs leave the instance's variation
to the engine", "An instance's baked multiplier reads back as the rotation and gain it was made
from".

## Context

A procedural instance carries its emission variation -- `hueField`, `hueRandom`, `emissiveRandom`,
sparsity and, per vertex, ADR-057's living chroma -- as a per-channel multiplier `m` of the
**material's** emissive colour: `m = rotate(base, h) * g / base` (ADR-054). With no program that is
exact: `base * m` is the rotated colour times the gain.

A program that writes emission owns its colour (ADR-179). The Glowmere scatter programs read
`instanceEmissive` (which is `m`), multiplied it into a colour of their own, and the shader then
multiplied the program's output by `m` again (`emissiveMul` in `fs_proc`). Two consequences:

- **Brightness went as m squared.** A spread of 0.35-1.65x became 0.12-2.7x.
- **The hue went wherever the ratios pushed it.** `m` was made against the layer's authored colour
  (GV3's fungi: purple, 0.341 0.082 1.0) and applied to `glowmereTissue`'s cyan-green. The audit's
  replica measured the displayed hue going 145 -> 166 degrees -> almost grey (chroma 0.02 at +0.08
  turns) -> 20 degrees pink, not a rotation. Retuning `hueField` or `chromaDrift` did nothing
  predictable.

Five library programs had the shape (`instanceEmissive x constant x mask`): `glowmereTissue` (the
valley's fungi, shelf fungi and beacons in GV2, GV3, `glowmere-stylized`, the labs),
`frondGlow`, `bushGlow`, `canopyFireflies` and `pineFireflies` (`terrain`, `_tier1`, `_tier1big`).

## Decision

**The variation is applied exactly once, by whichever side owns the colour:**

| The surface | The variation |
|---|---|
| no program, or a program that keeps the material's emission | the per-channel multiplier on the material's emission, as before |
| a program that writes emission and does not read the instance's | read back as (hue turns, gain) and applied **after** the program as a rotation of the displayed hue (OKLCH, lightness and chroma kept) and a gain |
| a program whose emission reads `instanceEmissive` or `materialEmission` | nothing: the program has applied it |

- **Reading it back** is exact, not a guess: an OKLCH rotation keeps L and C, and a gain g scales
  OKLab L by cbrt(g), so `g = (L(base m) / L(base))^3` and `h = hue(base m) - hue(base)`. The vertex
  stage does it once per vertex (constant across an instance, a flat varying), only for draws the
  CPU flags with `ProceduralUniforms::chroma.w`; ADR-057's drift turns are added to `h`. An
  achromatic material colour carried no hue in `m` (rotating grey does nothing), so none is read.
- **"Reads the instance's" is a data-flow fact,** computed at pack time over the ops the interpreter
  actually runs (enabled ops, the shared budget, every enabled layer): an emission output register
  that depends on either input. A program that reads `instanceEmissive` into its base colour, or
  overwrites the register before emitting, does not own the variation. The two facts ride in a new
  header vec4 (`flags`); the program header grew from 80 to 96 bytes (the block to 45,840).
- **`materialEmission` is a new input** (appended to the enum): the material's emission as the
  instance shows it with no program -- colour x intensity x `m`. It lets a program shape the
  material's own colour (mask it, ramp it) instead of asserting one. Reading it counts as reading
  `instanceEmissive`.
- **The library's scatter programs let the engine apply it.** In each, the op
  `input instanceEmissive` is replaced *in place* by `constant [1, 1, 1, 1]`, so every other op keeps
  its index and kind and so its parameter path (ADR-232). Only that op's six paths change kind,
  `op/<i>/input/...` to `op/<i>/constant/...`; a project that saved values there has them dropped with
  a warning at load, and the file's value stands. GV2-song's inline `glowmereTissue` is changed the
  same way. The heroes' `glowmere2TissueWarm`/`Cool` keep reading it: a single placement's variation
  is the identity, and their op paths are what the Glowmere projects' saved values are keyed by.

**Why not make `glowmereTissue` read `materialEmission`,** which the audit suggested: that makes
the layer's *authored* emissive colour the displayed one -- GV3's fungi turn purple against the art
direction's navy and teal (02-art-direction §2.3), and every hero part that zeroed its material
emission per ADR-179 goes dark. Keeping the program's colour and rotating it keeps every approved
palette and still makes a hue offset mean a hue offset.

## Consequences

@@MEASUREMENTS@@

- **Every scene with a program-lit scatter layer changes:** the brightness spread of its instances
  narrows from g squared to g, and their hue variation becomes a rotation of the program's colour by
  the authored offsets. For the valley's fungi that is teal +/- the layer's `hueField` (0.16 turns in
  GV3) instead of green, grey and pink. Affected: the GV2 family (multicam, song, atmospherics,
  `ufo-stack`, the `_diag-water-*` and `_pre-defects` probes), GV3, `glowmere-stylized` and
  `glowmere-lyrics`, the foot-IK and motion-matching labs, and (through `frondGlow`, `bushGlow` and
  the fireflies) `terrain`, `_tier1` and `_tier1big`.
- **Program-less layers do not change** (GV3's flowers, for one): the multiplier path is byte for
  byte what it was.
- **Program-lit layers that never read the instance** (the tree fireflies, `glowmereFirefliesCrown`
  and `...Scaled`) had `m` once already; their hue offsets now rotate the program's colour instead of
  multiplying it by a ratio. Where the program's colour is near the layer's -- GV2's and GV3's warm
  fireflies on a warm (1.0, 0.82, 0.45) layer, offsets under 0.09 turns -- the two nearly agree.
- **GV3's project** saved values on `material/glowmereTissue/op/9/input/*`; they are dropped with a
  warning at load until the project is re-saved.
- **Left as it was:** the ecology light still takes its colour from the layer's `emissiveColor`, so
  a program-lit layer still casts a colour it does not show (GV3's fungi cast purple). Setting the
  layer's `emissiveColor` to the program's colour removes the mismatch without touching anything
  else, because the program ignores it.
- **An effector that recolours instances** (`color`/`emission` effectors write `m`) is read back as
  hue and gain too; a chroma change it makes is not carried onto a program's colour.
