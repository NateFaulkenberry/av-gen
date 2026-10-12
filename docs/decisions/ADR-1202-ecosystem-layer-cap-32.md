# ADR-1202: An ecosystem holds 32 emitter layers

- Status: Accepted (proto/rift-jungle)
- Amends ADR-1200 (the ecosystem's emitter layers).
- Found by: THE ROOTWORKS (`docs/prototypes/bioluminescent/11-jungle-art-direction.md`), the jungle that replaced
  the Rift's sea life. The owner's CP1 note was that the scene "lacks actual bioluminescence"; the fix is more kinds
  of light (leaf margins, moss on the roots, mycelium, flowers, per-limb glowworm curtains), each its own layer.

## Problem

`kMaxEcosystemLayers` was 16. A layer's template is one point file attached to one set of host meshes, so a scene
needs a layer per (template, behaviour) pair. The Rootworks needs 29: 10 leaf-margin layers (a margin template is
per leaf mesh), 8 glowworm layers (the 8 MIDI sub-biomes are 8 limbs), sap and moss on two tree meshes, mycelium,
flowers, two lichens, ground moss and plankton.

## Decision

The cap is 32. Nothing on the GPU is sized by it: `EcosystemRenderer` keeps one uniform buffer and one bind group per
layer and dispatches each layer separately (`ecosystem_renderer.cpp`, `syncLayer`), so the cap only bounds a
scene file's ambition. It stays a cap (a scene with hundreds of layers is a mistake, and each costs a dispatch).

## Consequences

- Cost is per point, not per layer: the Rootworks' 29 layers measured inside the ecosystem's usual ~1 ms budget at
  1080p (the lit pass and the medium dominate, 03-architecture.md §6).
- Scenes with 16 or fewer layers are unchanged.
