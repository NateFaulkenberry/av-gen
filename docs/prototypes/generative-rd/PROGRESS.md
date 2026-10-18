# Generative R&D: progress

Branch `proto/generative-rd`, worktree `../av-gen-rd`, from main `dd27374c`.

**Docs:**
- the brief: `00-brief.md`;
- the plan: `01-plan.md`;
- the experiments: `A-oscillators.md`, `B-flow.md`, `C-reaction-diffusion.md`;
- the costs: `performance.md`;
- the recommendation: `recommendation.md`.

**Media:** `~/Desktop/av-gen-review/63-generative-rd/{A-oscillators,B-flow,C-reaction-diffusion}/`:
- `autonomous/`: the first review round;
- `compare/`: the same camera and window (40-60 s), autonomous next to Feline Footwear, plus Trench and Hyperborea
  for the lead of each experiment.

## How to resume

**Build:** `cmake --preset release && cmake --build --preset release`.

**Generate:** `python3 tools/generative_rd/{oscillators,flow,reaction_diffusion}.py`. Each writes
`examples/experiments/<id>/` for every configuration, plus the Experiments index entries.

**Options for one configuration:**
- `--config <name>`;
- `--autonomous` (no audio fields or routes);
- `--palette` / `--regime`;
- `--variant --out <v.json> [--wav <w>] [--camera x,y,z:tx,ty,tz] [--set path=json] [--track path=v@t,...]` writes a
  file project for review renders.

**Render** through the GPU lock with `AVGEN_GPU_LOCK_TIMEOUT=14400`, and sleep 15 s afterwards:
`avgen --headless --project <v.json> --render <out.mp4|dir> --range a:b --fps 30 --size 960x540`.
- Render at 15 fps or more: a grid takes at most `maxSubSteps` (4) sub-steps a frame, so a 6 fps render runs the
  simulation slower than real time.

**Live profile:** `avgen --live-profile --project examples/experiments/<id>/<id>.json --audio
assets/audio/feline-footwear.wav --mode live --target-fps 30 --size 1920x1080 --start 40 --json <f>`.

**Test audio:** `assets/audio/feline-footwear.wav`, `trench.wav`, `hyperborea.wav`.

## Log

1. **Audit and plan** (`01-plan.md`). Most of the machinery exists: GPU grid simulation with seek and checkpoints,
   Gray-Scott, fibres along streamlines, particles with field forces, onset and spectrum fields.
2. **Engine** (commit `3cbde25b`):
   - ADR-1450, the oscillator grid;
   - ADR-1451, an RD plane at speed with a regime field.

   Unit and GPU tests: `[oscillator]` and `[rd]`.
3. **C, autonomous.** Relief, bloom and pins; five palettes; regime survey (numpy). Reviewed by the coordinator:
   - relief strong;
   - fix the flat plane (now a valley that fills the frame) and the bloom's void (now a close framing);
   - keep the pins.
4. **A, autonomous.** Membrane, flaps and loom; regime survey (numpy). The activity channel was redefined as phase
   strain: with a lag, the torque is K sin α everywhere. Reviewed:
   - membrane the lead, islands the most distinctive;
   - give unison life (now a frequency tilt);
   - the flaps need framing (bigger, closer);
   - the loom weakest (dropped).
5. **B, autonomous.** Meadow, storm and ink, plus ADR-1452 (scalar and vector planes). Found:
   - the compound-is-scalar particle gap;
   - the field-position doubling (generator fix);
   - dotted field names silently reading 0.

   Reviewed: B weakest; one attempt allowed. The current lines (streamline rakes with travelling pulses) were added,
   and ink was dropped.
6. **Audio and MIDI** for all three (`music()`, `rd_music()`, `flow_music()`; `common.instrument`). Six CCs each,
   with Audio Influence as the depth of every audio route, plus MIDI notes. Amounts were tuned on Feline Footwear
   40-60 s.
7. **Media** (`compare/` folders), **performance** (`performance.md`: live profiles plus a grid-on/off A/B; the oscillator weights were made separable, 6.3 to 2.6 ms; the RD planes went to 384² and 320² at 12 iterations, 11.5 to 7.5 ms; the fog march was dropped, 7 ms), **docs**, and **final checks**, all exit 0 (`recommendation.md`, end). Status: complete; the coordinator merges.

8. **The RD Pins live scene** (owner decision, 2026-10-17):
   - research: `C-scene-research.md`;
   - plan, stage log and final checks: `C-scene-plan.md`;
   - generator: `tools/rd_pins/build.py`, which writes `examples/rd-pins/`;
   - ADR-1453: an RD grid can start from authored values, used for the pre-roll;
   - media: `~/Desktop/av-gen-review/65-rd-pins-scene/`.

   Current Lines is hidden (flow stays a tool). Status: complete; the coordinator merges.
