# Renderer professionalization — preparation phase

**Status: preparation complete. No implementation has been done, and none should begin before the
Phase A gate in [04](04-target-architecture.md) is satisfied.**

| Document | Deliverables |
|---|---|
| [01 — Audit and baseline](01-audit-and-baseline.md) | D1 repository audit, D3 performance baseline and protocol, D4 scalability gap analysis |
| [02 — Research: virtualized geometry, occlusion, shadows](02-research-virtualized-geometry-and-occlusion.md) | D2a |
| [03 — Research: GPU-driven, WebGPU envelope, frame graphs, Apple TBDR](03-research-platform-and-submission.md) | D2b |
| [04 — Target architecture](04-target-architecture.md) | D5 architecture, D6 roadmap, D7 risks, D8 backlog, D9 go/no-go |

> **Note (added 2026-09-13, wave 2 chore sweep):** the figures below (18.6 ms GPU, 15.73 ms scene
> pass, 430k tris) are the pre-upgrade numbers this preparation phase measured and diagnosed against.
> Wave 1's LOD0 and shadow work moved them; current figures are GPU 13.37 ms, scene pass 10.88 ms,
> 264,305 tris, 34 shadow draws — see [01 §3.2.2](01-audit-and-baseline.md). The *findings*
> (fragment-bound, quad overdraw, no multi-draw indirect, etc.) still hold; only the numbers moved.

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

## The gate: passed

The 9 ms resolution-independent cost had two candidate causes implying different architectures.
**Measured in-engine rather than through Xcode** (`[.perf][fragment]`): at constant full-screen
coverage, the scene pass costs **4.9× more with sub-pixel triangles than with 500-pixel ones**, with
the knee exactly at the 2×2-quad threshold and the depth-pass control flat throughout.

**Quad overdraw is confirmed.** Representation — LOD, HLOD, impostors — is the justified main line,
with per-pixel cost work alongside it. Glowmere averages 2.4 px/triangle.

One caveat carried into the design: two screen-filling triangles cost *more* than 2,048 of them, so
the target is a band of a few hundred pixels per triangle, not the fewest possible triangles.
