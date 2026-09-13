# ADR-130: What remains of the object ceiling is a byte budget, kept below the naming limit

**Status:** Accepted
**Date:** 2026-09-13

## Problem

ADR-128 made the object buffer grow with the frame. "Grows with the frame" still has to stop
somewhere — a scene that asks for ten million entities cannot be handed a five-gigabyte allocation —
so a ceiling exists. The question is what kind of number it should be, and how the next person finds
out it is there.

There is a second, sharper problem hiding behind it. Lifting one cap is only worth doing if the
failure it caused does not simply reappear a few thousand entities later somewhere quieter. The
identifier target (ADR-035/ADR-030) packs an object id into the low 16 bits of one `R32Uint` and a
material id into the high 16. `packPickId` gives each of the three pick spaces a two-bit tag, which
leaves **14 bits — 16,383 distinct entity indices** — and it *saturates* rather than wrapping, so
past that point two entities answer to the same name and a click resolves the wrong one. That is a
much worse failure than a dropped draw: a dropped draw is visible, a wrong pick is not.

## Decision

**The ceiling is a memory budget, not a slot count.** `kObjectBufferBudgetMiB = 64`, and
`kMaxObjectCapacity = 64 MiB / kObjectStride` = 131,072 slots. Exceeding it logs both numbers at the
moment of clamping — the whole point of ADR-128 was that the old limit's symptom was undiagnosable,
so the new one says what it is and what was asked of it.

**And the budget is deliberately set above the naming limit**, by a factor of eight: 131,072 slots
against 16,383 nameable entity indices. The object buffer must never again be the binding
constraint. Whatever runs out first should be the thing whose exhaustion the renderer can describe.

**The guard states this as a relation, not a number.** `test_renderer_layout_guards.cpp` scrapes
`kObjectBufferBudgetMiB`, `kObjectStride` and `kPickIndexBits` out of the headers and asserts that
the slots they imply exceed the names they imply. Nothing is retyped, so shrinking the budget or
widening the pick-space tag fails the guard instead of silently re-creating the problem.

The same test case pins two smaller things:

- **No compile-time visible-entity constant comes back.** `kMaxObjects` must not be declared in
  `scene_renderer.hpp`. This is worth an assertion precisely because its failure mode is silent:
  reintroducing it drops entities and nothing else in the suite notices. Verified by negative
  control — adding `kMaxObjects = 256` back fails both checks, and the guard reads the header at run
  time, so it catches it without a rebuild.
- **A small scene still allocates exactly 128 KB.** `kInitialObjects * kObjectStride == 128 * 1024`.
  Growth is the new behaviour; paying for it when nothing grows is not.

The existing stride guards are untouched and still mean what they meant. If anything they mean more:
a stride that is not a 256-byte multiple was a validation error at a fixed set of offsets before,
and is now a validation error at offsets the frame chooses.

## Consequences

- The renderer's remaining entity ceiling is 16,383 — a property of the identifier target's
  encoding, not of an allocation — and that is now written down in one place with a test holding it.
- `docs/renderer-limits.md` is updated: `kMaxObjects` leaves the table and is replaced by the
  budget, and the sibling caps that were *not* lifted stay listed with the mechanism they share.
- The GPU-side proof lives with the fixture rather than here: `tests/support/dense_scene.hpp` and
  `tests/rendering/test_object_capacity_gpu.cpp` check the identifier target texel by texel, which
  is the only reading of "renders correctly" that a pixel count cannot fake. Two traps are named in
  that file and tested rather than trusted: a scene with a floor covers most of the frame
  legitimately and hides a wrong claim, and `packPickId(Entity, 0)` is literally `0`, so the sky and
  entity zero are the same bits in the low half of a texel. The material id being one-based is the
  only thing separating them, and both halves are asserted.
