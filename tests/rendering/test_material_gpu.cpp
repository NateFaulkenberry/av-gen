// Procedural materials on the GPU (ADR-030): CPU/GPU parity of the op interpreter in
// shaders/material.wgsl against scene::evaluateMaterialProgram through a compute harness (every
// MaterialInput, every MaterialOpKind, Field ops against a live FieldSet, disabled ops and
// "keep the material's value" outputs), then render tests for the three shading paths
// (procedural instances, entities, raymarched SDF surfaces) and determinism.
//
// The harness compares the five program outputs (base colour, metallic, roughness, emission,
// opacity). Emission is the unclamped lane: it carries a register's rgb through untouched, so a
// negative or above-one result still shows up as a difference.
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/field_uniforms.hpp"
#include "rendering/material_programs.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/material_program.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "spatial/field.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
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

// 12 vec4s per context (the last is padding); results are 3 vec4s per context. The base values are
// baked in so both sides start from harnessBase().
constexpr const char* kKernel = R"(
@group(0) @binding(0) var<uniform> fieldBlock: FieldBlock;
@group(0) @binding(1) var<uniform> materialPrograms: MaterialProgramBlock;
@group(0) @binding(2) var<storage, read> contexts: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read_write> results: array<vec4<f32>>;

const CTX_STRIDE: u32 = 12u;

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

    var base: MaterialResult;
    base.baseColor = vec3<f32>(0.8, 0.7, 0.6);
    base.metallic = 0.25;
    base.roughness = 0.35;
    base.emission = vec3<f32>(0.1, 0.2, 0.3);
    base.opacity = 0.9;

    let r = evaluateMaterialProgram(programIndex, ctx, base);
    results[i * 3u] = vec4<f32>(r.baseColor, r.metallic);
    results[i * 3u + 1u] = vec4<f32>(r.emission, r.roughness);
    results[i * 3u + 2u] = vec4<f32>(r.opacity, 0.0, 0.0, 0.0);
}
)";

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
        std::array<wgpu::BindGroupLayoutEntry, 5> entries{};
        for (std::uint32_t i = 0; i < 4; ++i) {
            entries[i].binding = i;
            entries[i].visibility = wgpu::ShaderStage::Compute;
        }
        entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[1].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[2].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[3].buffer.type = wgpu::BufferBindingType::Storage;
        entries[4].binding = 15;
        entries[4].visibility = wgpu::ShaderStage::Compute;
        entries[4].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
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
        pipeline_ = device.CreateComputePipeline(&cdesc);
        REQUIRE(pipeline_ != nullptr);
    }

    // Uploads `programs` (at most 8) and evaluates every case; returns one result per case.
    std::vector<scene::MaterialResult> run(const std::vector<MaterialProgram>& programs,
                                           const spatial::FieldSet& fields, double time,
                                           const std::vector<Case>& cases) {
        rendering::FieldUniforms fieldBlock(ctx_);
        fieldBlock.update(fields, time);
        rendering::MaterialPrograms block(ctx_);
        block.update(programs, &fieldBlock);

        std::vector<glm::vec4> packed(cases.size() * 12, glm::vec4(0.0f));
        for (std::size_t i = 0; i < cases.size(); ++i) {
            const scene::MaterialContext& c = cases[i].ctx;
            glm::vec4* p = packed.data() + i * 12;
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
        rdesc.size = cases.size() * 3 * sizeof(glm::vec4);
        rdesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc;
        wgpu::Buffer results = device.CreateBuffer(&rdesc);

        std::array<wgpu::BindGroupEntry, 5> entries{};
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
        std::vector<glm::vec4> raw(cases.size() * 3);
        std::memcpy(raw.data(), bytes->data(), rdesc.size);
        std::vector<scene::MaterialResult> out(cases.size());
        for (std::size_t i = 0; i < cases.size(); ++i) {
            out[i].baseColor = glm::vec3(raw[i * 3]);
            out[i].metallic = raw[i * 3].w;
            out[i].emission = glm::vec3(raw[i * 3 + 1]);
            out[i].roughness = raw[i * 3 + 1].w;
            out[i].opacity = raw[i * 3 + 2].x;
        }
        return out;
    }

private:
    gpu::Context& ctx_;
    gpu::ShaderLibrary shaders_;
    wgpu::BindGroupLayout layout_;
    wgpu::ComputePipeline pipeline_;
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
                                      std::abs(cpu.opacity - g.opacity)});
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
        REQUIRE(p.ops.size() == static_cast<std::size_t>(scene::kMaxMaterialOps));
        p.baseColorRegister = 6;
        p.metallicRegister = 5;
        p.roughnessRegister = 3;
        p.emissionRegister = 6;
        p.emissionIntensity = 2.5f;
        p.opacityRegister = 7;
        batch.push_back(p);
    }
    checkParity(harness, batch, fields, 1.25, 1e-3f);
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
    REQUIRE(heavy.ops.size() == static_cast<std::size_t>(scene::kMaxMaterialOps));
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

    const MaterialProgram* configs[3] = {nullptr, &light, &heavy};
    const char* names[3] = {"no program", "3-op program (input, gradient, ramp)", "16-op program"};
    double base = -1.0;
    for (int cfg = 0; cfg < 3; ++cfg) {
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
