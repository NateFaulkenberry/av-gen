#include "rendering/distortion_renderer.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/shader_library.hpp"
#include "gpu/transient_pool.hpp"
#include "scene/mesh_generators.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace avgen::rendering {

namespace {

// Mirrors `DfParams` in shaders/distortion.wgsl.
struct DfParams {
    glm::vec4 target;   // width, height, 1/width, 1/height
    glm::vec4 copyRect; // UV min.xy, max.xy of the copied region
    glm::vec4 misc;     // x = max offset (UV), y = proxy mesh scale
};
static_assert(sizeof(DfParams) == 48);

constexpr std::uint64_t kProxyBufferSize =
    sizeof(world::DistortionProxy) * world::kMaxDistortionProxies;

// The proxy mesh's subdivision. Level 2 is 320 triangles: at a warp's usual screen size the hull hugs
// the analytic ellipsoid within ~3% of its radius, so few fragments are shaded only to be discarded.
constexpr int kIcoLevel = 2;

glm::ivec4 rectOfSphere(glm::vec3 c, float r, const glm::mat4& viewProj, float nearPlane, std::uint32_t w,
                        std::uint32_t h, bool& reachesNear) {
    glm::vec2 lo(1e30f), hi(-1e30f);
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 p = c + glm::vec3((corner & 1) ? r : -r, (corner & 2) ? r : -r, (corner & 4) ? r : -r);
        const glm::vec4 clip = viewProj * glm::vec4(p, 1.0f);
        if (clip.w < nearPlane * 0.5f) {
            reachesNear = true;
            return {0, 0, static_cast<int>(w), static_cast<int>(h)};
        }
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        const glm::vec2 px((ndc.x * 0.5f + 0.5f) * static_cast<float>(w), (0.5f - ndc.y * 0.5f) * static_cast<float>(h));
        lo = glm::min(lo, px);
        hi = glm::max(hi, px);
    }
    // Two pixels of margin: rasterisation rounding, and the bilinear footprint of the resolve's taps.
    return {static_cast<int>(std::floor(lo.x)) - 2, static_cast<int>(std::floor(lo.y)) - 2,
            static_cast<int>(std::ceil(hi.x)) + 2, static_cast<int>(std::ceil(hi.y)) + 2};
}

glm::ivec4 clampRect(glm::ivec4 r, std::uint32_t w, std::uint32_t h) {
    r.x = std::clamp(r.x, 0, static_cast<int>(w));
    r.z = std::clamp(r.z, 0, static_cast<int>(w));
    r.y = std::clamp(r.y, 0, static_cast<int>(h));
    r.w = std::clamp(r.w, 0, static_cast<int>(h));
    return r;
}

glm::ivec4 unite(glm::ivec4 a, glm::ivec4 b, bool first) {
    return first ? b : glm::ivec4(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.z, b.z), std::max(a.w, b.w));
}

std::uint32_t area(glm::ivec4 r) {
    return static_cast<std::uint32_t>(std::max(r.z - r.x, 0) * std::max(r.w - r.y, 0));
}

} // namespace

DistortionRects distortionRects(const world::DistortionFrame& frame, const glm::mat4& viewProj, float nearPlane,
                                std::uint32_t width, std::uint32_t height, float meshScale) {
    DistortionRects out;
    const std::size_t n = std::min<std::size_t>(frame.count, world::kMaxDistortionProxies);
    for (std::size_t i = 0; i < n && !out.fullScreen; ++i) {
        const world::DistortionProxy& p = frame.proxies[i];
        const glm::vec3 c(p.centre);
        const float r = std::max({glm::length(glm::vec3(p.axis0)), glm::length(glm::vec3(p.axis1)),
                                  glm::length(glm::vec3(p.axis2))}) *
                        meshScale;
        // How far a tap can reach beyond the field: the displacement's bound (every term's weight at
        // the profile's peak of 1, turbulence soft-limited to 2x its amount), and the chroma taps'
        // extra half.
        const float disp = p.terms.w * (std::abs(p.terms.x) + std::abs(p.terms.y) * p.motion.w +
                                        std::abs(p.terms.z) + 2.0f * p.shape.z);
        bool near = false;
        const glm::ivec4 s = rectOfSphere(c, r, viewProj, nearPlane, width, height, near);
        const glm::ivec4 k = rectOfSphere(c, r + 1.5f * disp, viewProj, nearPlane, width, height, near);
        if (near) {
            out.fullScreen = true;
            break;
        }
        out.scissor = unite(out.scissor, s, i == 0);
        out.copy = unite(out.copy, k, i == 0);
    }
    if (out.fullScreen) {
        out.scissor = out.copy = glm::ivec4(0, 0, static_cast<int>(width), static_cast<int>(height));
    }
    out.scissor = clampRect(out.scissor, width, height);
    out.copy = clampRect(out.copy, width, height);
    return out;
}

struct DistortionRenderer::Impl {
    Impl(gpu::Context& c, gpu::ShaderLibrary& s) : context(c), shaders(s) {}

    Result<void> createPipelines(const wgpu::ShaderModule& module);

    gpu::Context& context;
    gpu::ShaderLibrary& shaders;
    wgpu::TextureFormat hdrFormat = wgpu::TextureFormat::RGBA16Float;
    wgpu::TextureFormat emissionFormat = wgpu::TextureFormat::RGBA16Float;
    wgpu::BindGroupLayout frameLayout;
    wgpu::BindGroupLayout offsetLayout;
    wgpu::BindGroupLayout resolveLayout;
    wgpu::PipelineLayout offsetPipelineLayout;
    wgpu::PipelineLayout resolvePipelineLayout;
    wgpu::RenderPipeline offsetPipeline;
    wgpu::RenderPipeline resolvePipeline;
    wgpu::Buffer proxies;
    wgpu::Buffer params;
    wgpu::Buffer vertices;
    wgpu::Buffer indices;
    std::uint32_t indexCount = 0;
    float meshScale = 1.0f;
    wgpu::Sampler sampler;

    // Bind groups are rebuilt only when a view they hold changes (a resize, a different pooled
    // texture), never per frame.
    wgpu::BindGroup offsetGroup;
    wgpu::TextureView offsetGroupDepth;
    wgpu::BindGroup resolveGroup;
    std::array<wgpu::TextureView, 4> resolveGroupViews{}; // copy, offset, aux, depth

    gpu::FrameTimeline* timeline = nullptr;
    double lastOffsetMs = -1.0;
    double lastResolveMs = -1.0;
    bool passThisFrame = false;
    bool initialised = false;
};

DistortionRenderer::DistortionRenderer(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : impl_(std::make_unique<Impl>(context, shaders)) {}

DistortionRenderer::~DistortionRenderer() = default;

float DistortionRenderer::meshScale() const { return impl_->meshScale; }

Result<void> DistortionRenderer::Impl::createPipelines(const wgpu::ShaderModule& module) {
    const auto& device = context.device();
    const auto make = [&](const wgpu::RenderPipelineDescriptor& desc, const char* label) -> Result<wgpu::RenderPipeline> {
        device.PushErrorScope(wgpu::ErrorFilter::Validation);
        wgpu::RenderPipeline made = device.CreateRenderPipeline(&desc);
        std::string error;
        auto future = device.PopErrorScope(
            wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                if (type != wgpu::ErrorType::NoError) {
                    error = gpu::Context::toString(msg);
                }
            });
        context.waitFor(future);
        if (!error.empty() || !made) {
            return fail("pipeline '{}' creation failed: {}", label, error);
        }
        return made;
    };

    // ---- the offset pass: instanced proxies, additive into both targets, no depth attachment ----
    wgpu::BlendState add{};
    add.color.operation = wgpu::BlendOperation::Add;
    add.color.srcFactor = wgpu::BlendFactor::One;
    add.color.dstFactor = wgpu::BlendFactor::One;
    add.alpha = add.color;
    std::array<wgpu::ColorTargetState, 2> offsetTargets{};
    for (auto& t : offsetTargets) {
        t.format = DistortionRenderer::kOffsetFormat;
        t.blend = &add;
        t.writeMask = wgpu::ColorWriteMask::All;
    }
    wgpu::FragmentState offsetFragment{};
    offsetFragment.module = module;
    offsetFragment.entryPoint = "fs_offset";
    offsetFragment.targetCount = offsetTargets.size();
    offsetFragment.targets = offsetTargets.data();
    wgpu::VertexAttribute position{};
    position.format = wgpu::VertexFormat::Float32x3;
    position.offset = 0;
    position.shaderLocation = 0;
    wgpu::VertexBufferLayout vertexLayout{};
    vertexLayout.arrayStride = sizeof(float) * 3;
    vertexLayout.stepMode = wgpu::VertexStepMode::Vertex;
    vertexLayout.attributeCount = 1;
    vertexLayout.attributes = &position;
    wgpu::RenderPipelineDescriptor offsetDesc{};
    offsetDesc.label = "distortion-offset";
    offsetDesc.layout = offsetPipelineLayout;
    offsetDesc.vertex.module = module;
    offsetDesc.vertex.entryPoint = "vs_proxy";
    offsetDesc.vertex.bufferCount = 1;
    offsetDesc.vertex.buffers = &vertexLayout;
    offsetDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    // BACK faces: a closed convex hull's back faces cover its screen footprint exactly once whether
    // the camera is outside the proxy or inside it, so one pipeline serves both and nothing is shaded
    // twice. (Front faces vanish the moment the camera enters a warp.)
    offsetDesc.primitive.frontFace = wgpu::FrontFace::CCW;
    offsetDesc.primitive.cullMode = wgpu::CullMode::Front;
    offsetDesc.multisample.count = 1;
    offsetDesc.multisample.mask = 0xFFFFFFFFu;
    offsetDesc.fragment = &offsetFragment;
    auto offset = make(offsetDesc, "distortion-offset");
    if (!offset) {
        return std::unexpected(offset.error());
    }

    // ---- the resolve: HDR replaced where bent, emission added where the rim glows ----
    std::array<wgpu::ColorTargetState, 2> resolveTargets{};
    resolveTargets[0].format = hdrFormat;
    resolveTargets[0].writeMask = wgpu::ColorWriteMask::All;
    resolveTargets[1].format = emissionFormat;
    resolveTargets[1].blend = &add;
    resolveTargets[1].writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState resolveFragment{};
    resolveFragment.module = module;
    resolveFragment.entryPoint = "fs_resolve";
    resolveFragment.targetCount = resolveTargets.size();
    resolveFragment.targets = resolveTargets.data();
    wgpu::RenderPipelineDescriptor resolveDesc{};
    resolveDesc.label = "distortion-resolve";
    resolveDesc.layout = resolvePipelineLayout;
    resolveDesc.vertex.module = module;
    resolveDesc.vertex.entryPoint = "vs_resolve";
    resolveDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    resolveDesc.primitive.cullMode = wgpu::CullMode::None;
    resolveDesc.multisample.count = 1;
    resolveDesc.multisample.mask = 0xFFFFFFFFu;
    resolveDesc.fragment = &resolveFragment;
    auto resolve = make(resolveDesc, "distortion-resolve");
    if (!resolve) {
        return std::unexpected(resolve.error());
    }
    offsetPipeline = *offset;
    resolvePipeline = *resolve;
    return {};
}

Result<void> DistortionRenderer::init(const wgpu::BindGroupLayout& frameLayout, wgpu::TextureFormat hdrFormat,
                                      wgpu::TextureFormat emissionFormat) {
    Impl& im = *impl_;
    const auto& device = im.context.device();
    im.frameLayout = frameLayout;
    im.hdrFormat = hdrFormat;
    im.emissionFormat = emissionFormat;

    {
        wgpu::BufferDescriptor desc{};
        desc.label = "distortion-proxies";
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        desc.size = kProxyBufferSize;
        im.proxies = device.CreateBuffer(&desc);
        desc.label = "distortion-params";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = sizeof(DfParams);
        im.params = device.CreateBuffer(&desc);
    }

    // The proxy hull: a unit icosphere, scaled out by 1/inradius so it circumscribes the unit sphere
    // the shader's analytic ellipsoid lives in -- the hull may overdraw a sliver, never underdraw.
    {
        const scene::MeshData ico = scene::makeIcosphere(1.0f, kIcoLevel);
        std::vector<float> positions;
        positions.reserve(ico.vertices.size() * 3);
        for (const auto& v : ico.vertices) {
            positions.push_back(v.position.x);
            positions.push_back(v.position.y);
            positions.push_back(v.position.z);
        }
        float inradius = 1.0f;
        for (std::size_t i = 0; i + 2 < ico.indices.size(); i += 3) {
            const glm::vec3 a = ico.vertices[ico.indices[i]].position;
            const glm::vec3 b = ico.vertices[ico.indices[i + 1]].position;
            const glm::vec3 c = ico.vertices[ico.indices[i + 2]].position;
            const glm::vec3 n = glm::cross(b - a, c - a);
            const float len = glm::length(n);
            if (len > 1e-8f) {
                inradius = std::min(inradius, std::abs(glm::dot(n / len, a)));
            }
        }
        im.meshScale = 1.01f / std::max(inradius, 0.5f);
        im.indexCount = static_cast<std::uint32_t>(ico.indices.size());
        wgpu::BufferDescriptor desc{};
        desc.label = "distortion-hull-vertices";
        desc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
        desc.size = (positions.size() * sizeof(float) + 3u) & ~std::uint64_t{3};
        im.vertices = device.CreateBuffer(&desc);
        im.context.queue().WriteBuffer(im.vertices, 0, positions.data(), positions.size() * sizeof(float));
        desc.label = "distortion-hull-indices";
        desc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
        desc.size = ico.indices.size() * sizeof(std::uint32_t);
        im.indices = device.CreateBuffer(&desc);
        im.context.queue().WriteBuffer(im.indices, 0, ico.indices.data(), desc.size);
    }

    {
        wgpu::SamplerDescriptor desc{};
        desc.label = "distortion-copy-sampler";
        desc.addressModeU = wgpu::AddressMode::ClampToEdge;
        desc.addressModeV = wgpu::AddressMode::ClampToEdge;
        desc.magFilter = wgpu::FilterMode::Linear;
        desc.minFilter = wgpu::FilterMode::Linear;
        im.sampler = device.CreateSampler(&desc);
    }

    const auto texture = [](std::uint32_t binding, wgpu::TextureSampleType type) {
        wgpu::BindGroupLayoutEntry e{};
        e.binding = binding;
        e.visibility = wgpu::ShaderStage::Fragment;
        e.texture.sampleType = type;
        e.texture.viewDimension = wgpu::TextureViewDimension::e2D;
        return e;
    };
    const auto uniform = [](std::uint32_t binding, wgpu::ShaderStage stages) {
        wgpu::BindGroupLayoutEntry e{};
        e.binding = binding;
        e.visibility = stages;
        e.buffer.type = wgpu::BufferBindingType::Uniform;
        e.buffer.minBindingSize = sizeof(DfParams);
        return e;
    };
    {
        std::array<wgpu::BindGroupLayoutEntry, 3> entries{};
        entries[0].binding = 1;
        entries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
        entries[0].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[0].buffer.minBindingSize = sizeof(world::DistortionProxy);
        entries[1] = texture(2, wgpu::TextureSampleType::Depth);
        entries[2] = uniform(3, wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment);
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "distortion-offset-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        im.offsetLayout = device.CreateBindGroupLayout(&desc);
    }
    {
        std::array<wgpu::BindGroupLayoutEntry, 6> entries{};
        entries[0] = texture(2, wgpu::TextureSampleType::Depth);
        entries[1] = uniform(3, wgpu::ShaderStage::Fragment);
        entries[2] = texture(4, wgpu::TextureSampleType::Float);
        entries[3] = texture(5, wgpu::TextureSampleType::UnfilterableFloat);
        entries[4] = texture(6, wgpu::TextureSampleType::UnfilterableFloat);
        entries[5].binding = 7;
        entries[5].visibility = wgpu::ShaderStage::Fragment;
        entries[5].sampler.type = wgpu::SamplerBindingType::Filtering;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "distortion-resolve-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        im.resolveLayout = device.CreateBindGroupLayout(&desc);
    }
    {
        std::array<wgpu::BindGroupLayout, 2> layouts = {im.frameLayout, im.offsetLayout};
        wgpu::PipelineLayoutDescriptor desc{};
        desc.label = "distortion-offset-pipeline-layout";
        desc.bindGroupLayoutCount = layouts.size();
        desc.bindGroupLayouts = layouts.data();
        im.offsetPipelineLayout = device.CreatePipelineLayout(&desc);
        layouts[1] = im.resolveLayout;
        desc.label = "distortion-resolve-pipeline-layout";
        im.resolvePipelineLayout = device.CreatePipelineLayout(&desc);
    }

    auto module = im.shaders.load("distortion.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    if (auto r = im.createPipelines(*module); !r) {
        return r;
    }
    im.initialised = true;
    return {};
}

Result<void> DistortionRenderer::reload() {
    auto module = impl_->shaders.load("distortion.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    return impl_->createPipelines(*module);
}

void DistortionRenderer::setTimeline(gpu::FrameTimeline* timeline) { impl_->timeline = timeline; }

void DistortionRenderer::collectTimings() {
    Impl& im = *impl_;
    if (im.timeline != nullptr) {
        const double offsetMs = im.timeline->msFor("distort.offset");
        const double resolveMs = im.timeline->msFor("distort.resolve");
        if (offsetMs >= 0.0) {
            im.lastOffsetMs = offsetMs;
        }
        if (resolveMs >= 0.0) {
            im.lastResolveMs = resolveMs;
        }
    }
    stats_.offsetMs = im.passThisFrame ? im.lastOffsetMs : -1.0;
    stats_.resolveMs = im.passThisFrame ? im.lastResolveMs : -1.0;
}

bool DistortionRenderer::encode(wgpu::CommandEncoder& encoder, const world::DistortionFrame& frame,
                                const DistortionTargets& targets, gpu::TransientPool& pool) {
    Impl& im = *impl_;
    im.passThisFrame = false;
    stats_ = DistortionStats{};
    stats_.dropped = frame.dropped;
    const std::uint32_t count =
        std::min<std::uint32_t>(frame.count, static_cast<std::uint32_t>(world::kMaxDistortionProxies));
    // THE GATE. No producer: no upload, no acquire, no pass. Everything below is reached only on a
    // frame that has something to bend.
    if (!im.initialised || count == 0 || targets.width == 0 || targets.height == 0) {
        return false;
    }
    const DistortionRects rects =
        distortionRects(frame, targets.viewProj, targets.nearPlane, targets.width, targets.height, im.meshScale);
    if (area(rects.scissor) == 0) {
        return false; // every proxy is off-screen: nothing a resolve could change
    }

    const auto& device = im.context.device();
    auto& queue = im.context.queue();
    queue.WriteBuffer(im.proxies, 0, frame.proxies.data(), sizeof(world::DistortionProxy) * count);
    const float w = static_cast<float>(targets.width);
    const float h = static_cast<float>(targets.height);
    DfParams params{};
    params.target = glm::vec4(w, h, 1.0f / w, 1.0f / h);
    // The copied region, pulled half a texel inward, so a bilinear tap clamped to it reads only
    // copied texels.
    params.copyRect = glm::vec4((static_cast<float>(rects.copy.x) + 0.5f) / w, (static_cast<float>(rects.copy.y) + 0.5f) / h,
                                (static_cast<float>(rects.copy.z) - 0.5f) / w, (static_cast<float>(rects.copy.w) - 0.5f) / h);
    params.misc = glm::vec4(kMaxOffsetUv, im.meshScale, 0.0f, 0.0f);
    queue.WriteBuffer(im.params, 0, &params, sizeof(params));

    const wgpu::TextureUsage targetUsage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    const gpu::TransientTexture offset =
        pool.acquire(targets.width, targets.height, kOffsetFormat, targetUsage, "distortion-offset");
    const gpu::TransientTexture aux =
        pool.acquire(targets.width, targets.height, kOffsetFormat, targetUsage, "distortion-aux");
    const gpu::TransientTexture copy =
        pool.acquire(targets.width, targets.height, im.hdrFormat,
                     wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst, "distortion-scene-copy");

    if (!im.offsetGroup || im.offsetGroupDepth.Get() != targets.depthView.Get()) {
        std::array<wgpu::BindGroupEntry, 3> e{};
        e[0].binding = 1;
        e[0].buffer = im.proxies;
        e[0].size = kProxyBufferSize;
        e[1].binding = 2;
        e[1].textureView = targets.depthView;
        e[2].binding = 3;
        e[2].buffer = im.params;
        e[2].size = sizeof(DfParams);
        wgpu::BindGroupDescriptor desc{};
        desc.label = "distortion-offset-group";
        desc.layout = im.offsetLayout;
        desc.entryCount = e.size();
        desc.entries = e.data();
        im.offsetGroup = device.CreateBindGroup(&desc);
        im.offsetGroupDepth = targets.depthView;
    }
    const std::array<wgpu::TextureView, 4> views = {copy.view, offset.view, aux.view, targets.depthView};
    bool stale = !im.resolveGroup;
    for (std::size_t i = 0; i < views.size(); ++i) {
        stale = stale || im.resolveGroupViews[i].Get() != views[i].Get();
    }
    if (stale) {
        std::array<wgpu::BindGroupEntry, 6> e{};
        e[0].binding = 2;
        e[0].textureView = targets.depthView;
        e[1].binding = 3;
        e[1].buffer = im.params;
        e[1].size = sizeof(DfParams);
        e[2].binding = 4;
        e[2].textureView = copy.view;
        e[3].binding = 5;
        e[3].textureView = offset.view;
        e[4].binding = 6;
        e[4].textureView = aux.view;
        e[5].binding = 7;
        e[5].sampler = im.sampler;
        wgpu::BindGroupDescriptor desc{};
        desc.label = "distortion-resolve-group";
        desc.layout = im.resolveLayout;
        desc.entryCount = e.size();
        desc.entries = e.data();
        im.resolveGroup = device.CreateBindGroup(&desc);
        im.resolveGroupViews = views;
    }

    // ---- 1. the offset field ----
    {
        std::array<wgpu::RenderPassColorAttachment, 2> attachments{};
        for (std::size_t i = 0; i < attachments.size(); ++i) {
            attachments[i].view = i == 0 ? offset.view : aux.view;
            attachments[i].loadOp = wgpu::LoadOp::Clear;
            attachments[i].storeOp = wgpu::StoreOp::Store;
            attachments[i].clearValue = {0.0, 0.0, 0.0, 0.0};
        }
        wgpu::RenderPassDescriptor pass{};
        pass.label = "distortion-offset-pass";
        pass.colorAttachmentCount = attachments.size();
        pass.colorAttachments = attachments.data();
        pass.timestampWrites =
            im.timeline != nullptr ? im.timeline->mark("distort.offset", gpu::FrameTimeline::PassKind::Render) : nullptr;
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        rp.SetPipeline(im.offsetPipeline);
        rp.SetBindGroup(0, targets.frameBindGroup);
        rp.SetBindGroup(1, im.offsetGroup);
        rp.SetVertexBuffer(0, im.vertices);
        rp.SetIndexBuffer(im.indices, wgpu::IndexFormat::Uint32);
        rp.DrawIndexed(im.indexCount, count);
        rp.End();
    }

    // ---- 2. the scene copy: only what the resolve can read ----
    {
        wgpu::TexelCopyTextureInfo src{};
        src.texture = targets.hdr;
        src.origin = {static_cast<std::uint32_t>(rects.copy.x), static_cast<std::uint32_t>(rects.copy.y), 0};
        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = copy.texture;
        dst.origin = src.origin;
        const wgpu::Extent3D size = {static_cast<std::uint32_t>(rects.copy.z - rects.copy.x),
                                     static_cast<std::uint32_t>(rects.copy.w - rects.copy.y), 1};
        encoder.CopyTextureToTexture(&src, &dst, &size);
    }

    // ---- 3. the resolve, scissored to the producers ----
    {
        std::array<wgpu::RenderPassColorAttachment, 2> attachments{};
        attachments[0].view = targets.hdrView;
        attachments[1].view = targets.emissionView;
        for (auto& a : attachments) {
            a.loadOp = wgpu::LoadOp::Load;
            a.storeOp = wgpu::StoreOp::Store;
        }
        wgpu::RenderPassDescriptor pass{};
        pass.label = "distortion-resolve-pass";
        pass.colorAttachmentCount = attachments.size();
        pass.colorAttachments = attachments.data();
        pass.timestampWrites =
            im.timeline != nullptr ? im.timeline->mark("distort.resolve", gpu::FrameTimeline::PassKind::Render) : nullptr;
        wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
        rp.SetPipeline(im.resolvePipeline);
        rp.SetBindGroup(0, targets.frameBindGroup);
        rp.SetBindGroup(1, im.resolveGroup);
        rp.SetScissorRect(static_cast<std::uint32_t>(rects.scissor.x), static_cast<std::uint32_t>(rects.scissor.y),
                          static_cast<std::uint32_t>(rects.scissor.z - rects.scissor.x),
                          static_cast<std::uint32_t>(rects.scissor.w - rects.scissor.y));
        rp.Draw(3);
        rp.End();
    }

    pool.release(offset);
    pool.release(aux);
    pool.release(copy);

    im.passThisFrame = true;
    stats_.encoded = true;
    stats_.proxies = count;
    stats_.scissorPixels = area(rects.scissor);
    stats_.copyPixels = area(rects.copy);
    stats_.fullScreen = rects.fullScreen;
    stats_.offsetMs = im.lastOffsetMs;
    stats_.resolveMs = im.lastResolveMs;
    return true;
}

} // namespace avgen::rendering
