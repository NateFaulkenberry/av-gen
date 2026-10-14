# Code audit findings (production code, src/ + build + tests)

- Date: 2026-10-14. Worktree: `av-gen-cleanup`, branch `cleanup/repo-consolidation`.
- Audited at `7b83fd58`. HEAD moved to `ed9926db` during the audit. That commit touches CI, tests and examples only; `git diff --stat 7b83fd58 HEAD -- src` is empty, so every `src/` line reference below is valid at HEAD.
- Method: static reading plus scripted analysis (include graph, symbol references, `getenv`, `catch`, `(void)`, statics, threads, test tags). Nothing was built or run.
- Scope: findings only. Scene-file dependencies are covered by the separate scene audit and are not repeated here.
- Confidence levels:
  - *confirmed*: read in the code, and the failure follows from the code as written.
  - *likely*: the code path exists, but whether it happens at runtime was not observed.
  - *suspect*: plausible, but not proven.

---

## Critical

None. No defect was found that corrupts saved projects or always breaks a shipped path.

---

## High

### CA-001: process-wide mutable state raced by the background scratch Engines
- **Where:**
  - `src/scene/procedural.cpp:715-733`, `generatorRegistry()`: an unsynchronised `static std::map`. `registerGenerator` writes it.
  - `src/app/engine.cpp:562-563`: every `Engine` constructor calls `registerMushroomGenerator()` and `registerTreeGenerator()`.
  - `src/scene/procedural.cpp:553`, `generatedMeshForBounds`: an unsynchronised `static std::map<std::string, std::optional<MeshData>> cache`. It is read and inserted into during procedural generation.
  - `src/core/phase2_probe.hpp:79`, `probe2::frame()`: a static counter block. Its header says "main-thread only". It is written from `Engine::seekSeconds` (`engine.cpp:4437`) and from `EntityWorld` seek (`entity.cpp:1752, 1888`).
  - `src/world/atmospherics.cpp:796`: `static std::set<EffectKind> warned`. Lower risk, because it is only reached on a packer defect.
- **Evidence:** several full `Engine` instances run on worker threads while the editor's own `Engine` keeps ticking on the main thread:
  - `src/app/director_stills.cpp:258` (`worker_ = std::thread([this]{ work(); })`) and `:271` (`std::make_unique<Engine>(EngineMode::Offline)`, `loadProject`, `seekSeconds`).
  - `src/app/directing_evaluate.cpp:587`, which leads to `loadForEvaluation` (`:138`, `make_unique<Engine>`).
  - `src/app/directing_record.cpp:399/578`, which leads to `:38`.

  Each worker Engine constructor rewrites the `std::function`s in `generatorRegistry()` while the main thread may be looking one up (`procedural.cpp:1719`). Both Engines insert into the bounds cache `std::map` during procedural generation. A concurrent insert and find on a `std::map` can corrupt the tree or crash.

  Contrast `tree_generated.cpp:22-62` and `text_mesh.cpp:236`: their caches *do* carry a mutex, so a neighbouring author already anticipated cross-thread use.
- **Why it matters:** this is undefined behaviour in the editor. It shows up as intermittent crashes or wrong bounds when a Director stills, evaluate or record job runs while the user edits or scrubs. TSan would flag it, but no test runs two Engines concurrently, so the nightly TSan set cannot see it.
- **Fix (minimal):**
  1. Register generators once, under `std::call_once`, from the Engine constructor (or a static initialiser).
  2. Guard `generatorRegistry()` reads with a `std::shared_mutex`, or freeze it after the first registration.
  3. Put a `std::mutex` around the `generatedMeshForBounds` cache (or make it `thread_local`). The pointer it returns must stay valid; `std::map` node stability already guarantees that, and a lock does not change it.
  4. Make `probe2::frame()` `thread_local`, or no-op it off the main thread.
  5. Add one TSan case that constructs an `Engine` on a `std::thread` while the main thread loads a procedural scene.
- **Confidence:** confirmed (code); likely (a runtime race in practice).
- **Safe to auto-fix?** Partly. The `call_once` and mutex changes are mechanical. The `probe2` change touches live-profile numbers (see CA-006).

### CA-002: path-trace tests report a pass when the project is missing or fails to load
- **Where:**
  - `tests/unit/test_pathtrace_glowmere.cpp:41-45, 65-68, 100, 149`
  - `tests/unit/test_trace_sequence.cpp:61, 92, 120, 142, 164, 260`
  - `tests/unit/test_pathtrace_job.cpp:87, 142, 168, 185, 233, 269`
- **Evidence:**
  ```cpp
  if (!std::filesystem::exists(path)) return nullptr;
  auto engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
  if (!engine->loadProject(path)) return nullptr;      // a load FAILURE ...
  ...
  if (engine == nullptr) { SUCCEED("... or its assets are not in this worktree"); return; }  // ... is a PASS
  ```
- **Why it matters:**
  - `SUCCEED`+`return` counts as a pass. The project convention is `SKIP()` when a precondition is absent (docs/testing.md:1286), so the SKIP count in the verdict line is the signal.
  - In `test_pathtrace_glowmere.cpp` it is worse: a regression that makes `glowmere-valley-2-multicam.json` fail to load turns three path-tracer cases green.
  - On CI the commercial assets are absent, so these 15 cases are permanently "passing" there.
- **Fix:** replace each `SUCCEED(...); return;` with `SKIP(...)`. In `loadAt`, `REQUIRE` the load whenever the file and its assets exist; `testsupport::skipUnlessProjectAssetsPresent` (used in ed9926db for `test_engine_seek_parity`) is the existing helper.
- **Confidence:** confirmed.
- **Safe to auto-fix?** Yes. It is a test-only change. Pass counts drop and SKIP counts rise on CI, which is the point.

### CA-003: an offline render ships fallback geometry when a mesh asset or environment map fails to load
- **Where:** `src/scene/composition.cpp:6505-6509` (mesh assets), `:7361` (base colour texture), `:9366-9369` (environment map).
- **Evidence:**
  ```cpp
  auto loaded = registry_.loadScene(assetPath);
  if (!loaded) {
      log::warn("'{}': mesh asset '{}': {}", owner, assetPath, loaded.error().message);
      return {};   // "a warning and not an error: the object keeps whatever source it had"
  }
  ```
  Nothing counts these failures: there is no `missingAssets` or `assetFailures` anywhere in `src/`. `RenderJob` therefore cannot tell. ADR-1288 refuses a render only when the *scene file* failed to load.
- **Why it matters:** `avgen --render` of a project whose hero GLB is missing, corrupt or unlinked exits 0. It produces a deliverable with primitives or no sky, and one warning line is buried in the log. This is the "log-and-continue on load failure" class, on the path that produces the output anyone ships. (The Tree of Life GLB export incident in memory is this shape.)
- **Fix:** count asset-load failures in `Composition` and expose them (for example `assetLoadFailures()`). In `RenderJob::start` for `EngineMode::Offline`, fail with the list unless `--allow-missing-assets` is passed. The live editor keeps today's behaviour.
- **Confidence:** confirmed.
- **Safe to auto-fix?** No. It changes CLI behaviour, and every CI render of an asset-less project would need the flag. Owner decision.

---

## Medium

### CA-004: output-changing `AVGEN_*` environment variables are read in deterministic code and recorded nowhere
- **Where:** see the env-var table below. The ones that change a rendered frame or simulation:
  - `AVGEN_SEEK_MODE` (`entity/entity.cpp:1428`)
  - `AVGEN_LEVEL_BLEND` and `AVGEN_PATH_CUTOFF` (`world/world_map.cpp:241, 144`). These change terrain heights.
  - `AVGEN_NAV_FORCE_TRUST` (`entity/nav_grid.cpp:840`)
  - `AVGEN_DIAG_NO_SIGHTLINE` (`scene/composition.cpp:2402`)
  - `AVGEN_NO_WARMUP` (`app/render_job.cpp:163`)
  - `AVGEN_LEGACY_PROCGEN` (`app/engine.cpp:678`)
  - `AVGEN_NO_CONTROL_REPLAY` (`app/engine.cpp:4586`)
  - `AVGEN_HEIGHT_CACHE` and `AVGEN_NO_TERRAIN_CACHE`. These are meant to be output-neutral.
- **Evidence:** each is read once into a function-local `static`. No code enumerates `AVGEN_*` for the render log or manifest: the only `environ` users are the spawn calls. Six of these variables (`AVGEN_NO_SEEK_DEFERRAL`, `AVGEN_SEEK_BODY_STEPS`, `AVGEN_NO_CONTROL_REPLAY`, `AVGEN_PROFILE_CONTROL_REPLAY`, `AVGEN_TRACE_DEBUG`, `AVGEN_ASTRAL_TRACE`) are not mentioned in any doc.
- **Why it matters:** a variable left exported in a shell or agent environment silently produces a different deliverable or test result, and nothing in the output says so. The comments themselves say "It must never be set for anything anybody keeps" (`nav_grid.cpp:834`).
- **Fix:**
  - Add one `core/diag_env.{hpp,cpp}` table: name, whether it changes output, and a doc line.
  - Every read goes through it.
  - At startup, log a WARN listing every set output-changing variable, and write them into the render manifest. `RenderJob` refuses to render while one is set unless `--diag` is passed.
- **Confidence:** confirmed.
- **Safe to auto-fix?** The logging half: yes. The refusal half: no (owner).

### CA-005: leftover control-replay profiling in `Engine::replayControl` (Digital Mosh)
- **Where:** `src/app/engine.cpp:4586` (`AVGEN_NO_CONTROL_REPLAY`), `:4620-4711` (`prof[6]` plus `t0..t5`), `:4706` (`AVGEN_PROFILE_CONTROL_REPLAY`).
- **Evidence:**
  - Six `steady_clock::now()` calls run per replayed frame, unconditionally, and fill `std::array<double,6> prof`.
  - That array is read only when `AVGEN_PROFILE_CONTROL_REPLAY` is set.
  - That `getenv` is not cached and runs on every replay.
  - Neither variable appears in docs/, tests/ or tools/.
  - Memory already lists this as an open item.
- **Why it matters:** a 90 s seek at 60 fps is about 5,400 iterations, so about 32k clock reads per seek. That is small but pure overhead, and the variables are undocumented debug switches in a hot seek path.
- **Fix:** delete the `prof`, `t0..t5` and `AVGEN_PROFILE_CONTROL_REPLAY` block. Keep `AVGEN_NO_CONTROL_REPLAY` only if ADR-1168's A/B is still wanted, register it in the CA-004 table, and document it.
- **Confidence:** confirmed.
- **Safe to auto-fix?** Yes for the profiling block. It is diagnostics-only and no control flow reads it.

### CA-006: the "TEMPORARY" `probe2` instrumentation has become load-bearing
- **Where:** `src/core/phase2_probe.hpp:1-7` ("TEMPORARY INSTRUMENTATION … Delete this header and every `probe2::` reference when the investigation closes"). It has 42 references, in `application.cpp`, `engine.cpp`, `entity.cpp` and `live_profile_run.cpp:62-73`.
- **Evidence:**
  - `docs/investigations/ui-responsiveness.md:3` says "Status: Complete".
  - `live_profile_run.cpp::fillFromProbe` now copies `probe2` fields into `LiveProfileFrame`, so the live profiler depends on the temporary header.
  - The "main thread only" claim is false (CA-001).
- **Why it matters:** the header contradicts its own role. If someone follows its instruction to delete it, the live profiler breaks.
- **Fix:** promote it to a permanent, per-Engine `FrameCounters` owned by `Engine` (passed to `EntityWorld`), drop the "TEMPORARY" banner, and make off-main-thread writes impossible (per-Engine instance).
- **Confidence:** confirmed.
- **Safe to auto-fix?** No. It is a small refactor across three libraries.

### CA-007: `QualityPolicy` (ADR-134) is a test-only aggregate; the renderer composes the tables itself
- **Where:**
  - `src/rendering/quality_policy.hpp:5-14`: "The one place editor/realtime/high/offline differ … every consumer takes the object rather than the enum."
  - No file in `src/` includes it. Only `tests/unit/test_{interactive_resolution,live_optimizer,material_tier,astral_forge}.cpp` do.
  - `SceneRenderer::setQuality` (`scene_renderer.cpp:1144-1149`) uses `QualitySettings::forTier(tier)` directly, so the reconciliation in `QualityPolicy::forTier` (`quality_policy.hpp:63-70`) never runs in production.
- **Evidence:** ADR-134 §Consequences: "Nothing is forced to route through it yet … the value that remains is one caller at a time." About 1,100 ADRs later, no caller has been converted.
- **Why it matters:** `assertOfflineIsUncompromised()` is cited in `render_quality.hpp:325`, `interactive_resolution.hpp:55/88` and others as *the* guarantee for offline renders. It actually asserts a property of an object no render uses. If `QualitySettings::forTier` and `MaterialTierPolicy::forTier` ever disagree, the test stays green and the deliverable differs.
- **Fix (minimal):**
  - Make `SceneRenderer::setQuality` use `QualityPolicy::forTier(tier).settings`. First verify, with `--quality-arms` and the sequence hash, that it is byte-identical for all four tiers today (ADR-182).
  - Or add a GPU or `RenderJob` test that asserts the *renderer's* `qualitySettings()` after `RenderJob::start` equals `QualityPolicy::forTier(Offline).settings`.
  - Fix the header comment either way.
- **Confidence:** confirmed.
- **Safe to auto-fix?** Not blind. It needs one hash comparison first.

### CA-008: per-frame and per-entity heap work in `SceneRenderer::render`
- **Where:** `src/rendering/scene_renderer.cpp`:
  - `:3386-3389, 3417, 3422`: five local `std::vector`s (`opaque`, `grid`, `blended`, `water`, `shadowCasters`) and `cascadePlanes`, built every frame.
  - `:3393-3396`: `prevModelsNext_` is a `std::unordered_map<std::string, glm::mat4>`, `clear()`ed and refilled with one node allocation and one string copy per entity per frame.
  - `:3528`: `std::vector<LodRung> rungs = chain.rungs;` is a heap copy *per entity per frame* ("Rebuilt per entity because two instances … at different scales cost differently").
- **Why it matters:** these allocations scale with entity count on the hot path, and the engine already tracks per-frame allocation counts (ADR around `stats_.allocs*`). Separately, keying motion history by `entity.name` collapses two entities with the same name onto one previous transform, which would give a wrong motion vector. That part is suspect: name uniqueness was not verified.
- **Fix:**
  - Make the draw lists members and `clear()` them, keeping capacity.
  - Scale the rungs into a member scratch vector (or compute the scaled error inline without copying).
  - Key `prevModels` by a stable entity index or id, with two `std::vector<glm::mat4>` swapped per frame.
- **Confidence:** likely (cost); suspect (name collision).
- **Safe to auto-fix?** Draw lists and rungs: yes, mechanically, with a frame-hash check. The `prevModels` re-key: no.

### CA-009: an offline motion-database load ignores its 120 s timeout
- **Where:** `src/scene/composition.cpp:2170-2172`.
- **Evidence:**
  ```cpp
  if (blockingMotionLoads_) {
      (void)it->second->waitIdle(std::chrono::seconds(120));
  }
  ```
  `composition.hpp:2043-2046` promises: "wait for every requested database … so its frames never depend on how long a load took". `waitIdle` returns false on timeout, and the result is discarded.
- **Why it matters:** an offline render whose database takes over 120 s (a cold disk, or a large pack under ASan) silently proceeds with clip fallback. The deliverable then depends on load time, which is exactly what the flag exists to prevent.
- **Fix:** if `!waitIdle(...)`, `log::error` and record a failure that `RenderJob` refuses on (same channel as CA-003), or wait without a bound in Offline mode.
- **Confidence:** confirmed.
- **Safe to auto-fix?** Yes for the log. The refusal needs the CA-003 channel.

### CA-010: the engine source list is copied three times in `tools/CMakeLists.txt`, and `engine.cpp` is compiled into six extra targets
- **Where:** `tools/CMakeLists.txt`:
  - the `avgen_overlay_shot` list (22 app sources);
  - the `avgen_metal_rt_probe` list (29);
  - `AVGEN_OFFLINE_ENGINE_SOURCES` (29), used by `avgen_character_quality`, `avgen_cast_trace`, `avgen_foot_probe` and `avgen_song_cut`.
- **Evidence:**
  - The three lists have already drifted: overlay_shot carries `output_manager.cpp` and none of the `ui/*` files.
  - `app/profile_capture.cpp` is compiled into every one of them, although no file in `src/` includes `profile_capture.hpp` (see dead code).
  - `src/app/engine.cpp` (6,801 lines) plus `world_builder`, `camera_director` and the rest are recompiled for each of the seven targets.
- **Why it matters:** build time, and silent drift. The next app source that `engine.cpp` needs must be added in three places, or one tool stops linking.
- **Fix:** add `add_library(avgen_engine STATIC <the list>)` once in `src/CMakeLists.txt`, link `avgen` and the seven tools against it, and delete the three lists.
- **Confidence:** confirmed.
- **Safe to auto-fix?** Yes. It is a build-only change, verified by the existing tool targets linking.

### CA-011: wall-clock budget assertions in the default suite, which also runs in ASan "rest" shards
- **Where:**
  - `tests/unit/test_entity_action.cpp:1150-1160`: a single run, `CHECK(msPerFrame < 1.0)` for 500 entities, with no best-of.
  - `tests/unit/test_directing_golden.cpp:201-202`: `< 250 ms` per run.
  - `tests/unit/test_entity_system.cpp:523`: `< 1600 µs`, best of `kRuns`, so more robust.
- **Evidence:** these are tagged `[performance]`/`[benchmark]`, not hidden. `tools/ci/sanitizer-plan.txt:25` runs `~[.]` (everything not hidden) under Debug+ASan on 3-vCPU runners.
- **Why it matters:** these are flake candidates under load or sanitizers. That is a known "green suite lies" vector in reverse: a red build that is not a regression.
- **Fix:** use a best-of-N minimum (as `test_entity_system` does), and skip the wall-clock CHECK under sanitizers (`#if defined(__has_feature) && __has_feature(address_sanitizer)`), or move them to `[.perf]`.
- **Confidence:** suspect (no flaky run observed in this audit).
- **Safe to auto-fix?** Yes, test-only.

---

## Low

### CA-012: Sonic Garden score generation on CI (root cause and residuals)
- **Diagnosis (from code, at `7b83fd58`):**
  - `tools/make_sonic_material.py:26` does `import numpy as np`. The other score generators are stdlib-only, and the hosted runner's `python3` has no numpy, so the generator exits non-zero. `generate-audio.sh` prints "sonic-pad: the generator failed (log below); its cases will SKIP".
  - Secondary defect: all seven manifest entries share one command, and the old script launched it seven times concurrently, each copy writing all seven WAVs.
- **Status:** **fixed in `ed9926db`** (landed during this audit). It adds a numpy==2.0.2 venv via `.github/actions/test-inputs` and `tools/ci/requirements.txt`, and runs each distinct command once. Verified by reading the new `generate-audio.sh`.
- **Residuals:**
  1. When only some `sonic-*.wav` files are missing, the single run rewrites *all seven*, including ones "present, left alone". It also rewrites the tracked `examples/sonic-garden/notes/*.mid` every time (`make_sonic_material.py:318-320`). If bytes ever differ, the CI checkout is dirtied.
  2. Generator failure stays a warning plus SKIP (`exit 0`). That is by design, but nothing fails CI if numpy disappears again; consider failing when `requirements.txt` is installed but a hash mismatches.
  3. numpy's SIMD `tanh`/`sin` may differ across CPU families, which is a hash-stability risk if a runner changes architecture.
- **Confidence:** confirmed (root cause matches the commit message).
- **Safe to auto-fix?** Residual 1 (write notes to a temp dir unless asked): yes.

### CA-013: `tools/_slope_probe.cpp` is marked TEMPORARY, its deletion condition appears met, and it has no argument check
- **Evidence:**
  - The file header says: "TEMPORARY … Delete it when per-foot ground sampling lands."
  - Per-foot planes now exist: `docs/design/autonomous-character-animation.md:1669` (`footContacts` per foot: the sampled plane) and `tests/unit/test_alien_foot_lab.cpp:291`.
  - `main` reads `argv[1..3]` with no `argc` check (UB when run with no arguments).
  - It is built by default (`tools/CMakeLists.txt`, `avgen_slope_probe`).
- **Fix:** delete the file and its two CMake lines. It is cited once, historically, in the design doc (`:2091`); keep that citation as history.
- **Confidence:** likely (that the condition is met).
- **Safe to auto-fix?** Yes, after the owner confirms per-foot sampling is the thing meant.

### CA-014: stray tracked files at the repository root
- **What is there:**
  - `indtune.cpp`: 0 bytes. No reference except `docs/engineering-labs.md:514` and `docs/prototypes/sonic-garden/PROGRESS.md:510`, both of which already call it stray.
  - `temporal-{plain,echo-3,echo-10,echo-cold,ring-full,ring-settling}.png`: outputs of the hidden `[.capture]` cases in `tests/rendering/test_temporal_gpu.cpp:274, 396`. They write to `AVGEN_CAPTURE_DIR` or, if unset, `"."`, i.e. the cwd (the repo root when run from there). Committed in e841d1e0.
  - `05-static-reference-vs-f027-full.png` (3.7 MB): added in the owner's own latest commit `7b83fd58 "ref"`. Nothing references it.
- **Fix:**
  - `git rm indtune.cpp temporal-*.png`.
  - Change the capture default from `"."` to `std::filesystem::temp_directory_path()/"avgen-capture"`.
  - **Ask the owner** about `05-static-reference…png`. It looks deliberate (a reference image), so it should move under `docs/` or `~/Desktop/av-gen-review/` rather than be deleted.
  - Add `/*.png` to `.gitignore` to stop recurrence.
- **Confidence:** confirmed.
- **Safe to auto-fix?** `indtune.cpp` and `temporal-*.png`: yes. `05-…png`: no.

### CA-015: orphaned CMake comment for a removed option
- **Where:** `CMakeLists.txt:37-38`, "Transitional (ADR-023): link the test-only scene::makeSourceMesh stub from tests/support while the CPU side … lives on another branch. Off once both branches are merged." No option follows it, and `makeSourceMesh` is real code in `src/scene/procedural.cpp:1701`.
- **Fix:** delete the two comment lines.
- **Confidence:** confirmed.
- **Safe to auto-fix?** Yes.

### CA-016: the pre-ADR-233 "legacy" double procedural generation is still shipped
- **Where:** `src/app/engine.cpp:678-684` (`AVGEN_LEGACY_PROCGEN`), `src/app/application.cpp:113-129, 260, 853, 4737` (the `+legacy` bench arm), and `src/scene/composition.cpp:8585, 8604` (`legacyProceduralGeneration_`).
- **Why it matters:** this is a compatibility arm kept only for an A/B that ADR-233 settled. Under the ADR-441 no-shims spirit, it is a candidate for removal once nobody needs the byte comparison.
- **Fix:** delete the flag, the arm token and the two branches in `Composition`, after confirming with the owner that no benchmark still uses `+legacy`.
- **Confidence:** confirmed.
- **Safe to auto-fix?** No (owner).

### CA-017: an unchecked write of `materials.json` on the UNSTABLE path
- **Where:** `src/app/render_job.cpp:976-977`: `std::ofstream file(aovDir_ / "materials.json"); file << unstable.dump(2) << "\n";`. The stream state is never checked.
- **Why it matters:** this is the path that exists to *warn* that a manifest is invalid. If the write fails, the old "stable" file stays beside the frames.
- **Fix:** check `file` after writing and call `fail(...)` on error.
- **Confidence:** confirmed.
- **Safe to auto-fix?** Yes.

### CA-018: ad-hoc hash mixers duplicated across roughly 32 files, with two different FNV offset constants
- **Evidence:**
  - 32 files in `src/` hand-roll FNV-1a. `1469598103934665603ull` (19 digits) is used in `gpu/readback.cpp:161,171`, `scene_renderer.cpp:56,147,2966`, `cloud_renderer.cpp:151`, `ecosystem_renderer.cpp:292`, `entity/field.cpp:260`, `ai/inference.cpp:26` and others.
  - The true FNV-64 offset basis, `14695981039346656037ull` / `0xcbf29ce484222325`, is used in `frame_range.hpp:163`, `editor_layout.cpp:198` and `route_row_logic.hpp:58`.
  - 36 files embed the golden-ratio constant in their own `mix`/`hash32` helpers (`entity/decision.cpp:124`, `world/effects/transform_frame.cpp:41`, `scene/generator.cpp:16`, …).
  - `scene/struct_hash.hpp:61` (`mixHash`) and `core/noise.hpp` (`hashIndex`, already the single source for `procedural.cpp:2232` and `:2525`) exist as shared utilities.
- **Why it matters:** low. All of these are internal identities, so the truncated constant is harmless as a seed, but anyone checking the image or sequence hash against a reference FNV gets a different number.
- **Fix:** no churn now. When one of these files is touched, use `scene/struct_hash.hpp`. Do not "correct" the 19-digit constant: it would change every recorded sequence hash (ADR-113 bench records).
- **Confidence:** confirmed.
- **Safe to auto-fix?** No. It would change hashes.

### CA-019: name aliases in parsers (compat-shim policy)
- **Where:**
  - `src/entity/action.cpp:267-269`: `move|walkTo|goTo`, `face|lookAt`, `pose|play`.
  - `src/entity/field.cpp:67`: `inverseSquare|inversesquare`.
  - `src/app/engine.cpp:551`: `midi|midiClock|midi-clock`.
  - `src/params/serialization.cpp:501-521`: project migrations 1→2→3→4, while all 224 example projects are already `"version": 4`.
- **Why it matters:** ADR-441 says no aliases during heavy development. Some of these may be deliberate authoring synonyms (`walkTo`), so this is a policy question, not a defect.
- **Fix:** have the owner decide. If they go, strip the aliases from `examples/` first (the scene audit can list users).
- **Confidence:** confirmed.
- **Safe to auto-fix?** No.

### CA-020: duplicated comment line in `seq::Shot`
- **Where:** `src/seq/sequence.hpp:323-326`. "A span of the piece with one visual setup." appears twice: an older paragraph and a newer header were both kept.
- **Fix:** drop the first paragraph.
- **Safe to auto-fix?** Yes.

---

## Info

- **CA-021, test-only modules in `src/`.** 16 headers are included by no `src/` file, only by tests or tools (table below). Several are deliberate test oracles: `reference_renderer`, `renderer_snapshot`, `*_conformance`, `march_schedule` (the CPU transliteration of a WGSL schedule), and `visibility_reason`. Others are staged-and-dark by ADR-615: `root_motion_adapt` and `trajectory`. The ones with no stated reason are listed in the dead-code table.
- **CA-022, hidden test cases.** 157 hidden cases (`[.probe]`, `[.capture]`, `[.perf]`, `[.analysis]`, `[.tree-probe]`, `[.abduction-instrument]`, …) are compiled but never run by any CI job. They are instruments by design, but none is checked for still-running, so they rot silently. Consider a weekly `--list-tests "[.]"` plus a smoke run of a few.
- **CA-023, the one expected failure.** The only `[!shouldfail]` is `tests/unit/test_character_lab_slopes.cpp:118`. It is documented as a correct invariant the engine does not yet satisfy (memory: "shouldfail is not a failure").
- **CA-024, prototypes not built in CI.** `prototypes/gpu-world` and `prototypes/astral-forge` sit behind `AVGEN_GPUWORLD_PROTOTYPE` and `AVGEN_ASTRAL_FORGE_PROTOTYPE`, both OFF and absent from `.github/` and `tools/ci`. `astral-forge` now builds *from production files* (ADR-1221), so a production change can break it unseen.
- **CA-025, very large files.** `scene/composition.cpp` (13,274 lines), `app/application.cpp` (7,400), `app/engine.cpp` (6,801), `rendering/scene_renderer.cpp` (5,263) and `ui/control_panel.cpp` (5,252). Not a defect; noted as the main reason per-frame costs (CA-008) are hard to see.
- **Not found (clean):**
  - no `#if 0` blocks;
  - no commented-out code blocks (only doc examples);
  - zero TODO, FIXME, HACK or XXX in `src/`;
  - no `AVGEN_NEGATIVE_CONTROL` remnant in `src/`, `tests/`, `tools/`, `shaders/` or `cmake/`;
  - no raw `new`/`delete` outside `unique_ptr` construction with private constructors, plus one documented intentional leak (`control/midi_coremidi.cpp:283`);
  - no direct `wgpu*Release` (all C++ wrappers);
  - no per-frame pipeline creation (every `createRenderPipeline` is in init or a keyed `pipelineFor` cache);
  - all five `catch (...)` blocks convert to an error, log, or return;
  - all third-party dependencies in `cmake/Dependencies.cmake` are used (implot by three UI files, embree by pathtrace, kissfft by fft, meshoptimizer by LOD).

---

## Environment variables read by `src/`

| Variable | Read at | Changes output? | Documented? | Note |
|---|---|---|---|---|
| AVGEN_SEEK_MODE | entity/entity.cpp:1428 | **yes** (seek algorithm) | 1 doc | CA-004 |
| AVGEN_LEVEL_BLEND | world/world_map.cpp:241 | **yes** (terrain heights) | 2 | CA-004 |
| AVGEN_PATH_CUTOFF | world/world_map.cpp:144 | intended no; it is a fast path | 2 | |
| AVGEN_HEIGHT_CACHE | world/world_map.cpp:586 | intended no | 2 | |
| AVGEN_NO_TERRAIN_CACHE | scene/composition.cpp:6890 | intended no | 4 | |
| AVGEN_NAV_FORCE_TRUST | entity/nav_grid.cpp:840 | **yes** | 1 | "never for anything anybody keeps" |
| AVGEN_DIAG_NO_SIGHTLINE | scene/composition.cpp:2402 | **yes** | 1 | |
| AVGEN_NO_WARMUP | app/render_job.cpp:163 | **yes** (a few pixels) | 5 | |
| AVGEN_LEGACY_PROCGEN | app/engine.cpp:678 | intended no | 3 | CA-016 |
| AVGEN_NO_CONTROL_REPLAY | app/engine.cpp:4586 | **yes** (the seek lands differently) | **0** | CA-005 |
| AVGEN_PROFILE_CONTROL_REPLAY | app/engine.cpp:4706 | no; log only | **0** | CA-005, delete |
| AVGEN_NO_SEEK_DEFERRAL | app/engine.cpp:636 | live only | **0** | |
| AVGEN_SEEK_BODY_STEPS | app/engine.cpp:665 | live only; changes exactness | **0** | |
| AVGEN_SCATTER_WORKERS | world/ecology.cpp:427 | intended no | 3 | |
| AVGEN_TRACE_DEBUG | world/terrain_gen.cpp:1033 | no; stderr | **0** | uncached getenv inside a loop |
| AVGEN_ASTRAL_TRACE | rendering/astral_renderer.cpp:820 | no; log | **0** | uncached getenv per frame |
| AVGEN_FRAME_COUNTERS / AVGEN_SHADOW_STATS / AVGEN_CPU_STAGES | rendering/scene_renderer.cpp:5020/5062/5089 | no | yes | |
| AVGEN_TIMELINE_RAW | gpu/frame_timeline.cpp:15 | no | 2 | |
| AVGEN_HEIGHT_CENSUS | world/world_map.cpp:443 | no | 3 | |
| AVGEN_DIRTY_TRACE | app/application.cpp:2687 | no | 3 | uncached getenv per dirty sample |
| AVGEN_SLOW_PHASE_MS, AVGEN_VIEWPORT_PROBE/DRAG, AVGEN_UI_SELFTEST, AVGEN_SLIDER_FILTER, AVGEN_EDITOR_TRACE | app/, ui/ | no | 1-4 | |
| AVGEN_CAPTURE_WORLD_INSPECTOR, AVGEN_HELP_SELFTEST | ui/control_panel.cpp:1530, ui/help_panel.cpp:243 | no | **0** | |
| AVGEN_SHADER_DIR, AVGEN_EXAMPLES_DIR, AVGEN_HELP_DIR, AVGEN_FFMPEG, AVGEN_NDI_LIB, AVGEN_QUALITY | various | locations only | yes | legitimate configuration |

---

## Dead code candidates

### Whole modules with no `src/` consumer

| Symbol / file | Evidence of no use | Removal risk |
|---|---|---|
| `src/app/profile_capture.{hpp,cpp}` (ADR-031, 203 lines) | Included only by `tests/unit/test_profile_capture.cpp`. The live profiler (`live_profile*.cpp`) replaced it. It is still compiled into seven tool targets (CA-010). | Low: delete it, its test, and the tool-list lines |
| `src/ai/inference.{hpp,cpp}` (ADR-065, 595 lines) | Included only by `tests/unit/test_ai_inference.cpp`. `makeBuiltinBackend`, `modelTaskFromName` and the rest have no product caller. | Medium: ADR-065 frames it as the optional AI runtime. Ask before deleting. |
| `src/entity/motion_evaluation.{hpp,cpp}` (229) | Test only (`test_motion_evaluation.cpp`). An offline evaluation harness. | Low; it could move to tools/ |
| `src/scene/motion_variants.{hpp,cpp}` (265) | Test only. Phase B §42/43 offline generator. | Low-Medium |
| `src/scene/motion_search_index.{hpp,cpp}` (526) | Tests only (`test_motion_search_index`, `test_motion_search_perf`). §54 says do not adopt unless exact search is too slow. | Low: a research artefact |
| `src/scene/tree_audio.{hpp,cpp}` (238) | Test only (`test_tree_audio.cpp`). `registerTreeParameters` and `defaultTreeRoutes` are never called by the engine. | Medium: it may be the intended tree→music wiring, so it is "built but unreachable" |
| `src/entity/root_motion_adapt.*`, `src/entity/trajectory.*` | Test only | **Keep**: ADR-615 says "staged and dark", documented |
| `src/rendering/quality_policy.hpp` | Tests only | **Keep and wire** (CA-007) |
| `src/rendering/reference_renderer.*`, `renderer_snapshot.*`, `scene/temporal_conformance.*`, `world/effects/effect_conformance.*`, `world/march_schedule.*`, `scene/deformation_contract.hpp`, `labs/visibility_reason.hpp` | Tests only | **Keep**: deliberate oracles and conformance |
| `tools/_slope_probe.cpp` | Self-declared TEMPORARY | Low (CA-013) |
| `indtune.cpp` (repo root, 0 bytes) | No reference | None (CA-014) |

### Functions declared in a `src/` header with no caller anywhere (`src/`, `tests/`, `tools/`, `prototypes/`)

These were found by counting word occurrences: each name appears only at its declaration and its definition.

| Function | Header | Risk |
|---|---|---|
| `domainCapabilities` | ai/capabilities.hpp | low |
| `sceneLightKeys` | scene/composition.hpp | low |
| `phaseRateTable` | scene/route_liveness.hpp | low |
| `boltStripVertices` | world/effects/bolt_path.hpp | low |
| `envelopeRamp` | world/effects/effect_timing.hpp | low |
| `nearestCourse` | world/terrain_water.hpp | low |
| `effectStyleNames` | world/atmospherics.hpp | low |
| `hoverTip` | ui/style.hpp | low |
| `attentionModelToJson`, `memorySettingsToJson` | entity/mind.hpp | low. The `FromJson` halves are used (`behaviors.cpp:2560, 2568`), so the round trip is untested, not dead weight. Keep and add a test, or delete. |
| `tierFromName`, `cameraMoveFromName`, `performanceModeFromName`, `contentDomainFromName`, `subjectKindNames` | directing/plan.hpp | low |
| enum name helpers: `credentialSourceName`, `stopReasonName`, `hitClassName`, `transportModeName`, `reactiveGroupFromName`, `authorityName`, `movementModeName`, `reasonCode`, `casterStateName`, `interpolationName`, `viewportCameraName`, `bodyCompensationStatusName`, `phaseLandmarkKindName`, `motionLoadStateName`, `humanoidRoleFromName`, `branchTierName`, `treePartName`, `lyricFormatName`, `snapModeName`, `snapModeFromName`, `stageEventKindName`, `itemMarkName`, `selectionKindName`, `performanceClassName`, `terrainRejectName`, `propagationKindName`, `propagationKindFromName`, `directionModeFromName`, `depthBandName`, `modelTaskFromName` | various | trivial. They are cheap and conventional; delete only during a touch. |

About 280 further header-declared functions are used only inside their own `.cpp` (they could be `static`/anonymous, which tightens the API). That is not dead code, so they are not listed.

---

## Scene-specific code in shared engine

| Location | What it does | Should it live behind a capability? |
|---|---|---|
| `scene/scene.hpp:94-96` (`Ecosystem ecosystem; AstralForge astral; Clouds clouds;`), `scene/composition.hpp:22, 1434-1440, 2332-2336`, `composition.cpp:1084-1095` and roughly 50 more lines | Each Environment (ADR-1200 Rift ecosystem, ADR-1221 Astral Forge, ADR-1289 clouds) is a typed member of `Scene` and `Composition`, with bespoke setters, parameters and conductors. The seam is generic only on the renderer side (`environments_` in `scene_renderer.cpp:551`). | Accepted by ADR-1221 ("adds nothing to the interface"). Debt: a fourth environment means editing `Scene`, `Composition`, `SceneRenderer`, `QualitySettings` and the live ladder. A `std::vector<std::unique_ptr<EnvironmentBlock>>` keyed by block name would make it plug-in. Not this sprint. |
| `rendering/render_quality.hpp:357, 435` (`astralTier`), `app/interactive_resolution.cpp:96-121` (`astral(...)` on every rung), `rendering/scene_renderer.cpp:1830-1834` (`astral0..4` arms), `app/live_profile.cpp:387-391` (`astral.*` stage mapping) | Astral-Forge-only quality levers baked into the generic quality struct and live ladder (ADR-1222) | Yes, eventually: each Environment could expose its own tier count and lever. Retained for now (ADR-1222). |
| `world/world_map.cpp:1007-1010` (`defaultWorld()` → `w.name = "glowmere"`) used by `scene/composition.cpp:12782` | A `{"kind":"terrain"}` node with no `"world"` silently becomes the Glowmere valley | Low. Either require `"world"`, or rename it `defaultWorld` → `glowmereWorld` and log which world was chosen. **Do not change Glowmere's geometry** (owner rule). |
| `organism/mushroom.cpp:457-461` (`findArtProfile("glowmere")`) | The procedural mushroom generator hard-wires Glowmere's palette for every scene, cached in a function-local static | Yes: pass the scene's art profile through `GeneratedSource` (a value or name), with Glowmere as the fallback |
| `world/atmospherics.cpp:399` (`glowmereAurora`) | A named preset factory, used only by `tests/unit/test_aurora_audio_response.cpp` | Low: move it to the test or to presets |
| Effect style presets named after scenes (`bioluminescence_effect.cpp:82` "Glowmere Creature", `particle_emitter_effect.cpp:398-437`, `force_field_effect.cpp:114`, `meteor_shower_effect.cpp:229`, `aurora_effect.cpp:151`, `pulsing_veins_effect.cpp:80` "Tree of Life") | Authoring preset names, which are data | No; this is content, not a hack |
| `labs/lab.cpp:165` | The Interaction Lab names `examples/world/glowmere-valley-2-multicam.json` as its reference project | No (lab registry metadata) |
| `app/application.cpp:204` (`--sonic-trace` help text) and `app/sonic_trace_cli.cpp` | A Sonic Garden CLI | Fine: `sonic/` is a general subsystem and the CLI only names it |

No `if (name == "<scene>")`-style special cases were found in the renderer, Composition or Entity code. The scene coupling above is structural (typed members), not ad-hoc branching.

---

## Retained debt (looks bad, but is intentional)

| Item | Why it stays | Reference |
|---|---|---|
| `entity/root_motion_adapt.*`, `entity/trajectory.*` have no product caller | "Staged and dark": left built and documented rather than wired or deleted | `docs/decisions/ADR-615-the-character-motion-seam-is-staged-and-dark.md` |
| Typed Environment blocks in `Scene` and `Composition`, plus `astralTier` | The Environment seam decision | ADR-1200, ADR-1221, ADR-1222 |
| `QualityPolicy` not yet routed through `SceneRenderer` | "Nothing is forced to route through it yet … one caller at a time" (but see CA-007: the guarantee it is cited for is not real) | `ADR-134-one-quality-policy-object.md` |
| A/B environment switches (`AVGEN_NO_TERRAIN_CACHE`, `AVGEN_SCATTER_WORKERS`, `AVGEN_HEIGHT_CACHE`, `AVGEN_PATH_CUTOFF`, `AVGEN_LEGACY_PROCGEN`) | An interleaved A/B in one binary, because the machine's load moves (comments cite `docs/application-performance.md` §3). Keep, but register and announce them (CA-004). | ADR-170, ADR-182, ADR-233 |
| `seq::Shot`, `scene::CameraShot` and `app::Shot` | Three different objects: a sequence span, the director's camera-track span, and the cinematic shot vocabulary (`seq` holds an `app::Shot` by value) | ADR-062, ADR-089, ADR-245; memory "two shot types" |
| `app/cinematic.cpp`, `song_plan.cpp` and `song_director.cpp` compiled into `avgen_core` | So that `seq` and Song Mode test without the editor | Comment in `src/CMakeLists.txt:12-20` |
| The CoreMIDI `ClientHost` intentionally leaked, with a detached run-loop thread | The CoreMIDI client is never disposed; ordering is stated in code | `control/midi_coremidi.cpp:283, 301`; ADR-880 |
| The `[!shouldfail]` case in `test_character_lab_slopes.cpp` | A correct invariant the engine does not meet yet; one expected FAILED line per run | memory "shouldfail is not a failure" |
| 157 hidden `[.]` instrument cases | Probes and captures, run on demand | docs/testing.md |
| A project-format migration chain from v1 to v4 | A format envelope; arguably outside ADR-441, which is about effects (but see CA-019) | `params/serialization.cpp:430-521` |
| `catch (...)` in job_system, tool_api, main_thread_queue, application and route_liveness | Each converts to a reported failure; a throw must not kill a worker or the frame loop | inline comments |

---

## Top 10 recommended fixes for this sprint (ordered by value against risk)

1. **CA-002**: `SUCCEED`→`SKIP` in the 15 path-trace cases, and REQUIRE the load in `test_pathtrace_glowmere::loadAt`. Test-only, removes a lying pass, zero product risk.
2. **CA-001**: `std::call_once` for generator registration, plus mutexes on `generatorRegistry()` and the `generatedMeshForBounds` cache. Small, closes a real UB race with the Director's background engines. Add one TSan case.
3. **CA-005**: delete the `prof[]`, `t0..t5` and `AVGEN_PROFILE_CONTROL_REPLAY` block in `Engine::replayControl`. Pure removal; it closes the open item from Digital Mosh.
4. **CA-014 and CA-015**: `git rm indtune.cpp temporal-*.png`, point the capture-test default at a temp dir, drop the orphan ADR-023 CMake comment, and add `/*.png` to `.gitignore`. Ask about `05-static-reference…png`.
5. **CA-010**: one `avgen_engine` static library instead of three hand-copied source lists in `tools/CMakeLists.txt`. Build-only, faster builds, ends drift.
6. **CA-009 and CA-017**: stop discarding `waitIdle`'s timeout in offline motion loads (at least `log::error`), and check the `materials.json` UNSTABLE write. Two-line fixes on the deliverable path.
7. **CA-004 (logging half)**: a central table of `AVGEN_*` diagnostics, a startup WARN listing any set output-changing variable, and the set ones stamped into the render manifest.
8. **CA-007**: route `SceneRenderer::setQuality` through `QualityPolicy::forTier(t).settings` after a 4-tier sequence-hash comparison, or add a RenderJob-level test of the renderer's real offline settings.
9. **CA-008 (mechanical half)**: hoist the five per-frame draw-list vectors to members and stop copying `chain.rungs` per entity per frame. Verify with the frame hash.
10. **CA-013 and CA-011**: delete `tools/_slope_probe.cpp` (owner confirm), and make the single-run wall-clock CHECKs best-of-N and sanitizer-aware.

Deferred for an owner decision: CA-003 (refuse offline renders with missing assets), CA-006 (promote `probe2` to per-Engine counters), CA-016 and CA-019 (legacy procgen arm and parser aliases), and the dead-module removals (`ai/inference`, `scene/tree_audio`, `app/profile_capture`).
