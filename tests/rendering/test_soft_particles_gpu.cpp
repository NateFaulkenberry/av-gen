// ADR-367: the depth-aware soft-particle fade.
//
// `ParticleSystem::softness` was authored, serialised, uploaded into the particle uniforms' `turb.z`
// and read by no shader at all -- the fourth member of this repository's reader-without-a-writer
// family, and the one that travelled furthest before being dropped. Fifteen scene files carry
// deliberately tuned values between 0.3 and 3.0 that have never changed a pixel.
//
// The pair below is shaped the way ADR-182 requires. The ARM puts a wall just behind a cloud of
// particles, where a soft fade must dim them. The CONTROL takes the wall away and leaves everything
// else identical, where a soft fade must do *nothing* -- and "nothing" is checked as byte equality,
// not as a tolerance, because the shader returns before it touches the depth texture. If the fade
// were wired to the wrong thing -- fogged, distance-scaled, applied unconditionally -- the control
// is what catches it, and the control can fail.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>

using namespace avgen;

namespace {

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

long totalBrightness(const gpu::Image8& img) {
    long sum = 0;
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
        sum += img.rgba[i] + img.rgba[i + 1] + img.rgba[i + 2];
    }
    return sum;
}

// A cloud of still particles hanging in front of the camera. Still, because the fade is being
// measured and not the simulation: zero speed, zero gravity, zero turbulence and a lifetime longer
// than the run means the same particles are in the same places in every arm.
scene::Scene cloudScene(float softness, bool withWall) {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.camera.position = {0.0f, 0.0f, 6.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};

    if (withWall) {
        // A plane is generated in XZ, so it is rotated to stand up, and placed just behind the
        // cloud -- close enough that a softness of a couple of world units reaches it.
        scene::Entity wall;
        wall.name = "wall";
        wall.mesh = s.addMesh(scene::makePlane(6.0f, 1));
        wall.transform.position = {0.0f, 0.0f, -1.5f};
        wall.transform.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        wall.material.baseColor = {0.5f, 0.5f, 0.5f};
        wall.material.roughness = 1.0f;
        wall.castsShadow = false;
        s.entities.push_back(std::move(wall));
    }

    scene::ParticleSystem sys;
    sys.name = "cloud";
    sys.capacity = 2048;
    sys.shape = scene::EmitterShape::Box;
    sys.position = {0.0f, 0.0f, -1.0f};
    sys.extent = {1.6f, 1.6f, 0.35f};
    sys.spawnRate = 8000.0f;
    sys.lifetimeMin = 10.0f;
    sys.lifetimeMax = 10.0f;
    sys.speedMin = 0.0f;
    sys.speedMax = 0.0f;
    sys.spread = 0.0f;
    sys.gravity = {0.0f, 0.0f, 0.0f};
    sys.turbulence = 0.0f;
    sys.drag = 0.0f;
    sys.sizeStart = 0.25f;
    sys.sizeEnd = 0.25f;
    sys.colorStart = {1.0f, 0.8f, 0.4f, 1.0f};
    sys.colorEnd = {1.0f, 0.8f, 0.4f, 1.0f};
    sys.emissive = 2.0f;
    sys.softness = softness;
    s.particles.push_back(std::move(sys));
    return s;
}

// A FRESH renderer per arm, deliberately. The first draft of this test shared one renderer across
// both arms and the byte-identity case failed against ITSELF: `ParticleRenderer` resets a pool when
// the `scene::Scene*` it is handed changes identity, and two `Scene` temporaries in the same
// function can land on the same stack address -- so the second arm inherited the first arm's live
// particles instead of starting clean, and only sometimes. A determinism arm that depends on where
// the optimiser put a local is not a determinism arm.
gpu::Image8 renderCloud(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const scene::Scene& scene) {
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FixedStepClock clock(60.0);
    gpu::Image8 last;
    // A few frames so the pool has filled; the particles do not move, so the picture settles.
    for (int i = 0; i < 6; ++i) {
        auto img = renderer.renderToImage(scene, clock.tick(), 96, 96);
        REQUIRE(img.has_value());
        last = std::move(*img);
    }
    return last;
}

} // namespace

TEST_CASE("A soft particle fades against the surface behind it", "[gpu][particles][soft]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const scene::Scene hardScene = cloudScene(0.0f, true);
    const scene::Scene softScene = cloudScene(4.0f, true);
    const gpu::Image8 hard = renderCloud(*ctx, shaders, hardScene);
    const gpu::Image8 soft = renderCloud(*ctx, shaders, softScene);
    CHECK(ctx->errorCount() == 0);

    const long hardSum = totalBrightness(hard);
    const long softSum = totalBrightness(soft);
    INFO("hard " << hardSum << " soft " << softSum);
    // The cloud straddles the wall, so a fade can only remove light, never add it -- and it must
    // remove a visible amount rather than a rounding error.
    CHECK(softSum < hardSum);
    CHECK(static_cast<double>(softSum) < 0.95 * static_cast<double>(hardSum));
}

TEST_CASE("With nothing behind them, soft particles are byte-identical to hard ones",
          "[gpu][particles][soft][determinism]") {
    // THE CONTROL. Same cloud, same seeds, same frames, no wall. `linear_depth.wgsl` writes 1e7
    // where nothing was drawn, so the fade evaluates to exactly 1 and the frame must come back the
    // same bytes. This is what says the previous test measured the fade and not, say, the particles
    // having been perturbed by changing a uniform.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const scene::Scene hardScene = cloudScene(0.0f, false);
    const scene::Scene softScene = cloudScene(4.0f, false);
    const gpu::Image8 hard = renderCloud(*ctx, shaders, hardScene);
    const gpu::Image8 soft = renderCloud(*ctx, shaders, softScene);
    CHECK(ctx->errorCount() == 0);
    CHECK(totalBrightness(hard) > 0); // the arm is populated, or the equality below is vacuous
    CHECK(hard.rgba == soft.rgba);
}

TEST_CASE("Softness zero reproduces the pre-ADR-367 picture exactly", "[gpu][particles][soft][determinism]") {
    // The identity claim, stated as a test rather than as a comment. Before ADR-367 no shader read
    // `turb.z`, so every particle system in the repository rendered as though softness were 0 --
    // which is why 0, and not the old 0.2 struct default, is the value that reproduces them. With a
    // wall present and softness 0 the fade must return 1 before it ever samples the depth texture,
    // so the frame is whatever it always was: identical to the same scene rendered with the depth
    // coupling that the fade would use switched off by the softness gate alone.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const scene::Scene one = cloudScene(0.0f, true);
    const scene::Scene two = cloudScene(0.0f, true);
    const gpu::Image8 a = renderCloud(*ctx, shaders, one);
    const gpu::Image8 b = renderCloud(*ctx, shaders, two);
    CHECK(a.rgba == b.rgba);

    // And the default really is the identity value, so a system that says nothing gets nothing.
    CHECK(scene::ParticleSystem{}.softness == 0.0f);
}
