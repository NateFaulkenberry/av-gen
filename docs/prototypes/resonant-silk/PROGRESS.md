# Resonant Silk: progress

Branch `proto/resonant-silk`, worktree `../av-gen-silk`, from main `ef3774da`, with origin/main `74edc063` merged.
- Brief: `00-brief.md`.
- Resonance audit: `01-resonance-audit.md`.
- Report: `02-report.md` (at the end).
- Media: `~/Desktop/av-gen-review/59-resonant-silk/`, one folder per stage.

## How to resume

- Build: `cmake --preset release -DCPM_SOURCE_CACHE=../av-gen/.cache/cpm && cmake --build --preset release`.
- Generate the scene: `python3 tools/resonant_silk/build.py`. This writes `examples/resonant-silk/`: the scene, the
  project and `meshes/rs-silk.glb`.
- Research compositions: `--composition a|b|c --out-dir <dir>`. `a` is the chosen one, the Gather.
- Review variant with audio: `--variant --wav <wav> --out <v.json>`. The live project carries no audio; the
  variant drops `sonic.live` and names the WAV.
- Look-development switches:
  - `RS_NOMUSIC=1`: drift only, no music fields.
  - `RS_SAIL=<radians>`: the sail's extra roll (default -0.8).
- Render every frame through `tools/gpu-lock.sh` with `AVGEN_GPU_LOCK_TIMEOUT=14400`, and sleep 15 s afterwards:
  `avgen --headless --project <v.json> --render <out.mp4|dir> --range a:b --fps 24 --size 640x360`.
- Test audio:
  - a synthetic probe (scratch, generator in this log's step 4);
  - `assets/audio/feline-footwear.wav`, `trench.wav`, `hyperborea.wav`.

## Log

1. Audit (`01-resonance-audit.md`). Main has everything needed: the ADR-1400 resonance, and the ADR-1116 spectrum and
   onset fields for travel and wavefronts. No engine edits.
2. Stage A, the research board: three compositions rendered in the engine. The board is
   `stage-a/research-board.png`.
   - A, the Gather (chosen): a wall of pleats drawn through an invisible ring that opens into a sail.
   - B, the Passage: symmetric, it reads as a vanishing point.
   - C, the Canopy: no focal point.
3. Stage A, the material: lit satin (indigo, with a violet Fresnel edge) and thin film. The hems are a thin
   luminous line whose hue drifts along the length.
   - Key: cool pearl-cyan, grazing from behind right.
   - Rim: violet.
   - Sail lamp: a violet point light.
   - Sky: a soft gradient with no horizon line.

   Baseline at 1080p Ultra, headless: GPU 17.4 ms median, of which the scene pass is 15.2 ms.

   Gate 1 passed. The coordinator's notes:
   - the sail end looked cut: now a rounded, fluttering hem;
   - the sail was small and dim: it is larger and nearer;
   - the silent motion was too subtle: the noise drift is now 7 m at 0.06.
4. Stage B, music fields. First pass: displacement along the surface normal inflated the pleats into pillows, and
   the lattice drew regular corrugation. Rework:
   - Every displacement is along each half's MEAN sheet normal, using plane masks through the gather.
   - The mids have a curved point source instead of the lattice.
   - Amounts were lowered.
   - The probe WAV: drone 55 Hz 0-8 s, kicks 12-20, 330 Hz 20-28, snares 28-36, hats with 7 kHz 36-44, then
     silence.
