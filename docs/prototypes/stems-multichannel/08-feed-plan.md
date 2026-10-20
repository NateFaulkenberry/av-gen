# Stems and live channels feed the beat clock and response.*: the plan (gate)

Branch `eng/stems-feed` from main `4fe11873`, 2026-10-20. The owner's request: "Stems and live channels: they don't feed
the beat clock or the response.* signals, and there's no per-input gain." ADR range 1580-1589. Nothing is built yet;
this waits for approval.

## 1. What is there today (read in the code)

| Piece | Where | What it reads |
|---|---|---|
| `response.*` | `sonic::ResponseModel::step` (src/sonic/response.cpp), stepped by `SonicRuntime::step` on the hop clock | only `TimbreFeatures::causal` (the mix's `CausalOnsets`: `hit/strength/drumness[5]`, `levelDb`, `bassDb`, `ratio[Onset]`) plus `f0Hz`/`pitchConfidence` for `melodic`/`pitch` |
| ...offline | `SonicRuntime::advance` walks `AnalysisTrack` frames with `SonicSetup::timbre[i]`; a backward seek replays from frame 0 | |
| ...live | `LiveTimbre` (main runner's tap) -> SPSC snapshots -> `LiveSonic::frame` steps the same `SonicRuntime` | |
| Beat clock `beat.*` | `Engine::advanceClock`: MIDI clock, else the track's offline grid (`clockBeatsAt(track_->beats().beatTimes, ...)`, a pure function of the second), else live: the main runner's causal `BeatTracker` via `clock.latest` | `AnalysisFrame::onsetStrength`/`onset` of the mix |
| Metre | `Engine::resolveMeter` from `track_->meterEstimate()`; `music.*` from the frame's `beat`/`beatCount` | the mix grid |
| Other grid readers | sequence panel markers and snap, `beatBoundary`, transport origin, camera director, directing context, effect triggers, astral song, engine tools; structure and audio history | `track_->beats()` |
| Stems | `StemTrack` per file (own rate, own hop); `StemFrame` keeps rms/peak/level/onset/bands and, on the hits tier, kick/snare/hat flags only | the full `CausalOnsets` is computed on the hits tier and thrown away |
| Live sources | `ChannelAnalysis` (one thread, a `StemAnalyzer` per source, triple buffers); times are input-stream frames, the same clock the main runner stamps | |
| Gain | one `audio/inputGain` (linear 0..8 param, clamped 16 in the callback), applied to the main stream AND every source tap in the capture callback | |

## 2. response.* from sources: (a), (b) or (c)

**(a) Feeds: choose which source feeds today's `response.*`.** One routing table: each hit class (kick, low, snare, hat,
onset) and the level family each take their input from the mix or from one source. No new signals.

**(b) A per-source vocabulary** `stem.<key>.response.*` (or `stem.<key>.kickEnv`, `.sustain`, ...): a `ResponseModel`
per source.

**(c) Both.**

| | (a) feeds | (b) per-source vocabulary | (c) both |
|---|---|---|---|
| "The kick comes from the drums stem", no route edits | yes: one line | no: every route on `response.kick*` must be rewritten to `stem.drums.response.kick*` | yes |
| Existing scenes unchanged by default | yes (no feed = today's code path) | yes | yes |
| New signals on the bus | 0 | about 27 per source (8 sources: 216), declared and replayed | 216 |
| Cost | the hits tier on the fed sources only (measured 80-100 us/hop each) | the hits tier on every source with a response, plus a ResponseModel per source in every step and every seek replay | the most |
| ADR-1510 | the levels' adaptive reference follows the levels' source (one reference, one source: see below) | one reference per source | both |
| Overlap with what exists | none | large: `stem.<key>.level` is already ADR-1510's level, `stem.<key>.kick/snare/hat` already the hits | |
| Sensitivity / Transient / Sustain / Attack / Release knobs | apply as today, to whatever feeds | need a decision per source (shared knobs, or N sets) | |

**Choice: (a).** It answers the request with the smallest new surface, every scene keeps working, and a project
changes where the kick comes from without touching a route. What (b) adds beyond `stem.<key>.*` today is shaped
envelopes and sustain/flux per source *at the same time as the mix's*; no scene needs that yet, and if one does, the
cheap form is a few `stem.<key>.kickEnv`-style envelopes on the existing stem vocabulary, not a second response model.
The owner may prefer (b) or (c); the feeds layer in (a) is a prerequisite of neither, so choosing (a) now does not
block them.

### 2.1 The granularity

- **Hits: per class.** `kick`, `low`, `snare`, `hat`, `onset` may each name a source. `hits` sets all five at once; a
  per-class key overrides it. Typical: `"hits": "stem.drums"`, or a multitrack kit with
  `"kick": "stem.kick_in", "snare": "stem.snare_top", "hat": "stem.oh"`.
- **Levels: one source for the family.** `bass`, `level`, `levelSlow`, `intensity`, `sustain`, `transient`, `flux` all
  come from `levels`. They share one adaptive loudness reference (ADR-1510), and that reference must be the loudness
  of the same signal the levels are read from: bass from a bass stem read against the mix's reference would sit at
  the bottom of its window. Splitting the family would need one reference per level; not worth it now (bass from the
  bass stem is already `stem.bass.level`).
- **Stays on the mix:** `melodic`, `pitch` (the timbre stage's f0, mix only), `note` (MIDI), `hatRate` follows the hat
  feed (it counts hat hits).
- **The drumness gate (ADR-1068)** reads the drumness of whichever source fired the snare or hat.

### 2.2 How it works

- **`ResponseFeed`**: the seven fields the model reads, per class where it is per class (`valid`, `hit[5]`,
  `strength[5]`, `drumness[5]`, `onsetRatio`, `levelDb`, `bassDb`), built from a `CausalOnsets`.
  `ResponseModel::step` reads a `ResponseFeed` instead of `f.causal` directly. With no feeds it is built from
  `f.causal` every step, the same floats in the same order: bit-identical (the trace proves it, §6).
- **Folding by time.** A source frame belongs to the main hop whose time is the first at or after it (project time:
  the stem's time plus its offset). Events in (previous main hop, this hop] are OR-ed at their strongest; continuous
  values (`levelDb`, `bassDb`, `onsetRatio`) are the newest source frame at or before the hop. One function,
  `foldFeed`, used by both paths.
- **Offline:** when any response feed is set, the engine builds `SonicSetup::feed`, a vector parallel to `timbre`
  (about 60 B per hop: 1.3 MB for 3.5 min), when the stems, the feeds or the track change. `SonicRuntime::advance`
  steps with `feed[i]`. It is a pure function of the frame sequence, so a seek replay equals a play; `SonicSetup::key`
  hashes it so the replay cache sees a change.
- **Live:** `ChannelAnalysis` pushes each fed source's `ResponseFeed` with its stream time into a fixed SPSC queue
  (the `LiveTimbre` pattern: no lock, no allocation, drop-newest when full, counted). `LiveSonic::frame` folds the
  queued source frames into each main snapshot by stream time before stepping it. Both streams are written by the same
  capture callback, so their frame clocks agree. A source frame that arrives after its main hop was stepped folds into
  the next (at most a hop late; counted).
- **The hits tier is turned on for a fed source** (a stem or a live source): the `CausalOnsets` it needs is the hits
  tier. The saved `hits` flag is not changed; `stem.<key>.kick/snare/hat` publish whenever the tier runs. (On a fed
  source that had `hits: false`, those three stem signals start firing; no shipped scene routes them.)
- **An inactive source feeds nothing.** Outside a stem's span, on a disconnected or absent channel, its classes have no
  hits and the levels read 0 (below the gate, the reference holds). Falling back to the mix silently would make a dead
  kick channel indistinguishable from a working one.
- **An unknown key** (no stem or live source of that key) is a project warning, and that class reads from the mix: a
  typo must not black out a show. The Live panel shows which feeds are unresolved.

### 2.3 ADR-1510 kept

- Every level is read against the adaptive reference of its own source (the levels feed). Hits stay level-free.
- `tools/check_response_range.py` runs unchanged (defaults, no feeds) and must pass. I will also run its targets on
  the Feline stems with `levels: stem.drums` and `hits: stem.drums` as a report (not a gate: a drum stem's levels are
  a different signal from a master's), and add `--feed` to the tool if that is cheap.

## 3. The beat clock from a source

One project setting, `feeds.beat`: `"audio"` (the mix, the default) or `"stem.<key>"`.

**Offline (stems).**
- At analysis the stem also keeps the plain Analyzer's onset strength and picked onset per hop (the input the mix's
  trackers use: two fields on `StemFrame`). From that the engine runs the same offline Ellis tracker and the same
  ADR-896 grid refinement (`stampBeats` factored into a function over (times, onset strengths)), shifts the beats by
  the stem's offset, and estimates the metre (downbeat, phrase) with the existing `estimateMeter` over the mix's frames
  and the stem's beats. Result: `analysis::BeatGrid {beatTimes, tempoBpm, confidence, meter}`.
- `Engine::beatGrid()` returns the source's grid, or the track's when `feeds.beat` is unset or the stem cannot give one
  (no beats found: a warning, and the mix's grid). Outside the stem's span the clock extrapolates at the stem's tempo
  (what `clockBeatsAt` already does past the last beat).
- **Readers that switch to `beatGrid()`:** `advanceClock` (beat.*), `resolveMeter`, the transport origin and tempo
  readout, `beatBoundary`, the sequence panel's markers and snap, the camera director, the directing context, effect
  triggers, the astral song, the engine tools. `music.*` takes its beat and count from the grid at each analysis frame
  (`MusicRuntime::consume` gets the beat fields as an argument), so `music.downbeat` and `beat.bar` stay in step.
- **Stay on the mix:** `audio.beat`, `audio.tempo`, `audio.beatPhase`, `audio.beatCount` (they are the mix's
  measurement; their meaning does not change), the ADR-898 band onsets, structure and audio history.
- **Seek equals play:** the grid is a pure function of the second, so the existing seek arithmetic holds; a test steps
  a play and lands seeks at the same instants and compares `beat.*` and `music.*`.

**Live (channels).**
- `ChannelAnalysis` runs a causal `BeatTracker` on the beat source's slot (the same tracker and config the main runner
  uses, fed the same Analyzer onset), reset at a discontinuity. Its `BeatState` rides on that source's `LiveStemFrame`.
- `advanceClock`'s live branch resynchronises to the source's beat count and phase, and the detected tempo
  (`resolvedTempo`, `beat.bpm`) is the source's. A source that goes inactive leaves the clock free-running at its last
  tempo (as the mix's tracker does in silence).
- A live input with no source of that key: the mix drives it and the Live panel says so.

**Precedence, unchanged in shape:** MIDI clock (when `tempoSource` is midi and it runs) > the tempo override's bpm >
embedded tempo > the beat source's detected tempo. The phase comes from the beat source whenever no MIDI clock runs.

## 4. Per-input gain

- **What it is:** a trim in dB per source, -24..+24, default 0, applied to the source's samples before its analysis.
- **Live:** a per-tap atomic in `AudioInput`, multiplied with the global input gain in `feedTap` (one multiply per
  sample, no lock). Saved in the per-machine channel map (`live.inputSources[device][].gainDb`, written when not 0). A
  "Gain" column in the Inputs table (a small dB drag, double-click resets).
- **Offline:** `assets.stems[].gainDb`, written when not 0, applied to the decoded chunks before the `StemAnalyzer`; a
  change re-analyses that stem (2-4 s for 3.5 min, as a file change does). A Gain drag in the Stems section.
  I prefer `gainDb` to the brief's `gain`: the clip list already has a linear `gain`, and a trim is read in dB.
- **What it changes, consistent with ADR-1510:**
  - `stem.<key>.rms` and `.peak` scale with it (they are raw).
  - `stem.<key>.level` and the levels it feeds: the gate (-60 dBFS) and the reference floor (-40) move relative to the
    signal; once the reference has adapted (seconds) the reading is the same. Gain does not make a level bigger.
  - Hits: the detector's absolute floors (the kick's -45/-60 dBFS, the snare's band floor, the deltas) move relative to
    the signal, so a quiet channel's hits start to register. The ratios are level-free.
  - `.onset` (log flux) and the auto-gained bands barely move.
  - The beat tracker's onset strength scales, and its picked onsets have an absolute floor, so a trim also brings a
    quiet kick channel into the beat tracker.
  So it does exactly what the global Input gain does since ADR-1510, per source: it gates and feeds detection; it is
  not a visual intensity.
- **Not MIDI-mappable.** The sources are per machine and keyed at run time; a parameter has one home in the project
  file, and the channel map's home is the machine's settings: two homes for one number. A trim is set at sound check.
  The performance controls are the response knobs, which already map. (If the owner wants a mappable per-source level,
  the place is a `stem.<key>`-keyed parameter added like the sources' parameters; that is a separate decision.)
- **Mix pair:** the main stream keeps only the global Input gain; the per-source trim applies to sources.

## 5. Where it is saved, and the UI

```json
"feeds": {
  "beat": "stem.drums",
  "hits": "stem.drums",
  "kick": "stem.kick_in",
  "levels": "audio"
}
```

- A top-level project block, written only when a feed is set (a project without one round-trips byte for byte).
  Values: `"audio"` or `"stem.<key>"` (the bus names: unambiguous even if a stem is keyed `mix` or `audio`). The same
  key resolves to an offline stem or, while an input is open, a live source, as `stem.<key>.*` already does, so a
  project set up on stems plays live with the drum bus on an input keyed `drums`.
- Not under `sonic`: a project without a `sonic` block (live uses its defaults) must be able to set feeds without
  gaining a `sonic` block, and the beat feed has nothing to do with Sonic.
- **UI, compact, existing style:** one "Feeds" row of three combos (Beat / Hits / Levels: Main mix or a source), drawn
  by one helper under the Live panel's Inputs table and in the Audio clips popup's Stems section. A per-class override
  from the file shows as "Hits: custom" with a tooltip listing it. Gain as above.
- **Unresolved or inactive feeds** show beside the combo (a dim "drums: inactive").

## 6. Proof that defaults are unchanged

- `--sonic-trace` on Feline Footwear, Trench and Hyperborea (the projects `check_response_range.py` builds, plus each
  shipped example on those tracks that has a `sonic` block), on main (`4fe11873`) and on the branch, compared byte for
  byte (CSV), every `sonic.*`, `timbre.*`, `notes.*` and `response.*` column. The beat clock: a trace of `beat.*` and
  `music.*` over the same three (a small `--beat-trace` hidden flag if no existing trace carries them).
- Tests (CPU unless marked), on the M1 stem fixture and the synthetic multichannel backend only:
  - feed off: the response feed built from `f.causal` equals the old path, frame for frame;
  - `kick` fed from the fixture's drum stem fires on the drum stem's kicks and not on the bass stem's notes; levels
    fed from a stem follow that stem's reference; an inactive span feeds nothing; unknown key warns and uses the mix;
  - offline seek equals play for `response.*` and `beat.*`/`music.*` with feeds set;
  - beat grid from a click-only stem under a noisy mix: the tempo and beats of the stem, the offset honoured;
  - live: synthetic 4-channel device, beat source on the kick channel, the tracker locks; live response feed folds
    hits by stream time (counts and timing within one hop); a disconnected source feeds nothing;
  - gain: a -30 dB stem with +24 dB trim fires the hits the 0 dB stem fires; `gainDb` 0 reproduces the old
    `StemFrame`s bit for bit; settings and project round-trip; the callback with trims stays allocation-free (TSan set);
  - project integrity: `feeds` is read, written only when set, and an unknown value is a load warning.
- Final checks as briefed: cpu-assets, gpu, integrity, response range, each exit 0.

## 7. ADRs

- **ADR-1580** response.* feeds: per-class hit sources and one levels source; the reference follows the levels'
  source; inactive feeds nothing, unknown falls back with a warning; (b)/(c) considered.
- **ADR-1581** the beat grid from a source: `Engine::beatGrid()`, the readers that switch and those that stay on the
  mix (`audio.*` is the mix's measurement); live tracker on the channel thread; MIDI precedence.
- **ADR-1582** the per-source trim: dB, before analysis, what it changes, not a parameter.

## 8. Order of work (each step committed, tests with it)

1. `ResponseFeed` + `foldFeed`; `ResponseModel::step` on a feed; the trace baselines on main first.
2. `StemFrame` keeps the onset input and the hits tier keeps `ResponseFeed`; `feeds` parsed/saved; offline response
   feeds; tests.
3. `BeatGrid`, `beatGrid()` and its readers, `music.*` beat fields; offline beat source; seek tests.
4. Live: `ChannelAnalysis` beat tracker and feed queue; `LiveSonic` fold; live clock; synthetic-device tests.
5. Gain: offline and live, settings, UI column; tests.
6. UI feeds row; docs (`docs/audio.md`, help pages, ADRs, README row); before/after traces; final checks; report.

## 9. Questions for the owner (defaults I will take if there is no answer)

1. (a) feeds, rather than (b) per-source response signals? **Default: (a).**
2. An inactive fed source gives silence, not the mix? **Default: silence** (an unknown key falls back, with a warning).
3. Levels as one family (one source)? **Default: yes.**
4. Trim in dB, not MIDI-mappable? **Default: yes.**
