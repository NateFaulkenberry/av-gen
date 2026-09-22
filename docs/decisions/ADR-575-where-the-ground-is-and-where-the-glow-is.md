# ADR-575: §18 is one route working and three with no GPU resource; §26 is one term

Status: §26 accepted, §18 **investigated with a recommendation that needs a decision**, §25
assessed. Date: 2026-09-21.

## §18 -- fog should hug the world

The brief asks this one to be *investigated* rather than built: *"Investigate terrain-aware fog via
world height, scene depth, terrain heightmap, depth buffer, scene collision/depth, signed-distance
approximation ... Do not make this depend on expensive per-frame CPU scene traversal; use
GPU/depth-based approaches where practical."*

Five routes are named. Measured, here is the state of each:

| route | state |
|---|---|
| **scene depth / depth buffer** -- "behind rocks", "between foreground and background" | **works.** `fs_march` clamps `maxDistance` to the first surface and `fs_composite` upsamples depth-aware (ADR-573 checked it) |
| **world height** -- a flat layer at an altitude | **works.** `fogHeight`, `fogHeightFalloff`, and ADR-568's floor and curve |
| **signed-distance approximation** | **a stub.** `FieldKind::SdfDistance` is a declared field kind that returns **0 on the CPU and 0 on the GPU** -- `spatial/field.cpp` says *"SdfDistance is 0 on the CPU (unbound; ADR-027 binds it later)"* and `fields.wgsl` returns 0 for it. A scene naming an SdfDistance density field gets a density of zero, which is **no fog at all** |
| **terrain heightmap** | **no GPU resource exists.** The terrain's height lives in `world::TerrainQuery::heightAt`, which is CPU and takes a `WorldMap`. Nothing uploads a height texture; terrain reaches the GPU as chunk meshes |
| **scene collision** | same -- CPU only |

So: **two of five work, one is an unimplemented enum, and two need a GPU-side resource that does
not exist.** "Fog sits in a valley", "over water" and "around trees" are all in the last group.

### The recommendation, and why it is a decision rather than a task

**Bake a terrain height texture once and bind it to the volume pass.** The terrain's height is
*static* for a scene -- `WorldMap` does not change per frame -- so a bake at rebuild is not the
"expensive per-frame CPU scene traversal" §18 forbids; it is the opposite of it. One texture would
serve fog, and it is the same number water, grass and anything else that wants to know where the
ground is has been asking a CPU query for.

It is not mine to do unilaterally. It adds a binding to the volume pass, a product to
`TerrainProducts`, and a resource to a system another agent owns. The cost is real and the benefit
is three of the brief's five routes at once, plus `fogGroundFollow` -- a control that makes the
layer's height track the terrain instead of a plane, which is the whole of "fog sits in valleys".

**And `FieldKind::SdfDistance` should either be implemented or removed.** As it stands a scene can
select it from a documented list and silently get nothing. ADR-441 takes no shims; a declared
option that evaluates to zero is worse than an absent one, because an artist who picks it concludes
the fog is broken rather than that the option is not there. That is ADR-574's rule again with the
polarity flipped: **a control that is offered and does nothing.**

## §25 -- colour: assessed, not built, with the gap named

§25 wants fog colour, scattering colour, absorption colour, height colour and distance colour. The
medium has **three colours through its depth** (`colorDeep`, `colorMid`, `colorAccent`, mixed on
density with the accent reserved for filaments), which is the hierarchy the brief's own §25 asks
for and warns not to overload: *"avoid making colour responsible for structure."*

What is missing is a **height colour** and a **distance colour**. Both are additive, neither is
blocked, and neither was built here for a reason worth stating rather than hiding: **nobody has
asked for them, and §25's own warning is that colour should not be doing structure's work.** Two
more colour gradients on a medium whose structure was only finished this week is the kind of
addition that is easy to justify and hard to remove. They are named here so the next person does
not have to re-derive that they are missing.

## §26 -- emission: the fourth of four

§26 lists *"intensity, color, height influence, density influence"*. The march had three:
`emission` is the intensity, the three colours are the colour, and `mediumEmissionAt` keys the
colour hierarchy on `shape`, which is the density influence. **There was no height influence.**

`fogEmissionHeight` is it, and it **reuses `fogVerticalProfile`** rather than introducing a second
vertical shape -- so a bank whose glow follows its height follows the same curve its density does.
One vertical model for the medium, which is ADR-567's argument one level down. It is a **separate
number** from the density's `heightInfluence` on purpose: a bank can be densest at its floor and
glow evenly, or be uniform and glow only where it is low.

**It is a per-kind ARM, not a lane read.** `mediumEmissionAt` is a shared accessor and lane 12's
meaning is per-kind -- precisely the shape ADR-562 §9 recorded three defects of -- so the fog's
number is reached through the same `mediumKind` dispatch `mediumShape` uses, and a kind with no
such control is untouched by construction rather than by a zero.

## Consequences

- **The parity harness had no slot for the new term, and breaking the shader is how I found out.**
  I added `fogEmissionHeight` to the GPU, deleted its effect from the shader to demonstrate the
  break, and `test_fog_parity_gpu` **passed** -- because it compared four quantities and the
  emission was not one of them. The harness writes two `vec4` per sample now and the same break
  fails at 1.0 against 0.300. **A parity test covers the terms it reads and no others**, and a pair
  that gains a term without gaining an assertion has quietly become a pair with a gap. The
  discipline that caught it is ADR-182's: never trust a new test until it has failed.
- **Three CPU cases** for the term: exactly 1.0 at amount 0 (what it has always been), equal to the
  vertical profile at amount 1, and **independent of the density's height influence** -- which is
  the check that the two rows did not end up sharing a lane slot.
- **One of those cases failed on a real-but-wrong tolerance.** Comparing `1 + (p - 1)` with `p`
  where `p` is 5.8e-5 fails a *relative* tolerance on float rounding that is nothing in absolute
  terms. **A relative tolerance on a quantity that legitimately approaches zero is a test that
  fails where the value stops mattering**; it uses a margin, and says why.
- **The fog block is fifteen stored rows**, counted from the declarations rather than read off the
  failure.

## Revisit when

- **The terrain height texture is decided.** It is the single change that would move §18 from two
  routes to five, and it is worth more to the engine than to fog.
- **`FieldKind::SdfDistance` is implemented or removed.** ADR-027 deferred it; the deferral has
  outlived the ADR that made it, and in the meantime it is an offered control that does nothing.
- **Someone asks for height or distance colour.** §25's gap is named and costed; building it before
  it is wanted is how a medium ends up with colour doing structure's job.
