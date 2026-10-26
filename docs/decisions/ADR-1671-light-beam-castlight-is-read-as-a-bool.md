# ADR-1671: Light Beam's `castLight` is read as the Bool it is

- Status: Accepted (scene/talking-heads; the owner approved the fix, 2026-10-25)
- Found by ADR-1670.
- Code: `src/world/effects/kinds/light_beam_effect.cpp`, which reads `castLight` through `StoredRows::b()`.
- Test: `tests/unit/test_shell2.cpp` `[adr1671]`. A beam loaded with `castLight: false` asks the pool for no light,
  `true` asks for one, and an unset row keeps the default (lights). The cone draws byte-identically either way.

## Problem

Bool rows live in the effect value store's flags (`setFieldBool`), but `emit` read `castLight` through
`StoredRows::f()`, which looks only at the store's floats and fell back to the row's default (true). So a project's
`"castLight": false`, and the panel's checkbox, never turned off an Entity or World beam's pool light. A beam with a
non-zero `light` always took one of the 16 effect-light slots.

## Decision

Read the row as a Bool: `kRows.b(e, "castLight")`.

## Consequences

- **No shipped picture changes.**
  - The only tracked project that writes `castLight: false` is 8-Bit Ocean (`examples/sonic-abstract/bit-ocean.json`,
    beams `shaft0`-`shaft5`). It also writes `light: 0`, and a pool light is only asked for when `light > 0`, so it
    never had one.
  - Rendered before and after the fix (realtime tier, 1080p, 13.0-14.0 s), the frames are pixel-identical, with the
    same sequence hash `cde0c040995e3c0a`.
  - Talking Heads also writes `castLight: false` with `light: 0`.
- **The same defect elsewhere, reported and not changed here:**
  - Force Field reads `revealCamera` and Halo reads `screenSize` through `f()`. Halo's styles also write it with
    `set()` as a float, so its styles and its file disagree.
  - No tracked project sets either row.
  - Orbit and Spiral read `reverse` correctly (`XformRows::flag`), and Particle Emitter reads `trail` correctly.
