// Glowmere Valley 2, looked at (Phase 2). The brief's acceptance criteria are visual -- "the river
// visibly traverses the complete map", "the valley is recognisable from multiple viewpoints" -- and
// this repo's §50 rule is that a frame is looked at rather than only timed. So this writes the
// frames; a person reads them.
//
// Hidden behind a dot tag: it is a capture, not an assertion about pixels.

#include "app/engine.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/render_stats.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <memory>
#include <string>

#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

using namespace avgen;
namespace fs = std::filesystem;

namespace {
std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available");
    }
    return std::move(*ctx);
}
} // namespace

TEST_CASE("Glowmere Valley 2 from several viewpoints", "[.capture][glowmere2]") {
    const fs::path sceneFile =
        fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2.scene.json";
    const char* over = std::getenv("GV2_SCENE");
    if (!fs::is_regular_file(sceneFile)) {
        SKIP("the Glowmere Valley 2 scene is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(over ? fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / over : sceneFile).has_value());
    REQUIRE(engine.composition() != nullptr);

    constexpr std::uint32_t kWidth = 1280;
    constexpr std::uint32_t kHeight = 720;
    const fs::path outDir = fs::path(AVGEN_SOURCE_DIR) / "build" / "glowmere-valley-2";
    std::error_code ec;
    fs::create_directories(outDir, ec);

    struct View {
        const char* name;
        glm::vec3 eye;
        glm::vec3 target;
        float fov;
    };
    // Chosen to answer the acceptance criteria rather than to flatter the scene: two of these look
    // straight down the valley's axis from opposite ends, which is where a river that did not
    // traverse would be obvious, and one is a high oblique that shows the whole corridor at once.
    const std::array<View, 6> views{{
        {"01-opening", {-118.0f, 32.1f, -96.0f}, {-20.0f, 4.0f, 40.0f}, 40.0f},
        {"02-upstream-axis", {10.0f, 22.0f, 268.0f}, {-10.0f, 6.0f, -160.0f}, 38.0f},
        {"03-downstream-axis", {-30.0f, 30.0f, -232.0f}, {10.0f, -4.0f, 200.0f}, 38.0f},
        {"04-high-oblique", {-250.0f, 132.0f, -230.0f}, {20.0f, -4.0f, 130.0f}, 46.0f},
        {"05-elder-and-pool", {-58.0f, 9.0f, 86.0f}, {-12.0f, 10.0f, 52.0f}, 42.0f},
        {"06-east-wall", {236.0f, 52.0f, 60.0f}, {-30.0f, 0.0f, 30.0f}, 44.0f},
    }};

    std::printf("\n===== Glowmere Valley 2, %ux%u =====\n", kWidth, kHeight);
    for (const View& v : views) {
        FixedStepClock clock(60.0);
        clock.restartAt(6.0);
        const FrameTime time = engine.tick(clock);
        engine.setViewport(kWidth, kHeight);
        engine.update(time);
        // After update: `applyParameters` rebuilds the scene's camera from the project's parameters
        // every frame, so a camera written before it would be overwritten by the authored one.
        scene::Scene& scene = engine.composition()->scene();
        scene.camera.position = v.eye;
        scene.camera.target = v.target;
        scene.camera.lens.useExplicitFov = true;
        scene.camera.fovYRadians = glm::radians(v.fov);
        // The default far plane is 200 m and this map is 640 m across its diagonal-and-a-half. A
        // view down the valley's axis with the authored default would end in a hard clip at the
        // exact distance the acceptance criterion is about.
        scene.camera.farPlane = 1400.0f;
        auto image = renderer.renderToImage(scene, time, kWidth, kHeight);
        REQUIRE(image.has_value());
        REQUIRE(assets::writePng(outDir / (std::string(v.name) + ".png"), image->width, image->height,
                                 image->rgba)
                    .has_value());
        const rendering::RenderStats& stats = renderer.stats();
        std::printf("  %-20s tris %8llu  visible %6llu  draws %4u\n", v.name,
                    static_cast<unsigned long long>(stats.triangles),
                    static_cast<unsigned long long>(stats.visibleInstances), stats.drawCalls);
        std::fflush(stdout);
    }
    std::printf("  frames written to %s\n", outDir.string().c_str());
}
