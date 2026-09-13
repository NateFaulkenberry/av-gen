# Renderer professionalization — preparation phase

**Status: preparation complete. No implementation has been done, and none should begin before the
Phase A gate in [04](04-target-architecture.md) is satisfied.**

| Document | Deliverables |
|---|---|
| [01 — Audit and baseline](01-audit-and-baseline.md) | D1 repository audit, D3 performance baseline and protocol, D4 scalability gap analysis |
| [02 — Research: virtualized geometry, occlusion, shadows](02-research-virtualized-geometry-and-occlusion.md) | D2a |
| [03 — Research: GPU-driven, WebGPU envelope, frame graphs, Apple TBDR](03-research-platform-and-submission.md) | D2b |
| [04 — Target architecture](04-target-architecture.md) | D5 architecture, D6 roadmap, D7 risks, D8 backlog, D9 go/no-go |

## The five findings that matter

1. **The scene pass is fragment-bound; geometry work is 1.7% of it.** The same 430 k triangles cost
   0.26 ms through a depth-only shader and 15.73 ms through the scene shader.
2. **56% of the scene pass does not scale with resolution.** 8× the pixels buys 2.1× the time.
3. **Nanite's rasterizer and virtual shadow maps are impossible on WebGPU** — both require 64-bit
   *texture* atomics, and WGSL has neither 64-bit atomics nor texture atomics of any width.
4. **Dawn-on-Metal has no multi-draw indirect**, so GPU-driven submission cannot deliver its
   headline benefit here.
5. **The five scene targets cost exactly 32 bytes/sample — exactly WebGPU's default limit**, and 25%
   of the M2's tile budget. Tile memory is not the constraint; portability headroom is zero.

## Two corrections to the brief, and one to myself

- **Constellation is 41% faster than the brief states** (3.60 ms GPU, not 6.09). Not noise: 1% spread
  over five runs.
- **A third Glowmere number exists** (23.79 ms in the QA doc vs 18.6 ms today). Within-session spread
  is 1.0%, so a 28% gap is a real discrepancy, recorded as unresolved.
- I hypothesised that the five-attachment layout was pressuring tile memory. **It is not** — 32 of
  128 bytes per pixel. Corrected in place rather than quietly dropped.

## The one decision that gates everything

The 9 ms resolution-independent cost has two candidate causes — quad overdraw on sub-pixel triangles,
or an expensive per-invocation shader — and **they imply different architectures.** Apple's overdraw
counter distinguishes them in a single GPU capture. Phase A takes that measurement and nothing else.
