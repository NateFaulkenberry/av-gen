# Rainbow World 18: Rainbow World 2, final report

- **Brief:** `13-brief-stem-reactivity.md`.
- **Branch:** `art/rainbow-world-2`.
- **ADRs:** 1570-1572, plus the 1571 addendum.
- **Review media:** `~/Desktop/av-gen-review/76-rainbow-world-2/`.

> Status: DRAFT while rendering is paused (owner, 2026-10-20). The numbers marked "to re-measure" are re-taken on a
> quiet machine before hand-back.

## 1. The deliverables

| # | Deliverable | Where |
|---|---|---|
| 1 | Stem analysis report | `14-stem-analysis.md`, `stems/`, `scripts/stem_analysis.py` |
| 2 | Research dossier | `15-research.md` |
| 3 | Effect mapping | `16-effect-mapping.md` (layers, each effect's trigger, feature, conditioning, shape, limits, the timeline) |
| 4 | The Rainbow World pass | `examples/rainbow-world/` (`blender/music.py`, `blender/look.py`, `build_world.py`), the live project `rainbow-world.json` |
| 5 | The Feline Footwear showcase | `examples/rainbow-world/rainbow-world-feline.json` (the mix + three stems; usage in `README.md`) |
| 6 | Validation captures | `~/Desktop/av-gen-review/76-rainbow-world-2/` |
| 7 | Performance report | `17-slow-frames.md` and section 3 below |
| 8 | Tests and usage | `tests/unit/test_route_fallback.cpp`, `test_node_program.cpp`, `test_live_profile.cpp` (trace row), `tests/integration/test_sonic_trace_stems.cpp`, `tests/unit/test_rainbow_world_music.cpp`; usage in `README.md` ("Playing it") |

## 2. What changed in the engine

| ADR | Change | Why |
|---|---|---|
| 1570 | A route can be `optional` (an unknown source is expected: silent, not a binding problem or dead route), and a depth signal can have a `depthDefault` | One live project follows stems when they exist (files offline, mapped input channels live) and the main mix otherwise |
| 1571 | The headless live profile writes a per-frame trace; the sonic trace writes `stem.*`, `beat.*`, `env.*` | To place slow frames on the ride and compare runs frame by frame; to check stem features against the grid |
| 1572 | A gltf node can be drawn through a material program | The arches are blended glTF tubes; a procedural node draws blend opaque |

## 3. Performance (to re-measure)

See `17-slow-frames.md`. Summary so far:
- The spikes are not compiles, not contention, and not content (section 1 there).
- The leaf cards beyond 90 m were the arch holds' cost; a fourth leaf LOD level fixes the 40 s hold.
- The swell is the effects' main cost; the programs are cheap.

## 4. Limitations

To be completed.
