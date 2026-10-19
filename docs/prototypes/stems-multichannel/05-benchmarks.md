# Stems + multichannel: benchmarks (M8)

Measured on 2026-10-19 on the M2 Max (Mac14,6, 38-core GPU, 64 GB, macOS 26.7.1, a 120 Hz display) with build
`20cc8af1` (the M8 telemetry commit; the capture and analysis code is identical to the branch head). The GPU was shared
with two other agents. Every GPU job ran under `tools/gpu-lock.sh`, in eight holds of at most about 6 minutes each,
with 90 s between holds.

## Method

- **Path:** `avgen --project examples/<scene>.json <input> --live-profile --mode live --target-fps 30 --size 1920x1080
  --measure 30`. This is the editor's own loop at 1080p30, with present, Fifo and the frame cap. `--projection off`
  is used except in A6.
- **Scenes:** Cymatic World (the demanding one: a 47-op material program) and Sonic Abstract Neon Vector (lighter).
- **Arms** (the brief's §9 ladder):

  | Arm | Input | What it exercises |
  |---|---|---|
  | A1 | BlackHole 2ch, no sources | the existing stereo path, unchanged (the pre-change baseline is M0, `04-baseline.md`) |
  | A1b | BlackHole 2ch, `--input-sources 2h1` | the real Core Audio path: 2 sources, hits on 1 |
  | A2 | `synthetic:4`, `--input-sources 4` | 4 channels, levels tier |
  | A3 | `synthetic:8`, `--input-sources 8` | 8 channels, levels tier |
  | A4 | `synthetic:8`, `--input-sources 8h` | 8 channels with the hits tier on all |
  | A5 | `synthetic:8`, `--input-sources 8h2` | 8 channels, hits on 2 (selective) |
  | A6 | A4 + MIDI (`avgen_sonic_probe resonator`, about 1,480 messages per 30 s) + the projection window | the most demanding combination available |

- **The tiers:** the five bands and the centroid are computed by every source; the FFT is shared, so they cost nothing
  extra. "Spectral analysis on every channel" (brief §9 arm 4) therefore corresponds here to the **hits tier** on every
  channel. That is the expensive stage, the ADR-1060 classifier (report §9.1).
- **Audio:** Feline Footwear loops into BlackHole 2ch from 95 s for A1/A1b, through a small player that is not
  committed. The synthetic generator puts a tone and a click every 0.5 s on each channel (staggered by 31 ms), driven
  by its own thread in real time.
- **Repeats:** two per arm, interleaved, with the order reversed on the second repeat.
- **Live-source columns:** from the run's last `live input sources:` log line (ADR-1521's 10-second report).
- **Max RSS:** from `/usr/bin/time -l`.
- **Summary:** `tools/stems_bench/matrix.py build/bench/m8` makes the table.

## Results

| scene | arm | rep | level | frame p50/p99 | cpu work p50/p99 | gpu p50/p95/p99 | misses | max RSS MB | callback mean/max us | drops/gaps/ovl | analysis us/src-hop | core % | onset latency p50/p99 ms |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| cymatic-world | A1 | 1 | Ultra | 33.3/37.4 | 4.48/8.09 | 24.1/25.4/27.7 | 8/901 | 578 | - | - | - | - | - |
| cymatic-world | A1 | 2 | Ultra | 33.3/37.0 | 4.35/6.36 | 24.2/25.2/30.1 | 7/901 | 613 | - | - | - | - | - |
| cymatic-world | A1b | 1 | Ultra | 33.3/37.0 | 4.48/7.21 | 24.2/25.0/26.5 | 6/900 | 620 | 1.87/10.8 | 0/0/0 | 111.1 | 2.01 | 6.4/11.8 (413 ev) |
| cymatic-world | A1b | 2 | Ultra | 33.3/37.3 | 4.57/7.85 | 24.3/25.1/25.6 | 7/901 | 583 | 1.76/9.8 | 0/0/0 | 117.8 | 2.17 | 5.8/11.7 (486 ev) |
| cymatic-world | A2 | 1 | Ultra | 33.3/37.3 | 2.59/7.05 | 24.2/29.2/30.6 | 9/901 | 607 | 3.19/242.0 | 0/0/0 | 20.7 | 1.01 | 6.1/11.7 (242 ev) |
| cymatic-world | A2 | 2 | Ultra | 33.3/35.8 | 2.13/4.78 | 24.2/25.2/28.9 | 0/900 | 638 | 6.79/4889.4 | 0/0/0 | 48.2 | 2.06 | 6.3/14.5 (241 ev) |
| cymatic-world | A3 | 1 | Ultra | 33.3/36.5 | 2.34/6.80 | 25.2/29.0/30.7 | 3/901 | 575 | 5.45/4486.0 | 0/0/0 | 22.5 | 1.98 | 6.0/11.8 (481 ev) |
| cymatic-world | A3 | 2 | Ultra | 33.3/36.1 | 2.25/4.99 | 24.2/26.2/28.1 | 1/900 | 617 | 10.20/2292.3 | 0/0/0 | 51.4 | 3.46 | 6.6/12.0 (481 ev) |
| cymatic-world | A4 | 1 | Ultra | 33.3/36.8 | 3.72/7.05 | 25.9/27.9/29.0 | 5/901 | 563 | 9.88/3370.4 | 0/0/0 | 154.8 | 12.06 | 6.4/12.8 (482 ev) |
| cymatic-world | A4 | 2 | Ultra | 33.3/35.6 | 2.22/4.68 | 24.6/25.7/27.4 | 0/900 | 628 | 11.26/8064.2 | 0/0/0 | 154.9 | 10.83 | 5.9/12.3 (482 ev) |
| cymatic-world | A5 | 1 | High | 33.3/36.1 | 2.23/4.82 | 21.2/31.0/31.5 | 2/900 | 606 | 10.36/2207.5 | 0/0/0 | 66.9 | 5.74 | 6.3/12.8 (482 ev) |
| cymatic-world | A5 | 2 | Ultra | 33.3/35.6 | 2.23/4.70 | 24.8/29.6/30.6 | 0/901 | 636 | 9.71/1850.5 | 0/0/0 | 69.7 | 5.84 | 6.3/13.4 (483 ev) |
| cymatic-world | A6 | 1 | Ultra | 33.3/36.8 | 2.27/5.57 | 24.5/28.2/29.4 | 4/900 | 608 | 10.90/3448.6 | 0/0/0 | 139.4 | 11.16 | 6.3/12.8 (480 ev) |
| cymatic-world | A6 | 2 | High | 33.3/36.3 | 2.27/5.16 | 21.4/30.0/31.5 | 3/900 | 619 | 11.49/2920.2 | 0/0/0 | 139.6 | 11.02 | 6.5/15.7 (483 ev) |
| neon-vector | A1 | 1 | Ultra | 33.3/36.2 | 2.93/5.87 | 15.6/30.1/31.1 | 4/900 | 525 | - | - | - | - | - |
| neon-vector | A1 | 2 | Ultra | 33.3/37.3 | 6.20/10.00 | 15.1/30.2/31.7 | 9/901 | 505 | - | - | - | - | - |
| neon-vector | A1b | 1 | Ultra | 33.3/36.3 | 3.15/6.12 | 15.3/30.1/30.9 | 2/900 | 533 | 1.62/4.3 | 0/0/0 | 127.1 | 2.35 | 6.5/13.2 (393 ev) |
| neon-vector | A1b | 2 | Ultra | 33.3/37.4 | 4.32/8.72 | 21.4/30.6/31.5 | 8/901 | 523 | 1.51/13.0 | 0/0/0 | 86.5 | 1.67 | 6.2/11.7 (482 ev) |
| neon-vector | A2 | 1 | Ultra | 33.3/36.2 | 3.28/6.10 | 15.1/30.5/31.6 | 1/900 | 525 | 6.20/3641.6 | 0/0/0 | 43.5 | 1.86 | 5.9/11.7 (242 ev) |
| neon-vector | A2 | 2 | Ultra | 33.3/37.2 | 3.28/7.36 | 15.1/29.6/30.3 | 7/901 | 522 | 6.10/1551.4 | 0/0/0 | 41.9 | 1.77 | 6.5/11.7 (242 ev) |
| neon-vector | A3 | 1 | Ultra | 33.3/37.2 | 3.90/8.22 | 15.3/30.7/31.3 | 9/901 | 545 | 7.39/1232.8 | 0/0/0 | 42.8 | 2.75 | 5.9/11.8 (481 ev) |
| neon-vector | A3 | 2 | Ultra | 33.3/36.5 | 3.40/6.09 | 15.1/30.8/31.9 | 3/900 | 530 | 10.29/2333.4 | 0/0/0 | 40.4 | 3.22 | 6.6/13.9 (482 ev) |
| neon-vector | A4 | 1 | Ultra | 33.3/36.2 | 3.40/6.11 | 15.1/29.8/31.1 | 1/900 | 531 | 10.29/4080.1 | 0/0/0 | 130.6 | 10.41 | 7.0/12.7 (481 ev) |
| neon-vector | A4 | 2 | Ultra | 33.3/36.2 | 3.40/6.27 | 15.5/30.3/31.5 | 3/900 | 541 | 10.74/2584.3 | 0/0/0 | 132.0 | 10.91 | 6.1/12.5 (482 ev) |
| neon-vector | A5 | 1 | Ultra | 33.3/36.1 | 3.41/6.20 | 15.8/30.4/31.9 | 2/900 | 506 | 11.27/3097.5 | 0/0/0 | 75.8 | 5.45 | 5.9/12.4 (481 ev) |
| neon-vector | A5 | 2 | Ultra | 33.3/36.7 | 3.42/7.02 | 15.3/30.1/31.1 | 7/900 | 540 | 9.85/3483.4 | 0/0/0 | 63.8 | 5.21 | 6.9/13.8 (481 ev) |
| neon-vector | A6 | 1 | Ultra | 33.3/36.4 | 3.42/6.49 | 15.5/30.4/31.9 | 2/900 | 503 | 9.30/2278.5 | 0/0/0 | 138.6 | 10.78 | 6.6/13.6 (480 ev) |
| neon-vector | A6 | 2 | Ultra | 33.3/36.4 | 3.40/6.36 | 15.1/30.5/31.3 | 3/900 | 500 | 10.73/2817.5 | 0/0/0 | 135.1 | 12.08 | 6.6/12.3 (482 ev) |

## Reading it

- **Frame pacing does not regress.**
  - Every run holds a frame p50 of 33.3 ms at Ultra. Two runs were at High: Cymatic World A5 r1 and A6 r2.
  - Misses run 0-9 of 900 across all 28 runs, inside M0's run-to-run spread on the same input (2-9).
  - The CPU-work p50 moves within the spread M0 measured (up to 2.4 ms between repeats).
  - No arm is worse than A1 beyond that noise. Nothing here points at a regression, so no optimisation was made.
- **The analysis thread's cost scales as measured:**
  - Levels tier: 20-51 µs per source per hop; 1-3.5% of one core at 4-8 sources.
  - Hits tier on all eight: 130-155 µs per source per hop; 10.4-12.1% of one core.
  - Hits on two: 64-76 µs per source-hop on average, about 5.5% of a core.
- **No drops, gaps or overloads** in any run: 0 dropped blocks, 0 sample-time gaps, 0 overloads.
- **The real Core Audio callback (A1b, BlackHole 2ch):** 1.5-1.9 µs mean and 4-13 µs max, against a 2,667 µs period.
- **The synthetic arms' callback max (1-8 ms) is not a callback cost.** The synthetic generator is an ordinary
  (non-real-time) thread that the OS preempts. Its means (3-11 µs) agree with the planar microbenchmark (2.8 µs for 8
  sources, 8.8 µs for 24).
- **Onset latency, measured:** p50 5.8-7.0 ms and p99 11.7-15.7 ms in every arm. It runs from the newest sample of the
  analysed window to the render frame that put the onset on the bus, so it covers the ring, the analysis and the wait
  for a 30 fps frame.
  - It does **not** include the window itself: an attack is seen in the first hop whose window holds it (up to one hop,
    10.7 ms).
  - It does not include the device's own input latency (`kAudioDevicePropertyLatency`, 0 frames on BlackHole).
- **Memory:** max RSS 500-640 MB in every arm, the same as A1. Eight sources add 8 rings of 64 KB and 8 analyzers.

## Offline stems (M3)

Measured headless, `avgen --headless --frames 2`, with Feline Footwear (3.5 min) as the mix and the three permitted
tracks as stand-in stems. These files are not stems musically; the run measures cost only.

| Stems | Analysis (wall) | Peak RSS: PCM held (before `fee91422`) | Peak RSS: analysed while decoding (after) |
|---|---|---|---|
| 0 | -- | 909 MB | 861-914 MB |
| 3 | 0.46-0.70 s | 1,292-1,364 MB | 885-896 MB |
| 8 (2 with hits) | 2.06-3.59 s | 2,119-2,160 MB | 879-900 MB |

Stem analysis no longer raises peak memory. Each stem holds about 0.4 MB per minute of compact frames.

## What was not measured

- **Real multichannel hardware.** The Volt 876 is not connected yet (`07-hardware-test.md`). BlackHole 16ch is not
  installed: it needs the owner's password.
- **Rates other than 48 kHz.** The synthetic generator and BlackHole ran at 48 kHz only.
- **The callback's worst case.** Only the mean and max are measured, not a histogram.
