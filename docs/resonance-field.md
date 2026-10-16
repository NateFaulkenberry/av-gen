# The resonance field

ADR-1400. Code: `src/spatial/resonance.{hpp,cpp}` (CPU reference), `shaders/fields.wgsl` (`resonance*`, the GPU
mirror), `src/rendering/field_uniforms.*` (the mode table), `src/scene/field_params.cpp` (parameters). Extended by
ADR-1403 (moving figures, ripples), ADR-1404 (fixed layout, per-band response), ADR-1405 (designed bank, focus, nodal
relief), ADR-1406 (silent rows skipped) and ADR-1407 (bake grids). First user: Cymatic World
(`tools/cymatic_world/build.py`, `docs/prototypes/cymatic-world/`).

A resonance field is a field (ADR-025) of kind `resonance`. Anything that can read a field can read it: the Field
deformer (geometry), effectors (instances), material programs (`field` op), particle field forces, fibre streamlines,
SDF marching. It is how several unlike things answer one piece of music *as one system*.

## Data flow

```
audio history (ADR-1116: 64-bin log spectrogram + low/mid/high onsets, heard through the Response controls)
   |  per row, cached: each mode's resonance curve (selectivity Q) + spectral contrast
   v
mode envelopes a_i(t)  = rows convolved with a rise/decay kernel  + onset strikes   (fast: the vibration)
mode memory    s_i(t)  = the same with a slower kernel                              (slow: the sand)
   |  idle (silence keeps the lowest modes breathing), competition, soft limit
   |  per source: designed orders, focus window, kinematics (drift, spin), amplitude after the limiter
   |  + ripple rows: the newest onsets per band, travelling ring packets
   v
mode table (48 rows x 3 vec4 in the FieldBlock):  shape (k, m, theta, kind), place (centre, reach),
                                                   drive (a cos(Omega t + psi), a, u, s)
   |  per sample, CPU or GPU
   v
channels: displacement D | amplitude |F| | sand | force -grad F^2 | gradient grad D
```

Everything is computed at the frame's clock from the history alone: nothing is stepped, so a seek lands exactly where
playing would, a reset costs nothing, and an offline render equals a live one fed the same audio. The only thing
kept between frames is a cache of filtered rows (`ResonanceCache`, one per Composition).

## Authoring

A resonator:

```json
{ "name": "cymatic", "kind": "field", "position": [0, 0, 0],
  "field": { "kind": "resonance", "seed": 7, "falloff": {"kind": "none"},
    "resonance": { "modes": 11, "fLow": 40, "fHigh": 6000, "lambdaMax": 34, "lambdaMin": 5,
                   "selectivity": 3, "contrast": 0.45, "persistence": 2.8, "buildup": 0.3, "memory": 1.6,
                   "competition": 3, "sustain": 1, "transient": 0.9, "idle": 0.5, "breath": 0.16, "limit": 0.9,
                   "sources": [ {"kind": "point", "position": [0, 9, 0], "order": 5, "bandLow": 0, "bandHigh": 0.7},
                                {"kind": "lattice", "position": [0, 0, 0], "order": 3, "bandLow": 0.5,
                                 "bandHigh": 1, "reach": 95, "gain": 0.75} ],
                   "channel": "displacement" } } }
```

A reader (shares the resonator's modes):

```json
{ "name": "sand", "kind": "field", "field": { "kind": "resonance", "reference": "cymatic",
  "resonance": { "channel": "sand", "sharpness": 0.9 } } }
```

A tuned reader (each height hears its own band of modes): `"listenAlongAxis": true` with the field's `axis`, `point`
(the middle) and `length`; `listenLow`/`listenHigh` are the bands at the two ends, `listenWidth` the Gaussian's width.

## Parameters (what a performer and a route can move)

| path (`field/<name>/resonance/...`) | meaning |
|---|---|
| `modes` | complexity: how many modes (fractional fades the last in) |
| `fLow`, `fHigh` | the frequencies the lowest and highest modes hear |
| `lambdaMax`, `lambdaMin`, `scale` | the wavelengths they draw; `scale` multiplies every wavenumber (spatial scale) |
| `selectivity` | each mode's Q: high = only its own pitch rings it |
| `contrast` | how much of a row's mean level is removed first (keeps a dense mix from ringing everything) |
| `persistence`, `buildup` | ring-down of the lowest mode (higher modes shorter), and the rise as a share of it |
| `memory` | how long the sand takes to settle and to forget |
| `competition` | how strongly the loudest mode suppresses the rest (legible figures) |
| `sustain`, `transient` | excitation by held sound and by onsets |
| `idle` | the figure kept in silence |
| `breath` | the visible oscillation rate (Hz) of the lowest mode |
| `limit` | soft limit of the total amplitude |
| `source/<i>/position`, `gain`, `reach` | move, weigh or bound a source |
| `listenLow`, `listenHigh`, `listenWidth`, `sharpness` | a reader's band and its sand line width |
| `layout` | ADR-1404: fixed mode places on the dispersion axis; `modes` then only says how many of them ring |
| `attack`, `release` | ADR-1404: three times each (low, mid, high), in place of `persistence`/`buildup`; the attack is also an onset strike's rise |
| `ripple`, `rippleSpeed`, `rippleLife`, `rippleCount`, `rippleFade`, `rippleU`, `rippleArea` | ADR-1403/1404/1405: onset ripples, their number per band, the fade of a displaced ring, where each band's ring sits on the dispersion axis |
| `source/<i>/drift`, `driftPeriod`, `driftAudio`, `spin` | ADR-1403: each mode's centre wanders and a point mode's petals turn |
| `source/<i>/amplitude`, `breath` | ADR-1403/1404: a source's weight after the limiter (a spectral tilt), its breath multiplier |
| `source/<i>/orders` | ADR-1405: designed angular orders (point) or directions (lattice), in u order, cycled |
| `source/<i>/focus`, `focusWidth`, `focusSweep`, `focusPeriod`, `focusFloor` | ADR-1405: a Gaussian window over u that lets one or two modes lead |
| `reliefSand`, `reliefLine`, `reliefVibration`, `reliefFigure`, `reliefAmplitude`, `reliefRound` | ADR-1403/1405: a relief reader's terms (see below) |
| `sandNormalized` | ADR-1405: the sand as a 0-1 mask whatever the loudness (also a relief reader's sand terms) |

## Mode shapes

- `point`: `R_m(kr) cos(m (alpha - theta))`, `R_m(x) = sqrt(2) cos(x - (2m+1) pi/4) / sqrt(1 + x / 2 pi)` (the
  asymptotic Bessel form, decaying cylindrically), with a regular core: for `m = 0` the argument is
  `sqrt(x^2 + 1) - 1` (smooth through the centre); for `m > 0` a factor `(k^2 rho^2 / (k^2 rho^2 + m^2))^(m/2)`
  vanishes on the vertical axis where the azimuth is undefined. `r` is 3D, so a raised source draws rings on the
  ground and nodal shells about itself.
- `lattice`: `sqrt(2/n) sum_j cos(k x . d_j)`, `n = order` directions half a turn apart (2: squares, 3: hexagons, 5:
  a quasicrystal), and a source's `depth` (whole metres) gives any source a layer: `e^(-|y - y0| / depth)` (a
  surface wave fading away from its height, so a raised body does not hear the ground's carpet). A source's lattice modes share one orientation, so lattices of different
  wavelengths nest.
- `reach > 0` multiplies a mode by `1 / (1 + (r / reach)^2)`: fine modes stay local and the far ground stays calm.

## Moving figures (ADR-1403, ADR-1404)

Everything here is still a function of the clock and the history, so seek equals play.

- **Kinematics.** Each mode's centre wanders on its own Lissajous (`drift`, `driftPeriod`; `driftAudio` widens it by
  the mode's memory envelope); a point mode's petals turn at `spin` times a per-mode factor, some against the others.
  The CPU writes these into the table it rewrites every frame; they cost nothing on the GPU.
- **Ripples.** The newest onsets per band (`rippleCount`, at most 3) are rows after the modes: travelling ring packets
  about a launch point hashed from the onset's time, radius `rippleSpeed` x age. A ring a newer one displaces fades
  over `rippleFade`. Low rings are broad and slow, mid rings faster and local, high rings fine and faint.
- **Fixed places.** With `layout > 0`, mode i sits at u = i / (layout - 1) whatever `modes` is, so a route on `modes`
  adds or removes modes instead of re-placing all of them (re-placing was a cause of visible twitch).
- **Per-band response.** `attack` / `release` (low, mid, high, log-interpolated) set each mode's envelope; an onset's
  strike rises over the band's attack, never a step.

## A designed bank and nodal relief (ADR-1405)

- `orders` makes a bank designed rather than hashed: rings, a trefoil, four lobes, a dipole, six lobes.
- `focus` (+ `focusWidth`, `focusSweep`, `focusPeriod`, `focusFloor`) is a Gaussian window over u on a source's
  weights: one or two adjacent modes lead and the figure crossfades through the bank as the window moves (routable;
  the sweep keeps a steady passage evolving). The window is exactly 0 beyond three widths.
- A `relief` reader sums terms of the slow figure F = sum s_i phi_i: `reliefSand` (the nodal ridge), `reliefLine`
  (a narrower crest), `reliefVibration` (D), `reliefFigure` (signed F) and `reliefAmplitude` (|F|; negative sinks
  the antinodes into basins). `reliefRound` rounds |F|'s crease (sqrt(F^2 + (c sigma)^2) - c sigma): the exact |F|
  has a cusp on every nodal line that no mesh can carry.
- Several readers with listening windows (`listenLow`/`listenHigh`/`listenWidth`) give each spatial scale its own
  shaping; Cymatic World uses large, medium and fine readers summed by a compound field.

```json
{ "name": "relief-large", "kind": "field", "field": { "kind": "resonance", "reference": "plate",
  "resonance": { "channel": "relief", "listenLow": 0.0, "listenHigh": 0.3, "listenWidth": 0.04,
                 "reliefSand": 1.0, "sandNormalized": true, "reliefAmplitude": -1.2, "reliefRound": 0.4 } } }
```

## Bake grids for costly consumers (ADR-1407)

A reader evaluated per vertex (three times for a finite-difference normal) or per pixel pays the whole mode sum each
time. A bake grid samples it once a frame on a plane instead:

```json
"grids": [ { "name": "relief-bake", "mode": "bake", "wrap": "clamp", "resolution": [896, 1, 896],
             "boundsMin": [-86, -4, -86], "boundsMax": [86, -2, 86], "injectField": "relief" } ]

{ "name": "relief-grid", "kind": "field", "field": { "kind": "grid", "reference": "relief-bake",
  "falloff": {"kind": "none"} } }
```

`cs_bake` fills it at the frame's clock (no stepping, no catch-up, no checkpoints: a seek lands where a play would),
and consumers read it through a `grid` field (the second object). Bilinear sampling sets the
detail limit: a cell must be well under the finest wavelength baked. Cymatic World bakes its relief (896^2) and its
material's sand, amplitude and colour fields (512^2-896^2).

## Costs

- CPU: per resonator per frame, `modes x rows in the window` multiply-adds (the window is 5 x the longest ring-down,
  at most 12 s = 1,125 rows): tens of microseconds; plus the filtering of new rows (64 bins x 33 taps each).
- GPU: per sample, `modes` mode shapes (a cos, a sqrt; point modes with m > 0 add an atan2 and a pow; lattice modes
  `order` cosines). The vector channels take six samples (central differences). A Field deformer pays this per
  vertex per pass, three times when the procedural vertex stage takes a finite-difference normal. Bake it (above).
- Silent rows (every drive below 1e-5, e.g. outside a focus window or a source at amplitude 0) are skipped by every
  sampler, CPU and GPU (ADR-1406).

## Extending it

- A new mode family: a `ResonanceSourceKind`, its branch in `resonanceModeShape` (CPU) and in
  `fields.wgsl::resonanceModeShape` (the same operations), JSON names, and a case in the parity test
  (`tests/rendering/test_resonance_gpu.cpp`).
- A new channel: `ResonanceChannel`, `scalarChannel`/`resonanceVector`, the WGSL dispatch, the parity test.
- More modes: `kMaxResonanceModes` (per resonator) and the table (`kMaxGpuResonanceModes`, the literal 144 in
  `FieldBlock` and `fields.wgsl`; the layout guard checks them).

## Limitations

- The table holds 48 rows in all (modes and ripple rows); a resonator that does not fit samples 0 on the GPU.
- The memory and the ring-down are bounded by the history ring (~12 s audible).
- Modes are analytic standing waves, not a solved PDE on the actual geometry: the ground does not reflect waves off
  its own walls. That is the trade for being exact under seek and cheap enough to evaluate per vertex.

## Coupling (ADR-1402)

A reader with `"probe": [x, y, z]` publishes `field.<name>.probe` (its value there) to the signal bus each frame; a
source's `drive` adds excitation straight into the modes it owns. A route from one to the other couples two parts of
the field. Keep the probe's listening band disjoint from the driven source's band, or the loop closes (for example, a
probe that hears only u < 0.22 driving a source that owns u 0.25-0.6). Competition and the limiter act per source, so
a driven body keeps its own figure beside a louder one. The mechanism is tested but has no scene user at present.
