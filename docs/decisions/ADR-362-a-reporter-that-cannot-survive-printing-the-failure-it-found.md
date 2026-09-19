# ADR-362: A reporter that cannot survive printing the failure it found

Status: accepted
Date: 2026-09-19
Branch: `agent/mbackend`
Relates to: ADR-358 (which recorded the symptom), ADR-170 (one GPU, one run), ADR-009 (Catch2)

*Numbered 362 alongside ADR-361 on the same branch. If another branch takes it, this file moves.*

## The symptom, already on record

ADR-358: `avgen_render_tests` dies with a **bus error** in a full-suite run. With `-s`, the last
thing printed is

```
tests/rendering/test_wind_gpu.cpp:272 FAILED:
```

in *"Wind off is byte-identical to wind never having existed"*, then SIGABRT. The same test passes
3 of 3 in isolation under the GPU lock, and checking `src/` out to the merge base reproduces it, so
it is on `main` and it belongs to nobody's branch.

There are **two** defects tangled together, and ADR-358 said so. This record fixes the second one
only.

## The second one

```cpp
REQUIRE(a.rgba == b.rgba);   // std::vector<std::uint8_t>, four megabytes each
```

When that fails, Catch2 stringifies **both** operands into the report. Eight megabytes of decimal
byte values, formatted, into a string, inside a failing assertion handler. The process dies there.

Note what that costs beyond the crash: the `FAILED:` line is printed and the `with expansion:` line
is not, which is the exact signature this project already uses to recognise **a killed process
rather than a real failure** (`docs/` records four ways a suite result lies here, and that is one of
them). So the reporter does not merely crash — it disguises a real failure as a killed run.

And the difference it found is never shown. `a == b` says nothing about *how* the two frames
differ, which is precisely the information needed to tell GPU contention (ADR-170's documented
signature) from state leaking between tests.

## Decision

A frame comparison in this repository reports a **count and a first offset**, never an equality
over the byte vector.

`tests/support/image_diff.hpp` — `avgen::testing::byteDiff(a, b)` returning
`{sameSize, sizeA, sizeB, differing, firstOffset, maxDelta, identical(), describe()}` — and the
call site reads:

```cpp
const auto d = testing::byteDiff(a.rgba, b.rgba);
INFO("calm at 0 s vs 4 s: " << d.describe());
REQUIRE(d.identical());
```

which fails with, for example, *"1742 of 8294400 bytes differ, first at 3118092, max delta 7"* and
leaves the binary alive to run the rest of the suite.

This is **not a new convention.** `tests/rendering/test_gpu.cpp:1346-1349` and
`test_resource_lifetime_gpu.cpp:136` already did exactly this, by hand. The helper exists so the
remaining call sites can stop being the exception, and so a seventh copy is not written.

Converted: `test_wind_gpu.cpp` (four sites, including the one that crashes),
`test_post_gpu.cpp:398`, `test_motion_gpu.cpp:191`, `test_quality_lab.cpp:552`. A census found no
others.

`maxDelta` is in the struct because it is the cheapest discriminator available between the two
hypotheses ADR-358 separated: a handful of bytes off by one is quantisation or a race in a blend;
whole regions off by a hundred is a different frame.

## What this does not fix, said plainly

**The underlying non-determinism is untouched.** "Wind off is byte-identical to wind never having
existed" still passes alone and may still fail in a suite. This change does not make it pass; it
makes the failure *printable*, which is the precondition for finding out why — and it stops one
test's failure from destroying every result after it in the same run.

Anyone picking that up should start from the `describe()` line the converted assertion now prints,
under `tools/gpu-lock.sh`, with the suite run whole. If the count is small and `maxDelta` is 1-2,
ADR-170's contention signature is the first hypothesis. If whole regions differ, look for state
leaking between GPU tests instead.
