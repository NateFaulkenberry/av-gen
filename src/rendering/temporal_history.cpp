#include "rendering/temporal_history.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/shader_library.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace avgen::rendering {
namespace {

[[nodiscard]] std::uint32_t scaled(std::uint32_t value, float scale) {
    const auto v = static_cast<std::uint32_t>(static_cast<float>(value) * std::clamp(scale, 0.05f, 1.0f) + 0.5f);
    return std::max(v, 1u);
}

[[nodiscard]] std::uint32_t bytesPerTexel(wgpu::TextureFormat format) {
    switch (format) {
    case wgpu::TextureFormat::RG11B10Ufloat: return 4;
    case wgpu::TextureFormat::RG16Float: return 4;
    default: return 0;
    }
}

struct Uniforms {
    std::array<float, 4> sizes{};
    std::array<float, 4> outputSize{};
    std::array<float, 4> ring{};
    std::array<float, 4> params{};
};
static_assert(sizeof(Uniforms) == 64);

} // namespace

bool TemporalHistoryConfig::anyEnabled() const {
    return std::any_of(frames.begin(), frames.end(), [](std::uint32_t f) { return f > 0; });
}

std::uint32_t TemporalHistoryConfig::maxFrames() const {
    return *std::max_element(frames.begin(), frames.end());
}

struct TemporalHistory::Impl {
    struct Ring {
        wgpu::Texture texture;
        wgpu::TextureView arrayView;
        std::vector<wgpu::TextureView> layerViews;
        std::uint32_t frames = 0;
        std::uint32_t writeLayer = 0;
        [[nodiscard]] bool valid() const { return texture != nullptr; }
    };

    explicit Impl(gpu::Context& ctx, gpu::ShaderLibrary& sh) : context(ctx), shaders(sh) {}

    gpu::Context& context;
    gpu::ShaderLibrary& shaders;
    gpu::FrameTimeline* timeline = nullptr;

    bool initialised = false;
    std::array<Ring, kTemporalChannelCount> rings{};
    std::array<wgpu::TextureView, kTemporalChannelCount> placeholders{};
    std::array<wgpu::Texture, kTemporalChannelCount> placeholderTextures{};

    TemporalHistoryConfig config{};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t sceneWidth = 0;
    std::uint32_t sceneHeight = 0;

    wgpu::Buffer uniforms;
    wgpu::Sampler sampler;
    wgpu::BindGroupLayout layout;
    wgpu::PipelineLayout pipelineLayout;
    wgpu::RenderPipeline captureColour;

    // ---- the determinism state machine (ADR-394) ----
    //
    // Copied deliberately from `AoRenderer::update`, which solved this once. Two cases that look
    // alike and need opposite treatment -- treating them the same is the bug this repo calls
    // SYM-TERRAIN-1.
    //
    // A **discontinuity** (a seek, a cut, a reversed step) means the previous frame is not this
    // frame's past. Accumulating against it smears across the jump, so the history is dropped.
    //
    // A **repeat** -- the same frame index rendered twice -- is the opposite. The first render
    // legitimately had history and used it; dropping it on the second would make the second render
    // a different picture from the first, which is exactly the non-determinism the rule exists to
    // prevent. So the state the frame index *started* with is restored and the same computation
    // runs against the same inputs. Tests render a frame twice and the app re-renders a paused
    // frame, so this is a normal path, not an exotic one.
    bool haveLastFrame = false;
    std::uint64_t lastFrameIndex = 0;
    std::uint32_t framesValid = 0;
    std::uint32_t framesValidAtFrameStart = 0;
    std::array<std::uint32_t, kTemporalChannelCount> writeAtFrameStart{};
    std::uint32_t framesNeeded = 0;

    [[nodiscard]] Result<void> ensureRing(Ring& ring, TemporalChannel channel, std::uint32_t frames,
                                          std::uint32_t w, std::uint32_t h);
    [[nodiscard]] Result<void> createPlaceholders();
};

Result<void> TemporalHistory::Impl::createPlaceholders() {
    const auto& device = context.device();
    for (std::uint32_t i = 0; i < kTemporalChannelCount; ++i) {
        const auto channel = static_cast<TemporalChannel>(i);
        wgpu::TextureDescriptor desc{};
        const std::string label = std::string("temporal-placeholder-") + temporalChannelName(channel);
        desc.label = label.c_str();
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment;
        desc.dimension = wgpu::TextureDimension::e2D;
        desc.size = {1, 1, 1};
        desc.format = temporalChannelFormat(channel);
        wgpu::Texture texture = device.CreateTexture(&desc);
        if (texture == nullptr) {
            return fail("temporal placeholder '{}'", temporalChannelName(channel));
        }
        wgpu::TextureViewDescriptor view{};
        view.dimension = wgpu::TextureViewDimension::e2DArray;
        view.arrayLayerCount = 1;
        placeholders[i] = texture.CreateView(&view);
        placeholderTextures[i] = std::move(texture);
    }
    return {};
}

Result<void> TemporalHistory::Impl::ensureRing(Ring& ring, TemporalChannel channel, std::uint32_t frames,
                                              std::uint32_t w, std::uint32_t h) {
    if (frames == 0) {
        ring = Ring{};
        return {};
    }
    if (ring.valid() && ring.frames == frames) {
        return {};
    }
    const auto& device = context.device();
    wgpu::TextureDescriptor desc{};
    const std::string label = std::string("temporal-ring-") + temporalChannelName(channel);
    desc.label = label.c_str();
    desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding |
                 wgpu::TextureUsage::CopySrc;
    desc.dimension = wgpu::TextureDimension::e2D;
    desc.size = {w, h, frames};
    desc.format = temporalChannelFormat(channel);

    device.PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::Texture texture = device.CreateTexture(&desc);
    std::string error;
    auto future = device.PopErrorScope(wgpu::CallbackMode::WaitAnyOnly,
                                       [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                                           if (type != wgpu::ErrorType::NoError) {
                                               error = gpu::Context::toString(msg);
                                           }
                                       });
    context.waitFor(future);
    if (!error.empty() || texture == nullptr) {
        return fail("temporal ring '{}' {}x{}x{}: {}", temporalChannelName(channel), w, h, frames, error);
    }

    ring = Ring{};
    ring.texture = std::move(texture);
    ring.frames = frames;
    {
        wgpu::TextureViewDescriptor view{};
        view.label = "temporal-ring-array";
        view.dimension = wgpu::TextureViewDimension::e2DArray;
        view.arrayLayerCount = frames;
        ring.arrayView = ring.texture.CreateView(&view);
    }
    ring.layerViews.reserve(frames);
    for (std::uint32_t i = 0; i < frames; ++i) {
        wgpu::TextureViewDescriptor view{};
        view.label = "temporal-ring-layer";
        view.dimension = wgpu::TextureViewDimension::e2D;
        view.baseArrayLayer = i;
        view.arrayLayerCount = 1;
        ring.layerViews.push_back(ring.texture.CreateView(&view));
    }
    return {};
}

TemporalHistory::TemporalHistory(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : impl_(std::make_unique<Impl>(context, shaders)) {}

TemporalHistory::~TemporalHistory() = default;

Result<void> TemporalHistory::init() {
    Impl& im = *impl_;
    const auto& device = im.context.device();
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "temporal-uniforms";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = sizeof(Uniforms);
        im.uniforms = device.CreateBuffer(&desc);
    }
    {
        wgpu::SamplerDescriptor desc{};
        desc.label = "temporal-sampler";
        desc.minFilter = wgpu::FilterMode::Linear;
        desc.magFilter = wgpu::FilterMode::Linear;
        // Clamp: a tap that walks off the frame must repeat the edge, not wrap it. A wrapped echo
        // puts the right-hand edge of the picture on the left-hand side, which reads as a glitch
        // that the artist did not author.
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
        desc.label = "temporal-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        im.layout = device.CreateBindGroupLayout(&desc);
    }
    {
        wgpu::PipelineLayoutDescriptor desc{};
        desc.label = "temporal-pipeline-layout";
        desc.bindGroupLayoutCount = 1;
        desc.bindGroupLayouts = &im.layout;
        im.pipelineLayout = device.CreatePipelineLayout(&desc);
    }
    if (auto r = im.createPlaceholders(); !r) return r;
    if (auto r = reload(); !r) return r;
    im.initialised = true;
    return {};
}

Result<void> TemporalHistory::reload() {
    Impl& im = *impl_;
    auto module = im.shaders.load("temporal.wgsl");
    if (!module) return std::unexpected(module.error());

    wgpu::ColorTargetState target{};
    target.format = temporalChannelFormat(TemporalChannel::Colour);

    wgpu::FragmentState fragment{};
    fragment.module = *module;
    fragment.entryPoint = "fs_capture_colour";
    fragment.targetCount = 1;
    fragment.targets = &target;

    wgpu::RenderPipelineDescriptor desc{};
    desc.label = "temporal-capture-colour";
    desc.layout = im.pipelineLayout;
    desc.vertex.module = *module;
    desc.vertex.entryPoint = "vs_fullscreen";
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.fragment = &fragment;

    im.context.device().PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::RenderPipeline pipeline = im.context.device().CreateRenderPipeline(&desc);
    std::string error;
    auto future = im.context.device().PopErrorScope(
        wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
            if (type != wgpu::ErrorType::NoError) {
                error = gpu::Context::toString(msg);
            }
        });
    im.context.waitFor(future);
    // Keep the old pipeline on failure, so a hot reload of a broken shader leaves a working
    // renderer rather than a black frame.
    if (!error.empty() || pipeline == nullptr) {
        return fail("temporal capture pipeline: {}", error);
    }
    im.captureColour = std::move(pipeline);
    return {};
}

Result<void> TemporalHistory::configure(std::uint32_t sceneWidth, std::uint32_t sceneHeight,
                                        const TemporalHistoryConfig& config) {
    Impl& im = *impl_;
    if (!im.initialised) {
        return {};
    }
    if (sceneWidth == 0 || sceneHeight == 0 || !config.anyEnabled()) {
        if (im.config.anyEnabled()) {
            // Turning every effect off releases the rings; holding ~25 MB for a feature nobody has
            // on is exactly what §8 tells us not to do.
            for (auto& ring : im.rings) ring = Impl::Ring{};
            im.config = TemporalHistoryConfig{};
            im.framesValid = 0;
        }
        return {};
    }
    const std::uint32_t w = scaled(sceneWidth, config.resolutionScale);
    const std::uint32_t h = scaled(sceneHeight, config.resolutionScale);
    const bool sizeChanged = w != im.width || h != im.height;
    if (sizeChanged) {
        for (auto& ring : im.rings) ring = Impl::Ring{};
    }
    for (std::uint32_t i = 0; i < kTemporalChannelCount; ++i) {
        const std::uint32_t want = std::min(config.frames[i], kMaxTemporalFrames);
        if (auto r = im.ensureRing(im.rings[i], static_cast<TemporalChannel>(i), want, w, h); !r) {
            // A failed allocation must not leave a half-built ring that an effect then samples.
            for (auto& ring : im.rings) ring = Impl::Ring{};
            im.framesValid = 0;
            return r;
        }
    }
    if (sizeChanged || im.config.maxFrames() != config.maxFrames()) {
        // A resized or resized-depth ring holds nothing this frame can use.
        im.framesValid = 0;
        im.framesValidAtFrameStart = 0;
        im.haveLastFrame = false;
    }
    im.width = w;
    im.height = h;
    im.sceneWidth = sceneWidth;
    im.sceneHeight = sceneHeight;
    im.config = config;
    im.config.frames = {};
    for (std::uint32_t i = 0; i < kTemporalChannelCount; ++i) {
        im.config.frames[i] = std::min(config.frames[i], kMaxTemporalFrames);
    }
    return {};
}

void TemporalHistory::beginFrame(std::uint64_t frameIndex) {
    Impl& im = *impl_;
    const bool repeat = im.haveLastFrame && frameIndex == im.lastFrameIndex;
    if (repeat) {
        // Restore what this frame index started with, so the second render of a frame reads
        // exactly what the first one read. See Impl's comment.
        im.framesValid = im.framesValidAtFrameStart;
        for (std::uint32_t i = 0; i < kTemporalChannelCount; ++i) {
            im.rings[i].writeLayer = im.writeAtFrameStart[i];
        }
    } else {
        if (im.haveLastFrame && frameIndex != im.lastFrameIndex + 1) {
            im.framesValid = 0;
        }
        im.framesValidAtFrameStart = im.framesValid;
        for (std::uint32_t i = 0; i < kTemporalChannelCount; ++i) {
            im.writeAtFrameStart[i] = im.rings[i].writeLayer;
        }
    }
    im.lastFrameIndex = frameIndex;
    im.haveLastFrame = true;

    const std::uint32_t capacity = im.config.maxFrames();
    state_.framesValid = std::min(im.framesValid, capacity);
    state_.framesNeeded = im.framesNeeded;
    // Stalled, not settling: the ring is smaller than what the live effects ask for, so no amount
    // of waiting fills it. The two need different words in the UI (ADR-394).
    state_.stalled = im.framesNeeded > capacity;
}

void TemporalHistory::encodeCapture(wgpu::CommandEncoder& encoder, const wgpu::TextureView& sceneHdr,
                                    const wgpu::TextureView& velocity) {
    (void)velocity; // the motion ring lands with §10; the channel exists and allocates today
    Impl& im = *impl_;
    if (!im.initialised || sceneHdr == nullptr) {
        return;
    }
    auto& ring = im.rings[static_cast<std::uint32_t>(TemporalChannel::Colour)];
    if (!ring.valid() || ring.frames == 0) {
        return;
    }

    Uniforms u{};
    u.sizes = {static_cast<float>(im.width), static_cast<float>(im.height), 1.0f / static_cast<float>(im.width),
               1.0f / static_cast<float>(im.height)};
    // The capture reads the *scene* resolution, so the box offsets are scene texels.
    u.outputSize = {static_cast<float>(im.sceneWidth), static_cast<float>(im.sceneHeight),
                    1.0f / static_cast<float>(im.sceneWidth), 1.0f / static_cast<float>(im.sceneHeight)};
    u.ring = {static_cast<float>(ring.frames), static_cast<float>(ring.writeLayer),
              static_cast<float>(state_.framesValid), 0.0f};
    im.context.queue().WriteBuffer(im.uniforms, 0, &u, sizeof(u));

    std::array<wgpu::BindGroupEntry, 4> entries{};
    entries[0].binding = 0;
    entries[0].buffer = im.uniforms;
    entries[0].size = sizeof(Uniforms);
    entries[1].binding = 1;
    entries[1].sampler = im.sampler;
    entries[2].binding = 2;
    entries[2].textureView = sceneHdr;
    entries[3].binding = 3;
    entries[3].textureView = ring.arrayView;
    wgpu::BindGroupDescriptor groupDesc{};
    groupDesc.label = "temporal-capture-group";
    groupDesc.layout = im.layout;
    groupDesc.entryCount = entries.size();
    groupDesc.entries = entries.data();
    wgpu::BindGroup group = im.context.device().CreateBindGroup(&groupDesc);

    wgpu::RenderPassColorAttachment colour{};
    colour.view = ring.layerViews[ring.writeLayer];
    colour.loadOp = wgpu::LoadOp::Clear;
    colour.storeOp = wgpu::StoreOp::Store;
    colour.clearValue = {0.0, 0.0, 0.0, 1.0};
    wgpu::RenderPassDescriptor pass{};
    pass.label = "temporal-capture";
    pass.colorAttachmentCount = 1;
    pass.colorAttachments = &colour;
    if (im.timeline != nullptr) {
        pass.timestampWrites = im.timeline->mark("temporal", gpu::FrameTimeline::PassKind::Render);
    }
    wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
    rp.SetPipeline(im.captureColour);
    rp.SetBindGroup(0, group);
    rp.Draw(3);
    rp.End();

    ring.writeLayer = (ring.writeLayer + 1) % ring.frames;
    if (im.framesValid < ring.frames) {
        ++im.framesValid;
    }
    state_.framesValid = std::min(im.framesValid, im.config.maxFrames());
}

void TemporalHistory::reset() {
    Impl& im = *impl_;
    im.framesValid = 0;
    // ...and the frame-start snapshot with it, or a repeat of the next frame would restore exactly
    // the history this call was asked to drop. `AoRenderer::resetHistory` learned this the hard
    // way; the same three lines are the fix.
    im.framesValidAtFrameStart = 0;
    im.haveLastFrame = false;
    state_.framesValid = 0;
}

void TemporalHistory::setFramesNeeded(std::uint32_t frames) {
    Impl& im = *impl_;
    im.framesNeeded = std::min(frames, kMaxTemporalFrames);
    // Reflected into the reported state immediately rather than at the next `beginFrame`. The
    // panel reads `state()` in the same frame the effect's tap count changed, and a badge that is
    // one frame stale is a badge that flickers "settling" every time a slider moves.
    state_.framesNeeded = im.framesNeeded;
    state_.stalled = im.framesNeeded > im.config.maxFrames();
}

bool TemporalHistory::active() const {
    const Impl& im = *impl_;
    return im.initialised && im.config.anyEnabled() &&
           im.rings[static_cast<std::uint32_t>(TemporalChannel::Colour)].valid();
}

const wgpu::TextureView& TemporalHistory::arrayView(TemporalChannel channel) const {
    const Impl& im = *impl_;
    const auto i = static_cast<std::uint32_t>(channel);
    const auto& ring = im.rings[i];
    return ring.valid() ? ring.arrayView : im.placeholders[i];
}

std::uint32_t TemporalHistory::frames(TemporalChannel channel) const {
    return impl_->rings[static_cast<std::uint32_t>(channel)].frames;
}

std::uint32_t TemporalHistory::writeLayer(TemporalChannel channel) const {
    return impl_->rings[static_cast<std::uint32_t>(channel)].writeLayer;
}

std::uint32_t TemporalHistory::width() const { return impl_->width; }
std::uint32_t TemporalHistory::height() const { return impl_->height; }

std::uint64_t TemporalHistory::bytes() const {
    const Impl& im = *impl_;
    std::uint64_t total = 0;
    for (std::uint32_t i = 0; i < kTemporalChannelCount; ++i) {
        const auto& ring = im.rings[i];
        if (!ring.valid()) continue;
        total += static_cast<std::uint64_t>(im.width) * im.height * ring.frames *
                 bytesPerTexel(temporalChannelFormat(static_cast<TemporalChannel>(i)));
    }
    return total;
}

void TemporalHistory::setTimeline(gpu::FrameTimeline* timeline) { impl_->timeline = timeline; }

} // namespace avgen::rendering
