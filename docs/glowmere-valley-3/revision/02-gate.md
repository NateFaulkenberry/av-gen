# Phase 0: the evaluator gate

The owner's brief puts a hard gate before any scene edit: the quality evaluator must be installed,
usable by the production agent, and part of the loop. The evaluator is **Creative Critic**, a
standalone local project at `~/Documents/GitHub/creative-critic`. It was built from the owner's own
evaluator specification.

The checks below were run by the production agent against the running service, using GV3's real
renders and scene data. The evaluator agent's own claims were not relied on.

## Verification, 2026-09-26

| Gate item | How it was checked | Result |
|---|---|---|
| Installed and usable | `critic start --daemon`, `critic health`, `critic capabilities` | passes |
| Results without polling | `critic submit ... --wait --json`, run as a background command. It blocks on the event stream and exits 0 (completed), 2 (failed), 3 (cancelled) or 4 (unreachable), so the production agent is notified when the command exits. | passes |
| Three depths | fast: the whole film's scene data, 40 shots, 8.4 s. Preview: two shots, 29–36 s cold. Deep: one shot, 3.7 s once cached. | passes |
| Inspect shots and frames | `critic shot`, `critic findings --shot`, `critic finding`, `critic frame --t --exact` | passes |
| Compare iterations and keep history | v1 then v2 of s16 and s29 on one session track (details below). `critic session history` shows the trend. | passes |
| Temporal behaviour | stability, flashes, novelty (when a shot stops showing anything new), and a full-length luma stream, on video inputs | passes, for video |
| Audio reactivity, configured vs observed | The kick route to the elder (the first pass's heartbeat) scored "configured but no measurable response" in v1 (z −1.1) and "configured and observed" in v2 (+9.5% at 0 ms, z 4.1). The production's own measurement was +10%. | passes, for video |
| **A fresh render, submitted** | s16 rendered as 222 PNG frames and submitted as a sequence at film time 85.4 s | **fails** (below) |

**The v1 → v2 comparison.** It named exactly the two changes the production had made:
- the heartbeat route became visible (finding resolved);
- s29's clipped pixels fell from 23% to 15% (the dimmer beam).

It measured no degradation, which is correct.

**What fails.**
- **A video clip cannot be placed at its film time.** A `--range` render starts at local t=0, so its shots, traces, route event times and audio would all misalign.
- **A frame sequence is placed correctly, but the evaluator silently skips its core temporal analyses.** Audio reactivity, stability, novelty, flashes and visibility were all skipped, while the job reported "completed" with a normal headline. The skip appeared only in the analyzer list, not in the uncertainty section or the headline. That is the same silent-no-op pattern this production keeps finding in the engine.

Both are being fixed in the evaluator, with tests. The gate stays closed until a fresh render of one shot goes through end to end: render → submit → reactivity, stability and pacing measured → compared with the previous iteration.

## Limitations, accepted for now
- **Learned aesthetic models do not run by default.** On 40 version-to-version preferences from GV3, the best reached 0.65 pairwise accuracy (95% CI 0.50–0.78), and none beat chance on the v1→v2 changes. The deterministic measurements carry the loop.
- **The generic per-shot audio-reactivity grid is weak on short shots.** It sees too few events, and it missed the heartbeat that the route-locked check caught. Route-locked checks are the ones to trust.
- **The fast mode cannot see occlusion.** It projects bounding boxes, so foliage and terrain never block anything.
- **Several thresholds were tuned on GV3.** The labels behind them were the production agent's own judgments, not independent ratings.
