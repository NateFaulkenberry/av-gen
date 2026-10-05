# Live optimizer, Phase 5: perceptual / scene-aware optimization (plan, 2026-10-04)

**Branch:** `live/optimizer-p5` (from main `ba0003de`), worktree `../av-gen-opt`. **ADRs 1108-1119** are this phase's
(1108-1113 allocated below; 1114-1119 spare). Spec: `00-brief.md` "PHASE 5" (5.1-5.6) and "Phase 5 -- LATER /
ADVANCED". Report: `~/Desktop/av-gen-review/32-live-optimizer-phase5/REPORT.md`.

**What it builds on (reused, not rebuilt):** `avgen --live-profile` (ADR-1090..1093) and its per-entity data (Stage 5
groundwork); the importance-weighted, hero-exempt levers (ADR-1094..1098); the candidate rules and `--verify-candidates`
(the `--ab` machinery, `compareArms`); the ceilings the project keeps (ADR-1100/1101, `applyLeverToCeiling`); the
Quality Lab's metric engine (`tools/quality-lab`, ADR-250); the Creative Critic (`critic` CLI, daemon at :8765); the
agent tools (ADR-1106) and their child-process hook.

**Rules that run through every stage (the owner's):**
- The optimizer never depends on the Critic. Critic scoring is an optional extra; with no Critic every stage works.
- Estimated and measured numbers stay in separate fields everywhere (JSON, text, tool answers). A combination's
  saving predicted from measured singles is ESTIMATED until the combination itself is measured.
- Visual equivalence is never claimed. The strongest statement allowed is "within the measured self-difference floor"
  (ORIGINAL rendered twice and compared with itself, same metrics), and only when every metric is inside it.
- No compat shims (ADR-441). Everything that ships is reachable from the CLI and an AI tool and exercised end to end.

## Stage A: contribution analysis (5.1, 5.2) -- ADR-1108

- The live profile's per-entity rows gain the projected radius in pixels (the caster floor's own formula, so the
  analysis predicts exactly what `castercull` removes), screen coverage, the importance weight and a
  **contribution** = coverage / lever weight (hero: protected, never ranked as low).
- A `contribution` section of `avgen.liveprofile/1`:
  - top contributors; **low-contribution shadow casters** (non-hero casters under the caster floor at 8/16/24/48 px:
    count, summed coverage, names), **LOD candidates** (non-hero, under 0.5% of the frame), **particle emitters**
    beyond the particle cull (per system: distance, reach, importance);
  - **hero regions** (screen boxes of every on-screen hero entity), for Stage C's region metrics;
  - **suggested heroes** when the scene declares none (the largest visible contributors), never applied.
- Text report: a CONTRIBUTION block. Limits stated: bounding spheres, not silhouettes; a small caster's shadow can be
  large; coverage is the camera's at the measured instant.

## Stage B: hero preservation (5.3) -- ADR-1109

- **Audit** every live lever against the brief's hero list (geometry, shadow, animation, particles, reactive effects).
  Known leaks to close: `particleSpawnScale` applies to hero emitters; the draw-distance lever's CPU half
  (`DetailLimits::distanceScale`) slows hero rigs and hero entity bands. Fix each in the engine; test each.
- **Candidates carry a hero classification:** `heroEffect` = `exempt` (a per-object lever the renderer skips heroes
  for), `image-wide` (changes every pixel uniformly, heroes included: resolution, fog resolution, post), or `degrades`
  (reduces a hero's own representation: the `noprograms` diagnostic). Default policy **protect**: `degrades` is never
  proposed, applied or searched; `image-wide` is admitted but its effect inside the hero regions is MEASURED (Stage C)
  and weighs double in the search's rank. **strict**: image-wide levers excluded too. `--hero-policy protect|strict`.

## Stage C: ORIGINAL vs OPTIMIZED A/B (5.4) -- ADR-1110

- `avgen --live-profile --compare <lever,lever|project>` (headless, deterministic): from the measured start, render
  ORIGINAL and OPTIMIZED at the same piece times (settle frames, then a run of consecutive captured frames), plus
  ORIGINAL a second time for the **self-difference floor**; and time both with the counterbalanced A/B.
- Metrics live in the Quality Lab (ADR-250: the engine does not link the Lab), as a new `avgen_quality ab`
  subcommand run across a process boundary, like the Critic: **pixel** (mean |dRGB|, changed-pixel fraction, PSNR),
  **structural** (SSIM, MS-SSIM), **edge** (Sobel magnitude difference, edges lost/added), **luminance** (mean luma
  delta, and SSIM / pixel at **matched luminance**), **temporal** (difference of frame-to-frame change, activity
  ratio), each also inside the hero regions. Schema `avgen.abdiff/1`.
- Frames written as PNGs beside the record (`--ab-dir`) so a person can look at them.

## Stage D: candidate-combination search (5.5) -- ADR-1111

- `avgen --live-profile --optimize` (headless). Target: max(GPU, CPU work) p50 <= budget x margin (0.9, ADR-1107's).
  1. Baseline measured. 2. Every admissible single (a ceiling form, the hero policy, risk <= `--optimize-risk`) is
  MEASURED: time (A/B) and visual change (Stage C). Singles inside the noise are dropped (visual cost, no saving).
  3. Combinations (up to 3 levers) are ranked by ESTIMATED saving (sum of measured singles, labelled) and ESTIMATED
  visual change; the cheapest-looking ones that reach the target by estimate are then MEASURED, as combinations.
  4. Chosen: the measured combination that reaches the target with the least measured visual change (rank key:
  whole-frame 1-SSIM at matched luminance + hero-region 1-SSIM). None reaches: the best measured, said plainly, and
  the live controller is what remains.
- Also reported: the **low-risk set** (low-risk, hero-safe singles with a measured saving outside the noise).
- Record section `optimization`; text block OPTIMIZATION.

## Stage E: Critic (optional) and agent workflow (5.4 loop, 5.6) -- ADR-1112, ADR-1113

- `--critic` on `--compare` / `--optimize`: the ORIGINAL and chosen OPTIMIZED frame runs submitted to the Critic
  (`critic submit --sequence`, then `critic compare`); its answer is stored as the Critic's, beside, never inside, the
  objective metrics; unreachable = "critic: unavailable" and nothing else changes. (ADR-1112)
- AI tools (ADR-1113), through the ADR-1106 child-process hook: `performance.optimize_scene` (target fps; `apply`
  writes the chosen levers as project ceilings in `settle`), `performance.apply_low_risk_optimizations` (applies the
  measured low-risk set), `performance.benchmark_before_after` (`--compare`: the project's ceilings or given levers vs
  none). "profile scene" and "find largest bottleneck" are `performance.profile_scene` (its critical path and
  categories). `tools/live_scene_profile.py` passes the new flags through.

## Test plan

- CPU (`avgen_tests`): contribution maths (radius px vs the caster floor's formula; caster table; suggested heroes);
  hero classification of every candidate lever; the search's selection logic on synthetic measurements (estimated vs
  measured kept apart; noise-only singles dropped; a combo reaching by estimate but not by measurement not chosen); the
  `ab` metrics (identical frames = zero; exposure shift moves luminance but not matched-luminance SSIM; an edge blur
  moves the edge metric; region masks); argument parsing; the tool command lines; hero exemptions on the CPU levers
  (rig rate, entity bands).
- GPU (`avgen_render_tests`): particle spawn scale leaves a hero emitter's count unchanged.
- End to end (measured, under `tools/gpu-lock.sh`): `--compare` and `--optimize` on Sonic, Glowmere Valley 2,
  Liminal all-you-got @60 s; an AI-tool call through a headless session if one is reachable.
- Both full suites at the end, one after the other, exit codes recorded.
