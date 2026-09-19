// The CPU output transform (`src/scene/tonemap.cpp`), anchored against measurements rather than
// against itself.
//
// ADR-372 is the reason this file is shaped the way it is. The shader's AgX undid its own sRGB
// encode with `pow(v, 2.2)`, which is not the inverse of the piecewise curve it re-encodes with,
// and the CPU mirror that existed at the time carried THE SAME wrong inverse. So the CPU/GPU
// comparison confirmed the shader agreed with itself and never that it was right. Nine code values
// of 255 went through every shipped frame for as long as that pair existed.
//
// The defence is a third party. These tests check the port against BYTE VALUES MEASURED FROM THE
// SHIPPED GPU PIPELINE AND WRITTEN DOWN -- `docs/hdr-lab/README.md`'s grey ramp and
// `docs/image-formation.md`'s scene-linear (8, 1, 0.2) -- neither of which was derived from this
// code. And the last test carries the historical wrong inverse as a control arm and requires it to
// produce the historical WRONG bytes, which is what proves the anchor has the resolution to catch
// exactly the class of defect that got through last time.

#include "scene/tonemap.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

// Scene-linear grey -> the byte the operator writes, through the whole per-pixel stage with every
// position-dependent effect off, which is the configuration the documented tables were measured in.
int greyByte(scene::TonemapOperator op, float linear) {
    scene::TonemapInputs in;
    in.op = op;
    const glm::vec3 encoded =
        scene::tonemapPixel(in, glm::vec3(linear), glm::vec2(0.5f), glm::vec2(256.0f, 256.0f));
    return scene::quantise8(encoded.r);
}

glm::ivec3 colourBytes(scene::TonemapOperator op, glm::vec3 linear) {
    scene::TonemapInputs in;
    in.op = op;
    const glm::vec3 e =
        scene::tonemapPixel(in, linear, glm::vec2(0.5f), glm::vec2(256.0f, 256.0f));
    return {scene::quantise8(e.r), scene::quantise8(e.g), scene::quantise8(e.b)};
}

} // namespace

TEST_CASE("The CPU AgX reproduces the grey ramp measured off the GPU", "[tonemap][color]") {
    // docs/hdr-lab/README.md section 2, "AgX, the default, chromaRetention 0, exposure scale 1,
    // bloom off -- the operator alone". The left column is what the tonemap pass was handed, read
    // back from the HDR target; the right is the byte it wrote. Measured on the device, published,
    // and not derived from anything in src/scene/tonemap.cpp.
    struct Row {
        float intoTonemap;
        int displayByte;
    };
    const Row ramp[] = {{0.1798f, 127},  {0.4998f, 173},   {0.9995f, 201}, {1.9990f, 223},
                        {3.9980f, 238},  {15.9922f, 254},  {49.9688f, 255}};
    for (const Row& r : ramp) {
        const int got = greyByte(scene::TonemapOperator::AgX, r.intoTonemap);
        INFO("scene-linear " << r.intoTonemap << ": documented " << r.displayByte << ", CPU " << got);
        CHECK(got == r.displayByte);
    }
}

TEST_CASE("The CPU operators reproduce the published saturated-highlight values", "[tonemap][color]") {
    // docs/image-formation.md: the reason AgX is the default (ADR-039) is what the two operators do
    // to scene-linear (8, 1, 0.2) -- the ACES fit crushes the blue and skews the hue.
    const glm::ivec3 agx = colourBytes(scene::TonemapOperator::AgX, glm::vec3(8.0f, 1.0f, 0.2f));
    const glm::ivec3 aces = colourBytes(scene::TonemapOperator::AcesFitted, glm::vec3(8.0f, 1.0f, 0.2f));
    INFO("agx " << agx.r << "," << agx.g << "," << agx.b << "  aces " << aces.r << "," << aces.g
                << "," << aces.b);
    CHECK(agx == glm::ivec3(255, 209, 174));
    CHECK(aces == glm::ivec3(255, 232, 149));
    // And the property the documentation draws from those numbers, so a future re-tune that moved
    // both rows together would still have to keep the reason the default is the default.
    CHECK(agx.b > aces.b + 10);
}

TEST_CASE("Every operator is monotonic and lands inside the display range", "[tonemap][color]") {
    // Cheap, but it is the arm that catches a transcription error in an operator nobody has
    // published a table for: Reinhard and PBR Neutral have no documented ramp to check against.
    for (const auto op : {scene::TonemapOperator::AcesFitted, scene::TonemapOperator::AgX,
                          scene::TonemapOperator::Reinhard, scene::TonemapOperator::PbrNeutral,
                          scene::TonemapOperator::Clamp}) {
        int previous = -1;
        for (const float grey : {0.0f, 0.01f, 0.05f, 0.18f, 0.5f, 1.0f, 2.0f, 8.0f, 64.0f}) {
            const int b = greyByte(op, grey);
            INFO("operator " << static_cast<int>(op) << " at " << grey << " -> " << b);
            CHECK(b >= 0);
            CHECK(b <= 255);
            CHECK(b >= previous);   // brighter in never darker out
            previous = b;
        }
        CHECK(greyByte(op, 0.0f) == 0);     // black stays black under every curve
        CHECK(greyByte(op, 1000.0f) == 255); // and everything saturates eventually
    }
}

TEST_CASE("Exposure, chroma retention and the vignette do what the shader does", "[tonemap][color]") {
    SECTION("exposure multiplies before the curve, not after") {
        // If it were applied after, doubling exposure on a mid grey would double the BYTE, which is
        // 254 rather than the 158 a stop up the curve actually gives.
        scene::TonemapInputs in;
        in.op = scene::TonemapOperator::AgX;
        in.exposure = 2.0f;
        const glm::vec3 e = scene::tonemapPixel(in, glm::vec3(0.18f), glm::vec2(0.5f), glm::vec2(256.0f));
        CHECK(static_cast<int>(scene::quantise8(e.r)) == greyByte(scene::TonemapOperator::AgX, 0.36f));
    }

    SECTION("chroma retention holds the source hue in a compressed highlight, and only there") {
        const glm::vec3 hot(8.0f, 1.0f, 0.2f);
        scene::TonemapInputs off;
        off.op = scene::TonemapOperator::AgX;
        scene::TonemapInputs on = off;
        on.chromaRetention = 1.0f;
        const glm::vec3 a = scene::tonemapPixel(off, hot, glm::vec2(0.5f), glm::vec2(256.0f));
        const glm::vec3 b = scene::tonemapPixel(on, hot, glm::vec2(0.5f), glm::vec2(256.0f));
        INFO("off " << a.b << " on " << b.b);
        CHECK(b.b < a.b);  // the blue is pulled back toward the source ratio

        // THE CONTROL: below scene white the operator's own look stands, so the same setting must
        // change nothing. Without this the test above would pass for a retainChroma that ignored
        // its smoothstep and applied everywhere.
        const glm::vec3 dim(0.2f, 0.1f, 0.02f);
        const glm::vec3 c = scene::tonemapPixel(off, dim, glm::vec2(0.5f), glm::vec2(256.0f));
        const glm::vec3 d = scene::tonemapPixel(on, dim, glm::vec2(0.5f), glm::vec2(256.0f));
        CHECK_THAT(d.r, WithinAbs(c.r, 1e-6f));
        CHECK_THAT(d.g, WithinAbs(c.g, 1e-6f));
        CHECK_THAT(d.b, WithinAbs(c.b, 1e-6f));
    }

    SECTION("the vignette darkens the corner and leaves the centre alone") {
        scene::TonemapInputs in;
        in.op = scene::TonemapOperator::AgX;
        in.vignette = 0.8f;
        const glm::vec2 size(1920.0f, 1080.0f);
        const glm::vec3 centre = scene::tonemapPixel(in, glm::vec3(0.5f), glm::vec2(0.5f, 0.5f), size);
        const glm::vec3 corner = scene::tonemapPixel(in, glm::vec3(0.5f), glm::vec2(0.0f, 0.0f), size);
        CHECK(corner.r < centre.r);
        scene::TonemapInputs plain = in;
        plain.vignette = 0.0f;
        const glm::vec3 unvignetted = scene::tonemapPixel(plain, glm::vec3(0.5f), glm::vec2(0.5f, 0.5f), size);
        // The centre is inside smoothstep's lower edge, so it is untouched -- which is also the
        // control on the corner assertion: if the vignette applied flatly, this would fail.
        CHECK_THAT(centre.r, WithinAbs(unvignetted.r, 1e-6f));
    }
}

TEST_CASE("tonemapImage writes the same pixels the per-pixel path does", "[tonemap][color]") {
    // The bulk entry point is what an offline renderer calls, so it has to agree with the function
    // every assertion above is written against, including the uv it derives per pixel.
    constexpr std::uint32_t kW = 7, kH = 5;   // deliberately odd, so a row-stride error shows
    std::vector<glm::vec3> hdr(kW * kH);
    for (std::uint32_t i = 0; i < hdr.size(); ++i) {
        hdr[i] = glm::vec3(0.02f * static_cast<float>(i), 0.5f, 3.0f);
    }
    scene::TonemapInputs in;
    in.op = scene::TonemapOperator::AgX;
    in.vignette = 0.5f;   // on, so the per-pixel uv actually matters

    std::vector<std::uint8_t> rgba(kW * kH * 4);
    scene::tonemapImage(in, kW, kH, hdr, rgba);

    const glm::vec2 size(static_cast<float>(kW), static_cast<float>(kH));
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * kW + x;
            const glm::vec2 uv((static_cast<float>(x) + 0.5f) / size.x,
                               (static_cast<float>(y) + 0.5f) / size.y);
            const glm::vec3 e = scene::tonemapPixel(in, hdr[i], uv, size);
            INFO("pixel " << x << "," << y);
            CHECK(static_cast<int>(rgba[i * 4 + 0]) == static_cast<int>(scene::quantise8(e.r)));
            CHECK(static_cast<int>(rgba[i * 4 + 1]) == static_cast<int>(scene::quantise8(e.g)));
            CHECK(static_cast<int>(rgba[i * 4 + 2]) == static_cast<int>(scene::quantise8(e.b)));
            CHECK(static_cast<int>(rgba[i * 4 + 3]) == 255);
        }
    }
    // The vignette is on, so the corner and the centre must actually differ -- otherwise the loop
    // above is comparing two copies of a function that ignores uv.
    CHECK(rgba[0] != rgba[((kH / 2) * kW + kW / 2) * 4]);
}

TEST_CASE("The anchor can detect the inverse that was wrong for a year", "[tonemap][color][control]") {
    // ADR-182, and the specific failure ADR-372 records: a parity test between two copies of one
    // mistake is not a parity test. This reimplements AgX with the HISTORICAL WRONG INVERSE --
    // `pow(v, 2.2)` instead of the exact piecewise sRGB decode -- and requires it to produce
    // ADR-372's OLD byte column. If the tables above could not tell the two apart, they would be
    // measuring nothing, and this is the arm that says they can.
    const auto agxWrongInverse = [](float grey) {
        const glm::mat3 inset(glm::vec3(0.842479062253094f, 0.0423282422610123f, 0.0423756549057051f),
                              glm::vec3(0.0784335999999992f, 0.878468636469772f, 0.0784336f),
                              glm::vec3(0.0792237451477643f, 0.0791661274605434f, 0.879142973793104f));
        const glm::mat3 outset(glm::vec3(1.19687900512017f, -0.0528968517574562f, -0.0529716355144438f),
                               glm::vec3(-0.0980208811401368f, 1.15190312990417f, -0.0980434501171241f),
                               glm::vec3(-0.0990297440797205f, -0.0989611768448433f, 1.15107367264116f));
        const float minEv = -12.47393f, maxEv = 4.026069f;
        glm::vec3 v = inset * glm::vec3(grey);
        v = glm::clamp(glm::log2(glm::max(v, glm::vec3(1e-10f))), glm::vec3(minEv), glm::vec3(maxEv));
        v = (v - minEv) / (maxEv - minEv);
        const glm::vec3 x2 = v * v, x4 = x2 * x2;
        v = 15.5f * x4 * x2 - 40.14f * x4 * v + 31.96f * x4 - 6.868f * x2 * v + 0.4298f * x2 +
            0.1191f * v - 0.00232f;
        v = outset * v;
        return scene::srgbByte(glm::pow(glm::clamp(v, glm::vec3(0.0f), glm::vec3(1.0f)),
                                        glm::vec3(2.2f)).x);
    };

    // ADR-372's own table, both columns. Left is what shipped before the fix; right is what ships.
    struct Row {
        float grey;
        int wasWrong;
        int isRight;
    };
    const Row table[] = {{0.00562f, 7, 16}, {0.01125f, 21, 27}, {0.02250f, 40, 45},
                         {0.04500f, 66, 68}, {0.09000f, 95, 96}, {0.18000f, 128, 127},
                         {0.36000f, 160, 158}};
    for (const Row& r : table) {
        const int wrong = static_cast<int>(agxWrongInverse(r.grey));
        const int right = greyByte(scene::TonemapOperator::AgX, r.grey);
        INFO("scene-linear " << r.grey << ": wrong-inverse " << wrong << " (ADR-372 says "
                             << r.wasWrong << "), this port " << right << " (says " << r.isRight << ")");
        CHECK(wrong == r.wasWrong);   // the control reproduces the historical defect exactly
        CHECK(right == r.isRight);    // and the shipping code does not
    }

    // The headline number, stated as its own assertion because it is the one a person remembers:
    // a grey five stops under mid rendered 7 where it should have rendered 16.
    CHECK(static_cast<int>(agxWrongInverse(0.00562f)) == 7);
    CHECK(greyByte(scene::TonemapOperator::AgX, 0.00562f) == 16);
}
