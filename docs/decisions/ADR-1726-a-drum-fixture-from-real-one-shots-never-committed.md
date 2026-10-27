# ADR-1726: A drum fixture from real one-shots, never committed

- Superseded by ADR-1784 (2026-10-26): the drum classifier and this fixture are cut.
- Status: Accepted (eng/audio-response-lab; the owner allowed the ~/Samples packs for the drum-class tests,
  2026-10-26)
- Code:
  - `tools/audio_lab/make_drum_fixture.py`;
  - `assets/audio/drum-samples/.gitignore`, which ignores itself and everything else in the folder. The audio is
    copied there by hand from `~/Samples` and is never committed.
- Tests: `tests/integration/test_audio_lab_e2e.cpp` `[adr1726]`. It SKIPs where the samples are absent, saying so.

## Problem

The Gate 1 finding "the drums classifier fires hat on every snare" came from a synthetic snare that was 55% white noise.
The owner asked for it to be retested on real drums before it is called a classifier defect.

## Decision

**A fixture built from real one-shots:**
- **Acoustic:** Drum Drops Modern Funk: a 20" kick, Birch and Supraphonic snares, two closed hats.
- **Electronic:** Slate Digital trap: a kick, two snares, a closed hat.
- Each is trimmed to its first sample over 1% of its peak and placed at a known sample.

**Layout:**
- each one-shot alone, four times;
- a groove with no stacking;
- a groove with the snare stacked on the kick.

The manifest records each one-shot's `~/Samples` path and sha256, so it can be rebuilt. The repository is public: the
audio stays in the self-ignoring folder and the build tree, and is never committed, cached or uploaded.

## Consequences

**Measured, each one-shot alone; class events within -23 ms to +120 ms of the hit:**

| One-shot | kick | snare | hat |
|---|---|---|---|
| 20" kick (acoustic) | 4/4 | 1/4 | 0 |
| Slate kick | 4/4 | 0 | 0 |
| Birch snare | 0 | 4/4 | **4/4** |
| Supraphonic snare | 0 | 4/4 | 0 |
| Slate snares (two) | 0 | 8/8 | 0 |
| Prototype closed hat | 0 | **4/4** | 4/4 |
| Flat closed hat | 0 | **4/4** | 4/4 |
| Slate closed hat | 0 | **3/4** | 4/4 |

**The confusion is real, and its main direction is the reverse of the synthetic finding:**
- every real closed hat also fires the snare class (11 of 12);
- one of four real snares also fires hat.

**On the Feline drum stem**, there is no ground truth, so only co-firing was measured: a hat within one hop of 4 of 235
snares (31 within 30 ms).

**Median latency after the attack:** kick +23 ms, snare +29 ms, hat -6 ms (centre-stamped file path).

Reported, not fixed in this branch.
