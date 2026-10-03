# Research report 2: audiovisual modulation and audio analysis for live visuals

Sonic Garden VFX expansion, deliverable 2 (engineering agent, 2026-10-02). Reproducible techniques only. Where I give a
value from memory of a paper rather than from a checked source, I say so. Section 9 maps the findings onto what AV Gen
already has; the design that follows from them is in `../VFX-ARCHITECTURE.md`.

## 1. Onset and transient detection

**Front end.** Take an STFT with a Hann window, 1024-2048 samples per frame and a hop of 10 ms or less, then compress
the magnitudes: `Y(n,k) = log(1 + gamma |X(n,k)|)`. Log compression is the single biggest robustness win, because it
makes the flux level-independent. Use gamma = 1 on filterbank magnitudes (madmom) or 10-1000 on a raw STFT
([FMP, onset detection](https://www.audiolabs-erlangen.de/resources/MIR/FMP/C6/C6S1_OnsetDetection.html)).

**Onset detection functions (ODFs):**
- **Spectral flux** (half-wave rectified): `SF(n) = sum_k max(0, Y(n,k) - Y(n-1,k))`. It is the best general ODF for
  its cost.
- **SuperFlux** (Boeck & Widmer, DAFx 2013): before the difference, apply a 3-bin maximum filter along frequency to the
  reference frame. This suppresses vibrato and tremolo false onsets. madmom's defaults are 200 fps, 24 bands/octave
  from 30 Hz to 17 kHz, and `diff_max_bins=3`. Its peak picking uses threshold 1.1, pre_max 10 ms, post_max 50 ms,
  pre_avg 150 ms and combine 30 ms ([madmom SuperFlux](https://github.com/CPJKU/madmom/blob/main/bin/SuperFlux),
  [onsets.py](https://github.com/CPJKU/madmom/blob/main/madmom/features/onsets.py)).
- **Complex domain** (Bello, Duxbury): the distance of each bin from its phase-predicted value. It catches soft pitched
  onsets, at 2-3x the cost of flux. BTrack's default ODF is this one ([BTrack](https://github.com/adamstark/BTrack)).
- **HFC**: `sum_k k |X|^2`. Cheap and biased to noisy attacks: good for hats and snares, poor for kicks.
- **Log-energy derivative**: `max(0, log E(n) - log E(n-1))`. Per band, it is enough for drums.

**Adaptive threshold and peak picking.** Dixon ("Onset detection revisited", DAFx 2006,
[pdf](https://www.dafx.de/paper-archive/2006/papers/p_133.pdf)) normalises the ODF and requires three things of an
onset: a local maximum over +-w frames, a value above the local mean plus delta, and a value above a decaying
peak-follower `g(n) = max(f(n), a g(n-1) + (1-a) f(n))` (a about 0.9, from memory). The follower rejects bumps on the
tail of a large onset. A moving median, `lambda median(f[n-M..n]) + delta`, is more robust than a mean when onsets are
dense. librosa's defaults ([source](https://github.com/librosa/librosa/blob/main/librosa/onset.py)) are pre_max 30 ms,
post_max 0 (+1 frame), pre_avg and post_avg 100 ms, wait 30 ms and delta 0.07, on an ODF normalised to 0..1.

**Latency.** A centred peak picker needs post_max/post_avg frames of look-ahead. In online mode madmom sets every
`post_*` to 0. Boeck, Krebs & Schedl (ISMIR 2012, [pdf](https://www.cp.jku.at/research/papers/Boeck_etal_ISMIR_2012.pdf))
show that causal picking costs only a few F-measure points. The most useful anti-chatter rule is a **minimum
inter-onset interval**: 30-50 ms broadband, or 80-150 ms per drum class.

## 2. Kick, snare and hat from a mix, in real time

The cheap, reliable method is **per-band ODFs with per-class thresholds and refractory times**:

| class | band | ODF | refractory |
|---|---|---|---|
| kick | 40-120 Hz (sub 20-60 Hz as a gate) | log-energy derivative or band flux | 100-150 ms |
| snare | 150-250 Hz body and 2-8 kHz noise, both | band flux, plus flatness > 0.3 in 2-8 kHz | 80-120 ms |
| hat | 6-16 kHz | HFC or band flux, high flatness | 40-60 ms |

The alternative is to classify a broadband onset by centroid (kick < 200 Hz, snare 1-4 kHz, hat > 6 kHz), flatness and
the low/high energy ratio. The TouchDesigner `audioAnalysis` palette component is the cheap approach in production: low,
mid and high levels, centroid, and kick, snare and rhythm triggers, each with its own threshold
([docs](https://docs.derivative.ca/Palette:audioAnalysis)).

NMF drum transcription (fixed drum templates W, per-frame activations by multiplicative updates; real-time with
semi-adaptive bases in Dittmar & Gaertner, DAFx 2014) and CNN/RNN transcription (Wu et al., TASLP 2018) are more
accurate. They are heavy: 10-30 iterations a frame, templates primed to the kit, look-ahead for the networks. They are a
later upgrade, not a first step.

**Licences:** aubio and BTrack are GPL-3, Essentia AGPL-3, and madmom's models CC BY-NC-SA. Flux and peak picking are
trivial to reimplement from the papers, which avoids all of these. A bass note's attack in the kick band is the classic
confusion. Offline, AV Gen already separates them by harmonic/percussive separation with look-ahead (ADR-898). Live,
the causal cue is the ratio of the low band's flux to the flux of the bass's harmonics just above it.

## 3. Sustained against transient

- **HPSS by median filtering** (Fitzgerald, DAFx 2010): the harmonic part is the median of |X| along time (about 17
  frames), the percussive part the median along frequency (about 17 bins), with soft masks `H^2/(H^2+P^2)`. Made causal
  (past frames only), the harmonic part lags by L hops, which a "sustain" signal can afford. `P/(H+P)` per frame is a
  "percussiveness" scalar that needs no resynthesis.
- **Envelope follower** with separate attack and release:
  `y += c (x - y)`, with `c = 1 - exp(-dt/tau)` and tau the attack time when x > y, the release time otherwise.
- **The transient-designer principle** (SPL Transient Designer): two followers on one signal, fast (attack about 1 ms,
  release 20-50 ms) and slow (attack 20-50 ms, release 200-500 ms). `transient = max(0, fast - slow)`, and sustain is
  the slow follower. In dB this is level-independent: a cheap real-time alternative to HPSS for the transient/sustain
  pair.
- **RMS against peak:** RMS over 20-50 ms reads as "level", the peak or fast follower as "hit". Work in dB; a power
  law of about 0.6 on amplitude maps to visuals better than linear amplitude.

## 4. Beat and tempo

BTrack (causal, C++, GPL-3) uses a complex-domain ODF, a cumulative score with a log-Gaussian transition window, and an
autocorrelation tempo through a comb filterbank, and predicts the next beat. For visuals, the recommended practice is a
**phase-locked oscillator**: phase advancing at BPM/60, nudged toward each detected beat by a gain of about 0.1-0.3, the
period corrected by the phase error, and the pulse a shaped function of phase. It free-runs when the tempo confidence is
low. Visuals do not fire on raw beat events.

## 5. How VJ tools expose audio

**Synesthesia** ([SSF audio uniforms](https://app.synesthesia.live/docs/ssf/audio_uniforms.html)) sorts every signal
into kinds, and that is the most transferable idea in this survey:
- **level**: the loudness now: `syn_Level`, `syn_BassLevel`, `syn_MidLevel`, `syn_MidHighLevel`, `syn_HighLevel`;
- **hits**: the change now, per band: `syn_Hits`, `syn_BassHits`, and the others;
- **time**: level integrated over time, to drive phase and speed without jitter: `syn_Time`, `syn_BassTime`, and the
  others, plus `syn_CurvedTime`, which accelerates on rises;
- **presence**: whether a band is active at all: `syn_BassPresence`, and the others;
- **beat**: `syn_OnBeat` (1 on the beat, then decays), `syn_ToggleOnBeat`, `syn_RandomOnBeat`, `syn_BeatTime`;
- **BPM**: `syn_BPM`, `syn_BPMConfidence`, `syn_BPMSin`/`Tri` at 1, 2, 4 and 8 beats;
- **macro**: `syn_FadeInOut` (a slow rise when music starts and a slow fall when it ends) and `syn_Intensity` (a slow
  accumulation with the song's intensity).

The other tools:
- **MilkDrop:** `bass`, `mid` and `treb` are ratios of instantaneous band energy to its long-term average, so 1 is
  normal; `*_att` are damped versions
  ([authoring guide](https://www.pawelporwisz.pl/winamp/Help/Windows/winamp_milkdrop_preset_authoring_en.php)). Dividing
  by a running mean is built-in automatic gain.
- **Resolume:** an FFT grouped into Low, Mid and High. Each parameter's audio source has Gain and Fall (decay), plus an
  optional Envelope curve ([summary](https://vjacademy.info/resolume-audio-reactivity)).
- **VDMX:** input gain, then filters with centre, range, gain and smoothing, then "Num FX" chains (smoothing,
  threshold, curve) ([tutorials](https://vdmx.vidvox.net/tutorials/more-fun-audio-analysis-techniques)).
- **TouchDesigner:** the `audioAnalysis` component's levels each have Threshold, Smooth, Gain and Add. The Lag CHOP has
  separate lag up and lag down (attack and release). The Trigger CHOP turns a threshold crossing into an ADSR envelope.
- **Unreal and Notch:** Unreal's Audio Synesthesia has Loudness, ConstantQ and Onset analysers. Unreal, Notch and Magic
  Music Visuals all follow the same pattern: band level, then gain, then envelope, then curve.

**What performers actually touch:** gain or sensitivity (constantly, as rooms and sources change), and decay/fall/release
(the "feel" knob). They touch the threshold occasionally, and attack is usually left fast. The tools avoid an
audio-engineering UI in three ways:
- named bands and events instead of Hz;
- signals pre-shaped into kinds (level, hit, time) so nobody builds envelopes by hand;
- normalisation by default.

## 6. Gain and normalisation

The recommended chain, per band:
1. **noise-floor gate**: a slowly rising, quickly falling floor (or a 10th percentile over 5-10 s); zero below the
   floor plus 3-6 dB;
2. **normalisation**: `(x - floor)/(ref - floor)`, where ref is a slow peak (instant attack, 3-10 s release) or a
   running 95th percentile, clamped from below so a quiet passage is not boosted into noise;
3. **sensitivity**: applied after normalisation, so one knob means the same at any input level;
4. **response curve**: a power curve, `x^p` with p 0.5-2, or a soft knee;
5. **attack and release**: an asymmetric one-pole. Release is the expressive control.

There is a trade-off between the two kinds of range:
- A **fixed** (dB-calibrated) range keeps the music's dynamics, so a quiet verse looks quiet, but it breaks when the
  source level changes.
- An **adaptive** range is robust, but it flattens dynamics and pumps after loud passages.

The compromise is short-window normalisation for hits (an onset is judged against its own recent median, which is level
free by construction) and a fixed or long-tau range for levels, plus a slow `intensity` signal that keeps the macro
dynamics.

## 7. MIDI as distinct signals

- **Per event:** note-on and note-off (trigger and gate); velocity, which is a hit strength rather than a level and is
  mapped through a curve; pitch; and duration, which is known only at note-off, so live it is "held so far".
- **Windowed descriptors** (1-4 s, or 1-2 bars):
  - density (onsets per second) and polyphony (notes held);
  - pitch range and centroid;
  - intervals (successive differences, giving contour and leap size);
  - velocity mean and spread;
  - inter-onset regularity (the coefficient of variation of the IOIs: low is metric, high is free).
- **MPE** adds per-note pitch bend, pressure and timbre (CC74). This is a per-voice continuous source: give each voice
  its own visual object.
- **Precedent:** Stephen Malinowski's Music Animation Machine ([musanim.com](https://www.musanim.com)) is the
  piano-roll convention: height is pitch, length is duration, colour is voice or pitch class, and a note brightens
  while it sounds.

## 8. Mapping design principles

- **Topologies** (Hunt & Wanderley, *Organised Sound* 7(2), 2002). One-to-one mappings are learnable but shallow.
  One-to-many (kick to scale, bloom and shake) and many-to-one (brightness from level, centroid and intensity) mappings
  were more engaging once learned.
- **Timescale separation:**
  - fast (10-100 ms) signals drive impulses: flashes, spawns, shakes;
  - medium (0.1-2 s) signals drive amplitudes: size, glow;
  - slow (5-60 s) signals drive state: palette, density, camera regime.
  
  One parameter takes two timescales only as a base plus a pulse.
- **Modulation of modulation:** an envelope scales an LFO's depth or rate, as in modular synthesis. Integrating level
  into a phase (Synesthesia's `*Time`) is the special case that gives jitter-free speed modulation.
- **Cross-modal correspondences** (Spence 2011, *Atten. Percept. Psychophys.* 73:971-995). The robust pairings are:
  - pitch with elevation (high is up);
  - pitch with size (high is small);
  - pitch with lightness (high is bright);
  - loudness with size and brightness;
  - noisy or sharp timbre with angular shapes and high spatial frequency.
  
  A mapping that follows them reads as "right" without explanation.

## 9. What AV Gen already has, and the gap

Verified in the code (2026-10-02):
- **Analyzer** (`src/analysis/analyzer.*`, ADR-004): a 2048 window and 512 hop at 48 kHz (10.7 ms). It computes rms,
  peak, 8 bands, centroid, half-wave flux, a median-threshold broadband `onset` with a 60 ms minimum interval, and the
  ADR-897 band levels, relative flux and energy composite. It runs live and offline.
- **Band onsets** (ADR-898, `src/analysis/band_onsets.*`): kick (100-300 Hz plus HPSS percussive share), snare/clap
  (2-6 kHz) and hat (6-16 kHz). They are **offline only**: they use look-ahead (centred medians, +-1 s relative
  strength), and the live path never runs them. A file project and the live instrument therefore cannot share them.
- **Sonic** (ADR-1020/1025): `sonic.transient` is one broadband event, a rise of the level over a 150 ms follower,
  18 dB for full strength, on both paths. Everything else in `sonic.*` is medium or slow tier. `notes.*` has 18 musical
  descriptors and three events.
- **Route chains**: smoothing, envelopes with attack and release, threshold gates, springs and integrators (ADR-1041).
  Integration covers Synesthesia's "time" signals.

The gap the brief names (§9-11) is therefore concrete:
1. There is no causal per-band onset that is the same live and offline.
2. There is no level-free broadband onset strength (`sonic.transient` is a level rise, which fires on a swell and misses
   a hit in a dense mix).
3. There is no sustain/transient split.
4. There are no pre-shaped kinds (level, hit, presence) on a shared sensitivity model.
5. MIDI has no per-note signals: the pitch and velocity of the latest note, the interval, the velocity spread, how long
   the longest note has been held, and the note-off with its duration.

## Recommendations

1. **One causal detector in the Sonic step, shared by file and live**, on the existing 2048/512 analyzer frames. I do
   not change the analyzer, because live/file equivalence (ADR-1025) and everything downstream are tuned to it.
   - It reads log-compressed band flux (SuperFlux-style max-filtered reference) in the bands kick 40-120 Hz, bass
     harmonics 120-400 Hz, snare body 150-300 Hz, snare noise 2-8 kHz and hat 6-16 kHz.
   - Each class has a causal median threshold with a delta and a minimum interval (kick 110 ms, snare 90 ms, hat 45 ms,
     broadband 40 ms).
   - Firing on the threshold crossing (no post-max) puts the detection on the attack frame.
2. **Signals in kinds** (Synesthesia's lesson):
   - hits: `onset`, `kick`, `snare`, `hat`, `low` (any low attack: a kick or a bass note);
   - shaped hit envelopes;
   - levels: `bass`, `level`, `sustain`, `transient` (fast minus slow, in dB), `flux`;
   - presence/macro: `intensity`, `hatRate`, `melodic`.
3. **One conditioning chain**: floor gate, then fixed-range normalisation in dB (with the hits normalised against their
   own median, level-free), then sensitivity, then curve, then attack/release. Sensitivity moves the floor and the
   curve, not a multiplier on the output.
4. **Five live controls**: Sensitivity, Transient, Sustain, Attack and Release. The last two are multipliers, and Release
   is the feel knob. Band edges, refractory times and floors go in the project's `sonic.response` block, not on the
   panel.
5. **MIDI**: add per-note signals (the latest note's pitch and velocity, the interval, the velocity spread, the held
   time, the note-off with its duration) and hit envelopes for note-on, so one sustained note and one staccato note give
   different signals.
