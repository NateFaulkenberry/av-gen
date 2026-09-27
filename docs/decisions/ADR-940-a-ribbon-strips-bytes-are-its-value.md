# ADR-940: A ribbon strip's bytes are its value

**Status:** Accepted
**Date:** 2026-09-27
**Follows:** ADR-703 (RIBBON: the frame block the effects write and the renderer reads), and Effect
Library Wave 3's play = scrub proof for triggered bolts
**Found by:** main's CI, in 3 of 4 runs: the nightly 36318924797 (seed 1790512056, shard 2), main
876a11e2's push 36341601980 (seed 1790536064, shard 0) and 983221a9's push 36354090309 (seed
1790548209, shard 1) failed; ec515c8b's push 36335160280 passed. This is the GV3 revision's cihealth
brief, item 1.
**Implemented by:** `RibbonStrip::reserved` and two `static_assert`s (`src/world/effects/ribbon_frame.hpp`)
**Tests:** `tests/unit/test_bolt_path.cpp`:
- "a ribbon strip's bytes are its value, whatever the memory it was built in last held" (new; the
  control arm);
- "a triggered Lightning and Discharge on a moving owner are the same played and scrubbed"
  (unchanged: the proof that failed).

## Context

The proof plays one `Engine` to frames 331, 337 and 407, and seeks a second one there. It then
compares the two frames' ribbons byte for byte. On CI it failed in 3 runs of 4, always the same way:

- `CHECK(sameVector(a.ribbons.strips, b.ribbons.strips))` was false at all three frames;
- the vertices, every effect's status and the flash lights matched;
- alone it passed, even with a failing run's seed.

The brief's reading was that another case leaves global state behind. The method was to replay the
failing shard's order and bisect it.

**Reproducing CI's order.** Catch2 v3 orders cases by a hash of each case, salted by `--rng-seed`,
and cuts `--shard-count` contiguous slices from that order. So a subset of cases, run with the same
seed, keeps CI's relative order. Checks:

- `--list-tests` with run 36341601980's spec, seed and shard reproduces its shard 0 exactly
  (1262 cases);
- the bolt case is at index 127, after 127 others.

**The local binary does not fail.** Main 876a11e2, built with Xcode 27, passed all 128 cases in CI's
order.

**CI's own binary does.** Run 36341601980's `bin-release` artifact was run here with:

- its baked checkout path (`/Users/runner/work/av-gen/av-gen`) rewritten to a same-length relative
  path;
- a `git archive` of 876a11e2 as the checkout, so no gitignored assets, as on the runner;
- the runner's `libsimdjson.33.dylib`, taken from Homebrew's bottle without installing it.

It failed the bolt case exactly as CI did: three times at line 565. Then:

- **Minimising** the 127 preceding cases (ddmin) leaves one case: "deleting a camera the scene file
  declares stays deleted after a save" (`test_director_persistence.cpp:262`).
  - That case then the bolt case fails **5 of 5**. The bolt case alone passes **3 of 3**.
  - The same pair on the local binary passes.
- **What differs.** lldb on CI's binary logged each strip's bytes as `RibbonSink::write` stored them.
  - In each of the three frames, all 19 strips' `firstVertex`, `vertexCount` and `blend` are equal.
  - One strip (index 4) differs, **in bytes 9-11 only**: the played engine's hold `02 00 00` (or
    `1b ce 43`), the scrubbed engine's `00 00 3f`.

**Why.** `RibbonStrip` was `{uint32 firstVertex, uint32 vertexCount, uint8 blend}`: 12 bytes, three of
them padding that nothing writes.

- **CI's compiler** (Apple clang 21, Xcode 26.6) stores the counts with `stp` and `blend` with `strb`
  straight into the vector's element.
  - The padding keeps whatever the heap block held before. Each engine reserves its strip storage
    once (256 strips).
  - The polluting case's freed blocks carry old data into one engine's block but not the other's.
    `00 00 3f` is the top of a float 0.5.
- **The local compiler** (clang-2100.3) builds the strip in a stack slot and copies 4 bytes from
  `sp+0xa0`, three of which it never wrote.
  - That is stack residue: floats from the same frame's work, and in every local run they matched
    between the engines.

So no global state reached the simulation: the two engines computed the same frame. What leaked was
memory contents, through three bytes that are nobody's value. Which earlier work left what in the
heap is exactly what depends on test order and seed, and on the allocator's per-CPU magazines.

**Why a different case each time, and why only one failure reproduces here.** The three failures came
in three shards and three orders, and there is no single polluter to find, because the defect is not
in one earlier case: it is three bytes that take whatever the heap held. Any earlier case can be the
one that left the bytes that one engine's strips then got. In each failure, whichever it was, the
evidence says the bytes that differ are those three:

- **By elimination, in every failure.** All three report line 565 (`strips`) three times, and none
  report line 566 (`vertices`). Equal vertex arrays mean equal strips in every field:
  - `firstVertex` and `vertexCount` are how the same vertices were cut into strips. Each strip is
    written contiguously, and core, glow and spark strips differ in their vertices' profile lane, so
    a different cut would change vertex bytes;
  - every strip in this proof is `Additive`;
  - a missing or extra strip would change the vertex count.

  That leaves bytes 9-11.
- **The same code in every failing binary.** 983221a9's CI binary stores `blend` with the same
  1-byte `strb` and never writes those bytes.
- **Reproduced on one, not on the other.** 876a11e2's failure is a two-case pair here (5 of 5), and
  the patched binary cures it. 983221a9's (bolt case at index 924 of shard 1) passed here: once
  replaying CI's exact shard command with its binary, once with its preceding cases, and once with
  `MallocScribble=1`.
  - Over a 924-case history, which freed block each engine's storage lands in depends on the
    machine. macOS's allocator keeps a magazine per CPU, and the runner has 3 vCPUs to this Mac's 12.
  - The fix does not depend on finding that case: once the bytes are a value, no earlier case can
    change them.

## Decision

- **The three bytes are a member:** `std::array<std::uint8_t, 3> reserved{}`. They are named,
  zero-initialised and part of the value, so every construction writes them.
- **The layout is held at compile time:**
  - `static_assert(sizeof(RibbonStrip) == 12)`;
  - `static_assert(std::has_unique_object_representations_v<RibbonStrip>)`.

  A field that brings padding back fails to build; `-Wpadded` flags the old layout with 3 bytes.
- **The fix is in the struct, not the test.** A byte-for-byte comparison is the right contract for a
  frame block the renderer uploads and the proofs compare, and the struct now meets it.
  - Comparing fields in the test was rejected: it would hide the next padded frame block.
  - Zeroing in `write` was rejected: every future writer would have to remember.
  - The polluting case does nothing wrong. Freeing memory is not a leak.

## Consequences

- **The proof no longer depends on what ran before it.** The control arm builds the same strip twice:
  over storage and a stack painted 0x00, and over both painted 0xF0.
  - Before this change the fields agreed and the bytes did not: `000000001400000000000000` against
    `000000001400000000f0f0f0`.
  - After, they are equal.
  - The local build now zeroes the slot (`stp w8, wzr, [sp, #0x9c]`) before it writes `blend`.
- **`test_ufo_stack_demo.cpp` had the same latent failure.** It compares `ribbons.strips` byte for
  byte at lines 214 and 313, and this fixes it too.
  - A `-Wpadded` census of every other struct the tests `memcmp` found no padding: `DistortionProxy`,
    `ShellInstance`, `InstanceRecord`, `CometGpu`, `AuroraGpu`, `MediumSlot`, `WaveGpu`, `Vertex`,
    `Transform`, `SplineSampleGpu` and the entity-fx records.
- **No picture changes, and this was not seek ≠ play in the app.**
  - `RibbonRenderer` reads `firstVertex`, `vertexCount` and `blend`, and never the other bytes.
  - The vertices (four `vec4`s) are uploaded and have no padding. `sizeof` is unchanged.
  - Nothing hashes, digests or compares `scene.ribbons`. The garbage reached only the test's `memcmp`.
- **The fix's effect, on CI's own binary.** CI's compiler is not installed here, so this change
  cannot be rebuilt with it. Instead its effect was applied to CI's binary by hand:
  - its two `strb w24, [..., #8]` became `str w24`. `w24` holds `blend` zero-extended, so bytes
    9-11 are written 0, which is what `reserved{}` does;
  - the polluter pair then passes **5 of 5**, where the unpatched binary fails 2 of 2 in the same
    session;
  - the whole 128-case CI prefix passes.

  So the three bytes are the whole cause on CI's compiler too. A binary rebuilt with this change
  comes from the next CI run.
- **Method, for the next order-dependent failure** (`docs/testing.md` entry 42): when a case fails in
  CI's company and passes in yours, replay CI's own binary in CI's order before bisecting on a local
  build. A different compiler can be a different defect.
