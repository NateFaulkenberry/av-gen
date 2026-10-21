# ADR-1574: A route's envelope can latch (a refractory time and a rearm level)

- Status: Accepted (art/rainbow-world-2)
- Amends ADR-011 (the processor chain) and ADR-900. Additive: both fields are off by default, so every chain behaves
  exactly as before. They are written only when used.
- Code:
  - `src/params/processor.{hpp,cpp}`: `envelopeRefractoryMs`, `envelopeRearm`, and their state;
  - `src/params/serialization.cpp`;
  - the route hashes in `src/app/engine.cpp` and `src/scene/composition.cpp`.
- Tests:
  - `tests/unit/test_envelope_latch.cpp` (`[adr1574]`): off by default; a refractory envelope plays its whole hold
    and fall whatever the input does; a held input triggers once, and again only on a new rising edge past the
    rearm level; the round trip.
  - `tests/unit/test_rainbow_world_music.cpp`: the live scan's two routes latch.

## Problem

Rainbow World's live world scan is an envelope (hold, then fall) triggered by a surge in the music. Two things broke
its tail, and the route chain had no way to prevent either:

1. **The gate was a route's depth.** Depth is applied after the chain, every frame, so it does not latch. When the
   gating signal dipped mid-scan, the scan vanished; when it recovered, the scan came back. The owner saw this at
   gate 4 as the effect "popped back into view for a second".
2. **A peak-hold envelope re-triggers whenever its input is above it.** A surge that lingers, or a second one during
   the fall, lifts the scan back up part-way through.

Gating by combining two routes into a macro and latching the macro with a stateful route would need a chain reading a
macro. That is not what the seek replay rebuilds (ADR-901).

## Decision

Two optional fields on the envelope stage (PeakHold and LinearFall):

- **`envelopeRefractoryMs`.** Once the input triggers the envelope, it ignores its input for this long, so the
  envelope plays its whole hold and fall untouched.
- **`envelopeRearm`.** When finite, a trigger disarms the envelope until the input has fallen to this level. Only
  then can it trigger again, and only on a rise (an input above the rearm level and above its own previous value).
  A continuous input therefore re-triggers only on a new rising edge: hysteresis.

The trigger fires on whatever the input is when it rises. The input should be conditioned to on/off (a binary
threshold, an instant attack) so the latched value is the full one. A slow decay before the envelope holds the
input "on" through the music's own dips, such as a kick's sidechain pump.

The state (refractory time left, armed, last input) lives in the chain's state. The seek replay (ADR-901) advances it
like every other chain state, so a seek lands where a play does.

## Consequences

- Rainbow World's live project:
  - **Scan amount:** one latched route per trigger into `macros/scan` (op max):
    - with a synth channel, the synth's rms rising past 0.16;
    - on a plain stereo input, the mix's sustained energy against its adaptive reference rising past 0.6 (standing
      aside while a synth stem is active).
  - **Front:** driven by the same latch, so it never resets mid-scan.
  - Traced over Feline Footwear (26-world-scan.md section 4), the scan has no dips on the way up and no rises on the
    way down.
- Any route can now express "fire once, play it out, don't fire again for a while".
