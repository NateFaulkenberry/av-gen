# Cymatic World: resonant motion overhaul (report)

The brief is [05-brief-resonant-simulation-overhaul.md](05-brief-resonant-simulation-overhaul.md). It builds on the colour pass ([04-report-color-lighting-pass.md](04-report-color-lighting-pass.md), 129a137a). The work is on `proto/cymatic-split`, with one commit per pass ("Cymatic World motion, Pass N: ...").

**Audio.** Hyperborea only: `assets/audio/hyperborea.wav` in the repo (gitignored).

**Media.** `~/Desktop/av-gen-review/58-cymatic-world/motion/`, one folder per pass. Every clip comes in three versions: `-geometry-only` (`CW_PALETTE=G`: one grey material, white light, no emission, no colour routes), `-shaded`, and `-side-by-side` (geometry on the left, shaded on the right, with audio).

**The measure.** The hidden diagnostic `[.resonance-motion]` in `tests/unit/test_resonance_probe.cpp` samples the ground's relief on an 81 × 81 grid over the visible ±35 m, every 0.25 s, from the scene file and the real song, and reports:
- the height's rms in metres;
- the mean height change |ΔH| over a lag, in metres;
- **structural change**, 1 − corr(H(t), H(t − lag)), after a 3 m Gaussian low-pass, so it reads the structure the eye reads. A normalised correlation does not see a figure that only grows and shrinks in place; it rises only when the figure reorganises.

## Pass 1: diagnosis and baseline

### How the ground is made, and why it looks frozen

- **Geometry.** One static polar mesh, `cw-ground.glb`: 130k vertices, rings 0.36 m apart out to 70 m and coarser beyond. Its heights are the WorldMap's basin. **The only thing that moves it** is one Field deformer, the `relief` reader of the resonance field (ADR-1400), at amount 2.6 along +Y. It is evaluated per vertex on the GPU in the procedural vertex stage, with a finite-difference normal (step 0.12 m, ADR-1401).
- **What `relief` is.** relief = sand(F_slow, 0.9) + 0.08·sand(F_slow, 0.6) + 0.23·D.
  - Most of the shape is **sand**: the nodal lines of the *slow* figure F_slow = Σ sᵢφᵢ, which settles over memory (1.6 s) plus the ring-down.
  - The part that oscillates, D = Σ aᵢ cos(Ωᵢt + ψᵢ) φᵢ, is weighted 0.23 at a breath of 0.16 Hz: about ±0.1 m on a six-second cycle.
- **Why it is frozen.** Audio changes only the amplitudes aᵢ and sᵢ. **Every spatial pattern φᵢ is fixed in space.**
  - The point sources sit still (the heart at (0, 12, 0), the second centre at (−23, 0, 9)).
  - Each mode's petal orientation is a constant hash, and the lattice's origin and orientation never move.
  - With competition 3, the loudest mode in each source dominates, so the figure is mostly one fixed pattern whose cells deepen and fill.
  - The soft limiter (0.29 at 48 s, a dense passage) then flattens loud against quiet.
  - The result is the brief's failure mode exactly: a fixed figure that breathes in place.
- **Colour moves more than shape.** The material reads the same field per pixel (`field shade`: sand, crest, amplitude), and the colour pass added kick, snare, hat, flux and build routes on the colours. That is why the scene looks like an animated colour map over a still relief.
- **Measured on Hyperborea**, relief at 3 m scale over the ±35 m grid:

  | Window | Height rms | Mean \|ΔH\| over 1 s | Over 10 s | Structural change at 1 s |
  |---|---|---|---|---|
  | 40-80 s | 0.24 m (0.14-0.33) | 0.13 m | 0.20 m | 0.26 |
  | 0-30 s | 0.18 m | 0.10 m | 0.21 m | 0.25 |

  The structural change comes almost entirely from the small, reversing D term. The visible craters stand still, and nothing larger than about 0.3 m moves.
- **Geometry quality.** The geometry-only baseline shows:
  - sawtooth rims, where the narrow sand crest (sharpness 0.6, a band about one vertex wide) is sampled by a 0.36 m mesh;
  - a soft foreground, where broad lumps and vertex-interpolated normals carry no detail.

### Cymatic Plate (what makes it feel synchronous)

Its figure is a material program, f = cos(nπu) cos(mπv) − cos(mπu) cos(nπv). The routes drive the **mode numbers n and m themselves**, from pitch and the sound's brightness (attack 180 ms). Kicks and bass flex the plate and widen the lines. So the figure *re-forms* continuously with the melody rather than brightening in place. The lesson for the World: **move the patterns, not only their amplitudes.**

### The engine, and the smallest change that can work

- **The mode table.** The CPU rewrites it every frame (48 modes × 3 vec4: shape, place, drive), as a pure function of the clock and the audio history. The GPU evaluates it per vertex (the deformer) and per pixel (the material). So anything that makes φᵢ a function of time (centres, orientations, a lattice's origin, a travelling packet's radius) costs nothing on the GPU beyond the modes already paid for, and stays exact under seek.
- **Option B or C, a stepped GPU height field, would need:**
  - a new stateful field kind backed by a texture;
  - a compute pass to step it;
  - a resampling path for the deformer and the material;
  - its own answer to seek (a re-simulation from a snapshot or from silence).

  That is a large engine change, so I am not taking it first.
- **Option A extended, the plan:**
  1. Per-source **kinematics**: a centre that wanders (a slow Lissajous), modes whose lobes turn (point modes at their own rates, some against each other) and a lattice that travels. The nodal lines then migrate, split and reconnect as the patterns slide across one another.
  2. **Onset-launched travelling wave packets** ("ripples"), computed analytically from the onset list (ADR-1116), so they are seek-exact too. A kick launches a broad, slow ring; a snare a sharp, fast, local one.
  3. **Geometry from the vibration D**, at a scale bass can move by metres, with the sand's nodal ridges kept as the structure's seams; competition and the limiter loosened so several modes interfere.

### Baseline captures and cost

- Clip (geometry-only, shaded, side-by-side): `motion/pass1-baseline/baseline-hyperborea-40-70s-*.mp4`. Hyperborea 40-70 s covers the dense section, the break at 60-75 s and the return.
- Baseline stills: the colour pass's `colour-pass/f-final/` (quiet 10 s, dense 48 s, sustained 116 s, transient 134 s).
- Cost (colour-pass baseline): live 1080p, Auto → High: GPU median 26.0 ms, 0 deadline misses. Headless Ultra: 32.1 ms. The CPU resolve is 35 µs a frame played and 1.8 ms after a seek.
