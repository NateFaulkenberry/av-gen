# ADR-564: Fog density is an optical depth, so a preset can ship one

- Status: Accepted (2026-09-20)
- The brief's §23 (softness over detail) and §24 (density remapping), taken before §46 C on
  ADR-563's reorder. Extends ADR-563's analytic field. ADR-389's family: a coefficient tuned
  against a quantity is invalidated by a change to that quantity's distribution.

## The defect

`Vortex::density` is an extinction coefficient **per metre**. The optical depth through a bank is
that coefficient times the path, so it scales with the bank's **size**. Measured, with the shipped
`Valley Mist` value of `0.0016` and a `bankLength` of 2.8:

| radius | crossing path | optical depth | reads as |
|---|---|---|---|
| 100 m | 560 m | 0.45 | fog |
| 300 m | 1680 m | 1.34 | fog |
| 600 m | 3360 m | 2.69 | **opaque slab** |
| 900 m | 5040 m | 4.03 | **opaque slab** |

**An artist crosses that range by dragging the radius slider without touching density at all.**

And the consequence that makes it more than a nuisance: **no preset can ship a correct density**,
because there is no number that is right at two sizes. Every fog arm rendered during ADR-560 and
ADR-563 was either a wash or nothing, at values the presets themselves ship. That is why the bank
"did not look like fog" after §46 B gave it a shape -- the shape was fine and the unit was wrong.

## The fix

The authored number is an **optical depth through the bank's long axis**, converted to a per-metre
extinction in `packMedium` by dividing by that crossing. `1.0` means "you can just see through it"
at any size. `Environment::volumeAbsorption` still scales it as a scene-wide multiplier, which is
what that control is for.

The long axis rather than the thickness because it is the direction a bank is usually looked
*through*: fog is a thing you see into horizontally, not a lid you look down at.

The three presets are retuned to the new unit, which is required rather than tidy -- ADR-389's rule
exactly. **No authored content breaks, because zero scenes author a fog effect** (measured in
ADR-562). This is the one change in this programme where built-but-unreachable was an advantage.

## The probe (ADR-182)

`one fog density reads the same at any bank size`: the optical depth at radii 100, 400 and 900 must
agree to within float rounding. Broken deliberately by restoring the per-metre packing:

```
optical depth at radius 100 / 400 / 900:  800 / 3200 / 7200
```

A ninefold spread where the fix gives one constant. Two controls, both necessary: the depth must not
be **zero** (which would satisfy equality vacuously), and the authored number must still **change**
it (or the test would pass on a packer that ignored the control entirely).

## A measurement trap this produced

Comparing the two sizes by whole-frame mean luminance gives 49.0 against 83.8 and looks like a
refutation. It is not: a 900 m bank **covers more of the frame** than a 250 m one however
transparent it is, so that number conflates coverage with opacity. The images settle it -- stars are
visible through both and neither is a slab.

The general form, which is `docs/testing.md`'s family C again: **a metric that aggregates over the
frame cannot separate "how much of the picture" from "how much of the light".** When the change is
about opacity, measure where the medium is or look at it.

## Consequences

- **`fogDensity`'s range and meaning changed** -- 0..8 as a depth, not 0..8 as a per-metre
  coefficient. The row's tooltip says so; the JSON key is unchanged and no file moves.
- The vortex is untouched: it has its own packer and its own per-metre semantics, which are correct
  for a medium whose size is the scene's rather than the artist's.
- §46 C's primitives can now be judged by eye, which is what ADR-563 deferred them for.

## Revisit when

- A fog bank wants a different crossing axis -- a tall column looked *down* into would want the
  thickness rather than the long axis. The choice is stated above rather than assumed.
