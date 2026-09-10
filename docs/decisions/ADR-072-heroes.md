# ADR-072: Heroes

Status: accepted
Date: 2026-09-10

## Context

A composed world without heroes is a texture. It can be dense, well lit and correctly hierarchical
and still contain nothing that rewards being approached, because every instance of a scatter layer
is interchangeable with every other instance of that layer — that is what a scatter layer *is*.

## Decision

A `HeroPoint` is one object, in one place, that the composition is arranged around. It carries what
a camera director needs in order to discover it: importance, focal weight, preferred stand-off and
elevation, activation radius, its own size, and a reaction profile.

**Importance descends and never ties.** Heroes that all claim 1.0 are heroes among which nothing can
be chosen, which is the same as having none.

**A hero is meant to be an authored assembly, not a generated shape.** Glowmere's elder is three
procedural nodes plus a practical light; its silhouette works because the parts have wildly
different scales and because the filaments move while the cap does not. No parameterisation of "a
mushroom" produces that, and a generator general enough to try would produce a worse elder and be
used by nothing else. The composer currently places a *single asset* per hero, which is the fallback
and is honestly weaker: it gives a large silhouette in a cleared space with a camera that knows how
to approach it, and not a designed object. `HeroPoint::assembly` is the seam for the real thing.

**Most heroes do almost nothing.** Reaction profiles are authored and small — `monument` (still, its
runes light on the beat), `organism` (opens slowly with low-frequency energy), `craft` (hovers, its
lights on the beat clock), `still` (almost nothing). A profile may hold at most four reactions;
that is not a technical limit but the observation that a hero doing five things at once is doing
none of them legibly. Only the top two heroes in a world get a lively profile.

Reaction sources are validated against the signals the modulator actually publishes. A reaction
naming a signal that does not exist never fires, which is indistinguishable from a hero that does
not react — the kind of failure found by staring at a scene rather than by reading an error.

## Three things the renders caught

Each of these was reasoning that looked sound and was wrong, and each was found by looking at a
frame rather than by thinking harder.

1. **Clearance sized off camera distance emptied the world.** Stand-off is about three times a
   hero's height, so scaling clearance from stand-off made a tall hero clear a radius comparable to
   its own stand-off. Five heroes in a 400 m world took the instance count from 762k to 68k and the
   frame was a bare hillside with five objects on it. Clearance is now sized from the hero's own
   footprint, and `HeroPoint` carries its `radius` and `height` so nothing has to hold the asset
   library to find out how big something is.

2. **Heroes were too big for the world they stood in.** A 44 m hero pushes its camera 133 m back,
   and foreground vegetation is culled at 90 m — so the shot was a large object across empty ground.
   Heroes are now sized against the world (Glowmere's elder, for reference, is about 20 m), and the
   viewpoint is clamped so it never stands further off than the foreground survives.

3. **Placement was blind to the terrain.** The composer chose hero positions from a hash with no
   knowledge of the ground, so the subject landed on scree about as often as the map is scree — and
   the viewpoint went with it, because the viewpoint is chosen to look at the subject. The world was
   dense everywhere except the two places that mattered.

   The fix moved terrain construction out of `installWorld` and into the composer as
   `world::terrainFor`. That is where it belonged anyway: the composer decides where the viewpoint,
   the corridor and the heroes go, and all three need to know what grows there. Heroes now try ten
   candidate positions and the viewpoint twelve bearings, scored by `groundScore`, which weighs
   forest over meadow over marsh, ignores the bare biomes entirely, and penalises slope — a subject
   on a cliff is a subject the camera cannot stand in front of.

## Consequences

- `GeneratedWorld` carries the asset library it was composed from, so installation can resolve hero
  asset ids to files. The alternative — writing absolute paths into every hero — bakes one machine's
  filesystem into a saved world.
- The focal region is now derived from hero 0 rather than from a second search for "the best asset".
  Two independent answers to the same question is how a world ends up framed on something other than
  the thing it put in the clearing.
- Two existing composer invariants changed, deliberately. A canopy clearing is *supposed* to sit on
  a hero — that is what gives it room to read — so only regions that empty the ground as well count
  as holes in the composition. And `negativeSpace: 0` no longer means "no voids", because neither
  the corridor nor the hero clearings are discretionary; it means no *discretionary* emptiness.
