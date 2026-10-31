# ADR-1887: A lunar curvature radius, for a terminator the camera can cross

- Status: Proposed (Art Pass 2, Gate R2), scene/strange-attractor.
- Code: `LunarGen::curvature`, the `lunarWorld` key `curvature` (`src/scene/lunar_world.*`).
- Tests: `[lunar-world][pass2]`.

## Context

Art Pass 2 asks the camera to travel from lunar daylight across the terminator into the night side, physically, not
by fading the light. The real Moon's curvature turns the sun by about 1° every 30 km, so a flight of a few kilometres
cannot cross a terminator.

## Decision

The lunar generator takes a `curvature` radius (default: the Moon's own, 1737.4 km; 1 km to 10 000 km). The ground
falls away as d² / 2R from the tile's centre. The surface's own normals then carry the sun's elevation down across the
route: the lit fraction falls as sin(elevation − d/R). Combined with a low sun, a tighter radius brings a true terminator
within reach. The default is bit-identical to Art Pass 1: the hash only takes the key when it differs.

## Consequences

- The Gate R2 proof crossed a terminator at R = 12 km. The horizon then sits under a kilometre away, which hides
  distant crater domes.
- The plan uses a moderate radius (about 150–200 km) with a designed shadow basin, plus earthshine on the night side
  (`33-pass2-plan.md` §2).
