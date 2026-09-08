#include "gpu/render_target.hpp"

#include "gpu/context.hpp"

namespace avgen::gpu {

Result<RenderTarget> RenderTarget::create(Context& context, const RenderTargetDesc& desc) {
    if (desc.width == 0 || desc.height == 0) {
        return fail("render target size must be non-zero ({}x{})", desc.width, desc.height);
    }
    RenderTarget target;
    target.desc_ = desc;

    wgpu::TextureDescriptor colorDesc{};
    colorDesc.label = desc.label;
    colorDesc.usage = wgpu::TextureUsage::RenderAttachment | desc.extraColorUsage;
    colorDesc.dimension = wgpu::TextureDimension::e2D;
    colorDesc.size = {desc.width, desc.height, 1};
    colorDesc.format = desc.colorFormat;
    target.color_ = context.device().CreateTexture(&colorDesc);
    if (!target.color_) {
        return fail("failed to create colour texture {}x{}", desc.width, desc.height);
    }
    target.colorView_ = target.color_.CreateView();

    if (desc.depthFormat != wgpu::TextureFormat::Undefined) {
        wgpu::TextureDescriptor depthDesc{};
        depthDesc.label = "render-target-depth";
        depthDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
        depthDesc.dimension = wgpu::TextureDimension::e2D;
        depthDesc.size = {desc.width, desc.height, 1};
        depthDesc.format = desc.depthFormat;
        target.depth_ = context.device().CreateTexture(&depthDesc);
        if (!target.depth_) {
            return fail("failed to create depth texture {}x{}", desc.width, desc.height);
        }
        target.depthView_ = target.depth_.CreateView();
    }
    return target;
}

} // namespace avgen::gpu
