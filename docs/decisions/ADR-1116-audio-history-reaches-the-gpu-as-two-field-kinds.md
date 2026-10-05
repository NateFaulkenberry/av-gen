# ADR-1116: Audio history reaches the GPU as two field kinds

- Status: Accepted (gpu/productionization)
- Builds on ADR-025 (fields and effectors), ADR-032 (the shared grid table at group 0 binding 15),
  ADR-097 (one reactivity system), ADR-898 (band onsets), ADR-906 (field triggers).
- Evidence: the GPU world spike, Phase 2 and Phase 3 A (Echo Field),
  `docs/research/gpu-world-architecture-spike.md`; `docs/research/gpu-world-productionization.md`
  §Phase 3, step 1.

## Problem

The spike measured one capability that production could not express on any path: a population where
each element answers the music **in its own way** — its own frequency band, at its own moment (a delay
that grows with its distance from a source), and to the last few kicks as fronts that travel outward.
Production's effector pass ran over GPU-resident records, but it had no audio inputs at all. The
records' seed (`InstanceRecord::random`) was stored and never read, no spectrum reached a compute
pass, and onset history reached a field only as the age of its single latest trigger. The spike's
Phase 5 proposed "new inputs to `cs_effectors`". This record decides their shape.

## Decision

1. **Two new field kinds, `spectrum` and `onset`**, appended to `FieldKind` and mirrored in
   `shaders/fields.wgsl`. Audio becomes a spatial control signal like every other field. So it reaches,
   unchanged:
   - effectors (per-record motion, scale, colour and emission);
   - Field deformers (per-vertex);
   - emissive fields;
   - particle field forces;
   - grid injection (ADR-032);
   - and materials and volumes.

   There is no effector-specific audio path and no second reactivity system. A field's `strength` is
   an ordinary parameter, so routes still modulate it.
2. **Delay by distance** reuses the field's wave geometry (`waveGeometry` about `point` along `axis`):
   `delay = audioDelay + d / audioSpeed`. With `audioSpeed` 0 the field has no travel.
3. **Band selection**, `audioBand`:
   - `range`: the mean of `bandLow..bandHigh` on a 64-bin log axis, 32 Hz-16 kHz.
   - `element`: one band per element, picked by the element's own random. That is
     `InstanceRecord::random.w` in the effector pass and the vertex stage, and the particle's seed in
     the particle pass. The value is set as a WGSL `var<private> fieldElement` before sampling; every
     other caller reads the middle band.
   - `angle`: fans of frequency about the axis.
4. **Onset fronts:** an `onset` field sums the newest 8 onsets of one source (low / mid / high band
   onsets from ADR-898, or beats). Each onset contributes `strength · exp(−decay·age) ·
   exp(−((d − age·speed)/width)²)`; width 0 is a flash everywhere at once.
5. **Where the data lives.**
   - The **spectrogram ring** (1,536 rows × 64 bins, 16.4 s, 384 KB) is appended to the existing field
     table at group 0 binding 15, after the grids' unchanged 8 MB region. **No new binding.** Every
     `fields.wgsl` consumer already binds that table, including the PBR fragment stage. A second
     storage binding there would count against `maxStorageBuffersPerShaderStage`, whose WebGPU
     default is 8.
   - The **onset history** (4 sources × 8 ages and strengths) and the ring's metadata ride in the
     `FieldBlock` uniform, which grows from 5,904 to 6,192 B.
   - The audio parameters of a field ride in its `FieldGpu` grid lanes, which are unused by every
     non-grid kind, so `FieldGpu` keeps its size.
6. **What fills it** is a `spatial::AudioHistory`, attached to the scene's `FieldSet` each frame
   (`Composition::setAudioHistory`, next to ADR-906's trigger clock). There are two sources:
   - **A file with an analysed track** (offline render, editor, and live playback of a file): the whole
     track, folded once to 64 log bins. Each bin is stretched over its own 20th to 98th percentile, so
     a quiet intro stays dark (the spike's Phase 3 lesson). The clock is the transport second. **Every
     sample is a pure function of t: play and seek are exact by construction, with nothing to
     checkpoint.**
   - **Live input**: rows appended per render frame from the newest analysis frame, with running
     stand-ins for the percentiles. The clock is the input's own. This is visually equivalent to the
     offline history, never equal: it holds a row between render frames rather than inventing the hops
     the triple buffer dropped.
7. **The ring is filled incrementally** (`FieldUniforms::updateAudio`). Playback writes about 1.5 rows
   a frame (≈400 B). A scrub back inside the window writes only the rows it lacks. A jump, a new track
   or a live restart refills all 384 KB. The only data crossing to the GPU is `WriteBuffer`;
   **nothing is read back**.

## Rejected alternatives

- **New effector inputs and ops in `points.wgsl` only** (the spike's literal proposal). Rejected: the
  same history would then be unreachable from deformers, particles, grids and materials. A
  second, effector-only reactivity path is what ADR-097 forbids.
- **A new storage binding for the spectrogram.** Rejected for the PBR stage's storage-buffer budget
  (above) and because seven bind-group layouts would change for one 384 KB array.
- **The whole track on the GPU** (5.8 MB, as the spike's art prototype did). Rejected: live input has
  no whole track, and a ring makes offline and live identical in shape. The ring caps delay at 16.4 s,
  more than the 12 s the Echo Field used.
- **The raw 1,025-bin spectrum.** Rejected: 64 log bins are what the art used and carry the musical
  structure; the raw bins are 16× the memory and mostly treble.

## Consequences

- Determinism. Offline the field is a pure function of (t, position, element); the ring holds exactly
  the rows the CPU history holds. The CPU reference (`spatial::sampleScalar` with an `element`
  argument) agrees with the GPU within 2e-4 through every ring path: forward, back, jump and a new
  track (`tests/rendering/test_audio_fields_gpu.cpp`). Live input is not seekable and is not promised
  to be.
- The effector pass now gives every record its own band at its own delay (the same test, through the
  real `ProceduralRenderer`).
- Memory: 384 KB fixed in the field table, plus the CPU history (6 MB for a 4-minute track).
- Found here: the GPU effector pass and the CPU reference disagreed on `Scale` with the default `Add`
  blend, and on 16 other (op, blend) pairs. **Fixed by ADR-1121** on the owner's ruling.

## Revisit triggers

- A song longer than about 47 hours of live input: row indices pass 2^24 in the `f32` newest-row lane.
- Delays beyond 16 s are wanted: raise `kAudioRingRows`; the table grows by 256 B per row.
- A lossless live spectrogram is wanted: add a FrameTap feeding an SPSC queue of rows (the `sonic.live`
  pattern) instead of sampling the triple buffer.
