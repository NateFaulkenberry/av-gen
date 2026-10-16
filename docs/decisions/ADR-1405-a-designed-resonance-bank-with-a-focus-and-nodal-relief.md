# ADR-1405: A designed resonance bank, with a focus, and a relief shaped from its nodes

- Status: Accepted (2026-10-16), proto/cymatic-split (Cymatic World's shape language: `docs/prototypes/cymatic-world/08-brief-resonant-shape-language.md`)
- Code:
  - `ResonanceSource::{orders, focus, focusWidth, focusSweep, focusPeriod, focusFloor}`.
  - `ResonanceSpec::{reliefSand, reliefFigure, reliefAmplitude}` and `ResonanceSums::figure` (`src/spatial/resonance.*`).
  - The relief's terms in `fields.wgsl` (`resonanceSums` now returns the signed figure as w; a reader's `wave0` lanes carry the weights).
  - `source/<i>/focus` and the relief weights as parameters (`src/scene/field_params.cpp`).
- Tests:
  - `tests/unit/test_resonance_motion.cpp` ("A source's orders list designs its bank", "The relief's signed-figure and magnitude terms ...").
  - The GPU parity case (orders, and a reader with all five relief terms).

## Context

Cymatic World's owner found the moving ground "a sea of moving blobs". Rendered in isolation from the scene camera, each mode already draws a clean Chladni-like figure. The blobs were the sum: five or six modes from four sources at comparable weights, which is statistically a Gaussian random field. Mode competition could not pick leaders because a dense mix drives every mode alike. The relief was led by the signed vibration, so it had no crisp feature, and every scale summed into one reader.

Cymatic Plate is compelling because it shows one coherent figure at a time, moves that figure's mode numbers with the music, and makes the nodal set the picture.

## Decision

1. **A designed bank.** A source's `orders` list gives its modes their angular orders (point) or numbers of directions (lattice) in u order, cycled. A bank is then designed rather than hashed: rings, a trefoil, four lobes, a dipole, six lobes...
2. **A focus.** `focus` puts a Gaussian window over u, e^(−((u − f)/focusWidth)²), on a source's mode weights.
   - The window's centre is f = focus + focusSweep·sin(2πt/focusPeriod + φ), clamped to the band.
   - One or two adjacent modes lead, and the figure crossfades from one designed mode to the next as f moves.
   - `focusFloor` keeps the others faintly present.
   - The weight acts on both envelopes, so every reader (geometry and colour) sees the same figure.
   - A route moves `focus` with the music. The sweep, a pure function of the clock, keeps a steady passage evolving.
   - This is Cymatic Plate's principle (move the figure's identity, continuously) carried into the persistent bank.
3. **Relief from the nodes.** A relief reader now sums five terms of the slow figure F = Σ sᵢφᵢ:
   - `reliefSand`: the nodal ridge (the old base term, weight 1 by default);
   - `reliefLine`: the narrower crest;
   - `reliefVibration`: D;
   - `reliefFigure`: the signed F;
   - `reliefAmplitude`: |F| (negative weights sink the antinodes into basins).

   Several readers with listening windows give each spatial scale its own shaping and its own deformer.

## Consequences

- Files without these members behave exactly as before (default weights and empty orders reproduce ADR-1400).
- Deterministic and seek-exact: the focus is a function of the clock plus parameters.
- The scale hierarchy costs one deformer per scale, but each reader evaluates only its band's modes (the listening window skips the rest), so the per-vertex work is about that of one reader over all modes, plus the overhead of the extra passes.
- A route on `focus` is the music's hand on the figure. It inherits the route's own smoothing (attack and decay), not a phase: the figure's identity moves, its oscillators keep their phase.
