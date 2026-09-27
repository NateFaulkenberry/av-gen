# ADR-897: Musical features that survive a flat master

**Status:** Accepted
**Date:** 2026-09-26
**Found by:** the GV3 revision audit (reports/modulation.md §1 and §6, director.md §2-§3). "Rebuild"
is a flat-loudness master (1.1 LU range), and every energy the engine measured was its loudness.
**Follows:** ADR-004 (the analyzer), ADR-206 (the structure detector)
**Implemented by:** `bandLevelFromPower`, `energyComposite` and the ADR-897 block of
`Analyzer::computeFrame` (`src/analysis/analyzer.hpp/.cpp`); the side channel in
`AnalysisTrack::analyze`; `stampBandOnsets`' onset rate and composite (`src/analysis/band_onsets.cpp`);
`profileSpan` (`src/analysis/span_profile.hpp/.cpp`); the section energy and density in
`detectStructure` (`src/analysis/structure.cpp`); the `audio.*` signals in
`src/signals/audio_signals.cpp`
**Tests:** `tests/unit/test_flat_master.cpp`, `tests/integration/test_audio_features_bus.cpp`,
`tests/integration/test_rebuild_analysis.cpp` (`[flatmaster]`, skips without the song),
`tests/unit/test_song_structure.cpp`

## Context

Three measures carried "how much is happening", and on a limited master all three were noise:

- **Energy was loudness.** The structure detector's section energy was mean RMS, min-max rescaled to
  the track: "Rebuild"'s sub-heavy break, where the loudness *peaks*, read 1.0 and its thinned
  suspension 0.0. `signals::MusicalEventDetector` works from RMS and the auto-gained bass.
- **Density was structurally zero.** A section's density was the median of a per-hop onset flag --
  one hop in thirty carries an onset, so the median was 0 in every section of every track. The test
  only range-checked it.
- **The bands are auto-gained.** Each `audio.<band>` is divided by its own running peak with a 4 s
  decay: a passage 5 dB quieter reads full scale again within two seconds, so no band level carried
  a section's energy to a route.

The analysis of "Rebuild" made for GV3 (docs/glowmere-valley-3/01-music.md §1.2-1.3) located the
drama where a limiter cannot reach it: the share of power above 2 kHz, brightness, spectral flux,
onset density and stereo width.

## Decision

**Long-term band levels, beside the auto-gained ones and under new names.** `audio.bassLevel`,
`audio.lowMidLevel`, `audio.midLevel`, `audio.highMidLevel`, `audio.trebleLevel`: each band's power
through a 1 s one-pole, in sine-amplitude units (divided by the Hann window's 1.5-bin noise
bandwidth, so a full-scale sine is 0 dB), read on a fixed scale -- 0 = -60 dB, 1 = 0 dB, 6 dB = 0.1.
Never divided by a running maximum.

**A loudness-independent energy composite** (`energyComposite`, `audio.energy`, 0..1), the weighted
mean of fixed-range terms:

| term | range mapped to 0..1 | weight |
|---|---|---|
| power above 2 kHz / all power | -36..-12 dB | 2 |
| spectral centroid, log | 250 Hz..6 kHz | 2 |
| flux / the frame's summed magnitude | 0.08..0.24 | 1 |
| percussive onsets per second | 2..12 | 1, when known |
| stereo side/mid RMS | 0.1..0.4 | 1, when known |

Every term is a ratio or a rate, so scaling the input leaves it where it was. Smoothed over 1 s.
The streaming analyzer computes it without the density term (it has no band onsets); the offline
track recomputes it with that term after its band-onset pass (ADR-898). Live input has neither
density nor width, and a missing term is left out of the mean rather than read as zero.

**Density is an onset rate.** `audio.onsetRate` counts onsets per second through a 2 s leaky window:
offline, the percussive band onsets (one per 30 ms, so a kick with its clap is one hit); live, the
broadband onsets.

**Stereo width** (`audio.width`): side over mid RMS over the analysis window, from the file's side
channel pushed beside the mono downmix. 0 with `stereo = false` for a mono source.

**Section measures** (`profileSpan`): the span's composite -- its frames' level-free terms with the
span's *own* onset rate as the density term, so a short riser is not averaged with the lag of a 1 s
one-pole -- its onset, kick, snare and hat rates, its mean brightness in Hz, each band's mean level in
dB and on the level scale, and its width. The structure detector's `SongSection::energy` and
`density` are these, divided by the piece's largest: a ratio, so the quietest section keeps its
distance from silence instead of being stretched to 0.

### Where an artist finds them

The Modulation panel's route-source picker (Routes -> Add route) lists every signal by its name
with what it is beside it (`ui::routeSourceItems`, from `SignalInfo::label`), so a person scanning
for "density" or "energy" finds these without knowing the code names:
`audio.bassLevel - bass level (steady, not auto-gained)` (and the four other bands),
`audio.energy - energy (how much is happening, loudness-independent)`,
`audio.onsetRate - density (percussive hits per second)`, `audio.width - stereo width`. The World
Inspector's "why is this moving" rows name a route's source the same way, and the assistant's
`signal.list` matches the labels ("density" finds `audio.onsetRate`). Nothing here is visible until
a route makes it so; the route's own controls are where it is adjusted.

## Consequences

- **"Rebuild"**, 13 segments of 01-music.md §1.3: the section composite ranks them as the owner's
  hand-built composite does -- Spearman 0.993, Pearson 0.956 (re-measured 2026-09-26). Riser 1.00, drop 0.95, arrival 0.90
  (scaled to the largest); the break 0.55 is the third-lowest, not the highest; the pull-back 0.38
  is the lowest. Onsets per second run from 0.5 (the pull-back) to 5.5 (the arrival); kicks 2.17/s
  through every groove, 0 in the pull-back.
- **Loudness independence** (synthetic): 12 dB down moves the composite by under 0.03 while RMS
  falls 4x; a dark passage and a bright busy one at the same RMS differ by more than 0.15.
- **Band levels** (synthetic): a 6 dB drop reads 0.10 lower 3.5 s later; the auto-gained band has
  already gone back to full scale.
- **Changed behaviour in existing scenes:** the structure detector's section energy and density
  change for every analysed track -- they are the composite and the onset rate now. Song Mode's
  measured-plan fallback (`songPlanFromMeasurements`), which derives framing, movement and cut rate
  from them, therefore directs detected sections differently; authored section timelines keep the
  energy and density their authors typed. Nothing that already existed on the bus changed value:
  the new features are new signals.
- The composite's weights and ranges were chosen from where these measures sit in produced music and
  checked against "Rebuild"; they are named constants in one function, not scattered.
- The analyzer's per-frame cost grows by one pass over the bins (high-band power, magnitude sum) and,
  offline, one over the side channel.
