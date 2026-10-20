# ADR-1582: Each stem and input source has a trim in dB before its analysis; it is not a parameter

- Status: Accepted (eng/stems-feed)
- Code:
  - `src/analysis/stem_analysis.*`: `StemAnalysisConfig::gainDb`, `StemAnalyzer::setGainDb`, `trimGain`;
  - `src/analysis/channel_analysis.*`: `setGainDb`;
  - `src/audio/stem_source.hpp`, `src/audio/input_source.hpp`: `gainDb`;
  - `src/app/settings.cpp`: `live.inputSources[device][].gainDb`;
  - `src/app/engine.cpp`: `assets.stems[].gainDb`, the trims-only fast path;
  - `src/ui/live_panel.cpp`: the Inputs table's Gain column;
  - `src/ui/sequence_panel.cpp`: the Stems section's gain.
- Tests:
  - `tests/unit/test_feeds.cpp`;
  - `tests/unit/test_app_settings.cpp`;
  - `tests/integration/test_stem_feeds.cpp`.

## Context

One Input gain (`audio/inputGain`) scaled the main stream and every live source together. A quiet channel on a wide
interface (a DI at -40 dBFS beside a hot drum bus) could not be brought up on its own. A quiet stem had no trim at all.

## Decision

**What the trim is:**
- A trim per source, in dB, from -24 to +24, default 0.
- It is applied to the source's samples before its analysis: in `StemAnalyzer::push`, on a copy.
- At 0 dB it touches nothing, and a test compares the frames bit for bit.

**Where it lives:**
- **Offline:** `assets.stems[].gainDb`, written when not 0. A change analyses that stem again, as a file change does.
  The name is `gainDb`, not `gain`, because the clip list already has a linear `gain` and a trim is read in dB.
- **Live:** `live.inputSources[device][].gainDb` in the per-machine channel map, written when not 0.
  - It stacks on Input gain, which still scales every source in the callback.
  - The analysis thread applies a change at its next read, without a reset. The callback is untouched.
  - A trims-only edit takes a fast path: no stream or tier is rebuilt.
  - The Inputs table's Gain column applies it while dragging and saves on release.

**What it changes (ADR-1510's choice for Input gain, per source):**
- `stem.<key>.rms` and `.peak` scale with it.
- The levels (`stem.<key>.level`, and the response levels a feed takes from it) read against the source's own
  adaptive reference. The trim moves the gate and the reference floor relative to the signal; it does not make a level
  bigger once the reference has adapted. Tested: +12 dB moves the drum stem's mean level by less than 0.05.
- Hits and the beat tracker: the detectors' absolute floors move relative to the signal, so a quiet source's attacks
  register. Tested: the drum stem 42 dB down loses more than half its kicks, and a +24 dB trim brings back at least
  three quarters.
- So it gates and feeds detection; it is not a visual intensity.

**Not MIDI-mappable.** The sources are keyed per machine. A parameter's one home is the project file, and the channel
map's home is the machine's settings: that would be two homes for one number. A trim is set at sound check. The
performance controls are the response knobs, which map.
