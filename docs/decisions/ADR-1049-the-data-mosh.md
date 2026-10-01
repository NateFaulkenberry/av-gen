# ADR-1049: The data mosh: a temporal effect that corrupts blocks from the clean history

- Status: Accepted (2026-10-01), proto/liminal-space (All You Got art pass 2)
- `TemporalEffectKind::Mosh` and `MoshSettings` in `src/scene/temporal_settings.*`; the pass in
  `src/rendering/temporal_effects.cpp` and `fs_mosh` in `shaders/temporal.wgsl` (whose uniform block gained
  an `extra` vec4, mirrored in `temporal_history.cpp`).
- Tests: `tests/rendering/test_temporal_gpu.cpp` (`[adr1049]`), and the family's conformance checks.

## Context

Art pass 2 asks for the world to feel like "a slightly broken simulation": data moshing, positional
corruption, colour corruption, frame-like discontinuities, used sparingly and on musical events. Positional
jitter is keyable already. What was missing is a picture-level corruption: blocks frozen or smeared from
earlier frames, and the colour channels pulled apart.

A naive data mosh is a feedback effect (it smears its own output), which ADR-410 forbids: an IIR filter
cannot be rebuilt by a seek's warm-up. The temporal family already holds a ring of *clean* past frames.

## Decision

A second temporal kind, `mosh`, reads the ring like the echo does. For each block of `block` pixels, a hash
of the block and the epoch `floor(time x rate) + seed` decides whether it is corrupted (probability
`amount`), how many frames back it comes from (1..`frames`), and which way it is dragged (`smear` pixels).
The red and blue channels are offset by `shift` pixels everywhere and twice that inside corrupted blocks.
Pixel sizes are authored at 1080 lines and scale with the output. It runs after the echo and before the
capture, so the ring never holds a corrupted frame.

Parameters `temporal/mosh/{enabled, frames, amount, block, smear, shift, rate, seed}`; JSON `"temporal":
{"mosh": {...}}`. Enabled with amount and shift both 0 encodes nothing: the frame is byte-identical and the
ring stays warm for the event that turns it up.

## Consequences

- FIR over the clean ring and a function of the clock: scrub-safe after the ring's warm-up, deterministic
  across fresh renderers (tested).
- The ring is half resolution by default, so a corrupted block is softer than its neighbours. That reads as
  damage, and is kept.
- One more full-screen pass at output resolution while it is active; the capture is shared with the echo.
- No panel row yet (the echo has one); it is reachable through parameters, routes and the timeline.
