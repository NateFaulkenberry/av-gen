# ADR-941: The comet packing tests read the effect their caller holds

**Status:** Accepted
**Date:** 2026-09-27
**Follows:** ADR-230 (comets) and ADR-500 (a kind resolves by its bucket). The same mistake was fixed
in `test_wave_effects.cpp` on 2026-09-25 (20de27ac).
**Found by:** the nightly Sanitizers runs 36241405408 and 36321265640 (UBSan, in the "main" part).
This is the GV3 revision's cihealth brief, item 2.
**Implemented by:** `resolveComet` in `tests/unit/test_atmospherics.cpp`
**Tests:** `tests/unit/test_atmospherics.cpp`:
- "a resolved comet's record points at the effect its caller holds" (new; the control arm);
- the four `packComet(*resolveComet(...))` calls it guards, in "a comet's trajectory is a pure
  function of its parameters and the transport second" and "packing puts every authored number in
  the lane the shader reads".

## Context

Both runs reported, byte for byte:

```
atmospherics.cpp:698:39: runtime error: load of value 240, which is not a valid value for type 'bool'
    #0 avgen::world::packComet(avgen::world::ResolvedAtmospheric const&)+0x1ba8
    #1 CATCH2_INTERNAL_TEST_30()+0xfe4
```

Line 698 reads `c.sparkle.enabled`, where `c` is `r.effect->comet`. The CI watcher placed the report
beside `glowmere-valley-2-multicam.json`, whose load the log shows just before it, and that scene has
no comet. The brief asked what constructs or reuses an effect payload without initialising it.

**Nothing does.** The evidence:

- **The stack says who called.** `packComet` was called straight from a test body, not from
  `buildAtmosphericFrame`. `CATCH2_INTERNAL_TEST_30` is per translation unit, so the name alone says
  little.
  - The run's `bin-asan` artifact has a symbol table with the object file of every function.
  - `0x100fef668 + 0xfe4` is `test_atmospherics.cpp.o`'s `CATCH2_INTERNAL_TEST_30`, the eighth of the
    file's sixteen cases: "packing puts every authored number in the lane the shader reads".
- **The multicam link was coincidence.** In run 36321265640 the report sits in shard 1, after a helix
  scene and a craft, and nowhere near a multicam case.
- **The helper returned a pointer into its own dead frame.** `resolveComet` copied the effect into a
  local `std::array<EffectInstance, 1>`, resolved that, and returned `comets[0]`.
  - The record's `effect` pointed at the copy. After the return that is a dead stack frame.
  - `packComet` reads the comet's appearance, sparkle and rainbow through it.
  - In Release the dead frame usually still held the copy, so the checks passed on stale but right
    values. Under the sanitizer build it held 240.
- **Why ASan was silent.** The read is a stack-use-after-return: the helper had returned. ASan
  reports that only with `detect_stack_use_after_return=1`, which `sanitizers.yml` does not set.
  UBSan saw it only because the dead byte happened not to be 0 or 1.

**The product is not exposed.** `buildAtmosphericFrame` resolves and packs within one call, over the
engine's live `effects_`, and nothing keeps a `ResolvedAtmospheric` past its span. A census of the
tests found every other resolve safe:

- the wave tests (since 20de27ac) and `test_effect_registry.cpp` use a span over a named local;
- the aurora case and the GPU fixtures pack before their local array dies, and the vortex dispatch
  case returns only counts.

## Decision

- **`resolveComet` resolves the caller's own instance** (`std::span(&e, 1)`), so the record points at
  the `e` the caller holds for as long as it uses the record. This is the wave tests' fix.
- **No product change.** `ResolvedAtmospheric` keeps its pointer. Copying the comet (about 200 bytes)
  into every record, to protect tests from themselves, was considered and not done.
- **Control arm.** `REQUIRE(r->effect == &e)`: the record refers to the caller's effect. Then an edit
  to `e` after the resolve (a head size of 77) is what packing reads.

## Consequences

- The UBSan report goes. The packing tests now assert what the comet they built says, not what a
  dead frame held.
- The control arm failed before the fix: `0x16ee04dd0 == 0x16ee05a18`, a dead local against the
  caller's effect. It passes after.
- **No product, scene or picture changes.**
- **The pattern has now bitten twice**: a record that points into its input, handed a temporary. A
  third instance would be caught by ASan with `detect_stack_use_after_return=1`. That is not
  enabled, because it costs time and the sanitizer run is at its ceiling (`docs/development/ci.md`,
  follow-up 6).
