# ADR-057: The living chromatic field

Status: Accepted

## Context

ADR-054 gave the ecology colour that clusters in space: a world-space field decides each
instance's hue, so a patch agrees with itself and differs from the next valley over. It is
resolved on the CPU when the point cloud is projected, and written into the instance's colour
and emissive multipliers.

That is also its limit. Instance colour is baked once. The world's colour cannot move, and the
brief this came from asked for a field of `(worldPosition, time, ...)` — a world whose colour
breathes — explicitly *inside materials* rather than as a fullscreen post effect.

Re-baking per frame is not available: it would be a CPU pass over every instance in the world
every frame, which is the one thing the brief and the performance work both rule out.

## Decision

Drift the hue in the vertex shader, sampled once per instance root.

**A sum of travelling plane waves, not noise.** Three incommensurate terms in world XZ, each
travelling at its own rate, weights summing to one so the result is bounded by the authored
amount. This is the same choice ADR-055 made for wind and for the same reasons: a single
`valueNoise` is about 480 scalar operations and `fbm3` about 1400, which is the wrong tool for a
smooth low-frequency swell; and because every term *travels*, two patches a hundred metres apart
are never in phase. Six transcendentals.

**Sampled at the instance root, in the vertex stage.** The value is constant across an instance,
so it interpolates exactly and costs once per vertex rather than once per pixel. `chroma.x` gates
the whole path and is uniform across a draw, so a rock layer pays nothing.

**Rotate the product, not the multiplier.** `InstanceRecord::emissive` is a *multiplier* on the
material's emissive colour. Rotating that directly rotates the wrong thing — what anyone sees is
the product. So the shader forms the product, rotates it in OKLCH (equal angles are equal
perceived steps, and lightness and chroma survive, per ADR-052's reasoning), and expresses the
result back as a multiplier. The clamp is the same 96 the CPU path uses, and for the same reason:
a saturated emitter has a channel near zero, and turning its hue means raising that channel by
tens.

## Consequences

The valley's colour moves. Measured against a control that isolates it from the wind — which also
changes pixels by moving geometry — the hue-ratio change over twenty seconds is 0.047 with drift
on against 0.012 with it off, so 0.036 is the field itself, three times what the wind contributes.

The cost is not resolvable. Interleaved three times at 1440x900 against a steady 2.45 ms
calibration, the arms come out 21.6/22.0, 21.5/21.6, 21.2/21.9 — with drift *on* faster in every
pair, which is impossible and is simply what sub-noise effects look like here. Six transcendentals
per vertex on seven layers is below what the frame minimum can see.

`color::livingChromaTurns` is the CPU reference and is what the tests exercise: bounded by the
authored amount, deterministic, actually moving in time, and coherent between neighbours while
distant patches decorrelate.

Two things this does not do. The static per-instance hue from ADR-054 still exists and still
dominates the palette — this drifts around it rather than replacing it, which is deliberate: the
brief said the Quaternius look must survive. And base colour is untouched; only emission drifts,
because that is where the world's colour actually lives in this scene.
