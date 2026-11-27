#include "rendering/volumetric_light_renderer.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/shader_library.hpp"
#include "scene/scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace avgen::rendering {

namespace {

// Group 1 binding 1 of both passes. Mirrors `VolumetricLightUniforms` in shaders/volumetric_light.wgsl.
struct VolumetricLightUniforms {
    glm::vec4 grid;     // x, y, z = froxel counts, w = subsamples
    glm::vec4 depth;    // x = slice origin depth, y = far, z = log(far / origin), w = intensity
    glm::vec4 medium0;  // volumeDensity, fogHeight, fogHeightFalloff, volumeScattering
    glm::vec4 medium1;  // volumeAbsorption, volumeAnisotropy, volumeEmission, horizonDensity
    glm::vec4 shape;    // fogUpperDensity, fogHeightCurve, fogGroundFollow, 0
    glm::vec4 noise;    // volumeNoise, volumeNoiseScale, volumeNoiseSpeed, time
    glm::vec4 fogColor; // rgb, w = volumeLocalLights
    glm::vec4 info;     // x = glow systems, y = shadow taps, z = local lights per froxel, w = 0
    std::array<glm::vec4, 8> lightShadow;
};
static_assert(sizeof(VolumetricLightUniforms) == 16 * (8 + 8));

constexpr std::uint32_t kMaxParticleGlowSystems = 8; // ParticleRenderer::kMaxGlowSystems (ADR-040)
constexpr std::uint64_t kParticleGlowBytes = kMaxParticleGlowSystems * 32;
// Where the exponential slicing starts. Slice 0 still begins at the eye (the integrate pass carries the
// first metres in it); this only sets how the remaining slices are spread. 0.5 m keeps the first slices
// from being millimetres thick at a 0.1 m near plane.
constexpr float kSliceOrigin = 0.5f;

} // namespace

struct VolumetricLightRenderer::Impl {
    Impl(gpu::Context& c, gpu::ShaderLibrary& s) : context(c), shaders(s) {}

    Result<void> createPipelines();
    void ensureGrid(glm::uvec3 dims);

    gpu::Context& context;
    gpu::ShaderLibrary& shaders;
    wgpu::ComputePipeline inject;
    wgpu::ComputePipeline integrate;
    wgpu::Buffer uniforms;
    wgpu::Buffer emptyGlow;
    wgpu::Texture scatter, integrated;
    wgpu::TextureView scatterView, integratedView;
    glm::uvec3 dims{0};
    gpu::FrameTimeline* timeline = nullptr;
    VolumetricLightApply apply;
    bool activeThisFrame = false;
    bool initialised = false;
    VolumetricLightDecline loggedDecline = VolumetricLightDecline::Off;
    glm::uvec3 loggedDims{0};
    std::uint32_t loggedTaps = 0;
    std::uint32_t loggedSubsamples = 0;
};

VolumetricLightRenderer::VolumetricLightRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : impl_(std::make_unique<Impl>(context, shaders)) {}

VolumetricLightRenderer::~VolumetricLightRenderer() = default;

Result<void> VolumetricLightRenderer::Impl::createPipelines() {
    auto module = shaders.load("volumetric_light.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    const auto& device = context.device();
    device.PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::ComputePipelineDescriptor d{};
    d.label = "volumetric-light-inject";
    d.compute.module = *module;
    d.compute.entryPoint = "cs_inject";
    wgpu::ComputePipeline a = device.CreateComputePipeline(&d);
    d.label = "volumetric-light-integrate";
    d.compute.entryPoint = "cs_integrate";
    wgpu::ComputePipeline b = device.CreateComputePipeline(&d);
    std::string error;
    auto future = device.PopErrorScope(
        wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
            if (type != wgpu::ErrorType::NoError) {
                error = gpu::Context::toString(msg);
            }
        });
    context.waitFor(future);
    if (!error.empty() || !a || !b) {
        return fail("volumetric light pipelines failed: {}", error);
    }
    inject = a;
    integrate = b;
    return {};
}

void VolumetricLightRenderer::Impl::ensureGrid(glm::uvec3 want) {
    if (scatter && dims == want) {
        return;
    }
    dims = want;
    const auto& device = context.device();
    for (int k = 0; k < 2; ++k) {
        wgpu::TextureDescriptor d{};
        d.label = k == 0 ? "volumetric-light-scatter" : "volumetric-light-integrated";
        d.usage = wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::TextureBinding;
        d.dimension = wgpu::TextureDimension::e3D;
        d.size = {want.x, want.y, want.z};
        d.format = wgpu::TextureFormat::RGBA16Float;
        (k == 0 ? scatter : integrated) = device.CreateTexture(&d);
    }
    scatterView = scatter.CreateView();
    integratedView = integrated.CreateView();
}

Result<void> VolumetricLightRenderer::init() {
    Impl& im = *impl_;
    const auto& device = im.context.device();
    {
        wgpu::BufferDescriptor d{};
        d.label = "volumetric-light-uniforms";
        d.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        d.size = sizeof(VolumetricLightUniforms);
        im.uniforms = device.CreateBuffer(&d);
        d.label = "volumetric-light-empty-glow";
        d.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        d.size = kParticleGlowBytes;
        im.emptyGlow = device.CreateBuffer(&d);
        const std::array<float, kParticleGlowBytes / sizeof(float)> zero{};
        im.context.queue().WriteBuffer(im.emptyGlow, 0, zero.data(), kParticleGlowBytes);
    }
    if (auto r = im.createPipelines(); !r) {
        return r;
    }
    im.initialised = true;
    return {};
}

Result<void> VolumetricLightRenderer::reload() { return impl_->createPipelines(); }

void VolumetricLightRenderer::update(const scene::Scene& scene, const FrameTime& time, std::uint32_t width,
                                     std::uint32_t height, const std::array<int, 8>& packedIndexOf,
                                     const std::array<bool, 8>& shadowsAir, const QualitySettings& quality,
                                     std::uint32_t particleGlowSystems) {
    Impl& im = *impl_;
    im.activeThisFrame = false;
    im.apply = VolumetricLightApply{};
    stats_ = VolumetricLightStats{};
    const VolumetricLightDecline why = decline(scene);
    stats_.decline = why;
    if (why != VolumetricLightDecline::Off && why != im.loggedDecline) {
        if (why != VolumetricLightDecline::None) {
            log::warn("volumetric lighting asked for and not drawn: {}", volumetricLightDeclineName(why));
        }
    }
    im.loggedDecline = why;
    if (!im.initialised || why != VolumetricLightDecline::None || width == 0 || height == 0) {
        return;
    }
    const scene::Environment& env = scene.environment;
    const glm::uvec3 dims = volumetricLightGrid(width, height, quality);
    im.ensureGrid(dims);

    const float far = std::clamp(env.volumetricLight.distance, scene::kVolumetricLightMinDistance,
                                 scene::kVolumetricLightMaxDistance);
    const float origin = std::min(kSliceOrigin, far * 0.5f);
    const float logRatio = std::log(far / origin);
    const std::uint32_t subsamples = std::clamp<std::uint32_t>(quality.volumetricLightSubsamples, 1, 4);
    const std::uint32_t taps = quality.volumetricLightShadowTaps >= 4 ? 4u : 1u;
    const std::uint32_t local = std::min<std::uint32_t>(quality.volumetricLightLocalLights, 32);

    VolumetricLightUniforms u{};
    u.grid = glm::vec4(glm::vec3(dims), static_cast<float>(subsamples));
    u.depth = glm::vec4(origin, far, logRatio, std::max(env.volumetricLight.intensity, 0.0f));
    u.medium0 = glm::vec4(std::max(env.volumeDensity, 0.0f), env.fogHeight, std::max(env.fogHeightFalloff, 0.0f),
                          std::max(env.volumeScattering, 0.0f));
    u.medium1 = glm::vec4(std::max(env.volumeAbsorption, 0.0f), env.volumeAnisotropy, std::max(env.volumeEmission, 0.0f),
                          std::clamp(env.horizonDensity, 0.0f, scene::kHorizonDensityMax));
    u.shape = glm::vec4(std::clamp(env.fogUpperDensity, 0.0f, 1.0f), std::clamp(env.fogHeightCurve, 0.0f, 1.0f),
                        scene.terrainGround.valid() ? std::clamp(env.fogGroundFollow, 0.0f, 1.0f) : 0.0f, 0.0f);
    u.noise = glm::vec4(env.volumeNoiseAmount, env.volumeNoiseScale, env.volumeNoiseSpeed,
                        static_cast<float>(time.renderTime));
    u.fogColor = glm::vec4(env.fogColor, std::max(env.volumeLocalLights, 0.0f));
    const std::uint32_t glow = std::min(particleGlowSystems, kMaxParticleGlowSystems);
    u.info = glm::vec4(static_cast<float>(glow), static_cast<float>(taps), static_cast<float>(local), 0.0f);
    std::uint32_t shadowed = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        const bool shadows = packedIndexOf[i] >= 0 && shadowsAir[i];
        u.lightShadow[i] = glm::vec4(static_cast<float>(packedIndexOf[i]), shadows ? 1.0f : 0.0f, 0.0f, 0.0f);
        shadowed += shadows ? 1u : 0u;
    }
    im.context.queue().WriteBuffer(im.uniforms, 0, &u, sizeof(u));

    im.activeThisFrame = true;
    im.apply.active = true;
    im.apply.integrated = im.integratedView;
    im.apply.nearDepth = origin;
    im.apply.logRatio = logRatio;
    im.apply.distance = far;
    im.apply.slices = dims.z;
    stats_.active = true;
    stats_.gridX = dims.x;
    stats_.gridY = dims.y;
    stats_.gridZ = dims.z;
    stats_.shadowTaps = taps;
    stats_.subsamples = subsamples;
    stats_.localLightsPerFroxel = local;
    stats_.shadowedLights = shadowed;
    stats_.distance = far;
    stats_.memoryBytes = 2ull * dims.x * dims.y * dims.z * 8ull;
    if (dims != im.loggedDims || taps != im.loggedTaps || subsamples != im.loggedSubsamples) {
        log::info("volumetric lighting: {}x{}x{} froxels to {:.0f} m, {} shadow tap(s), {} sample(s) a froxel, {:.1f} MB",
                  dims.x, dims.y, dims.z, far, taps, subsamples,
                  static_cast<double>(stats_.memoryBytes) / (1024.0 * 1024.0));
        im.loggedDims = dims;
        im.loggedTaps = taps;
        im.loggedSubsamples = subsamples;
    }
}

void VolumetricLightRenderer::encode(wgpu::CommandEncoder& encoder, const VolumetricLightInputs& in,
                                     bool allowDiagnosticRepeat) {
    Impl& im = *impl_;
    if (!im.activeThisFrame) {
        return;
    }
    const auto& device = im.context.device();
    const auto entry = [](std::uint32_t binding) {
        wgpu::BindGroupEntry e{};
        e.binding = binding;
        return e;
    };
    const auto group = [&](const wgpu::ComputePipeline& p, std::uint32_t index, std::vector<wgpu::BindGroupEntry> e) {
        wgpu::BindGroupDescriptor d{};
        d.layout = p.GetBindGroupLayout(index);
        d.entryCount = e.size();
        d.entries = e.data();
        return device.CreateBindGroup(&d);
    };
    const wgpu::Buffer glow = in.particleGlow ? in.particleGlow : im.emptyGlow;
    // The inject: the frame's resources at their frame-group binding numbers, for the compute stage.
    std::vector<wgpu::BindGroupEntry> frame;
    {
        auto e = entry(0); e.buffer = in.frameUniforms; frame.push_back(e);
        e = entry(1); e.buffer = in.lights; frame.push_back(e);
        e = entry(2); e.buffer = in.clusters; frame.push_back(e);
        e = entry(3); e.buffer = in.shadowUniforms; frame.push_back(e);
        e = entry(4); e.textureView = in.shadowAtlas; frame.push_back(e);
        e = entry(5); e.sampler = in.shadowSampler; frame.push_back(e);
        e = entry(12); e.textureView = in.terrainHeight; frame.push_back(e);
    }
    std::vector<wgpu::BindGroupEntry> own;
    {
        auto e = entry(1); e.buffer = im.uniforms; own.push_back(e);
        e = entry(2); e.buffer = glow; e.size = kParticleGlowBytes; own.push_back(e);
        e = entry(3); e.textureView = im.scatterView; own.push_back(e);
    }
    // DIAGNOSTIC ONLY: AVGEN_VOLUMETRIC_REPEAT=n encodes inject and integrate n times a frame, so their cost can be
    // measured by amplification, at any tier but Offline -- compute timestamps are unwritten on this Dawn/Metal path (ADR-2210) and a single
    // pass hides inside a frame A/B's noise. The result is identical (each repeat rewrites the same volumes); the
    // variable is an AVGEN_* override, so every run that sets it says so in its first log lines.
    static const std::uint32_t repeat = [] {
        const char* v = std::getenv("AVGEN_VOLUMETRIC_REPEAT");
        const long n = v != nullptr ? std::strtol(v, nullptr, 10) : 1;
        return static_cast<std::uint32_t>(std::clamp<long>(n, 1, 64));
    }();
    // Never in an offline render: a final carries no diagnostic, even one that changes only its cost.
    static bool loggedIgnored = false;
    if (repeat > 1 && !allowDiagnosticRepeat && !loggedIgnored) {
        log::warn("AVGEN_VOLUMETRIC_REPEAT={} ignored: this render is at the Offline tier", repeat);
        loggedIgnored = true;
    }
    const std::uint32_t passes = allowDiagnosticRepeat ? repeat : 1u;
    for (std::uint32_t rep = 0; rep < passes; ++rep) {
        {
            wgpu::ComputePassDescriptor pd{};
            pd.label = "volumetric-light-inject";
            pd.timestampWrites = im.timeline != nullptr && rep == 0
                                     ? im.timeline->mark("froxel.inject", gpu::FrameTimeline::PassKind::Compute)
                                     : nullptr;
            wgpu::ComputePassEncoder pass = encoder.BeginComputePass(&pd);
            pass.SetPipeline(im.inject);
            pass.SetBindGroup(0, group(im.inject, 0, frame));
            pass.SetBindGroup(1, group(im.inject, 1, own));
            pass.DispatchWorkgroups((im.dims.x + 3) / 4, (im.dims.y + 3) / 4, (im.dims.z + 3) / 4);
            pass.End();
        }
        {
            std::vector<wgpu::BindGroupEntry> f0;
            auto e = entry(0); e.buffer = in.frameUniforms; f0.push_back(e);
            std::vector<wgpu::BindGroupEntry> f1;
            e = entry(1); e.buffer = im.uniforms; f1.push_back(e);
            e = entry(4); e.textureView = im.scatterView; f1.push_back(e);
            e = entry(5); e.textureView = im.integratedView; f1.push_back(e);
            wgpu::ComputePassDescriptor pd{};
            pd.label = "volumetric-light-integrate";
            pd.timestampWrites = im.timeline != nullptr && rep == 0
                                     ? im.timeline->mark("froxel.integrate", gpu::FrameTimeline::PassKind::Compute)
                                     : nullptr;
            wgpu::ComputePassEncoder pass = encoder.BeginComputePass(&pd);
            pass.SetPipeline(im.integrate);
            pass.SetBindGroup(0, group(im.integrate, 0, f0));
            pass.SetBindGroup(1, group(im.integrate, 1, f1));
            pass.DispatchWorkgroups((im.dims.x + 7) / 8, (im.dims.y + 7) / 8, 1);
            pass.End();
        }
    }
}

VolumetricLightApply VolumetricLightRenderer::apply() const { return impl_->apply; }
void VolumetricLightRenderer::setTimeline(gpu::FrameTimeline* timeline) { impl_->timeline = timeline; }

} // namespace avgen::rendering
