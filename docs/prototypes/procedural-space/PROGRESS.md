# Procedural Space POC: progress

Resume from here. Branch `proto/procedural-space` in `../av-gen-space`. ADR block 1000-1019 (1000 used).

## Status

| phase | state | notes |
|---|---|---|
| 1 architecture | done | `ARCHITECTURE.md`, ADR-1000: host on the existing ADR-027 SDF path as composition `sdf` nodes |
| 2 research | in progress | `RESEARCH.md` |
| 3-6 foundation | not started | |

## Rules in force

- The GPU belongs to the owner until about 23:45 on 2026-09-29. After that, all GPU work, **including
  `avgen_tests`**, goes through `tools/gpu-lock.sh`.
- Build: `cmake --preset release && cmake --build --preset release`.
