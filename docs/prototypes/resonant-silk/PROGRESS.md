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
5. Stage B fixes:
   - A spectrum `audioSmooth` above about 0.34 s exceeds the 32-tap cap, and the sparse taps alias into
     stripes. It is now 0.33.
   - The resonator field was renamed `resonator`. A field and a procedural node sharing the name `silk` had left
     every `procedural/silk/*` parameter unregistered.
   - Material op paths are `material/<prog>/op/<n>/<kind>/<field>`.

   Gate 2 passed. Dense music (Feline 40-70 s) was over-lit; that is fixed in Stage C.
6. The palette brief (`03-brief-palettes.md`, owner) supersedes the single palette.
   - The engine gap was a direct blend, select, lock and advance. Approved by the coordinator as ADR-1420, the
     shared mechanism for Silk and Cymatic World: `src/params/palette.*`, the step call and the seek replay in
     `src/app/engine.cpp`, and tests. Commit `b8dc088a`, kept separate from the scene commits.
   - The eight palettes, roles bound to geometry and light:
     - environment: the sky and the fog;
     - shadow: the fill light;
     - base and fold: the albedo, Fresnel-masked;
     - grazing: the key light and the hems;
     - resonant: the rim light, the sail lamp, and the fronts and ringing folds;
     - peak: the gather seam and the treble sheen.

     Per palette, the tuning sets roughness, film thickness and ior, key intensity, emission, fold contrast and a
     spectral sheen (`irid`).
   - The journey on `beat.phrasePulse` has a 40 s hold, in the order A F E C G B H D.
   - Palette select on CC 9, lock on CC 10.
   - Project `parameters` cannot set `palette/*`, because the palette registers after they are read. Variants
     write the selector block instead (`--set palette/select=k`).
   - `pal8.sh <tag>` (scratch) renders the eight locked palettes at Feline 50 s and composes a sheet.
7. Stage C fixes from the coordinator's notes:
   - The sail is now a thin, broad scarf. Its cloth follows its span, so no pleatless band crosses it. Its hem is
     a curved, fluttering arc, and two lamps graze it.
   - The near wall moved back 40 m, and the music's displacement fades near the camera (`nearCalm`), so the
     pleats read crisp.
   - Oil-Slick and Opal are told apart by a view-dependent spectral sheen at true grazing angles.
   - The gather glow is a 9 m seam.
8. Stage D: performance. 190 ms GPU at 1080p Ultra came down to 30.5 ms, by these changes:
   - no AO or contact shadows, so no depth prepass;
   - spectrum taps 32 to 8;
   - treble as a uniform;
   - no per-pixel resonance read;
   - 52k vertices.

   Live Auto settles at High, at 30 fps, with 0 misses. The bass swell travels at 65 m/s, so its history rows sit a
   vertex apart and show no stepped ribbing. The bass/mid split is at 250 Hz, and the mid source sits beside the wall
   at 2.6x wavenumber.
9. Owner process: one merge of all three resonance scenes, done by the coordinator; the category is `Resonator`; no
   more review clips.
   - ADR-1421 (the Cymatic agent's energy-change advance) was cherry-picked as `ddac5683`.
   - Silk enables it: sonic energy, brightness and density; averages of 3 s and 24 s; primaryTimeout 30 s;
     maxHold 90 s.
   - It was checked on 1 fps thumbnails, classified by colour (scratch `classify.py`), on Feline, Trench and
     Hyperborea, with and without a phrase signal.
