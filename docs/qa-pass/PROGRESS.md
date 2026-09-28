# QA pass: progress

The plan is in `PLAN.md`, and the brief in `00-brief.md`. Each workstream's detailed notes live in
`perf.md`, `ci.md` and `clean.md`, on each agent's branch until they merge.

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
- W1: the GV3 performance baseline and the shot sweep.
- W2: triage of the red CPU run (`4a138886`) and the failing nightly sanitizer stages.
- W3 is DONE and merged into qa/coord (`3b3d323c`). Its full serial CPU suite was still running at hand-back, and
  its exit code is the check before main.

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
Interim from W1, measured while the other agents were loading the machine, so these are ratios and not
baselines. Headless, 640x360, the 73 shots of GV3 r7b:

| | base | ADR-950 fix | sightline off |
|---|---:|---:|---:|
| median frame | 57.7 ms | 41.7 ms | 21.0 ms |
| shots under 10 FPS | 20 | 13 | 0 |
| worst shot (66) | 389 ms | 252 ms | 26.8 ms |

- **The cost is on the CPU, in `heroSightline` (ADR-834).** Each in-shot character is marched to every frame, and
  the frame cost against characters x distance gives r = 0.975. With the sightline off, the GPU frame is at most
  17.2 ms in any shot.
- **ADR-950:** the ground query asks for 2 values instead of a full sample. The output is bit-identical and 3.6x
  cheaper per sightline.

## Bugs Found
Known from the GV3 wrap-up, and not yet triaged:
1. A project whose scene fails to load falls back silently to the orb scene.
2. `--export-bundle` drops `lightRig`.
3. `--export-bundle` without `--headless` opens the windowed app.

## Performance Regressions
(pending W1)

## CI Status
| Check | State (2026-09-28) |
|---|---|
| PR/push CI (`ci.yml`) | RED: CPU tests failed on `4a138886`; the run on `77ea4247` is in progress |
| GPU CI | informational only (hosted VM); nothing authoritative |
| ASan/UBSan (`sanitizers.yml`, nightly) | RED: stages 0, 1, 5 and main failed on 2026-09-27 |
| TSan | Sundays only; not run recently |
| Nightly/extended | `ci.yml` nightly exists |

## Remaining Problems
- **Committed renders:** `examples/treeisland/renders` has 28 PNGs (51 MB) committed despite its `.gitignore` rule.
  They are listed and not removed.
- **Dead data:** unknown-parameter entries (`material/*/op/9/*`, `emissionIntensity`) and `lod.lodCount` are in
  every Glowmere scene. Removing them needs a GPU frame-hash proof.
- **Foot slip (for the art pass):** the test log warns "travelling at 0.02 m/s against a walk clip authored for
  3.24 m/s ... rate matching is on and saturated". This fits the owner's report of pre-footstep sliding.

## Decisions Required From Human
(none open)
