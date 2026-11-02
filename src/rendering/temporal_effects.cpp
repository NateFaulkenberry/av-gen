#include "rendering/temporal_effects.hpp"
#include "gpu/resource_stats.hpp"

#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/shader_library.hpp"
#include "scene/scene_types.hpp"

#include <algorithm>
#include <cmath>
#include <array>
#include <string>

#include <glm/glm.hpp>

namespace avgen::rendering {
namespace {

struct Uniforms {
    std::array<float, 4> sizes{};
    std::array<float, 4> outputSize{};
    std::array<float, 4> ring{};
    std::array<float, 4> params{};
    std::array<float, 4> extra{}; // ADR-1049: x = the mosh's epoch
    std::array<float, 4> more{};  // ADR-1931: the sampler's stride, blend, reverse, scan switch
};
static_assert(sizeof(Uniforms) == 96);

// ADR-1934: the gate's per-slot table, mirrored by `GateUniforms` in temporal.wgsl.
struct GateUniforms {
    std::array<float, 4> header{}; // layers the trail scan may visit, the fracture's epoch, span, 0
    std::array<std::array<float, 4>, 48> slots{}; // three per slot (ADR-1936 added the third)
};
static_assert(sizeof(GateUniforms) == 784);
static_assert(scene::SlitSettings::kSlots * 3 == 48);
// The identifier's low 16 bits index the slot table, four 8-bit slots to a word.
constexpr std::uint32_t kSlotTableWords = 65536 / 4;
// The fracture re-chooses its moments this many times a second (a function of the render time, so scrub-safe).
constexpr double kFractureRate = 6.0;

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
    wgpu::RenderPipeline mosh; // ADR-1049
    wgpu::Buffer moshUniforms; // its own buffer: one queue write per buffer per submit is what lands
    wgpu::RenderPipeline feedback; // ADR-1066
    wgpu::RenderPipeline slit;     // ADR-1066
    wgpu::Buffer feedbackUniforms;
    wgpu::Buffer slitUniforms;
    wgpu::TextureFormat feedbackFormat = wgpu::TextureFormat::Undefined;
    wgpu::TextureFormat slitFormat = wgpu::TextureFormat::Undefined;
    wgpu::RenderPipeline debugHistory;
    wgpu::TextureFormat echoFormat = wgpu::TextureFormat::Undefined;
    wgpu::TextureFormat moshFormat = wgpu::TextureFormat::Undefined;
    wgpu::TextureFormat debugFormat = wgpu::TextureFormat::Undefined;
    wgpu::ShaderModule module;
    // A 1x1 2D texture for the debug pass's `source` binding. The history-state view samples only
    // the ring, but binding 2 is declared `texture_2d<f32>` and a bind group must satisfy every
    // entry -- passing the ring's e2DArray view there invalidates the whole command buffer, not
    // just the pass, which is how one wrong binding blanked an entire frame.
    wgpu::Texture placeholder2dTexture;
    wgpu::TextureView placeholder2d;

    // ADR-1934: the identity gate. Its slot ring has the colour ring's size and depth and is written at the same layer
    // in the same frames, so one write cursor and one validity count serve both.
    wgpu::Texture slotRing;
    wgpu::TextureView slotRingArray;
    std::vector<wgpu::TextureView> slotLayerViews;
    std::uint32_t slotW = 0, slotH = 0, slotLayers = 0;
    wgpu::Buffer slotTable;
    std::vector<std::uint32_t> slotTableShadow;
    wgpu::Buffer gateUniforms;
    wgpu::Buffer slotCaptureUniforms;
    wgpu::BindGroupLayout gatedLayout;
    wgpu::PipelineLayout gatedPipelineLayout;
    wgpu::BindGroupLayout slotCaptureLayout;
    wgpu::PipelineLayout slotCapturePipelineLayout;
    wgpu::RenderPipeline gated;
    wgpu::TextureFormat gatedFormat = wgpu::TextureFormat::Undefined;
    wgpu::RenderPipeline slotCapture;
    wgpu::Texture placeholderIdTexture; // a 1x1 R32Uint, when the host gave no identifier
    wgpu::TextureView placeholderId;

    [[nodiscard]] Result<wgpu::RenderPipeline> makeGatePipeline(const char* entry, wgpu::TextureFormat format,
                                                                const wgpu::PipelineLayout& layout, const char* label);
    [[nodiscard]] bool ensureSlotRing(std::uint32_t w, std::uint32_t h, std::uint32_t layers);
    void uploadSlotTable(const std::vector<std::uint8_t>* entitySlots);

    [[nodiscard]] Result<wgpu::RenderPipeline> makePipeline(const char* entry, wgpu::TextureFormat format,
                                                            const char* label);
    [[nodiscard]] wgpu::BindGroup makeGroup(const wgpu::TextureView& source, const wgpu::TextureView& history,
                                            const wgpu::Buffer* buffer = nullptr);
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
    wgpu::RenderPipeline pipeline = gpu::createRenderPipeline(context.device(), &desc);
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

Result<wgpu::RenderPipeline> TemporalEffects::Impl::makeGatePipeline(const char* entry, wgpu::TextureFormat format,
                                                                    const wgpu::PipelineLayout& layout,
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
    desc.layout = layout;
    desc.vertex.module = module;
    desc.vertex.entryPoint = "vs_fullscreen";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.fragment = &fragment;
    context.device().PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::RenderPipeline pipeline = gpu::createRenderPipeline(context.device(), &desc);
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

bool TemporalEffects::Impl::ensureSlotRing(std::uint32_t w, std::uint32_t h, std::uint32_t layers) {
    if (slotRing != nullptr && slotW == w && slotH == h && slotLayers == layers) {
        return true;
    }
    slotRing = nullptr;
    slotRingArray = nullptr;
    slotLayerViews.clear();
    slotW = slotH = slotLayers = 0;
    if (w == 0 || h == 0 || layers == 0) {
        return false;
    }
    wgpu::TextureDescriptor desc{};
    desc.label = "temporal-slot-ring";
    desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment;
    desc.dimension = wgpu::TextureDimension::e2D;
    desc.size = {w, h, layers};
    desc.format = wgpu::TextureFormat::R8Uint;
    slotRing = context.device().CreateTexture(&desc);
    if (slotRing == nullptr) {
        return false;
    }
    wgpu::TextureViewDescriptor av{};
    av.dimension = wgpu::TextureViewDimension::e2DArray;
    av.baseArrayLayer = 0;
    av.arrayLayerCount = layers;
    slotRingArray = slotRing.CreateView(&av);
    slotLayerViews.reserve(layers);
    for (std::uint32_t i = 0; i < layers; ++i) {
        wgpu::TextureViewDescriptor lv{};
        lv.dimension = wgpu::TextureViewDimension::e2D;
        lv.baseArrayLayer = i;
        lv.arrayLayerCount = 1;
        slotLayerViews.push_back(slotRing.CreateView(&lv));
    }
    slotW = w;
    slotH = h;
    slotLayers = layers;
    return true;
}

void TemporalEffects::Impl::uploadSlotTable(const std::vector<std::uint8_t>* entitySlots) {
    std::vector<std::uint32_t> words(kSlotTableWords, 0u);
    if (entitySlots != nullptr) {
        // Entity space only (the pick id's tag 0): index i is identifier i. Entity 0 shares identifier 0 with the
        // cleared background, so it is never gated.
        const std::size_t n = std::min<std::size_t>(entitySlots->size(), scene::kPickMaxIndex);
        for (std::size_t i = 1; i < n; ++i) {
            const std::uint32_t slot = std::min<std::uint32_t>((*entitySlots)[i], 16u);
            const std::uint32_t id = scene::packPickId(scene::PickSpace::Entity, i) & 0xFFFFu;
            words[id >> 2u] |= slot << ((id & 3u) * 8u);
        }
    }
    if (words != slotTableShadow) {
        context.queue().WriteBuffer(slotTable, 0, words.data(), words.size() * sizeof(std::uint32_t));
        slotTableShadow = std::move(words);
    }
}

wgpu::BindGroup TemporalEffects::Impl::makeGroup(const wgpu::TextureView& source, const wgpu::TextureView& history,
                                                 const wgpu::Buffer* buffer) {
    std::array<wgpu::BindGroupEntry, 4> entries{};
    entries[0].binding = 0;
    entries[0].buffer = buffer != nullptr ? *buffer : uniforms;
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
        desc.label = "temporal-mosh-uniforms";
        im.moshUniforms = device.CreateBuffer(&desc);
        desc.label = "temporal-feedback-uniforms";
        im.feedbackUniforms = device.CreateBuffer(&desc);
        desc.label = "temporal-slit-uniforms";
        im.slitUniforms = device.CreateBuffer(&desc);
        desc.label = "temporal-slot-capture-uniforms"; // ADR-1934
        im.slotCaptureUniforms = device.CreateBuffer(&desc);
        desc.label = "temporal-gate-uniforms";
        desc.size = sizeof(GateUniforms);
        im.gateUniforms = device.CreateBuffer(&desc);
        desc.label = "temporal-slot-table";
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        desc.size = kSlotTableWords * sizeof(std::uint32_t);
        im.slotTable = device.CreateBuffer(&desc);
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
    // ADR-1934: the gate's two layouts. The capture writes a slot layer, so it cannot also bind the slot ring.
    {
        const auto uniformEntry = [](std::uint32_t binding, std::uint64_t size) {
            wgpu::BindGroupLayoutEntry e{};
            e.binding = binding;
            e.visibility = wgpu::ShaderStage::Fragment;
            e.buffer.type = wgpu::BufferBindingType::Uniform;
            e.buffer.minBindingSize = size;
            return e;
        };
        const auto uintTexture = [](std::uint32_t binding, wgpu::TextureViewDimension dim) {
            wgpu::BindGroupLayoutEntry e{};
            e.binding = binding;
            e.visibility = wgpu::ShaderStage::Fragment;
            e.texture.sampleType = wgpu::TextureSampleType::Uint;
            e.texture.viewDimension = dim;
            return e;
        };
        wgpu::BindGroupLayoutEntry table{};
        table.binding = 6;
        table.visibility = wgpu::ShaderStage::Fragment;
        table.buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        table.buffer.minBindingSize = kSlotTableWords * sizeof(std::uint32_t);
        {
            std::array<wgpu::BindGroupLayoutEntry, 3> entries{uniformEntry(0, sizeof(Uniforms)),
                                                              uintTexture(5, wgpu::TextureViewDimension::e2D), table};
            wgpu::BindGroupLayoutDescriptor desc{};
            desc.label = "temporal-slot-capture-layout";
            desc.entryCount = entries.size();
            desc.entries = entries.data();
            im.slotCaptureLayout = device.CreateBindGroupLayout(&desc);
            wgpu::PipelineLayoutDescriptor pl{};
            pl.label = "temporal-slot-capture-pipeline-layout";
            pl.bindGroupLayoutCount = 1;
            pl.bindGroupLayouts = &im.slotCaptureLayout;
            im.slotCapturePipelineLayout = device.CreatePipelineLayout(&pl);
        }
        {
            wgpu::BindGroupLayoutEntry samplerEntry{};
            samplerEntry.binding = 1;
            samplerEntry.visibility = wgpu::ShaderStage::Fragment;
            samplerEntry.sampler.type = wgpu::SamplerBindingType::Filtering;
            wgpu::BindGroupLayoutEntry sourceEntry{};
            sourceEntry.binding = 2;
            sourceEntry.visibility = wgpu::ShaderStage::Fragment;
            sourceEntry.texture.sampleType = wgpu::TextureSampleType::Float;
            sourceEntry.texture.viewDimension = wgpu::TextureViewDimension::e2D;
            wgpu::BindGroupLayoutEntry historyEntry{};
            historyEntry.binding = 3;
            historyEntry.visibility = wgpu::ShaderStage::Fragment;
            historyEntry.texture.sampleType = wgpu::TextureSampleType::Float;
            historyEntry.texture.viewDimension = wgpu::TextureViewDimension::e2DArray;
            std::array<wgpu::BindGroupLayoutEntry, 8> entries{uniformEntry(0, sizeof(Uniforms)),
                                                              samplerEntry,
                                                              sourceEntry,
                                                              historyEntry,
                                                              uintTexture(4, wgpu::TextureViewDimension::e2DArray),
                                                              uintTexture(5, wgpu::TextureViewDimension::e2D),
                                                              table,
                                                              uniformEntry(7, sizeof(GateUniforms))};
            wgpu::BindGroupLayoutDescriptor desc{};
            desc.label = "temporal-gated-layout";
            desc.entryCount = entries.size();
            desc.entries = entries.data();
            im.gatedLayout = device.CreateBindGroupLayout(&desc);
            wgpu::PipelineLayoutDescriptor pl{};
            pl.label = "temporal-gated-pipeline-layout";
            pl.bindGroupLayoutCount = 1;
            pl.bindGroupLayouts = &im.gatedLayout;
            im.gatedPipelineLayout = device.CreatePipelineLayout(&pl);
        }
        wgpu::TextureDescriptor desc{};
        desc.label = "temporal-placeholder-id";
        desc.usage = wgpu::TextureUsage::TextureBinding;
        desc.dimension = wgpu::TextureDimension::e2D;
        desc.size = {1, 1, 1};
        desc.format = wgpu::TextureFormat::R32Uint;
        im.placeholderIdTexture = device.CreateTexture(&desc);
        if (im.placeholderIdTexture == nullptr) {
            return fail("temporal id placeholder");
        }
        im.placeholderId = im.placeholderIdTexture.CreateView();
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
    im.mosh = nullptr;
    im.feedback = nullptr;
    im.slit = nullptr;
    im.gated = nullptr; // ADR-1934
    im.gatedFormat = wgpu::TextureFormat::Undefined;
    im.slotCapture = nullptr;
    im.feedbackFormat = wgpu::TextureFormat::Undefined;
    im.slitFormat = wgpu::TextureFormat::Undefined;
    im.debugHistory = nullptr;
    im.echoFormat = wgpu::TextureFormat::Undefined;
    im.moshFormat = wgpu::TextureFormat::Undefined;
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
    // ADR-1931: the ring's layers are the settings' `ringLayers()` -- a fixed capacity when the scene sets one, so a
    // routed span never reallocates. Not a tier setting: how far back an effect reaches is an artistic control, and the
    // tier scales only the ring's resolution (`temporalHistoryScale`). The needed count, in layers at the stride,
    // drives the badge.
    const std::uint32_t needed = settings.historyFrames();
    const std::uint32_t stride = settings.captureStride();
    const std::uint32_t layers = settings.ringLayers();
    history_->setFramesNeeded(std::min((needed + stride - 1) / stride, layers));

    TemporalHistoryConfig config{};
    config.resolutionScale = in.resolutionScale;
    config.stride = stride;
    config.frames[static_cast<std::uint32_t>(TemporalChannel::Colour)] = layers;
    if (auto r = history_->configure(in.width, in.height, config); !r) {
        // An allocation failure disables the family for the frame rather than dropping the frame.
        stats_.stalled = true;
        return in.sceneHdr;
    }
    history_->beginFrame(in.frameIndex, in.renderTime, in.haveCaptureFrame ? in.captureFrame : in.frameIndex);

    const auto& state = history_->state();
    stats_.framesValid = state.framesValid;
    stats_.framesNeeded = state.framesNeeded;
    stats_.settling = state.settling();
    stats_.stalled = state.stalled;
    stats_.historyBytes = history_->bytes();
    stats_.historyWidth = history_->width();
    stats_.historyHeight = history_->height();
    stats_.historyLayers = history_->frames(TemporalChannel::Colour); // ADR-1931
    stats_.historyStride = history_->stride();
    stats_.historyWriteLayer = history_->writeLayer(TemporalChannel::Colour);
    stats_.historyAllocations = history_->allocations();

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
                pass.timestampWrites = im.timeline->mark("temporal/echo", gpu::FrameTimeline::PassKind::Render);
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

    // ---- ADR-1066 feedback and slit-scan: FIR over the clean ring, before the mosh (which corrupts what they show)
    // and before the capture. Nothing is encoded at amount 0: the ring stays warm for the event that turns them up.
    const auto ringPass = [&](const char* entry, const char* label, const char* mark, wgpu::RenderPipeline& pipeline,
                              wgpu::TextureFormat& format, wgpu::Buffer& buffer, const Uniforms& u) -> bool {
        if (pipeline == nullptr || format != in.hdrFormat) {
            auto made = im.makePipeline(entry, in.hdrFormat, label);
            if (!made) {
                return false;
            }
            pipeline = *made;
            format = in.hdrFormat;
        }
        auto out = pool.acquire(in.width, in.height, in.hdrFormat);
        im.context.queue().WriteBuffer(buffer, 0, &u, sizeof(u));
        wgpu::BindGroup group = im.makeGroup(result, history_->arrayView(TemporalChannel::Colour), &buffer);
        wgpu::RenderPassColorAttachment colour{};
        colour.view = out.view;
        colour.loadOp = wgpu::LoadOp::Clear;
        colour.storeOp = wgpu::StoreOp::Store;
        colour.clearValue = {0.0, 0.0, 0.0, 1.0};
        wgpu::RenderPassDescriptor pass{};
        pass.label = label;
        pass.colorAttachmentCount = 1;
        pass.colorAttachments = &colour;
        if (im.timeline != nullptr) {
            pass.timestampWrites = im.timeline->mark(mark, gpu::FrameTimeline::PassKind::Render);
        }
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        rp.SetPipeline(pipeline);
        rp.SetBindGroup(0, group);
        rp.Draw(3);
        rp.End();
        result = out.view;
        ++stats_.passes;
        im.passThisFrame = true;
        return true;
    };
    const auto ringUniforms = [&](std::uint32_t taps) {
        Uniforms u{};
        u.sizes = {static_cast<float>(history_->width()), static_cast<float>(history_->height()),
                   1.0f / static_cast<float>(history_->width()), 1.0f / static_cast<float>(history_->height())};
        u.outputSize = {static_cast<float>(in.width), static_cast<float>(in.height),
                        1.0f / static_cast<float>(in.width), 1.0f / static_cast<float>(in.height)};
        u.ring = {static_cast<float>(history_->frames(TemporalChannel::Colour)),
                  static_cast<float>(history_->writeLayer(TemporalChannel::Colour)),
                  static_cast<float>(state.framesValid), static_cast<float>(taps)};
        return u;
    };
    const scene::FeedbackSettings& fb = settings.feedback;
    if (fb.enabled && fb.amount > 0.0f) {
        const std::uint32_t taps =
            std::min<std::uint32_t>(static_cast<std::uint32_t>(std::max(fb.frames, 1)), state.framesValid);
        if (taps > 0) {
            Uniforms u = ringUniforms(taps);
            u.params = {fb.amount, std::clamp(fb.decay, 0.0f, 0.99f), std::max(fb.zoom, 1e-3f), glm::radians(fb.rotate)};
            u.extra = {fb.driftX, fb.driftY, fb.hue,
                       static_cast<float>(in.width) / static_cast<float>(std::max(in.height, 1u))};
            if (!ringPass("fs_feedback", "temporal-feedback", "temporal/feedback", im.feedback, im.feedbackFormat,
                          im.feedbackUniforms, u)) {
                return result;
            }
        }
    }
    // ---- ADR-1931: the sampler. Each pixel's age comes from the slit map (direction, radial or luminance; strips;
    // a scan that may move); the stored frames are addressed by age with the stride and the newest layer's age, so a
    // span of any length costs two fetches. FIR over the clean ring like the rest.
    const scene::SlitSettings& sl = settings.slit;
    // ADR-1934: the identity gate. Its slot ring follows the colour ring's size and depth; the table is re-sent only
    // when an entity's slot changes.
    const bool gateOn = sl.enabled && sl.gate > 0.5f;
    const wgpu::TextureView idView = in.identifier != nullptr ? in.identifier : im.placeholderId;
    if (gateOn && im.ensureSlotRing(history_->width(), history_->height(), history_->frames(TemporalChannel::Colour))) {
        im.uploadSlotTable(in.entitySlots);
        stats_.slotRingBytes = static_cast<std::uint64_t>(im.slotW) * im.slotH * im.slotLayers;
    } else if (!gateOn && im.slotRing != nullptr) {
        im.ensureSlotRing(0, 0, 0); // released with the gate, like the ring with its effects
    }
    if (gateOn && im.slotRing != nullptr && sl.amount > 0.0f && state.framesValid > 0 && history_->newestAge() > 0) {
        if (im.gated == nullptr || im.gatedFormat != in.hdrFormat) {
            auto made = im.makeGatePipeline("fs_gated", in.hdrFormat, im.gatedPipelineLayout, "temporal-gated");
            if (!made) {
                return result;
            }
            im.gated = *made;
            im.gatedFormat = in.hdrFormat;
        }
        Uniforms u = ringUniforms(state.framesValid);
        u.ring[3] = static_cast<float>(history_->newestAge());
        u.params = {std::clamp(sl.amount, 0.0f, 1.0f), std::max(sl.span, 0.0f), 0.0f, 0.0f};
        u.more = {static_cast<float>(history_->stride()), 1.0f, 0.0f, 0.0f};
        GateUniforms g{};
        float maxTrail = 0.0f;
        for (std::size_t k = 0; k < sl.slots.size(); ++k) {
            const scene::SlitSettings::Slot& s = sl.slots[k];
            const float a = glm::radians(s.angle);
            const bool live = s.trail > 0.0f || s.fracture > 0.0f || s.rgb > 0.0f;
            g.slots[k * 3] = {std::max(s.trail, 0.0f), std::max(s.echo, 0.0f), std::clamp(s.fade, 0.0f, 1.0f),
                              std::clamp(s.fracture, 0.0f, 1.0f)};
            g.slots[k * 3 + 1] = {std::max(std::round(s.strips), 1.0f), std::cos(a), std::sin(a), live ? 1.0f : 0.0f};
            g.slots[k * 3 + 2] = {std::clamp(s.rgb, 0.0f, std::max(sl.span, 0.0f) * 0.5f), 0.0f, 0.0f, 0.0f}; // ADR-1936
            maxTrail = std::max(maxTrail, std::min(s.trail, sl.span));
        }
        // The trail scan visits only the layers the longest live trail can reach.
        const float newest = static_cast<float>(history_->newestAge());
        const float stride = static_cast<float>(history_->stride());
        const float scan = maxTrail >= newest ? std::floor((maxTrail - newest) / stride) + 1.0f : 0.0f;
        g.header = {scan, static_cast<float>(std::fmod(std::floor(in.renderTime * kFractureRate), 8388608.0)),
                    std::max(sl.span, 0.0f), 0.0f};
        im.context.queue().WriteBuffer(im.slitUniforms, 0, &u, sizeof(u));
        im.context.queue().WriteBuffer(im.gateUniforms, 0, &g, sizeof(g));
        std::array<wgpu::BindGroupEntry, 8> entries{};
        entries[0].binding = 0;
        entries[0].buffer = im.slitUniforms;
        entries[0].size = sizeof(Uniforms);
        entries[1].binding = 1;
        entries[1].sampler = im.sampler;
        entries[2].binding = 2;
        entries[2].textureView = result;
        entries[3].binding = 3;
        entries[3].textureView = history_->arrayView(TemporalChannel::Colour);
        entries[4].binding = 4;
        entries[4].textureView = im.slotRingArray;
        entries[5].binding = 5;
        entries[5].textureView = idView;
        entries[6].binding = 6;
        entries[6].buffer = im.slotTable;
        entries[6].size = kSlotTableWords * sizeof(std::uint32_t);
        entries[7].binding = 7;
        entries[7].buffer = im.gateUniforms;
        entries[7].size = sizeof(GateUniforms);
        wgpu::BindGroupDescriptor gd{};
        gd.label = "temporal-gated-group";
        gd.layout = im.gatedLayout;
        gd.entryCount = entries.size();
        gd.entries = entries.data();
        wgpu::BindGroup group = im.context.device().CreateBindGroup(&gd);
        auto out = pool.acquire(in.width, in.height, in.hdrFormat);
        wgpu::RenderPassColorAttachment colour{};
        colour.view = out.view;
        colour.loadOp = wgpu::LoadOp::Clear;
        colour.storeOp = wgpu::StoreOp::Store;
        colour.clearValue = {0.0, 0.0, 0.0, 1.0};
        wgpu::RenderPassDescriptor pass{};
        pass.label = "temporal-gated";
        pass.colorAttachmentCount = 1;
        pass.colorAttachments = &colour;
        if (im.timeline != nullptr) {
            pass.timestampWrites = im.timeline->mark("temporal/gated", gpu::FrameTimeline::PassKind::Render);
        }
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        rp.SetPipeline(im.gated);
        rp.SetBindGroup(0, group);
        rp.Draw(3);
        rp.End();
        result = out.view;
        ++stats_.passes;
        stats_.gated = true;
        im.passThisFrame = true;
    } else if (!gateOn && sl.enabled && sl.amount > 0.0f && state.framesValid > 0 && history_->newestAge() > 0) {
        Uniforms u = ringUniforms(state.framesValid);
        u.ring[3] = static_cast<float>(history_->newestAge());
        u.params = {std::clamp(sl.amount, 0.0f, 1.0f), std::max(sl.span, 0.0f), std::max(std::round(sl.strips), 0.0f),
                    std::clamp(sl.edge, 0.0f, 1.0f)};
        const float angle = glm::radians(sl.angle);
        const bool scanning = sl.scan != 0.0f || sl.scanSpeed != 0.0f;
        // The scan offset is a pure function of the render time; wrapped here in double so a long piece does not lose
        // the fraction to float precision.
        const double offset = static_cast<double>(sl.scan) + static_cast<double>(sl.scanSpeed) * in.renderTime;
        u.extra = {std::cos(angle), std::sin(angle), static_cast<float>(offset - std::floor(offset)), std::round(sl.mode)};
        u.more = {static_cast<float>(history_->stride()), std::clamp(sl.blend, 0.0f, 1.0f), sl.reverse > 0.5f ? 1.0f : 0.0f,
                  scanning ? 1.0f : 0.0f};
        if (!ringPass("fs_sample", "temporal-sample", "temporal/sample", im.slit, im.slitFormat, im.slitUniforms, u)) {
            return result;
        }
    }

    // ---- ADR-1049 data mosh and channel shift ----------------------------------------------
    //
    // After the echo (it corrupts what the viewer would otherwise see) and, like the echo, before the
    // capture, so its taps are genuinely past frames and the ring never holds a corrupted frame. At an
    // amount and a shift of zero nothing is encoded: the ring is kept warm for the event that turns
    // it up, and the frame is byte-identical to one without the effect.
    const scene::MoshSettings& mosh = settings.mosh;
    if (mosh.enabled && (mosh.amount > 0.0f || mosh.shift > 0.0f)) {
        const std::uint32_t taps =
            std::min<std::uint32_t>(static_cast<std::uint32_t>(std::max(mosh.frames, 1)), state.framesValid);
        if (im.mosh == nullptr || im.moshFormat != in.hdrFormat) {
            auto pipeline = im.makePipeline("fs_mosh", in.hdrFormat, "temporal-mosh");
            if (!pipeline) {
                return result;
            }
            im.mosh = *pipeline;
            im.moshFormat = in.hdrFormat;
        }
        // Pixel sizes are authored at 1080 lines and scaled with the output, so a preview and a 4K
        // render corrupt the same share of the picture.
        const float lines = static_cast<float>(in.height) / 1080.0f;
        auto out = pool.acquire(in.width, in.height, in.hdrFormat);
        Uniforms u{};
        u.sizes = {static_cast<float>(history_->width()), static_cast<float>(history_->height()),
                   1.0f / static_cast<float>(history_->width()), 1.0f / static_cast<float>(history_->height())};
        u.outputSize = {static_cast<float>(in.width), static_cast<float>(in.height),
                        1.0f / static_cast<float>(in.width), 1.0f / static_cast<float>(in.height)};
        u.ring = {static_cast<float>(history_->frames(TemporalChannel::Colour)),
                  static_cast<float>(history_->writeLayer(TemporalChannel::Colour)),
                  static_cast<float>(state.framesValid), static_cast<float>(taps)};
        u.params = {std::clamp(mosh.amount, 0.0f, 1.0f), std::max(mosh.block * lines, 1.0f), mosh.smear * lines,
                    mosh.shift * lines};
        const double epoch = std::floor(in.renderTime * static_cast<double>(std::max(mosh.rate, 0.0f))) +
                             std::floor(static_cast<double>(mosh.seed));
        u.extra = {static_cast<float>(std::fmod(epoch, 8388608.0)), 0.0f, 0.0f, 0.0f};
        im.context.queue().WriteBuffer(im.moshUniforms, 0, &u, sizeof(u));

        wgpu::BindGroup group = im.makeGroup(result, history_->arrayView(TemporalChannel::Colour), &im.moshUniforms);
        wgpu::RenderPassColorAttachment colour{};
        colour.view = out.view;
        colour.loadOp = wgpu::LoadOp::Clear;
        colour.storeOp = wgpu::StoreOp::Store;
        colour.clearValue = {0.0, 0.0, 0.0, 1.0};
        wgpu::RenderPassDescriptor pass{};
        pass.label = "temporal-mosh";
        pass.colorAttachmentCount = 1;
        pass.colorAttachments = &colour;
        if (im.timeline != nullptr) {
            pass.timestampWrites = im.timeline->mark("temporal/mosh", gpu::FrameTimeline::PassKind::Render);
        }
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        rp.SetPipeline(im.mosh);
        rp.SetBindGroup(0, group);
        rp.Draw(3);
        rp.End();
        result = out.view;
        ++stats_.passes;
        im.passThisFrame = true;
    }

    // ---- ADR-1934: the slot capture, into the layer the colour capture is about to write ----------------------------
    if (gateOn && im.slotRing != nullptr && history_->capturesThisFrame()) {
        if (im.slotCapture == nullptr) {
            auto made = im.makeGatePipeline("fs_capture_slot", wgpu::TextureFormat::R8Uint, im.slotCapturePipelineLayout,
                                            "temporal-capture-slot");
            if (made) {
                im.slotCapture = *made;
            }
        }
        const std::uint32_t layer = history_->writeLayer(TemporalChannel::Colour);
        if (im.slotCapture != nullptr && layer < im.slotLayerViews.size()) {
            Uniforms u{};
            u.sizes = {static_cast<float>(im.slotW), static_cast<float>(im.slotH), 1.0f / static_cast<float>(im.slotW),
                       1.0f / static_cast<float>(im.slotH)};
            u.outputSize = {static_cast<float>(in.width), static_cast<float>(in.height),
                            1.0f / static_cast<float>(in.width), 1.0f / static_cast<float>(in.height)};
            im.context.queue().WriteBuffer(im.slotCaptureUniforms, 0, &u, sizeof(u));
            std::array<wgpu::BindGroupEntry, 3> entries{};
            entries[0].binding = 0;
            entries[0].buffer = im.slotCaptureUniforms;
            entries[0].size = sizeof(Uniforms);
            entries[1].binding = 5;
            entries[1].textureView = idView;
            entries[2].binding = 6;
            entries[2].buffer = im.slotTable;
            entries[2].size = kSlotTableWords * sizeof(std::uint32_t);
            wgpu::BindGroupDescriptor gd{};
            gd.label = "temporal-slot-capture-group";
            gd.layout = im.slotCaptureLayout;
            gd.entryCount = entries.size();
            gd.entries = entries.data();
            wgpu::BindGroup group = im.context.device().CreateBindGroup(&gd);
            wgpu::RenderPassColorAttachment colour{};
            colour.view = im.slotLayerViews[layer];
            colour.loadOp = wgpu::LoadOp::Clear;
            colour.storeOp = wgpu::StoreOp::Store;
            colour.clearValue = {0.0, 0.0, 0.0, 0.0};
            wgpu::RenderPassDescriptor pass{};
            pass.label = "temporal-capture-slot";
            pass.colorAttachmentCount = 1;
            pass.colorAttachments = &colour;
            if (im.timeline != nullptr) {
                pass.timestampWrites = im.timeline->mark("temporal/capture-slot", gpu::FrameTimeline::PassKind::Render);
            }
            wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
            rp.SetPipeline(im.slotCapture);
            rp.SetBindGroup(0, group);
            rp.Draw(3);
            rp.End();
            ++stats_.passes;
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
        const double ms = im.timeline->msForPrefix("temporal/"); // ADR-1066: every pass marks its own label
        if (ms >= 0.0) {
            im.lastMs = ms;
        }
    }
    stats_.temporalMs = im.passThisFrame ? im.lastMs : -1.0;
}

} // namespace avgen::rendering
