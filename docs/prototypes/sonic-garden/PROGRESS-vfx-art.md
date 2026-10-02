# Sonic Garden VFX expansion: art progress (sonic-art)

Brief: `02-brief-vfx-expansion.md` (the owner's words govern). The engineer's notes are `PROGRESS-vfx-eng.md`.
Worktree `../av-gen-sonic`, branch `proto/sonic-garden`. Commit only my own paths (`git commit -- <paths>`).
Review media: `~/Desktop/av-gen-review/25-sonic-vfx/`.

## Resume here (cold)

- 2026-10-02 06:50: survey done; research reports 1 and 3 being gathered (two web-research subagents); the
  engine's Effect Library (48 kinds, ADR-702/703/716/719) found to be the brief's "Entity Effects" and unused by
  any Sonic scene so far.
- Asked the coordinator for a stable sha to pin (the worktree's `build/release/src/avgen` is stale: built
  2026-10-01 16:25 before the branch was fast-forwarded to main; it lacks mosh, sweep and the beat grid).
- Asked for live effect triggers (TriggerClock reads only the offline analysis track; ADR-716 "Live triggers are
  not built").
- Next: write `research/01-vfx-scene-construction.md` and `research/03-authoredness.md`, then
  `SCENE-CATALOG.md`, commit; then the scene generator `tools/sonic_vfx/`.

## Plan

1. Research reports 1 and 3 (`research/`), ending in generation rules.
2. `SCENE-CATALOG.md`: 12-20 scenes (thesis, composition, palette, motion hierarchy, modulation vocabulary).
3. Build the scenes as data: one generator per scene in `tools/sonic_vfx/`, writing
   `examples/sonic-vfx/<scene>.json` + `.scene.json`, each `sonic.live: true`, listed in the Examples menu.
4. `TEST-MATRIX.md` from probe scenarios (file-based replay renders + live captures).
5. Evaluate (critic, engineer's evaluator), iterate, kill weak scenes.
6. Captures in the review folder: a still + a short clip with audio per scene, and a tour.

## Engine facts that shape the art (verified in code, 2026-10-02)

- **Effect Library** (`src/world/effects/kinds/*.cpp`, schemas with `stored*` rows): instances in the project's
  `effects` array, owner `world | entity | camera | light`, parameters `fx/<id>/<leaf>` (modulatable). Surface
  effects (FXL: Dissolve, Growth, Breathing, Organic Pulsation, Bioluminescence, Pulsing Veins, Fresnel, Rim Light,
  Color Cycling, Motion Smear) reach mesh AND procedural owners (`vs_proc`), not SDF.
- **Trigger activation** (Beat, Onset, MusicEvent, TimelineMarker, Repeat, Proximity) reads the OFFLINE analysis
  track only; live, only Repeat/Proximity fire.
- **Live signals today:** `notes.*` (noteOn with velocity, pitch, polyphony, density, rhythm, duration, legato,
  chord, tension, range, motion, direction, regularity, repetition, phrase), `sonic.*` (+ `.slow`),
  `sonic.transient`, `timbre.*`, `audio.*` (rms, bands, onset, onsetStrength, spectralFlux, beat clock). NOT live:
  `audio.onsetLow/Mid/High`.
- **Temporal:** `temporal/echo/*` (FIR echo), `temporal/mosh/*` (ADR-1049). **Post:** bloom, halation, anamorphic,
  lens distortion + chromatic aberration, DoF, tilt-shift, motion blur, grade (lift/gamma/gain, hueShift,
  saturation, contrast, temperature, tint), look (atmospheric, local contrast, light wrap), sweep (ADR-1050),
  vignette, grain, sharpen.
- **SDF** (`docs/sdf.md`): compiled raymarch, 96 nodes, 8 surfaces, line look (ADR-1047), `look/edge/*`.
