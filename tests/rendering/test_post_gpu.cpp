// Built-in post-processing on the GPU: bloom spread, grading, tone-map operators, vignette,
// depth of field, motion blur, and the transient pool.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "gpu/transient_pool.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
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

// A small very bright emissive cube in the centre against black; bloom should spread its light.
scene::Scene brightCubeScene() {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.camera.position = {0.0f, 0.0f, 8.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    const auto mesh = s.addMesh(scene::makeCube(0.4f));
    auto& e = s.addEntity("bright", mesh);
    e.material.baseColor = {0.0f, 0.0f, 0.0f};
    e.material.emissiveColor = {1.0f, 0.9f, 0.6f};
    e.material.emissiveIntensity = 30.0f;
    s.post.bloomEnabled = false;
    s.post.bloomIntensity = 0.0f;
    return s;
}

int sum3(const std::uint8_t* px) { return px[0] + px[1] + px[2]; }

int edgeSharpness(const gpu::Image8& img, std::uint32_t y) {
    // Max horizontal gradient along a row: sharp edges score high, blurred ones low.
    int best = 0;
    for (std::uint32_t x = 1; x < img.width; ++x) {
        best = std::max(best, std::abs(sum3(img.pixel(x, y)) - sum3(img.pixel(x - 1, y))));
    }
    return best;
}
} // namespace

TEST_CASE("Bloom spreads light beyond a bright object", "[gpu][post]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    auto s = brightCubeScene();
    FrameTime time{};
    auto off = renderer.renderToImage(s, time, 128, 128);
    REQUIRE(off.has_value());
    s.post.bloomEnabled = true;
    s.post.bloomIntensity = 1.0f;
    s.post.bloomThreshold = 1.0f;
    auto on = renderer.renderToImage(s, time, 128, 128);
    REQUIRE(on.has_value());
    CHECK(ctx->errorCount() == 0);
    // Outside the cube (which covers roughly the middle 24 px) the halo appears only with bloom.
    const int offHalo = sum3(off->pixel(64, 40));
    const int onHalo = sum3(on->pixel(64, 40));
    INFO("halo off=" << offHalo << " on=" << onHalo);
    CHECK(offHalo < 20);
    CHECK(onHalo > offHalo + 30);
    CHECK(renderer.stats().post.bloomLevels >= 4);
    CHECK(renderer.stats().post.passes >= 8);
    if (const char* dumpDir = std::getenv("AVGEN_DUMP_DIR")) {
        REQUIRE(gpu::writePpm(*on, std::filesystem::path(dumpDir) / "bloom.ppm").has_value());
    }
}

TEST_CASE("Grading: zero saturation yields grey, vignette darkens corners, tone operators stay in range",
          "[gpu][post]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    scene::Scene s;
    s.environment.backgroundColor = {0.6f, 0.1f, 0.05f}; // saturated red background
    FrameTime time{};
    auto colour = renderer.renderToImage(s, time, 64, 64);
    REQUIRE(colour.has_value());
    CHECK(colour->pixel(32, 32)[0] > colour->pixel(32, 32)[2] + 60);

    s.post.saturation = 0.0f;
    auto grey = renderer.renderToImage(s, time, 64, 64);
    REQUIRE(grey.has_value());
    const auto* g = grey->pixel(32, 32);
    CHECK(std::abs(int(g[0]) - int(g[1])) <= 3);
    CHECK(std::abs(int(g[1]) - int(g[2])) <= 3);

    s.post.saturation = 1.0f;
    s.post.vignette = 1.0f;
    auto vig = renderer.renderToImage(s, time, 64, 64);
    REQUIRE(vig.has_value());
    CHECK(sum3(vig->pixel(32, 32)) > sum3(vig->pixel(1, 1)) + 40);

    s.post.vignette = 0.0f;
    std::vector<std::uint64_t> hashes;
    for (int op = 0; op < 5; ++op) {
        s.post.tonemap = static_cast<scene::TonemapOperator>(op);
        auto img = renderer.renderToImage(s, time, 32, 32);
        REQUIRE(img.has_value());
        const auto* px = img->pixel(16, 16);
        INFO("operator " << op);
        CHECK(px[0] > 40); // red survives every operator
        hashes.push_back(gpu::hashImage(*img));
    }
    // Operators differ from one another (at least ACES vs clamp vs AgX).
    CHECK(hashes[0] != hashes[4]);
    CHECK(hashes[0] != hashes[1]);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Depth of field blurs an out-of-focus edge and motion blur smears camera motion", "[gpu][post]") {
    // ADR-040: motion blur is now tile-based reconstruction over the velocity target rather than a
    // depth reprojection. test_motion_gpu.cpp checks that object motion smears along its own
    // velocity; this case keeps the *camera* covered.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.camera.position = {0.0f, 0.0f, 6.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    const auto mesh = s.addMesh(scene::makeCube(1.0f));
    auto& e = s.addEntity("cube", mesh);
    e.material.baseColor = {1.0f, 1.0f, 1.0f};
    e.material.emissiveColor = {1.0f, 1.0f, 1.0f};
    e.material.emissiveIntensity = 1.0f;
    FrameTime time{};
    auto sharp = renderer.renderToImage(s, time, 128, 128);
    REQUIRE(sharp.has_value());
    const int sharpEdge = edgeSharpness(*sharp, 64);

    s.post.dofEnabled = true;
    s.post.focusDistance = 20.0f; // cube at 5 m is far out of focus
    s.post.focusRange = 0.5f;
    s.post.dofMaxRadius = 12.0f;
    auto blurred = renderer.renderToImage(s, time, 128, 128);
    REQUIRE(blurred.has_value());
    const int blurredEdge = edgeSharpness(*blurred, 64);
    INFO("edge sharp=" << sharpEdge << " dof=" << blurredEdge);
    CHECK(blurredEdge < sharpEdge * 0.7f);
    if (const char* dumpDir = std::getenv("AVGEN_DUMP_DIR")) {
        REQUIRE(gpu::writePpm(*sharp, std::filesystem::path(dumpDir) / "post_sharp.ppm").has_value());
        REQUIRE(gpu::writePpm(*blurred, std::filesystem::path(dumpDir) / "post_dof.ppm").has_value());
    }
    s.post.dofEnabled = false;

    // Motion blur: render once to seed the previous matrices, then pan the camera sideways.
    // `motionBlurMaxRadius` is in pixels at 720p and scales with the frame height, so at 128 px
    // the default would clamp the smear to seven pixels; it is lifted here so the pan, not the
    // clamp, sets the length. The ADR-040 filter weights each tap by whether it actually reaches
    // this pixel, so a silhouette gets a soft ramp rather than the flat box average the old
    // reprojection blur produced - which is why the threshold is 0.8 rather than 0.7.
    s.post.motionBlurAmount = 1.0f;
    s.post.motionBlurMaxRadius = 200.0f;
    (void)renderer.renderToImage(s, time, 128, 128);
    s.camera.position.x += 0.6f;
    s.camera.target.x += 0.6f;
    auto moved = renderer.renderToImage(s, time, 128, 128);
    REQUIRE(moved.has_value());
    const int movedEdge = edgeSharpness(*moved, 64);
    INFO("edge moved=" << movedEdge << " post passes=" << renderer.stats().post.passes);
    if (const char* dumpDir = std::getenv("AVGEN_DUMP_DIR")) {
        REQUIRE(gpu::writePpm(*moved, std::filesystem::path(dumpDir) / "post_moved.ppm").has_value());
    }
    CHECK(movedEdge < sharpEdge * 0.8f);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Transient pool reuses textures across frames", "[gpu][post]") {
    auto ctx = makeContext();
    gpu::TransientPool pool(*ctx);
    auto a = pool.acquire(64, 64, wgpu::TextureFormat::RGBA16Float);
    auto b = pool.acquire(64, 64, wgpu::TextureFormat::RGBA16Float);
    CHECK(a.texture.Get() != b.texture.Get());
    CHECK(pool.allocations() == 2);
    pool.endFrame();
    auto c = pool.acquire(64, 64, wgpu::TextureFormat::RGBA16Float);
    CHECK((c.texture.Get() == a.texture.Get() || c.texture.Get() == b.texture.Get()));
    CHECK(pool.allocations() == 2);
    auto d = pool.acquire(32, 32, wgpu::TextureFormat::RGBA16Float);
    CHECK(pool.allocations() == 3);
    CHECK(pool.size() == 3);
    for (int i = 0; i < 70; ++i) {
        pool.endFrame(60);
    }
    CHECK(pool.size() == 0); // everything aged out
}
