# Resonant Silk: the resonance audit

Brief §2. Audited against main `ef3774da`. The unmerged branch `proto/cymatic-split` (ADR-1403..1408) was read
too, but nothing here depends on it.

The short answer: AV Gen already has a complete resonance system, ADR-1400, and Resonant Silk consumes it
unchanged. The engine also has two audio field kinds that do what resonance alone does not do on main: travelling
energy and launched wavefronts. These are ADR-1116's `spectrum` and `onset` fields. No new engine code, and no
second analysis pipeline or resonance manager.

## 1. The path, audio to pixel

```
audio (file, or live capture)
  -> analysis (analysis/): 64-bin log spectrogram 32 Hz-16 kHz at 93.75 rows/s; low/mid/high/beat onsets
  -> AudioHistory (spatial/audio_history.hpp, ADR-1116), heard through the Response controls (ADR-1361)
       |-> resonance resolver (spatial/resonance.cpp, once a frame): per-mode envelopes from the history
       |     -> FieldBlock mode table (48 modes x 3 vec4) -> fields.wgsl resonance*
       |-> spectrum / onset fields: the GPU ring of the newest 1536 rows + 8 onsets per source
       -> any field consumer: Field deformer (vertex), material `field` op (fragment), effectors, particle forces
  -> signal bus: response.* (kick, snare, hat, bass, level, sustain, intensity, ...), control.* (MIDI/OSC)
       -> routes (project `routes`, with attack/decay smoothing) -> any parameter path
          (field/<name>/..., procedural/<node>/deform/<i>/..., material/<prog>/op/<n>/..., lights/..., post/...)
```

## 2. What exists, question by question

| Brief §2 question | On main? | Where / how |
|---|---|---|
| Persistent modes with stable phase | Yes | Resonance field: D = sum a_i(t) cos(Omega_i t + psi_i) phi_i(x). Omega and psi are fixed per mode. The phase is the clock's, so it is continuous by construction. |
| Several modes, different frequencies, amplitudes and spatial patterns | Yes | Up to 24 modes per resonator, 48 in the table. They are placed on a dispersion axis: low pitch draws a long wavelength, high pitch a short one. Each source is a `point` (Bessel rings and petals) or a `lattice` (plane-wave crystals). Bands, `reach`, `gain`, `scale` and `depth` are set per source. |
| Attack, decay, damping, sustain, energy accumulation | Yes | Each mode's envelope is the spectrogram convolved with a rise/decay kernel: `persistence` is the ring-down (low modes ring longer), `buildup` the rise. A slow `memory` envelope accumulates. `sustain` is the spectral drive, `limit` a soft energy limiter, `competition` lets the loudest mode lead. Per-band attack/release is ADR-1404 (unmerged). |
| Excitation by bands, RMS, transients, onsets | Yes | Modes hear their own pitch (selectivity Q, spectral contrast). Onsets strike the modes in their band (`transient`). Bands and levels are also on the bus as `response.*` for routes. |
| Mode mixing and spatial field evaluation | Yes | CPU reference and a WGSL mirror (parity tested). Channels: displacement, amplitude, sand, force, gradient, relief, shading. Readers share a resonator; a tuned reader hears a band. |
| MIDI control and automation | Yes | Project `control.midi` bindings put CCs and notes on the bus as `control.*`; routes move any parameter, the resonance's included (`field/<name>/resonance/...`). Timeline tracks automate. |
| Deterministic playback | Yes | Envelopes are a pure function of the history at the clock. A seek lands where a play would, and offline equals live fed the same audio. Spectrum and onset fields are pure functions of the history too. |
| Smoothing and behaviour in silence | Yes | Route chains smooth (attackMs, decayMs, springs). `idle` keeps the lowest modes breathing in silence, and `breath` sets their visible rate. Silence is not a freeze. |
| Travelling waves along a body | **Not in resonance on main** (ADR-1403 adds moving modes, unmerged) | **The spectrum field covers it.** With `audioSpeed`, the history is laid out in space: the bass of a second ago is further along the sheet. Multiplied by a `wave` field (a compound), it is a carrier travelling under the music's own envelope. `audioSmooth` (ADR-1370) low-passes the row jitter. |
| A transient launching a wavefront that decays | **Not in resonance on main** (ADR-1403 onset ripples, unmerged) | **The onset field is exactly this:** strength x exp(-decay x age) x exp(-((d - age x speed) / width)^2), about a point, per onset source (low/mid/high). Exact under seek. |

## 3. Reused, and what is scene-specific

Reused as is, through scene data only:
- the resonance field (one resonator and its readers);
- the spectrum, onset, wave and compound fields;
- the Field deformer (GPU, per vertex, with the finite-difference normal on the whole chain);
- the Sine and Noise deformers for the slow, music-independent drift of the silhouette;
- material programs (`field` op, Fresnel, curvature, uv);
- thin film (ADR-1143), double-sided lit materials, bloom;
- `response.*` routes;
- `control.midi` bindings.

Scene-specific (in `tools/resonant_silk/build.py`):
- the sheet's sculpture: centreline, transported frame, roll, span, billow and pleats, in a static GLB with UVs;
- where the sources sit relative to the sculpture;
- which bands each consumer hears;
- the palette and the eight performer controls.

## 4. Gaps found

These are recorded for the coordinator. None blocks this scene, and none was patched.
- The anisotropy tangent (ADR-1143) on procedural geometry is the object's +Y, not the mesh's UV direction. A
  silk highlight that runs along the fibres would need the tangent from the UVs (`pbr_shade.wgsl`). The scene works
  around it.
- The resonance channels are standing waves on main. Travelling resonance (ADR-1403) would let the persistent modes
  themselves travel. Here, the spectrum field supplies the travel.

## 5. For Resonant Lattice (relay)

- Read `docs/resonance-field.md` first.
- Feed the Field deformer and material `field` ops from one resonator through readers, not several resonators: one
  resolve, one table.
- Cost: every reader sample is a loop over all of a resonator's modes. A Field deformer pays for that per vertex,
  times three for the finite-difference normal, and again in any depth prepass. Keep the mode count modest and
  prefer one combined reader (the `relief` channel combines three terms in one pass).
- For travel and wavefronts on main, use spectrum (`audioSpeed`, `audioSmooth`) and onset fields rather than
  waiting for ADR-1403.
- Routes into `field/<name>/resonance/*` (modes, sustain, transient, persistence, scale, idle) are the
  performer's controls. `control.*` from MIDI drives them the same way.
