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
2. Field design, on a CPU mirror of the mode shapes (scratchpad, look-dev only). This let field designs be compared
   without waiting on the GPU lock.
   - **Point sources make onions.** The radial part of a point mode is concentric shells, which read as a target
     or tunnel.
   - **Several unrelated families make noise.** Different centres, orders and wavelengths gave blobs.
   - **One family of plane waves heard by three rotated readers works.** The readers share wavelength and phase, so
     the result is coherent: a curved, aperiodic relative of the triply periodic minimal surfaces, with tunnels,
     saddles and openings. The rotations put the three wave directions 66-69 degrees apart, for seed 11's lattice
     orientation.
   - **Families with different wavelengths do not mix this way.** The longest family dominates and the result
     reads as stacked sheets.
3. Engine findings:
   - Field `strength` is clamped to +-1000 and `length` to 1000, so the plane trick's helper is `-y - 500`.
   - A compiled SDF tree with displaceField took 25 s to compile on first sight.
   - The SDF march ends after one step (hit 0%) when the distance is wrong by a constant. `--frames` prints
     `sdf march` step statistics, which diagnose this.
4. **Stage A, the three prototypes.** All three use the same palette, the same material program, the same lights,
   the same camera and the same moment (Feline Footwear, 44 s). Media is in `stage-a/`.
   - **A, nested isosurfaces (SDF).** The level sets of carrier + resonance are sphere-traced, so topology changes
     are real.
     - First version: the coherent rotated-triple field's nodal membrane, a TPMS relative. It read as sponge or coral:
       every cell the same size, no hierarchy (`earlier-A-tpms-sponge-variants.png`).
     - Final version: the carrier's level sets. Busy, blobby lobes in the frame.
     - Cost at 1080p on the M2 Max: 550 ms with direct evaluation; 209-165 ms with a volume bake (ADR-1440, uncommitted
       patch); 620 ms for the final version (three levels, each sampling the field). Not viable live.
   - **B, designed contour sheets.** Three parametric sheets (a saddle, an arch, a veil) displaced by the resonance.
     Cheap and controllable, but it reads as generic animated drapery. The silhouette has no identity of its own.
   - **C, hybrid.**
     - The large form is a static CARRIER: the standing interference of three long spherical waves from far outside the
       frame. Its level sets are extracted offline by surface nets into five nested sheets at uneven levels. A second,
       shorter interference, the APERTURE, opens them; the material cuts each sheet at the aperture with sub-cell
       precision and traces the cut.
     - The resonance displaces the sheets along their normals on the GPU. Topology is fixed.
     - It has the strongest silhouette and spatial structure: vast curved sheets passing through one another, nested,
       with openings that show the next sheet behind. It reads in neutral grey too.
     - Cost: 121 ms GPU at 1080p, almost all in the scene pass. That is per-pixel field evaluation (the `en` reader)
       times five layered, masked sheets. It is the Stage E bottleneck to fix; bake or simplify.
   - **Choice: C.** A volume-baked variant of A is shelved: three times the GPU of C for blobbier forms.
5. **Cost, before Stage C** (coordinator: find the representation that fits first). All figures 1080p, M2 Max,
   Feline 44 s.
   - Breakdown of 121 ms:
     - The masked sheets' discard defeated hidden-surface removal: the bare meshes cost 56 ms.
     - The field deformers: 16.6 ms per geometry pass.
     - The per-pixel excitation reader: 9 ms.
     - Lit PBR on one full-screen sheet: about 12 ms.
     - The program: about 0.7 ms per op with overdraw.
   - Fixes:
     - Sheets cut as geometry (`levelsets.clip_mesh`), so they are opaque.
     - The displacement and the excitation baked into volume grids (ADR-1440, committed e3df526e): one deformer and
       one fetch.
     - The program cut to 14 ops.
     - The depth prepass asked for: 2.4 ms buys about 20 ms of shading.
     - Surface-nets cell 1.4 m: 130k vertices.
   - **121 -> 28.4 ms GPU** (scene 22.8, bakes 2.9, prepass 2.4). At High's 0.85 scale the pixel-bound part shrinks
     about 28%.
6. **Stage B, sculptural design** (neutral gate):
   - The aperture is three waves from off-centre points (about 45% open): many partial openings, never one centred
     portal.
   - Relief calmed to 0.9-1.1 m on the low-mid folds and 3 m on the bass lobes. At 2.6 m the sheets read as crumpled
     foil.
   - The camera is an elevated three-quarter arc (radius 125 m, 85 m up, a 70 degree sweep over 160 s, 24 mm),
     looking down into the nested sheets. They fill the frame and recede into dark openings.
   - Normals are taken across one bake cell, which removed the cell striations.
   - Media: `stage-b/`.
   - Weaknesses: mid-arc frames are busy (many sheets of similar size), and the hierarchy of large over small could
     be stronger.
7. **Stage B, second pass** (coordinator: the grey form read as a pile of crumpled sheets).
   - **The carrier now has one leading wave** (amplitudes 1, 0.6, 0.4). Its shells are the large sheets: nested and
     roughly offset from one another, with uneven spacing where the weaker waves bend them into saddles.
   - **Four levels.**
   - **Two longer aperture waves** at threshold -0.65 (about 15% open): a few large, smooth, off-centre openings.
   - **Resonance relief calmed** to 0.4-0.55 m on the folds and 2.5 m on the bass lobes. It moves the sculpture; it no
     longer is the sculpture.
   - **The cut edges are drawn** as a pale line in the neutral program (and as the crimson line in colour). Without
     them, neighbouring zero-thickness sheets merged into one grey.
   - **Lighting and fog:** a stronger grazing key (3.2) and fog 0.0045.
   - **Tried and set aside:**
     - thick slabs with lit rims (the deformer moves each face along its own normal, and it doubled the vertices);
     - a steeper camera arc.
   - Media: `stage-b/stageB-pass2-*.png`.
8. **Stage C, resonant animation.**
   - The instrument (`instrument()`):
     - bass multiplies the bass lobes' relief (1 + 1.3 bass);
     - held sound lengthens the memory and grows all three low-mid frames (standing patterns accumulate);
     - the sound's brightness moves the low-mid focus (modal exchange between wavelengths);
     - spectral change widens the mids' drift (travelling bands);
     - the song's build lifts the whole relief;
     - a kick launches a spherical shell (onset field, 32 m/s) that lifts the sheets it passes, 4 m.
   - Phrase-scale evolution comes from the focus sweeps (37-71 s periods), the slow drift and the families' spin.
     Silence keeps every family faintly ringing (`drive`).
   - MIDI CC 1-9 cover the brief's nine controls (§10).
   - **Synthetic probe** (`mkprobe.py` in the scratchpad): silence 0-8 s; a 110/165/220 Hz chord 8-22; a 900 Hz
     tone 22-32; kicks every 0.6 s 32-44; dense (kicks, hats, a chord) 44-58; silence 58-70.
   - Neutral, fixed camera, mean absolute luminance change (0-255) per 0.5 s, and versus the 7 s silent frame:

     | Section | Per 0.5 s | Versus silence |
     |---|---|---|
     | silence | 1.2 | 3.8 |
     | low chord | 2.1 | 13.0 |
     | mid tone | 1.8 | 12.9 |
     | kicks | 2.7 | 10.7 |
     | dense | 3.4 | 17.6 |
     | silence after | 2.0 | 11.9 (decaying back) |

   - What each condition looks like: the sustained chord accumulates crossing standing folds; the tone gives finer
     bands; kicks give broad lobes and travelling shells (`stageC-kick-front-difference.png`); dense gives the
     strongest folding. The silhouette holds throughout.
   - Weakness: the geometric front is subtle on its own. Stage D lights it.
