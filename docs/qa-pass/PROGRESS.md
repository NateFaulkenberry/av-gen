# QA pass: progress

The plan is in `PLAN.md`, and the brief in `00-brief.md`. Each workstream's detailed notes live in
`perf.md`, `ci.md` and `clean.md`, on each agent's branch until they merge.

## Restart plan (2026-09-28, the owner's "option 2")
- **The checkpoint:** the owner is restarting Claude Code and clearing the session mid-pass. W1 (perf) and W2 (ci)
  were told to checkpoint: commit, write a cold "Resume here" in `perf.md` / `ci.md`, and hand back. W3 is done.
- **After the restart, immediately:**
  1. Confirm the Blender MCP tools are loaded.
  2. Launch the `astronaut-prototype` agent in its own worktree from main.
  3. Launch a new W1 agent resuming from `../av-gen-qa-perf/docs/qa-pass/perf.md`.
- **Later:** a CI monitor agent resuming from `../av-gen-qa-ci/docs/qa-pass/ci.md`, for the sanitizer runs in flight
  on qa/ci.
- **After W1 is done and qa/perf is merged:** the `gv3-art-pass` agent.
- **Limit:** 3 concurrent agents.
- The same plan is in memory note `av-gen-restart-handoff-2026-09-28`.

## Resumed after the restart (2026-09-28)
- The Blender MCP was confirmed connected: `get_objects_summary` returned Blender's default scene.
- **Three agents launched (3 of 3 slots):**
  - The `astronaut-prototype` agent, in `../av-gen-astro` (branch `proto/astronaut-musicians` from main `77ea4247`, with the ignored assets symlinked per file).
  - A resumed W1 agent, in `../av-gen-qa-perf`, working from perf.md's "Resume here".
  - The CI monitor, in `../av-gen-qa-ci`. At launch, qa/ci was at `f20739dd`. On run 36434165586, TSan, UBSan, the ASan build, heavy-2 and heavy-3 had passed; heavy-1, rest-a and rest-b were still running. Run 36434180499 was still running.
- **Owner decisions, 2026-09-28:**
  - ADR-951 is accepted (W1 was told).
  - The aurora should react to the audio with a similar low-energy bass pulse. This is added to the art-pass brief
    as an addendum, because it is a look change. It is not a revert of the per-frame response, which made the sky
    jump on the kick.
  - **GPU CI:** PARKED by the owner (no self-hosted runner for now).
    - Steps 1-3 are done: `NateFaulkenberry/av-gen-test-assets` is private on GitHub at `61ff6dd` (202 files,
      248 MB), and the CI monitor was asked to pin that sha in `tools/ci/test-assets.lock`.
    - Steps 4-6 are pending: the deploy key and secret, the variables, and the runner.
    - Decided: the runner will live on the private repository. `Rebuild.mp3` may go into it (the owner's own
      song); it is not wired in yet.
- **The astronaut prototype is DONE:** `proto/astronaut-musicians` `7f1dc21d`, not pushed or merged. It needed no
  engine change.
  - The owner flagged helmets crumpling. The cause was the rig's automatic weights (the helmet was 37% head and the
    rest neck, arms and clavicles). The fix is a rigid helmet plus half neck stiffness, taking the drummer from
    7.66 cm to 0 and the pianist from 8.90 cm to 0.
  - Report: `~/Desktop/av-gen-review/21-astronaut-musicians/REPORT.md`.
  - Owner decisions pending: the texture, the keyboard, the drummer's corrections, and the CGTrader licences.
- **Owner, 2026-09-28: the astronaut decisions.** Accepted as they are: the flat white suit and dark visor, the
  keyboard at 2x, the drummer's corrections, and a neck stiffness of 0.5. The licences, as the owner confirmed them on 2026-09-28:
  - the astronaut: CC0 1.0 Universal;
  - the keyboard and the stand: CC0;
  - the drum kit: a personal royalty-free licence, not for redistribution. It may appear in renders, but the files
    are never committed.
- **Owner, 2026-09-28: the Astronaut Musicians + UFO Abduction addendum** is appended to the art-pass brief. It asks
  for:
  - the two musicians in GV3 near the river as hero entities, with a rainbow bioluminescent audio pulse and optional
    stage lighting;
  - the UFO abducting the drummer instead of the animal.
  - **The launch gate changes:** `proto/astronaut-musicians` must be merged into main (or into the art-pass branch)
    before the art pass starts.
- **W1 is DONE:** `qa/perf` `25510c39`. The coordinator then recorded ADR-951 as accepted by the owner
  (`ad3c5b9f`). Owner summary: `docs/qa-pass/perf-summary.md`.
  - **A (complexity): not the cause of the collapse, but what remains.** Wides run at 31-33 FPS at 720p and are
    GPU-bound; the scene pass is about 9.4 ms plus 10 ms per megapixel. No single feature owns the cost.
  - **B (stale state): no.**
  - **C (engine regression): yes, and it is the cause.** ADR-834 took the engine update from 3.2 to 21.7 ms, and
    ADR-893/894 took it on to 38.8 ms. ADR-950/951 bring it back to 3.3 ms. On s66 the frame went from 376.2 to
    32.2 ms.
  - **GPU:** +1.9 ms, of which ADR-945's glow pools are 1.3 ms (the look the owner chose) and creep across
    `0b623b88..22ce5c3d` is 1.1 ms.
  - **Editor:** the `ui.build` spike of about 35 ms is ADR-440's unsaved-changes check, which serialises the
    whole project about every 380 ms. The owner needs to decide what to do about it.
  - **Suites on qa/perf:** `avgen_tests` exits 0; `avgen_render_tests` exits 0 (548 passed, 1 skipped).
- **qa/perf and `proto/astronaut-musicians` are merged into qa/coord** (`77a6db26`, `0a941dfe`), and both suites are
  running on it. qa/ci is still with the CI monitor.
- **Owner, 2026-09-28: skip the ADR-440 check during playback.** An agent is fixing it in `../av-gen-qa-dirty`
  (branch `qa/dirty-check`, from `4360244c`, taking ADR-952). Merge it into qa/coord once both suites pass.
- **qa/coord `581c018c`: both suites pass after the merges.** `avgen_tests` exits 0 (3,846 cases: 3,826 passed,
  19 skipped, 1 failed as expected). `avgen_render_tests` exits 0 (549 cases: 548 passed, 1 skipped).
- **ADR-952 is DONE and merged** (`qa/dirty-check` `1b74414c`). The unsaved-changes check waits while the
  transport plays, and every discard path still serialises on demand. While playing, GV3's `ui.build` p95 dropped
  from 35.4 to 0.86 ms and the frame p95 from 50.4 to 17.7 ms. Paused is unchanged, by the owner's choice. Both
  suites exit 0 on its branch (3,851 CPU cases; 548 GPU passed and 1 skipped). Drift during playback now measures
  zero paths; ADR-440 recorded 20.
- **The GV3 art pass has launched:** worktree `../av-gen-art`, branch `gv3/art-pass`, from `581c018c`.
- **Superseded:** once qa/coord's suites pass, launch the art pass from qa/coord's head. The final merge into main waits for qa/ci.
- **Superseded, kept for the record:** when W1 finishes and qa/perf is merged, launch `gv3-art-pass` in the freed slot.

## The final report
`FINAL-REPORT.md` (`d7f624ed`) answers the brief's 11 questions. Where this file's interim numbers differ from it,
the report and `perf-summary.md` (the clean retake) win.

## Current Status
2026-09-28:
- Phase 0 is complete: the inventory and the plan are in `PLAN.md`.
- Three agents have been launched: W1 perf, W2 ci and W3 clean.
- Each starts with measurement or triage only. No changes are made before its baseline is recorded.

## Completed
- 2026-09-28, **the old worktrees removed** (owner's decision): all 17 GV3-era worktrees. Each had a clean tracked
  tree, and their branches are kept. gv3-int's review snapshots from r1 to r7b and the cast trace (2.0 GB) were
  moved to `~/Documents/GitHub/av-gen-archive/gv3-int-review-snapshots/` first.
- 2026-09-28, **W3 cleanup** (`qa/clean` `b8edd995`, merged into qa/coord `3b3d323c`):
  - **GV3 r7b installed** as `examples/world/glowmere-valley-3{,.scene}.json`, with the song path relative. It
    loads with 0 errors: 105/105 routes, 130/130 tracks, 18/18 effects, 73 shots. Main's generator reproduces it
    (`make_glowmere_valley_3.py && gv3/ufo.py --no-trace`): the scene byte-identical, the project differing only in
    the audio path.
  - **Removed:** nine `_diag-water-*` arms and `_pre-defects` (5.8 MB), with nothing referencing them.
    `check_project_integrity` went from 20 problems to 9 (the 9 left are `treeisland/_vx2-*`, whose generator no
    longer runs).
  - **Three engine defects fixed, with tests:** the silent orb fallback now shows a status-bar error;
    `--export-bundle` carries a top-level light rig; the batch flags imply `--headless`.
  - **The GV3 state audit** is in `gv3-state-audit.md`: no broken references; 12 effect values stored twice with
    the parameter winning (the aurora's audio sensitivity is 0 as rendered); hidden beam pools, hero pulses and
    ground pools passed to W1 to measure.
- 2026-09-28, Phase 0: the inventory; worktrees `av-gen-qa-{coord,perf,ci,clean}` branched from `77ea4247`, with
  assets linked; the plan.

## In Progress
- W1: checkpointed for the restart (`cd1db417`). Open: the clean baseline retake, the regression-2 bisect, the GPU cost on wides one feature at a time, the editor `ui.build` p90 spike of about 36 ms, and `avgen_render_tests`.
- W2: triage of the red CPU run (`4a138886`) and the failing nightly sanitizer stages.
- W3 is DONE and merged into qa/coord (`3b3d323c`). Its full serial CPU suite on `qa/clean` passed: 3,843 cases,
  3,823 passed, 19 skipped, and 1 failed as expected (the `[!shouldfail]` slope lean); 9,124,064 assertions passed.

## Queued after this pass: GV3 targeted art pass
- **Brief:** `docs/glowmere-valley-3/art-pass/00-brief.md`, the owner's words, 2026-09-28.
- **Water references:** `~/Desktop/av-gen-review/20-gv3-art-pass/reference/`, not in the repository.
- **Gate:** it launches only when W1 (perf) and W3 (clean) are complete and merged, so it starts from the cleaned
  GV3 in `examples/world/` and the fixed engine.
- **The agent:** one agent, the `gv3-art-pass` type (`~/.claude/agents/gv3-art-pass.md`: `model: opus`,
  `effort: max`). It needs a Claude Code restart to load, because this was the first file in `~/.claude/agents/`.
  Without the restart, fall back to `model: opus` and tell the owner.
- **Where it works:** its own worktree from main after the merge, with notes in
  `docs/glowmere-valley-3/art-pass/PROGRESS.md`.
- **It is a feature pass:** the UFO warp and the pulse lifecycle are new behaviour. That is allowed there, not
  in the QA pass. It gets W1's performance envelope, so the warp's frame cost is measured.

## Queued after this pass: astronaut musician prototype (runs in parallel with the art pass)
- **Brief:** `docs/prototypes/astronaut-musicians/00-brief.md`, the owner's words, 2026-09-28.
- **The agent:** one dedicated agent, the `astronaut-prototype` type (`~/.claude/agents/`: opus, effort max).
- **Launch gate:** the QA pass is complete, then the owner restarts Claude Code, then the coordinator confirms the
  Blender MCP tools are present. Blender MCP is NOT registered yet: the Blender add-on is installed, but Claude Code
  has no server configured. The registration steps are in the brief's header.
- **Assets:** `~/Desktop/musician_assets/`. Their licences are unknown, so they are never committed.
- **GV3 is untouched by this task.**

## Findings
- The repository's GV3 (`334c4cf6`, the Phase 3 foundation) is older than the r7b project on the Desktop, which is
  the one the owner opens.
- The Desktop bundle contains the purchased models and the song. It must not be committed.
- Hosted macOS runners cannot compile the renderer's Metal pipelines: 363 of 443 GPU cases fail, according to
  `ci.yml`. An authoritative GPU job needs real Apple GPU hardware.

## Performance Baselines
W1's interim, checkpointed 2026-09-28 at `qa/perf` `cd1db417`, not merged. Its full CPU suite exits 0.
`avgen_render_tests` has NOT been run on it yet; that is owed before merge. Details: `perf.md`.

**Why GV3's wides collapse.** ADR-834 (`29e6918e`, 2026-09-25) runs `world::heroSightline` every frame for every
in-frame character. Each call is 9 rays, sampling the terrain every 2 m. The only output is
`character.<name>.visibility`, which nothing shipped reads.
- **Evidence:**
  - 7,436 of 7,437 main-thread samples fall in it;
  - engine-update ms against (characters x metres) gives r = 0.975 over the 73 shots;
  - it runs even while the editor is paused (75 ms per frame on a paused wide), which is the "navigation is
    slow" complaint.

**Worst wide (s66), 1280x720 headless, 3 repeats.** These are INTERIM numbers, measured under contention. The clean
retake supersedes them: 376.2 -> 235.4 -> 32.2 ms. See `perf-summary.md` and `FINAL-REPORT.md`.

| build | frame | FPS |
|---|---:|---:|
| main `77ea4247` | 402.8 ms | 2.5 |
| + ADR-950 (the ground query asks for 2 values; bit-identical) | 254.7 ms | 3.9 |
| + ADR-951 (the visibility sightline runs only when the signal is read) | 33.4 ms | 30 |

- **With the fixes:** GV3 runs at 30-43 FPS at 720p and 40-58 FPS at 360p on every measured shot; the live
  editor's playing wide goes from 322 ms to 9 ms.
- **What is left is ordinary renderer cost.** The wide is GPU-bound at about 24 ms: the scene pass is 19.3 ms
  (614k triangles), against 6.5 ms (135k) on a close-up. grove runs at 61 FPS at 720p on every build.
- **Caveat:** every absolute number was measured while other agents' CPU suites were running (the load is recorded
  per run). The clean baseline retake is owed.

**Verdicts:**
- **A, complexity:** not the collapse. What remains is the known renderer cost, which scales with triangles on wides.
- **B, project state:** NO. Ten removal arms (hero pulses, all effects, staging and plans, routes, automation, scout
  and fields, all entities, the hidden 32k beam pools, ground pools back to GV2's) moved nothing beyond the spread
  on the fixed build. The hidden beam pools are not simulated. The static report finds no duplicate or dead state.
- **C, regression:** YES, in two steps. See Performance Regressions.

## Bugs Found
Known from the GV3 wrap-up, and not yet triaged:
1. A project whose scene fails to load falls back silently to the orb scene.
2. `--export-bundle` drops `lightRig`.
3. `--export-bundle` without `--headless` opens the windowed app.

- **Found and fixed by W2** (each with a regression test that failed first):
  1. `scene.delete_node` / `scene.set_parent` from the assistant skipped `Engine::rebind()`. Routes kept a dangling
     pointer, and the app segfaulted on the next frame.
  2. A known project key with the wrong type (e.g. `"format": 1`) threw out of `loadProject` and terminated the app.
     It is now a load error.
  3. The flaky "Cancellation stops execution" test: one pump drained the whole queue on a loaded runner. The test
     now pumps one item at a time; 40 of 40 runs passed.
  4. A stale sha256 for `glowmere-valley.wav` in the manifest.
- **Found by W2, NOT fixed:**
  - G2: a load that fails part-way leaves the project half-replaced, and Cmd+S then overwrites the previous file.
  - G3: a missing scene file keeps the previous composition installed.

## Performance Regressions
GV2 multicam wide, 720p:

| build | frame | engine update | GPU |
|---|---:|---:|---:|
| `3e09f9e1` (before ADR-834) | 25.6 ms | 3.2 ms | 17.2 ms |
| `29e6918e` (the ADR-834 merge) | 47.4 ms | 23.6 ms | 17.9 ms |
| `77ea4247` (main) | 65.7 ms | 40.6 ms | 20.0 ms |
| main + ADR-950/951 | 28.0 ms | 3.6 ms | |

1. **ADR-834's per-frame sightline** (+20 ms of engine update; draws and triangles identical). FIXED by ADR-950 and
   ADR-951.
2. **Each sightline got about 1.7x more expensive after ADR-834** (another +17 ms). Suspect: ADR-893's terrain
   changes. It is neutralised by 951, which removes the calls. **Resolved later:** it was bisected to `0fd83284`
   (ADR-893/894), where each sightline became 1.79x as expensive. The bisect worktrees are removed.
3. **The GPU scene pass is +2.5-3 ms (+18%)** while clustered lights went from 206 to 88-115. Suspect: ADR-945's
   ecology lights. It may be an accepted look change. **Resolved later**, on the clean retake: +1.9 ms (+11%).
   ADR-945's glow pools are 1.3 ms, an accepted look change. Creep across `0b623b88..22ce5c3d` is about 1.1 ms,
   each part under 0.5 ms and not narrowed further.

## CI Status
W2 checkpointed 2026-09-28 at `qa/ci` `bfa795b4`, pushed, not merged. Details and the monitor's resume point:
`../av-gen-qa-ci/docs/qa-pass/ci.md`.

| Check | State |
|---|---|
| Push CI (`ci.yml`) | GREEN on qa/ci (36434148550, 36437357981). The CPU gate takes about 8 min, down from 22. 36444149446 and 36461173197 are in progress |
| Hosted GPU | informational only; the hosted Metal device cannot compile the pipelines |
| Self-hosted GPU (`gpu` job) | written, gated off; waiting on the owner's actions below |
| `cpu-assets` (nightly, private assets) | written, gated off |
| ASan (nightly, 5 jobs, 11 rest shards) | heavy-2 and heavy-3 PASS; heavy-1 and rest-a/b still running (36434165586) |
| UBSan (now its own job, `halt_on_error=1`) | running (36434165586); risk of hitting the 330 min limit, in which case split it |
| TSan (now nightly, concurrency set) | PASS: 243 cases, 0 reports, 40 min of tests in an 82 min job. The whole-suite probe 36434180499 will probably time out |

**What changed:**
- one composite build action;
- `tools/ci/run-suite.sh`, one command that runs what CI runs;
- caches saved only from main, plus a prune script (the repository was at 10.7 of 10 GB);
- the scores regenerated on CI, which un-skips 26 cases; the exceptions file is now empty;
- the farm-asset cases skip cleanly through one shared guard.

**Minimal private asset set** (`tools/ci/test-assets.list`, measured with an open() tracer): the GPU suite needs 63
files (65 MB); the CPU Glowmere tier adds 133 files (44 MB), plus optionally the Tree of Life (152 MB). That is
against 1.4 GB in `assets/`.

## Remaining Problems
- **Committed renders:** `examples/treeisland/renders` has 28 PNGs (51 MB) committed despite its `.gitignore` rule.
  They are listed and not removed.
- **Dead data:** unknown-parameter entries (`material/*/op/9/*`, `emissionIntensity`) and `lod.lodCount` are in
  every Glowmere scene. Removing them needs a GPU frame-hash proof.
- **Foot slip (for the art pass):** the test log warns "travelling at 0.02 m/s against a walk clip authored for
  3.24 m/s ... rate matching is on and saturated". This fits the owner's report of pre-footstep sliding.

## Decisions Required From Human
- **GPU CI setup (owner actions),** from `docs/development/gpu-ci-private-assets.md` on qa/ci:
  1. run `tools/ci/make-test-assets-repo.sh`;
  2. `gh repo create ... --private --push`;
  3. pin the sha in `tools/ci/test-assets.lock`;
  4. add a read-only deploy key as secret `AVGEN_TEST_ASSETS_SSH_KEY`;
  5. `gh variable set AVGEN_TEST_ASSETS true`;
  6. register a self-hosted runner labelled `avgen-gpu`.
  The runner is recommended on the PRIVATE repository, because a fork PR can run its own workflow file on a public
  repo's self-hosted runner.
  **Decisions:** whether the songs go into the private repository; where the runner lives; whether to track three
  CC0 Quaternius fixtures (about 4 MB).
- **Farm GLBs in public history** (`4bdc42ff`): removing them needs a history rewrite. The owner earlier chose not
  to; this is recorded here only for completeness.
- **ADR-951** makes an unread `character.<name>.visibility` signal read 0 instead of a computed value. No shipped
  project reads it. Reverting commit `97925c18` alone restores the old behaviour. Accept?
- **The aurora's audio sensitivity** is 0 in the parameter (the one that wins) and 1.0 in the block, so as rendered
  the aurora ignores the audio. Is that intended?
