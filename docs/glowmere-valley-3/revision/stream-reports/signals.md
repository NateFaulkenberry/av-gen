# Signals stream (ADRs 896–899): the agent's final report

The finishing agent reported this on 2026-09-26. The coordinator merged it into main as
`e0657a26`, after restoring `glowmere-valley-2-multicam.json` (the owner's rule; see PROGRESS).
This is the agent's report, lightly trimmed.

## Summary
3 commits on `agent/signals` (final `a4549cce`, based on `0b623b88`), then the coordinator's
`fe711eab`. Both full suites passed:
- CPU: 3,554 of 3,554 passed, 0 failed, 19 skipped, exit 0.
- GPU: 510 cases, 509 passed, 1 skipped (Syphon client), exit 0.

## Validation on Rebuild
- **Downbeats:** estimated as tracked beat 0 (confidence 1.00), with 8-bar phrases. All 122 bar lines are within 30 ms of `0.480 + (n−1) × 1.846154` s: mean 8.9 ms, worst 18.9 ms. Through the engine at 60 fps, 122 `music.downbeat` events, worst 19.7 ms.
- **Kicks:** low onsets against the 475 scored kicks: 480 detected, 465 matched, **precision 0.969, recall 0.979**, mean error +1.7 ms.
  - 6 of the 10 misses are riser-roll kicks stamped 31–45 ms late.
  - 4 extras sit on the kick-gap beats (beat 4 of bars 24/40/56/72), where the bass turnaround hits.
- **Energy:** the section composite ranks the 13 segments like the owner's composite (Spearman 0.993).

## Where the controls live in the UI
- **Parameters panel → music → meter** (on every authoring layer):
  - "bar 1 starts on beat (-1 = detect)" (`music/meter/bar1Beat`);
  - "bars per phrase (0 = detect)" (`music/meter/phraseBars`);
  - "phrases per section" (`music/meter/sectionPhrases`).
  
  Each detect setting shows what the analysis decided beside it.
- **Modulation panel → Routes → Add route source picker:** every signal is listed as name plus meaning, e.g. `audio.onsetLow - kick (low-band onset)`.
- **Sequence panel → click a section:** its `section.energy` and measured profile, read-only.
- **Transport bar:** the bars readout counts from bar 1.

## Changes to existing scenes
- **Rebuild projects:**
  - downbeats move from beat 4 onto beat 1;
  - phrases become 8 bars and counted sections 32 bars (were 4 and 16);
  - the multicam saucer turns on every beat.
- **bass.mp3** (`glowmere-atmospherics`): the downbeat moves one beat earlier. Pin `bar1Beat` 3 to restore it.
- **glowmere-valley.wav** (`glowmere-stylized`, `glowmere-lyrics`): the downbeat moves two beats earlier (tempo confidence 0.24).
- **The structure detector** now uses the energy composite and the onset rate, so Song Mode's fallback directs differently.
- **The camera fix outside its scope:** a continuous take's last aim-follow gets a join-out (a latent ADR-891 defect, exposed on the multicam film's last frame).

## Defects found, not fixed
- Scene-state Beat/Bar triggers count from the state machine's reset, not bar 1.
- `section.energy` cannot be edited in the UI.
- Mid and high onsets are band-limited, not drum classes. On Rebuild, 229 of 562 mid onsets are the claps; 436 of 842 high onsets are the off-beat hats.
- Live input has no band onsets or stereo width.
- GV3's project still has `control.phraseBars`, so it loads with a warning.

## How GV3 should use it
- **Project JSON:** delete `control.phraseBars` and `control.sectionPhrases`, and pin `"music/meter/bar1Beat": 0, "music/meter/phraseBars": 8`.
- **Musical time:**
  - `beat.count` is 0 at bar 1;
  - bar n, beat m is `(n−1)×4 + (m−1)` beats;
  - the Beat trigger `everyN 4, offset 0` fires on every downbeat;
  - beat-synced LFOs can replace GV3's free-running ones. To keep the 20 ms lead, set `sources/<n>/phase = 0.02 / (beatsPerCycle × 0.461538)`.
- **Kicks:** `audio.onsetLow` can replace `timeline.kick`, but it fires 0–18 ms after the kick. Keep the scored kicks where the 20 ms lead matters.
- **Section drivers (Rebuild means):**
  - `audio.energy`: 0.40–0.53 typical; pull-back 0.23, break 0.34; riser 0.62, drop 0.58, arrival 0.56.
  - `audio.onsetRate`: 4.3/s in the grooves; pull-back 0.5; 5.2–5.5 in the lift, arrival and drop.
  - Hat onsets: 3.3/s in the grooves; break 2.0; 4.3–4.4 from the lift and arrival.
  - `audio.trebleLevel`: ~0.55; break 0.23, pull-back 0.15; it barely moves at the arrival. Use the hat rate or `audio.energy` for the arrival's sparkle.
  - `audio.width`: 0.16–0.21, and 0.28 in the riser and drop.
- **Sections:** `section.*` follows GV3's 13-section timeline, and `section.energy` carries the owner's values.
- **Resolution** does not affect these signals.

## How the Director streams should use it
- **song:**
  - `director.inspect_scene` gives each section's `energy`/`density`, an `audio` block (`energy`, `onsetRate`, `kickRate`, `snareRate`, `hatRate`, `brightnessHz`, `bandsDb`, `bandLevels`, `width`) and a `meter` block.
  - The same data is in `SongPlanSection::audio` and `MusicalContext::downbeat`.
  - `Engine::meter().barTimes(beats)` gives the downbeat grid.
- **reactivity:**
  - `audio.onsetLow`/`Mid`/`High` for micro;
  - `beat.bar` and `music.downbeat` for meso;
  - `section.energy`, `audio.energy` and the band levels as macro depth sources.
