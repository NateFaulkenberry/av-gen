// ADR-938 on the GPU: a scatter layer's sway controls reach the pixels.
//
// test_scatter_sway_controls.cpp proves the parameters land on every part of their layer and change
// the species model's gains. This is the last step: a frame of ferns in a breeze, rendered through the
// composition exactly as the engine renders one, changes when "sway at the tip" is raised -- and does
// not change when the same edit is made with the wind off, which is the control that the difference is
// the sway and not a replant, a re-light or a history effect. "catches the wind" at 0 is the other end:
// the ferns stand still between two seconds that differ with the layer as authored.

#include "assets/asset_registry.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kW = 480;
constexpr std::uint32_t kH = 300;

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

std::size_t differingPixels(const gpu::Image8& a, const gpu::Image8& b) {
    REQUIRE(a.rgba.size() == b.rgba.size());
    std::size_t differ = 0;
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        if (a.rgba[i] != b.rgba[i] || a.rgba[i + 1] != b.rgba[i + 1] || a.rgba[i + 2] != b.rgba[i + 2]) {
            ++differ;
        }
    }
    return differ;
}

// With AVGEN_EFFECT_DUMP set: both frames and their difference (x8), for a person to look at.
void dump(const std::string& name, const gpu::Image8& a, const gpu::Image8& b) {
    const char* dir = std::getenv("AVGEN_EFFECT_DUMP");
    if (dir == nullptr || *dir == '\0') {
        return;
    }
    std::vector<std::uint8_t> diff(a.rgba.size(), 255);
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        for (std::size_t c = 0; c < 3; ++c) {
            const int d = std::abs(static_cast<int>(a.rgba[i + c]) - static_cast<int>(b.rgba[i + c]));
            diff[i + c] = static_cast<std::uint8_t>(std::min(255, d * 8));
        }
    }
    const fs::path out(dir);
    std::error_code ec;
    fs::create_directories(out, ec);
    static_cast<void>(assets::writePng(out / ("sway-" + name + "-a.png"), kW, kH, a.rgba));
    static_cast<void>(assets::writePng(out / ("sway-" + name + "-b.png"), kW, kH, b.rgba));
    static_cast<void>(assets::writePng(out / ("sway-" + name + "-diff.png"), kW, kH, diff));
}

// Ferns on a flat meadow in a breeze, their response the GV3 fan plants' before gv3-look retuned them
// (tip 0.08, catching 0.65 of the wind, stiffness 2.4 and a 1.2 kg tip). No skybox: its stars twinkle
// with time, and the "stands still" arm compares two seconds.
struct Meadow {
    fs::path path;
    assets::AssetRegistry registry{testsupport::processTempDir()};
    std::unique_ptr<scene::Composition> comp;
    params::ParameterSet params;
    params::Modulator modulator;

    explicit Meadow(const fs::path& fern) {
        std::string text = R"({
          "format": "avgen-scene", "version": 1, "name": "sway-gpu",
          "camera": { "mode": 1, "position": [0.0, 2.0, 7.5], "target": [0.0, 0.5, 0.0], "fov": 42.0 },
          "lights": [ { "name": "key", "type": "directional", "role": "key", "direction": [0.35, -0.8, -0.45],
                        "color": [1.0, 0.95, 0.85], "intensity": 2.4 } ],
          "environment": { "background": [0.02, 0.03, 0.05], "skybox": false },
          "wind": { "enabled": true, "direction": 0.0, "speed": 1.2, "gustAmount": 0.8, "gustScale": 24.0,
                    "gustSpeed": 5.0, "turbulence": 0.35, "regionAmount": 0.3 },
          "nodes": [
            { "name": "valley", "kind": "terrain",
              "world": { "name": "flat", "size": [24, 24], "layers": [], "features": [] },
              "terrain": { "chunkSize": 12.0, "resolution": 8, "lodLevels": 1, "viewDistance": 120.0 },
              "scatter": [
                { "name": "ferns", "asset": "@FERN@", "height": 1.3, "seed": 7,
                  "densities": { "marsh": 0.9, "meadow": 0.9, "forest": 0.9, "scree": 0.9, "rim": 0.9 },
                  "motion": { "stiffness": 2.4, "mass": 1.2, "damping": 0.9, "windSensitivity": 0.65,
                              "bendLimit": 0.4, "tipAmplitude": 0.08, "gustResponse": 1.2 } } ] }
          ]
        })";
        text.replace(text.find("@FERN@"), 6, fern.string());
        path = testsupport::processTempDir() / "avgen_sway_gpu.json";
        std::ofstream(path) << text;
        auto loaded = scene::Composition::loadFile(path.filename(), registry);
        if (!loaded) {
            FAIL(loaded.error().message);
        }
        comp = std::move(*loaded);
        comp->attach(params, modulator);
        comp->setViewport(kW, kH);
    }
    ~Meadow() { std::filesystem::remove(path); }

    params::Parameter<float>* sway(const std::string& leaf) {
        return params.findAs<float>("nodes/valley/scatter/ferns/sway/" + leaf);
    }
    // One frame as the engine makes one: finals from bases, the composition applies, then a render
    // with no history carried in from the frame before. `frameIndex` is the frame's jitter and noise
    // seed; comparing two SECONDS for motion holds it fixed, so only what time animates can differ.
    gpu::Image8 frame(rendering::SceneRenderer& renderer, double seconds, std::uint64_t frameIndex = 180) {
        params.resetFinals();
        FrameTime time{};
        time.renderTime = seconds;
        time.deltaTime = 1.0 / 60.0;
        time.frameIndex = frameIndex;
        comp->update(time);
        renderer.resetTemporalHistory();
        auto image = renderer.renderToImage(comp->scene(), time, kW, kH);
        REQUIRE(image.has_value());
        return std::move(*image);
    }
};

fs::path fernAsset() {
    return fs::path(AVGEN_SOURCE_DIR) / "assets/quaternius/glTF/Fern_1.gltf";
}

} // namespace

TEST_CASE("a scatter layer's sway controls reach the picture", "[gpu][wind][ecology][adr938]") {
    if (!fs::exists(fernAsset())) {
        SKIP("the Quaternius library is not present in this checkout");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    Meadow m(fernAsset());
    std::size_t ferns = 0;
    for (const scene::ProceduralGeometry& g : m.comp->scene().procedurals) {
        ferns += g.partOf.empty() && g.name.find("ferns") != std::string::npos ? g.instances.size() : 0;
    }
    INFO("ferns placed: " << ferns);
    REQUIRE(ferns > 20);
    constexpr double kAt = 3.0;

    SECTION("sway at the tip: 0.08 as authored, 0.40 loosened") {
        const gpu::Image8 authored = m.frame(renderer, kAt);
        m.sway("tipAmplitude")->setBase(0.40f);
        const gpu::Image8 loosened = m.frame(renderer, kAt);
        const std::size_t differ = differingPixels(authored, loosened);
        dump("tip-0.08-vs-0.40", authored, loosened);
        INFO("pixels the loosened tips moved: " << differ);
        CHECK(differ > kW * kH / 200);
        // Back to its authored value: the frame it was, byte for byte -- the parameter decides it, not
        // anything the edit left behind.
        m.sway("tipAmplitude")->setBase(0.08f);
        CHECK(differingPixels(authored, m.frame(renderer, kAt)) == 0);
    }

    SECTION("the control: with the wind off the same edit moves nothing") {
        auto* windOn = m.params.findAs<bool>("scene/wind/enabled");
        REQUIRE(windOn != nullptr);
        windOn->setBase(false);
        const gpu::Image8 still = m.frame(renderer, kAt);
        m.sway("tipAmplitude")->setBase(0.40f);
        const gpu::Image8 stillLoosened = m.frame(renderer, kAt);
        CHECK(differingPixels(still, stillLoosened) == 0);
    }

    SECTION("catches the wind at 0: the ferns stand still") {
        // As authored, two seconds half a second apart differ: the ferns sway. (The same frame index for
        // both, so the jitter and noise seed is not what differs.)
        const gpu::Image8 a = m.frame(renderer, kAt);
        const gpu::Image8 b = m.frame(renderer, kAt + 0.5);
        const std::size_t swaying = differingPixels(a, b);
        dump("authored-3.0-vs-3.5", a, b);
        INFO("pixels the authored ferns moved in half a second: " << swaying);
        CHECK(swaying > 0);
        m.sway("windSensitivity")->setBase(0.0f);
        const gpu::Image8 c = m.frame(renderer, kAt);
        const gpu::Image8 d = m.frame(renderer, kAt + 0.5);
        CHECK(differingPixels(c, d) == 0);
    }
    CHECK(ctx->errorCount() == 0);
}
