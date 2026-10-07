# ADR-1146: A latent can bind matter to authored tendon curves, streaming along them

**Status:** Accepted (proto/astral-forge, the production path of THE ASTRAL FORGE, iteration 4, step 3)
**Date:** 2026-10-06
**Resolves:** "tendons ... in the particle renderer" (`docs/prototypes/astral-forge/07-iteration-3.md`, retirement
item 3): the prototype's meso scale, `sim.wgsl` role 6, which production did not have.
**Implemented by:** `ParticleTendons` and `particles/<n>/latent/tendons/speed` in `src/scene/particles.{hpp,cpp}`;
the `latent.tendons` block in `src/scene/particle_io.cpp`; `cs_tendon` and `tendonPoint` in
`shaders/particles.wgsl`; the curves' packing after the latent program, `ParticleUniforms::tendon0/tendonInfo` and
the dispatch in `src/rendering/particle_renderer.{hpp,cpp}`; the face curves in `tools/astral_face.py curves`;
the `tendons` system of `examples/astral-forge/compare-t01-face.scene.json`.
**Tests:** `tests/unit/test_astral_port.cpp` (`[tendons]`: round trip, refusals by name, the parameter);
`tests/rendering/test_astral_port_gpu.cpp` ("tendons: bound matter rides the authored curves and streams along
them; coherence 0 does not"); the cost in `[.perf][astral4]`.

## Context

In the prototype 16% of the matter is tendon matter: each particle is attracted to one skeleton curve (brows,
cheek and nasolabial lines, the jaw arc, the forehead midline, temples, and eight outer curls leaving the rim
backward), streams along it, sprays off its end into the field and re-joins another curve. It is what makes the
face read as built from flowing matter at the scale between the surface and the dust. It keeps per-particle state
the production record has no room for: a stored target, a streaming direction and a generation counter (the pool
record is 64 bytes and full, ADR-015/ADR-1140).

## Decision

**A latent may carry `tendons`** (every key but `curves` defaults as shown):

```json
"latent": {"sdf": "latent", "coherence": 1.0,
           "tendons": {"curves": [[[x, y, z], ...], ...], "speed": 0.6, "stiffness": 40, "spray": 3.0, "ramp": 0.12}}
```

The curves are point lists (2..64 points, at most 32 curves) in the latent SDF object's local space; its transform
carries them. With `tendons` present the system's force is `cs_tendon` instead of `cs_latent` (the tree is not
read; the object is still required, for its frame).

**Stateless streaming.** Each particle's curve parameter is `u = fract(phase + rate t)` with its own phase
(`fract(seed 7.31 + 0.137)`) and `rate = speed / the curves' mean length`; its curve is a hash of (particle,
generation `floor(phase + rate t)`). The spring (stiffness x the latent's `strength`, the same 0.8 / dt^2 cap and
0.55 damping ratio as ADR-1140) pulls it to the point `curve(u)` and damps its velocity toward that point's own
(`d curve / du x rate`), so bound matter travels with the curve. A fresh generation's binding ramps in over `ramp`
of `u`, so matter flies in from wherever its last curve left it; crossing `u = 0.97` adds `spray` along the curve
plus a hashed jitter: the curve's end sprays matter into the field. The release impulse of a coherence drop
throws it away from its curve. The coherence binding curve is ADR-1140's. Everything is a pure function of the
particle, the uniforms and time, so it needs no state and determinism is unchanged.

**Curves on the GPU** ride after the latent program in the pool's latent buffer (binding 10, which has no
neighbour to spare): each curve resampled on the CPU to 16 points by arc length, one record per point. A curve
lookup is one lerp between two records.

**The face's curves** are the prototype's `faceCurve`, sampled at 13 points by `tools/astral_face.py curves`
(the outer curls' time term is dropped: authored curves are static). They are bound by a second particle system
(`tendons`, 320 k flakes, the prototype's 16% of 2 M), as ADR-1140 intends roles to be: systems, not hashes.

## Consequences

- No other system changes: `cs_tendon` is a new entry dispatched only for a tendon latent.
- Differences from the prototype, by construction: the particle follows a moving point rather than projecting
  onto the nearest point and streaming from there (no stored target), the curves do not curl with time, and
  tendon matter does not splat into the surface's density (it is a separate system; the prototype weighted it 0.22,
  below the iso level, so in both it reads as streams of flakes rather than surface).
- Measured: 18 face curves for 1 M particles, the particle compute pass 0.79-1.7 ms against the latent sphere
  control's 1.1-1.6 ms: following a curve costs no more than a one-record latent. The T01 scene's 320 k tendon
  flakes cost 7.6 ms of the formed-face frame (110.5 against 102.9 ms GPU p50 without the system, 1440x900),
  almost all of it drawing the flakes (`scene` pass +6.9 ms).
