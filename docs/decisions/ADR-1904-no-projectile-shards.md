# ADR-1904: No projectile shards

- Status: Proposed (Art Pass 2, M3e), scene/strange-attractor.
- Amends: ADR-1872 (the mesh wire's shards) and ADR-1901 (Gate A's storm).
- Code: `vs_mesh_shard` in `shaders/mesh_wire.wgsl`.

## Context

The owner saw long, thin dark rods shoot across the frame from the pyramid portal's burst (M3e notes §2). A shard is a
source triangle of the target mesh, broken off whole. Most source triangles are rock-sized, but the pyramid's sand
drift and the ocean base's box faces are not. A drift triangle is a sliver many metres long, and a hull face is a large
triangle. Once broken off and turned, either one crosses the frame as a rod or a slab.

## Decision

1. **A sliver never breaks off.** A triangle whose longest edge is more than six times its height on that edge
   (longest² > 6 × twice its area) is skipped. A degenerate (zero-area) triangle is skipped too.
2. **A shard's size is capped.** Its longest edge is scaled to at most 15% of the opening's half-width (and never under
   1 m). Large triangles therefore join the storm at its own size.

## Consequences

- No scene changes. A storm on fine geometry, such as the Moon's regolith or the canyon's rock, is unchanged, because
  its triangles are already under the cap.
- The cap is a share of the opening, so a larger opening keeps proportionally larger shards.
