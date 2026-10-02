# Sonic Garden VFX expansion: engineering progress

Brief: `02-brief-vfx-expansion.md` (the owner's words govern). This file is the engineering agent's; the art
agent keeps its own notes. Worktree `../av-gen-sonic`, branch `proto/sonic-garden`. ADR block **1060-1079**.

## Resume here (cold)

- Done and committed:
  - `643b3dba`: research 2/4/5 and `VFX-ARCHITECTURE.md`;
  - `4bc1a762`: ADR-1060 (causal onsets, live kick/snare/hat) and ADR-1061 (Signal triggers). Sent to the
    coordinator.
- In progress: ADR-1062, the response model (`response.*`), MIDI per-note signals, voice slots
  (`notes.voice.<i>.*`), pitch-class lanes (`notes.class.<k>`), and the `sonic/response/*` parameters with Live panel
  sliders.
- Then, in the coordinator's order: the live scene switcher (Live panel list, next/previous, a shortcut, MIDI
  program change; keep live input and the projection across the switch); then `publish`, the post/temporal effects,
  the evaluator (it reads each scene's `sonicScene` block and the scene's `composition.focalPoints`), performance.
- The art agent works in this worktree (`tools/sonic_vfx/`, scenes). Never commit its files.

## Rules in force

- GPU work through `tools/gpu-lock.sh` (including every full `avgen_tests` run).
- Every new effect off by default and byte-identical when off; offline deterministic, seek == play.
- Commit only my own paths (`git commit -- <paths>`): the art agent works in the same worktree.
- No new dependencies without asking.

## Plan

See `VFX-ARCHITECTURE.md` section 5 (ADR-1060..1065).

## Capabilities landed (name, usage)

- **ADR-1060 causal onsets** (`4bc1a762`): `AnalysisFrame::causal` (ratio, hit and strength per class kick/low/snare/
  hat/onset; bassDb, levelDb, snareDb, hatDb, snareRise). Live `audio.onsetLow/Mid/High` now fire. Live events are
  carried until acquired (`LiveEventLatch`). Kit test P/R: kick 1.00/1.00, snare 1.00/1.00, hat 0.94/0.98.
- **ADR-1061 Signal triggers** (`4bc1a762`): `"trigger": {"source": "signal", "name": "<bus event>", "threshold": t}`.
  Derived from the piece for files (seek-exact), recorded live.

## Measurements

(none yet)
