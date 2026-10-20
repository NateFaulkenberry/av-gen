# ADR-1581: The beat clock runs on the beat feed's grid: a stem's offline, an input source's tracker live

- Status: Accepted (eng/stems-feed)
- Plan: `docs/prototypes/stems-multichannel/08-feed-plan.md`.
- Code:
  - `src/analysis/analysis_track.*`: `trackGrid`, `TrackedGrid`;
  - `src/analysis/stem_analysis.*`: `StemFrame::beatOnsetStrength/beatOnset`;
  - `src/analysis/channel_analysis.*`: the source's `BeatTracker`;
  - `src/app/music_runtime.hpp`: `BeatFields`;
  - `src/app/engine.*`: `beatGrid()`, `advanceClock`, `resolveMeter`, `resolvedTempo`, `beatBoundary`;
  - the readers below.
- Tests:
  - `tests/integration/test_stem_feeds.cpp` (`[beat]`, `[seek]`);
  - `tests/unit/test_feeds.cpp` (live).

## Context

`beat.*` came from the mix: the offline Ellis grid (ADR-896) or, live, the causal `BeatTracker` on the main stream. A
clean kick or drum bus gives a tracker less to be wrong about than a full mix does.

## Decision

**`feeds.beat`** (ADR-1580's block) is `"audio"` (the default) or `"stem.<key>"`.

**Offline: the stem's grid.**
- **What a stem keeps:** each stem hop keeps the Analyzer's own onset strength and picked onset, the input both mix
  trackers use.
- **Tracking:** `trackGrid`, factored out of `AnalysisTrack`'s stamping with the same arithmetic, runs the offline
  tracker and the ADR-896 refinement over them. The beats go onto the project clock with the stem's offset added.
- **The metre:** `estimateMeter` reads the mix's frames on those beats.
- **Fallback:** a stem with no beats (fewer than four onsets) is a warning, and the mix's grid runs the clock.

**`Engine::beatGrid()` is the one grid.** Every reader of "where the beats are" switched to it:
- the beat clock (`beat.*`), the metre (`resolveMeter`), the transport's origin and bpm, and beat stepping;
- the timeline's beat markers and Beats snap;
- the Director's musical context, the camera director's song grid, and effect triggers;
- the engine tools' audio summary.

**`music.*`:** `music.beat`/`downbeat` take each analysis frame's beat and count from the grid
(`MusicRuntime::consume(..., BeatFields)`), so they stay in step with `beat.bar`.

**Stays the mix's measurement (meaning unchanged):**
- `audio.beat`, `audio.tempo`, `audio.beatPhase` and `audio.beatCount`;
- the ADR-898 band onsets, the structure analysis and the audio history.

**Live: an input source's tracker.**
- `ChannelAnalysis` runs a causal `BeatTracker` on the beat feed's source. It is the mix runner's tracker and config,
  fed the same onset input, and reset at a discontinuity. Its state rides on that source's `LiveStemFrame`.
- **Clock and tempo:** the live clock resynchronises to its count and phase, and the detected tempo is its tempo.
- **When the source goes inactive:** the clock free-runs at the last tempo and does not resynchronise to the mix's
  counts, which are another count.
- A live input with no source of that key runs on the mix.

**Precedence:**
- A running MIDI clock still owns phase and count. A tempo override and an embedded tempo still outrank a detected
  tempo.
- Seek equals play: the grid is a pure function of the second, and the test compares every `beat.*` and `music.*`
  after a seek and after a play.

## Consequences

- **Feline Footwear:** the drum stem's grid sits within 3 ms (median) of the mix's, at 121.0 against 120.8 bpm.
- **The stem fixture's backbeat:** its snare on 2 and 4 reads as 60 bpm to both trackers. The test pins the stem's
  tempo to the mix's and every beat to a kick, rather than to 120.
- **Defaults:** with no `feeds.beat`, every `beat.*` and `music.*` column of `--sonic-trace` (which now writes them)
  equals main's on the three tracks.
