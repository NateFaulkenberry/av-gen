# QA pass 2: final report

2026-10-27. Branch `eng/qa-pass-2` (worktree `../av-gen-qa2`), from main `3ea9b678`; main had not moved at handback.
Brief: `00-brief.md`. Plan: `PLAN.md`. Running log: `PROGRESS.md`. ADRs 1801-1805. Review media:
`~/Desktop/av-gen-review/100-qa-pass-2/`. Machine: Apple M2 Max, 64 GB, macOS 26.7.1, LG UltraFine at 60 Hz, **AC
power for every performance number** (`pmset` logged in each run's `conditions.log`).

This pass builds on `docs/qa-pass/` (2026-09-28) and `docs/audits/repository-cleanup/` (2026-10-14) and does not redo
them. One agent did all of it (the owner's rule); the brief's independent review is the coordinator's to arrange.

## A. Executive summary

- **Talking Heads live regression: fixed, root cause found.** The foil merge took Talking Heads to 1,306 routes; the
  Control panel re-checked every route's liveness badge in one frame, once a wall-clock second. That frame missed the
  deadline: 31-32 misses per 900 frames, period 1.00 s, absent headless. ADR-1802 spreads the re-check over frames and
  stops drawing off-screen route rows. Live: **32 -> 1 miss/900, p99 47.3 -> 36.1 ms**, T1-T4 pass; the baseline
  entry is updated. The look is untouched (UI-only change).
- **Scene-switch freeze in live sets: fixed.** A first switch to a scene with an unlit surface held the main thread
  **17 s** on a cold shader cache: the unlit fast-path pipeline (mislabelled `procedural-refract`) was built
  synchronously in the frame. ADR-1805 builds it, and the refraction pipelines, in the background in the live editor;
  offline output is unchanged.
- **Seek is not play: fixed for 11 of 13 affected projects** (ADR-1801). Integrating routes summed over the seek
  replay's fixed 60 Hz grid, not the render's 30 fps; Neon Vector's noise drift landed 14 m off. Seek output changes
  for the 13 shipped projects listed in C.
- **The baseline CPU suite failed** (exit 1) on the MIDI rig test: environment-dependent, because a live scene whose
  generated scene file was missing "loaded" onto the default scene and was counted. Fixed with the measured common
  set (ADR-1804).
- **Other defects fixed:** a project switch bound the last project's timeline against the new one (spurious
  warnings); a live project's audio routes were reported "dead" at load; `tools/ci/build.sh` could not run on a Mac
  without ccache; the CoreMIDI wildcard test raced other test processes; a failed UNSTABLE `materials.json` rewrite
  was ignored (CA-017).
- **New coverage:** `tools/ci/e2e.py` (the app end to end: live scene switches, synthetic audio, virtual MIDI, a
  headless render), seek-parity tests, a sweep test, a clock-lamp test, a timeline-leak test, a liveness test, and a
  hygiene test that keeps every `AVGEN_*` variable documented.
- **Refactor:** `engine.cpp` 8,091 -> 5,754 lines; project persistence moved verbatim to `engine_project.cpp`.
- **Unresolved:** the renderer's rare non-repeatability under GPU contention (ADR-1563's open item; the probable cause
  of the EXR flake), Trench God's seek, a particle `burst` on a seek's landing frame, and everything in H.

## B. Baseline versus final verification

| | baseline (3ea9b678 + build.sh fix) | final (this branch's last commit) |
|---|---|---|
| Release build | exit 0, 478 s, 3,758 warnings | exit 0 |
| CPU suite (`gpu-lock.sh run-suite.sh cpu-assets`) | **exit 1**: 4544 pass, 1 fail (rig test), 29 skip, 1 shouldfail, 4575 cases, 16 min | **exit 0**: 4554 pass, 0 fail, 29 skip (environmental: audio devices, private motion packs), 1 shouldfail, 4584 cases, 17m30s |
| GPU suite (`run-suite.sh gpu`) | exit 0: 711 pass, 0 fail, 1 skip (NDI), 47m52s | **exit 0**: 711 pass, 0 fail, 1 skip (NDI not installed), 712 cases, 48m52s |
| `check_project_integrity.py` | 0 problems | 0 problems |
| `tools/ci/e2e.py` | did not exist | PASS, 14 of 14 checks |
| Talking Heads live (3 reps) | 32 misses/900, p99 47.3 ms | 1, 36.1 ms |
| Sanitizers | not run (nightly CI's; 0 reports at the 09-28 pass) | not run |

- **Warnings:** the four implicit float->double promotions in `astral_conductor.hpp` were 2,384 of the 3,758 (one
  header, ~600 translation units; counted from the baseline log). The three `-Wswitch`, three ignored `[[nodiscard]]`
  in `src/` and the dead code are gone. Left: `-Wshadow`, `-Wmissing-field-initializers` and test-file warnings
  (no defect found behind them).
- The baseline GPU suite ran while I was building (contention); it still passed.
- Both final suites ran on `3d84918e` (the last code commit); the commit after it adds only these reports, and the
  CPU suite's `[hygiene]` cases (which read docs) were re-run on it.

## C. Defects fixed

| defect | root cause | subsystem | regression test |
|---|---|---|---|
| Talking Heads misses a deadline every second, live (P1) | ADR-902 route badges re-checked for all 1,306 routes in one frame once a second; every row drawn every frame | UI | `test_route_row_logic.cpp` `[adr1802]`; the live baseline |
| 17 s freeze on a first scene switch, cold cache (P1) | unlit fast-path pipeline built synchronously in the frame; refraction the same way | renderer | e2e run (`render (background)` lines); offline unchanged by construction |
| Seek lands off the play for integrating routes / random, envelope sources (P1) | the signal replay's 60 Hz grid vs the render's grid; non-pure sources reset | engine seek | `test_integrate_seek.cpp` `[adr1801]` (fails with `AVGEN_NO_CONTROL_REPLAY`); `test_control_seek.cpp` |
| CPU suite fails where generated scenes exist (P1) | the rig test counted default-scene loads as loaded | MIDI rig, test | the rig test itself |
| A project switch logs the last project's timeline targets as the new project's warnings | old timeline rebound mid-load | project load | `test_project_lifecycle.cpp` (mutation-checked) |
| A live project's audio routes reported dead at load | the audit's `hasAudio` ignored live projects | route liveness | `test_project_lifecycle.cpp` (mutation-checked) |
| `run-suite.sh --build` fails locally | empty array under `set -u` on bash 3.2 | CI scripts | ran it |
| CoreMIDI wildcard test flakes | a system-wide count raced other test processes | tests | itself (24 parallel runs of the related EXR case pass; this one by construction) |
| UNSTABLE `materials.json` rewrite unchecked (CA-017) | stream state never read | render job | none (error path) |
| No lamp for `beat.source` 4 | the lab predates ADR-1783 | Audio Response Lab | `test_audio_response_lab.cpp` (every clock lamp) |
| `live_perf_baseline.py run` passed a run that skipped a scene | one-line skip, exit 0 | tooling | ran it |

**ADR-1801's seek-output change, by project** (Feline, seek to 60 s vs play to 60 s, parameters that differed):
Neon Vector World 60 -> 0, Glitch Signal 24 -> 0, Chromatic Topography 32 -> 0, Cymatic World 24 -> 0, Waveform
Dimension 18 -> 0, Resonant Silk 9 -> 0, Datascape 7 -> 0, Infinite Color Geometry 6 -> 0, Phase Hall 5 -> 0, Stellar
Nursery 2 -> 0, Sacred Geometry Flight 69 -> 1, 8-Bit Ocean 38 -> 1 (a landing-frame particle burst), Chorus Field -
Trench God 12 -> 12 (not a route chain; unchanged). Renders from 0 are unchanged; only seeks and mid-track starts move.

## D. Dead code and artifact cleanup

- Removed: six unused JSON helpers (`world/atmospherics.cpp`), an unused bind-group lambda (`SceneRenderer::init`),
  `InterestConsiderer::candidates`' unread count, a duplicated comment (CA-020). Each: no caller (compiler-verified).
- Not removed, for the owner (unchanged since 10-14, re-verified: still no `src/` or `tools/` caller, tests only):
  `app/profile_capture`, `ai/inference`, `scene/tree_audio` (named in one comment), `scene/motion_variants`,
  `scene/motion_search_index`, `entity/motion_evaluation`. The measurement arms (`AVGEN_LEGACY_PROCGEN` and the rest)
  are kept and documented (ADR-1803); retiring one is its owner's call.
- No tests removed. One test's contract changed with ADR-1801 (`test_control_seek.cpp`'s "does not pay": its project
  now has no integrating route) and one was made honest (the rig test).
- No assets or generated files committed. The Aurora Borealis generator reproduced its committed project byte for
  byte; the lab generator did too before the lamp.

## E. Refactoring report

- **`src/app/engine.cpp` (8,091 -> 5,754) -> `src/app/engine_project.cpp` (2,422):** the project document, save,
  copy, bundle, load, new project, the unsaved-changes sample and the asset-reference/relink helpers. Moved
  byte-identical (checked by script); no interface change; no helper crosses the boundary. Engine's
  update/seek/replay logic stays in `engine.cpp`.
- **`src/ui/control_panel.cpp`'s route list:** the badge schedule became `LivenessSweep` in the ImGui-free
  `route_row_logic.hpp`, so it is unit-tested.
- **Left intact, and why:** `scene/composition.cpp` (13,529; 10-14 audit CA-025 kept it, and its users include
  Glowmere: a split there needs its own pass with render hashes); `app/application.cpp` (8,005) and
  `rendering/scene_renderer.cpp` (5,509): no seam as clean as the engine's persistence block, and both carry live-path
  code this pass was measuring; `ui/control_panel.cpp` (5,280): the route tab is a candidate next split.

## F. Documentation audit

- Updated: `docs/architecture.md` (16 of 32 modules -> all; directory layout), `docs/help/reference-command-line.md`
  (every `AVGEN_*` variable, measurement arms), `docs/help/audio-input.md` (the cut drum classifier),
  `docs/audio-response-lab.md` (four clock lamps), `docs/testing.md` (End to end), the ADR index (range 1800-1849).
- Verified working: README's and `docs/build.md`'s commands (presets, `make_test_audio.py`, every `avgen` flag shown).
- Historical, kept as records: ADRs, `docs/prototypes/`, both earlier QA reports, `response-range-report.md` (names
  `kickEnv`/`hatEnv` as history).
- Gaps: `docs/architecture.md` §1's data-flow diagram is still milestone 0.1's (says so); the CLI reference still
  lists the common flags only.

## G. End-to-end testing

- `tools/ci/e2e.py`, run on this Mac: the live editor with a generated 120 BPM track playing, the `midi` ui-script arm
  (two CoreMIDI **virtual** sources, a scripted performance, Learn into mappings), four `--open-sequence` switches
  between Hyperspace and Glitch Signal, a capture, an orderly exit; then a headless render of 60 frames from 4 s.
  14 of 14 checks pass; resident memory 581 -> 581 MB across the switches. Its first run found the timeline leak.
- **Hardware:** real GPU and display, yes. **MIDI: synthetic only** (virtual CoreMIDI sources; no physical
  controller was connected, and the rig layer and mappings have never been tested on hardware). Audio input: the
  live path was driven from a file, not a physical input.
- Inspected by eye: the Talking Heads route list (top of the list only, not scrolled) and the Audio Response Lab
  board with the new lamp. Everything else is automated checks only.

## H. Remaining issues

1. **Renderer non-repeatability under GPU contention** (ADR-1563, open): the probable cause of the EXR determinism
   flake. 24 parallel runs of that case passed here; not reproduced, not fixed.
2. Trench God's seek (camera and face fields, not route chains) and the particle `burst` on a seek's landing frame.
3. A follower route rebuilt by the 60 Hz signal replay is a fraction of a release off on a 30 fps seek (ADR-1801
   consequences); moving that replay's grid would change entity seeks, Glowmere's included.
4. The cold-cache case of ADR-1805 was not re-measured (it would have cleared the owner's shader cache).
5. Event Horizon missed 24-30 deadlines per run late in the session with both this build and `3ea9b678`
   (interleaved), against 2-4 earlier: machine state, not code. Its baseline entry was left alone.
6. CA-003 (renders ship fallback content), CA-007, CA-008, CA-011 (wall-clock assertions in the default set), CA-018
   remain from the 10-14 audit.
7. MIDI on hardware; scrolling the clipped route list by eye.
8. **The Desktop Feline project** (`~/Desktop/Feline Footwear/`, read-only, not edited): its live `.json` files name no
   retired signal; only the `*.bak*` files do (`signal.kick`), and `make_project.py:401` has a stale comment saying
   the drum stem runs the kick/snare/hat classifier (cut by ADR-1784).

## I. Final repository status

Branch `eng/qa-pass-2`; working tree clean apart from local, untracked conveniences that are not committed (the
symlinked `examples/rainbow-world/scene`, excluded locally; the generated Aurora scene, gitignored). Every change is
committed; nothing pushed.

### Stale branches and worktrees (for the owner; nothing deleted)

Merged into main, worktree still present: `av-gen-borealis-eng`
(`eng/aurora-borealis`), `av-gen-cut` (`chore/cut-living-prism`), `av-gen-disp` (`eng/water-flow-streaks`),
`av-gen-ksh2` (`eng/mix-hit-phaseout`), `av-gen-p0` (`eng/latency-levers`), `av-gen-prism` (`scene/living-prism-merge`),
`av-gen-rainbow2` (`fix/rainbow-world-ride-determinism`), `av-gen-stems2` (`eng/stem-roles`), `av-gen-rwperf`
(detached). Worktrees on parked, unmerged branches: `av-gen-aurora` (`scene/living-prism-aurora`, the scrapped aurora), `av-gen-astral2`, `av-gen-furnace`, `av-gen-jungle`, `av-gen-biovalley-2d`, `av-gen-biovalley-water`, `av-gen-slammers`. Merged branches with no worktree: `art/*` (5), `audit/living-prism`, `eng/audio-courtesy`,
`eng/aurora-ribbons`, `eng/living-prism-p0/p1/p1b`, `eng/perf-reliability`, `eng/refraction-dispersion`,
`eng/remove-ksh-detect`, `eng/response-range`, `eng/stems-feed`, `eng/stems-multichannel`, `eng/terrain-textures`,
`fix/feline-revisions`, `fix/live-worlds-section`, `fix/rainbow-world-scan-regression`, `perf/rainbow-world-live`,
`perf/resonator-live`, `proto/generative-rd`, `proto/phase-wall`, `scene/living-prism`, `scene/talking-heads`,
`scene/talking-heads-fx`, `tmp/live-guide`, `ui/live-transport`. Unmerged and parked (leave): `proto/*` (5),
`scene/cursed-slammers`, `scene/dmt-hyperspace`, `scene/prismatic-dream`, `scene/shattered-lattice`,
`scene/living-prism-aurora`, `investigate/live-render-perf`.

### Scene-removal candidates (the owner's call; none cut)

From the 10-14 removal-impact study, class A (nothing else depends on them), all still in the index: The Tide,
Fungi, Organic Curves, Alien Squad - Modular Characters, Farm Animals - Imported Pack, Open Vista (stress), Astronaut
Musicians - Prototype.

## What changed for users

- The Modulation panel's route list stays smooth with very large projects; a route's dead/hazard badge can take up to
  about a second to update after the scene changes (an edit to the route itself updates at once).
- Switching live scenes no longer freezes on a cold shader cache; for the first seconds after a first switch, unlit
  surfaces and glass may draw their fallback look (glass looks opaque) until their shaders are ready.
- Scrubbing or starting a render mid-song now lands exactly where playing to that point does, for projects with
  integrating routes or random/envelope sources. Some seeks cost about a quarter more time.
- The Audio Response Lab has a fourth clock lamp, MANUAL CLOCK, lit when the beat comes from a marked beat.
- The MIDI rig's destination picker no longer marks the camera, environment, fog, volume, wind and root parameters as
  "this scene": every live scene has them.
- `avgen` warns at startup listing any `AVGEN_*` environment variable that is set; live profiles record them.
- The command-line help page lists every `AVGEN_*` variable, with the measurement arms in their own table.
- Opening a project no longer shows timeline warnings that belong to the previously open project.
- A live project no longer logs its audio routes as dead when it is opened without audio.
- `tools/live_perf_baseline.py run` fails when it skips a scene (and says how to generate it); `write` refuses runs
  with `AVGEN_*` overrides or not on AC power.
- New tool: `python3 tools/ci/e2e.py` runs the app end to end.
- Help: no drums source detects kicks, snares or hats; every source's onsets are filed by register.
