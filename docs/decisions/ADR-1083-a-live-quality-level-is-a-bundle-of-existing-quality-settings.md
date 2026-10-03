# ADR-1083: A live quality level is a bundle of existing QualitySettings fields, five named levels, LIVE only

**Status:** Accepted (live quality, `docs/live-quality/00-brief.md` §5, §20).
**Date:** 2026-10-03

## Context

The adaptive controller had one lever, `QualitySettings::renderScale`, with a floor of 0.5. Measured through the
projection output, Glowmere and Liminal both reached the floor with their GPU still at about 16 ms and nothing else
engaged: no volume reduction, no effect reduction, no tier change (`evidence-live-projection-2026-10-02.md` §C.2). The
`Preview` tier already had cheaper values for most of the expensive fields, but nothing switched to it at runtime.

## Decision

1. **Five levels, Ultra, High, Medium, Low and Emergency** (`app::LiveQualityLevel`). Each is a
   `LiveQualityRung`: a render scale and **ceilings** on existing `QualitySettings` fields:
   `volumeResolutionScale`, `volumeStepScale`, `cascadeCount`, `shadowResolution`, and the Preview tier's shadow
   filtering (`softShadows` off, 6 PCF and blocker taps). `app::applyLiveRung(current, base, rung, floor)` takes the
   minimum of the rung's value and the live tier's own (`base`), so **Ultra is the tier exactly** and no level ever
   raises a setting. Fields the ladder does not own are left as `current` has them (the live antialiasing floor
   survives a level change).
2. **Two new fields, both gates on the scene's own post effects:** `QualitySettings::motionBlur` and
   `QualitySettings::depthOfField`, default true in every tier. Closed, `SceneRenderer::render` zeroes the copy of
   `post.motionBlurAmount` / clears `post.dofEnabled` it hands the post chain, which is that chain's own "off"; open,
   nothing changes. No shader is touched. They are permissions, like `antialiasFloor` (ADR-1024), not duplicates of
   the scene's settings. `QualityPolicy::assertOfflineIsUncompromised` now requires both open.
3. **The values** are the Preview tier's own values for the same fields (quarter-resolution volumes, half the steps,
   two cascades, a 1024 atlas, plain filtering), plus a quarter of the steps and a scale below 0.5 at Emergency. The
   tables are in `src/app/interactive_resolution.cpp`; the order the levers engage in is ADR-1084's.
4. **The lowest scale is 0.38** (`kLiveScaleFloorMin`), for pixel-bound scenes (Liminal is 97% per-pixel). The
   "lowest adaptive scale" setting (ADR-1024) keeps its key and now ranges 0.38-1.0; levels below it keep their
   other reductions at that scale.
5. **A level can be pinned.** `general.liveQuality` is `auto` (the controller moves) or a level's name (it holds).
   "Ultra" is what the old "Adapt automatically: off" meant; `--adaptive-scale on|off` maps to auto|ultra and
   `--live-quality <level>` names one. `adaptiveCanvasScale` is removed (ADR-441).
6. **LIVE only.** The levels are applied in `Application::serviceLiveQuality`, in the live editor's frame loop and
   nowhere else. `RenderJob` and the headless runner never see them.

## Alternatives considered

- **Switch the whole `QualityTier` (Realtime to Preview) at the bottom of the ladder.** One huge step that also moves
  material tiers, AO, the sky and particle spawn: a visible change of look, not a fidelity step.
- **New fields for "live volume quality" etc.** Duplicates the fields the brief says to reuse.
- **Turn volumetrics off at Emergency** through `PassToggles::volume`. That is a forensic arm by its own header's
  rule, and fog is part of a scene's identity; a quarter of the steps at a quarter resolution is "minimal" and keeps
  the composition.
- **A continuous quality knob.** Every change can reallocate targets and reset history (ADR-1086); a few named,
  deterministic states are what a performer can be told about.

## Consequences

- A heavy scene keeps degrading after the scale reaches 0.5: effects first, then a 0.38-0.42 scale.
- Where a scene authors its own cascade count (`environment.shadowCascades`), that wins over the ladder's cap, as it
  always won over the tier's (the scene's rule in `SceneRenderer::render`). The atlas and filtering still drop.
- The machine's settings file may hold the old 0.5 floor (ADR-1024's default was written on every save); such a
  machine reaches Emergency at 0.5 until the floor is lowered in Settings.

## Revisit triggers

- A scene where a level's look change is judged worse than its frame-rate gain (the levels are data; change them).
- Temporal upscaling (out of scope here), which would make lower scales cheaper to look at and change the order.
