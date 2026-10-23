# ADR-1605: Sound to light is measured on a loopback, and an impact misses its target on the picture side

- Status: Accepted (eng/living-prism-p1b, The Living Prism P1-3). Measurement; no production default changed.
- Measures 03 §11 T10 as the owner adopted it (03 §14 #1): impacts ≤ 45 ms p50 and ≤ 60 ms p95, live, at 30 fps.
- Code:
  - `tools/sonic_live_probe.cpp`: the `transients` scenario (80 one-shots, kick/snare/hat/pluck in turn, 0.6031 s
    apart so the hop and frame phases walk; each logged at its stream sample on the host clock). The probe now opens
    its output with ADR-1550's `courteousPeriodFrames`, as AV Gen's capture does;
  - `src/app/application.cpp` `--sonic-live-log`: also `stem.*`, `signal.*`, `env.*`, `beat.*`, `audio.onset*`;
    events read on the frame they fired (`Engine::setKeepFrameEvents`/`frameEvents`; an event is cleared at the end of
    its update, so the log never saw one: `response.onset` "never moved" in the first run); with `--trace-finals`,
    every route target's final; `gpuEndRaw`/`gpuCompleted` (the frame timeline's raw end, for the record);
  - `--input-sources <key>@<channel>[h],...`: named sources, so a loopback channel can be `stem.drums.*` without the
    saved channel map (no unit test: `parseArgs` is not in a test binary; the runs below exercise it);
  - `src/audio/capture_coreaudio.cpp`: logs the buffer the capture got ("128-frame buffer (asked 128)");
  - `tools/sound_to_light.sh` (the rig) and `tools/sound_to_light.py` (`prepare`, `analyse`).
- Evidence: `~/Desktop/av-gen-review/84-living-prism-p1b/P1-3/` (`rw`, `rw-r2`, `rw-pooled.*`, `rw-p128`, `eh`).

## Method

`tools/sound_to_light.sh examples/rainbow-world/rainbow-world.json <out>` (opt-in, a real device, ADR-1550; never a
suite). Under the GPU lock, AV Gen runs the project exactly as the live profile does (`--live-profile --mode live`:
the projection window at 1920x1080, 30 fps target, Auto ladder, Fifo on a 60 Hz display), with BlackHole 2ch as its
live input, channel 2 also analysed as the drum stem (`--input-sources drums@2h`). When the log shows 15 s of frames,
`avgen_sonic_probe transients` plays into BlackHole (both channels). The owner's CoreAudio configuration is untouched:
no aggregate device, BlackHole was idle before and after (512 frames), and the app's settings file is restored if a
run changed it (none did).

The run's twin (`sound_to_light.py prepare`, written beside the project and deleted afterwards) adds the impact paths
Rainbow World does not author: a `signal.*` (ADR-1602) on `response.onset` and on `stem.drums.onset` (attack 0), an
`env.*` on `response.onset`, and route envelopes (peak-hold on the route) from both onsets to two untouched post
parameters. Rainbow World's own `signal.clap`, `signal.glint`, its clap route's final and `signal.kick` are read too.

Per transient and path: the first frame within 200 ms whose value rose by 10% of the path's range; the same test 300 ms
before each shot (silence) counts false alarms. Times are from the sound to:
- **bus:** that frame's update (its bus built);
- **present:** its present call returning;
- **gpu:** its GPU finishing: present + the GPU span. The frame timeline's timestamps tick at the host rate (10 ppm)
  but from an epoch about 5.5 s away, so they cannot be joined absolutely; on a paced loop whose GPU span is under the
  period the queue is empty at submit, which makes present + span the GPU's end;
- **light:** gpu + the wait for the next vsync (Fifo): half a refresh on average (8.3 ms), one at most. Scanout to the
  picture's middle (about 8 ms more) is not included.

**The reference.** The probe stamps its first callback and counts samples from it. A playback callback renders one
period ahead of its output, so the stamp is early by one probe period, and the analysis subtracts it (10.7 ms at the
512 frames the probe got: Core Audio keeps a buffer size per process, and BlackHole read 512 in the probe's process
while AV Gen's capture held 128). Uncorrected numbers are 10.7 ms higher. BlackHole has no device latency; an
interface adds its own (3.1 ms on the Volt 876, 02 §6.1).

## Results (corrected; Rainbow World pooled over two runs, 160 transients; GPU p50 24-27 ms, Ultra 93% and High 7% of frames, 1 and 2 deadline misses in 2,700)

| path (Rainbow World live, 30 fps) | n | bus p50 / p95 | present p50 / p95 | GPU end p50 / p95 | **light p50 / p95** |
|---|---|---|---|---|---|
| `response.onset` (event) | 160 | 28.2 / 43.2 | 41.2 / 56.3 | 64.7 / 83.0 | **73.1 / 91.3** |
| `stem.drums.onset` (event) | 160 | 28.2 / 43.2 | 41.2 / 56.3 | 64.7 / 83.0 | **73.1 / 91.3** |
| `signal.*` on either onset; `env.*`; route envelope | 160 | same frame as its onset | | | **73.1 / 91.3** |
| `response.kick` / `stem.drums.kick` (kicks) | 40 | 58.4 / 73.0 | 71.2 / 86.2 | 94.8 / 113.6 | 103 / 122 |
| `response.snare` / `stem.drums.snare` (snares) | 40 | 71.9 / 84.8 | 85.3 / 98.0 | 110.2 / 121.0 | 119 / 129 |
| `response.hat` (hats) | 40 | 32.0 / 41.9 | 45.0 / 54.3 | 68.2 / 81.3 | 77 / 90 |
| Rainbow World `signal.clap` (snares; `stem.drums.highMid`) | 40 | 42.3 / 54.7 | 55.1 / 68.1 | 80.3 / 97.9 | 89 / 106 |
| Rainbow World `signal.glint` (hats; 9 of 40 missed) | 31 | 33.6 / 41.7 | 47.4 / 54.2 | 71.5 / 79.3 | 80 / 88 |

Per family on `response.onset` (light p50 / p95): kick 72 / 93, snare 70 / 91, hat 77 / 90, pluck 74 / 90. No onset
path had a false alarm; `audio.onset` (the plain causal flux) had 49 in 160 silent windows. `signal.kick` follows the
beat grid, which locked to the probe's 0.6 s spacing: its "latency" is not a reaction and is not reported.

Run to run, the two Rainbow World runs' bus p50 differ by 4 ms (uncorrected 36.3 and 40.5). A third run with the
probe opened first at 128 frames (`rw-p128`, so AV Gen joined a running device; built before the buffer was logged)
read bus 29.5 / 44.1, light 79.2 / 94.8: inside that spread.

Event Horizon (GPU p50 11 ms, `eh`): `response.onset` bus 24.8 / 40.0, present 30.1 / 48.0, **light 51.7 / 69.0**;
`stem.drums.onset` light 57.4 / 75.4.

**Against T10 (≤ 45 p50, ≤ 60 p95): not met.** Rainbow World misses by 28 / 31 ms, Event Horizon by 7 / 9 ms. The
audio half meets it: sound to present is 41 / 56 ms on Rainbow World and 30 / 48 on Event Horizon. Every onset path a
scene would author an impact on (`response.onset`, `stem.drums.onset`, a `signal.*` or an `env.*` on them, a route
envelope) reaches the bus on the onset's own frame: P0-2 and P0-3 removed the hops. Class hits stay 30-44 ms behind
the onset (02 §2.3) and are not for impacts.

## Where the time goes (Rainbow World, p50)

| component | ms | how known |
|---|---|---|
| input buffer | 0-2.7 (128 frames; 0-10.7 when another client holds BlackHole at 512 and AV Gen keeps it) | log, ADR-1550 |
| analysis: window, hop, decision | ≈ 11 | bus p50 minus the mean frame wait; the app's own "onset latency" 5.6 / 12.3 ms plus buffer and hop |
| frame quantisation: wait for the next 33.3 ms update | 0-33, mean 16.7 | the bus spread is a frame wide |
| update to present (CPU: engine, record, UI) | 12.8 (p95 16.7); Event Horizon ≈ 5 | measured |
| GPU span | 23.9 (p95 30.5); Event Horizon 11-13 | measured |
| vsync wait (Fifo, 60 Hz) | 0-16.7, mean 8.3 | Fifo |

**What dominates is the picture, not the audio:** the frame (quantisation, CPU, GPU and vsync) is about 62 of the 73
ms. A 30 fps loop whose GPU fills its budget (the ladder aims for 29.3 ms) cannot meet 45 ms: the frame wait alone
averages 16.7 ms and the GPU 24.

## Levers, smallest first (estimates; none applied)

| lever | saves (p50 / p95) | cost, risk |
|---|---|---|
| Just-in-time pacing: start the frame so its GPU ends just before a vsync instead of a fixed phase | ≈ 8 / 16 (the vsync wait) | S-M; needs the paced controller to aim at the refresh phase (ADR-1362) |
| Late latch: read the audio state (bus or impact uniforms) at the end of the CPU frame, not its start | ≈ 10 / 14 (most of update-to-present) | M-L; touches the update order |
| A latency budget for the GPU: a lower level when a project asks for tight impacts (Rainbow World Ultra 24 ms vs Medium about 18, 01 §4.2) | ≈ 6-12 | S (a ladder target); a look change, owner's call |
| 60 fps target (halves the frame wait) | ≈ 8 / 16 | needs GPU < 14.7 ms; not Rainbow World |
| Analysis window 2048 → 1024 for the onset detector only | ≈ 5 [est] | M; detector retuning, false-alarm risk |
| Input buffer: AV Gen at 128 frames (the default when BlackHole is idle) | 0 now; 4 / 8 when the owner's Multi-Output holds BlackHole at 512 | ADR-1550 trade-off; no change |

With the first three, Rainbow World's light p50 would be about 73 − 8 − 10 − 8 ≈ 47 ms [est]; Event Horizon's about 30.
None of these is changed here (production defaults untouched).

## Consequences

- T10 has its numbers; it is not met, and the gap is the frame pipeline. The owner decides whether the target moves
  or the levers above are scheduled (pacing and late latch first).
- Impacts should be authored on `response.onset` / `stem.<k>.onset` (directly, through a `signal.*`, an `env.*` or a
  route envelope): all land on the onset's frame. Class hits are 30-44 ms later.
- `--sonic-live-log` now logs events (it never could) and every path an impact can take; any later latency change can
  be measured with the same command. The live log is opt-in, and frame events are kept only while it is on.
