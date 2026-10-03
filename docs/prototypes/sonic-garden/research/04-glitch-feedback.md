# Research report 4: real-time glitch, datamosh and feedback techniques

Sonic Garden VFX expansion, deliverable 4 (engineering agent, 2026-10-02). Each technique is described by its
algorithm, its inputs, its cost class at 1080p, and how it can be made deterministic. "FSP" is one full-screen pass
(about 2.07 M invocations at 1080p; an rgba16f read and write is about 33 MB of traffic, roughly 0.1 ms on this GPU
class). Costs are estimates until section 12, where they are mapped to AV Gen and measured later in
`../VFX-ARCHITECTURE.md`.

**Determinism rule used throughout.** Randomness comes only from an integer hash of quantised timeline time, the block
or pixel, and a seed (PCG; Hoskins' hash without sine, [shadertoy 4djSRW](https://www.shadertoy.com/view/4djSRW)).
Never from wall time or a frame counter.

## 1. Datamoshing

**What it is.**
- Codecs store I-frames (complete pictures) and P/B-frames (per-block motion vectors plus a residual against a reference).
- Datamoshing deletes or skips the I-frames, so the decoder applies later motion to the wrong picture.
- The result is either a **smear** (shot A pushed around by shot B's motion) or a **bloom** (a repeated P-frame pours
  pixels out of the moving regions).
- Precedents:
  - Takeshi Murata's *Monster Movie* (2005, [SAAM](https://americanart.si.edu/artwork/monster-movie-86385));
  - the "Welcome to Heartbreak" video ([Motionographer](https://motionographer.com/2009/02/19/tintori-and-nabil-breaking-your-internets/));
  - FFglitch, which edits motion vectors per frame through a JS hook ([ffglitch.org](https://ffglitch.org/2020/07/mv.html)).

**Real-time emulation.** "Freeze the reference, apply the current motion":
- a persistent buffer is advected each frame by the block-quantised motion field;
- it is refreshed from the clean frame on an "I-frame" event, or per block with a hashed probability;
- Keijiro's [KinoDatamosh](https://github.com/keijiro/KinoDatamosh) does exactly this from Unity's motion-vector texture.

Cost and properties:
- **Cost:** 1 FSP, plus a 16x16 tile reduce of the velocity (1/256 of the work).
- **Targets:** 2 ping-pong.
- **Determinism:** it is IIR: the state depends on everything since the last I-frame.

## 2. Feedback buffers

**Algorithm.** `F_t(uv) = blend(C_t(uv), d * g(F_{t-1}(T(uv))))`, where:
- T is the zoom/rotate/translate warp;
- d is the decay;
- g is a colour operation (hue rotation).

This is analogue video feedback, and the core of MilkDrop: a warp mesh moves the previous frame, `decay` is about 0.98,
and `echo_zoom`/`echo_alpha` add a zoomed copy at composite time
([MilkDrop preset authoring](http://wiki.winamp.com/wiki/MilkDrop_Preset_Authoring)). Resolume's feedback effects expose
move, scale and hue shift ([effects](https://resolume.com/support/en/effects)).

**Stability:**
- d < 1, and clamp the result;
- an additive HDR loop with gain >= 1 diverges;
- samples outside the frame must be black, or edge texels streak;
- 8-bit storage stalls dark trails at the round-off floor, so use rgba16f;
- express zoom and rotation per second in aspect-corrected UV, and make decay frame-rate-free with `d = d_sec^dt`.

**Determinism.** A feedback buffer is the textbook IIR filter, which ADR-410 forbids here. The research's resolution is
the **FIR unroll**. For a linear loop with a geometric map T and decay d:

`F_t(uv) = sum_{k=0}^{K-1} d^k * g^k(C_{t-k}(T^k(uv)))`

T^k is closed-form for zoom and rotation (zoom^k, k x angle, k x drift), and so is g^k for a hue rotation. With K = 8-16
it covers decays up to about 0.8 to a 1% residual. It reads K frames of the clean ring and needs no new targets. The
unroll is exact as the definition, not an approximation. A max-blend or a clamp inside the loop does not unroll, so the
FIR is defined with a sum (or a per-tap max, which is still a function of the ring).

## 3. Frame echo, smearing and time displacement

- **EMA** (IIR, one target): not seekable.
- **FIR echo over a ring of N clean frames**, `sum_k w_k ring[t-k]`: seekable by construction. A tap stride s gives a
  strobe trail.
- **Time displacement / slit-scan**: each pixel reads a different age, `out(uv) = ring[t - D(uv)](uv)`.
  - D can be per row (the rolling slit-scan), radial, noise or depth.
  - This is TouchDesigner's [Time Machine TOP](https://docs.derivative.ca/Time_Machine_TOP) (black oldest, white
    newest), descended from PRISMS "tima" (used on *Ghost in the Shell*).
  - For a survey of artworks, see Golan Levin's
    [slit-scan catalogue](http://www.flong.com/archive/texts/lists/slit_scan/index.html).
  - Smooth time comes from blending the two neighbouring layers.
- **Stutter/freeze**: hold `ring[t0]`, or loop `ring[t0 + (t - t0) mod L]`, with t0 from the beat grid.

Cost: 1 FSP with 1-8 taps. Memory: N frames of the ring.

## 4. Pixel sorting

**Kim Asendorf's algorithm** ([ASDFPixelSort](https://github.com/kimasendorf/ASDFPixelSort),
[how-to](http://datamoshing.com/2016/06/16/how-to-glitch-images-using-pixel-sorting/)):
1. per row (then per column), find spans that start where a pixel crosses a brightness threshold and end where it
   crosses back;
2. sort each span by luma;
3. leave pixels outside the spans as they are.

GPU options:
1. **Exact segmented bitonic sort in compute.** One workgroup per row, padded to 2048 entries; the composite key is
   `(spanId << 16) | luma`. The keys fill WebGPU's default 16 KB of workgroup memory.
   - That is 66 compare-exchange stages per row, about 0.5-1.5 ms per axis at 1080p.
   - Columns need a transpose ([Zucconi, GPU sorting](https://www.alanzucconi.com/2017/12/13/gpu-sorting-2/)).
2. **Progressive odd-even transposition sort**, k ping-pong passes per frame, so that spans melt into order over time
   ([shadertoy XdcGWf](https://www.shadertoy.com/view/XdcGWf),
   [Severien](https://tsev.dev/posts/2017-08-17-sorting-pixels-with-webgl/),
   [ciphrd](https://ciphrd.com/2020/04/08/pixel-sorting-on-shader-using-well-crafted-sorting-filters-glsl/)).
   It is stateful, which makes it the worst fit for seek-equals-play.
3. **The stateless "fake" sort.** Inside the threshold mask, march along the sort direction for up to 32 taps (stopping
   at the mask's edge) and keep the brightest (or darkest) sample, or the span's start colour. That is 1 FSP and about
   0.2-0.4 ms, and it is trivially seekable. It gives most of the look: the streaks of a sorted span running from its
   bright end.

## 5. RGB split and lens

- **Directional split**: `r = S(uv + o)`, `g = S(uv)`, `b = S(uv - o)`.
- **Radial (lateral CA)**: `o = (uv - c) k |uv - c|^n`.
- **Spectral**: 6-16 taps along the offset, weighted by a wavelength ramp and normalised so white stays white. This
  gives rainbow fringes instead of three ghost copies.
- **Brown-Conrady radial distortion**: `p' = p (1 + k1 r^2 + k2 r^4)`. Per channel, a slightly different k1 gives CA
  ([Distortion (optics)](https://en.wikipedia.org/wiki/Distortion_(optics))).

All of these are stateless. Each is 1 FSP with 3-16 taps, and can be folded into an existing pass. The RGB-split pulse
is the canonical kick-transient effect.

## 6. Block displacement and corruption

- **Block selection per time quantum**: `q = floor(t * rate)`, `b = floor(uv * grid)`, `h = hash(b, q, seed)`. When
  `h < amount`, offset the block's UV by a hashed vector, or swap in another block, or swap a channel. Keying on q
  rather than the frame holds the glitch for one quantum and makes it seekable.
- **Line tears**: per row band, `uv.x += step(hash(row, q), amount) * (hash(row, q + 1) - 0.5) * w`.
- **Macroblock quantisation**: chroma from a 16x16 block average (4:2:0 at macroblock scale) and luma quantised in 8x8
  blocks. A true DCT/IDCT needs a compute workgroup, so defer it.
- **Bit-crush / posterise**: `floor(c L)/L`, per channel.
- **Interlace**: odd rows from `ring[t-1]`.
- **VHS tracking**: a band at a hashed y with a noise offset, chroma blurred and delayed, and a head-switch strip.

Everything here except the true DCT is 1 FSP and stateless.

## 7. Scanlines and CRT

- **Scanlines**: `1 - s (0.5 + 0.5 cos(2 pi y_src))`, with a beam-width profile ([crt-lottes](https://docs.libretro.com/shader/crt/)).
- **Aperture grille**: a 3-pixel RGB stripe. Slot and dot masks need 1440p-4K to resolve
  ([crt-royale](https://docs.libretro.com/shader/crt_royale/)).

**Resolution matters.**
- The mask period must be in **output pixels** (an integer, from `@builtin(position)`), or it moires.
- The scanline count should be in **UV** (a virtual source height of 240-480 lines), so the look matches at every output
  size.
- An offline 4K render must scale the mask, or it will not match the 1080p preview.
- The mask darkens by 30-50%, so feed bloom before it and compensate the gain.

## 8. Pixelation, posterisation, dithering and hue

- **Mosaic**: `uv' = (floor(uv res / B) + 0.5) B / res`. Animate B in powers of two.
- **Ordered (Bayer) dithering**, indexed by output pixel or mosaic cell, never by UV
  ([Ordered dithering](https://en.wikipedia.org/wiki/Ordered_dithering)).
- **Palette quantisation** to the nearest of K colours in OKLab.
- **Hue rotation in OKLCh**, not HSV: HSV changes perceived lightness, so a yellow rotated to blue reads as a flash
  ([Ottosson, Oklab](https://bottosson.github.io/posts/oklab/)).

All are 1 FSP and stateless.

## 9. Screen-space displacement

- **Noise warp**: `uv += amp (fbm(uv f + t s) - 0.5)`, with t the timeline time.
- **Shockwave ring** ([Savakis](https://halisavakis.com/my-take-on-shaders-shockwave-effect/)):
  - radius `R = speed (t - t0)`, band `x = (r - R)/thickness`, weight `1 - x^2` inside the band, falloff `exp(-(t - t0)/tau)`;
  - offset along the radial direction by `w x strength falloff`;
  - a per-channel strength gives a CA fringe;
  - several overlapping onsets sum from a fixed array of the last K.
  
  Seekable when t0 is a pure function of the timeline.
- **Heat haze**: small, high-frequency noise warp masked by depth or region.
- **Radial (zoom) blur**: M taps toward a centre.
- **Directional blur**: M taps along a vector, with a hashed start per pixel.

Warps go on the HDR target before bloom, so the glow follows them.

## 10. Motion-vector effects

- **With a velocity buffer:**
  - McGuire's motion blur: TileMax/NeighborMax plus a gather
    ([paper](https://casual-effects.com/research/McGuire2012Blur/McGuire12Blur.pdf));
  - smear along `-v gain`;
  - velocity magnitude as a mask that glitches only what moves.
- **Without one:** frame difference as an undirected mask; block matching (SAD over 8x8 blocks, +-8 px search) in
  compute; Lucas-Kanade.

All of these are functions of frames t and t-1: seekable once the ring holds t-1.

## 11. Musical use

Rosa Menkman's [Glitch Studies Manifesto](https://www.are.na/block/2069419) argues that a glitch works by exposing the
medium's protocol. Effects read as glitch when they are **brief and structural**, not constant. This matches the
Liminal art pass 3 addendum: "do not turn the entire video into a glitch effect".

**Transient effects.** These have an attack of 0-5 ms and a decay of 50-200 ms:
- the RGB split pulse and the shockwave ring, on the kick;
- block displacement and line tear, on the snare (a sixteenth-note quantum);
- mosaic and bit-crush steps, on hats and fills;
- freeze and stutter, on a one-beat fill.

**Sustained effects.** These run on levels with 0.5-4 s smoothing:
- feedback zoom/rotation, on pads and risers;
- echo trails, on reverb tails;
- slit-scan, on ambient sections;
- noise warp and haze, as texture;
- mosh re-keying, at section boundaries.

**How VJ tools parameterise them.** One normalised `amount` (0..1) with an exact-zero bypass, and 2-4 shape controls:
direction, block size or quantum, decay, mix. Any parameter can be bound to audio (Resolume, TouchDesigner, Synesthesia,
whose docs recommend "hits" for impulsive effects and "levels" for continuous ones).

## 12. What AV Gen already has (verified 2026-10-02)

| technique | status | where |
|---|---|---|
| radial chromatic aberration, barrel/pincushion | yes (`post/lens/{chromaticAberration,distortion}`) | `fs_lens`, `shaders/post.wgsl` |
| hue rotation | yes, YIQ (`post/grade/hueShift`) | composite |
| saturation, contrast, lift/gamma/gain, exposure | yes | composite, meter |
| vignette, grain | yes (tonemap, grain hashed on `frameNonce`) | `shaders/tonemap.wgsl` |
| velocity motion blur | yes (`post/motionBlur/*`) | post pass 3 |
| frame echo (FIR) | yes (`temporal/echo/*`) | ADR-410 |
| block data mosh + R/B shift (FIR over the clean ring) | yes (`temporal/mosh/*`) | ADR-1049 |
| world-space distortion proxies (heat haze, lenses) | yes | `DistortionRenderer` |
| velocity target (RG16F), depth, emission, identifiers | yes, at scene resolution | `scene_targets.hpp` |
| a ring of clean past frames (RG11B10, 0.25-1x, up to 32) | yes | `temporal_history.*` |
| feedback, slit-scan, stutter, pixel sort, block displacement (stateless), line tear, scanlines, mosaic, posterise/dither, shockwave, directional/radial blur | **no** | |
| an IIR feedback target | exists only as shader-layer `PERSISTENT` targets, which are not seek-safe | `shader_layer.cpp` |

Three gaps in the existing temporal family matter for anything built on the ring:
1. **There is no K-frame pre-roll** (ADR-410's warm-up was designed and never built). A seek, or an offline `--range`
   starting mid-timeline, starts with an empty ring, and taps clamp to `framesValid`. Ring effects are deterministic
   (two renders of a range agree) but are cold for their first K frames after a seek.
2. **The temporal passes have no GPU timer** (`TemporalEffects::setTimeline` is never called), so their cost is
   charged to `post/meter`.
3. **The ring is half resolution** at the realtime tier, so ring effects soften what they show.

## Recommendations

**Ground rules:**
1. **Off is byte-identical by structure.** When the effect's amount is exactly zero, skip the pass (do not run it at
   zero strength: a resample at uv + 0 is not guaranteed bit-exact).
2. **Seek equals play.** Randomness comes from hashes of quantised timeline time. Envelopes come from the bus (whose
   producers already obey seek == play) rather than frame-to-frame state in the shader.
3. **Placement:**
   - geometric warps (shockwave, block displacement, tear, sort) go on HDR before bloom, beside `lens`;
   - feedback and slit-scan go in the temporal family (FIR over the clean ring);
   - display effects (scanlines, mosaic, posterise/dither) go after the composite, before FXAA.

**Ranked shortlist for AV Gen:**

| # | effect | passes / targets | seek | musical role |
|---|---|---|---|---|
| 1 | shockwave ring with a CA fringe | 1 FSP, no targets | stateless; the ring's age comes from the bus | kick, drop |
| 2 | block displacement + line tear + channel swap (stateless) | 1 FSP, no targets | hash on a quantum | snare, fills |
| 3 | feedback zoom/rotate/hue as an FIR unroll over the ring | 1 FSP, K taps, ring only | FIR (ADR-410) | pads, builds |
| 4 | slit-scan time displacement + stutter | 1 FSP, 1-2 taps, ring | FIR | ambient, fills |
| 5 | fake pixel sort (masked directional max-smear) | 1 FSP, <= 32 taps | stateless | accent, bridge |
| 6 | RGB split, directional or spectral (the existing radial CA is fixed-strength and centred) | folded in | stateless | kick |
| 7 | scanlines + mosaic + Bayer posterise (one display pass) | 1 FSP | stateless | section colour |
| 8 | radial/directional blur | 1 FSP, 8-16 taps | stateless (hashed jitter) | risers, impacts |

**Deferred:**
- the exact bitonic sort (compute, 0.5-1.5 ms per axis);
- the true 8x8 DCT;
- the progressive odd-even sort (stateful);
- a motion-vector datamosh (an IIR advected buffer). The existing FIR mosh plus the velocity-masked displacement cover
  its purpose without breaking ADR-410.
