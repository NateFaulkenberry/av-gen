# ADR-052: Holding hue in compressed highlights

Status: Accepted

## Context

The world's subject is bioluminescence: narrow-band cyan, teal, violet and magenta light
emitted by living things, authored well above scene white so it reads as light rather than
as paint. Every one of those emitters was arriving on screen white.

The cause is structural, not a bug in any one operator. A per-channel tone curve maps each
channel independently toward 1.0. Feed it a saturated colour above scene white and the
dim channels climb toward the bright ones, so the pixel desaturates exactly in proportion
to how bright — how much like a light source — it is meant to look.

Measured on a flat patch, saturation `(max - min) / max` of the mapped pixel:

| operator | 1x | 5x | 10x | 25x | 50x |
|---|---|---|---|---|---|
| ACES fitted | 0.79 | 0.41 | 0.24 | 0.09 | 0.04 |
| AgX (our default) | 0.45 | 0.23 | 0.14 | 0.07 | 0.03 |
| Khronos PBR Neutral | 0.83 | 0.34 | 0.21 | 0.10 | 0.05 |
| Reinhard (extended) | 0.79 | 0.74 | 0.70 | 0.63 | 0.54 |

A saturated cyan at 25x scene white leaves AgX as `(238, 255, 255)` — white with a hint of
warmth missing. Only Reinhard holds its chroma, because it scales all three channels by one
luminance ratio and so never crosstalks; but it is a weak curve elsewhere and swapping to it
to fix highlights would trade the whole image's look for one class of pixel.

## Decision

Keep the operator choice a look decision, and add a separate stage after it:
`post.chromaRetention`, 0 to 1.

The stage restates the source ratio at the brightness the operator chose. The mapped peak
channel is kept, and the other two are placed where the source colour had them relative to
that peak. The pixel keeps the operator's exposure and the emitter's hue.

In a curve's linear region the two are identical by construction — if `mapped = k * hdr`
then the restated colour is also `k * hdr` — so the stage is a mathematical no-op wherever
the curve is not compressing. It is additionally ramped in over `smoothstep(0.8, 3.0, peak)`
so that an ordinary exposure is untouched and enabling retention cannot restyle the rest of
the image. AgX's own inset desaturation below scene white therefore survives.

At 0.6 the same cyan holds 0.36 saturation at 25x and 0.34 at 50x, against 0.07 and 0.03
without, and still rolls off (0.45 at scene white down to 0.34 at 50x). At 1.0 saturation
becomes flat with intensity, which removes the rolloff altogether and reads like a clamp;
that is the reason the control is continuous rather than a switch.

Default is 0, so no existing scene changes.

## Consequences

Bright narrow-band light stays the colour it was authored. The cost is one `smoothstep`,
two `max`es and a `mix` in the tone-map fragment shader, on a full-screen pass that was
already bandwidth-bound.

This is a departure from a strictly photographic response: a real camera pointed at a very
bright cyan does clip toward white. The world's subject is light that is coloured, and an
image in which every light source is white does not describe it. The control is explicit
and per-scene so the departure is always a decision someone made.

Guarded by `Chroma retention keeps bright narrow-band light coloured` in
`tests/rendering/test_image_formation_gpu.cpp`, which asserts the failure still exists at
retention 0 before asserting the fix works at 0.6.
