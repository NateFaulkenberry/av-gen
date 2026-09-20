#include "rendering/cosmic_ocean_renderer.hpp"

#include "gpu/context.hpp"
#include "gpu/render_target.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_targets.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace avgen::rendering {
namespace {
// See the note on `nebulaFar` below for why this is not a packed format.
constexpr wgpu::TextureFormat kNebulaFormat = wgpu::TextureFormat::RGBA16Float;
} // namespace

struct CosmicOceanRenderer::Impl {
    gpu::Context& context;
    gpu::ShaderLibrary& shaders;

    wgpu::TextureFormat colorFormat = wgpu::TextureFormat::RGBA16Float;
    wgpu::TextureFormat depthFormat = wgpu::TextureFormat::Depth24Plus;
    wgpu::BindGroupLayout frameLayout;
    wgpu::BindGroupLayout oceanLayout;     // the composite draw: uniform + the two nebula textures
    wgpu::BindGroupLayout nebulaLayout;    // the nebula pass: the uniform alone
    wgpu::PipelineLayout pipelineLayout;
    wgpu::PipelineLayout nebulaPipelineLayout;
    wgpu::RenderPipeline pipeline;
    wgpu::RenderPipeline nebulaPipeline;
    wgpu::Buffer uniforms;
    wgpu::BindGroup bindGroup;             // rebuilt whenever the offscreen pair is recreated
    wgpu::BindGroup nebulaBindGroup;
    wgpu::Sampler nebulaSampler;
    bool live = false;

    // ADR-450. The two nebulae at a fraction of the frame. `RGBA16Float` and NOT a packed format:
    // `RG11B10Ufloat` is samplable everywhere and **renderable only behind an optional device
    // feature**, which is how a sibling branch came to claim a memory saving the hardware had not
    // agreed to. Sixteen-bit float is renderable as a colour attachment on every backend this
    // engine targets, and at a quarter of each axis the pair costs about 2 MB at 1080p -- a saving
    // that does not exist is not worth a capability query. It also has to be float: `coverage` in
    // the alpha channel is fine in 0..1, but the radiance is HDR and a comet-bright nebula filament
    // clipped to 1.0 by a UNORM target would be a silent change to the picture.
    //
    // Two separate targets rather than one array texture, because the pass writes them as two
    // attachments and the composite samples them as two textures; an array would buy one binding
    // and cost a layer index in both places.
    gpu::RenderTarget nebulaFar;
    gpu::RenderTarget nebulaMid;
    std::uint32_t nebulaWidth = 0;
    std::uint32_t nebulaHeight = 0;
    bool nebulaReduced = false;

    // A 1x1 stand-in bound when the lever is off, so the composite's bind group layout never
    // changes and there is only one pipeline to create. A layout that changed with a quality
    // setting would mean recreating the pipeline on a tier change, which is the kind of thing that
    // works until somebody moves the slider mid-render.
    gpu::RenderTarget nebulaDummy;

    Impl(gpu::Context& c, gpu::ShaderLibrary& s) : context(c), shaders(s) {}

    [[nodiscard]] Result<wgpu::RenderPipeline> createPipeline(const wgpu::ShaderModule& module) const {
        // The same additive blend the atmosphere layer uses, on the same two targets. Additive and
        // not `SrcAlpha` because everything this effect produces is *emitted radiance*: a star is a
        // light source and a nebula is glowing gas, and neither occludes what is behind it in any
        // sense a blend factor could express. The flat clear colour the scene starts from is two
        // orders of magnitude below the effect's own deep-space term, so adding rather than
        // replacing changes nothing visible and keeps the composite associative with the comet and
        // the aurora already in the frame.
        wgpu::BlendState additive{};
        additive.color.operation = wgpu::BlendOperation::Add;
        additive.color.srcFactor = wgpu::BlendFactor::One;
        additive.color.dstFactor = wgpu::BlendFactor::One;
        additive.alpha.operation = wgpu::BlendOperation::Add;
        additive.alpha.srcFactor = wgpu::BlendFactor::One;
        additive.alpha.dstFactor = wgpu::BlendFactor::One;

        std::array<wgpu::ColorTargetState, kSceneTargetCount> targets{};
        fillSceneTargets(targets, colorFormat, nullptr);
        for (std::uint32_t i = 0; i < kSceneTargetCount; ++i) {
            // Targets 1, 2 and 4 are masked off, for the reason `water_renderer.cpp` gives: a
            // normal or a velocity averaged over a transparency is worse than none at all, and the
            // identifier target must keep whatever geometry wrote it so picking still works through
            // the sky.
            targets[i].writeMask =
                (i == 0 || i == 3) ? wgpu::ColorWriteMask::All : wgpu::ColorWriteMask::None;
        }
        targets[0].blend = &additive;
        targets[3].blend = &additive;

        wgpu::FragmentState fragment{};
        fragment.module = module;
        fragment.entryPoint = "fs_cosmic";
        fragment.targetCount = kSceneTargetCount;
        fragment.targets = targets.data();

        // Tested so the island and the tree occlude the sky behind them -- which is the whole of
        // "the cosmos must be behind the subject", with no horizon to author because the depth
        // buffer already is one. Never written, so the water and the particles drawn after this
        // still composite over it.
        wgpu::DepthStencilState depth{};
        depth.format = depthFormat;
        depth.depthWriteEnabled = wgpu::OptionalBool::False;
        depth.depthCompare = wgpu::CompareFunction::LessEqual;

        wgpu::RenderPipelineDescriptor desc{};
        desc.label = "cosmic-ocean-pipeline";
        desc.layout = pipelineLayout;
        desc.vertex.module = module;
        desc.vertex.entryPoint = "vs_cosmic";
        desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        desc.primitive.cullMode = wgpu::CullMode::None;
        desc.depthStencil = &depth;
        desc.multisample.count = 1;
        desc.multisample.mask = 0xFFFFFFFFu;
        desc.fragment = &fragment;
        wgpu::RenderPipeline p = context.device().CreateRenderPipeline(&desc);
        if (p == nullptr) {
            return fail("cosmic ocean: failed to create the render pipeline");
        }
        return p;
    }

    // The reduced-resolution nebula pass. Two float attachments, no depth, no blending: it is not
    // compositing anything, it is evaluating a function into a buffer.
    [[nodiscard]] Result<wgpu::RenderPipeline> createNebulaPipeline(const wgpu::ShaderModule& module) const {
        std::array<wgpu::ColorTargetState, 2> targets{};
        for (auto& t : targets) {
            t.format = kNebulaFormat;
            t.writeMask = wgpu::ColorWriteMask::All;
            t.blend = nullptr;
        }
        wgpu::FragmentState fragment{};
        fragment.module = module;
        fragment.entryPoint = "fs_cosmic_nebula";
        fragment.targetCount = targets.size();
        fragment.targets = targets.data();

        wgpu::RenderPipelineDescriptor desc{};
        desc.label = "cosmic-ocean-nebula-pipeline";
        desc.layout = nebulaPipelineLayout;
        desc.vertex.module = module;
        desc.vertex.entryPoint = "vs_cosmic";
        desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        desc.primitive.cullMode = wgpu::CullMode::None;
        desc.depthStencil = nullptr;
        desc.multisample.count = 1;
        desc.multisample.mask = 0xFFFFFFFFu;
        desc.fragment = &fragment;
        wgpu::RenderPipeline p = context.device().CreateRenderPipeline(&desc);
        if (p == nullptr) {
            return fail("cosmic ocean: failed to create the nebula pipeline");
        }
        return p;
    }

    // Recreates the offscreen pair and the composite bind group. Returns false when nothing had to
    // change, which is the common case -- this runs every frame.
    bool resizeNebula(std::uint32_t w, std::uint32_t h, bool reduced) {
        if (reduced == nebulaReduced && w == nebulaWidth && h == nebulaHeight && bindGroup != nullptr) {
            return false;
        }
        nebulaReduced = reduced;
        nebulaWidth = w;
        nebulaHeight = h;
        if (reduced) {
            gpu::RenderTargetDesc d{};
            d.width = w;
            d.height = h;
            d.colorFormat = kNebulaFormat;
            d.depthFormat = wgpu::TextureFormat::Undefined; // nothing to test against; it is a field
            d.extraColorUsage = wgpu::TextureUsage::TextureBinding;
            d.label = "cosmic-nebula-far";
            auto far = gpu::RenderTarget::create(context, d);
            d.label = "cosmic-nebula-mid";
            auto mid = gpu::RenderTarget::create(context, d);
            if (far && mid) {
                nebulaFar = std::move(*far);
                nebulaMid = std::move(*mid);
            } else {
                // Allocation failed: fall back to evaluating in the main draw rather than drawing
                // nothing. A sky that costs more is better than a sky that is missing.
                nebulaReduced = false;
            }
        }
        rebuildBindGroup();
        return true;
    }

    void rebuildBindGroup() {
        const wgpu::TextureView farView =
            nebulaReduced && nebulaFar.valid() ? nebulaFar.colorView() : nebulaDummy.colorView();
        const wgpu::TextureView midView =
            nebulaReduced && nebulaMid.valid() ? nebulaMid.colorView() : nebulaDummy.colorView();
        std::array<wgpu::BindGroupEntry, 4> entries{};
        entries[0].binding = 0;
        entries[0].buffer = uniforms;
        entries[0].size = sizeof(world::CosmicOceanGpu);
        entries[1].binding = 1;
        entries[1].textureView = farView;
        entries[2].binding = 2;
        entries[2].textureView = midView;
        entries[3].binding = 3;
        entries[3].sampler = nebulaSampler;
        wgpu::BindGroupDescriptor desc{};
        desc.label = "cosmic-ocean-bind-group";
        desc.layout = oceanLayout;
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        bindGroup = context.device().CreateBindGroup(&desc);
    }
};

CosmicOceanRenderer::CosmicOceanRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : impl_(std::make_unique<Impl>(context, shaders)) {}
CosmicOceanRenderer::~CosmicOceanRenderer() = default;

bool CosmicOceanRenderer::ready() const { return impl_->pipeline != nullptr; }

Result<void> CosmicOceanRenderer::init(wgpu::TextureFormat colorFormat,
                                       wgpu::TextureFormat depthFormat,
                                       const wgpu::BindGroupLayout& frameLayout) {
    Impl& im = *impl_;
    const auto& device = im.context.device();
    im.colorFormat = colorFormat;
    im.depthFormat = depthFormat;
    im.frameLayout = frameLayout;

    {
        wgpu::BufferDescriptor desc{};
        desc.label = "cosmic-ocean-uniforms";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = sizeof(world::CosmicOceanGpu);
        im.uniforms = device.CreateBuffer(&desc);
        // Zeroed at creation, and the zero block is *off*: `cosmicOceanAt` returns on
        // `master.x <= 1e-5` before it reads anything else. So a frame recorded before the first
        // update cannot draw garbage.
        const world::CosmicOceanGpu zero{};
        im.context.queue().WriteBuffer(im.uniforms, 0, &zero, sizeof(zero));
    }
    {
        // The nebula pass reads the uniform and nothing else. A separate layout from the composite
        // below, and deliberately so: giving that pass the textures it is about to RENDER INTO
        // would be a read-write hazard the validator is right to refuse, and working around it with
        // a dummy binding would hide the hazard rather than remove it.
        std::array<wgpu::BindGroupLayoutEntry, 1> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.minBindingSize = sizeof(world::CosmicOceanGpu);
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "cosmic-ocean-nebula-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        im.nebulaLayout = device.CreateBindGroupLayout(&desc);
    }
    {
        std::array<wgpu::BindGroupLayoutEntry, 4> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.minBindingSize = sizeof(world::CosmicOceanGpu);
        for (std::uint32_t i = 1; i <= 2; ++i) {
            entries[i].binding = i;
            entries[i].visibility = wgpu::ShaderStage::Fragment;
            entries[i].texture.sampleType = wgpu::TextureSampleType::Float;
            entries[i].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        }
        entries[3].binding = 3;
        entries[3].visibility = wgpu::ShaderStage::Fragment;
        entries[3].sampler.type = wgpu::SamplerBindingType::Filtering;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "cosmic-ocean-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        im.oceanLayout = device.CreateBindGroupLayout(&desc);
    }
    {
        // Bilinear, and CLAMPED. The clamp is the edge rule: a bilinear tap at the frame's border
        // reaches half a texel outside the buffer, and `Repeat` would wrap the sky round to the
        // opposite side of the screen -- a seam down two edges of every frame, which is ADR-393's
        // boundary artefact wearing different clothes.
        wgpu::SamplerDescriptor desc{};
        desc.label = "cosmic-nebula-sampler";
        desc.addressModeU = wgpu::AddressMode::ClampToEdge;
        desc.addressModeV = wgpu::AddressMode::ClampToEdge;
        desc.addressModeW = wgpu::AddressMode::ClampToEdge;
        desc.magFilter = wgpu::FilterMode::Linear;
        desc.minFilter = wgpu::FilterMode::Linear;
        desc.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
        desc.maxAnisotropy = 1;
        im.nebulaSampler = device.CreateSampler(&desc);
    }
    {
        // The 1x1 stand-in, so the composite layout is satisfied even with the lever off.
        gpu::RenderTargetDesc d{};
        d.width = 1;
        d.height = 1;
        d.colorFormat = kNebulaFormat;
        d.depthFormat = wgpu::TextureFormat::Undefined;
        d.extraColorUsage = wgpu::TextureUsage::TextureBinding;
        d.label = "cosmic-nebula-dummy";
        auto dummy = gpu::RenderTarget::create(im.context, d);
        if (!dummy) {
            return std::unexpected(dummy.error());
        }
        im.nebulaDummy = std::move(*dummy);
    }
    {
        const std::array<wgpu::BindGroupLayout, 2> layouts = {im.frameLayout, im.oceanLayout};
        wgpu::PipelineLayoutDescriptor desc{};
        desc.label = "cosmic-ocean-pipeline-layout";
        desc.bindGroupLayoutCount = layouts.size();
        desc.bindGroupLayouts = layouts.data();
        im.pipelineLayout = device.CreatePipelineLayout(&desc);
    }
    {
        const std::array<wgpu::BindGroupLayout, 2> layouts = {im.frameLayout, im.nebulaLayout};
        wgpu::PipelineLayoutDescriptor desc{};
        desc.label = "cosmic-ocean-nebula-pipeline-layout";
        desc.bindGroupLayoutCount = layouts.size();
        desc.bindGroupLayouts = layouts.data();
        im.nebulaPipelineLayout = device.CreatePipelineLayout(&desc);
    }
    {
        wgpu::BindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer = im.uniforms;
        entry.size = sizeof(world::CosmicOceanGpu);
        wgpu::BindGroupDescriptor desc{};
        desc.label = "cosmic-ocean-nebula-bind-group";
        desc.layout = im.nebulaLayout;
        desc.entryCount = 1;
        desc.entries = &entry;
        im.nebulaBindGroup = device.CreateBindGroup(&desc);
    }
    im.rebuildBindGroup();

    auto module = im.shaders.load("cosmic_ocean.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    auto pipeline = im.createPipeline(*module);
    if (!pipeline) {
        return std::unexpected(pipeline.error());
    }
    im.pipeline = *pipeline;
    auto nebula = im.createNebulaPipeline(*module);
    if (!nebula) {
        return std::unexpected(nebula.error());
    }
    im.nebulaPipeline = *nebula;
    return {};
}

Result<void> CosmicOceanRenderer::reload() {
    Impl& im = *impl_;
    auto module = im.shaders.load("cosmic_ocean.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    // The old pipeline is kept on failure, so a typo saved mid-session leaves the last good sky on
    // screen with an error in the log rather than a black frame with none.
    auto pipeline = im.createPipeline(*module);
    if (!pipeline) {
        return std::unexpected(pipeline.error());
    }
    auto nebula = im.createNebulaPipeline(*module);
    if (!nebula) {
        return std::unexpected(nebula.error());
    }
    im.pipeline = *pipeline;
    im.nebulaPipeline = *nebula;
    return {};
}

void CosmicOceanRenderer::update(const world::CosmicOceanGpu& block, bool live,
                                 std::uint32_t dropped, float nebulaScale, std::uint32_t width,
                                 std::uint32_t height) {
    Impl& im = *impl_;
    stats_.dropped = dropped;
    im.live = live && im.pipeline != nullptr;
    stats_.drawn = im.live;

    // The reduced buffer, sized before anything is recorded. A floor of 64 px on each axis and a
    // requirement that the frame be at least twice the reduced size: at a thumbnail resolution the
    // pair would cost an allocation and two passes to save nothing, and a 320x200 GPU test would
    // be measuring the overhead rather than the effect.
    const float scale = std::clamp(nebulaScale, 0.05f, 1.0f);
    const std::uint32_t w = std::max(static_cast<std::uint32_t>(std::lround(width * scale)), 64u);
    const std::uint32_t h = std::max(static_cast<std::uint32_t>(std::lround(height * scale)), 64u);
    const bool reduced = im.live && scale < 0.999f && width >= w * 2 && height >= h * 2;
    im.resizeNebula(reduced ? w : 0, reduced ? h : 0, reduced);
    stats_.nebulaReduced = im.nebulaReduced;
    stats_.nebulaWidth = im.nebulaReduced ? im.nebulaWidth : 0;
    stats_.nebulaHeight = im.nebulaReduced ? im.nebulaHeight : 0;

    if (!im.live) {
        // Deliberately no upload. The `--disable cosmic` arm has to remove the fragment work *and*
        // the uniform content, or it is measuring the same frame through one more branch -- which
        // is the vacuous A/B ADR-182 names and `scene_renderer.cpp` already guards against for the
        // atmospheric layer.
        return;
    }
    im.context.queue().WriteBuffer(im.uniforms, 0, &block, sizeof(block));
}

void CosmicOceanRenderer::renderNebula(wgpu::CommandEncoder& encoder,
                                      const wgpu::BindGroup& frameBindGroup) {
    Impl& im = *impl_;
    if (!im.live || !im.nebulaReduced || im.nebulaPipeline == nullptr || !im.nebulaFar.valid()) {
        return;
    }
    std::array<wgpu::RenderPassColorAttachment, 2> colour{};
    colour[0].view = im.nebulaFar.colorView();
    colour[1].view = im.nebulaMid.colorView();
    for (auto& c : colour) {
        // Cleared, not loaded: the pass writes every pixel it is asked about, and `Load` on a
        // TBDR GPU pulls the previous contents into tile memory for nothing.
        c.loadOp = wgpu::LoadOp::Clear;
        c.storeOp = wgpu::StoreOp::Store;
        c.clearValue = {0.0, 0.0, 0.0, 0.0};
    }
    wgpu::RenderPassDescriptor desc{};
    desc.label = "cosmic-nebula-pass";
    desc.colorAttachmentCount = colour.size();
    desc.colorAttachments = colour.data();
    wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&desc);
    rp.SetPipeline(im.nebulaPipeline);
    rp.SetBindGroup(0, frameBindGroup);
    rp.SetBindGroup(1, im.nebulaBindGroup);
    rp.Draw(3);
    rp.End();
}

void CosmicOceanRenderer::draw(wgpu::RenderPassEncoder& pass) {
    Impl& im = *impl_;
    if (!im.live) {
        return;
    }
    pass.SetPipeline(im.pipeline);
    pass.SetBindGroup(1, im.bindGroup);
    pass.Draw(3);
}

} // namespace avgen::rendering
