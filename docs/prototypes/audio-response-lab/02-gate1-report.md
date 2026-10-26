# Audio Response Lab 02: Gate 1 report

**Gate 1 asked for:**
- the scene running on real production signals;
- the generator and two or three fixture categories end to end;
- one synchronised diagnostic clip;
- the first metrics report.

Everything here was measured on eng/audio-response-lab, on an Apple M2 Max on AC power. The plan and the threshold
reasoning are in `01-plan.md`.

**Media:** `~/Desktop/av-gen-review/96-audio-response-lab/01-gate1/`.

| File | What it is |
|---|---|
| `isolation-synchronised-diagnostic.mp4` | 24 s: the lab scene rendered at 1080p30 from the isolation fixture (four stems), with the timeline strip below it and the mix as the soundtrack |
| `syncopated-synchronised-diagnostic.mp4` | 32 s: the same on the syncopated fixture (mix only), which shows the beat clock failing |
| `isolation-still-12.5s.png`, `syncopated-still-21s.png` | One frame of each |
| `metrics-summary.md`, `metrics-results.json` | The first report: every check and every raw measurement (timing errors, missed, duplicate and false lists, per-source tables) |
| `isolation-pixel-check.json`, `syncopated-pixel-check.json` | Each indicator's rendered brightness, frame by frame, against its parameter final |
| `live-profile-1080p30.txt` | `--live-profile --mode live --target-fps 30` on the lab scene |

**How to read a clip:**
- The top 1080 rows are the scene.
- The strip below runs from 4 s of the past to 2 s ahead, with the red playhead on the frame shown.
- Row 1 is GROUND TRUTH, drawn only in the strip. Its colour is the event's register (orange low, yellow mid, cyan
  high, white broad); dim lines are ambiguous events; the blue underline is measured silence.
- The other rows are what the production pipeline published on each render frame: `response.onset`, `response.low`,
  then the beat (intended beats grey, `beat.pulse` pink), then each stem's onset and level, then the level, bass and
  onsetEnv curves, then the onset-flash parameter.

## What was run

`python3 tools/audio_lab/lab.py run --only sparse,syncopated,isolation`:
1. Generate the fixtures.
2. Write a lab project per fixture, holding only the audio file names.
3. Run `avgen --sonic-trace` twice: at the hop rate (93.75 fps) and at 30 fps with the finals.
4. Score.

`tools/audio_lab/compose.py <run>/<fixture> --render` renders through the GPU lock and builds the clip. Generation, the
traces and scoring take about 20 s for the three fixtures; each render and composition about 2.5 min.

**What the scene consumes:** route finals from the real bus. A test asserts that every meter and lamp equals its
declared function of its signal (`tests/integration/test_audio_response_lab.cpp`).

**What reaches the picture:** render frame N shows row N's finals:
- 37 indicators correlate 0.997-1.000 with their final at lag 0, and lag 0 beats ±1 frame for every one;
- the event lamps fall to about 0 at ±1 frame.

## Results: 109 of 111 checks pass. The two failures are real

| | sparse | syncopated | isolation |
|---|---|---|---|
| `response.onset` P / R (firm attacks) | 1.00 / 1.00 (13) | 1.00 / 0.99 (97) | 1.00 / 0.97 (63) |
| timing, hop clock, median / p95 | -10.7 / +0.3 ms | -11.3 / -6.4 ms | -11.3 / -6.7 ms |
| timing, 30 fps render frame, median / p95 | 0.0 / +13.3 ms | +3.1 / +21.2 ms | 0.0 / +16.7 ms |
| false per minute in silence; in held notes | 0; 0 | 0; n/a | 0; n/a |
| `audio.onset` (linear flux) P / R | **0.48** / 1.00 | 0.86 / 0.97 | **0.63** / 0.98 |
| `response.low` P / R against low-register attacks | 0.91 / 1.00 | 0.98 / 1.00 | 0.88 / 0.94 |
| `response.level` against the mix's RMS dB (Spearman, best lag) | 0.78 at 75 ms | 0.77 at 53 ms | 0.79 at 21 ms |
| beat clock F (±70 ms); tempo | n/a | **0.28; 144.6 BPM for 110** | 0.99; 120.0 BPM |
| onset flash (declared 120 ms fall) | 100-133 ms | 133 ms | 133 ms |

**Isolation, per source** (the stems analysed separately, which is the production stem path):

| Source | Role | Onset P / R | Timing median | Leaked onsets | Level in its own silence |
|---|---|---|---|---|---|
| drums | drums | 0.96 / 1.00 | -10.7 ms | 0 | 0.00 |
| bass | bass | 1.00 / 1.00 | -10.7 ms | 0 | 0.00 |
| synth | synth | 1.00 / 1.00 (its 300 ms swell excluded as ambiguous) | -12.0 ms | 0 | 0.00 |
| pad | other (silent) | no onsets, level 0, `active` 1 | | 0 | 0.00 |

**With the mix alone:**
- the source panel shows four red no-data lamps and empty meters;
- the scene does not substitute a mix signal for a missing source.

## Findings, most important first

1. **The beat clock fails on syncopated material, and it cannot say so.**
   - On the file path the grid locks to **144.6 BPM against an intended 110** (ratio 1.31, not an octave). It has
     F 0.28 and never locks; the median phase error is a quarter of a beat.
   - `audio.tempoConfidence` reads one value for the whole file: two distinct values in the trace, 0 and then 0.75.
     The file grid computes one tempo and one confidence for the whole file (`analysis_track.cpp` l. 246-248), so it
     cannot show acquisition, loss of lock or reacquisition. The dial looks confident while it is wrong.
   - The stem test fixture's plain 120 BPM groove is also read at **half time**: 20 `beat.pulse` in 20 s
     (`test_sonic_trace_stems.cpp`).
   - On the isolation fixture (a 120 BPM kick on every beat, then a backbeat) the grid is right: F 0.99.
   - **Not yet measured:** the live causal `BeatTracker`. The offline path never runs it. That is the next
     instrument (a causal replay).
2. **`--sonic-trace` never recorded an event (fixed, ADR-1721).**
   - Every event column (`response.onset`, `stem.*.onset`, `beat.pulse`, the drum classes) read 0 on every row,
     because `Engine::update` clears events before it returns.
   - ADR-1571's workaround traced events through `env.*` sources instead.
   - Any earlier analysis that read event columns from a trace read nothing.
3. **The drums classifier fires `hat` on every snare.**
   - Unstacked snares: snare 10/10 and **hat 10/10**.
   - A kick with a snare on top fires all three classes, 4/4.
   - Kicks alone are clean (13/13 kick, nothing else); hats alone are clean (27/27).
   - Latency after the attack, hop clock: kick +17 ms median, snare +28 ms, hat -13 ms (published from the centre).
   - Caveat: the fixture's snare is noise-heavy (55% white noise in a 50 ms decay). The audit's drum stem showed 0
     confusions on a different synthetic snare, so this needs a real drum stem before it is called a classifier
     defect.
4. **The mix-level onsets are drum-shaped.** `response.onset` missed 2 of 4 isolated bass notes in the bass-alone
   section, which `stem.bass.onset` caught 4 of 4. On the isolation mix, its hits land mostly on hats, snares and kicks.
   `response.low` lands on kicks, snares (a 190 Hz body is low-register) and bass notes alike. It is a low-register
   attack, not a kick, which is what ADR-1620 says. Measured, not a defect.
5. **`audio.onset` (the linear flux) over-fires:** precision 0.48 on sparse material, with duplicates and false
   triggers on held notes and bass. This confirms the audit (02 §2.3). `response.onset` is the one to author on.
6. **The MID and HIGH meters hold for seconds after the sound stops.**
   - They read `audio.midLevel` and `audio.trebleLevel`, power smoothed over 1 s on a 0 = -60 dB scale. They fall at
     4.3 dB/s, up to 13.8 s to read 0.
   - That is the documented behaviour (ADR-897), so no check fails. But the response vocabulary has no mix-level mid
     or high level with a short release, so a "silence returns the scene to baseline" probe cannot be built on one.
   - **Owner question:** keep the meters on these, or should a short-release mid/high level exist?
7. **The level followers lag the audio by design:** `response.level` against the mix's RMS dB has a Spearman of 0.77-0.79
   at 21-75 ms of lag. Once a sound stops they read 0 within 0.20 s (level) and 0.32 s (bass), inside their documented
   bounds of 0.60 s and 1.00 s.
8. **Timing conventions, measured.**
   - The file path is centre-stamped, so it publishes an onset about 11 ms **before** its sample (median, hop clock).
   - At 30 fps that becomes 0 to +21 ms (p95) after the onset.
   - Live adds the half window back (+21.3 ms), plus capture and the frame wait. ADR-1605 measured 73 ms (p50) and
     91 ms (p95) from sound to light; the fixtures will go through that rig after Gate 1.
9. **Perf: 1080p30 is met with margin, and text labels cost most of the GPU.**
   - Live mode, 15 s, 450 frames: frame p50 33.32 ms, p95 35.10, p99 38.0; **1 late frame in 450**.
   - GPU p50 11.7 ms; CPU work 8.8 ms (UI build 4.9 ms of it).
   - The first version cost 19.4 ms of GPU: shadows were 12.7 ms on an unlit board and AO 6.7 ms. The scene is now
     unlit and shadowless with AO off.
   - Of the remaining GPU time, about **7.5 ms is the 41 text labels**: 330k of the scene's 335k triangles. Glyphs are
     traced from an SDF bitmap with no tessellation control (`scene/text_mesh.*`). Without the labels the GPU is
     2.0 ms (headless A/B).
   - That is a production cost worth knowing for any scene with text; it is not fixed here.

## What was and was not verified by eye

**Seen (rendered stills and clips):**
- the board's layout and labels;
- the lamps at rest and lit;
- the meters' fill;
- the phase marker orbiting;
- the red no-data lamps on the mix-only run;
- the strip's alignment against the playhead.

**Measured, not eyeballed:**
- that a lamp's brightness is its final on the same frame (the pixel check).

**Not built and not seen:**
- an ImGui overlay for the lab. The existing Analysis, Live and Performance panels exist, but I cannot see ImGui and
  have not added to it. Brief §2G is open.
- the scene opened from File > Examples > Live in the GUI. It loads through the same project loader (tested), and the
  menu grouping is tested without ImGui.
- live input. The live profile played the fixture's file through the live loop (`Audio/MIDI: file`), not a capture
  device.

## Owner decisions wanted

1. **The MID and HIGH meters** (finding 6): keep `audio.*Level`, which decays slowly by design and is honest, or ask
   for a short-release mix mid/high level?
2. **The beat clock** (finding 1): the measurement will go on to the live tracker. Should a clock that cannot express
   lock be flagged on the dial now, for example by greying the CONF bar on the file path?
3. **Text in scenes** (finding 9): is a cheaper text mesh worth a separate ticket?
4. **Menu placement:** File > Examples > Live > **Diagnostics**, out of the live scene switcher (ADR-1722). Is that
   the right place?

## Next (after notes)

- **Fixtures:** score all seven categories. Dense, noisy, dynamic and adversarial already generate.
- **The live path:**
  - a causal replay (the runner, the `BeatTracker` and the carry, fed the file hop by hop);
  - the BlackHole loopback with fixtures played by the probe (ADR-1605's rig).
- **Cross-scene transfer:** Event Horizon and Rainbow World.
- **Tests:** Catch2 end to end per category, with the generator run by CMake as the stem fixture is.
- **Reports and docs:** the perf report and the documentation.
