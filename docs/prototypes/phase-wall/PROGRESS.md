# Phase Wall: progress (resumable)

Branch `proto/phase-wall`, worktree `../av-gen-phasewall`, from `proto/generative-rd` d0e62cf9 (assets linked).
Owner's ask: "oscillator A3-flaps looks good too - build this into a full scene, no dead space in the background."

**Docs:**
- the research: `01-research.md`;
- the debate, the proof renders and the staged plan: `02-plan.md`;
- this log.

**Media:** `~/Desktop/av-gen-review/66-phase-wall-scene/`.

**Rules that bind this work:**
- ADRs only in 1460-1469;
- no edits to `src/scene/material_program.*` or `shaders/material.wgsl` (quality pass, ADR-1470);
- ask before touching `src/spatial/grid_field.*`, `src/rendering/simulation.cpp` or `shaders/simulate.wgsl` (R&D
  agent), and tell the coordinator before any shared engine file;
- every GPU job through `tools/gpu-lock.sh` with `AVGEN_GPU_LOCK_TIMEOUT=14400`, with a 15 s gap after;
- test audio: Feline Footwear, Trench, Hyperborea (never All You Got);
- no push or merge.

## How to resume

1. Build: `cmake --preset release && cmake --build --preset release`.
2. Proof generator (stage 0, throwaway): `python3 tools/phase_wall/proof.py --out <dir> [--flap square|blade]
   [--back lit|dark] [--pitch p] [--single]`. It writes a project outside the repository.
3. Render: `AVGEN_GPU_LOCK_TIMEOUT=14400 tools/gpu-lock.sh build/release/src/avgen --headless --project <p.json>
   --render <dir|mp4> --range a:b --fps 30 --size 960x540`, then `sleep 15`.

## Log

- **2026-10-17. Stage 0** (research, debate, proofs, plan). The research is written. Proof generator: one wrapped
  network read by four walls of a closed hall, with the reader frames checked numerically (corner continuity exact).
  Status: see the end of `02-plan.md`. **Paused for the coordinator's review before any building.**
