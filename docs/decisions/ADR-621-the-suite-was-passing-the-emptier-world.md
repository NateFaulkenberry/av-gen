# ADR-621: A test's answer depended on what ran before it — and it passed in the configuration where the world was emptier

**Status:** Accepted
**Date:** 2026-09-21
**Related:** ADR-620 (the authored ramp, which supplied the observable), ADR-615, `docs/testing.md`
**Implemented by:** `tests/unit/test_abduction_poc.cpp` registering the generators its scene needs
**Found by:** an order bisect over the suite, prompted by a full-run failure that passed in isolation

---

## The symptom, and the check that made it a finding rather than a guess

`test_abduction_poc` failed in the full suite at 4.0% stalls and **passed when run alone**. The
first hypothesis was RNG sensitivity — Catch2 reseeds per run — and it was **wrong**: the test
passes alone under the full suite's own seed (`1299753991`), checked specifically.

Same binary, same seed, different answer by execution order. **That is shared mutable state**, and
it matters more than any single defect it hides: *a suite whose answers depend on what ran before
cannot serve as evidence*, and this project runs on suite evidence.

An order bisect over declaration order found the target at position 180 and then found that **a
single preceding test — any of several — reproduced the failure exactly**, at the identical stall
count. Not a culprit test: a culprit *category*.

## The mechanism

`scene::generatorRegistry()` is a process-global `static` map. It is filled by exactly one thing —
**`Engine`'s constructor**, which calls `registerMushroomGenerator()` and `registerTreeGenerator()`
— and emptied by `clearGenerators()`, which two tests call, one of them from a destructor, handing
the rest of the process an empty registry by design.

`test_abduction_poc` **constructs no `Engine`**. It loads `glowmere-valley-2.scene.json`, which
carries **forty `"generator": "mushroom"` sources**, and took whatever the process happened to hold.

So:

- run after anything that built an `Engine` → registry populated → forty mushrooms built → they
  reach the obstacle field → routes bend around them;
- run alone → registry empty → **forty sources produce no geometry at all** → the obstacle field
  is missing forty obstacles.

## The part that matters

> **The suite was passing this test in the configuration where the world was emptier.**

The test's own comments call the obstacle field load-bearing — *"without the obstacle field the
'not inside a tree' assertion below would be asking a question"* — and it asserts that animals do
not walk into things. **The things were absent.** A green run meant the world had fewer obstacles
in it, not that the animals avoided them.

This is the day's recurring shape in its purest form: a second defect making the first invisible.
Here the second defect was making the *test* easier, which is worse than making a measurement
wrong, because nothing about a pass invites inspection.

## The fix

The test registers the generators its scene requires, and **asserts** they are present rather than
assuming it:

```cpp
organism::registerMushroomGenerator();
scene::registerTreeGenerator();
REQUIRE(scene::hasGenerator("mushroom"));
```

A test that depends on a process-global must either populate it or assert it. Inheriting it is how
a suite acquires an order dependency that nobody wrote down.

**Result: deterministic.** 2,628 stalls (4.0%) alone, after one preceding test, and after ninety —
identical, with identical obstacle counts.

## And what determinism revealed

**It now fails honestly.** The 4.0% is ADR-620's acceleration ramp: the longest stall is **0.43 s**,
and the stall detector counts "commanded to move and not moving" without distinguishing a body that
is **accelerating** from one that is **stuck**.

That is the same definitional gap as the farm pack's `frozenWhileMoving`, in a second detector, and
it is left open on purpose: the detectors ask the right question and the answers they now give are
true. Redefining them to accept the ramp is a decision about what those words mean, not a fix.

## Consequences

- **A registry cleared by a destructor is a global with a hostile owner.** `clearGenerators()` on
  the way *out* of a fixture is deliberate isolation for that fixture and a landmine for everything
  after it.
- **Unexamined siblings.** The audit that supplied this mechanism also found that
  `generatedMeshForBounds` caches **refusals** for the life of the process, keyed without builder
  identity and cleared by nothing — so a source asked for while the registry was empty is
  permanently empty afterwards, and two builders registered under one name serve each other's
  geometry. Not chased here.
- **The order bisect is cheap and should be the first move next time**: Catch2 `--order decl` plus a
  spec file narrowed 2,959 tests to a category in six runs.
