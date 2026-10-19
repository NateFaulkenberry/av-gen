# Stems + multichannel: M0 baseline (before any capture or stem code)

Measured 2026-10-19 on base commit `0dbd0fc4` (main `99d45f13` plus docs only). Machine: MacBook Pro Mac14,6
(M2 Max, 38-core GPU), 64 GB, macOS 26.7.1. A 120 Hz display; nothing else connected.

## Method

- **Command:** `avgen --project examples/<scene>.json --input "BlackHole 2ch" --live-profile --mode live
  --projection off --target-fps 30 --size 1920x1080 --measure 30 --json ...`
  - It ran under `tools/gpu-lock.sh`, and the GPU was shared with two other agents.
- **Input:** the existing live path, a 2-channel BlackHole capture. To feed it, Feline Footwear loops into
  BlackHole 2ch from 95 s, using a small miniaudio player. That player, `build/probes/bh_play`, is not committed.
  This is the existing stereo workflow with real music on the input. The analysis thread runs on real signal; it is
  not idle.
- **Scenes:** Cymatic World (the demanding one) and Sonic Abstract Neon Vector (a lighter live scene).
- **Runs:** two repeats, interleaved, with the order reversed on the second (ADR-1490 practice). 30 s are measured
  per run after the profiler's warm-up.
- **Summary table:** produced by `tools/stems_bench/summarize.py build/bench/m0/*.json`.

## Results

| Run | Scene | Level (end) | Frame p50 / p99 ms | CPU work p50 / p99 ms | GPU p50 / p95 / p99 ms | Misses / frames |
|---|---|---|---|---|---|---|
| r1 | Cymatic World | Ultra | 33.3 / 36.8 | 4.41 / 6.78 | 24.1 / 27.0 / 28.8 | 6 / 901 |
| r1 | Neon Vector | Ultra | 33.3 / 37.5 | 3.74 / 8.49 | 21.4 / 31.1 / 33.0 | 9 / 901 |
| r2 | Neon Vector | Ultra | 33.3 / 37.2 | 6.13 / 8.70 | 15.0 / 30.2 / 32.0 | 5 / 901 |
| r2 | Cymatic World | Ultra | 33.3 / 36.6 | 2.44 / 6.43 | 25.0 / 30.2 / 32.0 | 2 / 901 |

**Spread between repeats** (the noise any later comparison has to beat):
- misses: 2-9 of 901 frames;
- CPU-work p50: up to 2.4 ms apart;
- GPU p50: up to 6 ms apart on Neon Vector.

Both scenes hold 30 fps at Ultra on this input.

## Gaps in what was measured

- **The live runner's per-hop cost.** The live profile does not time the live analysis thread; its "audio analysis"
  row covers only the offline catch-up. The per-hop cost of that thread comes from the microbenchmark instead
  (report §9.1: 94-114 µs per hop for the full chain). M8 adds a measured channel-analysis row.
- **Records:** the per-run JSON and text files are in `build/bench/m0/`, which is not committed.
