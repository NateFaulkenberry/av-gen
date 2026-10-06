# ADR-1153: Particles can be glint flakes that reflect the bands

**Status:** Accepted (proto/astral-forge, the production path of THE ASTRAL FORGE, iteration 3, step 4)
**Date:** 2026-10-06
**Resolves:** "glint flakes in the particle renderer" (`docs/prototypes/astral-forge/06-iteration-2.md`,
recommended iteration 3 item 2), the prototype's defence against "glowing dots" (`02-visual-language.md`).
**Implemented by:** `ParticleShape::Flake`, `ParticleFlake` and the `flake/*` parameters in
`src/scene/particles.{hpp,cpp}`; `src/scene/particle_io.cpp` (`shape2d: "flake"`, the `flake` block);
`vs_flake` and the latent-normal store in `cs_latent` (`shaders/particles.wgsl`); `ParticleUniforms::flake0..3`
and the band lanes, the flake pipelines, `ParticleSnapshot::home` in `src/rendering/particle_renderer.{hpp,cpp}`;
`ParticleFrameContext::bands` from `src/rendering/scene_renderer.cpp`.
**Tests:** `tests/unit/test_astral_production_look.cpp` (`[adr1153]`: round trip, refusals by name, `flake`
only with `shape2d: "flake"`, parameters only for a flake system); `tests/rendering/test_astral_production_look_gpu.cpp`
("flakes: dark without bands, glinting with them; a bound flake stores the latent normal"),
`[gpu][particles][flake][adr1153]`; the costs in `[.perf][astral3]`.

## Context

The prototype draws every particle as an oriented metal flake (`prototypes/astral-forge/shaders/flakes.wgsl`):
dark unless its normal reflects a band toward the eye (`envF`), so most of the dust is invisible at any instant
and it glitters. Bound flakes take the latent surface's normal, so a formed patch flashes as one plate; as the
form sharpens they fuse into the surface. Production particles are emissive billboards: iteration 2's example
drew 400 k bright dots, which is exactly the "glowing dots" the brief bans, and, half buried in the surface they
form, those dots z-fought it along its depth contours (rings on every rounded form, ADR-1150).

## Decision

**`"shape2d": "flake"`** draws a system as flakes, with an optional block (every key defaults as shown):

```json
"flake": {"metal": [0.5, 0.51, 0.54], "temper": 0, "filmIor": 2.4, "glint": 0.012, "tumble": 0.4,
          "free": 0.2, "bound": 0.85, "latentNormal": 1, "sparkle": 0.008, "sparkleGain": 0.6, "fuse": 0}
```

`flake` is refused on a system that is not a flake system, and unknown keys and out-of-range values are refused
by name.

**`vs_flake`, its own entry point and pipelines**, so `vs_particle` -- every other system's vertex code -- is not
touched. Per particle:

- a free normal tumbling at `tumble` rad/s from the particle's own hash; for a system with a latent (ADR-1140)
  the binding `b` from the system's own curve, and the stored latent normal mixed in by
  `smoothstep(0.2, 0.8, b) x latentNormal`; flipped to face the eye;
- its colour is `reflectionBands(reflect(-V, n), glint, true)` (ADR-1151) times a Schlick Fresnel on `metal`
  tinted by the prototype's 8-wavelength temper film of `temper x b x (0.8 + 0.4 h)` nm (cold dust is bare
  steel), times `free + bound x b`, times `1 - fuse x smoothstep(0.6, 1, b)` (fused matter thins to a residual
  sparkle); a fraction `sparkle` are hot cores of radiance `sparkleGain` that always shine;
- the quad stays camera-facing (a plate is a pixel or two; only its shading is oriented), lifted toward the eye
  by `0.01 x distance + size` so a plate resting on the surface it forms is never half buried.
- a plate nearer the lens than its focus is capped at 1.2 milliradians of size (the prototype's 2.5 px at 1080
  lines and a 30-degree lens) and fades by the area it lost, so a close-up is not a snowstorm of bokeh discs.

The particle's colour curve is not used; its alpha curve, size curve, blend mode and the fragment stage are the
system's own. A flake system with no bands in the scene is black.

**The latent normal.** `cs_latent` already computes the SDF's normal at each bound particle. For a flake system
(and only one that is not scatter-anchored) it now stores it in the record's last lane, `home`, with `w` = 0: the
simulate step reads `home` only when `w` > 0.5 (anchored systems), so this is "no home" to it. The pool record
stays 64 bytes. Other systems' pools, latent ones included, are untouched (`latentInfo.y` = 0).

**The bands** reach the particle uniforms as a copy of the frame lanes (`ParticleFrameContext::bands`), as the
wind does (ADR-370): the particle pipelines bind no frame block.

**Parameters**, registered only for a flake system: `particles/<n>/flake/{temper, glint, tumble, free, bound,
sparkle, fuse}`, all uniforms.

## Consequences

- No other system changes: a round or leaf system draws with `vs_particle` and its uniforms' new lanes stay
  zero; `stellar-nursery.json` and `particle-vfx-lab.scene.json` render with 0 differing channels (ADR-1150's
  reference list), and ADR-1140's "zero-strength latent changes no byte" test still passes.
- The GPU test: 16 k flakes on a sphere are entirely black without bands (0 pixels above 20) and glint with them;
  every bound particle's stored normal is the sphere's (dot > 0.95).
- What is not ported: the prototype's software rasteriser (an order-independent u32 splat whose dark flake
  bodies occlude the surface by coverage), its motion streaks, heat sparks (production particles carry no heat,
  ADR-1141), the per-role brightness, and iteration 2's shards (near flakes drawn as lit geometry). Production
  flakes are additive or alpha billboards: a dark flake in an additive system adds nothing rather than occluding.

## Measured cost

M2 Max, 1920x1080, under the GPU lock, `avgen_render_tests "[.perf][astral3]"`, p50 of frames 90..149:

| Arm | SDF raymarch pass | GPU frame |
|---|---|---|
| metal SDF sphere (~40% of the frame), one directional light, no bands | 7.47 ms | 9.96 ms |
| + bands (2 strips + soft box), ADR-1151 | 8.26 ms (+0.79) | 10.88 ms |
| + engraving (rosette + engine), ADR-1152 | 9.31 ms (+1.05) | 11.80 ms |
| + grating | 9.24 ms (+0) | 11.80 ms |
| 1 M round billboards (bands on) | | 13.11 ms |
| 1 M flakes (bands on), ADR-1153 | | 14.09 ms (+0.98) |
| frame-filling density sphere, occupancy skipping off / on (ADR-1150) | 30.0 / 13.2 ms | 50.5 / 23.2 ms |

These were taken while other agents' jobs queued for the same GPU; a single pass, not interleaved pairs, so
differences under ~0.5 ms are not resolved.

## Rejected alternatives

- **A uniform branch in `vs_particle`.** Every particle system's vertex code would change for a feature most
  do not use (ADR-388's drift).
- **The prototype's compute splat.** It is a second particle renderer (its own accumulation buffer, combine and
  coverage model). Billboards through the existing pipelines were enough to make the dust dark-unless-glint.
- **Recomputing the latent normal in the vertex stage.** Four SDF evaluations per particle per frame again, and
  the render layout has no SDF program bound.

## Revisit triggers

- Dark flakes should occlude what is behind them in an additive scene: the prototype's coverage model.
- Shards: near bound flakes as lit geometry (iteration 2's step 5).
- Heat: a second particle lane (sparks in the grooves).
