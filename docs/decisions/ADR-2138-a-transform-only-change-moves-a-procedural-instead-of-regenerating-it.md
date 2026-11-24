# ADR-2138: a transform-only change moves a procedural's records instead of regenerating them

- Status: Accepted (2026-11-24), eng/proc-transform (Neon Dreams editor slowdown, fix 4 of the owner-approved four;
  investigation `~/Desktop/av-gen-review/112-neon-dreams/02-ui-perf-investigation/REPORT.md`)
- Code: `scene/procedural.{hpp,cpp}` (`ProceduralGeometry::TransformReuse`, `transformInvariant`,
  `structuralHash(bool)`, `generateCloud`'s `locals`, `rebuild`, file-local `reuseForTransform` / `InstanceBounds` /
  `positionEffectorPadding`, `proceduralTransformReuseCount`), `scene/hierarchy.cpp` (`contextualHash(ctx, bool)`)
- Tests: `tests/unit/test_procedural_transform_reuse.cpp` (`[adr2138]`)

## Context

Neon Dreams has 873 procedural nodes (912 objects with their material parts) whose buildings ride bobbing cloud
islands (ADR-2112). The bob moves the node, the node's transform folds into `distributionTransform`, and that is
part of `structuralHash`. So every bobbing object regenerated its whole cloud every frame: "# procedural regen"
~875 per frame in the investigation, ~21 ms CPU, plus the re-upload.

Most of the 21 ms is not the instances (a kit node has a handful of points). It is the source: a Mesh source's
`sourceHalfExtent` and `sourceBox` walk every vertex of the imported mesh, three passes per rebuild, and rebuild
allocates a fresh `PointCloud` with every attribute column. None of that depends on where the object stands.

## Alternatives considered

1. **Re-transform the existing records on the CPU** from a cached pre-transform copy (chosen).
2. **Carry `distributionTransform` to the renderer as a per-object matrix** (the renderer already has
   `objectMatrices`). Rejected: the GPU would compute `model * record` where an offline render today composes the
   transform on the CPU (`compose`, which for a non-uniform scale goes through a matrix decomposition). Different
   operations in a different order on different hardware: the positions would move in the last bits and offline
   renders would stop being bit-identical. It would also touch the cull, LOD, shadow, simulation and path-trace
   paths that read records as world space. The 2-3 day version for a gain over (1) of the ~3 ms upload only.
3. **Invert the old transform and apply the new one to the records** (`D' * D^-1 * record`). Rejected: not exact.

## Decision

1. **Which kinds qualify (`transformInvariant`).** A flat generation composes `distributionTransform` last onto
   every placement (`compose(D, compose(placement, variation))`) and, audited, nothing else in the flat path reads
   it except two things. So the fast path is open to every non-generator, non-hierarchical distribution --
   Single, Linear, Grid, Radial, Spiral, Spline, Grammar, Scatter, Points -- and closed to:
   - a **hue field** (`materialVariation.hueField != 0`): noise sampled at the world position;
   - **any pointOps**: ops may sample fields and noise at the world position (not audited op by op; closed whole);
   - **self-recursion and Procedural sources**: hierarchy.cpp's own pipeline, and a referenced object's transform
     changes the child's shape;
   - **generators**: records are on the GPU; rebuild only recomputes the region bounds.
   Those regenerate on a move exactly as before. LOD is not a factor (only `lodCount` is structural, the rest are
   per-frame uniforms over the records); seeds are by index, never by position; spline curves are in the context
   hash and are not moved by this object's transform.
2. **What a move reuses.** `contextualHash(ctx, false)` is the contextual hash without this object's own
   `distributionTransform` (the shape). When `rebuild` finds the full hash changed but the shape not, it serves the
   change from `transformReuse`: for each instance `compose(distributionTransform, locals[i])` -- the very expression
   `generateCloud` evaluates, on the same operands -- written into the record's position, rotation and scale (and
   the kept cloud's columns), and the bounds recomputed through the same `InstanceBounds::add` the regenerating path
   now uses. Every other record column is a function of the shape. Counted by `proceduralTransformReuseCount`, not
   `proceduralRebuildCount` (the profiler's "# procedural regen" drops to 0); `structureVersion` still moves, so the
   renderer re-uploads the records.
3. **What is checked before reusing.** Inputs to the records that no hash covers are compared: the imported mesh,
   the scatter cloud and the points list by pointer (held, so an address cannot be reused), and the material's base
   and emissive colours by value (the hue multipliers are made from them; neither is structural). And the records
   must be the ones rebuild left (`version == structureVersion`): a floating layer writes records and bumps the
   version itself. Any mismatch falls back to a full regeneration.
4. **When it is captured.** Only on the second generation of one shape -- the first transform-only change -- so an
   object that never moves never holds the cache. The first move regenerates (and captures); every later move
   reuses.

## Bit-identity

The reuse evaluates `compose(D, local)` with `local` exactly the value the regenerating path computed (the inner
`compose` result is now named rather than a temporary; same value), so positions, rotations and scales are the same
floats. The bounds loop moved into `InstanceBounds::add` unchanged, expression for expression (default
`-ffp-contract=on`: contraction is per source expression, so a function boundary changes nothing). Proven three
ways: every `[adr2138]` case memcmp's the moved object's records, bounds, tight bounds and cloud hash against a
fresh generation at the same transform, through uniform and non-uniform scales; headless offline renders of Neon
Dreams, Talking Heads, Organic Garden and Glowmere Lyrics are byte-identical before and after (PNG md5 and sequence
hash; evidence in `03-engine-perf-fixes/proc-transform/`).

## Consequences

- Neon Dreams: regenerations per frame 912 -> 0 (912 reuses). Measured saving: see the evidence README (CPU only;
  the ~3 ms record re-upload remains).
- Talking Heads 50, Organic Garden 15, Cel Dream 12, Glowmere Lyrics 3 of 4 per frame move instead of regenerating;
  their sources are primitives, so the time saved there is small.
- Memory: a moving object holds one `Transform` (40 bytes) per instance.
- An editor object with expensive regeneration that is merely moved no longer waits in the editor's
  interactive rebuild deferral (`Composition::rebuildProcedurals`' budget): its rebuild is cheap, so it follows
  every frame. Offline has no budget and is unaffected.

## Revisit triggers

- A new input to `generateCloud` or `rebuild` that reads `distributionTransform` or the world position: add it to
  `transformInvariant`, or the reuse silently disagrees with a regeneration (the `[adr2138]` cases will not know).
- A new unhashed input to the records (like the material colours): add it to `reuseForTransform`'s checks.
- If the record upload itself becomes the cost, alternative 2 is the next step, with its own bit-identity argument.
