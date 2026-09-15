# ADR-030: Procedural materials as an interpreted op program; a colour utility layer

- Status: Accepted (2026-09-09)
- Research: `docs/research/procedural-materials.md`

## Decision
- `scene::MaterialProgram` (data): ≤ 16 ops over a register file of 8 vec4 registers; inputs (world/local position, normal, uv, object id, instance index/id, instance attributes, time, audio vector, field slots, noise); ops (gradient, noise, voronoi, fresnel, ramp, remap, multiply, add, mix, power, smoothstep, threshold, hueShift, saturate, palette); outputs (baseColor, metallic, roughness, emission, opacity). Evaluated by `material.wgsl` (included by `pbr_shade.wgsl`) from a uniform op array; also evaluated on the CPU for tests (`evaluateMaterialProgram`).
- `Material` gains an optional program name; programs live in `Scene::materialPrograms`; parameters `material/<name>/op/<i>/<kind>/…` (positional until ADR-232, which put the op's kind in the path).
- `core/color.hpp` + `color.wgsl`: RGB↔HSV/HSL/OKLab/OKLCH, hue shift, saturation, value, contrast, cosine palettes, gradient ramps; used by ops, by material variation and by the UI.

## Consequences
- Positive: height/distance/normal/noise/audio/field-driven surfaces with one pipeline and hot uniform edits; deterministic.
- Negative: an interpreter costs a few ALU per op per fragment; no normal-map generation yet; textures as inputs come later.
