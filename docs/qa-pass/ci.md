# W2: CI, sanitizers, GPU CI and test assets (notes)

Agent 2's working notes for the QA pass. Branch `qa/ci`, worktree `../av-gen-qa-ci`. Kept current so a
successor can resume from here. The plan is `PLAN.md` (W2); the brief is `00-brief.md`.

## Status
2026-09-28, end of Agent 2's session. All work is committed and pushed on `qa/ci` (see the hand-back for
the sha). Push CI is GREEN on `qa/ci` twice (36434148550 on `2e6c5bbb`, 36437357981 on `7b86e797`:
3576 passed, 0 failed, 240 skipped, 0 excluded, 8m 11s). Nothing is merged; the coordinator merges.

## Resume here (for a fresh CI-monitor agent)
**Runs still in flight when this session ended** (all on `qa/ci`; results stay on GitHub):

| Run | What | Expected |
|---|---|---|
| 36434165586 | Sanitizers, `sanitizer=both` on `2e6c5bbb` (the OLD 7-shard ASan matrix; dispatch "both" = asan+ubsan+tsan) | TSan PASS (done: 243 cases, 0 reports, 11 timing assertions not gated, 40 min). ASan heavy-2/heavy-3 PASS (done; heavy-3's 1/7 rest shard = 32 min). Still running: heavy-1 (the non-farm writeback/benchmark cases, estimated ~3 h), rest-a/rest-b (3/7 shards each), and UBSan (whole default set, one job: started 14:12 UTC, 40 min build; RISK: it may hit the 330 min test ceiling -- if it TIMEOUTs, split UBSan into two jobs of 3 shards via `tools/ci/run-suite.sh`, e.g. an `ubsan:a/b` split like asan's). Any `SANITIZER FAILURE` is a real finding: triage it. |
| 36434180499 | TSan over the WHOLE default set (`test_filter='~[.]'`), a measurement | Probably TIMEOUT at 300 min, which answers "can full TSan run nightly in one job": no. If it finishes, read its duration and reports; if it has race reports, those are real. Either way record the result in docs/development/ci.md "TSan subset". |
| 36444149446 | CI on `f987998c` (then later pushes) | green, like the two before. |

**Then:**
1. Once 36434165586 ends: fill docs/development/ci.md "Timings" with the measured ASan/UBSan/TSan job
   times, and check the rebalanced ASan matrix (committed after that run started: 11 rest shards,
   `tools/ci/run-suite.sh` `asan_job`) with a fresh dispatch: `gh workflow run sanitizers.yml --ref qa/ci
   -f sanitizer=asan`. With the farm films skipping, the 5 ASan jobs may be consolidatable to 3.
2. The nightly schedule only runs from `main`, so the new sanitizer layout first runs nightly after merge.
3. Unverified: the private-asset jobs (`cpu-assets`, `gpu`, the private-repo workflow) have never run on
   GitHub -- they are gated off until the owner acts (docs/development/gpu-ci-private-assets.md). The
   scripts were tested offline end to end. `tools/ci/prune-caches.sh` has only been exercised for its
   listing call; it first runs for real on a `main` report job.
4. Unverified: the asset-free delete regression was not shown failing against the pre-fix code.
5. Coverage gaps left for the owner/W3 (below): G2 (a failed load half-replaces the project and a save
   then overwrites the previous file) and G3 (a missing scene file keeps the previous composition).

## 1. Triage of the red runs

Artifacts were downloaded with `gh run download <id> -n test-results-cpu` (and `-p 'sanitizer-logs-*'`);
the per-case evidence is in each artifact's `summary.md` and `shard-N.xml`.

| Run | Commit | Job | Failure | Class | Resolution |
|---|---|---|---|---|---|
| 36421624102 | `4a138886` | CPU | 9 cases (abduction film, delete-while-running, glow, rig count, multicam walk) | **asset absence** | skip guards on `qa/ci` (below) |
| 36422061644 | `77ea4247` | CPU | the same 9, plus `Cancellation stops execution and rolls back deterministically` | asset absence + **flaky test** | guards; the race fixed (below) |
| 36354090309, 36341601980, 36318924797 | `983221a9`, `876a11e2`, `83a12334` | CPU | `a triggered Lightning and Discharge on a moving owner are the same played and scrubbed` | **real defect** (test compared uninitialised padding bytes) | already fixed on main by ADR-940 (`8c71edbe`); green on `22ce5c3d` and `ad5623d2` |
| 36321265640 (and 36241405408) | `83a12334` | ASan/UBSan stages 0, 1, 5, main | 5 TIMEOUTs (330 min ceiling) + one UBSan report | **infrastructure** (partition) + **real defect** | the partition was already rewritten (`tools/ci/sanitizer-plan.txt`, heavy-1..3/rest-a/b) but has not run yet; the UBSan report was fixed by ADR-941 (`d96ad60d`) |

### The farm-asset family (asset absence)
`4a138886` removed the purchased farm GLBs from the tree. Both Glowmere Valley 2 projects load them (GV2:
8 farm files; the multicam: 3), so on the runner the animals are nodes with no mesh and no rig, and these
cases asserted on a scene that is no longer the scene:

- `test_abduction_sequence.cpp`: `the film runs five complete abduction cycles` (2 cycles, not >= 4), `the
  abducted animal fades out before it is hidden`, `... most of the way up before it starts to dissolve`,
  `the fade reaches the flattened scene and is given back`, `the director publishes the state an overlay
  needs`;
- `test_abduction_alignment.cpp`: `The abducted animal lights up, and the glow is spent before it arrives`;
- `test_delete_nodes.cpp`: `deleting animals while the scene runs does not crash` (no animal nodes);
- `test_multi_character.cpp`: `the shipping scene carries one copy of the alien per alien` (0 rigs);
- `test_glowmere_multicam_defects.cpp`: `The film's five characters all walk, at more than one seed`
  (two bodies under 20 m).

Other cases in these files already skipped on `farmAssetsPresent()`, which four files each defined for
themselves.

### The cancellation race (flaky test)
`test_ai_control_plane.cpp`, "Cancellation stops execution and rolls back deterministically": the state was
`Failed` (9), not `Cancelled` (10), with the rollback done, in 0.196 s. `Failed` with `cancelled == false`
has one route: the scripted provider ran out of its nine turns. The test cancels once `toolCallsSoFar()`
is non-zero, but it services the main-thread queue with `plane.pump()`, whose default budget drains the
queue until empty. On a loaded 3-vCPU runner the worker woken by `done_.notify_all()` counts the call,
asks the provider and enqueues the next tool call before the pumping thread re-checks the queue, so one
`pump()` call can run every scripted turn. The test then cancels a task that has already failed. The
product behaviour is right (a frame may service several queued calls); the test's assumption "one pump
lets one turn land" is what was wrong.

### Sanitizer run 36321265640
- Timeouts: stage-0 (`the director publishes ...`), stage-1 (`The pre-ADR-262 scenario ...`, `the UFO
  abducts several animals ...`), stage-5 (`A lifted animal is drawn on the beam's axis ...`) and main
  (`The film's five characters all walk ...`). All are long single-process simulations; the rewritten
  plan places or skips each of them.
- `main` also listed 11 assertion failures: wall-clock ceilings (`[performance]`) that a Debug -O0 ASan
  build cannot meet. Not gated, by design (`--gate sanitizer`).
- The one sanitizer report: `atmospherics.cpp:698 invalid-bool-load` in `packComet`. The test helper
  returned a record pointing into a dead local array (stack use after return). Fixed by ADR-941. ASan did
  not catch it because `detect_stack_use_after_return` was off; see the sanitizer section.

## Fixes made (qa/ci)
- Farm guard: `testsupport::skipUnlessFarmAssetsPresent()` in `tests/support/project_assets.hpp` (all nine
  GLBs), replacing four per-file `farmAssetsPresent()` copies; added to the nine failing cases. Gotcha: a
  SKIP thrown inside a `REQUIRE(...)` expression is reported as FAILED ("unexpected exception ... nested
  SKIP()"), so guards go before any assertion that calls `film()`/`cycles()`.
  Verified with the farm links moved aside: `[abduction],[beam],[poc],[delete],[multi],[stage]` = 87 cases,
  55 passed, 32 skipped, 0 failed, exit 0; with the farm present the guard passes.
- Cancellation race: `plane.pump(0.0)` (one item per pump) in the wait loop; `REQUIRE_FALSE(finished())`
  before cancelling; `CHECK(toolCallsSoFar() < 9)`. 40/40 local passes.
- Scores: `tools/ci/generate-audio.sh` (both WAVs in ~50 s in parallel, verified vs manifest). Manifest
  sha for glowmere-valley.wav was stale -> fixed. Night-shift cases skip without the WAV; all three
  crash exclusions removed (exceptions file now excludes nothing).
- Asset-free delete regression: `test_delete_nodes.cpp`, "deleting a held animal from a running set
  piece does not crash, without any asset" (set-piece lab; roles are `target1..N`). NOT demonstrated
  against the pre-fix code.

## Test asset audit (deliverable 5)
**Method (repeatable).** `tools/ci/opentrace.c`: a dylib interposing `open`/`fopen` (read opens only),
logging paths under `/assets/`, `Desktop/` or with a media extension to `$OPENTRACE_OUT`. Build:
`clang -dynamiclib -O2 -o opentrace.dylib opentrace.c`. Run: `env DYLD_INSERT_LIBRARIES=... OPENTRACE_OUT=...
./build/release/tests/<binary> ...`. GOTCHAS: SIP strips `DYLD_*` when a protected binary is exec'd, so put
`env` LAST before the test binary (`tools/gpu-lock.sh env DYLD...=... ./bin`, `nice -n 10 env ... ./bin`;
`env ... nice ./bin` silently traces nothing). Loaders canonicalise symlinks, so map opens under BOTH the
worktree and the primary checkout back to `assets/<rel>`.

**Result.** GPU suite (549 cases, 19 min, under the lock): 5 aliens, 3 farm, 55 Quaternius (CC0) +
glowmere-valley.wav (generated) + ~/Desktop/Rebuild.mp3. The 280 CPU cases CI skips/fails for assets
(3 niced shards): + 1 alien, 6 farm, 55 Quaternius, 55 Kenney city, 15 nature, 1 HDRI, 6 treeisle (152 MB),
night-shift.wav (generated), ~/Desktop/MP3/bass.mp3. -> `tools/ci/test-assets.list` (202 files, 249 MB;
~110 MB without the island). With assets: CPU 263 pass / 17 skip / 0 fail; GPU 548 pass / 1 skip / 0 fail.
The 17 skips need motion packs absent even on the owner's machine: NO machine runs them.

**Tiers.**
- unit: tiny fixtures already dominate (tests/data, gltf_fixture.hpp, tracked examples). New: the
  set-piece lab now also carries the delete-while-running ASan regression.
- integration: tracked example projects (procedural); the repository's two scores are generated on CI.
- GPU: CommonTree_1 / Rock_Medium_1 / Mushroom_Common are deliberately production fixtures (§29 in those
  tests: chosen for origin/bounds properties); CC0, ~4 MB -> recommend tracking them (owner).
- Glowmere validation: GV2 / multicam films (aliens, farm, Quaternius, song) -> cpu-assets / private repo.
Replaced with fixtures: the delete regression (asset-free twin). Not replaced: the abduction films (their
subject IS the shipped film; a fixture version would test a different film).

## Coverage gaps (deliverable 6)
Survey by a read-only agent, then each finding verified by a failing test before any fix:
- **G5, FIXED (crash).** `scene.delete_node` / `scene.set_parent` (src/ai/engine_tools.cpp) destroyed a
  node's parameters without `Engine::rebind()`; routes/tracks kept raw pointers to freed memory. Release
  repro: dangling `targetParam` and SIGSEGV on the next frame. Now both re-bind. Test: test_ai_tools.cpp
  "Deleting or re-parenting a node through the assistant re-binds the routes that drive it".
- **G1, FIXED (crash).** `Engine::loadProject`/`loadFile` used `.value()`/`.get<T>()`, which throw
  `json::type_error` on a wrong-typed key; uncaught -> terminate. 4 of 6 damaged files threw
  (`format: 1`, numeric scene kind, numeric `control.tempoSource`, string shot-span time). Function-try-
  blocks turn it into a load error; `Application::opensADifferentProject` no longer uses `.value()`.
  Test: tests/integration/test_project_malformed.cpp.
- **G2, NOT fixed, for the owner/W3 (data loss).** A load that fails part-way (layers, params, control,
  timeline, songPlan, states, director: engine.cpp ~3020-3258) leaves the engine half-replaced while
  `projectPath_` still names project A; `Application::performOpen` only shows a status line, so Cmd+S
  writes the half-loaded B over A. Needs a transactional load: a design change, not a QA fix.
- **G3, NOT fixed (data loss).** A missing `assets.scene` file only warns (engine.cpp ~2802-2819) and keeps
  the PREVIOUS project's composition installed; the next save writes A's scene path into B.
  Related to W3's "a project whose scene fails to load falls back ... with no visible error".
- **G4, low risk.** `outputs` has no round-trip test (dropped silently if not an array). Not added.
- Invalid references: already well covered (route liveness, camera collections, staging refusals).

## Workflow structure (after)
- `ci.yml`: build -> cpu-tests (gate) | gpu-hosted (informational, main/nightly/dispatch) | cpu-assets
  (gate; nightly/dispatch; `vars.AVGEN_TEST_ASSETS`) | report (+ cache prune on main). `gpu` job:
  self-hosted `[self-hosted, macOS, ARM64, avgen-gpu]`, `vars.AVGEN_GPU_RUNNER`, push-to-main/schedule/
  dispatch only.
- `sanitizers.yml`: asan-build -> asan-tests x5 | ubsan (own preset, whole default set, halt_on_error=1)
  | tsan (nightly now) | report (+ prune).
- Shared: `.github/actions/cmake-preset` (NB: `.gitignore` has `build/` and `build-*/`, which swallowed
  `.github/actions/build*`), `tools/ci/run-suite.sh` (every suite; local == CI), `tools/ci/prune-caches.sh`.
- Caches saved from main only; before: 24 entries, 10.7 GB vs 10 GB quota, TSan cache evicted.
- actionlint clean (config `.github/actionlint.yaml` declares the `avgen-gpu` label).

## Private assets / GPU CI
- Design and owner steps: `docs/development/gpu-ci-private-assets.md`. Key security point: a fork PR runs
  the workflow file FROM the PR, so a job `if` cannot protect a self-hosted runner on a public repo;
  recommended: register the runner on the private asset repo (template workflow
  `tools/ci/private-repo-gpu-workflow.yml`), or require approval for fork PR workflows.
- Scripts tested offline end to end (build repo from a list, link into a fresh clone, git sees nothing;
  error paths: missing secret, unpublished lock, tracked path in list).
- Owner decisions: the song in the private repo or not; runner placement; farm GLBs still in public
  git history (`4bdc42ff`), removal needs a history rewrite.

## Log
- 2026-09-28: triage; fixes; workflow restructure; private-asset tooling; asset trace and list;
  G1/G5 crash fixes; ASan matrix rebalance; session ended with sanitizer runs in flight.
