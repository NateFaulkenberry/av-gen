# ADR-070: Art-direction profiles

Status: accepted
Date: 2026-09-10

## Context

A recipe says how much of a world there is: dense foreground, sparse ridge, a third empty. It said
nothing useful about what the world *looks* like, so the composer invented a look from the recipe's
weights — which produced, in order, a valley in a glass of milk and then a dark green field.

Meanwhile the Glowmere painterly scene already contains a good answer, written down layer by layer
(see `docs/glowmere-world-builder-audit.md`). The audit's finding was that the composer's vocabulary
was adequate and its *values* were wrong.

## Decision

An `ArtDirectionProfile` is data: a palette, an emission ladder, an atmosphere, a light rig and
post-processing restraint. A recipe names one. The composer resolves it once and composes against
it.

**Glowmere is one profile and must never be the only one.** Three ship: `glowmere` (a night lit by
its own flora), `emberwaste` (a hot plain under a low sun, where almost nothing emits and the accent
is *cool* against a warm world) and `palefen` (flat overcast, desaturated, nothing luminous, ambient
brighter than the key). They exist so the vocabulary is exercised rather than merely parameterised
around one world. A test asserts they disagree — if they ever converge, the system has become a
Glowmere configuration file.

### The ladder is absolute, and its gap is checked

Rung values are absolute emissive intensities, not fractions of a maximum. The ratio *between* rungs
is the art direction, and normalising them would let a later "brightness" control quietly flatten
the ladder into the ramp it exists to avoid.

`EmissionLadder::validate()` rejects a ladder whose step from ordinary vegetation to the first
bright rung is under 4× (Glowmere's is 13.3×). That gap is what makes bright things read as a
different *kind* of thing rather than the top of a gradient, and it is exactly what disappears when
someone reasonably asks for "a bit more glow everywhere". A world where nothing glows at all is
still legal; the check only applies once there is ordinary emission to measure against.

Composition applies the rung, then the asset's own emissive weight, which can dim a species but
never promote it past its rung. A manifest cannot overrule a recipe.

### The accent is reserved

A scatter layer may not reach the accent role while `reserveAccent` is set. One warm light in a cool
world is what makes a hero findable from anywhere in frame, and a few species wearing that colour is
the cheapest possible way to lose it.

### An unknown profile name is an error

Not a fallback. A typo that silently returns Glowmere is a typo that ships, and the whole point is
that a world can be something else. The message names the profiles that do exist.

### A recipe's own words win

Naming a profile is a starting point, not a cage. A recipe that states its own palette gets it — and
gets its own accent with it, or the reserved colour would still be the profile's and would no longer
be absent from the world.

## Consequences, and three things that had to be fixed to make it real

Applying an art direction turned out to need three engine changes, each found by rendering:

1. **`scene/styledSkyAmbient` and `scene/styledGroundAmbient` are now parameters.** They were copied
   from the scene file on the grounds that nothing animates them. True, and not sufficient: they are
   the two values that decide how dark a stylized world is, and the engine defaults are roughly four
   times Glowmere's. Anything an art direction sets should be as editable and saveable as everything
   else it sets.
2. **`Composition::installLightRig`** takes a rig built in memory. A profile describes a rig — a key
   that rakes over an ambient that stays out of its way — and writing it to a temporary file so the
   path overload could read it back would be a file nobody asked for. Named apart from
   `setLightRig` so `setLightRig({})` keeps meaning "clear the rig" rather than becoming ambiguous.
3. **A rig with no file is serialized inline.** Without this the rig survived exactly as long as the
   process: an offline render saves the project and reloads it in a fresh engine, and the world came
   back lit by the default key with the key-to-ambient ratio silently gone. The rendered frame was
   byte-identical to the one before the rig existed, which is how it was caught. Same failure shape
   as the clearances in ADR-067 — an in-memory value with no serialization, in a pipeline that
   round-trips through a file.

Measured on the generated valley after these: shadow fraction 0.0 → 0.21, saturation 0.38 → 0.68
(Glowmere itself is 0.716), and the `lightrig/...` parameters stopped being reported as unknown on
reload.

867 unit tests pass serially. Under `-j4` two pre-existing filesystem-collision flakes appear
intermittently (project asset relink, composition parameter registration); both pass alone and both
predate this work.
