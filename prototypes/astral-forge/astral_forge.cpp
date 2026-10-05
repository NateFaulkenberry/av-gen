// THE ASTRAL FORGE -- scene experiment prototype (docs/prototypes/astral-forge/). DISPOSABLE, OFF by default
// (-DAVGEN_ASTRAL_FORGE_PROTOTYPE=ON). Not linked into anything.
//
// One small hard-coded GPU renderer for the hybrid particle / implicit entity:
//   conductor (CPU, f(t))  ->  particles (60 Hz fixed step, bound to a latent SDF anatomy by coherence)
//   -> u32 density splat -> blur -> 3-D texture -> raymarched iso-surface, sharpened toward the latent
//   -> flakes splatted in compute (glints), depth-tested against the surface -> combine -> bloom -> filmic.
//
//   --test 1..6         the brief's six tests (§18). 6 is driven by a real track.
//   --approach A..E     the five architectures of §15 (E, the hybrid, is the default).
//   --at T              render a still at test time T (--png). Simulation pre-rolls 6 s before.
//   --clip DIR          render the whole test (or --from/--to) at --fps as PNGs.
//   --bench             realtime-style benchmark: 60 Hz, one step per frame, no readbacks; prints JSON.
//
// Shaders are read from ASTRAL_SHADER_DIR at run time.

#include "astral_audio.hpp"
#include "conductor.hpp"

#include "assets/image.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/readback.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

using namespace avgen;
using Clock = std::chrono::steady_clock;

namespace {

double msSince(Clock::time_point a) { return std::chrono::duration<double, std::milli>(Clock::now() - a).count(); }

struct Stat {
    std::vector<double> v;
    void add(double x) { v.push_back(x); }
    double pct(double q) const {
        if (v.empty()) return -1.0;
        std::vector<double> s = v;
        std::sort(s.begin(), s.end());
        return s[static_cast<std::size_t>(std::clamp(q * (s.size() - 1) + 0.5, 0.0, double(s.size() - 1)))];
    }
};

struct FrameU {
    glm::mat4 viewProj, invViewProj;
    glm::vec4 cam, camFwd, screen;
    glm::vec4 ent0, ent1, ent2, ent3;
    glm::vec4 fold0, fold1;
    glm::vec4 grid0, grid1;
    glm::vec4 sim;
    glm::vec4 bands[8];
    glm::vec4 rig, audio0, audio1, look, flags, entity, misc, ext;
};

std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) { std::fprintf(stderr, "cannot read %s\n", path.c_str()); std::exit(7); }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
std::string shader(const char* name) { return readFile(std::string(ASTRAL_SHADER_DIR) + "/" + name); }

wgpu::ShaderModule compile(gpu::Context& ctx, const std::string& src, const char* label) {
    wgpu::ShaderSourceWGSL wgsl{};
    wgsl.code = src.c_str();
    wgpu::ShaderModuleDescriptor desc{};
    desc.nextInChain = &wgsl;
    desc.label = label;
    return ctx.device().CreateShaderModule(&desc);
}
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
        case B::Tex2D: e[k].texture.sampleType = wgpu::TextureSampleType::Float; e[k].texture.viewDimension = wgpu::TextureViewDimension::e2D; break;
        case B::Tex2DUnfilt: e[k].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat; e[k].texture.viewDimension = wgpu::TextureViewDimension::e2D; break;
        case B::Tex3D: e[k].texture.sampleType = wgpu::TextureSampleType::Float; e[k].texture.viewDimension = wgpu::TextureViewDimension::e3D; break;
        case B::Tex3DUnfilt: e[k].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat; e[k].texture.viewDimension = wgpu::TextureViewDimension::e3D; break;
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
Res buf(const wgpu::Buffer& b, std::uint64_t size) { Res r; r.buffer = b; r.size = size; return r; }
Res tex(const wgpu::TextureView& v) { Res r; r.view = v; return r; }
Res smp(const wgpu::Sampler& s) { Res r; r.sampler = s; return r; }
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
wgpu::ComputePipeline makeCompute(gpu::Context& ctx, const wgpu::ShaderModule& m, const char* entry, const wgpu::PipelineLayout& pl) {
    wgpu::ComputePipelineDescriptor d{};
    d.layout = pl;
    d.compute.module = m;
    d.compute.entryPoint = entry;
    return ctx.device().CreateComputePipeline(&d);
}

// The light rig: four reflection-only bands. Directions are unit vectors; offset puts a band off a great
// circle (a ring light); segments > 0 cut it into strips (strip softboxes against a black sweep).
void setBands(FrameU& f, const astral::State& s) {
    const float ph = s.rigPhase;
    auto put = [&](int k, glm::vec3 axis, float offset, float width, float intensity, float segments, float warm) {
        f.bands[2 * k] = glm::vec4(glm::normalize(axis), offset);
        f.bands[2 * k + 1] = glm::vec4(width, intensity * s.bandGain, segments, warm);
    };
    put(0, {std::cos(ph * 0.5f), 0.35f, std::sin(ph * 0.5f)}, 0.0f, 0.014f, 17.0f, 5.0f, 0.15f * s.warmth);
    put(1, {0.25f * std::sin(ph * 0.3f), 1.0f, 0.3f}, 0.62f, 0.02f, 9.0f, 0.0f, 0.3f + 0.4f * s.warmth);
    put(2, {0.2f, -1.0f, 0.45f + 0.2f * std::sin(ph * 0.21f)}, 0.5f, 0.012f, 11.0f, 9.0f, 0.75f * s.warmth + 0.1f);
    // the sweep band: a vertical strip that crosses the entity on a snare
    put(3, {1.0f, 0.0f, 0.15f}, s.sweep * 0.85f, 0.018f, 28.0f * s.sweepStrength, 0.0f, 0.0f);
}

struct Options {
    int test = 1;
    char approach = 'E';
    std::uint32_t n = 2u << 20;
    int gridRes = 192;
    float gridSize = -1.0f; // default per test (the choir needs a tighter box for its small faces)
    std::uint32_t width = 1920, height = 1080;
    double at = -1.0, from = 0.0, to = -1.0, fps = 30.0;
    std::string png, clip, song;
    bool bench = false;
    std::uint32_t benchFrames = 240, benchWarm = 60;
    float preroll = 6.0f;
    int debug = 0;
    double songStart = 0.0; // TEST 06: where the excerpt starts in the song
};

astral::State conduct(const Options& o, float t, const astral::SongAnalysis& song, const astral::Score& score) {
    switch (o.test) {
    case 1: return astral::test01(t);
    case 2: return astral::test02(t);
    case 3: return astral::test03(t);
    case 4: return astral::test04(t);
    case 5: return astral::test05(t);
    default: return astral::test06(t, o.songStart, song, score);
    }
}
double testDuration(int test) {
    switch (test) {
    case 1: return 14.0;
    case 2: return 12.0;
    case 3: return 14.0;
    case 4: return 15.0;
    case 5: return 16.0;
    default: return 48.0;
    }
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--test") o.test = std::stoi(next());
        else if (a == "--approach") o.approach = next()[0];
        else if (a == "--n") o.n = static_cast<std::uint32_t>(std::stoul(next()));
        else if (a == "--grid") o.gridRes = std::stoi(next());
        else if (a == "--grid-size") o.gridSize = std::stof(next());
        else if (a == "--at") o.at = std::stod(next());
        else if (a == "--from") o.from = std::stod(next());
        else if (a == "--to") o.to = std::stod(next());
        else if (a == "--fps") o.fps = std::stod(next());
        else if (a == "--png") o.png = next();
        else if (a == "--clip") o.clip = next();
        else if (a == "--song") o.song = next();
        else if (a == "--song-start") o.songStart = std::stod(next());
        else if (a == "--bench") o.bench = true;
        else if (a == "--frames") o.benchFrames = static_cast<std::uint32_t>(std::stoul(next()));
        else if (a == "--warm") o.benchWarm = static_cast<std::uint32_t>(std::stoul(next()));
        else if (a == "--preroll") o.preroll = std::stof(next());
        else if (a == "--debug") o.debug = std::stoi(next());
        else if (a == "--size") { const auto s = next(); std::sscanf(s.c_str(), "%ux%u", &o.width, &o.height); }
        else { std::fprintf(stderr, "unknown arg %s\n", a.c_str()); return 2; }
    }
    const int approach = std::clamp(o.approach - 'A', 0, 4);
    if (o.gridSize <= 0.0f) o.gridSize = o.test == 4 ? 12.5f : 20.0f;
    if (o.song.empty()) o.song = std::string(std::getenv("HOME")) + "/Desktop/Nate/Fireballs.mp3";

    // ---- audio ----
    const auto cacheDir = std::filesystem::path(argv[0]).parent_path() / "cache";
    std::filesystem::create_directories(cacheDir);
    const std::string cache = (cacheDir / (std::filesystem::path(o.song).stem().string() + ".astral.bin")).string();
    const astral::SongAnalysis song = astral::loadSong(o.song, cache);
    const astral::Score score = astral::buildScore(song);
    std::fprintf(stderr, "song: %.1f s, %.1f bpm, %zu beats, %zu sections, %zu phrases, %zu kicks, %zu snares (analysis %.1f s)\n",
                 song.duration, song.tempoBpm, song.beats.size(), song.sections.size(), score.phrases.size(), song.kickT.size(),
                 song.snareT.size(), song.analyseSeconds);
    if (o.debug == 8) {
        // the conductor as CSV at 60 Hz (CPU only): what the music did to the entity's state
        const double d = o.to > 0.0 ? o.to : testDuration(o.test);
        std::printf("t,songT,C,S,flash,arch,morph,temper,mass,breath,flow,shimmer,twist,eyeDepth,tunnel,inversion,strobe,camera\n");
        for (double t = o.from; t <= d + 1e-9; t += 1.0 / 60.0) {
            const astral::State s = conduct(o, static_cast<float>(t), song, score);
            std::printf("%.4f,%.4f,%.4f,%.4f,%.4f,%.0f,%.3f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%s\n", t, o.songStart + t, s.C, s.S,
                        s.flash, s.archA, s.morph, s.temper, s.mass, s.breath, s.flow, s.shimmer, s.fold0.x, s.fold0.y, s.fold0.z, s.fold0.w, s.strobe,
                        s.label.c_str());
        }
        return 0;
    }
    if (o.debug == 9) {
        for (const auto& s : song.sections) std::fprintf(stderr, "  section %6.2f-%6.2f group %d %-12s energy %.2f density %.2f\n", s.start, s.end, s.group, s.label.c_str(), s.energy, s.density);
        for (const auto& p : score.phrases) std::fprintf(stderr, "  phrase %3d %6.2f-%6.2f sec %d kickOpens %d\n", p.index, p.start, p.end, p.section, p.kickOpens);
        return 0;
    }

    auto ctxR = gpu::Context::create(gpu::ContextDesc{});
    if (!ctxR) { std::fprintf(stderr, "context: %s\n", ctxR.error().message.c_str()); return 3; }
    gpu::Context& ctx = **ctxR;
    const wgpu::Device& dev = ctx.device();
    const wgpu::Queue& queue = ctx.queue();
    gpu::FrameTimeline timeline(ctx);

    const std::uint32_t W = o.width, H = o.height;
    const std::uint32_t N = o.n;
    const int R = o.gridRes;
    const int RC = (R + 7) / 8;
    const float cell = o.gridSize / static_cast<float>(R);

    // ---- buffers ----
    constexpr int kSlots = 4; // uniform slots: one per simulation sub-step in a frame
    std::array<wgpu::Buffer, kSlots> frameBufs;
    for (auto& b : frameBufs) b = makeBuffer(ctx, sizeof(FrameU), wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst, "frame");
    const std::uint64_t stateBytes = static_cast<std::uint64_t>(N) * 16;
    wgpu::Buffer Pb = makeBuffer(ctx, stateBytes, wgpu::BufferUsage::Storage, "P");
    wgpu::Buffer Vb = makeBuffer(ctx, stateBytes, wgpu::BufferUsage::Storage, "V");
    wgpu::Buffer Ab = makeBuffer(ctx, stateBytes, wgpu::BufferUsage::Storage, "A");
    wgpu::Buffer TGb = makeBuffer(ctx, stateBytes, wgpu::BufferUsage::Storage, "TG");
    const std::uint64_t gridBytes = static_cast<std::uint64_t>(R) * R * R * 2 * 4;
    wgpu::Buffer gridBuf = makeBuffer(ctx, gridBytes, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst, "grid");
    const std::uint64_t accumBytes = static_cast<std::uint64_t>(W) * H * 7 * 4;
    wgpu::Buffer accumBuf = makeBuffer(ctx, accumBytes, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst, "accum");

    auto makeTex = [&](wgpu::Extent3D size, wgpu::TextureFormat f, wgpu::TextureUsage u, wgpu::TextureDimension dim = wgpu::TextureDimension::e2D) {
        wgpu::TextureDescriptor td{};
        td.size = size;
        td.format = f;
        td.usage = u;
        td.dimension = dim;
        return dev.CreateTexture(&td);
    };
    const auto hdrFmt = wgpu::TextureFormat::RGBA16Float;
    wgpu::Texture densTex = makeTex({std::uint32_t(R), std::uint32_t(R), std::uint32_t(R)}, hdrFmt,
                                    wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::TextureBinding, wgpu::TextureDimension::e3D);
    wgpu::Texture coarseTex = makeTex({std::uint32_t(RC), std::uint32_t(RC), std::uint32_t(RC)}, wgpu::TextureFormat::R32Float,
                                      wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::TextureBinding, wgpu::TextureDimension::e3D);
    wgpu::Texture surfColor = makeTex({W, H, 1}, hdrFmt, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding);
    wgpu::Texture surfDepth = makeTex({W, H, 1}, wgpu::TextureFormat::R32Float, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding);
    wgpu::Texture hdr = makeTex({W, H, 1}, hdrFmt, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding);
    wgpu::Texture ldr = makeTex({W, H, 1}, wgpu::TextureFormat::RGBA8Unorm, wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc);
    constexpr int kBloom = 6;
    std::vector<wgpu::Texture> bloom;
    std::vector<wgpu::TextureView> bloomViews;
    for (int k = 0; k < kBloom; ++k) {
        bloom.push_back(makeTex({std::max(1u, W >> (k + 1)), std::max(1u, H >> (k + 1)), 1}, hdrFmt,
                                wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding));
        bloomViews.push_back(bloom.back().CreateView());
    }
    wgpu::TextureView densView = densTex.CreateView(), coarseView = coarseTex.CreateView();
    wgpu::TextureView surfColorView = surfColor.CreateView(), surfDepthView = surfDepth.CreateView();
    wgpu::TextureView hdrView = hdr.CreateView(), ldrView = ldr.CreateView();
    wgpu::SamplerDescriptor sd{};
    sd.minFilter = wgpu::FilterMode::Linear;
    sd.magFilter = wgpu::FilterMode::Linear;
    sd.addressModeU = sd.addressModeV = sd.addressModeW = wgpu::AddressMode::ClampToEdge;
    wgpu::Sampler sampler = dev.CreateSampler(&sd);

    // ---- shaders ----
    const std::string common = shader("common.wgsl");
    const std::string latent = shader("latent.wgsl");
    wgpu::ShaderModule simMod = compile(ctx, common + latent + shader("sim.wgsl"), "sim");
    wgpu::ShaderModule resMod = compile(ctx, common + shader("resolve.wgsl"), "resolve");
    wgpu::ShaderModule coarseMod = compile(ctx, common + shader("coarse.wgsl"), "coarse");
    wgpu::ShaderModule surfMod = compile(ctx, common + latent + shader("surface.wgsl"), "surface");
    wgpu::ShaderModule flakeMod = compile(ctx, common + shader("flakes.wgsl"), "flakes");
    wgpu::ShaderModule postMod = compile(ctx, shader("post.wgsl"), "post");

    const auto CS = wgpu::ShaderStage::Compute;
    const auto ALL = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment | wgpu::ShaderStage::Compute;
    auto frameLayout = makeLayout(ctx, {B::Uniform}, ALL);
    std::array<wgpu::BindGroup, kSlots> frameGroups;
    for (int k = 0; k < kSlots; ++k) frameGroups[k] = makeGroup(ctx, frameLayout, {buf(frameBufs[k], sizeof(FrameU))});

    auto simLayout = makeLayout(ctx, {B::Storage, B::Storage, B::Storage, B::Storage, B::Tex3D, B::Sampler, B::Storage}, CS);
    auto simPL = makePL(ctx, {frameLayout, simLayout});
    auto initPipe = makeCompute(ctx, simMod, "cs_init", simPL);
    auto stepPipe = makeCompute(ctx, simMod, "cs_step", simPL);
    auto splatPipe = makeCompute(ctx, simMod, "cs_splat", simPL);
    auto simGroup = makeGroup(ctx, simLayout, {buf(Pb, stateBytes), buf(Vb, stateBytes), buf(Ab, stateBytes), buf(gridBuf, gridBytes), tex(densView), smp(sampler), buf(TGb, stateBytes)});

    auto resLayout = makeLayout(ctx, {B::ReadOnly, B::StoreTex3D_RGBA16F}, CS);
    auto resPipe = makeCompute(ctx, resMod, "cs_resolve", makePL(ctx, {frameLayout, resLayout}));
    auto resGroup = makeGroup(ctx, resLayout, {buf(gridBuf, gridBytes), tex(densView)});
    auto coarseLayout = makeLayout(ctx, {B::Tex3D, B::StoreTex3D_R32F}, CS);
    auto coarsePipe = makeCompute(ctx, coarseMod, "cs_coarse", makePL(ctx, {frameLayout, coarseLayout}));
    auto coarseGroup = makeGroup(ctx, coarseLayout, {tex(densView), tex(coarseView)});

    auto flakeLayout = makeLayout(ctx, {B::ReadOnly, B::ReadOnly, B::ReadOnly, B::Storage, B::Tex2DUnfilt}, CS);
    auto flakePipe = makeCompute(ctx, flakeMod, "cs_flakes", makePL(ctx, {frameLayout, flakeLayout}));
    auto flakeGroup = makeGroup(ctx, flakeLayout, {buf(Pb, stateBytes), buf(Vb, stateBytes), buf(Ab, stateBytes), buf(accumBuf, accumBytes), tex(surfDepthView)});

    auto surfLayout = makeLayout(ctx, {B::Tex3D, B::Sampler, B::Tex3DUnfilt}, wgpu::ShaderStage::Fragment);
    wgpu::RenderPipeline surfPipe;
    {
        std::array<wgpu::ColorTargetState, 2> cts{};
        cts[0].format = hdrFmt;
        cts[1].format = wgpu::TextureFormat::R32Float;
        wgpu::FragmentState fst{};
        fst.module = surfMod;
        fst.entryPoint = "fs_surface";
        fst.targetCount = 2;
        fst.targets = cts.data();
        wgpu::RenderPipelineDescriptor rpd{};
        rpd.layout = makePL(ctx, {frameLayout, surfLayout});
        rpd.vertex.module = surfMod;
        rpd.vertex.entryPoint = "vs_full";
        rpd.fragment = &fst;
        surfPipe = dev.CreateRenderPipeline(&rpd);
    }
    auto surfGroup = makeGroup(ctx, surfLayout, {tex(densView), smp(sampler), tex(coarseView)});

    auto postLayout = makeLayout(ctx, {B::Tex2D, B::Sampler, B::Uniform, B::Tex2D, B::ReadOnly}, wgpu::ShaderStage::Fragment | wgpu::ShaderStage::Vertex);
    auto postPL = makePL(ctx, {postLayout});
    auto postPipe = [&](const char* fs, wgpu::TextureFormat fmt, bool additive) {
        wgpu::BlendState blend{};
        blend.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::One};
        blend.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One, wgpu::BlendFactor::One};
        wgpu::ColorTargetState cts{};
        cts.format = fmt;
        if (additive) cts.blend = &blend;
        wgpu::FragmentState fst{};
        fst.module = postMod;
        fst.entryPoint = fs;
        fst.targetCount = 1;
        fst.targets = &cts;
        wgpu::RenderPipelineDescriptor rpd{};
        rpd.layout = postPL;
        rpd.vertex.module = postMod;
        rpd.vertex.entryPoint = "vs_full";
        rpd.fragment = &fst;
        return dev.CreateRenderPipeline(&rpd);
    };
    auto combinePipe = postPipe("fs_combine", hdrFmt, false);
    auto downFirst = postPipe("fs_down_first", hdrFmt, false);
    auto down = postPipe("fs_down", hdrFmt, false);
    auto up = postPipe("fs_up", hdrFmt, true);
    auto composite = postPipe("fs_composite", wgpu::TextureFormat::RGBA8Unorm, false);
    wgpu::Buffer postBuf = makeBuffer(ctx, 32, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst, "post");
    wgpu::Texture dummyTex = makeTex({1, 1, 1}, hdrFmt, wgpu::TextureUsage::TextureBinding);
    wgpu::TextureView dummyView = dummyTex.CreateView();
    auto pg = [&](const wgpu::TextureView& src, const wgpu::TextureView& bl) {
        return makeGroup(ctx, postLayout, {tex(src), smp(sampler), buf(postBuf, 32), tex(bl), buf(accumBuf, accumBytes)});
    };
    auto combineGroup = pg(surfColorView, dummyView);
    std::vector<wgpu::BindGroup> downGroups, upGroups;
    downGroups.push_back(pg(hdrView, dummyView));
    for (int k = 1; k < kBloom; ++k) downGroups.push_back(pg(bloomViews[k - 1], dummyView));
    for (int k = kBloom - 1; k >= 1; --k) upGroups.push_back(pg(bloomViews[k], dummyView));
    auto compGroup = pg(hdrView, bloomViews[0]);

    ctx.waitForQueue();
    if (ctx.errorCount() != 0) { std::fprintf(stderr, "setup errors: %s\n", ctx.lastError().c_str()); return 4; }

    // ---- per-frame state ----
    const double simRate = 60.0;
    const double dur = testDuration(o.test);
    if (o.to < 0.0) o.to = dur;
    auto fillFrame = [&](FrameU& f, const astral::State& s, float Cprev, double t, std::uint64_t step) {
        const glm::mat4 view = glm::lookAt(s.eye, s.target, glm::vec3(0, 1, 0));
        const float nearZ = std::clamp(glm::length(s.eye - s.target) * 0.01f, 0.003f, 0.1f);
        const glm::mat4 proj = glm::perspectiveZO(glm::radians(s.fovDeg), static_cast<float>(W) / H, nearZ, 4000.0f);
        f.viewProj = proj * view;
        f.invViewProj = glm::inverse(f.viewProj);
        f.cam = glm::vec4(s.eye, static_cast<float>(t));
        f.camFwd = glm::vec4(glm::normalize(s.target - s.eye), 2.0f * std::tan(glm::radians(s.fovDeg) * 0.5f) / H);
        f.screen = glm::vec4(W, H, 1.0f / W, 1.0f / H);
        f.ent0 = glm::vec4(s.C, Cprev, s.S, s.temper);
        f.ent1 = glm::vec4(s.archA, s.archB, s.morph, s.breath);
        f.ent2 = glm::vec4(s.mass, s.flash, s.flow, s.shimmer);
        f.ent3 = glm::vec4(s.blast, s.heatInject, s.chaos, s.smoothK);
        f.fold0 = s.fold0;
        f.fold1 = s.fold1;
        const glm::vec3 origin = s.centre - glm::vec3(o.gridSize * 0.5f) + glm::vec3(0.0f, 0.0f, 0.0f);
        f.grid0 = glm::vec4(origin, cell);
        f.grid1 = glm::vec4(static_cast<float>(R), 1.0f / R, 1.0f, static_cast<float>(N));
        f.sim = glm::vec4(1.0f / simRate, static_cast<float>(step), 2026.0f, static_cast<float>(approach));
        setBands(f, s);
        f.rig = glm::vec4(s.rigPhase, s.sweep, s.sweepStrength, s.flicker);
        astral::AudioAtT a{};
        if (o.test == 6) a = astral::sampleSong(song, o.songStart + t);
        f.audio0 = a.env0;
        f.audio1 = glm::vec4(a.env1.x, a.rms, a.kickEnv, a.snareEnv);
        const float flakeSize = 0.013f;
        f.look = glm::vec4(s.exposure, 0.08f, s.haze, flakeSize);
        // density norm: a formed surface is ~T=1 at about 40 bound particles per cell for 2M particles; it
        // scales with N and with cell volume so the iso level means the same thing across the benchmark sweep
        const float perCell = 40.0f * (static_cast<float>(N) / 2097152.0f) * std::pow(cell / (20.0f / 192.0f), 2.0f);
        f.flags = glm::vec4(static_cast<float>(o.debug), s.mass, 1.0f / perCell, s.gratingUm);
        f.entity = glm::vec4(s.centre, s.scale);
        f.misc = glm::vec4(s.fall, s.escape, s.strobe, s.appendWeight);
        f.ext = glm::vec4(s.sharpSpread, s.filaments, s.metaRadius, s.metaFace);
    };

    auto dispatch1D = [&](wgpu::ComputePassEncoder& cp, std::uint32_t n) {
        const std::uint32_t groups = (n + 255) / 256;
        const std::uint32_t gx = std::min(groups, 32768u);
        cp.DispatchWorkgroups(gx, (groups + gx - 1) / gx, 1);
    };
    const bool useDensity = approach != 0 && approach != 3;

    // Simulation sub-steps (each with its own uniform slot) followed, if `render`, by the frame.
    std::uint64_t step = 0;
    double simT = 0.0;
    std::function<void(double, bool)> runFrame;
    runFrame = [&](double t, bool render) {
        // a long frame (low clip fps) first catches the simulation up in sim-only submits
        while (simT + (kSlots + 0.5) / simRate < t) runFrame(simT + kSlots / simRate, false);
        // steps owed up to t (fixed 60 Hz)
        std::vector<double> stepTimes;
        while (simT + 1.0 / simRate <= t + 1e-9 && stepTimes.size() < static_cast<std::size_t>(kSlots)) {
            simT += 1.0 / simRate;
            stepTimes.push_back(simT);
        }
        if (stepTimes.empty() && !render) return;
        FrameU fu{};
        astral::State sLast = conduct(o, static_cast<float>(t), song, score);
        for (std::size_t k = 0; k < stepTimes.size(); ++k) {
            const double ts = std::max(stepTimes[k], 0.0);
            const astral::State s = conduct(o, static_cast<float>(ts), song, score);
            const astral::State sp = conduct(o, static_cast<float>(std::max(ts - 1.0 / simRate, 0.0)), song, score);
            astral::State sc = s;
            // the camera of the frame, not of the step
            sc.eye = sLast.eye; sc.target = sLast.target; sc.fovDeg = sLast.fovDeg;
            fillFrame(fu, sc, sp.C, ts, step + k);
            queue.WriteBuffer(frameBufs[k], 0, &fu, sizeof(fu));
        }
        const int slot = stepTimes.empty() ? 0 : static_cast<int>(stepTimes.size()) - 1;
        if (stepTimes.empty()) {
            const astral::State sp = conduct(o, static_cast<float>(std::max(t - 1.0 / simRate, 0.0)), song, score);
            fillFrame(fu, sLast, sp.C, t, step);
            queue.WriteBuffer(frameBufs[0], 0, &fu, sizeof(fu));
        }
        wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
        if (render) timeline.beginFrame();
        if (!stepTimes.empty()) {
            wgpu::ComputePassDescriptor d{};
            if (render) d.timestampWrites = timeline.mark("sim", gpu::FrameTimeline::PassKind::Compute);
            auto cp = enc.BeginComputePass(&d);
            cp.SetPipeline(stepPipe);
            cp.SetBindGroup(1, simGroup);
            for (std::size_t k = 0; k < stepTimes.size(); ++k) {
                cp.SetBindGroup(0, frameGroups[k]);
                dispatch1D(cp, N);
            }
            cp.End();
            step += stepTimes.size();
        }
        if (useDensity) {
            enc.ClearBuffer(gridBuf, 0, gridBytes);
            wgpu::ComputePassDescriptor d{};
            if (render) d.timestampWrites = timeline.mark("density", gpu::FrameTimeline::PassKind::Compute);
            auto cp = enc.BeginComputePass(&d);
            cp.SetBindGroup(0, frameGroups[slot]);
            cp.SetPipeline(splatPipe);
            cp.SetBindGroup(1, simGroup);
            dispatch1D(cp, N);
            cp.SetPipeline(resPipe);
            cp.SetBindGroup(1, resGroup);
            cp.DispatchWorkgroups((R + 3) / 4, (R + 3) / 4, (R + 3) / 4);
            cp.SetPipeline(coarsePipe);
            cp.SetBindGroup(1, coarseGroup);
            cp.DispatchWorkgroups((RC + 3) / 4, (RC + 3) / 4, (RC + 3) / 4);
            cp.End();
        }
        if (render) {
            {
                std::array<wgpu::RenderPassColorAttachment, 2> ca{};
                ca[0].view = surfColorView;
                ca[0].loadOp = wgpu::LoadOp::Clear;
                ca[0].storeOp = wgpu::StoreOp::Store;
                ca[0].clearValue = {0, 0, 0, 1};
                ca[1].view = surfDepthView;
                ca[1].loadOp = wgpu::LoadOp::Clear;
                ca[1].storeOp = wgpu::StoreOp::Store;
                ca[1].clearValue = {1e9, 0, 0, 0};
                wgpu::RenderPassDescriptor rp{};
                rp.colorAttachmentCount = 2;
                rp.colorAttachments = ca.data();
                rp.timestampWrites = timeline.mark("surface", gpu::FrameTimeline::PassKind::Render);
                auto r = enc.BeginRenderPass(&rp);
                r.SetPipeline(surfPipe);
                r.SetBindGroup(0, frameGroups[slot]);
                r.SetBindGroup(1, surfGroup);
                r.Draw(3);
                r.End();
            }
            enc.ClearBuffer(accumBuf, 0, accumBytes);
            {
                wgpu::ComputePassDescriptor d{};
                d.timestampWrites = timeline.mark("flakes", gpu::FrameTimeline::PassKind::Compute);
                auto cp = enc.BeginComputePass(&d);
                cp.SetPipeline(flakePipe);
                cp.SetBindGroup(0, frameGroups[slot]);
                cp.SetBindGroup(1, flakeGroup);
                dispatch1D(cp, N);
                cp.End();
            }
            auto fullPass = [&](const wgpu::TextureView& target, const wgpu::RenderPipeline& p, const wgpu::BindGroup& g, bool load, const char* mark) {
                wgpu::RenderPassColorAttachment ca{};
                ca.view = target;
                ca.loadOp = load ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear;
                ca.storeOp = wgpu::StoreOp::Store;
                ca.clearValue = {0, 0, 0, 1};
                wgpu::RenderPassDescriptor rp{};
                rp.colorAttachmentCount = 1;
                rp.colorAttachments = &ca;
                if (mark) rp.timestampWrites = timeline.mark(mark, gpu::FrameTimeline::PassKind::Render);
                auto r = enc.BeginRenderPass(&rp);
                r.SetPipeline(p);
                r.SetBindGroup(0, g);
                r.Draw(3);
                r.End();
            };
            const float post[8] = {sLast.exposure, sLast.bloom, 0.4f, 0.018f, static_cast<float>(step), 1.0f, 1.0f, 1.6f};
            queue.WriteBuffer(postBuf, 0, post, sizeof(post));
            fullPass(hdrView, combinePipe, combineGroup, false, "post");
            fullPass(bloomViews[0], downFirst, downGroups[0], false, nullptr);
            for (int k = 1; k < kBloom; ++k) fullPass(bloomViews[k], down, downGroups[k], false, nullptr);
            for (int k = kBloom - 1, u = 0; k >= 1; --k, ++u) fullPass(bloomViews[k - 1], up, upGroups[u], true, nullptr);
            fullPass(ldrView, composite, compGroup, false, nullptr);
            timeline.resolve(enc);
        }
        wgpu::CommandBuffer cb = enc.Finish();
        queue.Submit(1, &cb);
        if (render) timeline.collect();
    };

    // initialise the particles
    auto initParticles = [&](double t) {
        FrameU fu{};
        const astral::State s = conduct(o, static_cast<float>(std::max(t, 0.0)), song, score);
        fillFrame(fu, s, s.C, t, 0);
        queue.WriteBuffer(frameBufs[0], 0, &fu, sizeof(fu));
        wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
        enc.ClearBuffer(gridBuf, 0, gridBytes);
        {
            auto cp = enc.BeginComputePass();
            cp.SetPipeline(initPipe);
            cp.SetBindGroup(0, frameGroups[0]);
            cp.SetBindGroup(1, simGroup);
            dispatch1D(cp, N);
            cp.End();
        }
        wgpu::CommandBuffer cb = enc.Finish();
        queue.Submit(1, &cb);
        simT = t;
        step = 0;
    };
    auto preroll = [&](double target) {
        const double start = target - o.preroll;
        initParticles(start);
        // prerolled conductor time is clamped at 0 inside the tests (chaos before the first frame)
        int k = 0;
        for (double t = start + 1.0 / simRate; t <= target + 1e-9; t += 1.0 / simRate) {
            runFrame(t, false);
            if (++k % 30 == 0) ctx.waitForQueue();
        }
        ctx.waitForQueue();
    };
    auto savePng = [&](const std::string& path) {
        auto img = gpu::readTexture8(ctx, ldr, W, H, false);
        if (img) (void)assets::writePng(path, W, H, img->rgba);
    };

    if (o.bench) {
        const double t0 = o.at >= 0.0 ? o.at : 8.0;
        preroll(t0);
        Stat sCpu, sGpu, sSim, sDen, sSurf, sFlk, sPost;
        std::uint64_t lastCompleted = timeline.completedFrames();
        const std::uint32_t total = o.benchWarm + o.benchFrames;
        for (std::uint32_t f = 0; f < total; ++f) {
            const double t = t0 + (f + 1) / simRate;
            const auto c0 = Clock::now();
            runFrame(t, true);
            const double cpu = msSince(c0);
            ctx.waitForQueue(); // one frame in flight: pass timestamps are then this frame's alone
            if (f >= o.benchWarm) {
                sCpu.add(cpu);
                if (timeline.completedFrames() != lastCompleted) {
                    lastCompleted = timeline.completedFrames();
                    sGpu.add(timeline.frameMs());
                    sSim.add(timeline.msFor("sim"));
                    sDen.add(timeline.msFor("density"));
                    sSurf.add(timeline.msFor("surface"));
                    sFlk.add(timeline.msFor("flakes"));
                    sPost.add(timeline.msFor("post"));
                }
            }
        }
        ctx.waitForQueue();
        if (!o.png.empty()) savePng(o.png);
        if (ctx.errorCount() != 0) { std::fprintf(stderr, "gpu errors: %s\n", ctx.lastError().c_str()); return 5; }
        auto j = [](const char* name, const Stat& s) { std::printf("\"%s\":{\"p50\":%.3f,\"p90\":%.3f},", name, s.pct(0.5), s.pct(0.9)); };
        std::printf("{\"test\":%d,\"approach\":\"%c\",\"n\":%u,\"grid\":%d,\"size\":\"%ux%u\",\"t\":%.2f,", o.test, o.approach, N, R, W, H, t0);
        j("cpu_ms", sCpu); j("gpu_frame_ms", sGpu); j("sim_ms", sSim); j("density_ms", sDen); j("surface_ms", sSurf); j("flakes_ms", sFlk); j("post_ms", sPost);
        std::printf("\"state_mb\":%.1f,\"grid_mb\":%.1f,\"accum_mb\":%.1f,\"dens_tex_mb\":%.1f,\"samples\":%zu}\n", 4 * stateBytes / 1048576.0,
                    gridBytes / 1048576.0, accumBytes / 1048576.0, R * R * R * 8 / 1048576.0, sGpu.v.size());
        return 0;
    }

    if (!o.clip.empty()) {
        std::filesystem::create_directories(o.clip);
        preroll(o.from);
        const int frames = static_cast<int>(std::floor((o.to - o.from) * o.fps + 1e-6));
        const auto c0 = Clock::now();
        for (int f = 0; f < frames; ++f) {
            const double t = o.from + f / o.fps;
            runFrame(t, true);
            char name[64];
            std::snprintf(name, sizeof(name), "/f%05d.png", f);
            savePng(o.clip + name);
            if (f % 30 == 0) {
                const astral::State s = conduct(o, static_cast<float>(t), song, score);
                std::fprintf(stderr, "  t=%6.2f C=%.2f S=%.2f arch=%.0f/%.0f m=%.2f temper=%.2f flash=%.2f %s (%.1f s)\n", t, s.C, s.S, s.archA, s.archB,
                             s.morph, s.temper, s.flash, s.label.c_str(), msSince(c0) / 1000.0);
            }
        }
        ctx.waitForQueue();
        if (ctx.errorCount() != 0) { std::fprintf(stderr, "gpu errors: %s\n", ctx.lastError().c_str()); return 5; }
        return 0;
    }

    // a still
    const double at = o.at >= 0.0 ? o.at : 10.0;
    preroll(at);
    runFrame(at, true);
    ctx.waitForQueue();
    // a second frame so the timeline has a completed sample
    savePng(o.png.empty() ? "astral.png" : o.png);
    if (ctx.errorCount() != 0) { std::fprintf(stderr, "gpu errors: %s\n", ctx.lastError().c_str()); return 5; }
    const astral::State s = conduct(o, static_cast<float>(at), song, score);
    std::fprintf(stderr, "still t=%.2f C=%.2f S=%.2f arch=%.0f temper=%.2f %s\n", at, s.C, s.S, s.archA, s.temper, s.label.c_str());
    return 0;
}
