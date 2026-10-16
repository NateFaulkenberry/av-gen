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

## Pass 2: geometry-only prototype (ADR-1403)

**Engine** ([ADR-1403](../../decisions/ADR-1403-a-resonance-figure-moves-and-onsets-launch-ripples.md)). The figure itself moves, and everything stays a pure function of the clock and the history:
- **Kinematics.** Each mode's centre wanders on its own Lissajous (`drift`, `driftPeriod`, widened by its memory envelope through `driftAudio`). Point modes' petals turn at `spin` × a per-mode ±[0.4, 1.3]. Lattices turn as one and slide mode by mode.
- **Ripples.** The newest heard onsets each launch a travelling ring packet: a kick's broad and slow, a snare's fast, short and local, a hat's fine and faint.
- **`amplitude`.** It scales a family after the limiter. A source's gain is cancelled by the limiter at saturation, which had made the fine carpet as tall as the swell.
- **Tests.** `test_resonance_motion.cpp` (6 cases: wander and turn within bounds, seek = play including ripples, ring speed, decay and life, kick versus snare, the newest-N rule, amplitude after the limiter, JSON, hash, validation and parameters) and a GPU parity case for moving modes and ripples. Both pass.

**The scene.** Four families, one per source, each moving:

| Family | Source | Band (u) | Amplitude | Wander | Spin |
|---|---|---|---|---|---|
| A, swell | lattice, 3 directions | 0-0.2 | 0.62 | 26 m / 95 s | 0.035 rad/s |
| B, rings | point at the centre, order 2 | 0.14-0.5 | 0.7 | 13 m / 62 s | 0.05 |
| C, petals | point at (−23, 0, 9), order 5 | 0.38-0.76 | 0.55 | 16 m / 44 s | 0.09 |
| D, carpet | lattice, local | 0.7-1 | 0.22 | 9 m / 26 s | 0.03 |
| E, ripples | onsets | | | 9 m/s, life 1.6 s, 6 at once | |

Other settings:
- 16 modes, λ 42 → 4.5 m.
- Competition 0.8 (was 3), so several modes interfere. Limit 2. Breath 0.12 Hz.
- The relief is now led by the vibration D (weight 1.2, was 0.23), with the sand's crest as a wider seam (sharpness 0.5).

**Measured** on Hyperborea, 3 m scale, ±35 m:

| Window | Height rms (min-max) | \|ΔH\| over 1 s | Structural change at 1 s | At 10 s |
|---|---|---|---|---|
| 0-30 s (quiet intro) | 1.31 m (0.14-3.12) | 1.05 m | 0.55 | 1.02 |
| 40-80 s (dense, break) | 1.75 m (0.54-4.10) | 1.42 m | 0.54 | 0.95 |
| 100-150 s | 2.11 m (1.23-3.15) | 1.51 m | 0.42 | 0.92 |
| baseline, 40-80 s | 0.24 m | 0.13 m | 0.26 | 0.50 |

The figure is about ten times larger and moves about eleven times faster. At 10 s it is uncorrelated with itself (about 1.0), where the baseline's crater kept half its structure. The quiet intro falls to 0.14 m rms, so the music sets the size.

**Gate clip:** `motion/pass2-geometry-prototype/geometry-prototype-hyperborea-40-70s-geometry-only.mp4`, with `-shaded` and `-side-by-side` beside it. Over a few seconds the hexagonal carpets slide and turn, ridges bend and part, rings form about a wandering centre and break, and broad lobes rise and sink across the frame.

**Open after Pass 2:**
- The broad swell's standing oscillation (0.12 Hz) still inverts large areas every few seconds, which is the anti-correlation at 4 s. Pass 3 balances this against the travelling motion.
- The geometry reads soft: no fine relief, interpolated normals.
- The polar mesh's stitched rows crease under metres of displacement (Pass 5).
- The shading was tuned for 0.3 m of relief, so its fuchsia now floods (Pass 6).
