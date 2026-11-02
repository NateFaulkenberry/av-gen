# ADR-1897: Pads for the attractor terrain

- Status: Proposed (Art Pass 2, M3b), scene/strange-attractor.
- Code: `AttractorGen::Pad`, `AttractorGen::pads` and the `pads` JSON key (`src/scene/attractor_world.*`).
- Tests: `[attractor-world][pass2][adr1897]`.

## Context

The owner's M3 notes asked to open up the Sasquatch area and to fix the terraced banding at the camp. The camp stands
on a narrow bench of the canyon's stepped terrain: the steps run under the gathering and under the parked craft, and a
low, close camera reads them as bands. ADR-1896 gave the lunar generator pads (a level footing for the pyramids); the
canyon generator had nothing like it.

## Decision

`attractorWorld.pads`: a list of `{x, z, radius, level}` in world metres. After the height field is built (and the
flight is planned over it), and before the mesh is made, every texel within a pad's radius is eased to the pad's
level: fully within two-thirds of the radius, by a smoothstep over the last third. Pads are part of the generator's
hash (a change rebuilds the world) and are saved and loaded.

## Consequences

- The camp's bench is one footing, 14 m round, wide enough for the gathering, the craft and the cave mouth behind;
  the river and the walls round it are untouched.
- The flight is planned before the pads, so a pad never moves the authored flythrough.
- A pad is a world-metre feature: the canyon scale POC (`SA_WORLD_SCALE=0.2`) keeps it at character size.
