# 1. The music: "Rebuild"

An independent analysis, made for directing rather than for tagging. The measurements (band
energies, harmonic/percussive separation, constant-Q pitch, a 16th-note onset grid, bar
self-similarity, EBU R128 loudness and stereo width) were taken locally with librosa and the
spectrograms were read bar by bar. The raw curves, plots and scripts stay outside the repository
with the audio they were measured from -- the track is not ours to publish -- in
`~/Desktop/av-gen-review/17-glowmere-valley-3-prep/music/`. What is here is what a director needs.

AV Gen's own analyser was used as one source and disagreed in ways worth recording (§6).

## 1.1 The grid

| | |
|---|---|
| Tempo | **130.000 BPM**, constant (quantised in the DAW: no drift, no tempo change) |
| Meter | 4/4. Beat 0.461538 s, bar 1.846154 s, 8-bar phrase 14.77 s |
| First downbeat | **0.480 s** -- the audio starts on it; 0.0-0.48 s is silent pre-roll |
| Length | 122 bars. Last hit bar 122 beat 3 at **224.79 s**; the reverb tail ends 225.38 s |

Bar *n* starts at `0.480 + (n - 1) x 1.846154` s. The grid was fitted exhaustively (129.90-130.10 BPM
in 0.01 steps, all phases) on four independent onset envelopes and agrees with a blind beat tracker
to 7 ms RMS. **Everything in this production is placed on that grid, not on a tracked one.**

## 1.2 What the track is

A 130 BPM four-on-the-floor groove over a G drone. No verses, no choruses, no vocal the spectra
reveal. It grows by **adding a layer every 16 bars** (riff at bar 5, body at 17, a mid synth at 33,
the top shimmer at 41, a new lead colour at 49), holds a long hypnotic plateau, and spends all its
drama in one place: a **subtract, submerge, build, drop** sequence across bars 81-97. The two-bar
pull-back at 26 s is the same gesture in miniature, a promise the track keeps two and a half minutes
later. It then leaves without ceremony: two bars of kick and clap and a hard stop.

**Loudness barely moves** (-10.9 LUFS integrated, a 1.1 LU range). The drama is in brightness,
density, stereo width and subtraction. Anything that drives visuals from raw loudness is driving
them from noise -- and worse, loudness *peaks* in the break, where the sub swells as the top closes.

## 1.3 The segments

Dance-music vocabulary, because it fits and pop vocabulary does not. Energy is a composite
(loudness, flux, onset density, >2 kHz power, percussive RMS, width) scaled so the riser crest is 1.

| # | Segment | Bars | Time (s) | Energy | What happens | Relation |
|---|---|---|---|---|---|---|
| 1 | **cold open** | 1-4 | 0.48-7.87 | 0.69 | Kick, clap, off-beat hats and a pumping G sub, full groove from the first beat | start |
| 2 | **riff groove** | 5-14 | 7.87-26.33 | 0.71 | A plucked, filtered riff (D/C# over G) joins: the basic statement | + one layer |
| 3 | **first pull-back** | 15-16 | 26.33-30.02 | 0.29 | Drums out, top gone (centroid 670 Hz), bass and riff under a low-pass, an Eb colour | release; a tease |
| 4 | **groove 2** | 17-32 | 30.02-59.56 | 0.73 | Full drums back and brighter; kick gap at bar 24 | variation, fuller |
| 5 | **lift** | 33-40 | 59.56-74.33 | 0.80 | A G-centred mid synth and busier hats: the biggest energy step so far | escalation |
| 6 | **arrival** | 41-48 | 74.33-89.10 | 0.81 | The shimmer top layer (~9 and 12.5 kHz) and 16th shakers arrive: the track *sparkles* | first secondary peak |
| 7 | **melodic plateau** | 49-72 | 89.10-133.40 | 0.81 | Long and hypnotic; the lead leans on F# over G. Kick gaps at 56 and 72 | variation |
| 8 | **lead forward** | 73-80 | 133.40-148.17 | 0.76 | The mid lead pushed forward, the top slightly sparser | variation |
| 9 | **suspension** | 81-88 | 148.17-162.94 | 0.74 | Shimmer and 16ths cut; the lead stands exposed over kick, clap and bass | anticipation by subtraction |
| 10 | **submerged break** | 89-92 | 162.94-170.33 | 0.51 | Low-pass "underwater": centroid ~1 kHz, sub +5 dB, kick muffled, an Eb pad | the release |
| 11 | **riser** | 93-96 | 170.33-177.71 | 0.87 (peak **1.00**) | Filter reopens; roll 8ths -> 16ths -> ~32nds; pitched riser 0.8->4 kHz; bass leans to G# | acceleration |
| 12 | **the drop** | 97-120 | 177.71-222.02 | 0.83 | Crash and impact; the full kit at the **widest** stereo and **brightest** sustained centroid | climax, recapitulation |
| 13 | **tail** | 121-122 | 222.02-225.38 | 0.41 | Kick and clap alone; last hit 224.79 s; a cut, not a fade | hard ending |

Sub-phrase lines inside the long segments, usable as cut points: bar 25 (44.79 s); bars 57
(103.86 s) and 65 (118.63 s); bars 105 (192.48 s) and 113 (207.25 s).

**Peak hierarchy:** the drop at 177.71 s (with the riser crest just before it) > the arrival at
74.33 s > the lift at 59.56 s > the return at 30.02 s. **Low points:** the pull-back (26.3-30.0 s),
the break (162.9-170.3 s), the tail.

## 1.4 Rhythm, and the motifs a director can hang things on

- **Kick:** four on the floor through every groove, 3-4x stronger than any off-beat. The bass is
  sidechain-pumped: it swells *between* kicks, so the groove breathes on every beat.
- **Clap/snare:** beats 2 and 4, about 10 dB brighter than 1 and 3 above 8 kHz.
- **Hats:** open hats on every off-beat from bar 1. **16th shakers** join at bar 41, leave at 81,
  return at 97 -- the same span as the shimmer layer.
- **The roll into the drop:** 8ths in bars 93-94, 16ths in 95-96, about 32nds on the last beat.
- **Syncopation:** low. Nearly everything sits on the 8th grid.

| Motif | Where | Period | Use |
|---|---|---|---|
| upward **bass glide** on beat 4 | every 2nd bar of every groove | 3.69 s | a breath for secondary motion |
| **bass turnaround** (octave jump, beats 3-4) | bars 24, 32, 40 ... 88, 104, 112 | 8 bars | a phrase-end accent |
| **kick gap** on beat 4 + turnaround | bars 24, 40, 56, 72 | 16 bars | a free micro-accent for a camera move |

## 1.5 Frequency, and where the arrangement's levers are

- **Sub** (20-60 Hz) is effectively a note: flat, except a **+5 dB swell in the break** and a dip in
  the pull-back.
- **Low-mid and mid** step up at the lift (59.6 s); the mid keeps climbing as the lead comes forward.
- **High (>6 kHz) is the arrangement's main lever:** arrives at bar 41 (+4.3 dB), removed at 81
  (-4.6 dB), closed off in the break (-13 dB), floods in the riser (+26 dB over 7 s), settles at the
  drop.
- **Brightness** (spectral centroid): ~2.8 kHz at the open, ~2.6 kHz as bars 17-40 add body,
  ~2.9-3.0 kHz from bar 41, 0.67 kHz in the pull-back, ~1 kHz in the break, **~6 kHz at the riser
  crest**, ~2.96 kHz through the drop.
- **Stereo width** (side/mid) sits at 0.18-0.21 for the first 162 s, opens to 0.40 through the riser
  and **stays wide (~0.28) for the whole climax**. The climax is bigger in *space*, not density.

## 1.6 AV Gen's own sections, and why they are not used

The source project carried two structures: AV Gen's detected `songPlan` (10 sections, 129.98 BPM)
and a hand-edited `sectionTimeline` (43 two-bar sections). Neither is authoritative.

- The detector labels by min-max-rescaled RMS per section. On a master this evenly limited, the
  rescale amplifies noise: **the sub-heavy break becomes energy 1.0** ("Chorus 3") and the thinned
  suspension 0.0. The verse/chorus calls follow from those numbers, and the music has neither.
- It found no repetition (every section `group 0`, boundary confidence 0.02-0.08): the harmony is a
  static drone, so chroma recurrence has nothing to latch onto.
- **Five of its nine boundaries sit exactly one beat after a true downbeat.** Its tempo is right; its
  bar phase is not.
- Its 8 s minimum section and 16-beat kernel smooth over the two-bar pull-back and the four-bar break
  and riser -- the two events a director most needs.
- It misses the arrival at 74.33 s, the most visible change in the first half, and starts the drop a
  bar early (175.88 s, mid-roll).

The hand-edited timeline agrees wherever it marks a real change (30.04, 59.56, 89.10, 148.18,
162.95, 170.34, 177.73), and invents changes where there are none (111.25 "interlude", 181.42
"breakdown", 218.34 "outro" two bars before the music actually thins).

## 1.7 What this asks of the picture

1. **The first frame has to be alive.** The track opens at groove energy with no ramp.
2. **Cut on bars and phrases; the grid is perfect.** Phrases are 14.77 s, cycles 29.54 s.
3. **The 2-bar bass glide is a breath** for secondary motion.
4. **74.33 s is the first "world lights up" moment**, a smaller version of the drop. The shimmer that
   enters there and leaves at 148.17 s is a natural driver for sparkle.
5. **The break wants a submerged look** -- darker, lower, heavier. **The riser wants accelerating
   motion and opening light.**
6. **Express the drop through scale and width** more than faster cutting: the music gets wider, not
   busier.
7. **Do not drive anything from raw loudness.** Use the high band, brightness and the events.
8. **The ending is abrupt.** Land the final image by 222.0 s and cut to black on the last hit.
