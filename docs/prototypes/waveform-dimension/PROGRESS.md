# Waveform Dimension: progress

Branch `proto/waveform-dimension` (worktree `../av-gen-waveform`, from main `74edc063`). Brief `00-brief.md`, with
the owner's corrected palette appended at its end. Audit `01-resonance-audit.md`. Review media:
`~/Desktop/av-gen-review/61-waveform-dimension/`.

## How to resume

- Build: `cmake --preset release && cmake --build --preset release`.
- Generate: `WD_PROTO=<A|B|C> python3 tools/waveform_dimension/build.py`. It writes `examples/waveform-dimension/`
  and the index entry.
- Review variant: `python3 tools/waveform_dimension/build.py --variant --out <v.json> [--camera x,y,z:tx,ty,tz]
  [--set path=json]`.
- Render through the GPU lock: `AVGEN_GPU_LOCK_TIMEOUT=14400 tools/gpu-lock.sh build/release/src/avgen --headless
  --project <v.json> --audio assets/audio/feline-footwear.wav --render <dir> --format png --range t:t+0.034
  --size 960x540`. Sleep 15 s between GPU jobs.
- Test tracks: `assets/audio/feline-footwear.wav` (dense), `trench.wav`, `hyperborea.wav`. Never All You Got.

## Log

1. Audit (`01-resonance-audit.md`). Fields are 3D. Rotated readers give oriented families. Level sets can be
   sphere-traced through SDF `displaceField` using the plane trick. A volume bake (ADR-1440) is approved in
   principle and waits on the Stage A cost.
