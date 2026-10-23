# Stem-aware audio analysis: where it stands and what to do next (report only)

The owner's brief, §4 (`16-hit-phaseout-option-b-brief.md`). Nothing here is implemented. The measurements were made on
2026-10-23 with eng/mix-hit-phaseout at 9031be29 (option B).

**The material:**
- the mix, `assets/audio/feline-footwear.wav`, which stays the playback source;
- three stems, `assets/audio/stems/feline-footwear (Drums|Bass|Synth).wav`, used for analysis only.

The stems are local and gitignored, and are never committed.

## 1. The architecture as built

| Part | Code | What it does |
|---|---|---|
| Stems in a project | `assets.stems[]`: `key`, `label`, `file`, `hits`, `gainDb`, `offsetSeconds` (`src/audio/stem_source.*`, ADR-1520) | Each stem is analysed at its own rate into a `StemTrack` (`src/analysis/stem_analysis.*`) when the project loads. Playback stays the mix. |
| The stem vocabulary | `stem.<key>.active, rms, peak, level, onset, onsetStrength`, the five bands, `centroid`, `width`, `kick/snare/hat` (with `hits` on), `onsetLow/Mid/High` (`src/signals/stem_signals.*`, ADR-1520/1521/1604) | A pure function of the instant, so a seek replays it exactly. Inactive outside the stem's span. |
| Live input sources | `live.inputSources`: a key and a channel or linked pair; `ChannelAnalysis` (`src/analysis/channel_analysis.*`); the AUHAL backend (ADR-1521/1522) | Each mapped channel publishes the same `stem.<key>.*`, so a scene authored on `stem.drums.*` offline plays live with the drum bus patched to an input keyed `drums`. |
| Feeds | the `feeds` block (ADR-1580/1581) | Routes `response.*` hit classes, the level family and the beat clock to a stem or input source. `mergeFeeds` / `FeedFolder` offline, `LiveFeedMixer` and an SPSC queue live. |
| Choosing per idea | `signal.*` with `firstActive` (ADR-1602) | "The stem if it is active, else the mix" as one signal. Rainbow World (14 signals) and ADR-1620's `signal.kick` use it. |
| Not stem-aware | `AudioHistory` (the GPU onset rows and spectra: Resonator, the generative fields), `TriggerClock` band triggers, Astral's `SongAnalysis`, the Director's span profile | These read the mix's analysis frames only. This is audit P1-4. |

## 2. Test results on Feline Footwear

**Alignment (measured):**
- All four files are 48 kHz, stereo, 24-bit.
- The stems are 210.5000 s long; the mix is 19 samples longer (210.5004 s).
- The three stems sum to the mix to within -51.5 dBFS, a residual 40.4 dB below the mix, with a best gain of 1.001.
- The sample-level lag is 0 (cross-correlation within ±200 samples at 70 s) and the envelope lag is 0 ms.
- The set therefore needs no offset and no trim: `offsetSeconds` 0 and `gainDb` 0 are correct.

**Analysis cost:** the three stems analyse in 2.0-2.2 s at load (`stems: analysed 3 of 3`).

**Events, against the drum stem's own classifier** (60 fps; matched within -30..+120 ms; the "fed" column uses
`"hits": "stem.drums", "levels": "stem.bass", "beat": "stem.drums"`):

| signal | mix only | fed from the stems |
|---|---|---|
| kick (`response.kick`) | none (ADR-1620); the old mix detector was 0.54 / 0.62 | **1.00 / 1.00**: event for event the stem's |
| snare (`response.snare`) | 0.93 / 0.73 | 1.00 / 1.00 |
| hat (`response.hat`) | 0.85 / 0.94 | 1.00 / 1.00 |
| any drum hit (`response.onset`) | 0.98 / 0.84 | 0.99 / 0.95 |
| `response.low` against the bass stem's onsets | 0.38 / 0.81 (it also fires on kicks: 0.27 / 0.66) | 0.36 / 0.43 (fed from the drums it no longer hears the bass) |
| `beat.pulse` against the stem's kicks | 0.59 / 0.62, +17 ms | 0.49 / 0.51, -17 ms (the drum stem's grid is not better on this song) |

**Activity per stem (210 s):** 1,120 drum onsets (406 kicks, 235 snares, 545 hats), 465 bass onsets and 296 synth
onsets. The mix's `response.onset` (951) is 0.98 precise against the drums, but covers only 51% of the onsets of all
three stems: on a mix, the onset is a drum signal, not a bass or synth one.

**Fallback (measured):**
- With no stems, every `firstActive` signal plays its mix input (ADR-1602's test, and B4's mix-only runs).
- With no feeds, `response.*` is the mix's, minus the kick (ADR-1620).
- A feed naming a missing key warns and reads the mix. The kick then reads nothing, and the route audit says so.
- An inactive source feeds nothing rather than the mix: a stem outside its span, or a stalled channel.

## 3. Gaps and risks

1. **Identity and activity are mixed in the vocabulary.**
   - `stem.<key>.kick/snare/hat` exist on every source with `hits` on, including a bass or synth stem. A pluck or a bass
     note can raise a "hat" or a "kick" there. Measured: the mix's hat detector fired 42 times on the synth stem alone.
   - Nothing in a project says "this source is drums".
   - Only `response.*` through `feeds`, and the drum signals' `stem.drums.*`, rely on the key name `drums`.
2. **A drums source without `hits` is a silent kick.** `signal.kick` picks an active `stem.drums` and never fires. A
   live input keyed `drums` defaults to no hits.
3. **The level family from one source.** `feeds.levels` routes all seven levels to one source. Routing the levels from
   the drums makes them pulse (ADR-1580), and the bass and the synth cannot each drive their own level through
   `response.*`. Today scenes read `stem.bass.level` directly.
4. **`AudioHistory` is mix-only (P1-4).** Resonator, Cymatic World and the generative fields read the GPU history of the
   mix. They cannot excite a field from the drums alone or the bass alone without scene-specific routes. This is the
   largest gap for "Resonator and experimental systems".
5. **Live timing (ADR-1605, loopback, 30 fps, sound to light):**
   - an onset (mix or stem) is 73 / 91 ms (p50 / p95);
   - a kick class is 103 / 122 ms, and a snare 119 / 129 ms, because the class is decided 28-41 ms after the attack;
   - the stem path is not slower than the mix path;
   - a live source's hop that arrives after its mix hop folds into the next one, adding up to one hop (10.7 ms);
   - a source more than 250 ms stale reads inactive.

   The class latency, not stems, is the live risk.
6. **The beat grid from the drum stem is worse here.** Against Feline's stem kicks it is 0.49 / 0.51, against 0.59 /
   0.62 from the mix. `feeds.beat` should not become a default.

## 4. Recommendations, ordered by impact and then cost

**Implemented** on eng/stem-roles (the owner's "go for it"):
- recommendations 1, 2 and 4 are ADR-1630;
- recommendation 3 is ADR-1631;
- recommendation 5 is ADR-1632;
- recommendation 6 needed nothing.

The before and after measurements are probe B5 (`probes/B5-stem-roles/`).

1. **Say what a source is (high impact, small).** Add a `role` to a stem or input source (`drums`, `bass`, `synth`,
   `vocal`, `other`).
   - Run the kick/snare/hat classifier only where the role is `drums`. Other roles publish onsets, levels and register
     only, never a class name.
   - The drum signals and the feeds' defaults then key on the role, not on the string `drums`.

   This is the brief's rule ("measured identity separate from activity") made structural.
2. **Turn on `hits` for a `drums` role automatically (high, trivial).** This removes the silent-kick trap of gap 2.
3. **`AudioHistory` from a source (P1-4: high impact, medium cost).** Let a field read the drum source's onset rows and
   the bass source's spectrum, falling back to the mix. Resonator and the generative fields gain the stems with no
   scene change.
4. **Per-role levels (medium, small).** Add `signal.*` presets in `tools/drum_signals.py`, e.g. `signal.bassLevel` =
   `stem.bass.level` else `response.bass`, so a scene names an idea once rather than routing `feeds.levels`.
5. **A stem-set check at load (medium, small).** Check rate, length, channel count and sum-to-mix (the residual
   measured above), and warn when a stem set is misaligned. Today a wrong offset is silent.
6. **Measure, don't default, `feeds.beat` (low).** Keep the mix's grid as the default (gap 6).

There is no need for source separation, ML or a new library. The material is already separated, and the detectors are
level-free.

## 5. Proposal: validating live multichannel inputs on the owner's hardware

**Hardware and routing:**
- **Interface:** the owner's Universal Audio Volt 876 (analog and ADAT banks: ADR-1522), with BlackHole as the loopback.
- **Setup:** play the three Feline stems out of a DAW or `tools/sonic_live_probe` into separate BlackHole channels,
  one each for the drums, the bass and the synth, and the mix on its own pair.
- **AV Gen's inputs:** `live.inputSources` (or `--input-sources`), e.g. `drums@<ch>h` (with hits), `bass@<ch>` and
  `synth@<ch>`, with the mix as the main input.
- Then repeat on the Volt 876, through its analog inputs and a cable loop from its outputs. That adds the interface's
  3.1 ms of device latency (02 §6.1) and its clock.

**Checks, each against the same project offline with the stem files** (`--sonic-live-log` plus `--sonic-trace`):

| # | What | Pass |
|---|---|---|
| 1 | **Equivalence.** Live `stem.<key>.*` events against the offline stems' | ≥ 95% of events within ±1 frame (30 fps) |
| 2 | **Sound-to-light latency** (ADR-1605's probe) for the drum stem's onset and kick, live | onset ≤ 45 ms p50 / 60 ms p95 at the bus (T10); report light |
| 3 | **Channel skew.** One click on all channels at once | ≤ 1 hop (10.7 ms) apart on the bus |
| 4 | **Dropout and recovery.** Mute or unplug the drums channel mid-song | `stem.drums.active` reads 0 within 250 ms; `signal.kick` falls back to the beat in the same frame; no stale events; it recovers when the channel returns |
| 5 | **Format change.** Switch the Volt's sample rate, or ADAT off and on | sources on missing channels read inactive (ADR-1522); no crash; queue drops counted |
| 6 | **Load.** Four sources plus the mix for 30 minutes at 1080p30 on a live scene | no feed-queue drops; live-perf baseline T1-T9 unchanged (ADR-1608) |

ADR-1550 applies throughout: never resize a device that another app is using, and device tests are opt-in.
