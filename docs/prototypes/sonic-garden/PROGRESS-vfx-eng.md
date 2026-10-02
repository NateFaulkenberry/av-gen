# Sonic Garden VFX expansion: engineering progress

Brief: `02-brief-vfx-expansion.md` (the owner's words govern). This file is the engineering agent's; the art
agent keeps its own notes. Worktree `../av-gen-sonic`, branch `proto/sonic-garden`. ADR block **1060-1079** (used
1060-1066).

## Resume here (cold)

- **Committed, in order:**

  | commit | what |
  |---|---|
  | `643b3dba` | research 2/4/5 and `VFX-ARCHITECTURE.md` |
  | `4bc1a762` | ADR-1060 (causal onsets, live kick/snare/hat) and ADR-1061 (Signal triggers) |
  | `67f74e6a` | ADR-1062 (the response model, per-note MIDI, the Live Response group, interpret ranges) |
  | `c52d1768` | ADR-1063 (the live scene switcher) |
  | `5c94b436` | ADR-1064 (the publish source) |
  | `337903f0` | ADR-1065 (post glitch and display) and ADR-1066 (temporal feedback and slit-scan, temporal timers) |
  | `0f809c90` | the evaluator, `tools/sonic_vfx_critic.py` |
  | `d44d7c51` | a hidden detector benchmark |
  | `1ccc0404` | Help documents PageUp/PageDown (test_help caught the binding) |
  | `f5e72f9d` | ADR-1067: drums under a mix (the art agent's finding), its matrix as `[adr1067]` |
  | `e6978f28` | ADR-1068: a note is not a drum (arp/chords false hits), `[adr1068]` |

  Every one was sent to the coordinator with its usage.
- **State:** the engineer's deliverables 11-16, 18 and 19 are all in. ADR-1067/1068 rebuilt the detector after the
  art agent's measurements. Its full-mix kick, snare and hat recall meets the targets.
- **Queue, in the coordinator's order:**
  1. the evaluator's four biases (palette roles, a periodic null, the slow tier, resolution);
  2. `response.*` in `--sonic-live-log`;
  3. a probe program-change scenario;
  4. a Voronoi F2-F1 material op;
  5. (cosmetic) the Mask prepass;
  6. the performance tiers;
  7. FULL suites only at the final hand-back (the GPU rule: in between, targeted tags only, one job per lock).
- **Measuring the drums:** `python3 tools/sonic_vfx/drum_recall.py --avgen $PWD/build/release/src/avgen`. Without
  `--avgen` it runs the art agent's pinned engine.
- **If resuming:**
  1. Re-run the suites if the head moved.
  2. Restore the tracked `temporal-*.png` with `git checkout -- temporal-*.png` (the GPU suite rewrites them).
  3. Pick up any request from the coordinator.
- **Open items for a successor:**
  - the evaluator does not yet read `composition.focalPoints`; it needs either a screen-space `regions` block from the
    art agent or a camera projection;
  - a real MIDI program change and a real drum kit have not been tested on hardware (no device on this Mac);
  - ADR-410's ring pre-roll after a seek is still unbuilt. The ring refills in at most `frames` frames, and the cold
    frames are deterministic.
- The art agent works in this worktree (`tools/sonic_vfx/`, `examples/sonic-vfx/`, `PROGRESS-vfx-art.md`). Never
  commit its files.

## Rules in force

- GPU work goes through `tools/gpu-lock.sh`, including every full `avgen_tests` run.
- Every new effect is off by default and byte-identical when off. Offline output is deterministic, and seek equals
  play.
- Commit only my own paths (`git commit -- <paths>`; in zsh pass an array). The art agent works in the same worktree.
- No new dependencies without asking.

## Capabilities landed (name, usage)

- **ADR-1060 causal onsets** (`4bc1a762`, refined `67f74e6a`):
  - `AnalysisFrame::causal` carries, per class (kick, low, snare, hat, onset), the ratio, the hit and the strength,
    plus `bassDb`, `levelDb`, `snareDb`, `hatDb` and `snareRise`.
  - Live `audio.onsetLow/Mid/High` now fire.
  - Live events are carried until acquired (`LiveEventLatch`).
  - The compression is relative to a causal spectral peak, so the detector is level-free.
- **ADR-1061 Signal triggers** (`4bc1a762`): `"trigger": {"source": "signal", "name": "<bus event>", "threshold": t}`.
  The events are derived from the piece for files (seek-exact) and recorded live.
- **ADR-1062 response model** (`67f74e6a`):
  - hits `response.{kick,low,snare,hat,onset,note}` (events), each with an `...Env` envelope;
  - levels `response.{bass,level,transient,sustain,flux}`;
  - presence `response.{hatRate,melodic,pitch,intensity}`;
  - per-note MIDI: `notes.voice.<0..7>.{held,velocity,pitch,age,on}`, `notes.class.<k>` and `notes.classOn.<k>`,
    `notes.{lastPitch,lastVelocity,interval,lowest,highest,velocitySpread,held,channel}`, and the events
    `notes.{release,low,high}`;
  - controls: `sonic/response/{sensitivity,transient,sustain,attack,release}` and the Live panel's Response group;
  - interpret bias and gain hard ranges are now +-64 and +-256.
- **ADR-1063 the live scene switcher** (`c52d1768`): the Live panel's Scene row, PageUp/PageDown, and MIDI program
  change. The performer's response carries across a switch as offsets.
- **ADR-1064 publish** (`5c94b436`): `{"kind": "publish", "settings": {"parameters": ["fx/a/b"]}}` publishes the
  signal `fx.a.b`.
- **ADR-1065 post instruments** (`337903f0`):
  - the `post/glitch` pass: `post/shock/*`, `post/glitch/*`, `post/split/*`, `post/sort/*`, `post/radial/*`;
  - the `post/display` pass: `post/display/*`.
- **ADR-1066 temporal** (`337903f0`): `temporal/feedback/*` and `temporal/slit/*`. The temporal passes now have GPU
  timers.
- **Evaluator** (`0f809c90`): `tools/sonic_vfx_critic.py {measure, inputs, compare, trace, selftest}`. The example
  report is in `~/Desktop/av-gen-review/25-sonic-vfx/eng/critic-example-garden-perc.md`.
- **Tools:** `tools/sonic_post_fx_sheet.py` makes a contact sheet of the effects on any project, or a cost table with
  `--bench`.

## Measurements (§19), Apple M2 Max

**CPU, per frame:**

| item | cost | notes |
|---|---|---|
| causal onset detector | 14.4 us per analysis frame | about 1.3 ms per second of audio. On the analysis thread live, at load for a file |
| Sonic update (character, response model, per-note facts, publish) | 2.0 us per render frame | it was 1.3 us before ADR-1062 |
| timbre pass | 121 us per analysis frame | unchanged |
| Signal-trigger derivation | one Sonic walk per piece, at the first query | the timbre is precomputed, so it is about the publish cost x frames |

**GPU, 1920x1080, Sonic Garden perc world, 240 frames.** Pass medians are at the 0.066 ms timer floor. The frame's
p50 is 21.56 ms at the default tier and 19.27 ms at Preview, with no effect on.

| effect | pass median (default) | frame p50 delta (default) | frame p50 delta (Preview) | memory / targets |
|---|---|---|---|---|
| shock ring | 0.066 | +0.13 | +0.13 | one transient HDR target |
| block glitch | 0.066 | +0.13 | +0.13 | one transient HDR target |
| line tear | 0.066 | +0.13 | 0.00 | one transient HDR target |
| RGB split | 0.066 | +0.13 | +0.07 | one transient HDR target |
| spectral split (8 taps; 4 at Preview) | 0.131 | +0.20 | +0.07 | one transient HDR target |
| pixel sort (32 taps; 16 at Preview) | 0.066 | +0.13 | noise | one transient HDR target |
| radial blur (12 taps; 6 at Preview) | 0.197 | +0.20 | noise | one transient HDR target |
| scanlines / mosaic / posterise | at most 0.066 | +0.07..0.20 | noise | one transient HDR target |
| feedback (8 taps) | 0.655 | +0.92 | +0.13 | the clean ring, about 2 MB a frame at half resolution |
| slit-scan (16 frames) | 0.066 | +0.20 | noise | the clean ring |

- Every effect is disabled dynamically by its amount, and off costs nothing (the pass is not encoded).
- Resolution dependence: every effect is one full-screen pass at output resolution. The ring effects read the ring at
  `temporalHistoryScale` (0.25 at Preview, 0.5 at Realtime/High, 1.0 at Offline).
- Tiers: Preview halves the tap counts (`postEffectTapScale`). Amounts, lengths and history lengths never change with
  the tier.

**Detector accuracy (test kit: 16 bars, 124 BPM, kick, snare, hats, a saw bass and a pad):**

| class | precision | recall |
|---|---|---|
| kick | 1.00 | 1.00 |
| snare | 1.00 | 1.00 |
| hat | 0.94 | 0.98 |

- Kicks fired on 0 of 32 bass notes, and `low` fired on all 32.
- A take 12 dB quieter gives the same kicks.
- ADR-898's clicky sine-bass groove: 7 of 63 bass notes read as kicks.

## Review media

`~/Desktop/av-gen-review/25-sonic-vfx/eng/`:
- `live-panel-response.png`: the Live panel with the Scene row and the Response group;
- `post-fx-sheet.png`: every post and temporal effect on the garden perc world;
- `critic-example-garden-perc.{md,json}`.

## Suites

(filled in at the hand-back)
