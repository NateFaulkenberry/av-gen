# Cymatic World: progress

Branch `proto/cymatic-world` (worktree `../av-gen-cymatic`, from main `4421568b`). Brief `00-brief.md`, design
`01-design.md`. Review media: `~/Desktop/av-gen-review/58-cymatic-world/`.

## How to resume

- Build: `cmake --preset release && cmake --build --preset release`.
- Generate the scene: `python3 tools/cymatic_world/build.py` (writes `examples/cymatic-world/`; the ground GLB's
  heights come from `build/release/tools/avgen_world_preview --heights`, so build first).
- A review variant driven by a WAV: `python3 tools/cymatic_world/build.py --variant assets/audio/trench.wav --out
  <v.json> [--camera x,y,z:tx,ty,tz] [--set path=json]`, then `avgen --headless --project <v.json> --render <dir>
  --format png --range t:t+0.034 --size 960x540` through `tools/gpu-lock.sh`.
- Test audio conditions used: quiet = all-you-got 7 s; sustained = trench 130 s; transient = night-shift 12 s; dense =
  feline-footwear 44 s; plus a synthetic probe (55 Hz 0-8 s, 330 Hz 8-16, 2 kHz 16-24, chord 24-32, silence, clicks
  40-48), made by the snippet in this log (kept in the scratchpad, not the repo).

## Log

1. Research and design (`01-design.md`): the field is a new FieldKind, so every existing field consumer reads it.
2. ADR-1400 resonance field (`src/spatial/resonance.*`, `shaders/fields.wgsl`, FieldBlock mode table): CPU unit tests
   `[resonance]`, GPU parity `avgen_render_tests [resonance]`. Pure function of the audio history (seek = play).
3. Additions found necessary by looking at frames:
   - a slow envelope (`memory`) for the sand channel, so relief settles rather than jumping with every hit;
   - the figure is COHERENT (F = sum e_i phi_i, sand where F = 0): an incoherent energy has only nodal points, and
     the ground read as egg-crate bumps; coherent gives Chladni lines;
   - mode competition (`competition`): many driven modes otherwise make a maze; one figure leads;
   - a lattice's modes share their orientation (nested crystal instead of moire).
4. Prototype A (ground): first critique sheet `prototype-a/a1-first-critique-sheet.png`, `a2-sheet.png`. Field reads
   from geometry alone; low tone -> broad rings, high tone -> hexagonal cells. Coordinator's notes (2026-10-16):
   visible world edge, still a flat plane + displacement, murky violet grade. In progress: terraced amphitheatre
   geography (terraces, gullies, outcrops, rim, two mountain rings), raking shadowed moon, silent-vs-loud proof.

## Open items

- Ragged dark patches in hollows came from glossy stone reflecting the dark lower sky; roughness raised.
- Volumetric haze at 0.0006 already washed the frame: atmosphere to revisit with the composition.
- `examples/cymatic-world/meshes/cw-ground.glb` is ~6 MB (committed like Digital Ocean's meshes).
