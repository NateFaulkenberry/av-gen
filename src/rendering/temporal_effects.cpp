#include "rendering/temporal_effects.hpp"

#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/shader_library.hpp"

#include <algorithm>
#include <array>
#include <string>

namespace avgen::rendering {
namespace {

struct Uniforms {
    std::array<float, 4> sizes{};
    std::array<float, 4> outputSize{};
    std::array<float, 4> ring{};
    std::array<float, 4> params{};
};
static_assert(sizeof(Uniforms) == 64);

// The two ceilings, tied. `scene/` cannot include a WebGPU header, so the constant exists twice;
// this is the line that stops the copies drifting.
static_assert(static_cast<int>(kMaxTemporalFrames) == scene::kMaxTemporalFrames,
              "the authored history ceiling and the ring's must agree");

} // namespace

struct TemporalEffects::Impl {
    Impl(gpu::Context& ctx, gpu::ShaderLibrary& sh) : context(ctx), shaders(sh) {}

    gpu::Context& context;
    gpu::ShaderLibrary& shaders;
    gpu::FrameTimeline* timeline = nullptr;
    bool initialised = false;
    double lastMs = -1.0;
    bool passThisFrame = false;

    wgpu::Buffer uniforms;
    wgpu::Sampler sampler;
    wgpu::BindGroupLayout layout;
    wgpu::PipelineLayout pipelineLayout;
    wgpu::RenderPipeline echo;
    wgpu::RenderPipeline debugHistory;
    wgpu::TextureFormat echoFormat = wgpu::TextureFormat::Undefined;
    wgpu::TextureFormat debugFormat = wgpu::TextureFormat::Undefined;
    wgpu::ShaderModule module;
    // A 1x1 2D texture for the debug pass's `source` binding. The history-state view samples only
    // the ring, but binding 2 is declared `texture_2d<f32>` and a bind group must satisfy every
    // entry -- passing the ring's e2DArray view there invalidates the whole command buffer, not
    // just the pass, which is how one wrong binding blanked an entire frame.
    wgpu::Texture placeholder2dTexture;
    wgpu::TextureView placeholder2d;

    [[nodiscard]] Result<wgpu::RenderPipeline> makePipeline(const char* entry, wgpu::TextureFormat format,
                                                            const char* label);
    [[nodiscard]] wgpu::BindGroup makeGroup(const wgpu::TextureView& source, const wgpu::TextureView& history);
};

Result<wgpu::RenderPipeline> TemporalEffects::Impl::makePipeline(const char* entry, wgpu::TextureFormat format,
                                                                const char* label) {
    wgpu::ColorTargetState target{};
    target.format = format;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = entry;
    fragment.targetCount = 1;
    fragment.targets = &target;

    wgpu::RenderPipelineDescriptor desc{};
    desc.label = label;
    desc.layout = pipelineLayout;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_fullscreen";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.fragment = &fragment;

    context.device().PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::RenderPipeline pipeline = context.device().CreateRenderPipeline(&desc);
    std::string error;
    auto future = context.device().PopErrorScope(
        wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
            if (type != wgpu::ErrorType::NoError) {
                error = gpu::Context::toString(msg);
            }
        });
    context.waitFor(future);
    if (!error.empty() || pipeline == nullptr) {
        return fail("temporal pipeline '{}': {}", entry, error);
    }
    return pipeline;
}

wgpu::BindGroup TemporalEffects::Impl::makeGroup(const wgpu::TextureView& source, const wgpu::TextureView& history) {
    std::array<wgpu::BindGroupEntry, 4> entries{};
    entries[0].binding = 0;
    entries[0].buffer = uniforms;
    entries[0].size = sizeof(Uniforms);
    entries[1].binding = 1;
    entries[1].sampler = sampler;
    entries[2].binding = 2;
    entries[2].textureView = source;
    entries[3].binding = 3;
    entries[3].textureView = history;
    wgpu::BindGroupDescriptor desc{};
    desc.label = "temporal-effect-group";
    desc.layout = layout;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return context.device().CreateBindGroup(&desc);
}

TemporalEffects::TemporalEffects(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : impl_(std::make_unique<Impl>(context, shaders)),
      history_(std::make_unique<TemporalHistory>(context, shaders)) {}

TemporalEffects::~TemporalEffects() = default;

Result<void> TemporalEffects::init() {
    Impl& im = *impl_;
    const auto& device = im.context.device();
    if (auto r = history_->init(); !r) return r;
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "temporal-effect-uniforms";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = sizeof(Uniforms);
        im.uniforms = device.CreateBuffer(&desc);
    }
    {
        wgpu::SamplerDescriptor desc{};
        desc.label = "temporal-effect-sampler";
        desc.minFilter = wgpu::FilterMode::Linear;
        desc.magFilter = wgpu::FilterMode::Linear;
        desc.addressModeU = wgpu::AddressMode::ClampToEdge;
        desc.addressModeV = wgpu::AddressMode::ClampToEdge;
        im.sampler = device.CreateSampler(&desc);
    }
    {
        std::array<wgpu::BindGroupLayoutEntry, 4> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.minBindingSize = sizeof(Uniforms);
        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Fragment;
        entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;
        entries[2].binding = 2;
        entries[2].visibility = wgpu::ShaderStage::Fragment;
        entries[2].texture.sampleType = wgpu::TextureSampleType::Float;
        entries[2].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        entries[3].binding = 3;
        entries[3].visibility = wgpu::ShaderStage::Fragment;
        entries[3].texture.sampleType = wgpu::TextureSampleType::Float;
        entries[3].texture.viewDimension = wgpu::TextureViewDimension::e2DArray;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "temporal-effect-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        im.layout = device.CreateBindGroupLayout(&desc);
    }
    {
        wgpu::PipelineLayoutDescriptor desc{};
        desc.label = "temporal-effect-pipeline-layout";
        desc.bindGroupLayoutCount = 1;
        desc.bindGroupLayouts = &im.layout;
        im.pipelineLayout = device.CreatePipelineLayout(&desc);
    }
    {
        wgpu::TextureDescriptor desc{};
        desc.label = "temporal-effect-placeholder-2d";
        desc.usage = wgpu::TextureUsage::TextureBinding;
        desc.dimension = wgpu::TextureDimension::e2D;
        desc.size = {1, 1, 1};
        desc.format = wgpu::TextureFormat::RGBA16Float;
        im.placeholder2dTexture = device.CreateTexture(&desc);
        if (im.placeholder2dTexture == nullptr) {
            return fail("temporal 2d placeholder");
        }
        im.placeholder2d = im.placeholder2dTexture.CreateView();
    }
    if (auto r = reload(); !r) return r;
    im.initialised = true;
    return {};
}

Result<void> TemporalEffects::reload() {
    Impl& im = *impl_;
    if (auto r = history_->reload(); !r) return r;
    auto module = im.shaders.load("temporal.wgsl");
    if (!module) return std::unexpected(module.error());
    im.module = *module;
    // The pipelines are format-dependent and built lazily in run(); dropping them here makes the
    // next frame rebuild against the new module rather than keep the old code silently.
    im.echo = nullptr;
    im.debugHistory = nullptr;
    im.echoFormat = wgpu::TextureFormat::Undefined;
    im.debugFormat = wgpu::TextureFormat::Undefined;
    return {};
}

wgpu::TextureView TemporalEffects::run(wgpu::CommandEncoder& encoder, const TemporalFrameInputs& in,
                                       gpu::TransientPool& pool) {
    Impl& im = *impl_;
    collectTimings();
    im.passThisFrame = false;
    stats_ = TemporalStats{};
    if (!im.initialised || in.sceneHdr == nullptr || in.width == 0 || in.height == 0) {
        return in.sceneHdr;
    }
    const scene::TemporalSettings settings = in.settings != nullptr ? *in.settings : scene::TemporalSettings{};

    // The bound, taken from the effects themselves. This is the single number that sizes the ring,
    // sizes a seek warm-up and drives the settling badge.
    const std::uint32_t needed = settings.historyFrames();
    history_->setFramesNeeded(needed);

    TemporalHistoryConfig config{};
    config.resolutionScale = in.resolutionScale;
    config.frames[static_cast<std::uint32_t>(TemporalChannel::Colour)] = needed;
    if (auto r = history_->configure(in.width, in.height, config); !r) {
        // An allocation failure disables the family for the frame rather than dropping the frame.
        stats_.stalled = true;
        return in.sceneHdr;
    }
    history_->beginFrame(in.frameIndex);

    const auto& state = history_->state();
    stats_.framesValid = state.framesValid;
    stats_.framesNeeded = state.framesNeeded;
    stats_.settling = state.settling();
    stats_.stalled = state.stalled;
    stats_.historyBytes = history_->bytes();
    stats_.historyWidth = history_->width();
    stats_.historyHeight = history_->height();

    if (!history_->active()) {
        return in.sceneHdr;
    }

    wgpu::TextureView result = in.sceneHdr;

    // ---- §9 frame echo ---------------------------------------------------------------------
    //
    // Runs BEFORE the capture, and the order is load-bearing. The ring's write cursor points at
    // the slot this frame is about to occupy, so `writeLayer - 1` is genuinely the previous frame.
    // Capturing first would make tap 1 the current frame and the echo would double-expose it --
    // a subtle brightening that looks like a strength setting rather than an off-by-one.
    if (settings.echo.enabled) {
        // Clamped to what actually exists. A settling ring gives a shorter echo; it never reads a
        // layer still holding a frame from before the seek, which would put pre-seek geometry into
        // a post-seek frame -- the exact silent-divergence failure this design exists to prevent.
        const std::uint32_t taps = std::min<std::uint32_t>(static_cast<std::uint32_t>(settings.echo.frames),
                                                           state.framesValid);
        if (taps > 0) {
            if (im.echo == nullptr || im.echoFormat != in.hdrFormat) {
                auto pipeline = im.makePipeline("fs_echo", in.hdrFormat, "temporal-echo");
                if (!pipeline) {
                    return in.sceneHdr;
                }
                im.echo = *pipeline;
                im.echoFormat = in.hdrFormat;
            }
            auto out = pool.acquire(in.width, in.height, in.hdrFormat);
            Uniforms u{};
            u.sizes = {static_cast<float>(history_->width()), static_cast<float>(history_->height()),
                       1.0f / static_cast<float>(history_->width()), 1.0f / static_cast<float>(history_->height())};
            u.outputSize = {static_cast<float>(in.width), static_cast<float>(in.height),
                            1.0f / static_cast<float>(in.width), 1.0f / static_cast<float>(in.height)};
            u.ring = {static_cast<float>(history_->frames(TemporalChannel::Colour)),
                      static_cast<float>(history_->writeLayer(TemporalChannel::Colour)),
                      static_cast<float>(state.framesValid), static_cast<float>(taps)};
            u.params = {settings.echo.strength, settings.echo.decay, 0.0f, 0.0f};
            im.context.queue().WriteBuffer(im.uniforms, 0, &u, sizeof(u));

            wgpu::BindGroup group = im.makeGroup(in.sceneHdr, history_->arrayView(TemporalChannel::Colour));
            wgpu::RenderPassColorAttachment colour{};
            colour.view = out.view;
            colour.loadOp = wgpu::LoadOp::Clear;
            colour.storeOp = wgpu::StoreOp::Store;
            colour.clearValue = {0.0, 0.0, 0.0, 1.0};
            wgpu::RenderPassDescriptor pass{};
            pass.label = "temporal-echo";
            pass.colorAttachmentCount = 1;
            pass.colorAttachments = &colour;
            if (im.timeline != nullptr) {
                pass.timestampWrites = im.timeline->mark("temporal", gpu::FrameTimeline::PassKind::Render);
            }
            wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
            rp.SetPipeline(im.echo);
            rp.SetBindGroup(0, group);
            rp.Draw(3);
            rp.End();
            result = out.view;
            ++stats_.passes;
            im.passThisFrame = true;
        }
    }

    // ---- capture: the clean scene radiance, never the effect's own output --------------------
    history_->setTimeline(im.timeline);
    history_->encodeCapture(encoder, in.sceneHdr, in.velocity);
    ++stats_.passes;
    im.passThisFrame = true;
    stats_.framesValid = history_->state().framesValid;
    return result;
}

void TemporalEffects::encodeDebugView(wgpu::CommandEncoder& encoder, const wgpu::TextureView& target,
                                      std::uint32_t width, std::uint32_t height, wgpu::TextureFormat format) {
    Impl& im = *impl_;
    if (!im.initialised || target == nullptr) {
        return;
    }
    if (im.debugHistory == nullptr || im.debugFormat != format) {
        auto pipeline = im.makePipeline("fs_debug_history", format, "temporal-debug-history");
        if (!pipeline) {
            return;
        }
        im.debugHistory = *pipeline;
        im.debugFormat = format;
    }
    const auto& state = history_->state();
    const std::uint32_t depth = std::max(history_->frames(TemporalChannel::Colour), 1u);
    Uniforms u{};
    u.sizes = {static_cast<float>(std::max(history_->width(), 1u)), static_cast<float>(std::max(history_->height(), 1u)),
               0.0f, 0.0f};
    u.outputSize = {static_cast<float>(width), static_cast<float>(height), 1.0f / static_cast<float>(std::max(width, 1u)),
                    1.0f / static_cast<float>(std::max(height, 1u))};
    u.ring = {static_cast<float>(depth), static_cast<float>(history_->writeLayer(TemporalChannel::Colour)),
              static_cast<float>(state.framesValid), 0.0f};
    im.context.queue().WriteBuffer(im.uniforms, 0, &u, sizeof(u));

    wgpu::BindGroup group = im.makeGroup(im.placeholder2d, history_->arrayView(TemporalChannel::Colour));
    wgpu::RenderPassColorAttachment colour{};
    colour.view = target;
    colour.loadOp = wgpu::LoadOp::Clear;
    colour.storeOp = wgpu::StoreOp::Store;
    colour.clearValue = {0.0, 0.0, 0.0, 1.0};
    wgpu::RenderPassDescriptor pass{};
    pass.label = "temporal-debug-history";
    pass.colorAttachmentCount = 1;
    pass.colorAttachments = &colour;
    wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
    rp.SetPipeline(im.debugHistory);
    rp.SetBindGroup(0, group);
    rp.Draw(3);
    rp.End();
}

void TemporalEffects::reset() { history_->reset(); }

void TemporalEffects::setTimeline(gpu::FrameTimeline* timeline) {
    impl_->timeline = timeline;
    history_->setTimeline(timeline);
}

void TemporalEffects::collectTimings() {
    Impl& im = *impl_;
    if (im.timeline != nullptr) {
        const double ms = im.timeline->msFor("temporal");
        if (ms >= 0.0) {
            im.lastMs = ms;
        }
    }
    stats_.temporalMs = im.passThisFrame ? im.lastMs : -1.0;
}

} // namespace avgen::rendering
