# ADR-1060: Causal onsets: kick, snare, hat, a low attack and a broadband onset, the same live and from a file

- **Status:** Accepted (2026-10-02), proto/sonic-garden (VFX expansion, brief §11)
- **Code:** `src/analysis/causal_onsets.{hpp,cpp}`. `AnalysisFrame::causal` and the live event carry
  (`onsetStamp`...`liveSerial`, `LiveEventLatch`) are in `analysis/analyzer.*`. Also `AnalysisRunner` and
  `AnalysisTrack::analyze`, and `Engine::publishFrame` (the latch).
- **Tests:** `tests/unit/test_causal_onsets.cpp` (`[adr1060]`).
- **Research:** `docs/prototypes/sonic-garden/research/02-modulation.md` §1-3 and §9.

## Context

The owner calls transient detection "probably one of the most important upgrades in this phase" (§11): "kick ->
large transient, hi-hat -> fine-grained activity". ADR-898's band onsets distinguish the kick, the snare and the hat,
but only offline: they use centred medians, a +-1 s relative test and harmonic/percussive separation with look-ahead.
Live input had only the broadband `audio.onset` and `sonic.transient`.

Two more live defects:
- the render thread reads only the newest analysis frame (a triple buffer), so a live event on an analysis frame
  between two render frames was lost: about a third of them at 60 fps;
- the Effect Library's EVENT activation could not fire live at all (ADR-1061).

## Decision

1. **A causal detector** runs one per frame stream, on the analyzer's 2048/512 frames. Its value at frame n is a
   function of frames 0..n.
   - The detection function is SuperFlux on `log(1 + 40|X|/ref)`, where `ref` is a causal spectral peak (up at once,
     down over 3 s, floored at -80 dB). The previous frame is max-filtered across 3 bins, then the flux is averaged
     per band. Being relative to `ref` makes the detector level-free: a take 12 dB quieter gives the same kicks
     (ADR-1062's test).
   - Its threshold is `max(1.4 x median(last 31 frames) + delta, 0.12 x decaying peak)`. The decaying peak is Dixon's
     (tau 0.12 s): a bump on the tail of a big attack is not a hit.
   - The output per class is a continuous **ratio** (the ODF over its threshold), plus a default **hit**
     (`HitPicker`: threshold, rising edge, hysteresis, refractory time).
2. **The classes:**
   - `low` is 40-120 Hz: any low attack, a kick or a bass note.
   - `kick` is a low attack that coincides with a broadband click (the onset ratio over about 2.6-5 on this frame or
     the last) while 40-120 Hz holds at least 3-6% of the frame's summed log-flux. A bright bass pluck's attack
     spreads up its harmonics (1-2%). A clickless sine 808 is a `low`.
   - `snare` is 1.5-5 kHz noise, plus the 150-300 Hz body, holding a real share of the frame's power: a kick's click
     is -26 to -37 dB of the total, a bass pluck or hat about -20, a snare -13 to -17.
   - `hat` is 7-16 kHz holding a quarter to a third or more of the frame's new energy.
   - `onset` is 30 Hz-16 kHz.
   - The frame also carries `bassDb`, `levelDb`, `snareDb`, `hatDb`, `snareRise`, `kickShape`, `bodyRatio` and the
     raw ODFs, for the response model (ADR-1062).
3. **The same code runs in both places.** `AnalysisTrack::analyze` walks the file in order with a fresh detector;
   `AnalysisRunner` runs one per frame live (and resets it at a discontinuity). The Sonic response model reads
   `causal`, so file and live agree.
4. **Live `audio.onsetLow/Mid/High`.** The runner writes the causal kick, snare and hat into the band-onset fields,
   which a file gets from ADR-898. So a live drum kit drives `audio.onsetLow` (the kick), `audio.onsetMid` (the snare)
   and `audio.onsetHigh` (the hats). File playback keeps ADR-898's look-ahead answers (`overlayTrackFields`, ADR-896).
5. **Live events are not lost between render frames.**
   - The runner carries each event onto later frames until the render thread has acquired a frame holding it.
     Acquiring publishes the frame's serial.
   - The render thread's `LiveEventLatch` fires each carried event once, by the serial of the frame that raised it.
     A new runner restarts the serials and the latch.
   - This covers `audio.onset` and `audio.beat` too.

## Measured

| material | kick | snare | hat |
|---|---|---|---|
| drum-machine kit with a saw bass and a pad (test, 16 bars, 124 BPM) | P 1.00 / R 1.00 | P 1.00 / R 1.00 | P 0.94 / R 0.98 |

Precision and recall are against the placed hits. In the same kit:
- kicks fired on 0 of 32 bass notes, and `low` fired on all 32;
- the mean latency is -10 to -7 ms: a frame's time is its window's centre, so the attack enters it early;
- a pad alone fires no drum.

On ADR-898's groove with an off-beat bass (a sine with a 4 ms attack, the clickiest bass): kick recall is 1.0, and
the kick fired on 7 of 63 bass notes.

Against ADR-898's offline answers on night-shift (a real mix, 105 s), the causal hats agree P 0.83 / R 0.86 and the
causal kicks find every offline kick (R 1.00) plus 38 more. The offline "mid" onsets there are mostly hats and
near-silence (their relative test fires at -75 dB), so they are not ground truth for snares, and the snare agreement
(0.24) says nothing about either detector.

Cost: one pass over 1025 bins a frame, a 31-entry `nth_element` per class. It is below the analyzer's own FFT and
was not measurable in the live timbre cost readout.

## Consequences

- `AnalysisFrame` grows by about 150 bytes. Nothing serialises it.
- **Limits.**
  - The snare's share test reads a snare in a sparse or drum-led mix. In a dense mix whose other parts fill
    1.5-5 kHz it reads weaker; `snareRise` is published for a route that wants the band's rise instead.
  - A synthetic white-noise "hat" (ADR-898's groove) has energy down to 40 Hz, which no hat has, and is scored only
    on the kick-versus-bass test.
  - Real drums on hardware were not available (no device on this Mac). The live path is the same code, tested
    through the runner.
- ADR-898 stays the file's `audio.onset*`. `response.*` (ADR-1062) is the causal one on both paths.

## Rejected

- **NMF or CNN drum transcription:** templates, iterations and look-ahead (research 2 §2).
- **Classifying by spectral tilt or per-bin flux dominance:** measured, and it failed on the test kit (a clap's
  noise and a hat's read alike).
- **Shortening the analyzer window for latency:** it would break live/file equivalence (ADR-1025 §8).
