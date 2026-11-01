# ADR-1888: Punctual light flicker, and card fur with no engine change

- Status: Proposed (Art Pass 2, Milestone 1), scene/strange-attractor.
- Code: `PunctualLight::flicker`, `flickerRate`; the light keys `flicker`, `flickerRate`
  (`src/scene/scene_types.hpp`, `src/scene/composition.cpp`).
- Tests: `[pass2][adr1888]`.

## Context

The first encounter is three Sasquatches at a campfire. A fire light that holds still reads as a lamp. The plan held
ADR-1888 for a fur decision (shell fur or alpha cards) to be made in M1.

## Decision

- **Flicker.** A point or spot light may set `flicker` (0..1) and `flickerRate` (Hz). Its intensity is multiplied by
  max(0, 1 + flicker * n(t)), where n is a deterministic sum of incommensurate sines in about -1..1 (no noise state,
  so seek equals play). The campfire uses 0.35 at 3.2 Hz: a slow breathing, not a strobe (owner flicker sensitivity).
- **Fur.** Alpha cards baked in Blender (`pass2/build_sasquatch.py`: 24 000 cards, the fur colour baked into the strand
  texture, weights transferred from the body) render through the existing skinned, blended material path. No engine
  change; shell fur is not built.

## Consequences

- Fur cards at 20-30 m read as a shaggy silhouette; up close the card edges and the fire's spill on them can look
  pale. Roughness 0.95 keeps them from glinting.
- Flicker is applied to the authored intensity after looks and routes, so a look may switch the fire on (intensity)
  and it still breathes.

## Revision at Art Pass 2, M1b

The flicker's fast terms are gone. At the campfire's rate they ran at 6-12 Hz, near a 30 fps render's Nyquist, and the
fire-lit ground jumped up to 7 % in brightness from frame to frame. n is now 0.6 sin(2 pi t) sin(2.3 t + 1.7) +
0.4 sin(4.1 t + 0.4), with t = seconds x flickerRate: a breath of a few hertz at most.
