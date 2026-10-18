# Cymatic World: progress

Branch `proto/cymatic-world` (worktree `../av-gen-cymatic`, from main `4421568b`), then `proto/cymatic-split` for
the passes after it. Approved by the owner and on main at `74edc063` (2026-10-16). Brief `00-brief.md`, design
`01-design.md`, later briefs and reports `03`-`11`. Review media: `~/Desktop/av-gen-review/58-cymatic-world/`.

## How to resume

- Build: `cmake --preset release && cmake --build --preset release`.
- Generate the scene: `python3 tools/cymatic_world/build.py` (writes `examples/cymatic-world/`; the ground GLB's
  heights come from `build/release/tools/avgen_world_preview --heights`, so build first).
- A review variant: `python3 tools/cymatic_world/build.py --variant --out <v.json> [--camera x,y,z:tx,ty,tz]
  [--set path=json]` (an absolute scene path, still no audio), then `avgen --headless --project <v.json> --audio
  assets/audio/feline-footwear.wav --render <dir> --format png --range t:t+0.034 --size 960x540` through
  `tools/gpu-lock.sh`. The scene carries no track (`sonic.live`); `--audio` wins over the saved live input (ADR-1408).
- Generator switches (environment): `CW_PALETTE` (A, B, C; G = geometry-only grey), `CW_CAM` (A, B, C), `CW_DEBUG`
  (debug views), `CW_BAKE`, `CW_PREPASS`, `CW_MESH_STEP`.
- Live profile: `avgen --live-profile --project examples/cymatic-world/cymatic-world.json --audio
  assets/audio/feline-footwear.wav --mode live --target-fps 30 --size 1920x1080 --start <t> --quality <q> --json <f>`.
- Test audio: Feline Footwear (`assets/audio/feline-footwear.wav`, gitignored) for every render, clip and profile;
  Trench and Hyperborea for the second-track checks. The first prototype also used a synthetic probe (55 Hz 0-8 s,
  330 Hz 8-16, 2 kHz 16-24, chord 24-32, silence, clicks 40-48), kept in the scratchpad, not the repo.

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
12. Terrain only: the druse and the medusa cut, the spires and lights removed by the reframe.
13. Colour pass (`04-report-color-lighting-pass.md`): an unlit ground with painted light; plasma palette B.
14. Motion overhaul and stability (`06-report-resonant-motion.md`): ADR-1403 (moving figures, ripples) and ADR-1404
    (fixed places, per-band attack/release, fading ripples).
15. Shape language (`09-report-resonant-shape-language.md`): ADR-1405 (designed bank, focus, nodal relief), ADR-1406
    (unlit-scene prepass/shadow switches, silent rows), ADR-1407 (bake grids); a correction round; camera B; the full
    Feline Footwear render in `shape-language/final/`.
16. The project's audio track removed (live scene); ADR-1408. Merged to main by the coordinator at `74edc063`.
17. Colour, lighting and optics pass (`10-brief`, `11-report`), Stages A-H:
    - colour field, darkness and crest hierarchy;
    - the travelling light (ADR-1409);
    - optics rejected;
    - per-layer audio;
    - performance: 43 ops, prepass off, loud High 31.8 ms;
    - layer debug views `CW_DEBUG=albedo|light|emission`.
18. The ten palettes (`12-brief`): ADR-1043 states with role bindings, ADR-1420's phrase-driven selector (24 s hold,
    6 s OKLab blend, temperature-arc order), CC 9 select / CC 10 lock, `palette_sheet.py`. Build switches:
    `CW_PALETTE` (M = the ten palettes, the default), `CW_PAL`, `CW_PAL_LOCK`, `CW_LINE2`, `CW_PREPASS`.
19. ADR-1421, the no-tempo fallback: the palette also advances on a sustained change in Sonic energy, brightness and
    density, and at the latest every 75 s. The scene is listed under "Resonator" with Silk and Waveform.

20. Quality pass (`docs/prototypes/resonator-quality/`, 02-report §11):
    - the chromatic field (ADR-1470);
    - brighter, less tinted light;
    - the dead colour routes fixed;
    - the offset-lattice ground, which removes the serrated lips.

21. The Resonant Visual Worlds overhaul (`docs/prototypes/resonator-quality/03-06`):
    - four mode families with an LFO director: plate (ADR-1471), hexagonal, radial and arcs;
    - a second resonator for the medium and fine scales;
    - thin ridges over shallow lobes, with the ripples out of the relief;
    - an oblique camera over a widened basin, with a palette sky and fog;
    - multi-hue palettes;
    - the depth prepass back on.

## Open items

- A faint roughness remains on the steepest lips in extreme close-up. The outer cells (60-85 m) are coarser than the
  middle.
- The material is at 41 of 48 ops and the scene at 16 of 16 GPU fields.
- `examples/cymatic-world/meshes/cw-ground.glb` is committed like Digital Ocean's meshes.
