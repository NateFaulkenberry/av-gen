#include "rendering/attractor_renderer.hpp"

#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/resource_stats.hpp"
#include "gpu/shader_library.hpp"
#include "scene/scene.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace avgen::rendering {

namespace {

constexpr int kPaletteSlots = 16;

// The art-directed palettes (docs/prototypes/strange-attractor/02-plan.md §look). sRGB hex, converted to linear.
// Slots: 0 sky zenith, 1 sky horizon (sun side), 2 Belt of Venus (anti-sun), 3 Earth's shadow, 4 sun, 5 sun glow,
// 6 haze, 7 ambient sky, 8 ground bounce, 9..12 strata (sandstone, rust, bone, accent), 13 dust, 14 playa, 15 rock base.
constexpr std::array<std::array<std::uint32_t, kPaletteSlots>, 3> kPalettes{{
    // Dusk Mineral
    {0x26285a, 0xe9b59b, 0xd8a0b2, 0x6a6c9e, 0xffd2a0, 0xffb07a, 0xb9a2c2, 0x7a80c4, 0xb27a5c, 0xc8915c, 0xa25a48,
     0xd9bf9a, 0xc9634f, 0x9b7e88, 0xdccbc4, 0x5a4458},
    // Nocturne
    {0x0c1030, 0x5a4a80, 0x7a5a8c, 0x23295a, 0xffa070, 0xd07a80, 0x55588e, 0x5a6cc8, 0x4a3040, 0x7a6680, 0x553858,
     0xa898b4, 0xc06272, 0x4a4260, 0x9a96bc, 0x262238},
    // Chromatic
    {0x24428c, 0xffb070, 0xff86ac, 0x4a5fc0, 0xffd890, 0xff9a50, 0xdca2bc, 0x6f8fff, 0xff8a50, 0xdc9a3a, 0xc23a30,
     0xf0e0b8, 0x8a3e62, 0xa8788a, 0xf0d8d0, 0x6a2a4a},
}};

glm::vec3 linearOf(std::uint32_t hex) {
    const auto ch = [](std::uint32_t v) {
        const float c = static_cast<float>(v) / 255.0f;
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    };
    return {ch((hex >> 16) & 0xff), ch((hex >> 8) & 0xff), ch(hex & 0xff)};
}

struct AttractorUniformGpu {
    glm::vec4 world;   // xy origin, z extent, w resolution
    glm::vec4 heights; // x min, y max, z texel (m), w time (s)
    glm::vec4 sun;     // xyz toward the sun, w intensity
    glm::vec4 strata;  // x tilt angle, y tilt slope, z band height (m), w phase
    glm::vec4 fold;    // x direction angle, y wavelength (m), z amplitude (m)
    glm::vec4 look;    // x atmosphere, y strata, z exposure, w detail
    glm::vec4 look2;   // x ambient, y palette, z noise seed (u32 bits), w terraces
    glm::vec4 view;    // xy view direction (xz), z sun side, w domain warp
    std::array<glm::vec4, kPaletteSlots> pal;
    // ADR-1855: the view ray basis, built in double precision from the frame's camera. Reconstructing rays from
    // invViewProj at ndc z = 0 and 1 is ill-conditioned in float32 with this scene's near/far ratio (0.1 m to 32 km):
    // the reconstructed direction wandered by up to 0.35 degrees, differently every frame -- the whole picture jumped.
    glm::vec4 rayForward; // xyz the camera's forward
    glm::vec4 rayRight;   // xyz right * tan(half fov x)
    glm::vec4 rayUp;      // xyz up * tan(half fov y)
};
static_assert(sizeof(AttractorUniformGpu) == 8 * 16 + kPaletteSlots * 16 + 3 * 16);

} // namespace

bool AttractorRenderer::wants(const scene::Scene& scene) const {
    return scene.attractorWorld.active();
}

Result<void> AttractorRenderer::init(gpu::Context& context, gpu::ShaderLibrary& shaders, wgpu::TextureFormat hdrFormat,
                                     wgpu::TextureFormat depthFormat) {
    context_ = &context;
    hdrFormat_ = hdrFormat;
    depthFormat_ = depthFormat;
    const wgpu::Device& device = context.device();
    std::array<wgpu::BindGroupLayoutEntry, 6> e{};
    e[0].binding = 0;
    e[0].visibility = wgpu::ShaderStage::Fragment | wgpu::ShaderStage::Vertex;
    e[0].buffer.type = wgpu::BufferBindingType::Uniform;
    e[1].binding = 1;
    e[1].visibility = wgpu::ShaderStage::Fragment;
    e[1].buffer.type = wgpu::BufferBindingType::Uniform;
    e[2].binding = 2;
    e[2].visibility = wgpu::ShaderStage::Fragment;
    e[2].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
    e[2].texture.viewDimension = wgpu::TextureViewDimension::e2D;
    e[3].binding = 3;
    e[3].visibility = wgpu::ShaderStage::Fragment;
    e[3].texture.sampleType = wgpu::TextureSampleType::Float;
    e[3].texture.viewDimension = wgpu::TextureViewDimension::e2D;
    e[4].binding = 4;
    e[4].visibility = wgpu::ShaderStage::Fragment;
    e[4].sampler.type = wgpu::SamplerBindingType::Filtering;
    e[5].binding = 5; // ADR-1854: the river's flow
    e[5].visibility = wgpu::ShaderStage::Fragment;
    e[5].texture.sampleType = wgpu::TextureSampleType::Float;
    e[5].texture.viewDimension = wgpu::TextureViewDimension::e2D;
    wgpu::BindGroupLayoutDescriptor d{};
    d.label = "attractor-0";
    d.entryCount = e.size();
    d.entries = e.data();
    layout_ = device.CreateBindGroupLayout(&d);
    wgpu::BufferDescriptor bd{};
    bd.label = "attractor-uniforms";
    bd.size = sizeof(AttractorUniformGpu);
    bd.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    uniforms_ = device.CreateBuffer(&bd);
    wgpu::SamplerDescriptor sd{};
    sd.label = "attractor-masks";
    sd.magFilter = wgpu::FilterMode::Linear;
    sd.minFilter = wgpu::FilterMode::Linear;
    sd.addressModeU = wgpu::AddressMode::ClampToEdge;
    sd.addressModeV = wgpu::AddressMode::ClampToEdge;
    sampler_ = device.CreateSampler(&sd);
    return createPipeline(shaders);
}

Result<void> AttractorRenderer::reload(gpu::ShaderLibrary& shaders) {
    if (context_ == nullptr) {
        return {};
    }
    return createPipeline(shaders);
}

Result<void> AttractorRenderer::createPipeline(gpu::ShaderLibrary& shaders) {
    auto module = shaders.load("attractor/world.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    const wgpu::Device& device = context_->device();
    wgpu::PipelineLayoutDescriptor pl{};
    pl.label = "attractor";
    pl.bindGroupLayoutCount = 1;
    pl.bindGroupLayouts = &layout_;
    wgpu::ColorTargetState target{};
    target.format = hdrFormat_;
    wgpu::FragmentState fragment{};
    fragment.module = *module;
    fragment.entryPoint = "fs_world";
    fragment.targetCount = 1;
    fragment.targets = &target;
    wgpu::DepthStencilState depth{};
    depth.format = depthFormat_;
    depth.depthWriteEnabled = wgpu::OptionalBool::True;
    depth.depthCompare = wgpu::CompareFunction::Always;
    wgpu::RenderPipelineDescriptor rd{};
    rd.label = "attractor";
    rd.layout = device.CreatePipelineLayout(&pl);
    rd.vertex.module = *module;
    rd.vertex.entryPoint = "vs_world";
    rd.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    rd.depthStencil = &depth;
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
        return fail("attractor pipeline: {}", error);
    }
    pipeline_ = pipeline;
    return {};
}

void AttractorRenderer::encode(const EnvironmentFrame& f) {
    if (f.encoder == nullptr || f.scene == nullptr || f.frameUniforms == nullptr || !ready() || !wants(*f.scene) ||
        f.width == 0 || f.height == 0) {
        return;
    }
    const scene::AttractorWorldBlock& block = f.scene->attractorWorld;
    const scene::AttractorWorld& w = *block.world;
    const wgpu::Device& device = context_->device();

    // The world, uploaded when it changes (a new seed or structural knob), never for a look change.
    if (w.hash != worldHash_ || !height_) {
        const auto n = static_cast<std::uint32_t>(w.res);
        const auto make = [&](wgpu::TextureFormat format, const char* label) {
            wgpu::TextureDescriptor td{};
            td.label = label;
            td.size = {n, n, 1};
            td.format = format;
            td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
            return device.CreateTexture(&td);
        };
        height_ = make(wgpu::TextureFormat::RGBA32Float, "attractor-height"); // r height, g strata offset, b max slope
        masks_ = make(wgpu::TextureFormat::RGBA8Unorm, "attractor-masks");
        flow_ = make(wgpu::TextureFormat::RGBA8Unorm, "attractor-flow");
        const auto upload = [&](const wgpu::Texture& tex, const void* data, std::uint32_t texelBytes) {
            wgpu::TexelCopyTextureInfo dst{};
            dst.texture = tex;
            wgpu::TexelCopyBufferLayout layout{};
            layout.bytesPerRow = n * texelBytes;
            layout.rowsPerImage = n;
            const wgpu::Extent3D extent{n, n, 1};
            context_->queue().WriteTexture(&dst, data, static_cast<std::size_t>(n) * n * texelBytes, &layout, &extent);
        };
        std::vector<float> field(static_cast<std::size_t>(n) * n * 4, 0.0f);
        for (std::size_t i = 0; i < w.height.size(); ++i) {
            field[4 * i] = w.height[i];
            field[4 * i + 1] = i < w.strataOffset.size() ? w.strataOffset[i] : 0.0f;
            field[4 * i + 2] = i < w.slopeMax.size() ? w.slopeMax[i] : 8.0f;
        }
        upload(height_, field.data(), 16);
        upload(masks_, w.masks.data(), 4);
        if (w.flow.size() == w.masks.size()) {
            upload(flow_, w.flow.data(), 4);
        } else {
            const std::vector<std::uint32_t> none(w.masks.size(), 0u);
            upload(flow_, none.data(), 4);
        }
        worldHash_ = w.hash;
        ++uploads_;
    }

    const scene::AttractorLook& k = block.live;
    AttractorUniformGpu u{};
    u.world = glm::vec4(w.origin, w.extent, static_cast<float>(w.res));
    u.heights = glm::vec4(w.minHeight, w.maxHeight, w.extent / static_cast<float>(w.res),
                          static_cast<float>(f.time.renderTime));
    // The sun: on the side that lights the near escarpment's valley face, `sunAzimuth` degrees from the view.
    const glm::vec2 fwd(std::sin(w.viewAzimuth), std::cos(w.viewAzimuth));
    const glm::vec2 right(-fwd.y, fwd.x);
    const float az = glm::radians(k.sunAzimuth) * w.sunSide;
    const float el = glm::radians(k.sunElevation);
    const glm::vec2 sxz = fwd * std::cos(az) + right * std::sin(az);
    u.sun = glm::vec4(glm::normalize(glm::vec3(sxz.x * std::cos(el), std::sin(el), sxz.y * std::cos(el))),
                      k.sunIntensity);
    u.strata = w.strataSeed[0];
    u.fold = w.strataSeed[1];
    u.look = glm::vec4(k.atmosphere, k.strata, k.exposure, k.detail);
    float noiseSeedBits = 0.0f; // the u32 noise seed, carried bit for bit (the shader bitcasts it back)
    std::memcpy(&noiseSeedBits, &w.noiseSeed, 4);
    u.look2 = glm::vec4(k.ambient, k.palette, noiseSeedBits, w.gen.terraces);
    u.view = glm::vec4(fwd, w.sunSide, w.gen.domainWarp);
    // The palette: a blend between the two nearest tables.
    const float pf = std::clamp(k.palette, 0.0f, static_cast<float>(kPalettes.size() - 1));
    const int p0 = std::min(static_cast<int>(pf), static_cast<int>(kPalettes.size()) - 2);
    const float t = pf - static_cast<float>(p0);
    for (int i = 0; i < kPaletteSlots; ++i) {
        u.pal[i] = glm::vec4(glm::mix(linearOf(kPalettes[p0][i]), linearOf(kPalettes[p0 + 1][i]), t), 0.0f);
    }
    {
        const scene::Camera& cam = f.scene->camera;
        const glm::dvec3 pos(cam.position), tgt(cam.target), upHint(cam.up);
        const glm::dvec3 fw = glm::normalize(tgt - pos);
        glm::dvec3 rt = glm::cross(fw, upHint);
        if (glm::dot(rt, rt) < 1e-18) rt = glm::cross(fw, glm::dvec3(0, 0, 1));
        rt = glm::normalize(rt);
        const glm::dvec3 up = glm::cross(rt, fw);
        const double ty = std::tan(0.5 * static_cast<double>(cam.effectiveFovY()));
        const double tx = ty * static_cast<double>(f.width) / static_cast<double>(f.height);
        u.rayForward = glm::vec4(glm::vec3(fw), 0.0f);
        u.rayRight = glm::vec4(glm::vec3(rt * tx), 0.0f);
        u.rayUp = glm::vec4(glm::vec3(up * ty), 0.0f);
    }
    context_->queue().WriteBuffer(uniforms_, 0, &u, sizeof(u));

    std::array<wgpu::BindGroupEntry, 6> e{};
    e[0].binding = 0;
    e[0].buffer = *f.frameUniforms;
    e[0].size = f.frameUniforms->GetSize();
    e[1].binding = 1;
    e[1].buffer = uniforms_;
    e[1].size = sizeof(AttractorUniformGpu);
    e[2].binding = 2;
    e[2].textureView = height_.CreateView();
    e[3].binding = 3;
    e[3].textureView = masks_.CreateView();
    e[4].binding = 4;
    e[4].sampler = sampler_;
    e[5].binding = 5;
    e[5].textureView = flow_.CreateView();
    wgpu::BindGroupDescriptor bg{};
    bg.label = "attractor-0";
    bg.layout = layout_;
    bg.entryCount = e.size();
    bg.entries = e.data();
    const wgpu::BindGroup group = device.CreateBindGroup(&bg);

    wgpu::RenderPassColorAttachment colour{};
    colour.view = f.hdr;
    colour.loadOp = wgpu::LoadOp::Load;
    colour.storeOp = wgpu::StoreOp::Store;
    colour.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    wgpu::RenderPassDepthStencilAttachment depth{};
    depth.view = f.depth;
    depth.depthLoadOp = wgpu::LoadOp::Load;
    depth.depthStoreOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor rp{};
    rp.label = "attractor";
    rp.timestampWrites = f.timeline != nullptr ? f.timeline->mark("attractor", gpu::FrameTimeline::PassKind::Render)
                                               : nullptr;
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &colour;
    rp.depthStencilAttachment = &depth;
    wgpu::RenderPassEncoder pass = f.encoder->BeginRenderPass(&rp);
    pass.SetPipeline(pipeline_);
    pass.SetBindGroup(0, group);
    pass.Draw(3);
    pass.End();
}

} // namespace avgen::rendering
