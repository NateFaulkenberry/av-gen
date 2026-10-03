# ADR-1090: The live profile is one command with two loops and one record

**Status:** Accepted (live optimizer, `docs/live-optimizer/02-plan.md` Stage 1).
**Date:** 2026-10-03

## Context

The brief's Phase 1 asks for an external tool that answers "can this scene run at the requested live target, and
what is consuming the budget?" The engine already measured most of it (01-research.md): `--headless --bench-json`
(fixed-step, no present), `--profile-cpu` (the live editor's phases), the GPU timeline. But no single command ran a
scene the way a performance does, so no number predicted a touring frame: the headless bench cannot see Fifo,
present waits, the projection output or deadline misses.

## Decision

1. **`avgen --live-profile`**, with its own flags parsed first (`parseLiveProfileArgs`, `app/live_profile.cpp`):
   `--mode headless|live`, `--target-fps`, `--size WxH` (the OUTPUT in pixels), `--start`, `--warmup`, `--measure`
   (fast 3 + 5 s, `--deep` 5 + 20 s), `--quality auto|level|profile|tier`, `--camera`, `--no-audio`, `--midi`,
   `--capture`, `--json`, `--text/--no-text`, `--verify-candidates N`. Its flags are flags only when
   `--live-profile` is present, so `--start` and the rest cannot collide with the editor's.
2. **Two loops, one builder.** Headless: `Application::runLiveProfileHeadless`, a fixed-step loop at the target rate
   (the live ladder and live antialiasing applied, since it emulates the live picture; `auto` runs the live
   controller during warm-up and holds the level it settles on). Live: the editor loop runs unchanged, with Start
   projection on a window sized to the output, and hands each frame to `noteLiveProfileFrame`. Both call
   `buildLiveProfile`; the statistics, categories, verdict and writers live in GPU-free code with unit tests.
3. **Order:** load (and asset upload), pre-warm (Stage 4), warm-up until a rolling median stops moving
   (`SteadyStateDetector`) or the cap, then measure. Cold costs (load, pre-warm, first frame, warm-up frames, whether
   steady state was reached, pipelines compiled while measuring) are reported apart from the frame statistics.
4. **Frame statistics against the budget.** `Distribution` plus frames over budget, percent under, and (live only)
   deadline misses in vsync terms: a frame that took more display refreshes than the target allows. Headless has no
   present, so its budget is checked against max(GPU span, CPU work) per frame, labelled as that model; the serial
   wall clock is still reported as itself.
5. **Output:** `avgen.liveprofile/1` JSON with a definitions block, and the brief's text report. Estimated and
   measured savings are separate fields and separate lines. `tools/live_scene_profile.py` wraps it for agents.
6. A profile never writes the settings file or the layout: the projection size is an override that is not saved.

## Consequences

- One command answers the brief's question in either mode; live-mode numbers come from the same loop the
  live-quality matrix measured (`~/Desktop/av-gen-review/30-live-quality/`).
- `runHeadless`'s per-frame `cpu(update)` log line printed running totals (probe2 was never cleared in the headless
  loop); it is now cleared per frame, as the live loop does.
