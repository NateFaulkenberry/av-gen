# ADR-1661: A material program can hold 64 ops

- Status: Accepted (scene/aurora-borealis)
- Extends ADR-036 (the shared op budget, 16 → 48). Changes no op, no register and no packing rule.
- Asked for by Aurora Borealis pass 3 (`docs/prototypes/aurora-borealis/22-research-pass3-scene.md` §5.1, §9 E1): the
  right bank's program carries two instrument glint terms (toms and tone block), the crash's crest flare and the glass
  rim, which does not fit 48.
- Code:
  - `src/scene/material_program.hpp`: `kMaxMaterialOps` 48 → 64. `MaterialProgramGpu` grows by 16 ops of 112 bytes
    (5,744 → 7,536 bytes); its `static_assert` and the layout guard are parametric.
  - `shaders/material.wgsl`: `ops: array<MaterialOpGpu, 64>` and `MAT_MAX_OPS` 64.
  - `docs/procedural-materials.md`: the budget and the layout table.
- Tests: `tests/unit/test_material_program.cpp` (`[adr1661]`: a 48-op layered program packs to the same 5,744 bytes,
  by an FNV-1a hash taken from the 48-op build, and every byte past them is zero; the validation and layer budgets
  are stated in `kMaxMaterialOps`); `tests/rendering/test_material_gpu.cpp`'s full-budget cost case spends 64 (a
  44-op base plus four layers of five); `tests/unit/test_renderer_layout_guards.cpp` re-derives the WGSL twin's size.

## Context

Programs compile to straight-line WGSL per scene (ADR-1493), so a program's cost is its own ops, not the ceiling. The
ceiling is a validation limit, plus the length of the interpreter's packed array, which the interpreter path walks
only up to each program's packed op count. Raising it costs storage only: the 16-slot program block grows from about
92 KB to 120 KB.

The Aurora Borealis land is one unlit program per bank. The pass-2 program was 47 ops. Pass 3 adds the tone block's
edge glints and the toms' plane flashes on the right bank (5 ops each), a crash crest flare (3 ops), and widens the
glint line on a hit (1-2 ops). Without more room the right bank loses its glass rim, the edge-on catch-light that
reads the facets against the sky.

## Decision

Raise the shared budget to 64. Nothing else changes: the header, the layer records and the first 48 op slots sit at
the same offsets, and a program packs to the same bytes it did, followed by zeroed slots it never reaches.

## Consequences

- Every existing material renders the same (the golden pack hash, and the GPU suite's unchanged scene hashes).
- A scene may now write programs that a build before this ADR rejects at validation.
- The program block is 28 KB larger on the GPU.

## Rejected

- **Dropping the right bank's rim to fit 48** (B's probe did this): the rim is half of how the facets read at dusk.
- **A second material on the right bank**: two draws of the same mesh and two programs, for the same result.
- **128 ops**: no scene needs it, and the block would double.
