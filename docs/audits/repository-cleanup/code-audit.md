# Code audit

The 2026-10-14 cleanup.

- **Full findings,** with evidence and `file:line`: [`data/code-audit-findings.md`](data/code-audit-findings.md),
  taken at `ed9926db`.
- **Fix records:** [`data/code-fixes-applied.md`](data/code-fixes-applied.md) for the C++ fixes;
  [`data/docs-fixes-applied.md`](data/docs-fixes-applied.md) for the docs.
- **CI and infrastructure findings:** in [`ci-and-test-report.md`](ci-and-test-report.md).

The audit treated AV Gen as a production application. It traced actual use before calling anything dead, and did
not rewrite working systems.

## Findings by severity, and what happened to each

| ID | Severity | Finding | Outcome |
|---|---|---|---|
| CA-001 | High | The Director's worker-thread Engines (stills, evaluate, record) race the main Engine. Every Engine constructor rewrote the unsynchronised static generator registry; the `generatedMeshForBounds` cache and `probe2::frame()` were shared without locks. This is undefined behaviour, and a crash is possible while a Director job runs. | **Fixed** (`31b900f7`). Registry under a `shared_mutex`, bounds cache under a mutex, `probe2::frame()` `thread_local`, plus the atmospherics warn-once set. New test `tests/unit/test_engine_concurrency.cpp` (`[threading]`, so it is in the TSan set). Under TSan it is clean (0 warnings). With the old `procedural.cpp` restored, TSan aborts on the race, so the test catches it. |
| CA-002 | High | 15 path-trace cases `SUCCEED` and return when the project is missing, or fails to load. A regression that stops the project loading shows up as passes. | **Fixed** (`f8569444`). They SKIP naming the missing file, and FAIL on a load failure. Both paths were checked with a temporary probe. |
| CA-003 | High | An offline render ships fallback geometry or an unlit sky when a mesh, texture or environment map fails to load, and exits 0. ADR-1288 covers only the scene file. | **Owner decision.** Making renders refuse is a policy change, and some scenes deliberately tolerate optional assets. |
| CA-004 | Medium | About 8 `AVGEN_*` environment variables change rendered output, are recorded nowhere, and 6 are undocumented. | **Open** (follow-up: log them in the render manifest). |
| CA-005 | Medium | Leftover Digital Mosh profiling: six clock reads per replayed frame behind `AVGEN_PROFILE_CONTROL_REPLAY`. | **Fixed** (`84fc926c`). `[adr1168]` and `[seek]` pass. |
| CA-006 | Medium | `phase2_probe.hpp` is marked "TEMPORARY", but the live profiler now depends on it. | **Owner decision:** promote it to a supported instrument, or remove both. |
| CA-007 | Medium | `QualityPolicy` (ADR-134) has no production user, so the "offline is uncompromised" assertion checks an object no render uses. | **Open.** It needs an ADR-level decision: wire it in, or retire it and the claim. |
| CA-008 | Medium | Per-frame heap work in `SceneRenderer::render` (draw lists rebuilt, LOD rung vectors copied per entity, a string-keyed `prevModels` map). | **Open.** It needs profiling before and after (ADR-170 lock). Not a correctness bug. |
| CA-009 | Medium | The offline motion-database wait discarded its 120 s timeout. | **Fixed** (`eb05d866`). It logs an error naming the database and pack. Failing the render waits on CA-003. |
| CA-010 | Medium | The engine source list was hand-copied three times in `tools/CMakeLists.txt` (already drifted), and `engine.cpp` was compiled into 7 targets. | **Fixed** (`330788f4`). One list, and one object library (`avgen_tool_engine`) for five tools. |
| CA-011 | Medium | Wall-clock budget assertions in the default suite, which also runs under ASan. Two have flaked on hosted CI (`ci-and-test-report.md`). | **Open** (follow-up: tag them `[performance]` and gate them on a release build or a quiet machine). |
| CA-012 | Low | The sonic score generator on CI. | **Fixed** (numpy venv, one run per command: `ed9926db`, `22a56ce7`). |
| CA-013 | Low | `tools/_slope_probe.cpp` is temporary, its deletion condition is met, and it has no argument check. | **Removed**, with its target (`58a598ac`). |
| CA-014 | Low | Stray root files: `indtune.cpp` (0 B), six `temporal-*.png` written to the current directory by a capture test, and `.agent/` scratch. | **Removed.** The capture cases write to `$AVGEN_CAPTURE_DIR` or `build/captures`; `/temporal-*.png` is ignored (`58a598ac`). `05-static-reference-vs-f027-full.png` (3.7 MB, from the owner's own "ref" commit) is **left for the owner**. |
| CA-015 | Low | An orphaned "Transitional (ADR-023)" CMake comment. | **Removed.** |
| CA-016 | Low | The pre-ADR-233 "legacy" double procedural generation still ships as an A/B arm. | **Owner decision** (ADR-441 says no shims; the arm may still be wanted for comparisons). |
| CA-017 | Low | An unchecked write of `materials.json` on the UNSTABLE path. | **Open** (small). |
| CA-018 | Low | Ad-hoc hash mixers in about 32 files, with two different FNV offset constants. | **Open.** Consolidating them would change hashes that fingerprints and determinism tests depend on, so it needs a deliberate migration. |
| CA-019 | Low | Name aliases in parsers and old project migrations (compat-shim policy, ADR-441). | **Owner decision:** which old projects must still open. |
| CA-020 | Low | A duplicated comment line in `seq::Shot`. | **Open** (trivial). |

### Fixed outside the code audit's list (found during this sprint)

- **Non-hermetic build.** fastgltf linked Homebrew's simdjson whenever one was installed: here, and on the CI build
  image from 2026-10-14. The test VMs then could not load the binary. Fixed (`CMAKE_DISABLE_FIND_PACKAGE_simdjson`),
  and the CI build job now refuses any non-system dylib.
- **Duplicate implementations removed:**
  - `test_rebuild_analysis.cpp`'s private copy of the Rebuild loader, now `tests/support/rebuild_track.hpp`;
  - the project-path resolution the examples menu needed, lifted out of `engine.cpp` as `app/project_paths.hpp`
    rather than duplicated.
- **Misleading tests strengthened:**
  - the index test now requires each project's scene to load;
  - the explicit example lists in the procedural tests REQUIRE their files;
  - the seek-parity audio cases REQUIRE the track.
- **`tools/ci/run-suite.sh`:** a relative `--out` gave the shards a relative `TMPDIR`, and about 10 tests failed.

## Dead code and duplicates

- **Removed:**
  - `indtune.cpp`, `tools/_slope_probe.cpp` (and its target), `.agent/`, the six root PNGs;
  - the `AVGEN_PROFILE_CONTROL_REPLAY` block;
  - the duplicated Rebuild loader;
  - the triplicated tool source lists.
- **Not removed, for the owner:** six `src/` modules have no product caller, only tests or tools:
  `app/profile_capture`, `ai/inference`, `scene/tree_audio`, `scene/motion_variants`, `scene/motion_search_index` and
  `entity/motion_evaluation`. About 45 header functions are unused. Several other test-only modules are deliberate
  oracles or staged-dark by ADR-615 (findings, CA-021). The brief says not to remove a feature merely because the
  examples do not exercise it, so each needs a yes or no.

## Scene-specific code in shared engine code

There are no `if (scene == "...")` hacks in `src/`. The coupling is structural and mostly sanctioned:

- **The Astral renderer.** Astral, Ecosystem and Clouds are typed fields on `Scene`/`Composition`, and `astralTier`
  sits in the shared quality ladder (ADR-1221/1222, accepted). These are *capabilities* that one scene uses today.
- **"Sonic Live" by name** in `src/app/live_scenes.cpp:33` and `src/app/application.cpp:2068`. The Sonic VFX and
  Sonic Abstract category names are in `live_scenes.hpp`.
- **Glowmere defaults.** A terrain node without `"world"` silently becomes Glowmere (`world_map.cpp:1009`); the
  mushroom generator always uses the `glowmere` art profile (`mushroom.cpp:458`); `art_direction.cpp:16-18` holds
  values copied from `glowmere-stylized.scene.json`.
- **Lab fixtures** hard-code example paths (`src/labs/lab.cpp:45-165`).

None is a correctness defect. They are the main things that make scenes D-class to remove
(`scene-removal-impact.md`). Follow-up: a named default world and art profile instead of "glowmere", and a
`"leads": true` index flag instead of the "Sonic Live" string.

## Retained technical debt (intentional)

These are listed so nobody "fixes" them by accident. Each cites the ADR that accepted it; details are in the
findings, under "Retained debt":

- `seq::Shot` and `scene::CameraShot` stay separate (two camera architectures).
- The `[!shouldfail]` slope case.
- The large files: `composition.cpp` is 13,274 lines.
- The two prototypes behind CMake options, absent from CI (CA-024). `astral-forge` builds from production files, so a
  production change can break it unseen. Follow-up: a configure-only CI check.

## Follow-up work, prioritised

1. **CA-003:** decide whether a render may ship fallback content. Then CA-009's timeout can fail the render too.
2. **CA-011:** move wall-clock assertions out of the default and sanitizer sets.
3. **CA-004:** record output-changing environment variables in the render manifest.
4. **Dead modules and CA-016/CA-019:** owner yes/no, then delete.
5. **CA-007/CA-008:** wire `QualityPolicy` in or retire it; profile per-frame allocations.
