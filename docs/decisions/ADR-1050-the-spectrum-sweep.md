# ADR-1050: The spectrum sweep: a band of hue that travels across the frame

- Status: Accepted (2026-10-01), proto/liminal-space (All You Got art pass 2)
- `PostSettings::sweep*` and `post/sweep/*` in `src/scene/post_settings.*`; the composite's uniforms in
  `src/rendering/post_processor.cpp`; `applySweep` in `shaders/post.wgsl`.
- Tests: `tests/rendering/test_post_gpu.cpp` and `tests/unit/test_post_sweep.cpp` (`[adr1050]`).

## Context

The owner calls bridge 3 bar 8 (owner bar 90, beats 3-4, 198.651-199.732 s) "one of the strongest
transitions in the video": the drums pause and a bright synth sweep crosses beats 3-4, "a high-pass filter
sweep translated into colour and light". Nothing in the engine draws a travelling band of hue; the grade's
hue shift rotates the whole frame at once.

## Decision

The composite pass (HDR, after bloom and the grade, before the tone map) gains a sweep. A band of
half-width `width` (a fraction of the frame along the sweep) travels along `angle` as `progress` goes 0 to 1,
entering off the leading edge and leaving past the far one. Across the band the hue runs `span` cycles from
`hue`; the band tints what is under it by `wash` and adds `intensity` of light, and behind it leaves `trail`
of wash. All parameters (`post/sweep/{progress, width, intensity, wash, angle, span, hue, trail}`), keyed
on the timeline or routed from a beat-grid envelope; the scene's `post` block takes `sweepProgress` and so on.

## Consequences

- Off while intensity and wash are both 0 (the defaults): byte-identical (tested).
- A pure function of the frame's parameters, so it seeks like its keys and routes.
- It sits after bloom, so the band's own light does not bloom; key `post/bloom/intensity` with it, or use
  `wash` over emissive geometry, for a glow.
