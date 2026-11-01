# CHRONOS: progress

Branch `scene/chronos` in `../av-gen-chronos`, from main 214dafd8. ADRs 1930-1969 (used: 1930-1933; next 1934).
Review media: `~/Desktop/av-gen-review/104-chronos/gate<N>/`.

| Gate | Phases | State |
|---|---|---|
| 0 | recon, plan, proof clips | **approved** (all five recommendations accepted) |
| 1 | 1 ring and slit scan; 2 canyon and camera | **done, awaiting the owner** (`04-gate1.md`, media in `gate1/`); ADR-1930 handed back early as 3d73b32c |
| 2 | 3 effects; 4 audio | not started |
| 3 | 5 acts, tuning, perf, full render | not started |

## Gate 0

- The existing temporal family (ADR-410, 1049, 1066) is the base. CHRONOS extends it and does not build a second
  system.
- The gaps that matter:
  - the ring is capped at 32 frames;
  - its depth reallocates when a route moves `frames`;
  - there is no strip store for slit photography;
  - there is no RGB temporal displacement and no region fracture;
  - feedback is FIR-only (by ADR-410);
  - the offline render has no temporal pre-roll.
- The proof clips are rendered from the Strange Attractor flythrough (main):
  - the engine's existing slit;
  - an offline numpy preview of a longer ring and a strip ring.
- Open decisions for the owner are in `03-plan.md` §6.

## Gate 1

See `04-gate1.md`.
- ADR-1930: the mesher extraction, handed back early (3d73b32c, both suites at exit 0 in a clean worktree).
- ADR-1931: the ring (capacity, stride, metadata) and the sampler, plus the debug-capture leak fix.
- ADR-1932: the render pre-roll.
- ADR-1933: the canyon.
- Temporal cost: 0.10 ms at 1080p.
- The clean flight's jitter: p99 0.042, max 0.104.
- The canyon's live frame is 60.6 ms (the fog march is 39.8 ms of it). That is Gate 3's problem.

Lesson recorded: never edit the worktree while a suite runs. The tests read shaders, the ADR index and sources at run
time, so the first run on 3d73b32c was invalid. Final suites run in `../av-gen-chronos-ci`, a clean worktree checked
out at the commit under test.
