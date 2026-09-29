# QA pass: final report

2026-09-28. Branch `qa/coord`, written at `ecf0fca4` (every QA branch merged: `qa/clean`, `qa/perf`,
`qa/dirty-check`, `qa/ci`). The brief is `00-brief.md`, the plan `PLAN.md`, and the running log `PROGRESS.md`.
The workstream notes are `perf.md` and `perf-summary.md` (W1), `ci.md` (W2), `clean.md` and
`gv3-state-audit.md` (W3). The CI reference is `docs/development/ci.md` and
`docs/development/gpu-ci-private-assets.md`.

Unless a row says otherwise, performance numbers come from W1's clean retake (`perf.md` section 1): this
machine, headless, 1280x720, the median of 3 interleaved runs, with no other `avgen` process running.
"Before" is main `77ea4247`, and "after" is main plus ADR-950 and ADR-951.

## Summary

**GV3 was slow because of an engine regression, not because of the project or the renderer.** ADR-834
(`29e6918e`, merged 2026-09-25) made the engine trace `world::heroSightline` for every in-shot character on
every frame, to publish a `character.<name>.visibility` signal that no project reads. Its cost grows with the
number of characters in frame and their distance, which is exactly what a wide has: r = 0.975 across GV3's
73 shots. ADR-893/894 then made every terrain height query 1.79x as expensive. On the worst wide (s66) the
engine update was 348 ms of a 376 ms frame (2.7 FPS). It also ran while paused, which is why navigating was
slow. ADR-950 and ADR-951 fix it: s66 now takes 32.2 ms (31 FPS). What remains is ordinary GPU cost from the
valley's foliage. A fixed wide is GPU-bound at 31-33 FPS at 720p, and no project state was found that costs
a measurable share of the frame.

**CI:**
- Push CI is green on `qa/ci` (`2f89010a`).
- ASan, UBSan and TSan have all run on hosted runners with **0 sanitizer reports**, and the sanitizer gate now
  fails on an exit code it cannot explain.
- The nightly schedule runs only from `main`, so the new layout's first nightly run comes after the merge.
- GPU CI is built but **parked by the owner**. The private asset repository exists and is pinned. The deploy
  key, the variables and the self-hosted runner are not set up.
- Hosted macOS runners cannot give a GPU verdict: 363 of 443 GPU cases fail there because the Metal pipelines
  do not compile.

**Also merged into `qa/coord`, outside the QA scope:** the astronaut musician prototype
(`proto/astronaut-musicians`, `7f1dc21d` and `3f89673a`). The owner asked for it as a separate task. It adds
`examples/musicians/`, an `examples/index.json` entry, `tools/make_astronaut_musicians.py` and
`assets/musicians/ATTRIBUTION.md`. **It needed no engine change.** The GV3 art pass runs separately, after this
pass, and is not covered here.

---

## 1. Why is GV3 slow?

Four separate contributions, each measured.

### Engine regression: the collapse
- **ADR-834's per-frame sightline.** `Composition::publishCinematicSignals` ran `heroSightline` (9 rays, a
  ground sample every 2 m) for each in-frame character to publish `visibility`. Nothing shipped reads that
  signal.
  - A `sample` profile put 7,436 of 7,437 main-thread samples inside `Engine::update` in
    `publishCinematicSignals`, about 7,300 of them in `heroSightline`.
  - Across the 73 shots, engine-update ms against (sightlines x mean metres) gives r = 0.975.
  - s66 has 18.2 characters in frame at a mean of 286 m. It is a mushroom close-up by label, with the farm
    herd behind it.
- **ADR-893/894** (`0fd83284`, merged in `0b623b88`) made each height query 1.79x as expensive
  (`blendedLevel`, `closestOnPath`). That doubled the sightline's cost.
- On GV2 multicam's Valley Wide, the same file on each build (`perf.md` section 6):

  | build | wall | engine update | GPU |
  |---|---:|---:|---:|
  | `3e09f9e1`, before ADR-834 | 25.6 ms | 3.2 ms | 17.2 ms |
  | `29e6918e`, ADR-834 merged | 43-47 ms | 21.7-23.6 ms | 17.2-17.9 ms |
  | `0b623b88`, ADR-893/894 merged | 59.1 ms | 38.8 ms | 16.8 ms |
  | `77ea4247`, main | 61.6-65.7 ms | 37.6-40.6 ms | 19.1-20.0 ms |
  | main + ADR-950/951 | 27.0-28.0 ms | 3.3 ms | 19.2 ms |

### Unavoidable renderer cost: what remains
- With the fix, a GV3 wide is **GPU-bound at 720p**. s66 spends 23.5 ms on the GPU inside a 32.2 ms frame,
  and 18.6 ms of that is the scene pass. The s27 close-up's scene pass is 3.4 ms. The wide has 614k
  triangles against 135k, 2,415 visible procedural instances against 20, and 206 clustered lights against 38.
- **No optional feature owns the cost.** Each `--disable` kill switch saves 0.6-1.9 ms of a 25 ms GPU frame:
  water 1.9, post 1.3, animation 1.2, AO 1.0, particles 0.8, and shadows, volumetrics and transparency 0.6
  each. Turning off the ecology lights saves 1.4 ms, but that arm ran while the machine was busy.
- The scene pass is about **9.4 ms fixed plus about 10 ms per megapixel**. This is the renderer's known cost
  for dense distant foliage (`docs/renderer-upgrade/README.md`), and it is the same in GV2 multicam.

### Scene complexity
**GV3's world is GV2 multicam's world**:
- the same 18 scatter layers and 197,100-instance cap;
- the same 29 unique meshes and 14 shadow-casting layers;
- 3 rig lights;
- no layer's density, instance cap, view distance or shadow flag changed.

GV3 adds direction (73 shots, 84 camera tracks), modulation (81 routes), staging (2 actors, 5 scenarios), two
directing plans, and one hidden 32k particle pool.

The one way complexity mattered is that GV3's wides frame more characters at a greater distance, and the
regression charged for exactly that. GV2 multicam's wides ran at 15.6 FPS before the fix, against GV3's 2.7,
in the same world.

### Project-specific problems
**None measurable** (section 2). Ten copies of GV3, each with one category of state removed, stayed inside
the control's spread on the fixed build.

### Accepted look costs and the editor
- **Accepted look costs.** The GPU grew 1.9 ms (+11%) at identical geometry between `3e09f9e1` and main:
  - ADR-945's glow pools cost +1.3 ms of scene pass. It is the look the owner chose.
  - A creep of about +1.1 ms happened across `0b623b88..22ce5c3d`. The largest single step is +0.5 ms, at
    the emission merge `7b030ac4`. It is the shading cost of features the revision added.
  - The two parts sum to more than 1.9 ms because they were measured from different baseline builds. See
    "Where the sources disagree".
- **Editor stall.** ADR-440's unsaved-changes check serialises the whole project, about 36 ms on GV3's 8,092
  parameters, every ~380 ms while the project is unmodified. That is a two-frame hitch about 2.6 times a
  second.
  - Fixed for playback by ADR-952: `ui.build` p95 went from 35.4 to 0.86 ms, and the frame p95 from 50.4 to
    17.7 ms.
  - Unchanged while paused, by the owner's choice.

## 2. Did GV3 contain bad or stale state?

**No state that costs frame time.** The evidence is W1's removal experiment (`perf.md` section 5): 640x360,
3 interleaved repeats, the s66 wide and the s27 close-up, on both the base and the fixed binary. Median wall
ms:

| arm (what was removed) | s66, base | s66, fixed | s27, base | s27, fixed |
|---|---:|---:|---:|---:|
| control (generator round trip) | 385.1 | 23.4 | 28.4 | 17.2 |
| 26 `groundPulse` hero pulses | 384.3 | 23.5 | 26.1 | 17.7 |
| every effect | 381.5 | 22.9 | 26.6 | 16.8 |
| staging and both directing plans | 410.4 | 23.6 | 23.4 | 17.5 |
| all 81 routes | 385.0 | 23.8 | 30.0 | 17.3 |
| the 44 non-camera tracks | 387.9 | 23.5 | 27.4 | 17.4 |
| scout, scout-beam, the 2 fields | 365.7 | 23.2 | 23.0 | 17.0 |
| every entity | **22.4** | 22.1 | **16.3** | 16.8 |
| hidden beam pools at 256 | 384.8 | 24.7 | 28.6 | 16.9 |
| ground pools back to GV2's | 383.2 | 25.1 | 28.0 | 17.1 |

- Only removing the entities moves the old build, because it removes what the sightline was traced to. On
  the fixed build every arm is inside the control's spread.
- The two hidden 32,768-particle beam pools are not simulated: `particleCapacity` reads 21,888 in every arm.
- The ground pools add 47 clustered lights to the wide and nothing measurable to its GPU time at 360p.
- This ran while W3's CPU suite was running, with a maximum load of 13.6.

**What W3's static audit found anyway** (`gv3-state-audit.md`). None of it has a frame cost:
- 12 effect values stored twice with different numbers, in the effect's block and in its `fx/` parameter. The
  parameter wins. GV2 multicam has 2. The notable one: `fx/aurora/audioSensitivity` is 0 as rendered, while
  the block says 1.0. The owner has since asked for an aurora bass pulse, which went into the art-pass brief.
- The scene's 12 `effects` are dead under the project's 18 (ADR-702).
- `lod.lodCount` on three procedural nodes, and terrain `groundGlow 0.08` under `paintedGround2`. Both are
  dead data inherited from GV2 multicam.
- 16 false "dead route" load warnings on the marker-triggered effects.
- **Not found:** broken references, orphaned parameters, dead routes, tracks or effects (the audit reports
  105/105 routes, 130/130 tracks, 18/18 effects), stale cameras (74 cameras, of which 73 are used and the
  74th is `Main`), orphaned staging, or other hidden nodes.

**What was removed or cleaned:**
- **Nothing was removed from GV3 itself.** It was measured on copies only.
- The repository's stale GV3 (`334c4cf6`, the Phase 3 foundation) was replaced by r7b, the reviewed revision
  (question 6's GV integration).
- `bc2c1939` removed the nine `examples/world/_diag-water-*` arms and `_pre-defects{,.scene}` (5.8 MB). Nothing
  referenced them, and ten ADRs had to migrate them anyway. It also refreshed two stale fingerprints.
  `check_project_integrity.py` went from 20 problems to 9.
- The 17 GV3-era worktrees were removed and their branches kept. gv3-int's r1-r7b review snapshots (2.0 GB)
  were archived to `~/Documents/GitHub/av-gen-archive/` first.

## 3. Were there engine regressions?

Yes, two on the CPU. The details are in the "Performance regressions" table below.

1. **ADR-834 (`29e6918e`).** It ran the per-frame sightline for an unread signal: engine update 3.2 to 21.7 ms
   on GV2 multicam's wide, with draws and triangles identical. Fixed:
   - **ADR-950** (`edf62e2d`): `surfaceAt` asks for `max(height, waterSurface)` instead of a full
     `WorldMap::sample`. The output is bit-identical (digest `b27d09046eeb9d4b` on both builds), and each
     sightline is 3.6x cheaper (4,481 to 1,229 us at 200 m). s66 went from 376.2 to 235.4 ms.
   - **ADR-951** (`97925c18`, accepted by the owner): `visibility` is traced only for a character whose
     signal something has looked up through `SignalBus::find`. Otherwise it reads 0. s66 went from 235.4 to
     32.2 ms, and its engine update from 347.7 to 3.1 ms. Reverting that one commit restores the old
     behaviour.
2. **ADR-893/894 (`0fd83284`).** Height queries became dearer: engine update 21.7 to 38.8 ms, bisected to that
   one commit (ADR-892 checked and cleared). ADR-951 **neutralises** it by taking the sightline out of the
   frame: the engine update is 3.3 ms, against 3.2 ms before ADR-834. There is no separate fix. Its cost
   outside the sightline is below measurement.
3. **GPU +1.9 ms:** not a defect (section 1).

**Regression tests:**
- `tests/unit/test_sightline_surface_cost.cpp` (`[adr950]`) pins bit-identity, with a `[.perf][adr950]`
  benchmark.
- `test_signal_bus.cpp` and `test_cinematic_signals.cpp` cover ADR-951 (`[adr951]`).
- `test_unsaved_changes.cpp` and `test_project_lifecycle.cpp` cover ADR-952 (`[adr952]`).

## 4. What is the practical performance envelope?

On the fixed build, this machine:

| workload | 1280x720 | 640x360 |
|---|---:|---:|
| GV3 r7b wides (s66, s24, s26) | 30.8-32.2 ms, **31-33 FPS** | 19.1-23.1 ms, **43-52 FPS** |
| GV3 r7b close-up (s27) | 15.8 ms, 63 FPS | 13.4 ms, 75 FPS |
| GV3 r7b, no characters (s17) | 26.5 ms, 38 FPS | 18.3 ms, 55 FPS |
| GV2 multicam wide / hero | 27.7 / 31.0 ms, 36 / 32 FPS | 18.9 / 18.5 ms, 53 / 54 FPS |
| grove (small scene) | 13.2 ms, 76 FPS | 7.5 ms, 134 FPS |

- **The limit is the GPU scene pass at 720p.** At 360p the fixed wide's frame is ~23 ms against ~15 ms of
  GPU, so the frame is no longer GPU-bound there.
- **1080p is extrapolated, not measured.** A GV3 wide would take about 30 ms of scene pass, which puts it near
  24 FPS.
- **All 73 shots.** The only whole-cut sweep was taken at 640x360, one repeat per arm, with another agent's
  suite running part of the time. It used the `nosight` diagnostic arm (the fix binary with the visibility
  march switched off) as a stand-in for the fixed build:
  - median 21.0 ms;
  - worst 26.8 ms;
  - 0 shots under 30 FPS, against 58 on base;
  - worst GPU frame 17.2 ms.

  No full 73-shot sweep exists for the fixed binary itself.
- **The live editor** (732x664 canvas):
  - a playing wide: 9.3 ms frame p50;
  - a paused wide: 16.7 ms (vsync), down from 81.8 ms;
  - camera orbit: 16.7-24.7 ms against 76 ms before. Caveat: `--ui-ab` ignored `--start-at`, so these
    runs measured the opening shots playing, not the paused wide.
  - With ADR-952, the playing frame p95 on s66 is 17.7 ms.
  - A paused editor still hitches about 36 ms every ~380 ms while the project is unmodified.
- **Levers beyond this:** content (scatter density and view distance on the wides), or renderer work this pass
  did not do. Occlusion culling and entity LOD are not live.

## 5. Which testing gaps were fixed?

Each fix below came with a test that failed first, unless noted.

- **G5, a crash.** An assistant `scene.delete_node` or `scene.set_parent` left routes pointing at freed
  parameters. The test is `test_ai_tools.cpp`, "Deleting or re-parenting a node through the assistant
  re-binds the routes that drive it".
- **G1, a crash.** A wrong-typed project key terminated the app. 4 of 6 damaged files reproduced it. The test
  is `tests/integration/test_project_malformed.cpp`.
- **An asset-free twin of the delete-while-running regression.** Added to `test_delete_nodes.cpp` using the
  set-piece lab. **It was not shown failing against the pre-fix code.**
- **The three GV3 wrap-up defects** (W3, `9cdb68be`), each with a test:
  - a scene-load failure is now reported;
  - `--export-bundle` keeps a top-level `lightRig` (3 assertions fail without the fix);
  - the batch flags imply `--headless` (`[app][cli]`).
- **The farm-asset family.** 9 cases asserted on a scene with no animals after `4a138886`. Four per-file
  `farmAssetsPresent()` copies became one guard, `testsupport::skipUnlessFarmAssetsPresent()`. With the farm
  files moved aside, 87 cases give 55 passed, 32 skipped and 0 failed.
- **A flaky test.** In "Cancellation stops execution and rolls back deterministically", one `pump()` could
  drain the whole queue. It now pumps one item at a time, and passed 40 of 40 runs.
- **The scores are generated on CI** (`tools/ci/generate-audio.sh`, checked against the manifest).
  - This un-skips 26 cases.
  - It exposed a stale sha256 for `glowmere-valley.wav`, which is fixed.
  - Both crash exclusions are retired, and `hosted-runner-exceptions.txt` now excludes nothing.
- **The verdict.** An unexplained exit code now fails a sanitizer job, and Catch2's exit 4 (every case
  skipped) is recognised exactly (question 7).
- **ADR-440's schedule was only described before. It is now unit-tested,** as `ui::DirtySampleSchedule` (ADR-952).
- **Not fixed:**
  - G2 and G3, the data-loss hazards (section 10);
  - G4: `outputs` has no round-trip test (low risk).

## 6. What CI infrastructure was added or cleaned?

- **Triage of every red run** (`ci.md` section 1):
  - the farm-asset failures were caused by missing assets and are now guarded;
  - the cancellation failure was a flaky test and is fixed;
  - the uninitialised-padding comparison was a real defect, already fixed by ADR-940;
  - the old sanitizer stages hit 5 timeouts (a partition problem, re-planned) and one real UBSan report
    (`packComet`, fixed by ADR-941).
- **One composite build action**, `.github/actions/cmake-preset`, shared by both workflows. (`.gitignore`'s
  `build*/` rule had been swallowing `.github/actions/build*`.)
- **`tools/ci/run-suite.sh`**: one command runs any suite exactly as CI does. Local and CI behaviour are now
  the same.
- **Caches** are saved from `main` only, and `tools/ci/prune-caches.sh` keeps the newest entry per key.
  Before: 24 entries, 10.7 GB against a 10 GB quota, and `main`'s TSan cache had been evicted.
- **Workflow structure:**
  - `ci.yml`: build, then `cpu-tests` (the gate), `gpu-hosted` (informational), `cpu-assets` and the
    self-hosted `gpu` job (both gated off), and a report.
  - `sanitizers.yml`: `asan-build` then 5 ASan jobs, plus `ubsan`, `tsan` and a report.
  - `actionlint` passes cleanly.
- **The push gate's time:** PROGRESS.md says about 8 min, down from 22. `ci.md` records 6 min for the CPU job
  on `df333c8c` (run 36464437262).
- **Private-asset tooling:**
  - `tools/fetch-test-assets.sh`, `tools/ci/make-test-assets-repo.sh`, `test-assets.list` and
    `test-assets.lock`;
  - the private-repository workflow template;
  - `tools/ci/opentrace.c`, the `open()` tracer that measured the asset list.
- **Security:**
  - `contents: read` on every workflow;
  - actions pinned to commit SHAs;
  - `persist-credentials: false`;
  - dispatch inputs reach scripts only through `env:`;
  - the one asset secret is used on trusted events only.

## 7. Which sanitizer workflows exist?

`sanitizers.yml` runs nightly from `main` and on dispatch. It has three sanitizers and a report job.

| job | build | what runs | latest result |
|---|---|---|---|
| ASan (with UBSan's checks, recoverable) | `--preset asan`, Debug -O0, `detect_stack_use_after_return=1` | 5 jobs from `tools/ci/sanitizer-plan.txt`: heavy-1..3 and rest-a/b over 11 rest shards | 36469640514 (rebalanced): **SUCCESS, 0 reports, 0 disagreements**. Build 19 min. Critical path **2 h 46 min**, down from 3 h 37 min. Wall clock 3 h 08 min, 479 macOS job-minutes |
| UBSan alone | own preset, `halt_on_error=1` | the whole default set, 3 shards | 36434165586: **SUCCESS, 3838/3838, 0 reports**, 3 h 48 min |
| TSan | `--preset tsan` | the concurrency subset: `[jobs] [job] [motionthreads] [threading] [worldbuilder] [ring] [analysis] [motionlib] [ai] [transport]` | 36434165586: **243 cases, 0 reports**, an 82 min job. Nightly since 2026-09-28; it was Sundays only |

- **The same run, 36434165586, on the old 7-shard layout: SUCCESS, 0 reports anywhere.**
- **Whole-suite TSan does not fit.** Probe 36434180499 timed out at 300 min after 1680/3838 cases, with 0
  reports and 0 crashes. The whole set would need about 11-12 h of 3-shard time. The subset stays the nightly
  TSan, and its summary labels it `SUBSET`.
- **What fails a job** (`--gate sanitizer`): a sanitizer report, a crash, a timeout, an unexercised case, or
  an exit code the Catch2 XML does not explain.
  - That last rule is new. `catch2_run.py`'s `all_skipped()` accepts exit 4 only when 0 passed, 0 failed
    and more than 0 were skipped.
  - It was checked by replaying the run's own artifacts and synthetic statuses.
  - Plain assertion failures (wall-clock ceilings under -O0) are listed but do not gate.
- **Exclusions and why:**
  - **Two cases skipped by name in the ASan plan:** the farm locomotion case (1,122 s in Release, killed at
    311 min) and "the five characters all walk, at more than one seed" (killed at 299 min). ASan does not
    check them. UBSan runs whatever the runner can.
  - **Asset-bound cases skip on hosted runners**, and plan processes whose cases all skip exit 4.
  - **LeakSanitizer is off:** Apple clang on macOS does not support it.
  - **GPU tests are not run under any sanitizer:** they run about six cases an hour, and the hosted GPU is
    not authoritative.
  - **The three bit-exact golden traces need `assets/aliens`,** so they cannot run on CI at all. No exclusion
    was invented for them.
- **Every job** uploads `sanitizer-logs-*` (14 days). The summary shows each report as `SANITIZER FAILURE`
  with its type, first frame and stack.

## 8. How does GPU CI reach the restricted assets?

The design is in `docs/development/gpu-ci-private-assets.md`, and it is **implemented, then parked by the
owner**.

- **The private repository.** `NateFaulkenberry/av-gen-test-assets` is **private**, at `61ff6dd` (202 files,
  248 MB). It mirrors `assets/` paths and carries `SHA256SUMS`, and `tools/ci/test-assets.lock` pins it.
- **The only way in is `tools/fetch-test-assets.sh`,** for CI and locally alike. It:
  - clones outside the checkout;
  - checks out the pinned commit;
  - verifies every hash;
  - symlinks each file into `assets/`, and refuses any path git does not ignore;
  - fails with an `::error::` naming what is missing: the secret, the repository or the commit.
- **Authentication:** a read-only deploy key as secret `AVGEN_TEST_ASSETS_SSH_KEY`, or a fine-grained token.
  It is never in a workflow file or a log, and it is used only on nightly and dispatch runs, never on
  `pull_request`.
- **Nothing** is cached or uploaded, not even a rendered frame.
- **Two consumers:**
  - `cpu-assets`, a hosted job for the Glowmere tier of the CPU suite;
  - the GPU suite on a self-hosted Apple-silicon runner.

  **The owner decided the runner lives on the private repository.** On a public repository a fork's pull
  request can run its own workflow file, so a job's `if` cannot protect a self-hosted runner there.
- **Status:**
  - Steps 1-3 are done: the repository was built, pushed private, and pinned.
  - **Steps 4-6 are parked:** no deploy key or secret, `AVGEN_TEST_ASSETS` and `AVGEN_GPU_RUNNER` unset, and
    no runner. Both asset jobs are skipped, and the summary says so.
  - The private-asset jobs have never run on GitHub. The scripts were tested offline end to end, including
    their error paths.
- **Hosted GPU stays informational:** 363 of 443 cases fail on the paravirtual Metal device. Until a runner
  exists, a GPU verdict comes from `tools/ci/run-suite.sh gpu` on a real Mac.

## 9. How were the test assets minimised?

- **Measured, not guessed.** An `open()`/`fopen()` interposer (`tools/ci/opentrace.c`) traced the whole GPU
  suite (549 cases) and the 280 CPU cases that a hosted runner skips or fails for want of an asset. The result
  is `tools/ci/test-assets.list`:

  | tier | files | size |
  |---|---:|---:|
  | 1, `avgen_render_tests`: 5 aliens, 3 farm animals, 55 Quaternius | 63 | 65 MB |
  | 2, Glowmere-tier CPU cases: more aliens and farm, 55 Quaternius, 55 Kenney city, 15 nature, 1 HDRI | 133 | 44 MB |
  | 2, Tree of Life island (optional) | 6 | 152 MB |

  That is against 1.4 GB in `assets/`. With every tier present: the CPU cases give 263 passed, 17 skipped and
  0 failed; the GPU suite 548 passed, 1 skipped and 0 failed.
- **The 17 skips need motion packs that are not on the owner's machine either. No machine runs them.**
- **Generated, not stored:** both repository scores are regenerated on CI and checked against the manifest.
- **The tiers:**
  - unit tests already use tiny fixtures (`tests/data`, `gltf_fixture.hpp`);
  - the delete-while-running regression gained an asset-free twin;
  - integration tests use the tracked procedural examples;
  - the abduction films were **not** replaced. Their subject is the shipped film, and a fixture would test a
    different film.
- **Three CC0 GPU fixtures** (`CommonTree_1`, `Rock_Medium_1`, `Mushroom_Common`, about 4 MB) are
  production assets on purpose, chosen for their origin and bounds. Tracking them is recommended (a decision
  below).

## 10. What remains unresolved?

- **G2 (data loss).** A load that fails part-way leaves the engine half-replaced while `projectPath_` still
  names the previous project, and Cmd+S then overwrites it (`engine.cpp` ~3020-3258). Fixing it needs a
  transactional load, which is a design change.
- **G3 (data loss).** A missing `assets.scene` file only warns, and keeps the previous project's composition,
  so the next save writes A's scene path into B (`engine.cpp` ~2802-2819).
- **The paused editor hitch.** It is about 36 ms every ~380 ms while a project is unmodified (ADR-440, kept
  by the owner's choice in ADR-952).
- **GPU CI and the asset-bearing nightly are parked.** No GPU or Glowmere-tier verdict comes from CI.
  `Rebuild.mp3` is not in the private set; 41 CPU cases skip on CI without it.
- **55 CPU cases pass on CI having asserted nothing.** They return early without `assets/aliens`. A green run
  says nothing about them.
- **The two ASan-skipped cases** need sanitizer-sized variants before ASan can check them.
- **The new sanitizer layout has not run nightly yet:** the schedule runs from `main` only. `prune-caches.sh`
  has only been exercised for its listing call.
- **The ADR-893/894 height query is still 1.79x as expensive.** It is harmless while nothing reads
  `visibility`, but a route that names it brings back a cost linear in distance for that character.
- **Repository hygiene, listed and not changed:**
  - `examples/treeisland/renders/` holds 28 tracked PNGs (51 MB) under a path `.gitignore` excludes;
  - the 9 `_vx2-*` arms are the 9 remaining integrity problems, and their generator no longer runs;
  - about 40 other treeisland arms were not audited;
  - the farm GLBs are still in public history (`4bdc42ff`).
- **Dead data in every Glowmere scene:** unknown-parameter entries (`material/*/op/9/*`, `emissionIntensity`)
  and `lod.lodCount`. Removing them needs a GPU frame-hash proof. GV3's 12 duplicated effect values are
  unchanged.
- **Tests that write into `examples/world`** during a run (`test_glowmere_multicam*`,
  `test_motion_matching_default_off`) race between concurrent local agents.
- **Unverified by eye:** the scene-load error's status-bar text (no screenshot), and `--export-bundle`
  opening a window (not reproduced by launching the app).
- **For the art pass:** foot slip. The log warns about "0.02 m/s against a walk clip authored for 3.24 m/s".

## 11. What should be investigated next?

These are recommendations only; none was implemented.

1. **A transactional project load** for G2 and G3, the only open data-loss paths found.
2. **The paused dirty check:** a cheaper serialiser or a snapshot. ADR-952 records that a digest does not
   help, because the serialisation is the cost.
3. **The wide's scene pass:**
   - measure GV3 at 1080p rather than extrapolate;
   - then decide between content (scatter density and view distance on the wides) and renderer work
     (occlusion culling, entity LOD).
4. **Before anything consumes `character.<name>.visibility`:** budget or cache the sightline, or make the
   ADR-893/894 height query cheaper. Otherwise one route brings the wide collapse back for that character.
5. **The GPU creep:** narrow the +0.5 ms at the emission merge `7b030ac4` and the +0.4 ms in
   `3f720bfa..22ce5c3d`, if 1 ms of a 19 ms GPU frame matters.
6. **Once the nightly has run from `main` a few times:**
   - consolidate ASan to 3 jobs;
   - consider ASan at `-O1` if the margin shrinks;
   - write sanitizer-sized variants of the two skipped cases.
7. **Make the 55 early-return cases report as skipped,** so CI's count is honest about them.
8. **Stop tests writing into `examples/world`.**
9. **Fix `--ui-ab` ignoring `--start-at`,** if the navigation benchmark is used again.

---

## Bugs fixed

| symptom | cause | fix | validation |
|---|---|---|---|
| App segfaults on the frame after the assistant deletes or re-parents a node | `scene.delete_node` / `scene.set_parent` destroyed parameters without `Engine::rebind()`, so routes kept dangling pointers | Both now re-bind (`7b86e797`) | A release repro showed the SIGSEGV. `test_ai_tools.cpp` failed first |
| App terminates on a project with a wrong-typed known key (e.g. `"format": 1`) | `.value()` / `.get<T>()` threw `json::type_error` out of `loadProject` / `loadFile` | Function-try-blocks turn it into a load error. `opensADifferentProject` no longer uses `.value()` (`7b86e797`) | 4 of 6 damaged files threw. `test_project_malformed.cpp` |
| A project whose scene fails to load silently shows the orb scene | No error surfaced | `Engine::projectSceneError()`, plus a status-bar error ahead of the warning count (`9cdb68be`) | Reproduced CPU-side (routes 19/81 live). A test. The UI text was not seen |
| `--export-bundle` drops a scene's top-level `lightRig` | `bundleScene` rewrote only one location | It rewrites both (`9cdb68be`) | "Bundles carry a scene's top-level light rig": 3 assertions fail without the fix |
| `--export-bundle` without `--headless` opens the windowed app | The batch flags did not imply headless | `src/app/cli_batch.hpp` lists them, and `parseArgs` sets headless (`9cdb68be`) | An `[app][cli]` test. Not reproduced by launching the app |
| "Cancellation stops execution..." flaky on CI | One `pump()` drained the queue, so every scripted turn ran before the cancel | `pump(0.0)`, `REQUIRE_FALSE(finished())` before the cancel (`2e6c5bbb`) | 40/40 local passes |
| 9 CPU cases fail on CI after `4a138886` | The tests asserted on GV2 scenes whose farm animals are absent | The shared `skipUnlessFarmAssetsPresent()` guard, placed before any assertion (`2e6c5bbb`) | With the farm moved aside: 87 cases, 55 passed, 32 skipped, 0 failed |
| The manifest's sha256 for `glowmere-valley.wav` is wrong | Stale entry | Corrected. `generate-audio.sh` checks each score against it (`e9681785`) | Both WAVs regenerate and verify on CI |
| A sanitizer job could pass on an unexplained non-zero exit; heavy jobs showed false "exit 4" disagreements | `--gate sanitizer` ignored disagreements, and exit 4 (all skipped) was not recognised | `all_skipped()`, and the gate fails on any other disagreement (`8df83f4c`) | Replayed on 36434165586's artifacts and on synthetic statuses. 36469640514: 0 disagreements |
| Editor drops two frames ~2.6x/s while playing an unmodified project | ADR-440's dirty sample serialised the whole project (~36 ms) every ~380 ms | ADR-952: no periodic sample while playing; every discard still measures on demand (`1b74414c`) | Playing `ui.build` p95 35.4 to 0.86 ms, frame p95 50.4 to 17.7 ms. Suites exit 0 on the branch |

## Performance regressions

| revision | cause | fix | before / after |
|---|---|---|---|
| `29e6918e` (ADR-834) | Per-frame `heroSightline` for every in-shot character, to publish an unread `visibility`. Cost linear in distance x characters (r = 0.975) | ADR-950 (`edf62e2d`) + ADR-951 (`97925c18`) | GV3 s66: **376.2 to 32.2 ms** (2.7 to 31 FPS); engine update 347.7 to 3.1 ms. s24: 296.5 to 30.8. s26: 334.0 to 31.7. GV2 multicam wide: 64.2 to 27.7. Paused editor wide: 81.8 to 16.7 |
| `0b623b88` (`0fd83284`, ADR-893/894) | Each terrain height query dearer, so each sightline 1.79x | Neutralised by ADR-951 (no separate fix) | GV2 multicam engine update 21.7 to 38.8 ms; 3.3 ms after the fix (3.2 before ADR-834) |
| `ad5623d2` (ADR-945) | Ecology glow pools: fewer lights (206 to 115), each reaching twice as far | None: an accepted look change | Scene pass +1.3 ms (13.6 to 14.9). Gone with the ecology lights off |
| `0b623b88..22ce5c3d` | Shading cost of features the revision added; the largest single step is +0.5 ms at `7b030ac4` (ADR-903-906, emission) | None: each part is under 0.5 ms | Scene pass 12.2 to 13.4 ms with the ecology lights off |
| ADR-440 (by design, not a regression) | Whole-project serialisation on the UI thread for the dirty check | ADR-952, for playback only | See "Bugs fixed". Paused is unchanged |

ADR-950 alone took s66 from 376.2 to 235.4 ms, and 13 of 73 shots stayed under 10 FPS in the sweep. ADR-951
removed the rest.

## Decisions for the owner

Only what is still open. Already decided and not listed: ADR-951 (accepted), ADR-952 (skip during
playback), the runner's home (the private repository), the song (may go into the private repository), GPU CI
parked, and the aurora (art pass).

1. **ASan: 5 jobs or 3.**
   - Three jobs fit: about 3 h 20 min to 3 h 40 min against the 330 min ceiling.
   - They leave two macOS slots for push CI. On 2026-09-28 the five-job run queued pushes for up to an hour.
   - They need `catch2_run.py --part` to accept several parts. It is a scheduling choice. The recommendation
     is to decide after a few nightly runs from `main`.
2. **GPU CI steps 4-6** (parked): the deploy key and secret, `AVGEN_TEST_ASSETS` and `AVGEN_GPU_RUNNER`, and a
   self-hosted runner registered on the private repository.
3. **Wire `Rebuild.mp3` into the private set.** It needs the list, step 1 again, and the fetch script linking
   it to `~/Desktop/Rebuild.mp3` or `AVGEN_REBUILD_AUDIO`. It would un-skip 41 CPU cases.
4. **The farm GLBs in public history** (`4bdc42ff`). Removing them needs `git filter-repo` and a force-push,
   which breaks every clone and worktree. PROGRESS.md says you chose earlier not to, and lists it "for
   completeness". The GPU CI doc still lists it as open.
5. **G2 and G3, the partial-load data-loss hazards.** Accept them, or schedule a transactional load.
6. **The paused ADR-440 hitch** (~36 ms every ~380 ms). Accept it, or pay for a cheaper serialiser or a
   snapshot.
7. **DONE (see the addendum).** ~~Track the three CC0 Quaternius GPU fixtures (about 4 MB).~~ Tracked on
   `qa/tidy`: 12 files, 12.7 MB with their textures and the licence file. 16 CPU cases now run on every push.
8. **Repository hygiene: DONE, except the PNGs (see the addendum).**
   - remove the 28 committed treeisland PNGs (51 MB; this does not shrink history): **not done**, because four
     ADRs cite them (ADR-372 keeps them on purpose as a record). Still the owner's call;
   - ~~remove the 9 `_vx2-*` arms, plus a `.gitignore` rule~~: done. `check_project_integrity` reports 0 problems;
   - ~~whether to audit the other ~40 treeisland arms~~: audited. All 22 are referenced and load cleanly, and
     none was removed.
9. **`--ui-ab` ignores `--start-at`:** fix it if that benchmark will be used again.

## Success criteria (from the brief)

| criterion | verdict | why |
|---|---|---|
| The Glowmere projects are integrated, and GV3 is reproducible from repository state | **Met** | r7b is in `examples/world/`. It loads with 0 errors (105/105 routes, 130/130 tracks, 18/18 effects, 73 shots). Main's generator reproduces it byte-identically, apart from the relative song path. The song and the licensed models are excluded, and the reason is documented |
| The causes of GV3's performance are understood | **Met** | A verdict on each of A, B and C, backed by measurement, a profile, r = 0.975, bisection, and one-at-a-time GPU attribution |
| Real project-state problems are cleaned up, and demonstrable regressions and important bugs are fixed | **Partly met** | Both CPU regressions are removed from the frame, and the ten fixes in "Bugs fixed" each have a test or a measured validation. Stale arms are gone. But the G2/G3 data-loss hazards are open, and GV3's duplicated values and the family's dead data are listed, not removed |
| CI is materially cleaner | **Met** | Every red run was triaged. One build action, one local==CI script, cache discipline, an exceptions file with no exclusions (its 27 entries are reported `needs-assets` cases), an honest sanitizer gate. Push CI is green |
| ASan, UBSan and TSan (where appropriate) run in CI | **Met** | All three have run with 0 reports: ASan over the whole plan, UBSan over 3838/3838, TSan over its subset. Whole-suite TSan was measured and does not fit, which is documented. The nightly runs from `main` start after the merge |
| GPU CI works through the private-asset strategy or the best documented alternative | **Partly met** | Built, documented, the private repository published and pinned. Parked by the owner before the secret and the runner, so no GPU verdict comes from CI. Hosted GPU is informational (363/443 fail) |
| Fixtures are minimal where practical, and nightly validation runs without the owner's machine | **Partly met** | The asset set was measured down to 202 files. The CPU suite and all three sanitizers run nightly on hosted runners. But the Glowmere-tier and GPU nightlies are gated off until steps 4-6 |
| A progress report and a final report exist, and no features were added | **Met** | `PROGRESS.md` and this file. The QA changes are fixes, tests, diagnostics and CI. The astronaut prototype on this branch was a separate, owner-requested task, with no engine change |

## Final validation (merged head)

> **PLACEHOLDER: the coordinator fills this in.** Results of both suites and the build on the merged
> `qa/coord` head, from `build/qa-runs/final/summary.txt` and the logs beside it (`build.log`,
> `configure.log`, suite logs). Record for each binary: the commit, the exit code, cases passed / skipped /
> failed (the one `[!shouldfail]` expected), and the assertion count.

| binary | commit | exit | cases | passed | skipped | failed | assertions |
|---|---|---:|---:|---:|---:|---:|---:|
| `avgen_tests` | | | | | | | |
| `avgen_render_tests` | | | | | | | |

## Where the sources disagree

- **s66 before and after.** PROGRESS.md and ADR-951's table say 402.8 to 33.4 ms (2.5 to 30 FPS).
  `perf-summary.md` and `perf.md`'s clean retake say 376.2 to 32.2 ms. The first figures were taken while
  other agents' suites were running. This report uses the clean retake. PROGRESS.md's interim "30-43 FPS at
  720p on every measured shot" comes from the same contended take.
- **Regression step 2.** PROGRESS.md (interim) says "about 1.7x ... NOT bisected, suspect ADR-893". `perf.md`
  bisected it to `0fd83284` at 1.79x, and `perf-summary.md` rounds that to 1.8x. This report uses `perf.md`.
- **The GPU step.** PROGRESS.md says "+2.5-3 ms (+18%)". `perf.md` says +1.9 ms (+11%), and explains that
  the +18% was measured under contention. Its two parts (+1.3 ms from ADR-945 and +1.1 ms of creep) sum to
  2.4 ms, because they come from different baseline builds and arms. `perf.md` does not reconcile them
  exactly.
- **"No duplicate or dead state."** `perf.md` and `perf-summary.md` say the static audit found none.
  `gv3-state-audit.md` lists 12 duplicated effect values, a shadowed scene effect list, and inherited dead
  keys. They agree on what matters: none of it has a frame cost. `perf.md`'s claim refers to its own script's
  checks (duplicate routes, tracks and node names, and dead references).
- **The TSan subset.** `docs/development/ci.md` records 226 cases in 27 min (run 36060803359, earlier) and 243
  cases in an 82 min job (36434165586). PROGRESS.md says "243 cases, 40 min", which is the test time inside
  that 82 min job.
- **Exclusions in `docs/development/ci.md`.** Its Sanitizers section says no case is excluded by name any
  more. Its older "Coverage gaps" census, partition section and Findings still describe two crash exclusions,
  including "the `json.hpp` assert ... is still excluded". `ci.md` (W2) and PROGRESS.md say the exceptions
  file is empty. The later text appears correct, and the older passages are stale.
- **The private set's size.** The repository at `61ff6dd` is 248 MB. W2's notes say 249 MB. The tier table
  (65 + 44 + 152 MB) sums to 261 MB, and the GPU CI doc says "about 110 MB without the island and 260 MB with
  it".
- **The push-gate time.** PROGRESS.md says about 8 min, down from 22. W2's status gives 6 min for the CPU job
  (run 36464437262).
- **The removed arms.** PROGRESS.md says "nine `_diag-water-*` arms and `_pre-defects`". Commit `bc2c1939`
  calls them eleven (nine files plus `_pre-defects` and its scene).

## Addendum: the owner's tidy decision

2026-09-28, after this report. The owner decided: "Track those assets, tidy up repo if needed." The work is
on `qa/tidy`, branched from `qa/coord` `9dc9a06c`.

- **Decision 7: done. The three CC0 Quaternius fixtures are tracked.**
  - The files: `CommonTree_1`, `Rock_Medium_1` and `Mushroom_Common` (`.gltf` + `.bin`), plus the five
    textures they reference (`Bark_NormalTree`, `Bark_NormalTree_Normal`, `Leaves_NormalTree_C`, `Rocks_Diffuse`,
    `Mushrooms`) and the pack's `License_Standard.txt`. That is 12 files and 12.7 MB, byte-identical to the
    owner's checkout, with one `.gitignore` negation per file.
  - **The size was misstated.** "About 4 MB" left out the textures. The meshes alone are 0.53 MB.
  - **The licence** is CC0 1.0, by the pack's own `License_Standard.txt`, `assets/imported/ATTRIBUTION.md`
    ("gitignored for size, not for licence") and `assets/manifest.json`. ADR-542 says that Quaternius "may be
    embedded but not redistributed". The same sentence calls the purchased alien and farm packs CC0, so it is
    not a reliable record.
  - **The effect:** 16 CPU cases that skipped on a clean checkout now pass. They are in
    `test_world_navigation` (6), `test_visibility_culling` (3), `test_composition` (2),
    `test_ecology_light_selection` (2), `test_lod_ladder`, `test_mesh_lod` and `test_emission_lanes`. This
    was measured by running `run-suite.sh cpu` without the untracked assets, with and without the fixtures:
    3601 passed and 226 skipped before, 3617 passed and 210 skipped after.
  - **It exposed one test defect,** now fixed (`e568d2c4`). The second section of `test_lod_ladder` asserted
    whole-ladder bounds over whichever layers were present, so with three layers present it failed.
  - `tools/ci/test-assets.list` loses 11 files. Tier 1 is now 52 files, 52.0 MB. The GPU CI doc's table is
    corrected: the pinned repository's "248 MB" is 260.5 MB counted in MiB.
  - **The private repository was not touched.** At `61ff6dd` it still holds the 11 files.
    `tools/fetch-test-assets.sh` would have refused the whole fetch over them, because they are no longer
    ignored paths. It now skips a path this repository tracks, and the fix was tested offline. The 11 files
    drop out when the set is next rebuilt, which also needs a new lock sha.
  - **The hosted-runner exceptions are unchanged.** Both Quaternius entries need the rest of the pack.
- **Decision 8: done, except the PNGs.**
  - **The 9 `_vx2-*` arms are removed** (648 KB), with a `.gitignore` rule. Nothing loaded or named them.
    `check_project_integrity.py` now prints "9 project(s) carrying effect parameters, 12 fingerprinting a
    scene / 0 problem(s)" and exits 0. Their generator, `tools/make_vortex2_arms.py`, was kept, but it still
    does not run against today's deliverable.
  - **The 28 treeisland PNGs (51 MB) are NOT removed.** Four ADRs cite them. ADR-393 and ADR-450 name
    individual images as evidence. ADR-372 says the `cosmickey/` frames are kept deliberately as the record
    of ADR-358's investigation. ADR-390 is the fourth. Removing them from the tip would leave those citations
    pointing at nothing. To remove them anyway, repoint each citation at a commit that holds the image (for
    example `git show dab170b1:examples/treeisland/renders/...`). This is the owner's call. Removing them
    would not shrink history either way.
  - **The treeisland arms are audited.** 22 arms remain beside the 3 deliverables. Each one:
    - is named by a generator (`make_cosmickey_arms.py`, `make_treeisland_arms.py`), an ADR (358, 371, 372,
      390, 339, 900), `renders/treeisland/stills.json` or a test;
    - loads with `avgen --project <p> --audit-routes` at rc 0 with 0 errors. The warnings are the same
      class as the deliverable's own: unknown `shader/glowmere-cosmos/*` parameters and a dead hero pulse.

    None is clearly dead, so none was removed. `_ca-vg-g1/g2` were committed deliberately (ADR-371: "so the
    next pass starts from them"). `_ck-before-*` and `_compare-original` are hand-written records of a
    "before" state.
- **`docs/development/ci.md` now gives one account of the exclusions** ("Where the sources disagree", item
  6). `hosted-runner-exceptions.txt` has **0 `exclude` and 27 `needs-assets` entries**. The json.hpp case
  was retired in `e9681785`. So "the exceptions file is empty" (above, and in PROGRESS.md) is true of
  exclusions only.
- **Validation on `qa/tidy`:**
  - `cmake --preset release` then `--build` succeeded.
  - `avgen_tests` exits 0: 3854 cases, 3834 passed, 19 skipped, 1 failed as expected (the `[!shouldfail]`
    at `test_character_lab_slopes.cpp:187`).
  - `avgen_render_tests`, run under `tools/gpu-lock.sh`, exits 0: 549 cases, 548 passed, 1 skipped.
  - `run-suite.sh cpu` without the untracked assets exits 0: 3617 passed, 0 failed, 210 skipped,
    26 needs-assets, 1 expected failure.
