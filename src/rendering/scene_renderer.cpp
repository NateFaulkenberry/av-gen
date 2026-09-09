#include "rendering/scene_renderer.hpp"

#include "rendering/environment.hpp"
#include "rendering/scene_targets.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"

#include <glm/gtc/matrix_inverse.hpp>

#include "gpu/texture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace avgen::rendering {

const char* auxDebugViewName(AuxDebugView view) {
    switch (view) {
    case AuxDebugView::None: return "none";
    case AuxDebugView::Normal: return "normal";
    case AuxDebugView::Roughness: return "roughness";
    case AuxDebugView::Velocity: return "velocity";
    case AuxDebugView::Emission: return "emission";
    case AuxDebugView::Ids: return "ids";
    case AuxDebugView::Occlusion: return "occlusion";
    case AuxDebugView::Depth: return "depth";
    }
    return "none";
}

namespace {

// Hash of the material inputs that select a bind group (textures + sampler settings).
std::uint64_t materialKey(const scene::Material& m) {
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&](std::uint64_t v) {
        h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    };
    for (const auto* ref : {&m.baseColorTexture, &m.metallicRoughnessTexture, &m.normalTexture, &m.emissiveTexture,
                            &m.occlusionTexture}) {
        mix(ref->texture);
    }
    mix(static_cast<std::uint64_t>(m.baseColorTexture.wrapU) | (static_cast<std::uint64_t>(m.baseColorTexture.wrapV) << 4) |
        (m.baseColorTexture.linearFilter ? 1ull << 8 : 0ull));
    return h;
}

} // namespace

SceneRenderer::SceneRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : context_(context), shaders_(shaders), timer_(std::make_unique<gpu::GpuTimer>(context)),
      shadowTimer_(std::make_unique<gpu::GpuTimer>(context)),
      samplers_(std::make_unique<gpu::SamplerCache>(context)),
      environment_(std::make_unique<EnvironmentProcessor>(context, shaders)),
      shaderStack_(std::make_unique<ShaderStack>(context, shaders)),
      fields_(std::make_unique<FieldUniforms>(context)),
      materialPrograms_(std::make_unique<MaterialPrograms>(context)),
      splines_(std::make_unique<SplineBuffers>(context)),
      particles_(std::make_unique<ParticleRenderer>(context, shaders)),
      procedurals_(std::make_unique<ProceduralRenderer>(context, shaders)),
      sdfs_(std::make_unique<SdfRenderer>(context, shaders)),
      volumes_(std::make_unique<VolumeRenderer>(context, shaders)),
      debug_(std::make_unique<DebugDraw>(context, shaders)),
      simulation_(std::make_unique<Simulation>(context, shaders)),
      shadows_(std::make_unique<ShadowRenderer>(context)), ao_(std::make_unique<AoRenderer>(context, shaders)),
      postProcessor_(std::make_unique<PostProcessor>(context, shaders)), pool_(std::make_unique<gpu::TransientPool>(context)) {
    objectStaging_.resize(static_cast<std::size_t>(kMaxObjects) * kObjectStride);
}

SceneRenderer::~SceneRenderer() = default;

Result<void> SceneRenderer::init() {
    const auto& device = context_.device();

    // ---- bind group layouts ----
    auto uniformLayout = [&](const char* label, std::uint64_t minSize, bool dynamic) {
        wgpu::BindGroupLayoutEntry entry{};
        entry.binding = 0;
        entry.visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entry.buffer.type = wgpu::BufferBindingType::Uniform;
        entry.buffer.hasDynamicOffset = dynamic;
        entry.buffer.minBindingSize = minSize;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = label;
        desc.entryCount = 1;
        desc.entries = &entry;
        return device.CreateBindGroupLayout(&desc);
    };
    {
        // Group 0 of every scene pass. 0 = FrameUniforms and 15 = the simulated-grid table declared
        // by shaders/fields.wgsl (ADR-032; read-only storage, inert when the scene has no grids).
        // 1..10 are the lighting bindings shaders/lighting.wgsl declares (ADR-033/034); a pass whose
        // shader does not mention them simply never reads them.
        std::array<wgpu::BindGroupLayoutEntry, 12> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.minBindingSize = sizeof(FrameUniforms);
        entries[1].binding = 1; // the packed scene lights
        entries[1].visibility = wgpu::ShaderStage::Fragment;
        entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[2].binding = 2; // the froxel light lists
        entries[2].visibility = wgpu::ShaderStage::Fragment;
        entries[2].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[3].binding = 3; // ShadowUniforms
        entries[3].visibility = wgpu::ShaderStage::Fragment;
        entries[3].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[3].buffer.minBindingSize = sizeof(ShadowUniforms);
        entries[4].binding = 4; // the shadow atlas
        entries[4].visibility = wgpu::ShaderStage::Fragment;
        entries[4].texture.sampleType = wgpu::TextureSampleType::Depth;
        entries[4].texture.viewDimension = wgpu::TextureViewDimension::e2DArray;
        entries[5].binding = 5;
        entries[5].visibility = wgpu::ShaderStage::Fragment;
        entries[5].sampler.type = wgpu::SamplerBindingType::Comparison;
        entries[6].binding = 6; // ambient occlusion
        entries[6].visibility = wgpu::ShaderStage::Fragment;
        entries[6].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
        entries[6].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        entries[7] = entries[6];
        entries[7].binding = 7; // linear scene depth (contact shadows)
        entries[8].binding = 8; // the LTC matrix table
        entries[8].visibility = wgpu::ShaderStage::Fragment;
        entries[8].texture.sampleType = wgpu::TextureSampleType::Float;
        entries[8].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        entries[9] = entries[8];
        entries[9].binding = 9; // the LTC magnitude / Fresnel table
        entries[10].binding = 10;
        entries[10].visibility = wgpu::ShaderStage::Fragment;
        entries[10].sampler.type = wgpu::SamplerBindingType::Filtering;
        entries[11].binding = 15;
        entries[11].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entries[11].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "frame-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        frameLayout_ = device.CreateBindGroupLayout(&desc);
    }
    objectLayout_ = uniformLayout("object-layout", sizeof(ObjectUniforms), true);
    {
        // 0 sampler, 1..5 the glTF textures, then ADR-030: 6 the material program block, 7 the
        // 16-byte select region naming this material's program, 8 the field block (only the entity
        // pass reads it there; procedural.wgsl and sdf_raymarch.wgsl bind their own at group 1).
        std::array<wgpu::BindGroupLayoutEntry, 9> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].sampler.type = wgpu::SamplerBindingType::Filtering;
        for (std::uint32_t i = 1; i < 6; ++i) {
            entries[i].binding = i;
            entries[i].visibility = wgpu::ShaderStage::Fragment;
            entries[i].texture.sampleType = wgpu::TextureSampleType::Float;
            entries[i].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        }
        entries[6].binding = 6;
        entries[6].visibility = wgpu::ShaderStage::Fragment;
        entries[6].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[6].buffer.minBindingSize = MaterialPrograms::kBufferSize;
        entries[7].binding = 7;
        entries[7].visibility = wgpu::ShaderStage::Fragment;
        entries[7].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[7].buffer.minBindingSize = MaterialPrograms::kSelectSize;
        entries[8].binding = 8;
        entries[8].visibility = wgpu::ShaderStage::Fragment;
        entries[8].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[8].buffer.minBindingSize = FieldUniforms::kBufferSize;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "material-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        materialLayout_ = device.CreateBindGroupLayout(&desc);
    }
    {
        std::array<wgpu::BindGroupLayoutEntry, 4> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].sampler.type = wgpu::SamplerBindingType::Filtering;
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Fragment;
        entries[1].texture.sampleType = wgpu::TextureSampleType::Float;
        entries[1].texture.viewDimension = wgpu::TextureViewDimension::Cube;
        entries[2] = entries[1];
        entries[2].binding = 2;
        entries[3].binding = 3;
        entries[3].visibility = wgpu::ShaderStage::Fragment;
        entries[3].texture.sampleType = wgpu::TextureSampleType::Float;
        entries[3].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "ibl-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        iblLayout_ = device.CreateBindGroupLayout(&desc);
    }
    {
        std::array<wgpu::BindGroupLayoutEntry, 2> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
        entries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Fragment;
        entries[1].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[1].buffer.minBindingSize = sizeof(TonemapUniforms);
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "tonemap-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        tonemapLayout_ = device.CreateBindGroupLayout(&desc);
    }
    {
        std::array<wgpu::BindGroupLayout, 4> layouts = {frameLayout_, objectLayout_, materialLayout_, iblLayout_};
        wgpu::PipelineLayoutDescriptor desc{};
        desc.label = "scene-pipeline-layout";
        desc.bindGroupLayoutCount = layouts.size();
        desc.bindGroupLayouts = layouts.data();
        scenePipelineLayout_ = device.CreatePipelineLayout(&desc);
    }
    {
        wgpu::PipelineLayoutDescriptor desc{};
        desc.label = "tonemap-pipeline-layout";
        desc.bindGroupLayoutCount = 1;
        desc.bindGroupLayouts = &tonemapLayout_;
        tonemapPipelineLayout_ = device.CreatePipelineLayout(&desc);
    }

    // ---- uniform buffers and bind groups ----
    {
        wgpu::BufferDescriptor desc{};
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.label = "frame-uniforms";
        desc.size = sizeof(FrameUniforms);
        frameUniforms_ = device.CreateBuffer(&desc);
        desc.label = "object-uniforms";
        desc.size = static_cast<std::uint64_t>(kMaxObjects) * kObjectStride;
        objectUniforms_ = device.CreateBuffer(&desc);
        desc.label = "tonemap-uniforms";
        desc.size = sizeof(TonemapUniforms);
        tonemapUniforms_ = device.CreateBuffer(&desc);
    }
    auto bufferGroup = [&](const char* label, const wgpu::BindGroupLayout& layout, const wgpu::Buffer& buffer,
                           std::uint64_t size) {
        wgpu::BindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer = buffer;
        entry.size = size;
        wgpu::BindGroupDescriptor desc{};
        desc.label = label;
        desc.layout = layout;
        desc.entryCount = 1;
        desc.entries = &entry;
        return device.CreateBindGroup(&desc);
    };
    objectBindGroup_ = bufferGroup("object-bind-group", objectLayout_, objectUniforms_, sizeof(ObjectUniforms));

    // ---- default textures ----
    whiteSrgb_ = gpu::solidTexture(context_, 255, 255, 255, 255, true, "default-white-srgb");
    whiteLinear_ = gpu::solidTexture(context_, 255, 255, 255, 255, false, "default-white");
    flatNormal_ = gpu::solidTexture(context_, 128, 128, 255, 255, false, "default-normal");
    {
        wgpu::TextureDescriptor desc{};
        desc.label = "default-black-cube";
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        desc.dimension = wgpu::TextureDimension::e2D;
        desc.size = {1, 1, 6};
        desc.format = wgpu::TextureFormat::RGBA16Float;
        blackCube_.texture = device.CreateTexture(&desc);
        blackCube_.format = desc.format;
        blackCube_.width = blackCube_.height = 1;
        wgpu::TextureViewDescriptor viewDesc{};
        viewDesc.label = "default-black-cube";
        viewDesc.dimension = wgpu::TextureViewDimension::Cube;
        viewDesc.arrayLayerCount = 6;
        blackCubeView_ = blackCube_.texture.CreateView(&viewDesc);
        blackLut_ = gpu::solidTexture(context_, 0, 0, 0, 255, false, "default-lut");
    }
    {
        wgpu::SamplerDescriptor desc{};
        desc.label = "ibl-sampler";
        desc.addressModeU = wgpu::AddressMode::ClampToEdge;
        desc.addressModeV = wgpu::AddressMode::ClampToEdge;
        desc.addressModeW = wgpu::AddressMode::ClampToEdge;
        desc.magFilter = wgpu::FilterMode::Linear;
        desc.minFilter = wgpu::FilterMode::Linear;
        desc.mipmapFilter = wgpu::MipmapFilterMode::Linear;
        iblSampler_ = device.CreateSampler(&desc);
    }
    rebuildIblBindGroup();

    if (auto r = createLightResources(); !r) {
        return r;
    }
    if (auto r = shadows_->init(sizeof(FrameUniforms)); !r) {
        return r;
    }
    if (auto r = ao_->init(frameLayout_); !r) {
        return r;
    }
    rebuildFrameBindGroups();

    if (auto r = createPipelines(); !r) {
        return r;
    }
    if (auto r = environment_->init(); !r) {
        return r;
    }
    if (auto r = particles_->init(fields_->buffer(), splines_->buffer(), fields_->gridBuffer()); !r) {
        return r;
    }
    if (auto r = procedurals_->init(kHdrFormat, kDepthFormat, frameLayout_, materialLayout_, iblLayout_, 1,
                                    fields_->buffer(), splines_->buffer(), fields_->gridBuffer());
        !r) {
        return r;
    }
    if (auto r = sdfs_->init(kHdrFormat, kDepthFormat, frameLayout_, objectLayout_, materialLayout_, iblLayout_,
                             fields_->buffer());
        !r) {
        return r;
    }
    sdfs_->setMeshPipelines(litOpaqueCull_, litOpaqueNoCull_); // Mesh-mode objects draw as entities
    if (auto r = debug_->init(kHdrFormat, kDepthFormat, frameLayout_); !r) {
        return r;
    }
    if (auto r = volumes_->init(kHdrFormat, kDepthFormat, frameLayout_, fields_->buffer()); !r) {
        return r;
    }
    if (auto r = simulation_->init(fields_->buffer(), fields_->gridBuffer()); !r) {
        return r;
    }
    if (auto r = postProcessor_->init(); !r) {
        return r;
    }
    if (context_.errorCount() > 0) {
        return fail("renderer initialisation raised {} GPU error(s): {}", context_.errorCount(),
                    context_.lastError());
    }
    initialised_ = true;
    return {};
}

void SceneRenderer::updateEnvironment(const scene::Scene& scene) {
    const scene::TextureId id = scene.environment.environmentMap;
    const bool valid = id != scene::kInvalidTexture && id < scene.textures.size() && scene.textures[id].isHdr();
    if (!valid) {
        if (ibl_.valid) {
            setIbl(IblResources{});
        }
        environmentTexture_ = scene::kInvalidTexture;
        return;
    }
    if (id == environmentTexture_ && scene.textureVersion == environmentVersion_) {
        return;
    }
    auto ibl = environment_->process(scene.textures[id]);
    if (!ibl) {
        log::error("environment: {}", ibl.error().message);
        setIbl(IblResources{});
    } else {
        setIbl(*ibl);
    }
    environmentTexture_ = id;
    environmentVersion_ = scene.textureVersion;
}


// The froxel-build pass's uniform block (shaders/clusters.wgsl `ClusterParams`).
namespace {
struct ClusterParamsGpu {
    glm::uvec4 grid;  // x, y, z froxel counts, w = local light count
    glm::vec4 depth;  // near, far, tan(fovY/2) * aspect, tan(fovY/2)
    glm::vec4 lights[kMaxSceneLights]; // xyz = view-space position, w = influence radius
};
static_assert(sizeof(ClusterParamsGpu) == 32 + 16 * kMaxSceneLights);
constexpr std::uint32_t kClusterBufferWords = kClusterCount * (1 + kMaxLightsPerCluster);
} // namespace

Result<void> SceneRenderer::createLightResources() {
    const auto& device = context_.device();
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "scene-lights";
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        desc.size = sizeof(GpuLight) * kMaxSceneLights;
        lightBuffer_ = device.CreateBuffer(&desc);
        desc.label = "cluster-lights";
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;
        desc.size = static_cast<std::uint64_t>(kClusterBufferWords) * sizeof(std::uint32_t);
        clusterBuffer_ = device.CreateBuffer(&desc);
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        desc.label = "cluster-params";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = sizeof(ClusterParamsGpu);
        clusterParams_ = device.CreateBuffer(&desc);
    }
    // The cluster build writes what the shading pass reads, so it needs its own writable layout.
    {
        std::array<wgpu::BindGroupLayoutEntry, 2> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Compute;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.minBindingSize = sizeof(ClusterParamsGpu);
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Compute;
        entries[1].buffer.type = wgpu::BufferBindingType::Storage;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "cluster-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        clusterLayout_ = device.CreateBindGroupLayout(&desc);
    }
    {
        std::array<wgpu::BindGroupEntry, 2> entries{};
        entries[0].binding = 0;
        entries[0].buffer = clusterParams_;
        entries[0].size = sizeof(ClusterParamsGpu);
        entries[1].binding = 1;
        entries[1].buffer = clusterBuffer_;
        entries[1].size = static_cast<std::uint64_t>(kClusterBufferWords) * sizeof(std::uint32_t);
        wgpu::BindGroupDescriptor desc{};
        desc.label = "cluster-bind-group";
        desc.layout = clusterLayout_;
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        clusterBindGroup_ = device.CreateBindGroup(&desc);
    }
    {
        auto module = shaders_.load("clusters.wgsl");
        if (!module) {
            return std::unexpected(module.error());
        }
        clusterModule_ = *module;
        wgpu::PipelineLayoutDescriptor layoutDesc{};
        layoutDesc.label = "cluster-pipeline-layout";
        layoutDesc.bindGroupLayoutCount = 1;
        layoutDesc.bindGroupLayouts = &clusterLayout_;
        wgpu::PipelineLayout layout = device.CreatePipelineLayout(&layoutDesc);
        wgpu::ComputePipelineDescriptor desc{};
        desc.label = "cluster-build";
        desc.layout = layout;
        desc.compute.module = clusterModule_;
        desc.compute.entryPoint = "cs_build";
        device.PushErrorScope(wgpu::ErrorFilter::Validation);
        clusterPipeline_ = device.CreateComputePipeline(&desc);
        std::string error;
        auto future = device.PopErrorScope(wgpu::CallbackMode::WaitAnyOnly,
                                           [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                                               if (type != wgpu::ErrorType::NoError) {
                                                   error = gpu::Context::toString(msg);
                                               }
                                           });
        context_.waitFor(future);
        if (!error.empty() || !clusterPipeline_) {
            return fail("cluster build pipeline failed: {}", error);
        }
    }
    // The linearly transformed cone table (ADR-033): fitted at startup, never shipped as data.
    {
        const LtcTable table = buildLtcTable();
        auto upload = [&](const std::vector<glm::vec4>& source, const char* label) {
            gpu::GpuTexture out;
            wgpu::TextureDescriptor desc{};
            desc.label = label;
            desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
            desc.dimension = wgpu::TextureDimension::e2D;
            desc.size = {table.size, table.size, 1};
            desc.format = wgpu::TextureFormat::RGBA16Float;
            out.texture = device.CreateTexture(&desc);
            out.view = out.texture.CreateView();
            out.width = out.height = table.size;
            out.format = desc.format;
            std::vector<std::uint16_t> halves(source.size() * 4);
            for (std::size_t i = 0; i < source.size(); ++i) {
                for (int c = 0; c < 4; ++c) {
                    halves[i * 4 + static_cast<std::size_t>(c)] =
                        gpu::floatToHalf(std::clamp(source[i][c], -1000.0f, 1000.0f));
                }
            }
            wgpu::TexelCopyTextureInfo destination{};
            destination.texture = out.texture;
            wgpu::TexelCopyBufferLayout layout{};
            layout.bytesPerRow = table.size * 8;
            layout.rowsPerImage = table.size;
            const wgpu::Extent3D size = {table.size, table.size, 1};
            context_.queue().WriteTexture(&destination, halves.data(), halves.size() * sizeof(std::uint16_t),
                                          &layout, &size);
            return out;
        };
        ltc1_ = upload(table.matrix, "ltc-matrix");
        ltc2_ = upload(table.terms, "ltc-terms");
        wgpu::SamplerDescriptor desc{};
        desc.label = "ltc-sampler";
        desc.addressModeU = wgpu::AddressMode::ClampToEdge;
        desc.addressModeV = wgpu::AddressMode::ClampToEdge;
        desc.addressModeW = wgpu::AddressMode::ClampToEdge;
        desc.magFilter = wgpu::FilterMode::Linear;
        desc.minFilter = wgpu::FilterMode::Linear;
        ltcSampler_ = device.CreateSampler(&desc);
    }
    {
        // Bound in place of the linear depth target before the first frame allocated it.
        wgpu::TextureDescriptor desc{};
        desc.label = "linear-depth-placeholder";
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        desc.dimension = wgpu::TextureDimension::e2D;
        desc.size = {1, 1, 1};
        desc.format = kLinearDepthFormat;
        linearDepthDefault_.texture = device.CreateTexture(&desc);
        linearDepthDefault_.view = linearDepthDefault_.texture.CreateView();
        linearDepthDefault_.width = linearDepthDefault_.height = 1;
        linearDepthDefault_.format = desc.format;
        const float far = 1.0e7f;
        wgpu::TexelCopyTextureInfo destination{};
        destination.texture = linearDepthDefault_.texture;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = 4;
        layout.rowsPerImage = 1;
        const wgpu::Extent3D size = {1, 1, 1};
        context_.queue().WriteTexture(&destination, &far, sizeof(far), &layout, &size);
    }
    lightStaging_.resize(kMaxSceneLights);
    clusterStaging_.assign(kClusterBufferWords, 0u);
    return {};
}

void SceneRenderer::rebuildFrameBindGroups() {
    const auto& device = context_.device();
    auto make = [&](const wgpu::Buffer& frameBuffer, const wgpu::TextureView& shadowAtlas,
                    const wgpu::TextureView& aoView, const wgpu::TextureView& depthView, const char* label) {
        std::array<wgpu::BindGroupEntry, 12> entries{};
        entries[0].binding = 0;
        entries[0].buffer = frameBuffer;
        entries[0].size = sizeof(FrameUniforms);
        entries[1].binding = 1;
        entries[1].buffer = lightBuffer_;
        entries[1].size = sizeof(GpuLight) * kMaxSceneLights;
        entries[2].binding = 2;
        entries[2].buffer = clusterBuffer_;
        entries[2].size = static_cast<std::uint64_t>(kClusterBufferWords) * sizeof(std::uint32_t);
        entries[3].binding = 3;
        entries[3].buffer = shadows_->uniforms();
        entries[3].size = sizeof(ShadowUniforms);
        entries[4].binding = 4;
        entries[4].textureView = shadowAtlas;
        entries[5].binding = 5;
        entries[5].sampler = shadows_->comparisonSampler();
        entries[6].binding = 6;
        entries[6].textureView = aoView;
        entries[7].binding = 7;
        entries[7].textureView = depthView;
        entries[8].binding = 8;
        entries[8].textureView = ltc1_.view;
        entries[9].binding = 9;
        entries[9].textureView = ltc2_.view;
        entries[10].binding = 10;
        entries[10].sampler = ltcSampler_;
        entries[11].binding = 15;
        entries[11].buffer = fields_->gridBuffer();
        entries[11].size = FieldUniforms::kGridBufferSize;
        wgpu::BindGroupDescriptor desc{};
        desc.label = label;
        desc.layout = frameLayout_;
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        return device.CreateBindGroup(&desc);
    };
    const wgpu::TextureView sceneDepth = linearDepth_.valid() ? linearDepth_.view : linearDepthDefault_.view;
    frameBindGroup_ = make(frameUniforms_, shadows_->atlasView(), ao_->output(), sceneDepth, "frame-bind-group");
    // The prepass, the shadow passes, the linear-depth pass and the AO passes all write something
    // the shading pass reads, so their copy binds placeholders in those three slots. Ambient
    // occlusion reads the linear depth through its own group instead.
    frameBindGroupAux_ = make(frameUniforms_, shadows_->dummyAtlasView(), ao_->placeholder(),
                              linearDepthDefault_.view, "frame-bind-group-aux");
    for (std::uint32_t v = 0; v < kMaxShadowViews; ++v) {
        shadowFrameGroups_[v] = make(shadows_->viewUniforms(v), shadows_->dummyAtlasView(), ao_->placeholder(),
                                     linearDepthDefault_.view, "shadow-frame-group");
    }
}

Result<void> SceneRenderer::createAuxTargets(std::uint32_t width, std::uint32_t height) {
    const auto& device = context_.device();
    auto make = [&](AuxTarget& target, wgpu::TextureFormat format, const char* label) -> Result<void> {
        wgpu::TextureDescriptor desc{};
        desc.label = label;
        desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding |
                     wgpu::TextureUsage::CopySrc;
        desc.dimension = wgpu::TextureDimension::e2D;
        desc.size = {width, height, 1};
        desc.format = format;
        device.PushErrorScope(wgpu::ErrorFilter::Validation);
        wgpu::Texture texture = device.CreateTexture(&desc);
        std::string error;
        auto future = device.PopErrorScope(wgpu::CallbackMode::WaitAnyOnly,
                                           [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                                               if (type != wgpu::ErrorType::NoError) {
                                                   error = gpu::Context::toString(msg);
                                               }
                                           });
        context_.waitFor(future);
        if (!error.empty() || !texture) {
            return fail("auxiliary target '{}' {}x{}: {}", label, width, height, error);
        }
        target.texture = std::move(texture);
        target.view = target.texture.CreateView();
        return {};
    };
    if (auto r = make(normalRough_, kNormalFormat, "aux-normal-roughness"); !r) return r;
    if (auto r = make(velocity_, kVelocityFormat, "aux-velocity"); !r) return r;
    if (auto r = make(emission_, kEmissionFormat, "aux-emission"); !r) return r;
    if (auto r = make(ids_, kIdFormat, "aux-ids"); !r) return r;
    if (auto r = make(linearDepth_, kLinearDepthFormat, "aux-linear-depth"); !r) return r;
    linearDepthGroup_ = nullptr;
    auxDebugGroup_ = nullptr;
    return {};
}

void SceneRenderer::setQuality(QualityTier tier) {
    tier_ = tier;
    qualitySettings_ = QualitySettings::forTier(tier);
    if (ao_) {
        ao_->resetHistory();
    }
}

Result<void> SceneRenderer::createPipelines() {
    auto pbr = shaders_.load("pbr.wgsl");
    if (!pbr) return std::unexpected(pbr.error());
    auto grid = shaders_.load("grid.wgsl");
    if (!grid) return std::unexpected(grid.error());
    auto sky = shaders_.load("skybox.wgsl");
    if (!sky) return std::unexpected(sky.error());
    auto tonemap = shaders_.load("tonemap.wgsl");
    if (!tonemap) return std::unexpected(tonemap.error());
    tonemapModule_ = *tonemap;
    auto linear = shaders_.load("linear_depth.wgsl");
    if (!linear) return std::unexpected(linear.error());
    auto auxDebug = shaders_.load("aux_debug.wgsl");
    if (!auxDebug) return std::unexpected(auxDebug.error());
    pbrModule_ = *pbr;

    auto a = createLitPipeline(*pbr, LitVariant::OpaqueCull);
    if (!a) return std::unexpected(a.error());
    litOpaqueCull_ = *a;
    auto b = createLitPipeline(*pbr, LitVariant::OpaqueNoCull);
    if (!b) return std::unexpected(b.error());
    litOpaqueNoCull_ = *b;
    auto c = createLitPipeline(*pbr, LitVariant::Blend);
    if (!c) return std::unexpected(c.error());
    litBlend_ = *c;
    auto g = createGridPipeline(*grid);
    if (!g) return std::unexpected(g.error());
    gridPipeline_ = *g;
    auto s = createSkyboxPipeline(*sky);
    if (!s) return std::unexpected(s.error());
    skyboxPipeline_ = *s;
    auto d = createDepthOnlyPipeline(*pbr);
    if (!d) return std::unexpected(d.error());
    depthOnlyPipeline_ = *d;
    auto l = createLinearDepthPipeline(*linear);
    if (!l) return std::unexpected(l.error());
    linearDepthPipeline_ = *l;
    auto x = createAuxDebugPipeline(*auxDebug);
    if (!x) return std::unexpected(x.error());
    auxDebugPipeline_ = *x;
    return {};
}

Result<wgpu::RenderPipeline> SceneRenderer::finishPipeline(const wgpu::RenderPipelineDescriptor& desc,
                                                          const char* label) {
    context_.device().PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::RenderPipeline pipeline = context_.device().CreateRenderPipeline(&desc);
    std::string error;
    auto future = context_.device().PopErrorScope(
        wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
            if (type != wgpu::ErrorType::NoError) {
                error = gpu::Context::toString(msg);
            }
        });
    context_.waitFor(future);
    if (!error.empty() || !pipeline) {
        return fail("pipeline '{}' creation failed: {}", label, error);
    }
    return pipeline;
}

namespace {
struct VertexLayoutStorage {
    std::array<wgpu::VertexAttribute, 3> attributes{};
    wgpu::VertexBufferLayout layout{};
    VertexLayoutStorage() {
        attributes[0].format = wgpu::VertexFormat::Float32x3;
        attributes[0].offset = offsetof(scene::Vertex, position);
        attributes[0].shaderLocation = 0;
        attributes[1].format = wgpu::VertexFormat::Float32x3;
        attributes[1].offset = offsetof(scene::Vertex, normal);
        attributes[1].shaderLocation = 1;
        attributes[2].format = wgpu::VertexFormat::Float32x2;
        attributes[2].offset = offsetof(scene::Vertex, uv);
        attributes[2].shaderLocation = 2;
        layout.arrayStride = sizeof(scene::Vertex);
        layout.stepMode = wgpu::VertexStepMode::Vertex;
        layout.attributeCount = attributes.size();
        layout.attributes = attributes.data();
    }
};
static_assert(sizeof(scene::Vertex) == 32);
} // namespace

Result<wgpu::RenderPipeline> SceneRenderer::createLitPipeline(const wgpu::ShaderModule& module, LitVariant variant) {
    VertexLayoutStorage vertex;
    wgpu::BlendState blend{};
    blend.color.operation = wgpu::BlendOperation::Add;
    blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
    blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.alpha.operation = wgpu::BlendOperation::Add;
    blend.alpha.srcFactor = wgpu::BlendFactor::One;
    blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    // Blended surfaces keep the auxiliary targets of the opaque geometry behind them (ADR-035):
    // a normal or an identifier averaged over a transparency is worse than none.
    std::array<wgpu::ColorTargetState, kSceneTargetCount> colorTargets{};
    fillSceneTargets(colorTargets, kHdrFormat, variant == LitVariant::Blend ? &blend : nullptr,
                     variant == LitVariant::Blend ? wgpu::ColorWriteMask::None : wgpu::ColorWriteMask::All);
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = kSceneTargetCount;
    fragment.targets = colorTargets.data();
    wgpu::DepthStencilState depth{};
    depth.format = kDepthFormat;
    depth.depthWriteEnabled = variant == LitVariant::Blend ? wgpu::OptionalBool::False : wgpu::OptionalBool::True;
    depth.depthCompare = variant == LitVariant::Blend ? wgpu::CompareFunction::Less
                                                      : wgpu::CompareFunction::LessEqual;

    wgpu::RenderPipelineDescriptor desc{};
    const char* label = variant == LitVariant::OpaqueCull ? "pbr-opaque" : variant == LitVariant::OpaqueNoCull ? "pbr-opaque-twosided" : "pbr-blend";
    desc.label = label;
    desc.layout = scenePipelineLayout_;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_main";
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vertex.layout;
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.frontFace = wgpu::FrontFace::CCW;
    desc.primitive.cullMode = variant == LitVariant::OpaqueCull ? wgpu::CullMode::Back : wgpu::CullMode::None;
    desc.depthStencil = &depth;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
    return finishPipeline(desc, label);
}

Result<wgpu::RenderPipeline> SceneRenderer::createGridPipeline(const wgpu::ShaderModule& module) {
    VertexLayoutStorage vertex;
    wgpu::BlendState blend{};
    blend.color.operation = wgpu::BlendOperation::Add;
    blend.color.srcFactor = wgpu::BlendFactor::One;
    blend.color.dstFactor = wgpu::BlendFactor::One;
    blend.alpha.operation = wgpu::BlendOperation::Add;
    blend.alpha.srcFactor = wgpu::BlendFactor::One;
    blend.alpha.dstFactor = wgpu::BlendFactor::Zero;
    std::array<wgpu::ColorTargetState, kSceneTargetCount> colorTargets{};
    fillSceneTargets(colorTargets, kHdrFormat, &blend);
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = kSceneTargetCount;
    fragment.targets = colorTargets.data();
    wgpu::DepthStencilState depth{};
    depth.format = kDepthFormat;
    depth.depthWriteEnabled = wgpu::OptionalBool::False;
    depth.depthCompare = wgpu::CompareFunction::Less;

    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "grid-pipeline";
    desc.layout = scenePipelineLayout_;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_main";
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vertex.layout;
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.frontFace = wgpu::FrontFace::CCW;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.depthStencil = &depth;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
    return finishPipeline(desc, "grid-pipeline");
}

Result<wgpu::RenderPipeline> SceneRenderer::createSkyboxPipeline(const wgpu::ShaderModule& module) {
    std::array<wgpu::ColorTargetState, kSceneTargetCount> colorTargets{};
    fillSceneTargets(colorTargets, kHdrFormat, nullptr);
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_sky";
    fragment.targetCount = kSceneTargetCount;
    fragment.targets = colorTargets.data();
    wgpu::DepthStencilState depth{};
    depth.format = kDepthFormat;
    depth.depthWriteEnabled = wgpu::OptionalBool::False;
    depth.depthCompare = wgpu::CompareFunction::LessEqual;

    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "skybox-pipeline";
    desc.layout = scenePipelineLayout_;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_sky";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.depthStencil = &depth;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
    return finishPipeline(desc, "skybox-pipeline");
}

// Depth-only entities and meshed SDFs: the same vertex stage as the lit pipeline, so the depth it
// writes matches exactly, used by the prepass and by every shadow view (ADR-034).
Result<wgpu::RenderPipeline> SceneRenderer::createDepthOnlyPipeline(const wgpu::ShaderModule& module) {
    VertexLayoutStorage vertex;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_depth";
    fragment.targetCount = 0;
    fragment.targets = nullptr;
    wgpu::DepthStencilState depth{};
    depth.format = kDepthFormat;
    depth.depthWriteEnabled = wgpu::OptionalBool::True;
    depth.depthCompare = wgpu::CompareFunction::Less;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "depth-only";
    desc.layout = scenePipelineLayout_;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_main";
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vertex.layout;
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.frontFace = wgpu::FrontFace::CCW;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.depthStencil = &depth;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
    return finishPipeline(desc, "depth-only");
}

Result<wgpu::RenderPipeline> SceneRenderer::createLinearDepthPipeline(const wgpu::ShaderModule& module) {
    const auto& device = context_.device();
    if (!linearDepthLayout_) {
        wgpu::BindGroupLayoutEntry entry{};
        entry.binding = 0;
        entry.visibility = wgpu::ShaderStage::Fragment;
        entry.texture.sampleType = wgpu::TextureSampleType::Depth;
        entry.texture.viewDimension = wgpu::TextureViewDimension::e2D;
        wgpu::BindGroupLayoutDescriptor layoutDesc{};
        layoutDesc.label = "linear-depth-layout";
        layoutDesc.entryCount = 1;
        layoutDesc.entries = &entry;
        linearDepthLayout_ = device.CreateBindGroupLayout(&layoutDesc);
    }
    const std::array<wgpu::BindGroupLayout, 2> layouts = {frameLayout_, linearDepthLayout_};
    wgpu::PipelineLayoutDescriptor layoutDesc{};
    layoutDesc.label = "linear-depth-pipeline-layout";
    layoutDesc.bindGroupLayoutCount = layouts.size();
    layoutDesc.bindGroupLayouts = layouts.data();
    wgpu::PipelineLayout layout = device.CreatePipelineLayout(&layoutDesc);
    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = kLinearDepthFormat;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_linear_depth";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "linear-depth";
    desc.layout = layout;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_linear_depth";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
    return finishPipeline(desc, "linear-depth");
}

Result<wgpu::RenderPipeline> SceneRenderer::createAuxDebugPipeline(const wgpu::ShaderModule& module) {
    const auto& device = context_.device();
    if (!auxDebugUniforms_) {
        wgpu::BufferDescriptor bufferDesc{};
        bufferDesc.label = "aux-debug-uniforms";
        bufferDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        bufferDesc.size = sizeof(glm::vec4);
        auxDebugUniforms_ = device.CreateBuffer(&bufferDesc);
    }
    if (!auxDebugLayout_) {
        std::array<wgpu::BindGroupLayoutEntry, 7> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.minBindingSize = sizeof(glm::vec4);
        for (std::uint32_t i = 1; i < 7; ++i) {
            entries[i].binding = i;
            entries[i].visibility = wgpu::ShaderStage::Fragment;
            entries[i].texture.sampleType = i == 4 ? wgpu::TextureSampleType::Uint
                                                   : wgpu::TextureSampleType::UnfilterableFloat;
            entries[i].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        }
        wgpu::BindGroupLayoutDescriptor layoutDesc{};
        layoutDesc.label = "aux-debug-layout";
        layoutDesc.entryCount = entries.size();
        layoutDesc.entries = entries.data();
        auxDebugLayout_ = device.CreateBindGroupLayout(&layoutDesc);
    }
    wgpu::PipelineLayoutDescriptor layoutDesc{};
    layoutDesc.label = "aux-debug-pipeline-layout";
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &auxDebugLayout_;
    wgpu::PipelineLayout layout = device.CreatePipelineLayout(&layoutDesc);
    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = kHdrFormat;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_aux";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "aux-debug";
    desc.layout = layout;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_aux";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
    return finishPipeline(desc, "aux-debug");
}

Result<wgpu::RenderPipeline> SceneRenderer::tonemapPipelineFor(wgpu::TextureFormat format) {
    const auto key = static_cast<std::uint32_t>(format);
    if (auto it = tonemapPipelines_.find(key); it != tonemapPipelines_.end()) {
        return it->second;
    }
    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = format;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState fragment{};
    fragment.module = tonemapModule_;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "tonemap-pipeline";
    desc.layout = tonemapPipelineLayout_;
    desc.vertex.module = tonemapModule_;
    desc.vertex.entryPoint = "vs_main";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
    auto pipeline = finishPipeline(desc, "tonemap-pipeline");
    if (pipeline) {
        tonemapPipelines_[key] = *pipeline;
    }
    return pipeline;
}

Result<void> SceneRenderer::resize(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        return fail("resize to zero size ({}x{})", width, height);
    }
    if (hdr_.valid() && hdr_.width() == width && hdr_.height() == height) {
        return {};
    }
    gpu::RenderTargetDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.colorFormat = kHdrFormat;
    desc.depthFormat = kDepthFormat;
    desc.extraColorUsage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc; // HDR readback
    desc.label = "hdr-target";
    auto target = gpu::RenderTarget::create(context_, desc);
    if (!target) {
        return std::unexpected(target.error());
    }
    hdr_ = std::move(*target);
    if (auto r = createAuxTargets(width, height); !r) {
        return r;
    }
    rebuildFrameBindGroups();
    ao_->resetHistory();
    tonemapBindGroup_ = nullptr;
    tonemapBoundView_ = nullptr;
    tonemapGroups_.clear();
    stats_.width = width;
    stats_.height = height;
    log::debug("HDR target resized to {}x{}", width, height);
    return {};
}

void SceneRenderer::ensureTonemapBindGroup() {
    if (tonemapBindGroup_ && tonemapBoundView_.Get() == hdr_.colorView().Get()) {
        return;
    }
    std::array<wgpu::BindGroupEntry, 2> entries{};
    entries[0].binding = 0;
    entries[0].textureView = hdr_.colorView();
    entries[1].binding = 1;
    entries[1].buffer = tonemapUniforms_;
    entries[1].size = sizeof(TonemapUniforms);
    wgpu::BindGroupDescriptor desc{};
    desc.label = "tonemap-bind-group";
    desc.layout = tonemapLayout_;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    tonemapBindGroup_ = context_.device().CreateBindGroup(&desc);
    tonemapBoundView_ = hdr_.colorView();
}

void SceneRenderer::setIbl(const IblResources& ibl) {
    ibl_ = ibl;
    rebuildIblBindGroup();
}

void SceneRenderer::rebuildIblBindGroup() {
    std::array<wgpu::BindGroupEntry, 4> entries{};
    entries[0].binding = 0;
    entries[0].sampler = iblSampler_;
    entries[1].binding = 1;
    entries[1].textureView = ibl_.valid ? ibl_.irradiance : blackCubeView_;
    entries[2].binding = 2;
    entries[2].textureView = ibl_.valid ? ibl_.prefiltered : blackCubeView_;
    entries[3].binding = 3;
    entries[3].textureView = ibl_.valid ? ibl_.brdfLut : blackLut_.view;
    wgpu::BindGroupDescriptor desc{};
    desc.label = "ibl-bind-group";
    desc.layout = iblLayout_;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    iblBindGroup_ = context_.device().CreateBindGroup(&desc);
}

Result<void> SceneRenderer::reloadEngineShaders() {
    Result<void> first{};
    auto keep = [&](const char* what, Result<void> r) {
        if (!r && first) {
            first = std::unexpected(Error{std::string(what) + ": " + r.error().message});
        }
    };
    if (auto pbr = shaders_.load("pbr.wgsl")) {
        auto a = createLitPipeline(*pbr, LitVariant::OpaqueCull);
        auto b = createLitPipeline(*pbr, LitVariant::OpaqueNoCull);
        auto c = createLitPipeline(*pbr, LitVariant::Blend);
        if (a && b && c) {
            litOpaqueCull_ = *a;
            litOpaqueNoCull_ = *b;
            litBlend_ = *c;
            sdfs_->setMeshPipelines(litOpaqueCull_, litOpaqueNoCull_);
        } else {
            keep("pbr.wgsl", std::unexpected((!a ? a : !b ? b : c).error()));
        }
    } else {
        keep("pbr.wgsl", std::unexpected(pbr.error()));
    }
    if (auto grid = shaders_.load("grid.wgsl")) {
        if (auto g = createGridPipeline(*grid)) {
            gridPipeline_ = *g;
        } else {
            keep("grid.wgsl", std::unexpected(g.error()));
        }
    } else {
        keep("grid.wgsl", std::unexpected(grid.error()));
    }
    if (auto sky = shaders_.load("skybox.wgsl")) {
        if (auto sp = createSkyboxPipeline(*sky)) {
            skyboxPipeline_ = *sp;
        } else {
            keep("skybox.wgsl", std::unexpected(sp.error()));
        }
    } else {
        keep("skybox.wgsl", std::unexpected(sky.error()));
    }
    if (auto tonemap = shaders_.load("tonemap.wgsl")) {
        tonemapModule_ = *tonemap;
        tonemapPipelines_.clear();
    } else {
        keep("tonemap.wgsl", std::unexpected(tonemap.error()));
    }
    if (auto r = particles_->reload(); !r) {
        keep("particles.wgsl", r);
    }
    if (auto r = procedurals_->reload(); !r) {
        keep("procedural.wgsl", r);
    }
    if (auto r = sdfs_->reload(); !r) {
        keep("sdf_raymarch.wgsl", r);
    }
    if (auto r = volumes_->reload(); !r) {
        keep("volume.wgsl", r);
    }
    if (auto r = simulation_->reload(); !r) {
        keep("simulate.wgsl", r);
    }
    if (auto r = postProcessor_->reload(); !r) {
        keep("post.wgsl", r);
    }
    ++engineReloads_;
    if (first) {
        log::info("engine shaders reloaded");
    } else {
        log::error("engine shader reload kept previous pipelines: {}", first.error().message);
    }
    return first;
}

void SceneRenderer::updateSpectrum(const analysis::AnalysisFrame* frame) {
    if (frame == nullptr || frame->spectrum.empty()) {
        return;
    }
    const std::size_t bins = frame->spectrum.size();
    if (!spectrum_.valid() || spectrumBins_ != bins) {
        wgpu::TextureDescriptor desc{};
        desc.label = "audio-spectrum";
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        desc.dimension = wgpu::TextureDimension::e2D;
        desc.size = {static_cast<std::uint32_t>(bins), 1, 1};
        desc.format = wgpu::TextureFormat::RGBA16Float;
        spectrum_.texture = context_.device().CreateTexture(&desc);
        spectrum_.view = spectrum_.texture.CreateView();
        spectrum_.width = static_cast<std::uint32_t>(bins);
        spectrum_.height = 1;
        spectrum_.format = desc.format;
        spectrumBins_ = bins;
        spectrumStaging_.resize(bins * 4);
    }
    for (std::size_t i = 0; i < bins; ++i) {
        spectrumStaging_[i * 4 + 0] = gpu::floatToHalf(frame->spectrum[i]);
        spectrumStaging_[i * 4 + 1] = gpu::floatToHalf(i < frame->magnitude.size() ? frame->magnitude[i] : 0.0f);
        spectrumStaging_[i * 4 + 2] = gpu::floatToHalf(0.0f);
        spectrumStaging_[i * 4 + 3] = gpu::floatToHalf(1.0f);
    }
    wgpu::TexelCopyTextureInfo dst{};
    dst.texture = spectrum_.texture;
    wgpu::TexelCopyBufferLayout layout{};
    layout.bytesPerRow = static_cast<std::uint32_t>(bins * 8);
    layout.rowsPerImage = 1;
    wgpu::Extent3D extent{static_cast<std::uint32_t>(bins), 1, 1};
    context_.queue().WriteTexture(&dst, spectrumStaging_.data(), spectrumStaging_.size() * 2, &layout, &extent);
}

Result<void> SceneRenderer::ensurePostTargets(std::uint32_t width, std::uint32_t height) {
    for (auto& t : post_) {
        if (t.valid() && t.width() == width && t.height() == height) {
            continue;
        }
        gpu::RenderTargetDesc desc{};
        desc.width = width;
        desc.height = height;
        desc.colorFormat = kHdrFormat;
        desc.depthFormat = wgpu::TextureFormat::Undefined;
        desc.extraColorUsage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc;
        desc.label = "post-target";
        auto made = gpu::RenderTarget::create(context_, desc);
        if (!made) {
            return std::unexpected(made.error());
        }
        t = std::move(*made);
    }
    return {};
}

wgpu::BindGroup SceneRenderer::tonemapBindGroupFor(const wgpu::TextureView& view) {
    auto it = tonemapGroups_.find(view.Get());
    if (it != tonemapGroups_.end()) {
        return it->second;
    }
    std::array<wgpu::BindGroupEntry, 2> entries{};
    entries[0].binding = 0;
    entries[0].textureView = view;
    entries[1].binding = 1;
    entries[1].buffer = tonemapUniforms_;
    entries[1].size = sizeof(TonemapUniforms);
    wgpu::BindGroupDescriptor desc{};
    desc.label = "tonemap-bind-group";
    desc.layout = tonemapLayout_;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    wgpu::BindGroup group = context_.device().CreateBindGroup(&desc);
    tonemapGroups_[view.Get()] = group;
    return group;
}

void SceneRenderer::uploadMeshes(const scene::Scene& scene) {
    if (scene.meshVersion == meshVersion_ && meshes_.size() == scene.meshes.size()) {
        return;
    }
    meshes_.clear();
    meshes_.reserve(scene.meshes.size());
    for (std::size_t i = 0; i < scene.meshes.size(); ++i) {
        const auto& mesh = scene.meshes[i];
        GpuMesh gpuMesh;
        if (!mesh.valid()) {
            log::warn("mesh {} is invalid and will not be drawn", i);
            meshes_.push_back(std::move(gpuMesh));
            continue;
        }
        wgpu::BufferDescriptor vdesc{};
        vdesc.label = "mesh-vertices";
        vdesc.size = mesh.vertices.size() * sizeof(scene::Vertex);
        vdesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
        gpuMesh.vertices = context_.device().CreateBuffer(&vdesc);
        context_.queue().WriteBuffer(gpuMesh.vertices, 0, mesh.vertices.data(), vdesc.size);
        wgpu::BufferDescriptor idesc{};
        idesc.label = "mesh-indices";
        idesc.size = mesh.indices.size() * sizeof(std::uint32_t);
        idesc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
        gpuMesh.indices = context_.device().CreateBuffer(&idesc);
        context_.queue().WriteBuffer(gpuMesh.indices, 0, mesh.indices.data(), idesc.size);
        gpuMesh.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
        meshes_.push_back(std::move(gpuMesh));
    }
    meshVersion_ = scene.meshVersion;
}

void SceneRenderer::uploadTextures(const scene::Scene& scene) {
    if (scene.textureVersion == textureVersion_ && textures_.size() == scene.textures.size()) {
        return;
    }
    textures_.clear();
    textures_.reserve(scene.textures.size());
    for (std::size_t i = 0; i < scene.textures.size(); ++i) {
        // Environment maps are consumed by the EnvironmentProcessor, not bound as material textures.
        if (scene.textures[i].isHdr()) {
            textures_.push_back(gpu::GpuTexture{});
            continue;
        }
        auto tex = gpu::uploadTexture(context_, scene.textures[i], true);
        if (!tex) {
            log::warn("texture {} not uploaded: {}", i, tex.error().message);
            textures_.push_back(gpu::GpuTexture{});
            continue;
        }
        textures_.push_back(std::move(*tex));
    }
    materialBindGroups_.clear();
    textureVersion_ = scene.textureVersion;
    stats_.textures = static_cast<std::uint32_t>(textures_.size());
}

const gpu::GpuTexture& SceneRenderer::textureOrDefault(const scene::TextureRef& ref,
                                                        const gpu::GpuTexture& fallback) const {
    if (ref.valid() && ref.texture < textures_.size() && textures_[ref.texture].valid()) {
        return textures_[ref.texture];
    }
    return fallback;
}

const wgpu::BindGroup& SceneRenderer::materialBindGroup(const scene::Material& material) {
    // The program slot is part of the key: the same textures with a different program need their
    // own group (it binds a different 16-byte region of the select buffer).
    const int programSlot = materialPrograms_->slotOf(material.program);
    const std::uint64_t key = materialKey(material) * 31ull + static_cast<std::uint64_t>(programSlot + 1);
    if (auto it = materialBindGroups_.find(key); it != materialBindGroups_.end()) {
        return it->second;
    }
    std::array<wgpu::BindGroupEntry, 9> entries{};
    entries[0].binding = 0;
    entries[0].sampler = samplers_->get(material.baseColorTexture);
    entries[1].binding = 1;
    entries[1].textureView = textureOrDefault(material.baseColorTexture, whiteSrgb_).view;
    entries[2].binding = 2;
    entries[2].textureView = textureOrDefault(material.metallicRoughnessTexture, whiteLinear_).view;
    entries[3].binding = 3;
    entries[3].textureView = textureOrDefault(material.normalTexture, flatNormal_).view;
    entries[4].binding = 4;
    entries[4].textureView = textureOrDefault(material.emissiveTexture, whiteSrgb_).view;
    entries[5].binding = 5;
    entries[5].textureView = textureOrDefault(material.occlusionTexture, whiteLinear_).view;
    entries[6].binding = 6;
    entries[6].buffer = materialPrograms_->buffer();
    entries[6].size = MaterialPrograms::kBufferSize;
    entries[7].binding = 7;
    entries[7].buffer = materialPrograms_->selectBuffer();
    entries[7].offset = MaterialPrograms::selectOffset(programSlot);
    entries[7].size = MaterialPrograms::kSelectSize;
    entries[8].binding = 8;
    entries[8].buffer = fields_->buffer();
    entries[8].size = FieldUniforms::kBufferSize;
    wgpu::BindGroupDescriptor desc{};
    desc.label = "material-bind-group";
    desc.layout = materialLayout_;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return materialBindGroups_.emplace(key, context_.device().CreateBindGroup(&desc)).first->second;
}

// Packs this frame's lights, chooses the shadow views, uploads both and encodes the froxel build
// (ADR-033). `frame` gains the cluster and light counts the shading pass reads.
void SceneRenderer::updateLights(wgpu::CommandEncoder& encoder, const scene::Scene& scene, const glm::mat4& view,
                                 float aspect, FrameUniforms& frame) {
    const std::uint32_t directional = orderLightsForShading(scene.lights, lightOrder_);
    const auto total = static_cast<std::uint32_t>(lightOrder_.size());

    // A radius that covers what the camera can see: the lit bounds when there are any, otherwise
    // how far the camera stands off its target. It sizes the cascades and the caster range.
    const auto [lo, hi] = scene.bounds();
    float sceneRadius = glm::length(hi - lo) * 0.5f;
    sceneRadius = std::max(sceneRadius, glm::length(scene.camera.target - scene.camera.position));
    sceneRadius = std::clamp(sceneRadius, 1.0f, std::max(scene.camera.farPlane, 2.0f));

    const double shadowMs = stats_.shadows.shadowMs;
    shadows_->update(lightOrder_, frame.viewProj, scene.camera.nearPlane, scene.camera.farPlane, sceneRadius,
                     qualitySettings_);
    stats_.shadows = shadows_->stats();
    stats_.shadows.shadowMs = shadowMs;

    for (std::uint32_t i = 0; i < total; ++i) {
        bool cascaded = false;
        const int shadowView = shadows_->viewForLight(i, cascaded);
        lightStaging_[i] = packLight(*lightOrder_[i], shadowView, cascaded);
    }
    if (total > 0) {
        context_.queue().WriteBuffer(lightBuffer_, 0, lightStaging_.data(),
                                     static_cast<std::size_t>(total) * sizeof(GpuLight));
    }

    // ---- the froxel grid: local lights only, in view space ----
    const bool clustered = qualitySettings_.clusteredLighting;
    ClusterGrid grid;
    grid.zNear = std::max(scene.camera.nearPlane, 0.01f);
    grid.zFar = std::max(scene.camera.farPlane, grid.zNear * 2.0f);
    grid.tanHalfFovY = std::tan(scene.camera.effectiveFovY() * 0.5f);
    grid.aspect = aspect;
    frame.clusterParams = glm::vec4(static_cast<float>(grid.x), static_cast<float>(grid.y),
                                    static_cast<float>(grid.z), clustered ? 1.0f : 0.0f);
    frame.clusterDepth = glm::vec4(grid.sliceScale(), grid.sliceBias(), grid.zNear, grid.zFar);
    frame.lightCounts = glm::vec4(static_cast<float>(directional), static_cast<float>(total), 0.0f, 0.0f);
    stats_.clusteredLights = clustered ? total - directional : 0;
    if (!clustered) {
        return;
    }
    ClusterParamsGpu params{};
    params.grid = glm::uvec4(grid.x, grid.y, grid.z, total - directional);
    params.depth = glm::vec4(grid.zNear, grid.zFar, grid.tanHalfFovY * grid.aspect, grid.tanHalfFovY);
    for (std::uint32_t i = directional; i < total; ++i) {
        const scene::PunctualLight& light = *lightOrder_[i];
        const glm::vec3 viewPos = glm::vec3(view * glm::vec4(light.position, 1.0f));
        params.lights[i - directional] = glm::vec4(viewPos, lightInfluenceRadius(light));
    }
    context_.queue().WriteBuffer(clusterParams_, 0, &params, sizeof(params));
    wgpu::ComputePassDescriptor desc{};
    desc.label = "cluster-build-pass";
    wgpu::ComputePassEncoder pass = encoder.BeginComputePass(&desc);
    pass.SetPipeline(clusterPipeline_);
    pass.SetBindGroup(0, clusterBindGroup_);
    pass.DispatchWorkgroups((grid.x + 3) / 4, (grid.y + 3) / 4, (grid.z + 3) / 4);
    pass.End();
}

Result<void> SceneRenderer::render(wgpu::CommandEncoder& encoder, const scene::Scene& scene, const FrameTime& time,
                                   const gpu::TargetView& target, const ShaderFrameInputs* shaderInputs) {
    if (!initialised_) {
        return fail("renderer not initialised");
    }
    if (!hdr_.valid() || target.width != hdr_.width() || target.height != hdr_.height()) {
        if (auto r = resize(target.width, target.height); !r) {
            return r;
        }
    }
    stats_.shadows.shadowMs = shadowTimer_->collect();
    uploadMeshes(scene);
    uploadTextures(scene);
    updateEnvironment(scene);
    ensureTonemapBindGroup();

    // ---- user shader layers: sync GPU objects, spectrum texture, background intermediate passes ----
    const shaders::ShaderLayerSet* layerSet = shaderInputs ? shaderInputs->layers : nullptr;
    ShaderFrameContext shaderCtx;
    shaderCtx.width = hdr_.width();
    shaderCtx.height = hdr_.height();
    shaderCtx.frameIndex = time.frameIndex;
    if (layerSet != nullptr) {
        shaderStack_->sync(*layerSet);
        updateSpectrum(shaderInputs->frame);
        shaderCtx.audioSpectrum = spectrum_.view;
        for (const auto& layer : layerSet->layers()) {
            if (layer->enabled && layer->stage == shaders::LayerStage::Background) {
                if (auto* gpuLayer = shaderStack_->find(layer->id)) {
                    gpuLayer->renderPasses(encoder, *layer, shaderCtx);
                }
            }
        }
    }

    const auto& queue = context_.queue();
    const float aspect = static_cast<float>(hdr_.width()) / static_cast<float>(hdr_.height());
    const glm::mat4 view = scene.camera.view();
    const glm::mat4 proj = scene.camera.projection(aspect);

    // ---- frame uniforms ----
    FrameUniforms frame{};
    frame.viewProj = proj * view;
    frame.invViewProj = glm::inverse(frame.viewProj);
    frame.cameraPos = glm::vec4(scene.camera.position, 1.0f);
    frame.prevViewProj = havePrevViewProj_ ? prevViewProj_ : frame.viewProj;
    {
        const glm::mat4 invView = glm::inverse(view);
        frame.cameraRight = glm::vec4(glm::normalize(glm::vec3(invView[0])), 0.0f);
        frame.cameraUp = glm::vec4(glm::normalize(glm::vec3(invView[1])), 0.0f);
        // The camera looks down -Z in view space; the froxel grid and the contact-shadow march
        // measure depth along this axis.
        frame.cameraForward = glm::vec4(-glm::normalize(glm::vec3(invView[2])), 0.0f);
    }
    frame.targetSize = glm::vec4(static_cast<float>(hdr_.width()), static_cast<float>(hdr_.height()),
                                 1.0f / static_cast<float>(hdr_.width()), 1.0f / static_cast<float>(hdr_.height()));
    const bool ibl = ibl_.valid && scene.environment.environmentMap != scene::kInvalidTexture;
    frame.params = glm::vec4(static_cast<float>(time.renderTime), scene.environment.gridIntensity,
                             scene.environment.brightness, scene.environment.environmentIntensity);
    std::uint32_t lightCount = 0;
    for (const auto& light : scene.lights) {
        if (!light.enabled || lightCount >= kMaxLights) {
            continue;
        }
        LightUniform& u = frame.lights[lightCount++];
        u.positionType = glm::vec4(light.position, static_cast<float>(light.type));
        const glm::vec3 dir = glm::length(light.direction) > 1e-6f ? glm::normalize(light.direction) : glm::vec3(0, -1, 0);
        u.directionRange = glm::vec4(dir, light.range);
        u.colorIntensity = glm::vec4(light.color * light.intensity, 1.0f);
        const float cosOuter = std::cos(light.outerConeAngle);
        const float cosInner = std::cos(light.innerConeAngle);
        u.cone = glm::vec4(cosOuter, 1.0f / std::max(cosInner - cosOuter, 1e-4f), 0.0f, 0.0f);
    }
    frame.envParams = glm::vec4(scene.environment.environmentRotation,
                                static_cast<float>(ibl ? ibl_.prefilteredMips - 1 : 0), static_cast<float>(lightCount),
                                ibl ? 1.0f : 0.0f);
    frame.skyParams = glm::vec4(scene.environment.backgroundColor, scene.environment.skyboxBlur);
    frame.fogParams = glm::vec4(scene.environment.fogColor, std::max(scene.environment.fogDensity, 0.0f));
    // ---- lights, shadow views and the froxel grid (ADR-033/034) ----
    updateLights(encoder, scene, view, aspect, frame);
    frame.shadowParams = glm::vec4(static_cast<float>(shadows_->resolution()), 1.5f,
                                   qualitySettings_.contactShadows
                                       ? static_cast<float>(qualitySettings_.contactSteps)
                                       : 0.0f,
                                   0.6f);
    // ADR-030 audio inputs, from the analysis frame the caller passed (zero without one): the same
    // band picks as the user-shader std uniforms (engine.cpp), plus the centroid and the flux.
    if (shaderInputs != nullptr && shaderInputs->frame != nullptr) {
        const analysis::AnalysisFrame& af = *shaderInputs->frame;
        const auto band = [&af](std::size_t i) { return i < af.bandCount ? af.bands[i] : 0.0f; };
        frame.audio = glm::vec4(af.rms, band(0), band(2), band(4));
        frame.audioBands = glm::vec4(band(1), band(3), af.centroidNorm, af.flux);
        frame.beat = glm::vec4(af.beatPhase, 1.0f - af.beatPhase, std::min(1.0f, af.onsetStrength / 2.0f),
                               std::fmod((static_cast<float>(af.beatCount) + af.beatPhase) * 0.25f, 1.0f));
    }
    // ---- ambient occlusion (ADR-034): sized here so the frame block can carry its resolution ----
    {
        const float aoRadius = std::clamp(glm::length(scene.camera.target - scene.camera.position) * 0.05f,
                                          0.15f, 4.0f);
        ao_->update(hdr_.width(), hdr_.height(), linearDepth_.view, qualitySettings_, time.frameIndex,
                    scene.camera.effectiveFovY(), aspect, scene.camera.nearPlane, scene.camera.farPlane,
                    aoRadius, 1.0f);
        stats_.ao = ao_->stats();
        const bool on = ao_->active();
        frame.aoParams = glm::vec4(on ? 1.0f : 0.0f, on ? 1.0f : 0.0f,
                                   static_cast<float>(std::max(stats_.ao.width, 1u)),
                                   static_cast<float>(std::max(stats_.ao.height, 1u)));
    }
    queue.WriteBuffer(frameUniforms_, 0, &frame, sizeof(frame));
    // Each shadow view is the same block with its own light-space matrix, so the depth-only passes
    // reuse the ordinary vertex shaders (ADR-034).
    shadows_->upload(&frame, sizeof(frame));
    // The AO output and the shadow atlas are bound through the frame group, and both change layer
    // count / target as the frame is set up.
    rebuildFrameBindGroups();

    // ---- object uniforms (one 256-byte slot per visible entity) ----
    struct DrawItem {
        std::uint32_t offset;
        const scene::Entity* entity;
        float viewDepth;
    };
    std::vector<DrawItem> opaque;
    std::vector<DrawItem> grid;
    std::vector<DrawItem> blended;
    std::uint32_t objectIndex = 0;
    std::size_t entityIndex = 0;
    for (const auto& entity : scene.entities) {
        const std::size_t thisEntity = entityIndex++;
        if (!entity.visible || entity.mesh >= meshes_.size() || meshes_[entity.mesh].indexCount == 0) {
            continue;
        }
        if (objectIndex >= kMaxObjects) {
            log::warn("more than {} visible entities; extra entities skipped", kMaxObjects);
            break;
        }
        const auto& m = entity.material;
        ObjectUniforms obj{};
        obj.model = entity.transform.matrix();
        obj.normalMatrix = glm::transpose(glm::inverse(obj.model));
        // Velocity needs last frame's matrix; keyed by name so reordering entities cannot make an
        // object inherit another's motion (ADR-035).
        {
            const auto previous = prevModels_.find(entity.name);
            obj.prevModel = previous != prevModels_.end() ? previous->second : obj.model;
            prevModelsNext_.insert_or_assign(entity.name, obj.model);
        }
        obj.baseColor = glm::vec4(m.baseColor, m.opacity);
        obj.emissive = glm::vec4(m.emissiveColor, m.emissiveIntensity);
        obj.material = glm::vec4(m.roughness, m.metallic, m.normalScale, m.occlusionStrength);
        std::uint32_t mask = 0;
        auto has = [&](const scene::TextureRef& ref) {
            return ref.valid() && ref.texture < textures_.size() && textures_[ref.texture].valid();
        };
        if (has(m.baseColorTexture)) mask |= 1;
        if (has(m.metallicRoughnessTexture)) mask |= 2;
        if (has(m.normalTexture)) mask |= 4;
        if (has(m.emissiveTexture)) mask |= 8;
        if (has(m.occlusionTexture)) mask |= 16;
        obj.flags = glm::vec4(static_cast<float>(m.alphaMode), m.alphaCutoff, m.unlit ? 1.0f : 0.0f,
                              static_cast<float>(mask));
        // x = the ADR-030 `objectId` input; y = material id and z = bloom weight feed the
        // identifier and emission targets (ADR-035).
        obj.ids = glm::vec4(static_cast<float>(thisEntity), static_cast<float>(thisEntity + 1), 1.0f, 0.0f);
        const std::uint32_t offset = objectIndex * kObjectStride;
        std::memcpy(objectStaging_.data() + offset, &obj, sizeof(obj));
        const float depth = -(view * glm::vec4(entity.transform.position, 1.0f)).z;
        DrawItem item{offset, &entity, depth};
        if (entity.style == scene::MeshStyle::Grid) {
            grid.push_back(item);
        } else if (m.alphaMode == scene::AlphaMode::Blend) {
            blended.push_back(item);
        } else {
            opaque.push_back(item);
        }
        ++objectIndex;
    }
    if (objectIndex > 0) {
        queue.WriteBuffer(objectUniforms_, 0, objectStaging_.data(),
                          static_cast<std::size_t>(objectIndex) * kObjectStride);
    }
    std::stable_sort(blended.begin(), blended.end(),
                     [](const DrawItem& a, const DrawItem& b) { return a.viewDepth > b.viewDepth; });
    prevModels_.swap(prevModelsNext_);
    prevModelsNext_.clear();

    TonemapUniforms tonemap{};
    tonemap.exposure = scene.environment.brightness;
    tonemap.operatorId = static_cast<float>(scene.post.tonemap);
    tonemap.vignette = scene.post.vignette;
    tonemap.grain = scene.post.grain;
    tonemap.size[0] = static_cast<float>(hdr_.width());
    tonemap.size[1] = static_cast<float>(hdr_.height());
    tonemap.seed = static_cast<float>(time.frameIndex % 1024);
    queue.WriteBuffer(tonemapUniforms_, 0, &tonemap, sizeof(tonemap));

    stats_.drawCalls = 0;
    stats_.triangles = 0;
    stats_.entities = objectIndex;
    stats_.lights = lightCount;
    stats_.ibl = ibl;

    // ---- fields (ADR-025): the per-frame field block shared by particles and procedurals ----
    fields_->update(scene.fields, time.renderTime);
    // ---- material programs (ADR-030): packed after the fields so Field ops resolve to slots ----
    materialPrograms_->update(scene.materialPrograms, fields_.get());
    // ---- splines (ADR-026): the sample tables, re-uploaded only when a spline changed ----
    splines_->update(scene.splines);
    // ---- simulated grid fields (ADR-032): fixed sub-steps into the shared grid table ----
    simulation_->update(encoder, scene, time);
    stats_.simulation = simulation_->stats();

    // ---- particle simulation (compute) ----
    particles_->setPreviousViewProjection(frame.prevViewProj); // ADR-035: particles write velocity
    particles_->update(encoder, scene, time, view, proj, fields_.get(), splines_.get());
    stats_.particles = particles_->stats();

    // ---- procedural geometry (ADR-023): mesh/instance uploads, per-frame uniforms, effector pass ----
    // The scene places its procedurals itself in this phase: identity object matrices.
    {
        const std::vector<glm::mat4> identity(scene.procedurals.size(), glm::mat4(1.0f));
        // Culling and screen-size LOD need this frame's viewport (ADR-029).
        procedurals_->setViewport(hdr_.width(), hdr_.height());
        procedurals_->update(encoder, scene, identity, time, fields_.get(), splines_.get());
        stats_.procedural = procedurals_->stats();
    }

    // ---- SDF objects (ADR-027): node packing, mesh uploads, per-object uniforms ----
    sdfs_->update(scene, time, frame.viewProj, fields_.get());
    stats_.sdf = sdfs_->stats();

    // ---- shadow depth passes (ADR-034): one per cascade / spot map, depth only ----
    // Every caster is drawn with its ordinary vertex shader against a frame block whose
    // view-projection is the light's, so entities, procedural instances and SDFs need no second
    // data path. The raymarched SDFs march their bounding box at a quarter of the steps.
    const std::uint32_t shadowViews = std::min(shadows_->stats().views, kMaxShadowViews);
    for (std::uint32_t v = 0; v < shadowViews; ++v) {
        wgpu::RenderPassDepthStencilAttachment depth{};
        depth.view = shadows_->layerView(v);
        depth.depthLoadOp = wgpu::LoadOp::Clear;
        depth.depthStoreOp = wgpu::StoreOp::Store;
        depth.depthClearValue = 1.0f;
        wgpu::RenderPassDescriptor pass{};
        pass.label = "shadow-pass";
        pass.colorAttachmentCount = 0;
        pass.depthStencilAttachment = &depth;
        // One timer spanning every shadow view, so the budget in docs/performance is measurable.
        if (shadowViews == 1) {
            pass.timestampWrites = shadowTimer_->passWrites();
        } else if (v == 0) {
            pass.timestampWrites = shadowTimer_->beginWrites();
        } else if (v + 1 == shadowViews) {
            pass.timestampWrites = shadowTimer_->endWrites();
        }
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        rp.SetBindGroup(0, shadowFrameGroups_[v]);
        rp.SetBindGroup(3, iblBindGroup_);
        rp.SetPipeline(depthOnlyPipeline_);
        for (const auto& item : opaque) {
            const GpuMesh& mesh = meshes_[item.entity->mesh];
            rp.SetBindGroup(1, objectBindGroup_, 1, &item.offset);
            rp.SetBindGroup(2, materialBindGroup(item.entity->material));
            rp.SetVertexBuffer(0, mesh.vertices);
            rp.SetIndexBuffer(mesh.indices, wgpu::IndexFormat::Uint32);
            rp.DrawIndexed(mesh.indexCount);
        }
        sdfs_->drawMeshes(rp, scene, [this](const scene::Material& m) { return materialBindGroup(m); },
                          &depthOnlyPipeline_);
        procedurals_->drawDepthOnly(rp, scene, [this](const scene::Material& m) { return materialBindGroup(m); });
        sdfs_->drawRaymarchDepth(rp, scene, [this](const scene::Material& m) { return materialBindGroup(m); },
                                 true);
        rp.End();
    }
    if (shadowViews > 0) {
        shadowTimer_->resolve(encoder);
    }

    // ---- background pass: the HDR clear and the background user-shader layers ----
    // These are fullscreen quads with a single colour output, so they get their own pass; the
    // geometry pass that follows loads the colour and clears the auxiliary targets.
    {
        wgpu::RenderPassColorAttachment color{};
        color.view = hdr_.colorView();
        color.loadOp = wgpu::LoadOp::Clear;
        color.storeOp = wgpu::StoreOp::Store;
        const auto& bg = scene.environment.backgroundColor;
        color.clearValue = {static_cast<double>(bg.r), static_cast<double>(bg.g), static_cast<double>(bg.b), 1.0};
        wgpu::RenderPassDescriptor pass{};
        pass.label = "background-pass";
        pass.colorAttachmentCount = 1;
        pass.colorAttachments = &color;
        pass.timestampWrites = timer_->beginWrites();
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        if (layerSet != nullptr) {
            for (const auto& layer : layerSet->layers()) {
                if (layer->enabled && layer->stage == shaders::LayerStage::Background) {
                    if (auto* gpuLayer = shaderStack_->find(layer->id)) {
                        gpuLayer->drawOutput(rp, kHdrFormat, false, *layer, shaderCtx);
                        ++stats_.drawCalls;
                    }
                }
            }
        }
        rp.End();
    }

    // ---- depth prepass: the scene depth, before anything reads it ----
    // Ambient occlusion and the contact-shadow march both need the depth of the whole opaque scene
    // while it is being shaded, which a forward pass cannot give them (ADR-034/035). The prepass is
    // cheap - no fragment work - and the shading pass then tests LessEqual against it.
    const bool needsDepthPrepass = ao_->active() || qualitySettings_.contactShadows;
    if (needsDepthPrepass) {
        wgpu::RenderPassDepthStencilAttachment depth{};
        depth.view = hdr_.depthView();
        depth.depthLoadOp = wgpu::LoadOp::Clear;
        depth.depthStoreOp = wgpu::StoreOp::Store;
        depth.depthClearValue = 1.0f;
        wgpu::RenderPassDescriptor pass{};
        pass.label = "depth-prepass";
        pass.colorAttachmentCount = 0;
        pass.depthStencilAttachment = &depth;
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        rp.SetBindGroup(0, frameBindGroupAux_);
        rp.SetBindGroup(3, iblBindGroup_);
        rp.SetPipeline(depthOnlyPipeline_);
        for (const auto& item : opaque) {
            const GpuMesh& mesh = meshes_[item.entity->mesh];
            rp.SetBindGroup(1, objectBindGroup_, 1, &item.offset);
            rp.SetBindGroup(2, materialBindGroup(item.entity->material));
            rp.SetVertexBuffer(0, mesh.vertices);
            rp.SetIndexBuffer(mesh.indices, wgpu::IndexFormat::Uint32);
            rp.DrawIndexed(mesh.indexCount);
        }
        sdfs_->drawMeshes(rp, scene, [this](const scene::Material& m) { return materialBindGroup(m); },
                          &depthOnlyPipeline_);
        procedurals_->drawDepthOnly(rp, scene, [this](const scene::Material& m) { return materialBindGroup(m); });
        sdfs_->drawRaymarchDepth(rp, scene, [this](const scene::Material& m) { return materialBindGroup(m); });
        rp.End();

        // ---- linear depth: the R32F view distance AO and the contact march read ----
        if (!linearDepthGroup_ || linearDepthBoundView_.Get() != hdr_.depthView().Get()) {
            wgpu::BindGroupEntry entry{};
            entry.binding = 0;
            entry.textureView = hdr_.depthView();
            wgpu::BindGroupDescriptor desc{};
            desc.label = "linear-depth-group";
            desc.layout = linearDepthLayout_;
            desc.entryCount = 1;
            desc.entries = &entry;
            linearDepthGroup_ = context_.device().CreateBindGroup(&desc);
            linearDepthBoundView_ = hdr_.depthView();
        }
        wgpu::RenderPassColorAttachment colour{};
        colour.view = linearDepth_.view;
        colour.loadOp = wgpu::LoadOp::Clear;
        colour.storeOp = wgpu::StoreOp::Store;
        colour.clearValue = {1.0e7, 0.0, 0.0, 0.0};
        wgpu::RenderPassDescriptor linearPass{};
        linearPass.label = "linear-depth-pass";
        linearPass.colorAttachmentCount = 1;
        linearPass.colorAttachments = &colour;
        wgpu::RenderPassEncoder lrp = encoder.BeginRenderPass(&linearPass);
        lrp.SetPipeline(linearDepthPipeline_);
        lrp.SetBindGroup(0, frameBindGroupAux_);
        lrp.SetBindGroup(1, linearDepthGroup_);
        lrp.Draw(3);
        lrp.End();

        // ---- ground-truth ambient occlusion (ADR-034) ----
        ao_->encode(encoder, frameBindGroupAux_);
    }

    // ---- pass 1: scene -> HDR + the auxiliary targets (ADR-035) ----
    {
        std::array<wgpu::RenderPassColorAttachment, kSceneTargetCount> attachments{};
        attachments[0].view = hdr_.colorView();
        attachments[0].loadOp = wgpu::LoadOp::Load; // the background pass already cleared it
        attachments[0].storeOp = wgpu::StoreOp::Store;
        const std::array<wgpu::TextureView, kAuxTargetCount> auxViews = {normalRough_.view, velocity_.view,
                                                                         emission_.view, ids_.view};
        for (std::uint32_t i = 0; i < kAuxTargetCount; ++i) {
            attachments[i + 1].view = auxViews[i];
            attachments[i + 1].loadOp = wgpu::LoadOp::Clear;
            attachments[i + 1].storeOp = wgpu::StoreOp::Store;
            attachments[i + 1].clearValue = {0.0, 0.0, 0.0, 0.0};
        }
        wgpu::RenderPassDepthStencilAttachment depth{};
        depth.view = hdr_.depthView();
        depth.depthLoadOp = needsDepthPrepass ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear;
        depth.depthStoreOp = wgpu::StoreOp::Store;
        depth.depthClearValue = 1.0f;
        wgpu::RenderPassDescriptor pass{};
        pass.label = "scene-pass";
        pass.colorAttachmentCount = kSceneTargetCount;
        pass.colorAttachments = attachments.data();
        pass.depthStencilAttachment = &depth;

        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        rp.SetBindGroup(0, frameBindGroup_);
        rp.SetBindGroup(3, iblBindGroup_);
        auto drawItems = [&](const std::vector<DrawItem>& items, bool lit) {
            for (const auto& item : items) {
                const GpuMesh& mesh = meshes_[item.entity->mesh];
                if (lit) {
                    const auto& m = item.entity->material;
                    rp.SetPipeline(m.alphaMode == scene::AlphaMode::Blend ? litBlend_
                                   : m.doubleSided                        ? litOpaqueNoCull_
                                                                          : litOpaqueCull_);
                    rp.SetBindGroup(2, materialBindGroup(m));
                } else {
                    rp.SetPipeline(gridPipeline_);
                    rp.SetBindGroup(2, materialBindGroup(item.entity->material));
                }
                rp.SetBindGroup(1, objectBindGroup_, 1, &item.offset);
                rp.SetVertexBuffer(0, mesh.vertices);
                rp.SetIndexBuffer(mesh.indices, wgpu::IndexFormat::Uint32);
                rp.DrawIndexed(mesh.indexCount);
                ++stats_.drawCalls;
                stats_.triangles += mesh.indexCount / 3;
            }
        };
        drawItems(opaque, true);
        procedurals_->draw(rp, scene, [this](const scene::Material& m) { return materialBindGroup(m); });
        // One indirect draw per populated LOD level when an object uses LOD, else one per object.
        stats_.drawCalls += std::max(stats_.procedural.objects, stats_.procedural.drawCalls);
        stats_.triangles += static_cast<std::uint32_t>(
            std::min<std::uint64_t>(stats_.procedural.logicalTriangles, 0xFFFFFFFFull - stats_.triangles));
        // SDF objects (ADR-027): meshed ones draw here like entities; raymarched ones need their own
        // pass (own timestamps, frag_depth writes), so the lit pass is split around it only when
        // there is raymarch work, keeping the no-SDF frame identical.
        sdfs_->drawMeshes(rp, scene, [this](const scene::Material& m) { return materialBindGroup(m); });
        stats_.drawCalls += stats_.sdf.meshObjects;
        stats_.triangles += stats_.sdf.meshTriangles;
        if (sdfs_->hasRaymarchWork()) {
            rp.End();
            const std::array<wgpu::TextureView, kAuxTargetCount> raymarchAux = {normalRough_.view, velocity_.view,
                                                                                emission_.view, ids_.view};
            sdfs_->encodeRaymarchPass(encoder, hdr_.colorView(), hdr_.depthView(), frameBindGroup_, iblBindGroup_,
                                      scene, [this](const scene::Material& m) { return materialBindGroup(m); },
                                      raymarchAux.data(), kAuxTargetCount);
            stats_.drawCalls += stats_.sdf.raymarchObjects;
            stats_.triangles += stats_.sdf.raymarchObjects * 2;
            for (auto& attachment : attachments) {
                attachment.loadOp = wgpu::LoadOp::Load;
            }
            depth.depthLoadOp = wgpu::LoadOp::Load;
            pass.label = "scene-pass-after-sdf";
            rp = encoder.BeginRenderPass(&pass);
            rp.SetBindGroup(0, frameBindGroup_);
            rp.SetBindGroup(3, iblBindGroup_);
        }
        if (scene.environment.showSkybox && ibl) {
            rp.SetPipeline(skyboxPipeline_);
            const std::uint32_t zeroOffset = 0; // layout requires group 1; the skybox ignores it
            rp.SetBindGroup(1, objectBindGroup_, 1, &zeroOffset);
            rp.SetBindGroup(2, materialBindGroup(scene::Material{}));
            rp.Draw(3);
            ++stats_.drawCalls;
            ++stats_.triangles;
        }
        drawItems(grid, false);
        particles_->draw(rp, scene);
        stats_.drawCalls += particles_->stats().systems;
        if (!blended.empty() && particles_->stats().systems > 0) {
            rp.SetBindGroup(0, frameBindGroup_); // the particle pass rebinds group 0 with its own layout
        }
        drawItems(blended, true);
        rp.End();
    }

    // ---- volumetric atmosphere (ADR-032): half-res raymarch + depth-aware composite ----
    // Skipped entirely when Environment::volumeDensity is 0, so scenes without fog are unchanged.
    volumes_->update(scene, time, hdr_.width(), hdr_.height(), hdr_.depthView(), fields_.get());
    volumes_->encode(encoder, hdr_.colorView(), frameBindGroup_);
    stats_.volume = volumes_->stats();

    // ---- debug drawing (ADR-031): whatever the host queued this frame, over the lit scene ----
    if (!debug_->empty()) {
        debug_->upload();
        wgpu::RenderPassColorAttachment colour{};
        colour.view = hdr_.colorView();
        colour.loadOp = wgpu::LoadOp::Load;
        colour.storeOp = wgpu::StoreOp::Store;
        wgpu::RenderPassDepthStencilAttachment depth{};
        depth.view = hdr_.depthView();
        depth.depthLoadOp = wgpu::LoadOp::Load;
        depth.depthStoreOp = wgpu::StoreOp::Store;
        wgpu::RenderPassDescriptor pass{};
        pass.label = "debug-pass";
        pass.colorAttachmentCount = 1;
        pass.colorAttachments = &colour;
        pass.depthStencilAttachment = &depth;
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        debug_->render(rp, frameBindGroup_, debugDepthTest_);
        rp.End();
        debug_->clear();
    }

    // ---- post layers: HDR -> ping-pong HDR ----
    wgpu::TextureView finalHdr = hdr_.colorView();
    hdrOutput_ = hdr_.colorTexture();
    if (layerSet != nullptr) {
        int ping = 0;
        for (const auto& layer : layerSet->layers()) {
            if (!layer->enabled || layer->stage != shaders::LayerStage::Post) {
                continue;
            }
            auto* gpuLayer = shaderStack_->find(layer->id);
            if (gpuLayer == nullptr) {
                continue;
            }
            if (auto r = ensurePostTargets(hdr_.width(), hdr_.height()); !r) {
                return r;
            }
            ShaderFrameContext postCtx = shaderCtx;
            postCtx.inputImage = finalHdr;
            gpuLayer->renderPasses(encoder, *layer, postCtx);
            wgpu::RenderPassColorAttachment color{};
            color.view = post_[ping].colorView();
            color.loadOp = wgpu::LoadOp::Clear;
            color.storeOp = wgpu::StoreOp::Store;
            wgpu::RenderPassDescriptor pass{};
            pass.label = "post-layer-pass";
            pass.colorAttachmentCount = 1;
            pass.colorAttachments = &color;
            wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
            gpuLayer->drawOutput(rp, kHdrFormat, false, *layer, postCtx);
            rp.End();
            ++stats_.drawCalls;
            finalHdr = post_[ping].colorView();
            hdrOutput_ = post_[ping].colorTexture();
            ping = 1 - ping;
        }
    }

    // ---- built-in post chain: DoF, motion blur, bloom, grading ----
    {
        PostFrameInputs postIn;
        postIn.sceneHdr = finalHdr;
        postIn.depth = hdr_.depthView();
        postIn.width = hdr_.width();
        postIn.height = hdr_.height();
        postIn.prevViewProj = havePrevViewProj_ ? prevViewProj_ : frame.viewProj;
        postIn.invViewProj = frame.invViewProj;
        postIn.cameraPos = scene.camera.position;
        postIn.frameIndex = time.frameIndex;
        postIn.settings = &scene.post;
        finalHdr = postProcessor_->run(encoder, postIn, *pool_);
        if (postProcessor_->outputTexture() != nullptr) {
            hdrOutput_ = postProcessor_->outputTexture();
        }
        stats_.post = postProcessor_->stats();
        stats_.transientTextures = static_cast<std::uint32_t>(pool_->size());
    }
    prevViewProj_ = frame.viewProj;
    havePrevViewProj_ = true;

    // ---- auxiliary-target debug view (ADR-035): one target full-screen, before tone mapping ----
    if (auxDebugView_ != AuxDebugView::None) {
        if (!auxDebugGroup_) {
            std::array<wgpu::BindGroupEntry, 7> entries{};
            entries[0].binding = 0;
            entries[0].buffer = auxDebugUniforms_;
            entries[0].size = sizeof(glm::vec4);
            entries[1].binding = 1;
            entries[1].textureView = normalRough_.view;
            entries[2].binding = 2;
            entries[2].textureView = velocity_.view;
            entries[3].binding = 3;
            entries[3].textureView = emission_.view;
            entries[4].binding = 4;
            entries[4].textureView = ids_.view;
            entries[5].binding = 5;
            entries[5].textureView = ao_->output();
            entries[6].binding = 6;
            entries[6].textureView = linearDepth_.view;
            wgpu::BindGroupDescriptor desc{};
            desc.label = "aux-debug-group";
            desc.layout = auxDebugLayout_;
            desc.entryCount = entries.size();
            desc.entries = entries.data();
            auxDebugGroup_ = context_.device().CreateBindGroup(&desc);
        }
        const glm::vec4 info(static_cast<float>(auxDebugView_),
                             auxDebugView_ == AuxDebugView::Velocity ? 40.0f : 1.0f,
                             static_cast<float>(hdr_.width()), static_cast<float>(hdr_.height()));
        queue.WriteBuffer(auxDebugUniforms_, 0, &info, sizeof(info));
        wgpu::RenderPassColorAttachment colour{};
        colour.view = finalHdr;
        colour.loadOp = wgpu::LoadOp::Clear;
        colour.storeOp = wgpu::StoreOp::Store;
        colour.clearValue = {0.0, 0.0, 0.0, 1.0};
        wgpu::RenderPassDescriptor pass{};
        pass.label = "aux-debug-pass";
        pass.colorAttachmentCount = 1;
        pass.colorAttachments = &colour;
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        rp.SetPipeline(auxDebugPipeline_);
        rp.SetBindGroup(0, auxDebugGroup_);
        rp.Draw(3);
        rp.End();
        ++stats_.drawCalls;
    }

    // ---- pass 2: tonemap -> target ----
    {
        auto pipeline = tonemapPipelineFor(target.format);
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        wgpu::RenderPassColorAttachment color{};
        color.view = target.view;
        color.loadOp = wgpu::LoadOp::Clear;
        color.storeOp = wgpu::StoreOp::Store;
        color.clearValue = {0.0, 0.0, 0.0, 1.0};
        wgpu::RenderPassDescriptor pass{};
        pass.label = "tonemap-pass";
        pass.colorAttachmentCount = 1;
        pass.colorAttachments = &color;
        pass.timestampWrites = timer_->endWrites();
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        rp.SetPipeline(*pipeline);
        rp.SetBindGroup(0, finalHdr.Get() == hdr_.colorView().Get() ? tonemapBindGroup_ : tonemapBindGroupFor(finalHdr));
        rp.Draw(3);
        rp.End();
        ++stats_.drawCalls;
        ++stats_.triangles;
    }
    timer_->resolve(encoder);
    pool_->endFrame();
    return {};
}

namespace {

Result<wgpu::Texture> makeReadbackTarget(gpu::Context& context, std::uint32_t width, std::uint32_t height) {
    wgpu::TextureDescriptor desc{};
    desc.label = "render-to-image";
    desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
    desc.dimension = wgpu::TextureDimension::e2D;
    desc.size = {width, height, 1};
    desc.format = wgpu::TextureFormat::RGBA8Unorm;
    wgpu::Texture texture = context.device().CreateTexture(&desc);
    if (!texture) {
        return fail("cannot create {}x{} readback texture", width, height);
    }
    return texture;
}

} // namespace

Result<wgpu::Texture> SceneRenderer::renderSubmitted(const scene::Scene& scene, const FrameTime& time,
                                                     std::uint32_t width, std::uint32_t height,
                                                     const ShaderFrameInputs* shaderInputs) {
    auto texture = makeReadbackTarget(context_, width, height);
    if (!texture) {
        return texture;
    }
    gpu::TargetView target{texture->CreateView(), wgpu::TextureFormat::RGBA8Unorm, width, height};
    if (auto r = resize(width, height); !r) {
        return std::unexpected(r.error());
    }
    wgpu::CommandEncoder encoder = context_.device().CreateCommandEncoder();
    if (auto r = render(encoder, scene, time, target, shaderInputs); !r) {
        return std::unexpected(r.error());
    }
    wgpu::CommandBuffer commands = encoder.Finish();
    context_.queue().Submit(1, &commands);
    stats_.gpuFrameMs = timer_->collect();
    procedurals_->collectTimings();
    particles_->collectTimings();
    sdfs_->collectTimings();
    volumes_->collectTimings();
    simulation_->collectTimings();
    context_.waitForQueue();
    stats_.gpuFrameMs = timer_->collect();
    procedurals_->collectTimings();
    particles_->collectTimings();
    sdfs_->collectTimings();
    stats_.procedural.effectorPassMs = procedurals_->stats().effectorPassMs;
    stats_.particles.simulateMs = particles_->stats().simulateMs;
    stats_.sdf.raymarchMs = sdfs_->stats().raymarchMs;
    volumes_->collectTimings();
    stats_.volume.volumeMs = volumes_->stats().volumeMs;
    simulation_->collectTimings();
    stats_.simulation.simulateMs = simulation_->stats().simulateMs;
    return texture;
}

Result<gpu::Image8> SceneRenderer::renderToImage(const scene::Scene& scene, const FrameTime& time,
                                                 std::uint32_t width, std::uint32_t height,
                                                 const ShaderFrameInputs* shaderInputs) {
    auto texture = renderSubmitted(scene, time, width, height, shaderInputs);
    if (!texture) {
        return std::unexpected(texture.error());
    }
    return gpu::readTexture8(context_, *texture, width, height, false);
}

Result<gpu::ImageF> SceneRenderer::renderToImageFloat(const scene::Scene& scene, const FrameTime& time,
                                                      std::uint32_t width, std::uint32_t height,
                                                      const ShaderFrameInputs* shaderInputs) {
    auto texture = renderSubmitted(scene, time, width, height, shaderInputs);
    if (!texture) {
        return std::unexpected(texture.error());
    }
    if (hdrOutput_ == nullptr) {
        return fail("no HDR output texture after render");
    }
    return gpu::readTextureF16(context_, hdrOutput_, width, height);
}

} // namespace avgen::rendering
