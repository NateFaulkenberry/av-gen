# ADR-061: The world composer

Status: Accepted

## Context

Milestone 2 asks for a composer that creates visual hierarchy: foreground, midground, background,
focal regions, negative space. `world::Ecology` already places things, and places them well — biome
densities, slope and altitude limits, water avoidance with a shore offset, clustering, proximity
between layers, view distance, screen-radius culling, instance ceilings, mesh budgets.

The temptation is to write a second placement system with hierarchy built in. That would fork all
of the above, and the fork would be the copy without the water-avoidance fixes.

## Decision

**The composer's output type is `world::ScatterLayer`.** It emits the thing the ecology already
consumes, so a composed world drops into any scene with a terrain node and is placed by code that
is already tested. A test asserts the type, so that if it ever becomes something else the fork is
visible in a diff rather than discovered later.

What the composer adds is what ecology has no opinion about:

**Bands.** An explicit `foreground` / `midground` / `background` tag always wins, because it is
somebody stating an intention and guessing over the top of a stated intention is how authored
worlds get quietly rearranged. Absent a tag, height decides: under 1.5 m is underfoot, over 6 m is
a silhouette. Each band carries its own view distance, screen radius, clustering and instance
ceiling — a foreground fern surviving to 400 m costs a fortune to contribute a pixel, and a ridge
silhouette culled at 90 m leaves a hole in the horizon.

**A focal subject.** The library's highest `visualImportance` gets it, placed off-centre by a hash
of the recipe's seed, because a subject in the middle of the world is the composition nobody chose.

**Negative space as a positive instruction.** Void regions are placed away from the focal subject:
an empty region on top of the one thing worth looking at is not negative space, it is a hole.

**Relations.** Small shade-dwellers are given a `ScatterProximity` pointing at the tallest layer of
a taller band. This uses the relation ecology already has; it is one rule, not a system, and it is
the difference between a world and a collection of independent scatters.

**Determinism by hashing, not by streams.** Every placement decision is `hash(seed, salt)`. A PRNG
stream would make results depend on the *order* decisions are made in, so adding a layer would
silently move everything after it.

## Consequences

Composition is a pure function of `(recipe, library)` and is tested as one. Nothing runs per frame.

Density is `assetDensity × ecologyWeight × bandWeight`, so halving a band's weight halves exactly
what is in that band, which a test pins. An asset with no authored density gets one from its
footprint, because a thing two metres across cannot be as numerous as one twenty centimetres across.

The judgement calls are all in one place and all crude: the band thresholds, the per-band tuning
and the single shade relation. They are meant to be replaced by better ones, not extended into a
rule engine.
