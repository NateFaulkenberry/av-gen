# Continuous integration (GitHub Actions)

GitHub-hosted runners are this project's **remote build and test machine**. Push a branch and they
build it and run the full regression suite, so the developer's Mac doesn't have to.

> **Do not repeatedly run the entire AV Gen test suite locally merely to verify ordinary changes
> when GitHub Actions can perform the authoritative full regression run.**

Local testing is still the right tool for:

- rapid iteration on one tag;
- debugging, and investigating a specific failure;
- tests that need the local GPU, the local assets or a debugger;
- reproducing a CI-specific failure.

## The loop

1. Make the change.
2. Run targeted local tests only when they help, e.g. `./build/release/tests/avgen_tests "[ai]"`.
3. Commit.
4. Push the branch: `git push -u origin <branch>`. The push starts CI.
5. Wait for it: `gh run watch $(gh run list --branch <branch> --limit 1 --json databaseId --jq '.[0].databaseId') --exit-status`.
6. Read the result (commands below). The run summary's `## Failures` section names each failing
   test with its expected value, actual value and source line.
7. If it failed:
   - take the **first meaningful** failure;
   - read that test and the source it covers, and the log if the summary isn't enough;
   - fix it, commit, and push again. A new push cancels the stale run on the same branch.
8. Repeat until green.

## Local vs remote

| Local (your Mac) | Remote (GitHub Actions) |
|---|---|
| targeted tag runs while iterating | the full regression suite, both binaries |
| debugging a failure, lldb, Instruments | the ASan/UBSan suite (nightly, or on dispatch) |
| interactive renderer checks, screenshots | TSan over the concurrency subset (weekly, or on dispatch) |
| tests that need `assets/` (see Coverage gaps) | repeat verification after every push |
| `[.perf]` probes and GPU timing | pre-merge verification of a branch |

## Commands

```sh
# Latest run on a branch: the one-liner for "is my branch green?"
gh run list --branch <branch> --workflow CI --limit 1

# Follow it live; exits non-zero if the run fails
gh run watch <run-id> --exit-status

# Job results and the step that failed
gh run view <run-id>

# Only the failing steps' logs. catch2_run.py prints each FAILED test, CRASH or
# SANITIZER FAILURE here, with the source line and "with expansion:".
gh run view <run-id> --log-failed

# The summary page (the "# AV Gen CI" report) in a browser
gh run view <run-id> --web

# Machine-readable results: per-shard Catch2 XML, JUnit, full logs, result.json
gh run download <run-id> -n test-results-cpu -D /tmp/ci-cpu
jq '.failures[] | {name, file, line}' /tmp/ci-cpu/result.json

# Re-run only the failed jobs (e.g. after a runner hiccup)
gh run rerun <run-id> --failed

# Request runs without a commit
gh workflow run ci.yml --ref <branch>                          # the full CI
gh workflow run ci.yml --ref <branch> -f test_filter='[ai]'    # one tag, remotely (labelled FILTERED)
gh workflow run ci.yml --ref <branch> -f rng_seed=12345        # replay a run's test order
gh workflow run sanitizers.yml --ref <branch> -f sanitizer=asan   # asan | tsan | both
```

`workflow_dispatch` only works for a workflow file that exists on the default branch. Until this
lands on `main`, dispatch is unavailable and CI starts on push only.

## Workflows and jobs

### `CI` (`.github/workflows/ci.yml`): on every push, fork PRs, and dispatch

```
push ─▶ Build (Release) ─┬─▶ CPU tests (avgen_tests, full)
                         ├─▶ GPU tests (avgen_render_tests)
                         └─▶ Report  (writes the "# AV Gen CI" summary)
```

- **Build (Release):**
  - runs `cmake --preset release` then `cmake --build --preset release`, building every target:
    engine, tools and both test binaries;
  - uses the repository's presets unchanged; the only CI addition is ccache as the compiler launcher;
  - `-k 0` makes one run report every compile error;
  - failures show as annotations and in the summary, and the full log is the `build-logs` artifact.
- **CPU tests:**
  - `avgen_tests`, the whole default Catch2 set, which is what `ctest -L unit` runs;
  - split into 3 Catch2 shards (`--shard-count`) that run in parallel on the runner's 3 cores;
  - every shard uses the same `--rng-seed`, so the shards partition the set. The script checks that
    **ran == listed**, so a shard that dies early cannot turn into a quiet subset.
- **GPU tests:**
  - `avgen_render_tests` in one process (one GPU; see `RESOURCE_LOCK` in `tests/CMakeLists.txt`);
  - then the headless smoke test (`avgen --headless --example Hyperspace --frames 1`), the same
    command ctest registers as `gpu.the-binary-renders-a-frame-headless`.
- **Report:** a Linux job that merges each job's `result.json` into the run summary.

A same-repository pull request doesn't run CI a second time: its branch push already did, and
GitHub shows those checks on the PR. Only a PR from a fork gets its own `pull_request` run, with
read-only permissions and no secrets.

**Branch protection caution.** A same-repository PR also gets a `pull_request` run, and all of its
jobs are *skipped* (verified on PR #1). Those skipped checks carry the same names as the real
ones, and GitHub treats a skipped required check as passing. So if required checks are ever
configured, make sure they cannot be satisfied by the skipped `pull_request` run: for example,
require the `Report` result, or drop same-repo PRs from the trigger.

**Concurrency.** A new push to a branch cancels the in-flight run for that branch. On `main`,
nothing is cancelled.

**No path filters.** `docs/` is a test input:

- `test_repo_hygiene` reads `docs/decisions/README.md` and `docs/dependencies.md`;
- `test_help` reads `docs/help/`;
- `test_material_program` parses `docs/procedural-materials.md`.

So a docs-only push can break the suite. To skip a run you know is pointless, put `[skip ci]` in
the commit message.

### `Sanitizers` (`.github/workflows/sanitizers.yml`): nightly on main, and dispatch

- **ASan/UBSan:**
  - `--preset asan`, which is Debug + `-fsanitize=address,undefined` on engine targets and tools,
    built once;
  - the CPU suite runs in a matrix: everything except `[stage]` in 3 shards on one runner, and
    the 66 `[stage]` simulation cases in 12 shards over four runners. Nothing is left out.
  - ASan halts at its first report. UBSan reports every site and carries on, but any
    `runtime error:` line fails the job.
  - Each report appears in the summary as `SANITIZER FAILURE`, with its type, first frame and a
    stack excerpt.
- **TSan:** `--preset tsan` over the concurrency subset (below). Runs weekly (Sunday) and on dispatch.
- **Capacity:** a sanitizer run uses up to 5 macOS jobs at once (4 `[stage]` parts plus `main` or
  TSan). That is the whole free-account macOS concurrency, so push CI queues behind it. That is why
  it runs at night (07:17 UTC), and why a dispatch during working hours will delay everyone's
  pushes.

## What the categories mean

| Category | Where | What runs |
|---|---|---|
| FULL | `CI` on every push | build of every target; all default `avgen_tests` cases; all default `avgen_render_tests` cases; the headless binary smoke test |
| SANITIZER | `Sanitizers` nightly / dispatch | ASan+UBSan over the default CPU set, minus the documented exclusions |
| EXTENDED | `Sanitizers` weekly / dispatch | TSan over the concurrency subset |
| targeted | `CI` dispatch with `test_filter` | one tag on both binaries, labelled `FILTERED` in the summary; never a substitute for FULL |

There is no separate "fast" category. The suite has no fast/slow tag split to build one from, and
a warm FULL run is quick enough to be the per-push check (numbers below).

Catch2's hidden tests (`[.perf]`, `[.probe]`, `[.bench]`, `[.report]` and the rest) are not run by
CI. They aren't run by `ctest` either: `catch_discover_tests` never sees them. They are wall-clock
probes and investigations, meant to be run deliberately and alone, and a shared VM is the wrong
place to time them.

## Reading a result

The run summary (the run page, or `gh run view --web`) has these sections:

- `# AV Gen CI`
- `## Build`: PASS or FAIL, plus the runner, macOS, arch, compiler, Xcode, SDK, CMake, timings and
  cache state.
- `## Tests`: one row per binary with passed, failed, skipped, expected failures, ran/listed and
  duration, then the top skip reasons.
- `## Jobs`: each job's result.
- `## Failures`: one `###` block per failing test with the source, `expected:` (the assertion as
  written) and `actual:` (Catch2's expansion). A crash is a `CRASH: SIGSEGV` block naming the test
  that was running. A kill is `TIMEOUT`. A sanitizer report is `SANITIZER FAILURE`.
- `## Artifacts`

The verdict rules come from docs/testing.md:

- **The binary's own exit code is authoritative** (entries 5, 11 and 18). Every shard's exit code
  is read straight from the process, not through a pipe. An exit code that disagrees with Catch2's
  XML is reported as a disagreement, never resolved as a pass.
- **A `[!shouldfail]` case is an expected failure, not a failure** (entry 8).
  `test_character_lab_slopes.cpp:187` prints a `FAILED:` block on every healthy run. The summary
  counts it under "Expected failures", and Catch2 exits 0 for it.
- **A killed run is not a test failure** (entry 4). Catch2 prints `FAILED:` with no
  `with expansion:` when it is SIGTERMed. CI reports that as `TIMEOUT` or `CRASH`, with the case
  that was running, which comes from the streaming XML.
- **A missing verdict is not a pass** (entry 11). A shard whose XML never closed is a crash.
- **A selection is never a name.** Filters are tags only (entry 14: a name with a comma splits
  into two specs). An empty selection fails.

## Coverage gaps: what "CI green" does NOT mean

**Assets.** The gitignored asset packs are not on the runner. They are not uploaded anywhere: not
to caches, not to artifacts, not to LFS. Publishing them is the owner's decision (see follow-ups).
The missing packs:

- `assets/aliens`, `assets/quaternius`, `assets/nature`, `assets/terrain`
- `assets/hdri/*.hdr`, `assets/environments/*`, `assets/audio/*.wav`
- `assets/kenney/city`, `assets/imported/concert`, `assets/treeisle`
- `~/Desktop/*.mp3`

Tracked on the runner: `assets/farm`, `assets/imported/{alien,ufo}.gltf`, and the manifests. The
suite behaves in four ways without the packs (census 2026-09-24):

| Behaviour | Cases (run 36066934636) | What the summary shows |
|---|---|---|
| explicit `SKIP` | 149 of the CPU suite's skips; almost all are asset skips (e.g. 45× "the Glowmere alien is not present", 18× "glowmere-valley.wav is generated") | counted as skipped, grouped by reason under "Skipped cases by reason" |
| early return that passes having asserted nothing | 55 (static census) | counted as **passed**; CI cannot tell. Listed below |
| degraded scene, still asserts something | many | passed, with reduced meaning |
| fails for lack of the asset | 29 | 28 run and are reported as **"Failed: needs local assets"** (not a pass and not gating); 1 is excluded because it segfaults |
| crashes for lack of the asset | 2 | **excluded** by name; each is named in every summary |

`tools/ci/hosted-runner-exceptions.txt` lists every case in the last three rows, with the asset it
needs, taken from observed CI failures. A needs-assets case that starts passing is flagged as a
stale entry. A new failure that is not on the list fails the job, as it should: add it to the
list only with a run URL showing the asset is the cause.

Two of these are **test defects the runner exposed**, left for the owner:

- `test_body_compensation.cpp:433` calls `nodes().front()` on an empty composition when
  `alien-scout.glb` is absent, and segfaults instead of failing or skipping.
- "sequence.get_state reports the music video, not the timeline" (`test_ai_tools.cpp:780`) fails
  in Release without `night-shift.wav`, and aborts (SIGABRT) in the Debug sanitizer builds.

The pass-without-asserting group has 55 cases, all guarded on `assets/aliens/*.glb`:

- `test_phase_d_autonomy.cpp` (18)
- `test_character_lab_layers.cpp` (8)
- `test_entity_perception.cpp` (6)
- `test_character_lab_sockets.cpp` (6)
- `test_character_intelligence_lab.cpp` (5)
- `test_route_pricing.cpp` (5)
- `test_entity_decision.cpp` (5)
- `test_decision_extraction.cpp` (2)

They WARN or SUCCEED and return. **A green CI run says nothing about those 55 cases.** Run them
locally with the assets present.

**GPU. The hosted runner's GPU is not authoritative.** Verified on run 36060799310:

- Dawn does get an adapter, "Apple Paravirtual device (apple, integrated) via Metal", with no
  timestamp queries.
- **363 of 443** `avgen_render_tests` cases fail anyway. `SceneRenderer::init()` fails because Metal
  rejects compute pipelines (`Error creating pipeline state Compilation failed` for
  `procedural-effectors`, `procedural-cull-classify` and others).
- Several parity cases return zeros from the GPU (the fog shader `detail` is 0.0 where the CPU
  says 0.82).
- The headless smoke render times out in readback.

This is the VM's paravirtualized Metal (MTLGPUFamilyMac2 only), not the change under test. So:

- The GPU job is labelled **informational**. It never gates the run.
- It runs on `main`, nightly and on dispatch, and its results are still uploaded and summarised.
- A branch push shows it as "not run: branch push" in the Jobs table.

**Every GPU verdict still has to come from a real Mac: run `avgen_render_tests` locally.** The
fix is a self-hosted Apple Silicon runner (see follow-ups).

**Hidden tests.** `[.perf]` and the other hidden tags are not run (see above).

## Caching

| Cache | Key | Restore fallback | Why it is safe |
|---|---|---|---|
| CPM sources (`.cache/cpm`) | OS, arch, hash of `cmake/Dependencies.cmake`, `cmake/CPM.cmake`, `cmake/patches/**` | **none** | Every dependency is pinned by tag, commit or SHA-256. There is no fallback because CPM's own cache key covers a patch's path but not its content, so an older cache after a patch change could carry stale patched sources. |
| ccache (`~/ccache`, 2 GB cap) | OS, arch, Xcode version, preset, commit | previous commit's cache for the same OS/arch/Xcode/preset | ccache keys each object on the compiler binary, the flags and the preprocessed input, so an old cache yields misses, never stale objects. |

Branch caches can read `main`'s caches, but not each other's (GitHub's scoping rule). An agent
branch's first run therefore starts from `main`'s ccache. A job that fails saves no cache
(actions/cache saves only on success). Each build step prints the ccache hit rate, and the
summary shows whether each cache was restored.

The build directory itself is not cached. Every run configures fresh, which also sidesteps the
configure-time test glob (docs/testing.md entry 1).

## Runner and toolchain

| | |
|---|---|
| Runner label | `macos-26` (image `macos-26-arm64`; `macos-latest` resolves to the same image as of 2026-09) |
| Machine | Apple M1 (Virtual), 3 vCPU, 7 GB RAM, arm64 |
| macOS | 26.6.2 at image 20260907 |
| Xcode | 26.6, pinned by `DEVELOPER_DIR`, not the image default. SDK 26.5 |
| Compiler | Apple clang 21.0.0 (clang-2100.1.1.101). Locally it is Xcode 27 / clang-2100.3.34.2 |
| CMake / Ninja | 4.4.3 / 1.13.2 (preinstalled) |
| Metal | one device, "Apple Paravirtual device" (MTLGPUFamilyMac2; not Apple7, not Metal3) |

Why this runner:

- The prebuilt Dawn archive is arm64 with a macOS 26.0 deployment floor (`cmake/Dependencies.cmake`),
  so neither Intel nor macOS 15 runners can run the binaries.
- `macos-26` is named explicitly rather than `macos-latest`, so a future image migration is a
  deliberate edit.
- When the image drops Xcode 26.6, the Toolchain step fails with a message listing what is
  available. Bump `XCODE_VERSION` and `DEVELOPER_DIR` in both workflow files together.

## Timings (measured 2026-09-24)

| | Cold (no caches) | Warm (CPM hit, ccache hit) |
|---|---|---|
| CPM dependency download + configure | 80-112 s | 37-61 s |
| Build, all targets (Release) | 983-1694 s (16-28 min; varies by VM) | 42-70 s (1236 of 1237 objects from ccache) |
| Build job, end to end | 18-31 min | ~2-3 min |
| CPU suite, 3 shards | 5m 36s - 6m 34s | same (tests are not cached) |
| **Push → verdict, wall clock** | **~25-37 min** | **~9-10 min** (run 36066934636: 9m 06s) |
| GPU job (informational, main/nightly) | ~15 min (853 s for `avgen_render_tests`) | same |
| TSan build + 226-case subset | 25 min build + 27 min tests | n/a |

Runs: cold [36060799310](https://github.com/NateFaulkenberry/av-gen/actions/runs/36060799310), warm
[36066934636](https://github.com/NateFaulkenberry/av-gen/actions/runs/36066934636).

For comparison, the whole CPU suite takes about 17 minutes as one process on the developer's Mac
under its usual load. It takes about 6 minutes on the runner in three shards, and occupies no
local CPU. This is not a claim that the runner is faster: the runner has 3 cores to the Mac's
many, and the win comes from sharding and from the work being somewhere else. Nothing in the loop
above needs the Mac.

A change to a widely included header rebuilds most of the tree whatever ccache holds. Expect
something between the two columns for those.

**Minutes.** The repository is public, so standard GitHub-hosted runners, macOS included, cost
nothing. They are limited only by concurrency (5 macOS jobs at a time on a free account). For
reference, if it were private (macOS billed at 10× Linux):

- a warm push run is ~10 macOS job-minutes, or ~100 billed minutes;
- a cold run is ~40, or ~400 billed.

## Sanitizer exclusions

The ASan/UBSan jobs run the same default CPU set as the per-push job, minus the same two
documented crash exclusions (`tools/ci/hosted-runner-exceptions.txt`). There is no
sanitizer-specific exclusion.

**Why it is split, measured on run 36060803359.** A single 3-shard ASan job ran only 847 of 3,233
cases before its shards stopped (4.8 h):

- the median case took 4 ms;
- the 66 `[stage]` cases (abduction, beam and farm simulations) take up to 2 h **each** under
  Debug+ASan, and were 97% of the measured time;
- each shard was also stopped early by an ASan report (below), which halts the process.

**Findings of that run, reported to the owner, not fixed here** (product code):

- `heap-use-after-free` in `Composition::unregisterNodeParameters`
  (`src/scene/composition.cpp:4818`). A parameter is freed by `ParameterSet::remove` via
  `unregisterParameters` (4865, called at 4842) while the particle-parameter walk still reads its
  name. It is reached from `Composition::removeNode` in "Composition nests scene files and flattens
  them with prefixed parameters" (`tests/unit/test_composition.cpp:868`). Until it is fixed, the
  nightly ASan main part reports it, and the cases after it in that shard show as never run.
- UBSan null dereference in `test_body_compensation.cpp:437`, the same missing-asset defect that
  segfaults in Release (now excluded).
- Debug-only `assert` in `json.hpp:2174` from the music-video `get_state` case without its audio
  (now excluded).

The brief expected the bit-exact golden trace tests to drift by 1 ULP under the -O0 sanitizer
build. On a fresh clone they cannot run at all: all three stored-baseline comparisons need
`assets/aliens`, and they skip or fail for lack of it:

- `test_decision_extraction.cpp:141`, "…walks the same route to the bit";
- `test_motion_matching_default_off.cpp:116`, the pre-wiring digest;
- `test_visual_regression.cpp:177`, the posed alien baseline.

So no exclusion was needed for them on CI, and none was invented. There is also no shared
`[golden]`-style tag to exclude them by: `[golden]` in `test_motion_golden_scenarios.cpp` is a
structural check. If they are ever run on an asset-carrying runner, exclude them by exact name
here, with the reason.

GPU tests are not run under ASan. Measured locally, that is about six cases an hour
(docs/renderer-forensics-report.md), and the hosted GPU is not authoritative anyway.

## TSan subset

A full TSan sweep of the CPU suite has been running locally for over 15 hours, so it does not fit
a hosted job's 6-hour limit. The job runs the tags whose cases start threads (`TSAN_SUBSET` in
`sanitizers.yml`):

`[jobs] [job] [motionthreads] [threading] [worldbuilder] [ring] [analysis] [motionlib] [ai] [transport]`

That is 226 cases, which took 27 minutes on 3 shards (run 36060803359). Result:

- **zero ThreadSanitizer reports.**
- 12 assertion failures, all timing under TSan's slowdown or missing assets:
  - `jobs.waitFor(id, 10s)` timing out in `test_world_builder`;
  - `test_pathtrace_job` progress windows;
  - the 100 µs transport ceiling;
  - a Glowmere LOD case;
  - an audio case.

The TSan job therefore uses `--gate sanitizer`. **Only a race report, a crash, a timeout or an
unexercised case fails it**, and assertion failures are listed but do not gate. The subset is
labelled `SUBSET` in its summary. It is not whole-suite TSan coverage and must not be quoted as
such.

## Security

- `permissions: contents: read` on every workflow, and no secrets are used or needed.
- Checkout uses `persist-credentials: false`.
- Third-party actions are pinned to full commit SHAs, with the version in a comment.
- `workflow_dispatch` inputs reach scripts only through `env:`, never interpolated into a script.
- Fork PRs run with GitHub's read-only token.

## Open decisions and follow-ups

1. **Assets (owner's decision).** 1.4 GB locally under `assets/`. Nothing has been published.
   The options, cheapest first:
   - (a) accept the gap and keep running asset-bound tags locally;
   - (b) a private, access-controlled asset bundle fetched with a read-only secret. This means a
     secret, and it means the workflow must never run for forks;
   - (c) a self-hosted Apple Silicon runner that already has the assets. This also fixes the GPU.
2. **GPU.** Only a self-hosted runner on real Apple Silicon makes `avgen_render_tests`
   authoritative remotely. Until then it is local.
3. **Two test defects** exposed by the runner: `test_body_compensation.cpp:433` (segfault) and
   the `test_ai_tools.cpp:780` abort. Each should SKIP or fail cleanly without its asset, after
   which its exclusion can go.
4. **Tests that write into `examples/world`** during a run (`test_glowmere_multicam*`,
   `test_motion_matching_default_off`). They are harmless on CI but racy between concurrent
   local agents.

## Files

- `.github/workflows/ci.yml`, `.github/workflows/sanitizers.yml`
- `tools/ci/build.sh`: configure and build a preset, with timings, the ccache report and a
  `meta.json`.
- `tools/ci/catch2_run.py`:
  - runs a Catch2 binary in shards and reads its XML and exit codes;
  - writes `result.json` and `summary.md`, emits annotations, and fails the step on any failure,
    crash, timeout, sanitizer report or unexercised case;
  - its `report` subcommand builds the run summary.

  It can be run locally against any build: `python3 tools/ci/catch2_run.py run --binary
  build/release/tests/avgen_tests --name cpu --label cpu --out /tmp/r --filter '[ai]'`.
