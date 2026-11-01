# ADR-1933: CHRONOS's canyon is carved along an authored river

- Status: **Superseded** (2026-11-01). The owner pivoted CHRONOS to "The Devourer of Souls"
  (`docs/prototypes/chronos/10-devourer-brief.md`), and the canyon was removed outright under ADR-441 (no compat
  shims): the block, its generator, its tests and its scene files. Git history keeps it, at 313b2c47. Its findings
  stay true and are recorded here; the temporal engine it was built beside (ADR-1931, ADR-1932) carries on.
- Was: Accepted (CHRONOS Gate 1, 2026-10-31), scene/chronos.
- Code:
  - `src/scene/chronos_canyon.{hpp,cpp}`: `ChronosCanyonGen`, `generateChronosCanyon`, `acquireChronosCanyon` and the
    `chronosCanyon` block's JSON.
  - `src/scene/composition.{hpp,cpp}`: `setChronosCanyon`, the scene file key, and the flattening (terrain chunks and
    the river's water surface).
  - `examples/chronos/build_scene.py`, which writes `chronos.scene.json` and the projects. They stay out of
    `examples/index.json` until the owner approves.
- Tests: `[chronos-canyon][adr1933]` in `tests/unit/test_chronos_canyon.cpp`:
  - the canyon is deterministic, and the cache does not regenerate it;
  - the mesh is watertight and wound upward;
  - the river lies under water along its whole centreline, with walls rising beside it and spires above their ground;
  - JSON round-trips and unknown keys are refused.

## Context

The brief (`docs/prototypes/chronos/00-brief.md` §2) asks for a broad, winding canyon of layered sandstone, with a
river, distant ranges and foreground formations. It must feel handcrafted and must not copy Strange Attractor.
Strange Attractor's generator places geological masses on a seed and grows a straight trunk valley between them. Its
flight is derived afterwards, and its knobs are about its own layout.

## Decision

A new block, separate from `attractorWorld`, that shares Strange Attractor's techniques and none of its layout:

- **The river is authored:** Catmull-Rom control points in world xz. The canyon is carved along it, and the scene's
  camera spline is authored along the same centreline (the build script reproduces the engine's resampling). The two
  cannot disagree, and the composition is a choice rather than a seed's accident.
- **The cross-section is a function of the distance d to the river and the arc length s along it.**
  - The bed is a parabola below the water line, with sandy banks.
  - Above that is a stepped wall: a base profile (a steep inner gorge easing out to the rim), quantised into
    cliff-and-bench tiers whose boundaries wander with noise.
  - Alcoves push d in and out, so the walls have bays and buttresses.
  - The width swells and pinches along s.
  - Gullies are channels in (s, d) space running down the walls.
  - Above the rim are the plateau's relief and the far ranges (ridged noise, rising from 2.4 to 5.6 km from the
    river), which close the horizon so no world edge shows.
- **Spires** (hoodoos) are capped columns on the local ground, placed by the build script on the benches beside the
  flight.
- **The strata coordinate** is the height plus a seeded dip and a slow warp, in bands. It is carried in uv.x, with
  the floor level and the baked visibility in uv.y: Strange Attractor's `canyonRock` packing. The scene's walls use
  `chronosRock` (`examples/chronos/build_rock_program.py`), a copy of `canyonRock` kept as scene art, retuned to the
  CHRONOS palette with softer laminations. The full colour pass is Phase 5.
- **The mesh** is the shared RTIN (ADR-1930): the field is sampled as a cubic B-spline at 1.5 m, and the tolerance is
  0.003 per metre of distance from the river, with a 0.2 m floor. Normals come from the continuous surface. The baked
  horizon visibility replaces GTAO (ADR-1859's reason). The river is a flat water sheet cut from the same grid.
- **No live parameters.** The look lives in the material program, the lights and the sky, which already have their
  parameters. The canyon regenerates only when its structural hash changes.

## Consequences

- Generation is measured in `docs/prototypes/chronos/04-gate1.md`, along with the mesh's triangles and memory.
- The canyon has no overhangs: a height field cannot make an arch. The brief's suspended formations and arches are
  separate meshes, a later phase.
- Two small helpers are copies of Strange Attractor's (the cubic B-spline sampler and the horizon visibility). Lifting
  them into the shared module would touch `attractor_world.cpp` again while Art Pass 2 is editing it, so that waits.
