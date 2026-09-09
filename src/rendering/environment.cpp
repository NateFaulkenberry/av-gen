#include "rendering/environment.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"

#include <array>
#include <chrono>
#include <cstring>

namespace avgen::rendering {

EnvironmentProcessor::EnvironmentProcessor(gpu::Context& context, gpu::ShaderLibrary& shaders)
    : context_(context), shaders_(shaders) {}

Result<void> EnvironmentProcessor::init() {
    const auto& device = context_.device();
    {
        std::array<wgpu::BindGroupLayoutEntry, 4> entries{};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[0].buffer.hasDynamicOffset = true;
        entries[0].buffer.minBindingSize = sizeof(EnvUniforms);
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
        entries[3].texture.viewDimension = wgpu::TextureViewDimension::Cube;
        wgpu::BindGroupLayoutDescriptor desc{};
        desc.label = "env-layout";
        desc.entryCount = entries.size();
        desc.entries = entries.data();
        layout_ = device.CreateBindGroupLayout(&desc);
        wgpu::PipelineLayoutDescriptor pl{};
        pl.label = "env-pipeline-layout";
        pl.bindGroupLayoutCount = 1;
        pl.bindGroupLayouts = &layout_;
        pipelineLayout_ = device.CreatePipelineLayout(&pl);
    }
    {
        wgpu::SamplerDescriptor desc{};
        desc.label = "env-sampler";
        desc.addressModeU = wgpu::AddressMode::Repeat; // equirect wraps horizontally
        desc.addressModeV = wgpu::AddressMode::ClampToEdge;
        desc.addressModeW = wgpu::AddressMode::ClampToEdge;
        desc.magFilter = wgpu::FilterMode::Linear;
        desc.minFilter = wgpu::FilterMode::Linear;
        desc.mipmapFilter = wgpu::MipmapFilterMode::Linear;
        sampler_ = device.CreateSampler(&desc);
    }
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "env-uniforms";
        desc.size = static_cast<std::uint64_t>(kUniformSlots) * kUniformStride;
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        uniforms_ = device.CreateBuffer(&desc);
    }
    placeholder2d_ = gpu::solidTexture(context_, 0, 0, 0, 255, false, "env-placeholder-2d");
    auto cube = createCube(1, 1, "env-placeholder-cube");
    if (!cube) {
        return std::unexpected(cube.error());
    }
    placeholderCube_ = std::move(*cube);

    auto module = shaders_.load("environment.wgsl");
    if (!module) {
        return std::unexpected(module.error());
    }
    auto p1 = createPipeline(*module, "fs_equirect", wgpu::TextureFormat::RGBA16Float, "env-equirect");
    if (!p1) return std::unexpected(p1.error());
    equirectPipeline_ = *p1;
    auto p2 = createPipeline(*module, "fs_irradiance", wgpu::TextureFormat::RGBA16Float, "env-irradiance");
    if (!p2) return std::unexpected(p2.error());
    irradiancePipeline_ = *p2;
    auto p3 = createPipeline(*module, "fs_prefilter", wgpu::TextureFormat::RGBA16Float, "env-prefilter");
    if (!p3) return std::unexpected(p3.error());
    prefilterPipeline_ = *p3;
    auto p4 = createPipeline(*module, "fs_brdf", wgpu::TextureFormat::RG16Float, "env-brdf");
    if (!p4) return std::unexpected(p4.error());
    brdfPipeline_ = *p4;
    auto p5 = createPipeline(*module, "fs_sky", wgpu::TextureFormat::RGBA16Float, "env-sky");
    if (!p5) return std::unexpected(p5.error());
    skyPipeline_ = *p5;
    initialised_ = true;
    return {};
}

Result<wgpu::RenderPipeline> EnvironmentProcessor::createPipeline(const wgpu::ShaderModule& module, const char* entry,
                                                                  wgpu::TextureFormat format, const char* label) {
    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = format;
    colorTarget.writeMask = wgpu::ColorWriteMask::All;
    wgpu::FragmentState fragment{};
    fragment.module = module;
    fragment.entryPoint = entry;
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;
    wgpu::RenderPipelineDescriptor desc{};
    desc.label = label;
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
        return fail("pipeline '{}' creation failed: {}", label, error);
    }
    return pipeline;
}

Result<EnvironmentProcessor::CubeTexture> EnvironmentProcessor::createCube(std::uint32_t size, std::uint32_t mips,
                                                                           const char* label) {
    CubeTexture cube;
    cube.size = size;
    cube.mips = mips;
    wgpu::TextureDescriptor desc{};
    desc.label = label;
    desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment;
    desc.dimension = wgpu::TextureDimension::e2D;
    desc.size = {size, size, 6};
    desc.format = wgpu::TextureFormat::RGBA16Float;
    desc.mipLevelCount = mips;
    cube.texture = context_.device().CreateTexture(&desc);
    if (!cube.texture) {
        return fail("failed to create cube texture '{}'", label);
    }
    wgpu::TextureViewDescriptor viewDesc{};
    viewDesc.label = label;
    viewDesc.dimension = wgpu::TextureViewDimension::Cube;
    viewDesc.arrayLayerCount = 6;
    viewDesc.mipLevelCount = mips;
    cube.cubeView = cube.texture.CreateView(&viewDesc);
    return cube;
}

wgpu::BindGroup EnvironmentProcessor::makeBindGroup(const wgpu::TextureView& equirect, const wgpu::TextureView& cube) {
    std::array<wgpu::BindGroupEntry, 4> entries{};
    entries[0].binding = 0;
    entries[0].buffer = uniforms_;
    entries[0].size = sizeof(EnvUniforms);
    entries[1].binding = 1;
    entries[1].sampler = sampler_;
    entries[2].binding = 2;
    entries[2].textureView = equirect ? equirect : placeholder2d_.view;
    entries[3].binding = 3;
    entries[3].textureView = cube ? cube : placeholderCube_.cubeView;
    wgpu::BindGroupDescriptor desc{};
    desc.label = "env-bind-group";
    desc.layout = layout_;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return context_.device().CreateBindGroup(&desc);
}

void EnvironmentProcessor::runPass(wgpu::CommandEncoder& encoder, const wgpu::RenderPipeline& pipeline,
                                   const wgpu::TextureView& target, const wgpu::BindGroup& bindGroup,
                                   const EnvUniforms& uniforms, std::uint32_t slot) {
    const std::uint32_t offset = slot * kUniformStride;
    context_.queue().WriteBuffer(uniforms_, offset, &uniforms, sizeof(uniforms));
    wgpu::RenderPassColorAttachment color{};
    color.view = target;
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor pass{};
    pass.label = "env-pass";
    pass.colorAttachmentCount = 1;
    pass.colorAttachments = &color;
    wgpu::RenderPassEncoder rp = encoder.BeginRenderPass(&pass);
    rp.SetPipeline(pipeline);
    rp.SetBindGroup(0, bindGroup, 1, &offset);
    rp.Draw(3);
    rp.End();
}

Result<void> EnvironmentProcessor::ensureBrdf(const EnvironmentSettings& settings) {
    if (brdf_.valid()) {
        return {};
    }
    wgpu::TextureDescriptor desc{};
    desc.label = "brdf-lut";
    desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment;
    desc.dimension = wgpu::TextureDimension::e2D;
    desc.size = {settings.brdfSize, settings.brdfSize, 1};
    desc.format = wgpu::TextureFormat::RG16Float;
    brdf_.texture = context_.device().CreateTexture(&desc);
    if (!brdf_.texture) {
        return fail("failed to create BRDF LUT");
    }
    brdf_.view = brdf_.texture.CreateView();
    brdf_.width = brdf_.height = settings.brdfSize;
    brdf_.format = desc.format;
    wgpu::CommandEncoder encoder = context_.device().CreateCommandEncoder();
    EnvUniforms u{};
    u.sampleCount = settings.brdfSamples;
    runPass(encoder, brdfPipeline_, brdf_.view, makeBindGroup(nullptr, nullptr), u, 0);
    wgpu::CommandBuffer commands = encoder.Finish();
    context_.queue().Submit(1, &commands);
    return {};
}

// Cube face views are needed by every pass that writes one; the descriptor is the same each time.
namespace {
wgpu::TextureView cubeFaceView(const wgpu::Texture& texture, std::uint32_t face, std::uint32_t mip) {
    wgpu::TextureViewDescriptor desc{};
    desc.dimension = wgpu::TextureViewDimension::e2D;
    desc.baseArrayLayer = face;
    desc.arrayLayerCount = 1;
    desc.baseMipLevel = mip;
    desc.mipLevelCount = 1;
    return texture.CreateView(&desc);
}
} // namespace

Result<IblResources> EnvironmentProcessor::filterCube(const CubeTexture& sourceCube, const EnvironmentSettings& settings) {
    auto irradiance = createCube(settings.irradianceSize, 1, "env-irradiance");
    if (!irradiance) return std::unexpected(irradiance.error());
    auto prefiltered = createCube(settings.prefilteredSize, settings.prefilteredMips, "env-prefiltered");
    if (!prefiltered) return std::unexpected(prefiltered.error());

    std::uint32_t slot = 0;
    wgpu::CommandEncoder encoder = context_.device().CreateCommandEncoder();
    auto flush = [&]() {
        wgpu::CommandBuffer commands = encoder.Finish();
        context_.queue().Submit(1, &commands);
        context_.waitForQueue();
        encoder = context_.device().CreateCommandEncoder();
        slot = 0;
    };
    auto nextSlot = [&]() {
        if (slot >= kUniformSlots) {
            flush();
        }
        return slot++;
    };

    const wgpu::BindGroup cubeGroup = makeBindGroup(nullptr, sourceCube.cubeView);
    for (std::uint32_t face = 0; face < 6; ++face) {
        EnvUniforms u{};
        u.faceIndex = face;
        u.sampleCount = settings.irradianceSamples;
        u.sourceMipCount = static_cast<float>(sourceCube.mips);
        u.sourceSize = static_cast<float>(sourceCube.size);
        runPass(encoder, irradiancePipeline_, cubeFaceView(irradiance->texture, face, 0), cubeGroup, u, nextSlot());
    }
    for (std::uint32_t mip = 0; mip < settings.prefilteredMips; ++mip) {
        const float roughness = settings.prefilteredMips > 1
                                    ? static_cast<float>(mip) / static_cast<float>(settings.prefilteredMips - 1)
                                    : 0.0f;
        for (std::uint32_t face = 0; face < 6; ++face) {
            EnvUniforms u{};
            u.faceIndex = face;
            u.mipLevel = mip;
            u.sampleCount = settings.prefilterSamples;
            u.roughness = roughness;
            u.sourceMipCount = static_cast<float>(sourceCube.mips);
            u.sourceSize = static_cast<float>(sourceCube.size);
            runPass(encoder, prefilterPipeline_, cubeFaceView(prefiltered->texture, face, mip), cubeGroup, u,
                    nextSlot());
        }
    }
    flush();

    IblResources out;
    out.irradiance = irradiance->cubeView;
    out.prefiltered = prefiltered->cubeView;
    out.brdfLut = brdf_.view;
    out.prefilteredMips = settings.prefilteredMips;
    out.valid = context_.errorCount() == 0;
    // The textures stay alive through the views the caller holds (a wgpu::TextureView keeps a
    // reference to its texture), exactly as the pre-ADR-036 code relied on.
    return out;
}

Result<IblResources> EnvironmentProcessor::process(const scene::TextureData& equirect,
                                                   const EnvironmentSettings& settings) {
    if (!initialised_) {
        return fail("environment processor not initialised");
    }
    const auto start = std::chrono::steady_clock::now();
    if (auto r = ensureBrdf(settings); !r) {
        return std::unexpected(r.error());
    }
    auto source = gpu::uploadTextureAsHalf(context_, equirect, true);
    if (!source) {
        return std::unexpected(source.error());
    }

    const std::uint32_t cubeMips = gpu::mipLevelCount(settings.cubeSize, settings.cubeSize);
    auto sourceCube = createCube(settings.cubeSize, cubeMips, "env-source-cube");
    if (!sourceCube) return std::unexpected(sourceCube.error());

    std::uint32_t slot = 0;
    wgpu::CommandEncoder encoder = context_.device().CreateCommandEncoder();
    const wgpu::BindGroup equirectGroup = makeBindGroup(source->view, nullptr);
    for (std::uint32_t mip = 0; mip < cubeMips; ++mip) {
        for (std::uint32_t face = 0; face < 6; ++face) {
            if (slot >= kUniformSlots) {
                wgpu::CommandBuffer commands = encoder.Finish();
                context_.queue().Submit(1, &commands);
                context_.waitForQueue();
                encoder = context_.device().CreateCommandEncoder();
                slot = 0;
            }
            EnvUniforms u{};
            u.faceIndex = face;
            u.mipLevel = std::min(mip, source->mipLevels - 1);
            runPass(encoder, equirectPipeline_, cubeFaceView(sourceCube->texture, face, mip), equirectGroup, u,
                    slot++);
        }
    }
    {
        wgpu::CommandBuffer commands = encoder.Finish();
        context_.queue().Submit(1, &commands);
        context_.waitForQueue();
    }

    auto out = filterCube(*sourceCube, settings);
    if (!out) {
        return out;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    log::info("environment '{}' ({}x{}) processed in {:.1f} ms: cube {} ({} mips), irradiance {}, prefiltered {} x {} mips",
              equirect.name, equirect.width, equirect.height, ms, settings.cubeSize, cubeMips,
              settings.irradianceSize, settings.prefilteredSize, settings.prefilteredMips);
    if (!out->valid) {
        return fail("environment processing raised GPU errors: {}", context_.lastError());
    }
    return out;
}

Result<IblResources> EnvironmentProcessor::processSky(const scene::SkyRuntime& sky,
                                                      const EnvironmentSettings& settings) {
    if (!initialised_) {
        return fail("environment processor not initialised");
    }
    const auto start = std::chrono::steady_clock::now();
    if (auto r = ensureBrdf(settings); !r) {
        return std::unexpected(r.error());
    }
    const std::uint32_t cubeMips = gpu::mipLevelCount(settings.cubeSize, settings.cubeSize);
    auto sourceCube = createCube(settings.cubeSize, cubeMips, "env-sky-cube");
    if (!sourceCube) return std::unexpected(sourceCube.error());

    EnvUniforms sky4{};
    sky4.skyZenith = glm::vec4(sky.zenithColor, sky.hazeWidth);
    sky4.skyHorizon = glm::vec4(sky.horizonColor, sky.sunAngularRadius);
    sky4.skyGround = glm::vec4(sky.groundColor, sky.sunGlowWidth);
    sky4.skySun = glm::vec4(sky.sunColor * sky.sunIntensity, sky.intensity);
    sky4.skySunDir = glm::vec4(sky.sunDirection, 0.0f);

    std::uint32_t slot = 0;
    wgpu::CommandEncoder encoder = context_.device().CreateCommandEncoder();
    const wgpu::BindGroup skyGroup = makeBindGroup(nullptr, nullptr);
    for (std::uint32_t mip = 0; mip < cubeMips; ++mip) {
        for (std::uint32_t face = 0; face < 6; ++face) {
            if (slot >= kUniformSlots) {
                wgpu::CommandBuffer commands = encoder.Finish();
                context_.queue().Submit(1, &commands);
                context_.waitForQueue();
                encoder = context_.device().CreateCommandEncoder();
                slot = 0;
            }
            EnvUniforms u = sky4;
            u.faceIndex = face;
            u.mipLevel = mip;
            u.faceSize = static_cast<float>(std::max(settings.cubeSize >> mip, 1u));
            runPass(encoder, skyPipeline_, cubeFaceView(sourceCube->texture, face, mip), skyGroup, u, slot++);
        }
    }
    {
        wgpu::CommandBuffer commands = encoder.Finish();
        context_.queue().Submit(1, &commands);
        context_.waitForQueue();
    }

    auto out = filterCube(*sourceCube, settings);
    if (!out) {
        return out;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    log::info("procedural sky built in {:.1f} ms: cube {} ({} mips), irradiance {}, prefiltered {} x {} mips; "
              "sun ({:.2f}, {:.2f}, {:.2f})",
              ms, settings.cubeSize, cubeMips, settings.irradianceSize, settings.prefilteredSize,
              settings.prefilteredMips, sky.sunDirection.x, sky.sunDirection.y, sky.sunDirection.z);
    if (!out->valid) {
        return fail("procedural sky processing raised GPU errors: {}", context_.lastError());
    }
    return out;
}

} // namespace avgen::rendering
