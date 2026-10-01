# ADR-1052: SDF rim emission (a fresnel glow on smooth silhouettes)

- Status: Accepted (2026-10-01), proto/liminal-space (All You Got art pass 3, §31-32)
- Code:
  - `SdfLook::rim*` and `SdfObject::Surface::rim` in `src/scene/sdf_object.*`.
  - `SdfObjectUniforms::look5` and `look4.w` in `src/rendering/sdf_renderer.*`.
  - The rim term in `fs_sdf` in `shaders/sdf_raymarch.wgsl`.
- Tests:
  - `tests/unit/test_sdf_rim.cpp` (`[adr1052]`): JSON read and round trip.
  - `tests/rendering/test_sdf_gpu.cpp` (`[adr1052]`): a lit ring, a dark centre, the power, the surface mask,
    and off being byte-identical.

## Context

In the dark, line-drawn world the mannequin's head reads as a black blob. The line look (ADR-1047) draws only
creases, through `sdfEdge`'s normal-angle test, so a rounded, smooth head has no lines. Its fill is near-black
and so is the background. The owner asks for a subtle glowing contour that keeps the head readable against dark
environments (§32). (The head missing from the final scene is a different defect: the object's march bounds clip
it. ADR-1051 now reports that.)

## Decision

An optional rim term on ray-marched SDF objects:

```text
glow = rimColor * rimIntensity * surfaceRim * (1 - |n . v|)^rimPower
```

- The glow is fogged by the same transmittance as the edges, and added to both the colour and the bloom target.
- **Parameters:** `sdf/<o>/look/rim/intensity` (0 = off, the default), `look/rim/color` (palette-bindable),
  `look/rim/power` (3; higher gives a thinner rim), and `sdf/<o>/surface/<k>/rim` (a per-surface multiplier:
  1 by default, 0 = no rim on that surface).
- All of them are keyable and routable.
- **In JSON:** `"look": {"rimIntensity", "rimColor", "rimPower"}` and `"surfaces": [{..., "rim": 0}]`. These
  keys are written only when they differ from the defaults.

## Consequences

- The uniform block grows from 240 to 256 bytes, which still fits the 512-byte object stride.
- At intensity 0 (the default) the term is skipped, and the image is byte-identical to the image without it
  (tested). The surface multiplier is stored in the surface record's unused `p2.w`.
- The term runs only in the raymarch mode; mesh-mode SDF objects do not get it, as with the edges.
- **To give only the head a rim:** put the head on its own surface, set `rim: 1` on that surface and 0 on the
  others, and key or route `look/rim/intensity`.
