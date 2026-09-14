# ADR-175: A searched organism is a parameter vector the scene owns, not geometry the generator kept

**Status:** Accepted
**Date:** 2026-09-14
**Scope:** `scene::PrimitiveKind::Generated`, `scene::GeneratedSource`, the generator registry

## The gap

Two projects — Glowmere Valley 2's hero mushrooms and the procedural Tree of Life — pick their
assets by searching a parameter space: generate hundreds of candidates, score them, curate a handful.
`scene::PrimitiveKind` could name a box, a cylinder, a sphere, a torus, a point, another procedural,
a swept tube or an imported asset. None of those is *"built by a registered generator from a
parameter vector"*, so a searched organism could be searched but not put in a scene as itself.

The workaround was obvious and wrong: bake the winner to a mesh and import it. That produces an
object an artist cannot click, inspect, nudge, key, modulate, undo or save — a mesh blob wearing a
procedural label.

## The decision

**One new kind, `PrimitiveKind::Generated`, whose spec is a parameter vector with provenance.**

```cpp
struct GeneratedSource {
    std::string   generator;          // names a builder in the registry
    std::uint32_t generatorVersion;   // bumped when a parameter set means something new
    std::uint64_t schemaHash;         // 0 = unchecked; non-zero is enforced
    std::uint32_t index;              // provenance: which candidate these values came from
    std::vector<float> values;        // authoritative
};
```

### `values` is authoritative and `index` is not

This ordering is the ADR. This engine's governing rule is that a **parameter** is authoritative and
`Scene` is a per-frame derivation rebuilt by `Composition::applyParameters` — so anything that lives
only as a mesh does not survive one update. Storing the candidate index alone and regenerating from
it would have been smaller and would have made the organism un-editable: the first slider drag would
be overwritten by the next frame's rebuild.

So the scene stores the numbers. `index` records where they came from, which is worth keeping
because an untouched winner satisfies `values == search::sampleAt(schema, index)` exactly, and that
is an assertion a test makes. The moment an artist edits one, the two diverge, and that is correct.

### `schemaHash` is what makes the provenance honest

The values are **positional**. Widen a parameter's range or reorder the schema and every one of them
means something else. The hash moves with the schema, so a stale record announces itself instead of
silently regenerating a *different* organism under the same name. `search::validateAgainst` refuses
rather than reinterprets.

### A registry, not a switch

`scene` cannot know about mushrooms or trees, and a data-driven scene format has to reach code
somehow. `registerGenerator(name, builder)` is process-global, which is a real cost, and the
mitigations are that registration order cannot matter (names are unique) and that re-registering a
name replaces it (which is what a hot reload needs).

### `generatedPart`, the exact analogue of `assetPart`

An organism is several meshes from one parameter set — a mushroom's cap, underside, stem and gills —
the way an imported asset is several meshes from one file. Each part becomes one named node, which
is what makes it separately selectable in the viewport and separately materialled, and it is hashed
for the same reason `assetPart` is: without it the gills come out of the mesh cache wearing the cap's
geometry.

## What it is not

**Not mushroom-specific, and not Glowmere Valley 2's.** A tree and a mushroom differ in their
generator's name and schema, not in how a scene stores them. Glowmere Valley 2 landed it because it
needed it first; the Tree of Life should use it rather than adding a second kind.

## Evidence

`tests/unit/test_candidate_search.cpp`, `[search][generated]`, with a fixture generator called
"fixture" that builds boxes — deliberately, because nothing here belongs to either domain:

- validation refuses an empty generator, no values, a non-finite value and an out-of-range part;
- the structural hash **moves** when a value or the part changes and **does not move** when the index
  changes, which is the provenance/identity split asserted rather than described;
- `makeSourceMesh` resolves through the registry, names the generator in its error when there is no
  builder, and returns different geometry for different parts;
- the scene's own JSON round-trips every field and the hash across the trip;
- a scene that does not use it **does not grow the key** — `"generated"` is written only for the kind
  that uses it, so every scene authored before this exists round-trips byte-identically;
- a duplicated node keeps it. `cloneNodeSpec` copies `procedural` wholesale, so a field nested inside
  `SourceSpec` is carried without touching that function's list — asserted rather than assumed,
  because that header says it is where a duplicate comes back missing something, and it has been
  wrong five times.

## Consequences

- `PrimitiveKind` gained a value. The enum is append-only: its integer is in `structuralHash` and its
  name is in every scene file.
- `source/kind`'s parameter range went 0..7 → 0..8. A test asserted the old ceiling and caught it,
  which is the test doing its job.
- A generated object is **selectable and editable**: `search::parameterPaths` registers one keyable,
  modulatable parameter per schema axis under the node's own prefix.
- Live regeneration is affordable because `Composition::setInteractiveRebuildBudget` already exists
  and is already measured. No second throttle.

## Rejected alternatives

- **Bake the winner to a mesh and import it.** Above.
- **Store only the index and regenerate.** Smaller, and un-editable.
- **A `PrimitiveKind::Mushroom`.** Domain vocabulary in the engine core, and it would not generalise
  to the tree being built in parallel against the same requirement.
- **Pass a registry through `makeSourceMesh`.** Cleaner in the abstract; every caller in the engine
  would have to carry one, for a lookup that happens once per mesh rebuild.

## Revisit when

- Two generators want the same name — the registry silently replaces, which is right for reloads and
  wrong for a collision.
- A generator needs to declare its own part count, rather than the scene naming a part and the
  builder refusing an out-of-range one.
