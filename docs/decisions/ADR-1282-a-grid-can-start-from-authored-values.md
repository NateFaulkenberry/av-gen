# ADR-1282: A grid can start from authored values

- Status: Accepted (2026-10-08), proto/bio-valley (the Bioluminescent Valley, CP4)
- Code: `src/spatial/grid_field.{hpp,cpp}` (`GridField::initial`, JSON `"initial"`).
- Tests: `tests/unit/test_grid_field.cpp` (`[adr1282]`).

## Context

The valley's mist must glow where living light pools (spec §11, §15), and the ground, rock and crowns must catch a
faint pool of it. That needs a map of where the light is. The light comes from millions of placed organisms, known
only to the scene generator. No field kind could carry a 2D map authored outside the engine. The closest options
were noise (unrelated to where anything grows) and a simulated grid (which starts at zero, or at noise).

## Decision

A scalar or vector grid may carry `"initial"`: `components()` floats per cell, in the grid's storage order.
`reset()` loads them instead of zero plus `seedAmount` noise.

A grid with no injection, diffusion or dissipation keeps them for ever. It is then a static, authored field, read
like any other through a `Grid` field: the mist's `volumeColorField` (via a compound with a height mask) and the
material programs' `field` op.

`initial` is validated (count, finite values, scalar or vector mode only), written to JSON only when present, and
hashed only when present, so every existing grid keeps its hash.

## Alternatives

- **A heightmap image as a field.** It needs image decoding and a texture binding in the field block, and a
  128 x 128 map is 16 K floats of JSON, which is acceptable for a generated scene.
- **Point lights at the clusters.** These are the Rift's 22 ms and its 224-light cap (ADR-1200).

## Consequences

- Seek equals play trivially: the values never change.
- The values travel in the scene file. Three 128² maps add about 0.4 MB to the valley's generated scene, which is not
  committed.
