#pragma once

// Offscreen colour (+ optional depth) target. The scene always renders offscreen first
// (offline-rendering research §12): the swapchain is just one possible sink.

#include "core/error.hpp"

#include <webgpu/webgpu_cpp.h>

#include <cstdint>

namespace avgen::gpu {

class Context;

struct RenderTargetDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    wgpu::TextureFormat colorFormat = wgpu::TextureFormat::RGBA16Float;
    wgpu::TextureFormat depthFormat = wgpu::TextureFormat::Depth24Plus; // Undefined = no depth
    wgpu::TextureUsage extraColorUsage = wgpu::TextureUsage::TextureBinding;
    const char* label = "render-target";
};

class RenderTarget {
public:
    RenderTarget() = default;
    static Result<RenderTarget> create(Context& context, const RenderTargetDesc& desc);

    [[nodiscard]] bool valid() const { return color_ != nullptr; }
    [[nodiscard]] std::uint32_t width() const { return desc_.width; }
    [[nodiscard]] std::uint32_t height() const { return desc_.height; }
    [[nodiscard]] wgpu::TextureFormat colorFormat() const { return desc_.colorFormat; }
    [[nodiscard]] wgpu::TextureFormat depthFormat() const { return desc_.depthFormat; }
    [[nodiscard]] bool hasDepth() const { return depth_ != nullptr; }
    [[nodiscard]] const wgpu::Texture& colorTexture() const { return color_; }
    [[nodiscard]] const wgpu::TextureView& colorView() const { return colorView_; }
    [[nodiscard]] const wgpu::TextureView& depthView() const { return depthView_; }

private:
    RenderTargetDesc desc_{};
    wgpu::Texture color_;
    wgpu::TextureView colorView_;
    wgpu::Texture depth_;
    wgpu::TextureView depthView_;
};

// A view someone else owns (the swapchain, or a test's readback texture) that a pass renders into.
struct TargetView {
    wgpu::TextureView view;
    wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

} // namespace avgen::gpu
