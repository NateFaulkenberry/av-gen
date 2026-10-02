"""The test matrix's prose (matrix.py report reads it): the material, the detector on it, how to read a row, and the
art agent's verdict per scene. Kept in the repo so TEST-MATRIX.md regenerates from data, not from the review folder.
"""

PREAMBLE = """
## The input material

The thirteen classes are synthesized at 120 BPM by `tools/sonic_vfx/make_test_material.py`. The WAVs go to
`assets/audio` (gitignored) and the MIDI to `examples/sonic-vfx/test/`. A scene hears the drums from the audio, as it
would from a kit's output: the drum-only classes run with no MIDI at all (their hits are written separately, on
channel 10, as `<class>-drums.mid`, for reference).

| class | what plays |
|---|---|
| pads | four slow five-note pad chords, two bars each |
| chords | four-note chord stabs in a syncopated rhythm |
| bass | an 8th-note bass line, driven |
| lead | a monophonic saw lead: legato phrases and a fast run |
| arp | 16th-note arpeggios over two octaves on a pluck |
| edrums | electronic drums one at a time (kick, snare, hats), then all three |
| drumloop | four-on-the-floor with snares and claps, 16th hats with open hats, and a tom fill |
| dense | 32nd-note pluck bursts and wide eight-note chords (no drums) |
| sparse | four bell notes with long silences between |
| velocity | one motif six times, velocity 20 to 127 |
| rapid | repeated 16ths on one note, then a 32nd-note trill |
| sustained | very long held notes, then a held chord |
| full | the pads 10 dB under, the bass, the lead and the drum loop together |

## The live drum detector, on this material

The engine was pinned at e6978f28 (ADR-1067, drums under a mix; ADR-1068, a note is not a drum) and then at 96bc0214
(ADR-1069, a material op, and ADR-1070, the live sky; neither touches the detector or a file render). Recall counts a
hit's envelope passing 0.3 within -30..+90 ms of the placed hit (`tools/sonic_vfx/drum_recall.py`); false detections
are in brackets.

| mix | kick | snare/clap | hat (open hats included) |
|---|---|---|---|
| drums alone | 31/32 (+0) | 16/16 (+0) | 107/120 (+0) |
| + lead (a saw) | 31/32 (+0) | 16/16 (+0) | 109/120 (+0) |
| + bass line | 31/32 (+3) | 16/16 (+7) | 102/120 (+0) |
| + pads, 10 dB under | 28/32 (+0) | 16/16 (+0) | 105/120 (+1) |
| full mix | 29/32 (+4) | 16/16 (+0) | 106/120 (+1) |

Before those two ADRs the full mix was 1/32, 0/16 and 13/105. On melodic material, an arp fired 14 kicks, 48 snares
and 27 hats, and chord stabs fired 8 kicks and 6 snares. With the MIDI cue the arp now fires 0/0/2 and the stabs
0/3/0.

## How to read a row

- **expected:** the scene's vocabulary rows that this input exercises.
- **observed:** each of those signals as the evaluator measured it:
  - *answered* (synchrony z >= 3), *weak* (2-3) or *silent* (< 2), with the hit latency;
  - *not played* when the input never exercised it (a hit the detector did not fire, a level that did not move).
- **quality:** the evaluator's mean dimension score and its lowest dimension.
- **problems:** its medium and high findings.

The evaluator is `tools/sonic_vfx_critic.py` at 28347601. That version judges accents and highlights among the rare
pixels, uses a fair null for periodic material, has a slow tier, and analyses at 384x216.

What the numbers cannot say:
- A slow, sustained answer (a sky warming over a held chord) has little variance to correlate when the input holds
  still for the whole clip. *Silent* on `sustain` in the pads and sustained classes often means the input never
  moved it, not that the picture ignores it; the full class, where sustain comes and goes, is the fairer test.
- *Late response* fires for every `response.*Env` whose answer peaks more than 100 ms after the hit, including
  envelopes the scene never routes (`lowEnv`, `onsetEnv`). Each is a medium penalty on musical_synchronization
  (`exp(-0.35 x penalties)`), so that dimension sits near 0 on most clips whatever the scene's own drums do. Read the
  observed column for synchronisation, not that score.
- *No dominant subject* is a 64x36 spectral-residual saliency map's top peak under 1.5x its second. It splits one
  subject into several peaks (the black hole's two disk limbs, a plate's figure), so it fires on nearly every clip.
- The verdicts below are the art agent's, from the clips and the sheets, with the numbers as evidence.
"""

# The live run (live.py --scenario tour): every scene through the switcher with live input, its table pasted here.
LIVE = """
## Live, through the switcher

`python3 tools/sonic_vfx/live.py salt-flat-mirage --scenario tour --no-capture` (2026-10-02, pin 96bc0214, an M2 Max
driving a 60 Hz 5K display): the editor opened with live input on (BlackHole for audio, the probe's CoreMIDI source for
notes), and the probe played the `tour` scenario. Every 20 s it sends a MIDI program change (program 0 is Sonic Live,
then the sixteen scenes in set-list order), then plays a held chord, a 16-note arpeggio and a distorted low riff.
Every scene opened through the switcher, received all 24 of its notes, and its response envelopes moved.

The statistics skip each scene's first 3 s (the switch loads the scene). An interval under 16.7 ms means the editor
presented faster than the display refreshes; the light scenes do. The editor's adaptive render scale
(budget 16.67 ms of GPU, floor 0.5) moves between the full canvas (2732x1978) and half of it (1366x988). It
steps down a rung at a time, so a heavy scene that follows a light one starts at the light one's resolution. The
Cathedral's row shows exactly that. Opened alone (`live.py corrupted-cathedral --scenario demo --no-capture`), it
settled at 0.5 within seven seconds and ran at a 17.1 ms median interval.

| # | scene | frames | interval p50 / p99 ms | GPU p50 / p95 ms | notes | noteEnv max | kickEnv max | sustain max |
|---|---|---|---|---|---|---|---|---|
| 0 | Sonic Live | 610 | 33.3 / 33.8 | 27.5 / 30.9 | 24 | 0.92 | 1.00 | 0.92 |
| 1 | Salt Flat Mirage | 1020 | 16.7 / 16.9 | 12.5 / 13.3 | 24 | 0.94 | 1.00 | 0.88 |
| 2 | Lantern Lake | 1020 | 16.7 / 16.9 | 12.1 / 13.9 | 24 | 0.94 | 1.00 | 0.97 |
| 3 | Aurora Tundra | 1020 | 16.7 / 17.2 | 12.1 / 14.2 | 24 | 0.94 | 1.00 | 0.89 |
| 4 | The Breathing Deep | 1013 | 16.7 / 17.9 | 16.2 / 16.7 | 24 | 0.93 | 1.00 | 0.97 |
| 5 | Abyssal Bloom | 1020 | 16.7 / 17.4 | 15.1 / 17.2 | 24 | 0.94 | 1.00 | 0.89 |
| 6 | Cymatic Plate | 1019 | 16.7 / 17.5 | 13.2 / 15.4 | 24 | 0.94 | 1.00 | 0.96 |
| 7 | Silk Theatre | 808 | 20.1 / 24.7 | 19.5 / 23.2 | 24 | 0.93 | 1.00 | 0.89 |
| 8 | Ferrofluid Crown | 683 | 24.8 / 30.4 | 24.2 / 26.6 | 24 | 0.94 | 1.00 | 0.96 |
| 9 | Feedback Mirror | 1023 | 9.3 / 26.0 | 8.9 / 11.7 | 24 | 0.93 | 1.00 | 0.88 |
| 10 | Tesla Choir | 764 | 22.8 / 23.7 | 22.3 / 22.8 | 24 | 0.94 | 1.00 | 0.92 |
| 11 | Datascape | 1020 | 9.4 / 26.1 | 10.9 / 13.5 | 24 | 0.94 | 1.00 | 0.86 |
| 12 | Ember Forest | 762 | 22.3 / 23.5 | 21.6 / 22.3 | 24 | 0.94 | 1.00 | 0.97 |
| 13 | The Corrupted Cathedral | 286 | 59.2 / 63.9 | 58.5 / 61.1 | 24 | 0.89 | 0.80 | 0.86 |
| 14 | Storm Cell | 789 | 21.7 / 23.4 | 21.2 / 22.3 | 24 | 0.95 | 1.00 | 0.98 |
| 15 | Stellar Nursery | 1020 | 16.8 / 26.0 | 12.6 / 13.0 | 24 | 0.94 | 1.00 | 0.89 |
| 16 | Event Horizon | 1017 | 16.8 / 25.9 | 13.0 / 13.3 | 24 | 0.93 | 1.00 | 0.97 |
""".strip()

# scene id -> the art agent's verdict, written from the clips and the sheets after the matrix run
VERDICTS = {}
