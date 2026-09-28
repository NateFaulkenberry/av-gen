# QA, CI hardening, performance investigation and cleanup pass: plan

Started 2026-09-28. The owner's brief is saved verbatim in `00-brief.md`, next to this file. Progress is tracked in
`PROGRESS.md` and the conclusions go in `FINAL-REPORT.md`. This file holds the plan. When the plan changes, the
change is recorded here with its reason.

**Hard constraint: no new features.** Allowed work is bug fixes, regression fixes, removing dead state,
instrumentation that only diagnoses, tests, CI and project cleanup. A performance change needs a measured cause
first. Anything that looks like a feature goes in the report as a recommendation.

## Phase 0 inventory (2026-09-28)

### Repository and branches
- main is `77ea4247` and is pushed. It carries the whole GV3 revision (the generator `tools/make_glowmere_valley_3.py`
  plus `tools/gv3/`, and the docs `docs/glowmere-valley-3/`) and every engine stream up to ADR-947.
- The owner's main checkout is left on main. This pass works in worktrees, one per workstream, each branched from
  `77ea4247`:

  | Worktree | Branch | Owner |
  |---|---|---|
  | `../av-gen-qa-coord` | `qa/coord` | coordinator: docs and integration |
  | `../av-gen-qa-perf` | `qa/perf` | Agent 1: performance |
  | `../av-gen-qa-ci` | `qa/ci` | Agent 2: CI and sanitizers |
  | `../av-gen-qa-clean` | `qa/clean` | Agent 3: cleanup and GV integration |

- Seventeen older worktrees remain from the GV3 program (engine-3..6, gv3-*, agent/*). They are local machine
  state, not repository state, and are listed in the report as optional cleanup for the owner.

### Glowmere Valley 3
- **The repository's copy is stale.** `examples/world/glowmere-valley-3.{json,scene.json}` were last committed at
  `334c4cf6` ("Phase 3 foundation"): 716 KB, no `directingPlans`, `staging` or `songPlan`.
- **The Desktop holds r7b.** It is in `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/integrate/project-for-review/`:
  - `unbundled-copies/` has the project (1.26 MB) and scene, with an absolute song path;
  - `bundle/` has an `--export-bundle` output (project.json, 1.37 MB) and an `assets/` folder of 77 MB.
- **The bundle's `assets/` includes the purchased alien and farm models and `Rebuild.mp3`.** None of it can be
  committed (memory note `av-gen-ci-assets-decision`; the repository is public).
- The generator regenerates the project from the GV2 multicam files, `tools/gv3/` and a cast trace, which is a build
  artifact (`--final-trace`).

### CI (already substantial; currently red)
- `ci.yml` runs on every push: a Release build, then the CPU suite in 3 shards (this gates), a GPU suite that is
  informational only (main, nightly and dispatch), and a report job. It runs nightly at 06:43 UTC.
- `sanitizers.yml` runs nightly: ASan and UBSan combined, over the CPU suite in 6 partitioned stages (from
  `tools/ci/sanitizer-plan.txt`); TSan runs on the concurrency subset on Sundays only.
- **State on 2026-09-28:**
  - CPU tests FAILED on `4a138886`, the farm-model removal. The likely cause is tests that depend on the farm
    assets and do not skip. To be confirmed.
  - The last two nightly sanitizer runs FAILED: stages 0, 1, 5 and main on 2026-09-27.
  - Several earlier main pushes failed.
- **Hosted-runner GPU:** `ci.yml` records that 363 of 443 GPU cases fail on the hosted VM's paravirtual Metal device,
  because it cannot compile the renderer's pipelines. The hosted GPU job is therefore informational by design. A
  GPU verdict that means anything needs real Apple GPU hardware.

### Performance tooling that already exists
- `avgen` flags:
  - `--profile-cpu`: per-phase distribution of main-thread time;
  - `--profile-csv`: one row per frame per phase;
  - `--bench-json`: percentiles and counters;
  - `--frames`, `--size` and `--cluster-stats`;
  - kill switches for water, transparency, particles, animation, camera motion and fxaa;
  - `--lab-case`.
- `tools/`: `bench_ab.sh`, `bench_world.sh`, `render_bench.py`, `make_bench_scenes.py`, `make_mcperf_arms.py`.
- Earlier baselines and rules: `docs/renderer-upgrade/`, `docs/renderer-forensics-report.md`,
  `docs/investigations/ui-responsiveness.md`.
- Measurement rules learned the hard way:
  - compare only within one session;
  - pin the resolution;
  - discard the first run after a build;
  - use repeats (small frame sizes vary 39-54%);
  - prove each probe reached the state it claims to measure;
  - run only in release;
  - one GPU run at a time (`tools/gpu-lock.sh`).

## Workstreams

### W1: performance and the GV3 investigation (Agent 1, `qa/perf`)
**Hypotheses:**
- A: the cost is scene complexity or a renderer limit.
- B: GV3 has accumulated bad project state.
- C: an engine regression.
- They can combine.

**Baseline (before any change):**
- **Scenes:** GV3 r7b in the repository layout, GV3 as committed, GV2 multicam, GV2, and one small scene (grove or
  constellation).
- **Conditions:** Release build, a fixed `--size` (1280x720, plus 640x360 for the "low resolution" complaint), 3
  repeats.
- **Cameras:** the same set of shots in every scene: a close-up, a medium shot and a wide.
- **Recorded per run:** frame p50/p95/p99, CPU phases, GPU passes, draw calls, triangles, visible entities and
  meshes, lights and shadow casters, materials, and uploads or allocations wherever counters exist.

**Experiments:**
1. **Shot sweep.** Measure every shot of the GV3 cut at its own time. This gives FPS per shot and ranks the worst
   wides against the best close-ups.
2. **Close-up against wide attribution.** For the worst wide and the best close-up, turn each kill switch off one
   at a time (water, transparency, particles, animation, shadows, volumetrics, post). Take the CPU/GPU split and
   the pass breakdown, to find whether the collapse follows visible count, a specific object or effect, a pass,
   CPU scene work, or sync.
3. **Structural comparison, GV2 against GV3.** Write a script that emits a machine-readable complexity report per
   project: entities, instances, meshes, triangles, lights, effects, routes, tracks, cameras, materials, hidden
   objects, and dead or duplicated references.
4. **Isolate project state (hypothesis B).** Remove one category at a time on a copy: effects, routes, staging and
   directing plans, hero pulses, hidden entities, duplicates. Measure after each.
5. **Regression check (hypothesis C).** Run the same GV2 and GV3 workload on main and on historical commits (the
   pause at `STATUS-2026-09-25`, before the GV3 engine streams, and the renderer-upgrade baseline if it still
   loads). Bisect any step that is 10% or more beyond run-to-run spread.
6. **Interactive navigation.** The owner also reports slowness while navigating. Measure editor idle and
   camera-drag frames (`--ui-ab`) on GV3.

**Evidence standard:** every claim is backed by a number, a command and a repeat count. A fix is allowed only for a
demonstrated cause, and needs before and after numbers on the same session and the same workload.

### W2: CI, sanitizers, GPU CI and test assets (Agent 2, `qa/ci`)
1. **Triage the red runs first:**
   - the CPU failure on `4a138886` (farm assets);
   - the ASan/UBSan stage failures from 2026-09-27;
   - the earlier failures on main.
   Classify each as a real defect, a flaky test, an environment problem or infrastructure, and fix the real ones.
2. **Audit both workflows:** redundancy, caching, artifacts and the failure summary. Make local and CI runs
   consistent. Separate the fast PR check from long-running validation.
3. **Sanitizers:**
   - ASan and UBSan run nightly. Either split UBSan out, or make its findings fatal and reported on their own; the
     choice needs a documented reason.
   - TSan: widen it from Sundays-only if the runtime allows, and document every exclusion.
4. **GPU CI:**
   - Design the private-asset path: a private repository, fetched with a GitHub secret, on trusted events only,
     never cached or uploaded, with a clear error when it is missing, and the same asset location locally.
   - Since hosted Metal cannot compile the pipelines, the authoritative GPU job needs a self-hosted Apple-silicon
     runner, or the best documented alternative.
5. **Test assets:** audit which tests load production or licensed assets. Replace them with tiny fixtures where
   that loses no coverage, and mark the rest as asset-dependent so they SKIP clearly without the assets.
6. **Coverage gaps:** serialization, malformed projects, missing assets, resource lifetime. Add a test only where it
   guards a real risk.
7. **Hand-off:** Agent 2 (or a successor) becomes the CI monitor once the workflows are in.

### W3: repository and project cleanup (Agent 3, `qa/clean`)
1. **Integrate GV3 r7b into `examples/world/`:**
   - inventory absolute and machine-specific paths, and references outside the project;
   - no licensed assets and no song;
   - decide how r7b is reproduced: the committed file, the generator, or both, with the trace;
   - verify it loads headless from the repository layout with 0 errors and matches the Desktop copy's content.
2. **Audit the Glowmere family** (GV2, GV2 multicam, GV2 song, stylized, atmospherics): broken references, stale
   `_diag-*`, `_pre-*` and `_tier*` files in `examples/world/`, orphaned scenes, and duplicated resources.
3. **Repository hygiene:** stale files, unused fixtures, and `tools/check_project_integrity.py` drift.
4. **Engine defects already known from the wrap-up, if they are bugs rather than features:**
   - a project whose scene fails to load falls back to the orb scene with no visible error;
   - `--export-bundle` drops a scene's `lightRig`;
   - `--export-bundle` without `--headless` opens the windowed app.
5. **Report GV3 project state (hypothesis B)** to W1, which measures it. W3 does not remove state from GV3 without
   W1's measurement.

## Coordination rules
- **At most 3 sub-agents at once.**
- **The GPU is shared:**
  - every GPU run or measurement goes through `tools/gpu-lock.sh`;
  - W1's timings are valid only when nothing else is running on the GPU, and W1 records that it checked.
- **ADRs:** each agent is given a number block. W1 owns 950-959, W2 960-969 and W3 970-979. They are renumbered on
  merge if they collide.
- **Merges:** into `qa/coord` first, then main once the full CPU and GPU suites are green. The CI config is merged
  early so the monitor has something to watch. Pushing is part of this pass, because CI must run. Pushes never
  include licensed assets or the song.
- Each agent keeps `docs/qa-pass/<workstream>.md` current in its own worktree. The coordinator folds these into
  `PROGRESS.md`.

## Validation criteria
- **GV3:**
  - it loads from repository state with 0 errors;
  - its routes, effects and tracks are all live;
  - the shot count matches r7b's (73).
- **Performance:** the final report states per-shot FPS, the ranked causes with numbers, and a separate verdict on
  each of complexity, project state, regression and unavoidable cost.
- **CI:**
  - main is green on push;
  - ASan/UBSan and TSan run on schedule and fail on any finding;
  - the GPU path is implemented or its blocker is documented with the alternative.
- **Tests:** asset-dependent tests skip with a named reason when the assets are absent, and nothing else skips on
  a clean CI run.

## Stopping criteria
- **W1** stops when every hypothesis has a verdict backed by measurement and every demonstrated regression or
  pathology is fixed or explained.
- **W2** stops when the workflows are merged, one full nightly cycle has run, and its failures are triaged.
- **W3** stops when GV3 reproduces from the repository and the audit's findings are fixed or listed.
- **The pass ends** with `FINAL-REPORT.md`, which answers the brief's eleven questions.
