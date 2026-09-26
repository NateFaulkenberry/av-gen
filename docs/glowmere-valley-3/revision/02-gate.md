# Phase 0: the evaluator gate

The owner's brief puts a hard gate before any scene edit: the quality evaluator must be installed,
usable by the production agent, and part of the loop. The evaluator is **Creative Critic**, a
standalone local project at `~/Documents/GitHub/creative-critic`. It was built from the owner's own
evaluator specification.

The checks below were run by the production agent against the running service, using GV3's real
renders and scene data. The evaluator agent's own claims were not relied on.

**Status: passed on 2026-09-26.** GV3 scene edits may begin once the Director work they depend on
has merged.

## Verification, 2026-09-26

| Gate item | How it was checked | Result |
|---|---|---|
| Installed and usable | `critic start --daemon`, `critic health`, `critic capabilities` | passes |
| Results without polling | `critic submit ... --wait --json`, run as a background command. It blocks on the event stream and exits 0 (completed), 2 (failed), 3 (cancelled) or 4 (unreachable), so the production agent is notified when the command exits. | passes |
| Three depths | fast: the whole film's scene data, 40 shots, 8.4 s. Preview: two shots, 29–36 s cold. Deep: one shot, 3.7 s once cached. | passes |
| Inspect shots and frames | `critic shot`, `critic findings --shot`, `critic finding`, `critic frame --t --exact` | passes |
| Compare iterations and keep history | v1 then v2 of s16 and s29 on one session track (details below). `critic session history` shows the trend. | passes |
| Temporal behaviour | stability, flashes, novelty (when a shot stops showing anything new), and a full-length luma stream, on video clips and frame sequences | passes |
| Audio reactivity, configured vs observed | The kick route to the elder (the first pass's heartbeat) scored "configured but no measurable response" in v1 (z −1.1) and "configured and observed" in v2 (+9.5% at 0 ms, z 4.1). The production's own measurement was +10%. On fresh renders: below. | passes |
| **A fresh render, submitted** | s16 rendered fresh twice and submitted at film time 85.4 s: as 222 PNG frames (480×270, with the song as audio) and as a 960×540 video clip (with its own audio track) | **passes** (second round, below) |

**The v1 → v2 comparison.** It named exactly the two changes the production had made:
- the heartbeat route became visible (finding resolved);
- s29's clipped pixels fell from 23% to 15% (the dimmer beam).

It measured no degradation, which is correct.

**The first round failed.**
- **A video clip could not be placed at its film time.** A `--range` render starts at local t=0, so its shots, traces, route event times and audio would all misalign.
- **A frame sequence was placed correctly, but the evaluator silently skipped its core temporal analyses.** Audio reactivity, stability, novelty, flashes and visibility were all skipped, while the job reported "completed" with a normal headline. The skip appeared only in the analyzer list, not in the uncertainty section or the headline. That is the same silent-no-op pattern this production keeps finding in the engine.

Both were fixed in the evaluator with tests (`creative-critic` commits `35d317a`, `c740b66`, `88a928e`; 37 tests pass):
- clips take a film-time start;
- sequences run every check;
- any core check that does not run makes the job **PARTIAL**, in the headline, in `completeness` and in `uncertainty.not_evaluated`. `--strict` turns that into exit code 5.

## Second round, 2026-09-26: the gate passes

Run by the production agent against the restarted service, on shot s16 (85.4–89.1 s, the elder's heartbeat):

| Check | PNG sequence, 480×270 (`job_1a0ddcb2105b8078b`) | Video clip, 960×540 (`job_1a0ddcc01d73db1ed`) |
|---|---|---|
| Placed at film time | 85.40–89.10 s | 85.40–89.10 s |
| Completeness (`--strict`, exit 0) | complete: all 11 core checks evaluated | complete: all 11 core checks evaluated |
| The kick route to the elder | configured and observed: +8.9%, z 18.4, 33 ms lag | configured and observed: +9.9%, z 13.5, 0 ms lag |
| Stability, novelty, flashes, visibility | measured | measured |
| Audio | the film's song, placed in film time | the clip's own track, placed at 85.4 s; tempo read as 129.2 BPM from 3.7 s |

The production's own measurement of the heartbeat was +10%.

**Compared with earlier iterations:**
- **On the session track** (PNG, then clip): "No dimension moved beyond the noise band." That is correct: the scene is the same, and only the resolution and encoding differ.
- **Against the v2 film's s16** (`critic compare job_1a0dd94c9c4bdb8e1 job_1a0ddcc01d73db1ed`): water shimmer fell from 0.0146 to 0.0043. That is the F37 ripple fix, the one change made to this shot after v2. Nothing degraded. The comparison also flags that the two jobs cover different spans (v2's job also held s29), and it limits the like-for-like claims to s16.

**A remaining flaw, not blocking.** For a job made before the fix, the comparison's explanation says "a: no pixels". It means the old report has no coverage block, not that it had no pixels: six pixel pairs were compared.

## The single-shot loop, as run for the gate
```
# render one shot fresh (absolute path; the range is film time)
tools/gpu-lock.sh ./build/release/src/avgen --project examples/world/glowmere-valley-3.json \
    --render $PWD/build/gv3/gate/s16-clip-540.mov --size 960x540 --range 85.4:89.1

# evaluate it in film time; blocks on the event stream, so run it as a background command
~/Documents/GitHub/creative-critic/.venv/bin/critic submit --inputs work/gv3-v2/inputs.json \
    --video <clip.mov> --video-start 85.4 --options '{"only_shots":["s16"]}' --mode preview \
    --session gate-check --track s16-gate --label <name> --wait --json --strict
# exit 0 complete, 5 partial (a core check did not run), 2 failed, 4 evaluator unreachable

# a frame sequence instead: --sequence <dir> --fps 60 --sequence-start 85.4 --audio ~/Desktop/Rebuild.mp3
# compare any two jobs: critic compare <baseline job> <candidate job> --json
```
Run `critic submit` from `~/Documents/GitHub/creative-critic`, where `work/gv3-v2/inputs.json`
holds the adapter's scene and intent for GV3. Regenerate them with the adapter when the scene
changes.

## Limitations, accepted for now
- **Learned aesthetic models do not run by default.** On 40 version-to-version preferences from GV3, the best reached 0.65 pairwise accuracy (95% CI 0.50–0.78), and none beat chance on the v1→v2 changes. The deterministic measurements carry the loop.
- **The generic per-shot audio-reactivity grid is weak on short shots.** It sees too few events, and it missed the heartbeat that the route-locked check caught. Route-locked checks are the ones to trust.
- **The fast mode cannot see occlusion.** It projects bounding boxes, so foliage and terrain never block anything.
- **Several thresholds were tuned on GV3.** The labels behind them were the production agent's own judgments, not independent ratings.
