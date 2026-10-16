# ADR-1400: A resonance field is a field kind

- Status: Accepted (2026-10-16), proto/cymatic-world (Cymatic World: one audio-excited field drives the ground, the
  sculpture and the floating matter)
- Builds on: ADR-025 (fields), ADR-1116 (the audio history), ADR-1361 (the Response controls).
- Code: `src/spatial/resonance.{hpp,cpp}` (`ResonanceSpec`, `evaluateResonance`, `resolveResonance`, the sampling
  functions, `ResonanceCache`), `FieldKind::Resonance` and `FieldSpec::resonance` / `resonanceState`
  (`src/spatial/field.{hpp,cpp}`), the mode table in `rendering::FieldBlock` (`src/rendering/field_uniforms.*`),
  `resonance*` in `shaders/fields.wgsl`, `field/<name>/resonance/*` (`src/scene/field_params.cpp`), the per-frame
  resolve in `Composition` (beside `resolveFieldTriggers`).
- Tests: `tests/unit/test_resonance_field.cpp` (`[resonance]`: mode shapes, dispersion, frequency selectivity,
  build-up and ring-down, strikes, purity under play/seek/reset and the cache, phase continuity and the limiter,
  idle, channels, nodal lines, readers and listening windows, JSON/validation/hash, parameters, memory, competition),
  `tests/rendering/test_resonance_gpu.cpp` (`[gpu][resonance]`: CPU/GPU parity in every channel and reader through the
  production FieldUniforms; the 48-mode table limit).

## Context

The brief asks for one resonant system that terrain, sculpture, matter and light each interpret, not a set of
FFT-driven effects. AV Gen already routes one spatial quantity to every consumer: a field (ADR-025) is packed once a
frame into the `FieldBlock` that the procedural vertex stage (Field deformer), the effector pass (instance records),
material programs (`field` op), particle forces, fibre streamlines and SDF marching all bind. The Cymatic Plate draws
its Chladni figure inside one material program, so nothing else can read it.

## Decision

1. **A new field kind, `resonance`.** A field of that kind with no `reference` is a *resonator*: N modes (`modes`,
   fractional fades the last in) on a dispersion axis u: `f_i = fLow (fHigh/fLow)^u`, `lambda_i = lambdaMax
   (lambdaMin/lambdaMax)^u`. Each mode belongs to a source (`point`: an asymptotic-Bessel standing wave about a centre,
   angular order up to the source's `order`, regular on the vertical axis; `lattice`: a Faraday pattern of `order`
   directions, all of a source's modes sharing its orientation). A field whose `reference` names a resonator reads
   its modes: one resonator, many readings.
2. **The envelope is a pure function of the audio history at the clock.** Each mode hears the spectrogram through
   its resonance curve (`selectivity` Q) with a spectral `contrast`, convolved with a unit-area rise/decay kernel
   (`buildup`, `persistence`: low modes ring longest), plus struck-plate impulses from the history's low/mid/high
   onsets (`transient`); a slow second envelope (`memory`) for the sand; `idle` keeps the lowest modes breathing in
   silence; mode `competition` lets the strongest lead; a soft `limit`. The visible oscillation `cos(Omega_i t + psi_i)`
   (`breath`) advances only with the clock. No state is carried between frames: seek = play, reset is free, live and
   offline agree. Response Attack/Release scale the kernels (ADR-1361; the history is already heard through the rest).
3. **Readers choose a channel:** `displacement` (D), `amplitude` (|F|, F the coherent figure sum e_i phi_i), `sand`
   (where Chladni sand gathers: F = 0, from the slow envelope), `force` (-grad F^2, vector), `gradient` (grad D,
   vector); optionally through a listening window over u, or one laid along the field's axis (a tuned structure).
4. **GPU data:** `FieldBlock` grows a `resonance` table of 144 vec4 (48 modes, three vec4 each); a record's grid lanes
   carry its first row, count, channel and window. No new binding. `FieldUniforms::pack` lays every resonator out
   once; readers point at their resonator's rows.
5. **The per-row filtering is cached** (`ResonanceCache`, owned by the Composition): a frame filters only the rows it
   has not seen; dropping the cache changes nothing but the cost.

## Consequences

- Every existing field consumer can read the resonance with no change of its own.
- CPU cost per resonator per frame: the window's rows x modes, a few tens of microseconds; GPU cost is per sample,
  O(modes) cosines (point modes add an atan2 and a pow; the vector channels take six samples).
- The table holds 48 modes in all: a resonator that does not fit samples 0 on the GPU (the CPU has no such limit).
- The history ring bounds the memory: a kernel longer than ~12 s is cut at the ring's audible depth.
- `FieldBlock` is 2304 bytes larger (8496 bytes).
