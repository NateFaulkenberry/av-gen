# ADR-1521: Live input sources publish the stem vocabulary, from one callback and one analysis thread

- Status: Accepted (eng/stems-multichannel)
- Extends: ADR-021 (live input) and ADR-1025 (the 128-frame period). Uses ADR-1520's `StemAnalyzer` and vocabulary.
- Code:
  - `src/audio/audio_input.*`: sources, taps and stats;
  - `src/audio/capture_backend.hpp`, `src/audio/input_source.hpp`, `src/audio/synthetic_capture.*`;
  - `src/analysis/channel_analysis.*`;
  - `src/app/engine.*`: `setInputSources`, `useAudioInputBackend`, `publishInputSources`;
  - `src/app/settings.*`: `live.inputSources`;
  - `src/ui/live_panel.cpp`: the Inputs table;
  - `src/app/application.cpp`: `--input-sources`, `--input synthetic:<n>`.
- Tests: `tests/unit/test_multichannel_input.cpp` (`[multichannel][threading]`, in the TSan set) and
  `tests/unit/test_app_settings.cpp`.

## Decision

**One vocabulary.** A live input channel, or a linked pair, analysed on its own publishes `stem.<key>.*` with exactly
an offline stem's names and meanings. A scene routed on `stem.drums.level` while it is made offline therefore plays
live with the drum bus patched to an input keyed `drums`.
- While an input is open, a live key wins over an offline stem's.
- Offline stems with no live counterpart read inactive then: there is no mix playing for them to follow.

**One callback.** `AudioInput`'s callback writes each source's own SPSC ring (the existing `AnalysisStream`) from the
block it already walks, beside the main mono stream. The main stream is unchanged: the average of every channel.
- Every source shares the device's sample clock.
- A block goes into a ring **whole or not at all**. A dropped block, or a jump in the device's sample time, marks a
  discontinuity at the true frame, so the analyzer resets instead of splicing two separated pieces of audio into one
  window.
- Rings are 16,384 floats (341 ms of mono at 48 kHz).
- Unchanged sources keep their rings across an edit.

**Pairs.** A pair is ONE source, analysed as the mono sum (L+R)/2 with `width` its side/mid. Its ring carries (mid,
side) frames. Two channels are summed only when the artist makes them a pair.

**One analysis thread** for every source (`ChannelAnalysis`), not one per source. It steps each source's
`StemAnalyzer`.
- Measured per source per hop: 16 µs microbenchmarked at the levels tier; 95-114 µs microbenchmarked and 158 µs live
  at the hits tier.
- It publishes each source through a triple buffer, with ADR-1060's event carry. `LiveStemLatch` fires a carried
  event once.
- A rebuild adopts the state of unchanged sources.

**Never stale.** A source reads inactive (every value 0, no event):
- on a channel the device lacks;
- while the device is disconnected;
- after 250 ms with no new frame.

**The channel map is the machine's.** `live.inputSources` in settings, keyed by device id; channels are 1-based in the
file. Every place that opens an input applies it first, and `--input-sources <n>[h[k]]` overrides it for a run.

**The synthetic backend** (`synthetic:<n>`) is test- and benchmark-only and is in no device list. It puts a distinct
tone and staggered clicks on each channel, pumped by the caller or in real time, and can simulate a disconnect, a
format change or a sample-time skip on command. The integration tests drive the real `AudioInput`, `ChannelAnalysis`
and `Engine` through it.

**Telemetry.** Every 10 s while sources run, the log reports:
- sources active;
- the callback's mean and max time;
- drops, gaps and overloads;
- analysis µs per source-hop and its share of a core;
- onset latency p50/p99, measured from the newest sample of the analysed window to the render frame that put the onset
  on the bus.

## Consequences

- **Callback cost (synthetic, planar):** 0.75 / 2.76 / 8.75 µs per callback with 2 / 8 / 24 sources, against a
  2,667 µs period.
- **Live arms:** `05-benchmarks.md`.
- **Not done:**
  - per-source Sonic, `response.*` or beat tracking;
  - per-source gain (the input gain applies to all);
  - ADAT channels on the Volt, untested without an ADAT source.
