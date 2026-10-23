# ADR-1068: A note is not a drum: timbre gates, and a MIDI note-on demotes a noise-poor hit

- **Status:** Accepted (2026-10-02), proto/sonic-garden (VFX expansion; the art agent's second finding)
- **Code:**
  - `CausalOnsets::{snareNoise, hatNoise, hatTilt, drumness}` and the gates in `src/analysis/causal_onsets.cpp`;
  - the demotion in `ResponseModel::publish`;
  - `ResponseSettings::midiDrumness`.
- **Tests:** `tests/unit/test_drum_recall.cpp` (`[adr1068]`, ported pluck-arpeggio and keys-stab material with its MIDI).

## Context

On the pinned engine, `response.kick/snare/hat` fired on melodic material with no drums:

| material | kicks | snares | hats |
|---|---|---|---|
| 13 s pluck arpeggio | 14 | 48 | 27 |
| keys stabs | 8 | 6 | 0 |

ADR-1067's percussive flux alone still passes a pluck's or a stab's attack, because a 1-3 ms attack is a broadband
transient.

## Decision

1. **Hat tilt.** A hat needs its 7-16 kHz percussive flux to be at least 0.6 of the 1.5-5 kHz band's.
   - Measured: arp attacks reach at most about 0.44 (p90); hats have a median in the hundreds and a p10 of 0.49-0.70.
   - A kick's click (even across the bands) no longer counts as a hat.
2. **Snare noise share.** A snare needs 80% of its band's flux at the attack to survive the frequency median.
   - Measured: drum attacks are about 1.0 (p10 0.87 even under the full mix); a chord stab's median is 0.70.
3. **The MIDI cue.** A snare or hat published within 120 ms of a MIDI note-on is that note's attack, and is dropped
   (event and envelope), unless at least 90% of its band's flux was percussive (`sonic.response.midiDrumness`).
   - The window allows for the snare being decided 43 ms late and for one render frame.
   - A drum machine's snare that lands on a stab still fires when it is clearly noise.
   - The cue is a pure function of the note track and time, the same in a file and live.

## Measured

**The art agent's material, through the engine, with its MIDI:**

| material | kicks | snares | hats |
|---|---|---|---|
| arp (88 notes) | 0 | 0 | 2 |
| chords (30 stabs) | 0 | 3 | 0 |
| edrums (16 kicks, 8 snares, 32 hats) | 15 | 8 | 37 |

**The ported material (the test):**
- arp: 0 kicks, 0 snares, 4 hats for 88 attacks;
- chords: 0 kicks, 0 snares, 0 hats for 30 attacks.

**The drum matrix is unchanged in its conclusions.** On the art agent's renders:
- full mix: kick 29/32, snare 16/16, hat 91/105 (98 before the tilt gate);
- drums alone: hat 92/105;
- the port's full mix: hat 93/105.

## Consequences

- Hats that coincide with a kick, whose click spreads below 7 kHz, read as the kick only. That is about 10% of a
  16th-note loop's hats.
- Without MIDI, a bright pluck arpeggio can still fire a few hats. The tilt gate alone took the arp from 45 hats to
  about 2 on the art agent's render.
