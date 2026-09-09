// Milestone 1.2: OutputMapper on the GPU. A 64x64 gradient is mapped to a 64x64 target and read
// back: identity, crop, flip, blend margins, projective warp. Skipped without an adapter.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/render_target.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/output_mapper.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;

namespace {
constexpr std::uint32_t kSize = 64;

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

// r = x * 4, g = y * 4, b = 128: every pixel identifies its source position.
gpu::Image8 gradient() {
    gpu::Image8 img;
    img.width = kSize;
    img.height = kSize;
    img.rgba.resize(kSize * kSize * 4);
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            auto* px = img.rgba.data() + (y * kSize + x) * 4;
            px[0] = static_cast<std::uint8_t>(x * 4);
            px[1] = static_cast<std::uint8_t>(y * 4);
            px[2] = 128;
            px[3] = 255;
        }
    }
    return img;
}

struct Source {
    wgpu::Texture texture;
    wgpu::TextureView view;
};

Source uploadSource(gpu::Context& ctx, const gpu::Image8& img) {
    wgpu::TextureDescriptor desc{};
    desc.label = "output-source";
    desc.size = {img.width, img.height, 1};
    desc.format = wgpu::TextureFormat::RGBA8Unorm;
    desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    Source s;
    s.texture = ctx.device().CreateTexture(&desc);
    wgpu::TexelCopyTextureInfo dst{};
    dst.texture = s.texture;
    wgpu::TexelCopyBufferLayout layout{};
    layout.bytesPerRow = img.width * 4;
    layout.rowsPerImage = img.height;
    wgpu::Extent3D extent{img.width, img.height, 1};
    ctx.queue().WriteTexture(&dst, img.rgba.data(), img.rgba.size(), &layout, &extent);
    s.view = s.texture.CreateView();
    return s;
}

struct Fixture {
    std::unique_ptr<gpu::Context> ctx;
    std::unique_ptr<gpu::ShaderLibrary> shaders;
    std::unique_ptr<rendering::OutputMapper> mapper;
    Source source;
    gpu::Image8 sourceImage;

    Fixture() : ctx(makeContext()) {
        shaders = std::make_unique<gpu::ShaderLibrary>(*ctx, std::vector<std::filesystem::path>{AVGEN_SHADER_SOURCE_DIR});
        mapper = std::make_unique<rendering::OutputMapper>(*ctx, *shaders);
        REQUIRE(mapper->init().has_value());
        sourceImage = gradient();
        source = uploadSource(*ctx, sourceImage);
    }

    gpu::Image8 map(const rendering::OutputMapping& mapping, const char* dumpName = nullptr) {
        gpu::RenderTargetDesc desc{};
        desc.width = kSize;
        desc.height = kSize;
        desc.colorFormat = wgpu::TextureFormat::RGBA8Unorm;
        desc.depthFormat = wgpu::TextureFormat::Undefined;
        desc.extraColorUsage = wgpu::TextureUsage::CopySrc;
        desc.label = "output-target";
        auto target = gpu::RenderTarget::create(*ctx, desc);
        REQUIRE(target.has_value());
        wgpu::CommandEncoder encoder = ctx->device().CreateCommandEncoder();
        REQUIRE(mapper->draw(encoder, source.view, target->colorView(), kSize, kSize, mapping,
                             wgpu::TextureFormat::RGBA8Unorm)
                    .has_value());
        wgpu::CommandBuffer commands = encoder.Finish();
        ctx->queue().Submit(1, &commands);
        auto image = gpu::readTexture8(*ctx, target->colorTexture(), kSize, kSize, false);
        REQUIRE(image.has_value());
        if (const char* dumpDir = std::getenv("AVGEN_DUMP_DIR"); dumpDir != nullptr && dumpName != nullptr) {
            REQUIRE(gpu::writePpm(*image, std::filesystem::path(dumpDir) / dumpName).has_value());
        }
        return *image;
    }
};

int maxChannelDiff(const std::uint8_t* a, const std::uint8_t* b) {
    int d = 0;
    for (int i = 0; i < 3; ++i) {
        d = std::max(d, std::abs(int(a[i]) - int(b[i])));
    }
    return d;
}

bool near(int a, int b, int tol) { return std::abs(a - b) <= tol; }
} // namespace

TEST_CASE("OutputMapper identity reproduces the source through both paths", "[gpu][outputs]") {
    Fixture f;
    const auto blit = f.map(rendering::OutputMapping::identity(), "output_identity.ppm");
    int worst = 0;
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            worst = std::max(worst, maxChannelDiff(blit.pixel(x, y), f.sourceImage.pixel(x, y)));
        }
    }
    CHECK(worst <= 1);
    CHECK(f.mapper->drawCount() == 1);

    // The full mapping path with an identity mapping gives the same image.
    f.mapper->setFastPathEnabled(false);
    const auto full = f.map(rendering::OutputMapping::identity());
    worst = 0;
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            worst = std::max(worst, maxChannelDiff(full.pixel(x, y), f.sourceImage.pixel(x, y)));
        }
    }
    CHECK(worst <= 1);
    CHECK(f.ctx->errorCount() == 0);
}

TEST_CASE("OutputMapper crop fills the target with the centre quarter", "[gpu][outputs]") {
    Fixture f;
    rendering::OutputMapping m;
    m.crop = {0.25f, 0.25f, 0.5f, 0.5f};
    const auto img = f.map(m, "output_crop.ppm");
    // Target pixel centres map to source x = 16 + (x + 0.5) * 0.5 - 0.5 ... within a texel.
    CHECK(near(img.pixel(0, 0)[0], 16 * 4, 6));
    CHECK(near(img.pixel(0, 0)[1], 16 * 4, 6));
    CHECK(near(img.pixel(63, 63)[0], 48 * 4, 6));
    CHECK(near(img.pixel(63, 63)[1], 48 * 4, 6));
    CHECK(near(img.pixel(32, 32)[0], f.sourceImage.pixel(32, 32)[0], 4));
    CHECK(near(img.pixel(32, 32)[1], f.sourceImage.pixel(32, 32)[1], 4));
    // Magnified by two: neighbouring target pixels differ by ~2 in red instead of 4.
    CHECK(near(int(img.pixel(40, 10)[0]) - int(img.pixel(30, 10)[0]), 20, 4));
    CHECK(f.ctx->errorCount() == 0);
}

TEST_CASE("OutputMapper flips mirror the source", "[gpu][outputs]") {
    Fixture f;
    rendering::OutputMapping m;
    m.flipX = true;
    const auto fx = f.map(m, "output_flipx.ppm");
    for (std::uint32_t x = 0; x < kSize; x += 7) {
        CHECK(maxChannelDiff(fx.pixel(x, 20), f.sourceImage.pixel(kSize - 1 - x, 20)) <= 1);
    }
    m.flipX = false;
    m.flipY = true;
    const auto fy = f.map(m, "output_flipy.ppm");
    for (std::uint32_t y = 0; y < kSize; y += 7) {
        CHECK(maxChannelDiff(fy.pixel(20, y), f.sourceImage.pixel(20, kSize - 1 - y)) <= 1);
    }
    CHECK(f.ctx->errorCount() == 0);
}

TEST_CASE("OutputMapper blend margins darken the edges with a monotone ramp", "[gpu][outputs]") {
    Fixture f;
    rendering::OutputMapping m;
    m.blend.left = 0.5f;
    m.blend.top = 0.25f;
    m.blendGamma = 1.0f;
    const auto img = f.map(m, "output_blend.ppm");
    // Blue is constant 128 in the source, so it exposes the weight directly.
    CHECK(img.pixel(0, 40)[2] < 8);            // at the left edge: ~0
    CHECK(img.pixel(16, 40)[2] > 50);          // halfway through the margin: ~64
    CHECK(img.pixel(16, 40)[2] < 80);
    CHECK(near(img.pixel(40, 40)[2], 128, 3)); // past the margin: full
    CHECK(near(img.pixel(63, 63)[2], 128, 3));
    for (std::uint32_t x = 1; x < 40; ++x) {
        CHECK(img.pixel(x, 40)[2] >= img.pixel(x - 1, 40)[2]);
    }
    CHECK(img.pixel(40, 0)[2] < 12);           // top margin
    CHECK(img.pixel(40, 8)[2] < img.pixel(40, 15)[2]);
    // The top-left corner is the product of both margins.
    CHECK(img.pixel(8, 4)[2] < img.pixel(8, 40)[2]);
    // Gamma 2.2 makes the ramp darker at the same position.
    m.blendGamma = 2.2f;
    const auto gam = f.map(m);
    CHECK(gam.pixel(16, 40)[2] < img.pixel(16, 40)[2] - 20);
    CHECK(f.ctx->errorCount() == 0);
}

TEST_CASE("OutputMapper warp narrows the top row and blacks out the outer corners", "[gpu][outputs]") {
    Fixture f;
    rendering::OutputMapping m;
    m.corners = {{{0.25f, 0.0f}, {0.75f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}};
    const auto img = f.map(m, "output_warp.ppm");
    // Top row: outer quarters are black, the middle shows source content from the top row.
    CHECK(img.pixel(2, 0)[2] == 0);
    CHECK(img.pixel(61, 0)[2] == 0);
    CHECK(near(img.pixel(32, 0)[2], 128, 3));
    CHECK(img.pixel(32, 0)[1] < 12);   // top of the source (g ~ 0)
    // The source's top-left corner lands near target x = 16.
    CHECK(img.pixel(14, 0)[2] == 0);
    CHECK(near(img.pixel(18, 0)[2], 128, 3));
    CHECK(img.pixel(18, 0)[0] < 20);   // left of the source (r ~ 0)
    // Bottom row is full width and shows the source bottom row.
    CHECK(near(img.pixel(1, 63)[2], 128, 3));
    CHECK(near(img.pixel(62, 63)[2], 128, 3));
    CHECK(img.pixel(32, 63)[1] > 240);
    // Perspective-correct: the quad's diagonals cross at target y = 1/3, so the source's vertical
    // centre (g = 128) lands on row ~21 and the target's middle row is already further down the
    // source (an affine stretch would put g = 128 on row 32).
    CHECK(near(img.pixel(32, 21)[1], 128, 12));
    CHECK(img.pixel(32, 32)[1] > 140);
    CHECK(f.ctx->errorCount() == 0);
}

TEST_CASE("OutputMapper brightness and gamma scale the colour", "[gpu][outputs]") {
    Fixture f;
    rendering::OutputMapping m;
    m.brightness = 0.5f;
    const auto half = f.map(m);
    CHECK(near(half.pixel(32, 32)[2], 64, 3));
    m.brightness = 1.0f;
    m.gamma = 2.0f; // out = in^(1/2): 0.5 -> 0.707
    const auto lifted = f.map(m);
    CHECK(near(lifted.pixel(32, 32)[2], 180, 4));
    CHECK(f.ctx->errorCount() == 0);
}
