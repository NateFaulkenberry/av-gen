# ADR-1403: A resonance figure moves, and onsets launch ripples

- Status: Accepted (2026-10-16), proto/cymatic-split (Cymatic World's motion overhaul, `docs/prototypes/cymatic-world/05-brief-resonant-simulation-overhaul.md`)
- Code:
  - `ResonanceSource::{drift, driftPeriod, spin, driftAudio, amplitude}` and `ResonanceSpec::{ripple, rippleSpeed, rippleLife, rippleWidth, rippleArea, rippleCount}` (`src/spatial/resonance.*`).
  - The ripple branch of `resonanceModeShape` (CPU) and `fields.wgsl::resonanceModeShape` (GPU).
  - The parameters in `src/scene/field_params.cpp`.
- Tests:
  - `tests/unit/test_resonance_motion.cpp` (kinematics, seek, ripples, amplitude, JSON and parameters).
  - `tests/rendering/test_resonance_gpu.cpp` ("Moving modes and onset ripples match the CPU reference on the GPU").
  - The hidden diagnostic `[.resonance-motion]` (`tests/unit/test_resonance_probe.cpp`).

## Context

ADR-1400's modes are standing waves whose patterns φᵢ are fixed in space. Each sits on a still source with a fixed petal orientation, and audio moves only their amplitudes. A figure made of them can deepen, fill and breathe, but it cannot reorganise.

Cymatic World's owner found the ground "almost frozen". On Hyperborea, at 3 m scale, its relief moved about 0.13 m a second against an rms of 0.24 m. The brief asks for ridges that migrate, split and reconnect, loops that break and reform, nodal boundaries that move, and transients that physically disturb the surface. It also asks for the simplest approach that does this, deterministic and frame-rate independent, with seek matching play.

## Decision

The patterns themselves move. Everything stays a pure function of the clock and the audio history (ADR-1400's rule), so a seek still lands exactly where a play does and nothing is stepped.

1. **Kinematics, per source.**
   - Each mode's centre wanders on its own Lissajous: radius `drift`, period about `driftPeriod`, with finer modes up to 1.6 times faster, and its own phases and a per-mode rate in [0.8, 1.2].
   - `driftAudio` widens a mode's wander by its memory envelope, (1 + driftAudio·s), so a mode that rings harder swings wider.
   - A point mode's petals turn at `spin` times a per-mode factor in ±[0.4, 1.3], so some turn against the others.
   - A lattice turns as one at `spin` (its wavelengths keep nesting) while each of its modes slides its own way.
   - The CPU writes these into the mode table it already rewrites every frame (centre, orientation). They cost nothing on the GPU.
2. **Ripples.**
   - Each of the newest heard onsets still alive (since ADR-1404, `rippleCount` a band, at most 3, with displaced rings fading out) is a row after the modes: a travelling ring packet, cos(k(ρ − R))·e^(−((ρ − R)/w)²)/√(1 + ρ/λ), about a launch point hashed from the onset's time within `rippleArea`.
   - Its radius is R = speed × age.
   - The low band (kick) draws a broad, slow, long ring. The mid band (snare) is faster (×1.6), shorter-lived (×0.55), sharper and local (reach 0.6 × area). The high band (hat) is fine and faint.
   - A ripple adds to the vibration and the fast figure, and 0.35 of itself to the slow figure, so a passing ring disturbs the sand's seams too.
   - Geometry 2 in the mode table; the depth now packs as geometry + 4 × depth.
3. **Amplitude after the limiter.** `amplitude` scales a source's modes after competition and the soft limiter. A source's `gain` acts before them, so at saturation the limiter cancelled it and fine families came out as tall as broad ones. `amplitude` gives the field a spectral tilt: broad modes large, fine modes small.
4. **Performable.** `source/<i>/drift`, `spin` and `amplitude`; `resonance/ripple`, `rippleSpeed` and `rippleLife`; and a relief reader's `reliefLine` and `reliefVibration` are parameters, so routes and MIDI can move them.

**Considered and not taken (yet):** a stepped GPU height field (the brief's Options B and C). It would need a stateful texture field, a compute pass, a resampling path for deformers and materials, and its own answer to seek. The analytic kinematics produced the required reorganisation first (`06-report-resonant-motion.md` has the measurements). It stays the next step if propagation through the medium (reflection, true interference of travelling waves) turns out to be what is missing.

## Consequences

- Seek-exact and frame-rate independent: tested by playing through a cache against a fresh and a sought evaluation, with modes, ripples and places all equal.
- A source with no drift, spin or amplitude, and a field with no ripples, is exactly ADR-1400. Those members are written to JSON only when set.
- The GPU table is still 48 rows. A resonator's rows are its modes plus its ripples (up to 12 since ADR-1404).
- Ripples are only as dense as the onset analysis (ADR-1116). Music with no onsets launches none.
- The kinematics are deterministic in the clock, not in the music's cumulative history (there is none to integrate under seek). The music moves the figure through the amplitudes, `driftAudio` and routes on the new parameters, not through its own integrated phase.
