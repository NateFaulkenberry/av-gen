# ADR-896: One definition of musical time

**Status:** Accepted
**Date:** 2026-09-26
**Found by:** the GV3 revision audit (reports/modulation.md §1, director.md §3): on "Rebuild" every
bar the engine drew began on beat 4.
**Follows:** ADR-041 (phrases and sections from the beat clock), ADR-073 (`music.*`), ADR-870 (the
seek replays the signal bus)
**Implemented by:** `analysis::Meter`, `analysis::estimateMeter`, `analysis::clockBeatsAt`
(`src/analysis/meter.hpp/.cpp`); the sub-hop grid refinement in `stampBeats`
(`src/analysis/analysis_track.cpp`); `Engine::meter`, `Engine::meterSource`, the `music/meter/*`
parameters (`Engine::registerMeterParameters`) and `Engine::advanceClock` (`src/app/engine.cpp`);
`ui::meterDetectNote` and `music/` on the Beginner layer (`src/ui/ui_logic.hpp`); every consumer
listed below
**Tools:** `avgen_music_report` (`tools/music_report.cpp`); `tools/make_test_audio.py --pickup-beats`
**Tests:** `tests/unit/test_meter.cpp`, `tests/integration/test_musical_time.cpp` (`[meter]`),
`tests/integration/test_rebuild_analysis.cpp` (`[rebuild]`, skips without the song),
`tests/rendering/test_material_gpu.cpp` (the bar input)

## Context

A beat tracker finds beats. Turning a beat into a bar needs one more fact -- which beat is beat 1 --
and every consumer supplied its own:

- **The bus.** `AnalysisFrame::beatCount` is how many beats have landed: 1 on the first tracked beat.
  `beat.bar` was `(count % 4 + phase) / 4` and `music.downbeat` fired on `count % 4 == 0`, so the
  tracker's first beat was beat 2 of a bar and every bar line fell on its fourth beat. "Rebuild"
  starts on its downbeat (0.480 s), so every `music.downbeat`, `beat.bar` wrap and phrase boundary
  was one beat early. The same count fed LFO beat sync, the timeline's beats time base, the shaders'
  bar input (`frame.beat.w`, recomputed in the renderer from the analysis frame) and the user-shader
  std uniforms.
- **The effect triggers** counted beats from 0, so their "every 4th beat" landed on beats 0, 4, 8 and
  the bus's downbeats on 3, 7, 11: never the same instant.
- **The sequencer** (its Bar events, the bar snap) and **the Director** ("bar 97") took every fourth
  tracked beat from the first -- right on "Rebuild" by luck, wrong on any track with a pickup.
- **The transport's bars readout** counted from 0 s, a beat before "Rebuild"'s first bar.
- **The phrase length** was a fixed 4 bars; "Rebuild"'s phrases are 8.

Nothing estimated where the downbeat is, and nothing let a person say.

## Decision

**One value, `analysis::Meter`**: beats per bar, bars per phrase, phrases per section, and
`downbeat` -- the beat of the beat clock (0 = the first tracked beat) on which bar 1 begins. A clock
position `clockBeats` becomes the musical position `meter.beats(clockBeats)`: 0.0 exactly on bar 1
beat 1, negative in a pickup. Bars, phrases and sections are floor divisions of it, defined on both
sides of zero.

**The engine resolves one meter per call** (`Engine::meter`):

| field | from |
|---|---|
| `downbeat` | `music/meter/bar1Beat` when pinned (0 or more), else the analysis's estimate, else 0 (and 0 for a MIDI clock, whose beat 0 is its own downbeat) |
| `phraseBars` | `music/meter/phraseBars` when pinned (1 or more), else the estimate, else 4 |
| `sectionPhrases` | `music/meter/sectionPhrases` (default 4) |

The three settings are exposed integer parameters, saved in the project's `parameters` like any
other and not modulatable (a bar line that moved with the audio would be no bar line). Their
"detect" values, -1 and 0, are saved as detect: an estimated phrase length or bar phase is
recomputed from the audio on load, because writing it would turn a measurement into a decision no
later analysis could correct. The old `control.phraseBars` and `control.sectionPhrases` keys are not
read (see Consequences).

### Where an artist finds it

- **Parameters panel -> music -> meter**: "bar 1 starts on beat (-1 = detect)", "bars per phrase
  (0 = detect)", "phrases per section". The value of "bar 1 starts on beat" counts tracked beats from
  0 (the first beat the tracker heard), so a bar line one beat early is fixed by adding 1.
- `music/` is on the **Beginner** layer list (`ui::detail::kBeginnerPrefixes`). Off every list, the
  group showed only on Advanced while the editor opens on Intermediate: ADR-375's defect a third
  time, caught by the test before it shipped.
- A setting left on detect says what the analysis decided, beside its slider: "detected: beat 0
  (confidence 1.00)", "detected: 8 bars", or "not clear from the audio: 4 bars"
  (`ui::meterDetectNote` over `Engine::meterSource`).
- The transport's bars readout counts from bar 1, so a wrong phase is visible as "1.1" landing a beat
  off the first downbeat.

**Everything that turns beats into bars reads it:**

| consumer | before | now |
|---|---|---|
| `beat.bar`, `beat.phrase*`, `beat.section*` | `count % 4` | `meter.barPhase(musical)` etc. |
| `beat.count` | beats landed (1 on the first) | the musical beat: 0 on bar 1 beat 1 |
| `music.downbeat`, `music.bar/phrase/section` | `count % 4 == 0` | `meter.beatInBar(beat) == 0` |
| effect trigger, Beat source | tracked beat index | musical beat (offset 0 = bar 1 beat 1) |
| LFO `beatSync` | count + phase | `SourceContext::musicalBeats` |
| timeline `timeBase: beats` | count + phase | musical beats |
| material `beatPhase.w`, std `beat.yz` | renderer's own count | `ShaderFrameInputs::barPhase`, the bus |
| sequence Bar/Beat events, bar snap | every 4th beat from the first | `meter.barTimes(beats)` |
| Director "bar N beat M" | index `(N-1)*4 + (M-1)` | offset by `meter.downbeat` |
| transport bars readout | from 0 s | from the second bar 1 falls on |
| Auto-director structure fold | 4-bar phrases from beat 4 | the meter |

**The estimate** (`estimateMeter`) scores each phase from two kinds of evidence:

1. *The backbeat*: the 2-16 kHz rise at each beat; beats 2 and 4 carry the snare or clap. A 6 dB
   backbeat is full evidence; it fixes the phase to within two beats.
2. *Where things change*: the spectral change across each beat over 1, 2 and 4-beat windows, counting
   only clear local maxima (2.5 robust units), each once at its peak. Arrangement changes land on
   bar lines.

Plus a 0.05 prior for the first tracked beat. The phrase length is the longest of 4, 8, 16 bars whose
starts carry at least twice the change of the half-phrase points (and a mean robust z of 1.5).

**The offline beat clock is a pure function of the second.** With an analysed grid,
`clockBeatsAt(beatTimes, period, seconds)` interpolates between tracked beats and extrapolates at the
track's tempo outside them, in both engine modes. It replaces the per-frame extrapolation, which
resynchronised at analysis beats and could pulse twice when a tracked interval ran long. Live input
keeps the extrapolating clock.

**The tracked grid is refined below the hop.** The Ellis tracker places beats on 10.7 ms hops and,
through a drum-less passage, on whatever onsets exist: through "Rebuild"'s two-bar pull-back its
beats walked 33 ms off the grid and one bar line landed 30.4 ms out. Each beat is replaced by a robust
local constant-tempo fit through its 8 neighbours either side, weighted by onset strength and a Tukey
biweight (25 ms), moving no beat more than 0.1 of a period.

**Live playback of a file** overlays the whole-track analysis's beat fields and band onsets on the
live runner's frames (`overlayTrackFields`), so the editor's `music.downbeat` is a render's.

**A baked sequence follows the meter.** A sequence's Beat and Bar events are resolved against the
meter when it is installed. The engine records that meter, and when a sequence that has such events
finds the meter changed -- pinned in the Parameters panel, or a new track's estimate -- the next
`update` re-installs it, so the editor's events land where a render of the saved project puts them.
A sequence with no Beat or Bar event is never re-installed for this.

**Not changed: the scene-state machine's Beat and Bar triggers** (`app::StateMachine`) count beats
and bar wraps from the machine's own reset, not from bar 1. Their bar edge is `beat.bar`'s wrap, so
it lands on the meter's downbeat; the "every N" count does not start at bar 1. No example project
uses them; recorded here rather than folded in.

## Consequences

- **"Rebuild"** (`avgen_music_report ~/Desktop/Rebuild.mp3 --grid 130 0.480`, re-measured
  2026-09-26): estimate tracked beat 0 (confidence 1.00; phase scores 1.17, -0.25, 0.43, -0.31),
  phrases 8 bars (evidence 5.2); **all 122 bar lines within 30 ms of the true bars** (mean 8.9 ms,
  worst 18.9 ms), none off the grid. Before: every bar line a beat (461 ms) early. With the estimate
  but without the grid refinement, 121 of 122 were within 30 ms (worst 30.4 ms; measured during
  development, before the refinement landed). Through the engine at 60 fps: 122 `music.downbeat`
  events, worst 19.7 ms from the true bar; `beat.bar` wraps on the first render frame at or after
  each bar, worst 31.5 ms -- the grid's error plus up to one frame (16.7 ms) of quantisation.
- **Synthetic grooves** with pickups of 0-3 beats: the estimate is the pickup every time, and every
  bar line is within 30 ms. The old convention misses them by a beat or more.
- **`assets/audio/night-shift.wav`** (`tools/make_city_score.py`, bar 1 at 0 s, a drum-less intro):
  estimate tracked beat 3, all 41 bar lines within 30 ms (worst 2.3 ms). The old convention's
  beat-3 answer happened to be right for this track, so nothing built on it moves.
- **Changed behaviour in existing scenes** -- every scene with audio whose routes read the bar:
  `beat.bar`, `beat.phrase*`, `beat.section*`, `music.downbeat/bar/phrase/section`, beat-synced LFOs
  with `beatsPerCycle` > 1, timeline keys in beats and effect Beat triggers now count from the
  estimated downbeat. The old convention put bar 1 on tracked beat 3 (the fourth beat heard) on
  every track. `beat.count` is the musical beat -- 1 + downbeat less than the old count of beats
  landed -- and negative in a pickup. Per track, measured with `avgen_music_report`:
  - **"Rebuild"** -- `glowmere-valley-2`, `-multicam`, `-song`, `effects/ufo-stack`, `_pre-defects`
    and the nine `_diag-water-*` arms (14 projects), all routing `music.downbeat`, `music.phrase` and
    `music.section`. Estimate beat 0: every downbeat moves one beat later, from the old beat-4
    position onto beat 1 (verified against the hand-measured grid, above), so the elder's practical
    light answers the true downbeat. Their phrase length becomes the estimate's **8 bars** (was the
    unpinned default 4, see the next bullet): `music.phrase` fires every 8 bars from bar 1 (was every
    4 from beat 4), and `music.section` and `beat.section*` every 32 bars (4 phrases; was 16). The
    audit found those phrase and section routes swallowed by their attack (1.2% and 0.8% of their
    amount reaches the target) or on the dead `emissiveBoost`, so little of this is visible.
  - **`bass.mp3`** -- `glowmere-atmospherics`: estimate beat 2 at confidence 0.41, no clear phrase
    length (4 bars). Its downbeats move one beat earlier than the old beat 3. Not checked against a
    hand-measured grid; pin `music/meter/bar1Beat` 3 to restore the old phase.
  - **`assets/audio/glowmere-valley.wav`** -- `glowmere-stylized`, `composition/glowmere-lyrics`: an
    ambient score the tracker reads at tempo confidence 0.24. Estimate beat 1 (confidence 0.99),
    phrases 4 bars: the downbeat moves two beats from the old beat 3; the phrase length is unchanged.
  - **"Hellopines 2"** -- `tree-of-life-floating-island` and its twelve `_ca-*` / `_vx2-*` arms:
    estimate beat 3, the old convention's beat, and no clear phrase length: nothing moves (and none
    of them routes a bar signal).
  - `infinite` (`beat.bar`) and `machine` (`beat.phrasePulse`) have no audio: no clock runs, nothing
    moves.
  - No example project uses beat Triggers, beat-based timeline keys or beat-synced LFOs. GV3 reads
    none of these signals yet.
  - A user shader reading `sys.beat.y` (the beat count; `shaders/examples/feedback.wgsl` orbits a
    blob by it) now reads the musical beat: with audio, a constant offset of 1 + downbeat beats.
  - The Auto-director's structure fold (`structureOfTrack`) now reads the engine's meter: its
    phrase and section boundaries land on true downbeats (the audit measured every one of them a
    beat early on "Rebuild"). With an 8-bar phrase estimate its counted sections are 32 bars long
    (`sectionPhrases` 4); set `music/meter/sectionPhrases` to 2 for 16.
- **The old `control.phraseBars` / `control.sectionPhrases` keys are gone** (ADR-442, no alias).
  Every save wrote them, so 29 tracked projects carried them -- all at the defaults, 4 and 4, never
  an authored decision. They are stripped from all 29, which is what hands the "Rebuild" projects
  their estimated 8-bar phrases above. A file that still carries either key loads with a project
  warning naming the parameters that replaced it, rather than dropping a phrase length without a
  word. GV3's project on its own branch carries both keys and needs the same strip.
- **The effect trigger's Beat source and `beat.count` agree**: offset *k* fires first on the beat
  `beat.count` reads *k* on.
- **`audio.beat` no longer loses beats offline**: the batch merge that already kept onsets now keeps
  the beat flag (and the band onsets, ADR-898) of every analysis frame a render frame consumes.
- `SignalClock` gains `clockBeats`, `lastSectionIndex` and the overlay cursor; the seek replay and
  its checkpoints carry them, and the replay key includes the resolved meter.
- **Not estimated:** beats per bar (everything downstream is written for 4/4) and a phrase offset
  other than bar 1.
