# Beat clock pass 01: plan

Owner-approved scope (2026-10-26): fix the beat clock, cut the drum classifier, remove the Aurora Borealis Feline
Footwear version. ADRs 1780-1799. Media: `~/Desktop/av-gen-review/99-beat-clock/`.

The measured starting point is `docs/prototypes/audio-response-lab/03-final-report.md` §3.1.

## 1. What the literature does (from memory: no downloads)

- **Ellis 2007, dynamic programming.** Choose a tempo, then find the beat sequence that maximises onset strength at
  the beats minus a penalty `tightness x log(interval / period)^2`. One global tempo. This is our offline tracker today.
  librosa's `beat_track` is the same, with a log-Gaussian tempo prior (centre 120, sigma 1 octave).
- **Comb-filter / ACF tempo with metrical weighting** (Scheirer 1998; Davies and Plumbley 2007, the basis of BTrack).
  The tempo score of a period L sums the autocorrelation at L, 2L, 3L and 4L. The true beat collects the bar
  periodicity at its multiples; a syncopated sub-period (3 sixteenths) does not. A Rayleigh or log-Gaussian weighting
  near 120 BPM settles octaves. BTrack runs this causally with a cumulative score and a beat predictor.
- **Two-state models** (Davies and Plumbley): a general state finds the tempo from scratch, a context state keeps the
  current tempo unless the evidence moves. Hysteresis is what keeps a lock through a fill.
- **Tempo tracking through changes** (Klapuri 2006; madmom's DBN): tempo is a hidden state; a Viterbi path over
  windowed tempo observations with a transition cost on log-tempo change. Dixon's BeatRoot uses multiple agents with
  competing hypotheses; the DBN is the modern form.
- **madmom / aubio.** madmom's best trackers are neural (RNN activation + DBN): too big a dependency and not allowed.
  aubio's live tracker is a comb-filter ACF with a context/general switch, the BTrack family.

## 2. What we build

**One shared tempo scorer** (`analysis::scoreTempo`): the smoothed, mean-removed onset envelope; its unbiased
autocorrelation; for every candidate period L a metrical score = the mean of r(kL) for k = 1..4 (those that fit in
the window), times the log-Gaussian prior (centre 120 BPM, sigma 1 octave). The best candidate is then compared with
its related periods (x2, x1/2, x3/2, x2/3, x4/3, x3/4) on the same score, so a 4:3 or an octave error must beat the
right answer on the full metrical evidence, not on one ACF peak.

**File grid (offline):**
1. Local tempo: the scorer over 8 s windows every 1 s.
2. A Viterbi path over the windows' scores (state = tempo bin, a cost on log-tempo change): a tempo per section, which
   changes only when the evidence holds.
3. Ellis DP with that local period at each hop.
4. Each frame carries the local tempo (from the beats around it), so `beat.bpm` follows a tempo change.
5. Phase: investigate the dense fixture's F 0.0 first, then fix what it shows.

**Live tracker:**
- **Silence gate.** Below the noise floor (-60 dBFS RMS, held 0.15 s) it stops beats, reports tempo 0, and confidence
  decays to under 0.1 within about 0.4 s. The tempo history is frozen (silence is not written into it) and the phase
  runs on internally for up to 8 s. When sound returns, the first onsets are checked against the held phase: a fit
  resumes the lock with its confidence; a miss restarts acquisition.
- **Fast acquisition.** The first estimate after 2 s of history (four onsets), re-estimated every 0.25 s until locked,
  from all the history (up to 8 s). The metrical scorer and the related-period check guard octaves.
- **Hysteresis.** A locked period is kept while its score stays within 80% of the best; a different tempo must win two
  estimates in a row to replace it.

**Manual override:** the transport's existing tempo override gains an anchor: one hand-marked beat. BPM + anchor is the
file's beat clock (`beat.source` 4, MANUAL). Project format: `transport.tempo = {bpm, source: "user", anchor}`. UI: the
transport bar's tempo field, a "Mark beat" button that stamps the playhead as the anchor (right-click clears).
Precedence for `beat.*`: MIDI clock (1) > manual (4) > the beat feed's stem grid / the mix's grid (2) > live tracker (3).

**Gates:** the lab's beat checks become gates with bounds that the new trackers meet with margin, and the lab's e2e
test (which already fails on any failed gate) enforces them.

## 3. Checks before and after

- `tools/audio_lab/lab.py run` (file grid, causal replay) on every fixture, before and after.
- `loopback.sh` on syncopated and dynamic, after (BlackHole, if present).
- `tools/audio_lab/transfer.py` before and after (Event Horizon, Rainbow World; plus Sacred Flight, a beat-heavy live
  scene), and the differences in the beat family reported.
- The existing beat tests (click tracks, Rebuild's grid, embedded tempo, musical time) must still pass.
