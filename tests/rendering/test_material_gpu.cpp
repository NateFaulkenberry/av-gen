// Procedural materials on the GPU (ADR-030): CPU/GPU parity of the op interpreter in
// shaders/material.wgsl against scene::evaluateMaterialProgram through a compute harness (every
// MaterialInput, every MaterialOpKind, Field ops against a live FieldSet, disabled ops and
// "keep the material's value" outputs), then render tests for the three shading paths
// (procedural instances, entities, raymarched SDF surfaces) and determinism.
//
// The harness compares the five program outputs (base colour, metallic, roughness, emission,
// opacity). Emission is the unclamped lane: it carries a register's rgb through untouched, so a
// negative or above-one result still shows up as a difference.
#include "analysis/analyzer.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "gpu/texture.hpp"
#include "rendering/field_uniforms.hpp"
#include "rendering/material_programs.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/material_program.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "spatial/field.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
using scene::MaterialInput;
using scene::MaterialOp;
using scene::MaterialOpKind;
using scene::MaterialProgram;

namespace {

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

gpu::ShaderLibrary makeShaders(gpu::Context& ctx) {
    return gpu::ShaderLibrary(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
}

// The material values every harness program starts from; the CPU side uses exactly these, so an
// output register of -1 ("keep the material's value") must come back unchanged.
scene::MaterialResult harnessBase() {
    scene::MaterialResult base;
    base.baseColor = {0.8f, 0.7f, 0.6f};
    base.metallic = 0.25f;
    base.roughness = 0.35f;
    base.emission = {0.1f, 0.2f, 0.3f};
    base.opacity = 0.9f;
    return base;
}

// ---- the compute harness --------------------------------------------------------------------------

// 16 vec4s per context (ADR-036 added the geometric inputs, ADR-1642 the tangent frame); results are 4 vec4s per context. The
// base values are baked in so both sides start from harnessBase().
constexpr const char* kKernel = R"(
@group(0) @binding(0) var<uniform> fieldBlock: FieldBlock;
@group(0) @binding(1) var<storage, read> materialPrograms: MaterialProgramBlock;
@group(0) @binding(2) var<storage, read> contexts: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read_write> results: array<vec4<f32>>;
@group(0) @binding(16) var programTexturesSrgb: texture_2d_array<f32>;   // ADR-1642
@group(0) @binding(17) var programTexturesLinear: texture_2d_array<f32>;
@group(0) @binding(18) var programTextureSampler: sampler;

const CTX_STRIDE: u32 = 16u;

@compute @workgroup_size(64)
fn cs_material(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    let b = i * CTX_STRIDE;
    if (b >= arrayLength(&contexts)) { return; }
    var ctx = materialContextZero();
    ctx.worldPosition = contexts[b].xyz;
    ctx.objectId = contexts[b].w;
    ctx.localPosition = contexts[b + 1u].xyz;
    ctx.instanceIndex = contexts[b + 1u].w;
    ctx.normal = contexts[b + 2u].xyz;
    ctx.instanceId = contexts[b + 2u].w;
    ctx.uv = contexts[b + 3u].xy;
    ctx.time = contexts[b + 3u].z;
    ctx.depth = contexts[b + 3u].w;
    ctx.instanceRandom = contexts[b + 4u];
    ctx.instanceColor = contexts[b + 5u];
    ctx.instanceEmissive = contexts[b + 6u];
    ctx.audio = contexts[b + 7u];
    ctx.audioBands = contexts[b + 8u];
    ctx.beat = contexts[b + 9u];
    ctx.viewDirection = contexts[b + 10u].xyz;
    let programIndex = i32(contexts[b + 10u].w);
    ctx.curvature = contexts[b + 11u].x;
    ctx.cavity = contexts[b + 11u].y;
    ctx.occlusion = contexts[b + 11u].z;
    ctx.height = contexts[b + 11u].w;
    ctx.normalVariance = contexts[b + 12u].x;
    ctx.footprint = contexts[b + 12u].y;
    ctx.materialId = contexts[b + 12u].z;
    ctx.materialEmission = contexts[b + 13u]; // ADR-904
    ctx.tangent = contexts[b + 14u].xyz;       // ADR-1642
    ctx.bitangent = contexts[b + 15u].xyz;

    var base = materialResultZero();
    base.baseColor = vec3<f32>(0.8, 0.7, 0.6);
    base.metallic = 0.25;
    base.roughness = 0.35;
    base.emission = vec3<f32>(0.1, 0.2, 0.3);
    base.opacity = 0.9;

    let r = evaluateMaterialProgram(programIndex, ctx, base);
    results[i * 4u] = vec4<f32>(r.baseColor, r.metallic);
    results[i * 4u + 1u] = vec4<f32>(r.emission, r.roughness);
    results[i * 4u + 2u] = vec4<f32>(r.opacity, r.occlusion, r.height, 0.0);
    results[i * 4u + 3u] = vec4<f32>(r.normal, 0.0);
}
)";

// ADR-1642: the program texture arrays a harness run binds, and each texture's layer in them.
struct HarnessTextures {
    gpu::GpuTexture srgb;
    gpu::GpuTexture linear;
    scene::MaterialTextureLayerFn layerOf;
};

// One evaluation: a MaterialContext plus the program slot to run it against.
struct Case {
    scene::MaterialContext ctx;
    int program = 0;
};

class MaterialHarness {
public:
    explicit MaterialHarness(gpu::Context& ctx)
        : ctx_(ctx), shaders_(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)}) {
        auto fields = shaders_.loadSource("fields.wgsl");
        REQUIRE(fields.has_value());
        auto material = shaders_.loadSource("material.wgsl");
        REQUIRE(material.has_value());
        auto module = shaders_.compile(*fields + "\n" + *material + kKernel, "material-harness");
        if (!module) {
            FAIL(module.error().message);
        }
        const auto& device = ctx_.device();
        // fields.wgsl declares the shared grid table at binding 15 (ADR-032), so every layout
        // serving a consumer of it must carry that entry too.
        std::array<wgpu::BindGroupLayoutEntry, 8> entries{};
        for (std::uint32_t i = 0; i < 4; ++i) {
            entries[i].binding = i;
            entries[i].visibility = wgpu::ShaderStage::Compute;
        }
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[1].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[2].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[3].buffer.type = wgpu::BufferBindingType::Storage;
        entries[4].binding = 15;
        entries[4].visibility = wgpu::ShaderStage::Compute;
        entries[4].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        for (std::uint32_t i = 5; i < 7; ++i) { // ADR-1642: the program texture arrays and their sampler
            entries[i].binding = 11 + i;
            entries[i].visibility = wgpu::ShaderStage::Compute;
            entries[i].texture.sampleType = wgpu::TextureSampleType::Float;
            entries[i].texture.viewDimension = wgpu::TextureViewDimension::e2DArray;
        }
        entries[7].binding = 18;
        entries[7].visibility = wgpu::ShaderStage::Compute;
        entries[7].sampler.type = wgpu::SamplerBindingType::Filtering;
        wgpu::BindGroupLayoutDescriptor ldesc{};
        ldesc.entryCount = entries.size();
        ldesc.entries = entries.data();
        layout_ = device.CreateBindGroupLayout(&ldesc);
        wgpu::PipelineLayoutDescriptor pdesc{};
        pdesc.bindGroupLayoutCount = 1;
        pdesc.bindGroupLayouts = &layout_;
        wgpu::ComputePipelineDescriptor cdesc{};
        cdesc.layout = device.CreatePipelineLayout(&pdesc);
        cdesc.compute.module = *module;
        cdesc.compute.entryPoint = "cs_material";
        wgpu::ConstantEntry texturesOn{}; // ADR-1642: the harness reads texture ops (the entity pass's variant)
        texturesOn.key = "MAT_PROGRAM_TEXTURES";
        texturesOn.value = 1.0;
        cdesc.compute.constantCount = 1;
        cdesc.compute.constants = &texturesOn;
        pipeline_ = device.CreateComputePipeline(&cdesc);
        REQUIRE(pipeline_ != nullptr);
        auto srgb = gpu::uploadTextureArray(ctx_, {}, true, {255, 255, 255, 255}, "harness-srgb");
        auto linear = gpu::uploadTextureArray(ctx_, {}, false, {128, 128, 255, 255}, "harness-linear");
        REQUIRE(srgb.has_value());
        REQUIRE(linear.has_value());
        defaultTextures_.srgb = std::move(*srgb);
        defaultTextures_.linear = std::move(*linear);
        wgpu::SamplerDescriptor sd{};
        sd.addressModeU = wgpu::AddressMode::Repeat;
        sd.addressModeV = wgpu::AddressMode::Repeat;
        sd.magFilter = wgpu::FilterMode::Linear;
        sd.minFilter = wgpu::FilterMode::Linear;
        sd.mipmapFilter = wgpu::MipmapFilterMode::Linear;
        sampler_ = device.CreateSampler(&sd);
    }

    // Uploads `programs` (at most 8) and evaluates every case; returns one result per case.
    std::vector<scene::MaterialResult> run(const std::vector<MaterialProgram>& programs,
                                           const spatial::FieldSet& fields, double time,
                                           const std::vector<Case>& cases, const HarnessTextures* textures = nullptr) {
        const HarnessTextures& tex = textures != nullptr ? *textures : defaultTextures_;
        rendering::FieldUniforms fieldBlock(ctx_);
        fieldBlock.update(fields, time);
        rendering::MaterialPrograms block(ctx_);
        block.update(programs, &fieldBlock, tex.layerOf);

        std::vector<glm::vec4> packed(cases.size() * 16, glm::vec4(0.0f));
        for (std::size_t i = 0; i < cases.size(); ++i) {
            const scene::MaterialContext& c = cases[i].ctx;
            glm::vec4* p = packed.data() + i * 16;
            p[0] = glm::vec4(c.worldPosition, c.objectId);
            p[1] = glm::vec4(c.localPosition, c.instanceIndex);
            p[2] = glm::vec4(c.normal, c.instanceId);
            p[3] = glm::vec4(c.uv, c.time, c.depth);
            p[4] = c.instanceRandom;
            p[5] = c.instanceColor;
            p[6] = c.instanceEmissive;
            p[7] = c.audio;
            p[8] = c.audioBands;
            p[9] = c.beat;
            p[10] = glm::vec4(c.viewDirection, static_cast<float>(cases[i].program));
            p[11] = glm::vec4(c.curvature, c.cavity, c.occlusion, c.height);
            p[12] = glm::vec4(c.normalVariance, c.footprint, c.materialId, 0.0f);
            p[13] = c.materialEmission; // ADR-904
            p[14] = glm::vec4(c.tangent, 0.0f);   // ADR-1642
            p[15] = glm::vec4(c.bitangent, 0.0f);
        }

        const auto& device = ctx_.device();
        wgpu::BufferDescriptor cdesc{};
        cdesc.label = "material-harness-contexts";
        cdesc.size = packed.size() * sizeof(glm::vec4);
        cdesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
        wgpu::Buffer contexts = device.CreateBuffer(&cdesc);
        ctx_.queue().WriteBuffer(contexts, 0, packed.data(), cdesc.size);
        wgpu::BufferDescriptor rdesc{};
        rdesc.label = "material-harness-results";
        rdesc.size = cases.size() * 4 * sizeof(glm::vec4);
        rdesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc;
        wgpu::Buffer results = device.CreateBuffer(&rdesc);

        std::array<wgpu::BindGroupEntry, 8> entries{};
        entries[0].binding = 0;
        entries[0].buffer = fieldBlock.buffer();
        entries[0].size = rendering::FieldUniforms::kBufferSize;
        entries[1].binding = 1;
        entries[1].buffer = block.buffer();
        entries[1].size = rendering::MaterialPrograms::kBufferSize;
        entries[2].binding = 2;
        entries[2].buffer = contexts;
        entries[2].size = cdesc.size;
        entries[3].binding = 3;
        entries[3].buffer = results;
        entries[3].size = rdesc.size;
        entries[4].binding = 15;
        entries[4].buffer = fieldBlock.gridBuffer();
        entries[4].size = rendering::FieldUniforms::kGridBufferSize;
        entries[5].binding = 16; // ADR-1642
        entries[5].textureView = tex.srgb.view;
        entries[6].binding = 17;
        entries[6].textureView = tex.linear.view;
        entries[7].binding = 18;
        entries[7].sampler = sampler_;
        wgpu::BindGroupDescriptor gdesc{};
        gdesc.layout = layout_;
        gdesc.entryCount = entries.size();
        gdesc.entries = entries.data();
        wgpu::BindGroup group = device.CreateBindGroup(&gdesc);

        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
        pass.SetPipeline(pipeline_);
        pass.SetBindGroup(0, group);
        pass.DispatchWorkgroups(static_cast<std::uint32_t>((cases.size() + 63) / 64));
        pass.End();
        wgpu::CommandBuffer commands = encoder.Finish();
        ctx_.queue().Submit(1, &commands);
        auto bytes = gpu::readBuffer(ctx_, results, 0, rdesc.size);
        REQUIRE(bytes.has_value());
        std::vector<glm::vec4> raw(cases.size() * 4);
        std::memcpy(raw.data(), bytes->data(), rdesc.size);
        std::vector<scene::MaterialResult> out(cases.size());
        for (std::size_t i = 0; i < cases.size(); ++i) {
            out[i].baseColor = glm::vec3(raw[i * 4]);
            out[i].metallic = raw[i * 4].w;
            out[i].emission = glm::vec3(raw[i * 4 + 1]);
            out[i].roughness = raw[i * 4 + 1].w;
            out[i].opacity = raw[i * 4 + 2].x;
            out[i].occlusion = raw[i * 4 + 2].y;
            out[i].height = raw[i * 4 + 2].z;
            out[i].normal = glm::vec3(raw[i * 4 + 3]);
        }
        return out;
    }

private:
    gpu::Context& ctx_;
    gpu::ShaderLibrary shaders_;
    wgpu::BindGroupLayout layout_;
    wgpu::ComputePipeline pipeline_;
    HarnessTextures defaultTextures_;
    wgpu::Sampler sampler_;
};

// The CPU field sampler the Field op reads through: exactly what materialFieldValue does on the GPU.
class SetSampler : public scene::MaterialFieldSampler {
public:
    SetSampler(const spatial::FieldSet& set, double time) : set_(set), time_(time) {}
    [[nodiscard]] glm::vec4 sample(std::string_view field, const glm::vec3& p) const override {
        const spatial::FieldSpec* f = set_.find(field);
        if (f == nullptr || !f->enabled) {
            return glm::vec4(0.0f);
        }
        switch (f->type()) {
        case spatial::FieldType::Vector:
            return glm::vec4(spatial::sampleVector(*f, p, time_, &set_), 0.0f);
        case spatial::FieldType::Color:
            return spatial::sampleColor(*f, p, time_, &set_);
        case spatial::FieldType::Scalar:
            break;
        }
        return glm::vec4(spatial::sampleScalar(*f, p, time_, &set_));
    }

private:
    const spatial::FieldSet& set_;
    double time_;
};

// ---- program builders ------------------------------------------------------------------------------

MaterialOp op(MaterialOpKind kind, int dst, int srcA = 0, int srcB = 0, int srcC = 0) {
    MaterialOp o;
    o.kind = kind;
    o.dst = dst;
    o.srcA = srcA;
    o.srcB = srcB;
    o.srcC = srcC;
    return o;
}

MaterialOp inputOp(MaterialInput input, int dst) {
    MaterialOp o = op(MaterialOpKind::Input, dst);
    o.input = input;
    return o;
}

MaterialOp constantOp(int dst, const glm::vec4& k) {
    MaterialOp o = op(MaterialOpKind::Constant, dst);
    o.constant = k;
    return o;
}

// A program whose last op writes r7 and whose every output reads it (emission unclamped x1.5).
MaterialProgram probe(std::string name, std::vector<MaterialOp> ops) {
    MaterialProgram p;
    p.name = std::move(name);
    p.ops = std::move(ops);
    p.baseColorRegister = 7;
    p.metallicRegister = 7;
    p.roughnessRegister = 7;
    p.emissionRegister = 7;
    p.emissionIntensity = 1.5f;
    p.opacityRegister = 7;
    return p;
}

// A deterministic spread of contexts.
std::vector<scene::MaterialContext> makeContexts() {
    std::vector<scene::MaterialContext> out;
    for (int i = 0; i < 10; ++i) {
        const float t = static_cast<float>(i);
        scene::MaterialContext c;
        c.worldPosition = {std::cos(t * 1.3f) * (0.4f + t * 0.35f), t * 0.21f - 0.7f, std::sin(t * 0.9f) * 1.7f};
        c.localPosition = c.worldPosition * 0.5f - glm::vec3(0.3f, 0.1f, -0.2f);
        c.normal = glm::normalize(glm::vec3(std::sin(t), 0.6f, std::cos(t * 0.7f)));
        c.uv = {std::fmod(t * 0.37f, 1.0f), std::fmod(t * 0.11f + 0.2f, 1.0f)};
        c.objectId = t;
        c.instanceIndex = t / 9.0f;
        c.instanceId = t * 3.0f;
        c.instanceRandom = {std::fmod(t * 0.31f, 1.0f), std::fmod(t * 0.57f, 1.0f), std::fmod(t * 0.79f, 1.0f),
                            std::fmod(t * 0.13f, 1.0f)};
        c.instanceColor = {0.9f - t * 0.05f, 0.3f + t * 0.04f, 0.5f, 1.0f};
        c.instanceEmissive = {0.2f, 0.4f + t * 0.03f, 0.8f, 0.0f};
        c.time = t * 0.4f;
        c.audio = {0.3f + t * 0.05f, 0.7f, 0.2f, 0.55f};
        c.audioBands = {0.11f, 0.44f, 0.66f, 0.22f};
        c.beat = {std::fmod(t * 0.23f, 1.0f), 0.5f, 0.8f, 0.25f};
        c.viewDirection = glm::normalize(glm::vec3(0.2f, 0.4f, 1.0f - t * 0.05f));
        c.depth = 2.0f + t;
        // ADR-036 geometric lanes: a spread that covers convex, flat and concave fragments, a
        // footprint from "sharp" to "several noise periods per pixel", and both AO extremes.
        c.curvature = (t - 4.5f) * 0.35f;
        c.cavity = std::fmod(t * 0.17f, 1.0f);
        c.occlusion = 1.0f - std::fmod(t * 0.11f, 1.0f);
        c.height = std::fmod(t * 0.29f, 1.0f);
        c.normalVariance = t * 0.011f;
        c.footprint = t * 0.004f;
        c.materialId = std::fmod(t, 4.0f);
        c.materialEmission = {0.35f + t * 0.02f, 0.1f + t * 0.05f, 0.9f - t * 0.03f, 1.0f}; // ADR-904
        out.push_back(c);
    }
    return out;
}

float maxAbs(const glm::vec3& v) {
    return std::max(std::abs(v.x), std::max(std::abs(v.y), std::abs(v.z)));
}

// Runs one batch of programs (at most 8) over every context and compares against the CPU.
void checkParity(MaterialHarness& harness, const std::vector<MaterialProgram>& programs,
                 const spatial::FieldSet& fields, double time, float tolerance = 1e-4f) {
    REQUIRE(programs.size() <= static_cast<std::size_t>(rendering::kMaxGpuMaterialPrograms));
    const auto contexts = makeContexts();
    const SetSampler sampler(fields, time);
    std::vector<Case> cases;
    for (std::size_t p = 0; p < programs.size(); ++p) {
        for (const auto& c : contexts) {
            Case k;
            k.ctx = c;
            k.ctx.fields = &sampler;
            k.program = static_cast<int>(p);
            cases.push_back(k);
        }
    }
    const auto gpu = harness.run(programs, fields, time, cases);
    REQUIRE(gpu.size() == cases.size());
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const MaterialProgram& program = programs[static_cast<std::size_t>(cases[i].program)];
        REQUIRE(program.validate().has_value());
        const scene::MaterialResult cpu = scene::evaluateMaterialProgram(program, cases[i].ctx, harnessBase());
        const scene::MaterialResult& g = gpu[i];
        const float worst = std::max({maxAbs(cpu.baseColor - g.baseColor), maxAbs(cpu.emission - g.emission),
                                      std::abs(cpu.metallic - g.metallic), std::abs(cpu.roughness - g.roughness),
                                      std::abs(cpu.opacity - g.opacity), maxAbs(cpu.normal - g.normal),
                                      std::abs(cpu.occlusion - g.occlusion), std::abs(cpu.height - g.height)});
        if (worst > tolerance) {
            INFO("program '" << program.name << "' context " << (i % 10) << " differs by " << worst);
            CHECK(worst <= tolerance);
        }
    }
}

// ---- scenes for the render tests --------------------------------------------------------------------

std::uint64_t sourceHash(int salt) {
    return 0xC0FFEEull + static_cast<std::uint64_t>(salt) * 0x9E3779B9ull;
}

scene::Scene baseScene() {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.environment.gridIntensity = 0.0f;
    s.post.bloomEnabled = false;
    s.post.tonemap = scene::TonemapOperator::Clamp;
    s.camera.position = {0.0f, 0.0f, 9.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.6f));
    key.intensity = 3.0f;
    s.addLight(key);
    return s;
}

// A wall of boxes spanning y in [-3, 3]: the height gradient in the program paints them.
scene::ProceduralGeometry boxWall() {
    scene::ProceduralGeometry g;
    g.name = "wall";
    g.source.kind = scene::PrimitiveKind::Box;
    g.source.size = {0.9f, 0.9f, 0.9f};
    g.structureVersion = 1;
    g.meshHash = sourceHash(1);
    g.material.baseColor = {0.5f, 0.5f, 0.5f};
    g.material.unlit = true;
    for (int i = 0; i < 7; ++i) {
        scene::InstanceRecord r{};
        const float y = -3.0f + static_cast<float>(i);
        r.position = {0.0f, y, 0.0f, 1.0f};
        r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        r.scale = {1.0f, 1.0f, 1.0f, static_cast<float>(i) / 6.0f};
        r.random = {0.1f, 0.2f, 0.3f, 0.4f};
        r.color = {1.0f, 1.0f, 1.0f, static_cast<float>(i)};
        r.emissive = {1.0f, 1.0f, 1.0f, 0.0f};
        g.instances.push_back(r);
    }
    return g;
}

// Height gradient -> three-stop ramp (red at the bottom, green in the middle, blue at the top),
// with an fBM noise roughness. Unlit, so the ramp colour reaches the image unchanged.
MaterialProgram heightRampProgram() {
    MaterialProgram p;
    p.name = "heightRamp";
    p.ops.push_back(inputOp(MaterialInput::WorldPosition, 0));
    MaterialOp grad = op(MaterialOpKind::Gradient, 1, 0);
    grad.constant = {0.0f, 1.0f, 0.0f, 0.5f}; // y / 6 + 0.5 over y in [-3, 3]
    grad.value = 1.0f / 6.0f;
    p.ops.push_back(grad);
    MaterialOp ramp = op(MaterialOpKind::Ramp, 2, 1);
    ramp.constant = {1.0f, 0.0f, 0.0f, 1.0f};
    ramp.constant2 = {0.0f, 1.0f, 0.0f, 1.0f};
    ramp.constant3 = {0.0f, 0.0f, 1.0f, 1.0f};
    p.ops.push_back(ramp);
    MaterialOp noise = op(MaterialOpKind::Noise, 3, 0);
    noise.value = 0.7f;
    noise.seed = 5;
    p.ops.push_back(noise);
    p.baseColorRegister = 2;
    p.roughnessRegister = 3;
    return p;
}

gpu::Image8 renderWith(rendering::SceneRenderer& renderer, const scene::Scene& s, std::uint32_t w = 160,
                       std::uint32_t h = 160) {
    FrameTime t{};
    t.renderTime = 0.0;
    auto img = renderer.renderToImage(s, t, w, h);
    REQUIRE(img.has_value());
    return *img;
}

bool brighterThan(const std::uint8_t* p, int v) {
    return p[0] > v || p[1] > v || p[2] > v;
}

} // namespace

// =================================================================================================

TEST_CASE("material program inputs match the CPU interpreter", "[material][gpu]") {
    auto ctx = makeContext();
    MaterialHarness harness(*ctx);
    const spatial::FieldSet noFields;
    const auto build = [](const char* name, MaterialInput input) {
        return probe(name, {inputOp(input, 7)});
    };
    checkParity(harness,
                {build("worldPosition", MaterialInput::WorldPosition),
                 build("localPosition", MaterialInput::LocalPosition), build("normal", MaterialInput::Normal),
                 build("uv", MaterialInput::Uv), build("objectId", MaterialInput::ObjectId),
                 build("instanceIndex", MaterialInput::InstanceIndex), build("instanceId", MaterialInput::InstanceId),
                 build("instanceRandom", MaterialInput::InstanceRandom)},
                noFields, 0.0);
    checkParity(harness,
                {build("instanceColor", MaterialInput::InstanceColor),
                 build("instanceEmissive", MaterialInput::InstanceEmissive), build("time", MaterialInput::Time),
                 build("audio", MaterialInput::Audio), build("audioBands", MaterialInput::AudioBands),
                 build("beatPhase", MaterialInput::BeatPhase), build("viewDirection", MaterialInput::ViewDirection),
                 build("depth", MaterialInput::Depth)},
                noFields, 0.0);
    // ADR-904: the material's own emission as the instance shows it.
    checkParity(harness, {build("materialEmission", MaterialInput::MaterialEmission)}, noFields, 0.0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("material program ops match the CPU interpreter", "[material][gpu]") {
    auto ctx = makeContext();
    MaterialHarness harness(*ctx);
    const spatial::FieldSet noFields;
    // r0 = worldPosition, r1 = a colour constant, r2 = a second colour constant.
    const auto prelude = [] {
        return std::vector<MaterialOp>{inputOp(MaterialInput::WorldPosition, 0),
                                       constantOp(1, {0.6f, 0.25f, 0.9f, 0.4f}),
                                       constantOp(2, {0.15f, 0.8f, 0.35f, 0.7f})};
    };
    const auto with = [&prelude](const char* name, std::vector<MaterialOp> tail) {
        std::vector<MaterialOp> ops = prelude();
        ops.insert(ops.end(), tail.begin(), tail.end());
        return probe(name, std::move(ops));
    };

    std::vector<MaterialProgram> batch;
    batch.push_back(with("constant", {constantOp(7, {-0.3f, 0.5f, 1.6f, 0.25f})}));
    {
        MaterialOp o = op(MaterialOpKind::Gradient, 7, 0);
        o.constant = {0.3f, -0.2f, 0.5f, 0.1f};
        o.value = 1.7f;
        batch.push_back(with("gradient", {o}));
    }
    {
        MaterialOp o = op(MaterialOpKind::Noise, 7, 0);
        o.constant = {1.5f, -2.5f, 0.75f, 0.0f};
        o.value = 0.9f;
        o.seed = 7;
        batch.push_back(with("noise", {o}));
    }
    {
        MaterialOp o = op(MaterialOpKind::Voronoi, 7, 0);
        o.constant = {0.25f, 3.0f, -1.0f, 0.0f};
        o.value = 1.4f;
        o.seed = 3;
        batch.push_back(with("voronoi", {o}));
    }
    {
        MaterialOp o = op(MaterialOpKind::Fresnel, 7);
        o.value = 3.5f;
        batch.push_back(with("fresnel", {o}));
    }
    {
        MaterialOp g = op(MaterialOpKind::Gradient, 3, 0);
        g.constant = {0.0f, 0.4f, 0.0f, 0.5f};
        g.value = 1.0f;
        MaterialOp o = op(MaterialOpKind::Ramp, 7, 3);
        o.constant = {0.9f, 0.1f, 0.0f, 1.0f};
        o.constant2 = {0.0f, 0.7f, 0.2f, 0.5f};
        o.constant3 = {0.1f, 0.0f, 1.0f, 0.0f};
        batch.push_back(with("ramp", {g, o}));
    }
    {
        MaterialOp o = op(MaterialOpKind::Remap, 7, 1);
        o.constant = {0.2f, 0.8f, -0.5f, 1.5f};
        o.value = 1.0f; // clamped
        MaterialOp o2 = op(MaterialOpKind::Remap, 6, 1);
        o2.constant = {0.3f, 0.3f, 0.25f, 2.0f}; // degenerate input span
        o2.value = 0.0f;
        MaterialOp add = op(MaterialOpKind::Add, 7, 7, 6);
        batch.push_back(with("remap", {o, o2, add}));
    }
    batch.push_back(with("multiply", {op(MaterialOpKind::Multiply, 7, 1, 2)}));
    checkParity(harness, batch, noFields, 0.0, 1e-3f); // noise / voronoi share the batch

    batch.clear();
    batch.push_back(with("add", {op(MaterialOpKind::Add, 7, 1, 2)}));
    {
        MaterialOp o = op(MaterialOpKind::Mix, 7, 1, 2);
        o.value = 0.35f;
        batch.push_back(with("mix", {o}));
    }
    {
        MaterialOp g = op(MaterialOpKind::Gradient, 3, 0);
        g.constant = {0.5f, 0.0f, 0.0f, 0.5f};
        g.value = 1.0f;
        batch.push_back(with("mixBy", {g, op(MaterialOpKind::MixBy, 7, 1, 2, 3)}));
    }
    {
        MaterialOp neg = constantOp(3, {-0.4f, 0.5f, 2.0f, 0.0f});
        MaterialOp o = op(MaterialOpKind::Power, 7, 3);
        o.value = 2.5f;
        MaterialOp zero = op(MaterialOpKind::Power, 6, 3);
        zero.value = 0.0f; // pow(0, 0) == 1
        MaterialOp add = op(MaterialOpKind::Add, 7, 7, 6);
        batch.push_back(with("power", {neg, o, zero, add}));
    }
    {
        MaterialOp o = op(MaterialOpKind::Smoothstep, 7, 1);
        o.constant = {0.2f, 0.7f, 0.0f, 0.0f};
        MaterialOp degenerate = op(MaterialOpKind::Smoothstep, 6, 2);
        degenerate.constant = {0.4f, 0.4f, 0.0f, 0.0f}; // step(0.4, a)
        MaterialOp add = op(MaterialOpKind::Add, 7, 7, 6);
        batch.push_back(with("smoothstep", {o, degenerate, add}));
    }
    {
        MaterialOp o = op(MaterialOpKind::Threshold, 7, 1);
        o.value = 0.4f;
        batch.push_back(with("threshold", {o}));
    }
    {
        MaterialOp o = op(MaterialOpKind::HueShift, 7, 1, 2);
        o.value = 0.31f; // plus reg2.x
        batch.push_back(with("hueShift", {o}));
    }
    {
        MaterialOp o = op(MaterialOpKind::Saturate, 7, 1);
        o.value = 1.8f;
        MaterialOp grey = op(MaterialOpKind::Saturate, 6, 2);
        grey.value = 0.0f;
        MaterialOp add = op(MaterialOpKind::Add, 7, 7, 6);
        batch.push_back(with("saturate", {o, grey, add}));
    }
    checkParity(harness, batch, noFields, 0.0);
    CHECK(ctx->errorCount() == 0);
}

// ---- ADR-036: the geometric inputs, the new ops and layers ---------------------------------------

TEST_CASE("ADR-036 geometric inputs match the CPU interpreter", "[material][gpu]") {
    auto ctx = makeContext();
    MaterialHarness harness(*ctx);
    const spatial::FieldSet noFields;
    const auto build = [](const char* name, MaterialInput input) { return probe(name, {inputOp(input, 7)}); };
    checkParity(harness,
                {build("curvature", MaterialInput::Curvature), build("convexity", MaterialInput::Convexity),
                 build("concavity", MaterialInput::Concavity), build("cavity", MaterialInput::Cavity),
                 build("occlusion", MaterialInput::Occlusion), build("height", MaterialInput::Height),
                 build("normalVariance", MaterialInput::NormalVariance),
                 build("objectPosition", MaterialInput::ObjectPosition)},
                noFields, 0.0);
    checkParity(harness,
                {build("triplanarWeights", MaterialInput::TriplanarWeights),
                 build("cameraDistance", MaterialInput::CameraDistance),
                 build("materialId", MaterialInput::MaterialId), build("footprint", MaterialInput::Footprint)},
                noFields, 0.0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("ADR-036 ops match the CPU interpreter", "[material][gpu]") {
    auto ctx = makeContext();
    MaterialHarness harness(*ctx);
    const spatial::FieldSet noFields;
    std::vector<MaterialProgram> batch;

    {
        MaterialOp o = op(MaterialOpKind::WorldProject, 7);
        o.value = 1.7f;
        o.constant = {0.3f, -0.6f, 0.2f, 0.0f};
        batch.push_back(probe("worldProject", {o}));
        o.kind = MaterialOpKind::ObjectProject;
        batch.push_back(probe("objectProject", {o}));
    }
    {
        MaterialOp o = op(MaterialOpKind::Triplanar, 7, 0);
        o.value = 0.8f;
        o.seed = 13;
        o.constant = {0.2f, 0.0f, -0.4f, 0.0f};
        o.constant2 = {4.0f, 0.0f, 0.0f, 0.0f};
        batch.push_back(probe("triplanar", {inputOp(MaterialInput::WorldPosition, 0), o}));
        MaterialOp sharp = o;
        sharp.constant2 = {12.0f, 0.0f, 0.0f, 0.0f};
        batch.push_back(probe("triplanarSharp", {inputOp(MaterialInput::WorldPosition, 0), sharp}));
    }
    {
        MaterialOp o = op(MaterialOpKind::HeightBlend, 7, 1, 2, 3);
        o.value = 0.35f;
        batch.push_back(probe("heightBlend", {inputOp(MaterialInput::Height, 1),
                                              inputOp(MaterialInput::Cavity, 2),
                                              inputOp(MaterialInput::Occlusion, 3), o}));
    }
    {
        MaterialOp o = op(MaterialOpKind::DetailNormal, 7, 1, 2);
        batch.push_back(probe("detailNormal", {inputOp(MaterialInput::Normal, 1),
                                               constantOp(2, {-0.3f, 0.2f, 0.9f, 0.0f}), o}));
    }
    {
        MaterialOp o = op(MaterialOpKind::CurvatureMask, 7);
        o.value = 1.8f;
        o.constant = {0.05f, 0.7f, 0.0f, 0.0f};
        batch.push_back(probe("curvatureMask", {o}));
    }
    {
        MaterialOp o = op(MaterialOpKind::EdgeWear, 7);
        o.value = 2.5f;
        o.constant = {1.3f, 0.4f, -0.2f, 0.7f};
        o.constant2 = {0.6f, 0.05f, 0.85f, 0.0f};
        o.seed = 23;
        batch.push_back(probe("edgeWear", {o}));
    }
    checkParity(harness, batch, noFields, 0.0, 1e-3f); // noise-based ops
    batch.clear();

    {
        MaterialOp o = op(MaterialOpKind::DecalBox, 7);
        o.value = 0.35f;
        o.constant = {0.2f, -0.1f, 0.4f, 0.0f};
        o.constant2 = {1.5f, 0.9f, 2.0f, 0.0f};
        batch.push_back(probe("decalBox", {o}));
        MaterialOp hard = o;
        hard.value = 0.0f;
        batch.push_back(probe("decalBoxHard", {hard}));
    }
    {
        MaterialOp o = op(MaterialOpKind::Anisotropy, 7, 1);
        o.value = 0.7f;
        o.constant = {0.0f, 1.0f, 0.0f, 0.0f};
        batch.push_back(probe("anisotropy", {constantOp(1, glm::vec4(0.32f)), o}));
        MaterialOp negative = o;
        negative.value = -0.7f;
        negative.constant = {1.0f, 0.0f, 0.0f, 0.0f};
        batch.push_back(probe("anisotropyNegative", {constantOp(1, glm::vec4(0.32f)), negative}));
        MaterialOp degenerate = o;
        degenerate.constant = {0.0f, 0.0f, 0.0f, 0.0f}; // no direction at all
        batch.push_back(probe("anisotropyDegenerate", {constantOp(1, glm::vec4(0.32f)), degenerate}));
    }
    {
        MaterialOp o = op(MaterialOpKind::RoughnessFilter, 7, 1);
        o.value = 1.0f;
        batch.push_back(probe("roughnessFilter", {constantOp(1, glm::vec4(0.25f)), o}));
        MaterialOp strong = o;
        strong.value = 40.0f; // saturates the clamped kernel
        batch.push_back(probe("roughnessFilterClamped", {constantOp(1, glm::vec4(0.25f)), strong}));
    }
    checkParity(harness, batch, noFields, 0.0);
    batch.clear();

    {
        MaterialOp o = op(MaterialOpKind::MicroDetail, 7, 0);
        o.value = 60.0f;
        o.seed = 29;
        o.constant = {0.1f, 0.2f, 0.3f, 0.0f};
        batch.push_back(probe("microDetail", {inputOp(MaterialInput::WorldPosition, 0), o}));
    }
    {
        // ADR-1069: Worley F2 - F1 (the cell edge), F1, the cell hash and F2, against the CPU twin.
        MaterialOp o = op(MaterialOpKind::VoronoiEdge, 7, 0);
        o.value = 3.0f;
        o.seed = 11;
        o.constant = {0.4f, -0.2f, 0.7f, 0.0f};
        batch.push_back(probe("voronoiEdge", {inputOp(MaterialInput::WorldPosition, 0), o}));
    }
    {
        // ADR-1162: the cell centre and the per-cell random, against the CPU twin.
        MaterialOp o = op(MaterialOpKind::Quantize, 7, 0);
        o.value = 3.0f;
        o.seed = 17;
        o.constant = {0.25f, 0.0f, -0.5f, 0.0f};
        batch.push_back(probe("quantize", {inputOp(MaterialInput::WorldPosition, 0), o}));
    }
    {
        // ADR-1470: four stops in OKLab, clamped and cyclic, driven across and past [0, 1] by the world position's x
        // (the contexts span about -3.5 .. 3.5 m), against the CPU twin. The stops are saturated and far apart in
        // hue so a linear-RGB shortcut on either side would show.
        MaterialOp o = op(MaterialOpKind::OklabRamp, 7, 0);
        o.constant = {0.02f, 0.03f, 0.35f, 1.0f};
        o.constant2 = {0.25f, 0.02f, 0.60f, 0.8f};
        o.constant3 = {0.90f, 0.05f, 0.40f, 0.6f};
        o.constant4 = {0.05f, 0.80f, 0.90f, 0.2f};
        batch.push_back(probe("oklabRamp", {inputOp(MaterialInput::WorldPosition, 0), o}));
        MaterialOp cyclic = o;
        cyclic.value = -1.0f;
        batch.push_back(probe("oklabRampCyclic", {inputOp(MaterialInput::WorldPosition, 0), cyclic}));
    }
    checkParity(harness, batch, noFields, 0.0, 1e-3f);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("swizzle moves a component into the channel every mask op reads", "[material][gpu]") {
    // Every op that takes a scalar takes it from x. The values worth masking with mostly arrive
    // somewhere else -- terrain carries its biome axis and its slope in the two halves of a uv, and
    // without this op a program can use exactly one of them.
    auto ctx = makeContext();
    MaterialHarness harness(*ctx);
    const spatial::FieldSet noFields;
    std::vector<MaterialProgram> batch;
    {
        // Broadcast y into every channel: the default constant is all zeroes, so the pattern has to
        // be written out, which is also the read the terrain material makes.
        MaterialOp o = op(MaterialOpKind::Swizzle, 7, 1);
        o.constant = {1.0f, 1.0f, 1.0f, 1.0f};
        batch.push_back(probe("swizzleBroadcastY", {constantOp(1, {0.2f, 0.7f, 0.4f, 0.9f}), o}));
    }
    {
        MaterialOp o = op(MaterialOpKind::Swizzle, 7, 1); // all-zero constant broadcasts x
        batch.push_back(probe("swizzleDefaultX", {constantOp(1, {0.2f, 0.7f, 0.4f, 0.9f}), o}));
    }
    {
        MaterialOp o = op(MaterialOpKind::Swizzle, 7, 1);
        o.constant = {3.0f, 2.0f, 1.0f, 0.0f}; // reverse
        batch.push_back(probe("swizzleReverse", {constantOp(1, {0.2f, 0.7f, 0.4f, 0.9f}), o}));
    }
    {
        MaterialOp o = op(MaterialOpKind::Swizzle, 7, 1);
        o.constant = {-4.0f, 9.0f, 1.0f, 1.0f}; // out of range clamps rather than reading rubbish
        batch.push_back(probe("swizzleClamped", {constantOp(1, {0.2f, 0.7f, 0.4f, 0.9f}), o}));
    }
    {
        // The case it exists for: a normal's y is the slope, and a mask op can only see x.
        MaterialOp pickY = op(MaterialOpKind::Swizzle, 1, 0);
        pickY.constant = {1.0f, 1.0f, 1.0f, 1.0f};
        MaterialOp blend = op(MaterialOpKind::MixBy, 7, 2, 3, 1);
        batch.push_back(probe("swizzleAsMask", {inputOp(MaterialInput::Normal, 0), pickY,
                                                constantOp(2, {1.0f, 0.0f, 0.0f, 1.0f}),
                                                constantOp(3, {0.0f, 0.0f, 1.0f, 1.0f}), blend}));
    }
    checkParity(harness, batch, noFields, 0.0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("layered programs match the CPU interpreter", "[material][gpu]") {
    auto ctx = makeContext();
    MaterialHarness harness(*ctx);
    const spatial::FieldSet noFields;

    // A base plus three layers that between them write every channel, mask from a geometric input
    // and blend against a real height field.
    MaterialProgram p;
    p.name = "layered";
    p.ops = {inputOp(MaterialInput::WorldPosition, 0), constantOp(1, {0.6f, 0.3f, 0.2f, 1.0f})};
    {
        MaterialOp height = op(MaterialOpKind::Triplanar, 2, 0);
        height.value = 0.9f;
        height.seed = 7;
        height.constant2 = {4.0f, 0.0f, 0.0f, 0.0f};
        p.ops.push_back(height);
    }
    p.baseColorRegister = 1;
    p.roughnessRegister = 2;
    p.heightRegister = 2;

    scene::MaterialLayer rust;
    rust.name = "rust";
    rust.ops = {inputOp(MaterialInput::Cavity, 3), constantOp(4, {0.3f, 0.12f, 0.05f, 1.0f}),
                constantOp(5, glm::vec4(0.85f))};
    rust.maskRegister = 3;
    rust.blendRange = 0.3f;
    rust.baseColorRegister = 4;
    rust.roughnessRegister = 5;
    rust.metallicRegister = 3;
    p.layers.push_back(rust);

    scene::MaterialLayer wear;
    wear.name = "wear";
    {
        MaterialOp o = op(MaterialOpKind::CurvatureMask, 6);
        o.value = 1.2f;
        o.constant = {0.0f, 0.9f, 0.0f, 0.0f};
        wear.ops = {o, constantOp(7, {0.95f, 0.95f, 1.0f, 1.0f})};
    }
    wear.maskRegister = 6;
    wear.heightRegister = 2;
    wear.blendRange = 0.2f;
    wear.baseColorRegister = 7;
    wear.emissionRegister = 7;
    wear.emissionIntensity = 0.4f;
    p.layers.push_back(wear);

    scene::MaterialLayer glow;
    glow.name = "glow";
    glow.ops = {inputOp(MaterialInput::Occlusion, 3), constantOp(4, {0.1f, 0.4f, 0.9f, 0.0f}),
                constantOp(5, glm::vec4(0.6f))};
    glow.maskRegister = 3;
    glow.normalRegister = 4;
    glow.occlusionRegister = 5;
    p.layers.push_back(glow);
    REQUIRE(p.validate().has_value());

    // The same program with the middle layer disabled must differ, and must match the CPU too.
    MaterialProgram disabled = p;
    disabled.name = "layeredDisabled";
    disabled.layers[1].enabled = false;

    // A base-only program: the pre-ADR-036 path, byte for byte.
    MaterialProgram plain = p;
    plain.name = "baseOnly";
    plain.layers.clear();

    checkParity(harness, {p, disabled, plain}, noFields, 0.0, 1e-3f);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("material field ops and program outputs match the CPU interpreter", "[material][gpu]") {
    auto ctx = makeContext();
    MaterialHarness harness(*ctx);

    spatial::FieldSet fields;
    {
        spatial::FieldSpec f;
        f.name = "grain";
        f.kind = spatial::FieldKind::Noise;
        f.frequency = 0.8f;
        f.strength = 1.3f;
        fields.fields.push_back(f);
    }
    {
        spatial::FieldSpec f;
        f.name = "swirl";
        f.kind = spatial::FieldKind::Vortex;
        f.strength = 1.1f;
        f.axis = {0.0f, 1.0f, 0.0f};
        fields.fields.push_back(f);
    }
    {
        spatial::FieldSpec f;
        f.name = "tint";
        f.kind = spatial::FieldKind::Gradient;
        f.colorA = {0.9f, 0.2f, 0.1f, 1.0f};
        f.colorB = {0.1f, 0.3f, 1.0f, 1.0f};
        f.length = 4.0f;
        f.strength = 0.8f;
        fields.fields.push_back(f);
    }

    const auto fieldProbe = [](const char* name, const char* field) {
        MaterialOp o = op(MaterialOpKind::Field, 7);
        o.field = field;
        return probe(name, {o});
    };

    std::vector<MaterialProgram> batch;
    batch.push_back(fieldProbe("fieldScalar", "grain"));
    batch.push_back(fieldProbe("fieldVector", "swirl"));
    batch.push_back(fieldProbe("fieldColor", "tint"));
    batch.push_back(fieldProbe("fieldMissing", "nosuchfield"));
    {
        MaterialOp o = op(MaterialOpKind::Palette, 7, 0);
        o.constant = {0.5f, 0.5f, 0.5f, 0.0f};
        o.constant2 = {0.5f, 0.4f, 0.45f, 0.0f};
        o.constant3 = {1.0f, 1.0f, 0.5f, 0.0f};
        o.constant4 = {0.0f, 0.33f, 0.67f, 0.0f};
        o.value = 0.15f;
        batch.push_back(probe("palette", {inputOp(MaterialInput::Time, 0), o}));
    }
    {
        // A disabled op must be skipped on both sides (it is not packed at all).
        MaterialOp disabled = constantOp(7, {9.0f, 9.0f, 9.0f, 9.0f});
        disabled.enabled = false;
        batch.push_back(probe("disabled", {constantOp(7, {0.2f, 0.4f, 0.6f, 0.8f}), disabled}));
    }
    {
        // Every output -1: the material's own values come back untouched.
        MaterialProgram p;
        p.name = "keepAll";
        p.ops.push_back(constantOp(3, {1.0f, 1.0f, 1.0f, 1.0f}));
        batch.push_back(p);
    }
    {
        // A full 16-op program mixing fields, noise, colour ops and every output register.
        MaterialProgram p;
        p.name = "full";
        p.ops.push_back(inputOp(MaterialInput::WorldPosition, 0));      // 1
        p.ops.push_back(inputOp(MaterialInput::Audio, 1));              // 2
        MaterialOp drift = constantOp(2, {0.0f, 0.0f, 0.15f, 0.0f});
        p.ops.push_back(drift);                                        // 3
        p.ops.push_back(inputOp(MaterialInput::Time, 3));               // 4
        p.ops.push_back(op(MaterialOpKind::Multiply, 2, 2, 3));         // 5
        p.ops.push_back(op(MaterialOpKind::Add, 2, 0, 2));              // 6
        MaterialOp noise = op(MaterialOpKind::Noise, 4, 2);
        noise.value = 1.2f;
        noise.seed = 11;
        p.ops.push_back(noise);                                        // 7
        MaterialOp field = op(MaterialOpKind::Field, 5);
        field.field = "tint";
        p.ops.push_back(field);                                        // 8
        MaterialOp pal = op(MaterialOpKind::Palette, 6, 4);
        pal.constant = {0.35f, 0.3f, 0.4f, 0.0f};
        pal.constant2 = {0.3f, 0.25f, 0.35f, 0.0f};
        pal.constant3 = {1.0f, 0.8f, 0.6f, 0.0f};
        pal.constant4 = {0.0f, 0.2f, 0.4f, 0.0f};
        pal.value = 0.05f;
        p.ops.push_back(pal);                                          // 9
        p.ops.push_back(op(MaterialOpKind::Multiply, 6, 6, 5));         // 10
        MaterialOp hue = op(MaterialOpKind::HueShift, 6, 6, 1);
        hue.value = 0.12f;
        p.ops.push_back(hue);                                          // 11
        MaterialOp sat = op(MaterialOpKind::Saturate, 6, 6);
        sat.value = 1.4f;
        p.ops.push_back(sat);                                          // 12
        MaterialOp fres = op(MaterialOpKind::Fresnel, 7);
        fres.value = 2.0f;
        p.ops.push_back(fres);                                         // 13
        MaterialOp remap = op(MaterialOpKind::Remap, 7, 7);
        remap.constant = {0.0f, 1.0f, 0.2f, 0.95f};
        remap.value = 1.0f;
        p.ops.push_back(remap);                                        // 14
        MaterialOp smooth = op(MaterialOpKind::Smoothstep, 3, 4);
        smooth.constant = {0.3f, 0.8f, 0.0f, 0.0f};
        p.ops.push_back(smooth);                                       // 15
        MaterialOp threshold = op(MaterialOpKind::Threshold, 5, 4);
        threshold.value = 0.5f;
        p.ops.push_back(threshold);                                    // 16
        REQUIRE(p.ops.size() == 16); // the pre-ADR-036 budget: still valid unchanged
        p.baseColorRegister = 6;
        p.metallicRegister = 5;
        p.roughnessRegister = 3;
        p.emissionRegister = 6;
        p.emissionIntensity = 2.5f;
        p.opacityRegister = 7;
        batch.push_back(p);
    }
    checkParity(harness, batch, fields, 1.25, 1e-3f);

    {
        // Four distinct fields plus a repeat of the first (ADR-050): the shader samples each
        // distinct field once, before the op loop, and every Field op reads its own by ordinal.
        // Get the ordinals wrong and a Field op returns some other field's value, so this fails
        // for any packing that does not carry them.
        MaterialProgram p;
        p.name = "multiField";
        const auto fieldOp = [](int dst, const char* field) {
            MaterialOp o = op(MaterialOpKind::Field, dst);
            o.field = field;
            return o;
        };
        p.ops.push_back(fieldOp(0, "grain"));
        p.ops.push_back(fieldOp(1, "swirl"));
        p.ops.push_back(fieldOp(2, "tint"));
        p.ops.push_back(fieldOp(3, "nosuchfield"));  // a fourth distinct name, unresolved
        p.ops.push_back(fieldOp(4, "tint"));         // the repeat shares an ordinal, not a new one
        p.ops.push_back(op(MaterialOpKind::Add, 5, 1, 2));
        CHECK(p.validate().has_value());
        p.baseColorRegister = 2;
        p.metallicRegister = 0;
        p.roughnessRegister = 4;
        p.emissionRegister = 5;   // the unclamped lane
        p.opacityRegister = 3;
        checkParity(harness, {p}, fields, 1.25, 1e-3f);
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("material programs shade procedural instances", "[material][gpu]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    if (auto init = renderer.init(); !init) {
        FAIL(init.error().message);
    }

    scene::Scene s = baseScene();
    s.camera.position = {0.0f, 0.0f, 12.0f};
    s.materialPrograms.push_back(heightRampProgram());
    scene::ProceduralGeometry wall = boxWall();
    wall.material.program = "heightRamp";
    s.procedurals.push_back(wall);

    const auto img = renderWith(renderer, s);
    CHECK(ctx->errorCount() == 0);

    // The wall spans y in [-3.45, 3.45] world; the camera is 12 away with a 60-degree fov, so the
    // boxes cover roughly the middle third of the image horizontally and most of it vertically.
    const std::uint8_t* bottom = img.pixel(80, 130);
    const std::uint8_t* middle = img.pixel(80, 80);
    const std::uint8_t* top = img.pixel(80, 30);
    INFO("bottom " << int(bottom[0]) << "," << int(bottom[1]) << "," << int(bottom[2]) << "  middle "
                   << int(middle[0]) << "," << int(middle[1]) << "," << int(middle[2]) << "  top " << int(top[0])
                   << "," << int(top[1]) << "," << int(top[2]));
    CHECK(bottom[0] > bottom[2]); // red end of the ramp low down
    CHECK(top[2] > top[0]);       // blue end up top
    CHECK(middle[1] > middle[0]); // green in the middle
    CHECK(middle[1] > middle[2]);

    // Without the program the same geometry is the flat grey the material asks for.
    scene::Scene plain = baseScene();
    plain.camera.position = {0.0f, 0.0f, 12.0f};
    plain.procedurals.push_back(boxWall());
    const auto plainImg = renderWith(renderer, plain);
    const std::uint8_t* grey = plainImg.pixel(80, 80);
    CHECK(std::abs(grey[0] - grey[1]) < 8);
    CHECK(std::abs(grey[1] - grey[2]) < 8);
    CHECK(gpu::hashImage(img) != gpu::hashImage(plainImg));
}

TEST_CASE("material program rendering is deterministic across fresh renderers", "[material][gpu]") {
    auto ctx = makeContext();
    scene::Scene s = baseScene();
    s.camera.position = {0.0f, 0.0f, 12.0f};
    s.materialPrograms.push_back(heightRampProgram());
    scene::ProceduralGeometry wall = boxWall();
    wall.material.unlit = false; // exercise the lit path, roughness from the noise op
    wall.material.program = "heightRamp";
    s.procedurals.push_back(wall);

    std::uint64_t hashes[2] = {0, 0};
    for (int i = 0; i < 2; ++i) {
        auto shaders = makeShaders(*ctx);
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        const auto img = renderWith(renderer, s);
        const auto again = renderWith(renderer, s);
        CHECK(gpu::hashImage(img) == gpu::hashImage(again));
        hashes[i] = gpu::hashImage(img);
    }
    CHECK(hashes[0] == hashes[1]);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("material programs shade entities and SDF surfaces", "[material][gpu]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    if (auto init = renderer.init(); !init) {
        FAIL(init.error().message);
    }

    // A program that paints everything a bright emissive cyan from a constant, so the object is
    // unmistakably lit by the program and not by the material's own (black) values.
    MaterialProgram glow;
    glow.name = "glow";
    glow.ops.push_back(constantOp(0, {0.0f, 0.8f, 0.9f, 1.0f}));
    glow.ops.push_back(constantOp(1, {0.0f, 0.0f, 0.0f, 1.0f}));
    glow.baseColorRegister = 1;
    glow.emissionRegister = 0;
    glow.emissionIntensity = 2.0f;

    SECTION("an entity with a program is not black") {
        scene::Scene s = baseScene();
        s.materialPrograms.push_back(glow);
        const scene::MeshId id = s.addMesh(scene::makeBox(glm::vec3(3.0f), 1));
        scene::Entity& e = s.addEntity("cube", id);
        e.material.baseColor = {0.0f, 0.0f, 0.0f};
        e.material.emissiveColor = {0.0f, 0.0f, 0.0f};
        e.material.emissiveIntensity = 0.0f;
        e.material.program = "glow";
        const auto img = renderWith(renderer, s);
        CHECK(ctx->errorCount() == 0);
        const std::uint8_t* centre = img.pixel(80, 80);
        INFO("entity centre " << int(centre[0]) << "," << int(centre[1]) << "," << int(centre[2]));
        CHECK(brighterThan(centre, 40));
        CHECK(centre[1] > centre[0]);
        CHECK(centre[2] > centre[0]);
    }

    SECTION("an SDF surface with a program is not black") {
        scene::Scene s = baseScene();
        s.materialPrograms.push_back(glow);
        scene::SdfObject o;
        o.name = "blob";
        spatial::SdfNode root;
        root.kind = spatial::SdfNodeKind::Sphere;
        root.radius = 2.0f;
        o.tree.root = std::move(root);
        o.boundsMin = glm::vec3(-2.5f);
        o.boundsMax = glm::vec3(2.5f);
        o.material.baseColor = {0.0f, 0.0f, 0.0f};
        o.material.emissiveIntensity = 0.0f;
        o.material.program = "glow";
        s.sdfs.push_back(o);
        const auto img = renderWith(renderer, s);
        CHECK(ctx->errorCount() == 0);
        CHECK(renderer.stats().sdf.raymarchObjects == 1);
        const std::uint8_t* centre = img.pixel(80, 80);
        INFO("sdf centre " << int(centre[0]) << "," << int(centre[1]) << "," << int(centre[2]));
        CHECK(brighterThan(centre, 40));
        CHECK(centre[1] > centre[0]);
        CHECK(centre[2] > centre[0]);
    }
}

// Hidden performance probe: `avgen_render_tests "[.perf][material]"` (Release). Reports GPU time
// per frame at 1080p for 100,000 instanced boxes with no program and with a 16-op program (the
// interpreter runs once per shaded fragment), i.e. the cost the material interpreter adds.
TEST_CASE("Material program throughput", "[.perf][material]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // 100k boxes on a 320 x 320 grid, seen from far enough that they cover the frame.
    scene::ProceduralGeometry g;
    g.name = "swarm";
    g.source.kind = scene::PrimitiveKind::Box;
    g.source.size = {0.4f, 0.4f, 0.4f};
    g.structureVersion = 1;
    g.meshHash = sourceHash(2);
    g.material.baseColor = {0.6f, 0.6f, 0.65f};
    g.material.roughness = 0.4f;
    constexpr int kSide = 320;
    g.instances.reserve(static_cast<std::size_t>(kSide) * kSide);
    for (int i = 0; i < kSide * kSide; ++i) {
        const float x = (static_cast<float>(i % kSide) - kSide * 0.5f) * 0.55f;
        const float z = (static_cast<float>(i / kSide) - kSide * 0.5f) * 0.55f;
        scene::InstanceRecord r{};
        r.position = {x, std::sin(x * 0.1f) * 2.0f, z, 1.0f};
        r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        r.scale = {1.0f, 1.0f, 1.0f, static_cast<float>(i) / static_cast<float>(kSide * kSide - 1)};
        r.random = {0.25f, 0.5f, 0.75f, 0.125f};
        r.color = {1.0f, 1.0f, 1.0f, static_cast<float>(i)};
        r.emissive = {1.0f, 1.0f, 1.0f, 0.0f};
        g.instances.push_back(r);
    }

    // A 16-op program: noise and voronoi over a drifting world position, a palette, colour ops and
    // a Fresnel rim - the heaviest shape a program can take.
    MaterialProgram heavy;
    heavy.name = "heavy";
    heavy.ops.push_back(inputOp(MaterialInput::WorldPosition, 0));
    heavy.ops.push_back(inputOp(MaterialInput::Time, 1));
    heavy.ops.push_back(constantOp(2, {0.0f, 0.0f, 0.2f, 0.0f}));
    heavy.ops.push_back(op(MaterialOpKind::Multiply, 2, 2, 1));
    heavy.ops.push_back(op(MaterialOpKind::Add, 2, 0, 2));
    {
        MaterialOp o = op(MaterialOpKind::Noise, 3, 2);
        o.value = 1.1f;
        o.seed = 6;
        heavy.ops.push_back(o);
    }
    {
        MaterialOp o = op(MaterialOpKind::Voronoi, 4, 2);
        o.value = 0.8f;
        o.seed = 2;
        heavy.ops.push_back(o);
    }
    {
        MaterialOp o = op(MaterialOpKind::Palette, 5, 3);
        o.constant = {0.4f, 0.35f, 0.5f, 0.0f};
        o.constant2 = {0.35f, 0.3f, 0.4f, 0.0f};
        o.constant3 = {1.0f, 0.9f, 0.7f, 0.0f};
        o.constant4 = {0.0f, 0.2f, 0.45f, 0.0f};
        o.value = 0.05f;
        heavy.ops.push_back(o);
    }
    {
        MaterialOp o = op(MaterialOpKind::HueShift, 5, 5, 4);
        o.value = 0.1f;
        heavy.ops.push_back(o);
    }
    {
        MaterialOp o = op(MaterialOpKind::Saturate, 5, 5);
        o.value = 1.3f;
        heavy.ops.push_back(o);
    }
    {
        MaterialOp o = op(MaterialOpKind::Smoothstep, 6, 4);
        o.constant = {0.2f, 0.9f, 0.0f, 0.0f};
        heavy.ops.push_back(o);
    }
    {
        MaterialOp o = op(MaterialOpKind::Remap, 6, 6);
        o.constant = {0.0f, 1.0f, 0.2f, 0.85f};
        heavy.ops.push_back(o);
    }
    {
        MaterialOp o = op(MaterialOpKind::Fresnel, 7);
        o.value = 3.0f;
        heavy.ops.push_back(o);
    }
    {
        MaterialOp o = op(MaterialOpKind::Ramp, 7, 7);
        o.constant = {0.0f, 0.0f, 0.0f, 1.0f};
        o.constant2 = {0.2f, 0.5f, 0.6f, 1.0f};
        o.constant3 = {0.9f, 1.0f, 1.0f, 1.0f};
        heavy.ops.push_back(o);
    }
    heavy.ops.push_back(op(MaterialOpKind::Multiply, 7, 7, 5));
    {
        MaterialOp o = op(MaterialOpKind::Mix, 5, 5, 7);
        o.value = 0.3f;
        heavy.ops.push_back(o);
    }
    REQUIRE(heavy.ops.size() == 16);
    heavy.baseColorRegister = 5;
    heavy.roughnessRegister = 6;
    heavy.metallicRegister = 3;
    heavy.emissionRegister = 7;
    heavy.emissionIntensity = 0.6f;
    REQUIRE(heavy.validate().has_value());

    // A cheap program for reference: the interpreter's own overhead, no noise and no colour ops.
    MaterialProgram light = heightRampProgram();
    light.name = "light";
    light.ops.pop_back(); // drop the noise op
    light.roughnessRegister = -1;
    REQUIRE(light.ops.size() == 3);
    REQUIRE(light.validate().has_value());

    // ADR-036: the same shape of program at the full op budget (48 then, 64 since ADR-1661), spent the
    // way a shipped material spends it -- the rest of the budget in the base plus four layers of five
    // ops, each masked and height-blended. This is the cost the ADR asked to be measured against the
    // 16-op path above.
    constexpr std::size_t kBaseOps = static_cast<std::size_t>(scene::kMaxMaterialOps) - 20;
    MaterialProgram layered;
    layered.name = "layeredFull";
    layered.ops = heavy.ops;
    while (layered.ops.size() < kBaseOps) {
        MaterialOp o = op(MaterialOpKind::Triplanar, 4, 0);
        o.value = 0.4f + 0.1f * static_cast<float>(layered.ops.size());
        o.seed = static_cast<std::uint32_t>(31 + layered.ops.size());
        o.constant2 = {4.0f, 0.0f, 0.0f, 0.0f};
        layered.ops.push_back(o);
        MaterialOp micro = op(MaterialOpKind::MicroDetail, 6, 0);
        micro.value = 40.0f + static_cast<float>(layered.ops.size());
        micro.seed = static_cast<std::uint32_t>(71 + layered.ops.size());
        layered.ops.push_back(micro);
    }
    layered.ops.resize(kBaseOps);
    layered.baseColorRegister = 5;
    layered.roughnessRegister = 6;
    layered.metallicRegister = 3;
    layered.emissionRegister = 7;
    layered.emissionIntensity = 0.6f;
    layered.heightRegister = 4;
    for (int li = 0; li < scene::kMaxMaterialLayers; ++li) {
        scene::MaterialLayer layer;
        layer.name = "layer" + std::to_string(li);
        MaterialOp wear = op(MaterialOpKind::EdgeWear, 1);
        wear.value = 2.0f + static_cast<float>(li);
        wear.constant = {1.5f, 0.2f * static_cast<float>(li), 0.0f, 0.0f};
        wear.constant2 = {0.5f, 0.1f, 0.8f, 0.0f};
        wear.seed = static_cast<std::uint32_t>(101 + li);
        layer.ops.push_back(wear);
        MaterialOp tint = op(MaterialOpKind::Triplanar, 2, 0);
        tint.value = 0.7f + 0.3f * static_cast<float>(li);
        tint.seed = static_cast<std::uint32_t>(151 + li);
        tint.constant2 = {4.0f, 0.0f, 0.0f, 0.0f};
        layer.ops.push_back(tint);
        MaterialOp ramp = op(MaterialOpKind::Ramp, 3, 2);
        ramp.constant = {0.2f, 0.1f, 0.08f, 1.0f};
        ramp.constant2 = {0.5f, 0.4f, 0.35f, 1.0f};
        ramp.constant3 = {0.8f, 0.75f, 0.7f, 1.0f};
        layer.ops.push_back(ramp);
        MaterialOp micro = op(MaterialOpKind::MicroDetail, 2, 0);
        micro.value = 90.0f + 10.0f * static_cast<float>(li);
        micro.seed = static_cast<std::uint32_t>(201 + li);
        layer.ops.push_back(micro);
        MaterialOp filter = op(MaterialOpKind::RoughnessFilter, 2, 2);
        filter.value = 1.0f;
        layer.ops.push_back(filter);
        layer.maskRegister = 1;
        layer.heightRegister = 2;
        layer.blendRange = 0.2f;
        layer.baseColorRegister = 3;
        layer.roughnessRegister = 2;
        layered.layers.push_back(std::move(layer));
    }
    REQUIRE(layered.totalOpCount() == scene::kMaxMaterialOps);
    REQUIRE(layered.validate().has_value());

    const MaterialProgram* configs[4] = {nullptr, &light, &heavy, &layered};
    const char* names[4] = {"no program", "3-op program (input, gradient, ramp)", "16-op program",
                            "64-op layered program (44-op base + 4 layers x 5)"};
    double base = -1.0;
    for (int cfg = 0; cfg < 4; ++cfg) {
        scene::Scene s = baseScene();
        s.camera.position = {0.0f, 40.0f, 120.0f};
        s.camera.target = {0.0f, 0.0f, 0.0f};
        s.procedurals.push_back(g);
        if (configs[cfg] != nullptr) {
            s.materialPrograms.push_back(*configs[cfg]);
            s.procedurals[0].material.program = configs[cfg]->name;
        }
        FixedStepClock clock(60.0);
        double gpuSum = 0.0;
        int counted = 0;
        for (int i = 0; i < 60; ++i) {
            auto img = renderer.renderToImage(s, clock.tick(), 1920, 1080);
            REQUIRE(img.has_value());
            if (i >= 20 && renderer.stats().gpuFrameMs >= 0.0) {
                gpuSum += renderer.stats().gpuFrameMs;
                ++counted;
            }
        }
        CHECK(ctx->errorCount() == 0);
        const double ms = counted != 0 ? gpuSum / counted : -1.0;
        if (cfg == 0) {
            base = ms;
        }
        WARN("material 1080p, " << g.instances.size() << " instances, " << names[cfg] << ": GPU " << ms << " ms/frame"
                                << (cfg == 0 ? std::string() : " (+" + std::to_string(ms - base) + " ms)"));
    }
}

// ADR-896: the bar lane of a material program's `beatPhase` input (its w) is the engine's bar phase,
// handed over in `ShaderFrameInputs::barPhase` -- not a count the renderer derived from the analysis
// frame, which took the first tracked beat as beat 1 of the count and put every bar on beat 4.
TEST_CASE("a material program's bar input is the engine's bar phase", "[material][gpu][meter]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    if (auto init = renderer.init(); !init) {
        FAIL(init.error().message);
    }
    // base colour = beatPhase.www: the bar phase painted straight onto an unlit wall.
    MaterialProgram program;
    program.name = "barPhase";
    program.ops.push_back(inputOp(MaterialInput::BeatPhase, 0));
    MaterialOp swizzle = op(MaterialOpKind::Swizzle, 1, 0);
    swizzle.constant = {3.0f, 3.0f, 3.0f, 3.0f};
    program.ops.push_back(swizzle);
    program.baseColorRegister = 1;
    scene::Scene s = baseScene();
    s.camera.position = {0.0f, 0.0f, 12.0f};
    s.materialPrograms.push_back(program);
    scene::ProceduralGeometry wall = boxWall();
    wall.material.program = "barPhase";
    s.procedurals.push_back(wall);

    // The analysis frame says "one beat has landed, at its start" -- under the old derivation
    // frac((beatCount + beatPhase) / 4) that is a bar phase of 0.25 whatever the engine thought.
    analysis::AnalysisFrame af;
    af.bandCount = 5;
    af.beatCount = 1;
    af.beatPhase = 0.0f;
    const auto grey = [&](float barPhase) {
        const rendering::ShaderFrameInputs inputs{nullptr, &af, barPhase};
        FrameTime t{};
        auto img = renderer.renderToImage(s, t, 160, 160, &inputs);
        REQUIRE(img.has_value());
        return static_cast<int>(img->pixel(80, 80)[1]);
    };
    const int early = grey(0.1f);
    const int late = grey(0.8f);
    const int quarter = grey(0.25f);
    INFO("bar phase 0.1 -> " << early << ", 0.25 -> " << quarter << ", 0.8 -> " << late);
    CHECK(ctx->errorCount() == 0);
    // The pixels follow the value passed in...
    CHECK(late > quarter + 40);
    CHECK(quarter > early + 10);
    // ...and not the frame's own beat count: with the old derivation all three were the 0.25 image.
    CHECK(late != quarter);
}

// ---- ADR-1290: sixteen program slots ------------------------------------------------------------------------------

TEST_CASE("the ninth to sixteenth material programs render; the seventeenth falls back to the material (ADR-1290)",
          "[material][gpu][adr1290]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    if (auto init = renderer.init(); !init) {
        FAIL(init.error().message);
    }

    // Seventeen programs, each a flat base colour: the first eight grey (the old table, filled), the ninth to
    // sixteenth red, and the seventeenth green. A wall drawn with program i must take its colour when i < 16.
    auto flat = [](const std::string& name, glm::vec3 c) {
        MaterialProgram p;
        p.name = name;
        MaterialOp k = op(MaterialOpKind::Constant, 0);
        k.constant = {c.x, c.y, c.z, 1.0f};
        p.ops.push_back(k);
        p.baseColorRegister = 0;
        return p;
    };
    scene::Scene base = baseScene();
    base.camera.position = {0.0f, 0.0f, 12.0f};
    for (int i = 0; i < 17; ++i) {
        const glm::vec3 c = i < 8 ? glm::vec3(0.5f) : (i < 16 ? glm::vec3(0.9f, 0.05f, 0.05f) : glm::vec3(0.05f, 0.9f, 0.05f));
        base.materialPrograms.push_back(flat("p" + std::to_string(i), c));
    }
    auto wallWith = [&](const std::string& program) {
        scene::Scene s = base;
        scene::ProceduralGeometry wall = boxWall();
        wall.material.program = program;
        s.procedurals.push_back(wall);
        return renderWith(renderer, s);
    };
    const auto ninth = wallWith("p8");
    const auto twelfth = wallWith("p11");
    const auto sixteenth = wallWith("p15");
    const auto seventeenth = wallWith("p16");
    CHECK(ctx->errorCount() == 0);
    auto rgb = [](const gpu::Image8& img) {
        const std::uint8_t* p = img.pixel(80, 80);
        return std::array<int, 3>{p[0], p[1], p[2]};
    };
    for (const auto* img : {&ninth, &twelfth, &sixteenth}) {
        const auto c = rgb(*img);
        INFO("program slot 9..16 pixel " << c[0] << "," << c[1] << "," << c[2]);
        CHECK(c[0] > c[1] + 30); // red: the program ran
        CHECK(c[0] > c[2] + 30);
    }
    // the seventeenth is not uploaded: the wall shades with its own (grey) material, not green
    const auto g = rgb(seventeenth);
    INFO("seventeenth pixel " << g[0] << "," << g[1] << "," << g[2]);
    CHECK(std::abs(g[1] - g[0]) < 12);
    CHECK(std::abs(g[1] - g[2]) < 12);
}

// ---- ADR-1642: texture inputs ------------------------------------------------------------------------------------

namespace {

// A smooth 32x32 image: neighbouring texels differ little, so the GPU's fixed-point bilinear weights stay within the
// parity tolerance. `normalMap`: a tangent-space normal map whose tilt turns across the tile.
scene::TextureData smoothImage(bool normalMap) {
    scene::TextureData t;
    t.name = normalMap ? "normal" : "colour";
    t.width = 32;
    t.height = 32;
    t.format = normalMap ? scene::TextureFormat::Rgba8Unorm : scene::TextureFormat::Rgba8Srgb;
    t.data.resize(32 * 32 * 4);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const float a = 6.2831853f * static_cast<float>(x) / 32.0f;
            const float b = 6.2831853f * static_cast<float>(y) / 32.0f;
            std::uint8_t* p = t.data.data() + (y * 32 + x) * 4;
            if (normalMap) {
                const glm::vec3 n = glm::normalize(glm::vec3(0.45f * std::sin(a), 0.45f * std::cos(b), 1.0f));
                for (int c = 0; c < 3; ++c) {
                    p[c] = static_cast<std::uint8_t>(std::lround((n[c] * 0.5f + 0.5f) * 255.0f));
                }
            } else {
                p[0] = static_cast<std::uint8_t>(std::lround(127.5f + 100.0f * std::sin(a)));
                p[1] = static_cast<std::uint8_t>(std::lround(127.5f + 100.0f * std::cos(b)));
                p[2] = static_cast<std::uint8_t>(std::lround(127.5f + 60.0f * std::sin(a + b)));
            }
            p[3] = 255;
        }
    }
    return t;
}

// The CPU reference's lookup: level 0, bilinear, repeating, sRGB decoded -- what the GPU reads at a zero footprint.
class ImageSampler : public scene::MaterialTextureSampler {
public:
    ImageSampler(const scene::TextureData& colour, const scene::TextureData& normal) : colour_(colour), normal_(normal) {}
    std::optional<glm::vec4> sample(const scene::MaterialTexture& t, const glm::vec2& uv) const override {
        const scene::TextureData& img = t.srgb ? colour_ : normal_;
        const auto texel = [&](int x, int y) {
            const int w = static_cast<int>(img.width);
            const int h = static_cast<int>(img.height);
            x = ((x % w) + w) % w;
            y = ((y % h) + h) % h;
            const std::uint8_t* p = img.data.data() + (y * w + x) * 4;
            glm::vec4 v;
            for (int c = 0; c < 4; ++c) {
                const float f = static_cast<float>(p[c]) / 255.0f;
                v[c] = t.srgb && c < 3 ? (f <= 0.04045f ? f / 12.92f : std::pow((f + 0.055f) / 1.055f, 2.4f)) : f;
            }
            return v;
        };
        const float fx = uv.x * static_cast<float>(img.width) - 0.5f;
        const float fy = uv.y * static_cast<float>(img.height) - 0.5f;
        const int x0 = static_cast<int>(std::floor(fx));
        const int y0 = static_cast<int>(std::floor(fy));
        const float tx = fx - static_cast<float>(x0);
        const float ty = fy - static_cast<float>(y0);
        return glm::mix(glm::mix(texel(x0, y0), texel(x0 + 1, y0), tx), glm::mix(texel(x0, y0 + 1), texel(x0 + 1, y0 + 1), tx),
                        ty);
    }

private:
    const scene::TextureData& colour_;
    const scene::TextureData& normal_;
};

} // namespace

TEST_CASE("texture ops match the CPU interpreter (ADR-1642)", "[material][gpu][adr1642]") {
    auto ctx = makeContext();
    MaterialHarness harness(*ctx);
    const scene::TextureData colour = smoothImage(false);
    const scene::TextureData normal = smoothImage(true);
    HarnessTextures textures;
    auto srgb = gpu::uploadTextureArray(*ctx, {&colour}, true, {255, 255, 255, 255}, "parity-srgb");
    auto linear = gpu::uploadTextureArray(*ctx, {&normal}, false, {128, 128, 255, 255}, "parity-linear");
    REQUIRE(srgb.has_value());
    REQUIRE(linear.has_value());
    textures.srgb = std::move(*srgb);
    textures.linear = std::move(*linear);
    textures.layerOf = [](const scene::MaterialTexture& t) { return t.name == "missing" ? -1 : 0; };

    MaterialProgram colourProgram;
    colourProgram.name = "colour";
    colourProgram.textures = {{"albedo", "albedo.png", true, {}}};
    MaterialOp albedo = op(MaterialOpKind::TextureTriplanar, 0);
    albedo.texture = "albedo";
    albedo.value = 0.37f;
    albedo.constant = {0.1f, 0.2f, 0.3f, 0.0f};
    albedo.constant2 = {6.0f, 0.0f, 0.0f, 0.0f};
    colourProgram.ops = {albedo};
    colourProgram.baseColorRegister = 0;
    colourProgram.emissionRegister = 0;
    colourProgram.roughnessRegister = 0;

    MaterialProgram normalProgram;
    normalProgram.name = "normal";
    normalProgram.textures = {{"bumps", "bumps.png", false, {}}};
    MaterialOp bumps = op(MaterialOpKind::TextureTriplanarNormal, 1);
    bumps.texture = "bumps";
    bumps.value = 0.5f;
    bumps.constant2 = {0.0f, -1.0f, 0.0f, 1.5f}; // default sharpness, a DirectX map, stronger
    normalProgram.ops = {bumps};
    normalProgram.normalRegister = 1;

    MaterialProgram missingProgram = colourProgram;
    missingProgram.name = "missing";
    missingProgram.textures[0].name = "missing";
    missingProgram.ops[0].texture = "missing";

    const std::vector<MaterialProgram> programs = {colourProgram, normalProgram, missingProgram};
    const ImageSampler sampler(colour, normal);
    std::vector<Case> cases;
    for (std::size_t p = 0; p < programs.size(); ++p) {
        for (scene::MaterialContext c : makeContexts()) {
            c.footprint = 0.0f; // level 0 on both sides
            // a skewed, unnormalised frame (as the cotangent frame is), so the inverse is exercised
            c.tangent = glm::normalize(glm::cross(c.normal, glm::vec3(0.3f, 0.1f, 1.0f))) * 1.3f;
            c.bitangent = glm::cross(c.normal, c.tangent) * 0.8f + c.tangent * 0.2f;
            c.textures = p == 2 ? nullptr : &sampler; // the missing texture: the CPU has no image either
            Case k;
            k.ctx = c;
            k.program = static_cast<int>(p);
            cases.push_back(k);
        }
    }
    const auto gpu = harness.run(programs, spatial::FieldSet{}, 0.0, cases, &textures);
    REQUIRE(gpu.size() == cases.size());
    float worst = 0.0f;
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const MaterialProgram& program = programs[static_cast<std::size_t>(cases[i].program)];
        REQUIRE(program.validate().has_value());
        const scene::MaterialResult cpu = scene::evaluateMaterialProgram(program, cases[i].ctx, harnessBase());
        const scene::MaterialResult& g = gpu[i];
        const float d = std::max({maxAbs(cpu.baseColor - g.baseColor), maxAbs(cpu.emission - g.emission),
                                  std::abs(cpu.roughness - g.roughness), maxAbs(cpu.normal - g.normal)});
        INFO("program '" << program.name << "' context " << (i % 10) << " differs by " << d);
        CHECK(d <= 4e-3f);
        worst = std::max(worst, d);
    }
    // the ops did something: the colour is not the missing texture's 1, and the normal leans
    CHECK(gpu[0].baseColor.x < 0.99f);
    CHECK(gpu[10].normal.z < 0.999f);
    CHECK(gpu[20].baseColor == glm::vec3(1.0f));
    WARN("texture-op parity: worst difference " << worst);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("a program's texture reaches an entity's pixels, and no texture leaves it white (ADR-1642)",
          "[material][gpu][adr1642]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    if (auto init = renderer.init(); !init) {
        FAIL(init.error().message);
    }
    // a red/blue checker, 8 texels a square, 64 texels a tile
    auto checker = std::make_shared<scene::TextureData>();
    checker->name = "checker";
    checker->width = 64;
    checker->height = 64;
    checker->format = scene::TextureFormat::Rgba8Srgb;
    checker->data.resize(64 * 64 * 4);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const bool red = ((x / 8) + (y / 8)) % 2 == 0;
            std::uint8_t* p = checker->data.data() + (y * 64 + x) * 4;
            p[0] = red ? 230 : 20;
            p[1] = 20;
            p[2] = red ? 20 : 230;
            p[3] = 255;
        }
    }
    MaterialProgram paint;
    paint.name = "paint";
    paint.textures = {{"checker", "checker.png", true, "/fixture/checker.png"}};
    MaterialOp sample = op(MaterialOpKind::TextureTriplanar, 0);
    sample.texture = "checker";
    sample.value = 0.5f; // a tile every 2 m: a 3 m box face shows a dozen squares across
    paint.ops = {sample};
    paint.emissionRegister = 0;
    paint.baseColorRegister = 0;

    scene::Scene s = baseScene();
    s.materialPrograms.push_back(paint);
    const scene::MeshId id = s.addMesh(scene::makeBox(glm::vec3(3.0f), 1));
    scene::Entity& e = s.addEntity("cube", id);
    e.material.program = "paint";
    e.transform.rotation = glm::angleAxis(0.5f, glm::normalize(glm::vec3(0.3f, 1.0f, 0.0f)));
    s.programTextures.push_back(
        scene::ProgramTexture{scene::materialTextureKey(paint.textures[0]), true, checker});
    s.programTextureVersion = 1;

    const auto count = [](const gpu::Image8& img) {
        int red = 0, blue = 0;
        for (std::uint32_t y = 40; y < 120; ++y) {
            for (std::uint32_t x = 40; x < 120; ++x) {
                const std::uint8_t* p = img.pixel(x, y);
                red += p[0] > p[2] + 60 ? 1 : 0;
                blue += p[2] > p[0] + 60 ? 1 : 0;
            }
        }
        return std::pair{red, blue};
    };
    const auto textured = renderWith(renderer, s);
    const auto [red, blue] = count(textured);
    INFO("red " << red << ", blue " << blue);
    CHECK(red > 400);
    CHECK(blue > 400);
    CHECK(ctx->errorCount() == 0);

    // the same scene without the image: the op reads colour 1, no checker
    s.programTextures.clear();
    s.programTextureVersion = 2;
    const auto plain = renderWith(renderer, s);
    const auto [red2, blue2] = count(plain);
    CHECK(red2 == 0);
    CHECK(blue2 == 0);
    // and back: the arrays are rebuilt when the table changes
    s.programTextures.push_back(
        scene::ProgramTexture{scene::materialTextureKey(paint.textures[0]), true, checker});
    s.programTextureVersion = 3;
    CHECK(gpu::hashImage(renderWith(renderer, s)) == gpu::hashImage(textured));
    CHECK(ctx->errorCount() == 0);
}

// Hidden ([.]): ADR-1642's cost in the entity path terrain draws through (honest timestamps, ADR-1600). A ground-like
// entity fills a 4K frame with a 12-op program and no texture op, drawn by a renderer whose material.wgsl is the
// baseline (AVGEN_MATERIAL_BASELINE: a directory holding the interpreter from before ADR-1642, with only the header lane
// added) and by the current one, interleaved; then the same program with a colour and a normal texture op. It WARNs
// the medians and asserts only that it measured.
TEST_CASE("texture ops: cost in the entity path (measure)", "[.][gpu][material][measure][adr1642]") {
    const char* baselinePath = std::getenv("AVGEN_MATERIAL_BASELINE");
    if (baselinePath == nullptr) {
        SKIP("set AVGEN_MATERIAL_BASELINE to a directory holding a baseline material.wgsl");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary baseShaders(*ctx, {std::filesystem::path(baselinePath),
                                          std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    gpu::ShaderLibrary nowShaders = makeShaders(*ctx);
    rendering::SceneRenderer before(*ctx, baseShaders);
    rendering::SceneRenderer after(*ctx, nowShaders);
    REQUIRE(before.init().has_value());
    REQUIRE(after.init().has_value());
    if (!after.timeline().available()) {
        SKIP("timestamp queries unavailable");
    }

    // a ground program shaped like the aurora valley's: noise, ramps, a slope mask from the normal, a mix
    MaterialProgram ground;
    ground.name = "ground";
    ground.ops.push_back(inputOp(MaterialInput::WorldPosition, 0));
    MaterialOp n1 = op(MaterialOpKind::Noise, 1, 0);
    n1.value = 0.4f;
    MaterialOp n2 = op(MaterialOpKind::Noise, 2, 0);
    n2.value = 2.3f;
    n2.seed = 7;
    ground.ops.push_back(n1);
    ground.ops.push_back(n2);
    MaterialOp mixed = op(MaterialOpKind::Mix, 3, 1, 2);
    mixed.value = 0.35f;
    ground.ops.push_back(mixed);
    MaterialOp ramp = op(MaterialOpKind::Ramp, 3, 3);
    ramp.constant = {0.02f, 0.05f, 0.02f, 1.0f};
    ramp.constant2 = {0.05f, 0.1f, 0.03f, 1.0f};
    ramp.constant3 = {0.08f, 0.07f, 0.05f, 1.0f};
    ground.ops.push_back(ramp);
    ground.ops.push_back(inputOp(MaterialInput::Normal, 4));
    MaterialOp slope = op(MaterialOpKind::Gradient, 4, 4);
    slope.constant = {0.0f, 1.0f, 0.0f, 0.0f};
    ground.ops.push_back(slope);
    MaterialOp edge = op(MaterialOpKind::Smoothstep, 4, 4);
    edge.constant = {0.7f, 0.9f, 0.0f, 0.0f};
    ground.ops.push_back(edge);
    ground.ops.push_back(constantOp(5, {0.06f, 0.05f, 0.045f, 1.0f}));
    ground.ops.push_back(op(MaterialOpKind::MixBy, 3, 5, 3, 4));
    MaterialOp rough = op(MaterialOpKind::Remap, 6, 2);
    rough.constant = {0.0f, 1.0f, 0.7f, 0.95f};
    ground.ops.push_back(rough);
    ground.ops.push_back(op(MaterialOpKind::Multiply, 3, 3, 6));
    ground.baseColorRegister = 3;
    ground.roughnessRegister = 6;
    REQUIRE(ground.totalOpCount() == 12);

    MaterialProgram textured = ground;
    textured.name = "textured";
    textured.textures = {{"albedo", "albedo.png", true, "/fixture/albedo.png"},
                         {"normal", "normal.png", false, "/fixture/normal.png"}};
    MaterialOp albedo = op(MaterialOpKind::TextureTriplanar, 7);
    albedo.texture = "albedo";
    albedo.value = 0.5f;
    MaterialOp bumps = op(MaterialOpKind::TextureTriplanarNormal, 1);
    bumps.texture = "normal";
    bumps.value = 0.5f;
    textured.ops.push_back(albedo);
    textured.ops.push_back(op(MaterialOpKind::Multiply, 3, 3, 7));
    textured.ops.push_back(bumps);
    textured.normalRegister = 1;

    // 1K images, as a CC0 set ships
    auto colour = std::make_shared<scene::TextureData>(smoothImage(false));
    auto normal = std::make_shared<scene::TextureData>(smoothImage(true));
    const auto sceneWith = [&](const MaterialProgram& program) {
        scene::Scene s = baseScene();
        s.materialPrograms.push_back(program);
        s.camera.position = {0.0f, 6.0f, 9.0f};
        s.camera.target = {0.0f, 0.0f, -6.0f};
        const scene::MeshId id = s.addMesh(scene::makeBox(glm::vec3(60.0f, 1.0f, 60.0f), 64));
        scene::Entity& e = s.addEntity("ground", id);
        e.material.program = program.name;
        s.programTextures.push_back(scene::ProgramTexture{scene::materialTextureKey(textured.textures[0]), true, colour});
        s.programTextures.push_back(scene::ProgramTexture{scene::materialTextureKey(textured.textures[1]), false, normal});
        s.programTextureVersion = 1;
        return s;
    };
    const scene::Scene plain = sceneWith(ground);
    const scene::Scene withTextures = sceneWith(textured);
    const auto measure = [](rendering::SceneRenderer& r, const scene::Scene& s) {
        std::vector<double> sceneMs;
        for (int i = 0; i < 100; ++i) {
            FrameTime t{};
            t.renderTime = i / 30.0;
            REQUIRE(r.renderFrame(s, t, 3840, 2160).has_value());
            if (i >= 20) {
                sceneMs.push_back(r.timeline().msFor("scene"));
            }
        }
        std::sort(sceneMs.begin(), sceneMs.end());
        return sceneMs[sceneMs.size() / 2];
    };
    static_cast<void>(measure(before, plain));
    static_cast<void>(measure(after, plain));
    static_cast<void>(measure(after, withTextures));
    std::vector<double> b, a, t;
    for (int rep = 0; rep < 6; ++rep) {
        b.push_back(measure(before, plain));
        a.push_back(measure(after, plain));
        t.push_back(measure(after, withTextures));
    }
    const auto list = [](const std::vector<double>& v) {
        std::string out;
        for (const double x : v) {
            out += fmt::format(" {:.3f}", x);
        }
        return out;
    };
    WARN("4K, a ground entity filling the frame, 12-op program, 6 interleaved reps of 80 frames, the scene label's "
         "median (ms): baseline interpreter"
         << list(b) << " | ADR-1642 interpreter, no texture op" << list(a) << " | + a colour and a normal texture op"
         << list(t));
    CHECK(a[0] > 0.0);
}
