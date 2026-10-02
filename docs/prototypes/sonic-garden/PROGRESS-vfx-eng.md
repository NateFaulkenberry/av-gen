# Sonic Garden VFX expansion: engineering progress

Brief: `02-brief-vfx-expansion.md` (the owner's words govern). This file is the engineering agent's; the art
agent keeps its own notes. Worktree `../av-gen-sonic`, branch `proto/sonic-garden`. ADR block **1060-1079**.

## Resume here (cold)

- Research reports 2, 4, 5 (`research/`) and `VFX-ARCHITECTURE.md` are written and committed.
- Next: implement in the order of VFX-ARCHITECTURE.md section 5. The coordinator made the live Effect Library trigger
  (`TriggerSource::Signal`) and live kick/snare/hat the top priority (the art agent is blocked on it).

## Rules in force

- GPU work through `tools/gpu-lock.sh` (including every full `avgen_tests` run).
- Every new effect off by default and byte-identical when off; offline deterministic, seek == play.
- Commit only my own paths (`git commit -- <paths>`): the art agent works in the same worktree.
- No new dependencies without asking.

## Plan

See `VFX-ARCHITECTURE.md` section 5 (ADR-1060..1065).

## Capabilities landed (name, usage)

(none yet)

## Measurements

(none yet)
