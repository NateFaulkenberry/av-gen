#pragma once

// The output transform on the CPU: scene-linear HDR -> display-referred sRGB bytes.
//
// ## Why this exists
//
// `shaders/tonemap.wgsl` was the ONLY implementation of AgX, the ACES fit, Reinhard, Khronos PBR
// Neutral and clamp in this project, and `src/app/render_job.cpp` refused to write a second one --
// on the grounds that a CPU copy would be free to drift from the picture it claims to be of. That
// was the right instinct and it had a consequence nobody had written down: **a path-traced frame
// could not become a video frame without a GPU.** The tracer's whole advantage on a shared machine
// is that it needs no device (ADR-351); routing its frames through the GPU to tone map them spends
// exactly that.
//
// So the operators live here, once, and the drift the old comment feared is answered by a test
// rather than by an absence.
//
// ## The trap this file is built to avoid
//
// ADR-372: the shader's AgX undid its own sRGB encode with `pow(v, 2.2)`, which is not the inverse
// of the piecewise IEC 61966-2-1 curve it re-encodes with -- worth up to **9 code values of 255**,
// and a scene-linear grey five stops under mid rendered 7 where it should have rendered 16. The CPU
// mirror that existed at the time carried **the same wrong inverse**, so the CPU/GPU comparison
// confirmed only that the shader agreed with itself.
//
// A parity test between two copies of one mistake is not a parity test. `tests/unit/test_tonemap.cpp`
// therefore anchors this code against **measured byte values documented from the shipped GPU
// pipeline** (`docs/hdr-lab/README.md`'s grey ramp, `docs/image-formation.md`'s
// scene-linear (8, 1, 0.2)) and not only against the shader -- and carries the historical wrong
// inverse as a control arm, which must produce the historical wrong bytes, so the anchor is proven
// to have the resolution to catch exactly the defect that got through last time.
//
// ## Where the CPU and the GPU genuinely cannot agree
//
// `grain` is `fract(sin(x) * 43758.5453123)`. `sin` of a large argument is not specified to the same
// precision on a GPU as in libm, and the multiply amplifies the difference into a different random
// number. Film grain is therefore **not** bit-comparable between the two paths and the test does not
// pretend otherwise: parity is asserted at grain 0. Everything else -- operator, chroma retention,
// vignette, the encode -- is.

#include "scene/post_settings.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>

namespace avgen::scene {

// `TonemapUniforms` in shaders/tonemap.wgsl, in the same units.
struct TonemapInputs {
    TonemapOperator op = TonemapOperator::AgX;
    float exposure = 1.0f;
    float vignette = 0.0f;
    float grain = 0.0f;
    float seed = 0.0f;
    // 0 = the operator's own highlight rolloff, 1 = hold the source hue.
    float chromaRetention = 0.0f;
};

// The operator alone. Input is scene-linear radiance AFTER exposure; output is display-LINEAR and
// is not yet encoded -- exactly what `agx()` and its siblings return in the shader.
[[nodiscard]] glm::vec3 tonemapOperator(TonemapOperator op, glm::vec3 hdr);

// The shader's `retainChroma`, unchanged.
[[nodiscard]] glm::vec3 retainChroma(glm::vec3 hdr, glm::vec3 mapped, float amount);

// One pixel of `fs_main`, returning the sRGB-ENCODED value the shader writes. `uv` is the shader's
// -- origin top-left, so uv.y increases downward -- and `size` is the HDR target's dimensions, which
// is what the vignette's aspect and the grain's hash are computed against.
[[nodiscard]] glm::vec3 tonemapPixel(const TonemapInputs& in, glm::vec3 hdr, glm::vec2 uv,
                                     glm::vec2 size);

// A whole scene-linear image to 8-bit RGBA, top-left origin, alpha 255. `hdr` is w*h RGB triples in
// row-major order; `rgba` must be w*h*4 bytes. This is the entry point an offline renderer wants:
// it is the difference between a path-traced EXR and a path-traced frame of video.
void tonemapImage(const TonemapInputs& in, std::uint32_t width, std::uint32_t height,
                  std::span<const glm::vec3> hdr, std::span<std::uint8_t> rgba);

// ---- the two halves of "make a byte", which are NOT the same function ---------------------------
//
// These are separate because collapsing them is a mistake that hides: `tonemapPixel` returns an
// ENCODED value (it is the shader's `fs_main`, which ends in `linearToSrgb`), while
// `tonemapOperator` returns a display-LINEAR one (it is `agx()` and its siblings, which do not).
// A single `srgbByte` used for both silently encodes one of them twice or neither.
//
// It is not hypothetical: the first version of this header had one function, documented as applying
// the OETF and not applying it, and the control arm in `test_tonemap.cpp` caught it by producing
// 1 where ADR-372's historical table says 7.

// Quantise an already-ENCODED channel to 8 bits, the way a UNORM render target does.
[[nodiscard]] std::uint8_t quantise8(float encoded);

// Encode one display-LINEAR channel with the sRGB OETF and quantise it. The composition of the two
// above, for a caller holding the output of `tonemapOperator`.
[[nodiscard]] std::uint8_t srgbByte(float displayLinear);

} // namespace avgen::scene
