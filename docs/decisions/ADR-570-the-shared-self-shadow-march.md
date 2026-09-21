# ADR-570: the shared self-shadow march, and what it costs

Status: accepted, **off by default**. Date: 2026-09-21. The fog brief's §20 and §22.

## Context

§20 asks for fog that "responds convincingly to directional light ... bright illuminated mist, dark
moody fog, backlit fog". §22 asks whether light shafts can come from the same volumetric density,
and warns against bolting on a 2D radial blur if a proper integration is practical.

The march had in-scatter with a Henyey–Greenstein phase and **no shadowing of any kind**. Its own
comment said so: *"There is no shadowing in the fog: a beam is the falloff of a local emitter, not
an occluded shaft."* The consequence is one property: **a bank was exactly as bright on the side
facing the light as on the side away from it.** No setting of any control produced the asymmetry,
so §20's backlit and moody cases were unreachable by construction rather than by tuning -- the same
shape of finding as ADR-560's first bar. ADR-358 had already refused to build a volumetric beam
without shadow sampling, which is this gap named from the other side.

`agent/tornado` needs the same term and it is the last thing it needs from this branch.

## Decision

### One function, in the march, dispatching through `mediumShape`

At each march sample, for each light that lights the air, march a short secondary ray toward that
light through the placed media's own density and attenuate that light's in-scatter by the
transmittance. It lives in `shaders/volume.wgsl` and evaluates whatever each slot's *kind* says its
density is, so a kind added tomorrow self-shadows correctly without touching this function -- the
property ADR-562 gave the primary march, and ADR-562 §9 is the record of what it costs when a
shared reader is left assuming one kind's layout.

**It is also §22's answer without a second system.** A shaft is what you see when the air behind an
occluder is dark and the air beside it is not, and here the occluder is the medium itself. One
field, integrated once, no screen-space pass.

**It reuses ADR-566's bound.** The shadow ray is clipped by `mediumInterval`, the same claim the
camera ray is clipped by -- so a ray toward the sun from a point nowhere near a medium costs four
analytic interval tests and evaluates no field at all, and an ADR-566-style defect cannot be
introduced here separately because there is one bound and this is a second reader of it.

### And it is OFF by default, because of the measurement

`volumeShadowSteps` defaults to 0, and at 0 the shader returns 1.0 from a branch that tests the step
count first. Every existing scene renders bit-identically, which is a case rather than a claim.

**`volume.march` minima, interleaved within each repeat** (`docs/testing.md` 19; the Tree of Life
project, a placed fog bank, the shipped 32 march steps, **three** lights all with the default
`volumetricStrength` of 1.0), five repeats:

| shadow steps | 0 | 2 | 4 | 8 |
|---|---|---|---|---|
| min over repeats (ms) | **8.13** | 23.00 | 33.36 | 56.82 |
| paired delta vs 0 | -- | all five positive | all five positive | all five positive |

**2.8x at two steps and 7x at eight.** That is not a default, and it is barely a real-time feature
at all in that configuration.

### The cost is linear in the number of lights that light the air

Second ladder, interleaved, four repeats, at four shadow steps:

| | 3 volumetric lights | 1 volumetric light |
|---|---|---|
| shadow off (ms) | 7.73 | 7.54 |
| shadow on (ms) | 38.67 | **18.22** |
| added cost | +31 to +34 | **+10.1 to +11.8** |
| ratio | 4.5-5.4x | **2.2-2.6x** |

+31 ms for three lights against +10.6 ms for one is a factor of 2.9 for a factor of 3 in lights.
**The cost is linear in the light count**, as the mechanism predicts, and that turns the headline
number into something an artist controls rather than something the feature costs.

**Which surfaces a separate finding worth the owner's attention:** `volumetricStrength` defaults to
**1.0**, so *every light in every scene lights the air*. With the self-shadow march on, that is a
3x multiplier nobody chose -- the Tree of Life's three lights are a key, a rim and a fill, and only
the key was ever going to be the one casting the shaft. A default of 0 with the key opted in would
make this feature three times cheaper in every scene that has not thought about it, at the price of
a migration.

### The early-out that was tried, measured, and removed

The primary march breaks at `transmittance < 0.002`, and the same test was written into the shadow
loop expecting the same win. Interleaved, four steps, one light, three repeats: **+1.57, +0.85,
−0.78 ms.** Mixed sign, so **no effect is established** -- `docs/testing.md` 19's rule is discard on
sign disagreement, never on spread -- and it was removed rather than kept on the argument that it
must help. At four steps the loop can save at most three evaluations and only deep inside a thick
medium, which is probably why. If the step cap of 16 is raised, measure again.

The clustered local lights are deliberately **not** shadowed, and that is arithmetic rather than
principle: that loop runs over a whole froxel's light list with no `volumetricStrength` gate to
thin it. A scene that wants a shadowed practical should make it one of the eight frame lights.

## Consequences

- **The property is measured, not asserted.** In the render case -- isotropic phase, a dense
  sphere, one light from +X -- the left/right luminance difference goes from **0.018 to 0.134**,
  a factor of 7.3, and the shadowed side falls from 0.184 to 0.033. The *far* side got darker
  rather than the near side brighter, which is the check that the term removes light rather than
  adding it, and a case asserting only "one side is brighter" would pass with the feature deleted
  because Henyey–Greenstein already leans the frame slightly.
- **Break demonstrations.** Deleting the shadow factor from `inScatterAt`: 0 of 9216 pixels move
  and the asymmetry stays at 0.018. Reading the strength before the step-count gate: the
  zero-steps case fails at 0.0000227 against 0.0000188 -- which is the right size for a guard whose
  whole job is "exactly nothing".
- **`test_vortex_gpu.cpp` was writing the slot by hand** -- `pack` and then `.kind`, leaving lane
  15 (the tag the *shader* reads) at zero. It happened to work because zero is the vortex; a fog
  bank there would have rendered as a vortex and nothing would have said so. It uses
  `packMediumSlot` now, which is ADR-566's one writer.
- **The panel gained an integer row type**, because a step count drawn as a float slider reading
  "3.47" is a control an artist has to interpret -- the same argument `FieldType::Choice` makes one
  type down (ADR-566).
- **The step count is NOT scaled by the quality tier.** ADR-035 lets a tier scale sample counts,
  and this is one -- but it is also the difference between a bank lit from a direction and one that
  is not, which is a property of the look. A tier that silently flattened the lighting would be
  changing the picture rather than its fidelity.

## Revisit when

- **A shaft is wanted in a shipped shot.** The arm rendered here is a directional key on a thin
  bank and the effect is real but modest (9.5% of pixels move, at most 21 levels). The dramatic
  case is a *spotlight* through a dense bank, which is the §22 picture and which no scene in the
  repository currently sets up.
- **The cost needs to come down.** Three avenues are unmeasured and in order of expected value:
  shadow only the brightest volumetric light; compute the shadow at a coarser rate than the march
  and interpolate; precompute a low-resolution transmittance volume per light. The first is a
  policy change and free; the third is a different architecture.
- **`volumetricStrength`'s default is decided.** See above; it is the cheapest 3x available.
