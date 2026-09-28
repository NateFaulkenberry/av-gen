# QA pass: progress

The plan is in `PLAN.md`, and the brief in `00-brief.md`. Each workstream's detailed notes live in
`perf.md`, `ci.md` and `clean.md`, on each agent's branch until they merge.

## Current Status
2026-09-28:
- Phase 0 is complete: the inventory and the plan are in `PLAN.md`.
- Three agents have been launched: W1 perf, W2 ci and W3 clean.
- Each starts with measurement or triage only. No changes are made before its baseline is recorded.

## Completed
- 2026-09-28, Phase 0: the inventory; worktrees `av-gen-qa-{coord,perf,ci,clean}` branched from `77ea4247`, with
  assets linked; the plan.

## In Progress
- W1: the GV3 performance baseline and the shot sweep.
- W2: triage of the red CPU run (`4a138886`) and the failing nightly sanitizer stages.
- W3: integration of GV3 r7b from the Desktop into `examples/world/`.

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

## Findings
- The repository's GV3 (`334c4cf6`, the Phase 3 foundation) is older than the r7b project on the Desktop, which is
  the one the owner opens.
- The Desktop bundle contains the purchased models and the song. It must not be committed.
- Hosted macOS runners cannot compile the renderer's Metal pipelines: 363 of 443 GPU cases fail, according to
  `ci.yml`. An authoritative GPU job needs real Apple GPU hardware.

## Performance Baselines
(pending W1)

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
(to be filled in)

## Decisions Required From Human
(none yet)
