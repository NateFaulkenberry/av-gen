# ADR-041: World Director, look presets and musical phrasing

- Status: Accepted (2026-09-09)
- Research: `docs/research/procedural-composition.md`

## Decision
- A **World Director** is a curated set of world macros with artistic names: scale, density, drama,
  contrast, warmth, mystery, energy, depth, atmosphere, motion, chaos, stillness, focus, glow,
  organic, mechanical, sacred, alien. Each expands, through the existing macro-to-route mechanism,
  onto the technical parameters that idea moves. It introduces no new evaluation path: a director
  knob is a macro knob, and every target is an ordinary route with a remap.
- **Look presets** are ordinary presets that capture a whole visual configuration (light rig,
  exposure, grade, atmosphere, post, director knobs) without touching world geometry, so the same
  world can be rendered as Monumental, Sacred, Alien, Industrial, Bioluminescent, Cosmic, Dreamlike
  or Hyperreal. They ship as data under `examples/looks/`.
- **Musical phrasing**: the engine publishes `beat.phrase` and `beat.section` derived from the bar
  count with a configurable phrase length, so state transitions and slow escalations key to musical
  structure rather than to individual beats. The 80/15/5 guideline (autonomous, audio-influenced,
  event-driven) is documented as authoring guidance, enforced by review rather than by code.

## Consequences
- Positive: worlds become art-directable in words; a look is separable from a world; escalation
  happens over phrases.
- Negative: a director knob's mapping is per world, so a shared vocabulary needs discipline; look
  presets that name lights or fields a world lacks apply partially, which the inspector must show.
