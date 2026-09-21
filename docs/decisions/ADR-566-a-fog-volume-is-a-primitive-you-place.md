# ADR-566: a fog volume is a primitive you place, and a bound is a claim the field can refute

Status: accepted. Date: 2026-09-21. Phase C of the Fog Bank brief (§46 C, §9).

## Context

ADR-563 gave the fog bank a shape: an ellipse in plan with a height profile, analytic, no noise in
it. That is *one* shape. The brief's §9 asks for five more -- **sphere, ellipsoid, box, capsule,
cylinder**, each placed, scaled, rotated, with density, softness, falloff, height influence -- and
for the artist to be able to put several of them around a world.

The reason that is a phase and not a row of extra sliders is in ADR-563's own closing note: *the
hero shot still cannot show a placed medium.* A 900 m bank centred under the island **encloses the
camera**, and a volume you are standing inside has no silhouette. Every arm rendered through
Phases A and B came back a wash. A local primitive is the first medium small enough for a camera to
stand outside and look at, which is why C is the phase where this work stops being a number.

## Decision

### 1. One normalised distance, six shapes

`fogPrimitiveDistance` returns **1 at the surface, less inside, and the field is zero past 1.35**
for every shape. Everything downstream was written against that threshold for the bank alone -- the
rim smoothstep, the early-out, the ray interval, the CPU twin -- and keeps working for five more
shapes because they are normalised the same way, not because each was given its own cutoff. A
per-shape cutoff would be five more numbers for a bound to get wrong, and the second half of this
ADR is about a bound that got **one** number wrong.

The forms are the cheap ones: a `max` for the box, a clamped segment for the capsule. Every one is
evaluated per sample per slot, and the C0 creases a `max` leaves on a box's edges are smoothed by
the rim's smoothstep, which is already there and already wide.

Two deliberate asymmetries:

- **the sphere ignores `bankLength` and `thickness`.** A sphere an artist has to set two other
  controls to 1 to get back is not a sphere, it is a trap;
- **the Bank ignores `heightInfluence`** -- and the reason is the bound rather than the picture. A
  bank's rim is horizontal, so the vertical profile is the only term that makes it end in Y. Blend
  that toward 1 and the bank becomes an infinite vertical column inside a finite bound: this ADR's
  own defect, reintroduced through a control instead of through a constant.

### 2. `FieldType::Choice`, and it serialises as a name

A primitive selector drawn as a 0..5 slider is a control an artist has to decode. So the registry
gains a fourth row type. It is a **float index** everywhere a number is what the machinery wants --
`values`, a project parameter (ADR-264), a modulation route's clamp -- because a second scalar
representation would double every conversion in `effect_registry.cpp`. It is written to JSON as its
**name**, because a scene file outlives the order of an enum: appending a seventh primitive must not
silently change what every saved bank is, and an index in a file makes exactly that mistake
available. An unknown name leaves the default rather than selecting index 0, so a file from a newer
build degrades to "unchanged" instead of to "Bank".

`FieldType` has no `default:` arm anywhere it is switched on, so adding the case was eight compile
errors and no silent gaps. That is the property to keep.

**And the row registers as an `Int`, which the conformance suite had to tell me.** It first
registered as a float like every other row, and `effect_conformance`'s round trip failed on the
first full run:

```
fog [round-trip] 1 component(s) did not survive save -> load;
the first of them: atmos/conformance probe/shape
```

It was right. The probe sets each parameter to a fraction of its range -- 1.55 here -- the
serialiser writes the name of the index 1.55 rounds to, and what loads back is 2. **A control whose
parameter can hold 1.55 has states the file cannot represent**, and a modulation route or a
keyframe could put it in one. `params::Components<int>::set` rounds on the way in, so registering
as an Int makes the whole chain -- slider, route, project parameter, save -- integral, and the
representable states are exactly the choices.

The probe needed one change of its own to stay honest: a fraction of an Int's range is a value it
cannot hold, and for a **two**-state Choice every fraction would round back to the default, leaving
the probe asserting that the default survives. `distinctValue` now quantises and, if that collapses
onto the default, walks the integers. **A probe that cannot fail proves nothing** (ADR-182), and a
probe made to pass by rounding is one step from that.

### 3. A bound is a claim about the support of a field

**This is the larger half of the ADR and it was found by building the primitives, not by looking
for it.** ADR-562 §4 gave every medium a ray interval -- a vertical cylinder the march clips to --
and that interval is a claim: *every non-zero sample of this slot's field lies inside it.*

The asymmetry that makes the claim worth checking: **the march's step positions do not depend on
the bound.** The same `steps` over the same `maxDistance`; the interval decides only whether a step
evaluates the field or skips it. So a bound that claims too much costs field evaluations and
nothing else -- no quality change, no sampling change. A bound that claims too little **deletes
part of the medium**, and deletes it in the way that is hardest to see: the volume is still there,
still soft-edged, still the right colour, and simply smaller than the number the artist typed.

**When a bound is uncertain, be generous.** The only currency it spends is samples that were going
to be taken anyway.

It was tight in two places. Both were exactly right at the defaults, which is why a day of
rendering never showed either:

| | the bound said | the field reaches | clipped at the control's end |
|---|---|---|---|
| horizontally | `radius * 1.35` | `radius * bankLength * 1.35` | **71% of peak density** outside, at `bankLength` 4 |
| vertically | `thickness * 3` (a **Gaussian**'s tail) | an **exponential** at rate `heightFalloff` | **56% of peak** above the ceiling, at `heightFalloff` 0.12 |

The vertical one is the more interesting mistake. `3 * thickness` is where a Gaussian ends, and it
was inherited from the vortex, whose wall *is* a Gaussian. ADR-563 replaced the fog's vertical
profile with a base and an exponential falloff and did not revisit the bound -- and at the default
falloff of 1.4 the old constant left 0.37% of the column outside, so it was still right. **A
constant that was derived from one function and is still correct under another is a coincidence,
and the control that ends the coincidence is the one nobody moved.**

**This is ADR-389's family, and it is the clearest instance of it in the tree.** ADR-389: *a
coefficient tuned against a quantity is invalidated by a change to that quantity's distribution.*
The three-sigma rule was not a guess -- it was **correct**, for the distribution it was written
against. What invalidated it was ADR-563 changing the shape the constant described, in a different
file, one ADR earlier, **by me**. Nothing connected the two: no test failed, no comment pointed
from the profile to the bound, and the bound's own comment went on describing a Gaussian tail that
no longer existed.

So the rule this ADR is asking for is narrower and more actionable than "audit your constants":
**when you change the SHAPE of a quantity, grep for the constants that were sized against the old
shape.** They are not near the change -- that is the point of them being constants -- and they do
not announce themselves. In this case the search term was `thickness * 3`, and the thing that
would have found it is asking, at the moment of writing the new profile, *what else knows how far
this reaches?*

The fix: carry `bankLength` horizontally, and solve `exp(-h * falloff) = 0.01` vertically, floored
at the old three thicknesses and capped at forty. 1% of the column left outside, which is below
what a frame can show.

### 4. The bound is a C++ function now, and that is the point

Until this ADR the claim lived only in WGSL, so the only thing that could check it was a rendered
frame -- and a rendered frame cannot tell you a bank is three times too short unless you already
know how long it should be. `world::mediumBound` is the transliteration, so `test_medium_bound.cpp`
can put the bound and the field in front of each other: sample the field on a grid, and assert that
everything it says is dense lies inside what the bound says is possible.

The property comes in two halves, and they are different assertions on purpose. A closed primitive
is **exactly zero** past its surface, so no tolerance is owed. A bank has no lid, so a finite bound
must cut something and the only honest question is how much -- 1.2% here, against the 56% and 71%
the old bound left.

And a third assertion that is not about containment at all: **the bound must not be vacuous.** A
bound of infinity contains every field perfectly. So the case also measures how far the field
actually reaches and requires the bound to be within about a factor of two of it. Without that, the
first two assertions are satisfiable by giving up.

### 5. `packMediumSlot`: one writer for the bytes the march reads

Packing a medium is three steps in one order -- the reserved-lane sentinel, the kind's packer, the
kind tag -- and until now the order lived inside `buildAtmosphericFrame`. A test that wanted those
bytes had to call `pack` and then write the tag by hand, which is a second copy of the very
ordering rule ADR-565's sentinel exists to enforce. ADR-554: a seam published from two places must
be written by both. So it is published from one.

## Consequences

- **The conformance round trip is the third time this session a registry-wide probe has reported a
  true consequence of a local change.** It is worth naming why it works: it does not compare
  `toJson(fromJson(x))` with `toJson(x)` -- that identity holds even when a field is missing from
  *both* directions -- it re-registers the reloaded effect and compares each parameter's default
  against the value set before the save. It asks the only question worth asking: did this number
  survive?
- **The fog block is ten stored rows**, up from eight: `shape` and `heightInfluence`.
  `test_effect_registry` notices, as it is meant to. The number was taken from
  `grep -c '    storedFloat('` plus `grep -c 'storedChoice('` on the effect file, not from the
  failure -- a count copied out of red output is a test that now agrees with whatever the code does.
- **The panel's sections were wrong and are fixed on the way past.** `agent/tornado`'s section guard
  found it: `heightFalloff` declared "Structure" on the Advanced page while `detailAmount` was Main,
  so the Main page drew no separator at all and Detail amount landed under whatever preceded it --
  and "Structure" was declared **twice** on the Advanced page, which draws the same header twice.
  The detail rows now carry their own "Detail" section on both pages.
- **Six arms and a picture.** `tools/make_fog_primitive_arms.py` writes one arm per primitive,
  identical in every number except `shape`, placed 313 m down the camera's own look direction so
  the camera is outside all six. The silhouettes are distinguishable at a glance: the bank is a
  shelf with no lid, the box has a flat top and a vertical face, the cylinder has a level lid over
  straight sides, the capsule has parallel sides and a rounded cap where the ellipsoid tapers.
  **This is the first frame in the fog work that shows a placed medium rather than a wash.**
  - The first two placements did not. One put the camera inside the soft rim and rendered the same
    featureless white ADR-560 and ADR-563 had each recorded -- **the third time that specific
    mistake has been made on this branch**, which says the finding was written down and the
    *procedure* was not. It is now: a placement arm states the camera-to-volume distance and the
    volume's longest half-axis, and the first is larger than 1.35x the second or the arm is not
    rendered.
  - The arms run at 256 steps. They are shape arms, not cost arms: at the shipped 32 the march's
    own sampling grain is louder than the silhouette and all six read as the same speckled blob --
    **and ADR-577 corrects what that speckle is.** "The march's own sampling grain" reads as the
    per-pixel start jitter and it is not: measured, the jitter's contribution to a frame's
    high-frequency content at 32 steps and above is a ratio of **1.000** against the jitter off.
    The speckle is the medium sampled too coarsely along the ray; the jitter is what breaks up its
    banding rather than what causes it. The arms are still right to run at 256 steps and the
    sentence below is still true; only the cause named here was wrong, and a misattributed cause
    sends the next person to the wrong knob --
    §48's point made against my own diagnostic. Cost is Phase I's, with its own arms under the lock.

## Revisit when

- **A primitive wants a full rotation.** Only yaw is carried (lane 13's cos/sin), because a volume
  that sits in a world is oriented about up. A tilted capsule needs two more lanes and the fog
  block has none spare below 15.
- **Several primitives want to be ONE effect.** §9 says "several fog banks", and today that is
  several effects against `kMaxMedia` of 4. Raising the cap is a measurement (ADR-562 §8), and
  composing primitives inside one slot is a different design -- a union of distances rather than a
  dispatch to one.
- **The bound's 40-thickness ceiling is reached in a real scene.** At `heightFalloff` 0.05 it
  leaves 13% of the column outside. That is the one place the bound is knowingly allowed to lie,
  and at that setting the "bank" is global haze rather than a placed volume -- which may mean the
  control's bottom end is wrong rather than the bound's top.
