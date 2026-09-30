# ADR-1024: Live antialiasing is a floor on the scene's FXAA, and the adaptive scale's floor is a setting

**Status:** Accepted (Sonic Garden, second brief PARTS 2-7).
**Date:** 2026-09-30

## Context

The owner reported that live playback shows jagged, shimmering edges that offline renders do not. The investigation
is in `docs/prototypes/sonic-garden/AA-RESEARCH.md`. It traced both paths and measured each difference between them.

- **The primary cause is the adaptive render scale (§15-§17).** On the owner's 1640x1326 Retina canvas it drops the
  Sonic Garden, GV3 and the Procedural Space presets to its 0.5 floor within seconds: a quarter of the pixels,
  bilinear-stretched by the tone map. That doubles edge error and crawl against full resolution.
- **Nothing antialiases the live frame.** FXAA (ADR-059) runs only when a scene authors `post/output/antialias`, and
  the Sonic Garden does not. There is no MSAA, TAA or jitter.
- **Offline renders the same renderer at 1:1, and often supersampled.** GV3 renders at supersample 2 plus FXAA 0.96,
  and the review stills at supersample 2.
- **The quality tier is not a factor.** Realtime and Offline are 0.6% apart at matched resolution.

The research compared MSAA, FXAA, SMAA, TAA/TAAU/TSR, MetalFX, supersampling and phone-wire widening for this
renderer.
- MSAA would be a renderer redesign: six MRTs including unresolvable R32Uint and R32Float, no depth resolve, and
  every screen-space pass needing a custom resolve.
- Temporal methods fail exactly where AV Gen lives: one-frame emissive flashes, deformation, thin geometry.
- No tested project has live headroom for supersampling.
- FXAA works at full resolution: edge error and crawl each fall 12-16%, at a cost below the GPU timer's resolution.
- No spatial method repairs the floor, where 4-5 px rings are 2 px and the shock ring is sub-pixel.

## Decision

1. `QualitySettings::antialiasFloor`: FXAA runs at `max(authored, floor)`.
   - It is 0 at every tier. `QualityPolicy::assertOfflineIsUncompromised()` now requires 0, so an offline render is
     unchanged by construction.
   - The live editor sets it to `kLiveAntialiasFloor` (0.75) from the per-machine setting
     `general.liveAntialias` (`"fxaa"` by default, or `"off"`), shown as Settings > Rendering > "Anti-aliasing".
   - `--live-aa fxaa|off` applies it to a headless run, and the quality arm `liveaa` applies it to any run, including
     a render, for comparison.
   - It is a floor rather than a tier default, so that a scene authoring more keeps its own, and so that no GPU test
     rendering at the default tier moves.
2. The adaptive scale's floor is the per-machine setting `general.adaptiveCanvasFloor`, snapped to a rung.
   - Default 0.5, unchanged.
   - Shown as Settings > Rendering > "Lowest scale", also `--adaptive-floor`, and followed live.
   - The trade it controls (thin geometry against frame rate) cannot be made automatically: on the garden the floor
     buys 38% of the frame for 75% of the pixels, because shadows do not scale.
3. The status bar shows the scene's extent beside the canvas's when they differ.

## Consequences

- Offline: 180 frames across Sonic Garden bell and perc, GV3 at its own supersample 2, and the Space showcase are
  byte-identical between the head binary and this one. So is the noise floor (two head runs).
- Live, by default: FXAA on every scene that did not author it, so the Space presets' SDF edge lines become
  continuous at full resolution. GV3 is unchanged, because it authors 0.96.
- The owner's jaggies at the floor are only lightly reduced by default (2-3%). The settings panel says why, and the
  "Lowest scale" choice removes them at a stated frame-rate cost.
- Not done, and recorded as next steps in AA-RESEARCH.md: phone-wire widening for `torus`/`tube` procedurals (the
  thin-geometry fix that works at any resolution); SMAA if FXAA is judged too soft; supersample 2 in the Sonic
  Garden's own render block (the art owner's call).
- Tests: `[adr1024]` in `tests/rendering/test_post_gpu.cpp` (the floor raises FXAA exactly to the authored
  equivalent, a stronger authored value wins, a zero floor is byte-identical), `tests/unit/test_app_settings.cpp`,
  `tests/unit/test_interactive_resolution.cpp` and `tests/unit/test_material_tier.cpp`.
