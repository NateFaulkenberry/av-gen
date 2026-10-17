# ADR-1470: A material program can ramp through four colours in OKLab

- Status: Accepted (art/resonator-quality)
- Amends ADR-030 (material programs). It is an appended op, so the wire format of every other op is unchanged.
- Found by: the Resonator quality pass (owner addendum `docs/prototypes/resonator-quality/02-addendum-procedural-color-fields.md`).
  The three Resonator scenes had to carry broad multi-colour gradients (indigo into cobalt into violet into fuchsia)
  between the active palette's roles, with no grey or dark midpoints and no banding.

## Problem

The only colour gradient op, `ramp`, has three stops and blends them linearly in RGB. Between two saturated colours
far apart in hue (violet and cyan, cobalt and fuchsia) the linear midpoint loses chroma and reads grey or muddy, and
three stops are one fewer than a palette's shadow, primary, secondary and accent roles. Doing it with existing ops
costs a chain of ramps, a `mixBy` and a `hueShift` per segment, which the Resonator programs cannot afford (Cymatic
World sat at 43 of 48 ops), and still is not perceptual.

## Decision

Add one op, `oklabRamp`:

```text
stops  = k, k2, k3, k4                   // constant .. constant4, linear RGB (+ w)
f >= 0 (clamped; the default value 1):  t = clamp(a.x, 0, 1); stops at 0, 1/3, 2/3, 1
f <  0 (cyclic):     t = fract(a.x);       stops at 0, 1/4, 1/2, 3/4, and k again at 1
out.rgb = mixOklab(stop[i], stop[i+1], u)  // color.wgsl / core::color, the same mix the palette uses (ADR-1043)
out.w   = linear mix of the stops' w
```

It reads only `a`. The stops stay in linear RGB, so a project palette's role bindings (ADR-1043, ADR-1420) write them
exactly as they write a `ramp`'s or a `constant`'s colour, and palette shifting, the selector and its locks keep
working: the palette owns the colours, the program owns where each one falls.

The cyclic form lets a colour coordinate that only ever grows (a phase integrated by a route, ADR-1372) run through
the palette for ever with no seam and no reset.

## Consequences

- One op gives a four-colour perceptual gradient; Cymatic World, Waveform Dimension and Resonant Silk use it for their
  colour fields (`docs/prototypes/resonator-quality/01-report.md`).
- Existing programs are unchanged: the op is appended, every other op's code path and packing is untouched, and
  programs that do not use it render bit-identically (checked: see the report).
- The interpreter gained one branch. Its cost on a scene that never uses the op is reported in the Resonator quality
  report (a non-Resonator scene measured before and after).
- Tests:
  - `oklabRamp blends four stops in OKLab, clamped or cyclic` (`[adr1470]`): the stops, the clamp, the OKLab midpoint,
    more chroma than the linear midpoint, the cyclic wrap with no seam, validation and the JSON round trip;
  - the GPU parity batch in `test_material_gpu.cpp` (the `oklabRamp` and `oklabRampCyclic` probes) against the CPU twin.

## Rejected alternatives

- **Making `ramp` perceptual.** That changes every existing program's colours.
- **A `hueShift` chain.** It rotates hue at constant lightness and chroma, so it cannot reach a darker or paler role,
  and it costs two to three ops per segment.
- **Baking the gradient into a colour grid.** It is a texture per scene per palette, and a palette blend would have
  to rebake it; Cymatic World also has no field slot left (16 of 16).
