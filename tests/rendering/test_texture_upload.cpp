// The mip chain's sRGB decode table (ADR-481).
//
// `uploadTexture` builds the whole mip chain on the CPU, and the box filter over an sRGB image
// decoded every source texel and re-encoded every destination texel with `std::pow`. Opening the
// multicam film spent 2.1 seconds on 58 textures because of it -- the largest single component of
// a 3.2 second project load.
//
// The decode side is now a 256-entry table. That is the one kind of fast path that can be *proved*
// rather than argued about, because its input is a byte: there are 256 possible answers and this
// test checks all of them against the function the table replaced, written out again here so that
// a future edit to the curve in one place fails rather than diverges silently.
//
// No GPU is needed, which is the point of putting the check here rather than inside an upload:
// a table that is wrong is wrong before any device is opened.

#include "gpu/texture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>

using namespace avgen;

TEST_CASE("the sRGB decode table is the function it replaced, for every byte",
          "[gpu][texture][srgb]") {
    int differing = 0;
    for (int i = 0; i <= 255; ++i) {
        const auto v = static_cast<std::uint8_t>(i);
        const float c = static_cast<float>(v) / 255.0f;
        const float expected =
            c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        const float got = gpu::srgbToLinear8(v);
        if (got != expected) {
            ++differing;
            INFO("byte " << i << ": table " << got << " vs pow " << expected);
            CHECK(got == expected);
        }
    }
    CHECK(differing == 0);

    // The control (ADR-182): the two ends of the curve are not the same number, and the split at
    // 0.04045 is on the linear side of 11/255. A table filled with a constant, or one built from
    // the wrong branch, fails here even if the loop above were somehow vacuous.
    CHECK(gpu::srgbToLinear8(0) == 0.0f);
    CHECK(gpu::srgbToLinear8(255) == 1.0f);
    CHECK(gpu::srgbToLinear8(10) < gpu::srgbToLinear8(11));
    // Below the knee the curve is a straight 1/12.92, and above it is not: at the midpoint an sRGB
    // 128 is about 0.216 in linear light, not 0.502. If this read ~0.5 the table would be the
    // identity and every mip in the project would be filtered in the wrong space.
    CHECK(gpu::srgbToLinear8(128) > 0.21f);
    CHECK(gpu::srgbToLinear8(128) < 0.22f);
}
