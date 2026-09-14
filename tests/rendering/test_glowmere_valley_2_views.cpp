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
#include "params/parameter.hpp"
#include "params/parameter_set.hpp"
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

    // **Load the project, not the scene.** These captures are meant to be what the user sees, and
    // what the user opens is the project -- whose parameter block overrides the scene's transforms
    // and is where the hand edits live. Loading the scene alone renders a world with none of them:
    // the heroes stood at the headings the script authored rather than the ones the user turned them
    // to, so a spore-fall that had come adrift from its cap under those rotations looked perfectly
    // aligned here. A capture that cannot show the defect is not evidence that there isn't one.
    const fs::path projectFile =
        fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2.json";
    app::Engine engine(app::EngineMode::Offline);
    if (over != nullptr) {
        REQUIRE(engine.loadComposition(fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / over).has_value());
    } else {
        REQUIRE(engine.loadProject(projectFile).has_value());
    }
    REQUIRE(engine.composition() != nullptr);

    constexpr std::uint32_t kWidth = 1920;
    constexpr std::uint32_t kHeight = 1080;
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
    const std::array<View, 12> views{{
        {"01-opening", {-118.0f, 32.1f, -96.0f}, {-20.0f, 4.0f, 40.0f}, 40.0f},
        {"02-upstream-axis", {10.0f, 22.0f, 268.0f}, {-10.0f, 6.0f, -160.0f}, 38.0f},
        {"03-downstream-axis", {-30.0f, 30.0f, -232.0f}, {10.0f, -4.0f, 200.0f}, 38.0f},
        {"04-high-oblique", {-250.0f, 132.0f, -230.0f}, {20.0f, -4.0f, 130.0f}, 46.0f},
        {"05-elder-and-pool", {-58.0f, 9.0f, 86.0f}, {-12.0f, 10.0f, 52.0f}, 42.0f},
        {"06-east-wall", {236.0f, 52.0f, 60.0f}, {-30.0f, 0.0f, 30.0f}, 44.0f},
        // A hero close-up: the elder from below and to one side, the angle its underside reads from
        // and the one the search's own winners' sheet was shot at.
        {"07-elder-closeup", {-27.0f, 6.6f, 63.0f}, {-12.0f, 15.5f, 52.0f}, 40.0f},
        // And a second hero at the other end of the valley, so the handover shows more than one.
        {"08-bloom-closeup", {-74.0f, 7.0f, 130.0f}, {-62.0f, 10.5f, 118.0f}, 42.0f},
        // The four drier-band heroes. Each eye stands on the bearing `preferredApproachAzimuth`
        // chose for that site -- eye = site + standOff * (cos a, sin a) in XZ -- rather than on a
        // bearing picked to flatter the mushroom, so these images show what the Auto-director's
        // default approach actually sees. The eye sits below the cap so the underside, the gills and
        // the spore-fall read; that is the half of the organism the new work changed.
        {"09-cairn-closeup", {-145.1f, 35.5f, -41.7f}, {-150.0f, 37.2f, -60.0f}, 42.0f},
        {"10-ridge-closeup", {122.8f, 23.7f, -180.8f}, {132.0f, 24.8f, -190.0f}, 42.0f},
        {"11-scree-closeup", {-164.1f, 24.7f, 88.0f}, {-178.0f, 26.1f, 96.0f}, 42.0f},
        {"12-ember-closeup", {165.4f, 14.2f, 147.2f}, {176.0f, 15.1f, 150.0f}, 42.0f},
    }};

    std::printf("\n===== Glowmere Valley 2, %ux%u =====\n", kWidth, kHeight);

    // **One clock, one resolution, and a warm-up, because particles are simulated and not placed.**
    //
    // Three things had to be true before a single spore appeared, and each was worth a render to
    // find:
    //
    //   1. *The clock has to advance.* A `FixedStepClock` constructed inside the loop makes every
    //      frame the first frame, so `deltaTime` is zero; the particle pass integrates by exactly
    //      that dt, so nothing is emitted and nothing moves. Every capture this test had ever
    //      written was a frame with **no particles at all** -- not only the new spore-fall, but the
    //      river motes and the visitor's beam that have been in the scene since the first handover.
    //      An empty emitter renders as clean sky, which is why it went unnoticed.
    //   2. *The resolution must not change.* Resizing the render target calls
    //      `resetTemporalHistory`, which calls `ParticleRenderer::resetAll`. Warming at 320x180 and
    //      then capturing at 1920x1080 threw the whole simulation away between the two. So the
    //      warm-up runs at the capture size; 240 full-size frames costs a few seconds.
    //   3. *Time must not go backwards.* `renderTime < previousRenderTime_` is a seek, and a seek
    //      resets the same history. Warming to 24 s and then restarting the capture clock at 6 s
    //      wiped the pools a second time. So the captures continue the warm-up's own clock.
    //
    // While any of those held, each view rendered only the handful of particles born in its own
    // single frame, sitting on the emitter disc where they spawned. That is why raising `emissive`
    // and `sizeStart` visibly changed the frame and lowering `drag` did not: nothing had lived long
    // enough to fall. A diagnostic that moves under one parameter and not under another is saying
    // which stage is broken.
    //
    // 240 frames at 0.1 s -- the ceiling `render` clamps dt to, so the cheapest legal way to buy
    // simulated time -- is 24 s, slightly more than the longest spore lifetime (22 s), which is what
    // steady state means for an emitter: deaths balancing births. Nothing is culled out of the
    // simulation, so warming once populates all thirteen systems.
    FixedStepClock clock(10.0);
    clock.restartAt(0.0);
    engine.setViewport(kWidth, kHeight);
    for (int f = 0; f < 240; ++f) {
        const FrameTime t = engine.tick(clock);
        engine.update(t);
        scene::Scene& ws = engine.composition()->scene();
        ws.camera.farPlane = 1400.0f;
        (void)renderer.renderToImage(ws, t, kWidth, kHeight);
    }
    std::printf("  warmed the particle simulation to 24.0 s of steady state\n");
    std::fflush(stdout);

    for (const View& v : views) {
        const FrameTime time = engine.tick(clock);
        engine.setViewport(kWidth, kHeight);
        // The camera must be set *before* `engine.update`, and through the parameters rather than
        // onto the scene. Two reasons, and the first cost a whole forensic investigation:
        //
        //   * `Composition::update` computes the terrain's frustum planes and its per-chunk LOD from
        //     `scene_.camera`, so a camera written afterwards renders a frame that was **culled for
        //     somebody else's viewpoint**. That is what produced the "dark valley floor": the floor
        //     was not dark, it was absent, because those chunks are not visible from the scene's
        //     authored camera and the cull had already run.
        //   * `applyParameters` rebuilds the scene's camera from the parameters every frame, so a
        //     value written onto the scene before update is overwritten by update.
        const auto setVec = [&](const char* path, glm::vec3 v) {
            params::IParameter* p = engine.params().find(path);
            REQUIRE(p != nullptr);
            for (std::size_t i = 0; i < 3; ++i) {
                p->setBaseComponent(i, v[static_cast<glm::length_t>(i)]);
            }
        };
        const auto setFloat = [&](const char* path, float v) {
            if (params::IParameter* p = engine.params().find(path)) {
                p->setBaseComponent(0, v);
            }
        };
        setVec("camera/position", v.eye);
        setVec("camera/target", v.target);
        setFloat("camera/fov", v.fov);
        engine.update(time);
        scene::Scene& scene = engine.composition()->scene();
        // The far plane is not a parameter and the default is 200 m; this map is 640 m across. It is
        // set after update because nothing in update derives from it.
        scene.camera.farPlane = 1400.0f;
        // The guard for the defect that never was: if the camera is ever set after `update` again,
        // this fails instead of producing a plausible frame culled for somebody else's viewpoint.
        REQUIRE(glm::distance(scene.camera.position, v.eye) < 0.01f);
        REQUIRE(glm::distance(scene.camera.target, v.target) < 0.01f);
        // **Let the camera settle before capturing.** Each view is a teleport, and the project the
        // user saved turns motion blur up to 0.80, so the first frame at a new viewpoint integrates
        // the jump and smears the whole world into streaks. Motion blur reads per-pixel velocity
        // from the previous frame, so a few frames standing still bring it to zero. This did not
        // arise while these captures loaded the bare scene, because the scene's own motion blur is
        // off -- the setting only exists in the project, which is exactly why the captures had to
        // start loading the project.
        FrameTime settled = time;
        for (int w = 0; w < 4; ++w) {
            settled = engine.tick(clock);
            engine.update(settled);
            scene::Scene& ss = engine.composition()->scene();
            ss.camera.farPlane = 1400.0f;
            (void)renderer.renderToImage(ss, settled, kWidth, kHeight);
        }
        auto image = renderer.renderToImage(scene, settled, kWidth, kHeight);
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
