# ADR-067: A generated world's art direction, and the negative space that was never applied

Status: accepted
Date: 2026-09-10

## Context

ADR-066 connected the world pipeline end to end: a recipe composes into scatter layers, the layers
install onto a terrain node, the terrain places them. The first world it produced was reported
honestly as having no art direction -- even scatter, no hierarchy, no landmark, flat lighting, a sky
that was a single dark navy.

Rendering it at full size showed something worse. The world was not badly art-directed; it was not
there. All 62 layers warned "mesh source has not been resolved" and were skipped, and the frame was
bare terrain. The instance counts previously reported -- 762k candidates, 28.5M triangles -- were
what the composer intended and not what was drawn.

Three separate things were broken, and each of them had been reported as working.

## Decision

### 1. Asset paths resolve where the library is, not where the reader is

`AssetLibrary::resolve()` joined an asset's file onto the manifest's parent directory *as written*.
Named relatively -- which is what `--generate` does and what the World Builder does -- the result
was relative, and its one consumer resolves paths against its own base directory. For a composition
never saved to a file that base is a temporary directory. `loadFile` now records an absolute base.

### 2. The library says what a species is; the recipe says what the world is

The manifest was a list of names, files and triangle counts, so every semantic field the composer
reads was at its default: `visualImportance` uniform (making the landmark choice degenerate),
`material.emissive` zero everywhere (so the bioluminescence branch had never once executed), and
`preferredScale` zero, leaving Kenney's 1.69-unit tree 1.69 metres tall.

All 31 entries are now authored: real heights, importance, densities, and which species glow. The
split is deliberate and is the one that lets a curated pack become several worlds:

- the **library** says a mushroom is the kind of thing that glows and a tree is nine metres tall;
- the **recipe** says how brightly *this* world burns and in what colours.

So `material.emissive` is a weight, never an absolute intensity, and a manifest cannot overrule a
recipe. Emission colour comes from the palette by role rather than from the asset -- a library
authored in reds does not get to decide that a world lit in cyan has red lights in it.

Palettes are written as names and read **by position, darkest first**: shadow, secondary, primary,
foliage, accent. Asking a recipe to spell out five roles by name would be a worse format than asking
it to put five colours in order. Unknown names resolve to nothing rather than to grey, because a
typo that silently becomes a neutral is a typo nobody finds.

### 3. Negative space is applied by the placer

The composer emitted void regions. `installWorld` translated them into the scene's exclusion
regions. Those became reserved `composition.exclusion` fields. **Nothing in the ecology read any of
it.** A world's negative space was carried faithfully from end to end of a pipeline and dropped at
the last step, so the corridor the brief calls mandatory was, in every world generated, absent.

`world::ScatterClearance` is now a property of an `Ecology`, applied by `scatter()` before the biome
is consulted. It carries a `minHeight`, because a lane through a forest is a lane in the canopy: the
first version cleared everything and produced a bald hillside with one tree on it, which is
technically a corridor and the exact opposite of a world composed to be dense at the viewer's feet.

Clearances round-trip as the terrain node's `"clearings"` rather than inside `"scatter"`, which is a
bare array of layers with nowhere for a property of the whole world to live. That mattered
immediately: an offline render reloads the project from a file before drawing it, so without
serialization the corridor existed in memory and never in a frame.

### 4. The composer chooses the viewpoint

A composition is a relationship between a viewpoint and what it looks at. The composer choosing the
material and leaving the viewpoint to whoever installs the world means nothing was ever arranged for
the place it is seen from -- and it is the only way a clear line can be promised, since a clear line
needs two known points. `CompositionPlan` now carries the viewpoint, and `installWorld` uses it
rather than inventing one.

Void regions are chosen before the viewpoint exists, so any that would contain it are dropped. One
that did produced a world dense everywhere except the single place it is seen from.

## Consequences

- `scatter()` takes a fourth argument. One call site.
- `Ecology::structuralHash` covers clearances, so moving a corridor replants the world.
- The manifest is now authored art direction over a CC0 pack. The meshes and textures remain
  Kenney's under CC0; the judgements about what each species is are this project's. Nothing is
  redistributed that was not already here.
- A test that pinned "no preferredScale means fall back to naturalSize" on the shipped `tree_tall`
  broke when the library was enriched. The rule had not changed; the test was reading authored data
  as if it were a default. It now builds its own descriptor.

## What this does not claim

The world is composed, not finished. The look was tuned only far enough to show the machinery works
-- foreground density, a readable lane, a landmark that reads as one, depth separation between
bands. Colour, the emission ladder's balance, and the camera's framing are all first passes and are
deliberately left for a later pass rather than polished now.
