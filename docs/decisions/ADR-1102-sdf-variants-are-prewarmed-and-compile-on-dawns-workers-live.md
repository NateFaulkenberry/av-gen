# ADR-1102: SDF variants are pre-warmed, and compile on Dawn's workers live

**Status:** Accepted (live optimizer, Stage 4.1). **Date:** 2026-10-03

## Context

ADR-1003's compiled SDF variants were built synchronously on the main thread at first use: 3.9 s mid-run in Liminal
with a cold Metal cache (0.8 s measured here with a warm one).

## Decision

- **Pre-warm:** every compile-flagged raymarch object in the scene asks for its variant on every frame (a map lookup
  once built), visible or not, so a variant exists before the frame that first shows it.
- **Live, asynchronously:** `SdfRenderer::setAsyncCompile(true)` (the live editor and the live profile) creates the
  three pipelines with `CreateRenderPipelineAsync` on Dawn's worker threads; the object is drawn by the interpreter
  until they exist. A tree the interpreter cannot draw (ADR-1005) still compiles synchronously, because drawing nothing
  would be worse than a hitch. Offline renders and tests keep the synchronous path: no frame there may draw a stand-in.
- Reachability is not knowable for trees that change structure later; those compile on the workers when they appear.
- The live profile waits for pending variants before warm-up and reports the wait as the pre-warm cold cost;
  `--no-prewarm` restores the old behaviour so the change can be measured in one build.
