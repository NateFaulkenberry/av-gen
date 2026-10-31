# CHRONOS: progress

Branch `scene/chronos` in `../av-gen-chronos`, from main 214dafd8. ADRs 1930-1969 (none used yet).
Review media: `~/Desktop/av-gen-review/104-chronos/gate<N>/`.

| Gate | Phases | State |
|---|---|---|
| 0 | recon, plan, proof clips | **done, awaiting the owner** (`02-recon.md`, `03-plan.md`, media in `gate0/`) |
| 1 | 1 ring and slit scan; 2 canyon and camera | not started |
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
