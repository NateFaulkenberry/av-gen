#include "rendering/cloud_renderer.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/resource_stats.hpp"
#include "gpu/shader_library.hpp"
#include "scene/scene.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace avgen::rendering {

namespace {

// Mirrors `CloudUniforms` in shaders/clouds.wgsl and shaders/clouds_resolve.wgsl.
struct CloudUniformGpu {
    glm::vec4 light;         // xyz toward the key, w = intensity
    glm::vec4 lightColor;    // rgb, w = anisotropy
    glm::vec4 ambientTop;    // rgb, w = extinction per metre (density folded in)
    glm::vec4 ambientBottom; // rgb, w = powder
    glm::vec4 warm;          // xyz toward the warm glow, w = warm height (m)
    glm::vec4 warmColor;     // rgb, w = noise amount
    glm::vec4 haze;          // rgb, w = haze distance (m)
    glm::vec4 march;         // x = steps, y = light steps, z = light distance (m), w = noise scale
    glm::vec4 march2;        // x = min step (m), y = step growth, z = max distance (m), w = detail scale
    glm::vec4 sea;           // x = base, y = top (swell applied), z = coverage, w = coverage scale
    glm::vec4 sea2;          // x = sea density, y = 1 when the sea is on, z = detail amount, w = swell
    glm::vec4 sea3;          // ADR-2101: x = relief (m), y = 1 / reliefScale, z = billow (m), w = the mean top
    glm::vec4 sea4;          // break 0: centre x, z, radius, elongation
    glm::vec4 sea5;          // break 1
    glm::vec4 sea6;          // x = yaw 0, y = yaw 1, z = break count, w = far fade start (m; 0 = none)
    glm::vec4 boxMin;        // xyz = the lobes' bounds, w = lobe count
    glm::vec4 boxMax;        // xyz, w = glow count
    glm::vec4 grid;          // x, y = origin (xz), z = cell size, w = cells along x
    glm::vec4 grid2;         // x = cells along z, y = seed, z = glow gain, w = glow occlusion
    glm::vec4 drift;         // xyz = the noise's offset (m), w = seconds
    glm::vec4 frameInfo;     // x = jitter (0..1), y = history weight (0 = none), zw = march target size
    glm::mat4 prevViewProj;  // the view-projection the history was drawn with
    std::array<glm::vec4, 2 * scene::kMaxCloudGlows> glows; // per glow: (position, radius), (colour * intensity, 0)
    // ADR-2102: per beam arm (origin, length), (direction, tan of the half angle), (colour * intensity, lamp radius)
    std::array<glm::vec4, 3 * scene::kMaxCloudBeamArms> beams;
    glm::vec4 beamInfo;      // x = arm count, y = air gain, z = cloud gain, w = 1 when the halo is on
    glm::vec4 halo;          // xyz toward the body, w = the inner glow's e-folding angle (radians)
    glm::vec4 haloColor;     // rgb * intensity
};
static_assert(sizeof(CloudUniformGpu) == (21 + 4 + 2 * scene::kMaxCloudGlows + 3 * scene::kMaxCloudBeamArms + 3) * 16);

struct LobeGpu {
    glm::vec4 centreRadius;
    glm::vec4 scaleDensity;
    glm::vec4 soft;
};

std::uint64_t mixKey(std::uint64_t h, float v) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, 4);
    return (h ^ (bits + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2))) * 1099511628211ull;
}

// ---- the noise volume ----------------------------------------------------------------------------------------------

std::uint32_t pcg(std::uint32_t v) {
    std::uint32_t state = v * 747796405u + 2891336453u;
    std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

std::uint32_t hash3(int x, int y, int z, std::uint32_t seed) {
    return pcg(static_cast<std::uint32_t>(x) * 73856093u ^ pcg(static_cast<std::uint32_t>(y) * 19349663u ^
                                                               pcg(static_cast<std::uint32_t>(z) * 83492791u ^ seed)));
}

float unit(std::uint32_t h) { return static_cast<float>(h & 0xFFFFFFu) / 16777216.0f; }

int wrap(int v, int period) { return ((v % period) + period) % period; }

// Gradient noise, tiling with `period` cells over [0, 1)^3. Returns about -1..1.
float perlin(glm::vec3 p, int period, std::uint32_t seed) {
    const glm::vec3 q = p * static_cast<float>(period);
    const glm::ivec3 i = glm::ivec3(glm::floor(q));
    const glm::vec3 f = q - glm::floor(q);
    const glm::vec3 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    float corner[8];
    for (int c = 0; c < 8; ++c) {
        const glm::ivec3 o(c & 1, (c >> 1) & 1, (c >> 2) & 1);
        const glm::ivec3 g = i + o;
        const std::uint32_t h = hash3(wrap(g.x, period), wrap(g.y, period), wrap(g.z, period), seed);
        // one of twelve edge directions
        static constexpr float dirs[12][3] = {{1, 1, 0},  {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1},  {-1, 0, 1},
                                              {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1},  {0, 1, -1}, {0, -1, -1}};
        const float* d = dirs[h % 12u];
        const glm::vec3 r = f - glm::vec3(o);
        corner[c] = d[0] * r.x + d[1] * r.y + d[2] * r.z;
    }
    const float x00 = corner[0] + u.x * (corner[1] - corner[0]);
    const float x10 = corner[2] + u.x * (corner[3] - corner[2]);
    const float x01 = corner[4] + u.x * (corner[5] - corner[4]);
    const float x11 = corner[6] + u.x * (corner[7] - corner[6]);
    const float y0 = x00 + u.y * (x10 - x00);
    const float y1 = x01 + u.y * (x11 - x01);
    return y0 + u.z * (y1 - y0);
}

// Inverted cellular noise, tiling with `period` cells: 1 at a feature point, 0 a cell away.
float worley(glm::vec3 p, int period, std::uint32_t seed) {
    const glm::vec3 q = p * static_cast<float>(period);
    const glm::ivec3 i = glm::ivec3(glm::floor(q));
    const glm::vec3 f = q - glm::floor(q);
    float best = 1e9f;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const glm::ivec3 c = i + glm::ivec3(dx, dy, dz);
                const std::uint32_t h = hash3(wrap(c.x, period), wrap(c.y, period), wrap(c.z, period), seed);
                const glm::vec3 feature(unit(h), unit(pcg(h)), unit(pcg(h + 1u)));
                const glm::vec3 d = glm::vec3(dx, dy, dz) + feature - f;
                best = std::min(best, glm::dot(d, d));
            }
        }
    }
    return 1.0f - std::clamp(std::sqrt(best), 0.0f, 1.0f);
}

std::vector<std::uint8_t> buildNoise() {
    constexpr int N = static_cast<int>(kCloudNoiseSize);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(N) * N * N * 4);
    const auto byte = [](float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    for (int z = 0; z < N; ++z) {
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                const glm::vec3 p = (glm::vec3(x, y, z) + 0.5f) / static_cast<float>(N);
                const float pf = 0.5f + 0.5f * (0.57f * perlin(p, 4, 11u) + 0.29f * perlin(p, 8, 12u) +
                                                0.14f * perlin(p, 16, 13u));
                const float w1 = 0.625f * worley(p, 4, 21u) + 0.25f * worley(p, 8, 22u) + 0.125f * worley(p, 16, 23u);
                const float w2 = 0.625f * worley(p, 8, 31u) + 0.25f * worley(p, 16, 32u) + 0.125f * worley(p, 32, 33u);
                // Perlin-Worley: the gradient noise dilated by the cells, so it billows rather than streaks
                const float lo = w1 - 1.0f;
                const float pw = std::clamp((pf - lo) / (1.0f - lo), 0.0f, 1.0f);
                const float pf2 = 0.5f + 0.5f * (0.6f * perlin(p, 4, 41u) + 0.4f * perlin(p, 8, 42u));
                std::uint8_t* t = out.data() + ((static_cast<std::size_t>(z) * N + y) * N + x) * 4;
                t[0] = byte(pw);
                t[1] = byte(w1 / 0.875f);
                t[2] = byte(w2 / 0.875f);
                t[3] = byte(pf2);
            }
        }
    }
    return out;
}

wgpu::BindGroupLayoutEntry uniformEntry(std::uint32_t binding) {
    wgpu::BindGroupLayoutEntry e{};
    e.binding = binding;
    e.visibility = wgpu::ShaderStage::Fragment | wgpu::ShaderStage::Vertex;
    e.buffer.type = wgpu::BufferBindingType::Uniform;
    return e;
}

wgpu::BindGroupLayoutEntry textureEntry(std::uint32_t binding, wgpu::TextureSampleType type,
                                        wgpu::TextureViewDimension dim = wgpu::TextureViewDimension::e2D) {
    wgpu::BindGroupLayoutEntry e{};
    e.binding = binding;
    e.visibility = wgpu::ShaderStage::Fragment;
    e.texture.sampleType = type;
    e.texture.viewDimension = dim;
    return e;
}

wgpu::BindGroupLayoutEntry samplerEntry(std::uint32_t binding) {
    wgpu::BindGroupLayoutEntry e{};
    e.binding = binding;
    e.visibility = wgpu::ShaderStage::Fragment;
    e.sampler.type = wgpu::SamplerBindingType::Filtering;
    return e;
}

wgpu::BindGroupLayoutEntry storageEntry(std::uint32_t binding) {
    wgpu::BindGroupLayoutEntry e{};
    e.binding = binding;
    e.visibility = wgpu::ShaderStage::Fragment;
    e.buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    return e;
}

wgpu::BindGroupLayout makeLayout(const wgpu::Device& device, const char* label,
                                 const std::vector<wgpu::BindGroupLayoutEntry>& entries) {
    wgpu::BindGroupLayoutDescriptor d{};
    d.label = label;
    d.entryCount = entries.size();
    d.entries = entries.data();
    return device.CreateBindGroupLayout(&d);
}

} // namespace

const std::vector<std::uint8_t>& cloudNoiseVolume() {
    static const std::vector<std::uint8_t> volume = buildNoise();
    return volume;
}

CloudLobeBins binCloudLobes(const std::vector<scene::CloudLobe>& lobes) {
    CloudLobeBins bins;
    if (lobes.empty()) {
        return bins;
    }
    glm::vec2 lo(1e30f), hi(-1e30f);
    bins.yLo = 1e30f;
    bins.yHi = -1e30f;
    for (const scene::CloudLobe& l : lobes) {
        bins.yLo = std::min(bins.yLo, l.centre.y - l.radius * l.scale.y);
        bins.yHi = std::max(bins.yHi, l.centre.y + l.radius * l.scale.y);
    }
    std::vector<float> extents;
    extents.reserve(lobes.size());
    for (const scene::CloudLobe& l : lobes) {
        const float e = l.radius * std::max(l.scale.x, l.scale.z);
        extents.push_back(e);
        lo = glm::min(lo, glm::vec2(l.centre.x, l.centre.z) - e);
        hi = glm::max(hi, glm::vec2(l.centre.x, l.centre.z) + e);
    }
    std::vector<float> sorted = extents;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2), sorted.end());
    const float span = std::max(hi.x - lo.x, hi.y - lo.y);
    // a cell about half a typical lobe's radius (few lobes per cell), and at most 64 along the longer side
    bins.cellSize = std::max({0.5f * sorted[sorted.size() / 2], span / 64.0f, 1.0f});
    bins.origin = lo;
    bins.nx = std::max(1u, static_cast<std::uint32_t>(std::ceil((hi.x - lo.x) / bins.cellSize)));
    bins.nz = std::max(1u, static_cast<std::uint32_t>(std::ceil((hi.y - lo.y) / bins.cellSize)));
    std::vector<std::vector<std::uint32_t>> cells(static_cast<std::size_t>(bins.nx) * bins.nz);
    for (std::size_t i = 0; i < lobes.size(); ++i) {
        const glm::vec2 c(lobes[i].centre.x, lobes[i].centre.z);
        const glm::ivec2 a = glm::ivec2(glm::floor((c - extents[i] - bins.origin) / bins.cellSize));
        const glm::ivec2 b = glm::ivec2(glm::floor((c + extents[i] - bins.origin) / bins.cellSize));
        for (int z = std::max(a.y, 0); z <= std::min(b.y, static_cast<int>(bins.nz) - 1); ++z) {
            for (int x = std::max(a.x, 0); x <= std::min(b.x, static_cast<int>(bins.nx) - 1); ++x) {
                cells[static_cast<std::size_t>(z) * bins.nx + x].push_back(static_cast<std::uint32_t>(i));
            }
        }
    }
    bins.words.assign(cells.size() * 2, 0u);
    for (std::size_t k = 0; k < cells.size(); ++k) {
        std::vector<std::uint32_t>& list = cells[k];
        if (list.size() > kMaxLobesPerCell) {
            // keep the biggest: they are the shape; the small ones are the detail
            std::partial_sort(list.begin(), list.begin() + kMaxLobesPerCell, list.end(),
                              [&](std::uint32_t p, std::uint32_t q) { return extents[p] > extents[q]; });
            list.resize(kMaxLobesPerCell);
            ++bins.truncated;
        }
        bins.words[2 * k] = static_cast<std::uint32_t>(bins.words.size());
        // ADR-2101: the column's vertical extent, so the march can skip the air above and below its lobes
        float y0 = 1e30f, y1 = -1e30f;
        for (const std::uint32_t i : list) {
            y0 = std::min(y0, lobes[i].centre.y - lobes[i].radius * lobes[i].scale.y);
            y1 = std::max(y1, lobes[i].centre.y + lobes[i].radius * lobes[i].scale.y);
        }
        const float span = std::max(bins.yHi - bins.yLo, 1.0f);
        const auto q = [&](float y, bool up) {
            const float u = std::clamp((y - bins.yLo) / span, 0.0f, 1.0f) * 4095.0f;
            return static_cast<std::uint32_t>(std::clamp(up ? std::ceil(u) : std::floor(u), 0.0f, 4095.0f));
        };
        const std::uint32_t packed = list.empty() ? 0u : (q(y0, false) << 8) | (q(y1, true) << 20);
        bins.words[2 * k + 1] = static_cast<std::uint32_t>(list.size()) | packed;
        bins.words.insert(bins.words.end(), list.begin(), list.end());
    }
    return bins;
}

bool CloudRenderer::wants(const scene::Scene& scene) const {
    return scene.clouds.active();
}

Result<void> CloudRenderer::init(gpu::Context& context, gpu::ShaderLibrary& shaders, wgpu::TextureFormat hdrFormat) {
    context_ = &context;
    hdrFormat_ = hdrFormat;
    const wgpu::Device& device = context.device();
    using ST = wgpu::TextureSampleType;
    marchLayout_ = makeLayout(device, "clouds-march",
                              {uniformEntry(0), uniformEntry(1), textureEntry(2, ST::UnfilterableFloat),
                               textureEntry(3, ST::Float, wgpu::TextureViewDimension::e3D), samplerEntry(4),
                               storageEntry(5), storageEntry(6)});
    resolveLayout_ = makeLayout(device, "clouds-resolve",
                                {uniformEntry(0), uniformEntry(1), textureEntry(2, ST::Float),
                                 textureEntry(3, ST::UnfilterableFloat), textureEntry(4, ST::Float),
                                 samplerEntry(5)});
    compositeLayout_ = makeLayout(device, "clouds-composite",
                                  {uniformEntry(0), uniformEntry(1), textureEntry(2, ST::Float),
                                   textureEntry(3, ST::UnfilterableFloat), textureEntry(4, ST::UnfilterableFloat)});
    wgpu::BufferDescriptor bd{};
    bd.label = "clouds-uniforms";
    bd.size = sizeof(CloudUniformGpu);
    bd.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    uniforms_ = device.CreateBuffer(&bd);

    wgpu::SamplerDescriptor sd{};
    sd.label = "clouds-repeat";
    sd.addressModeU = sd.addressModeV = sd.addressModeW = wgpu::AddressMode::Repeat;
    sd.magFilter = sd.minFilter = wgpu::FilterMode::Linear;
    repeatSampler_ = device.CreateSampler(&sd);
    sd.label = "clouds-clamp";
    sd.addressModeU = sd.addressModeV = sd.addressModeW = wgpu::AddressMode::ClampToEdge;
    clampSampler_ = device.CreateSampler(&sd);
    return createPipelines(shaders);
}

Result<void> CloudRenderer::reload(gpu::ShaderLibrary& shaders) {
    if (context_ == nullptr) {
        return {};
    }
    return createPipelines(shaders);
}

Result<void> CloudRenderer::createPipelines(gpu::ShaderLibrary& shaders) {
    auto marchModule = shaders.load("clouds.wgsl");
    if (!marchModule) {
        return std::unexpected(marchModule.error());
    }
    auto resolveModule = shaders.load("clouds_resolve.wgsl");
    if (!resolveModule) {
        return std::unexpected(resolveModule.error());
    }
    const wgpu::Device& device = context_->device();
    const auto build = [&](const char* label, const wgpu::ShaderModule& module, const char* vs, const char* fs,
                           const wgpu::BindGroupLayout& layout, const std::vector<wgpu::ColorTargetState>& targets,
                           wgpu::RenderPipeline& into) -> Result<void> {
        wgpu::PipelineLayoutDescriptor pl{};
        pl.label = label;
        pl.bindGroupLayoutCount = 1;
        pl.bindGroupLayouts = &layout;
        wgpu::FragmentState fragment{};
        fragment.module = module;
        fragment.entryPoint = fs;
        fragment.targetCount = targets.size();
        fragment.targets = targets.data();
        wgpu::RenderPipelineDescriptor rd{};
        rd.label = label;
        rd.layout = device.CreatePipelineLayout(&pl);
        rd.vertex.module = module;
        rd.vertex.entryPoint = vs;
        rd.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        rd.multisample.count = 1;
        rd.multisample.mask = 0xFFFFFFFFu;
        rd.fragment = &fragment;
        device.PushErrorScope(wgpu::ErrorFilter::Validation);
        wgpu::RenderPipeline pipeline = gpu::createRenderPipeline(device, &rd);
        std::string error;
        auto future = device.PopErrorScope(wgpu::CallbackMode::WaitAnyOnly,
                                           [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                                               if (type != wgpu::ErrorType::NoError) {
                                                   error = std::string(msg.data, msg.length);
                                               }
                                           });
        context_->instance().WaitAny(future, UINT64_MAX);
        if (!error.empty() || !pipeline) {
            return fail("{} pipeline: {}", label, error);
        }
        into = pipeline;
        return {};
    };
    wgpu::ColorTargetState colour{};
    colour.format = wgpu::TextureFormat::RGBA16Float;
    wgpu::ColorTargetState depth{};
    depth.format = wgpu::TextureFormat::RG32Float;
    wgpu::RenderPipeline march, resolve, composite;
    if (auto r = build("clouds-march", *marchModule, "vs_clouds", "fs_clouds", marchLayout_, {colour, depth}, march);
        !r) {
        return r;
    }
    if (auto r = build("clouds-resolve", *resolveModule, "vs_cloud_fullscreen", "fs_cloud_resolve", resolveLayout_,
                       {colour}, resolve);
        !r) {
        return r;
    }
    // Premultiplied over the lit frame: what is behind the cloud survives by its transmittance.
    wgpu::BlendState blend{};
    blend.color.operation = wgpu::BlendOperation::Add;
    blend.color.srcFactor = wgpu::BlendFactor::One;
    blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.alpha.operation = wgpu::BlendOperation::Add;
    blend.alpha.srcFactor = wgpu::BlendFactor::Zero;
    blend.alpha.dstFactor = wgpu::BlendFactor::One;
    wgpu::ColorTargetState hdr{};
    hdr.format = hdrFormat_;
    hdr.blend = &blend;
    if (auto r = build("clouds-composite", *resolveModule, "vs_cloud_fullscreen", "fs_cloud_composite",
                       compositeLayout_, {hdr}, composite);
        !r) {
        return r;
    }
    march_ = march;
    resolve_ = resolve;
    composite_ = composite;
    return {};
}

void CloudRenderer::ensureTargets(std::uint32_t w, std::uint32_t h) {
    if (w == marchW_ && h == marchH_ && marchColor_) {
        return;
    }
    const wgpu::Device& device = context_->device();
    const auto make = [&](const char* label, wgpu::TextureFormat format, wgpu::Texture& tex, wgpu::TextureView& view) {
        wgpu::TextureDescriptor td{};
        td.label = label;
        td.size = {w, h, 1};
        td.format = format;
        td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
        tex = device.CreateTexture(&td);
        view = tex.CreateView();
    };
    make("clouds-march-colour", wgpu::TextureFormat::RGBA16Float, marchColor_, marchColorView_);
    make("clouds-march-depth", wgpu::TextureFormat::RG32Float, marchDepth_, marchDepthView_);
    make("clouds-history-0", wgpu::TextureFormat::RGBA16Float, history_[0], historyView_[0]);
    make("clouds-history-1", wgpu::TextureFormat::RGBA16Float, history_[1], historyView_[1]);
    marchW_ = w;
    marchH_ = h;
    historyValid_ = false;
}

void CloudRenderer::encode(const EnvironmentFrame& f) {
    if (f.encoder == nullptr || f.scene == nullptr || f.frameUniforms == nullptr || !ready() || !wants(*f.scene) ||
        f.width == 0 || f.height == 0) {
        return;
    }
    const scene::Clouds& c = f.scene->clouds;
    const wgpu::Device& device = context_->device();

    // The noise volume, once.
    if (!noise_) {
        wgpu::TextureDescriptor td{};
        td.label = "clouds-noise";
        td.size = {kCloudNoiseSize, kCloudNoiseSize, kCloudNoiseSize};
        td.dimension = wgpu::TextureDimension::e3D;
        td.format = wgpu::TextureFormat::RGBA8Unorm;
        td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        noise_ = device.CreateTexture(&td);
        const std::vector<std::uint8_t>& texels = cloudNoiseVolume();
        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = noise_;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = kCloudNoiseSize * 4;
        layout.rowsPerImage = kCloudNoiseSize;
        wgpu::Extent3D extent{kCloudNoiseSize, kCloudNoiseSize, kCloudNoiseSize};
        context_->queue().WriteTexture(&dst, texels.data(), texels.size(), &layout, &extent);
        wgpu::TextureViewDescriptor vd{};
        vd.dimension = wgpu::TextureViewDimension::e3D;
        noiseView_ = noise_.CreateView(&vd);
    }

    // The lobes and their bins, uploaded when they change.
    std::uint64_t key = 1469598103934665603ull;
    for (const scene::CloudLobe& l : c.lobes) {
        for (float v : {l.centre.x, l.centre.y, l.centre.z, l.radius, l.scale.x, l.scale.y, l.scale.z, l.density,
                        l.soft}) {
            key = mixKey(key, v);
        }
    }
    if (key != lobesKey_ || !lobes_) {
        std::vector<LobeGpu> rows;
        rows.reserve(c.lobes.size());
        glm::vec3 lo(1e30f), hi(-1e30f);
        for (const scene::CloudLobe& l : c.lobes) {
            rows.push_back({glm::vec4(l.centre, l.radius), glm::vec4(l.scale, l.density), glm::vec4(l.soft, 0, 0, 0)});
            const glm::vec3 ext = l.radius * l.scale;
            lo = glm::min(lo, l.centre - ext);
            hi = glm::max(hi, l.centre + ext);
        }
        if (rows.empty()) {
            rows.push_back({glm::vec4(0.0f), glm::vec4(0.0f), glm::vec4(0.0f)});
            lo = hi = glm::vec3(0.0f);
        }
        wgpu::BufferDescriptor bd{};
        bd.label = "clouds-lobes";
        bd.size = rows.size() * sizeof(LobeGpu);
        bd.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        lobes_ = device.CreateBuffer(&bd);
        context_->queue().WriteBuffer(lobes_, 0, rows.data(), rows.size() * sizeof(LobeGpu));
        bins_cpu_ = binCloudLobes(c.lobes);
        if (bins_cpu_.truncated > 0) {
            log::warn("clouds: {} of {} lobe bins held more than {} lobes; the smallest were dropped there",
                      bins_cpu_.truncated, bins_cpu_.nx * bins_cpu_.nz, kMaxLobesPerCell);
        }
        std::vector<std::uint32_t> words = bins_cpu_.words;
        if (words.empty()) {
            words.assign(4, 0u);
        }
        bd.label = "clouds-bins";
        bd.size = words.size() * sizeof(std::uint32_t);
        bins_ = device.CreateBuffer(&bd);
        context_->queue().WriteBuffer(bins_, 0, words.data(), words.size() * sizeof(std::uint32_t));
        lobesKey_ = key;
        lobeCount_ = static_cast<std::uint32_t>(c.lobes.size());
        boxMin_ = lo;
        boxMax_ = hi;
    }

    // The targets at the march's resolution, and whether last frame's result can be reused.
    const float scale = std::clamp(c.resolution, 0.25f, 1.0f);
    const auto mw = std::max(1u, static_cast<std::uint32_t>(std::lround(static_cast<float>(f.width) * scale)));
    const auto mh = std::max(1u, static_cast<std::uint32_t>(std::lround(static_cast<float>(f.height) * scale)));
    ensureTargets(mw, mh);
    const double now = f.time.renderTime;
    const bool temporal = c.temporal > 0.0f;
    const bool historyUsable = temporal && historyValid_ && f.historyValid && f.haveCamera && now >= lastTime_ &&
                               now - lastTime_ < 0.5;

    CloudUniformGpu u{};
    const float ext = c.extinction * c.density;
    u.light = glm::vec4(glm::normalize(c.lightDirection), c.lightIntensity);
    u.lightColor = glm::vec4(c.lightColor, c.anisotropy);
    u.ambientTop = glm::vec4(c.ambientTop, ext);
    u.ambientBottom = glm::vec4(c.ambientBottom, c.powder);
    u.warm = glm::vec4(glm::normalize(c.warmDirection), c.warmHeight);
    u.warmColor = glm::vec4(c.warmColor, c.noiseAmount);
    u.haze = glm::vec4(c.hazeColor, c.hazeDistance);
    u.march = glm::vec4(static_cast<float>(c.steps), static_cast<float>(c.lightSteps), c.lightDistance, c.noiseScale);
    u.march2 = glm::vec4(c.minStep, c.stepGrowth, c.maxDistance, c.detailScale);
    const float seaTop = c.sea.top + c.swell * 0.5f * (c.sea.top - c.sea.base);
    u.sea = glm::vec4(c.sea.base, seaTop, std::clamp(c.sea.coverage + 0.15f * c.swell, 0.0f, 1.0f),
                      c.sea.coverageScale);
    u.sea2 = glm::vec4(c.sea.density, c.sea.enabled ? 1.0f : 0.0f, c.detailAmount, c.swell);
    if (c.sea.relief > 0.0f) { // ADR-2101: the band becomes [base, the highest the surface can reach]
        const float meanTop = seaTop;
        u.sea.y = meanTop + c.sea.relief * 1.15f + c.sea.billow;
        u.sea3 = glm::vec4(c.sea.relief, 1.0f / c.sea.reliefScale, c.sea.billow, meanTop);
    }
    for (std::size_t b = 0; b < std::min<std::size_t>(c.sea.breaks.size(), 2); ++b) {
        const scene::CloudSea::Break& br = c.sea.breaks[b];
        (b == 0 ? u.sea4 : u.sea5) = glm::vec4(br.centre, br.radius, br.elongation);
        (b == 0 ? u.sea6.x : u.sea6.y) = br.yaw;
    }
    u.sea6.z = static_cast<float>(std::min<std::size_t>(c.sea.breaks.size(), 2));
    u.sea6.w = c.sea.farFade > 0.0f ? c.maxDistance * (1.0f - c.sea.farFade) : 0.0f;
    u.boxMin = glm::vec4(boxMin_, static_cast<float>(lobeCount_));
    const std::size_t glowCount = std::min(c.glows.size(), scene::kMaxCloudGlows);
    u.boxMax = glm::vec4(boxMax_, static_cast<float>(glowCount));
    u.grid = glm::vec4(bins_cpu_.origin, bins_cpu_.cellSize, static_cast<float>(bins_cpu_.nx));
    u.grid2 = glm::vec4(static_cast<float>(bins_cpu_.nz), static_cast<float>(c.seed % 1024u), c.glowGain,
                        c.glowOcclusion);
    // The drift is a velocity: the offset is a pure function of the timeline second, so a seek lands where play would.
    const glm::vec3 offset = c.drift * static_cast<float>(std::fmod(now, 100000.0));
    u.drift = glm::vec4(offset, static_cast<float>(now));
    // a golden-ratio sequence over the frame nonce: every frame a different slice of the step, evenly spread
    const float jitter = static_cast<float>(std::fmod(static_cast<double>(f.time.frameNonce()) * 0.6180339887, 1.0));
    u.frameInfo = glm::vec4(jitter, historyUsable ? std::clamp(c.temporal, 0.0f, 0.97f) : 0.0f,
                            static_cast<float>(mw), static_cast<float>(mh));
    u.prevViewProj = historyUsable ? lastViewProj_ : f.viewProj;
    for (std::size_t i = 0; i < glowCount; ++i) {
        const scene::CloudGlow& g = c.glows[i];
        u.glows[2 * i] = glm::vec4(g.position, g.radius);
        u.glows[2 * i + 1] = glm::vec4(g.color * g.intensity, 0.0f);
    }
    // ADR-2102: the beams' arms at this timeline second
    std::size_t arms = 0;
    for (const scene::CloudBeam& b : c.beams) {
        for (int a = 0; a < b.count && arms < scene::kMaxCloudBeamArms; ++a, ++arms) {
            u.beams[3 * arms] = glm::vec4(b.origin, b.length);
            u.beams[3 * arms + 1] = glm::vec4(scene::cloudBeamDirection(b, a, now),
                                              std::tan(glm::radians(b.halfAngle)));
            u.beams[3 * arms + 2] = glm::vec4(b.color * b.intensity, std::max(b.radius, 0.05f));
        }
    }
    u.beamInfo = glm::vec4(static_cast<float>(arms), c.beamAir, c.beamCloud, c.halo.enabled ? 1.0f : 0.0f);
    u.halo = glm::vec4(glm::normalize(c.halo.direction), glm::radians(c.halo.radius));
    u.haloColor = glm::vec4(c.halo.color * c.halo.intensity, 0.0f);
    context_->queue().WriteBuffer(uniforms_, 0, &u, sizeof(u));

    const auto bufferEntry = [](std::uint32_t binding, const wgpu::Buffer& b) {
        wgpu::BindGroupEntry e{};
        e.binding = binding;
        e.buffer = b;
        e.size = b.GetSize();
        return e;
    };
    const auto viewEntry = [](std::uint32_t binding, const wgpu::TextureView& v) {
        wgpu::BindGroupEntry e{};
        e.binding = binding;
        e.textureView = v;
        return e;
    };
    const auto samplerEntryOf = [](std::uint32_t binding, const wgpu::Sampler& s) {
        wgpu::BindGroupEntry e{};
        e.binding = binding;
        e.sampler = s;
        return e;
    };
    const auto group = [&](const char* label, const wgpu::BindGroupLayout& layout,
                           const std::vector<wgpu::BindGroupEntry>& entries) {
        wgpu::BindGroupDescriptor bg{};
        bg.label = label;
        bg.layout = layout;
        bg.entryCount = entries.size();
        bg.entries = entries.data();
        return device.CreateBindGroup(&bg);
    };
    const auto pass = [&](const char* label, const std::vector<wgpu::TextureView>& targets, bool load,
                          const wgpu::RenderPipeline& pipeline, const wgpu::BindGroup& bindGroup) {
        std::vector<wgpu::RenderPassColorAttachment> colours(targets.size());
        for (std::size_t k = 0; k < targets.size(); ++k) {
            colours[k].view = targets[k];
            colours[k].loadOp = load ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear;
            colours[k].clearValue = {0.0, 0.0, 0.0, 0.0};
            colours[k].storeOp = wgpu::StoreOp::Store;
            colours[k].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        }
        wgpu::RenderPassDescriptor rp{};
        rp.label = label;
        rp.timestampWrites =
            f.timeline != nullptr ? f.timeline->mark(label, gpu::FrameTimeline::PassKind::Render) : nullptr;
        rp.colorAttachmentCount = colours.size();
        rp.colorAttachments = colours.data();
        wgpu::RenderPassEncoder enc = f.encoder->BeginRenderPass(&rp);
        enc.SetPipeline(pipeline);
        enc.SetBindGroup(0, bindGroup);
        enc.Draw(3);
        enc.End();
    };

    // 1. the march
    const wgpu::Buffer& frameBuf = *f.frameUniforms;
    pass("clouds", {marchColorView_, marchDepthView_}, false, march_,
         group("clouds-march", marchLayout_,
               {bufferEntry(0, frameBuf), bufferEntry(1, uniforms_), viewEntry(2, f.linearDepth),
                viewEntry(3, noiseView_), samplerEntryOf(4, repeatSampler_), bufferEntry(5, lobes_),
                bufferEntry(6, bins_)}));

    // 2. the temporal resolve (into the history slot not written last frame)
    wgpu::TextureView result = marchColorView_;
    if (temporal) {
        const int write = 1 - historyIndex_;
        // with no usable history the resolve reads the march itself (weight 0), so the slot is still written
        const wgpu::TextureView& prev = historyUsable ? historyView_[historyIndex_] : marchColorView_;
        pass("clouds.resolve", {historyView_[write]}, false, resolve_,
             group("clouds-resolve", resolveLayout_,
                   {bufferEntry(0, frameBuf), bufferEntry(1, uniforms_), viewEntry(2, marchColorView_),
                    viewEntry(3, marchDepthView_), viewEntry(4, prev), samplerEntryOf(5, clampSampler_)}));
        historyIndex_ = write;
        result = historyView_[write];
        historyValid_ = true;
    } else {
        historyValid_ = false;
    }
    lastViewProj_ = f.viewProj;
    lastTime_ = now;

    // 3. the upsample, composited into HDR
    pass("clouds.composite", {f.hdr}, true, composite_,
         group("clouds-composite", compositeLayout_,
               {bufferEntry(0, frameBuf), bufferEntry(1, uniforms_), viewEntry(2, result),
                viewEntry(3, marchDepthView_), viewEntry(4, f.linearDepth)}));
}

} // namespace avgen::rendering
