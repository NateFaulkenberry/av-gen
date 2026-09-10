# ADR-053: The light a glowing ecology casts

Status: Accepted

## Context

After ADR-052 the bioluminescent ecology reads as light rather than as paint, but it lit
nothing. A glowing fern stood on ground the same colour as ground fifty metres away, and the
mist above a cluster of luminous fungi was the same grey as the mist over bare rock. The
image had light sources in it and no light.

The obvious implementation is not available. A hero scene places tens of thousands of
emissive instances across four scatter layers; the clustered path takes 256 lights in total.
Nothing that scales with instance count can work, and the brief that asked for this ruled
out both a light per instance and any CPU readback of GPU state.

## Decision

Reduce first, then light. `world::aggregateGlow` bins a layer's placements into cells of a
coarse world grid and reduces each occupied cell to one soft emitter: the emission-weighted
centroid of the instances in it, the standard deviation of their positions as a radius, and
their summed emitting area as power. An instance's share is its area rather than one count
each, so a large specimen outweighs a seedling. The result is stable and sorted, because the
per-frame pass takes a prefix of it and must not depend on hash-map iteration order.

The count then follows the area a layer covers rather than how much grows on it: the valley's
four layers reduce 30,000-odd placements to about 7,000 emitters at build time, and each
frame the nearest 224 of those within range become ordinary point lights. Everything
downstream is machinery that already exists — cluster assignment, the froxel light lists,
the shading loop.

Three things this got wrong first, all measured:

- They began as `Sphere` lights, which is what they physically are. A sphere emitter goes
  through the LTC area-light integration, and a couple of hundred of them cost 7.8 ms against
  2.9 ms as point lights. Fog and ground do not show the difference between a disk and a point.
- Influence range began at eight times the cluster radius. Cost is dominated by light volume,
  not light count: cutting it to four times took the frame from 45.0 ms to 40.5 ms with almost
  no visible change, and *enlarging* the aggregation cells made it worse rather than better,
  because a bigger cell means a bigger radius means a bigger reach.
- The gain that turns summed emitting area into candela is the one free constant. At 0.02 the
  shoreline was bathed flat violet; at 0.006 the light pools where the fungi actually are.

The volumetric coupling reuses the same lights. `shaders/volume.wgsl` declares group 0's
light and cluster buffers itself and repeats the twelve lines of froxel indexing rather than
including `lighting.wgsl`, which would drag the shadow atlas, the LTC tables and the whole
BRDF into a pass that needs none of them. It samples at most six of a froxel's lights with a
point falloff and no shadowing: fog has no detail to resolve.

That term is off by default (`Environment::volumeLocalLights`). It is free in the hero scene's
thin air and cost about 4 ms at three times the density, and a scene with thin air gets
nothing for it, so scenes with real fog around glowing things opt in.

`Environment::ecologyLight` is likewise 0 by default. Turning a glowing ecology into hundreds
of lights changes what every scene costs, so it is a decision a scene makes.

## Consequences

Glowing things light the ground, the water and each other. Measured at 2880x1800 on the hero
scene, interleaved against a control: 37.5 ms to 41.5 ms, so the whole ecology light field
costs 4.0 ms.

The aggregates are built once and are static. Ecology that moves — a drifting swarm, a plant
that opens at night — would need them rebuilt, which the current structure does not do.

Cells are square in x and z and ignore height, so a glowing patch on a cliff face aggregates
with one at its foot. Terrain that steep carries little ecology, which is the only reason
this has not mattered.
