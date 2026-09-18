#pragma once

// The production post chain, driven directly, with every intermediate it renders read back.
//
// This is `PostProcessor::run` with nothing else attached: no scene, no camera, no clock, no
// renderer. One RGBA16Float texture the CPU wrote goes in, and every target the chain allocated
// comes out, named, at the resolution the chain actually chose. It is the only arrangement in which
// "the post chain produced this pattern" is falsifiable, because the input provably has no pattern
// in it -- and an impulse is the honest test of a filter anyway, since a filter's response to an
// impulse is the filter.
//
// It was written for the Glowmere water lattice (docs/post-artifact-forensics.md) and lived in
// `tests/rendering/test_post_artifact_forensics_gpu.cpp`. It is here because the HDR Lab needs the
// same instrument for a different question, and a second copy of a harness is a second thing that
// can disagree about what the chain did.
//
// The capture handles alias transient-pool entries, so `run()` reads them back before anything else
// renders and hands over decoded floats. Callers never see a live texture.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "gpu/texture.hpp"
#include "gpu/transient_pool.hpp"
#include "rendering/post_processor.hpp"
#include "scene/post_settings.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace avgen::testsupport {

// A device, or a skipped test saying why. Every GPU arm in this repository starts here.
inline std::unique_ptr<gpu::Context> gpuContextOrSkip() {
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

// An RGBA16Float texture the CPU wrote, standing in for the scene's HDR colour.
struct SyntheticHdr {
    wgpu::Texture texture;
    wgpu::TextureView view;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

inline SyntheticHdr makeHdr(gpu::Context& ctx, std::uint32_t width, std::uint32_t height,
                            const std::vector<float>& rgba) {
    SyntheticHdr out;
    out.width = width;
    out.height = height;
    wgpu::TextureDescriptor desc{};
    desc.label = "synthetic-hdr";
    desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::CopySrc;
    desc.dimension = wgpu::TextureDimension::e2D;
    desc.size = {width, height, 1};
    desc.format = rendering::PostProcessor::kHdrFormat;
    out.texture = ctx.device().CreateTexture(&desc);
    out.view = out.texture.CreateView();

    std::vector<std::uint16_t> halves(rgba.size());
    for (std::size_t i = 0; i < rgba.size(); ++i) {
        halves[i] = gpu::floatToHalf(rgba[i]);
    }
    wgpu::TexelCopyTextureInfo dst{};
    dst.texture = out.texture;
    wgpu::TexelCopyBufferLayout layout{};
    layout.bytesPerRow = width * 8;
    layout.rowsPerImage = height;
    wgpu::Extent3D extent{width, height, 1};
    ctx.queue().WriteTexture(&dst, halves.data(), halves.size() * sizeof(std::uint16_t), &layout, &extent);
    return out;
}

// A blank HDR frame with a plotting helper, so each pattern reads as what it is.
struct Canvas {
    std::uint32_t width, height;
    std::vector<float> rgba;
    Canvas(std::uint32_t w, std::uint32_t h)
        : width(w), height(h), rgba(static_cast<std::size_t>(w) * h * 4, 0.0f) {
        for (std::size_t i = 3; i < rgba.size(); i += 4) {
            rgba[i] = 1.0f;
        }
    }
    void set(std::uint32_t x, std::uint32_t y, float value) { set(x, y, value, value, value); }
    void set(std::uint32_t x, std::uint32_t y, float r, float g, float b) {
        if (x >= width || y >= height) {
            return;
        }
        float* px = rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
        px[0] = r;
        px[1] = g;
        px[2] = b;
    }
    void fillRect(std::uint32_t x0, std::uint32_t y0, std::uint32_t w, std::uint32_t h, float value) {
        for (std::uint32_t y = y0; y < y0 + h; ++y) {
            for (std::uint32_t x = x0; x < x0 + w; ++x) {
                set(x, y, value);
            }
        }
    }
};

// One captured intermediate, decoded. `name` is the chain's own label -- "bloom/down3",
// "halation/up0", "wide", "composite" -- and never one this harness invented.
struct CapturedStage {
    std::string name;
    gpu::ImageF image;
};

struct PostBench {
    std::unique_ptr<gpu::Context> ctx;
    std::unique_ptr<gpu::ShaderLibrary> shaders;
    std::unique_ptr<rendering::PostProcessor> post;
    std::unique_ptr<gpu::TransientPool> pool;

    static PostBench make() {
        PostBench b;
        b.ctx = gpuContextOrSkip();
        b.shaders = std::make_unique<gpu::ShaderLibrary>(
            *b.ctx, std::vector{std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
        b.post = std::make_unique<rendering::PostProcessor>(*b.ctx, *b.shaders);
        REQUIRE(b.post->init().has_value());
        b.pool = std::make_unique<gpu::TransientPool>(*b.ctx);
        return b;
    }

    // Runs the chain over `hdr` and reads back every captured stage, plus the input itself as
    // "scene-hdr" so a stage-to-stage comparison starts where the chain does. The readback happens
    // before anything else renders, which is what keeps the transient-pool handles meaningful.
    std::vector<CapturedStage> run(const SyntheticHdr& hdr, const scene::PostSettings& settings) {
        post->armCapture();
        wgpu::CommandEncoder encoder = ctx->device().CreateCommandEncoder();
        rendering::PostFrameInputs in;
        in.sceneHdr = hdr.view;
        in.width = hdr.width;
        in.height = hdr.height;
        in.settings = &settings;
        post->run(encoder, in, *pool);
        wgpu::CommandBuffer commands = encoder.Finish();
        ctx->queue().Submit(1, &commands);
        ctx->waitForQueue();

        rendering::PostCapture capture = post->takeCapture();
        std::vector<CapturedStage> out;
        auto source = gpu::readTextureF16(*ctx, hdr.texture, hdr.width, hdr.height);
        REQUIRE(source.has_value());
        out.push_back(CapturedStage{"scene-hdr", std::move(*source)});
        for (const rendering::PostCaptureStage& stage : capture.stages) {
            auto image = gpu::readTextureF16(*ctx, stage.texture.texture, stage.texture.width, stage.texture.height);
            REQUIRE(image.has_value());
            out.push_back(CapturedStage{stage.name, std::move(*image)});
        }
        pool->endFrame();
        return out;
    }
};

} // namespace avgen::testsupport
