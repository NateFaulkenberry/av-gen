# ADR-2168: `--capture` keeps the headless loop's frame (byte-identical after S10); re-pointing it at a one-frame render job is measured and left to the owner

- Status: Proposed (2026-11-26), eng/engine-api-s10 (Engine API stage S10; 04-poc-plan.md section 5 "the probe",
  03-boundaries.md section 2.4). The keep half is in force; the re-point is an open decision.
- Code: none beyond ADR-2165 (the headless loop's renderer is built from the host's device)
- Measured: `~/Desktop/av-gen-review/113-engine-architecture/04-implementation/s10/measurements/` (`capture-*.png`, `play-A/`, `seek-A/`), 2026-11-26

## Context

The plan said S10 re-points `--capture` at EngineHost and that its output would change (the phase-0 probe measured
14.6 % of pixels differing between today's `--capture` and a render job's frame at the same time). The orchestrator
asked for the change to be measured and brought back as a decision rather than accepted silently.

## What S10 did

`--capture` is still the headless frame loop's last frame, drawn by the application's renderer -- which since
ADR-2165 is built from the EngineHost's device and shader library. Nothing else about it changed.

## Measured (the S8 PoC project, copied to scratch, render block 320x180 at 24 fps; one gpu-lock.sh hold)

| Comparison | Pixels differing (of 57,600) | Max channel difference |
|---|---|---|
| `--headless --frames 25 --capture` before (f1f62385f) vs after (this branch), two runs each | **0** (one PNG sha256, `7a3fb160...`, all four runs) | 0 |
| `--capture` (t = 1.0) vs frame 24 of `--render --range 0:1.0417` (a render job, played from 0) | 13,161 (22.85 %), over the whole frame, mean abs 0.69 | 188 |
| `--capture` vs frame 0 of `--render --range 1:1.0417` (seeked) | 13,527 (23.48 %) | 188 |
| render job played vs seeked at t = 1.0 (control) | 1,021 (1.77 %) | 1 |

**Why they differ.** They are two render paths. The capture is the live path: the application's renderer at its own
quality (no `--tier` given: the renderer's default), no warm-up frames, no temporal pre-roll, materials compiled
blocking. The render job builds its own renderer at the project's render tier (`offline` here: its log shows the
tier raising anisotropy to 16x and the sky cubes to 1024 px, which the capture's does not), warms up two frames and
pre-rolls. The difference is spread over the whole frame (bounding box = the frame), not in one object, which fits
a tier/quality difference rather than a state difference; the played-vs-seeked control differs by at most 1.

## The decision left open

- **Keep (what S10 ships).** `--capture` stays the live frame: byte-identical to before, cheap (no second renderer),
  and the only command-line way to photograph a `--debug-target`, a `--quality-arm` or a `--disable` arm, which a
  render job ignores.
- **Re-point.** `--capture` becomes a one-frame render job (`render.capture`'s semantics, `history: play`): its pixels
  equal the deliverable's frame at that time (measured equal, byte for byte, in ADR-2159), but every existing
  `--capture` changes (23 % of pixels on this project), and the debug and arm views go.

The command-line reference now says what `--capture` is and how to get the deliverable's frame
(`--render out/ --range t:t+1/fps`, or `render.capture`).

## Revisit triggers

- The owner's choice between the two.
