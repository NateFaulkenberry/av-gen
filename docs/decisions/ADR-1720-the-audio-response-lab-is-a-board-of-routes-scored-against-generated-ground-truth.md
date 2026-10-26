# ADR-1720: The Audio Response Lab is a board of routes, scored against generated ground truth

- Status: Accepted (eng/audio-response-lab; the owner's brief, docs/prototypes/audio-response-lab/00-brief.md; Gate 1
  reviewed 2026-10-26)
- Code:
  - `examples/audio-response-lab/` (the project and scene), written by `tools/audio_lab/make_lab_scene.py`;
  - `tools/audio_lab/make_fixtures.py` (the generator), `lab.py` (run and score), `compose.py` (clip and pixel check).
- Tests: `tests/integration/test_audio_lab_e2e.cpp` (one case per category; every gate);
  `tests/integration/test_audio_response_lab.cpp` (`[audiolab]`, `[adr1720]`):
  - every indicator in the layout is a route from its declared signal, and every non-optional route binds;
  - on the stem fixture, every meter and lamp equals its declared function of its signal on every frame;
  - with no stems, every source meter stays empty and every ACTIVE lamp hidden (the red no-data lamp shows).
- Docs: `docs/audio-response-lab.md` (how to run, add a fixture and read a failure);
  `docs/prototypes/audio-response-lab/01-plan.md` (audit, clocks, thresholds), `02-gate1-report.md`,
  `03-final-report.md`.

## Problem

There was no way to answer "is this signal right, on time, from the right source, and does the picture show it" for the
analysis and response system as a whole. The audits measured pieces of it with one-off probes. The tests check units.
No fixture had sample-indexed ground truth across musical categories, and nothing joined a ground-truth event to the
pixel that answered it.

## Decision

**A scene that is only routes.**
- Every indicator is a route from a production signal to a parameter, with an identity chain unless it is a declared
  probe. So the board shows exactly what the bus published.
- Signals that may be absent (sources, MIDI) have `optional` routes and authored-empty targets, so absence reads as
  absence, never as another signal.
- It is unlit, shadowless and clamp-tonemapped, so a pixel is linear in its parameter.

**Ground truth outside the engine.**
- The generator writes audio and a manifest. The manifest's canonical time is the first sample each event wrote; its
  registers and its silences are measured from what was written.
- The runner writes each fixture's project from file names only, so ground truth cannot reach the analysis.

**The production path, offline, at two clocks.**
- `--sonic-trace` at the hop rate (93.75 fps) gives analysis time at hop resolution.
- The same trace at 30 fps, with the finals, is the render's own update sequence. Render frame N is row N, and the
  composer checks that by pixel.

**Thresholds from the implementation.**
- Each check states the bound and the reason: the window, the hop, the refractory and the documented release
  constants (01-plan §6).

## Consequences

- The first run found the trace's event blindness (ADR-1721) and four behaviours worth the owner's time: the file-path
  beat grid on syncopation, the snare/hat confusion, mix onsets that miss bass, and slow mid/high levels
  (02-gate1-report).
- The scene's own GPU cost is dominated by text labels (about 7.5 ms of 11.7 ms at 1080p30 on the M2 Max). It meets the
  target, but text meshes are expensive everywhere.
- The live path is measured by ADR-1725's replay and loopback. The fast mid and high levels (ADR-1723) and the clock
  source (ADR-1724) came out of Gate 1.
- **Gates and findings.** A check is a gate when it is a property the system promises (mapping, rest after silence,
  bounds, band selectivity, silence is silent). It is a finding when it is a measured quality the owner has chosen to
  treat as separate work (the beat clock, a detector in noise). The end-to-end tests fail on gates. Findings are always
  reported with their numbers.
