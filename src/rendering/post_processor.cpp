#include "rendering/post_processor.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/shader_library.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

namespace avgen::rendering {

namespace {

// IEEE 754 binary16 -> float. The metering result is one RGBA16F texel, so a tiny decoder beats
// pulling in a whole readback path.
float halfToFloat(std::uint16_t h) {
    const std::uint32_t sign = static_cast<std::uint32_t>(h & 0x8000u) << 16;
    std::uint32_t exponent = (h >> 10) & 0x1Fu;
    std::uint32_t mantissa = h & 0x3FFu;
    std::uint32_t bits = 0;
    if (exponent == 0) {
        if (mantissa != 0) { // subnormal: normalise it
            exponent = 127 - 15 + 1;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                --exponent;
            }
            mantissa &= 0x3FFu;
            bits = sign | (exponent << 23) | (mantissa << 13);
        } else {
            bits = sign;
        }
    } else if (exponent == 31) {
        bits = sign | 0x7F800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
    }
    float out = 0.0f;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

constexpr std::uint32_t kMeterFirstDivisor = 4;  // the prefilter drops straight to quarter size
constexpr std::uint32_t kMeterReduceFactor = 4;  // then quarters again per pass
// The most taps a side the anamorphic streak will spend. Beyond this the pass stops sampling more
// finely and reaches for a coarser pyramid level instead, so a long streak costs samples linearly
// rather than without bound. Forty-eight is the smallest budget measured to leave no comb at
// Glowmere's authored stretch (docs/post-artifact-forensics.md); the sweep is in
// tests/rendering/test_post_artifact_forensics_gpu.cpp.
constexpr std::uint32_t kAnamorphicTapBudget = 48;

} // namespace

PostProcessor::PostProcessor(gpu::Context& context, gpu::ShaderLibrary& shaders) : context_(context), shaders_(shaders) {}

void PostProcessor::resetExposure() {
    exposureState_.reset();
    haveMeasurement_ = false;
    measuredLuminance_ = 0.0f;
}

Result<void> PostProcessor::init() {
    const auto& device = context_.device();
    {
        std::array<wgpu::BindGroupLayoutEntry, 8> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.hasDynamicOffset = true;
        entries[0].buffer.minBindingSize = sizeof(Uniforms);
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Fragment;
        entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;
        entries[2].binding = 2;
        entries[2].visibility = wgpu::ShaderStage::Fragment;
        entries[2].texture.sampleType = wgpu::TextureSampleType::Float;
        entries[2].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        entries[3] = entries[2];
        entries[3].binding = 3;
        entries[4].binding = 4;
        entries[4].visibility = wgpu::ShaderStage::Fragment;
        entries[4].texture.sampleType = wgpu::TextureSampleType::Depth;
        entries[4].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        entries[5] = entries[2];
        entries[5].binding = 5;
        entries[6] = entries[2];
        entries[6].binding = 6;
        entries[7].binding = 7;
        entries[7].visibility = wgpu::ShaderStage::Fragment;
        entries[7].texture.sampleType = wgpu::TextureSampleType::Uint;
        entries[7].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "post-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        layout_ = device.CreateBindGroupLayout(&desc);
        wgpu::PipelineLayoutDescriptor pl{};
        pl.bindGroupLayoutCount = 1;
        pl.bindGroupLayouts = &layout_;
        pipelineLayout_ = device.CreatePipelineLayout(&pl);
    }
    {
        wgpu::SamplerDescriptor desc{};
        desc.label = "post-sampler";
        desc.addressModeU = wgpu::AddressMode::ClampToEdge;
        desc.addressModeV = wgpu::AddressMode::ClampToEdge;
        desc.magFilter = wgpu::FilterMode::Linear;
        desc.minFilter = wgpu::FilterMode::Linear;
        sampler_ = device.CreateSampler(&desc);
    }
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "post-uniforms";
        desc.size = static_cast<std::uint64_t>(kMaxSlots) * kSlotStride;
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        uniforms_ = device.CreateBuffer(&desc);
        wgpu::BufferDescriptor mdesc{};
        mdesc.label = "post-meter-readback";
        mdesc.size = kMeterReadbackBytes;
        mdesc.usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
        meterReadback_ = device.CreateBuffer(&mdesc);
    }
    {
        wgpu::TextureDescriptor desc{};
        desc.label = "post-black";
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        desc.dimension = wgpu::TextureDimension::e2D;
        desc.size = {1, 1, 1};
        desc.format = kHdrFormat;
        black_ = device.CreateTexture(&desc);
        blackView_ = black_.CreateView();
        wgpu::TextureDescriptor ddesc{};
        ddesc.label = "post-depth-placeholder";
        ddesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment;
        ddesc.dimension = wgpu::TextureDimension::e2D;
        ddesc.size = {1, 1, 1};
        ddesc.format = wgpu::TextureFormat::Depth24Plus;
        depthPlaceholder_ = device.CreateTexture(&ddesc);
        depthPlaceholderView_ = depthPlaceholder_.CreateView();
        // Identifier placeholder: one zero texel, so an unmasked chain reads "no object here".
        wgpu::TextureDescriptor idesc{};
        idesc.label = "post-id-placeholder";
        idesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        idesc.dimension = wgpu::TextureDimension::e2D;
        idesc.size = {1, 1, 1};
        idesc.format = wgpu::TextureFormat::R32Uint;
        idPlaceholder_ = device.CreateTexture(&idesc);
        idPlaceholderView_ = idPlaceholder_.CreateView();
    }
    auto module = shaders_.load("post.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    if (auto r = createPipelines(*module); !r) {
        return r;
    }
    initialised_ = true;
    return {};
}

Result<void> PostProcessor::reload() {
    auto module = shaders_.load("post.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    return createPipelines(*module);
}

Result<wgpu::RenderPipeline> PostProcessor::makePipeline(const wgpu::ShaderModule& module, const char* entry,
                                                        wgpu::TextureFormat format) {
    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = format;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = entry;
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = entry;
    desc.layout = pipelineLayout_;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_fullscreen";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.primitive.cullMode = wgpu::CullMode::None;
    desc.multisample.count = 1;
    desc.multisample.mask = 0xFFFFFFFFu;
    desc.fragment = &fragment;
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
        return fail("post pipeline '{}' failed: {}", entry, error);
    }
    return pipeline;
}

Result<void> PostProcessor::createPipelines(const wgpu::ShaderModule& module) {
    const std::array<std::pair<const char*, wgpu::RenderPipeline*>, 14> slots{{
        {"fs_exposure", &exposure_},
        {"fs_meter_prefilter", &meterPrefilter_},
        {"fs_meter_reduce", &meterReduce_},
        {"fs_prefilter", &prefilter_},
        {"fs_halation_prefilter", &halationPrefilter_},
        {"fs_downsample", &downsample_},
        {"fs_upsample", &upsample_},
        {"fs_wide", &wide_},
        {"fs_lens", &lens_},
        {"fs_composite", &composite_},
        {"fs_fxaa", &fxaa_},
        {"fs_sharpen", &sharpen_},
        {"fs_dof", &dof_},
        {"fs_motion_blur", &motionBlur_},
    }};
    // The two velocity-tile passes write RG16F, not the HDR format (ADR-040).
    const std::array<std::pair<const char*, wgpu::RenderPipeline*>, 2> tileSlots{{
        {"fs_velocity_tile_max", &velocityTileMax_},
        {"fs_velocity_neighbour_max", &velocityNeighbourMax_},
    }};
    // Build every pipeline first, so a shader that fails to compile leaves the previous set intact.
    std::array<wgpu::RenderPipeline, slots.size()> built{};
    for (std::size_t i = 0; i < slots.size(); ++i) {
        auto pipeline = makePipeline(module, slots[i].first);
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        built[i] = *pipeline;
    }
    std::array<wgpu::RenderPipeline, tileSlots.size()> builtTiles{};
    for (std::size_t i = 0; i < tileSlots.size(); ++i) {
        auto pipeline = makePipeline(module, tileSlots[i].first, kVelocityFormat);
        if (!pipeline) {
            return std::unexpected(pipeline.error());
        }
        builtTiles[i] = *pipeline;
    }
    for (std::size_t i = 0; i < slots.size(); ++i) {
        *slots[i].second = built[i];
    }
    for (std::size_t i = 0; i < tileSlots.size(); ++i) {
        *tileSlots[i].second = builtTiles[i];
    }
    return {};
}

void PostProcessor::runPass(wgpu::CommandEncoder& encoder, const wgpu::RenderPipeline& pipeline,
                            const wgpu::TextureView& target, const PassTextures& textures, const Uniforms& uniforms) {
    const std::uint32_t offset = (slot_ % kMaxSlots) * kSlotStride;
    ++slot_;
    context_.queue().WriteBuffer(uniforms_, offset, &uniforms, sizeof(uniforms));
    std::array<wgpu::BindGroupEntry, 8> entries{};
    entries[0].binding = 0;
    entries[0].buffer = uniforms_;
    entries[0].size = sizeof(Uniforms);
    entries[1].binding = 1;
    entries[1].sampler = sampler_;
    entries[2].binding = 2;
    entries[2].textureView = textures.source ? textures.source : blackView_;
    entries[3].binding = 3;
    entries[3].textureView = textures.second ? textures.second : blackView_;
    entries[4].binding = 4;
    entries[4].textureView = textures.depth ? textures.depth : depthPlaceholderView_;
    entries[5].binding = 5;
    entries[5].textureView = textures.third ? textures.third : blackView_;
    entries[6].binding = 6;
    entries[6].textureView = textures.emission ? textures.emission : blackView_;
    entries[7].binding = 7;
    entries[7].textureView = textures.identifier ? textures.identifier : idPlaceholderView_;
    wgpu::BindGroupDescriptor bdesc{};
    bdesc.label = "post-bind-group";
    bdesc.layout = layout_;
    bdesc.entryCount = entries.size();
    bdesc.entries = entries.data();
    wgpu::BindGroup group = context_.device().CreateBindGroup(&bdesc);

    wgpu::RenderPassColorAttachment color{};
    color.view = target;
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor pass{};
    pass.label = "post-pass";
    pass.colorAttachmentCount = 1;
    pass.colorAttachments = &color;
    // Every post pass marks the timeline under the stage that set `stage_`, so "post/bloom" and
    // "post/motionblur" are separable and the chain's total is the sum of the "post/" prefix.
    pass.timestampWrites = timeline_ != nullptr ? timeline_->mark(stage_) : nullptr;
    wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
    rp.SetPipeline(pipeline);
    rp.SetBindGroup(0, group, 1, &offset);
    rp.Draw(3);
    rp.End();
    ++stats_.passes;
}

void PostProcessor::runPass(wgpu::CommandEncoder& encoder, const wgpu::RenderPipeline& pipeline,
                            const wgpu::TextureView& target, const wgpu::TextureView& source,
                            const wgpu::TextureView& second, const wgpu::TextureView& depth, const Uniforms& uniforms) {
    PassTextures textures;
    textures.source = source;
    textures.second = second;
    textures.depth = depth;
    runPass(encoder, pipeline, target, textures, uniforms);
}

bool PostProcessor::takeMeasurement(float& luminance) {
    if (meterPending_) {
        bool ok = false;
        auto future = meterReadback_.MapAsync(wgpu::MapMode::Read, 0, kMeterReadbackBytes,
                                              wgpu::CallbackMode::WaitAnyOnly,
                                              [&](wgpu::MapAsyncStatus status, wgpu::StringView) {
                                                  ok = status == wgpu::MapAsyncStatus::Success;
                                              });
        context_.waitFor(future);
        if (ok) {
            const auto* raw = static_cast<const std::uint16_t*>(meterReadback_.GetConstMappedRange(0, kMeterReadbackBytes));
            if (raw != nullptr) {
                const float weighted = halfToFloat(raw[0]);
                const float weight = halfToFloat(raw[1]);
                if (weight > 1e-6f && std::isfinite(weighted)) {
                    measuredLuminance_ = std::max(weighted / weight, 0.0f);
                    haveMeasurement_ = true;
                }
            }
            meterReadback_.Unmap();
        }
        meterPending_ = false;
    }
    luminance = measuredLuminance_;
    return haveMeasurement_;
}

void PostProcessor::encodeMetering(wgpu::CommandEncoder& encoder, const PostFrameInputs& in, gpu::TransientPool& pool,
                                   const Uniforms& base) {
    const auto usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding |
                       wgpu::TextureUsage::CopySrc;
    std::uint32_t w = std::max(1u, in.width / kMeterFirstDivisor);
    std::uint32_t h = std::max(1u, in.height / kMeterFirstDivisor);
    auto current = pool.acquire(w, h, kHdrFormat, usage, "meter");
    {
        Uniforms u = base;
        u.texelSize = 1.0f / base.outputSize;
        u.params0 = glm::vec4(std::clamp(in.settings->exposure.meterCenterWeight, 0.0f, 1.0f),
                              base.outputSize.x / std::max(base.outputSize.y, 1.0f), 0.0f, 0.0f);
        stage_ = "post/meter";
        runPass(encoder, meterPrefilter_, current.view, in.sceneHdr, nullptr, nullptr, u);
    }
    while (w > 1 || h > 1) {
        const std::uint32_t nw = std::max(1u, (w + kMeterReduceFactor - 1) / kMeterReduceFactor);
        const std::uint32_t nh = std::max(1u, (h + kMeterReduceFactor - 1) / kMeterReduceFactor);
        auto next = pool.acquire(nw, nh, kHdrFormat, usage, "meter");
        Uniforms u = base;
        u.texelSize = 1.0f / glm::vec2(static_cast<float>(w), static_cast<float>(h));
        runPass(encoder, meterReduce_, next.view, current.view, nullptr, nullptr, u);
        current = next;
        w = nw;
        h = nh;
    }
    wgpu::TexelCopyTextureInfo src{};
    src.texture = current.texture;
    wgpu::TexelCopyBufferInfo dst{};
    dst.buffer = meterReadback_;
    dst.layout.bytesPerRow = static_cast<std::uint32_t>(kMeterReadbackBytes);
    dst.layout.rowsPerImage = 1;
    const wgpu::Extent3D extent{1, 1, 1};
    encoder.CopyTextureToBuffer(&src, &dst, &extent);
    meterPending_ = true;
}

const gpu::TransientTexture* PostCapture::find(std::string_view name) const {
    for (const PostCaptureStage& stage : stages) {
        if (stage.name == name) {
            return &stage.texture;
        }
    }
    return nullptr;
}

void PostProcessor::armCapture() {
    capturing_ = true;
    capture_ = PostCapture{};
}

PostCapture PostProcessor::takeCapture() {
    capturing_ = false;
    return std::move(capture_);
}

void PostProcessor::captureStage(std::string name, const gpu::TransientTexture& texture) {
    if (!capturing_ || !texture.valid()) {
        return;
    }
    capture_.stages.push_back(PostCaptureStage{std::move(name), texture});
}

// The pyramid and the wide tier are the only targets the chain allocates without CopySrc -- the
// default the pool hands everything else already has it. Widening them only while a capture is
// armed keeps the production allocation exactly as it was.
wgpu::TextureUsage PostProcessor::pyramidUsage() const {
    const auto base = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    return capturing_ ? (base | wgpu::TextureUsage::CopySrc) : base;
}

wgpu::TextureView PostProcessor::buildPyramid(wgpu::CommandEncoder& encoder, gpu::TransientPool& pool,
                                              const Uniforms& base, std::vector<gpu::TransientTexture>& down,
                                              float spread, float blend, const char* tier) {
    if (down.empty()) {
        return {};
    }
    wgpu::TextureView acc = down.back().view;
    for (int level = static_cast<int>(down.size()) - 2; level >= 0; --level) {
        const auto& fine = down[static_cast<std::size_t>(level)];
        const auto& coarse = down[static_cast<std::size_t>(level) + 1];
        auto target = pool.acquire(fine.width, fine.height, kHdrFormat, pyramidUsage(), "bloom-up");
        Uniforms u = base;
        u.texelSize = 1.0f / glm::vec2(static_cast<float>(coarse.width), static_cast<float>(coarse.height));
        u.params0 = glm::vec4(spread, blend, 0.0f, 0.0f);
        // No stage of its own: the pyramid is shared by bloom and halation, and the caller has
        // already said which one this is.
        runPass(encoder, upsample_, target.view, acc, fine.view, nullptr, u);
        captureStage(std::string(tier) + "/up" + std::to_string(level), target);
        acc = target.view;
    }
    return acc;
}

wgpu::TextureView PostProcessor::run(wgpu::CommandEncoder& encoder, const PostFrameInputs& in, gpu::TransientPool& pool) {
    stats_ = PostStats{};
    stage_ = "post"; // each stage names itself below; nothing inherits the previous frame's label
    slot_ = 0;
    output_ = nullptr;
    if (!initialised_ || in.settings == nullptr || in.width == 0 || in.height == 0) {
        return in.sceneHdr;
    }
    const auto& s = *in.settings;
    if (s.exposureReset) {
        resetExposure();
    }
    Uniforms base{};
    base.outputSize = glm::vec2(static_cast<float>(in.width), static_cast<float>(in.height));
    base.texelSize = 1.0f / base.outputSize;
    base.cameraPos = glm::vec4(in.cameraPos, 1.0f);
    base.prevViewProj = in.prevViewProj;
    base.invViewProj = in.invViewProj;
    base.lift = glm::vec4(s.lift, 0.0f);
    base.gamma = glm::vec4(s.gamma, 1.0f);
    base.gain = glm::vec4(s.gain, 1.0f);

    wgpu::TextureView current = in.sceneHdr;
    const float pixelScale = static_cast<float>(in.height) / 720.0f;
    const bool autoExposure = s.exposure.mode == scene::ExposureSettings::Mode::Automatic;

    // ---- 1. exposure (ADR-039: before everything, so bloom thresholds are in exposed units) ----
    // Metering reads the *pre-exposure* image. The measurement it consumes is the previous frame's,
    // read back synchronously, so the loop is a pure function of the frames that came before it.
    float measured = 0.0f;
    const bool haveMeasured = takeMeasurement(measured);
    const float exposure =
        scene::updateExposure(exposureState_, measured, autoExposure && haveMeasured, s.exposureDeltaSeconds, s.exposure);
    stats_.exposureScale = exposure;
    stats_.exposureEv100 = exposureState_.ev100;
    stats_.meteredLuminance = haveMeasured ? measured : -1.0f;
    if (autoExposure) {
        encodeMetering(encoder, in, pool, base);
    } else {
        meterPending_ = false;
    }
    if (std::abs(exposure - 1.0f) > 1e-3f) {
        auto target = pool.acquire(in.width, in.height, kHdrFormat);
        Uniforms u = base;
        u.params0 = glm::vec4(exposure, 0.0f, 0.0f, 0.0f);
        stage_ = "post/exposure";
        runPass(encoder, exposure_, target.view, current, nullptr, nullptr, u);
        captureStage("exposure", target);
        current = target.view;
    }

    // ---- 2. defocus: depth of field and the tilt-shift band (a lens effect, but it needs
    // undistorted depth) ---------------------------------------------------------------------
    // ADR-079: one pass serves both. They differ only in how the circle of confusion is decided -
    // by distance from a focus plane, or by distance from a band across the frame - and the shader
    // takes the larger of the two circles, so a scene may run either or both. The timeline stage
    // stays "post/dof" because it is still one pass and splitting the label would only make the
    // same microseconds harder to find.
    const bool dofOn = s.dofEnabled && s.dofMaxRadius > 0.0f;
    const bool tiltShiftOn = s.tiltShiftEnabled && s.tiltShiftMaxRadius > 0.0f;
    if (dofOn || tiltShiftOn) {
        auto target = pool.acquire(in.width, in.height, kHdrFormat);
        Uniforms u = base;
        u.params0 = glm::vec4(s.focusDistance, s.focusRange, s.dofMaxRadius * pixelScale, s.dofPhysical ? 1.0f : 0.0f);
        u.params1 = glm::vec4(s.lens.focalLength, s.lens.aperture, s.lens.sensorHeight, static_cast<float>(in.height));
        const float rotation = glm::radians(s.tiltShiftRotation);
        u.params2 = glm::vec4(s.tiltShiftCentre, std::cos(rotation), std::sin(rotation));
        u.params3 = glm::vec4(std::max(s.tiltShiftBandWidth, 0.0f) * 0.5f, std::max(s.tiltShiftFalloff, 1e-4f),
                              s.tiltShiftMaxRadius * pixelScale, tiltShiftOn ? 1.0f : 0.0f);
        u.params4 = glm::vec4(dofOn ? 1.0f : 0.0f, base.outputSize.x / std::max(base.outputSize.y, 1.0f), 0.0f, 0.0f);
        stage_ = "post/dof";
        // The depth view is still bound when only the band is running: binding the placeholder
        // instead would cost a bind group rebuild for a texture the shader never reads.
        runPass(encoder, dof_, target.view, current, nullptr, in.depth, u);
        captureStage("dof", target);
        current = target.view;
    }
    // ---- 3. motion blur: tile-based reconstruction over the velocity target (ADR-035/040) -------
    // Blur length is the frame's screen motion times the shutter fraction (ADR-037), so a zero
    // shutter angle is exactly no blur. Without a velocity target the pass is skipped entirely -
    // the old depth-reprojection fallback is gone, and with it camera-only blur.
    const float shutterFraction = std::clamp(s.lens.shutterAngle, 0.0f, 360.0f) / 360.0f;
    const float blurScale = s.motionBlurAmount * shutterFraction;
    if (blurScale > 1e-4f && in.velocity != nullptr) {
        const std::uint32_t tileSize = std::clamp<std::uint32_t>(s.motionBlurTileSize, 4, 40);
        const std::uint32_t tilesX = (in.width + tileSize - 1) / tileSize;
        const std::uint32_t tilesY = (in.height + tileSize - 1) / tileSize;
        const float maxRadius = std::max(1.0f, s.motionBlurMaxRadius * pixelScale);
        auto tiles = pool.acquire(tilesX, tilesY, kVelocityFormat);
        auto neighbours = pool.acquire(tilesX, tilesY, kVelocityFormat);
        Uniforms u = base;
        u.outputSize = glm::vec2(static_cast<float>(tilesX), static_cast<float>(tilesY));
        u.texelSize = 1.0f / u.outputSize;
        u.params0 = glm::vec4(blurScale, static_cast<float>(tileSize), maxRadius, 0.0f);
        u.params1 = glm::vec4(static_cast<float>(in.width), static_cast<float>(in.height), 0.0f, 0.0f);
        stage_ = "post/motionblur";
        runPass(encoder, velocityTileMax_, tiles.view, in.velocity, nullptr, nullptr, u);
        runPass(encoder, velocityNeighbourMax_, neighbours.view, tiles.view, nullptr, nullptr, u);

        auto target = pool.acquire(in.width, in.height, kHdrFormat);
        Uniforms b = base;
        b.params0 = glm::vec4(blurScale, static_cast<float>(std::clamp<std::uint32_t>(s.motionBlurSamples, 2, 32)),
                              maxRadius, static_cast<float>(tileSize));
        PassTextures textures;
        textures.source = current;
        textures.second = neighbours.view;
        textures.third = in.velocity;
        textures.depth = in.depth;
        runPass(encoder, motionBlur_, target.view, textures, b);
        captureStage("motionblur", target);
        current = target.view;
        pool.release(tiles);
        pool.release(neighbours);
    }
    // ---- 4. lens: distortion and chromatic aberration -------------------------------------------
    if (std::abs(s.distortion) > 1e-4f || s.chromaticAberration > 1e-4f) {
        auto target = pool.acquire(in.width, in.height, kHdrFormat);
        Uniforms u = base;
        u.params0 = glm::vec4(s.chromaticAberration, s.distortion, 0.0f, 0.0f);
        stage_ = "post/lens";
        runPass(encoder, lens_, target.view, current, nullptr, nullptr, u);
        captureStage("lens", target);
        current = target.view;
    }

    // ---- 5. bloom, halation, anamorphic ----------------------------------------------------------
    const bool bloomOn = s.bloomEnabled && s.bloomIntensity > 0.0f;
    const bool anamorphicOn = s.anamorphicEnabled && s.anamorphicIntensity > 0.0f;
    const bool halationOn = s.halationEnabled && s.halationIntensity > 0.0f;
    const bool pyramidOn = bloomOn || anamorphicOn;
    // The tent's blend weight: 0.5 halves each coarser level's share, so the pyramid's mean equals
    // the prefiltered image's mean whatever the level count (energy-conserving upsample, ADR-039).
    const float blend = std::clamp(s.bloomRadius * 0.5f, 0.05f, 0.95f);

    wgpu::TextureView bloom;
    std::vector<gpu::TransientTexture> down;
    if (pyramidOn) {
        const std::uint32_t levels = std::clamp<std::uint32_t>(s.bloomLevels, 1, 8);
        std::uint32_t w = std::max(1u, in.width / 2);
        std::uint32_t h = std::max(1u, in.height / 2);
        for (std::uint32_t level = 0; level < levels && w >= 2 && h >= 2; ++level) {
            auto target = pool.acquire(w, h, kHdrFormat, pyramidUsage(), "bloom-down");
            Uniforms u = base;
            if (level == 0) {
                u.texelSize = 1.0f / base.outputSize;
                u.params0 = glm::vec4(s.bloomThreshold, s.bloomKnee, std::clamp(s.bloomEmissionWeight, 0.0f, 1.0f),
                                      in.emission ? 1.0f : 0.0f);
                // The exposure the scene has already been scaled by. The emission target is written
                // by the scene pass, *before* exposure; `source` here is after it. Without this the
                // ratio of the two is off by the exposure factor -- which at ev-2 suppresses the
                // glow on the very lights the mask exists to keep.
                u.params1 = glm::vec4(exposure, 0.0f, 0.0f, 0.0f);
                PassTextures textures;
                textures.source = current;
                textures.emission = in.emission;
                stage_ = "post/bloom";
                runPass(encoder, prefilter_, target.view, textures, u);
            } else {
                u.texelSize =
                    1.0f / glm::vec2(static_cast<float>(down.back().width), static_cast<float>(down.back().height));
                runPass(encoder, downsample_, target.view, down.back().view, nullptr, nullptr, u);
            }
            captureStage(level == 0 ? std::string("bloom/prefilter") : "bloom/down" + std::to_string(level), target);
            down.push_back(target);
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
        }
        stats_.bloomLevels = static_cast<std::uint32_t>(down.size());
        bloom = buildPyramid(encoder, pool, base, down, s.bloomRadius, blend, "bloom");
    }

    // Halation: its own, coarser pyramid over a warm-weighted threshold (ADR-039).
    wgpu::TextureView halation;
    if (halationOn) {
        const std::uint32_t levels = std::clamp<std::uint32_t>(s.bloomLevels, 1, 8);
        std::vector<gpu::TransientTexture> hdown;
        std::uint32_t w = std::max(1u, in.width / 4);
        std::uint32_t h = std::max(1u, in.height / 4);
        for (std::uint32_t level = 0; level < levels && w >= 2 && h >= 2; ++level) {
            auto target = pool.acquire(w, h, kHdrFormat, pyramidUsage(), "halation-down");
            Uniforms u = base;
            if (level == 0) {
                u.texelSize = 1.0f / base.outputSize;
                u.params0 = glm::vec4(s.halationThreshold, s.bloomKnee, std::clamp(s.halationWarmth, 0.0f, 1.0f), 0.0f);
                stage_ = "post/halation";
                runPass(encoder, halationPrefilter_, target.view, current, nullptr, nullptr, u);
            } else {
                u.texelSize =
                    1.0f / glm::vec2(static_cast<float>(hdown.back().width), static_cast<float>(hdown.back().height));
                runPass(encoder, downsample_, target.view, hdown.back().view, nullptr, nullptr, u);
            }
            captureStage(level == 0 ? std::string("halation/prefilter") : "halation/down" + std::to_string(level),
                         target);
            hdown.push_back(target);
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
        }
        stats_.halationLevels = static_cast<std::uint32_t>(hdown.size());
        halation = buildPyramid(encoder, pool, base, hdown, s.bloomRadius * s.halationRadius, blend, "halation");
    }

    // The wide tier: halation tinted and the anamorphic streak, in one texture the composite adds.
    wgpu::TextureView wide;
    if (halationOn || (anamorphicOn && bloom)) {
        // Quarter resolution: both tiers are low-frequency by construction.
        const std::uint32_t w = std::max(1u, in.width / 4);
        const std::uint32_t h = std::max(1u, in.height / 4);
        auto target = pool.acquire(w, h, kHdrFormat, pyramidUsage(), "wide");
        Uniforms u = base;
        u.texelSize = 1.0f / glm::vec2(static_cast<float>(w), static_cast<float>(h));

        // ---- the streak's sampling rate and its source (docs/post-artifact-forensics.md) --------
        //
        // The streak is a gaussian reaching `8 * stretch` texels of *this* target. Which tells you
        // nothing about how often to sample it, and the pass used to sample it eight times a side
        // whatever the reach -- one tap every `stretch` texels. At Glowmere's authored stretch of
        // 10.386 that is a sigma-33 gaussian sampled every ten texels, undersampled twentyfold, and
        // an undersampled filter is not a blur: it is a comb. Measured on an impulse, the pass
        // returned a row of copies whose period equalled `stretch` exactly across a fivefold sweep,
        // and that comb over the water's field of discrete sparkles is the reported lattice.
        //
        // Two numbers fix it, and both follow from one rule -- *no tap may step further than the
        // texel of the texture it reads*:
        //
        //   taps    enough of them that the spacing is a texel of the level below, bounded by a
        //           budget so a large stretch costs samples linearly rather than quadratically;
        //   source  the finest pyramid level whose texel is at least the tap spacing, so whatever
        //           the budget leaves unresolved has already been filtered away rather than
        //           aliased. The level is chosen from the reach, not fixed, so a short streak reads
        //           a fine level and only a long one reaches for a coarse one -- which is the real
        //           trade here: the coarser the level, the softer the streak is *vertically*, and
        //           an anamorphic streak that has gone soft in both axes is a blob. The budget is
        //           what buys the anisotropy back, and it is why it is as high as it is.
        //
        // Nothing about the streak's *shape* changes: the reach stays `8 * stretch` texels and the
        // gaussian's sigma stays `3.2 * stretch` texels, expressed below in the new tap units.
        const float stretch = std::max(s.anamorphicStretch, 1e-3f);
        const float reach = 8.0f * stretch; // in this target's texels, as before
        const auto taps = static_cast<std::uint32_t>(
            std::clamp(std::ceil(reach), 1.0f, static_cast<float>(kAnamorphicTapBudget)));
        const float spacing = reach / static_cast<float>(taps); // texels of this target
        // The finest level whose texel is at least `spacing`. `down` runs fine to coarse and its
        // level 0 is half the frame, so that level's texel is half a texel of this quarter-
        // resolution target. `bloom` -- the assembled pyramid, which is what this pass has always
        // read -- sits at that same finest resolution, so it is tried first and kept whenever the
        // sampling can support it. In practice a streak long enough to be worth switching on always
        // moves off it: spacing lands near one texel of this target and `bloom`'s is half that.
        // The margin is Nyquist's, stated plainly: reconstructing a texel needs two samples across
        // it, so the tap spacing has to be *half* a source texel and not a whole one. Sampling at
        // exactly one tap per texel still aliases, and measurably so -- at stretch 4 and 6 the
        // one-texel rule left 28 and 43 isolated peaks in the streak where the half-texel rule
        // leaves none.
        const float required = 2.0f * spacing;
        wgpu::TextureView streakSource = bloom;
        if (!down.empty() && static_cast<float>(w) / static_cast<float>(down.front().width) < required) {
            streakSource = down.back().view; // nothing coarse enough: the coarsest is the best there is
            for (const gpu::TransientTexture& level : down) {
                if (static_cast<float>(w) / static_cast<float>(level.width) >= required) {
                    streakSource = level.view;
                    break;
                }
            }
        }
        stats_.anamorphicTaps = taps;

        u.params0 = glm::vec4(spacing, std::clamp(s.anamorphicGhosts, 0.0f, 1.0f),
                              (anamorphicOn && bloom) ? 1.0f : 0.0f, halationOn ? 1.0f : 0.0f);
        // Tap count, and the gaussian's sigma expressed in taps: `3.2 * stretch` texels over
        // `spacing` texels per tap, which is 0.4 * taps and so is exactly 3.2 at the old eight.
        u.params1 = glm::vec4(static_cast<float>(taps), 0.4f * static_cast<float>(taps), 0.0f, 0.0f);
        u.tintA = glm::vec4(s.halationTint * s.halationIntensity, 0.0f);
        u.tintB = glm::vec4(s.anamorphicTint * s.anamorphicIntensity, 0.0f);
        PassTextures textures;
        textures.source = streakSource;
        textures.second = halation;
        stage_ = "post/anamorphic";
        runPass(encoder, wide_, target.view, textures, u);
        captureStage("wide", target);
        wide = target.view;
    }

    // ---- 6. composite: bloom + wide tier + colour grade -------------------------------------------
    {
        auto target = pool.acquire(in.width, in.height, kHdrFormat);
        Uniforms u = base;
        u.params0 = glm::vec4(s.bloomIntensity, (bloomOn && bloom) ? 1.0f : 0.0f, wide ? 1.0f : 0.0f, 0.0f);
        u.params1 = glm::vec4(s.contrast, s.saturation, s.temperature, s.tint);
        // ADR-038: the scene's depth bands grade contrast and saturation by distance. Without
        // them the count is zero and the grade is exactly the uniform one it has always been.
        std::uint32_t layerCount = 0;
        if (in.composition != nullptr) {
            for (const scene::DepthLayer& layer : in.composition->layers) {
                if (layerCount >= kMaxDepthLayers) {
                    break;
                }
                u.depthLayers[layerCount++] = glm::vec4(layer.start, layer.end, layer.contrast, layer.saturation);
            }
        }
        u.params2 = glm::vec4(s.hueShift, static_cast<float>(layerCount), 0.0f, 0.0f);
        PassTextures textures;
        textures.source = current;
        textures.second = bloom;
        textures.third = wide;
        textures.depth = in.depth; // the depth grade needs the real depth, not the placeholder
        stage_ = "post/composite";
        runPass(encoder, composite_, target.view, textures, u);
        captureStage("composite", target);
        current = target.view;
        output_ = target.texture;
    }

    // ---- 7. output: edge antialiasing (ADR-059) ---------------------------------------------------
    // Before sharpening, because sharpening an aliased edge fixes the contrast and keeps the stair
    // step; and inside the HDR chain, where the pass can still be skipped without a target copy.
    if (s.antialias > 1e-4f) {
        auto target = pool.acquire(in.width, in.height, kHdrFormat);
        Uniforms u = base;
        u.params0 = glm::vec4(std::clamp(s.antialias, 0.0f, 1.0f), 0.0f, 0.0f, 0.0f);
        PassTextures textures;
        textures.source = current;
        stage_ = "post/fxaa";
        runPass(encoder, fxaa_, target.view, textures, u);
        captureStage("fxaa", target);
        current = target.view;
        output_ = target.texture;
    }

    // ---- 8. output: sharpening (the tone map, vignette and grain follow in tonemap.wgsl) ----------
    if (s.sharpen > 1e-4f) {
        auto target = pool.acquire(in.width, in.height, kHdrFormat);
        Uniforms u = base;
        u.params0 = glm::vec4(s.sharpen, static_cast<float>(s.sharpenId), in.identifier ? 1.0f : 0.0f, 0.0f);
        PassTextures textures;
        textures.source = current;
        textures.identifier = in.identifier;
        stage_ = "post/sharpen";
        runPass(encoder, sharpen_, target.view, textures, u);
        captureStage("sharpen", target);
        current = target.view;
        output_ = target.texture;
    }
    return current;
}

} // namespace avgen::rendering
