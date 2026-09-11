---
id: gaps/terrain-water-navigation
title: Terrain, Water and Navigation
category: Not Yet Documented
summary: Three subsystems under active rewrite; not documented, and deliberately not guessed at.
order: 201
status: not-yet-documented
tags: terrain, water, navigation, ecology, biome, sculpting
keywords: how do i edit terrain; how do i make water; navmesh; sculpt the ground; rivers; lakes; pathfinding
related: gaps/world-editor, gaps/ai-director
---

# Terrain, Water and Navigation

**These areas are not documented yet, and this page will not guess.**

Terrain, water and navigation are each being rewritten in parallel with this Help system. Their
controls, their parameters and their file formats are all expected to change.

## What is missing

**Terrain** — heightfield editing, sculpting, painting, biome weights, materials, the chunking
scheme, the relationship between the height function and what is scattered on it.

**Water** — creating a body of water, its surface, its transparency and refraction, reflections,
how it integrates with fog and the environment, and the shoreline.

**Navigation** — whether a navigation mesh exists, how paths are found, and how an actor's spline
path relates to it.

## What is safe to say today

- A generated world is composed from a **recipe**; `--generate <file>` runs one at startup and the
  World Builder panel does the same thing interactively. `examples/recipes/` holds several,
  including the four **Glowmere Density** rungs used as performance references.
- The composer budgets three depth bands with different view distances, minimum screen radii and
  instance caps. A practical consequence: **a hero standing more than about 90 m from the camera is
  already outside the foreground band**, and the composer clamps its own viewpoint accordingly.
- `world/scatter` is single-threaded, and a full composition rebuild is around 390 ms on the main
  thread. See [What costs what](help://performance/what-costs-what).

## When this will be written

When the terrain, water and navigation passes land.
