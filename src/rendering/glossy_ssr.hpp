#pragma once

// ADR-1820: glossy screen-space reflections, the resources beside the lit pipeline that draws them.
//
// The march (shaders/glossy_ssr.wgsl) reads this frame's linear view depth at full resolution and, above it, a min-depth
// pyramid this class builds after the depth prepass (shaders/hiz_reduce.wgsl). The SSR draws bind the IBL group
// extended with that pyramid (binding 6) and the parameters (binding 7) as their group 3, through their own pipeline
// layout: the scene pipeline layout, and every other pipeline's group 3, are unchanged.
//
// It owns nothing of the water's reflection (ADR-1286) and shares no code with it; the frame it reads hits from is
// ADR-1603's refraction copy, read-only.

#include "core/error.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace avgen::rendering {

// What the march is allowed: authored defaults, not per scene (a scene opts in with `environment.glossyReflections`).
struct GlossySsrSettings {
    float reach = 30.0f;            // metres a ray may travel
    float roughnessFadeStart = 0.25f; // full trust below this roughness...
    float roughnessFadeEnd = 0.45f;   // ...none above this (and the entity is not drawn again at all)
    std::uint32_t iterations = 64;  // Hi-Z steps per ray
    float thicknessPerMetre = 0.03f; // a surface is this thick per metre of view depth...
    float thicknessFloor = 0.15f;    // ...and at least this (m): a ray behind it by more passes under it
    std::uint32_t maxLevels = 7;    // pyramid levels above the full-resolution depth
};

class GlossySsr {
public:
    GlossySsr(gpu::Context& context, gpu::ShaderLibrary& shaders);

    // The layouts: the IBL group's six entries (as `iblLayout`), plus the pyramid and the parameters.
    [[nodiscard]] Result<void> init(const wgpu::BindGroupLayout& frameLayout, const wgpu::BindGroupLayout& objectLayout,
                                    const wgpu::BindGroupLayout& materialLayout);
    [[nodiscard]] const wgpu::PipelineLayout& pipelineLayout() const { return pipelineLayout_; }

    // Builds the pyramid from the full-resolution linear depth (`width` x `height`), one render pass per level.
    // `mark` names each pass for the frame timeline (may return null).
    using Mark = std::function<const wgpu::PassTimestampWrites*()>;
    void encodePyramid(wgpu::CommandEncoder& encoder, const wgpu::TextureView& linearDepth, std::uint32_t width,
                       std::uint32_t height, const Mark& mark);

    // Group 3 for the SSR draws: `ibl` are the IBL group's six entries as the scene binds them this frame.
    [[nodiscard]] const wgpu::BindGroup& bindGroup(const std::array<wgpu::BindGroupEntry, 6>& ibl);

    [[nodiscard]] const GlossySsrSettings& settings() const { return settings_; }
    [[nodiscard]] std::uint32_t levels() const { return levels_; }

private:
    Result<void> ensurePyramid(std::uint32_t width, std::uint32_t height);
    void writeParams();

    gpu::Context& context_;
    gpu::ShaderLibrary& shaders_;
    GlossySsrSettings settings_;
    wgpu::BindGroupLayout groupLayout_;   // group 3 of the SSR draws
    wgpu::PipelineLayout pipelineLayout_; // frame, object, material, groupLayout_
    wgpu::BindGroupLayout reduceLayout_;
    wgpu::RenderPipeline reducePipeline_;
    wgpu::Buffer params_;
    wgpu::Texture pyramid_;               // levels 1.. of the pyramid as mips 0..
    wgpu::TextureView pyramidView_;       // every mip, for the march
    std::vector<wgpu::TextureView> mipViews_;
    std::vector<wgpu::BindGroup> reduceGroups_; // [0] reads the linear depth, [k] reads mip k-1
    wgpu::TextureView reduceSource_;      // the linear-depth view reduceGroups_[0] was built on
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t levels_ = 0;
    wgpu::BindGroup group_;
    std::array<WGPUTextureView, 6> groupIbl_{}; // the IBL views group_ was built on
    WGPUSampler groupSampler0_ = nullptr;
    WGPUSampler groupSampler5_ = nullptr;
    WGPUTextureView groupPyramid_ = nullptr;
};

} // namespace avgen::rendering
