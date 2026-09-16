#include "gpu/surface.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"

namespace avgen::gpu {

Surface::Surface(Context& context, wgpu::Surface surface) : context_(context), surface_(std::move(surface)) {}

Surface::~Surface() {
    if (surface_ && configured_) {
        surface_.Unconfigure();
    }
}

Result<std::unique_ptr<Surface>> Surface::create(Context& context, void* metalLayer, std::uint32_t width,
                                                 std::uint32_t height) {
    if (metalLayer == nullptr) {
        return fail("Surface::create: null Metal layer");
    }
    wgpu::SurfaceSourceMetalLayer metalSource{};
    metalSource.layer = metalLayer;
    wgpu::SurfaceDescriptor surfaceDesc{};
    surfaceDesc.nextInChain = &metalSource;
    surfaceDesc.label = "avgen-output-surface";
    wgpu::Surface raw = context.instance().CreateSurface(&surfaceDesc);
    if (!raw) {
        return fail("failed to create a WebGPU surface from the Metal layer");
    }
    std::unique_ptr<Surface> surface(new Surface(context, std::move(raw)));
    if (auto r = surface->chooseFormat(); !r) {
        return std::unexpected(r.error());
    }
    surface->configure(width, height);
    return surface;
}

Result<void> Surface::chooseFormat() {
    wgpu::SurfaceCapabilities caps{};
    if (surface_.GetCapabilities(context_.adapter(), &caps) != wgpu::Status::Success || caps.formatCount == 0) {
        return fail("surface reports no supported formats");
    }
    format_ = caps.formats[0];
    for (std::size_t i = 0; i < caps.formatCount; ++i) {
        if (caps.formats[i] == wgpu::TextureFormat::BGRA8Unorm) {
            format_ = wgpu::TextureFormat::BGRA8Unorm;
            break;
        }
    }
    return {};
}

void Surface::configure(std::uint32_t width, std::uint32_t height) {
    if (!surface_ || width == 0 || height == 0) {
        return;
    }
    if (configured_ && width == width_ && height == height_) {
        return;
    }
    wgpu::SurfaceConfiguration config{};
    config.device = context_.device();
    config.format = format_;
    // CopySrc so the finished frame -- interface and all -- can be read back for a capture. The
    // scene has `renderToImage`; the *editor* has only this texture, because that is where Dear
    // ImGui draws. Costs nothing when nothing copies from it.
    config.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
    config.width = width;
    config.height = height;
    config.alphaMode = wgpu::CompositeAlphaMode::Auto;
    config.presentMode = wgpu::PresentMode::Fifo;
    surface_.Configure(&config);
    width_ = width;
    height_ = height;
    configured_ = true;
    log::debug("surface configured {}x{}", width, height);
}

Result<wgpu::TextureView> Surface::acquire() {
    if (!surface_ || !configured_) {
        return fail("no configured surface");
    }
    for (int attempt = 0; attempt < 2; ++attempt) {
        wgpu::SurfaceTexture surfaceTexture{};
        surface_.GetCurrentTexture(&surfaceTexture);
        switch (surfaceTexture.status) {
        case wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal:
        case wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal: {
            current_ = surfaceTexture.texture;
            wgpu::TextureViewDescriptor viewDesc{};
            viewDesc.label = "swapchain-view";
            viewDesc.format = format_;
            viewDesc.dimension = wgpu::TextureViewDimension::e2D;
            return surfaceTexture.texture.CreateView(&viewDesc);
        }
        case wgpu::SurfaceGetCurrentTextureStatus::Timeout:
        case wgpu::SurfaceGetCurrentTextureStatus::Outdated:
        case wgpu::SurfaceGetCurrentTextureStatus::Lost: {
            // Reconfigure once and retry.
            configured_ = false;
            const auto w = width_;
            const auto h = height_;
            width_ = 0;
            configure(w, h);
            break;
        }
        case wgpu::SurfaceGetCurrentTextureStatus::Error:
        default:
            return fail("surface texture acquisition error");
        }
    }
    return fail("surface texture unavailable after reconfigure");
}

void Surface::present() {
    if (surface_ && configured_) {
        surface_.Present();
    }
}

} // namespace avgen::gpu
