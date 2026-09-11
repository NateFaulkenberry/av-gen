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
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>

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
    : context_(context), shaders_(shaders), timeline_(std::make_unique<gpu::FrameTimeline>(context)),
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
      shadows_(std::make_unique<ShadowRenderer>(context)),
      skinning_(std::make_unique<SkinningRenderer>(context, shaders)),
      ao_(std::make_unique<AoRenderer>(context, shaders)),
      shadowMask_(std::make_unique<ShadowMaskRenderer>(context, shaders)),
      water_(std::make_unique<WaterRenderer>()),
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
        // 1..10 are the lighting bindings shaders/shadows.wgsl and shaders/lighting.wgsl declare
        // (ADR-033/034), and 11 is the shadow mask shaders/shadows.wgsl reads (ADR-087); a pass
        // whose shader does not mention them simply never reads them.
        std::array<wgpu::BindGroupLayoutEntry, 13> entries{};
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
        entries[11] = entries[6];
        entries[11].binding = 11; // ADR-087: the half-resolution directional shadow mask
        entries[12].binding = 15;
        entries[12].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entries[12].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
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
        // ADR-036: the program block outgrew the uniform size limit when the op budget went from
        // 16 to 48, so it is a read-only storage buffer.
        entries[6].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
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
        std::array<wgpu::BindGroupLayoutEntry, 6> entries{};
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
        // ADR-049: the sky's own equirect, with a sampler that wraps in longitude so the
        // antimeridian is not a visible column. Only shaders/skybox.wgsl declares these; the
        // pipeline layout is explicit, so every other pipeline on this group simply ignores them.
        entries[4] = entries[3];
        entries[4].binding = 4;
        entries[5].binding = 5;
        entries[5].visibility = wgpu::ShaderStage::Fragment;
        entries[5].sampler.type = wgpu::SamplerBindingType::Filtering;
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
        // ADR-049: the same filtering, wrapping in u. An equirect's u is longitude and is
        // periodic; clamping it there blends the seam column against itself.
        desc.label = "sky-equirect-sampler";
        desc.addressModeU = wgpu::AddressMode::Repeat;
        skySampler_ = device.CreateSampler(&desc);
    }
    rebuildIblBindGroup();

    if (auto r = createLightResources(); !r) {
        return r;
    }
    if (auto r = shadows_->init(sizeof(FrameUniforms)); !r) {
        return r;
    }
    if (auto r = shadowMask_->init(frameLayout_); !r) {
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
    // ADR-086: the skinned variants of the lit and depth-only pipelines, and the joint buffer they
    // read. Same frame, material and IBL groups; only group 1 differs.
    if (auto r = skinning_->init(frameLayout_, materialLayout_, iblLayout_, objectUniforms_,
                                 sizeof(ObjectUniforms), kHdrFormat, kDepthFormat);
        !r) {
        return r;
    }
    // ADR-091: water draws inside the scene pass with the scene's own frame, object and IBL
    // groups, and one of its own for the surface settings.
    if (auto r = water_->init(context_, shaders_, kHdrFormat, kDepthFormat, frameLayout_, objectLayout_,
                              iblLayout_);
        !r) {
        return r;
    }
    if (auto r = debug_->init(kHdrFormat, kDepthFormat, frameLayout_); !r) {
        return r;
    }
    // ADR-040: the volume march reads the particle systems' emissive aggregates, so emissive
    // particles light the dust around them (one-directional: the fog never touches the sim).
    if (auto r = volumes_->init(kHdrFormat, kDepthFormat, frameLayout_, fields_->buffer(), particles_->glowBuffer());
        !r) {
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
    // Every subsystem's passes mark themselves on the one frame timeline, so the intervals
    // between consecutive pass ends partition the frame and each pass is charged its own work.
    particles_->setTimeline(timeline_.get());
    procedurals_->setTimeline(timeline_.get());
    sdfs_->setTimeline(timeline_.get());
    volumes_->setTimeline(timeline_.get());
    simulation_->setTimeline(timeline_.get());
    ao_->setTimeline(timeline_.get());
    shadowMask_->setTimeline(timeline_.get());
    postProcessor_->setTimeline(timeline_.get());
    initialised_ = true;
    return {};
}

void SceneRenderer::updateEnvironment(const scene::Scene& scene) {
    const scene::TextureId id = scene.environment.environmentMap;
    const bool valid = id != scene::kInvalidTexture && id < scene.textures.size() && scene.textures[id].isHdr();
    if (!valid) {
        // ADR-036: no HDR map, so synthesise one from the procedural sky. It is built once and
        // rebuilt only when a sky parameter (or the key light it takes its sun from) changes, so
        // this costs nothing per frame.
        environmentTexture_ = scene::kInvalidTexture;
        if (!scene.environment.sky.enabled) {
            if (skyBuilt_ || ibl_.valid) {
                setIbl(IblResources{});
                skyBuilt_ = false;
                skyHash_ = 0;
            }
            return;
        }
        const scene::SkyRuntime sky = scene::resolveSky(scene.environment.sky, scene.lights);
        const std::uint64_t hash = sky.hash();
        if (skyBuilt_ && hash == skyHash_ && ibl_.valid) {
            return;
        }
        auto built = environment_->processSky(sky);
        if (!built) {
            log::error("procedural sky: {}", built.error().message);
            setIbl(IblResources{});
            skyBuilt_ = false;
            return;
        }
        built->fromSky = true;
        setIbl(*built);
        skyBuilt_ = true;
        skyHash_ = hash;
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
    skyBuilt_ = false;
    skyHash_ = 0;
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
                    const wgpu::TextureView& aoView, const wgpu::TextureView& depthView,
                    const wgpu::TextureView& maskView, const char* label) {
        std::array<wgpu::BindGroupEntry, 13> entries{};
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
        entries[11].binding = 11;
        entries[11].textureView = maskView;
        entries[12].binding = 15;
        entries[12].buffer = fields_->gridBuffer();
        entries[12].size = FieldUniforms::kGridBufferSize;
        wgpu::BindGroupDescriptor desc{};
        desc.label = label;
        desc.layout = frameLayout_;
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        return device.CreateBindGroup(&desc);
    };
    const wgpu::TextureView sceneDepth = linearDepth_.valid() ? linearDepth_.view : linearDepthDefault_.view;
    frameBindGroup_ = make(frameUniforms_, shadows_->atlasView(), ao_->output(), sceneDepth,
                           shadowMask_->output(), "frame-bind-group");
    // The prepass, the shadow passes, the linear-depth pass and the AO passes all write something
    // the shading pass reads, so their copy binds placeholders in those slots. Ambient occlusion
    // reads the linear depth through its own group instead.
    frameBindGroupAux_ = make(frameUniforms_, shadows_->dummyAtlasView(), ao_->placeholder(),
                              linearDepthDefault_.view, shadowMask_->placeholder(), "frame-bind-group-aux");
    // ADR-087: the mask pass is the one caller that needs the real atlas and the real linear depth
    // while still being forbidden the mask -- it is computing the shading pass's own shadow terms,
    // through the shading pass's own bindings, into the target it must not sample.
    frameBindGroupMask_ = make(frameUniforms_, shadows_->atlasView(), ao_->placeholder(), sceneDepth,
                               shadowMask_->placeholder(), "frame-bind-group-mask");
    for (std::uint32_t v = 0; v < kMaxShadowViews; ++v) {
        shadowFrameGroups_[v] = make(shadows_->viewUniforms(v), shadows_->dummyAtlasView(), ao_->placeholder(),
                                     linearDepthDefault_.view, shadowMask_->placeholder(), "shadow-frame-group");
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
    std::array<wgpu::BindGroupEntry, 6> entries{};
    entries[0].binding = 0;
    entries[0].sampler = iblSampler_;
    entries[1].binding = 1;
    entries[1].textureView = ibl_.valid ? ibl_.irradiance : blackCubeView_;
    entries[2].binding = 2;
    entries[2].textureView = ibl_.valid ? ibl_.prefiltered : blackCubeView_;
    entries[3].binding = 3;
    entries[3].textureView = ibl_.valid ? ibl_.brdfLut : blackLut_.view;
    // A procedural sky has no map, so the slot takes the 1x1 placeholder; skyExtra.x tells the
    // shader which of the two to read (ADR-049).
    entries[4].binding = 4;
    entries[4].textureView = (ibl_.valid && ibl_.background) ? ibl_.background : blackLut_.view;
    entries[5].binding = 5;
    entries[5].sampler = skySampler_;
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
    keep("pbr_skinned.wgsl", skinning_->reload()); // ADR-086
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
    if (auto r = shadowMask_->reload(); !r) {
        keep("shadow_mask.wgsl", r);
    }
    if (auto r = water_->reload(shaders_); !r) {
        keep("water.wgsl", r);
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
        if (mesh.skinned()) { // ADR-086
            wgpu::BufferDescriptor sdesc{};
            sdesc.label = "mesh-skin";
            sdesc.size = mesh.skin.size() * sizeof(scene::SkinInfluence);
            sdesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
            gpuMesh.skin = context_.device().CreateBuffer(&sdesc);
            context_.queue().WriteBuffer(gpuMesh.skin, 0, mesh.skin.data(), sdesc.size);
        }
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

// True when any part of the world-space box is inside the frustum. The usual conservative test:
// take the corner furthest along each plane's normal, and reject only when even that is behind
// the plane. `world::aabbVisible` is the same construction, but rendering does not depend on
// world/ and should not start here.
bool aabbInsideFrustum(const FrustumPlanes& planes, const glm::vec3& min, const glm::vec3& max) {
    for (const glm::vec4& plane : planes) {
        const glm::vec3 far(plane.x >= 0.0f ? max.x : min.x, plane.y >= 0.0f ? max.y : min.y,
                            plane.z >= 0.0f ? max.z : min.z);
        if (glm::dot(glm::vec3(plane), far) + plane.w < 0.0f) {
            return false;
        }
    }
    return true;
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
    // A scene may say how many cascades its scale needs; the tier says how many the machine can
    // afford. The scene wins when it has an opinion, because a cascade's worth depends on how much
    // world it has to cover.
    QualitySettings shadowQuality = qualitySettings_;
    if (scene.environment.shadowCascades > 0) {
        shadowQuality.cascadeCount = std::clamp(scene.environment.shadowCascades, 1u, 4u);
    }
    shadows_->update(lightOrder_, frame.viewProj, scene.camera.nearPlane, scene.camera.farPlane, sceneRadius,
                     shadowQuality);
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
    desc.timestampWrites = timeline_->mark("clusters", gpu::FrameTimeline::PassKind::Compute);
    ++clusterDispatches_;
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
    // Open this frame's timestamp timeline before anything is encoded: the first pass to mark
    // itself also writes the frame origin, and every later pass's cost is the interval from the
    // previous pass's end to its own.
    timeline_->beginFrame();
    clusterDispatches_ = 0;
    // The CPU side of the frame, stage by stage (ADR-077). The boundary rolls: every interval
    // between two marks is charged to the stage the mark names, so the stages partition render()
    // in submission order the way the timeline's intervals partition the GPU frame. What is left
    // after the last mark is `unattributedMs()`, and it should be microseconds.
    const auto renderStart = std::chrono::steady_clock::now();
    CpuFrameBreakdown& cpu = stats_.cpu;
    cpu = CpuFrameBreakdown{};
    auto stageStart = renderStart;
    const auto stage = [&stageStart](double& field) {
        const auto now = std::chrono::steady_clock::now();
        field += std::chrono::duration<double, std::milli>(now - stageStart).count();
        stageStart = now;
    };
    // Reset here rather than beside the draw counters below: the shadow-caster selection and the
    // shadow passes both record into these, and they run before that point.
    stats_.geometry = GeometryCounters{};
    stats_.state = StateChangeCounters{};
    stats_.shadowCasters = 0;
    uploadMeshes(scene);
    uploadTextures(scene);
    // ADR-086: this frame's joint palettes. The renderer never *poses* anything -- the scene
    // arrives already posed by scene::updateRigs -- it only moves matrices the scene computed.
    skinning_->resetFrameStats();
    skinning_->update(scene);
    updateEnvironment(scene);
    ensureTonemapBindGroup();

    // ---- user shader layers: sync GPU objects, spectrum texture, background intermediate passes ----
    const shaders::ShaderLayerSet* layerSet = shaderInputs ? shaderInputs->layers : nullptr;
    ShaderFrameContext shaderCtx;
    shaderCtx.width = hdr_.width();
    shaderCtx.height = hdr_.height();
    shaderCtx.frameIndex = time.frameIndex;
    shaderCtx.timeline = timeline_.get();
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

    stage(cpu.uploadsMs);

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
    // The IBL is live either from an HDR map or from the procedural sky (ADR-036). The sky carries
    // its own intensity, so `env/intensity` (which existing scenes often set to 0 because it did
    // nothing without a map) never silences it.
    const bool skyIbl = ibl_.valid && ibl_.fromSky;
    const bool ibl = ibl_.valid && (skyIbl || scene.environment.environmentMap != scene::kInvalidTexture);
    const float envIntensity = skyIbl ? scene.environment.sky.intensity : scene.environment.environmentIntensity;
    frame.params = glm::vec4(static_cast<float>(time.renderTime), scene.environment.gridIntensity,
                             scene.environment.brightness, envIntensity);
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
        // z carries the volumetric strength: shaders/volume.wgsl weights each light's in-scatter
        // by it, so a rig decides which sources are visible in the air (ADR-032/ADR-033).
        u.cone = glm::vec4(cosOuter, 1.0f / std::max(cosInner - cosOuter, 1e-4f),
                           std::max(light.volumetricStrength, 0.0f), 0.0f);
    }
    frame.envParams = glm::vec4(scene.environment.environmentRotation,
                                static_cast<float>(ibl ? ibl_.prefilteredMips - 1 : 0), static_cast<float>(lightCount),
                                ibl ? 1.0f : 0.0f);
    frame.skyParams = glm::vec4(scene.environment.backgroundColor, scene.environment.skyboxBlur);
    // ADR-049: zw carry the two controls the background pass owns and shading does not -- how
    // bright the sky is drawn, and how much of it the selective-bloom mask sees.
    frame.skyExtra = glm::vec4(skyIbl ? 1.0f : 0.0f,
                               (!skyIbl || scene.environment.sky.showBackground) ? 1.0f : 0.0f,
                               std::max(scene.environment.skyIntensity, 0.0f),
                               std::clamp(scene.environment.skyBloom, 0.0f, 1.0f));
    frame.fogParams = glm::vec4(scene.environment.fogColor, std::max(scene.environment.fogDensity, 0.0f));
    // ADR-058: the surface fog borrows the volumetric's mist layer rather than declaring one of
    // its own, so the air a ray is drawn through and the air it is marched through are the same
    // air. Amount 0 (the default) leaves applyFog on its uniform-distance branch.
    frame.fogHeight = glm::vec4(scene.environment.fogHeight,
                                std::max(scene.environment.fogHeightFalloff, 0.0f),
                                std::clamp(scene.environment.fogHeightAmount, 0.0f, 1.0f), 0.0f);
    // ADR-058: the styled hemisphere, authorable because a scene that is lit mostly by its ambient
    // needs to say how dark the side facing away from the sky is allowed to get.
    frame.styledSky = glm::vec4(scene.environment.styledSkyAmbient,
                                std::clamp(scene.environment.styledAmbientFloor, 0.0f, 1.0f));
    frame.styledGround = glm::vec4(scene.environment.styledGroundAmbient, 0.0f);
    // ADR-055: the wind, packed once per frame. Wavenumbers are pre-divided here so no vertex ever
    // spends a divide on them, and the shadow views inherit the block verbatim.
    frame.wind = wind::packWind(scene.environment.wind);
    // ---- lights, shadow views and the froxel grid (ADR-033/034) ----
    updateLights(encoder, scene, view, aspect, frame);
    frame.lightCounts.z = scene.environment.stylized ? 1.0f : 0.0f;
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
        QualitySettings aoQuality = qualitySettings_;
        aoQuality.ambientOcclusion = aoQuality.ambientOcclusion && toggles_.ao;
        ao_->update(hdr_.width(), hdr_.height(), linearDepth_.view, aoQuality, time.frameIndex,
                    scene.camera.effectiveFovY(), aspect, scene.camera.nearPlane, scene.camera.farPlane,
                    aoRadius, 1.0f);
        stats_.ao = ao_->stats();
        const bool on = ao_->active();
        frame.aoParams = glm::vec4(on ? 1.0f : 0.0f, on ? 1.0f : 0.0f,
                                   static_cast<float>(std::max(stats_.ao.width, 1u)),
                                   static_cast<float>(std::max(stats_.ao.height, 1u)));
    }
    // ---- the half-resolution directional shadow mask (ADR-087): sized here for the same reason ----
    // It reads the linear depth the prepass resolves, so it can only run when there is a prepass;
    // and the prepass only runs when something needs it, which now includes this.
    {
        shadowMask_->update(hdr_.width(), hdr_.height(), qualitySettings_, toggles_.shadowMask,
                            static_cast<std::uint32_t>(frame.lightCounts.x + 0.5f),
                            scene.camera.effectiveFovY(), aspect, scene.camera.nearPlane,
                            scene.camera.farPlane);
        stats_.shadowMask = shadowMask_->stats();
        const bool on = shadowMask_->active();
        frame.shadowMaskParams = glm::vec4(on ? 1.0f : 0.0f,
                                           on ? static_cast<float>(stats_.shadowMask.lights) : 0.0f,
                                           static_cast<float>(std::max(stats_.shadowMask.width, 1u)),
                                           static_cast<float>(std::max(stats_.shadowMask.height, 1u)));
    }
    queue.WriteBuffer(frameUniforms_, 0, &frame, sizeof(frame));
    // Each shadow view is the same block with its own light-space matrix, so the depth-only passes
    // reuse the ordinary vertex shaders (ADR-034).
    shadows_->upload(&frame, sizeof(frame));
    // The AO output and the shadow atlas are bound through the frame group, and both change layer
    // count / target as the frame is set up.
    rebuildFrameBindGroups();
    stage(cpu.lightsMs);

    const auto waterSlotFor = [&scene](const std::string& program) -> std::uint32_t {
        for (std::size_t i = 0; i < scene.waters.size() && i < kMaxWaterMaterials; ++i) {
            if (scene.waters[i].program == program) {
                return static_cast<std::uint32_t>(i);
            }
        }
        return 0; // a water entity whose surface was not registered draws with the first one
    };

    // ---- object uniforms (one 256-byte slot per visible entity) ----
    struct DrawItem {
        std::uint32_t offset;
        const scene::Entity* entity;
        float viewDepth;
        // ADR-086: where this entity's joint palette sits in the joint buffer. A zero slice means
        // a static mesh, which is every entity in a scene with no character in it.
        SkinningRenderer::Slice skin;
        // ADR-091: which of the scene's water surfaces this entity draws with, resolved once here
        // from the material-program name rather than looked up per draw.
        std::uint32_t water = 0;
        [[nodiscard]] bool skinned() const { return skin.valid(); }
    };
    std::vector<DrawItem> opaque;
    std::vector<DrawItem> grid;
    std::vector<DrawItem> blended;
    std::vector<DrawItem> water;
    // ADR-086. Everything a skinned entity does differently at a draw site: the skinned pipeline, a
    // second dynamic offset naming its slice of the joint buffer, and the influence stream in
    // vertex slot 1. A frame with no skinned entity in it never reaches this, and records exactly
    // the command stream it recorded before skinning existed.
    const auto bindSkinned = [&](wgpu::RenderPassEncoder& rp, const DrawItem& item, const GpuMesh& mesh,
                                 const wgpu::RenderPipeline& pipeline, bool& skinnedBound) {
        rp.SetPipeline(pipeline);
        const std::array<std::uint32_t, 2> offsets = {item.offset, item.skin.offset};
        rp.SetBindGroup(1, skinning_->objectBindGroup(), static_cast<std::uint32_t>(offsets.size()),
                        offsets.data());
        rp.SetVertexBuffer(SkinningRenderer::kInfluenceSlot, mesh.skin);
        skinning_->countDraw();
        ++stats_.state.vertexBufferBinds; // the influence stream; the draw site counts slot 0
        skinnedBound = true;
    };
    // The shadow passes' candidate list. It is the opaque draw list plus the entities the *camera*
    // frustum rejected that a cascade can still see, because "off screen" is not a reason to stop
    // casting (ADR-046): a hill behind the camera throws its shadow across the frame. Camera-culled
    // candidates are added in a second pass so they can never take a uniform slot from something
    // that is actually on screen.
    std::vector<DrawItem> shadowCasters;
    // Each cascade's own frustum, built once: the entity loop uses it to decide whether an
    // off-screen caster is worth a slot, and the passes below reuse it per view.
    const auto& shadowViewList = shadows_->views();
    const std::uint32_t shadowViews = toggles_.shadows ? std::min(shadows_->stats().views, kMaxShadowViews) : 0;
    std::vector<FrustumPlanes> cascadePlanes(shadowViews);
    for (std::uint32_t v = 0; v < shadowViews && v < shadowViewList.size(); ++v) {
        cascadePlanes[v] = frustumPlanes(shadowViewList[v].viewProj);
    }
    const auto entityWorldBounds = [&](const scene::Entity& entity) {
        const auto& [lo, hi] = scene.meshBounds(entity.mesh);
        const glm::mat4 model = entity.transform.matrix();
        glm::vec3 wlo(std::numeric_limits<float>::max());
        glm::vec3 whi(std::numeric_limits<float>::lowest());
        for (int c = 0; c < 8; ++c) {
            const glm::vec3 corner((c & 1) ? hi.x : lo.x, (c & 2) ? hi.y : lo.y, (c & 4) ? hi.z : lo.z);
            const glm::vec3 w = glm::vec3(model * glm::vec4(corner, 1.0f));
            wlo = glm::min(wlo, w);
            whi = glm::max(whi, w);
        }
        return std::pair{wlo, whi};
    };
    const auto anyCascadeSees = [&](const scene::Entity& entity) {
        if (cascadePlanes.empty()) {
            return false;
        }
        const auto [wlo, whi] = entityWorldBounds(entity);
        for (std::uint32_t v = 0; v < cascadePlanes.size() && v < shadowViewList.size(); ++v) {
            if (aabbInsideFrustum(cascadePlanes[v], wlo, whi)) {
                return true;
            }
        }
        return false;
    };
    std::uint32_t objectIndex = 0;
    // Writes one 256-byte object slot and returns the draw item, or nothing when the budget is
    // spent. Shared by the camera pass and the shadow-only pass so the two cannot describe the
    // same entity differently.
    const auto makeItem = [&](const scene::Entity& entity, std::size_t thisEntity) -> std::optional<DrawItem> {
        if (objectIndex >= kMaxObjects) {
            return std::nullopt;
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
        // ADR-086: a skinned entity only counts as one if its mesh carries influences and the
        // scene posed its rig. Anything short of that draws as the static mesh it is.
        SkinningRenderer::Slice skin;
        if (entity.rig != scene::kInvalidRig && entity.mesh < meshes_.size() && meshes_[entity.mesh].skin) {
            skin = skinning_->slice(entity.rig);
        }
        // x = the ADR-030 `objectId` input; y = material id and z = bloom weight feed the
        // identifier and emission targets (ADR-035); w = the joint count, which is how the skinned
        // vertex stage finds the previous frame's half of its palette slice.
        obj.ids = glm::vec4(static_cast<float>(thisEntity), static_cast<float>(thisEntity + 1), 1.0f,
                            static_cast<float>(skin.jointCount));
        const std::uint32_t offset = objectIndex * kObjectStride;
        std::memcpy(objectStaging_.data() + offset, &obj, sizeof(obj));
        const float depth = -(view * glm::vec4(entity.transform.position, 1.0f)).z;
        ++objectIndex;
        return DrawItem{offset, &entity, depth, skin};
    };
    // True when the shadow passes would draw this entity: they take the opaque list only.
    const auto shadowEligible = [](const scene::Entity& entity) {
        return entity.castsShadow && entity.style != scene::MeshStyle::Grid &&
               entity.style != scene::MeshStyle::Water &&
               entity.material.alphaMode != scene::AlphaMode::Blend;
    };
    const auto drawable = [&](const scene::Entity& entity) {
        return entity.visible && entity.mesh < meshes_.size() && meshes_[entity.mesh].indexCount != 0;
    };
    std::size_t entityIndex = 0;
    for (const auto& entity : scene.entities) {
        const std::size_t thisEntity = entityIndex++;
        if (!drawable(entity) || entity.cameraCulled) {
            continue;
        }
        const auto item = makeItem(entity, thisEntity);
        if (!item) {
            log::warn("more than {} visible entities; extra entities skipped", kMaxObjects);
            break;
        }
        if (entity.style == scene::MeshStyle::Grid) {
            grid.push_back(*item);
        } else if (entity.style == scene::MeshStyle::Water) {
            DrawItem w = *item;
            w.water = waterSlotFor(entity.material.program);
            // Every water chunk of a terrain shares the node's transform, so `makeItem`'s depth --
            // the origin's -- is the same number for all of them and sorts nothing. The chunk's own
            // centre is the key that means anything here. Chunks tile a plane and rarely overlap on
            // screen, but a river seen across a pond at a grazing angle does, and two blended
            // surfaces composited in the wrong order is a seam that only appears from one place.
            const auto& [lo, hi] = scene.meshBounds(entity.mesh);
            const glm::vec3 centre = glm::vec3(entity.transform.matrix() * glm::vec4((lo + hi) * 0.5f, 1.0f));
            w.viewDepth = -(view * glm::vec4(centre, 1.0f)).z;
            water.push_back(w);
        } else if (entity.material.alphaMode == scene::AlphaMode::Blend) {
            blended.push_back(*item);
        } else {
            opaque.push_back(*item);
            if (entity.castsShadow) {
                shadowCasters.push_back(*item);
            }
        }
    }
    // Second pass: casters the camera cannot see. Culling them here rather than leaving them out
    // of the scene is the whole point -- the cascade's frustum is the right test, and it is the one
    // being applied.
    entityIndex = 0;
    for (const auto& entity : scene.entities) {
        const std::size_t thisEntity = entityIndex++;
        if (!entity.cameraCulled || !drawable(entity) || !shadowEligible(entity)) {
            continue;
        }
        if (!anyCascadeSees(entity)) {
            ++stats_.shadows.entitiesCulled;
            continue;
        }
        const auto item = makeItem(entity, thisEntity);
        if (!item) {
            break; // the camera's own entities have the slots; nothing more to say about it
        }
        shadowCasters.push_back(*item);
    }
    stats_.shadowCasters = static_cast<std::uint32_t>(shadowCasters.size());
    if (objectIndex > 0) {
        queue.WriteBuffer(objectUniforms_, 0, objectStaging_.data(),
                          static_cast<std::size_t>(objectIndex) * kObjectStride);
    }
    std::stable_sort(blended.begin(), blended.end(),
                     [](const DrawItem& a, const DrawItem& b) { return a.viewDepth > b.viewDepth; });
    std::stable_sort(water.begin(), water.end(),
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
    tonemap.chromaRetention = scene.post.chromaRetention;
    queue.WriteBuffer(tonemapUniforms_, 0, &tonemap, sizeof(tonemap));

    stats_.drawCalls = 0;
    stats_.triangles = 0;
    stats_.entities = objectIndex;
    stats_.lights = lightCount;
    stats_.ibl = ibl;
    stage(cpu.objectsMs);

    // ---- fields (ADR-025): the per-frame field block shared by particles and procedurals ----
    fields_->update(scene.fields, time.renderTime);
    // ---- material programs (ADR-030): packed after the fields so Field ops resolve to slots ----
    materialPrograms_->update(scene.materialPrograms, fields_.get());
    // ---- splines (ADR-026): the sample tables, re-uploaded only when a spline changed ----
    splines_->update(scene.splines);
    stage(cpu.fieldsMs);
    // ---- simulated grid fields (ADR-032): fixed sub-steps into the shared grid table ----
    simulation_->update(encoder, scene, time);
    stats_.simulation = simulation_->stats();
    stage(cpu.simulationMs);

    // Ambient occlusion and the contact-shadow march both need the depth of the whole opaque
    // scene while it is being shaded (ADR-034/035); the particle fog coupling reads the linear
    // depth the same prepass resolves, so the flag is decided before either uses it.
    // ADR-091: water reads the prepass's linear depth to know how thick it is, which is what its
    // shoreline and its depth colour are made of. A scene with water in it therefore asks for the
    // prepass whatever else is on -- 0.20 ms of depth-only geometry, against a surface that
    // otherwise falls back to the vertex depth and to the mesh's own edge for its waterline.
    const bool needsDepthPrepass = ao_->active() || qualitySettings_.contactShadows ||
                                   shadowMask_->active() || !scene.waters.empty();
    // ---- water surfaces (ADR-091) ----
    // One uniform slot per authored surface, uploaded once a frame. `flowTime` is the timeline
    // second, never a wall clock and never an accumulated delta: a river at t = 12.0 has to be in
    // the same place in an offline render as it is live, and the project verifies that by hashing
    // captured frames.
    {
        waterUniforms_.clear();
        const float flowTime = static_cast<float>(time.renderTime);
        for (const scene::WaterSurface& surface : scene.waters) {
            if (waterUniforms_.size() >= kMaxWaterMaterials) {
                break;
            }
            waterUniforms_.push_back(
                waterUniformsFrom(surface.settings, flowTime, surface.fastestFlow, needsDepthPrepass));
        }
        water_->upload(queue, waterUniforms_);
    }


    // ---- particle simulation (compute) ----
    // ADR-040 frame context: the previous view-projection for the velocity target (ADR-035), the
    // eye (ribbons face it, the fog coupling marches from it), the shutter open time that sets the
    // stretch length (ADR-037), and the volumetric atmosphere the particles sit in. The linear
    // depth is only resolved when a depth prepass ran; without it the fog coupling switches off
    // rather than guessing what the volume composite will have marched to.
    {
        ParticleFrameContext particleFrame;
        particleFrame.prevViewProj = frame.prevViewProj;
        particleFrame.cameraPosition = scene.camera.position;
        particleFrame.shutterSeconds = static_cast<float>(std::clamp(time.deltaTime, 0.0, 0.1)) *
                                       std::clamp(scene.camera.lens.shutterAngle, 0.0f, 360.0f) / 360.0f;
        if (VolumeRenderer::enabled(scene.environment)) {
            particleFrame.fogDensity = scene.environment.volumeDensity;
            particleFrame.fogHeight = scene.environment.fogHeight;
            particleFrame.fogHeightFalloff = scene.environment.fogHeightFalloff;
            particleFrame.fogAbsorption = scene.environment.volumeAbsorption;
            particleFrame.fogMaxDistance = scene.environment.volumeMaxDistance;
        }
        particleFrame.linearDepth = needsDepthPrepass ? linearDepth_.view : nullptr;
        particles_->setFrameContext(particleFrame);
    }
    particles_->update(encoder, scene, time, view, proj, fields_.get(), splines_.get());
    stats_.particles = particles_->stats();
    stage(cpu.particlesMs);

    // ---- procedural geometry (ADR-023): mesh/instance uploads, per-frame uniforms, effector pass ----
    // The scene places its procedurals itself in this phase: identity object matrices.
    {
        const std::vector<glm::mat4> identity(scene.procedurals.size(), glm::mat4(1.0f));
        // Culling and screen-size LOD need this frame's viewport (ADR-029).
        procedurals_->setViewport(hdr_.width(), hdr_.height());
        procedurals_->update(encoder, scene, identity, time, fields_.get(), splines_.get());
        stats_.procedural = procedurals_->stats();
    }
    stage(cpu.proceduralMs);

    // ---- SDF objects (ADR-027): node packing, mesh uploads, per-object uniforms ----
    sdfs_->update(scene, time, frame.viewProj, fields_.get());
    stats_.sdf = sdfs_->stats();
    stage(cpu.sdfMs);

    // ---- shadow depth passes (ADR-034): one per cascade / spot map, depth only ----
    // Every caster is drawn with its ordinary vertex shader against a frame block whose
    // view-projection is the light's, so entities, procedural instances and SDFs need no second
    // data path. The raymarched SDFs march their bounding box at a quarter of the steps.
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
        // Every view marks the timeline under "shadow"; their intervals sum to the whole depth
        // phase, so a cascade that costs nothing cannot hide behind one that does.
        pass.timestampWrites = timeline_->mark("shadow", gpu::FrameTimeline::PassKind::Render);
        // Cull each caster against this cascade (P4). Without it every shadow-casting entity is
        // drawn into every view: a world of 256 terrain chunks submits them all, twice, whatever
        // the light can actually see, and the cost stays whether or not the camera is looking at
        // any of it. The cascade's own frustum is the right test -- not the camera's, because a
        // caster behind the camera still throws a shadow into shot.
        const bool cullCascade = v < shadowViewList.size() && v < cascadePlanes.size();
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        rp.SetBindGroup(0, shadowFrameGroups_[v]);
        rp.SetBindGroup(3, iblBindGroup_);
        rp.SetPipeline(depthOnlyPipeline_);
        stats_.state.bindGroupBinds += 2;
        ++stats_.state.pipelineBinds;
        bool skinnedBound = false;
        for (const auto& item : shadowCasters) {
            if (cullCascade) {
                const auto [wlo, whi] = entityWorldBounds(*item.entity);
                if (!aabbInsideFrustum(cascadePlanes[v], wlo, whi)) {
                    ++stats_.shadows.entitiesCulled;
                    continue;
                }
            }
            const GpuMesh& mesh = meshes_[item.entity->mesh];
            if (item.skinned()) {
                bindSkinned(rp, item, mesh, skinning_->depthPipeline(), skinnedBound);
                ++stats_.state.pipelineBinds;
            } else {
                if (skinnedBound) {
                    rp.SetPipeline(depthOnlyPipeline_);
                    ++stats_.state.pipelineBinds;
                    skinnedBound = false;
                }
                rp.SetBindGroup(1, objectBindGroup_, 1, &item.offset);
            }
            rp.SetBindGroup(2, materialBindGroup(item.entity->material));
            rp.SetVertexBuffer(0, mesh.vertices);
            rp.SetIndexBuffer(mesh.indices, wgpu::IndexFormat::Uint32);
            rp.DrawIndexed(mesh.indexCount);
            stats_.geometry.shadow.record(mesh.indexCount, 1, false);
            stats_.state.bindGroupBinds += 2;
            ++stats_.state.vertexBufferBinds;
            ++stats_.state.indexBufferBinds;
            ++stats_.shadows.entityDraws;
        }
        sdfs_->drawMeshes(rp, scene, [this](const scene::Material& m) { return materialBindGroup(m); },
                          &depthOnlyPipeline_);
        procedurals_->drawShadow(rp, scene, [this](const scene::Material& m) { return materialBindGroup(m); });
        sdfs_->drawRaymarchDepth(rp, scene, [this](const scene::Material& m) { return materialBindGroup(m); },
                                 true);
        rp.End();
    }
    stage(cpu.shadowEncodeMs);

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
        pass.timestampWrites = timeline_->mark("background", gpu::FrameTimeline::PassKind::Render);
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
    stage(cpu.backgroundEncodeMs);

    // ---- depth prepass: the scene depth, before anything reads it ----
    // The prepass is cheap - no fragment work - and the shading pass then tests LessEqual
    // against it. `needsDepthPrepass` was decided above, before the particle frame context.
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
        pass.timestampWrites = timeline_->mark("depth", gpu::FrameTimeline::PassKind::Render);
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        rp.SetBindGroup(0, frameBindGroupAux_);
        rp.SetBindGroup(3, iblBindGroup_);
        rp.SetPipeline(depthOnlyPipeline_);
        stats_.state.bindGroupBinds += 2;
        ++stats_.state.pipelineBinds;
        bool skinnedBound = false;
        for (const auto& item : opaque) {
            const GpuMesh& mesh = meshes_[item.entity->mesh];
            if (item.skinned()) {
                bindSkinned(rp, item, mesh, skinning_->depthPipeline(), skinnedBound);
                ++stats_.state.pipelineBinds;
            } else {
                if (skinnedBound) {
                    rp.SetPipeline(depthOnlyPipeline_);
                    ++stats_.state.pipelineBinds;
                    skinnedBound = false;
                }
                rp.SetBindGroup(1, objectBindGroup_, 1, &item.offset);
            }
            rp.SetBindGroup(2, materialBindGroup(item.entity->material));
            rp.SetVertexBuffer(0, mesh.vertices);
            rp.SetIndexBuffer(mesh.indices, wgpu::IndexFormat::Uint32);
            rp.DrawIndexed(mesh.indexCount);
            stats_.geometry.depth.record(mesh.indexCount, 1, false);
            stats_.state.bindGroupBinds += 2;
            ++stats_.state.vertexBufferBinds;
            ++stats_.state.indexBufferBinds;
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
        linearPass.timestampWrites = timeline_->mark("depth", gpu::FrameTimeline::PassKind::Render);
        wgpu::RenderPassEncoder lrp = encoder.BeginRenderPass(&linearPass);
        lrp.SetPipeline(linearDepthPipeline_);
        lrp.SetBindGroup(0, frameBindGroupAux_);
        lrp.SetBindGroup(1, linearDepthGroup_);
        lrp.Draw(3);
        lrp.End();
        ++stats_.state.pipelineBinds;
        stats_.state.bindGroupBinds += 2;

        // ---- ground-truth ambient occlusion (ADR-034) ----
        ao_->encode(encoder, frameBindGroupAux_);

        // ---- the directional shadow mask (ADR-087) ----
        // Last of the prepass group: it wants the shadow atlas (drawn above) and the linear depth
        // (resolved just now), and the lit pass that follows wants it.
        shadowMask_->encode(encoder, frameBindGroupMask_);
    }
    stage(cpu.depthEncodeMs);

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
        pass.timestampWrites = timeline_->mark("scene", gpu::FrameTimeline::PassKind::Render);

        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        rp.SetBindGroup(0, frameBindGroup_);
        rp.SetBindGroup(3, iblBindGroup_);
        stats_.state.bindGroupBinds += 2;
        auto drawItems = [&](const std::vector<DrawItem>& items, bool lit) {
            bool skinnedBound = false;
            for (const auto& item : items) {
                const GpuMesh& mesh = meshes_[item.entity->mesh];
                const auto& m = item.entity->material;
                const bool skinned = lit && item.skinned();
                if (skinned) {
                    // ADR-086: the same shading, reached through a vertex stage that poses the
                    // mesh first. pbr_skinned.wgsl includes pbr.wgsl, so `fs_main` is the same
                    // function, not a copy of it.
                    bindSkinned(rp, item, mesh,
                                skinning_->litPipeline(m.alphaMode == scene::AlphaMode::Blend, m.doubleSided),
                                skinnedBound);
                    rp.SetBindGroup(2, materialBindGroup(m));
                } else if (lit) {
                    rp.SetPipeline(m.alphaMode == scene::AlphaMode::Blend ? litBlend_
                                   : m.doubleSided                        ? litOpaqueNoCull_
                                                                          : litOpaqueCull_);
                    rp.SetBindGroup(2, materialBindGroup(m));
                    rp.SetBindGroup(1, objectBindGroup_, 1, &item.offset);
                } else {
                    rp.SetPipeline(gridPipeline_);
                    rp.SetBindGroup(2, materialBindGroup(m));
                    rp.SetBindGroup(1, objectBindGroup_, 1, &item.offset);
                }
                rp.SetVertexBuffer(0, mesh.vertices);
                rp.SetIndexBuffer(mesh.indices, wgpu::IndexFormat::Uint32);
                rp.DrawIndexed(mesh.indexCount);
                // One draw, one instance, and the CPU knows both: no estimate here.
                stats_.geometry.camera.record(mesh.indexCount, 1, false);
                // Counted as recorded, not as changed. This loop sets the pipeline and both groups
                // for every item whether or not they differ from the last, which is precisely the
                // redundancy a later phase is meant to remove -- so the number that would hide it
                // is not the one kept.
                ++stats_.state.pipelineBinds;
                stats_.state.bindGroupBinds += 2;
                ++stats_.state.vertexBufferBinds;
                ++stats_.state.indexBufferBinds;
                ++stats_.drawCalls;
            }
        };
        drawItems(opaque, true);
        procedurals_->draw(rp, scene, [this](const scene::Material& m) { return materialBindGroup(m); });
        // The procedural draws and triangles are folded in at the end of the frame instead of
        // here. The copy of ProceduralStats taken during update() is made before a single draw
        // exists, so reading its counters at this point read `objects` -- one draw per object --
        // for a renderer that issues up to four indirect draws per object, which is how `draws=112`
        // came to be reported alongside `indirect 154` in the same line.
        // SDF objects (ADR-027): meshed ones draw here like entities; raymarched ones need their own
        // pass (own timestamps, frag_depth writes), so the lit pass is split around it only when
        // there is raymarch work, keeping the no-SDF frame identical.
        sdfs_->drawMeshes(rp, scene, [this](const scene::Material& m) { return materialBindGroup(m); });
        stats_.drawCalls += stats_.sdf.meshObjects;
        // ADR-027 reports the meshed SDFs for the frame, not per pass, so their contribution is
        // charged to the camera. drawMeshes() also runs in the depth prepass and in every shadow
        // view; those are not counted, so `geometry.depth` and `geometry.shadow` are floors in a
        // scene that has meshed SDFs in it. The SDF renderer is not instrumented here (it is not
        // this phase's file to change).
        stats_.geometry.camera.triangles += stats_.sdf.meshTriangles;
        stats_.geometry.camera.instances += stats_.sdf.meshObjects;
        stats_.geometry.camera.draws += stats_.sdf.meshObjects;
        if (sdfs_->hasRaymarchWork()) {
            rp.End();
            const std::array<wgpu::TextureView, kAuxTargetCount> raymarchAux = {normalRough_.view, velocity_.view,
                                                                                emission_.view, ids_.view};
            sdfs_->encodeRaymarchPass(encoder, hdr_.colorView(), hdr_.depthView(), frameBindGroup_, iblBindGroup_,
                                      scene, [this](const scene::Material& m) { return materialBindGroup(m); },
                                      raymarchAux.data(), kAuxTargetCount);
            stats_.drawCalls += stats_.sdf.raymarchObjects;
            for (auto& attachment : attachments) {
                attachment.loadOp = wgpu::LoadOp::Load;
            }
            depth.depthLoadOp = wgpu::LoadOp::Load;
            pass.label = "scene-pass-after-sdf";
            pass.timestampWrites = timeline_->mark("scene", gpu::FrameTimeline::PassKind::Render);
            rp = encoder.BeginRenderPass(&pass);
            rp.SetBindGroup(0, frameBindGroup_);
            rp.SetBindGroup(3, iblBindGroup_);
        }
        // A procedural sky lights the scene without necessarily standing behind it: existing
        // scenes keep their flat background unless `env/sky/background` asks for the sky (ADR-036).
        const bool drawSky = ibl && (!ibl_.fromSky || scene.environment.sky.showBackground);
        if (scene.environment.showSkybox && drawSky) {
            rp.SetPipeline(skyboxPipeline_);
            const std::uint32_t zeroOffset = 0; // layout requires group 1; the skybox ignores it
            rp.SetBindGroup(1, objectBindGroup_, 1, &zeroOffset);
            rp.SetBindGroup(2, materialBindGroup(scene::Material{}));
            rp.Draw(3);
            ++stats_.drawCalls;
            // Counted in the legacy total but not in `geometry`: it is one triangle of sky-sized
            // fragment work, and a geometry budget that a resolution change moves is not one.
            ++stats_.triangles;
            ++stats_.state.pipelineBinds;
            stats_.state.bindGroupBinds += 2;
        }
        // ---- water (ADR-091) ----
        // After the sky and before the particles: the surface has to composite over the bed and
        // the bank, which are opaque and already drawn, and the motes and spores above it have to
        // composite over the surface. Its own pipeline, so the branch it needs is not in the
        // shader every other entity in the world runs.
        if (!water.empty() && water_->ready()) {
            rp.SetPipeline(water_->pipeline());
            ++stats_.state.pipelineBinds;
            for (const auto& item : water) {
                const GpuMesh& mesh = meshes_[item.entity->mesh];
                const std::uint32_t waterOffset = water_->offset(item.water);
                rp.SetBindGroup(1, objectBindGroup_, 1, &item.offset);
                rp.SetBindGroup(2, water_->bindGroup(), 1, &waterOffset);
                rp.SetVertexBuffer(0, mesh.vertices);
                rp.SetIndexBuffer(mesh.indices, wgpu::IndexFormat::Uint32);
                rp.DrawIndexed(mesh.indexCount);
                stats_.geometry.camera.record(mesh.indexCount, 1, false);
                stats_.state.bindGroupBinds += 2;
                ++stats_.state.vertexBufferBinds;
                ++stats_.state.indexBufferBinds;
                ++stats_.drawCalls;
            }
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
    stage(cpu.sceneEncodeMs);

    // ---- volumetric atmosphere (ADR-032): half-res raymarch + depth-aware composite ----
    // Skipped entirely when Environment::volumeDensity is 0, so scenes without fog are unchanged.
    if (toggles_.volume) {
        volumes_->update(scene, time, hdr_.width(), hdr_.height(), hdr_.depthView(), fields_.get(),
                         particles_->glowSystems());
        volumes_->encode(encoder, hdr_.colorView(), frameBindGroup_);
    }
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
        pass.timestampWrites = timeline_->mark("debug", gpu::FrameTimeline::PassKind::Render);
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        debug_->render(rp, frameBindGroup_, debugDepthTest_);
        rp.End();
        debug_->clear();
    }
    stage(cpu.volumeEncodeMs);

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
            pass.timestampWrites = timeline_->mark("shaderlayer", gpu::FrameTimeline::PassKind::Render);
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
        postIn.velocity = velocity_.view; // ADR-040: motion blur reconstructs from it
        postIn.width = hdr_.width();
        postIn.height = hdr_.height();
        postIn.prevViewProj = havePrevViewProj_ ? prevViewProj_ : frame.viewProj;
        postIn.invViewProj = frame.invViewProj;
        postIn.cameraPos = scene.camera.position;
        postIn.frameIndex = time.frameIndex;
        // ADR-037/040: the shutter that sets the motion-blur length belongs to the camera. The
        // engine already mirrors the whole lens into post settings; a scene driven through this
        // renderer directly may not have, so the shutter is taken from the camera either way.
        scene::PostSettings postSettings = scene.post;
        postSettings.lens.shutterAngle = scene.camera.lens.shutterAngle;
        postIn.settings = &postSettings;
        postIn.composition = &scene.composition; // ADR-038 depth layers grade the composite
        if (toggles_.post) {
            finalHdr = postProcessor_->run(encoder, postIn, *pool_);
        }
        if (toggles_.post && postProcessor_->outputTexture() != nullptr) {
            hdrOutput_ = postProcessor_->outputTexture();
        }
        stats_.post = postProcessor_->stats();
        stats_.transientTextures = static_cast<std::uint32_t>(pool_->size());
    }
    prevViewProj_ = frame.viewProj;
    havePrevViewProj_ = true;
    stage(cpu.postEncodeMs);

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
        pass.timestampWrites = timeline_->mark("auxdebug", gpu::FrameTimeline::PassKind::Render);
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        rp.SetPipeline(auxDebugPipeline_);
        rp.SetBindGroup(0, auxDebugGroup_);
        rp.Draw(3);
        rp.End();
        ++stats_.drawCalls;
        ++stats_.state.pipelineBinds;
        ++stats_.state.bindGroupBinds;
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
        pass.timestampWrites = timeline_->mark("tonemap", gpu::FrameTimeline::PassKind::Render);
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        rp.SetPipeline(*pipeline);
        rp.SetBindGroup(0, finalHdr.Get() == hdr_.colorView().Get() ? tonemapBindGroup_ : tonemapBindGroupFor(finalHdr));
        rp.Draw(3);
        rp.End();
        ++stats_.drawCalls;
        ++stats_.triangles; // as the skybox above: a fullscreen triangle, not scene geometry
        ++stats_.state.pipelineBinds;
        ++stats_.state.bindGroupBinds;
    }
    // ---- pass 3: the 2D composition over the finished picture (ADR-083) ----
    // Display-referred, after the tone map, loading the target rather than clearing it. Nothing
    // here knows what the scene was; that is the point.
    if (overlay_ != nullptr) {
        overlay_->encodeOverlay(encoder, target);
    }
    timeline_->resolve(encoder);
    pool_->endFrame();

    // ---- what the frame actually submitted (ADR-077) ----
    // Folded here, at the end of encoding, because this is the first moment every draw has been
    // recorded. The procedural renderer counts its own three budgets as it records them, and the
    // instance counts behind its indirect draws come from the last completed cull readback -- see
    // SubmittedGeometry::estimatedDraws for what that means.
    {
        const ProceduralStats& proc = procedurals_->stats();
        stats_.geometry.camera += proc.submittedCamera;
        stats_.geometry.depth += proc.submittedDepth;
        stats_.geometry.shadow += proc.submittedShadow;
        // The pre-cull figure, kept under a name that says so. Entities have neither LOD nor a
        // cull pass, so for them submitted and logical are the same number; this is the ecology's.
        stats_.geometry.logicalTriangles = proc.logicalTriangles;
        stats_.geometry.logicalInstances = proc.instances;
        stats_.drawCalls += proc.drawCalls;
        stats_.state += proc.state;
        stats_.state.renderPasses = timeline_->renderPasses();
        stats_.state.computePasses = timeline_->computePasses();
        stats_.unclassifiedPasses = timeline_->unclassifiedPasses();
        // Added to, not assigned: the fullscreen triangles counted during the pass above stay in
        // the legacy total, which is what `tests/rendering/test_gpu.cpp` pins and what the editor
        // status line has always shown.
        stats_.triangles += static_cast<std::uint32_t>(
            std::min<std::uint64_t>(stats_.geometry.camera.triangles, 0xFFFFFFFFull - stats_.triangles));
    }
    stage(cpu.tonemapEncodeMs);
    cpu.totalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - renderStart).count();
    // AVGEN_FRAME_COUNTERS=1 prints the submission split. The headline `tris=` and `draws=` reach
    // the log through src/app, but the per-pass split, the staleness flags and the pass
    // classification do not, and a counter nobody can get at from a command line does not get read.
    static const bool dumpCounters = std::getenv("AVGEN_FRAME_COUNTERS") != nullptr;
    if (dumpCounters && time.frameIndex % 30 == 0) {
        const GeometryCounters& g = stats_.geometry;
        const SubmittedGeometry procShadow = procedurals_->stats().submittedShadow;
        log::info("submitted: camera {} tris / {} inst / {} draws ({} estimated, {} unmeasured); "
                  "depth {} / {} / {}; shadow {} / {} / {} over {} casters; logical {} tris / {} inst; "
                  "binds {}pipe {}group {}vb {}ib ({} redundant avoided); passes {}render {}compute "
                  "{}unclassified; shadow split: ecology {} tris / {} inst, entities {} tris / {} inst",
                  g.camera.triangles, g.camera.instances, g.camera.draws, g.camera.estimatedDraws,
                  g.camera.unmeasuredDraws, g.depth.triangles, g.depth.instances, g.depth.draws,
                  g.shadow.triangles, g.shadow.instances, g.shadow.draws, stats_.shadowCasters,
                  g.logicalTriangles, g.logicalInstances, stats_.state.pipelineBinds,
                  stats_.state.bindGroupBinds, stats_.state.vertexBufferBinds,
                  stats_.state.indexBufferBinds, stats_.state.redundantBindsAvoided,
                  stats_.state.renderPasses, stats_.state.computePasses, stats_.unclassifiedPasses,
                  // The shadow budget split by who spent it. `geometry.shadow` sums the ecology's
                  // instanced draws and the entity meshes (terrain chunks among them), and those
                  // two have very different shapes -- thousands of small instanced draws against a
                  // handful of large single ones -- so the combined figure's triangles-per-instance
                  // says nothing about either. Without the split, "shadows submit 1.85x the
                  // camera's geometry" cannot be turned into a decision about what to change.
                  procShadow.triangles, procShadow.instances,
                  g.shadow.triangles - procShadow.triangles,
                  g.shadow.instances - procShadow.instances);
    }
    // AVGEN_CPU_STAGES=1 prints the breakdown. It is reachable from the API as stats().cpu, but the
    // headless benchmark's own log line is in src/app and a measurement nobody can get at from a
    // command line does not get taken. Read once, like AVGEN_TIMELINE_RAW.
    static const bool dumpStages = std::getenv("AVGEN_CPU_STAGES") != nullptr;
    if (dumpStages && time.frameIndex % 30 == 0) {
        log::info("cpu stages (ms): total {:.3f} = uploads {:.3f} lights {:.3f} objects {:.3f} "
                  "fields {:.3f} sim {:.3f} particles {:.3f} procedural {:.3f} sdf {:.3f} "
                  "shadow {:.3f} background {:.3f} depth {:.3f} scene {:.3f} volume {:.3f} "
                  "post {:.3f} tonemap {:.3f} | unattributed {:.4f}",
                  cpu.totalMs, cpu.uploadsMs, cpu.lightsMs, cpu.objectsMs, cpu.fieldsMs,
                  cpu.simulationMs, cpu.particlesMs, cpu.proceduralMs, cpu.sdfMs, cpu.shadowEncodeMs,
                  cpu.backgroundEncodeMs, cpu.depthEncodeMs, cpu.sceneEncodeMs, cpu.volumeEncodeMs,
                  cpu.postEncodeMs, cpu.tonemapEncodeMs, cpu.unattributedMs());
    }
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
    // The three costs the live path does not pay here and the offline one does. They are outside
    // render(), so without them the CPU breakdown of an offline frame would stop at the last pass
    // encoded and the wall clock around it would be unexplained (ADR-077).
    const auto elapsed = [](std::chrono::steady_clock::time_point from) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count();
    };
    auto mark = std::chrono::steady_clock::now();
    wgpu::CommandBuffer commands = encoder.Finish();
    stats_.cpu.finishMs = elapsed(mark);
    mark = std::chrono::steady_clock::now();
    context_.queue().Submit(1, &commands);
    stats_.cpu.submitMs = elapsed(mark);
    collectFrameTimings();
    mark = std::chrono::steady_clock::now();
    context_.waitForQueue();
    stats_.cpu.queueWaitMs = elapsed(mark);
    stats_.cpu.totalMs += stats_.cpu.finishMs + stats_.cpu.submitMs + stats_.cpu.queueWaitMs;
    // Again after the queue drained: a frame submitted two or three frames ago has landed in the
    // readback ring by now, so the numbers reported alongside this frame are only that stale.
    collectFrameTimings();
    return texture;
}

// Pumps the frame timeline and fans its per-pass intervals out into stats(). Everything here is
// non-blocking: the timeline reports whichever frame's readback has completed, two or three back.
void SceneRenderer::collectFrameTimings() {
    timeline_->collect();
    // Each subsystem reads its own label off the timeline (msFor) and keeps the "did this pass run
    // at all this frame" bookkeeping it already had, so a pass that is off still reports -1.
    procedurals_->collectTimings();
    particles_->collectTimings();
    sdfs_->collectTimings();
    volumes_->collectTimings();
    simulation_->collectTimings();
    ao_->collectTimings();
    shadowMask_->collectTimings();

    stats_.gpuFrameMs = timeline_->frameMs();
    stats_.gpuPasses = static_cast<std::uint32_t>(timeline_->passes().size());
    // Re-read the procedural stats now that every pass has recorded its draws. The copy taken
    // during update() is made before a single draw exists, so any counter incremented while
    // recording -- indirect draws, empty draws -- was being thrown away and read back as zero.
    stats_.procedural = procedurals_->stats();
    stats_.particles = particles_->stats();
    stats_.sdf = sdfs_->stats();
    stats_.volume = volumes_->stats();
    stats_.simulation = simulation_->stats();
    stats_.skinning = skinning_->stats(); // ADR-086
    stats_.ao = ao_->stats();
    stats_.shadowMask = shadowMask_->stats();
    stats_.shadows.shadowMs = stats_.shadows.views > 0 ? timeline_->msFor("shadow") : -1.0;
    stats_.post.postMs = timeline_->msForPrefix("post/");

    // ---- frame-wide submission accounting (the performance spec's counters) ----
    stats_.indirectDraws = stats_.procedural.indirectDraws;
    stats_.emptyDraws = stats_.procedural.emptyIndirectDraws;
    stats_.skippedDraws = stats_.procedural.skippedIndirectDraws;
    stats_.visibleInstances = stats_.procedural.visibleInstances;
    stats_.culledInstances = stats_.procedural.culledInstances;
    for (std::size_t i = 0; i < 4; ++i) {
        stats_.lodCounts[i] = stats_.procedural.lodCounts[i];
    }
    // Assigned, not accumulated: this runs twice per submitted frame (once before the queue
    // drains and once after), and a counter that adds on each pass would double what it reports.
    stats_.computeDispatches = clusterDispatches_ + stats_.simulation.dispatches +
                               stats_.particles.dispatches + stats_.procedural.effectorDispatches +
                               stats_.procedural.cullDispatches;
    stats_.shadowDraws = stats_.shadows.entityDraws;
}

Result<void> SceneRenderer::renderFrame(const scene::Scene& scene, const FrameTime& time,
                                        std::uint32_t width, std::uint32_t height,
                                        const ShaderFrameInputs* shaderInputs) {
    auto texture = renderSubmitted(scene, time, width, height, shaderInputs);
    if (!texture) {
        return std::unexpected(texture.error());
    }
    return {};
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
