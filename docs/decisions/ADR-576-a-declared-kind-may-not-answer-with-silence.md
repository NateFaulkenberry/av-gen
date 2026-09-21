# ADR-576: a declared kind may not answer with silence

Status: accepted. Date: 2026-09-21. A defect fix split out of ADR-575's §18 investigation.

## Context

ADR-575 reported `FieldKind::SdfDistance` as one of §18's five routes and found it **declared,
documented, selectable, and evaluating to 0 on the CPU and 0 on the GPU**. It reported it as a
blocked item: *"implement or remove -- the owner's call."*

That bundled two questions. **Whether SDF-driven fog gets built is a feature decision. Whether a
declared kind may silently produce nothing is not** -- that is a defect, and it is the same harm as
every other one this programme has found: the system answers a question with silence instead of an
error.

The specific harm is sharp. A scene author picks `sdfDistance` for a volume density field, the
field returns 0, the density is `volumeDensity × 0`, and the volumetric march renders **no fog at
all**. There is nothing in the log, nothing in the panel and nothing in the file to read. The next
hour goes into debugging a density that was never the problem.

## Decision

**Refuse at load, do not remove.** `FieldSpec::validate` rejects a spec naming `sdfDistance`, with
a message that names the kind, names the field, says what would have happened, and points at the
alternatives:

> `field 'blob': kind 'sdfDistance' is declared but not implemented -- it evaluates to 0
> everywhere, on the CPU and on the GPU, so a density field using it produces nothing at all.
> ADR-027 reserved the kind and never bound it. Use 'distance' for a point, 'sphere' or 'box' for a
> volume, or ask for the SDF binding to be built`

`FieldSpec::fromJson` calls `validate`, so every field in every scene file goes through it.

### Why refusing rather than removing

Removal was the other option and ADR-441/442 would have supported it -- this project cuts things
rather than leaving shims. Three reasons it is the wrong instrument here:

1. **The seam is real, not vestigial.** `Scene::sdfs` exists and `SdfRenderer` draws it. ADR-027
   reserved this kind against a subject the engine genuinely has. Removing the enum value would
   delete a planned join, not dead weight -- and the next person to want it would have to
   rediscover that the scene already carries SDFs.
2. **Nothing ships with it, so removal buys nothing.** `git grep -l sdfDistance -- examples` is
   **0**, and 0 shipped scenes name it, so there is no migration either way. Removal's usual payoff
   -- deleting a thing people are using wrongly -- is not on offer.
3. **Removing it is the larger change.** The kind is in a name round trip, an exhaustive kind loop
   in `test_fields.cpp`, and the GPU's `fieldScalar` fallback. Refusing is one branch.

**The unbound arms stay and are still 0**, on both sides, because a `FieldSpec` built in code and
never validated has to land somewhere -- defined rather than undefined. What changed is that no
scene can reach them.

## Consequences

- **A test that CERTIFIED the silent zero is now a test of the refusal.** `test_fields.cpp` asserted
  `sampleScalar(sdf, p, 0.0) == 0.0f` -- the defect, written down as correct behaviour, by
  somebody who was documenting what the code did rather than what it should do. It still asserts
  the arm's value (that path is real) and now also asserts that `validate` refuses it.
- **The refusal's message is asserted, not just its existence.** The case checks the error names
  both the kind and the field, because *a refusal nobody can act on is a quieter silence*. A test
  that only checked `has_value() == false` would pass against `fail("bad field")`.
- **An existing assertion was passing for the wrong reason and is fixed.** `m.kind = SdfDistance;
  m.reference.clear(); CHECK_FALSE(validate())` passed because the *reference* was empty. It now
  also checks the refusal **with** a reference -- otherwise removing ADR-576's branch and leaving
  the old reference check would keep that line green.
- **Break demonstration**: restoring `&& reference.empty()` fails three assertions across two
  cases, including the round trip through `fromJson`.
- **The general rule, which is not about fields.** ADR-421: a control that does nothing teaches an
  artist the system is broken. ADR-574: a capability with no control teaches them it cannot be
  done. This is the third face: **an option that is offered and evaluates to nothing teaches them
  their scene is wrong.** All three are a declaration and a consumer that never meet; the harm
  differs only in who gets blamed -- the system, the feature, or the author.

## Revisit when

- **The SDF binding is built.** Delete the branch and the kind works. That remains a feature
  decision and this ADR deliberately does not take it.
- **Another kind is added without an implementation on both sides.** The check that would have
  caught this one is cheap: for each `FieldKind`, does `sampleScalar` have a real arm and does
  `fields.wgsl` have a matching one? Two greps and a diff of the two lists.
