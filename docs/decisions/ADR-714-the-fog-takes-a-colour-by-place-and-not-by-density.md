# ADR-714: the fog takes a colour by place, and never by density

**Status:** Accepted. The look needs the owner's eye.
**Date:** 2026-09-24
**Resolves:** fog brief §25 (height colour, distance colour), ADR-575's "§25, assessed, not built",
and the production pass §2 "§25 colour" row.
**Implemented by:** `shaders/fog.wgsl` (`fogLuminance`, `fogHueMix`, `fogHeightColourWeight`,
`fogDistanceColourWeight`, `fogTintedColour`), `shaders/volume.wgsl` (`mediumEmissionAt`'s fog arm),
`src/world/fog_field.cpp` (the twin), `src/world/effects/kinds/volumetric_fog_effect.cpp` (five
rows and their lanes; see ADR-713's lane map).
**Tests:** `tests/unit/test_fog_colour.cpp` (new), the parity and render cases shared with ADR-713.

---

## Context

The bank had three colours through its depth (`colorDeep`, `colorMid`, `colorAccent`), mixed on
density in `mediumEmissionAt`. §25 also asks for a height colour and a distance colour, and it warns:
*"avoid making colour responsible for structure."* ADR-575 held both back for that reason.

In this medium the eye reads structure from luminance. The depth hierarchy is dark where the bank is
thin and bright where it is dense. A tint that moved luminance would draw density that is not there,
which is exactly what the warning describes.

## Decision

### Both tints are luminance-preserving, and that is the whole design

`fogHueMix(base, tint, w)` first rescales `tint` to `base`'s Rec. 709 luminance. Only then does it
mix, which gives `mix(base, tint * L(base) / L(tint), w)`. Luminance is linear, so the result has
`base`'s luminance at every weight. The tint changes hue and saturation and nothing else. A tint too
dark to carry a hue (L < 1e-4) leaves the colour alone rather than dividing by nothing.

Because luminance is linear, the property survives the march's integration. A pixel's luminance is a
sum of per-sample luminances, and none of them moved. `test_fog_flow_colour_gpu.cpp` measures it in
the frame: the worst relative change is 0.13%, which is half-float storage.

### The weights are functions of where a sample is, never of how dense it is

- **Height** (`heightColor`, `heightColorAmount`).
  - The weight is `amount * smoothstep(0, 2, h)`, where `h` is `fogVerticalProfile`'s own coordinate:
    0 at the densest layer, in thicknesses above it.
  - "The top of the bank" therefore means the same height to the density, to §26's glow height, and
    to this colour. That is one vertical model (ADR-567).
- **Distance** (`distanceColor`, `distanceColorAmount`, `distanceColorRange` in metres).
  - The weight is `amount * (1 - exp(-d / range))`, where `d` is the sample's distance from
    `frame.cameraPos`.
  - It is exponential because that is what extinction along a path is, and it takes one number
    instead of two.
- **Order.** Height is applied first, then distance.

Both weights are exactly 0 at amount 0, and `fogHueMix` then returns `base` itself.

### Where they apply

They apply where the three depth colours apply: the bank's own glow (`mediumEmissionAt`), through the
same per-kind arm ADR-575 used for the glow's height influence. So a lit bank with Self glow at 0
does not show them. The same is true of the three depth colours, and both tooltips say so.

- **A scattering or absorption colour** would tint the scene light the bank scatters, and §25 lists
  it separately. It is not built here. It would turn the march's scalar `mediumScatter` into a
  `vec3` and touch every medium kind's frame.
- **Lanes.** The height colour rides in the three depth colours' fourth components (9.w, 10.w,
  11.w), so the colour lanes carry a fourth colour. The distance colour and its amount are lane 8.
  The height amount is 5.w and the range is 2.w (ADR-713's table).

## Evidence

The sheet is in the session scratchpad (`fogflow/sheet-colour.png`), from the same placed ellipsoid
made self-luminous (emission 0.02).

- **Hue shift removed for the sheet.** The Tree of Life project routes `state.progress` to
  `post/grade/hueShift` with amount 1.0. `state.progress` is 1.0 nearly always (ADR-712), so every
  hue in that frame is rotated by a large angle. With the route in place, an amber tint rendered
  pink, and the teal depth colours rendered yellow-green. The colour arms drop that route; the
  project is unchanged.
- **Default.** A teal bank.
- **Height colour mauve, amount 1.** The top turns mauve and fades to teal at the floor, at the same
  brightness.
- **Distance colour amber, amount 1, range 120 m.** The whole bank reads amber, because it is about
  300 m away, where the weight is 0.92. A 120 m range on a bank that far saturates it. The render test
  checks the gradient, far over near, on a deep box.

**Matched luminance, measured.** The grain probe reports the medium's mean contribution for the three
colour arms as 0.2692 each: identical to four places, with grain 0.0013 on all three. The frame's
luminance does not know the tints are there.

**Byte identity at the defaults, for both ADRs' shader and packer changes.** See "Byte identity"
below.

## Tests, and how each was shown to fail

- **Luminance** (`test_fog_colour.cpp`).
  - `fogHueMix` preserves luminance over 400 colour, tint and weight triples.
  - The control: more than 200 of those mixes moved the colour.
  - Mixing toward the raw tint fails 399 of 527 assertions.
- **Identity.** At amount 0 the tinted colour is the base colour bit for bit.
- **Weights.** The height weight is 0 at the floor and `amount` two thicknesses above it, and it is
  monotone in between. The distance weight at `range` is `amount * (1 - 1/e)`. A bank ten times as
  dense, with a different contrast and threshold, tints the same point identically.
- **Rows.** Each row reaches its lane, and the lane changes the colour. The height colour lands in
  9.w..11.w and leaves the depth colours untouched. Not packing the height colour's red fails the
  lane case.
- **Parity.** Both weights and the tinted colour agree between the CPU and the GPU (ADR-713's case).
- **Render.** The height and distance arms each warm more than a quarter of the lit pixels, and the
  worst per-pixel luminance change is under 0.5%.
  - Removing the tint from `mediumEmissionAt` fails the warming check (0 of 1,899 pixels).
  - A non-preserving WGSL mix fails the luminance check at 99%.
  - The distance colour tints the far third of a deep box more than the near third.

## Byte identity

Both ADRs' shader and packer changes, with every new row at its default, measured against a
baseline built from `agent/tornado-fog`'s head, which is ADR-705 merged. Each case is a PNG
sequence, every frame hashed, at 960x540, 640x360 or the project's own settings, under
`tools/gpu-lock.sh`. All are identical frame for frame:

| case | frames | sequence hash (of the per-frame hashes) |
|---|---|---|
| Glowmere Valley 2, t = 60..60.25 | 15 | `eccea1fde7f748ec` |
| Tree of Life hero, t = 6 | 3 | `184bcbcc52441b2a` |
| fog lab: Bank / Sphere / Ellipsoid / Box / Capsule / Cylinder, every pre-existing control off its default | 3 each | `cbf3468b` / `a89b2b89` / `ae781da8` / `204fbe62` / `e3147c3a` / `0131bfbe` |
| the review arms' two bases (flow, colour) | 3 each | `8b9974f2` / `fc6f60c1` |

**It was not identical on the first try, and the difference is worth recording.** The first version
of `fogShapeAt` routed the default through `var q = p` and `fogSwellOffset(..., swell == 1)`. That is
the identity in exact arithmetic. It still moved one pixel of the default Bank by one level in
frame 0, deterministically, run after run.
- Reverting the fog shader function by function found it: the tint call was not the cause, the
  detail term was not the cause, and the head of `fogShapeAt` was.
- The default now has its own branch, which evaluates `p - centre` exactly as before ADR-713. The
  bound's new terms are added under branches for the same reason.
- The general form: a "+ 0" the compiler is free to re-associate is not the identity. Identity at a
  default has to be a branch.

## For the owner

- **A colour that is only a hue.** An artist cannot make the top of a bank darker with this. That is
  the point: darker is density, and density is the shape rows' job. If a darker top is wanted, it is
  Height falloff or Glow follows height.
- **The distance range is in metres from the camera.** A bank is usually hundreds of metres away, so
  useful ranges run from about 200 m to a few kilometres.
- **The Tree of Life frame's hue rotation** (`state.progress -> post/grade/hueShift`, amount 1) looks
  like ADR-712's cyan route: a route driven by a signal that is 1.0 nearly always. It is not touched
  here, but it is worth a look.
