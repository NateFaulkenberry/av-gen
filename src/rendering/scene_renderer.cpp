#include "rendering/scene_renderer.hpp"
#include "core/phase2_probe.hpp" // TEMPORARY: ui-responsiveness phase 2

#include "rendering/environment.hpp"
#include "rendering/scene_targets.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "scene/mesh_metrics.hpp"

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
#include <span>
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
    case AuxDebugView::LinearDepth: return "linear depth";
    case AuxDebugView::DepthEdges: return "depth edges";
    case AuxDebugView::ObjectDepth: return "object depth";
    case AuxDebugView::Overdraw: return "overdraw";
    case AuxDebugView::FragmentDensity: return "fragment density";
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

// The material's numbers, checked before they are packed into an object slot (forensics 9.3).
//
// A non-finite material is not a small error. A NaN in `baseColor` or `emissive` becomes a NaN
// pixel, and a NaN pixel spreads: bloom's downsample averages it across a whole tile, the tone map
// carries it to the frame, and what arrives is a white or black region with no object anywhere near
// it that could be blamed. The clamps the material already has cannot help, for the reason the
// bounds folds could not -- `std::clamp` is comparisons, and a NaN loses every one of them.
//
// So a bad material is *replaced*, loudly, rather than dropped or passed on: magenta at full
// roughness is unmistakably wrong on screen and stays where the object is, which is a thing somebody
// can chase. Dropping the draw would make the object vanish, which is the report that is hardest to
// act on -- this investigation has already spent time on one of those.
struct MaterialCheck {
    bool ok = true;
    const char* field = "";
    float value = 0.0f; // the offending number, so the report says what arrived and not only where
};

MaterialCheck checkMaterial(const scene::Material& m) {
    const auto bad3 = [](const glm::vec3& v, const char* name) -> std::optional<MaterialCheck> {
        for (int i = 0; i < 3; ++i) {
            if (!std::isfinite(v[i])) {
                return MaterialCheck{false, name, v[i]};
            }
        }
        return std::nullopt;
    };
    const auto bad1 = [](float v, const char* name) -> std::optional<MaterialCheck> {
        return std::isfinite(v) ? std::nullopt : std::optional<MaterialCheck>{MaterialCheck{false, name, v}};
    };
    if (auto c = bad3(m.baseColor, "baseColor")) return *c;
    if (auto c = bad1(m.opacity, "opacity")) return *c;
    if (auto c = bad3(m.emissiveColor, "emissiveColor")) return *c;
    if (auto c = bad1(m.emissiveIntensity, "emissiveIntensity")) return *c;
    if (auto c = bad1(m.roughness, "roughness")) return *c;
    if (auto c = bad1(m.metallic, "metallic")) return *c;
    if (auto c = bad1(m.normalScale, "normalScale")) return *c;
    if (auto c = bad1(m.occlusionStrength, "occlusionStrength")) return *c;
    if (auto c = bad1(m.alphaCutoff, "alphaCutoff")) return *c;
    return {};
}

bool finiteMatrix(const glm::mat4& matrix) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(matrix[column][row])) {
                return false;
            }
        }
    }
    return true;
}

bool nearlyEqual(const glm::vec3& a, const glm::vec3& b) {
    return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(1e-5f)));
}

bool nearlyEqual(const glm::mat4& a, const glm::mat4& b) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            if (std::abs(a[column][row] - b[column][row]) > 1e-5f) {
                return false;
            }
        }
    }
    return true;
}

void hashBytes(std::uint64_t& hash, const void* data, std::size_t size) {
    constexpr std::uint64_t kPrime = 1099511628211ull;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= kPrime;
    }
}

void hashDiagnosticFrame(RendererDiagnosticFrame& frame) {
    frame.stateHash = 1469598103934665603ull;
    hashBytes(frame.stateHash, &frame.cameraPosition, sizeof(frame.cameraPosition));
    hashBytes(frame.stateHash, &frame.view, sizeof(frame.view));
    hashBytes(frame.stateHash, &frame.projection, sizeof(frame.projection));
    for (const RenderObjectDiagnostic& object : frame.objects) {
        hashBytes(frame.stateHash, &object.entityIndex, sizeof(object.entityIndex));
        hashBytes(frame.stateHash, &object.objectSlot, sizeof(object.objectSlot));
        hashBytes(frame.stateHash, &object.mesh, sizeof(object.mesh));
        hashBytes(frame.stateHash, &object.materialHash, sizeof(object.materialHash));
        hashBytes(frame.stateHash, &object.worldPosition, sizeof(object.worldPosition));
        hashBytes(frame.stateHash, &object.worldMatrix, sizeof(object.worldMatrix));
        hashBytes(frame.stateHash, &object.worldBoundsMin, sizeof(object.worldBoundsMin));
        hashBytes(frame.stateHash, &object.worldBoundsMax, sizeof(object.worldBoundsMax));
        hashBytes(frame.stateHash, object.frustumMargins.data(), sizeof(object.frustumMargins));
        hashBytes(frame.stateHash, &object.rigIndex, sizeof(object.rigIndex));
        hashBytes(frame.stateHash, &object.jointCount, sizeof(object.jointCount));
        hashBytes(frame.stateHash, &object.paletteVersion, sizeof(object.paletteVersion));
        hashBytes(frame.stateHash, &object.paletteTime, sizeof(object.paletteTime));
        hashBytes(frame.stateHash, &object.visible, sizeof(object.visible));
        hashBytes(frame.stateHash, &object.cameraCulled, sizeof(object.cameraCulled));
        hashBytes(frame.stateHash, &object.submitted, sizeof(object.submitted));
        hashBytes(frame.stateHash, &object.finite, sizeof(object.finite));
    }
}

} // namespace

const RenderObjectDiagnostic* SceneRenderer::diagnosticObject(std::string_view name) const {
    for (const RenderObjectDiagnostic& object : diagnosticFrame_.objects) {
        if (object.name == name) {
            return &object;
        }
    }
    return nullptr;
}

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
      cosmicOcean_(std::make_unique<CosmicOceanRenderer>(context, shaders)),
      debug_(std::make_unique<DebugDraw>(context, shaders)),
      simulation_(std::make_unique<Simulation>(context, shaders)),
      shadows_(std::make_unique<ShadowRenderer>(context)),
      skinning_(std::make_unique<SkinningRenderer>(context, shaders)),
      ao_(std::make_unique<AoRenderer>(context, shaders)),
      shadowMask_(std::make_unique<ShadowMaskRenderer>(context, shaders)),
      water_(std::make_unique<WaterRenderer>()),
      postProcessor_(std::make_unique<PostProcessor>(context, shaders)),
      temporal_(std::make_unique<TemporalEffects>(context, shaders)), pool_(std::make_unique<gpu::TransientPool>(context)) {
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
        // ADR-137: filterable, with a linear sampler, so the tonemap can upscale a scene target
        // rendered below output resolution. RGBA16Float is filterable on this backend.
        std::array<wgpu::BindGroupLayoutEntry, 3> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].texture.sampleType = wgpu::TextureSampleType::Float;
        entries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Fragment;
        entries[1].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[1].buffer.minBindingSize = sizeof(TonemapUniforms);
        entries[2].binding = 2;
        entries[2].visibility = wgpu::ShaderStage::Fragment;
        entries[2].sampler.type = wgpu::SamplerBindingType::Filtering;
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
        desc.label = "tonemap-uniforms";
        desc.size = sizeof(TonemapUniforms);
        tonemapUniforms_ = device.CreateBuffer(&desc);
    }
    // ADR-128: the object buffer and its bind group are made by the same routine that later grows
    // them, so the allocation path has exactly one implementation and the first one is not special.
    ensureObjectCapacity(kInitialObjects);

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
        // ADR-137: the tonemap's upscale when the scene target is smaller than the output. Clamped
        // and linear, with no mip filtering -- the HDR target has one level.
        desc.label = "tonemap-sampler";
        desc.addressModeU = wgpu::AddressMode::ClampToEdge;
        desc.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
        tonemapSampler_ = device.CreateSampler(&desc);
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
    // ADR-099: water draws inside the scene pass with the scene's own frame, object and IBL
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
    // ADR-390. Its own pipeline and its own uniform, drawn inside the scene pass after the
    // atmospheric sky layer. A failure here is not fatal to the frame: a scene with no Cosmic Ocean
    // must still render, so the error is reported and the draw simply never happens.
    if (auto r = cosmicOcean_->init(kHdrFormat, kDepthFormat, frameLayout_); !r) {
        return r;
    }
    if (auto r = simulation_->init(fields_->buffer(), fields_->gridBuffer()); !r) {
        return r;
    }
    if (auto r = postProcessor_->init(); !r) {
        return r;
    }
    if (auto r = temporal_->init(); !r) {
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
                skyDeferral_ = scene::RebuildDeferral{};
            }
            return;
        }
        const scene::SkyRuntime sky = scene::resolveSky(scene.environment.sky, scene.lights);
        const std::uint64_t hash = sky.hash();
        if (skyBuilt_ && hash == skyHash_ && ibl_.valid) {
            skyDeferral_.deferring = false;
            return;
        }
        // ADR-233. The chain below is `processSky`, whose own header says "blocks like `process`
        // does; call it when the sky's hash changes, not per frame" -- and a drag on any of the ten
        // sky parameters, or on the key light the sun is resolved from, changes the hash every
        // frame. Measured interleaved against an idle arm in one process, a sky drag ran the chain
        // on 46% of its frames and took `render.record` from 0.96 ms median to 26.8 ms mean / 78.8
        // p95, and the frame from 32.9 ms median to 48.4.
        //
        // So the same policy ADR-084 gave procedural geometry: an expensive rebuild waits for the
        // inputs to hold still, with a ceiling so a long drag still refreshes. The sky that is on
        // screen meanwhile is the last one built, which is a few frames behind -- and the canvas
        // says so, because `environmentAwaitingRebuild()` feeds the activity indicator.
        //
        // The guard is `skyBuilt_ && ibl_.valid`: a scene that has never had a sky gets one at
        // once, whatever it costs. There is nothing to be behind.
        if (interactiveEnvBudgetMs_ > 0.0 && skyBuilt_ && ibl_.valid &&
            skyDeferral_.lastMs > interactiveEnvBudgetMs_) {
            const auto now = std::chrono::steady_clock::now();
            const double sinceMs = lastSkyPollTime_.time_since_epoch().count() == 0
                                       ? 0.0
                                       : std::chrono::duration<double, std::milli>(now - lastSkyPollTime_).count();
            lastSkyPollTime_ = now;
            if (!scene::advanceRebuildDeferral(skyDeferral_, hash, skyHash_, sinceMs)) {
                return;
            }
        }
        const auto skyStart = std::chrono::steady_clock::now();
        auto built = environment_->processSky(sky);
        if (!built) {
            log::error("procedural sky: {}", built.error().message);
            setIbl(IblResources{});
            skyBuilt_ = false;
            return;
        }
        // What it cost, which is what decides whether the next one is allowed to wait. Measured
        // rather than assumed: the chain's cost depends on the cube and prefilter sizes, and a
        // hard-coded "skies are expensive" would defer a cheap one for no reason.
        skyDeferral_.lastMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - skyStart).count();
        skyDeferral_.deferring = false;
        skyDeferral_.settledForMs = 0.0;
        skyDeferral_.heldForMs = 0.0;
        built->fromSky = true;
        setIbl(*built);
        skyBuilt_ = true;
        skyHash_ = hash;
        return;
    }
    if (&scene == environmentScene_ && scene.identity == environmentIdentity_ && id == environmentTexture_ &&
        scene.textureVersion == environmentVersion_) {
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
    environmentScene_ = &scene;
    environmentIdentity_ = scene.identity;
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

    // ADR-115: the overdraw counter, one atomic<u32> per pixel. Sized here alongside the other
    // auxiliary targets rather than lazily inside render() so the aux-debug bind group -- built once
    // and shared across every view -- always has something valid at its storage-buffer binding, even
    // when the overdraw views are never selected. The counting pass that fills it is still opt-in;
    // only the allocation is unconditional.
    {
        const std::uint64_t bytes = static_cast<std::uint64_t>(width) * height * sizeof(std::uint32_t);
        if (bytes != overdrawBufferBytes_) {
            wgpu::BufferDescriptor bufferDesc{};
            bufferDesc.label = "aux-overdraw-counts";
            // CopySrc so tests and tools can read the raw counts back (readBuffer(), gpu/readback.hpp)
            // rather than only judging the view through its rendered colours.
            bufferDesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst |
                               wgpu::BufferUsage::CopySrc;
            bufferDesc.size = bytes;
            overdrawBuffer_ = device.CreateBuffer(&bufferDesc);
            overdrawBufferBytes_ = bytes;
            overdrawGroup_ = nullptr;
        }
    }
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
    auto atmosphere = shaders_.load("atmosphere.wgsl");
    if (!atmosphere) return std::unexpected(atmosphere.error());
    auto tonemap = shaders_.load("tonemap.wgsl");
    if (!tonemap) return std::unexpected(tonemap.error());
    tonemapModule_ = *tonemap;
    auto linear = shaders_.load("linear_depth.wgsl");
    if (!linear) return std::unexpected(linear.error());
    auto auxDebug = shaders_.load("aux_debug.wgsl");
    if (!auxDebug) return std::unexpected(auxDebug.error());
    auto overdraw = shaders_.load("overdraw_count.wgsl");
    if (!overdraw) return std::unexpected(overdraw.error());
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
    auto atmos = createAtmospherePipeline(*atmosphere);
    if (!atmos) return std::unexpected(atmos.error());
    atmospherePipeline_ = *atmos;
    auto d = createDepthOnlyPipeline(*pbr);
    if (!d) return std::unexpected(d.error());
    depthOnlyPipeline_ = *d;
    auto l = createLinearDepthPipeline(*linear);
    if (!l) return std::unexpected(l.error());
    linearDepthPipeline_ = *l;
    if (auto x = createAuxDebugResources(*auxDebug); !x) {
        return std::unexpected(x.error());
    }
    overdrawModule_ = *overdraw;
    if (auto x = createOverdrawResources(); !x) {
        return std::unexpected(x.error());
    }
    auto o = createOverdrawCountPipeline(*overdraw);
    if (!o) return std::unexpected(o.error());
    overdrawCountPipeline_ = *o;
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

// ADR-230. Additive into the HDR target and the emission target; every other aux target keeps the
// opaque geometry's, which is the rule `water_renderer.cpp` states -- a normal or a velocity
// averaged over a transparency is worse than none at all. The comet's velocity in particular is
// deliberately left alone: writing the sky's camera-only velocity for a fast-moving comet is what
// would smear it under motion blur, and writing a correct one would need the previous frame's
// trajectory, which is state this system does not keep.
Result<wgpu::RenderPipeline> SceneRenderer::createAtmospherePipeline(const wgpu::ShaderModule& module) {
    wgpu::BlendState additive{};
    additive.color.operation = wgpu::BlendOperation::Add;
    additive.color.srcFactor = wgpu::BlendFactor::One;
    additive.color.dstFactor = wgpu::BlendFactor::One;
    additive.alpha.operation = wgpu::BlendOperation::Add;
    additive.alpha.srcFactor = wgpu::BlendFactor::One;
    additive.alpha.dstFactor = wgpu::BlendFactor::One;

    std::array<wgpu::ColorTargetState, kSceneTargetCount> colorTargets{};
    fillSceneTargets(colorTargets, kHdrFormat, nullptr);
    for (std::uint32_t i = 0; i < kSceneTargetCount; ++i) {
        colorTargets[i].writeMask =
            (i == 0 || i == 3) ? wgpu::ColorWriteMask::All : wgpu::ColorWriteMask::None;
    }
    colorTargets[0].blend = &additive;
    colorTargets[3].blend = &additive;

    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_atmosphere";
    fragment.targetCount = kSceneTargetCount;
    fragment.targets = colorTargets.data();

    // The same depth state as the sky: tested so terrain occludes it, never written so the water
    // and particles drawn after it still composite correctly.
    wgpu::DepthStencilState depth{};
    depth.format = kDepthFormat;
    depth.depthWriteEnabled = wgpu::OptionalBool::False;
    depth.depthCompare = wgpu::CompareFunction::LessEqual;

    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "atmosphere-pipeline";
    desc.layout = scenePipelineLayout_;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_atmosphere";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.depthStencil = &depth;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
    return finishPipeline(desc, "atmosphere-pipeline");
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

// ADR-115: the overdraw / fragment-density counting pass's own bind group layout and pipeline
// layout -- group 0 frame, group 1 object (both the ordinary ones), group 2 nothing but the
// counter buffer. It shares no bind group or pipeline with the real opaque pass on purpose: see
// overdraw_count.wgsl for why writing that buffer from a fragment shader must stay off the normal
// path.
Result<void> SceneRenderer::createOverdrawResources() {
    const auto& device = context_.device();
    if (!overdrawLayout_) {
        wgpu::BindGroupLayoutEntry entry{};
        entry.binding = 0;
        entry.visibility = wgpu::ShaderStage::Fragment;
        entry.buffer.type = wgpu::BufferBindingType::Storage;
        wgpu::BindGroupLayoutDescriptor layoutDesc{};
        layoutDesc.label = "overdraw-layout";
        layoutDesc.entryCount = 1;
        layoutDesc.entries = &entry;
        overdrawLayout_ = device.CreateBindGroupLayout(&layoutDesc);
    }
    std::array<wgpu::BindGroupLayout, 3> layouts = {frameLayout_, objectLayout_, overdrawLayout_};
    wgpu::PipelineLayoutDescriptor layoutDesc{};
    layoutDesc.label = "overdraw-pipeline-layout";
    layoutDesc.bindGroupLayoutCount = layouts.size();
    layoutDesc.bindGroupLayouts = layouts.data();
    overdrawPipelineLayout_ = device.CreatePipelineLayout(&layoutDesc);
    return {};
}

// Same vertex stage as the depth-only pipeline (so the geometry it counts is transformed exactly
// as the real opaque pass would), a fragment stage that only touches the counter buffer, no colour
// target, and `depthCompare = Always` with writes off: the depth attachment is present only because
// a render pass needs one, and Always means every fragment reaches the shader whatever is already
// in the buffer -- the disabled hidden-surface removal overdraw_count.wgsl's header explains.
Result<wgpu::RenderPipeline> SceneRenderer::createOverdrawCountPipeline(const wgpu::ShaderModule& module) {
    VertexLayoutStorage vertex;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = "fs_overdraw";
    fragment.targetCount = 0;
    fragment.targets = nullptr;
    wgpu::DepthStencilState depth{};
    depth.format = kDepthFormat;
    depth.depthWriteEnabled = wgpu::OptionalBool::False;
    depth.depthCompare = wgpu::CompareFunction::Always;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "overdraw-count";
    desc.layout = overdrawPipelineLayout_;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_main";
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vertex.layout;
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.frontFace = wgpu::FrontFace::CCW;
    // Back-face culled, like the real opaque pass (LitVariant::OpaqueCull) -- not None, like the
    // depth-only pipeline this otherwise mirrors. A closed mesh has a front face and a back face at
    // every covered pixel; counting both would report two hits for one solid object sitting alone in
    // the scene, which is not overdraw at all. Two-sided materials are outside this diagnostic's
    // scope for the same reason the depth prepass's conservative culling doesn't matter here: this
    // pass estimates depth complexity, not exact renderer cost.
    desc.primitive.cullMode = wgpu::CullMode::Back;
    desc.depthStencil = &depth;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
    return finishPipeline(desc, "overdraw-count");
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

Result<void> SceneRenderer::createAuxDebugResources(const wgpu::ShaderModule& module) {
    const auto& device = context_.device();
    if (!auxDebugUniforms_) {
        wgpu::BufferDescriptor bufferDesc{};
        bufferDesc.label = "aux-debug-uniforms";
        bufferDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        bufferDesc.size = sizeof(AuxDebugUniforms);
        auxDebugUniforms_ = device.CreateBuffer(&bufferDesc);
    }
    if (!auxDebugLayout_) {
        std::array<wgpu::BindGroupLayoutEntry, 8> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.minBindingSize = sizeof(AuxDebugUniforms);
        for (std::uint32_t i = 1; i < 7; ++i) {
            entries[i].binding = i;
            entries[i].visibility = wgpu::ShaderStage::Fragment;
            entries[i].texture.sampleType = i == 4 ? wgpu::TextureSampleType::Uint
                                                   : wgpu::TextureSampleType::UnfilterableFloat;
            entries[i].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        }
        // ADR-115: the overdraw counter, read-only here -- only the counting pass writes it.
        entries[7].binding = 7;
        entries[7].visibility = wgpu::ShaderStage::Fragment;
        entries[7].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
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
    auxDebugPipelineLayout_ = device.CreatePipelineLayout(&layoutDesc);
    auxDebugModule_ = module;
    return {};
}

Result<wgpu::RenderPipeline> SceneRenderer::auxDebugPipelineFor(wgpu::TextureFormat format) {
    const auto key = static_cast<std::uint32_t>(format);
    if (auto it = auxDebugPipelines_.find(key); it != auxDebugPipelines_.end()) {
        return it->second;
    }
    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = format;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState fragment{};
    fragment.module = auxDebugModule_;
    fragment.entryPoint = "fs_aux";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "aux-debug";
    desc.layout = auxDebugPipelineLayout_;
    desc.vertex.module = auxDebugModule_;
    desc.vertex.entryPoint = "vs_aux";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
    auto pipeline = finishPipeline(desc, "aux-debug");
    if (pipeline) {
        auxDebugPipelines_[key] = *pipeline;
    }
    return pipeline;
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
    // ADR-137 step 2: the caller's size is the *output* size; the scene renders at `renderScale` of
    // it and the tonemap upscales (step 1 gave it a filtered sampler for exactly this). Offline
    // forces 1.0, so a deliverable is never scaled. Rounded to even and floored at 16 px: an odd
    // width maps a column differently under a non-integer ratio, and a target small enough to lose
    // a froxel is not a quality setting.
    outputWidth_ = width;
    outputHeight_ = height;
    const float scale = std::clamp(qualitySettings_.renderScale, 0.25f, 2.0f);
    if (scale != 1.0f) {
        const auto scaled = [scale](std::uint32_t v) {
            const auto n = static_cast<std::uint32_t>(std::lround(static_cast<float>(v) * scale));
            return std::max<std::uint32_t>(16u, n & ~1u);
        };
        width = scaled(width);
        height = scaled(height);
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
    resetTemporalHistory();
    tonemapBindGroup_ = nullptr;
    tonemapBoundView_ = nullptr;
    tonemapGroups_.clear();
    // The *scene* resolution, deliberately: every per-pixel number in the harness -- fragment cost,
    // overdraw, px/triangle -- is a number about the pixels the scene pass actually shaded, not
    // about the buffer they were later stretched onto.
    stats_.width = width;
    stats_.height = height;
    if (width != outputWidth_ || height != outputHeight_) {
        log::info("render scale {:.2f}: scene target {}x{} -> output {}x{}",
                  static_cast<double>(qualitySettings_.renderScale), width, height, outputWidth_,
                  outputHeight_);
    } else {
        log::debug("HDR target resized to {}x{}", width, height);
    }
    return {};
}

std::span<const SceneRenderer::PassArm> SceneRenderer::passArms() {
    using T = SceneRenderer::PassToggles;
    static constexpr SceneRenderer::PassArm kArms[] = {
        {"shadows", &T::shadows},           {"ao", &T::ao},
        {"volume", &T::volume},             {"post", &T::post},
        {"shadowmask", &T::shadowMask},     {"culling", &T::culling},
        {"water", &T::water},               {"transparency", &T::transparency},
        {"particles", &T::particles},       {"animation", &T::animation},
        {"cameramotion", &T::cameraMotion},  {"animationmotion", &T::animationMotion},
        {"auxstore", &T::auxTargetStores},   {"fxaa", &T::antialias},
        // ADR-207. Off: the frame block reports zero effects, so the per-fragment loop in
        // world_effects.wgsl executes one uniform compare and returns. This is the arm that answers
        // "what does World Effects cost when nothing is running", which §22 of the brief asks for
        // separately from "what does one cost when it is".
        {"worldeffects", &T::worldEffects},
        // ADR-230. Off: the sky-layer draw is skipped entirely, which is what makes the "effects
        // disabled" arm in §12 a real arm rather than a frame that renders the same pixels.
        {"atmospherics", &T::atmospherics},
        // ADR-390. Off: the Cosmic Ocean's `Draw(3)` is not recorded AND its uniform is not
        // uploaded, because `update` is gated on the same toggle -- the arm removes the fragment
        // work and the uniform content together rather than measuring the same frame through one
        // more branch (ADR-182). This arm is also ADR-390 §39's acceptance test: "then disable
        // Cosmic Ocean; the scene should suddenly feel dramatically emptier".
        {"cosmic", &T::cosmicOcean},
    };
    return kArms;
}

bool SceneRenderer::setPassArm(PassToggles& toggles, std::string_view name, bool on) {
    for (const PassArm& arm : passArms()) {
        if (name == arm.name) {
            toggles.*(arm.flag) = on;
            return true;
        }
    }
    return false;
}

std::span<const SceneRenderer::QualityArm> SceneRenderer::qualityArms() {
    static constexpr SceneRenderer::QualityArm kArms[] = {
        // ADR-112's rule, off. Zero restores the pre-ADR-112 range of three scene radii -- which is
        // what the ADR's own `shadowTexelTarget` comment already promises, so this arm is a reader
        // of an existing contract rather than a new one.
        {"shadowrange", [](QualitySettings& q) { q.shadowTexelTarget = 0.0f; },
         "shadowTexelTarget=0 (the pre-ADR-112 range: three scene radii)"},
        // ADR-450. The Cosmic Ocean's two sample levers, separately, so that "what does an octave
        // of nebula cost" and "what does a cell of dust cost" are two measurements rather than one
        // tier that moved both and a guess about which mattered.
        {"cosmicoct", [](QualitySettings& q) { q.cosmicOctaveScale = 0.5f; },
         "cosmicOctaveScale=0.5 (nebula field octaves halved, and the domain warp down to one)"},
        {"cosmicsamples", [](QualitySettings& q) { q.cosmicSampleScale = 0.34f; },
         "cosmicSampleScale=0.34 (planet cells 3x3 -> 1x1, dust off)"},
        // The screen-space contact march, off. It is the one shadow term the mask does not cover,
        // so it is the term that is still evaluated per pixel per directional light.
        {"contact", [](QualitySettings& q) { q.contactShadows = false; q.contactSteps = 0; },
         "contactShadows=false (no screen-space contact march)"},
        // PCSS for the key light, off: plain PCF instead. The blocker search is `pcssBlockerTaps`
        // uninterpolated textureLoads on top of the filter, and ADR-111 measured it as the largest
        // single contributor to the mask's residual.
        {"pcss", [](QualitySettings& q) { q.softShadows = false; },
         "softShadows=false (PCF instead of PCSS for the key light)"},
        // The mask at full resolution -- the High and Offline tiers' own setting, not a fabricated
        // one. Separates "the mask pass costs this" from "the mask's half resolution saves this".
        {"maskfull", [](QualitySettings& q) { q.shadowMaskScale = 1.0f; },
         "shadowMaskScale=1.0 (the mask computed per pixel, as High/Offline do)"},
        // ADR-255's fidelity arm, and the only configuration in the engine where the mask pass runs
        // at full resolution AND the lit pass reads it. Against the same frame without it, the
        // difference is the answer to "is the exported shadow AOV the term the lit pass computed,
        // or a second opinion about it" -- which ADR-087 asserted and nobody had differenced.
        // Diagnostic: no tier sets it and no deliverable should.
        {"maskconsume", [](QualitySettings& q) { q.shadowMaskFullConsume = true; },
         "the mask at FULL resolution and consumed by the lit pass (ADR-255's fidelity arm)"},
        // The positive control for that comparison (ADR-182): halve the shadow atlas. Both arms
        // read the same atlas, so both must move -- an agreement measured by an instrument that
        // cannot disagree is not agreement.
        {"shadowatlas1k", [](QualitySettings& q) { q.shadowResolution = 1024; },
         "shadowResolution=1024 (the control arm: it must move the masked and unmasked frames alike)"},
        // ADR-133: the two material tiers, forced on every draw. These are *ceilings on the
        // saving*, not shippable configurations -- a shipping frame assigns the tier per draw from
        // importance, so only the small and distant reach it. Forcing the whole frame is how the
        // question "how much of the 8.4 ms residual can this rung reach at all" gets an answer
        // before anything is built on top of it.
        // The bound on D2: the clustered local-light loop reduced to nothing, with every other term
        // of the Full tier intact. Not shippable on a scene lit by 222 local lights -- it is the
        // ceiling the reduced rung is measured against, the way a pass arm is.
        {"matlights0",
         [](QualitySettings& q) {
             q.forcedMaterialTier = MaterialTier::ReducedLights;
             q.reducedTierLocalLights = 0;
         },
         "no local lights at all (the ceiling on what a light budget can save)"},
        {"matreduced",
         [](QualitySettings& q) { q.forcedMaterialTier = MaterialTier::ReducedLights; },
         "every draw at the reduced-lights tier (the ceiling on that rung's saving)"},
        {"matflat", [](QualitySettings& q) { q.forcedMaterialTier = MaterialTier::Flat; },
         "every draw at the flat tier (the ceiling on that rung's saving)"},
        // ADR-138's deciding arm: the flat tier on procedural draws only, entities left at Full.
        // The frame-global arms above measure a ceiling nobody can ship; this one measures the
        // share tier *assignment* could realize, which ADR-138 bounds at ~1.4 ms from a coverage
        // table and does not measure. Run this before building assignment, not after.
        {"matflatproc",
         [](QualitySettings& q) {
             q.proceduralMaterialTier = static_cast<int>(MaterialTier::Flat);
         },
         "the flat tier on procedural draws only (the assignable share, measured)"},
        // ADR-155: the shipping form of the same idea. `matflatproc` demotes every procedural draw
        // and fails §50 by flattening the foreground; these demote from a rung down, and rung
        // tracks projected size. The sweep is what says how much of that 5.64 ms survives sparing
        // what the camera is actually looking at.
        {"matrung1",
         [](QualitySettings& q) { q.materialTiers = true; q.flatTierFromRung = 1; },
         "flat from LOD rung 1 down (the foreground stays Full)"},
        {"matrung2",
         [](QualitySettings& q) { q.materialTiers = true; q.flatTierFromRung = 2; },
         "flat from LOD rung 2 down (only the far scatter is demoted)"},
        // ADR-139. The volumetric march at the resolution the High and Offline tiers ask for --
        // their own setting, not a fabricated one -- so the pair says what half resolution buys
        // rather than what an invented scale would.
        {"volumefull", [](QualitySettings& q) { q.volumeResolutionScale = 1.0f; },
         "volumeResolutionScale=1.0 (the march per pixel, as High/Offline do)"},
        // The Preview tier's volumetric settings, likewise its own: quarter resolution and half
        // the authored step count. Together with `volumefull` these bracket the shipped default.
        {"volumepreview",
         [](QualitySettings& q) { q.volumeResolutionScale = 0.25f; q.volumeStepScale = 0.5f; },
         "volumeResolutionScale=0.25, volumeStepScale=0.5 (the Preview tier's volume)"},
        // Resolution alone, at the Preview tier's scale, with the step count left where the scene
        // authored it. `volumepreview` moves both axes at once and so cannot say which one paid;
        // this arm and `volumesteps` are the two halves of it, and they do not divide evenly --
        // see ADR-141.
        {"volumequarter", [](QualitySettings& q) { q.volumeResolutionScale = 0.25f; },
         "volumeResolutionScale=0.25 (quarter-resolution march, authored step count)"},
        // Resolution held at the default and only the ray sampling halved: the axis that trades
        // banding along the ray, separated from the one that trades detail at silhouettes.
        {"volumesteps", [](QualitySettings& q) { q.volumeStepScale = 0.5f; },
         "volumeStepScale=0.5 (half the authored march steps, resolution unchanged)"},
    };
    return kArms;
}

bool SceneRenderer::setQualityArm(QualitySettings& settings, std::string_view name) {
    for (const QualityArm& arm : qualityArms()) {
        if (name == arm.name) {
            arm.apply(settings);
            return true;
        }
    }
    return false;
}

std::string SceneRenderer::qualityArmNames() {
    std::string names;
    for (const QualityArm& arm : qualityArms()) {
        names += names.empty() ? arm.name : std::string(",") + arm.name;
    }
    return names;
}

std::string_view SceneRenderer::qualityArmDescription(std::string_view name) {
    for (const QualityArm& arm : qualityArms()) {
        if (name == arm.name) {
            return arm.what;
        }
    }
    return {};
}

std::string SceneRenderer::passArmNames() {
    std::string names;
    for (const PassArm& arm : passArms()) {
        names += names.empty() ? arm.name : std::string(",") + arm.name;
    }
    return names;
}

void SceneRenderer::setPassToggles(const PassToggles& toggles) {
    // Arming the camera freeze captures nothing here: the *next* frame records the view it is asked
    // to freeze, and every frame after it reuses that. Capturing at the moment of the click would
    // freeze whatever the last rendered frame happened to be, which is one frame older than what the
    // person is looking at when they press it.
    if (toggles.cameraMotion && !toggles_.cameraMotion) {
        haveFrozenCamera_ = false;
    }
    toggles_ = toggles;
}

void SceneRenderer::resetTemporalHistory() {
    havePrevViewProj_ = false;
    prevModels_.clear();
    prevModelsNext_.clear();
    previousRenderTime_ = -std::numeric_limits<double>::infinity();
    temporalScene_ = nullptr;
    if (ao_ != nullptr) {
        ao_->resetHistory();
    }
    // The particle pools are temporal history too, and they were the one kind this call did not
    // reset. A pool carries alive lists, emit carry and trail rings across frames, so a renderer
    // that had played forty frames and then seeked back to half a second did not agree with a fresh
    // one -- 63 channels of a hundred thousand, which is the size of difference that reads as noise
    // and is not. `ParticleRenderer::resetAll` has said in its own comment since it was written that
    // it is "used on seek/offline restarts"; nothing was calling it except the scene-pointer change,
    // so a seek that kept the same scene kept the simulation.
    if (particles_ != nullptr) {
        particles_->resetAll();
    }
    // ADR-410: the temporal ring is history in exactly the sense this function means. It rides the
    // existing hook rather than a second one, so a seek, a cut, a resize and a scene swap all
    // invalidate it without anyone having to remember a new call.
    if (temporal_ != nullptr) {
        temporal_->reset();
    }
}

void SceneRenderer::ensureTonemapBindGroup() {
    if (tonemapBindGroup_ && tonemapBoundView_.Get() == hdr_.colorView().Get()) {
        return;
    }
    std::array<wgpu::BindGroupEntry, 3> entries{};
    entries[0].binding = 0;
    entries[0].textureView = hdr_.colorView();
    entries[1].binding = 1;
    entries[1].buffer = tonemapUniforms_;
    entries[1].size = sizeof(TonemapUniforms);
    entries[2].binding = 2;
    entries[2].sampler = tonemapSampler_;
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
    if (auto atmosphere = shaders_.load("atmosphere.wgsl")) {
        if (auto ap = createAtmospherePipeline(*atmosphere)) {
            atmospherePipeline_ = *ap;
        } else {
            keep("atmosphere.wgsl", std::unexpected(ap.error()));
        }
    } else {
        keep("atmosphere.wgsl", std::unexpected(atmosphere.error()));
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
    if (auto r = cosmicOcean_->reload(); !r) {
        keep("cosmic_ocean.wgsl", std::unexpected(r.error()));
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
    std::array<wgpu::BindGroupEntry, 3> entries{};
    entries[0].binding = 0;
    entries[0].textureView = view;
    entries[1].binding = 1;
    entries[1].buffer = tonemapUniforms_;
    entries[1].size = sizeof(TonemapUniforms);
    entries[2].binding = 2;
    entries[2].sampler = tonemapSampler_;
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
    // Address, identity and version, all three. The address alone is not an identity: a scene
    // destroyed and another built in its place lands at the same one, and every fresh Scene starts
    // its meshVersion at the same value -- so a new world with the same number of meshes was drawn
    // with the old world's geometry until something happened to bump the counter.
    if (&scene == meshScene_ && scene.identity == meshIdentity_ && scene.meshVersion == meshVersion_ &&
        meshes_.size() == scene.meshes.size()) {
        return;
    }
    // TEMPORARY (ui-responsiveness phase 2): this early-out was not taken, so every mesh in the
    // scene is about to be destroyed and re-created -- all of them, not the ones that changed.
    const probe2::Add probeUpload(probe2::frame().meshUploadMs);
    ++probe2::frame().meshUploadPasses;
    probe2::frame().meshesUploaded += scene.meshes.size();
    meshes_.clear();
    meshes_.reserve(scene.meshes.size());
    // ADR-351. Rebuilt with the meshes and from the same version, because a chain is geometry and
    // a chain left behind by a scene that has been re-flattened is the derived-copy defect this
    // engine has found nine of.
    meshLods_.assign(scene.meshes.size(), GpuMeshLod{});
    anyMeshLod_ = false;
    representation_.reset();
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
    // The rungs. Uploaded exactly the way LOD0 was, which is the point of holding them as MeshData:
    // there is one upload path and one GpuMesh, so a rung cannot be drawn through a path the source
    // mesh has never been through.
    for (const scene::MeshLodChain& chain : scene.meshLods) {
        if (chain.base >= meshLods_.size() || chain.levels.empty()) {
            continue;
        }
        GpuMeshLod& out = meshLods_[chain.base];
        out.hysteresis = chain.hysteresis;
        out.maxScreenError = chain.maxScreenError;
        // Rung 0 is the source, so the selector's level index and this array agree without an
        // off-by-one anywhere: `levels[n - 1]` is what rung n draws.
        out.rungs.push_back(LodRung{chain.sourceSurfaceArea, chain.sourceTriangles, 0.0f});
        for (const scene::MeshLodLevel& level : chain.levels) {
            if (!level.mesh.valid() || level.mesh.indices.empty()) {
                continue;
            }
            GpuMesh rung;
            wgpu::BufferDescriptor vdesc{};
            vdesc.label = "mesh-lod-vertices";
            vdesc.size = level.mesh.vertices.size() * sizeof(scene::Vertex);
            vdesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
            rung.vertices = context_.device().CreateBuffer(&vdesc);
            context_.queue().WriteBuffer(rung.vertices, 0, level.mesh.vertices.data(), vdesc.size);
            wgpu::BufferDescriptor idesc{};
            idesc.label = "mesh-lod-indices";
            idesc.size = level.mesh.indices.size() * sizeof(std::uint32_t);
            idesc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
            rung.indices = context_.device().CreateBuffer(&idesc);
            context_.queue().WriteBuffer(rung.indices, 0, level.mesh.indices.data(), idesc.size);
            rung.indexCount = static_cast<std::uint32_t>(level.mesh.indices.size());
            out.levels.push_back(std::move(rung));
            out.rungs.push_back(LodRung{level.surfaceArea, level.triangles, level.error});
        }
        if (out.levels.empty()) {
            out.rungs.clear();
        } else {
            anyMeshLod_ = true;
        }
    }
    meshScene_ = &scene;
    meshIdentity_ = scene.identity;
    meshVersion_ = scene.meshVersion;
}

void SceneRenderer::uploadTextures(const scene::Scene& scene) {
    if (&scene == textureScene_ && scene.identity == textureIdentity_ &&
        scene.textureVersion == textureVersion_ && textures_.size() == scene.textures.size()) {
        return;
    }
    // TEMPORARY (ui-responsiveness phase 2): same shape as the mesh probe above.
    const probe2::Add probeTexUpload(probe2::frame().textureUploadMs);
    probe2::frame().texturesUploaded += scene.textures.size();
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
    textureScene_ = &scene;
    textureIdentity_ = scene.identity;
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

// `aabbInsideFrustum` moved to rendering/shadow_math.hpp, where `casterState` needs the same test.
// Two copies of a six-line frustum test in two files is how a diagnostic and the pass it describes
// come to disagree about one entity, which is the whole subject of the Shadow Lab's §37 note.

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
                     shadowQuality, scene.environment.shadowRange);
    stats_.shadows = shadows_->stats();
    stats_.shadows.shadowMs = shadowMs;

    for (std::uint32_t i = 0; i < total; ++i) {
        bool cascaded = false;
        bool cube = false;
        const int shadowView = shadows_->viewForLight(i, cascaded, cube);
        lightStaging_[i] = packLight(*lightOrder_[i], shadowView, cascaded, cube);
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
    stats_.shadedLights = total;
    stats_.directionalLights = directional;
    stats_.haveClusters = false;
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
    // ADR-114: the grid's occupancy, from the same inputs the compute pass is about to be given.
    // Reading the cluster buffer back instead would stall the frame being measured, which is the
    // one frame a performance run must not stall -- so this is a CPU replica of the pass, using
    // the reference implementation that tests/rendering/test_shadows_gpu.cpp already checks the
    // pass's own output against index for index. It is off by default: it is real CPU work inside
    // a measured frame, and a diagnostic that silently taxes the measurement is worse than none.
    if (clusterStats_) {
        // Clamped to the uniform's own array: the encode above fills only that much, and a
        // count read past it would describe lights the pass was never handed.
        const std::uint32_t localCount = std::min(total - directional, kMaxSceneLights);
        std::vector<glm::vec3> viewPositions;
        std::vector<float> radii;
        viewPositions.reserve(localCount);
        radii.reserve(localCount);
        for (std::uint32_t i = 0; i < localCount; ++i) {
            viewPositions.emplace_back(params.lights[i]);
            radii.push_back(params.lights[i].w);
        }
        stats_.clusters = clusterOccupancy(grid, viewPositions, radii, kMaxLightsPerCluster);
        stats_.haveClusters = true;
    }
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

// ADR-128. The object cap used to be `kMaxObjects = 256`, a number that fell out of allocating one
// 128 KB uniform buffer at init() and never touching it again -- and Glowmere already flattens to
// 278 entities, so the only thing holding the scene together was that the camera never framed all
// of it. The fix is not a bigger number; it is that the allocation is a per-frame decision. The
// ABI is deliberately unchanged: still one uniform buffer of 512-byte slots addressed by dynamic
// offset, so every shader, every pipeline layout and every CPU/WGSL layout guard reads exactly as
// it did. What changed is that the buffer's *size* stopped being a compile-time constant.
//
// Growth is doubling with a floor, so a scene that walks its entity count up one at a time does not
// reallocate once per entity; and it never shrinks, because a camera turn that drops the count is
// about to raise it again.
bool SceneRenderer::drawable(const scene::Entity& entity) const {
    return entity.visible && entity.mesh < meshes_.size() && meshes_[entity.mesh].indexCount != 0;
}

void SceneRenderer::ensureObjectCapacity(std::uint32_t objects) {
    if (objects > kMaxObjectCapacity) {
        // The one remaining limit, and it is a byte budget rather than a slot count. Logged at the
        // moment of clamping with both numbers, because the failure this replaces -- entities that
        // silently stop drawing -- is precisely the one nobody could diagnose.
        log::warn("scene wants {} object slots; the object-uniform budget ({} MiB) allows {}",
                  objects, kObjectBufferByteBudget / (1024 * 1024), kMaxObjectCapacity);
        objects = kMaxObjectCapacity;
    }
    if (objectUniforms_ && objects <= objectCapacity_) {
        return;
    }
    std::uint32_t capacity = std::max(objectCapacity_, kInitialObjects);
    while (capacity < objects) {
        capacity = capacity <= kMaxObjectCapacity / 2 ? capacity * 2 : kMaxObjectCapacity;
    }
    if (objectUniforms_ && capacity == objectCapacity_) {
        return;
    }
    objectCapacity_ = capacity;
    objectStaging_.assign(static_cast<std::size_t>(capacity) * kObjectStride, 0);

    const auto& device = context_.device();
    wgpu::BufferDescriptor desc{};
    desc.label = "object-uniforms";
    desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    desc.size = static_cast<std::uint64_t>(capacity) * kObjectStride;
    objectUniforms_ = device.CreateBuffer(&desc);

    // The binding stays `sizeof(ObjectUniforms)` wide whatever the buffer's size: a dynamic offset
    // is added to the bound range, so a whole-buffer binding would run off the end on the first
    // non-zero offset. This is the same reasoning skinning.cpp records for its palette slices.
    wgpu::BindGroupEntry entry{};
    entry.binding = 0;
    entry.buffer = objectUniforms_;
    entry.size = sizeof(ObjectUniforms);
    wgpu::BindGroupDescriptor groupDesc{};
    groupDesc.label = "object-bind-group";
    groupDesc.layout = objectLayout_;
    groupDesc.entryCount = 1;
    groupDesc.entries = &entry;
    objectBindGroup_ = device.CreateBindGroup(&groupDesc);

    // The skinned path binds the same slots from the same buffer (ADR-086), so its group 1 names
    // this buffer too and goes stale the instant it is replaced. A skinned character rendering from
    // a freed buffer is the exact bug this hand-off exists to prevent.
    if (skinning_) {
        skinning_->setObjectBuffer(objectUniforms_, sizeof(ObjectUniforms));
    }
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
    const bool sceneChanged = temporalScene_ != &scene;
    if (sceneChanged) {
        resetTemporalHistory();
        temporalScene_ = &scene;
    }
    if (sceneChanged || time.renderTime < previousRenderTime_) {
        // A seek/reverse is a discontinuity, not motion. Reusing forward temporal history would
        // create false object/camera velocities and make the first reversed frame differ from a
        // fresh renderer at the same timeline second.
        havePrevViewProj_ = false;
        prevModels_.clear();
        prevModelsNext_.clear();
    }
    // ADR-360: the wind deformation is a function of time, so the velocity target needs the time
    // the PREVIOUS frame was at, and `previousRenderTime_` is about to stop being that. Captured
    // rather than read later: the object fill runs several hundred lines below this line, and
    // reading `previousRenderTime_` there quietly gave every swaying vertex a zero velocity --
    // correct-looking, silently wrong, and exactly the class of defect a motion-blur pass hides.
    // On a discontinuity the previous frame is not a previous frame, so the velocity is zero by
    // construction, which is what a seek should produce.
    windPrevTime_ = (havePrevViewProj_ && std::isfinite(previousRenderTime_))
                        ? static_cast<float>(previousRenderTime_)
                        : static_cast<float>(time.renderTime);
    previousRenderTime_ = time.renderTime;
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
    // ADR-128: size the object buffer for this frame before anything binds it. The bound is the
    // number of *drawable* entities, which is the most slots the two entity loops below can
    // between them consume -- each entity is offered a slot at most once across both. It is
    // computed after uploadMeshes() because `drawable` asks whether the mesh reached the GPU.
    {
        std::uint32_t drawableEntities = 0;
        for (const auto& entity : scene.entities) {
            drawableEntities += drawable(entity) ? 1u : 0u;
        }
        ensureObjectCapacity(drawableEntities);
    }
    uploadTextures(scene);
    // ADR-086: this frame's joint palettes. The renderer never *poses* anything -- the scene
    // arrives already posed by scene::updateRigs -- it only moves matrices the scene computed.
    skinning_->resetFrameStats();
    // Bind pose when animation is isolated out: the palettes are simply not uploaded, so every
    // skinned mesh draws from its rest vertices. Not "freeze the clip", which would still be a pose
    // and still be animation -- this is the arm that removes skinning from the frame.
    if (toggles_.animation) {
        // ...and `animationMotion` is the other arm: the palettes stay as they are, so a character
        // stops moving in place. The two together are what separate "the character's pose is wrong"
        // from "the character's pose is not changing".
        skinning_->setFrozen(!toggles_.animationMotion);
        skinning_->update(scene);
    }
    {
        const probe2::Add probeEnv(probe2::frame().environmentMs); // TEMPORARY: phase 2
        updateEnvironment(scene);
    }
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
    glm::mat4 view = scene.camera.view();
    glm::mat4 proj = scene.camera.projection(aspect);
    // A frozen camera keeps the matrices it had while the world goes on moving. Everything
    // downstream -- culling, shadows, the object uniforms, the diagnostics -- reads these two, so
    // the freeze reaches all of them from one place rather than each having to know about it.
    if (!toggles_.cameraMotion) {
        if (!haveFrozenCamera_) {
            frozenView_ = view;
            frozenProjection_ = proj;
            frozenCameraPosition_ = scene.camera.position;
            haveFrozenCamera_ = true;
        }
        view = frozenView_;
        proj = frozenProjection_;
    }
    if (!finiteMatrix(view) || !finiteMatrix(proj)) {
        return fail("scene render: camera produced a non-finite view or projection matrix");
    }
    diagnosticFrame_ = RendererDiagnosticFrame{};
    diagnosticFrame_.frameIndex = time.frameIndex;
    diagnosticFrame_.cameraPosition = scene.camera.position;
    diagnosticFrame_.view = view;
    diagnosticFrame_.projection = proj;
    diagnosticFrame_.viewProjection = proj * view;
    diagnosticFrame_.nearPlane = scene.camera.nearPlane;
    diagnosticFrame_.farPlane = scene.camera.farPlane;
    diagnosticFrame_.aspect = aspect;
    diagnosticFrame_.fovY = scene.camera.effectiveFovY();
    diagnosticFrame_.viewportWidth = hdr_.width();
    diagnosticFrame_.viewportHeight = hdr_.height();
    const FrustumPlanes diagnosticPlanes = frustumPlanes(diagnosticFrame_.viewProjection);
    diagnosticFrame_.objects.reserve(scene.entities.size());
    for (const scene::Entity& entity : scene.entities) {
        const glm::mat4 model = entity.transform.matrix();
        if (!finiteMatrix(model)) {
            return fail("scene render: entity '{}' produced a non-finite model matrix", entity.name);
        }
        RenderObjectDiagnostic diagnostic;
        diagnostic.name = entity.name;
        diagnostic.entityIndex = diagnosticFrame_.objects.size();
        diagnostic.worldPosition = entity.transform.position;
        diagnostic.mesh = entity.mesh;
        // A fingerprint of the surface, not the surface. Enough to answer "is this the same
        // material" in a capture comparison, which is the question a diff asks; the fields
        // themselves live in the scene.
        {
            std::uint64_t h = 1469598103934665603ull;
            const auto mix = [&h](const void* data, std::size_t size) {
                const auto* bytes = static_cast<const std::uint8_t*>(data);
                for (std::size_t i = 0; i < size; ++i) {
                    h ^= bytes[i];
                    h *= 1099511628211ull;
                }
            };
            const scene::Material& m = entity.material;
            mix(&m.baseColor, sizeof(m.baseColor));
            mix(&m.emissiveColor, sizeof(m.emissiveColor));
            mix(&m.emissiveIntensity, sizeof(m.emissiveIntensity));
            mix(&m.roughness, sizeof(m.roughness));
            mix(&m.metallic, sizeof(m.metallic));
            mix(&m.opacity, sizeof(m.opacity));
            mix(&m.alphaMode, sizeof(m.alphaMode));
            mix(&m.alphaCutoff, sizeof(m.alphaCutoff));
            mix(m.program.data(), m.program.size());
            diagnostic.materialHash = h;
        }
        diagnostic.worldMatrix = model;
        if (entity.rig != scene::kInvalidRig && entity.rig < scene.rigs.size()) {
            const scene::SkinnedRig& rig = scene.rigs[entity.rig];
            diagnostic.rigIndex = entity.rig;
            diagnostic.jointCount = static_cast<std::uint32_t>(rig.palette.size());
            diagnostic.paletteVersion = rig.paletteVersion;
            diagnostic.paletteTime = rig.paletteTime;
        }
        diagnostic.visible = entity.visible;
        diagnostic.cameraCulled = entity.cameraCulled;
        diagnostic.finite = finiteMatrix(model);
        if (entity.mesh >= scene.meshes.size()) {
            diagnostic.cullReason = "invalid-mesh";
        } else {
            // The box the cull actually used, asked of the function that built it -- not a second
            // transform of the bind-pose bounds written out here.
            //
            // It was the latter, and it disagreed twice over: `entityCullBounds` pads by a quarter
            // of the extent plus a quarter of a metre, and for a skinned entity it uses the *posed*
            // palette, while this rebuilt an unpadded box from the bind pose. A T-pose bind is wider
            // than the poses it animates into, so the two boxes are not even reliably ordered.
            // Swept over 72 cameras, all 72 margins differed, worst by 2.03 m, and 48 disagreed
            // about whether the object was inside the frustum at all. A margin is the answer to
            // "how close was this to being culled", and it was answering it about a box nothing
            // culled against (§37: the renderer is the source of truth, so the diagnostic was
            // wrong).
            // Two boxes, because two questions are being asked and they are not the same one.
            //
            // `worldBounds` is what this object's bounds *are*, and somebody reading it wants the
            // object -- so it is asked for with no padding. It still comes from `entityCullBounds`
            // rather than from a transform of the mesh's corners written out here, because that
            // function also uses the *posed* palette for a skinned entity, and a T-pose bind box is
            // not the box the thing occupies.
            //
            // `frustumMargins` answers "how close was this to being culled", so it must use the box
            // the cull actually tested -- padded by a quarter of the extent plus a quarter of a
            // metre. Measured over 72 cameras when this was one box: all 72 margins differed, worst
            // by 2.03 m, and 48 disagreed about whether the object was inside the frustum at all.
            //
            // Answering both from the padded box is the mistake this comment exists to prevent: it
            // makes the reported bounds 0.25 m larger than the object in every direction, which a
            // GPU test caught immediately and a person reading the panel would have believed.
            const scene::CullBounds trueBounds = scene::entityCullBounds(scene, entity, 0.0f, 0.0f);
            diagnostic.worldBoundsMin = trueBounds.min;
            diagnostic.worldBoundsMax = trueBounds.max;
            const scene::CullBounds culled = scene::entityCullBounds(scene, entity);
            for (std::size_t plane = 0; plane < diagnosticPlanes.size(); ++plane) {
                const glm::vec4& p = diagnosticPlanes[plane];
                const glm::vec3 far(p.x >= 0.0f ? culled.max.x : culled.min.x,
                                    p.y >= 0.0f ? culled.max.y : culled.min.y,
                                    p.z >= 0.0f ? culled.max.z : culled.min.z);
                diagnostic.frustumMargins[plane] = glm::dot(glm::vec3(p), far) + p.w;
            }
            diagnostic.cullReason = !entity.visible ? "hidden" : entity.cameraCulled ? "camera-frustum" : "eligible";
        }
        diagnosticFrame_.objects.push_back(std::move(diagnostic));
    }

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
    // The same answer the sky cube was built from, so the disc drawn over the sky lands on the one
    // inside it. `resolveSky` is pure and cheap, and asking it again is better than keeping a second
    // rule about which light the moon follows -- two rules is how they came to disagree.
    {
        const scene::SkyRuntime resolved = scene::resolveSky(scene.environment.sky, scene.lights);
        frame.skySun = glm::vec4(resolved.sunDirection, skyIbl ? 1.0f : 0.0f);
        // ADR-345: the same resolved sky, handed to the background pass as parameters rather than
        // as a cube. `resolveSky` is already being called here, so this costs nothing and cannot
        // disagree with the disc drawn above it.
        frame.skyZenithColor = glm::vec4(resolved.zenithColor, resolved.hazeWidth);
        frame.skyHorizonColor = glm::vec4(resolved.horizonColor, resolved.sunAngularRadius);
        frame.skyGroundColor = glm::vec4(resolved.groundColor, resolved.sunGlowWidth);
        // The flag only means anything when a map is the IBL: with the procedural sky already the
        // IBL there is nothing to decouple, and the existing cube path is the better one because
        // it is what the lighting was built from.
        const bool analyticBackground =
            scene::skyBackgroundFor(scene.environment, ibl, skyIbl) == scene::SkyBackground::Analytic;
        frame.skySunRadiance = glm::vec4(resolved.sunColor, analyticBackground ? 1.0f : 0.0f);
    // ADR-379: the vortex's own light on what floats above it. Zero intensity when there is no
    // vortex, which is the gate the surface shader tests.
    if (scene.atmospherics.hasVortex && scene.atmospherics.vortex.active()) {
        const world::Vortex& vx = scene.atmospherics.vortex;
        frame.vortexGlow = glm::vec4(vx.center, std::max(vx.radius, 1.0f));
        // The colour the eye reads out of the funnel is the mid tone lifted toward the accent.
        //
        // The strength is `spill` ALONE and is deliberately not derived from `emission`. My first
        // version multiplied the two, and that is a units error: `emission` is an emissive density
        // PER METRE integrated along a march, while this is a surface irradiance. Scaling one by
        // the other gave 0.24 where about 2.5 was wanted, and the island brightened by 0.09 of 255
        // -- invisible, and it took an A/B with the vortex on in both arms to see that, because
        // switching the vortex off to get a baseline moves the whole background and swamps it.
        const glm::vec3 tint = glm::mix(vx.colorMid, vx.colorAccent, 0.45f);
        frame.vortexGlowColor = glm::vec4(tint, std::max(vx.spill, 0.0f));
    } else {
        frame.vortexGlow = glm::vec4(0.0f);
        frame.vortexGlowColor = glm::vec4(0.0f);
    }
    }
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
    // ADR-133: the material tier table, as the shader reads it. A budget of kUnlimitedLocalLights
    // is written as the froxel cap rather than as 4 billion, so the float lane stays exact.
    const auto tierBudgetLane = [](std::uint32_t budget) {
        return static_cast<float>(std::min<std::uint32_t>(budget, kMaxLightsPerCluster));
    };
    frame.materialTier =
        glm::vec4(static_cast<float>(static_cast<std::uint8_t>(qualitySettings_.forcedMaterialTier)),
                  tierBudgetLane(qualitySettings_.reducedTierLocalLights),
                  tierBudgetLane(qualitySettings_.flatTierLocalLights),
                  qualitySettings_.proceduralMaterialTier < 0
                      ? -1.0f
                      : static_cast<float>(qualitySettings_.proceduralMaterialTier));
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
                    time.frameNonce(),
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
    // ADR-207: the world effects, already resolved and packed by whoever owns the scene. The
    // renderer copies them and never resolves them -- a source is a hero name or the camera's
    // trajectory, and a renderer that knew about either would be a renderer that has to be given
    // the director.
    if (toggles_.worldEffects) {
        const std::uint32_t effects =
            std::min<std::uint32_t>(scene.worldEffects.count, world::kMaxGpuWorldEffects);
        frame.worldEffectCount = glm::vec4(static_cast<float>(effects), 0.0f, 0.0f, 0.0f);
        for (std::uint32_t i = 0; i < effects; ++i) {
            frame.worldEffects[i] = scene.worldEffects.effects[i];
        }
        stats_.worldEffects = effects;
    } else {
        stats_.worldEffects = 0;
    }
    // ADR-230: the atmospheric effects, on the same terms. `drawAtmosphere_` is read again at the
    // draw site, so the toggle removes the fragment work and the uniform content together -- an arm
    // that zeroed the counts but still ran a fullscreen draw would be measuring the wrong thing.
    drawAtmosphere_ = false;
    if (toggles_.atmospherics && scene.atmospherics.any()) {
        const auto& atmos = scene.atmospherics;
        const std::uint32_t comets = std::min<std::uint32_t>(atmos.cometCount, world::kMaxGpuComets);
        const std::uint32_t auroras = std::min<std::uint32_t>(atmos.auroraCount, world::kMaxGpuAuroras);
        frame.atmosCount = glm::vec4(static_cast<float>(comets), static_cast<float>(auroras),
                                     static_cast<float>(std::max<std::uint32_t>(atmos.cometSteps, 4)), 0.0f);
        for (std::uint32_t i = 0; i < comets; ++i) {
            frame.comets[i] = atmos.comets[i];
        }
        for (std::uint32_t i = 0; i < auroras; ++i) {
            frame.auroras[i] = atmos.auroras[i];
        }
        frame.skyGround = atmos.ground;
        stats_.comets = comets;
        stats_.auroras = auroras;
        drawAtmosphere_ = true;
    } else {
        frame.atmosCount = glm::vec4(0.0f);
        frame.skyGround = world::SkyGroundGpu{};
        stats_.comets = 0;
        stats_.auroras = 0;
    }
    // ADR-390: the Cosmic Ocean, gated on its OWN predicate and not on `any()`.
    //
    // This line is what makes the effect reachable at all, and it is the one to check if the sky
    // comes up empty -- `CosmicOceanRenderer::update` had exactly one caller before it, a GPU test,
    // which is what "built, tested and unreachable" looks like from the inside. It is deliberately
    // not folded into the `any()` branch above: `any()` gates the fullscreen *atmosphere* draw, and
    // the ocean is a separate pipeline with a separate `Draw(3)`, so an ocean-only scene must not
    // switch the atmosphere draw on and an atmosphere-only scene must not upload an ocean block.
    // `AtmosphericFrame::any()` carries the same note at its declaration.
    //
    // Packing happens here rather than in `buildAtmosphericFrame` because the quality tier is the
    // renderer's to know: `cosmicOctaveScale` and `cosmicSampleScale` are a rendering decision and
    // the world has no business carrying them.
    {
        const auto& atmos = scene.atmospherics;
        const bool live = toggles_.cosmicOcean && atmos.anyCosmicOcean();
        const world::CosmicQualityScale quality{qualitySettings_.cosmicOctaveScale,
                                                qualitySettings_.cosmicSampleScale};
        const world::CosmicOceanGpu block =
            live ? world::packCosmicOcean(atmos.cosmicOcean, atmos.cosmicOceanEnvelope,
                                          time.renderTime, quality)
                 : world::CosmicOceanGpu{};
        cosmicOcean_->update(block, live, 0);
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
        // ADR-099: which of the scene's water surfaces this entity draws with, resolved once here
        // from the material-program name rather than looked up per draw.
        std::uint32_t water = 0;
        // ADR-351: which geometry this item draws. `meshes_[entity->mesh]` unless the selector
        // chose a rung, and a pointer rather than a level index so that every draw site reads one
        // field and none of them can look the choice up a second time and get a different answer.
        const GpuMesh* geometry = nullptr;
        std::uint8_t lodLevel = 0;
        [[nodiscard]] bool skinned() const { return skin.valid(); }
    };
    std::vector<DrawItem> opaque;
    std::vector<DrawItem> grid;
    std::vector<DrawItem> blended;
    std::vector<DrawItem> water;
    // Model history belongs to the scene entity, not to whether this frame happened to submit a
    // camera or shadow draw. Advancing it for every entity prevents a moving object from carrying
    // a stale transform through several culled frames and producing a false re-entry velocity.
    prevModelsNext_.clear();
    for (const scene::Entity& entity : scene.entities) {
        prevModelsNext_.insert_or_assign(entity.name, entity.transform.matrix());
    }
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
    // The bounds and the second cull both moved to rendering/shadow_math.hpp, and they moved for
    // the Shadow Lab's reason rather than for tidiness: these three lambdas *were* the caster rule,
    // and a rule that lives inside a 1,450-line render function cannot be asked a question by
    // anything else. An overlay that wants to colour an entity by whether this frame made it a
    // caster had the choice of reaching into the renderer or writing the rule again, and a rule
    // written twice is one that disagrees with itself the first time somebody edits a copy (§37).
    // Now the renderer and the overlay call the same function, and it needs no device.
    const auto entityWorldBounds = [&](const scene::Entity& entity) {
        return rendering::entityWorldBounds(entity, scene.meshBounds(entity.mesh));
    };
    const std::span<const FrustumPlanes> viewPlanes(cascadePlanes.data(),
                                                    std::min(cascadePlanes.size(), shadowViewList.size()));
    // ---- ADR-351: which rung each entity draws this frame ---------------------------------------
    //
    // The selector and the importance evaluator already existed (ADR-122 / ADR-123 / ADR-124) and
    // were consumed by nothing: this is the wiring, not a second system. Three properties are worth
    // stating because each is what keeps an existing scene's frame byte-identical.
    //
    //   * The whole block is skipped unless some mesh in this scene carries a chain, and a scene
    //     only carries one if a node asked. Nothing that does not opt in reaches the selector.
    //   * The kind bands -- proxy, impostor, cull -- are switched off, because those
    //     representations are declared and not built. Without that, an entity a hundred metres away
    //     with a chain would be selected into a billboard that does not exist and would stop being
    //     drawn. The selector can therefore only return LOD0 or a rung.
    //   * `forceTopRepresentation` comes from the tier and from the scene's DetailLimits. The
    //     offline tier sets it, which is §5.9's promise that a render never inherits a realtime
    //     compromise, and `--render-limits unlimited` sets it too, because that is what the flag
    //     already means for every other ladder in the engine.
    const ViewContext lodView = [&] {
        // `fromCamera` and not a second copy of its arithmetic: `pixelsPerUnit` is the same constant
        // shaders/cull.wgsl packs into cameraPos.w, and two subsystems disagreeing about how big
        // something is on screen is a defect that shows up as a seam between a scatter and the
        // hand-placed copy of the same asset (ADR-122's own warning).
        ViewContext v = ViewContext::fromCamera(scene.camera, hdr_.width(), hdr_.height());
        // A frozen view chooses rungs from where the camera was told to stay, not from where it is.
        // Everything else downstream reads `view` and `proj`; a selector reading scene.camera would
        // be the one subsystem disagreeing about where the frame is drawn from.
        if (!toggles_.cameraMotion) {
            v.cameraPosition = frozenCameraPosition_;
            v.viewProjection = proj * view;
        }
        return v;
    }();
    RepresentationPolicy lodPolicy = RepresentationPolicy::forTier(tier_);
    // Negative rather than zero: a projected radius is never negative, and an entity behind the
    // camera has a radius of exactly zero, which a threshold of zero would read as "small enough to
    // cull". An off-screen shadow caster demoted to a representation that does not exist is a
    // shadow that disappears, and it would have been reached by a comparison that looks disabled.
    lodPolicy.proxyRadius = -1.0f;
    lodPolicy.impostorRadius = -1.0f;
    lodPolicy.cullRadius = -1.0f;
    if (!scene.detailLimits.proceduralLodRungs) {
        lodPolicy.forceTopRepresentation = true;
    }
    entityLod_ = EntityLodStats{};
    // The selector remembers last frame's choice per *entity index*, so a renumbered entity list
    // would have it reading one object's history against another -- the derived-copy defect this
    // engine has found nine of. `uploadMeshes` resets it when the geometry changes, which covers
    // most rebuilds; this covers the rest, because a flatten can reorder entities without touching
    // a mesh. Cheap, and it fails towards forgetting rather than towards remembering wrongly.
    if (scene.identity != lodSceneIdentity_ || scene.entities.size() != lodEntityCount_) {
        representation_.reset();
        lodSceneIdentity_ = scene.identity;
        lodEntityCount_ = scene.entities.size();
    }
    // Returns the geometry to draw for one entity, and records what it decided.
    const auto chooseGeometry = [&](const scene::Entity& entity,
                                    std::size_t thisEntity) -> const GpuMesh* {
        const GpuMesh* source = &meshes_[entity.mesh];
        if (!anyMeshLod_ || entity.mesh >= meshLods_.size()) {
            return source;
        }
        const GpuMeshLod& chain = meshLods_[entity.mesh];
        if (chain.levels.empty()) {
            return source;
        }
        ++entityLod_.drawables;
        const auto& [lo, hi] = scene.meshBounds(entity.mesh);
        const glm::mat4 model = entity.transform.matrix();
        const glm::vec3 centre = glm::vec3(model * glm::vec4((lo + hi) * 0.5f, 1.0f));
        // The world-space radius of the mesh's bounding sphere under this instance's transform.
        // Taken from the transformed corners rather than from `length(hi - lo) * 0.5 * scale`,
        // because a non-uniform scale makes those two different numbers and the corners are the
        // one that is right.
        float radius = 0.0f;
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 p{(corner & 1) != 0 ? hi.x : lo.x, (corner & 2) != 0 ? hi.y : lo.y,
                              (corner & 4) != 0 ? hi.z : lo.z};
            radius = std::max(radius, glm::length(glm::vec3(model * glm::vec4(p, 1.0f)) - centre));
        }
        const float areaScale = scene::MeshMetrics::areaScale(entity.transform.scale);
        ImportanceInput in;
        in.center = centre;
        in.radius = radius;
        in.surfaceArea = chain.rungs.front().surfaceArea * areaScale;
        in.triangles = chain.rungs.front().triangles;
        const ImportanceRecord record =
            ImportanceEvaluator::evaluate(lodView, in, static_cast<std::uint32_t>(thisEntity));
        // The rungs, at this instance's scale. Rebuilt per entity because two instances of one mesh
        // at different scales cost differently, which is the whole of what px/triangle measures.
        std::vector<LodRung> rungs = chain.rungs;
        // The largest axis scale for the error, not the area scale: a deviation is a distance and
        // it grows by whichever axis stretches it most. Taking the smaller number would understate
        // the error of a non-uniformly scaled instance, and understating the error is the one
        // direction this must never fail in.
        const float lengthScale = std::max({std::abs(entity.transform.scale.x),
                                            std::abs(entity.transform.scale.y),
                                            std::abs(entity.transform.scale.z)});
        for (LodRung& rung : rungs) {
            rung.surfaceArea *= areaScale;
            rung.error *= lengthScale;
        }
        RepresentationPolicy policy = lodPolicy;
        policy.hysteresis = chain.hysteresis;
        if (chain.maxScreenError >= 0.0f) {
            policy.maxScreenError = chain.maxScreenError;
        }
        const RepresentationChoice choice = representation_.select(record, rungs, policy);
        // The kinds that are not built map to the coarsest rung rather than to nothing. They cannot
        // be reached with the radii zeroed above; this is the belt to that brace, and it fails
        // towards drawing the object.
        std::size_t level = choice.kind == Representation::FullMesh ? 0
                            : choice.kind == Representation::MeshLod ? choice.lodLevel
                                                                     : chain.levels.size();
        level = std::min(level, chain.levels.size());
        entityLod_.changed += choice.changed ? 1u : 0u;
        entityLod_.held += choice.held ? 1u : 0u;
        entityLod_.sourceTriangles += chain.rungs.front().triangles;
        entityLod_.drawnTriangles += chain.rungs[level].triangles;
        if (level < entityLod_.rungCounts.size()) {
            ++entityLod_.rungCounts[level];
        }
        if (level == 0) {
            return source;
        }
        ++entityLod_.demoted;
        return &chain.levels[level - 1];
    };

    std::uint32_t objectIndex = 0;
    // Writes one 256-byte object slot and returns the draw item, or nothing when the budget is
    // spent. Shared by the camera pass and the shadow-only pass so the two cannot describe the
    // same entity differently.
    const auto makeItem = [&](const scene::Entity& entity, std::size_t thisEntity,
                              bool shadowOnly = false) -> std::optional<DrawItem> {
        // A backstop, not the budget: ensureObjectCapacity() above sized the buffer for every
        // drawable entity in the scene, so this only fires if that arithmetic and this loop
        // disagree -- and then it drops a draw rather than writing past the staging mirror.
        if (objectIndex >= objectCapacity_) {
            return std::nullopt;
        }
        const scene::Material* material = &entity.material;
        scene::Material substitute;
        if (const MaterialCheck check = checkMaterial(entity.material); !check.ok) {
            // Named, with the value, and once per entity per frame rather than per draw: a guard
            // that floods the log is a guard people turn off.
            log::warn("scene render: entity '{}' has a non-finite material ({} = {}); drawing it in "
                      "magenta instead",
                      entity.name, check.field, check.value);
            substitute.baseColor = glm::vec3(1.0f, 0.0f, 1.0f);
            substitute.roughness = 1.0f;
            substitute.metallic = 0.0f;
            substitute.alphaMode = scene::AlphaMode::Opaque;
            material = &substitute;
        }
        const auto& m = *material;
        ObjectUniforms obj{};
        obj.model = entity.transform.matrix();
        obj.normalMatrix = glm::transpose(glm::inverse(obj.model));
        // Velocity needs last frame's matrix; keyed by name so reordering entities cannot make an
        // object inherit another's motion (ADR-035).
        {
            const auto previous = prevModels_.find(entity.name);
            obj.prevModel = previous != prevModels_.end() ? previous->second : obj.model;
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
        if (toggles_.animation && entity.rig != scene::kInvalidRig && entity.mesh < meshes_.size() &&
            meshes_[entity.mesh].skin) {
            skin = skinning_->slice(entity.rig);
        }
        // x = the ADR-030 `objectId` input; y = material id and z = bloom weight feed the
        // identifier and emission targets (ADR-035); w = the joint count, which is how the skinned
        // vertex stage finds the previous frame's half of its palette slice.
        // Tagged as an entity id, so the picker knows which numbering to resolve it against; the
        // material programs' `objectId` input is the untagged index and the shader strips it back.
        obj.ids = glm::vec4(
            static_cast<float>(scene::packPickId(scene::PickSpace::Entity, thisEntity)),
            static_cast<float>(thisEntity + 1), 1.0f,
                            static_cast<float>(skin.jointCount));
        // ADR-360: the wind body, if this mesh is part of one. Everything here is the body's, not
        // the mesh's, so five meshes of one tree hand the shader identical numbers and cannot come
        // apart at the joints between them. Off (strength 0) leaves all three lanes zero and the
        // vertex stage returns before it reads them.
        if (entity.wind.active()) {
            const scene::Entity::WindBody& w = entity.wind;
            obj.windOrigin = glm::vec4(w.origin, 1.0f / std::max(w.height, 1e-3f));
            obj.windShape = glm::vec4(1.0f / std::max(w.radius, 1e-3f), w.strength, w.branch, w.foliage);
            obj.windTune = glm::vec4(w.trunk, w.flutter, w.lag, windPrevTime_);
        }
        // ADR-376: the energy shares the wind body's frame, so it is only meaningful when that
        // frame exists -- an energy pulse needs to know where the root and the crown are.
        if (entity.energy.active() && entity.wind.height > 0.0f) {
            const scene::Entity::TreeEnergy& e = entity.energy;
            if (!entity.wind.active()) {
                // The body's frame without its motion: a tree can conduct while standing still.
                obj.windOrigin = glm::vec4(entity.wind.origin, 1.0f / std::max(entity.wind.height, 1e-3f));
                obj.windShape.x = 1.0f / std::max(entity.wind.radius, 1e-3f);
            }
            obj.energy0 = glm::vec4(e.intensity, e.pulseSpeed, std::max(e.pulseWidth, 1e-3f), e.propagation);
            obj.energy1 = glm::vec4(e.root, e.trunk, e.branch, e.canopy);
            obj.energy2 = glm::vec4(e.noiseAmount, e.noiseScale, e.noiseSpeed, e.bloom);
            obj.energy3 = glm::vec4(e.shimmer, e.shimmerSpeed, e.shimmerScale, e.shimmerVariation);
            obj.energyA = glm::vec4(e.colorNear, 0.0f);
            obj.energyB = glm::vec4(e.colorFar, 0.0f);
        }
        const std::uint32_t offset = objectIndex * kObjectStride;
        std::memcpy(objectStaging_.data() + offset, &obj, sizeof(obj));
        const float depth = -(view * glm::vec4(entity.transform.position, 1.0f)).z;
        RenderObjectDiagnostic& diagnostic = diagnosticFrame_.objects[thisEntity];
        diagnostic.objectSlot = objectIndex;
        diagnostic.bufferOffset = offset;
        diagnostic.submitted = true;
        diagnostic.cullReason = "submitted";
        ++objectIndex;
        DrawItem out{offset, &entity, depth, skin};
        // A skinned mesh never has a chain (the builder declines them: every level re-orders the
        // vertex buffer and MeshData::skin is parallel to the one it came from), so this asks only
        // for static geometry and the skinned path is untouched.
        // A skinned mesh never has a chain, and an entity the camera cannot see is drawing only
        // into a shadow map: its projected radius is measured against a frustum it is not in, so
        // there is no screen size to choose a rung by and LOD0 is the only honest answer.
        out.geometry = (skin.valid() || shadowOnly) ? &meshes_[entity.mesh]
                                                    : chooseGeometry(entity, thisEntity);
        out.lodLevel = 0;
        if (out.geometry != &meshes_[entity.mesh] && entity.mesh < meshLods_.size()) {
            const GpuMeshLod& chain = meshLods_[entity.mesh];
            for (std::size_t k = 0; k < chain.levels.size(); ++k) {
                if (&chain.levels[k] == out.geometry) {
                    out.lodLevel = static_cast<std::uint8_t>(k + 1);
                    break;
                }
            }
        }
        return out;
    };
    std::size_t entityIndex = 0;
    for (const auto& entity : scene.entities) {
        const std::size_t thisEntity = entityIndex++;
        // A frozen view cannot trust a cull decided against a camera that has since moved: the
        // verdict on the entity is about a frustum this frame is not drawing. So freezing the view
        // ignores it, which is what makes the freeze self-consistent rather than a view held over a
        // set of objects chosen for somewhere else.
        const bool trustCull = toggles_.culling && toggles_.cameraMotion;
        if (!drawable(entity) || (entity.cameraCulled && trustCull)) {
            continue;
        }
        const auto item = makeItem(entity, thisEntity);
        if (!item) {
            log::warn("more than {} visible entities; extra entities skipped", objectCapacity_);
            break;
        }
        if (entity.style == scene::MeshStyle::Grid) {
            grid.push_back(*item);
        } else if (entity.style == scene::MeshStyle::Water) {
            if (!toggles_.water) {
                continue;
            }
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
            if (!toggles_.transparency) {
                continue;
            }
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
        if (!entity.cameraCulled || !drawable(entity)) {
            continue;
        }
        const CasterState state = casterState(entity, scene.meshBounds(entity.mesh), viewPlanes);
        if (!casts(state)) {
            // Only the second cull's own rejections are counted here. An entity the scene already
            // said does not cast was never a candidate, and counting it as "culled by a cascade"
            // would inflate the one number that says whether the second cull is earning its keep.
            if (state == CasterState::OutsideEveryView) {
                ++stats_.shadows.entitiesCulled;
            }
            continue;
        }
        const auto item = makeItem(entity, thisEntity, /*shadowOnly=*/true);
        if (!item) {
            break; // the camera's own entities have the slots; nothing more to say about it
        }
        shadowCasters.push_back(*item);
    }
    stats_.shadowCasters = static_cast<std::uint32_t>(shadowCasters.size());
    hashDiagnosticFrame(diagnosticFrame_);
    if (!diagnosticEntity_.empty()) {
        if (const RenderObjectDiagnostic* current = diagnosticObject(diagnosticEntity_); current != nullptr) {
            const bool cameraChanged = !nearlyEqual(diagnosticFrame_.cameraPosition, previousDiagnosticFrame_.cameraPosition) ||
                                       !nearlyEqual(diagnosticFrame_.view, previousDiagnosticFrame_.view) ||
                                       !nearlyEqual(diagnosticFrame_.projection, previousDiagnosticFrame_.projection);
            const bool objectChanged = !previousDiagnosticObject_.has_value() ||
                                       !nearlyEqual(current->worldPosition, previousDiagnosticObject_->worldPosition) ||
                                       !nearlyEqual(current->worldMatrix, previousDiagnosticObject_->worldMatrix) ||
                                       current->cameraCulled != previousDiagnosticObject_->cameraCulled ||
                                       current->submitted != previousDiagnosticObject_->submitted ||
                                       current->objectSlot != previousDiagnosticObject_->objectSlot ||
                                       current->finite != previousDiagnosticObject_->finite;
            if (cameraChanged || objectChanged || !current->finite) {
                log::debug("renderer diagnostic '{}' frame {} world=({:.5f}, {:.5f}, {:.5f}) camera=({:.5f}, {:.5f}, {:.5f}) slot={} visible={} culled={} submitted={} finite={}",
                           current->name, time.frameIndex, current->worldPosition.x, current->worldPosition.y,
                           current->worldPosition.z, diagnosticFrame_.cameraPosition.x, diagnosticFrame_.cameraPosition.y,
                           diagnosticFrame_.cameraPosition.z, current->objectSlot, current->visible,
                           current->cameraCulled, current->submitted, current->finite);
            }
            previousDiagnosticObject_ = *current;
            previousDiagnosticFrame_ = diagnosticFrame_;
        }
    } else {
        previousDiagnosticObject_.reset();
    }
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
    tonemap.seed = static_cast<float>(time.frameNonce() % 1024);
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
    // ADR-099: water reads the prepass's linear depth to know how thick it is, which is what its
    // shoreline and its depth colour are made of. A scene with water in it therefore asks for the
    // prepass whatever else is on -- 0.20 ms of depth-only geometry, against a surface that
    // otherwise falls back to the vertex depth and to the mesh's own edge for its waterline.
    // ADR-255: an export-only mask pass needs the prepass exactly as a consumed one does -- it
    // reads the linear depth the prepass resolves -- so the question is whether a pass was
    // ENCODED, not whether the lit pass will read it. `active()` here would have produced a shadow
    // AOV computed against a depth target nobody filled.
    const bool needsDepthPrepass = ao_->active() || qualitySettings_.contactShadows ||
                                   shadowMask_->encoded() || !scene.waters.empty();
    // ---- water surfaces (ADR-099) ----
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
        // ADR-370: the same packed field the mesh vertex stage bends the tree with, so a leaf and
        // the branch it fell from are reading one description of the air.
        particleFrame.wind = wind::packWind(scene.environment.wind);
        particleFrame.spawnScale = std::max(qualitySettings_.particleSpawnScale, 0.0f); // ADR-382
        particleFrame.shutterSeconds = static_cast<float>(std::clamp(time.deltaTime, 0.0, 0.1)) *
                                       std::clamp(scene.camera.lens.shutterAngle, 0.0f, 360.0f) / 360.0f;
        // ADR-387: `enabled(scene)`, not `enabled(environment)`. The vortex used to live on the
        // environment and could therefore switch this march on by itself; moving it to the
        // atmospherics put it out of that overload's sight, and this call silently stopped filling
        // the particle fog coupling in the one scene whose `volumeDensity` is zero and whose
        // volume pass exists only because of the vortex. Worth 95% of the frame's pixels -- small
        // per-pixel deltas over every mote and leaf, which is what an unfilled coupling looks like.
        if (VolumeRenderer::enabled(scene)) {
            particleFrame.fogDensity = scene.environment.volumeDensity;
            particleFrame.fogHeight = scene.environment.fogHeight;
            particleFrame.fogHeightFalloff = scene.environment.fogHeightFalloff;
            particleFrame.fogAbsorption = scene.environment.volumeAbsorption;
            particleFrame.fogMaxDistance = scene.environment.volumeMaxDistance;
        }
        particleFrame.linearDepth = needsDepthPrepass ? linearDepth_.view : nullptr;
        particles_->setFrameContext(particleFrame);
    }
    // Off means no simulation either, not merely no draw: a particle system stepped but not drawn
    // would still advance its state, so switching it back on would show a system that had been
    // running invisibly rather than one that had been off.
    if (toggles_.particles) {
        particles_->update(encoder, scene, time, view, proj, fields_.get(), splines_.get());
        stats_.particles = particles_->stats();
    } else {
        // Nothing simulated and nothing drawn, so the frame reports none. Leaving the renderer's
        // own counters in place would report last frame's systems as this frame's, which is a
        // diagnostic saying the opposite of what happened.
        stats_.particles = {};
    }
    stage(cpu.particlesMs);

    // ---- procedural geometry (ADR-023): mesh/instance uploads, per-frame uniforms, effector pass ----
    // The scene places its procedurals itself in this phase: identity object matrices.
    {
        const std::vector<glm::mat4> identity(scene.procedurals.size(), glm::mat4(1.0f));
        // Culling and screen-size LOD need this frame's viewport (ADR-029).
        procedurals_->setViewport(hdr_.width(), hdr_.height());
        // ADR-146 / §5.9: the ladder's dead zone is a temporal shortcut, so the offline tier does
        // not get one. Applied per frame rather than at setQualitySettings, because the tier can
        // change between frames and the cull pass reads this on every one.
        procedurals_->setLodHysteresisAllowed(qualitySettings_.lodHysteresisAllowed);
        // ADR-155: per-rung material tier for the scatter. Distant rungs shade flat; the foreground
        // is rung 0 and is untouched.
        procedurals_->setFlatTierFromRung(qualitySettings_.materialTiers ? qualitySettings_.flatTierFromRung
                                                                        : -1);
        // ADR-287: the frame's shadow views, for the ecology's own caster list. The same plane sets
        // the entity second cull above is built from -- handed over rather than fitted again, so
        // the two caster rules cannot disagree about a volume (§37). An empty span (shadows off, or
        // no light that casts) is how the renderer says there is nothing to cast into, and the
        // ecology's shadow indirect draws are then not recorded at all.
        procedurals_->setShadowViews(viewPlanes);
        procedurals_->update(encoder, scene, identity, time, fields_.get(), splines_.get());
        stats_.procedural = procedurals_->stats();
    }
    stage(cpu.proceduralMs);

    // ---- SDF objects (ADR-027): node packing, mesh uploads, per-object uniforms ----
    // §16 / ADR-034: the shadow march's step budget, which the tier table has always set and
    // nothing has ever read. Per frame, for the same reason the cull ladder's hysteresis is.
    sdfs_->setSdfShadowSteps(qualitySettings_.sdfShadowSteps);
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
                    ++stats_.shadows.view[v].entitiesCulled;
                    continue;
                }
            }
            const GpuMesh& mesh = *item.geometry;
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
            ++stats_.shadows.view[v].entityDraws;
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
    //
    // ADR-119: the pass is encoded unconditionally, and the alternative was measured rather than
    // assumed. Eliding it when no layer is staged as Background -- and letting the scene pass clear
    // target 0 itself -- removes a store and a load of 8 bytes a pixel, which sounds like free
    // bandwidth and is not: the `auxstore` probe showed that not writing *three times that much*
    // (the four auxiliary targets, 24 B/px) moves the frame by nothing measurable. See ADR-119.
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
            const GpuMesh& mesh = *item.geometry;
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
            attachments[i + 1].storeOp =
                toggles_.auxTargetStores ? wgpu::StoreOp::Store : wgpu::StoreOp::Discard;
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
                const GpuMesh& mesh = *item.geometry;
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
        // ADR-345: one predicate, in scene_types.hpp beside the struct it reads, so which
        // background gets drawn is a thing a test can ask rather than a thing a render reveals.
        const scene::SkyBackground background =
            scene::skyBackgroundFor(scene.environment, ibl, ibl_.fromSky);
        if (background != scene::SkyBackground::FlatColour) {
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
        // ---- the atmospheric sky layer (ADR-230) ----
        //
        // After the sky and before the water, for the same reason the sky is where it is: it is
        // behind everything that is not sky, and the things composited over it -- water, motes,
        // spores -- have to be able to composite over it. Skipped outright when nothing is live,
        // which is what makes "effects disabled" a real arm rather than a frame that renders the
        // same pixels through one more branch.
        //
        // Note it draws whether or not the skybox above did: a comet does not require a procedural
        // sky to exist, and making it depend on one is how the effect ends up invisible in exactly
        // the scene somebody built to show it off.
        if (drawAtmosphere_ && atmospherePipeline_ != nullptr) {
            rp.SetPipeline(atmospherePipeline_);
            const std::uint32_t zeroOffset = 0; // layout requires group 1; this draw ignores it
            rp.SetBindGroup(1, objectBindGroup_, 1, &zeroOffset);
            rp.SetBindGroup(2, materialBindGroup(scene::Material{}));
            rp.Draw(3);
            ++stats_.drawCalls;
            // Counted with the sky and not with the geometry: it is one triangle of sky-sized
            // fragment work, and a geometry budget a resolution change moves is not one.
            ++stats_.triangles;
            ++stats_.state.pipelineBinds;
            stats_.state.bindGroupBinds += 2;
        }
        // ---- the Cosmic Ocean (ADR-390) ----
        //
        // SEQUENCING, and the reason this draw does not fire in any shipped frame yet: its source
        // is `scene.atmospherics.cosmicOcean`, and `AtmosphericFrame` is one of the four shared
        // files waiting on the vortex branch. Until that lands, the only caller of
        // `CosmicOceanRenderer::update` is `tests/rendering/test_cosmic_ocean_gpu.cpp`, so
        // `stats().drawn` is false in every real frame. The effect is **deliberately** unreachable
        // rather than accidentally so, and this comment is the entire difference between the two --
        // ADR-350's family is a list of things that were built, tested, and quietly reached
        // nothing, and every one of them looked exactly like this with no note attached.
        //
        // Immediately after the atmospheric layer and before the water, for the same reason that
        // one is where it is: it is behind everything that is not sky, and the things composited
        // over it -- water, motes, spores -- have to be able to composite over it.
        //
        // After rather than before the comet and the aurora, and it matters: both are additive, so
        // the order does not change the colour, but the ocean is a *background* and drawing it
        // second means its own depth attenuation cannot dim a comet that is supposed to be in front
        // of it. Additive composition is associative; the artistic ordering is not, and this is the
        // one that keeps a comet reading as nearer than the nebula.
        //
        // It shares group 0 with the pass and binds only its own group 1, so the bind groups the
        // draws after it need are the ones they already set for themselves.
        if (toggles_.cosmicOcean && cosmicOcean_->stats().drawn) {
            cosmicOcean_->draw(rp);
            ++stats_.drawCalls;
            // Counted with the sky and not with the geometry, as the atmosphere draw above is: one
            // triangle of sky-sized fragment work, and a geometry budget a resolution change moves
            // is not one.
            ++stats_.triangles;
            ++stats_.state.pipelineBinds;
            ++stats_.state.bindGroupBinds;
        }
        // ---- water (ADR-099) ----
        // After the sky and before the particles: the surface has to composite over the bed and
        // the bank, which are opaque and already drawn, and the motes and spores above it have to
        // composite over the surface. Its own pipeline, so the branch it needs is not in the
        // shader every other entity in the world runs.
        if (!water.empty() && water_->ready()) {
            rp.SetPipeline(water_->pipeline());
            ++stats_.state.pipelineBinds;
            for (const auto& item : water) {
                const GpuMesh& mesh = *item.geometry;
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
        if (toggles_.particles) {
            particles_->draw(rp, scene);
            stats_.drawCalls += particles_->stats().systems;
        }
        if (toggles_.particles && !blended.empty() && particles_->stats().systems > 0) {
            rp.SetBindGroup(0, frameBindGroup_); // the particle pass rebinds group 0 with its own layout
        }
        drawItems(blended, true);
        rp.End();
    }
    stage(cpu.sceneEncodeMs);

    // ---- volumetric atmosphere (ADR-032): raymarch + depth-aware composite ----
    // ADR-139: at `QualitySettings::volumeResolutionScale` of the scene's resolution, not a
    // hard-coded half. ADR-140: the two passes are timed separately as `volume.march` and
    // `volume.composite`.
    // Skipped entirely when Environment::volumeDensity is 0, so scenes without fog are unchanged.
    if (toggles_.volume) {
        volumes_->update(scene, time, hdr_.width(), hdr_.height(), hdr_.depthView(), fields_.get(),
                         particles_->glowSystems(), qualitySettings_);
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

    // ---- temporal media: capture the clean radiance, apply the temporal effects (ADR-410) ----
    //
    // Deliberately BEFORE the post chain. The ring must hold the scene's own radiance, not a
    // graded, bloomed, tone-mapped frame: post is a look, it changes when a grade changes, and a
    // history of looks cannot be rebuilt by re-rendering. Capturing here is what makes the family
    // a cache of a pure function rather than an accumulator, which is the whole of ADR-410.
    {
        TemporalFrameInputs temporalIn;
        temporalIn.sceneHdr = finalHdr;
        temporalIn.velocity = velocity_.view;
        temporalIn.width = hdr_.width();
        temporalIn.height = hdr_.height();
        // The frame index, not the render time: `beginFrame` distinguishes a repeat from a jump by
        // comparing indices, and a float second cannot be compared for equality to do that.
        temporalIn.frameIndex = time.frameIndex;
        temporalIn.settings = &scene.temporal;
        temporalIn.resolutionScale = qualitySettings_.temporalHistoryScale;
        temporalIn.hdrFormat = kHdrFormat;
        if (toggles_.post) {
            finalHdr = temporal_->run(encoder, temporalIn, *pool_);
        }
        stats_.temporal = temporal_->stats();
    }

    // ---- built-in post chain: DoF, motion blur, bloom, grading ----
    {
        PostFrameInputs postIn;
        postIn.sceneHdr = finalHdr;
        postIn.depth = hdr_.depthView();
        postIn.velocity = velocity_.view; // ADR-040: motion blur reconstructs from it
        // ADR-039's selective bloom: weight the bloom by how much of a pixel's radiance is actually
        // *emitted*, so a brightly lit surface stops glowing like a light source. The target has
        // always been written -- the debug view reads it -- and was never handed to the post chain,
        // so `post/bloom/emissionWeight` resolved, ran and changed nothing.
        postIn.emission = emission_.view;
        // ADR-035's identifier target, wired for the same reason the emission target above was:
        // `post/output/sharpenId` was a registered, round-tripping parameter whose shader path
        // exists and is tested, and which could not affect any frame a user rendered, because
        // nothing ever set this view and `fs_sharpen` gates the mask on its presence. Built, and
        // unreachable -- the failure family that has now bitten five times in this session.
        //
        // Default-neutral by construction: `sharpenId` is 0 by default and the shader's mask is
        // `identifierAvailable > 0.5 && maskId != 0u`, so with the default the mask stays 1.0 and
        // every existing frame is unchanged. Proved rather than asserted -- see ADR-372.
        postIn.identifier = ids_.view;
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
        postIn.antialias = toggles_.antialias; // ADR-187
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
    // ---- overdraw / fragment-density counting (ADR-115): opt-in, and only opt-in ----
    //
    // This pass exists only while one of the two views that read it is selected. It is not part of
    // the normal frame at any quality tier, because the technique it uses -- a fragment shader that
    // writes a storage buffer -- disables hidden-surface removal on the hardware this engine ships
    // on (see overdraw_count.wgsl), and a diagnostic that costs the frame it is diagnosing is not a
    // diagnostic worth having on by default.
    if (auxDebugView_ == AuxDebugView::Overdraw || auxDebugView_ == AuxDebugView::FragmentDensity) {
        // Say what this view cannot see, to whoever is looking at it. The pass covers plain opaque
        // entities only, so on a scene that is mostly procedural scatter it draws a confident,
        // detailed, *partial* picture -- which is worse than an empty one, because a blank view
        // announces its own failure and a partial view does not. An investigation into Glowmere's
        // quad overdraw selected this view, got the terrain and none of the ecology, and had to
        // build a CPU instrument instead (ADR-126). Warn once per selection, not per frame.
        if (!warnedOverdrawScope_ && !scene.procedurals.empty()) {
            log::warn("overdraw view: {} procedural object(s) are not counted -- this pass covers "
                      "plain opaque entities only, so the picture is partial",
                      scene.procedurals.size());
            warnedOverdrawScope_ = true;
        }
        encoder.ClearBuffer(overdrawBuffer_);
        if (!overdrawGroup_) {
            wgpu::BindGroupEntry entry{};
            entry.binding = 0;
            entry.buffer = overdrawBuffer_;
            entry.size = overdrawBufferBytes_;
            wgpu::BindGroupDescriptor desc{};
            desc.label = "overdraw-group";
            desc.layout = overdrawLayout_;
            desc.entryCount = 1;
            desc.entries = &entry;
            overdrawGroup_ = context_.device().CreateBindGroup(&desc);
        }
        wgpu::RenderPassDepthStencilAttachment depth{};
        depth.view = hdr_.depthView();
        // Load, not clear: this pass must not disturb the real scene depth the rest of the frame
        // still reads (linear depth, AO, water). `depthCompare = Always` in the pipeline means it
        // never tests against it anyway -- the attachment is here because a render pass needs one.
        depth.depthLoadOp = wgpu::LoadOp::Load;
        depth.depthStoreOp = wgpu::StoreOp::Discard;
        wgpu::RenderPassDescriptor pass{};
        pass.label = "overdraw-count-pass";
        pass.colorAttachmentCount = 0;
        pass.depthStencilAttachment = &depth;
        pass.timestampWrites = timeline_->mark("overdraw", gpu::FrameTimeline::PassKind::Render);
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        rp.SetBindGroup(0, frameBindGroupAux_);
        rp.SetBindGroup(2, overdrawGroup_);
        rp.SetPipeline(overdrawCountPipeline_);
        // Opaque geometry only (the same list the depth prepass draws), and skinned characters are
        // skipped: this diagnostic answers "is static geometry overdrawing", the question the
        // renderer-upgrade audit's quad-overdraw finding was about, not a complete fragment count
        // for every subsystem. Procedural instances, SDFs, particles and transparency are outside
        // its scope for now.
        for (const auto& item : opaque) {
            if (item.skinned()) {
                continue;
            }
            const GpuMesh& mesh = *item.geometry;
            rp.SetBindGroup(1, objectBindGroup_, 1, &item.offset);
            rp.SetVertexBuffer(0, mesh.vertices);
            rp.SetIndexBuffer(mesh.indices, wgpu::IndexFormat::Uint32);
            rp.DrawIndexed(mesh.indexCount);
        }
        rp.End();
    }
    // ---- auxiliary-target debug view (ADR-035): one target full-screen, over the finished frame ----
    //
    // **After** the tone map, not before it, and that is a correction rather than a preference
    // (forensics Phase 4.4). Drawn into the HDR target, which is where this pass used to live, every
    // diagnostic went through auto-exposure and a filmic curve on its way to the screen: a normal
    // encoded as 0.5 did not arrive as 0.5, an identifier's palette shifted with how bright the
    // scene happened to be, and a linear depth could not be read as a number at all -- 1.0 landed on
    // the screen as 202. A diagnostic whose values are a function of the picture it is diagnosing is
    // the kind of instrument this investigation exists to remove. Here the byte on the screen is the
    // value the shader wrote, up to the hardware's own encode on an sRGB target.
    if (auxDebugView_ != AuxDebugView::None) {
        auto auxPipeline = auxDebugPipelineFor(target.format);
        if (!auxPipeline) {
            return std::unexpected(auxPipeline.error());
        }
        if (!auxDebugGroup_) {
            std::array<wgpu::BindGroupEntry, 8> entries{};
            entries[0].binding = 0;
            entries[0].buffer = auxDebugUniforms_;
            entries[0].size = sizeof(AuxDebugUniforms);
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
            entries[7].binding = 7;
            entries[7].buffer = overdrawBuffer_;
            entries[7].size = overdrawBufferBytes_;
            wgpu::BindGroupDescriptor desc{};
            desc.label = "aux-debug-group";
            desc.layout = auxDebugLayout_;
            desc.entryCount = entries.size();
            desc.entries = entries.data();
            auxDebugGroup_ = context_.device().CreateBindGroup(&desc);
        }
        AuxDebugUniforms aux{};
        // Each view needs a default in the range of the quantity it shows, or it saturates and
        // reads as a silhouette. Velocity wants 40; FragmentDensity divides a 3x3 mean fragment
        // count and clamps to 1, so at a scale of 1 every shaded pixel is white and the view is a
        // black-and-white mask -- 8 puts an ordinary 1-8x shading load across the whole ramp.
        // Overdraw (mode 11) genuinely wants 1: there the scale is a band divisor, not a ceiling,
        // and one band per count is the palette those tools have always used.
        float defaultScale = 1.0f;
        if (auxDebugView_ == AuxDebugView::Velocity) {
            defaultScale = 40.0f;
        } else if (auxDebugView_ == AuxDebugView::FragmentDensity) {
            defaultScale = 8.0f;
        }
        aux.info = glm::vec4(static_cast<float>(auxDebugView_),
                             auxDebugScale_ > 0.0f ? auxDebugScale_ : defaultScale,
                             static_cast<float>(hdr_.width()), static_cast<float>(hdr_.height()));
        // The near and far planes the frame was actually built with, so a linear-depth display is
        // in metres against this camera rather than against a constant somebody has to remember.
        // The selected object's pick id is here for the object-depth view: -1 means "nothing
        // selected", which the shader shows as an empty frame rather than as object zero.
        float selectedId = -1.0f;
        if (!diagnosticEntity_.empty()) {
            if (const RenderObjectDiagnostic* selected = diagnosticObject(diagnosticEntity_); selected != nullptr) {
                selectedId = static_cast<float>(scene::packPickId(scene::PickSpace::Entity, selected->entityIndex));
            }
        }
        aux.depth = glm::vec4(diagnosticFrame_.nearPlane, diagnosticFrame_.farPlane, selectedId, 0.0f);
        queue.WriteBuffer(auxDebugUniforms_, 0, &aux, sizeof(aux));
        wgpu::RenderPassColorAttachment colour{};
        colour.view = target.view;
        colour.loadOp = wgpu::LoadOp::Clear;
        colour.storeOp = wgpu::StoreOp::Store;
        colour.clearValue = {0.0, 0.0, 0.0, 1.0};
        wgpu::RenderPassDescriptor pass{};
        pass.label = "aux-debug-pass";
        pass.colorAttachmentCount = 1;
        pass.colorAttachments = &colour;
        pass.timestampWrites = timeline_->mark("auxdebug", gpu::FrameTimeline::PassKind::Render);
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        rp.SetPipeline(*auxPipeline);
        rp.SetBindGroup(0, auxDebugGroup_);
        rp.Draw(3);
        rp.End();
        ++stats_.drawCalls;
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
                  "{}unclassified; shadow split: ecology {} tris / {} inst, entities {} tris / {} inst; "
                  "ecology cull: {} records, {} visible, {} casting",
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
                  g.shadow.instances - procShadow.instances,
                  // ADR-287. The three numbers the ecology's caster gap was invisible in: how many
                  // instances the cull looked at, how many the camera kept, and how many the shadow
                  // maps are drawn from. The last two were ONE number until the second list existed
                  // -- 496 and 496 on this scene -- and nothing printed could have said so.
                  stats_.procedural.culledInstances + stats_.procedural.visibleInstances,
                  stats_.procedural.visibleInstances, stats_.procedural.shadowInstances);
    }
    // AVGEN_SHADOW_STATS=1 prints everything §15 asks a shadow diagnostic to expose, per frame and
    // per view: the cascade id, the depths it covers, the volume it covers them with, its texel,
    // both halves of its bias in the units each is applied in, the caster accounting, and the
    // light direction and camera position the fit was made from. One line per view, because a
    // cascade is the unit somebody is asking about.
    //
    // It is printed rather than only exposed because a counter nobody can get at from a command
    // line does not get read -- the same reason AVGEN_FRAME_COUNTERS exists. Nothing here is
    // recomputed: every number is `ShadowRenderer::stats()`, which is written from `reportView`,
    // which is the same call that writes the uniform the shader reads.
    static const bool dumpShadow = std::getenv("AVGEN_SHADOW_STATS") != nullptr;
    if (dumpShadow && time.frameIndex % 30 == 0) {
        const ShadowStats& sh = stats_.shadows;
        log::info("shadows: {} view(s) ({} cascade, {} spot, {} point) at {}x{}; range {:.2f} m, "
                  "fade from {:.2f} m, coarsest texel {:.4f} m; splits {:.2f}/{:.2f}/{:.2f}/{:.2f}; "
                  "{} casters, {} entity draws, {} rejected by a view's own frustum; "
                  "light ({:.3f}, {:.3f}, {:.3f}) camera ({:.2f}, {:.2f}, {:.2f})",
                  sh.views, sh.cascades, sh.spots, sh.points, sh.resolution, sh.resolution, sh.range,
                  sh.fadeStart, sh.coarsestTexel, sh.splits[0], sh.splits[1], sh.splits[2],
                  sh.splits[3], stats_.shadowCasters, sh.entityDraws, sh.entitiesCulled,
                  sh.lightDirection.x, sh.lightDirection.y, sh.lightDirection.z, sh.cameraPosition.x,
                  sh.cameraPosition.y, sh.cameraPosition.z);
        for (std::uint32_t v = 0; v < sh.views && v < kMaxShadowViews; ++v) {
            const ShadowViewReport& r = sh.view[v];
            log::info("  view {} {}: depth {:.2f}..{:.2f} m, centre ({:.2f}, {:.2f}, {:.2f}), "
                      "half-extent {:.2f} m, texel {:.4f} m, depth range {:.2f} m, "
                      "bias {:.4f} m = {:.6f} normalised, normal offset {:.4f} m, "
                      "{} draws, {} culled",
                      r.index, r.cascade ? "cascade" : (r.cube ? "cube face" : "spot"), r.nearDepth,
                      r.farDepth, r.center.x, r.center.y, r.center.z, r.orthoRadius,
                      r.texelWorldSize, r.depthRange, r.worldBias, r.depthBias, r.normalOffset,
                      r.entityDraws, r.entitiesCulled);
        }
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
    if (toggles_.particles) {
        stats_.particles = particles_->stats();
    }
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
    // ADR-351. Copied rather than accumulated, for the reason the comment below gives about running
    // twice per submitted frame.
    stats_.entityLod.drawables = entityLod_.drawables;
    stats_.entityLod.demoted = entityLod_.demoted;
    stats_.entityLod.changed = entityLod_.changed;
    stats_.entityLod.held = entityLod_.held;
    stats_.entityLod.sourceTriangles = entityLod_.sourceTriangles;
    stats_.entityLod.drawnTriangles = entityLod_.drawnTriangles;
    for (std::size_t i = 0; i < entityLod_.rungCounts.size() && i < 8; ++i) {
        stats_.entityLod.rungs[i] = entityLod_.rungCounts[i];
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
    // ADR-277, and ADR-251 again at the call site it did not reach. `width`/`height` are the
    // OUTPUT size; `hdrOutput_` is the scene target, which `resize()` sizes to `output *
    // renderScale`. Reading the output's extent out of it returned the top-left corner of a
    // supersampled frame -- the right size, the right format, scene-linear, and the wrong part of
    // the picture, which is ADR-251's sentence about the same mistake in `RenderJob::renderOne`.
    // The caller gets the scene target's own extent and is told nothing it has to correct for: a
    // measurement in normalised coordinates is comparable across render scales as it stands, and
    // one in pixels was already a measurement of the scene target rather than of the output.
    return gpu::readTextureF16(context_, hdrOutput_, hdrOutput_.GetWidth(), hdrOutput_.GetHeight());
}

} // namespace avgen::rendering
