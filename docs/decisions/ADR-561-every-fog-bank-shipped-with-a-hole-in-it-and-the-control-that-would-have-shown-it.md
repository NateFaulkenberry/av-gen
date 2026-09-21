# ADR-561: Every fog bank shipped with a hole in it, and the one control that would have shown it

- Status: Accepted (2026-09-20)
- Two defects that were **live in the product on 2026-09-20**, found while diagnosing ADR-560 and
  fixed ahead of the rebuild because both are cheap and both are visible to an artist today. They
  are recorded separately rather than swept into the rebuild deliberately: a defect that disappears
  into a redesign is a defect nobody can date, reproduce or learn from.
- Related: ADR-560 (the diagnosis these came out of), ADR-421 (a control that does nothing teaches
  an artist the system is broken), ADR-369 (no edge anywhere for a hard line to live on), ADR-374
  (where the 0.22 came from), ADR-401 (a test can be green about a path nobody renders), ADR-500
  (an effect is one file and four lines), ADR-182 (a probe that cannot fail proves nothing).

## 1. Every fog bank had a hole in its middle, and the hole was a cyclone's eye

`AtmosphereKind::VolumetricFog` is a different authoring surface onto the vortex's field with the
funnel switched off (ADR-500's second proof, and the argument is sound). `applyStyle` writes the
four numbers that make it a bank rather than a funnel -- `swirl`, `funnelDepth`, `throat`,
`throatDensity` -- and sets `innerVoid` to 0 so that it is a filled disc and not a ring.

**`innerVoid = 0` does not remove the eye.** The envelope's eye term is

    eye = smoothstep(innerVoid, innerVoid + eyeWallWidth, rr)

and `eyeWallWidth` defaults to `0.22` -- the constant ADR-374 hardcoded for the *vortex*, promoted
to a field with its old value as the default so that nothing moved. `applyStyle` never reset it, so
with the void at zero the density still climbed from **exactly zero on the axis** to full at 22% of
the radius. Measured on the shipped `Valley Mist` preset:

| rr | 0.00 | 0.05 | 0.10 | 0.15 | 0.20 | 0.25 |
|---|---|---|---|---|---|---|
| envelope | **0.0000** | 0.1315 | 0.4320 | 0.7607 | 0.9767 | 1.0000 |

On the 900 m bank ADR-560 rendered, that is **a 200-metre clearing in the middle of the fog**, and
`eyeWallWidth` is not a row on the fog panel, so there was no way to close it. Every fog bank an
artist has ever made in this product has had it. The owner reported the fog looking wrong; this may
be part of what they were looking at.

### It survived in three places, and the test found the two I had not thought of

The first fix was one line in `applyStyle`. It was not enough, and each further layer was found by
the test failing rather than by reading:

1. **`applyStyle` never reset the default.** Fixed: `v.eyeWallWidth = 0`, and `eyeWallGain` with it.
2. **`radialProfile` floored it**: `wallW = max(v.v7.x, 1e-3)`, so a zero became the narrowest eye
   expressible rather than no eye. `smoothstep(eyeR, eyeR + w, eyeR)` is **exactly 0 for every
   positive w**, however small, so the 200 m hole became a sub-metre pinhole with the density still
   falling to nothing inside it -- invisible, and still a hard line where ADR-369 says there must
   not be one. Fixed: **a width of zero means no eye**, which is what an artist means by it, and the
   branch returns the rim alone.
3. **`packVortex` floored it again**, `max(f.eyeWallWidth, 1e-3)`, so the zero could never reach the
   profile at all. Fixed: clamped from below at 0 only, because a negative width is not a shape.

`eyeWallGain` goes with the eye, deliberately: the gain is the *wall's*, and a structure with no eye
has no wall for a ring of extra density to stand on. A crest of zero width is degenerate arithmetic
rather than a picture.

**This is a change to the shared field**, so `core/vortex.cpp` and `shaders/vortex.wgsl` move
together, in the same place and the same order, and `test_vortex_parity_gpu.cpp` is what holds them
to it. It is additive: nothing in the tree authors a zero width -- the shipped Tree of Life leaves
the 0.22 default and only the `_vx2-` comparison arms set it, to 0.1 -- so no existing frame moves.

## 2. The one control the brief is judged on was not on the panel

`cloudNoise` is the weight of the whole fBM stack against a flat field of the same mean. It is the
brief's §4 D (noise disabled), its §44 bar 1 (*noise disabled -> still looks like fog*) and its
Definition of Done (*with all noise and detail at zero the system must still produce clean, coherent
fog*), all in one parameter.

**The vortex declared it. The fog bank did not.** So the single arm the whole brief is judged on was
not reachable from the fog panel, could not be registered, could not be modulated, and could not be
aimed at by a route. Every diagnostic arm in ADR-560 had to be hand-authored into JSON, where it
round-trips only by accident of `AtmosphericEffect::toJson` walking **every** registered kind's rows
rather than just the effect's own.

It is a `main()` row now, called **Detail amount**, on the vortex's own JSON path so the two rows
write one key and cannot disagree. Four lines in one file, which is ADR-500 working as advertised.

ADR-421's family, and the sharpest instance of it yet: not a control that does nothing, but a
control that does the most important thing in the specification and could not be found.

## 3. A third thing, which is why the tests can exist at all

ADR-401 recorded that `vortex::packVortex` had exactly one caller -- the parity test -- because the
renderer kept its own hand-written copy of the clamps, so the bytes the shipped frame marched were
produced by code no test could reach.

**The step before it had the same defect and it was not noticed at the time.** The conversion from
the authored `world::Vortex` to `vortex::VortexField` -- twenty-four members, by hand -- lived inside
`rendering/volume_renderer.cpp` and nowhere else. A CPU test that wanted to ask "what is this
authored fog bank's density at its own centre" would have had to write that conversion out a second
time and would then have been asserting about its own copy.

It is `world::vortexFieldOf` now, in the library beside the struct, with the renderer as one caller
and the new `tests/unit/test_fog_bank.cpp` as the other. The test asks its questions through the
**same three calls** the renderer makes.

## The probes, and the failures that earn them (ADR-182)

`avgen_tests "[fog]"`, three cases, 61 assertions. Each was demonstrated failing against a
deliberately broken build before being accepted:

| break | result |
|---|---|
| `v.eyeWallWidth = 0` removed from `applyStyle` | **10 of 61 assertions fail**, `axis.envelope > 0.99` with expansion `0.0f > 0.99000001f` on all three styles, and the radial ladder fails at rr = 0.05 with `0.131480098f` |
| the `detailAmount` row removed | **1 assertion fails**, `REQUIRE( row != s.fields.end() )` |

The reachability case is deliberately more than an existence check: the row's setter must move the
member, the member must move the packed bytes, **and** the packed bytes must move a sample of the
field. That is the probe shape ADR-460's parity work caught a live defect with ("changing
`innerVoid` moved 0 of 160 GPU samples"), and a packed byte no sample depends on is the same defect
one layer down.

The third case is written in its **weak** form on purpose, and says so in its own comment: ADR-560's
finding is that with the detail at zero this field is uniform in angle, so a test asserting §44's
bar today would be a test that cannot pass. What it asserts instead is the floor -- that the field
varies at all -- plus the strong form recorded **inverted**, as the "before" that must break when
§46 B lands:

    CHECK(hi - lo == Approx(0.0f).margin(1e-6f)); // ADR-560 §3: uniform in angle. Invert here.

So the bar is in the suite as the thing that goes green, rather than in a document as the thing
somebody remembers.

## Consequences

- **A fog bank is filled to its axis**, and `eyeWallWidth = 0` means "no eye" everywhere in the
  engine rather than "the narrowest eye expressible".
- **`packVortex` no longer floors `eyeWallWidth`.** Anything that relied on the floor to avoid a
  divide is now relying on the profile's early return instead; there is one such site and it is the
  one changed.
- **`world::vortexFieldOf` is the conversion**, and a second hand-written copy of it in a renderer
  or a test is now a regression rather than a duplication.
- **Neither fix waits for the rebuild**, and neither is a substitute for it. The bank is filled now;
  it still has no shape in it, which is ADR-560's finding and §46 B's job.

## Revisit when

- Somebody wants a fog bank with a genuine hole in it -- a ring of mist around a clearing. That is a
  legitimate shape and it is now unauthorable from the fog panel, which is the right trade while
  `eyeWallWidth` has no row: the alternative was every bank having one by accident. When §9's local
  volume primitives land, a ring is a primitive rather than a leftover of a cyclone's eye.
