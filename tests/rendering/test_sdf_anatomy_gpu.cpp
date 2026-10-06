// ADR-1144 (the anatomical SDF vocabulary, compiled trees only) and ADR-1145 (a compiled latent): the GPU
// half. Every kind's compiled code (spatial::sdfCompileWgsl over sdf_program.wgsl's helpers) against the CPU
// tree (SdfTree::evaluate), alone and as the authored Astral Forge face; the far field's skipped child; the
// compiled latent force (a compiled variant of cs_latent) against its CPU reference, step for step, and
// pulling matter onto an ellipsoid the interpreter could not draw. The CPU half is
// tests/unit/test_sdf_anatomy.cpp.

#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/field_uniforms.hpp"
#include "rendering/particle_renderer.hpp"
#include "scene/particle_latent.hpp"
#include "scene/scene.hpp"
#include "spatial/sdf.hpp"

#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
using spatial::SdfNode;
using spatial::SdfNodeKind;
using spatial::SdfTree;

namespace {

// $AVGEN_SHADER_DIR first (a snapshot of the shaders a run was built with), then the source tree.
std::vector<std::filesystem::path> shaderDirs() {
    std::vector<std::filesystem::path> dirs;
    if (const char* env = std::getenv("AVGEN_SHADER_DIR")) {
        dirs.emplace_back(env);
    }
    dirs.emplace_back(AVGEN_SHADER_SOURCE_DIR);
    return dirs;
}

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

constexpr const char* kKernel = R"(
@group(0) @binding(0) var<storage, read> sdfNodes: array<SdfNodeGpu>;
@group(0) @binding(1) var<uniform> fieldBlock: FieldBlock;
@group(0) @binding(2) var<storage, read> samples: array<vec4<f32>>;
@group(0) @binding(4) var<storage, read_write> results: array<vec4<f32>>;
@group(0) @binding(5) var<uniform> harnessTime: vec4<f32>;

@compute @workgroup_size(64)
fn cs_sdf(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= arrayLength(&samples)) { return; }
    let identity = mat4x4<f32>(vec4<f32>(1.0, 0.0, 0.0, 0.0), vec4<f32>(0.0, 1.0, 0.0, 0.0),
                               vec4<f32>(0.0, 0.0, 1.0, 0.0), vec4<f32>(0.0, 0.0, 0.0, 1.0));
    let count = arrayLength(&sdfNodes);
    results[i] = vec4<f32>(sdfField(0u, count, samples[i].xyz, harnessTime.x, identity), 0.0, 0.0, 0.0);
}
)";

// One compiled tree per module (sdfField is the tree): its distances at `points`.
std::vector<float> compiledDistances(gpu::Context& ctx, const SdfTree& tree, double time, const std::vector<glm::vec3>& points) {
    gpu::ShaderLibrary shaders(ctx, shaderDirs());
    auto source = shaders.loadSource("sdf.wgsl");
    REQUIRE(source.has_value());
    std::vector<spatial::SdfNodeGpu> table;
    const std::string field = spatial::sdfCompileWgsl(tree, table);
    auto module = shaders.compile(*source + field + kKernel, "sdf-anatomy-harness");
    if (!module) {
        FAIL(module.error().message);
    }
    const auto& device = ctx.device();
    std::array<wgpu::BindGroupLayoutEntry, 6> entries{};
    const std::array<std::uint32_t, 6> bindings = {0, 1, 2, 4, 5, 15};
    for (std::size_t b = 0; b < entries.size(); ++b) {
        entries[b].binding = bindings[b];
        entries[b].visibility = wgpu::ShaderStage::Compute;
    }
    entries[0].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    entries[1].buffer.type = wgpu::BufferBindingType::Uniform;
    entries[2].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    entries[3].buffer.type = wgpu::BufferBindingType::Storage;
    entries[4].buffer.type = wgpu::BufferBindingType::Uniform;
    entries[5].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    wgpu::BindGroupLayoutDescriptor ldesc{};
    ldesc.entryCount = entries.size();
    ldesc.entries = entries.data();
    wgpu::BindGroupLayout layout = device.CreateBindGroupLayout(&ldesc);
    wgpu::PipelineLayoutDescriptor pdesc{};
    pdesc.bindGroupLayoutCount = 1;
    pdesc.bindGroupLayouts = &layout;
    wgpu::ComputePipelineDescriptor cdesc{};
    cdesc.layout = device.CreatePipelineLayout(&pdesc);
    cdesc.compute.module = *module;
    cdesc.compute.entryPoint = "cs_sdf";
    wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&cdesc);
    REQUIRE(pipeline != nullptr);

    spatial::FieldSet fields;
    rendering::FieldUniforms block(ctx);
    block.update(fields, time);
    auto storage = [&](const void* data, std::uint64_t size, wgpu::BufferUsage extra) {
        wgpu::BufferDescriptor desc{};
        desc.size = std::max<std::uint64_t>(size, 16);
        desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst | extra;
        wgpu::Buffer b = device.CreateBuffer(&desc);
        if (size > 0 && data != nullptr) {
            ctx.queue().WriteBuffer(b, 0, data, size);
        }
        return b;
    };
    std::vector<glm::vec4> positions;
    for (const auto& p : points) {
        positions.emplace_back(p, 0.0f);
    }
    wgpu::Buffer nodeBuf = storage(table.data(), table.size() * sizeof(spatial::SdfNodeGpu), wgpu::BufferUsage::None);
    wgpu::Buffer sampleBuf = storage(positions.data(), positions.size() * sizeof(glm::vec4), wgpu::BufferUsage::None);
    const std::uint64_t resultBytes = positions.size() * sizeof(glm::vec4);
    wgpu::Buffer resultBuf = storage(nullptr, resultBytes, wgpu::BufferUsage::CopySrc);
    wgpu::Buffer timeBuf;
    {
        wgpu::BufferDescriptor desc{};
        desc.size = 16;
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        timeBuf = device.CreateBuffer(&desc);
        const glm::vec4 t(static_cast<float>(time), 0.0f, 0.0f, 0.0f);
        ctx.queue().WriteBuffer(timeBuf, 0, &t, sizeof(t));
    }
    std::array<wgpu::BindGroupEntry, 6> groupEntries{};
    groupEntries[0].binding = 0;
    groupEntries[0].buffer = nodeBuf;
    groupEntries[1].binding = 1;
    groupEntries[1].buffer = block.buffer();
    groupEntries[1].size = rendering::FieldUniforms::kBufferSize;
    groupEntries[2].binding = 2;
    groupEntries[2].buffer = sampleBuf;
    groupEntries[3].binding = 4;
    groupEntries[3].buffer = resultBuf;
    groupEntries[4].binding = 5;
    groupEntries[4].buffer = timeBuf;
    groupEntries[5].binding = 15;
    groupEntries[5].buffer = block.gridBuffer();
    groupEntries[5].size = rendering::FieldUniforms::kGridBufferSize;
    wgpu::BindGroupDescriptor gdesc{};
    gdesc.layout = layout;
    gdesc.entryCount = groupEntries.size();
    gdesc.entries = groupEntries.data();
    wgpu::BindGroup group = device.CreateBindGroup(&gdesc);
    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
    pass.SetPipeline(pipeline);
    pass.SetBindGroup(0, group);
    pass.DispatchWorkgroups(static_cast<std::uint32_t>((positions.size() + 63) / 64));
    pass.End();
    wgpu::CommandBuffer commands = encoder.Finish();
    ctx.queue().Submit(1, &commands);
    auto bytes = gpu::readBuffer(ctx, resultBuf, 0, resultBytes);
    REQUIRE(bytes.has_value());
    std::vector<glm::vec4> raw(positions.size());
    std::memcpy(raw.data(), bytes->data(), resultBytes);
    std::vector<float> out;
    for (const auto& r : raw) {
        out.push_back(r.x);
    }
    return out;
}

std::vector<glm::vec3> grid(float half, int n) {
    std::vector<glm::vec3> out;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            for (int k = 0; k < n; ++k) {
                const auto at = [&](int c) { return -half + 2.0f * half * (static_cast<float>(c) + 0.37f) / static_cast<float>(n); };
                out.emplace_back(at(i), at(j) * 1.4f, at(k) * 0.8f);
            }
        }
    }
    return out;
}

SdfNode make(SdfNodeKind kind) {
    SdfNode n;
    n.kind = kind;
    return n;
}

SdfTree treeOf(SdfNode root) {
    SdfTree t;
    t.root = std::move(root);
    return t;
}

SdfTree authoredFace() {
    std::ifstream in(std::string(AVGEN_SOURCE_DIR) + "/examples/astral-forge/compare-t01-face.scene.json");
    REQUIRE(in.good());
    const nlohmann::json scene = nlohmann::json::parse(in);
    for (const auto& n : scene.at("nodes")) {
        if (n.at("name") == "latent") {
            auto tree = SdfTree::fromJson(n.at("sdf").at("tree"));
            REQUIRE(tree);
            return *tree;
        }
    }
    FAIL("the T01 scene has no latent");
    return {};
}

struct Case {
    std::string name;
    SdfTree tree;
    float tolerance;
};

} // namespace

TEST_CASE("anatomy: every compiled kind matches the CPU tree", "[gpu][sdf][anatomy]") {
    auto ctx = makeContext();
    std::vector<Case> cases;
    {
        SdfNode e = make(SdfNodeKind::Ellipsoid);
        e.size = glm::vec3(1.95f, 3.35f, 2.0f) * 0.4f;
        cases.push_back({"ellipsoid", treeOf(e), 1e-4f});
        SdfNode tc = make(SdfNodeKind::TaperedCapsule);
        tc.from = glm::vec3(0.0f, 1.38f, 0.78f) * 0.5f;
        tc.to = glm::vec3(-1.75f, 1.02f, 0.28f) * 0.5f;
        tc.radius = 0.17f;
        tc.radius2 = 0.07f;
        cases.push_back({"tapered capsule", treeOf(tc), 1e-4f});
        SdfNode o = make(SdfNodeKind::Octahedron);
        o.radius = 0.8f;
        cases.push_back({"octahedron", treeOf(o), 1e-4f});
        SdfNode f = make(SdfNodeKind::Facet);
        f.size = glm::vec3(0.9f, 1.3f, 0.8f);
        f.count = 24;
        f.offset = 0.93f;
        f.speed = 0.05f;
        f.amount = 0.3f;
        cases.push_back({"facet", treeOf(f), 1e-4f});
        SdfNode blend = make(SdfNodeKind::Blend);
        blend.children = {e, f};
        blend.amount = 0.85f;
        blend.axis = glm::vec3(1.0f, 0.0f, 0.0f);
        blend.offset = 0.05f;
        blend.smooth = 0.3f;
        cases.push_back({"blend", treeOf(blend), 1e-4f});
        SdfNode fray = make(SdfNodeKind::Fray);
        fray.children = {e};
        fray.amount = 0.22f;
        fray.frequency = 1.7f;
        fray.speed = 0.1f;
        fray.seed = 61;
        fray.radius = 0.3f;
        fray.offset = 1.2f;
        fray.size = glm::vec3(1.0f, 0.75f, 0.0f);
        cases.push_back({"fray", treeOf(fray), 1e-3f});
        SdfNode inner = make(SdfNodeKind::Translate);
        inner.translation = glm::vec3(0.1f, -0.4f, 0.2f);
        SdfNode slit = make(SdfNodeKind::Ellipsoid);
        slit.size = glm::vec3(0.95f, 0.13f, 0.85f);
        inner.children = {slit};
        SdfNode far = make(SdfNodeKind::FarField);
        far.translation = inner.translation;
        far.size = glm::vec3(1.0f, 1.6f, 1.0f);
        far.radius = 1.0f;
        far.offset = 0.6f;
        far.children = {inner};
        cases.push_back({"far field", treeOf(far), 1e-4f});
    }
    cases.push_back({"the authored face", authoredFace(), 1e-3f});
    const auto points = grid(2.2f, 14);
    for (const Case& c : cases) {
        REQUIRE(c.tree.validate(spatial::SdfEvaluator::Compiled));
        for (const double time : {0.0, 7.25}) {
            const auto gpu = compiledDistances(*ctx, c.tree, time, points);
            float worst = 0.0f;
            for (std::size_t i = 0; i < points.size(); ++i) {
                const float cpu = c.tree.evaluate(points[i], time);
                // a relative margin for the far field's large distances
                worst = std::max(worst, std::abs(cpu - gpu[i]) / (1.0f + 0.1f * std::abs(cpu)));
            }
            INFO(c.name << " at t=" << time << ": worst deviation " << worst << " over " << points.size() << " points");
            CHECK(worst <= c.tolerance);
        }
    }
    CHECK(ctx->errorCount() == 0);
}

// ---- ADR-1145: the compiled latent force -------------------------------------------------------------

namespace {

constexpr double kDt = 1.0 / 60.0;

FrameTime frameAt(std::uint64_t i) {
    FrameTime t{};
    t.renderTime = static_cast<double>(i) * kDt;
    t.deltaTime = kDt;
    t.frameIndex = i;
    return t;
}

// An invisible, compiled ellipsoid (a kind the interpreter does not have): the latent.
scene::SdfObject maskEllipsoid() {
    scene::SdfObject o;
    o.name = "mask";
    o.visible = false;
    o.compile = true;
    SdfNode n = make(SdfNodeKind::Ellipsoid);
    n.size = glm::vec3(1.2f, 0.7f, 0.9f);
    o.tree.root = n;
    o.transform.position = glm::vec3(0.2f, -0.1f, 0.0f);
    o.boundsMin = glm::vec3(-2.0f);
    o.boundsMax = glm::vec3(2.0f);
    return o;
}

scene::Scene ellipsoidScene(float coherence) {
    scene::Scene s;
    s.sdfs.push_back(maskEllipsoid());
    scene::ParticleSystem p;
    p.name = "matter";
    p.capacity = 4096;
    p.seed = 3;
    p.shape = scene::EmitterShape::Box;
    p.position = glm::vec3(0.2f, -0.1f, 0.0f);
    p.extent = glm::vec3(1.6f);
    p.spawnRate = 0.0f;
    p.burst = 4096.0f;
    p.lifetimeMin = p.lifetimeMax = 1000.0f;
    p.speedMin = p.speedMax = 0.0f;
    p.gravity = glm::vec3(0.0f);
    p.drag = 0.0f;
    p.turbulence = 0.0f;
    p.sizeStart = p.sizeEnd = 0.0f;
    p.latent.sdf = "mask";
    p.latent.coherence = coherence;
    p.latent.release = 12.0f;
    s.particles.push_back(p);
    return s;
}

struct Stepper {
    gpu::Context& ctx;
    gpu::ShaderLibrary shaders;
    rendering::ParticleRenderer particles;
    std::uint64_t frame = 0;
    explicit Stepper(gpu::Context& c)
        : ctx(c), shaders(c, shaderDirs()), particles(c, shaders) {
        REQUIRE(particles.init().has_value());
    }
    void step(scene::Scene& s, int frames) {
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 8.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
        for (int i = 0; i < frames; ++i) {
            wgpu::CommandEncoder encoder = ctx.device().CreateCommandEncoder();
            particles.update(encoder, s, frameAt(frame++), view, proj);
            wgpu::CommandBuffer commands = encoder.Finish();
            ctx.queue().Submit(1, &commands);
            for (auto& p : s.particles) {
                p.burst = 0.0f;
            }
        }
    }
};

} // namespace

TEST_CASE("anatomy: a compiled latent binds matter to a kind the interpreter cannot run",
          "[gpu][particles][latent][anatomy]") {
    auto ctx = makeContext();
    scene::Scene bound = ellipsoidScene(1.0f);
    REQUIRE_FALSE(bound.sdfs[0].tree.validate(spatial::SdfEvaluator::Interpreter));
    Stepper a(*ctx);
    a.step(bound, 180);
    CHECK(a.particles.stats().latentSystems == 1);
    auto all = a.particles.readParticles(0);
    REQUIRE(all.has_value());
    const SdfTree& tree = bound.sdfs[0].tree;
    const glm::vec3 centre = bound.sdfs[0].transform.position;
    std::size_t on = 0;
    std::size_t alive = 0;
    for (const auto& p : *all) {
        if (p.life <= 0.0f) {
            continue;
        }
        ++alive;
        on += std::abs(tree.evaluate(p.position - centre, 0.0)) < 0.02f ? 1 : 0;
    }
    REQUIRE(alive == 4096);
    const double share = static_cast<double>(on) / static_cast<double>(alive);
    INFO("share within 0.02 of the ellipsoid " << share);
    CHECK(share > 0.97);

    // The control: coherence 0 binds nothing, so nothing is on the surface beyond chance.
    scene::Scene free = ellipsoidScene(0.0f);
    Stepper b(*ctx);
    b.step(free, 180);
    auto loose = b.particles.readParticles(0);
    REQUIRE(loose.has_value());
    std::size_t onFree = 0;
    for (const auto& p : *loose) {
        onFree += p.life > 0.0f && std::abs(tree.evaluate(p.position - centre, 0.0)) < 0.02f ? 1 : 0;
    }
    CHECK(static_cast<double>(onFree) / 4096.0 < 0.1);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("anatomy: one compiled latent step matches the CPU reference, binding and release alike",
          "[gpu][particles][latent][anatomy]") {
    auto ctx = makeContext();
    scene::Scene s = ellipsoidScene(0.6f);
    Stepper st(*ctx);
    st.step(s, 20);
    const auto before = st.particles.readParticles(0);
    REQUIRE(before.has_value());
    const auto compare = [&](float coherence, float previous, const std::vector<rendering::ParticleSnapshot>& from) {
        s.particles[0].latent.coherence = coherence;
        st.step(s, 1);
        const auto after = st.particles.readParticles(0);
        REQUIRE(after.has_value());
        scene::LatentStep step;
        step.model = s.sdfs[0].transform.matrix();
        step.inverse = glm::inverse(step.model);
        step.normal = glm::transpose(step.inverse);
        step.coherence = coherence;
        step.prevCoherence = previous;
        step.width = s.particles[0].latent.width;
        step.strength = s.particles[0].latent.strength;
        step.release = s.particles[0].latent.release;
        step.epsilon = scene::latentGradientEpsilon(s.sdfs[0].boundsMin, s.sdfs[0].boundsMax);
        step.time = static_cast<double>(st.frame - 1) * kDt;
        step.dt = static_cast<float>(kDt);
        double worst = 0.0;
        std::size_t moved = 0;
        for (std::size_t i = 0; i < from.size(); ++i) {
            if (from[i].life <= 0.0f) {
                continue;
            }
            const glm::vec3 expected =
                scene::latentVelocityStep(step, s.sdfs[0].tree, from[i].position, from[i].velocity, from[i].seed);
            const glm::vec3 got = (*after)[i].velocity;
            worst = std::max(worst, static_cast<double>(glm::length(got - expected) / (1.0f + glm::length(expected))));
            moved += glm::length(expected - from[i].velocity) > 1e-3f ? 1 : 0;
        }
        INFO("coherence " << previous << " -> " << coherence << ": worst relative error " << worst << ", " << moved
                          << " moved");
        CHECK(worst < 1e-3);
        CHECK(moved > 500);
        return *after;
    };
    const auto held = compare(0.6f, 0.6f, *before);
    const auto released = compare(0.2f, 0.6f, held);
    double fastest = 0.0;
    for (const auto& p : released) {
        fastest = std::max(fastest, static_cast<double>(glm::length(p.velocity)));
    }
    CHECK(fastest > 3.0);
    CHECK(ctx->errorCount() == 0);
}
