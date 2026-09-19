#pragma once

// Path-tracer denoising through Open Image Denoise (ADR-353, spec section 51).
//
// OIDN is OPTIONAL and off by default (`AVGEN_PATHTRACE_DENOISE`). This header compiles and links
// either way: with it off, `denoiseAvailable()` returns false and `denoise()` fails with a message
// saying so. It never silently returns the input, because a denoise that quietly did nothing is
// indistinguishable from one that ran and is exactly the kind of thing section 54 forbids.
//
// The denoiser takes and returns SCENE-LINEAR HDR (spec section 32). It is not a display transform
// and it runs before any colour pipeline.

#include "core/error.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::pathtrace {

// True if this build can denoise at all.
[[nodiscard]] bool denoiseAvailable();

// Version string for the capability report, or "unavailable".
[[nodiscard]] std::string denoiseVersion();

struct DenoiseInput {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    const std::vector<glm::vec3>* color = nullptr;   // required, scene-linear HDR
    // Auxiliary feature buffers. Both optional, both strongly recommended: they are what lets the
    // filter keep an edge it would otherwise smooth away. Passing one without the other is allowed;
    // OIDN uses albedo alone, but normal alone is ignored by the RT filter, and this is documented
    // rather than silently accepted.
    const std::vector<glm::vec3>* albedo = nullptr;
    const std::vector<glm::vec3>* normal = nullptr;
    bool cleanAux = false;  // true when the aux buffers are themselves noise-free
};

// Denoises into `out`, which is resized. Fails rather than approximating if anything is wrong.
[[nodiscard]] Result<void> denoise(const DenoiseInput& in, std::vector<glm::vec3>& out);

} // namespace avgen::pathtrace
