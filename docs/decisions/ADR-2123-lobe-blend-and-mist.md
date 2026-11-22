# ADR-2123: the cloud lobes' soft union and the mist between them

- Status: Accepted (2026-11-22), avagent/neon-dreams (Neon Dreams, iteration 13; owner msg 178 at 39.8 s: "clouds are
  looking a little too blobby still, need a bit more blending")
- Code: `Clouds::lobeBlend`, `lobeMist`, `lobeMistReach` (`src/scene/clouds.hpp`, `.cpp`: read, validated, written);
  `CloudRenderer` (`src/rendering/cloud_renderer.cpp`: lobes binned and boxed `1 + lobeMistReach` bigger when the mist
  is on, the inflation in `soft.w`; `erosion3.zw`); `lobeGrid` (`shaders/clouds.wgsl`).
- Tests: `tests/unit/test_clouds.cpp` (`[adr2123]`), `tests/rendering/test_clouds_it12_gpu.cpp` (`[adr2123]`: the mist
  fills the clear air between three heads, the heads stay heads, a pure function of the second, off is the old
  picture byte for byte, the soft union thickens where soft edges meet).

## Context

A lobe block's density at a point was the hard maximum of its lobes' shapes, and a lobe's shape ended at its
ellipsoid. Two neighbouring lobes therefore met in a crease and anything between them was clear air: each formation
read as a set of separate balls. Erosion (ADR-2106/2107) breaks a lobe's own surface but only inward, and cannot put
cloud where no lobe is.

## Decision

Two block values, both 0 (off) by default:

- `lobeBlend` (0..1) mixes the union from the hard max toward the soft sum `a + b - ab`. Where the soft edges of two
  lobes (or a lobe and the mist) overlap, the density adds up a little instead of the stronger simply winning.
- `lobeMist` (0..1) with `lobeMistReach` (0..2, a fraction of the radius): every lobe carries a halo from 0.7 of its
  radius out to `1 + reach`; the mist at a point is `lobeMist x (0.15 h1 + 0.85 sqrt(h1 h2))` of the two strongest
  halos there, broken into patches and threads by the coarse and mid erosion billows (`smoothstep(0.3, 0.75, ...)`).
  The geometric mean puts the mist mostly BETWEEN lobes, where two halos overlap, and only a trace round a lone one.
  The block's box and the columns' height ranges grow by the reach so the march finds it; `heroSpan` follows them.
  A cell lists the lobes whose BODIES cover it first (ADR-2114 still splits until they fit), then, in the room left,
  the lobes only a halo reaches (`binCloudLobes(..., grow)`), so the mist never pushes a body out of a cell: with the
  halos binned as bodies, Neon Dreams' it13 data overfilled 512 cells and the smallest lobes were dropped. The cap is
  128 a cell (was 64; the count is 8 bits); the halo entries a full cell cannot hold are counted (`halosDropped`, one
  info line) -- 0 in Neon Dreams at 128, 11,518 at 64.

## Consequences

- Off is the old picture exactly (the GPU test compares the bytes), so every other cloud scene is unchanged.
- With the mist on, the march finds density in more of the air round each formation: more samples are taken. Neon
  Dreams (offline, 0.15 / 0.5) carries it; a live preset should keep it off or small.
- The art's own blending (torn envelopes, halo and bridge lobes, Neon Dreams 660d699e) is data; this is the renderer
  side of the same request, and the two compose.
