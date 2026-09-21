# ADR-579: a preset that continues is not a starting point, and a heading drawn twice is a defect

Status: accepted. Date: 2026-09-21. Phases J and K of the Fog Bank brief (§36, §37), with two
directed corrections.

## §36 -- the panel

Enumerating the fog panel as an artist meets it -- section, page, label, row -- found three things
that reading the file does not.

**A section heading drawn twice on one page.** `EffectField::sec` marks a row as the *start* of a
section and `drawSchemaRows` emits a separator when it meets one, **on the page it is drawing**. Two
rows with the same section name, separated by other rows of that page, draw the heading twice with
unrelated controls under each. `agent/tornado`'s guard found one instance and ADR-566 fixed it;
enumerating again for §36 found another the first fix did not cover.

**So it is a check now, not a fix.** `test_effect_registry` walks every kind and every page and
refuses a repeated section name -- and it caught **two** on its first run, one of them in the
**vortex**, which nobody had noticed:

| kind | page | heading |
|---|---|---|
| vortex | Main | `Cyclone structure`, reopened at `innerVoid` |
| fog | Advanced | `Structure`, reopened at `churn` |

**A defect that recurs in the same file after being fixed once needs a check rather than a fix.**

The repairs are structural rather than renames: the vortex's `spill`/`scattering` are *lighting*
and were sitting inside "Cyclone structure" -- which is *why* `innerVoid` had to reopen the heading
to get out from under them -- so they get a `Light` section and the eye radius gets `Eye`. On the
fog side the second "Structure" block is the noise stack, which is §36's **Detail**; and `Contrast`
**moved** to sit with the two rows it works with, because giving it its own `.sec` reopened
"Density curve" a second time -- the very defect the guard had just caught, reintroduced by the
fix for a different one.

The first eight rows carried no heading at all and now open §36's **Fog** and **Appearance**.

**And two artist-facing tooltips were stale in the way the file header was.** "Fog density" said
*"per metre travelled"* -- ADR-564 made it an optical depth through the bank. "Bank height" said
*"a soft Gaussian rather than a slab with a lid"* -- ADR-563 replaced the Gaussian and ADR-568 gave
it a lid. **A stale tooltip is the file-header defect in the place a person actually reads.**

## §37 -- the presets, and the one they were sitting on

§37 names ten. There were three, and they are renamed into the ten rather than kept beside them
(ADR-441). Nothing ships with a fog effect, so nothing migrates.

**But the presets were not starting points.** `applyStyle` opens with `v = Vortex{}` under a
comment that is exactly right -- *"a fog preset applied to an effect that was a vortex a moment ago
must not leave a spiral and a throat behind, and a preset that only set what it wanted would"* --
**and that argument covers half the parameters.** Since ADR-566 a bank's shape, drift, density
curve and glow height live in `AtmosphericEffect::values`, and nothing reset those. A preset applied
after an artist set Shape to Box got a box.

The reset reads the **schema's** declared defaults rather than a list, so a row added tomorrow is
reset without anybody remembering -- which is precisely the failure this was.

The property, stated so it cannot be satisfied by accident: **applying B gives the same effect
whether or not A was applied first**, tested over every ordered pair of styles with an artist's
edits in between. Break: delete the reset and it fails on the first pair.

### The presets were tuned by measurement, and two of them were wrong

A hidden instrument (`[.]`) prints each preset's density at its centre, at its floor and half way
out. On its first run:

| preset | centre | why |
|---|---|---|
| **Horror Fog** | **0.000** | `heightFalloff` 4.0 with a 0.3 threshold |
| **Dense Cinematic** | **0.035** | `heightFalloff` 1.0 with a 0.15 threshold |

**ADR-571's density threshold subtracts from the shape *after* the vertical profile**, so a steep
profile and a high floor clear everything but the base. Neither control shows that on its own, and
**a preset that is empty where an artist points at it is not a starting point.** Both retuned
against the instrument.

### And the case that caught it was itself asking the wrong question

"A fog bank is filled to its own axis" sampled radially **at y = 0** and required every sample
within 2% of the centre. Three things were wrong with that once ten presets existed:

- **the tolerance was a proxy for a property.** ADR-571's density curve legitimately makes a bank
  fall away faster than 2% by half a radius, so the curve working looked exactly like the defect.
  It asserts the property now -- **the axis is the maximum**, which is what a hole means;
- **y = 0 is not where a ground-hugging bank is.** `groundHug` slides the densest layer between the
  floor and the top, and for three of the ten it is one thickness *below* the centre. The probe
  samples the densest layer now, computed from the row;
- two neighbouring cases sampled a fixed multiple of the radius along the **long** axis, which
  stops being outside the bank when a preset sets a longer one. They sample the short axis, whose
  half-extent is `radius` whatever the length is.

**A test coupled to a preset's values fails when the preset is tuned, which is not what it is for.**

## The two directed corrections

- **`volumetricStrength` stays at 1.0 and the cost is disclosed where the decision is made.** The
  multiplier is only paid when the self-shadow march is on, and that is off by default -- so the
  default costs nothing today and moving it would change the look of every scene with a light. The
  Environment panel now says, beneath the shadow-steps row and only when it is above zero, that the
  cost scales with how many lights light the fog and roughly what that is. Same remedy as putting a
  number where it is read rather than in an ADR nobody opens.
- **Preview's `volumeStepScale` stays at 0.5 and the claim is corrected.** `render_quality.hpp` said
  the lever "buys depth banding back and almost no time"; ADR-577 measured **28%** on a placed
  medium. The header now carries both that and the threshold it buys -- 1% grain at 16 steps and
  optical depth 12 -- and says the value is left alone deliberately, because an interactive tier's
  quality is the artist's to feel.

## Consequences

- **`storeKey` is now used from an effect file**, which is the first time a kind has reset its own
  stored rows. It is the right seam: the schema knows the defaults and the effect does not have to.
- **The registry guard is worth more than either fix it prompted.** It is nine lines, it covers
  every kind, and it found a defect in a kind this branch does not own.

## Revisit when

- **A preset wants to leave a row alone.** Every preset now starts from the declared defaults, so
  "inherit what the artist had" is no longer expressible. Nothing wants it today and the §37 line
  -- *"presets are starting points"* -- argues it should stay that way.
- **§36's Quality group is wanted on the panel.** It is the one group of the eight with no rows
  here, because the quality tier is a renderer setting rather than an effect's. Whether an artist
  should reach it from the fog panel is a UX question, not a fog one.
