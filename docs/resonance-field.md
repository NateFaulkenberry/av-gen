# The resonance field

ADR-1400. Code: `src/spatial/resonance.{hpp,cpp}` (CPU reference), `shaders/fields.wgsl` (`resonance*`, the GPU
mirror), `src/rendering/field_uniforms.*` (the mode table), `src/scene/field_params.cpp` (parameters). First user:
Cymatic World (`tools/cymatic_world/build.py`, `docs/prototypes/cymatic-world/`).

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
   v
mode table (48 modes x 3 vec4 in the FieldBlock):  shape (k, m, theta, kind), place (centre, reach),
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

## Costs

- CPU: per resonator per frame, `modes x rows in the window` multiply-adds (the window is 5 x the longest ring-down,
  at most 12 s = 1,125 rows): tens of microseconds; plus the filtering of new rows (64 bins x 33 taps each).
- GPU: per sample, `modes` mode shapes (a cos, a sqrt; point modes with m > 0 add an atan2 and a pow; lattice modes
  `order` cosines). The vector channels take six samples (central differences). A Field deformer pays this per
  vertex per pass, three times when the procedural vertex stage takes a finite-difference normal.

## Extending it

- A new mode family: a `ResonanceSourceKind`, its branch in `resonanceModeShape` (CPU) and in
  `fields.wgsl::resonanceModeShape` (the same operations), JSON names, and a case in the parity test
  (`tests/rendering/test_resonance_gpu.cpp`).
- A new channel: `ResonanceChannel`, `scalarChannel`/`resonanceVector`, the WGSL dispatch, the parity test.
- More modes: `kMaxResonanceModes` (per resonator) and the table (`kMaxGpuResonanceModes`, the literal 144 in
  `FieldBlock` and `fields.wgsl`; the layout guard checks them).

## Limitations

- The table holds 48 modes in all; a resonator that does not fit samples 0 on the GPU.
- The memory and the ring-down are bounded by the history ring (~12 s audible).
- Modes are analytic standing waves, not a solved PDE on the actual geometry: the ground does not reflect waves off
  its own walls. That is the trade for being exact under seek and cheap enough to evaluate per vertex.

## Coupling (ADR-1402)

A reader with `"probe": [x, y, z]` publishes `field.<name>.probe` (its value there) to the signal bus each frame; a
source's `drive` adds excitation straight into the modes it owns. A route from one to the other couples two parts of
the field. Keep the probe's listening band disjoint from the driven source's band, or the loop closes (Cymatic World:
the probe hears u < 0.22, the druse's source owns u 0.25-0.6). Competition and the limiter act per source, so a
driven body keeps its own figure beside a louder one.
