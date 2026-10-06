# ADR-1148: Collapse heat lives on a release front

**Status:** Accepted (proto/astral-forge, the production path of THE ASTRAL FORGE, iteration 4, step 5)
**Date:** 2026-10-06
**Resolves:** "heat" in `docs/prototypes/astral-forge/07-iteration-3.md` §1 ("what it still lacks") and §4 (the
prototype's iteration-3 release front); ADR-1153's "heat sparks (production particles carry no heat)".
**Implemented by:** `ParticleHeat` and `particles/<n>/latent/heat/{inject, speed}` in `src/scene/particles.{hpp,cpp}`;
the `latent.heat` block in `src/scene/particle_io.cpp`; `cs_heat`, `heatColor` and the sparks in `vs_flake` /
`fs_shard` (`shaders/particles.wgsl`); the front's start and `ParticleUniforms::heat0..2` in
`src/rendering/particle_renderer.{hpp,cpp}`; `ParticleSnapshot::trail`.
**Tests:** `tests/unit/test_astral_port.cpp` (`[heat]`: round trip, refusals by name, refused with trails, the
parameters); `tests/rendering/test_astral_port_gpu.cpp` ("heat: a coherence drop heats what the front passes, not
what it does not reach, and the heat decays").

## Context

The prototype's collapse heats only the matter a shell passes as it expands from the mouth at about 30 units per
second: everything released elsewhere flies cold, so a collapse is a grey-steel spray with sparse warm sparks near
the mouth and not an orange cloud (iteration 3, §4). Production particles had no heat, and the record had no lane
for it.

## Decision

**A latent may carry `heat`:**

```json
"heat": {"origin": [0, -1.45, 0.62], "speed": 30, "width": 0.9, "inject": 0.8, "decay": 3, "fraction": 0.045, "gain": 1.4}
```

`origin`, `speed` and `width` are in the latent object's local units (its transform carries them).

**The front** sets out on the step the coherence starts to fall (the renderer remembers when, per pool; a rise
clears it, a reset forgets it): its radius is `speed x seconds since`. `cs_heat`, dispatched after the latent force,
adds `release x inject x (0.5 + h) x exp(-((|p - o| - r) / width)^2)` to each particle's heat, `release` being
ADR-1140's unbinding this step, and multiplies all heat by `exp(-decay dt)`. **The heat lives in the record's
`trailWrites` lane**, which nothing reads unless the system has trails, so a heated system with trails is refused
by name.

**Sparks.** A flake system shows heat as emission: a `fraction` of its plates (by their own hash) add
`heatColor(heat) x gain` (the prototype's ramp: dull red, orange, gold); a shard (ADR-1147) carries its plate's heat
in its film (+160 nm at heat 1) and sparks the same way.

## Consequences

- Without the block nothing is dispatched and `vs_flake`'s spark term is behind a uniform that is 0.
- The heat does not reach the density volume (the prototype also put it in the surface's grooves through a second
  density channel; ADR-1141's volume has one), so a collapse's heat is in the flying matter only.
- [TBD measured cost]
