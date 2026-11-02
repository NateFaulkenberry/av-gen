# ADR-1936: RGB temporal displacement per gated slot

- Status: Accepted (CHRONOS Gate S1, 2026-11-02), scene/chronos.
- Extends: ADR-1934, the gated slit.
- Code:
  - `SlitSettings::Slot::rgb` (`temporal/slit/slot<k>/rgb`, in frames, written to JSON only when gating is used), in
    `src/scene/temporal_settings.{hpp,cpp}`;
  - `rgbSplit` in `shaders/temporal.wgsl`;
  - the gate's table grows a third vec4 per slot (784 bytes), in `src/rendering/temporal_effects.cpp`.
- Tests:
  - `[gpu][temporal][adr1936]` in `tests/rendering/test_temporal_gate_gpu.cpp`;
  - the slot round trip and the parameters, in `tests/unit/test_temporal_gate.cpp`.

## Context

Brief §5E asks for "different historical moments for individual color channels". It asks for this on the figures
only, not on the whole frame (§5G: "meaningful variation in how the effect operates").

## Decision

1. **Each slot has an `rgb` offset in frames.**
   - Wherever the gated sampler shows a slot's image, green comes from `rgb` frames further back and blue from twice
     that. This covers the object now, its trail and its fracture strips.
   - Red stays at the moment the pixel already shows.
2. **The split is gated too.** Each channel is read only where the stored texel at its own moment held the same slot;
   otherwise that channel keeps the present. An ungated object, or the background, is never split.
3. **The bound is unchanged.** The offset is clamped to half the span, so no read goes past what the ring declared.

## Consequences

- **Complementary fringes.** A pale object splits into fringes of complementary colours, green and cyan among them.
  CHRONOS keeps them inside its palette with a scene-side guard in its Late lens layer. The engine stays neutral.
- **Cost.** At most two more point reads and two filtered reads per gated pixel.
- **FIR.** A render of a range lands where a full render does.
