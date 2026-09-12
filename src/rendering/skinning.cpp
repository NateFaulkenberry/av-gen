#include "rendering/skinning.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_targets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace avgen::rendering {

namespace {

// The dynamic-offset alignment WebGPU guarantees for uniform and storage buffers. A slice is
// rounded up to it so both offsets in a skinned bind group stay legal.
constexpr std::uint32_t kOffsetAlignment = 256;
constexpr std::uint32_t kMatrixBytes = 64;
// A ceiling on how many rigs a frame may draw. Beyond it the extra rigs simply do not get a
// palette, and their entities render in the bind pose rather than the frame failing.
constexpr std::uint32_t kMaxRigs = 64;

std::uint32_t alignUp(std::uint32_t value, std::uint32_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

bool finitePalette(const std::vector<glm::mat4>& palette) {
    for (const glm::mat4& matrix : palette) {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (!std::isfinite(matrix[column][row])) {
                    return false;
                }
            }
        }
    }
    return true;
}

// The skinned vertex layout: the scene's own Vertex in slot 0, exactly as the static pipelines
// take it, and the influence stream in slot 1.
struct SkinnedVertexLayout {
    std::array<wgpu::VertexAttribute, 3> base{};
    std::array<wgpu::VertexAttribute, 2> influence{};
    std::array<wgpu::VertexBufferLayout, 2> layouts{};
    SkinnedVertexLayout() {
        base[0].format = wgpu::VertexFormat::Float32x3;
        base[0].offset = offsetof(scene::Vertex, position);
        base[0].shaderLocation = 0;
        base[1].format = wgpu::VertexFormat::Float32x3;
        base[1].offset = offsetof(scene::Vertex, normal);
        base[1].shaderLocation = 1;
        base[2].format = wgpu::VertexFormat::Float32x2;
        base[2].offset = offsetof(scene::Vertex, uv);
        base[2].shaderLocation = 2;
        influence[0].format = wgpu::VertexFormat::Uint16x4;
        influence[0].offset = offsetof(scene::SkinInfluence, joints);
        influence[0].shaderLocation = 3;
        influence[1].format = wgpu::VertexFormat::Float32x4;
        influence[1].offset = offsetof(scene::SkinInfluence, weights);
        influence[1].shaderLocation = 4;
        layouts[0].arrayStride = sizeof(scene::Vertex);
        layouts[0].stepMode = wgpu::VertexStepMode::Vertex;
        layouts[0].attributeCount = base.size();
        layouts[0].attributes = base.data();
        layouts[1].arrayStride = sizeof(scene::SkinInfluence);
        layouts[1].stepMode = wgpu::VertexStepMode::Vertex;
        layouts[1].attributeCount = influence.size();
        layouts[1].attributes = influence.data();
    }
};
static_assert(sizeof(scene::SkinInfluence) == 24);

} // namespace

SkinningRenderer::SkinningRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : context_(context)
    , shaders_(shaders) {}

Result<void> SkinningRenderer::init(const wgpu::BindGroupLayout& frameLayout,
                                    const wgpu::BindGroupLayout& materialLayout,
                                    const wgpu::BindGroupLayout& iblLayout, const wgpu::Buffer& objectUniforms,
                                    std::uint64_t objectSize, wgpu::TextureFormat colorFormat,
                                    wgpu::TextureFormat depthFormat) {
    objectUniforms_ = objectUniforms;
    objectSize_ = objectSize;
    colorFormat_ = colorFormat;
    depthFormat_ = depthFormat;
    const auto& device = context_.device();

    std::array<wgpu::BindGroupLayoutEntry, 2> entries{};
    entries[0].binding = 0;
    entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    entries[0].buffer.hasDynamicOffset = true;
    entries[0].buffer.minBindingSize = objectSize;
    entries[1].binding = 1;
    entries[1].visibility = wgpu::ShaderStage::Vertex;
    entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    entries[1].buffer.hasDynamicOffset = true;
    entries[1].buffer.minBindingSize = kMatrixBytes;
    wgpu::BindGroupLayoutDescriptor layoutDesc{};
    layoutDesc.label = "skinned-object-layout";
    layoutDesc.entryCount = entries.size();
    layoutDesc.entries = entries.data();
    objectLayout_ = device.CreateBindGroupLayout(&layoutDesc);

    const std::array<wgpu::BindGroupLayout, 4> groups = {frameLayout, objectLayout_, materialLayout, iblLayout};
    wgpu::PipelineLayoutDescriptor pipelineDesc{};
    pipelineDesc.label = "skinned-pipeline-layout";
    pipelineDesc.bindGroupLayoutCount = groups.size();
    pipelineDesc.bindGroupLayouts = groups.data();
    pipelineLayout_ = device.CreatePipelineLayout(&pipelineDesc);

    auto module = shaders_.load("pbr_skinned.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    module_ = *module;
    if (auto r = createPipelines(module_); !r) {
        return r;
    }
    // A one-slice buffer so the bind group exists before any rig does; update() grows it.
    ensureBuffer(kOffsetAlignment, 1);
    ready_ = true;
    return {};
}

Result<void> SkinningRenderer::reload() {
    auto module = shaders_.load("pbr_skinned.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    const wgpu::RenderPipeline previousCull = opaqueCull_;
    const wgpu::RenderPipeline previousNoCull = opaqueNoCull_;
    const wgpu::RenderPipeline previousBlend = blend_;
    const wgpu::RenderPipeline previousDepth = depthOnly_;
    if (auto r = createPipelines(*module); !r) {
        opaqueCull_ = previousCull;
        opaqueNoCull_ = previousNoCull;
        blend_ = previousBlend;
        depthOnly_ = previousDepth;
        return r;
    }
    module_ = *module;
    return {};
}

Result<void> SkinningRenderer::createPipelines(const wgpu::ShaderModule& module) {
    SkinnedVertexLayout vertex;
    wgpu::BlendState blend{};
    blend.color.operation = wgpu::BlendOperation::Add;
    blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
    blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.alpha.operation = wgpu::BlendOperation::Add;
    blend.alpha.srcFactor = wgpu::BlendFactor::One;
    blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;

    const auto make = [&](const char* label, bool isBlend, bool doubleSided,
                          bool depthOnly) -> Result<wgpu::RenderPipeline> {
        std::array<wgpu::ColorTargetState, kSceneTargetCount> colorTargets{};
        fillSceneTargets(colorTargets, colorFormat_, isBlend ? &blend : nullptr,
                         isBlend ? wgpu::ColorWriteMask::None : wgpu::ColorWriteMask::All);
        wgpu::FragmentState fragment{};
        fragment.module = module;
        fragment.entryPoint = depthOnly ? "fs_depth" : "fs_main";
        fragment.targetCount = depthOnly ? 0 : kSceneTargetCount;
        fragment.targets = depthOnly ? nullptr : colorTargets.data();
        wgpu::DepthStencilState depth{};
        depth.format = depthFormat_;
        depth.depthWriteEnabled = isBlend ? wgpu::OptionalBool::False : wgpu::OptionalBool::True;
        depth.depthCompare = (isBlend || depthOnly) ? wgpu::CompareFunction::Less
                                                    : wgpu::CompareFunction::LessEqual;
        wgpu::RenderPipelineDescriptor desc{};
        desc.label = label;
        desc.layout = pipelineLayout_;
        desc.vertex.module = module;
        desc.vertex.entryPoint = "vs_skinned";
        desc.vertex.bufferCount = vertex.layouts.size();
        desc.vertex.buffers = vertex.layouts.data();
        desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        desc.primitive.frontFace = wgpu::FrontFace::CCW;
        desc.primitive.cullMode =
            (!depthOnly && !isBlend && !doubleSided) ? wgpu::CullMode::Back : wgpu::CullMode::None;
        desc.depthStencil = &depth;
        // Apple TBDR: every pass in this renderer is single-sampled.
        desc.multisample.count = 1;
        desc.multisample.mask = 0xFFFFFFFFu;
        desc.fragment = &fragment;

        context_.device().PushErrorScope(wgpu::ErrorFilter::Validation);
        wgpu::RenderPipeline pipeline = context_.device().CreateRenderPipeline(&desc);
        std::string error;
        auto future = context_.device().PopErrorScope(
            wgpu::CallbackMode::WaitAnyOnly,
            [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                if (type != wgpu::ErrorType::NoError) {
                    error = gpu::Context::toString(msg);
                }
            });
        context_.waitFor(future);
        if (!error.empty() || !pipeline) {
            return fail("pipeline '{}' creation failed: {}", label, error);
        }
        return pipeline;
    };

    auto a = make("pbr-skinned-opaque", false, false, false);
    if (!a) return std::unexpected(a.error());
    auto b = make("pbr-skinned-opaque-twosided", false, true, false);
    if (!b) return std::unexpected(b.error());
    auto c = make("pbr-skinned-blend", true, false, false);
    if (!c) return std::unexpected(c.error());
    auto d = make("pbr-skinned-depth", false, false, true);
    if (!d) return std::unexpected(d.error());
    opaqueCull_ = *a;
    opaqueNoCull_ = *b;
    blend_ = *c;
    depthOnly_ = *d;
    return {};
}

const wgpu::RenderPipeline& SkinningRenderer::litPipeline(bool blend, bool doubleSided) const {
    if (blend) {
        return blend_;
    }
    return doubleSided ? opaqueNoCull_ : opaqueCull_;
}

void SkinningRenderer::ensureBuffer(std::uint32_t sliceBytes, std::uint32_t rigCount) {
    if (jointBuffer_ && sliceBytes == sliceBytes_ && rigCount == sliceCount_) {
        return;
    }
    sliceBytes_ = sliceBytes;
    sliceCount_ = rigCount;
    wgpu::BufferDescriptor desc{};
    desc.label = "joint-matrices";
    desc.size = static_cast<std::uint64_t>(sliceBytes) * rigCount;
    desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    jointBuffer_ = context_.device().CreateBuffer(&desc);

    std::array<wgpu::BindGroupEntry, 2> entries{};
    entries[0].binding = 0;
    entries[0].buffer = objectUniforms_;
    entries[0].size = objectSize_;
    entries[1].binding = 1;
    entries[1].buffer = jointBuffer_;
    // An explicit size, not WHOLE_SIZE: a dynamic offset is added to the bound range, so a
    // whole-buffer binding would run off the end the moment the offset was non-zero.
    entries[1].size = sliceBytes;
    wgpu::BindGroupDescriptor groupDesc{};
    groupDesc.label = "skinned-object-bind-group";
    groupDesc.layout = objectLayout_;
    groupDesc.entryCount = entries.size();
    groupDesc.entries = entries.data();
    objectGroup_ = context_.device().CreateBindGroup(&groupDesc);
    // A sentinel no palette version can hold, so the first frame after a (re)build always uploads:
    // a fresh rig sits at version 0, and "0 == 0, already there" is how a palette silently never
    // reaches the GPU.
    uploadedVersions_.assign(rigCount, ~0ULL);
    staging_.assign(static_cast<std::size_t>(sliceBytes) * rigCount, 0);
}

void SkinningRenderer::update(const scene::Scene& scene) {
    const bool sceneChanged = scene_ != &scene;
    scene_ = &scene;
    stats_.rigs = 0;
    stats_.joints = 0;
    stats_.uploadBytes = 0;
    slices_.assign(scene.rigs.size(), Slice{});
    if (scene.rigs.empty()) {
        return;
    }
    const auto rigCount = static_cast<std::uint32_t>(std::min<std::size_t>(scene.rigs.size(), kMaxRigs));
    std::uint32_t maxJoints = 0;
    for (std::uint32_t i = 0; i < rigCount; ++i) {
        if (finitePalette(scene.rigs[i].palette) && finitePalette(scene.rigs[i].previousPalette)) {
            maxJoints = std::max(maxJoints, static_cast<std::uint32_t>(scene.rigs[i].palette.size()));
        }
    }
    if (maxJoints == 0) {
        return;
    }
    if (scene.rigs.size() > kMaxRigs) {
        log::warn("{} skinned rigs in the scene; only the first {} get a joint palette",
                  scene.rigs.size(), kMaxRigs);
    }
    // Two palettes per rig -- this frame's and the one it was drawn with last -- in one slice.
    const std::uint32_t sliceBytes = alignUp(maxJoints * 2 * kMatrixBytes, kOffsetAlignment);
    const bool rebuilt = !jointBuffer_ || sliceBytes != sliceBytes_ || rigCount != sliceCount_;
    ensureBuffer(sliceBytes, rigCount);

    std::uint32_t lo = sliceBytes * rigCount;
    std::uint32_t hi = 0;
    for (std::uint32_t i = 0; i < rigCount; ++i) {
        const scene::SkinnedRig& rig = scene.rigs[i];
        const auto joints = static_cast<std::uint32_t>(rig.palette.size());
        if (joints == 0 || !finitePalette(rig.palette) || !finitePalette(rig.previousPalette)) {
            if (joints > 0 && (!finitePalette(rig.palette) || !finitePalette(rig.previousPalette))) {
                log::warn("skinned rig '{}' has a non-finite joint palette; skipping GPU upload", rig.name);
            }
            continue;
        }
        slices_[i] = Slice{sliceBytes * i, joints};
        ++stats_.rigs;
        if (!sceneChanged && !rebuilt && uploadedVersions_[i] == rig.paletteVersion &&
            rig.previousPalette.size() == joints) {
            // A rig that did not re-pose is already on the GPU with prev == current, which is
            // exactly what a still character should report to the velocity target.
            continue;
        }
        std::uint8_t* dst = staging_.data() + static_cast<std::size_t>(sliceBytes) * i;
        std::memcpy(dst, rig.palette.data(), static_cast<std::size_t>(joints) * kMatrixBytes);
        const auto* previous = rig.previousPalette.size() == joints ? rig.previousPalette.data()
                                                                    : rig.palette.data();
        std::memcpy(dst + static_cast<std::size_t>(joints) * kMatrixBytes, previous,
                    static_cast<std::size_t>(joints) * kMatrixBytes);
        uploadedVersions_[i] = rig.paletteVersion;
        stats_.joints += joints * 2;
        lo = std::min(lo, sliceBytes * i);
        hi = std::max(hi, sliceBytes * (i + 1));
    }
    if (hi > lo) {
        // One write covering the rigs that changed. Contiguous rather than per rig because a scene
        // whose characters all move writes once, and one that has a single mover writes one slice.
        context_.queue().WriteBuffer(jointBuffer_, lo, staging_.data() + lo, hi - lo);
        stats_.uploadBytes = hi - lo;
    }
}

SkinningRenderer::Slice SkinningRenderer::slice(scene::RigId rig) const {
    if (rig >= slices_.size()) {
        return {};
    }
    return slices_[rig];
}

} // namespace avgen::rendering
