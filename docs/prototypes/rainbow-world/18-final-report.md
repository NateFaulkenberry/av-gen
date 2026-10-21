# Rainbow World 18: Rainbow World 2, final report

- **Briefs:** `13-brief-stem-reactivity.md`; refined by `19-brief-color-vfx.md`; the camera by `20-camera-note.md`.
- **Branch:** `art/rainbow-world-2`.
- **ADRs:** 1570-1573.
- **Review media:** `~/Desktop/av-gen-review/76-rainbow-world-2/`.

> Status: final checks done on the default flight; awaiting the owner's verdict on the flight (default or more-soar).

## 1. The deliverables

| # | Deliverable | Where |
|---|---|---|
| 1 | Stem analysis report | `14-stem-analysis.md`, `stems/`, `scripts/stem_analysis.py` |
| 2 | Research dossier | `15-research.md`; the camera's: `21-camera-research.md` |
| 3 | Effect mapping | `16-effect-mapping.md` (each effect's trigger, feature, conditioning, scope; the looks; the camera; the hierarchy; the timeline) |
| 4 | The Rainbow World pass | `examples/rainbow-world/` (`blender/music.py`, `blender/look.py`, `build_world.py`), the live project `rainbow-world.json` |
| 5 | The Feline Footwear showcase | `examples/rainbow-world/rainbow-world-feline.json` (the mix + three stems, the drums stem feeding the beat clock; usage in `README.md`) |
| 6 | Validation captures | `~/Desktop/av-gen-review/76-rainbow-world-2/` (gate 4: the flight proof, the looks at two moments, the lodBias comparison) |
| 7 | Performance report | `17-slow-frames.md` (sections 5-8: power state, the arches, each cut, the live ladder) |
| 8 | Tests and usage | `tests/unit/test_route_fallback.cpp`, `test_node_program.cpp`, `test_live_profile.cpp` (trace row), `tests/integration/test_sonic_trace_stems.cpp`, `test_macro_seek.cpp`, `tests/unit/test_rainbow_world_music.cpp`; usage in `README.md` ("Playing it") |

## 2. What changed in the engine

| ADR | Change | Why |
|---|---|---|
| 1570 | A route can be `optional` (an unknown source is expected: silent, not a binding problem or dead route), and a depth signal can have a `depthDefault` | One live project follows stems when they exist (files offline, mapped input channels live) and the main mix otherwise |
| 1571 | The headless live profile writes a per-frame trace; the sonic trace writes `stem.*`, `beat.*`, `env.*`, `macro.*` | To place slow frames on the ride and compare runs frame by frame; to check stem features against the grid |
| 1572 | A glTF node can be drawn through a material program | The arches are blended glTF tubes; a procedural node draws blend opaque |
| 1573 | A seek replays the control layer when routes feed a source | The scan, swell and synth macros landed a frame (or an envelope) wrong after a seek |

## 3. Performance

Live auto ladder, Feline showcase, 90 s, 1080p30, on AC (17 section 8): **High 0.85 for 2400 frames, Medium 184,
Ultra 117; frame p50 33.4 / p95 35.8 ms; GPU p50 23.5; 4 misses.** As built it sat at Emergency 0.5 with 84 misses.
The cuts and their visual costs are in 17 section 7.

## 4. Limitations

- **Stem hits are late:** the stems' hits tier is up to a 16th late on this kit, so the kick and clap read levels and
  bands, not hits (14 section 5).
- **No stem pitch:** nothing follows the synth's notes.
- **The scan needs the synth stem:** on a stereo live input it stays off.
- **The clap gate** can fire on an early beat's highs (a hat or a fill).
- **The arches' program runs the interpreter** (glTF entity programs are not compiled): about 2.2 ms under an arch.
- **Perf depends on the machine's state:** battery measured 25-35% slower; numbers are AC only.
- **The look selector** is the documented exception to seek equals play.
- **The flight's soar** is about 35% of the loop: the knot crosses faces every ~600 m and the ride dives to each rim.

## 5. Final checks

After a fresh CMake configure (`cmake --preset release --fresh`) and a full build (exit 0), on the default flight:

| Check | Head | Exit | Counts |
|---|---|---|---|
| `tools/ci/run-suite.sh gpu` | 2f0b16c6 | 0 | 682 passed, 0 failed, 1 skipped (no NDI runtime); 683/683 cases, 728,939 assertions, 39m 23s |
| `tools/ci/run-suite.sh cpu-assets` | 2a323a41 | 0 | 4401 passed, 0 failed, 29 skipped, 1 expected failure (the suite's one shouldfail); 4431/4431, 19m 31s |
| `python3 tools/review_examples.py --only "Rainbow World"` | 2a323a41 | 0 | Rainbow World ok |
| `python3 tools/check_project_integrity.py` | 2a323a41 | 0 | 0 problems |

The first cpu-assets run failed one case: the hygiene check that every writer of a parameter final has an Inspector
kind found ADR-1573's restore in `app/engine.cpp`. It restores what the routes wrote (Kind::Route); 2a323a41 names it
in the test. The GPU suite's binary does not contain that test, so its run at 2f0b16c6 stands.

The more-soar flight (`RW_MORE_SOAR=1`) is a variant for the owner to compare; if it is chosen, the projects are
rebuilt and cpu-assets, the review and the integrity check run again.
