# Liminal Euclidean World: engineering progress (PROGRESS-eng)

*The engineering agent's running notes, kept current so a cold successor can resume. The art agent keeps
PROGRESS-art.md. Governing documents: `00-brief.md`, `01-addendum-emotion.md` (wins where they differ).
Design: `ENGINEERING.md`.*

## Where things stand

- Branch `proto/liminal-space`, worktree `/Users/natefaulkenberry/Documents/GitHub/av-gen-liminal`. Never push or merge.
- ADR block 1040-1059 (1040 vocabulary nodes, 1041 spring/integrate stages, 1042 journey camera, 1043 palette are
  planned).
- Shared worktree: commit only engineering paths with `git commit -- <paths>`. The art agent owns ART-RESEARCH,
  SONG-ANALYSIS, DIRECTOR-PLAN, `tools/liminal/`, PROGRESS-art.md.

## Plan (Phase A, time box 23:00)

| step | what | status |
|---|---|---|
| 0 | ENGINEERING.md (research + feasibility) | done |
| a | `stairs`, `screw`, `warp` SDF nodes (CPU, interpreter, compiler, params, tests, docs) + `tools/liminal_sdf.py` | next |
| b | `spring` + `integrate` route-chain stages (seek-exact via ADR-901) | |
| c | journey camera (composition camera mode 3), collision guard, journey anchors | |
| d | project `palette` block (OKLab, position/saturation/value) | |
| e | props per cell, figure on the journey | |
| f | `examples/liminal/` via `tools/make_liminal_example.py`, capture to `~/Desktop/av-gen-review/24-liminal-space/eng/` | |
| 3 | analyzer: SDF adapter + temporal checks (critic lives in `../creative-critic`, see notes) | if time |

## Build and test

```sh
cd /Users/natefaulkenberry/Documents/GitHub/av-gen-liminal
cmake --preset release && cmake --build --preset release -j 8
tools/gpu-lock.sh ./build/release/tests/avgen_tests            # CPU suite; ONE FAILED line (slope lean) is expected
tools/gpu-lock.sh ./build/release/tests/avgen_render_tests     # GPU suite
./build/release/tests/avgen_tests "[sdf]"                      # quick filter (still under the lock for full runs)
```

## Notes for a successor

- Creative Critic is a separate repo, `/Users/natefaulkenberry/Documents/GitHub/creative-critic` (Python; adapter
  `adapters/avgen/avgen_adapter.py` is GV3-shaped and yields 0 shots for an SDF project; analyzers in
  `src/critic/analyzers/*.py`, rules in `src/critic/synthesis.py`).
- Seek-exactness comes from ADR-901: route chain state is replayed on the 60 Hz grid. New chain stages must keep
  all their state in `ProcessorChain::State`.
