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
2. **Scene generator:** `python3 tools/phase_wall/build.py` writes `examples/experiments/phase-wall/`.
   - Review variant: `--variant --out <v.json> --camera nave|east|corner|end|up [--wav w] [--set p=json]`.
   - Strobe metric: `python3 tools/phase_wall/fdiff.py <clip.mp4>` (gate p50 ≤ 6, p95 ≤ 12).
3. Proof generator (stage 0, throwaway): `python3 tools/phase_wall/proof.py --out <dir> [--flap square|blade]
   [--back lit|dark] [--pitch p] [--single]`. It writes a project outside the repository.
4. Render: `AVGEN_GPU_LOCK_TIMEOUT=14400 tools/gpu-lock.sh build/release/src/avgen --headless --project <p.json>
   --render <dir|mp4> --range a:b --fps 30 --size 960x540`, then `sleep 15`.

## Log

- **2026-10-17. Stage 0** (research, debate, proofs, plan). The research is written. Proof generator: one wrapped
  network read by four walls of a closed hall, with the reader frames checked numerically (corner continuity exact).
  Status: see the end of `02-plan.md`. **Paused for the coordinator's review before any building.**
- **2026-10-17. Stage 0 done.**
  - Proofs P1-P7 in `66-phase-wall-scene/00-proofs/`.
  - Chosen: the closed hall, blades, the painted face plus soft bands, the promenade camera.
  - Flicker gate (frame-diff p50 ≤ 6 / p95 ≤ 12) set against the R&D clip (5.4 / 7.4).
  - Live: the proof hall at full 1080p, GPU 13.4 ms, 0 misses.
  - Flicker measure: `scratchpad fdiff.py` logic. Reproduce with ffmpeg frames plus the mean |Δ| at 960 px wide; to
    be folded into `tools/phase_wall/` in Stage 1.
  - **Next: Stage 1 after the coordinator's review.**
- **2026-10-17. Coordinator approved the plan, with art notes:**
  1. the blades stay the hero, with bold, legible islands;
  2. the room is dark, tinted and palette-driven, not a grey office;
  3. favour oblique views, with no dead-centre corridor;
  4. index "Experiments"; the name clash is settled at merge.
- **Stage 1 (the hall), done.** `tools/phase_wall/build.py`, scene `examples/experiments/phase-wall/`.
  - **Geometry:** 8,064 blades (0.745 × 1.585 m, closed pitch, no see-through gaps, so no gap moiré). The network is
    293 × 25 at a 1.2 m cell. Nine GPU fields.
  - **The breath re-timed for the coarser lattice:** islands are held at K 4-5.5 from 16 to 34 s and from 66 to 80 s,
    and unison at K 12 from 44 to 56 s.
  - **Legibility (note 1).** These fixed the proofs' grey sheen:
    - the face tone runs over the full sin θ range (trough to metal), per pixel;
    - four wall-wash directionals, one per wall, from high in front of it (a wall is lit only by its own);
    - metallic 0.6, roughness 0.38;
    - bands at 2.5;
    - strain lights whole blades (an emission effector, read as `instanceEmissive`), so defects are crisp lit seams,
      not a cloud.
  - **Room (note 2, first pass):** dark tinted coffers, dark stone piers, and a dark lacquered metal floor (metallic,
    f0 = a dark teal, so its reflections are tinted) with a spill of `back` near the walls.
  - **Strobe:** islands p50 0.5 / p95 0.6; unison (K 12, the whole room swinging) p50 5.2 / p95 9.0.
  - **Media:** `66-phase-wall-scene/01-hall/`.
  - **Known for Stage 2:**
    - the floor still shows broad warm-white sheens (the bands and the washes reflected), which need the palette's
      tint;
    - the crest tone dominates the islands state (more trough contrast wanted);
    - unison at the trough instant reads as a dark louvre screen (it alternates with bronze about every 5 s).
