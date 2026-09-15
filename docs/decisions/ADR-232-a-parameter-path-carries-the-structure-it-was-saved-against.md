# ADR-232: A parameter path carries the structure it was saved against

**Status:** accepted (2026-09-15)
**Context:** `elder-2-under` rendered black in Glowmere Valley 2
**Extends** ADR-030 (procedural materials), ADR-011 (parameters), ADR-010 (project serialisation)

## Context

> "take a look at elder-2-under — it's reading as black in glowmere - it needs a glowing under part
> like the rest of em."

It was black, and every emissive lever was inert. Setting the node's `material/emissive` to 30 did
nothing. Setting `emissiveBoost` to 20 did nothing. Rewriting the material file's emission constant
to cyan and its `emissionIntensity` to 30 produced a **byte-identical frame**. That is not an
art-direction value; that is a knob wired to nothing.

It was worse than a dead knob. The knob was wired to the *wrong thing*.

`ADR-030` gave each material program's tunable values a parameter each, addressed positionally:

    material/<name>/op/<i>/value|constant|constant2|constant3|constant4|enabled

A material program is a **file** — `examples/materials/*.material.json` — and a project is a **table
of values** for it. The two are edited apart, by different people at different times, and neither
records anything about the other. `glowmere2-tissue.material.json` later gained one op: a `remap` at
position 5, lifting the emission mask's floor from 0 to 0.45 so the whole underside would carry
light. That insertion shifted every op after it by one. The project — saved against the fourteen-op
version — went on applying its old values by index:

| the file's op 5..15 | the value the project wrote onto it |
|---|---|
| 5 `remap` — the mask, range `[0, 1] -> [0.45, 1]` | `[0,0,0,0]` (from the old op 5, an `input`) |
| 8 `ramp` — the base colour's three stops | the old `remap`'s range, stops 2 and 3 zeroed |
| 11 `constant` — **the emission hue** `[1, 0.47, 0.15]` | `[0,0,0,0]` |
| 14 `ramp` — the emission's noise ramp | `[0,0,0,0]` |

Emission in that program is `instanceEmissive x constant x mask x ramp`. Three of those four
factors were forced to zero, so the product was exactly zero — which is why `emissionIntensity` was
inert, and why it *looked* like a broken uniform rather than a broken value: thirty times nothing is
still nothing.

Two details cost a previous investigation a wrong conclusion, and both are the same mistake:

- The saved `material/glowmere2TissueCool/op/10/constant` is `[0,0,0,0]` while the cool mushrooms
  glow. `op/N` is **1-based**; op 10 is the `input instanceEmissive` op, whose `constant` is unused.
  The cool hue is at `op/11`. An experiment that moved `op/10` was vacuous by construction.
- The project stored `emissionIntensity = 6.0` where the file says `6.15`. That is not a tuned
  value either — it is the same staleness, saved before the emission ladder gave the beacon rung
  its number.

Neither file was wrong on its own. The corruption lived in the *join*, and nothing in either file
could show it.

## Decision

**A parameter path names the structure it was saved against.** The op segment carries the op's
kind, not only its index:

    material/<name>/op/5/remap/constant

A project saved against a different op list now addresses a path nobody registered. The project
loader already drops an unknown path with a warning (ADR-010), so the file's own value stands —
which is the right answer when a file and a table disagree about what the program *is*. Registration
and application both go through one `opPath(index, kind)` helper, so the two cannot drift apart.

This is deliberately not a version stamp. A stamp would have to be saved before it could protect
anything, so it could not have rescued the project that was already broken; the kind is in the data
the project already writes.

It does not catch every shift — an op replaced by another of the same kind at the same index still
collides. It catches the shifts that happen, which are insertions and deletions, because those move
every op after them onto a different kind.

**The four example projects that carried these tables no longer carry them.** All 1380 of their
saved material op values were checked against the material file each names: every one matched its
file except `glowmere2TissueWarm`'s eighteen, in each of the two projects that use it, which were
the corruption. A table that only repeats its file is not information, and leaving it would have meant ninety dropped-path warnings per
program on every load. They are written again, correctly, the next time the project is saved.

## Consequences

- `elder-2-under` glows. Measured on the underside crop of a fixed probe camera, mean scene-linear
  luminance goes from **0.0023** to **0.3082**; a cool sibling (`lantern-under`) framed the same way
  measures **0.3095**. The warm hero now sits within 0.4% of the cool ones, on the emission ladder's
  `beacon` rung (6.15) that its file always asked for. **No art-direction change was needed or
  made** — the value was never the problem, and the 4x `special`/`noticeable` gap
  `world::EmissionLadder::validate` guards is untouched.
- A project written before this ADR loses its material op values and falls back to the material
  file. That is the intended migration: those values could not be trusted to name the ops they were
  written for.
- The parameter's label was already `"<kind>/<field>"`. The path now says the same thing, so what
  the UI shows and what the project stores finally agree.
