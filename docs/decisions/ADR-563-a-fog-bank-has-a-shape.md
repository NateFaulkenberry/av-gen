# ADR-563: A fog bank has a shape, and structure without appearance cannot be evaluated

- Status: Accepted (2026-09-20)
- Implements the brief's §46 B (the clean analytical volume) and **reorders §46 C after §23/§24**.
  Closes ADR-560's finding that §44's first quality bar was unreachable by construction. Builds on
  ADR-562's medium slots and kind dispatch.

## 1. What was wrong

ADR-560 measured a fog bank's density field as

    rim(rr) * exp(-y^2 / thickness^2)

-- monotone in radius, **completely uniform in angle**, symmetric in height. A circular grey disc.
Every feature anybody had ever seen in a fog bank came out of the fBM stack underneath, which is the
"procedural texture rendered as volume" the brief opens by rejecting, and it is why *noise disabled,
still looks like fog* was unreachable **by construction rather than by tuning**.

## 2. What was built

`shaders/fog.wgsl`: the fog bank's own analytic field, dispatched on ADR-562's kind tag.

- an **elliptical, rotatable footprint** -- the single term that breaks uniformity in angle, and the
  cheapest structure available. A fog bank in the world is not a disc; it lies along a valley.
- an **asymmetric vertical profile** built from a base and a falloff rather than a centre and a
  width, because fog sits ON something and is densest near its base.
- **edge softness independent of size**.

No noise anywhere, and that is a constraint on the file rather than a description of it: the moment
it samples an fBM, the bar it exists to make reachable becomes unreachable again.

`src/world/fog_field.cpp` is the CPU transliteration reading the **same packed lanes**, so there is
nothing to copy and nothing to drift -- ADR-401, ADR-561 and ADR-562 each found a hand-written copy
of one conversion that had.

Six controls in `EffectValueStore` rather than on `world::Vortex`, which is shared with the tornado.

## 3. The kind tag was never uploaded

ADR-562 declared a kind tag as part of the slot contract. It was set on the CPU, compared by
`frameDiffers`, and **dropped by the renderer**, which uploaded only the lanes -- so no shader could
read one, and the coordinator relayed "the march dispatches on the kind tag" to another agent as a
live fact.

**Built-but-unreachable, one day old, inside the foundation written to fix that family.** The lesson
is not that someone was careless: it is that **the family is the default outcome of writing a
producer and a consumer in separate commits, and awareness does not prevent it** -- four ADR sections
and five `docs/testing.md` entries on exactly this did not stop a fresh instance within a day.

Fixed **centrally**, in `buildAtmosphericFrame` after each kind's `pack` returns, so a new kind
cannot forget. Structural, not a checklist, for the same reason.

## 4. The reorder: §46 C goes after §23 and §24

The brief's §46 runs B clean volume, C local banks, D height and distance. **C is deferred until
after §23 (softness over detail) and §24 (density remapping).**

The measurement forcing it: with the structure built and detail at zero, a bank rendered from
outside is *"an elongated, rotated, soft-edged mass that is also far too opaque, with a uniform
interior -- nobody would call either image fog."* Structure delivered, appearance absent.

**§46 C's primitives -- sphere, ellipsoid, box, capsule, cylinder -- are judged by eye, and the eye
cannot judge them yet.** Five primitives that all read as solid slabs are five slabs. Building the
brief's artist-facing feature into a medium where its results are invisible, then tuning it against
an appearance not yet fixed, is the same error as measuring a cost ladder in a geometry the decision
was not about (ADR-562 §8).

Two further reasons, and they are about composition rather than sequencing:

- **§24's density remapping is a transfer function over the field just built**, so it composes with
  the ellipse rather than competing with it.
- **§23's softness changes what "edge" means**, which is precisely the property §46 C's primitives
  are *defined by*. Doing C first means defining primitives against an edge semantics about to
  change.

### This is the second inversion of this programme's order, and they read together

ADR-562 inverted §46 A→B to build the foundation first, because *§46's ordering assumes the
foundation exists and it did not*. This inverts B→C, because **the spec's order assumes appearance
follows structure, and the measurement says structure without appearance cannot be evaluated.**

Both inversions were forced by measurement rather than preference, and both have the same shape: the
brief describes a dependency order that is correct in the abstract and wrong about what this engine
already had. A spec's ordering is a hypothesis about the codebase.

## The probe (ADR-182)

ADR-560 recorded `test_fog_bank.cpp`'s strong form **inverted** -- asserting the field is uniform in
angle -- as the "before" §46 B had to break. It is flipped: the case now requires angular variation
with detail at zero.

Broken deliberately by making the footprint circular again (`along = 1.0`): the angular spread drops
to **1.19e-07** and the case fails. So it is the ellipse and not noise. The case carries its own
control -- a **circular** bank must still be uniform in angle -- without which the assertion would
pass on any field that merely varied.

And a finding from the probe's own first version: sampled at 0.45 of the radius it read **exactly
0**, because at default softness the bank is a flat plateau to ~65% of its radius and the ellipse
lives in the **silhouette**. A probe that samples the plateau is indistinguishable from the feature
not working. It samples 0.9 now, and the limit is stated in the case.

## Consequences

- **A fog bank is no longer a pure alias of the vortex's block.** It has six stored rows, so every
  effect of every kind now writes six more numbers in its `fog` block (`toJson` writes every kind's
  block). Defaults, so nothing loads differently -- which `test_effect_registry` checks and still
  checks, with the assertion updated from `empty()` to `size() == 6`.
- **Lane budget**: the vortex fills 0-12, fog adds 13-14, **15 is reserved for the kind tag**.
- **The hero shot still cannot show a placed medium.** The first acceptance arm was built on it
  anyway and came back a featureless wash, for the reason ADR-560 had already recorded: the bank
  encloses the camera. The camera has to be outside.

## Revisit when

- **§23 and §24 land.** The bar is reachable; it is not met in the sense an artist would mean, and
  the next ADR should say whether it is.
- A fog bank wants a genuine hole, a ring of mist around a clearing. `edgeSoftness` and the ellipse
  do not express one; that is §46 C's primitives.
