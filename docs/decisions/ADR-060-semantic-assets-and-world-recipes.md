# ADR-060: Semantic assets and world recipes

Status: Accepted

## Context

The cinematic upgrade asks for a world composer that can reason about foreground, midground,
background, focal regions and negative space. Before any of that is possible, something has to know
what an asset is *for*.

Today nothing does. `assets::AssetRegistry` answers "give me the mesh at this path" and is a file
cache; it has no opinion about whether a mesh is a fern or a cathedral. The knowledge exists, but it
is spread between a human's head and the hand-authored `scatter` arrays in each scene file, where a
fern's density, height, slope tolerance and hue variation are written out per world, per layer,
every time.

There is also already a manifest. `assets/manifest.json` records a source, a licence, and per-entry
name, file, triangle count and natural size, grouped into categories — and it is *curated*, with a
note saying only names listed there may be placed procedurally. What it lacks is the artistic layer:
what role an asset plays, how important it is, what it does when the music moves.

## Decision

**Extend that manifest rather than introduce a second one.** `assets::AssetLibrary` reads the file
that exists. Every field beyond `name` is optional with a defensible default, so the manifests
already in the repository load unchanged and can be enriched one entry at a time. Both shapes are
accepted: a flat `assets` array, which is what a manifest written for this system looks like, and
the existing grouped `categories` object, where the group supplies a category and a placement tag —
`midgroundPlants` becomes flora with a `midground` tag. A test loads the repository's real manifest
and checks exactly that, because a semantic layer that required every existing manifest to be
rewritten before anything worked would not be worth having.

**An asset carries four profiles, not one bag of numbers.** Material, audio response and variation
are separate structs because they are separate decisions made by separate people at separate times.
`AudioResponseProfile` in particular exists so a composer can decide which layers carry a drop and
which stay still — without it every layer would have to react equally, which is the failure the art
direction is written against.

**`visualImportance` is the single most load-bearing number.** It is what a composer sorts by when
deciding what may occupy a focal region. Categories say what a thing is; importance says whether the
shot is about it.

**A recipe is a world's intent with no objects in it.** `world::WorldRecipe` holds composition,
ecology, atmosphere and lighting as 0..1 weights plus an art direction. It does not say where the
ferns go: that is the composer's job, and separating them means the same recipe can be composed
against different libraries and different terrain and still read as the same world.

**Every weight has a default, so a three-line recipe is legal.** A recipe is meant to be edited by
someone deciding how a world should feel, and a format that demands forty numbers before it will
load is a format nobody edits. The aliases (`negative_space`, `crystals`, `creatures`,
`floating_elements`, `moon`) exist for one reason: a recipe copied out of the brief should load.

## What this deliberately does not do

It does not replace `world::Ecology`. The spec's "world composer" overlaps it substantially —
biome densities, slope and altitude filters, water avoidance, clustering and `ScatterProximity`
relations all exist and work. Building a parallel placer would fork the placement logic, and the
forked copy would be the one without the water-avoidance bug fixes. Milestone 2 extends ecology; it
does not compete with it.

It does not replace `app::WorldDirector` (ADR-041), which is already art direction in words —
eighteen knobs mapping to live routes. `ArtDirection` inside a recipe is the same vocabulary at
composition time rather than at playback time, and the two should meet rather than duplicate.

## Consequences

Nothing here runs per frame. No render path, no GPU code and no scene evaluation is touched; the
library is consulted when a world is composed and then not again. Frame cost is unchanged by
construction, not by measurement.

Two files, `src/assets/asset_library.*` and `src/world/world_recipe.*`, plus eleven tests.

A default caught by a test is worth recording: `preferredScale` first defaulted to 1.0, which meant
the documented fallback to the mesh's own bounds could never fire and every unauthored asset
silently became one metre tall. It defaults to 0 — "whatever size the mesh already is".

## Milestones this unblocks

2 (composer), 8 (a model that tags assets), 10 (the showcase). The remaining architectural risk is
the one named above: milestone 2 must extend `world::Ecology` rather than grow a second placement
system beside it, and the moment there are two, worlds will start disagreeing about what a density
means.
