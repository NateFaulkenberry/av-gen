# Code-audit fixes applied

- Date: 2026-10-14. Worktree `av-gen-cleanup-code`, branch `cleanup/code-fixes`, based on `464995fe`.
- Source: `code-audit-findings.md` (CA-xxx) and `docs-audit-findings.md` §8 items 1-3.
- Build: `cmake --preset release -DCPM_SOURCE_CACHE=…/av-gen/.cache/cpm && cmake --build --preset release`. The whole tree, including every tool target, builds with exit 0 at the final commit.
- Not done (owner decisions): CA-003, CA-006, CA-016, CA-019, dead-module deletions, and `05-static-reference-vs-f027-full.png`.

## Final verification

`build/release/tests/avgen_tests` (full CPU suite, final code commit `330788f4`), exit code **0**:

```
test cases:     4240 |     4220 passed | 19 skipped | 1 failed as expected
assertions: 10343053 | 10343052 passed |  0 skipped | 1 failed as expected
```

The log has exactly one `FAILED:` line. It is `test_character_lab_slopes.cpp:187`, from the `[!shouldfail]` case "a body's lean on a slope is the same whichever way it faces", and it matches the header's "1 failed as expected".

---

## CA-002: path-trace tests passed when the project was missing or failed to load (`f8569444`)

- **What changed:**
  - In `tests/unit/test_pathtrace_glowmere.cpp`, `test_trace_sequence.cpp` and `test_pathtrace_job.cpp`, every `if (!exists) { SUCCEED(...); return; }` is now `testsupport::skipUnlessProjectAssetsPresent(projectPath())`. The SKIP names the first missing file.
  - The "refuses what it cannot deliver" case only needs the project JSON, so it SKIPs on a plain `exists` check.
  - `loadAt` in `test_pathtrace_glowmere.cpp` calls the skip helper first, then `REQUIRE(engine->loadProject(path))`. If the files are present and the load fails, the case now FAILS instead of passing.
  - The `if (engine == nullptr)` early returns are gone, including the one in the hidden preview case.
- **How it was verified:**
  - `build/release/tests/avgen_tests "[pathtrace][glowmere],[pathtrace][sequence],[pathtrace][job]"`: 18 matching cases, all passed with assets present.
  - Both new paths were checked with a temporary edit, which was then reverted: one case pointed at a missing project, another at a malformed project file. The result was `test cases: 3 | 1 passed | 1 failed | 1 skipped`, with the SKIP naming the missing file and the FAIL at `REQUIRE( engine->loadProject(path) )`.
- **CI impact:** on a runner without the gitignored assets, these cases now count as SKIPs instead of passes.

## CA-001: process-wide state raced by the Director's worker-thread Engines (`31b900f7`)

- **What changed:**
  - **Generator registry** (`src/scene/procedural.cpp`):
    - A `std::shared_mutex` now guards every access.
    - `makeSourceMesh` copies the builder out under a shared lock and calls it unlocked.
    - `std::call_once` was not used: tests call `clearGenerators()` and rely on the next `Engine` to re-register. Re-registering per constructor is now safe, and the comment in `Engine::Engine` says why.
  - **Bounds cache** (`generatedMeshForBounds`):
    - A `std::mutex` guards it. Generation runs unlocked and the first insert wins (`try_emplace`).
    - Returned pointers stay valid because `std::map` nodes never move and nothing erases from the cache.
  - **`probe2::frame()`** is now `thread_local`:
    - Every consumer (the application frame loop, `live_profile_run`, and the tests) reads it on the thread that did the work.
    - Worker Engines' seeks land in their own copy, which nothing reads, so the main thread's numbers no longer include worker-thread work.
    - The header comment was updated to match.
  - **`atmospherics.cpp`**: the warn-once set for packer defects is now locked. This was a lower-risk item from the same finding.
  - **New test:** `tests/unit/test_engine_concurrency.cpp`, tagged `[unit][threading][engine]`.
    - The `[threading]` tag puts it in `TSAN_FILTER` in `tools/ci/run-suite.sh`.
    - Two threads each construct three `Engine`s, which re-register the generators, and seek each one.
    - Both threads walk the same 24 mushroom bounds keys from opposite ends, so they insert into the cache together.
    - The resulting boxes must equal the single-threaded reference.
    - Each thread's `probe2::frame().seeks` must equal its own seek count, and the main thread's must stay 0.
- **How it was verified:**
  - Release build: `avgen_tests "[threading][engine]"` passed (1 case, 52 assertions).
  - TSan build (`cmake --preset tsan …`, `cmake --build --preset tsan --target avgen_tests`):
    - `build/tsan/tests/avgen_tests "[threading][engine]"` exited 0 with 0 `WARNING: ThreadSanitizer`.
    - `build/tsan/tests/avgen_tests "[threading]"` (3 cases) exited 0 with 0 warnings.
  - Control: with the pre-fix `src/scene/procedural.cpp` temporarily restored, the same TSan run aborted with exit 134 on a data race in `scene::registerGenerator` / `organism::registerMushroomGenerator`. The fix was then restored and both builds rebuilt. So the test catches the race.

## CA-005: leftover control-replay profiling (`84fc926c`)

- **What changed:** `Engine::replayControl` (`src/app/engine.cpp`) no longer has the `tStart`/`t0..t5` `steady_clock` reads, the `prof[6]` array or the `AVGEN_PROFILE_CONTROL_REPLAY` getenv/log block. `AVGEN_NO_CONTROL_REPLAY` (ADR-1168's A/B arm) is kept.
- **How it was verified:**
  - `avgen_tests "[adr1168]"`: 7 cases, all passed.
  - `avgen_tests "[seek]"`: 83 cases, all passed (exit 0).
  - `grep -rn AVGEN_PROFILE_CONTROL_REPLAY` finds nothing outside the audit docs.

## CA-009: offline motion-database wait discarded its timeout (`eb05d866`)

- **What changed:**
  - In `Composition::motionSlotFor` (`src/scene/composition.cpp`), a `waitIdle(120 s)` that returns false now logs a `log::error`. The message names the database and the pack, and says that motion then depends on load time.
  - The 120 s timeout is now a named constant.
  - Nothing else changed. Refusing the render needs the CA-003 channel, which is an owner decision.
- **How it was verified:**
  - Build exit 0.
  - `avgen_tests "[motionlib]"`: 6 cases, all passed.
  - `avgen_tests "[motion][matching][database]"`: 4 cases, 1 passed and 3 skipped. The 3 SKIPs are pre-existing: the baked database pack is absent on this machine.
  - The 120 s timeout path itself was not exercised.

## Stray and dead files (CA-014, CA-015; docs audit §8 items 1-3) (`58a598ac`)

- **Removed with `git rm`:**
  - `indtune.cpp`
  - the six root `temporal-*.png`
  - `.agent/` (5 files)
- **`tools/_slope_probe.cpp`:**
  - Removed together with its `avgen_slope_probe` target in `tools/CMakeLists.txt`.
  - The file called itself "TEMPORARY … delete it when per-foot ground sampling lands", and that has landed (ADR-551, `composition.cpp`).
  - Its two mentions now say "since removed": a test comment in `test_alien_foot_lab.cpp` and `docs/design/autonomous-character-animation.md`.
- **Capture tests:**
  - The hidden `[.capture]` cases in `tests/rendering/test_temporal_gpu.cpp` now write to `AVGEN_CAPTURE_DIR`, or to `<source>/build/captures` (gitignored) when it is unset. They used to write to the CWD.
  - A temp dir was tried first and rejected. The suite's per-process `TMPDIR` (`tests/support/per_process_tmpdir.cpp`) is deleted at exit, so the captures disappeared.
  - The cases `WARN` the directory they wrote to.
- **`.gitignore`:** gains `/temporal-*.png`.
- **`CMakeLists.txt`:** the orphaned "Transitional (ADR-023)" comment is gone.
- **`docs/engineering-labs.md`:** item 8 is marked as removed.
- **Kept:** `05-static-reference-vs-f027-full.png` (owner's decision).
- **How it was verified:**
  - Reconfigure and full build exit 0, and no target references the removed file.
  - `tools/gpu-lock.sh ./build/release/tests/avgen_render_tests "[.capture][temporal]"`: 2 cases passed. The six PNGs landed in `build/captures/` and none were written to the repo root.
  - `AVGEN_CAPTURE_DIR=<scratch>` was checked under the lock and wrote there.
  - One slip: a single re-run of the debug-view capture case was run without `gpu-lock.sh`, to read the `-s` output. It is one small render, but it was not serialised with the other agents' GPU work.

## CA-010: engine source list hand-copied three times in `tools/CMakeLists.txt` (`330788f4`)

- **What changed:**
  - `AVGEN_TOOL_ENGINE_APP_SOURCES` (22 `src/app` files) and `AVGEN_TOOL_ENGINE_UI_SOURCES` (7 `src/ui` files) are each defined once.
  - A new OBJECT library, `avgen_tool_engine` (links `avgen_core` PUBLIC), compiles them once. Five targets now link it instead of compiling the engine themselves: `avgen_metal_rt_probe`, `avgen_character_quality`, `avgen_cast_trace`, `avgen_foot_probe` and `avgen_song_cut`.
  - `avgen_overlay_shot` keeps its own compile of the app list plus `output_manager.cpp`, because `avgen_platform` already carries the ui files.
  - The three lists were removed, which deleted 110 lines and added 57.
  - `docs/glowmere-valley-3/07-technical.md` named the old `AVGEN_OFFLINE_ENGINE_SOURCES`; it now names `avgen_tool_engine`.
  - A `src/` engine library for `avgen` itself (the audit's fuller suggestion) was not attempted.
- **How it was verified:**
  - Reconfigure and full build exit 0. All five tools relinked.
  - All four offline tools print their usage.
  - `avgen_cast_trace --project examples/world/glowmere-valley-2-multicam.json --seconds 1 --fps 10` loaded the project and produced the full trace JSON.
  - The full CPU suite above ran on this commit.
