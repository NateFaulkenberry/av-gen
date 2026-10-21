# ADR-1575: A route chain can read a trend (short-term over long-term average)

- Status: Accepted (art/rainbow-world-2)
- Amends ADR-011 (the processor chain) and ADR-1182 (the normalise stage). Additive: off by default (`trendSeconds`
  0), and written only when used.
- Code:
  - `src/params/processor.{hpp,cpp}`: the stage, after the normalise stage, before the gain;
  - `src/params/serialization.cpp`;
  - the route hashes in `src/app/engine.cpp` and `src/scene/composition.cpp`.
- Tests: `tests/unit/test_envelope_latch.cpp` (`[adr1575]`):
  - a steady input reads 1 at any level;
  - a swell to 1.5x reads above 1.2;
  - after silence, a blip is bounded by the floor;
  - the round trip.

## Problem

Rainbow World's live world scan should fire at the music's swells and peaks, 3-5 times a track including the drop,
on a synth stem or on a plain stereo mix. No single signal through the existing chain did that:

- **An absolute threshold** fires once and cannot re-arm while a loud section holds.
- **The normalise stage** (ADR-1182: level over a decaying peak) reads close to 1 throughout any steady section.
- **A rise built from two routes into a macro,** with a stateful route latching the macro, is not replay-safe. The
  seek replay (ADR-901) samples the sources but does not write the knobs those routes feed.

## Decision

A trend stage. When `trendSeconds` > 0, the chain replaces its input x with fast(x) / max(slow(x), `trendFloor`):

- fast(x) is a one-pole over `trendFastMs`;
- slow(x) is a one-pole over `trendSeconds`.

The ratio reads 1 while the music holds steady and above 1 while it swells past what it has just been doing,
whatever its absolute level: on any input gain, and over a loud section's own bed. It is the musical event
detector's rise measure (its short and long energies), made a route stage.

The floor bounds the ratio out of near-silence, such as an intro or a gap.

The state (the two averages) lives in the chain, so the seek replay advances it and a seek lands where a play does.

## Consequences

Rainbow World's live scan triggers on a trend, latched by ADR-1574:

| Path | Signal | Trend | Floor | Fires at |
|---|---|---|---|---|
| Stem | `stem.synth.rms` | 8 s | 0.06 | 1.25x |
| Mix | `response.sustain` | 8 s | 0.1 | 1.1x |

Both use a 12 s refractory time. Traced on three tracks (26-world-scan.md section 4), the scan fires 4-6 times per
track, and once within two bars of each drop.
