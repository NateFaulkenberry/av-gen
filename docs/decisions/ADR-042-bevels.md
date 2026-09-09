# ADR-042: Bevels as a first-class source operation

## Status
Accepted, 2026-09-09.

## Context
Every showcase scene read as "generated" rather than "made", and the most consistent reason was
edges. A mathematically sharp edge does not exist on a manufactured object: a real edge carries a
small radius, and that radius catches a moving highlight the eye reads before it reads anything
else about the surface. No material and no lighting recovers from its absence.

The engine had no bevel of any kind. Deformers could not supply one -- a deformer moves existing
vertices, and a bevel needs new geometry along the edges.

## Decision
`bevel` and `bevelSegments` become part of `SourceSpec`, honoured by `Box` and `Cylinder`.

- **Box**: `makeBeveledBox` builds the Minkowski sum of a smaller box with a sphere -- six flat
  faces over the inner rectangle, twelve quarter-cylinder fillets along the inner edges, eight
  spherical octants at the inner corners.
- **Cylinder**: `makeBeveledCylinder` revolves a profile whose two rims are quarter arcs.

Normals are analytic rather than averaged, so a 5 cm bevel on a 2-unit box still shades correctly.
The fields are hashed (an edited bevel re-uploads the mesh), serialised, and registered as
`source/bevel` and `source/bevelSegments`. `bevel: 0` reproduces the old primitive exactly, so no
existing scene moved.

## Consequences
Boxes and cylinders cost more vertices when beveled: a box goes from 6(n+1)² to that plus twelve
fillet patches and eight corner patches. At `bevelSegments` 4 that is roughly a fivefold vertex
increase for a low-subdivision box, which is the right trade for hero geometry and the wrong one
for a field of ten thousand instances -- so the bevel defaults to 0 and is opted into per object.
The hero/support/background hierarchy is the natural place to decide that automatically.

Two implementation notes that will matter to whoever extends this to spheres and tori:

- **Patch boundaries must be exact.** `cos(pi/2)` is 4e-8 in float, not 0, so the endpoints of
  every quarter turn are forced to exact values. Without that the fillets sit a hair away from the
  faces and the corner poles fail to collapse.
- **A collapsed edge cannot be detected by triangle area.** With FMA contraction `cross(v, v)`
  does not evaluate to exactly zero, so degenerate triangles are found by their coincident
  corners instead.

## What this cost
The first version passed a test that checked positions, normals, watertightness and determinism --
on a surface that rendered nothing, because the flat faces and fillets were wound inward and
back-face culling removed them. The symptom (flat metal black, curved metal lit) is
indistinguishable from a broken specular IBL, and was diagnosed as one for the better part of an
hour. A geometry test that does not check winding does not check that the surface can be seen; the
tests now assert that every triangle's geometric normal agrees with its vertex normals.
