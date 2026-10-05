# ADR-1118: Bake to points is the escape hatch

- Status: Accepted (gpu/productionization)
- Builds on ADR-1117 (generators), ADR-048 (scatter).

## Problem

A generated world has no CPU records. That makes three things impossible: editing one element by hand,
tracing it in the CPU path tracer, and feeding CPU consumers of records (navigation, ecology lights).
Production had no serialisable list of explicit placements: a `Scatter` cloud is supplied at resolve time
and never saved.

## Decision

1. **`DistributionKind::Points`** (appended): explicit placements, serialised as
   `"points": [[px, py, pz, qx, qy, qz, qw, sx, sy, sz], ...]`, at most 65,536 (`kMaxBakedPoints`). The
   list is shared by pointer, so the per-frame parameter pass copies a pointer, not the list. Its hash is
   computed once, when the list is set.
2. **`Composition::bakeGeneratorToPoints(node, centre, halfSide)`** writes the generator's elements in
   that square, through the CPU mirror (exact identity), into a new node `<node>-baked`. The new node has
   the same source, material, transform and effectors. The generator node is hidden, so the region is
   not drawn twice. The World inspector's "Bake to points" button calls it, centred on the picked element
   or the camera.
3. A baked node is an ordinary procedural with CPU records: it can be hand-edited, path-traced and read
   by every CPU consumer.

## Consequences

- The bake preserves placement (position, rotation, size). It does **not** preserve the random lanes:
  a points object's randoms come from its own seed and index, so an Element-band audio field picks
  different bands after a bake, and the value and emission variation re-roll.
- 65,536 placements is about 3.5 MB of JSON. A bigger region needs several bakes, or stays a generator.

## Rejected alternatives

- **Making `Scatter` serialisable.** Rejected: it is defined as supplied at resolve time by whatever knows
  more than the object (an ecology pass). Saving it would make two sources of truth.
- **A sidecar binary file.** Deferred: the JSON form is enough at this cap, and it diffs.
