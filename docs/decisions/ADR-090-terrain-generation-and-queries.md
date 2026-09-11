# ADR-090: Terrain that has geography in it, and one place to ask about it

Status: accepted
Date: 2026-09-11

## Context

Two problems, one file away from each other.

**§29–§32.** `terrainFor` — the ground every generated world stands on — was three octaves of noise
and nothing else. No ridge, no valley, no basin, no plateau, no river, no pond. Every seed produced
the same rolling dune field, every slope-and-altitude rule the ecology is written in had almost
nothing to bite on, and a generated world could not contain water at all. The brief asks for hills,
valleys, ridges, basins, plateaus, river channels, ponds and small lakes, controlled by artistic
parameters rather than noise settings.

**§3.** Four systems need the same facts about the same ground. The walker needs somewhere to stand,
water placement needs the low ground, the editor needs to know where a click lands, the composer
needs to know what grows where it is putting things. Each reached for a different half of the
answer — `WorldMap::height` here, `WorldMap::sample` there, `ClearanceField::canopyHeight` in a
third place — and the questions that need two of them together were re-derived per caller, with
different margins. `isOccupied(x, z, radius)` existed nowhere.

## Decision

### The generator writes an existing format

`WorldMap` already describes geography properly: a noise base plus an ordered list of `Feature`
stamps — ridge, valley, river, flat — drawn as polylines in world space, the same vocabulary an
authored world uses. What was missing was anything that *wrote* those features. So `terrain_gen` is
a generator for a format that already exists rather than a second terrain system. Everything it
produces serialises, hand-edits, reloads and renders through the code that was already there, and an
artist who wants to move a generated river moves three numbers in the world JSON.

### Nine numbers, no noise settings

`TerrainParams` is Terrain Style, Elevation min/max, Roughness, Valley Strength, Ridge Strength,
Water Amount, River Frequency, Pond Frequency and Seed. There is no frequency, octave count,
lacunarity, persistence or domain-warp distance in it: a parameter set that exposes those is a noise
editor, and §30 is explicit that it should not be one. Every noise setting is derived from the nine,
in one place, where it can be tuned once for everyone. `terrainPreset(style, extent)` is §30's
presets — pick "mountainous" and get mountains, then move Roughness, rather than discovering that
mountains also need the ridge strength at 0.8.

### Five stages, and the order is load-bearing

1. **Shape.** Style and Roughness choose an octave stack and an erosion weighting.
2. **Fit.** The stack is measured over the whole map and rescaled so it spans exactly
   `[elevationMin, elevationMax]`. `octaveSum` is linear in the amplitudes, so this is an exact
   one-pass fit rather than an iteration.
3. **Landform**, then **fit again**. The whole height function is affine in the layer amplitudes,
   `baseHeight` and the feature levels and amplitudes, so the same fit applies to a map with
   features stamped on it. Without the second fit, a style that lays down five ridge lines
   overshoots its range by more than the range itself: the mountainous preset measured 396 m of
   relief for a requested 210.
4. **Drainage.** Rivers are *traced*, not drawn — see below.
5. **Standing water.** Sinks become ponds; a course that ends inside the map ends in a lake sized to
   the hollow it ends in.

### A river is found, not drawn

A course is a walk downhill from a spring, and the step is a search over a fan of nine candidate
directions rather than a step along the gradient. That is not cosmetic. A gradient step walks into
the first bump it meets on a rough hillside and stops; every river on the first mountainous world
was a fifty-metre stub ending in a lake perched on a slope. A fan looks at where nine candidate
steps would land and takes the lowest, so a course rounds an obstruction the way water does.

Around that sit four rules, each of which exists because its absence was visible in a picture:

- **A course may not run back within its own channel width of itself.** Without it the fan circles
  on level ground: the first basin produced a river coiled into a flat spiral with a lake at the
  centre, and a polyline that doubles back becomes a barb once it is smoothed.
- **Springs are chosen by the length of the course they produce**, from a pool three times larger
  than the number of rivers wanted and kept well inside the map. The highest point on a map is as
  often a knoll above a short drop as it is the head of a valley, and one spring near the edge at the
  top of the height ranking was enough to make a world's whole drainage a pair of stubs in a corner.
- **A style's corridors descend along their length.** A valley of constant depth has no downhill
  along it, so a river that reaches its floor has nowhere to go and stops there.
- **The trough is cut before the channel.** A channel cut straight into a hillside is a slot with a
  water surface in it. The trough is what gives it banks and a floodplain, and both are built from
  the same control points at the same smoothing so the two curves coincide.

**The water line is taken from the lowest ground across the channel, not from the line down the
middle of it.** A water surface is flat across its width and stops where the ground rises through
it, so a level taken from the centreline renders as a wall of water standing over whichever bank is
lower. On the first generated valley, at the middle of one course, the ground three half-widths to
either side was 2.9 m and 3.5 m *below* the water line.

### Determinism (§32)

Every stage is a pure function of `TerrainParams`. The only randomness is a `Rng` seeded from
`params.seed`, drawn in a fixed order from numbered streams — numbered so that adding a stage does
not shift every later stage's stream and silently change every existing seed's world. No wall clock,
no `random_device`, no container with unspecified iteration order, and every sort has a total
comparator so that equal elements cannot be left in an order the standard library chooses. The test
compares 4225 heights on a lattice bit for bit, not a hash and not a spot check.

### One query surface

`world::TerrainQuery` is a *join*, not a new model: it owns no data, holds a `WorldMap` and a
`ClearanceField`, and answers what §3 names — `heightAt`, `normalAt`, `slopeAt`, `isWalkable`,
`isWater`, `isOccupied`, `nearestValidPoint`, plus `at()` for callers that need more than one scalar
from one set of evaluations. Its walkability rules are `entity::NavSettings`'s rules with the same
names and the same defaults, because they are the same rules.

Nothing was moved and nothing was renamed. Three other branches were in flight when this landed, and
every rename would have been a merge conflict for all of them.

### `isOccupied` is honest about what it cannot know

The canopy is statistical by design (ADR-080): it says "trees about nine metres tall grow around
here", never "there is a trunk at this spot". Asking it whether a two-metre disc is clear returns an
answer about the neighbourhood, not about the disc, so it is **not** consulted. Folding it in would
turn "is something standing here" into "does something grow nearby" and put the whole of a meadow
permanently out of bounds.

What `isOccupied` does answer is the world's edge and heroes, which are a handful of capsules with
real radii and are exact. Everything else is behind `world::ObstacleField`, a one-method interface
for §5's per-object set — navigation's to own — and `hasObstacles()` lets a caller tell "nothing is
there" from "nobody asked". A placement tool that treats the second as the first drops objects
inside trees and reports success.

### The seam with water

Terrain owns the channel; water renders it. The seam has two halves and both are needed.

- **Continuous.** `TerrainQuery::waterDepthAt(p)` and `WorldMap::waterSurface(p)` are analytic,
  exact at any resolution, and already what `buildChunkWater` meshes the surface from. No baked depth
  texture is handed over: the field is closed-form and a baked copy would be a second truth.
- **Described.** `world::WaterCourse` carries what a depth field cannot: a downstream-ordered
  centreline with the surface level at every node, a half-width, a depth, the descent over the
  length, `flowAt`, `surfaceAt`, `flowSpeed`, `contains` and `alongAt`.

A `WaterCourse` is terrain's statement of fact. It is deliberately *not* §12's `WaterBody`, which is
a renderable thing with a material, a flow strength and turbulence; the intended direction is one
way, a `WaterBody` built *from* a `WaterCourse`, so that moving a river in the terrain moves the
water. It reads `WorldMap::features`, so Glowmere's hand-written river describes itself without being
regenerated.

## Consequences

- Every generated world has landform and drainage. Biome coverage across the five styles now runs
  marsh 14–32%, forest 21–56%, meadow 7–37%, with scree and rim present where the landform warrants
  them — against a previous world where marsh existed only because `lowlandMoisture` had been raised
  to 0.78 to fake it. That crank is gone: real water supplies the wet end, so the lowland term is
  back to meaning low ground is a little damp and the water's edge is wet.
- `moistureReach` is now a real corridor along a real river, which is what makes §31's "vegetation
  responds to water proximity" true rather than aspirational.
- A height sample now walks more features, so `WorldMap::height` costs more than it did. Measured
  below; the feature bounding boxes are what keep it affordable.
- Existing recipes with no `terrain` block get the rolling-hills preset scaled to their own extent.
  The preset's elevation range scales linearly with the world's width — not geography, but what keeps
  the slope distribution, and therefore every slope rule in the ecology, the same across the density
  benchmark rungs.

## What this does not do

Courses can still cross without joining: the confluence test is a proximity check against the
courses already traced, so two rivers traced from different springs may meet at a shallow angle
without merging. There is no flow accumulation and no erosion simulation; a river's width comes from
its rank rather than from its catchment. And a course whose mouth is not in a hollow simply ends,
because the alternative — a lake wherever a course stops — puts a disc of water on an open hillside,
which is the failure §31 forbids arrived at from the other direction.
