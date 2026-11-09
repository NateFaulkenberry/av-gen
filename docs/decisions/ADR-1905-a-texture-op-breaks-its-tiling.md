# ADR-1905: A texture op breaks its tiling

- Status: Proposed (Art Pass 2, M3e), scene/strange-attractor.
- Extends: ADR-1642, which lets a material program read textures triplanar in world space.
- Code:
  - `matTextureTile` and `matTextureOp` in `shaders/material.wgsl`;
  - `textureTile` and `textureOpValue` in `src/scene/material_program.cpp`, the CPU reference;
  - the op's `constant3`, documented in `src/scene/material_program.hpp`.
- Tests:
  - `[material][adr1905]` in `tests/unit/test_material_program.cpp`;
  - GPU and CPU parity with hex tiling, macro variation and the second layer, in `[material][gpu][adr1642][adr1905]`
    in `tests/rendering/test_material_gpu.cpp`.

## Context

A photoscanned texture read triplanar repeats every tile. Its own large light and dark patches therefore line up into a
grid that is plain on a wide flat plain (owner, M3e notes §4: the Mars sand near the camera). Remedies that change only
the scene, such as noise multiplied over the read, hide the grid but cannot remove it: the period is still in every
read.

## Decision

A texture op's `constant3` = (hex, macro, macro scale, layer). All four are 0 by default, which is ADR-1642's plain read
unchanged.

1. **Hex tiling, `hex` 0..1** (Mikkelsen 2022, "Practical Real-Time Hex-Tiling").
   - Each plane's uv is covered by a triangle grid, with a vertex about every 0.67 tiles.
   - Each vertex reads the image shifted by a hash of the vertex and turned by up to ±180° × hex.
   - A fragment blends its triangle's three reads by their barycentric weights to the 6th power, so the seams are
     narrow and the image's contrast is kept away from them.
   - A normal map's decoded x and y are turned back by each read's angle, so its relief stays consistent.
2. **Macro variation, `macro` 0..1.** The colour is scaled by 1 ± 0.8 × macro × a world-space value fBm. The fBm's period
   is `macro scale` tiles (0 means 8).
3. **A second layer, `layer` 0..1.** The same image is read again 4.3× larger and turned 40°, with its own hex hashes,
   and mixed in at 0.4 × layer (colour only).

The mip is the op's own footprint mip, and the second layer's mip is lowered by log2 4.3. The explicit level means the
turned reads need no derivatives.

## Consequences

- **Cost.** Hex tiling is three reads per plane instead of one, and the second layer doubles that. This is
  offline-first (art before optimisation).
- **Every existing program is unchanged**, because `constant3` defaults to 0 and is written to JSON only when set.
- **Scenes:**
  - the canyon's close-up ground (`build_journey.py`);
  - the Mars sand (`build_mars.py`);
  - the ocean reef's rock and sand (`build_ocean.py`).
- **Not covered.** A glTF's own UV-mapped textures, such as the pyramids', are outside material programs. Their
  tiling is broken where the asset is built.
