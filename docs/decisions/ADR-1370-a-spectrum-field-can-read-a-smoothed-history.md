# ADR-1370: A spectrum field can read a smoothed history

- Status: Accepted (2026-10-15), art/digital-ocean (the Digital Ocean scene's "tidewriting" swell)
- Amends: ADR-1116 (audio history reaches the GPU as two field kinds).
- Code: `spatial::FieldSpec::audioSmooth` and `audioShape` (`src/spatial/field.{hpp,cpp}`),
  `spatial::kAudioSmoothTaps` (`src/spatial/audio_history.hpp`), `packField` (`FieldGpu::gridRes.y`),
  `field/<name>/audioSmooth` (`src/scene/field_params.cpp`), `audioShape` / `audioSpectrumTap`
  (`shaders/fields.wgsl`).
- Tests: `tests/unit/test_audio_history.cpp` ("A spectrum field's audioSmooth reads the mean of its band over a window
  round its delay"), `tests/rendering/test_audio_fields_gpu.cpp` (the CPU/GPU parity harness now carries a smoothed
  field).

## Context

Digital Ocean writes the bass's history into the sea: a `spectrum` field (the low band) heard with a delay that grows
with distance (`audioSpeed` 26 m/s), times a planar sine, applied by a field deformer. The history ring holds about 94
rows a second, so at 26 m/s one row covers 0.28 m of sea. A row is one analysis hop: a kick-heavy bass jumps from hop to
hop, and that jitter was laid out in space as 0.28 m noise multiplied by a 1.5 m swell. The sea turned to crumpled foil
(measured: the tide off, the sea clean; the tide on, foil; render A/B in `docs/prototypes/digital-ocean/PROGRESS.md`).
Nothing in a field could low-pass the history: `audioDelay` shifts it, `audioSpeed` spreads it, neither averages it.

## Decision

1. **`audioSmooth`** (seconds, [0, 16], default 0) on a Spectrum field: the field reads the MEAN of its band over a
   window that wide centred on its delay -- n taps at `delay + audioSmooth * ((k + 0.5) / n - 0.5)` -- for every band
   mode (range, element, angle). **n is one tap per history row** (`audioSmoothTaps`: ceil(window x row rate), at least
   2, at most `kAudioSmoothMaxTaps` = 32). A first version used a fixed eight taps: over a 0.9 s window they were ten
   rows apart, which ALIASED the jitter instead of removing it (the sea's concentric mesh rings showed it as moire).
   Taps a row apart are a true box filter: the envelope survives, the hop jitter does not. Windows longer than 32 rows
   (~0.34 s) thin out again; the cap bounds the cost.
2. **0 is unchanged, byte for byte**: one tap, the same code path; not written to JSON, not hashed, so every existing
   audio field keeps its file and its hash. Onset fields ignore it (their fronts are already analytic).
3. **CPU and GPU in step**: the CPU reference (`audioShape`) and `fields.wgsl` take the same taps (the GPU reads the
   row rate from the audio record's `timing.x`); the value rides
   in `FieldGpu::gridRes.y`, a lane every audio field left at 0, so `FieldGpu` keeps its size and layout.
4. A parameter, `field/<name>/audioSmooth`, like the other audio numbers (registered for audio kinds).

## Consequences

- Cost: up to 32 band reads instead of one per sample, only where `audioSmooth > 0`; narrow the band to keep it cheap. Digital Ocean's tide (an 8-bin band,
  three sea meshes, finite-difference normals) is inside its measured frame (PROGRESS.md, Stage 11).
- A window wider than the delay near the source reads the newest rows for its negative taps (the delay is clamped at 0
  as before), so close to the source the mean leans toward the present; irrelevant for the use here.
- Found while building this and recorded, not changed: for `waveGeometry: planar`, `waveDistance` is `dot(q, axis)`
  in the field's frame and ignores `point`, although ADR-1116 describes the delay as measured "about `point`". A planar
  audio field is therefore placed with the field node's `position`. Worth a line in ADR-1116 or a fix that honours
  `point` (it would move every existing planar field whose `point` is not at its origin).
