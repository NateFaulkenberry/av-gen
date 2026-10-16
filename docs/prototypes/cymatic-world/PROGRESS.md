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
   from geometry alone; low tone -> broad rings, high tone -> hexagonal cells. Coordinator's notes (2026-10-16): a
   visible world edge, a flat plane with displacement, a murky grade. Answered with a terraced amphitheatre (WorldMap
   terraces, gullies, outcrops, rim, two mountain rings), basalt spires on every horizon, a raking warm moon, a
   silent-vs-loud proof (`prototype-a/a3-silent-landform.png`).
5. ADR-1401 (`normalStep`): the black ragged craters were the 5 km mesh's 3.7 m finite-difference normal step.
6. Prototype B: a crescent "choir" of blades was rejected (a fence of cones, the brief's avoid list); replaced by the
   druse: 34 stepped quartz-habit crystals radiating from one root on the basin floor, tuned by length (listening
   along the cluster's height; long core = low modes).
7. Prototype C: particles on nodal shells. The fixes: the lattice made evanescent (later a general source `depth`),
   the singer's own source (finer `scale`), the force reading the slow figure, critical damping (the grains rang
   radially), an orbit and trails to weave the shells, a bell cut, a veil of falling strands. Note: a particle field
   force's strength is hard-capped at 100.
8. Performance: the ground's lit path (~7 ms) and its program (~12 ms) dominated; one `shading` field op, one `relief`
   deformer, an unlit ground with painted light: 33.2 -> 26.6 ms GPU at 1080p Ultra; live (`--mode live`, Auto, 30
   fps) holds at High.
9. Coupling (ADR-1402): probes to the bus, a driven source, competition and limiter per source; the druse rings
   sympathetically with the ground at its foot (`coupling/sympathetic-resonance-off-on.png`).
10. Instrument: response.* routes (intensity -> complexity, sustain -> memory, level/kick -> the lamp, kick -> veil),
    8 CCs and 2 pads; registered as the live example "Cymatic World".
11. Final media in `final/`; origin/main merged (43993c75); suites, integrity, review_examples: see `02-report.md`.

## Open items

- The medusa still reads as a woven dome / hairy ball in dense passages (grains crossing between shells).
- Far crater rims alias against the mesh; the crystals' stepped ledges read as stripes from some angles.
- Next experiment: bake the field to a plane texture once a frame (see `02-report.md` section 9).
- `examples/cymatic-world/meshes/cw-ground.glb` is ~6 MB (committed like Digital Ocean's meshes).
