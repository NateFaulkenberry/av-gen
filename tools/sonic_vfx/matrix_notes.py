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
- *Late response* findings are the evaluator's lag search on slow signals; a SLOW chain is late by design.
- The verdicts below are the art agent's, from the clips and the sheets, with the numbers as evidence.
"""

# scene id -> the art agent's verdict, written from the clips and the sheets after the matrix run
VERDICTS = {}
