# ADR-1675: Thirty-two material program slots

- Status: Accepted (2026-10-25), scene/talking-heads-fx (17-brief-signal-corruption.md)
- Code: `src/rendering/material_programs.hpp` (`kMaxGpuMaterialPrograms`), `src/rendering/material_programs.cpp`
  (the upload), `shaders/material.wgsl` (`MaterialProgramBlock`, `MAT_MAX_PROGRAMS`), `src/scene/route_liveness.cpp`
  (`kGpuProgramSlots`).
- Tests: `tests/rendering/test_material_gpu.cpp` ([adr1675]: the 17th and 32nd programs render, the 33rd falls back,
  and a table that shrinks draws with its new programs), `tests/unit/test_route_liveness.cpp` (`program-not-uploaded`
  now fires at the thirty-third program), `tests/unit/test_glowmere_tree_fireflies.cpp`.
- Supersedes the slot count of ADR-1290.

## Context

Talking Heads' corruption pass gives each of up to four busts a body and a head program. Three of the four profiles
also give it two copies (the tracking and broadcast colour channels, the ghosting frames), each with its own routed
opacity. With the background's seven programs that comes to 21. The table held sixteen, so the fourth bust's porcelain
and every copy past the sixteenth program were dropped. Liveness flagged them as dead routes, and they shaded as plain
material with no other sign. Merging the copies into one program would tie the two channels' opacities together.

## Decision

The table holds thirty-two programs. The block is a read-only storage buffer (ADR-036): 16 + 32 × 7,536 = 241,168
bytes (with ADR-1661's 64-op programs), well inside every backend's storage-binding limit. The select uniform buffer gains sixteen 256-byte regions.

The update used to write the whole block every time it ran. It now writes only the header and the `count` programs in
use. The shader rejects any slot at or past `count` (`programIndex >= materialPrograms.count`), so the bytes past it are
never read, and their staleness is unobservable. A scene of a few programs therefore uploads a few programs, not 241 KB.

## Consequences

- Scenes with sixteen or fewer programs keep the same slots, indices and pixels.
- `program-not-uploaded` now fires at the thirty-third program.
- The per-update upload shrinks for every scene that does not fill the table (it was the whole table every time).
