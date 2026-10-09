// The cinematic water path (ADR-1286): the GPU half.
//   * with every cinematic setting at 0 the surface is byte-identical to a renderer given the shader with the
//     cinematic blocks removed: the path costs a scene that does not ask for it nothing, not even a pass split;
//   * each of the switching settings reaches the picture (the control arm), with no GPU error;
//   * the screen-space reflection puts the lit frame into the water: with it on, the water carries more of the
//     scene's light than the environment reflection alone gives.

#include "support/water_bench.hpp"
#include "support/water_shader_variant.hpp"

#include "rendering/water_renderer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <filesystem>
#include <string>

using namespace avgen;
using namespace avgen::testsupport;
namespace fs = std::filesystem;

namespace {

constexpr const char* kCineBegin = "---- cinematic (ADR-1286) begin ----";
constexpr const char* kCineEnd = "---- cinematic (ADR-1286) end ----";

double meanOver(const gpu::ImageF& img, const std::vector<char>& mask) {
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < mask.size(); ++i) {
        if (!mask[i]) continue;
        sum += 0.2126 * img.rgba[i * 4] + 0.7152 * img.rgba[i * 4 + 1] + 0.0722 * img.rgba[i * 4 + 2];
        ++n;
    }
    return n ? sum / static_cast<double>(n) : 0.0;
}

} // namespace

TEST_CASE("with the cinematic settings at 0 the water is byte-identical to the shader with no cinematic code",
          "[gpu][renderer][water][adr1286][identity]") {
    if (!fs::is_regular_file(qaWaterScene())) {
        SKIP("the water QA scene is not present");
    }
    int blocks = 0;
    const std::string stripped = stripMarkedBlocks(readWaterShaderSource(), kCineBegin, kCineEnd, &blocks);
    INFO(blocks << " marked cinematic blocks removed");
    REQUIRE(blocks >= 5);
    for (const char* name : {"waterSsr", "waterSceneColor", "cineA", "cineGlint", "cineRough"}) {
        INFO(name);
        REQUIRE(stripped.find(name) == std::string::npos);
    }
    const fs::path strippedDir = writeWaterShaderVariant("no-cinematic", stripped);

    WaterBench bench = WaterBench::make(256, 176);
    WaterBench::Arm live = bench.arm();
    WaterBench::Arm none = bench.arm({strippedDir});
    bench.aim({40.0f, 6.0f, -86.0f}, {12.0f, -1.0f, -108.0f});
    bench.hold(12.0);

    scene::WaterSettings& w = bench.scene().waters.at(0).settings;
    const scene::WaterSettings original = w;
    // every shaping setting off its default, every switch at 0: still the plain path
    w.ssrDistance = 33.0f;
    w.ssrSteps = 20.0f;
    w.ssrGain = 3.0f;
    w.absorption = {1.0f, 2.0f, 3.0f};
    w.undulationScale = 0.3f;
    w.streak = 0.5f;
    w.reflectionRays = 5.0f;
    w.reflectionTint2 = {1.0f, 0.0f, 0.5f};
    w.tintPatchScale = 0.2f;
    w.glints = 4.0f;
    REQUIRE_FALSE(w.cinematic());
    const gpu::ImageF withCode = bench.render(live);
    const gpu::ImageF withoutCode = bench.render(none);
    const auto mask = waterMask(withCode, bench.renderDry(live));
    REQUIRE(countMask(mask) > 500);
    CHECK(gpu::hashImage(withCode) == gpu::hashImage(withoutCode));

    // THE CONTROL: each switch reaches the picture
    for (float scene::WaterSettings::*field : {&scene::WaterSettings::ssr, &scene::WaterSettings::transmission,
                                               &scene::WaterSettings::undulation,
                                               &scene::WaterSettings::roughnessVariation,
                                               &scene::WaterSettings::tintPatches}) {
        scene::WaterSettings s = w;
        s.*field = 0.8f;
        w = s;
        const bool differs = gpu::hashImage(bench.render(live)) != gpu::hashImage(withoutCode);
        CHECK(differs);
        w = original;
        w.ssrDistance = 33.0f;
    }
    CHECK(bench.ctx->errorCount() == 0);
}

TEST_CASE("the screen-space reflection puts the lit frame into the water", "[gpu][renderer][water][adr1286]") {
    if (!fs::is_regular_file(qaWaterScene())) {
        SKIP("the water QA scene is not present");
    }
    WaterBench bench = WaterBench::make(256, 176);
    WaterBench::Arm live = bench.arm();
    // low over the water, looking along it: the bank and its far side are in frame to be reflected
    bench.aim({40.0f, 3.0f, -86.0f}, {12.0f, 1.0f, -108.0f});
    bench.hold(12.0);
    scene::WaterSettings& w = bench.scene().waters.at(0).settings;
    w.reflection = 0.0f;    // no environment reflection: anything reflected is the frame
    w.specular = 0.0f;
    w.fresnel = 0.6f;
    const auto mask = waterMask(bench.render(live), bench.renderDry(live));
    REQUIRE(countMask(mask) > 300);
    w.ssr = 0.0f;
    const double off = meanOver(bench.render(live), mask);
    w.ssr = 1.0f;
    w.ssrDistance = 150.0f;
    w.ssrSteps = 96.0f;
    const double on = meanOver(bench.render(live), mask);
    INFO(fmt::format("water luma: ssr off {:.5f}, on {:.5f}", off, on));
    CHECK(on > off * 1.05 + 1e-5);
    CHECK(bench.ctx->errorCount() == 0);
}

TEST_CASE("the packed record selects the cinematic pipeline exactly when the settings are cinematic",
          "[gpu][water][adr1286]") {
    // The renderer decides from the packed uniforms; the settings decide from their fields. They must agree,
    // or a cinematic surface is drawn by a shader that cannot read its settings.
    const auto packedSaysCinematic = [](const rendering::WaterUniforms& u) {
        return u.cineA.x > 0.0f || u.cineA.z > 0.0f || u.cineA.w > 0.0f || u.cineB.x > 0.0f || u.cineC.z > 0.0f;
    };
    scene::WaterSettings w;
    CHECK_FALSE(packedSaysCinematic(rendering::waterUniformsFrom(w, 0.0f, 1.0f, true)));
    for (float scene::WaterSettings::*field : {&scene::WaterSettings::ssr, &scene::WaterSettings::transmission,
                                               &scene::WaterSettings::undulation,
                                               &scene::WaterSettings::roughnessVariation,
                                               &scene::WaterSettings::tintPatches}) {
        scene::WaterSettings s;
        s.*field = 0.5f;
        CHECK(s.cinematic());
        CHECK(packedSaysCinematic(rendering::waterUniformsFrom(s, 0.0f, 1.0f, true)));
    }
    // the others only shape a cinematic surface; on their own they do not switch the path
    scene::WaterSettings s;
    s.streak = 0.5f;
    s.glints = 2.0f;
    s.reflectionRays = 4.0f;
    CHECK_FALSE(s.cinematic());
    CHECK_FALSE(packedSaysCinematic(rendering::waterUniformsFrom(s, 0.0f, 1.0f, true)));
}

TEST_CASE("the plain water source has no cinematic code left in it", "[gpu][water][adr1286]") {
    const std::string src = "a\n// ---- cinematic (ADR-1286) begin ----\nb\n// ---- cinematic (ADR-1286) end ----\nc\n";
    auto plain = rendering::waterSourceWithout(src, "cinematic (ADR-1286)");
    REQUIRE(plain.has_value());
    CHECK(*plain == "a\nc\n");
    auto broken = rendering::waterSourceWithout("a\n// ---- cinematic (ADR-1286) begin ----\nb\n", "cinematic (ADR-1286)");
    CHECK_FALSE(broken.has_value());
}
