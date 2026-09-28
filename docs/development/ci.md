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
| debugging a failure, lldb, Instruments | ASan, UBSan and TSan (nightly, or on dispatch) |
| interactive renderer checks, screenshots | the CPU suite with the private test assets (nightly, once enabled) |
| tests that need `assets/` (see Coverage gaps) | repeat verification after every push |
| `[.perf]` probes and GPU timing | pre-merge verification of a branch |

**Run what CI runs.** Every suite goes through one script, `tools/ci/run-suite.sh`, and the
workflows call it with the same arguments a developer would:

```sh
tools/ci/run-suite.sh cpu --build              # the push gate: build Release, run avgen_tests as CI does
tools/ci/run-suite.sh ubsan --build            # the nightly UBSan job
tools/ci/run-suite.sh tsan --build             # the nightly TSan job
tools/ci/run-suite.sh asan:rest-a --build      # one of the five nightly ASan jobs
tools/ci/run-suite.sh gpu                      # the authoritative GPU suite (under tools/gpu-lock.sh)
tools/ci/run-suite.sh cpu --filter '[ai]'      # any suite, one tag (labelled FILTERED)
```

Results land in `ci-results/<suite>/` (gitignored): `summary.md`, `result.json`, shard logs and
XML, exactly what the CI artifacts hold. It generates the two repository scores
(`tools/ci/generate-audio.sh`) if they are missing. Locally the full CPU suite counts as GPU work
(`tools/gpu-lock.sh` says why): run it when nothing else is using the GPU.

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
gh workflow run sanitizers.yml --ref <branch> -f sanitizer=all    # all | asan | ubsan | tsan

# Does the sanitizer plan still name real cases, and run every case once? (Every push checks it.)
python3 tools/ci/catch2_run.py plan-check --binary build/release/tests/avgen_tests \
    --plan tools/ci/sanitizer-plan.txt --exceptions tools/ci/hosted-runner-exceptions.txt
```

`workflow_dispatch` only works for a workflow file that exists on the default branch. Until this
lands on `main`, dispatch is unavailable and CI starts on push only.

## Workflows and jobs

### `CI` (`.github/workflows/ci.yml`): on every push, fork PRs, and dispatch

```
push ─▶ Build (Release) ─┬─▶ CPU tests (avgen_tests, full)                         GATES
                         ├─▶ GPU tests (hosted, informational)                      main / nightly / dispatch
                         ├─▶ CPU tests with private assets (Glowmere tier)          nightly / dispatch, once enabled
                         └─▶ Report  (writes the "# AV Gen CI" summary; prunes caches on main)
      GPU tests (self-hosted Apple GPU, authoritative)                              once a runner exists
```

The setup every build shares (the Xcode check, the CPM and ccache caches, `tools/ci/build.sh`) is one
composite action, `.github/actions/cmake-preset`, used by both workflows. The two asset-bearing jobs
are described in [gpu-ci-private-assets.md](gpu-ci-private-assets.md); until the owner enables them
they are skipped, and the Jobs table says so.

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
  - a last step, `plan-check`, proves `tools/ci/sanitizer-plan.txt` still names real cases and runs
    each case once (see [The sanitizer partition](#the-sanitizer-partition)). It lists; it runs nothing.
- **CPU tests with private assets** (`cpu-assets`): the same binary, with the private test assets
  linked by `tools/fetch-test-assets.sh` and no hosted-runner exceptions, so a case that needs an
  asset must pass. Nightly and dispatch, never `pull_request`, and only when
  `vars.AVGEN_TEST_ASSETS == 'true'`.
- **GPU tests (hosted):**
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

```
asan-build ─▶ asan-tests (5 jobs, tools/ci/sanitizer-plan.txt)   ASan, with UBSan's checks (recoverable)
ubsan      (own build; the whole default set, 3 shards)          UBSan alone, fatal on the first report
tsan       (own build; the concurrency set, 3 shards)            TSan, every night
report     one row per sanitizer in the run summary; prunes caches on main
```

- **ASan:** `--preset asan` (Debug + `-fsanitize=address,undefined`), built once, then five jobs laid
  out by `tools/ci/sanitizer-plan.txt` (see [The sanitizer partition](#the-sanitizer-partition)).
  `ASAN_OPTIONS` adds `detect_stack_use_after_return=1`: the stack-use-after-return ADR-941 fixed was
  invisible to ASan without it. LeakSanitizer is off (unsupported by Apple clang on macOS).
- **UBSan: its own job, not only the checks inside the ASan build.** Why:
  - without ASan's shadow memory a Debug build runs a few times slower than Release rather than
    ~56x, so the WHOLE default set fits in one job, including the two cases the ASan plan must skip;
  - an ASan halt ends its process, so a UBSan report later in that process was never reached;
  - its verdict is its own row, not a line inside an ASan summary;
  - `UBSAN_OPTIONS=halt_on_error=1` makes a report kill its process with a non-zero exit, which
    `catch2_run.py` reads as a failure on its own, besides the `runtime error:` line it also gates on.
  The ASan build keeps UBSan's checks (recoverable, `halt_on_error=0`) as a second net: they cost
  nothing extra there, and any `runtime error:` line still fails that job.
- **TSan:** `--preset tsan` over the concurrency set (below), **every night** since 2026-09-28. It was
  Sundays only while the ASan run held all five macOS slots for 4-5 h.
- **Gating:** `--gate sanitizer` for all three. A sanitizer report, a crash, a timeout, an
  unexercised case or an exit code the Catch2 XML does not explain (since 2026-09-28; before, the
  sanitizer gate ignored it) fails the job. Catch2's exit 4 ("every case ran was skipped", what a
  plan process whose cases all need absent assets returns) agrees with its XML and is not flagged. Plain assertion failures are listed but do not gate (they were
  `[performance]` wall-clock ceilings and 10 s job waits that an -O0 instrumented build cannot meet).
  The Release run gates correctness. Each report appears in the summary as `SANITIZER FAILURE` with
  its type, first frame and a stack excerpt, and every job uploads its shard logs and XML
  (`sanitizer-logs-*`, 14 days).
- **Exclusions:** none by name any more in `tools/ci/hosted-runner-exceptions.txt` (the two crash
  exclusions were fixed and removed on 2026-09-28). The ASan plan skips two cases it cannot finish;
  UBSan runs them.
- **Locally:** `tools/ci/run-suite.sh ubsan --build`, `tsan --build`, `asan:<job> --build`.

## What the categories mean

| Category | Where | What runs |
|---|---|---|
| FULL | `CI` on every push | build of every target; all default `avgen_tests` cases; all default `avgen_render_tests` cases; the headless binary smoke test |
| SANITIZER | `Sanitizers` nightly / dispatch | ASan (with UBSan's checks) over the default CPU set, minus the two cases `tools/ci/sanitizer-plan.txt` skips; UBSan alone over the whole default set; TSan over the concurrency set |
| ASSETS | `CI` nightly / dispatch, once enabled | the CPU suite with the private test assets; the GPU suite on a self-hosted Apple GPU |
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

Tracked on the runner: `assets/imported/{alien,ufo}.gltf` and the manifests. `assets/farm/*.glb` was
tracked until `4a138886` (2026-09-28), when the owner confirmed the pack is purchased; the Glowmere
Valley 2 cases that need the animals now skip through `testsupport::skipUnlessFarmAssetsPresent()`.
`assets/audio/*.wav` is generated on every CI run by `tools/ci/generate-audio.sh` (the scripts are the
repository's own and deterministic; each file is checked against its manifest sha256). The
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

The two **test defects the runner exposed** are both fixed, and neither case is excluded any more:
`test_body_compensation.cpp` skips without `alien-scout.glb` (20de27ac), and the two night-shift
`sequence.get_state` cases skip without `night-shift.wav` (2026-09-28), which CI now generates anyway.

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

**Every GPU verdict still has to come from a real Mac.** Until the self-hosted runner exists, run
`tools/ci/run-suite.sh gpu` locally. [gpu-ci-private-assets.md](gpu-ci-private-assets.md) is the
design and the owner's setup steps.

**Hidden tests.** `[.perf]` and the other hidden tags are not run (see above).

## Caching

| Cache | Key | Restore fallback | Why it is safe |
|---|---|---|---|
| CPM sources (`.cache/cpm`) | OS, arch, hash of `cmake/Dependencies.cmake`, `cmake/CPM.cmake`, `cmake/patches/**` | **none** | Every dependency is pinned by tag, commit or SHA-256. There is no fallback because CPM's own cache key covers a patch's path but not its content, so an older cache after a patch change could carry stale patched sources. |
| ccache (`~/ccache`, 2 GB cap) | OS, arch, Xcode version, preset, commit | previous commit's cache for the same OS/arch/Xcode/preset | ccache keys each object on the compiler binary, the flags and the preprocessed input, so an old cache yields misses, never stale objects. |

**Saved from `main` only, and pruned.** Every job restores (`actions/cache/restore`), but only a
run on `main` saves (`actions/cache/save`); branch runs read `main`'s caches, which GitHub's scoping
allows, and save none of their own. Each workflow's report job on `main` then runs
`tools/ci/prune-caches.sh`, which keeps only the newest entry per key prefix. Before this
(2026-09-28) every push on every branch saved a 150-550 MB ccache under its own commit key: 24
entries, 10.7 GB against the repository's 10 GB quota, and GitHub was evicting the least recently
used, which was `main`'s TSan cache. A job that fails saves no cache. Each build step prints the ccache hit rate, and the
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
| ASan/UBSan build + tests (2026-09-26/27) | 38 min build; the old 7 parts ran 12:16-20:51 UTC with 4 timeouts | n/a |
| ASan, 5 jobs over 7 rest shards (run 36434165586, 2026-09-28, farm films skipping) | 42 min build (cold); jobs: rest-a 175 min and rest-b 174 min (3 rest shards each), heavy-1 103 min (two non-farm cases: the pre-ADR-262 benchmark case 102 min, the writeback case 72 min), heavy-3 33 min (1 rest shard), heavy-2 1.5 min. Critical path build + rest = ~3 h 37 min; wall clock 4 h 36 min because heavy-1 waited 1 h 36 min for a macOS slot. 0 reports | n/a (caches are saved from main only) |
| UBSan alone, whole default set (same run) | 40 min build + 204 min tests = 3 h 48 min job; 3838/3838 ran, 0 reports | n/a |
| TSan, concurrency set (same run) | 82 min job (build plus about 40 min of tests); 243 cases, 0 reports | n/a |

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

## The sanitizer partition

> **Since 2026-09-28 the heavy parts are mostly asset-bound.** Every film this section measures needs
> the farm GLBs, which left the tree in `4a138886`; on a hosted runner those cases now SKIP in
> milliseconds (`testsupport::skipUnlessFarmAssetsPresent()`). The plan still names them, because it is
> right wherever the assets are present, but on the hosted nightly the `heavy-*` jobs finish quickly
> and the run is bounded by the rest shards. The measurements below are from before that change.
> Measured after it (run 36434165586): the longest cases left in the rest are 44-50 min each under ASan
> with three processes sharing the runner (the sightline pass on the real project 50 min, every example
> loads 47 min, a fired section event 46 min, the wanderer's feet 46 min, Rook's benchmark run 44 min,
> the UFO stack demo 35 min, the multicam deciders 31 min), so "nothing in the rest over about 26 min"
> no longer holds; they fit easily, and no timeout came near.

The ASan/UBSan run covers the default CPU set, the same set as the per-push job, minus:

- the two documented crash exclusions (`tools/ci/hosted-runner-exceptions.txt`), as on every push;
- two cases that no hosted job can finish under ASan, skipped by name (below).

Where every case runs is decided by one file, `tools/ci/sanitizer-plan.txt`, read by
`tools/ci/catch2_run.py run --plan <file>`. A job runs one part's named processes (`--part`)
and/or some of the rest's shards (`--shards`, `--shard-total`, `--shard-first`). Each job runs three
processes, one per vCPU:

| Job | What runs | Expected (Debug + ASan, hosted runner) |
|---|---|---|
| `heavy-1` | `film`: the eleven cases that read `film()` in `test_abduction_sequence.cpp`, in one process; `delete`: deleting animals, the engine's writeback, the wandering animals; `pre262`: the pre-ADR-262 scenario, the slowed benchmark run, the beam's particles | 4.1-5.0 h. The film: 245-300 min measured. The other two: about 245-250 min each at the slowest measurements |
| `heavy-2` | `fade`: the three cases that read the fade film in `test_abduction_fade.cpp`; `director`: the director's own 30 s, the lab; `abduct`: the UFO abducting several animals, the animal inside the beam | 3.2-3.7 h. Measured: fade 190-216 min, director 154 + 36, abduct 147 + 71 at worst |
| `heavy-3` | `hidden`: the already-hidden animal, the same scene and seed; `lights`: the lit-up animal and three half-hour cases; rest shard 1 of 7 | 3.2-3.8 h. Measured: hidden 119 + 106, lights 83 + 33 + 31 + 32 at worst |
| `rest-a`, `rest-b` | rest shards 2-4 and 5-7 of 7: every other default case (`~[.]`, minus every case the plan names), none measured over 26 min | about 2-2.5 h per job |

All five run at once, so the run takes about 4-5 h after the 5-38 min build, against 8.5-10.5 h for
the seven parts it replaces. Every job has the same 330 min ceiling (`--timeout-min`, inside the
job's 355 min cap), and the thinnest margin is the film: 300 min at its worst against 330.

**How it was measured.** Runs 36241405408 (2026-09-26) and 36321265640 (2026-09-27), both at main
83a12334, all 14 parts, three processes per 3-vCPU runner.

- Per case, ASan took a median 56x its Release time (p90 109x, over 114 cases that take over 1 s in
  Release). Cases never run under ASan are estimated from Release at 56x.
- **The abduction simulations run far past that median:** "an animal that is already hidden..."
  took 119 min for 46 s in Release (155x). So every case measured at 29 min or more is placed, and
  the rest is left with nothing over 26 min.
- The rest figures come from 4,000 random orders cut the way Catch2 cuts them into 7 shards, taking
  each case at the slowest time it was ever measured, which is pessimistic. One rest shard: 1.4 h
  median, 2.1 h at p95. The slowest of all seven: 2.0 h median, 2.8 h at p99, 3.3 h at worst.
- The same case can differ by up to half between runs (the UFO case: 95 min, then 147), so these are
  ranges, not promises. If a job times out, the TIMEOUT block names the process and the case it was
  running.

**Why a plan, and not more shards.** The old split ran `[stage]` in 18 random shards over six
jobs, and five of the seven parts hit the ceiling. The time was not in the cases themselves:

- **Two test files build a film once per process** and share it between their cases (`film()`,
  90 s of Glowmere Valley 2 at 60 Hz, and the fade file's `static const Film f`). In one process
  the first reader pays for the film and the others take 0 min (09-26: 280 min, then 0).
  Random shards spread `film()`'s eleven readers over up to eleven processes, and each paid 4-5 h
  for the same film. More shards would have made that worse. Now each film is built once.
- **"main" held two cases that no hosted job can finish**, and no partition can split them:
  - "the farm animals travel the way they are drawn facing, at the speed their clips say" takes
    1122 s in Release on the runner, and was killed after 311 min under ASan (36241405408);
  - "The film's five characters all walk, at more than one seed" takes 512 s in Release (two
    150 s films), and was killed after 299 min (36321265640).

  They are the plan's two `skip` lines. **ASan does not check them.** Their code (farm locomotion,
  the multicam's walking) runs under ASan in lighter cases, but those two paths through it do not.
  They need a sanitizer-sized variant, a shorter film when `__has_feature(address_sanitizer)`,
  before they can come back; that is their owners' change.
- **Cases over about half an hour** each get a named process with a few others, packed to about
  250 min at their slowest measurements, so two of them can never meet in one random shard.

The plan also fixes a quiet over-selection. `[stage]` also matches a hidden probe, "probe: the
animal, the beam and the lag" (`[.][beam][probe][stage]`), which no default run includes, and the
old parts ran it for 94 min. The rest filter is `~[.]`.

**Keeping it honest.** A plan names cases, so a rename could quietly move a four-hour case into a
rest shard. Every push runs `catch2_run.py plan-check` (the CPU job's last step, listing only, a few
seconds). It counts, with the binary's own `--list-tests`, that placed + skipped + rest equals the
default set, and it fails on any plan name that matches no case or more than one:

```
3786 cases: 30 placed + 2 skipped + 3754 in the rest of ~[.] = 3786     (main 876a11e2)
```

A name that goes stale on the nightly anyway is a `Sanitizer plan` warning in that job's summary,
and the renamed case runs in a rest shard. To move a case, edit the plan. A new case needs no
entry unless it is heavy: new cases land in the rest.

**Earlier history.** Run 36060803359: one 3-shard ASan job ran 847 of 3,233 cases in 4.8 h, 97% of it
the `[stage]` cases. Run 36087867052 split it into `main` plus four `[stage]` parts, later six.

**Findings.** Runs 36060803359 and 36087867052 found three defects, all fixed on 2026-09-25
(20de27ac):

- the `stack-use-after-scope` in `test_wave_effects.cpp` (a `ResolvedWave` pointing into a
  temporary `std::array`);
- the `heap-use-after-free` in `Composition::unregisterParameters`;
- the null dereference in `test_body_compensation.cpp`, which now skips without its asset.

The Debug-only `assert` in `json.hpp` from the music-video `get_state` case without its audio is
still excluded.

Runs 36241405408 and 36321265640 found one more: UBSan's `load of value 240, which is not a valid
value for type 'bool'` at `atmospherics.cpp:698`, in `packComet`. It is the same mistake as the wave
test's, in `test_atmospherics.cpp`'s `resolveComet` helper, and ADR-941 fixes it. It is worth
knowing that ASan could not have caught it. The helper had *returned*, so the read was a
stack-use-after-return, which ASan reports only with `detect_stack_use_after_return=1` (off here,
because it costs time). UBSan saw it only because the dead byte happened not to be 0 or 1.

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

Nightly since 2026-09-28 (it was Sundays only). A full TSan sweep of the CPU suite has been running locally for over 15 hours, so it does not fit
a hosted job's 6-hour limit.

**Measured on the runner (run 36434180499, 2026-09-28, dispatch `test_filter='~[.]'`, the whole default
set in one job, 3 shards): TIMEOUT.** All three shards were killed at the 300 min ceiling with 1680 of
3838 cases run (44%); 0 ThreadSanitizer reports and 0 crashes in what did run, 4 timing assertions and
10 absent-asset failures. The shards died inside long single-process simulations (`three abductions,
one craft ...`, `Every example in the index exists and loads`, `the engine's own writeback does not dirty
an untouched project`). At that rate the whole set needs roughly 11-12 h of 3-shard time, so **whole-suite
TSan cannot run nightly in one hosted job**; it would take about three jobs of 3 shards each (a partition
like the ASan plan), which is not worth the macOS slots while the subset keeps finding nothing. The
subset below stays the nightly TSan. The job runs the tags whose cases start threads (`TSAN_SUBSET` in
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

The TSan job therefore uses `--gate sanitizer`. **Only a race report, a crash, a timeout, an
unexercised case or an unexplained exit code fails it**, and assertion failures are listed but do not gate. The subset is
labelled `SUBSET` in its summary. It is not whole-suite TSan coverage and must not be quoted as
such.

## Security

- `permissions: contents: read` on every workflow. The report jobs add `actions: write`, used on
  `main` only, to prune caches.
- One secret, used only by the asset jobs and only on trusted events (never `pull_request`):
  `AVGEN_TEST_ASSETS_SSH_KEY` (a read-only deploy key) or `AVGEN_TEST_ASSETS_TOKEN`. See
  [gpu-ci-private-assets.md](gpu-ci-private-assets.md), including why a self-hosted runner belongs on
  the private repository.
- Checkout uses `persist-credentials: false`.
- Third-party actions are pinned to full commit SHAs, with the version in a comment.
- `workflow_dispatch` inputs reach scripts only through `env:`, never interpolated into a script.
- Fork PRs run with GitHub's read-only token.

## Open decisions and follow-ups

1. **The private test assets and the GPU runner: PARKED, owner's decision (2026-09-28).** Designed and
   implemented, switched off: [gpu-ci-private-assets.md](gpu-ci-private-assets.md) has the design, the
   security reasoning and the exact steps. Owner's steps 1-3 are done (the PRIVATE repository
   `NateFaulkenberry/av-gen-test-assets` exists and `tools/ci/test-assets.lock` pins `61ff6dd`); steps
   4-6 (deploy key/secret, the `AVGEN_TEST_ASSETS`/`AVGEN_GPU_RUNNER` variables, the self-hosted runner)
   are pending and parked, so the asset jobs stay skipped. Decided: the runner will live on the private
   repository. Until then the asset-bound cases are local-only, and a GPU verdict comes from
   `tools/ci/run-suite.sh gpu` on a real Mac.
2. **The song: decided**, `Rebuild.mp3` may go into the private repository (the owner's own song); adding
   it to the list and the fetch script is future work, not done. **The farm GLBs still in public
   history** (`4bdc42ff`): still open, in that document's "Decisions" section.
3. **Tests that write into `examples/world`** during a run (`test_glowmere_multicam*`,
   `test_motion_matching_default_off`). They are harmless on CI but racy between concurrent
   local agents.
4. **The two cases the sanitizer plan skips** (the farm locomotion case, the five characters over
   two seeds) are not checked by ASan. Both now also need the farm GLBs, so on a hosted runner they
   skip before any simulation; UBSan runs whatever the runner can. On an asset-carrying runner they
   still need a sanitizer-sized variant, a shorter run when `__has_feature(address_sanitizer)`.
5. **ASan at `-O1`.** ASan's documentation recommends `-O1`; the preset is Debug `-O0`. It would
   likely be several times faster (not measured). Only worth doing if the nightly margin shrinks again.
6. **Three CC0 fixtures could be tracked.** The GPU LOD, visibility, ecology and emission cases load
   `CommonTree_1`, `Rock_Medium_1` and `Mushroom_Common` from the Quaternius pack (CC0; about 4 MB with
   their textures). They are gitignored only because the pack is large; tracking those three would let
   the CPU cases that use them run on every push, and shrink the private set.

## Files

- `.github/workflows/ci.yml`, `.github/workflows/sanitizers.yml`
- `tools/ci/sanitizer-plan.txt`: the ASan/UBSan partition; which process runs each heavy case,
  the rest filter, and the two skipped cases, each with its measurement.
- `.github/actions/cmake-preset/action.yml`: the shared build setup (Xcode check, CPM and ccache
  restore, `build.sh`, cache save on `main` only).
- `tools/ci/run-suite.sh`: every suite, as CI runs it; the one command for running CI locally.
- `tools/ci/generate-audio.sh`: regenerates `assets/audio/*.wav` and checks them against the manifest.
- `tools/ci/prune-caches.sh`: keeps the newest Actions cache per key prefix on `main`.
- `tools/fetch-test-assets.sh`, `tools/ci/test-assets.lock`, `tools/ci/test-assets.list`,
  `tools/ci/make-test-assets-repo.sh`, `tools/ci/private-repo-gpu-workflow.yml`: the private test
  assets ([gpu-ci-private-assets.md](gpu-ci-private-assets.md)).
- `tools/ci/build.sh`: configure and build a preset, with timings, the ccache report and a
  `meta.json`.
- `tools/ci/catch2_run.py`:
  - runs a Catch2 binary in shards and reads its XML and exit codes;
  - writes `result.json` and `summary.md`, emits annotations, and fails the step on any failure,
    crash, timeout, sanitizer report, unexercised case or exit code its XML does not explain;
  - `run --plan <file>` runs one job of the sanitizer plan: a part's named processes (`--part`)
    and/or rest shards (`--shards`, `--shard-total`, `--shard-first`);
  - its `plan-check` subcommand proves a plan runs every case of a binary exactly once;
  - its `report` subcommand builds the run summary.

  It can be run locally against any build: `python3 tools/ci/catch2_run.py run --binary
  build/release/tests/avgen_tests --name cpu --label cpu --out /tmp/r --filter '[ai]'`.
