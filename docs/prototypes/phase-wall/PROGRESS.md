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
   - Dead-space gate: `python3 tools/phase_wall/sweep.py write <dir> --palette i`, render `<dir>/list.txt`, then
     `sweep.py score <dir>` (60 frames; exit 0 = pass).
   - Review palette: `--palette i` on a variant selects and locks state i.
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
- **2026-10-17/18. Stage 2 (environment, light, colour) and the palettes, done.** The coordinator's Stage 1 notes
  were: the blades read as pixels; one mid value; a white floor sheen.
  - **The blades as louvres:**
    - a bevel (0.018 m, one segment), so each edge catches a highlight;
    - a shading profile in each blade's own frame (localPosition): a dish across the width, a lift toward the top,
      and soft shadow lines at the four edges that sink to 30%, never black.
  - **The value range:**
    - a three-stop ramp (trough, mid, crest) over the full sin θ range;
    - spread 0.15 → 0.28, which gives more islands per view, so dark, mid and bright are all on screen;
    - troughs lifted to a coloured dark, so a trough island is a colour, not a hole.
  - **The palettes:** 8 role-based states with the ADR-1420 selector and the ADR-1421 change fallback:
    - **the states:** Bronze Tide, Ember, Gilt Orchid, Coral Lagoon, Verdigris, Aurora, Cobalt Ice, Nocturne;
    - **the roles:** environment, back, trough, mid, crest, wash, front, peak, trim, floor;
    - every colour in the hall is bound to a role: 4 blade programs, the floor program, the backplanes, the strips,
      the inlay, the piers, the coffers, the 6 lights, the sky, the fog and the post tint. The selector is in the
      project now; the CC 9/10 routes come with the MIDI map in Stage 4.
  - **The floor:**
    - the 4 wall washes and the key are `diffuseOnly`, which killed the warm-white sheen;
    - the floor is **unlit**: lacquer in the `floor` role, with fbm mottling, plus each wall's own tone pooled on the
      floor beneath it (the floor program reads the 4 sin θ readers; a reader's frame maps a floor point to the
      wall's bottom row);
    - a bounce light (`back` role, diffuse only, from below) lights the coffers' soffits.
  - **Gates:**
    - **sweep:** 0 fails out of 60 frames on Nocturne (worst 4.6%) and on Ember (worst 4.0%). The threshold was
      recalibrated to near-black at a linear luminance under 0.008; the R&D A3 frames score 37-41% dead on it.
    - **strobe:** islands p50 0.5 / p95 0.7; unison p50 5.5 / p95 7.6.
  - **Live:** `--live-profile --mode live --audio feline --target-fps 30` holds at Medium (1362×766) with GPU 26.9 ms
    and **0 misses**.
    - The cost is geometry/opaque: the per-pixel programs (the 33-op blades, the 36-op floor with 4 field reads), up
      from 13.4 ms in the proof.
    - Key shadows were removed (6 ms, invisible).
    - Without `--target-fps 30`, Auto targets the 60 Hz display and drops to Emergency. Stage 6 should look at this.
  - **Media:** `66-phase-wall-scene/02-colour/`.
- **2026-10-18. Stages 3-7.** The coordinator's colour-stage notes were: islands read as mottled noise; a bright
  blob; floor as stone is fine.
  - **Islands:** a 1.6 m cell, coupling radius 3, spread 0.17, noise 0.02, tilt 0.006, after A/B matrices of cell,
    radius, spread and tilt.
    - Radius 4 on 19 rows coupled the whole height and drew vertical stripes.
    - Spread 0.12 locked the room.
  - **Seams:**
    - activity 0.2-0.5, squared;
    - the front glow is gated by a breath track (0.12 in disorder, 1.0 from the islands on), so disorder no longer
      floods the walls;
    - the "blob" was the defect core and peak glow, which is now narrower.
  - **Camera:** the promenade, 8 oblique and off-axis keys over 120 s.
  - **Audio and MIDI:** the routes in `03-report.md`; held sound is +3 K (+6 locked the hall on Feline).
  - **Index:** "Phase Hall" (Experiments, live).
  - **Performance:**
    - depth prepass, 30.0 → 23.2 ms headless Ultra;
    - the bevel swapped for a program edge highlight (live misses 7 → 1);
    - live at High: GPU 16-17 ms; misses 1 (Feline), 1 (Trench), 5 (Hyperborea).
  - **Sweep:** 0 fails on Aurora.
  - **Media:** `03-final/`.
  - **Removed:** `tools/phase_wall/proof.py`.
- **2026-10-18. Final checks on a4973dce, all exit 0:**
  - cpu-assets 4318 passed / 4338;
  - gpu 670 passed / 671;
  - review_examples "Phase Hall" ok;
  - integrity 0 problems.

  Status: **complete; the coordinator merges.** Report: `03-report.md`.
