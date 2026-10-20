// Investigation probe (hidden): renders the water QA frame with a fresh renderer again and again and reports any
// render that is not bit-identical to the first. AVGEN_PROBE_OFF=a,b turns pass arms off (SceneRenderer::setPassArm),
// AVGEN_PROBE_N sets the repeat count, AVGEN_PROBE_DUMP a directory for the odd frames.

#include "support/water_bench.hpp"

#include "gpu/readback.hpp"
#include "rendering/scene_renderer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

using namespace avgen;
using namespace avgen::testsupport;

TEST_CASE("probe: the water QA frame repeats bit for bit across fresh renderers", "[.probe-repeat][gpu]") {
    if (!std::filesystem::is_regular_file(qaWaterScene())) {
        SKIP("the water QA scene is not present");
    }
    WaterBench bench = WaterBench::make(256, 176);
    bench.aim({40.0f, 6.0f, -86.0f}, {12.0f, -1.0f, -108.0f});
    bench.hold(3.0);
    bench.scene().waters.at(0).settings.cascade = 1.5f;
    auto toggles = waterQuantitativeToggles();
    if (const char* off = std::getenv("AVGEN_PROBE_OFF")) {
        std::stringstream list(off);
        std::string token;
        while (std::getline(list, token, ',')) {
            if (!token.empty()) {
                REQUIRE(rendering::SceneRenderer::setPassArm(toggles, token, false));
            }
        }
    }
    const int n = std::getenv("AVGEN_PROBE_N") != nullptr ? std::atoi(std::getenv("AVGEN_PROBE_N")) : 20;
    std::uint64_t first = 0;
    gpu::ImageF reference;
    int odd = 0;
    for (int i = 0; i < n; ++i) {
        // As the identity test does: three renderers alive on the device, rendered in turn; the third is compared.
        WaterBench::Arm a = bench.arm();
        WaterBench::Arm b = bench.arm();
        WaterBench::Arm arm = bench.arm();
        a.renderer->setPassToggles(toggles);
        b.renderer->setPassToggles(toggles);
        arm.renderer->setPassToggles(toggles);
        static_cast<void>(bench.render(a));
        static_cast<void>(bench.render(b));
        const gpu::ImageF img = bench.render(arm);
        const std::uint64_t h = gpu::hashImage(img);
        if (i == 0) {
            WARN("PROBE first " << h);
            first = h;
            reference = img;
            continue;
        }
        if (h != first) {
            ++odd;
            std::size_t differing = 0;
            for (std::size_t p = 0; p < img.rgba.size(); ++p) {
                differing += img.rgba[p] != reference.rgba[p] ? 1 : 0;
            }
            WARN("ODD render " << i << ": " << differing << " channel values differ");
            if (const char* dir = std::getenv("AVGEN_PROBE_DUMP")) {
                const std::string base = std::string(dir) + "/" + std::to_string(::getpid()) + "-" + std::to_string(i);
                std::ofstream(base + "-odd.f32", std::ios::binary)
                    .write(reinterpret_cast<const char*>(img.rgba.data()),
                           static_cast<std::streamsize>(img.rgba.size() * sizeof(float)));
                std::ofstream(base + "-ref.f32", std::ios::binary)
                    .write(reinterpret_cast<const char*>(reference.rgba.data()),
                           static_cast<std::streamsize>(reference.rgba.size() * sizeof(float)));
            }
        }
    }
    WARN("PROBE renders " << n << " odd " << odd);
}
