// Texture sharing (milestone 1.2). Syphon is verified end to end: a server publishes Dawn textures
// and a SyphonMetalClient in the same process (discovered by name, like any other application)
// receives and reads them back. NDI needs the runtime library and is skipped without it.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "share/texture_share.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <algorithm>
#include <utility>
#include <vector>

#ifdef __APPLE__
#include "support/syphon_test_client.hpp"
#endif

using namespace avgen;
using namespace std::chrono_literals;

namespace {

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    gpu::ContextDesc desc{};
    auto ctx = gpu::Context::create(desc);
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

wgpu::Texture makeTexture(gpu::Context& ctx, std::uint32_t w, std::uint32_t h, wgpu::TextureFormat format) {
    wgpu::TextureDescriptor desc{};
    desc.label = "share-source";
    desc.size = {w, h, 1};
    desc.format = format;
    desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::CopyDst;
    return ctx.device().CreateTexture(&desc);
}

void clearTexture(gpu::Context& ctx, const wgpu::Texture& texture, wgpu::Color colour) {
    wgpu::RenderPassColorAttachment color{};
    color.view = texture.CreateView();
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    color.clearValue = colour;
    wgpu::RenderPassDescriptor pass{};
    pass.colorAttachmentCount = 1;
    pass.colorAttachments = &color;
    wgpu::CommandEncoder encoder = ctx.device().CreateCommandEncoder();
    encoder.BeginRenderPass(&pass).End();
    wgpu::CommandBuffer commands = encoder.Finish();
    ctx.queue().Submit(1, &commands);
}

// x -> red, y -> green, constant blue; written in the texture's own channel order.
std::uint8_t gradientR(std::uint32_t x, std::uint32_t w) { return static_cast<std::uint8_t>(x * 255u / (w - 1)); }
std::uint8_t gradientG(std::uint32_t y, std::uint32_t h) { return static_cast<std::uint8_t>(y * 255u / (h - 1)); }

void writeGradient(gpu::Context& ctx, const wgpu::Texture& texture, std::uint32_t w, std::uint32_t h, bool bgra) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            auto* px = pixels.data() + (static_cast<std::size_t>(y) * w + x) * 4;
            px[bgra ? 2 : 0] = gradientR(x, w);
            px[1] = gradientG(y, h);
            px[bgra ? 0 : 2] = 128;
            px[3] = 255;
        }
    }
    wgpu::TexelCopyTextureInfo dst{};
    dst.texture = texture;
    wgpu::TexelCopyBufferLayout layout{};
    layout.bytesPerRow = w * 4;
    layout.rowsPerImage = h;
    const wgpu::Extent3D extent{w, h, 1};
    ctx.queue().WriteTexture(&dst, pixels.data(), pixels.size(), &layout, &extent);
}

bool near(const std::uint8_t* px, int r, int g, int b, int tolerance = 2) {
    return std::abs(int(px[0]) - r) <= tolerance && std::abs(int(px[1]) - g) <= tolerance &&
           std::abs(int(px[2]) - b) <= tolerance && px[3] == 255;
}

} // namespace

TEST_CASE("TextureShare describes transports and rejects publishing when closed", "[gpu][share]") {
    CHECK_FALSE(share::TextureShare::describe().empty());
    CHECK(share::TextureShare::describe().find("Syphon: ") == 0);
    share::TextureShare share;
    CHECK_FALSE(share.isOpen());
    CHECK(share.stats().framesPublished == 0);
    auto ctx = makeContext();
    auto texture = makeTexture(*ctx, 8, 8, wgpu::TextureFormat::RGBA8Unorm);
    auto published = share.publish(texture, 8, 8);
    REQUIRE_FALSE(published.has_value());
    CHECK(published.error().message.find("not open") != std::string::npos);
}

#ifdef __APPLE__

TEST_CASE("Syphon server publishes Dawn textures that a client in this process receives", "[gpu][share][syphon]") {
    if (!share::TextureShare::available(share::ShareKind::Syphon)) {
        SKIP("Syphon not available");
    }
    auto ctx = makeContext();
    if (!ctx->capabilities().sharedTextureIOSurface) {
        SKIP("Dawn device lacks IOSurface shared-texture support");
    }
    constexpr std::uint32_t w = 64;
    constexpr std::uint32_t h = 48;
    const bool bgra = GENERATE(false, true);
    const auto format = bgra ? wgpu::TextureFormat::BGRA8Unorm : wgpu::TextureFormat::RGBA8Unorm;
    CAPTURE(bgra);

    share::TextureShare share;
    REQUIRE(share.open(share::ShareKind::Syphon, *ctx, "avgen-test").has_value());
    CHECK(share.isOpen());
    CHECK(share.kind() == share::ShareKind::Syphon);
    CHECK(share.name() == "avgen-test");

    auto client = testsupport::SyphonTestClient::connect("avgen-test", 2s);
    REQUIRE(client != nullptr);

    // ---- solid colour ----
    auto texture = makeTexture(*ctx, w, h, format);
    clearTexture(*ctx, texture, {0.2, 0.4, 0.6, 1.0});
    REQUIRE(share.publish(texture, w, h).has_value());
    REQUIRE(client->waitForFrame(2s));
    auto frame = client->readFrame();
    REQUIRE(frame.has_value());
    CHECK(frame->width == w);
    CHECK(frame->height == h);
    CHECK(near(frame->pixel(5, 7), 51, 102, 153));
    CHECK(near(frame->pixel(w - 1, h - 1), 51, 102, 153));

    // ---- gradient ----
    writeGradient(*ctx, texture, w, h, bgra);
    REQUIRE(share.publish(texture, w, h).has_value());
    REQUIRE(client->waitForFrame(2s));
    frame = client->readFrame();
    REQUIRE(frame.has_value());
    for (const auto [x, y] : {std::pair{0u, 0u}, std::pair{w - 1, 0u}, std::pair{0u, h - 1}, std::pair{w / 2, h / 3}}) {
        CAPTURE(x, y);
        CHECK(near(frame->pixel(x, y), gradientR(x, w), gradientG(y, h), 128));
    }

    auto stats = share.stats();
    CHECK(stats.framesPublished == 2);
    CHECK(stats.width == w);
    CHECK(stats.height == h);
    CHECK(stats.lastError.empty());
    CHECK(stats.connected);
    ctx->processEvents();
    CHECK(ctx->errorCount() == 0);

    // ---- close and reopen ----
    share.close();
    CHECK_FALSE(share.isOpen());
    CHECK(share.stats().framesPublished == 0);
    client.reset();
    REQUIRE(share.open(share::ShareKind::Syphon, *ctx, "avgen-test-2").has_value());
    auto client2 = testsupport::SyphonTestClient::connect("avgen-test-2", 2s);
    REQUIRE(client2 != nullptr);
    clearTexture(*ctx, texture, {1.0, 0.0, 0.0, 1.0});
    REQUIRE(share.publish(texture, w, h).has_value());
    REQUIRE(client2->waitForFrame(2s));
    frame = client2->readFrame();
    REQUIRE(frame.has_value());
    CHECK(near(frame->pixel(1, 1), 255, 0, 0));
    CHECK(share.stats().framesPublished == 1);
    share.close();
    ctx->processEvents();
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Syphon server follows source size changes and sustains a burst of frames", "[gpu][share][syphon]") {
    if (!share::TextureShare::available(share::ShareKind::Syphon)) {
        SKIP("Syphon not available");
    }
    auto ctx = makeContext();
    if (!ctx->capabilities().sharedTextureIOSurface) {
        SKIP("Dawn device lacks IOSurface shared-texture support");
    }
    share::TextureShare share;
    REQUIRE(share.open(share::ShareKind::Syphon, *ctx, "avgen-test-burst").has_value());
    auto client = testsupport::SyphonTestClient::connect("avgen-test-burst", 2s);
    REQUIRE(client != nullptr);

    auto small = makeTexture(*ctx, 32, 16, wgpu::TextureFormat::RGBA8Unorm);
    auto large = makeTexture(*ctx, 96, 80, wgpu::TextureFormat::RGBA8Unorm);
    clearTexture(*ctx, small, {0.0, 1.0, 0.0, 1.0});
    clearTexture(*ctx, large, {0.0, 0.0, 1.0, 1.0});
    for (int i = 0; i < 30; ++i) {
        REQUIRE(share.publish(small, 32, 16).has_value());
    }
    REQUIRE(share.publish(large, 96, 80).has_value());
    REQUIRE(client->waitForFrame(2s));
    // Syphon is latest-wins and notifications coalesce: read until the large blue frame shows up.
    std::optional<gpu::Image8> frame;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    for (;;) {
        frame = client->readFrame();
        REQUIRE(frame.has_value());
        if (frame->width == 96 && near(frame->pixel(50, 40), 0, 0, 255)) {
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        client->waitForFrame(100ms);
    }
    REQUIRE(frame->width == 96);
    CHECK(frame->height == 80);
    CHECK(near(frame->pixel(50, 40), 0, 0, 255));
    CHECK(near(frame->pixel(0, 79), 0, 0, 255));
    CHECK(share.stats().framesPublished == 31);
    CHECK(share.stats().width == 96);
    ctx->processEvents();
    CHECK(ctx->errorCount() == 0);
}

#endif // __APPLE__

TEST_CASE("NDI sender publishes a burst of frames when the runtime is installed", "[gpu][share][ndi]") {
    if (!share::TextureShare::available(share::ShareKind::Ndi)) {
        SKIP("NDI runtime not available: " << share::TextureShare::describe());
    }
    auto ctx = makeContext();
    share::TextureShare share;
    share.setFrameRate(60, 1);
    REQUIRE(share.open(share::ShareKind::Ndi, *ctx, "avgen-test").has_value());
    auto texture = makeTexture(*ctx, 64, 48, wgpu::TextureFormat::BGRA8Unorm);
    for (int i = 0; i < 30; ++i) {
        clearTexture(*ctx, texture, {i / 30.0, 0.5, 0.25, 1.0});
        REQUIRE(share.publish(texture, 64, 48).has_value());
        ctx->waitForQueue();
    }
    ctx->processEvents();
    REQUIRE(share.publish(texture, 64, 48).has_value()); // hands the last mapped frames to NDI
    auto stats = share.stats();
    CHECK(stats.lastError.empty());
    CHECK(stats.framesPublished >= 28);
    CHECK(stats.width == 64);
    CHECK(stats.clients >= 0);
    share.close();
    CHECK(ctx->errorCount() == 0);
}

#ifdef __APPLE__
// Hidden probe: how long publish() blocks the caller at 1080p (docs/rendering.md "Sharing").
TEST_CASE("Syphon publish cost at 1920x1080", "[.perf][share][syphon]") {
    if (!share::TextureShare::available(share::ShareKind::Syphon)) {
        SKIP("Syphon not available");
    }
    auto ctx = makeContext();
    if (!ctx->capabilities().sharedTextureIOSurface) {
        SKIP("Dawn device lacks IOSurface shared-texture support");
    }
    share::TextureShare share;
    REQUIRE(share.open(share::ShareKind::Syphon, *ctx, "avgen-perf").has_value());
    auto client = testsupport::SyphonTestClient::connect("avgen-perf", 2s);
    REQUIRE(client != nullptr);
    auto texture = makeTexture(*ctx, 1920, 1080, wgpu::TextureFormat::BGRA8Unorm);
    double total = 0.0;
    double worst = 0.0;
    constexpr int frames = 300;
    for (int i = 0; i < frames; ++i) {
        clearTexture(*ctx, texture, {i / double(frames), 0.5, 0.25, 1.0});
        const auto t0 = std::chrono::steady_clock::now();
        REQUIRE(share.publish(texture, 1920, 1080).has_value());
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        total += ms;
        worst = std::max(worst, ms);
        ctx->processEvents();
    }
    WARN("publish() 1920x1080 BGRA8: avg " << total / frames << " ms, worst " << worst << " ms, received "
                                            << client->framesReceived() << " frames");
    CHECK(total / frames < 1.0);
    CHECK(ctx->errorCount() == 0);
}
#endif
