// ADR-2131 (Neon Dreams, owner msg 320: "a 4K render at full cloud resolution comes out black with no error"). macOS
// aborts a command buffer that runs past its GPU watchdog and then ignores the process's later submissions; Dawn
// reports neither. Two answers: the cloud march is split into bands, each its own submission, so it never runs that
// long; and every frame carries an execution stamp, so a frame the GPU did not run is an error instead of black.
//
// None of these cases trips the watchdog itself: a process that has is ignored by the GPU from then on, which would
// fail every case after it in this binary. The threshold is reproduced by the 4K render in the ADR.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/cloud_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <memory>
#include <tuple>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

std::unique_ptr<gpu::Context> makeContext() {
    log::init(log::Level::Warn);
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

std::uint64_t readStamp(gpu::Context& ctx, const wgpu::Buffer& buffer) {
    auto bytes = gpu::readBuffer(ctx, buffer, 0, gpu::Context::kExecutionStampBytes);
    REQUIRE(bytes.has_value());
    std::uint64_t v = 0;
    std::memcpy(&v, bytes->data(), sizeof v);
    return v;
}

// a broken cloud sea under a low sun, at the march's full resolution, so every band has cloud and sky edges in it
scene::Scene cloudScene() {
    scene::Scene s;
    s.camera.position = {0.0f, 300.0f, 0.0f};
    s.camera.target = {0.0f, 150.0f, -2000.0f};
    s.camera.fovYRadians = 0.87f;
    s.camera.nearPlane = 0.5f;
    s.camera.farPlane = 60000.0f;
    s.clouds.sea.enabled = true;
    s.clouds.sea.base = -300.0f;
    s.clouds.sea.top = 0.0f;
    s.clouds.sea.coverage = 0.6f;
    s.clouds.sea.density = 2.0f;
    s.clouds.extinction = 0.02f;
    s.clouds.lightIntensity = 0.5f;
    s.clouds.resolution = 1.0f;
    s.clouds.steps = 96;
    s.clouds.lightSteps = 6;
    s.clouds.temporal = 0.0f;
    return s;
}

} // namespace

TEST_CASE("the cloud march splits into bands by its cost and keeps a live march whole", "[renderer][clouds][adr2131]") {
    using rendering::cloudMarchBands;
    // Neon Dreams' 4K master: 192 steps, 8 light taps. Full resolution ran ~7.8 s in one command buffer (black);
    // 0.75 ran 4.4 s (rendered, within a second of the watchdog).
    const std::uint32_t full = cloudMarchBands(3840, 2160, 192, 8);
    const std::uint32_t three4 = cloudMarchBands(2880, 1620, 192, 8);
    CHECK(full == 14);  // ~0.55 s a band at that view's measured cost
    CHECK(three4 == 8);
    // every band within the budget
    for (const auto [w, h, bands] : {std::tuple{3840u, 2160u, full}, std::tuple{2880u, 1620u, three4}}) {
        const std::uint64_t rows = (h + bands - 1) / bands;
        CHECK(static_cast<std::uint64_t>(w) * rows * 192 * 9 <= rendering::kCloudMarchUnitsPerSubmit + w * 192 * 9);
    }
    // Neon Dreams live (0.4 of 4K, 40 steps, 3 taps) and offline at 1080p (0.5, 128 steps, 6 taps): one pass, as
    // before ADR-2131
    CHECK(cloudMarchBands(1536, 864, 40, 3) == 1);
    CHECK(cloudMarchBands(960, 540, 128, 6) == 1);
    // never more bands than rows, never 0
    CHECK(cloudMarchBands(64, 4, 512, 32, 1) == 4);
    CHECK(cloudMarchBands(0, 0, 0, 0) == 1);
}

TEST_CASE("a banded cloud march draws the same picture as one pass", "[gpu][renderer][clouds][adr2131]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    constexpr std::uint32_t W = 192, H = 108;
    const scene::Scene s = cloudScene();
    FrameTime t{};
    t.renderTime = 1.0;
    t.frameIndex = 30;

    rendering::SceneRenderer whole(*ctx, shaders);
    REQUIRE(whole.init().has_value());
    auto a = whole.renderToImage(s, t, W, H);
    REQUIRE(a.has_value());
    REQUIRE(whole.clouds().lastMarchBands() == 1);

    rendering::SceneRenderer banded(*ctx, shaders);
    REQUIRE(banded.init().has_value());
    // five bands of the march's 108 rows
    const std::uint64_t units = std::uint64_t{W} * H * 96 * 7;
    banded.clouds().setMarchUnitsPerSubmit(units / 5 + 1);
    auto b = banded.renderToImage(s, t, W, H);
    REQUIRE(b.has_value());
    CHECK(banded.clouds().lastMarchBands() == 5);

    // the frame has cloud in it (not a black frame matching a black frame)
    double sum = 0.0;
    for (std::size_t i = 0; i < a->rgba.size(); i += 4) {
        sum += a->rgba[i] + a->rgba[i + 1] + a->rgba[i + 2];
    }
    CHECK(sum / (3.0 * W * H) > 10.0);
    CHECK(gpu::hashImage(*a) == gpu::hashImage(*b));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("a command buffer the GPU never ran fails its execution stamp", "[gpu][adr2131]") {
    auto ctx = makeContext();
    wgpu::BufferDescriptor bd{};
    bd.label = "stamp-test";
    bd.size = 256;
    bd.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
    const wgpu::Buffer dst = ctx->device().CreateBuffer(&bd);

    // run: the token lands
    wgpu::CommandEncoder ran = ctx->device().CreateCommandEncoder();
    const std::uint64_t first = ctx->encodeExecutionStamp(ran, dst, 0);
    wgpu::CommandBuffer commands = ran.Finish();
    ctx->queue().Submit(1, &commands);
    ctx->waitForQueue();
    CHECK(readStamp(*ctx, dst) == first);

    // not run -- encoded and dropped, as an aborted or ignored command buffer is: the slot keeps the last token
    {
        wgpu::CommandEncoder dropped = ctx->device().CreateCommandEncoder();
        const std::uint64_t second = ctx->encodeExecutionStamp(dropped, dst, 0);
        CHECK(second != first);
        ctx->waitForQueue();
        CHECK(readStamp(*ctx, dst) != second);
    }
    CHECK(ctx->errorCount() == 0);
    ctx->reportNotExecuted("test frame");
    CHECK(ctx->errorCount() == 1);
    ctx->clearErrors();
}
