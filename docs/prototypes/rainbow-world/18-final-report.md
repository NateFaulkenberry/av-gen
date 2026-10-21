# Rainbow World 18: Rainbow World 2, final report

- **Briefs:**
  - `13-brief-stem-reactivity.md`, refined by `19-brief-color-vfx.md`;
  - the camera: `20-camera-note.md`;
  - the scan's sparkle: `24-brief-scan-sparkle.md`;
  - the owner's verdicts, verbatim: `22-owner-fixes.md`.
- **Branch:** `art/rainbow-world-2`.
- **ADRs:** 1570-1576.
- **Review media:** `~/Desktop/av-gen-review/76-rainbow-world-2/`.

> Status: gates 1-8 done. Gate 7 was approved by the owner ("the scene is looking really nice now"). Gate 8's three
> fixes (live popping, the cloud pass, the final render) are done and await the owner's look. The final checks are
> below.

## 1. The deliverables

| # | Deliverable | Where |
|---|---|---|
| 1 | Stem analysis report | `14-stem-analysis.md`, `stems/`, `scripts/stem_analysis.py` |
| 2 | Research dossier | `15-research.md`; the camera's: `21-camera-research.md` |
| 3 | Effect mapping | `16-effect-mapping.md` (each effect's trigger, feature, conditioning, scope; the looks; the camera; the hierarchy; the timeline; revised for gates 5-8) |
| 4 | The Rainbow World pass | `examples/rainbow-world/` (`blender/music.py`, `blender/look.py`, `blender/ray_asset.py`, `blender/clouds.py`, `blender/engine_assets.py`, `build_world.py`), the live project `rainbow-world.json` |
| 5 | The Feline Footwear showcase | `examples/rainbow-world/rainbow-world-feline.json` (the mix plus three stems, the drums stem feeding the beat clock; usage in `README.md`). The whole song is rendered at `final-feline-footwear.mp4` in the review folder |
| 6 | Validation captures | the review folder: gate 3-8 clips and stills, each before and after |
| 7 | Performance report | `17-slow-frames.md` (sections 5-8: power state, the arches, the cuts, the live ladder; 10: gate 5; 11: the final scene against the owner's bar) |
| 8 | Tests and usage | listed below; usage in `README.md` ("Playing it") |
| 9 | Gate write-ups | `25-tree-popping.md`, `26-world-scan.md`, `27-scan-sparkle.md`, `28-twirl-sky-sun.md` |

The tests:
- `tests/unit/test_route_fallback.cpp`, `test_node_program.cpp` and `test_live_profile.cpp` (the trace row);
- `tests/integration/test_sonic_trace_stems.cpp` and `test_macro_seek.cpp`;
- `tests/unit/test_rainbow_world_music.cpp`, `test_envelope_latch.cpp` and `test_procedural_flat_far_rungs.cpp`.

## 2. What changed in the engine

| ADR | Change | Why |
|---|---|---|
| 1570 | A route can be `optional` (an unknown source is expected: silent, not a binding problem or a dead route), and a depth signal can have a `depthDefault` | One live project follows stems when they exist (files offline, mapped input channels live) and the main mix otherwise |
| 1571 | The headless live profile writes a per-frame trace; the sonic trace writes `stem.*`, `beat.*`, `env.*`, `macro.*` | To place slow frames on the ride and compare runs frame by frame; to check stem features against the grid |
| 1572 | A glTF node can be drawn through a material program | The arches are blended glTF tubes; a procedural node draws blend opaque |
| 1573 | A seek replays the control layer when routes feed a source | The scan, swell and synth macros landed a frame (or an envelope) wrong after a seek |
| 1574 | A route's envelope can latch (`envelopeRefractoryMs`, `envelopeRearm`) | A live scan gated by a depth, or re-triggered on its fall, popped back on its tail |
| 1575 | A route chain can read a trend (`trendSeconds`: short-term average over long-term, floored) | A live trigger on a swell over the current bed, at any level |
| 1576 | A procedural node can keep its far LOD rungs at the frame's material tier (`"flatFarRungs": false`) | Live, the flat demotion (ADR-155) turned the ground's far bands another colour without shadows, so both popped at rung boundaries |

All seven are additive and off by default. 1574 and 1575 are chain state, replayed on a seek.

## 3. Performance

Live auto ladder, the Feline showcase, 15-215 s, 1080p30, on AC (17 section 11). The ladder spent High 4484 frames,
Medium 808, Ultra 385 and Low 269. Frame p50 / p95 was 33.4 / 35.9 ms, GPU p50 24.0 ms, with 8 misses.

Against the owner's bar (High outside the scans, Medium allowed in them, 30 fps held):
- **Held:** 30 fps, with 8 misses in 200 s.
- **Not held:** 15-48 s and 169-191 s sit partly at Medium, and 150-165 s at Low.

The late song is geometry-bound (High pinned: 25 ms at 70 s, 28-31 ms at 150-185 s, against a 29.3 ms budget), so a
lower render scale barely helps, and the result varies from run to run (17 section 11).

## 4. Limitations and follow-ups

- **Live perf in the late song:**
  - 150-200 s does not hold High on the M2 Max (section 3). Draw distance at 75% saves only 1.4 ms.
  - A follow-up would cut geometry there (the trees' and clouds' LOD, or the knot's far bands).
  - The owner said it is usable live.
- **The same pop in other live scenes:** any live scene with multi-rung procedural LOD probably shows the
  flat-far-rung pop that ADR-1576 fixes here.
  - A grep of the live projects' scenes for procedural `lod.count >= 2` finds one: the Rift
    (`examples/bioluminescent/rift-live.json`, `rift.scene.json`, 4 nodes).
  - Not changed on this branch; for the owner to decide.
  - Generated ecology layers are not covered by that grep.
- **Rainbow World's own trees** stay on ADR-155. Far trees can still shift shade at a rung, a few pixels each. Opting
  them out cost +8 ms at p95.
- **Overlapping cloud puffs** still meet in a line. Two meshes crossing would need a soft-depth or volumetric pass.
- **The twirl's variety** is by family and distance. Trees of one family move alike; no field slot was left for
  per-tree noise (16 GPU fields; the scene uses 13).
- **The sun's glare** has no occlusion: it is a billboard, so a hill does not cut its streaks.
- **Stem hits are late:** the stems' hits tier is up to a 16th late on this kit, so the kick and clap read levels and
  bands, not hits (14 section 5).
- **No stem pitch:** nothing follows the synth's notes.
- **The clap gate** (the arches and the cloud splashes) can fire on an early beat's highs (a hat or a fill).
- **The arches' program runs the interpreter** (glTF entity programs are not compiled): about 2.2 ms under an arch.
- **Perf depends on the machine's state:** battery measured 25-35% slower, so the numbers are AC only.
- **The look selector** is the documented exception to seek equals play.

## 5. Final checks

All four ran at head 412ed944, after a fresh CMake configure (`cmake --preset release --fresh`, exit 0) and a full
build (exit 0):

| Check | Exit | Counts |
|---|---|---|
| `tools/ci/run-suite.sh cpu-assets` | 0 | 4410 passed, 0 failed, 29 skipped, 1 expected failure (the suite's one shouldfail); 4440/4440, 19m 05s |
| `tools/ci/run-suite.sh gpu` | 0 | 682 passed, 0 failed, 1 skipped (no NDI runtime); 683/683, 38m 46s |
| `python3 tools/review_examples.py --only "Rainbow World"` | 0 | Rainbow World ok |
| `python3 tools/check_project_integrity.py` | 0 | 0 problems |

The GPU suite reran because the engine changed since 2f0b16c6 (ADR-1574, 1575, 1576).

**The full render:** `final-feline-footwear.mp4` in the review folder. It is the Feline showcase, the whole song
(210.5 s, 6315 frames, 1080p30, with audio, the default look), rendered at 2dd93a58. The one commit after it removes a
palette binding that never bound, with no picture change.
