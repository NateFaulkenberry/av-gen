# ADR-147: The batch render path never selects the offline tier

**Status:** Accepted (a finding, not a change)
**Date:** 2026-09-13
**Relates to:** spec §5.9, ADR-146 (the other half of the same contract)

## The finding

**`app::RenderJob` renders at `QualityTier::Realtime`.** Every `--render` job, and every render
started from the UI, produces a frame at the interactive tier's quality.

By construction: `SceneRenderer::setQuality` has exactly one caller in the repository —
`Application::setup`, which calls it on the *interactive* renderer when `--tier` is given.
`app::RenderJob` constructs a `SceneRenderer` of its own (`render_job.cpp:93`) and never calls it.
`RenderSettings` has no tier field, so there is no channel through which it could be told one;
`RenderSettings::quality` is the video encoder's 0–100 setting and is unrelated.

By measurement, which is what this is actually asserted on. One frame of the RendererQA scene at
480×300 and second 1.0, rendered through the real batch path and then through the interactive path
at each tier:

| comparison | bytes differing (of 576,000) | worst channel delta |
|---|---:|---:|
| interactive realtime vs interactive offline | 395 | 116 |
| **batch vs interactive realtime** | **0** | **0** |
| batch vs interactive offline | 395 | 116 |

The batch frame is byte-identical to the realtime tier. The test asserts the *premise* first — that
the two tiers differ at all on this scene — so a fixture in which the tier changed nothing could not
have been read as a pass.

## What the tier would have changed

`QualitySettings::forTier(Offline)` against Realtime's defaults: shadow maps at 4096, four cascades,
24 PCF taps, 24 PCSS blocker taps, 24 contact-shadow steps, six AO slices at twelve steps each, a 16
frame AO history, 48 SDF shadow steps, and — the two that matter most for a still — the ambient
occlusion and the directional shadow mask at **full resolution** rather than a fraction of it
(`aoResolutionScale` and `shadowMaskScale` both 1.0).

The shadow mask's resolution is not a small term. §3.2.3's attribution measures "mask at full
resolution" at 1.02 ms of a 10.88 ms scene pass — 9% — which is the cost of the thing a batch render
is currently not paying for and not getting.

## Why the difference measured here is only 395 bytes

Because the RendererQA scene is a control, not a showcase: a floor, a few objects and one
directional key. It was chosen for this experiment precisely because it is the scene the performance
baselines are measured against, so it cannot have been picked to flatter the result. The 395 bytes
answer the question asked — *which tier ran* — conclusively, and say nothing about how large the
difference would be on Glowmere, which has 230 lights and a valley of shadow casters. That number is
not measured here and is not claimed.

## Why this is recorded rather than fixed

`app/render_job.cpp` and `app/render_settings.*` are not this agent's files, and the fix is a small
API decision rather than a line: `RenderSettings` gains a tier, defaulting to Offline for a batch
render, and `RenderJob::start` applies it. Defaulting it to Offline changes the output of every
existing render, which is the right change and is still a change somebody should make deliberately
rather than find in a certification agent's diff.

It also interacts with ADR-146. Fixing that one by extending `RepresentationPolicy::forTier` is
worth nothing to a batch render until this one is fixed, because the batch renderer never asks for
the offline tier in the first place. **These two should be fixed together, and this one first.**

## Consequences

- `tests/rendering/test_phase_g_certification.cpp` carries a second deliberately failing test,
  `[gpu][certification][offline][tier]`. Like ADR-146's, it is a transcription of a contract and is
  not weakened to make the suite green.
- Nothing in the upgrade's measurement record is invalidated: every benchmark in
  `01-audit-and-baseline.md` was taken through the *headless* path with an explicit `--tier`, which
  goes through `Application::setup` and does select the tier. This defect is confined to
  `RenderJob`, which none of those measurements used.

## Verified vs assumed

**Verified:** the byte comparison above, under `tools/gpu-lock.sh`, with the premise asserted; that
`setQuality` has one caller and `RenderSettings` has no tier field, by search over `src/`.

**Assumed:** that no other path reaches a job's renderer and sets its tier indirectly — the search
was for the setter's name, and a tier applied by some other mechanism would not have been found by
it. The pixel result is the stronger evidence and does not depend on the search being complete.
