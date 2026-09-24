// The tornado's STRUCTURE on pixels (ADR-580 §8.7, ADR-706, ADR-707).
//
// Three claims that only a rendered frame can settle, each through the whole engine path -- the
// scene file, the effect list, the packer, the march and the composite -- because every one of the
// defects these guard against lived between the field and the frame rather than inside the field:
//
//   * **The Detail=0 gate** (ADR-580 §8.7, the owner's hard constraint). With `cloudAmount` at 0 the
//     frame is byte-identical to the analytic field -- so the noise controls, set to anything at all,
//     must not move a single byte. The control is the same pair of settings with Detail on, which
//     must differ; without it a frame that ignored the whole detail stack would pass.
//   * **The Tall Column is whole** (ADR-707). `_tc-4` rendered as a cloud and a foot with nothing
//     between them. The field was never at fault; the march's ray directions were, by an f32 error
//     that grows with the camera's distance from the world origin. So the measure is continuity
//     down the column, and the control is the same storm moved to the origin, where the error was
//     always small enough to miss.
//   * **The debris cloud reaches the frame at the foot** (ADR-706), and is gone with Debris at 0.

#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "world/effects/effect_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 320;
constexpr std::uint32_t kHeight = 180;
constexpr double kSecond = 6.0; // past every lab's four-second fade-in, as the review renders are

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

fs::path lab(const char* name) { return fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / name; }

struct Rig {
    std::unique_ptr<gpu::Context> ctx;
    std::unique_ptr<gpu::ShaderLibrary> shaders;
    std::unique_ptr<rendering::SceneRenderer> renderer;
    app::Engine engine{app::EngineMode::Offline};

    explicit Rig(const fs::path& scene) {
        ctx = makeContext();
        shaders = std::make_unique<gpu::ShaderLibrary>(*ctx, std::vector<fs::path>{fs::path(AVGEN_SHADER_SOURCE_DIR)});
        renderer = std::make_unique<rendering::SceneRenderer>(*ctx, *shaders);
        REQUIRE(renderer->init().has_value());
        auto loaded = engine.loadComposition(scene);
        INFO("loading " << scene.string() << ": " << (loaded ? std::string("ok") : loaded.error().message));
        REQUIRE(loaded.has_value());
    }

    // A seek, then a fresh temporal history, then one frame: the shape the composition GPU tests
    // use, and what a `--range t:t` render does.
    gpu::Image8 frame() {
        engine.seekSeconds(kSecond);
        FixedStepClock clock(30.0);
        clock.restartAt(kSecond);
        const FrameTime time = engine.tick(clock);
        engine.setViewport(kWidth, kHeight);
        engine.update(time);
        renderer->resetTemporalHistory();
        auto image = renderer->renderToImage(engine.scene(), time, kWidth, kHeight);
        REQUIRE(image.has_value());
        return std::move(*image);
    }

    void set(const std::string& id, const char* leaf, float v) {
        auto* p = engine.params().find(world::effectParameterPrefix(id) + leaf);
        INFO("parameter " << world::effectParameterPrefix(id) << leaf);
        REQUIRE(p != nullptr);
        p->setBaseComponent(0, v);
    }
};

bool visiblyDiffers(const gpu::Image8& a, const gpu::Image8& b, std::size_t pixel) {
    int sum = 0;
    for (int c = 0; c < 3; ++c) {
        sum += std::abs(static_cast<int>(a.rgba[pixel * 4 + c]) - static_cast<int>(b.rgba[pixel * 4 + c]));
    }
    // 24 of 765, the threshold `test_effect_stack_gpu.cpp` measured: above the march's whole-frame
    // perturbation (ADR-702), far below anything a person can see as a column.
    return sum > 24;
}

std::size_t differingPixels(const gpu::Image8& a, const gpu::Image8& b) {
    REQUIRE(a.rgba.size() == b.rgba.size());
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.rgba.size() / 4; ++i) {
        n += visiblyDiffers(a, b, i) ? 1u : 0u;
    }
    return n;
}

// Of the rows between `y0` and `y1` (fractions of the height), how many contain ANY pixel where
// the storm visibly changes the frame. A column is continuous when this is every row; `_tc-4`'s
// defect was most of them empty.
double rowCoverage(const gpu::Image8& with, const gpu::Image8& without, double y0, double y1) {
    const auto r0 = static_cast<std::uint32_t>(y0 * kHeight);
    const auto r1 = static_cast<std::uint32_t>(y1 * kHeight);
    std::uint32_t covered = 0;
    for (std::uint32_t y = r0; y < r1; ++y) {
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            if (visiblyDiffers(with, without, static_cast<std::size_t>(y) * kWidth + x)) {
                ++covered;
                break;
            }
        }
    }
    return static_cast<double>(covered) / static_cast<double>(r1 - r0);
}

std::string enabledTornado(const app::Engine& engine) {
    for (const auto& e : engine.effects()) {
        if (e.kind == world::EffectKind::Tornado && e.enabled) {
            return e.id;
        }
    }
    FAIL("the scene has no enabled tornado");
    return {};
}

} // namespace

TEST_CASE("at Detail 0 the tornado frame is the analytic field, whatever the noise controls say",
          "[gpu][tornado][structure]") {
    // ADR-580 §8.7, and the owner's hard constraint: the effect reads as a tornado structurally
    // before any noise. `tornado-modes-a-structure` is §39's panel A -- Detail 0 -- so it is the
    // frame the gate is about.
    Rig rig(lab("tornado-modes-a-structure.scene.json"));
    const std::string id = enabledTornado(rig.engine);
    rig.set(id, "cloudAmount", 0.0f);
    const gpu::Image8 analytic = rig.frame();

    // Every control the detail stack reads, moved a long way.
    const auto scramble = [&] {
        rig.set(id, "macroAmp", 3.7f);
        rig.set(id, "mesoAmp", 2.9f);
        rig.set(id, "microAmp", 3.3f);
        rig.set(id, "detailScale", 17.0f);
        rig.set(id, "detailContrast", 9.0f);
        rig.set(id, "climbRate", -2.5f);
        rig.set(id, "erosion", 5.5f);
    };
    scramble();
    const gpu::Image8 scrambled = rig.frame();
    INFO("hash at Detail 0: " << gpu::hashImage(analytic) << ", with every noise control moved: "
                              << gpu::hashImage(scrambled));
    CHECK(gpu::hashImage(analytic) == gpu::hashImage(scrambled));

    // THE CONTROL. The same scrambled controls with Detail ON must change the frame, or the gate
    // above is passing because the detail stack never reaches the picture at all.
    rig.set(id, "cloudAmount", 0.8f);
    const gpu::Image8 detailed = rig.frame();
    const std::size_t moved = differingPixels(analytic, detailed);
    INFO("Detail 0.8 moves " << moved << " px against Detail 0");
    CHECK(moved > 200);
}

TEST_CASE("the Tall Column is continuous from its cloud to its foot", "[gpu][tornado][structure]") {
    // ADR-707. Before the fix, 7 of every 10 rows in the middle of the column showed nothing.
    const fs::path scene = lab("_tc-4-tall-column.scene.json");
    Rig rig(scene);
    const std::string id = enabledTornado(rig.engine);
    const gpu::Image8 with = rig.frame();
    rig.set(id, "enabled", 0.0f);
    const gpu::Image8 without = rig.frame();

    // The funnel's own span on screen: below the wall cloud (the top quarter) and above the foot
    // (the bottom few rows). Nothing in that band but the column.
    const double mid = rowCoverage(with, without, 0.32, 0.92);
    INFO("the column is present in " << mid * 100.0 << "% of the rows between its cloud and its foot");
    CHECK(mid > 0.97);

    SECTION("and the same storm at the world origin says the same, which is the control") {
        // Where the ray error was always small: the defect was a function of distance from the
        // origin, so the storm moved there is the arm that was never broken. If this ever fails
        // too, the column is gone for a reason that has nothing to do with ADR-707.
        std::ifstream in(scene);
        nlohmann::json j = nlohmann::json::parse(in);
        j["camera"]["position"][0] = 0.0;
        j["camera"]["target"][0] = 0.0;
        nlohmann::json kept = nlohmann::json::array();
        for (auto& e : j["effects"]) {
            if (e.value("id", std::string()) == id) {
                e["parameters"]["base"][0] = 0.0;
                kept.push_back(e);
            }
        }
        j["effects"] = kept;
        const fs::path moved = fs::temp_directory_path() / "avgen-tc4-at-origin.scene.json";
        std::ofstream(moved) << j.dump(1);
        Rig origin(moved);
        const gpu::Image8 w = origin.frame();
        origin.set(id, "enabled", 0.0f);
        const gpu::Image8 wo = origin.frame();
        const double atOrigin = rowCoverage(w, wo, 0.32, 0.92);
        INFO("at the origin the column is present in " << atOrigin * 100.0 << "% of the rows");
        CHECK(atOrigin > 0.97);
        fs::remove(moved);
    }
}

TEST_CASE("the debris cloud reaches the frame at the foot, and only there", "[gpu][tornado][structure]") {
    // ADR-706. §39's panel A, whose storm authors Debris 0.8 and stands with its foot at the bottom
    // of the frame: turning the debris off must change the foot and leave the column's upper half
    // alone, because the mound is a term at the ground and nowhere else.
    Rig rig(lab("tornado-modes-a-structure.scene.json"));
    const std::string id = enabledTornado(rig.engine);
    const gpu::Image8 with = rig.frame();
    rig.set(id, "skirtDensity", 0.0f);
    const gpu::Image8 without = rig.frame();

    std::size_t foot = 0;
    std::size_t upper = 0;
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            if (!visiblyDiffers(with, without, static_cast<std::size_t>(y) * kWidth + x)) {
                continue;
            }
            if (y >= kHeight * 3 / 4) {
                ++foot;
            } else if (y < kHeight / 2) {
                ++upper;
            }
        }
    }
    INFO("the debris moves " << foot << " px in the bottom quarter and " << upper << " px in the top half");
    CHECK(foot > 150);
    CHECK(upper == 0);
}
