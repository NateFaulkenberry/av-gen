# ADR-123: Representation is two decisions, not one ladder

**Status:** Accepted
**Date:** 2026-09-13

## Problem

Target architecture §5.4 sketched a single ladder of projected radius:

```
> 200 px   full mesh, LOD 0
40-200 px  mesh LOD n
8-40 px    HLOD proxy
2-8 px     impostor
< 2 px     cull
```

and said plainly that the thresholds were "placeholders to be calibrated against measurement, not
proposals". Calibrating them (ADR-124) showed the middle of the ladder is asking the wrong question.

A projected radius says how much screen an object covers. §4.5 measured that fragment cost does not
track that; it tracks **pixels per triangle** (ADR-122). A fern and a terrain chunk at 120 px of
projected radius differ in tessellation by four orders of magnitude, so one radius threshold cannot
put them on the right rung. But a radius *is* the right measure of something else: whether an object
is still large enough for its silhouette to matter. A 3 px object cannot show silhouette detail
however finely it is tessellated.

Two different questions had been folded into one ladder.

## Alternatives

1. **One radius ladder**, as sketched. Rejected above.
2. **One px/triangle ladder** for everything, including the kind. Rejected: px/triangle says nothing
   about whether a billboard would be an acceptable substitute. A large, coarsely tessellated rock
   has excellent px/triangle and must not become an impostor.
3. **Two orthogonal decisions.** Chosen.

## Decision

`rendering::RepresentationSelector` makes two decisions with two different measures:

**Which kind** — mesh, HLOD proxy, impostor, cull — from **projected radius**. This is a question
about what a thing can still look like at that size.

**Which rung of the mesh ladder** — from **pixels per triangle**. This is a question about cost, and
px/triangle is the only measure §4.5 supports.

Consequently §5.4's ">200 px = full mesh" band **does not exist as a threshold**. LOD 0 is simply
what the px/triangle rule returns for anything large: a rung whose triangles are already at or above
the target is not worth coarsening. Two thresholds deciding one thing is how they drift apart, and
the engine already has one ladder (ADR-029, on the GPU, for scattered instances) that this one must
not contradict.

A **hero** (ADR-104) is never demoted past `policy.heroFloor`, which defaults to "never coarser than
a mesh". A billboard of the subject of the shot is the kind of saving that costs the shot. A floor is
a bound, not an assignment: a hero large enough for LOD 0 still gets LOD 0.

`Representation::Culled` here means **"too small to be worth drawing"**, which is a different claim
from "off screen". The selector performs no visibility test at all; frustum culling belongs to
`Composition::cullEntityNodes` and to `shaders/cull.wgsl`, and a second, disagreeing answer to the
same question is a defect rather than a redundancy.

## Consequences

- A disabled policy returns `FullMesh` / LOD 0 for every drawable, which is exactly what the renderer
  does today. §6 asks the tests to assert that rollback; they do.
- `HlodProxy` and `Impostor` are decided but **not yet built**. The selector can name them before the
  machinery that draws them exists, which is what lets the decision be tested before the asset
  pipeline is written. Nothing consumes the choice yet — see the consequences of ADR-125.
- The choice carries `changed`, so a future transition mechanism has a hook without this ADR
  pre-empting what that mechanism is. Whether cross-fade, dither or stochastic alpha is viable
  without temporal AA is the wave's open question (C2) and is not decided here.

## Rejected alternatives

- **Keeping the §5.4 bands verbatim so the plan and the code agree.** The plan asked for them to be
  calibrated; keeping an uncalibrated threshold because it is written down is how a placeholder
  becomes load-bearing — the same failure ADR-110 found in the mesh budgets.
- **Folding visibility culling into the selector**, since it already has the camera. Refused: nine of
  this engine's eleven documented defects are state ownership at a boundary.

## Revisit when

HLOD proxies and impostors are actually built, at which point the radius thresholds have a real
quality consequence and should be re-derived against the look of the thing rather than against the
sketch.
