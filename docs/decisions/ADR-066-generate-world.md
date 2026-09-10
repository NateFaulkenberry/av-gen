# ADR-066: Generate World

Status: Accepted

## Context

Milestones 1–7 built an asset library, world recipes, a composer, a cinematic director, a musical
classifier, a job system and an AI abstraction. All of it was tested and none of it was reachable
from the application. 976 passing tests is not the same claim as a feature.

## Decision

**The composer's output is installed onto a Terrain node's `world::Ecology`, which is a vector of
exactly the `ScatterLayer`s it emits.** The join is three lines because the types were designed to
meet. Nothing forks the placer.

**Composition runs on a job worker; installation runs on the main thread.** Mutating the scene from
a worker while the renderer reads it is a race that appears as a crash under load rather than in a
test, so `WorldBuilder` holds finished worlds and the caller installs them at a moment of its
choosing. The UI does it in `applyFinished`; the CLI does it inline at start-up, where there is no
frame to keep responsive. This is also the split a future agent needs, which is why it lives in
`app/` rather than in the panel.

**The composition plan becomes ADR-038's `CompositionData`.** That type already carries focal
points and exclusion regions and already publishes them as the reserved `composition.clearance`,
`composition.exclusion` and `composition.weight` fields that density filters and effectors consume.
Translating into it means the void regions actually thin the ecology; keeping a second description
beside it would have meant they were drawn in a debug view and ignored by everything else.

**One path.** `--generate`, the World Builder panel and any future agent all call
`composeWorld` + `installWorld`. There is no separate human path and AI path, which is the
architectural constraint the agent addendum asks for and the cheapest time to honour it is before
either exists.

## What connecting them found

Four bugs, none of which a unit test would have produced, which is the argument for exercising the
application rather than trusting a green suite.

1. **`no biome named 'forest'`.** The composer writes layers against a biome vocabulary the terrain
   had never been told about. `world::composerBiomes()` now defines those five next to the composer
   and the terrain builder asks for it, so they cannot drift.
2. **`proximity layer 'tree_tall' must precede it`.** The ecology requires a proximity host to be
   placed before the layer referencing it. Layers are now emitted background → midground →
   foreground, which satisfies it by construction (a host is always in a taller band) and is the
   right order anyway. Stable, so same-band layers keep manifest order.
3. **The camera framing did nothing.** `camera/position` and `camera/target` are only read when
   `camera/mode` is 1 (free); a fresh composition defaults to orbit, which ignores both. Setting the
   mode took visible instances from 390 to 66,794 — the camera had been aimed at empty space. This
   default is also where the orbiting camera in every scene so far came from.
4. **The map's own edge was in shot**, because the camera stood outside the heightfield. It now
   sits inside the world, a tenth of the extent back from the focal subject, which also puts
   vegetation between the camera and its subject — where a foreground comes from.

## Consequences

`avgen --generate <recipe>` composes and renders a world with no window. Against the repository's
manifest: 31 assets → 31 layers, ~762,000 candidate instances, 28.5 M triangles.

**Deterministic to the pixel.** The same recipe rendered twice differs in 0 of 518,400 pixels; a
different seed differs in 39.5%. A saved world that does not reproduce is not a world, it is a
screenshot of one.

Generating twice reuses the terrain and replaces its ecology rather than appending, or every layer
would double. `CompositionNode` is deliberately not copyable, so the terrain is mutated in place —
removing and re-adding it would discard its transform, material, world map and its name in every
route that targets it.

The camera is only framed for a world that had no terrain before. A camera in an existing scene is
somebody's decision and generating into that scene is not a reason to overrule it.

## Not done

The generated world has no art direction: even scatter with no hierarchy, no landmark reading as a
landmark, flat lighting, and a sky that is a single dark navy. The pipeline is real, deterministic,
editable and visible, which is what this phase asked for. Making it *good* is the showcase's job.
