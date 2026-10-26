# ADR-1725: The live analysis path can be replayed from a file, and played through the loopback

- Status: Accepted (eng/audio-response-lab)
- Code:
  - `tools/audio_lab_replay.cpp` (`avgen_audio_lab_replay`);
  - `tools/sonic_live_probe.cpp`: the `file` scenario;
  - `tools/audio_lab/loopback.sh`, and `tools/audio_lab/lab.py loopback` / `live_metrics`;
  - `src/app/application.cpp`: `--sonic-live-log` writes `audio.*` whole.
- Tests: `tests/integration/test_audio_lab_e2e.cpp`, whose gates include the live path's recall and timing whenever
  the replay binary is built.

## Problem

The offline path (`--sonic-trace`) reads the whole-file analysis. Its beat is the offline DP grid, not the live
`BeatTracker`, and its frames are centre-stamped up to half a window before their samples are captured. So the lab
could not say how the live tracker locks, or when a live onset reaches the bus. ADR-1605's loopback rig measures
latency, but only on its own synthetic transients.

## Decision

**A deterministic replay of the live analysis.**
- `avgen_audio_lab_replay` pushes the file's mono samples a hop at a time into the same `Analyzer`, `BeatTracker` and
  `CausalOnsetDetector`, in the runner's order.
- A frame exists when its last sample arrives. Render frame n at n/30 s takes every frame that exists by then; its
  events are the OR of theirs (the runner's carry), and its values the newest frame's.
- There is no device and no thread.

**The live device through the loopback.**
- The probe's `file` scenario plays a fixture into BlackHole as one long shot, so its first sample is stamped on the
  host clock.
- `loopback.sh` runs AV Gen live (1080p30, under the GPU lock) and `lab.py loopback` joins the two logs.

## Consequences

**Measured, causal replay against the live loopback (both lock times are after the start of the tempo segment):**

| Fixture | Replay | Loopback |
|---|---|---|
| syncopated | F 0.19, never locks | F 0.15, never locks |
| dynamic, 120 BPM | locks at 22 s | locks at 25.5 s |
| dynamic, after the step to 96 BPM | relocks in 3.7 s | relocks in 3.75 s |

The replay is a fair, deterministic stand-in for the live analysis. It does not cover the live Sonic response model, the
device or the scheduling.

**Live onset latency (loopback, battery power, M2 Max, BlackHole):**
- sound to bus: 32-53 ms median, 55-59 ms p95;
- sound to the present call: 36-56 ms median, 58-62 ms p95.

**Real-device runs are opt-in** (ADR-1550) and never part of a suite.
