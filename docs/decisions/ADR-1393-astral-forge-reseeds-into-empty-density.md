# ADR-1393: Astral Forge re-seeds into an empty density field

**Status:** Accepted (found by the project-switch test, 2026-10-15).
**Date:** 2026-10-15

## Context

`cs_step` repels matter down `densTex` ("last frame's density"). A re-seed (`initParticles`: a seek, or the forge
opened again in the same editor) cleared the splat grid but not the texture resolved from it, so the new seed's first
steps were pushed along the previous simulation's density. Astral Forge - Trench rendered after any other project, in
an editor that had shown it before, differed from a fresh render in ~11,500 of 57,600 pixels.

## Decision

The re-seed resolves the empty grid into `densTex` and the coarse grid in the same submit, which is what a fresh
renderer's zero-initialised textures hold.

## Consequences

`tests/rendering/test_project_switch_gpu.cpp` is exact for the Astral Forge probe after every lab and live scene, and
after every example in the `[.sweep]` run.
