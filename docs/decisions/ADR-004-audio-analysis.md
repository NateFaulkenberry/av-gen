# ADR-004: Audio analysis

- Status: Accepted (2026-09-08)
- Research: `docs/research/audio-analysis.md` Parts B, §4-6

## Problem

Turn decoded PCM into a small vocabulary of normalised control signals (RMS, peak, frequency
bands, spectral descriptors, onsets, later beat/tempo) that is deterministic, testable with
synthetic signals, runs in real time, and can equally be evaluated against timeline position for
offline rendering.

## Alternatives considered

1. In-house STFT pipeline in C++ over a pluggable FFT (KissFFT, pffft, vDSP).
2. aubio (GPL-3, last release 2019).
3. Essentia (AGPL, heavy dependencies).
4. Gist (GPL-3).
5. JUCE DSP (AGPL/commercial).
6. KFR (GPL/commercial).
7. FFTW (GPL).

## Decision

**In-house analysis pipeline over an `IRealFFT` interface, with KissFFT as the first
implementation.**

- `analysis::RealFFT` wraps `kiss_fftr` (BSD-3). pffft (NEON/AVX) and Apple vDSP are later
  implementations of the same interface, selected by capability, with KissFFT remaining the
  golden reference for tests.
- `analysis::Analyzer` consumes mono float frames in hops (default window 2048, hop 512 at
  48 kHz, Hann window), producing an `AnalysisFrame` stamped with the PCM frame index of the
  window centre. Features in 0.1: RMS, peak, log-magnitude spectrum, five log-spaced bands
  (bass 20-150 Hz, lowMid 150-400, mid 400-2000, highMid 2000-6000, treble 6000-16000),
  spectral centroid, spectral flux, and onset strength via half-wave-rectified flux with an
  adaptive median threshold. Beat tracking and tempo are milestone 0.3.
- Every feature is float32, normalised to a documented range. Normalisation of band energies uses
  a running maximum with a time constant so live input auto-scales; offline mode can substitute a
  whole-file maximum.
- Smoothing is not applied inside the analyser. Raw features are published; attack/decay
  envelopes live in the signal-processing chain on the modulation side (ADR-011), so a single raw
  signal can drive several parameters with different responses.
- The analyser is a pure function of its input samples and its own state; given the same PCM and
  hop sequence it produces bit-identical output. Chunking (feeding samples in different block
  sizes) must not change results.
- Signals are exposed through `signals::SignalBus` under the `audio.` namespace.

## Rationale

- The algorithms needed for 0.1 are a few hundred lines; a GPL dependency for them is a poor
  trade, and Essentia/aubio are unmaintained or heavy.
- KissFFT is scalar and portable, giving cross-platform bit-identical golden tests. SIMD FFTs can
  be added for speed once tests exist to guard them.
- Frame-index stamping is what makes offline rendering reuse the same code path
  (`audio-analysis.md` §1.4).

## Consequences

- Analysis runs on a dedicated thread fed by the playback ring buffer; the render thread reads the
  latest `AnalysisFrame` through a lock-free triple buffer. Offline mode runs the analyser inline
  over the decoded buffer.
- Test suite uses synthetic signals: 440 Hz sine (peak bin), silence (all zero), white noise
  (flat spectrum, high flatness), impulse train (onsets at known frames), 120 BPM click (tempo,
  later).
- libebur128 (loudness) and pffft are documented optional additions, not linked in 0.1.

## Rejected alternatives

- aubio, Gist, Essentia, JUCE DSP, KFR, FFTW: licence (GPL/AGPL/commercial) or maintenance.
- pffft first: SIMD output may differ across platforms at the last bit, complicating golden
  tests; adopt after tests exist.
- Apple vDSP first: macOS-only.

## Revisit triggers

- Analysis time exceeding 10% of the hop period on the target machine (switch FFT backend).
- Beat tracking (0.3): Ellis 2007 DP tracker offline, causal tempogram live.
