# ADR-606: Re-read the specification, not your memory of what you built against it

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-385 (a stated reason is not evidence), ADR-182 (a probe that cannot fail proves
nothing), ADR-600 (a gate with nothing configured refuses nothing), Phase B §41, §49, §51
**Implemented by:** `src/scene/pose_layers.{hpp,cpp}` (`seedPhase`, `seedFromName`),
`src/scene/composition.cpp` (`driveLayers`)
**Tests:** `tests/unit/test_deterministic_seeds.cpp`, `tests/unit/test_phase_b_matrix.cpp`

---

## Context

Three times in one phase, a stage this branch's own audit had marked **done** turned out to be
half-done, and each time the thing that found it was re-reading the stage's own text rather than
consulting the memory of what had been built against it.

- **§41** (motion quality metrics) was marked met by a runtime sample. Its text asks for *offline*
  metrics over a clip. Different artefact, different consumer, different code.
- **§20/§22** were marked met by a switch count. The text also wants dwell, and a switch count alone
  recommends whichever dial reduces switching (ADR-559).
- **§49** (deterministic randomness) was marked met on the grounds that the secondary layer is a
  pure function of the timeline second with no random number generator anywhere near it. That is
  true, and it is only §49's *second* sentence. The first names two fields by name — `characterSeed`
  and `layerSeed` — and neither existed. Variation was **hand-authored**: one `phase` number per
  layer per character, typed into the scene file. Five aliens can be hand-spread. A hundred cannot,
  and **hand-authored spread is not a seed — it is the absence of one, done by hand.**

The failure is not carelessness. In each case the audit was performed against a true and relevant
fact about the code, and the fact answered a *neighbouring* question. "No global randomness" and
"seeded from an identity" both sound like determinism and are different properties: the first makes
a render reproducible, the second makes it reproducible **and** scalable, which is why §49 asks for
both and lists baking and debugging among the reasons.

## Decision

**A stage is audited against its own text, clause by clause, and never against a recollection of
what was built. A stage whose audit cites a property rather than a clause is not audited.**

Concretely, for §49, the clause that had no implementation now does: `seedPhase(characterSeed,
layerSeed)` is a bit-mixer with no state, no sequence and no order dependence, hashed into a phase
offset rather than into a generator. `seedPhase(0, 0)` is exactly `0.0f`, so an unseeded layer is
byte-for-byte what it was — and because a knob nobody sets refuses nothing (ADR-600), `driveLayers`
seeds **every** layer it drives, from the node's name and the layer's name rather than from an
index, since an index changes when someone reorders a scene file and a render that changes because
two characters swapped places in a JSON array is exactly what §49 exists to prevent.

The authored `secondaryPhase` survives as an offset on top rather than being replaced when it
happens to be zero: a rule of the form "the seed applies unless you authored something" makes two
mechanisms fight over one field, and which one wins depends on a number an artist typed.

Measured: a hundred seeded characters spread with no pair closer than 0.00002 and no gap in the
cycle wider than 0.0583, against a `worstGap < 0.25` bound that a hash piling everything into one
corner would fail.

## Consequences

- **§51's audit is the model.** It goes row by row through §51's own list of twenty-four required
  cases, cites the file and test name covering each, and marks six as gaps. Five were genuine
  omissions. The sixth — contact release — was a finding: before §46 a released contact dropped the
  foot 0.200 m in one frame, the same defect §46 found on the reach layer, on a different layer,
  and one the six-cause list missed because the slice's script never released a contact. §46's
  blend fixes it structurally, which is the argument for having put the blend on `PoseLayer` rather
  than in the reach code.
- **An audit row that fails is worth more than one that passes.** §51's "lowered ground" row failed
  because I asserted symmetry: raising the ground 0.20 m moves the foot 0.1999 m and lowering it
  moves the foot 0.0119 m. The engine is right — the alien binds with its leg nearly straight, so
  there is no slack to extend downward and the solve clamps. A foot layer **alone cannot follow
  ground that falls**; that is what body compensation is for. The expectation was wrong, not the
  code, and the row only means something now that it says which of the two is being asked.
- This ADR is the standing instruction for Phase C, where sixteen sections are already marked done
  from earlier work: **when a stage looks already-met, check it against that phase's text rather
  than the memory of what was built.**
