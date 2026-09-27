# The Critic's AV Gen adapter, Phase 3 update: the agent's report

Reported on 2026-09-27. Creative Critic `main` `88a928e..6ed180c` (7 commits, local only). The
coordinator restarted the daemon on 8765 at about 11:40, on the new code. The reports of both jobs
are in `~/Desktop/av-gen-review/18-glowmere-valley-3/revision/critic-phase3/`.

## What changed
- **Set-piece events from the cast trace's `setPieces`** replace the hard-coded GV3 times.
  - There is one event type per moment (`ufo beam`, `ufo lift`, …), so set pieces are compared with each other. The first pass's beats under one type had read as "repeats in the same place".
  - There is one `ufo taken` event per set piece, placed where the animal was last visible.
  - The hidden beats (`rest`, `transit`, `hover`) are skipped.
  - Without set pieces, the adapter uses the events the trace or scene declares, else none, and says so.
- **Ground samples:** `--world-preview` probes the engine's own ground height every metre around each body's path, and leaves everywhere else unknown. The grounding check skips invisible and `airborne` samples, and reports `judged_frac`.
- **Other:**
  - a set piece's craft counts as a vehicle;
  - shots default to the scene's own cut (`build/gv3/shots.json` is one frame late on every cut);
  - a shot with no subject falls back to its label;
  - `--scene` defaults to the project's scene.
- **Tests:** 48 passed, 0 failed (was 37). They include a real `avgen_cast_trace` run on AV Gen's set-piece lab.

## Regenerating GV3 inputs (INTEGRATION_GUIDE.md §6)
```bash
cd ~/Documents/GitHub/creative-critic
E=~/Documents/GitHub/av-gen-engine-2/build/release     # or av-gen-engine
P=<gv3 worktree>/examples/world/glowmere-valley-3.json
W=work/<name>
$E/tools/avgen_cast_trace --project $P --seconds 225.5 --hz 20 --out $W/cast.json
.venv/bin/python adapters/avgen/avgen_adapter.py --project $P --cast $W/cast.json \
  --lightrig <gv3 worktree>/examples/lightrigs/glowmere-valley.rig.json \
  --directives <gv3 worktree>/docs/glowmere-valley-3/03-directives.md \
  --shot-plan <gv3 worktree>/docs/glowmere-valley-3/04-shot-plan.md \
  --world-preview $E/tools/avgen_world_preview --climax drop --out $W
.venv/bin/critic submit --inputs $W/inputs.json --mode fast --session gv3 --track film --wait --json
```
A full-film cast trace takes about 20 minutes under load.

## Measured on GV3's first-pass project (engine `040d6644`)
- **The fast job** `job_1a0e365909bf5581b`: 63 issues (8 high, 16 medium).
- **Grounding can now be judged for all 17 bodies.**
  - Animals: all grounded (−0.06 to +0.17 m).
  - Aliens: four float while walking. ember and vane on 40% of their tracks, up to 0.51 m; rook 20%, up to 0.26 m; sage 11%, up to 0.20 m. The cause is not established; it is not the ground grid, and it has been handed to gv3-cast.
- **With gv3-cut's UFO-plan trace:** 18 events from E1–E5, and no variety finding (the set pieces are at least 80 m apart per moment). **In the first-pass cut, E1–E4 are off screen,** which shows in `framing[].on_screen`; no rule flags it yet.

## Follow-ups for AV Gen
- The Director's evaluator hook (ADR-931) calls the adapter without `--world-preview`, so its evaluations lack ground data.
- The cast trace's `atRetire` is recorded after the hidden body is dropped back to the ground.
- `scout` needs a hero record, for its radius of about 4.9 m.
