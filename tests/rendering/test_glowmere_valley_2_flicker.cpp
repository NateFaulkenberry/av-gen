// Temporal stability at a camera that sees a spore emitter, with and without it.
//
// Drifting emissive points are the shape of thing `tools/temporal_stats.py` was built to catch, and
// the baseline attribution measured particles as contributing *nothing* only because none were visible
// in that view. Adding ten spore emitters is exactly the change that could move the number, so it is
// measured at a camera that actually sees one -- before and after, from one process, with the scene
// otherwise identical.

#include "app/engine.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "params/parameter.hpp"
#include "params/parameter_set.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <memory>
#include <string>

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

TEST_CASE("Glowmere Valley 2 spore-fall flicker", "[.flicker][glowmere2]") {
    const fs::path sceneFile =
        fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2.scene.json";
    if (!fs::is_regular_file(sceneFile)) {
        SKIP("the scene is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadComposition(sceneFile).has_value());
    REQUIRE(engine.composition() != nullptr);

    constexpr std::uint32_t kWidth = 960;
    constexpr std::uint32_t kHeight = 540;
    constexpr int kFrames = 48;

    // A static camera on the elder, close enough that its spore-fall occupies real screen area. A
    // detector run at a camera that cannot see the thing under test measures nothing, which is how
    // the baseline came to report particles at zero.
    const glm::vec3 eye(-27.0f, 6.6f, 63.0f);
    const glm::vec3 target(-12.0f, 15.5f, 52.0f);

    for (const bool spores : {false, true}) {
        const fs::path dir = fs::path(AVGEN_SOURCE_DIR) / "build" /
                             (spores ? "gv2-flicker-with-spores" : "gv2-flicker-no-spores");
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        // **One clock, ticked forward.** A fresh `FixedStepClock` per frame makes every frame's
        // `deltaSeconds` zero, and a particle simulation advances by dt -- so nothing ever spawns and
        // nothing ever moves. Both arms then render identically because both render no particles,
        // which is exactly what happened: an emitter cranked to 4,000 a second at 0.9 m changed not
        // one pixel. A causally impossible result indicting the harness, again, and the same shape as
        // the camera set after `update`.
        // The measurement must be of a spore-fall at steady state, not of one filling up. A 22 s
        // lifetime needs 24 s of simulated time before deaths balance births, so each arm is warmed
        // first, at the measurement's own resolution and on a clock that only moves forward --
        // resizing the target and seeking backwards *both* call `resetTemporalHistory`, which resets
        // the particle pools, and either one silently empties the emitter the arm is here to weigh.
        {
            FixedStepClock warm(10.0);
            warm.restartAt(0.0);
            engine.setViewport(kWidth, kHeight);
            for (int f = 0; f < 240; ++f) {
                const FrameTime t = engine.tick(warm);
                engine.update(t);
                scene::Scene& ws = engine.composition()->scene();
                ws.camera.farPlane = 1400.0f;
                (void)renderer.renderToImage(ws, t, kWidth, kHeight);
            }
        }
        FixedStepClock clock(30.0);
        clock.restartAt(24.0);
        for (int f = 0; f < kFrames; ++f) {
            const FrameTime time = engine.tick(clock);
            engine.setViewport(kWidth, kHeight);
            const auto setVec = [&](const char* path, glm::vec3 v) {
                params::IParameter* p = engine.params().find(path);
                REQUIRE(p != nullptr);
                for (std::size_t k = 0; k < 3; ++k) {
                    p->setBaseComponent(k, v[static_cast<glm::length_t>(k)]);
                }
            };
            setVec("camera/position", eye);
            setVec("camera/target", target);
            engine.update(time);
            scene::Scene& sc = engine.composition()->scene();
            sc.camera.farPlane = 1400.0f;
            if (!spores) {
                //  has no visibility flag, so the arm turns the emitter off at the
                // source: spawn rate to zero and the pool emptied. Same scene, same everything else.
                for (scene::ParticleSystem& p : sc.particles) {
                    if (p.name.find("-spores") != std::string::npos) {
                        p.spawnRate = 0.0f;
                        p.burst = 0.0f;
                    }
                }
            }
            if (f == 0) {
                std::size_t sporeSystems = 0;
                for (const scene::ParticleSystem& p : sc.particles) {
                    if (p.name.find("-spores") != std::string::npos) ++sporeSystems;
                }
                std::printf("    arm %s: %zu particle systems, %zu of them spore-fall\n",
                            spores ? "with" : "without", sc.particles.size(), sporeSystems);
                for (const scene::ParticleSystem& p : sc.particles) {
                    std::printf("      %-18s pos (%8.2f,%7.2f,%8.2f) cap %6u rate %8.1f size %.3f\n",
                                p.name.c_str(), static_cast<double>(p.position.x),
                                static_cast<double>(p.position.y), static_cast<double>(p.position.z),
                                p.capacity, static_cast<double>(p.spawnRate),
                                static_cast<double>(p.sizeStart));
                }
            }
            if (spores && std::getenv("GV2_SPORE_SHOUT") != nullptr) {
                // A diagnostic arm whose sign is known in advance: an absurd emitter *must* change the
                // frame. If it does not, the emitter is broken rather than merely subtle (ADR-182).
                for (scene::ParticleSystem& p : sc.particles) {
                    if (p.name.find("-spores") != std::string::npos) {
                        p.spawnRate = 4000.0f;
                        p.sizeStart = 0.9f;
                        p.sizeEnd = 0.7f;
                        p.emissive = 8.0f;
                    }
                }
            }
            auto image = renderer.renderToImage(sc, time, kWidth, kHeight);
            REQUIRE(image.has_value());
            char name[64];
            std::snprintf(name, sizeof(name), "frame_%04d.png", f);
            REQUIRE(assets::writePng(dir / name, image->width, image->height, image->rgba).has_value());
        }
        std::printf("  %s: %d frames at %ux%u\n", dir.filename().string().c_str(), kFrames, kWidth,
                    kHeight);
        std::fflush(stdout);
    }
}
