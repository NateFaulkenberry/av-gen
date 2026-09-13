# ADR-134: A mode is one policy object, not four independently-set structs

**Status:** Accepted
**Date:** 2026-09-13

## Problem

Deliverable 5 §5.6 asks for `rendering::QualityPolicy` — *"the tier tables and the
editor/realtime/offline difference"* — with the justification *"one place to answer 'why did this
look different in a render?'"*.

Before this there were four places, and no relation between them that a compiler or a test could
check:

| what | where | who set it |
| --- | --- | --- |
| sample counts, resolutions, history lengths | `QualitySettings::forTier` | the renderer, from `--tier` |
| which geometry a drawable is drawn as | `RepresentationPolicy::forTier` | whoever calls the selector |
| how expensively it is shaded | did not exist | — |
| the render scale (§34) | did not exist | — |

Each was independently settable, so a caller could construct a `RepresentationPolicy` for Offline
against a renderer running Realtime settings and nothing would notice. Risk 4 in the register —
*offline silently inherits a realtime compromise* — is exactly this failure, and it is rated High
because the artefact it damages is a deliverable rather than a preview.

The second problem is subtler and is the one that motivated a single *object* rather than a single
*function*. Phase D needs the material tier in two forms: the shader reads a forced tier out of
`QualitySettings` (so an A/B arm can move it and the frame uniform can carry it), and the selector
reads bands out of a `MaterialTierPolicy`. Two statements of "is assignment on" is two things that
can drift, and the drift is invisible — it does not crash, it renders a slightly different picture.

## Decision

**`QualityPolicy::forTier(tier)` is the whole definition of a mode**, and it returns one object
holding all three sub-policies. Consumers take the object, not the enum, so a subsystem cannot ask
"which tier is this?" and answer it its own way.

`forTier` also **reconciles** the two statements of material-tier assignment, in the direction of
the policy:

```cpp
p.settings.materialTiers = p.materialTier.enabled && !p.materialTier.forceTopTier;
if (!p.settings.materialTiers) { p.settings.forcedMaterialTier = MaterialTier::Full; }
```

and the unit test asserts that relation for every one of the four tiers, so a future edit to either
table fails a test rather than a shot.

**Offline is stated as an invariant, not as a preset.** `assertOfflineIsUncompromised()` is every
promise §5.9 makes, in one predicate next to the thing that has to keep it: top representation, top
material tier, no hysteresis, no spread, render scale 1, shadow mask and AO at full resolution. The
test states it both ways — positively (the selectors return the top answer) and negatively (widening
a band to 10⁹ px still returns the top answer, because the *force* flag and not the band is what
decides). A test that only checked the bands would pass with the force removed, and the force is the
whole mechanism.

**The four modes keep the existing enum's names.** `QualityTier::Preview` is the editor tier the
plan calls "editor"; renaming it would touch every `--tier preview` in the repository and buy a
word. The header says the two are the same thing.

## Consequences

* `QualityPolicy` is a header-only aggregate over three existing structs. It adds no state and owns
  nothing, which is deliberate: it is a *derivation*, like everything else downstream of the
  parameters, and giving it a lifetime would make it the tenth boundary-ownership defect.
* Nothing is forced to route through it yet. `SceneRenderer` still holds a `QualitySettings`, and
  the two callers of `RepresentationPolicy::forTier` still build their own. Converting them is
  mechanical and is not done here, because it would touch four files this wave's other agents own.
  The value that exists today is the reconciliation and the offline invariant test; the value that
  remains is one caller at a time.
* `assertOfflineIsUncompromised()` returns true for the three non-offline tiers rather than being
  undefined for them, so a caller can assert it unconditionally.
