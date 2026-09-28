# W2: CI, sanitizers, GPU CI and test assets (notes)

Agent 2's working notes for the QA pass. Branch `qa/ci`, worktree `../av-gen-qa-ci`. Kept current so a
successor can resume from here. The plan is `PLAN.md` (W2); the brief is `00-brief.md`.

## Status
- 2026-09-28: deliverable 1 (triage) done; fixes in progress on `qa/ci`.

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

## Log
- 2026-09-28: triage.
