# ADR-1955: The ground seam, so a body can stand on a tiles ground

- Status: Accepted (Musical Geography Phase 6, 2026-11-09), scene/musical-geography.
- Code: `world::GroundSource`, `world::GroundSample` (`src/world/ground_source.hpp`); `scene::TileGround`
  (`src/scene/tile_ground.*`); `BehaviorContext::ground`, `EntityWorld::setGround`;
  `Composition::refreshTileGround`, `Composition::liveTileDynamics`.

## Context

The behaviours stand on a `WorldMap` through `Navigator` and `TerrainQuery`. A tiles ground is not a WorldMap. It is
a lattice whose tops move with time (ADR-1953) and spring under a landing (ADR-1954).

## Decision

A small interface. `at(x, z, t)` returns the height, water, flat (crown) and path of the cell under a point at
timeline second t. `pitch()` and `cellCentre()` describe the lattice. `landed(xz, t, strength)` reports a landing;
the source must ignore a landing it has already been told about, so a seek's replay does not spring twice.

`TileGround` implements it over `tileSampleMoving` at the cell centre, in world space (the tiles' transform is a
translation). It keeps its own landings and gives them to its dynamics as springs, so a body can stand on a tile that
another body's landing set ringing.

The composition chooses the ground node the same way the flight does (`flight.ground`, else the first tiles
generator). Before each entity step it refreshes the TileGround with the live dynamics (the authored block, then its
`env/tiles/*` parameters and live rings), which is what the renderer draws. Each frame it copies the landings into
`environment.tileDynamics.springs`, in generator space. A seek forgets landings after the target time, and the replay
adds back the ones a play would have made.

The existing path is untouched. `BehaviorContext::ground` is null unless the scene has a tiles ground and entities.
No existing behaviour reads it.

## Consequences

- Tests:
  - TileGround equals `tileSampleMoving` at the cell centre with an offset.
  - A landing is taken once, deepens the tile, and is forgotten on a seek back.
  - In a composition, landings reach `environment.tileDynamics.springs`.
  - Standing bodies sit on the ground.
  - A cold seek to 14 s lands where a play does, and a scrub back to 6 s matches a play to 6 s.
- `sizeof(EntityWorld)` grows to 2088 (`ground_`, configuration).
