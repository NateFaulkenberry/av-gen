# ADR-1785: the beat clock is gated by the Audio Response Lab

- Status: Accepted (eng/beat-clock)
- Code: `tools/audio_lab/lab.py` `checks()` (`BEAT_F_FILE`, `BEAT_F_LIVE`, `LOCK_GATE_S`, `SILENCE_FALL_S`).
- Tests: `tests/integration/test_audio_lab_e2e.cpp` (every gate must pass, in the CPU suite).

## Decision

The lab's beat findings (ADR-1724) are gates, on every fixture with an intended grid (dense, syncopated, dynamic,
isolation):

| Gate | Bound | Measured after this pass |
|---|---|---|
| file grid F-measure (+-70 ms) | >= 0.85 | 0.93-0.99 |
| file grid tempo per section | intended +- 3% | every section |
| live tracker F-measure (causal replay) | >= 0.70 | 0.80-0.95 |
| live tracker lock per tempo section | <= 8 s (a finding at <= 4 s) | 1.5-4.4 s |
| confidence under 0.1 after a silence starts | <= 0.8 s | 0.36-0.74 s |
| beats in a silence from 0.5 s in | <= 1 | 0 |
| lock back after a silence (music after it >= 6 s) | <= 4 s | 0-0.003 s |

The bounds sit below the measurements with margin and inside the owner's targets (lock in 4-8 s, confidence down in
about half a second). The BlackHole loopback (`loopback.sh`) is measured and reported, not gated: it needs the device.
