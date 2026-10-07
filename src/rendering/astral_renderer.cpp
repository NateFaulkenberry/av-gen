#include "rendering/astral_renderer.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/render_quality.hpp"
#include "scene/scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <vector>

namespace avgen::rendering {

namespace {

static_assert(sizeof(AstralRenderer::FrameU) % 16 == 0, "FrameU mirrors a WGSL struct of vec4s");

wgpu::Buffer makeBuffer(gpu::Context& ctx, std::uint64_t size, wgpu::BufferUsage usage, const char* label) {
    wgpu::BufferDescriptor d{};
    d.size = (size + 15) & ~std::uint64_t(15);
    d.usage = usage;
    d.label = label;
    return ctx.device().CreateBuffer(&d);
}

enum class B { Uniform, ReadOnly, Storage, Tex2D, Tex3D, Tex2DUnfilt, Tex3DUnfilt, Sampler, StoreTex3D_RGBA16F, StoreTex3D_R32F };

wgpu::BindGroupLayout makeLayout(gpu::Context& ctx, const std::vector<B>& kinds, wgpu::ShaderStage vis) {
    std::vector<wgpu::BindGroupLayoutEntry> e(kinds.size());
    for (std::size_t k = 0; k < kinds.size(); ++k) {
        e[k].binding = static_cast<std::uint32_t>(k);
        e[k].visibility = vis;
        switch (kinds[k]) {
        case B::Uniform: e[k].buffer.type = wgpu::BufferBindingType::Uniform; break;
        case B::ReadOnly: e[k].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage; break;
        case B::Storage: e[k].buffer.type = wgpu::BufferBindingType::Storage; break;
        case B::Tex2D:
            e[k].texture.sampleType = wgpu::TextureSampleType::Float;
            e[k].texture.viewDimension = wgpu::TextureViewDimension::e2D;
            break;
        case B::Tex2DUnfilt:
            e[k].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
            e[k].texture.viewDimension = wgpu::TextureViewDimension::e2D;
            break;
        case B::Tex3D:
            e[k].texture.sampleType = wgpu::TextureSampleType::Float;
            e[k].texture.viewDimension = wgpu::TextureViewDimension::e3D;
            break;
        case B::Tex3DUnfilt:
            e[k].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
            e[k].texture.viewDimension = wgpu::TextureViewDimension::e3D;
            break;
        case B::Sampler: e[k].sampler.type = wgpu::SamplerBindingType::Filtering; break;
        case B::StoreTex3D_RGBA16F:
            e[k].storageTexture.access = wgpu::StorageTextureAccess::WriteOnly;
            e[k].storageTexture.format = wgpu::TextureFormat::RGBA16Float;
            e[k].storageTexture.viewDimension = wgpu::TextureViewDimension::e3D;
            break;
        case B::StoreTex3D_R32F:
            e[k].storageTexture.access = wgpu::StorageTextureAccess::WriteOnly;
            e[k].storageTexture.format = wgpu::TextureFormat::R32Float;
            e[k].storageTexture.viewDimension = wgpu::TextureViewDimension::e3D;
            break;
        }
    }
    wgpu::BindGroupLayoutDescriptor d{};
    d.entryCount = e.size();
    d.entries = e.data();
    return ctx.device().CreateBindGroupLayout(&d);
}

struct Res {
    wgpu::Buffer buffer;
    std::uint64_t size = 0;
    wgpu::TextureView view;
    wgpu::Sampler sampler;
};
Res buf(const wgpu::Buffer& b, std::uint64_t size) {
    Res r;
    r.buffer = b;
    r.size = size;
    return r;
}
Res tex(const wgpu::TextureView& v) {
    Res r;
    r.view = v;
    return r;
}
Res smp(const wgpu::Sampler& s) {
    Res r;
    r.sampler = s;
    return r;
}
wgpu::BindGroup makeGroup(gpu::Context& ctx, const wgpu::BindGroupLayout& layout, const std::vector<Res>& res) {
    std::vector<wgpu::BindGroupEntry> e(res.size());
    for (std::size_t k = 0; k < res.size(); ++k) {
        e[k].binding = static_cast<std::uint32_t>(k);
        e[k].buffer = res[k].buffer;
        e[k].size = res[k].buffer ? res[k].size : 0;
        e[k].textureView = res[k].view;
        e[k].sampler = res[k].sampler;
    }
    wgpu::BindGroupDescriptor d{};
    d.layout = layout;
    d.entryCount = e.size();
    d.entries = e.data();
    return ctx.device().CreateBindGroup(&d);
}
wgpu::PipelineLayout makePL(gpu::Context& ctx, const std::vector<wgpu::BindGroupLayout>& groups) {
    wgpu::PipelineLayoutDescriptor d{};
    d.bindGroupLayoutCount = groups.size();
    d.bindGroupLayouts = groups.data();
    return ctx.device().CreatePipelineLayout(&d);
}
wgpu::ComputePipeline makeCompute(gpu::Context& ctx, const wgpu::ShaderModule& m, const char* entry,
                                  const wgpu::PipelineLayout& pl) {
    wgpu::ComputePipelineDescriptor d{};
    d.layout = pl;
    d.compute.module = m;
    d.compute.entryPoint = entry;
    return ctx.device().CreateComputePipeline(&d);
}
wgpu::Texture makeTex(gpu::Context& ctx, wgpu::Extent3D size, wgpu::TextureFormat f, wgpu::TextureUsage u,
                      wgpu::TextureDimension dim = wgpu::TextureDimension::e2D) {
    wgpu::TextureDescriptor td{};
    td.size = size;
    td.format = f;
    td.usage = u;
    td.dimension = dim;
    return ctx.device().CreateTexture(&td);
}

const wgpu::PassTimestampWrites* mark(gpu::FrameTimeline* t, const char* label, gpu::FrameTimeline::PassKind k) {
    return t != nullptr ? t->mark(label, k) : nullptr;
}

// The light rig: four reflection-only bands (the prototype's setBands).
void setBands(AstralRenderer::FrameU& f, const astral::State& s) {
    const float ph = s.rigPhase;
    auto put = [&](int k, glm::vec3 axis, float offset, float width, float intensity, float segments, float warm) {
        f.bands[2 * k] = glm::vec4(glm::normalize(axis), offset);
        f.bands[2 * k + 1] = glm::vec4(width, intensity * s.bandGain, segments, warm);
    };
    put(0, {std::cos(ph * 0.5f), 0.35f, std::sin(ph * 0.5f)}, 0.0f, 0.014f, 17.0f, 5.0f, 0.15f * s.warmth);
    put(1, {0.25f * std::sin(ph * 0.3f), 1.0f, 0.3f}, 0.62f, 0.02f, 9.0f, 0.0f, 0.3f + 0.4f * s.warmth);
    put(2, {0.2f, -1.0f, 0.45f + 0.2f * std::sin(ph * 0.21f)}, 0.5f, 0.012f, 11.0f, 9.0f, 0.75f * s.warmth + 0.1f);
    put(3, {1.0f, 0.0f, 0.15f}, s.sweep * 0.85f, 0.018f, 28.0f * s.sweepStrength, 0.0f, 0.0f);
}

// v2: each god's palette, normalised to unit luminance (the greyscale image is unchanged by it).
struct GodPalette {
    glm::vec3 key, rim, eye, zoneA, zoneB, fog;
};
glm::vec3 unitLuma(glm::vec3 c) { return c / std::max(glm::dot(c, glm::vec3(0.2126f, 0.7152f, 0.0722f)), 1e-4f); }
GodPalette godPalette(int arch) {
    switch (arch) {
    case 1: return {{0.80f, 0.97f, 1.12f}, {0.80f, 0.60f, 1.20f}, {0.35f, 0.90f, 1.15f}, {0.78f, 1.00f, 1.20f}, {0.92f, 0.76f, 1.20f}, {0.07f, 0.10f, 0.17f}};
    case 2: return {{0.78f, 0.50f, 1.05f}, {1.25f, 0.22f, 0.18f}, {1.10f, 0.10f, 0.06f}, {0.74f, 0.42f, 1.12f}, {1.22f, 0.28f, 0.28f}, {0.11f, 0.02f, 0.06f}};
    case 3: return {{0.55f, 1.15f, 0.78f}, {1.15f, 0.80f, 0.48f}, {0.20f, 1.05f, 0.50f}, {0.48f, 1.16f, 0.68f}, {1.22f, 0.80f, 0.42f}, {0.03f, 0.09f, 0.06f}};
    case 4: return {{1.18f, 0.92f, 0.52f}, {1.10f, 0.95f, 0.68f}, {1.05f, 0.70f, 0.18f}, {1.28f, 0.94f, 0.40f}, {0.74f, 0.86f, 1.04f}, {0.11f, 0.08f, 0.04f}};
    case 5: return {{1.15f, 0.80f, 0.75f}, {0.48f, 1.02f, 0.98f}, {0.28f, 1.02f, 0.92f}, {1.26f, 0.74f, 0.58f}, {0.52f, 1.06f, 1.02f}, {0.11f, 0.06f, 0.08f}};
    default: return {{0.85f, 0.90f, 1.05f}, {0.78f, 0.58f, 1.12f}, {0.65f, 0.42f, 1.05f}, {1.0f, 1.0f, 1.03f}, {0.86f, 0.76f, 1.10f}, {0.08f, 0.08f, 0.13f}};
    }
}

// ADR-1222: what each tier samples. The picture's composition is the same at every tier.
struct TierLevers {
    float particles;   // fraction of the authored particles simulated
    int rayTaps;       // god-ray taps (0: off)
    bool shards;
    bool fullMarch;    // the full-resolution march (no half-resolution pre-pass)
    bool exactSeek;    // a seek re-simulates from the song's start
};
TierLevers leversFor(std::uint32_t tier) {
    switch (std::min<std::uint32_t>(tier, 4)) {
    case 0: return {1.0f, 20, true, true, true};
    case 1: return {1.0f, 20, true, false, false};
    case 2: return {0.7f, 12, true, false, false};
    case 3: return {0.5f, 8, false, false, false};
    default: return {0.33f, 0, false, false, false};
    }
}

astral::State lerpState(const astral::State& a, const astral::State& b, float u) {
    astral::State s = b;
    auto L = [u](float x, float y) { return x + (y - x) * u; };
    s.C = L(a.C, b.C);
    s.S = L(a.S, b.S);
    s.temper = L(a.temper, b.temper);
    s.breath = L(a.breath, b.breath);
    s.flow = L(a.flow, b.flow);
    s.shimmer = L(a.shimmer, b.shimmer);
    s.chaos = L(a.chaos, b.chaos);
    s.smoothK = L(a.smoothK, b.smoothK);
    s.fold0 = glm::mix(a.fold0, b.fold0, u);
    s.fold1 = glm::mix(a.fold1, b.fold1, u);
    s.rigPhase = L(a.rigPhase, b.rigPhase);
    return s;
}

} // namespace

AstralRenderer::~AstralRenderer() = default;

bool AstralRenderer::wants(const scene::Scene& scene) const { return scene.astral.enabled && !pipelineFailed_; }

Result<void> AstralRenderer::init(gpu::Context& context, gpu::ShaderLibrary& shaders, wgpu::TextureFormat hdrFormat,
                                  wgpu::TextureFormat depthFormat) {
    context_ = &context;
    hdrFormat_ = hdrFormat;
    depthFormat_ = depthFormat;
    gpu::Context& ctx = context;
    const auto CS = wgpu::ShaderStage::Compute;
    const auto ALL = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment | wgpu::ShaderStage::Compute;
    frameLayout_ = makeLayout(ctx, {B::Uniform}, ALL);
    simLayout_ = makeLayout(ctx, {B::Storage, B::Storage, B::Storage, B::Storage, B::Tex3D, B::Sampler, B::Storage, B::Storage}, CS);
    resLayout_ = makeLayout(ctx, {B::ReadOnly, B::StoreTex3D_RGBA16F}, CS);
    coarseLayout_ = makeLayout(ctx, {B::Tex3D, B::StoreTex3D_R32F}, CS);
    cacheLayout_ = makeLayout(ctx, {B::Tex3DUnfilt, B::StoreTex3D_RGBA16F, B::StoreTex3D_RGBA16F}, CS);
    flakeLayout_ = makeLayout(ctx, {B::ReadOnly, B::ReadOnly, B::ReadOnly, B::Storage, B::Tex2DUnfilt, B::Storage, B::Storage}, CS);
    surfLayout_ = makeLayout(ctx, {B::Tex3D, B::Sampler, B::Tex3DUnfilt, B::Tex3D, B::Tex3D, B::Tex2DUnfilt}, wgpu::ShaderStage::Fragment);
    shardLayout_ = makeLayout(ctx, {B::ReadOnly, B::Tex2DUnfilt}, wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment);
    postLayout_ = makeLayout(ctx, {B::Tex2D, B::Sampler, B::Uniform, B::Tex2D, B::ReadOnly, B::Tex2DUnfilt},
                             wgpu::ShaderStage::Fragment | wgpu::ShaderStage::Vertex);
    wgpu::SamplerDescriptor sd{};
    sd.minFilter = wgpu::FilterMode::Linear;
    sd.magFilter = wgpu::FilterMode::Linear;
    sd.addressModeU = sd.addressModeV = sd.addressModeW = wgpu::AddressMode::ClampToEdge;
    sampler_ = ctx.device().CreateSampler(&sd);
    for (int k = 0; k < kSlots; ++k) {
        frameBufs_[k] = makeBuffer(ctx, sizeof(FrameU), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst, "astral.frame");
        frameGroups_[k] = makeGroup(ctx, frameLayout_, {buf(frameBufs_[k], sizeof(FrameU))});
    }
    renderBuf_ = makeBuffer(ctx, sizeof(FrameU), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst, "astral.render");
    renderGroup_ = makeGroup(ctx, frameLayout_, {buf(renderBuf_, sizeof(FrameU))});
    postBuf_ = makeBuffer(ctx, 96, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst, "astral.post");
    shardArgs_ = makeBuffer(ctx, 16, wgpu::BufferUsage::Storage | wgpu::BufferUsage::Indirect | wgpu::BufferUsage::CopyDst, "astral.shardArgs");
    shardBuf_ = makeBuffer(ctx, kShardMax * 48, wgpu::BufferUsage::Storage, "astral.shards");
    dummy2D_ = makeTex(ctx, {1, 1, 1}, wgpu::TextureFormat::RGBA32Float, wgpu::TextureUsage::TextureBinding);
    dummy2DView_ = dummy2D_.CreateView();
    shaders_ = &shaders; // the pipelines compile on the first frame that wants them: most scenes never do
    return {};
}

Result<void> AstralRenderer::reload(gpu::ShaderLibrary& shaders) {
    if (context_ == nullptr || !stepPipe_) {
        return {}; // never compiled: the first frame that wants them will
    }
    return createPipelines(shaders);
}

Result<void> AstralRenderer::createPipelines(gpu::ShaderLibrary& shaders) {
    gpu::Context& ctx = *context_;
    auto src = [&](const char* name) -> Result<std::string> { return shaders.loadSource(std::string("astral/") + name); };
    auto common = src("common.wgsl");
    auto latent = src("latent.wgsl");
    auto lighting = src("lighting.wgsl");
    if (!common || !latent || !lighting) {
        return fail("astral shaders: {}", !common ? common.error().message : !latent ? latent.error().message : lighting.error().message);
    }
    auto module = [&](std::initializer_list<const std::string*> parts, const char* file, const char* label) -> Result<wgpu::ShaderModule> {
        auto body = src(file);
        if (!body) return std::unexpected(body.error());
        std::string code;
        for (const std::string* p : parts) code += *p;
        code += *body;
        return shaders.compile(code, label);
    };
    auto simMod = module({&*common, &*latent}, "sim.wgsl", "astral.sim");
    auto resMod = module({&*common}, "resolve.wgsl", "astral.resolve");
    auto coarseMod = module({&*common}, "coarse.wgsl", "astral.coarse");
    auto surfMod = module({&*common, &*latent}, "surface.wgsl", "astral.surface");
    auto flakeMod = module({&*common, &*lighting}, "flakes.wgsl", "astral.flakes");
    auto shardMod = module({&*common, &*lighting}, "shards.wgsl", "astral.shards");
    auto cacheMod = module({&*common, &*latent}, "latent_cache.wgsl", "astral.cache");
    auto postMod = module({}, "post.wgsl", "astral.post");
    for (auto* m : {&simMod, &resMod, &coarseMod, &surfMod, &flakeMod, &shardMod, &cacheMod, &postMod}) {
        if (!*m) {
            return std::unexpected(m->error());
        }
    }
    const auto simPL = makePL(ctx, {frameLayout_, simLayout_});
    initPipe_ = makeCompute(ctx, *simMod, "cs_init", simPL);
    stepPipe_ = makeCompute(ctx, *simMod, "cs_step", simPL);
    splatPipe_ = makeCompute(ctx, *simMod, "cs_splat", simPL);
    resPipe_ = makeCompute(ctx, *resMod, "cs_resolve", makePL(ctx, {frameLayout_, resLayout_}));
    coarsePipe_ = makeCompute(ctx, *coarseMod, "cs_coarse", makePL(ctx, {frameLayout_, coarseLayout_}));
    cachePipe_ = makeCompute(ctx, *cacheMod, "cs_latent_cache", makePL(ctx, {frameLayout_, cacheLayout_}));
    flakePipe_ = makeCompute(ctx, *flakeMod, "cs_flakes", makePL(ctx, {frameLayout_, flakeLayout_}));
    {
        std::array<wgpu::ColorTargetState, 2> cts{};
        cts[0].format = wgpu::TextureFormat::RGBA16Float;
        cts[1].format = wgpu::TextureFormat::RG32Float;
        wgpu::FragmentState fst{};
        fst.module = *surfMod;
        fst.entryPoint = "fs_surface";
        fst.targetCount = 2;
        fst.targets = cts.data();
        wgpu::RenderPipelineDescriptor rpd{};
        rpd.label = "astral.surface";
        rpd.layout = makePL(ctx, {frameLayout_, surfLayout_});
        rpd.vertex.module = *surfMod;
        rpd.vertex.entryPoint = "vs_full";
        rpd.fragment = &fst;
        surfPipe_ = ctx.device().CreateRenderPipeline(&rpd);
    }
    {
        wgpu::ColorTargetState cts{};
        cts.format = wgpu::TextureFormat::RGBA32Float;
        wgpu::FragmentState fst{};
        fst.module = *surfMod;
        fst.entryPoint = "fs_march_half";
        fst.targetCount = 1;
        fst.targets = &cts;
        wgpu::RenderPipelineDescriptor rpd{};
        rpd.label = "astral.march";
        rpd.layout = makePL(ctx, {frameLayout_, surfLayout_});
        rpd.vertex.module = *surfMod;
        rpd.vertex.entryPoint = "vs_full";
        rpd.fragment = &fst;
        halfPipe_ = ctx.device().CreateRenderPipeline(&rpd);
    }
    {
        wgpu::ColorTargetState cts{};
        cts.format = hdrFormat_;
        wgpu::FragmentState fst{};
        fst.module = *shardMod;
        fst.entryPoint = "fs_shard";
        fst.targetCount = 1;
        fst.targets = &cts;
        wgpu::DepthStencilState ds{};
        ds.format = wgpu::TextureFormat::Depth32Float;
        ds.depthWriteEnabled = wgpu::OptionalBool::True;
        ds.depthCompare = wgpu::CompareFunction::Less;
        wgpu::RenderPipelineDescriptor rpd{};
        rpd.label = "astral.shards";
        rpd.layout = makePL(ctx, {frameLayout_, shardLayout_});
        rpd.vertex.module = *shardMod;
        rpd.vertex.entryPoint = "vs_shard";
        rpd.fragment = &fst;
        rpd.depthStencil = &ds;
        rpd.primitive.cullMode = wgpu::CullMode::None;
        shardPipe_ = ctx.device().CreateRenderPipeline(&rpd);
    }
    {
        // the combine: the scene's HDR (replaced) and depth (tested and written; the void at the far plane)
        wgpu::ColorTargetState cts{};
        cts.format = hdrFormat_;
        wgpu::FragmentState fst{};
        fst.module = *postMod;
        fst.entryPoint = "fs_combine_scene";
        fst.targetCount = 1;
        fst.targets = &cts;
        wgpu::DepthStencilState ds{};
        ds.format = depthFormat_;
        ds.depthWriteEnabled = wgpu::OptionalBool::True;
        ds.depthCompare = wgpu::CompareFunction::LessEqual;
        wgpu::RenderPipelineDescriptor rpd{};
        rpd.label = "astral.combine";
        rpd.layout = makePL(ctx, {postLayout_});
        rpd.vertex.module = *postMod;
        rpd.vertex.entryPoint = "vs_full";
        rpd.fragment = &fst;
        rpd.depthStencil = &ds;
        combinePipe_ = ctx.device().CreateRenderPipeline(&rpd);
    }
    return {};
}

void AstralRenderer::ensureState(const Config& c) {
    if (config_ == c && P_) {
        return;
    }
    gpu::Context& ctx = *context_;
    config_ = c;
    const std::uint32_t N = c.particles;
    const int R = c.gridRes;
    const int RC = (R + 7) / 8;
    stateBytes_ = static_cast<std::uint64_t>(N) * 16;
    P_ = makeBuffer(ctx, stateBytes_, wgpu::BufferUsage::Storage, "astral.P");
    V_ = makeBuffer(ctx, stateBytes_, wgpu::BufferUsage::Storage, "astral.V");
    A_ = makeBuffer(ctx, stateBytes_, wgpu::BufferUsage::Storage, "astral.A");
    TG_ = makeBuffer(ctx, stateBytes_, wgpu::BufferUsage::Storage, "astral.TG");
    TD_ = makeBuffer(ctx, stateBytes_, wgpu::BufferUsage::Storage, "astral.TD");
    gridBytes_ = static_cast<std::uint64_t>(R) * R * R * 2 * 4;
    grid_ = makeBuffer(ctx, gridBytes_, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst, "astral.grid");
    const auto R3 = static_cast<std::uint32_t>(R);
    densTex_ = makeTex(ctx, {R3, R3, R3}, wgpu::TextureFormat::RGBA16Float,
                       wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::TextureBinding, wgpu::TextureDimension::e3D);
    coarseTex_ = makeTex(ctx, {std::uint32_t(RC), std::uint32_t(RC), std::uint32_t(RC)}, wgpu::TextureFormat::R32Float,
                         wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::TextureBinding, wgpu::TextureDimension::e3D);
    cacheTex_ = makeTex(ctx, {kCacheRes, kCacheRes, kCacheRes}, wgpu::TextureFormat::RGBA16Float,
                        wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::TextureBinding, wgpu::TextureDimension::e3D);
    cacheTexCoarse_ = makeTex(ctx, {kCacheRes, kCacheRes, kCacheRes}, wgpu::TextureFormat::RGBA16Float,
                              wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::TextureBinding, wgpu::TextureDimension::e3D);
    densView_ = densTex_.CreateView();
    coarseView_ = coarseTex_.CreateView();
    cacheView_ = cacheTex_.CreateView();
    cacheViewCoarse_ = cacheTexCoarse_.CreateView();
    simGroup_ = makeGroup(ctx, simLayout_, {buf(P_, stateBytes_), buf(V_, stateBytes_), buf(A_, stateBytes_), buf(grid_, gridBytes_),
                                            tex(densView_), smp(sampler_), buf(TG_, stateBytes_), buf(TD_, stateBytes_)});
    resGroup_ = makeGroup(ctx, resLayout_, {buf(grid_, gridBytes_), tex(densView_)});
    coarseGroup_ = makeGroup(ctx, coarseLayout_, {tex(densView_), tex(coarseView_)});
    cacheGroup_ = makeGroup(ctx, cacheLayout_, {tex(coarseView_), tex(cacheView_), tex(cacheViewCoarse_)});
    discontinuity_ = true;
    if (width_ != 0) {
        rebuildGroups();
    }
}

void AstralRenderer::ensureTargets(std::uint32_t width, std::uint32_t height) {
    if (width == width_ && height == height_ && accum_) {
        return;
    }
    gpu::Context& ctx = *context_;
    width_ = width;
    height_ = height;
    accumBytes_ = static_cast<std::uint64_t>(width) * height * 7 * 4;
    accum_ = makeBuffer(ctx, accumBytes_, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst, "astral.accum");
    const std::uint32_t HW = (width + 1) / 2, HH = (height + 1) / 2;
    halfTex_ = makeTex(ctx, {HW, HH, 1}, wgpu::TextureFormat::RGBA32Float, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding);
    surfColor_ = makeTex(ctx, {width, height, 1}, wgpu::TextureFormat::RGBA16Float, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding);
    surfDepth_ = makeTex(ctx, {width, height, 1}, wgpu::TextureFormat::RG32Float, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding);
    shardDepth_ = makeTex(ctx, {width, height, 1}, wgpu::TextureFormat::Depth32Float, wgpu::TextureUsage::RenderAttachment);
    halfView_ = halfTex_.CreateView();
    surfColorView_ = surfColor_.CreateView();
    surfDepthView_ = surfDepth_.CreateView();
    shardDepthView_ = shardDepth_.CreateView();
    rebuildGroups();
}

void AstralRenderer::rebuildGroups() {
    if (!P_ || !accum_) {
        return;
    }
    gpu::Context& ctx = *context_;
    flakeGroup_ = makeGroup(ctx, flakeLayout_, {buf(P_, stateBytes_), buf(V_, stateBytes_), buf(A_, stateBytes_), buf(accum_, accumBytes_),
                                                tex(surfDepthView_), buf(shardBuf_, kShardMax * 48), buf(shardArgs_, 16)});
    surfGroup_ = makeGroup(ctx, surfLayout_, {tex(densView_), smp(sampler_), tex(coarseView_), tex(cacheView_), tex(cacheViewCoarse_), tex(halfView_)});
    halfGroup_ = makeGroup(ctx, surfLayout_, {tex(densView_), smp(sampler_), tex(coarseView_), tex(cacheView_), tex(cacheViewCoarse_), tex(dummy2DView_)});
    shardGroup_ = makeGroup(ctx, shardLayout_, {buf(shardBuf_, kShardMax * 48), tex(surfDepthView_)});
    combineGroup_ = makeGroup(ctx, postLayout_, {tex(surfColorView_), smp(sampler_), buf(postBuf_, 96), tex(surfColorView_), buf(accum_, accumBytes_),
                                                 tex(surfDepthView_)});
}

astral::State AstralRenderer::conductAt(const scene::AstralForge& a, double t) const {
    if (a.song) {
        return astral::conductSong(t, a.song->song, a.song->score, a.live);
    }
    // live: the composition conducted the frame; a sub-step between frames interpolates from the previous one
    const double span = a.time - liveFromT_;
    const float u = span > 1e-6 ? static_cast<float>(std::clamp((t - liveFromT_) / span, 0.0, 1.0)) : 1.0f;
    return lerpState(liveFrom_, a.state, u);
}

void AstralRenderer::fillFrame(FrameU& f, const scene::AstralForge& a, const astral::State& s, const astral::State& sPrev,
                               double tPrev, double t, std::uint64_t step, const Camera& cam) const {
    const std::uint32_t N = std::max<std::uint32_t>(activeN_, 256);
    const int R = config_.gridRes;
    const float cell = config_.gridSize / static_cast<float>(R);
    f = FrameU{};
    f.viewProj = cam.viewProj;
    f.invViewProj = glm::inverse(cam.viewProj);
    f.cam = glm::vec4(cam.eye, static_cast<float>(t));
    f.camFwd = glm::vec4(glm::normalize(cam.target - cam.eye), 2.0f * std::tan(cam.fovY * 0.5f) / static_cast<float>(cam.height));
    f.screen = glm::vec4(cam.width, cam.height, 1.0f / cam.width, 1.0f / cam.height);
    f.ent0 = glm::vec4(s.C, sPrev.C, s.S, s.temper);
    f.ent1 = glm::vec4(s.archA, s.archB, s.morph, s.breath);
    f.ent2 = glm::vec4(s.mass, s.flash, s.flow, s.shimmer);
    f.ent3 = glm::vec4(s.blast, s.heatInject, s.chaos, s.smoothK);
    f.fold0 = s.fold0;
    f.fold1 = s.fold1;
    const glm::vec3 origin = s.centre - glm::vec3(config_.gridSize * 0.5f);
    f.grid0 = glm::vec4(origin, cell);
    f.grid1 = glm::vec4(static_cast<float>(R), 1.0f / static_cast<float>(R), 1.0f, static_cast<float>(N));
    f.sim = glm::vec4(1.0f / static_cast<float>(kSimRate), static_cast<float>(step), 2026.0f, 4.0f); // approach E
    setBands(f, s);
    f.rig = glm::vec4(s.rigPhase, s.sweep, s.sweepStrength, s.flicker);
    if (a.song) {
        const astral::AudioAtT au = astral::sampleSong(a.song->song, t);
        f.audio0 = au.env0;
        f.audio1 = glm::vec4(au.env1.x, au.rms, au.kickEnv, au.snareEnv);
    }
    f.look = glm::vec4(s.exposure, 0.08f, s.haze, 0.013f);
    // density norm: ~40 bound particles per cell form the surface at 2M particles in a 20/192 cell; it scales with the
    // simulated count and the cell volume so the iso level means the same thing at every tier
    const float perCell = 40.0f * (static_cast<float>(N) / 2097152.0f) * std::pow(cell / (20.0f / 192.0f), 2.0f);
    f.flags = glm::vec4(0.0f, s.mass, 1.0f / perCell, s.gratingUm);
    f.entity = glm::vec4(s.centre, s.scale);
    f.misc = glm::vec4(s.fall, s.escape, s.strobe, s.appendWeight);
    f.fp0 = sPrev.fold0;
    f.fp1 = sPrev.fold1;
    f.fp2 = glm::vec4(static_cast<float>(tPrev), sPrev.breath, s.warpAdvect, s.tendonFlow);
    f.ext = glm::vec4(s.sharpSpread, s.tendonWeight, s.metaRadius, s.metaFace);
    {   // the latent cache's box, framed on the shot (LOD by framing), clamped to the density box
        const float half = std::clamp(0.4f * glm::length(cam.eye - cam.target), 0.5f, config_.gridSize * 0.5f);
        const glm::vec3 lo = s.centre - glm::vec3(config_.gridSize * 0.5f);
        const glm::vec3 c = glm::clamp(cam.target, lo + half, lo + glm::vec3(config_.gridSize) - half);
        f.cbox = glm::vec4(c - half, 2.0f * half);
    }
    {
        const GodPalette pa = godPalette(static_cast<int>(s.archA)), pb = godPalette(static_cast<int>(s.archB));
        const float m = s.morph;
        auto mx = [&](glm::vec3 x, glm::vec3 y) { return unitLuma(glm::mix(x, y, m)); };
        f.pal0 = glm::vec4(mx(pa.key, pb.key), s.paletteStrength);
        f.pal1 = glm::vec4(mx(pa.rim, pb.rim), s.rimLight);
        f.pal2 = glm::vec4(mx(pa.eye, pb.eye), s.eyeGlow);
        f.pal3 = glm::vec4(mx(pa.zoneA, pb.zoneA), s.keyLight);
        f.pal4 = glm::vec4(mx(pa.zoneB, pb.zoneB), s.absorb);
        f.atm0 = glm::vec4(glm::mix(pa.fog, pb.fog, m), s.atmosphere);
        f.lg0 = glm::vec4(s.legible, s.absorbOn, 0.0f, 0.0f);
    }
    const TierLevers lv = leversFor(tier_);
    f.it3 = glm::vec4(lv.fullMarch ? 0.0f : 1.0f, s.collapseAt > -1e8f ? static_cast<float>(t) - s.collapseAt : -1.0f, 0.0f, 0.0f);
    f.it2 = glm::vec4(1.0f, lv.shards ? 1.0f : 0.0f, 12.0f, 0.65f);
}

void AstralRenderer::dispatch1D(wgpu::ComputePassEncoder& cp, std::uint32_t n) const {
    const std::uint32_t groups = (n + 255) / 256;
    const std::uint32_t gx = std::min(groups, 32768u);
    cp.DispatchWorkgroups(gx, (groups + gx - 1) / gx, 1);
}

void AstralRenderer::initParticles(const scene::AstralForge& a, double t, const Camera& cam) {
    FrameU fu{};
    const astral::State s = conductAt(a, std::max(t, 0.0));
    fillFrame(fu, a, s, s, t, t, 0, cam);
    // cs_init seeds every slot, not only the active prefix: a tier that raises the count finds placed matter
    fu.grid1.w = static_cast<float>(config_.particles);
    context_->queue().WriteBuffer(frameBufs_[0], 0, &fu, sizeof(fu));
    wgpu::CommandEncoder enc = context_->device().CreateCommandEncoder();
    enc.ClearBuffer(grid_, 0, gridBytes_);
    {
        auto cp = enc.BeginComputePass();
        cp.SetPipeline(initPipe_);
        cp.SetBindGroup(0, frameGroups_[0]);
        cp.SetBindGroup(1, simGroup_);
        dispatch1D(cp, config_.particles);
        cp.End();
    }
    wgpu::CommandBuffer cb = enc.Finish();
    context_->queue().Submit(1, &cb);
    simStep_ = stepAt(t);
    step_ = 0;
    lastStepState_ = s;
}

int AstralRenderer::simulateTo(wgpu::CommandEncoder& frameEnc, const scene::AstralForge& a, double t, const Camera& cam) {
    gpu::Context& ctx = *context_;
    const int R = config_.gridRes;
    const int RC = (R + 7) / 8;
    int lastSlot = -1;
    std::uint32_t chunks = 0;
    for (;;) {
        std::vector<double> stepTimes;
        const std::int64_t target = stepAt(t);
        while (simStep_ < target && stepTimes.size() < static_cast<std::size_t>(kSlots)) {
            ++simStep_;
            stepTimes.push_back(simT());
        }
        if (stepTimes.empty()) {
            break;
        }
        const bool more = simStep_ < target;
        // a backlog beyond the slots is simulated in submits of its own, before the frame's
        wgpu::CommandEncoder own;
        if (more) {
            own = ctx.device().CreateCommandEncoder();
        }
        wgpu::CommandEncoder& enc = more ? own : frameEnc;
        for (std::size_t k = 0; k < stepTimes.size(); ++k) {
            const double ts = stepTimes[k];
            const astral::State s = conductAt(a, std::max(ts, 0.0)); // the pre-roll before 0 holds the opening state
            FrameU fu{};
            fillFrame(fu, a, s, lastStepState_, ts - 1.0 / kSimRate, ts, step_ + k, cam);
            ctx.queue().WriteBuffer(frameBufs_[k], 0, &fu, sizeof(fu));
            lastStepState_ = s;
            // the step, then the density it leaves (the next step's repulsion and, last, the frame's surface)
            {
                wgpu::ComputePassDescriptor d{};
                if (!more && k == 0) d.timestampWrites = mark(timeline_, "astral.sim", gpu::FrameTimeline::PassKind::Compute);
                auto cp = enc.BeginComputePass(&d);
                cp.SetPipeline(stepPipe_);
                cp.SetBindGroup(0, frameGroups_[k]);
                cp.SetBindGroup(1, simGroup_);
                dispatch1D(cp, activeN_);
                cp.End();
            }
            enc.ClearBuffer(grid_, 0, gridBytes_);
            {
                wgpu::ComputePassDescriptor d{};
                if (!more && k + 1 == stepTimes.size()) d.timestampWrites = mark(timeline_, "astral.density", gpu::FrameTimeline::PassKind::Compute);
                auto cp = enc.BeginComputePass(&d);
                cp.SetBindGroup(0, frameGroups_[k]);
                cp.SetPipeline(splatPipe_);
                cp.SetBindGroup(1, simGroup_);
                dispatch1D(cp, activeN_);
                cp.SetPipeline(resPipe_);
                cp.SetBindGroup(1, resGroup_);
                cp.DispatchWorkgroups((R + 3) / 4, (R + 3) / 4, (R + 3) / 4);
                cp.SetPipeline(coarsePipe_);
                cp.SetBindGroup(1, coarseGroup_);
                cp.DispatchWorkgroups((RC + 3) / 4, (RC + 3) / 4, (RC + 3) / 4);
                cp.End();
            }
        }
        step_ += stepTimes.size();
        stats_.steps += static_cast<std::uint32_t>(stepTimes.size());
        lastSlot = static_cast<int>(stepTimes.size()) - 1;
        if (more) {
            wgpu::CommandBuffer cb = own.Finish();
            ctx.queue().Submit(1, &cb);
            if (++chunks % 16 == 0) {
                ctx.waitForQueue(); // a long re-simulation: keep the queue from running away
            }
        } else {
            break;
        }
    }
    return lastSlot;
}

void AstralRenderer::encode(const EnvironmentFrame& frame) {
    if (frame.encoder == nullptr || frame.scene == nullptr || context_ == nullptr) {
        return;
    }
    if (!stepPipe_) {
        if (auto r = createPipelines(*shaders_); !r) {
            log::error("astral: {}", r.error().message);
            pipelineFailed_ = true;
            return;
        }
    }
    const scene::AstralForge& a = frame.scene->astral;
    timeline_ = frame.timeline;
    stats_ = AstralStats{};
    tier_ = frame.quality != nullptr ? frame.quality->astralTier : 1u;
    stats_.tier = tier_;
    const TierLevers lv = leversFor(tier_);

    ensureState(Config{a.particles, a.gridRes, a.gridSize});
    ensureTargets(frame.width, frame.height);
    const auto scaled = static_cast<std::uint32_t>(static_cast<double>(a.particles) * lv.particles * std::clamp(a.density, 0.05f, 1.0f));
    activeN_ = std::clamp<std::uint32_t>(scaled / 256 * 256, 65536u, a.particles);
    stats_.particlesActive = activeN_;

    Camera cam;
    cam.width = frame.width;
    cam.height = frame.height;
    const auto& sc = frame.scene->camera;
    cam.viewProj = sc.projection(static_cast<float>(frame.width) / static_cast<float>(std::max(frame.height, 1u))) * sc.view();
    cam.eye = sc.position;
    cam.target = sc.target;
    cam.fovY = sc.effectiveFovY();

    const double t = frame.time.renderTime;
    const bool jumped = !initialised_ || discontinuity_ || a.epoch != lastEpoch_ || stepAt(t) < simStep_ || t - simT() > 1.0;
    if (jumped) {
        stats_.reset = true;
        // exact at the offline tier in song mode: re-simulate from the song's start, as play did; otherwise ADR-360's
        // pre-roll (the god is in the same phase of the score, its matter visually equivalent)
        const double from = (lv.exactSeek && a.song) ? -static_cast<double>(a.preroll) : t - static_cast<double>(a.preroll);
        liveFrom_ = a.state;
        liveFromT_ = t;
        initParticles(a, from, cam);
        if (simStep_ < stepAt(t) - 1) {
            wgpu::CommandEncoder pre = context_->device().CreateCommandEncoder();
            const double upTo = t - 1.0 / kSimRate; // the frame's own steps go in the frame's encoder
            (void)simulateTo(pre, a, upTo, cam);
            wgpu::CommandBuffer cb = pre.Finish();
            context_->queue().Submit(1, &cb);
        }
        initialised_ = true;
        discontinuity_ = false;
        lastEpoch_ = a.epoch;
    }
    (void)simulateTo(*frame.encoder, a, t, cam);
    if (!a.song) {
        liveFrom_ = a.state;
        liveFromT_ = t;
    }

    // ---- the frame: conducted at its own second, with the frame's camera ----
    const astral::State s = a.song ? conductAt(a, t) : a.state;
    stats_.coherence = s.C;
    stats_.arch = static_cast<int>(s.archA);
    FrameU fu{};
    fillFrame(fu, a, s, lastStepState_, t - 1.0 / kSimRate, t, step_, cam);
    context_->queue().WriteBuffer(renderBuf_, 0, &fu, sizeof(fu));
    wgpu::CommandEncoder& enc = *frame.encoder;
    {
        wgpu::ComputePassDescriptor d{};
        d.timestampWrites = mark(timeline_, "astral.cache", gpu::FrameTimeline::PassKind::Compute);
        auto cp = enc.BeginComputePass(&d);
        cp.SetBindGroup(0, renderGroup_);
        cp.SetPipeline(cachePipe_);
        cp.SetBindGroup(1, cacheGroup_);
        cp.DispatchWorkgroups(kCacheRes / 4, kCacheRes / 4, kCacheRes / 4);
        cp.End();
    }
    if (!lv.fullMarch) {
        wgpu::RenderPassColorAttachment ca{};
        ca.view = halfView_;
        ca.loadOp = wgpu::LoadOp::Clear;
        ca.storeOp = wgpu::StoreOp::Store;
        ca.clearValue = {-1, 0, 0, 0};
        wgpu::RenderPassDescriptor rp{};
        rp.colorAttachmentCount = 1;
        rp.colorAttachments = &ca;
        rp.timestampWrites = mark(timeline_, "astral.march", gpu::FrameTimeline::PassKind::Render);
        auto r = enc.BeginRenderPass(&rp);
        r.SetPipeline(halfPipe_);
        r.SetBindGroup(0, renderGroup_);
        r.SetBindGroup(1, halfGroup_);
        r.Draw(3);
        r.End();
    }
    {
        std::array<wgpu::RenderPassColorAttachment, 2> ca{};
        ca[0].view = surfColorView_;
        ca[0].loadOp = wgpu::LoadOp::Clear;
        ca[0].storeOp = wgpu::StoreOp::Store;
        ca[0].clearValue = {0, 0, 0, 1};
        ca[1].view = surfDepthView_;
        ca[1].loadOp = wgpu::LoadOp::Clear;
        ca[1].storeOp = wgpu::StoreOp::Store;
        ca[1].clearValue = {1e9, 1, 0, 0};
        wgpu::RenderPassDescriptor rp{};
        rp.colorAttachmentCount = 2;
        rp.colorAttachments = ca.data();
        rp.timestampWrites = mark(timeline_, "astral.surface", gpu::FrameTimeline::PassKind::Render);
        auto r = enc.BeginRenderPass(&rp);
        r.SetPipeline(surfPipe_);
        r.SetBindGroup(0, renderGroup_);
        r.SetBindGroup(1, surfGroup_);
        r.Draw(3);
        r.End();
    }
    enc.ClearBuffer(accum_, 0, accumBytes_);
    {
        const std::uint32_t a0[4] = {12, 0, 0, 0};
        context_->queue().WriteBuffer(shardArgs_, 0, a0, sizeof(a0));
    }
    {
        wgpu::ComputePassDescriptor d{};
        d.timestampWrites = mark(timeline_, "astral.flakes", gpu::FrameTimeline::PassKind::Compute);
        auto cp = enc.BeginComputePass(&d);
        cp.SetPipeline(flakePipe_);
        cp.SetBindGroup(0, renderGroup_);
        cp.SetBindGroup(1, flakeGroup_);
        dispatch1D(cp, activeN_);
        cp.End();
    }
    {
        // v2 atmosphere: an off-screen source behind and above the god, god rays through the gaps in forming matter
        const glm::vec3 lightW = s.centre + glm::vec3(0.0f, 10.0f, -16.0f) * s.scale;
        const glm::vec4 lc = fu.viewProj * glm::vec4(lightW, 1.0f);
        glm::vec2 luv(0.5f, -0.3f);
        float lvis = 0.0f;
        if (lc.w > 0.1f) {
            luv = glm::vec2(lc.x / lc.w * 0.5f + 0.5f, 0.5f - lc.y / lc.w * 0.5f);
            lvis = 1.0f;
        }
        const GodPalette gp = godPalette(static_cast<int>(s.archA));
        const glm::vec3 rayC = unitLuma(glm::mix(gp.key, glm::vec3(1.0f), 0.3f));
        const float rays = lv.rayTaps > 0 ? s.godRays : 0.0f;
        const float post[24] = {s.exposure, s.bloom, 0.0f, 0.0f, static_cast<float>(step_), 1.0f, 1.0f, 1.6f,
                                luv.x, luv.y, lvis, rays, fu.atm0.x, fu.atm0.y, fu.atm0.z, s.atmosphere,
                                rayC.x, rayC.y, rayC.z, 0.12f, static_cast<float>(std::max(lv.rayTaps, 1)), 0.0f, 0.0f, 0.0f};
        context_->queue().WriteBuffer(postBuf_, 0, post, sizeof(post));
    }
    {
        wgpu::RenderPassColorAttachment ca{};
        ca.view = frame.hdr;
        ca.loadOp = wgpu::LoadOp::Load;
        ca.storeOp = wgpu::StoreOp::Store;
        wgpu::RenderPassDepthStencilAttachment da{};
        da.view = frame.depth;
        da.depthLoadOp = wgpu::LoadOp::Load;
        da.depthStoreOp = wgpu::StoreOp::Store;
        wgpu::RenderPassDescriptor rp{};
        rp.colorAttachmentCount = 1;
        rp.colorAttachments = &ca;
        rp.depthStencilAttachment = &da;
        rp.timestampWrites = mark(timeline_, "astral.combine", gpu::FrameTimeline::PassKind::Render);
        auto r = enc.BeginRenderPass(&rp);
        r.SetPipeline(combinePipe_);
        r.SetBindGroup(0, combineGroup_);
        r.Draw(3);
        r.End();
    }
    if (lv.shards) {
        wgpu::RenderPassColorAttachment ca{};
        ca.view = frame.hdr;
        ca.loadOp = wgpu::LoadOp::Load;
        ca.storeOp = wgpu::StoreOp::Store;
        wgpu::RenderPassDepthStencilAttachment da{};
        da.view = shardDepthView_;
        da.depthLoadOp = wgpu::LoadOp::Clear;
        da.depthStoreOp = wgpu::StoreOp::Discard;
        da.depthClearValue = 1.0f;
        wgpu::RenderPassDescriptor rp{};
        rp.colorAttachmentCount = 1;
        rp.colorAttachments = &ca;
        rp.depthStencilAttachment = &da;
        rp.timestampWrites = mark(timeline_, "astral.shards", gpu::FrameTimeline::PassKind::Render);
        auto r = enc.BeginRenderPass(&rp);
        r.SetPipeline(shardPipe_);
        r.SetBindGroup(0, renderGroup_);
        r.SetBindGroup(1, shardGroup_);
        r.DrawIndirect(shardArgs_, 0);
        r.End();
    }
    lastFrameTime_ = t;
    if (std::getenv("AVGEN_ASTRAL_TRACE") != nullptr) { // diagnostics: the steps each frame took
        log::info("astral t={:.4f} steps={} reset={} tier={} N={}", t, stats_.steps, stats_.reset, stats_.tier, stats_.particlesActive);
    }
}

} // namespace avgen::rendering
