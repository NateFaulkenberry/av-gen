# ADR-043: Tubes — the organic primitive

## Status
Accepted, 2026-09-09.

## Context
The next phase's subject is a living environment: stems, branches, vines, roots, tendrils,
tentacles, ribbons. The engine had splines (curves used to *place* instances and to deform along)
but nothing that turned a curve into geometry. Building a bespoke generator per organ -- a branch
generator, a vine generator, a root generator -- would be the wrong shape: they are one operation
with different parameters.

## Decision
`PrimitiveKind::Tube` sweeps a circular profile along a curve, with taper, twist and caps.

The curve is **embedded in the `SourceSpec` rather than referenced by name**. A referenced spline
would be more consistent with how distributions work, but it would make the mesh depend on scene
state that `makeSourceMesh(spec)` cannot see, and the mesh cache is keyed on the spec's hash. An
embedded `spatial::Spline` keeps the mesh a pure function of the spec, and the spline's own
generators (line, circle, spiral, helix, bezier, noise) already supply the shapes an organ needs.

Normals come from the swept surface itself -- the cross of the two surface tangents, by central
difference in both parameters -- rather than from the radial direction. That matters: a tapering
tube shades as a cone, and a radial normal would shade it as a cylinder. It also avoids the
special cases an analytic derivative needs at the ends and at a zero-radius ring.

A fully tapered end (`tubeTaper` 0) closes itself and gets no cap. A curve with no extent produces
an empty mesh rather than a fold.

## Consequences
One generator covers the whole list above, so a species is parameters rather than code, which is
what makes deterministic variation possible: same curve seed and parameters, same geometry.

Bounds come from sixteen samples along the curve grown by the widest the profile ever gets, rather
than from meshing it -- the culler only needs a bound that contains the tube.

Adding an enum value to `PrimitiveKind` is not free: `source/kind` is a registered integer
parameter with an explicit range, and the range clamped Tube back to Procedural, which surfaced as
every tube reporting an unresolvable source reference. Any future primitive has to widen both the
parameter range and the `copyEnum` count.

## What is not here
Ribbons (a flat profile rather than a circular one), profile scaling per-point beyond the curve's
own `scale`, and branching. Branching in particular belongs a level up: a branch is a tube whose
curve starts on another tube's curve, which is a generator concern rather than a primitive one.
