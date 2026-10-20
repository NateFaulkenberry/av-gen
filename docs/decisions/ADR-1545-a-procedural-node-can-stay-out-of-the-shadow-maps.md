# ADR-1545: A procedural node can stay out of the shadow maps

- Status: Accepted (art/rainbow-world)
- Amends ADR-023 (procedural geometry) and reuses ADR-287's flag. This change is additive. A file without the key
  casts shadows exactly as before, and a casting object writes no key, so every existing save is byte-identical.
- Code: `src/scene/procedural.cpp`, which reads and writes `castsShadow` in `ProceduralGeometry::fromJson` and
  `toJson`.
- Tests: `tests/unit/test_procedural_casts_shadow.cpp` (`[adr1545]`) checks the JSON both ways, the default, a
  refused non-boolean, and that the flag reaches the composition's flattened scene through attach and two frames of
  parameter application. Both cases fail with the change removed: 3 of 8 assertions.

## Problem

Rainbow World's Stage 2 experiment E5 measured meadow grass in AV Gen (1080p, Ultra, headless live profile;
`docs/prototypes/rainbow-world/06-stage2-report.md` section 2). 50,000 clumps cost 62.8 ms of GPU, and **31.7 ms of
that was the shadow cascades**. Four Great Trees cost 14.5 ms of shadows, almost all of it alpha-tested leaf cards.

Grass that small casts shadows smaller than a shadow-map texel, so the cost buys nothing visible. The renderer has
honoured `ProceduralGeometry::castsShadow` since ADR-287, but only ecology scatter layers could set it. A procedural
node written in a scene file had no way to say "do not draw me into the shadow maps".

## Decision

A procedural block may carry `"castsShadow": false`:

```json
{"name": "grass", "kind": "procedural", "procedural": {"castsShadow": false, "source": {...}, "distribution": {...}}}
```

- The key is optional and defaults to true. It is written only when false.
- It is not a parameter and not structural. It changes which passes draw the object, not what the object is, which
  is the same contract ADR-287 gave ecology layers.

## Consequences

- Rainbow World's grass and leaf cards can stay out of the cascades. A tree can keep its shadow through a separate,
  cheap, opaque hull node that casts but is not seen. That proxy is a scene-authoring pattern; it needs no engine
  support beyond this flag and `visible`.
- No shipped scene sets the key, so no shipped frame changes.
