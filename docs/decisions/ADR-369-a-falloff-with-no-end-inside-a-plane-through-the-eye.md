# ADR-369: A falloff with no end, inside a plane through the eye

- Status: Accepted (2026-09-19)
- Extends ADR-230 (atmospheric effects). Same discipline as ADR-367 (soft particles): "off" has to
  be off *at* the boundary, not near it.

*Numbered 369: main carries 360-367 (367 is this branch's own, already staged) and 368 is reserved
for the Image/Look branch.*

## Problem

The owner reported that the comet "sometimes draws a hard straight line across the whole frame" —
a crisp diagonal terminator with a visibly brighter region on one side. It reproduces in every
frame of the shipped Tree of Life project.

## What it was not, with the evidence

Three candidates were eliminated by measurement rather than by reading, because the first two
plausible readings were both wrong.

| candidate | probe | result |
|---|---|---|
| the coarse rejection sphere `bound` | `bound * 8`, rendered | **byte-identical** (`9c1ebb53a511b5cd` both) — the sphere is never the binding constraint |
| the aurora half of the effect | `out.radiance = comets.radiance` only | **byte-identical** — `resolveAtmosphericEffects` dispatches exclusively on `kind`, so a `kind: comet` effect never emits an aurora however much aurora data it carries |
| the `glowmere-cosmos` background layer | layer disabled | line **survives** |
| the tail | `tailIntensity = 0` | line **survives** (wedge contribution 5.373 against 5.496) |
| **the coma** | `haloIntensity = 0` | line **vanishes** (wedge contribution **5.496 → 0.110**) |

Before any of that was trusted, a plumbing probe: force `atmosphereSkyAt` to return pure red. Mean
frame RGB came back (201.6, 12.5, 220.2). Shader edits do reach the app, so the byte-identical null
results above are real null results and not a build that never picked the change up.

## The mechanism

```wgsl
let toH = head - ro;
let th = dot(toH, rd);
if (th > 0.0) {
    ...
    let halo = (hr * hr) / (perp * perp + hr * hr);
    radiance = radiance + (c.core.rgb * core + c.halo.rgb * halo * halo) * rainbowHead;
}
```

The coma is an inverse-square wash **squared**, and it is never zero at any distance. With
`haloSize 95`, `haloIntensity 3.8`, `coreIntensity 20` and bloom downstream, its far field is faint
but not nothing.

Above it sits `if (th > 0.0)`, which is a **half-space test through the camera**. That is the whole
of it: a plane containing the eye projects to an *exactly* straight line across the frame — not
approximately, the way a distant sphere's silhouette would. So a small but finite wash was being
cut to zero along a mathematically perfect line, and the "sometimes" is just whether that plane
crosses the frame, which depends on where the comet is on its arc.

This is worth separating from the original hypothesis, which was the bounding sphere. A sphere's
silhouette is a conic and only *looks* straight over a narrow field of view; a plane through the eye
**is** a line. The perfection of the edge was the clue, and it pointed past the sphere.

## Decision

Window the coma so it is compactly supported, and leave the rejection alone:

```wgsl
let facing = clamp(th / max(length(toH), 1.0e-4), 0.0, 1.0);
let coma = halo * halo * smoothstep(0.0, 0.30, facing);
```

`facing` is the cosine of the angle off the head, so the window closes smoothly long before the ray
turns side-on and the cut fires. Within about twenty degrees of the head — where the coma is
anything an eye can see — `facing` exceeds 0.94 and this multiplies by exactly one.

Widening `bound` was the alternative and is the wrong trade: it buys cost everywhere to hide a seam,
and the comment above the rejection is right that the rejection is what stops the effect being paid
for on every pixel. Making the falloff honest lets the rejection discard only what is already zero.

## The measurement

Contribution isolated as `render(shipped) - render(effect disabled)`, hero camera, t = 8.166 s,
`tools/gpu-lock.sh`:

| | wedge region | far side | head/core region |
|---|---|---|---|
| before | 5.496 | 0.147 | 37.718 |
| after | **0.995** | 0.147 | **37.718** |
| `haloIntensity = 0` floor | 0.110 | 0.147 | 37.718 |

The wedge loses 82% of its contribution and **the comet itself is unchanged to three decimals** —
that is the control, and it is the number that says this dimmed a seam rather than the effect.
Visually the straight boundary and the flat filled wedge are gone; what is left in the far field is
a smooth gradient with no edge, which is what a coma should look like.

`tests/rendering/test_comet_coma_gpu.cpp` guards it: an empty sky with one comet, five points along
the arc, asserting the sharpest adjacent-pixel step stays under a quarter of the frame's own peak.
Verified in both directions — with the window removed the test **fails**, 4 assertions of 13, with a
step of 16.52. It fails at some arc positions and not others, which is exactly the intermittency the
owner described and the reason the test sweeps five times rather than sampling one frame. A
`sawComet` check stops the sweep passing vacuously on five blank frames.

## Consequences

- The residual 0.995 is the windowed coma falling off smoothly, not a seam. It could be taken to
  the 0.110 floor by closing the window earlier, at the cost of visibly clipping the coma's skirt.
  0.30 was chosen as the widest window that removes the edge.
- The same shape of defect is worth looking for in the shed-fragment loop, which has its own
  `continue`s. The tail's gaussian is compactly supported in practice and is not at risk.

## Revisit when

- Anyone raises `haloSize` far above the current 95, which moves the wash's far field up and may
  make the residual gradient visible again.
