# ADR-1394: Renderer state keyed by name or by address follows the scene's identity

**Status:** Accepted (found by the project-switch test, 2026-10-15).
**Date:** 2026-10-15

## Context

The alien project ("Alien - Skinned Character") rendered after the Character Animation Lab differed from a fresh render
in 9,787 of 57,600 pixels: the floor's edge, the characters' shadows and feet. The engine's state was identical (rig
joints and entity positions, `tests/integration/test_project_switch_pose.cpp`); a fresh engine drawn by the switched
renderer reproduced the difference exactly. `ProceduralRenderer` keeps per-object GPU state keyed by the object's
**name** and re-uploads the instance records when `structureVersion` moves. Both are per scene: both projects have a
procedural "floor", their version counters start at the same small numbers, and on a match the lab's 46 x 16 m floor
records stayed on the GPU under the aliens.

`SkinningRenderer`, `ParticleRenderer` and `SceneRenderer`'s temporal check decide "a different scene" by the scene's
**address**, which a new project's scene can reuse (the mesh and texture uploads already compared `Scene::identity`
for exactly that reason).

## Decision

`ProceduralRenderer` drops its per-object state when `Scene::identity` changes (source meshes are keyed by content and
stay). Skinning, particles and the temporal reset compare the identity as well as the address. The procedural change
is the one shown to matter; the other three are the same mistake in waiting and are changed with it.

## Consequences

`tests/rendering/test_project_switch_gpu.cpp` gained the alien project as a third probe and a character-lab case:
exact after the Character and Animation labs (and every lab and live scene), with a control that requires the labs'
overlays, carried, to show.
