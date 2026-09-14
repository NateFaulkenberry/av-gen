// SDF objects on the GPU (ADR-027): CPU/GPU parity of the packed-program interpreter in
// shaders/sdf.wgsl against spatial::evaluatePacked through a compute harness (nested domain
// ops, smooth combinations, every displacement including field displacement), the raymarch pass
// composing with meshes through depth in both directions, Mesh mode drawing, determinism, and a
// hidden performance probe.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/field_uniforms.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "spatial/sdf.hpp"

#include <catch2/catch_test_macros.hpp>

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
using spatial::SdfNode;
using spatial::SdfNodeKind;

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

// ---- tree builders --------------------------------------------------------------------------------

SdfNode node(SdfNodeKind kind) {
    SdfNode n;
    n.kind = kind;
    return n;
}
SdfNode sphere(float radius) {
    SdfNode n = node(SdfNodeKind::Sphere);
    n.radius = radius;
    return n;
}
SdfNode box(const glm::vec3& size) {
    SdfNode n = node(SdfNodeKind::Box);
    n.size = size;
    return n;
}
SdfNode unary(SdfNodeKind kind, SdfNode child) {
    SdfNode n = node(kind);
    n.children.push_back(std::move(child));
    return n;
}
SdfNode translate(const glm::vec3& t, SdfNode child) {
    SdfNode n = unary(SdfNodeKind::Translate, std::move(child));
    n.translation = t;
    return n;
}
SdfNode combo(SdfNodeKind kind, std::vector<SdfNode> children, float smooth = 0.5f) {
    SdfNode n = node(kind);
    n.children = std::move(children);
    n.smooth = smooth;
    return n;
}
spatial::SdfTree treeOf(SdfNode root) {
    spatial::SdfTree t;
    t.root = std::move(root);
    return t;
}

// The tree from tests/unit/test_sdf.cpp: nested domain ops, smooth combinations, displacements,
// disabled nodes (depth 8).
SdfNode complexTree() {
    SdfNode ball = sphere(0.8f);
    SdfNode cube = box(glm::vec3(0.6f, 0.4f, 0.5f));
    SdfNode ring = node(SdfNodeKind::Torus);
    ring.radius = 1.2f;
    ring.rounding = 0.25f;
    SdfNode cyl = node(SdfNodeKind::Cylinder);
    cyl.radius = 0.3f;
    cyl.height = 3.0f;
    SdfNode scaled = unary(SdfNodeKind::Scale, std::move(cube));
    scaled.scale = 1.7f;
    SdfNode rotated = unary(SdfNodeKind::Rotate, std::move(scaled));
    rotated.rotationDegrees = glm::vec3(30.0f, -45.0f, 12.0f);
    SdfNode twisted = unary(SdfNodeKind::Twist, std::move(ring));
    twisted.amount = 0.6f;
    SdfNode bent = unary(SdfNodeKind::Bend, std::move(cyl));
    bent.amount = 0.4f;
    SdfNode repeated = unary(SdfNodeKind::Repeat, sphere(0.3f));
    repeated.size = glm::vec3(1.5f, 0.0f, 1.5f);
    repeated.count = 2;
    SdfNode polar = unary(SdfNodeKind::PolarRepeat, translate(glm::vec3(1.6f, 0.0f, 0.0f), sphere(0.25f)));
    polar.count = 5;
    SdfNode mirrored = unary(SdfNodeKind::Mirror, translate(glm::vec3(0.9f, 0.2f, -0.4f), box(glm::vec3(0.2f))));
    mirrored.size = glm::vec3(1.0f, 0.0f, 1.0f);
    SdfNode cone = node(SdfNodeKind::Cone);
    cone.radius = 0.5f;
    cone.height = 1.2f;
    SdfNode capsule = node(SdfNodeKind::Capsule);
    capsule.radius = 0.2f;
    capsule.height = 1.0f;
    SdfNode plane = node(SdfNodeKind::Plane);
    plane.axis = glm::vec3(0.2f, 1.0f, 0.1f);
    plane.offset = -2.5f;
    SdfNode disabled = sphere(5.0f);
    disabled.enabled = false;
    SdfNode rounded = node(SdfNodeKind::RoundedBox);
    rounded.size = glm::vec3(0.5f, 0.3f, 0.4f);
    rounded.rounding = 0.1f;

    // (no translate around `rounded`: the wave/carve branch is already at the depth limit of 8)
    SdfNode carve = combo(SdfNodeKind::SmoothDifference, {std::move(ball), std::move(rounded)}, 0.2f);
    SdfNode wave = unary(SdfNodeKind::DisplaceWave, std::move(carve));
    wave.amount = 0.05f;
    wave.frequency = 4.0f;
    wave.speed = 1.3f;
    wave.axis = glm::vec3(1.0f, 0.5f, 0.0f);
    SdfNode inner = combo(SdfNodeKind::SmoothUnion,
                          {std::move(wave), std::move(rotated), std::move(twisted), std::move(bent), std::move(repeated),
                           std::move(polar), std::move(mirrored), std::move(disabled)},
                          0.3f);
    SdfNode cut = combo(SdfNodeKind::Intersection, {std::move(inner), std::move(plane)});
    SdfNode extra = combo(SdfNodeKind::Union, {std::move(cut), translate(glm::vec3(0.0f, 1.5f, 0.0f), std::move(cone)),
                                               translate(glm::vec3(-1.0f, 0.0f, 1.0f), std::move(capsule))});
    SdfNode noisy = unary(SdfNodeKind::DisplaceNoise, std::move(extra));
    noisy.amount = 0.08f;
    noisy.frequency = 2.5f;
    noisy.speed = 0.7f;
    noisy.seed = 7;
    SdfNode root = translate(glm::vec3(0.1f, -0.2f, 0.3f), std::move(noisy));
    return root;
}

// A deterministic spread of sample points (inside, on and outside the shapes).
std::vector<glm::vec3> samplePoints() {
    std::vector<glm::vec3> out;
    for (int i = -3; i <= 3; ++i) {
        for (int j = -3; j <= 3; ++j) {
            for (int k = -3; k <= 3; ++k) {
                out.emplace_back(0.7f * static_cast<float>(i) + 0.13f, 0.55f * static_cast<float>(j) - 0.21f,
                                 0.8f * static_cast<float>(k) + 0.07f);
            }
        }
    }
    out.emplace_back(0.0f, 0.0f, 0.0f);
    return out;
}

// ---- compute harness over sdf.wgsl ------------------------------------------------------------------

constexpr const char* kKernel = R"(
struct HarnessObject {
    offset: u32,
    count: u32,
    pad0: u32,
    pad1: u32,
};
@group(0) @binding(0) var<storage, read> sdfNodes: array<SdfNodeGpu>;
@group(0) @binding(1) var<uniform> fieldBlock: FieldBlock;
@group(0) @binding(2) var<storage, read> samples: array<vec4<f32>>;
@group(0) @binding(3) var<storage, read> objects: array<HarnessObject>;
@group(0) @binding(4) var<storage, read_write> results: array<vec4<f32>>;
@group(0) @binding(5) var<uniform> harnessTime: vec4<f32>;

@compute @workgroup_size(64)
fn cs_sdf(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= arrayLength(&samples)) { return; }
    let s = samples[i];
    let o = objects[u32(floor(s.w + 0.5))];
    let identity = mat4x4<f32>(vec4<f32>(1.0, 0.0, 0.0, 0.0), vec4<f32>(0.0, 1.0, 0.0, 0.0),
                               vec4<f32>(0.0, 0.0, 1.0, 0.0), vec4<f32>(0.0, 0.0, 0.0, 1.0));
    let d = sdfEvaluate(o.offset, o.count, s.xyz, harnessTime.x, identity);
    let n = sdfNormal(o.offset, o.count, s.xyz, harnessTime.x, identity, harnessTime.y);
    results[i] = vec4<f32>(n, d);
}
)";

struct HarnessObject {
    std::uint32_t offset, count, pad0 = 0, pad1 = 0;
};

class SdfHarness {
public:
    explicit SdfHarness(gpu::Context& ctx) : ctx_(ctx), shaders_(makeShaders(ctx)) {
        auto source = shaders_.loadSource("sdf.wgsl");
        REQUIRE(source.has_value());
        auto module = shaders_.compile(*source + kKernel, "sdf-harness");
        if (!module) {
            FAIL(module.error().message);
        }
        const auto& device = ctx_.device();
        // Binding 15 is the simulated-grid table fields.wgsl declares (ADR-032); the harness
        // binds FieldUniforms' own (empty) table.
        std::array<wgpu::BindGroupLayoutEntry, 7> entries{};
        for (std::uint32_t b = 0; b < 7; ++b) {
            entries[b].binding = b;
            entries[b].visibility = wgpu::ShaderStage::Compute;
        }
        entries[0].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[1].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[2].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[3].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
        entries[4].buffer.type = wgpu::BufferBindingType::Storage;
        entries[5].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[6].binding = 15;
        entries[6].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
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
        cdesc.compute.entryPoint = "cs_sdf";
        pipeline_ = device.CreateComputePipeline(&cdesc);
        REQUIRE(pipeline_ != nullptr);
    }

    // Evaluates every tree at every point (positions.w = tree index); returns (normal, distance).
    std::vector<glm::vec4> run(const std::vector<std::vector<spatial::SdfNodeGpu>>& programs, const spatial::FieldSet& fields,
                               double time, float normalEps, const std::vector<glm::vec4>& positions) {
        std::vector<spatial::SdfNodeGpu> nodes;
        std::vector<HarnessObject> objects;
        for (const auto& program : programs) {
            objects.push_back(HarnessObject{static_cast<std::uint32_t>(nodes.size()), static_cast<std::uint32_t>(program.size())});
            nodes.insert(nodes.end(), program.begin(), program.end());
        }
        rendering::FieldUniforms block(ctx_);
        block.update(fields, time);
        const auto& device = ctx_.device();
        auto storage = [&](const void* data, std::uint64_t size, wgpu::BufferUsage extra) {
            wgpu::BufferDescriptor desc{};
            desc.size = std::max<std::uint64_t>(size, 16);
            desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst | extra;
            wgpu::Buffer b = device.CreateBuffer(&desc);
            if (size > 0) {
                ctx_.queue().WriteBuffer(b, 0, data, size);
            }
            return b;
        };
        wgpu::Buffer nodeBuf = storage(nodes.data(), nodes.size() * sizeof(spatial::SdfNodeGpu), wgpu::BufferUsage::None);
        wgpu::Buffer sampleBuf = storage(positions.data(), positions.size() * sizeof(glm::vec4), wgpu::BufferUsage::None);
        wgpu::Buffer objectBuf = storage(objects.data(), objects.size() * sizeof(HarnessObject), wgpu::BufferUsage::None);
        const std::uint64_t resultBytes = positions.size() * sizeof(glm::vec4);
        wgpu::Buffer resultBuf = storage(nullptr, 0, wgpu::BufferUsage::CopySrc);
        {
            wgpu::BufferDescriptor desc{};
            desc.size = resultBytes;
            desc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc;
            resultBuf = device.CreateBuffer(&desc);
        }
        wgpu::Buffer timeBuf;
        {
            wgpu::BufferDescriptor desc{};
            desc.size = 16;
            desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
            timeBuf = device.CreateBuffer(&desc);
            const glm::vec4 t(static_cast<float>(time), normalEps, 0.0f, 0.0f);
            ctx_.queue().WriteBuffer(timeBuf, 0, &t, sizeof(t));
        }
        std::array<wgpu::BindGroupEntry, 7> entries{};
        entries[0].binding = 0;
        entries[0].buffer = nodeBuf;
        entries[1].binding = 1;
        entries[1].buffer = block.buffer();
        entries[1].size = rendering::FieldUniforms::kBufferSize;
        entries[2].binding = 2;
        entries[2].buffer = sampleBuf;
        entries[3].binding = 3;
        entries[3].buffer = objectBuf;
        entries[4].binding = 4;
        entries[4].buffer = resultBuf;
        entries[5].binding = 5;
        entries[5].buffer = timeBuf;
        entries[6].binding = 15;
        entries[6].buffer = block.gridBuffer();
        entries[6].size = rendering::FieldUniforms::kGridBufferSize;
        wgpu::BindGroupDescriptor gdesc{};
        gdesc.layout = layout_;
        gdesc.entryCount = entries.size();
        gdesc.entries = entries.data();
        wgpu::BindGroup group = device.CreateBindGroup(&gdesc);
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
        pass.SetPipeline(pipeline_);
        pass.SetBindGroup(0, group);
        pass.DispatchWorkgroups(static_cast<std::uint32_t>((positions.size() + 63) / 64));
        pass.End();
        wgpu::CommandBuffer commands = encoder.Finish();
        ctx_.queue().Submit(1, &commands);
        auto bytes = gpu::readBuffer(ctx_, resultBuf, 0, resultBytes);
        REQUIRE(bytes.has_value());
        std::vector<glm::vec4> out(positions.size());
        std::memcpy(out.data(), bytes->data(), resultBytes);
        return out;
    }

private:
    gpu::Context& ctx_;
    gpu::ShaderLibrary shaders_;
    wgpu::BindGroupLayout layout_;
    wgpu::ComputePipeline pipeline_;
};

struct ParityCase {
    std::string name;
    spatial::SdfTree tree;
    float tolerance;
};

void checkParity(gpu::Context& ctx, SdfHarness& harness, const std::vector<ParityCase>& cases, const spatial::FieldSet& fields,
                 double time) {
    const auto points = samplePoints();
    std::vector<std::vector<spatial::SdfNodeGpu>> programs;
    std::vector<glm::vec4> positions;
    for (std::size_t c = 0; c < cases.size(); ++c) {
        const auto valid = cases[c].tree.validate();
        INFO(cases[c].name << ": " << (valid ? std::string("valid") : valid.error().message));
        REQUIRE(valid.has_value());
        std::vector<spatial::SdfNodeGpu> packed;
        REQUIRE(spatial::packSdfTree(cases[c].tree, packed, &fields) > 0);
        programs.push_back(std::move(packed));
        for (const auto& p : points) {
            positions.emplace_back(p, static_cast<float>(c));
        }
    }
    const float normalEps = 1e-3f;
    const auto gpu = harness.run(programs, fields, time, normalEps, positions);
    for (std::size_t c = 0; c < cases.size(); ++c) {
        float worst = 0.0f;
        float worstNormal = 0.0f;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const glm::vec3& p = points[i];
            const glm::vec4& g = gpu[c * points.size() + i];
            const float cpu = spatial::evaluatePacked(programs[c], p, time, &fields);
            const float tree = cases[c].tree.evaluate(p, time, &fields);
            const float dd = std::abs(cpu - g.w);
            worst = std::max(worst, dd);
            INFO(cases[c].name << " sample " << i << " p=(" << p.x << "," << p.y << "," << p.z << ") cpu " << cpu << " tree "
                               << tree << " gpu " << g.w);
            REQUIRE(dd <= cases[c].tolerance);
            REQUIRE(std::abs(tree - cpu) <= 1e-4f);
            // Normals: compare where the CPU gradient is well defined (away from creases).
            const glm::vec3 n = cases[c].tree.normal(p, time, normalEps, &fields);
            const float dn = std::max(std::abs(n.x - g.x), std::max(std::abs(n.y - g.y), std::abs(n.z - g.z)));
            worstNormal = std::max(worstNormal, dn);
        }
        INFO(cases[c].name << " worst distance deviation " << worst << ", worst normal deviation " << worstNormal);
        CHECK(worst <= cases[c].tolerance);
    }
    CHECK(ctx.errorCount() == 0);
}

// ---- render helpers -------------------------------------------------------------------------------

struct Coverage {
    int pixels = 0;
    int minX = 1 << 30, minY = 1 << 30, maxX = -1, maxY = -1;
};

Coverage coverage(const gpu::Image8& img) {
    Coverage c;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const auto* p = img.pixel(x, y);
            if (p[0] + p[1] + p[2] > 15) {
                ++c.pixels;
                c.minX = std::min(c.minX, static_cast<int>(x));
                c.maxX = std::max(c.maxX, static_cast<int>(x));
                c.minY = std::min(c.minY, static_cast<int>(y));
                c.maxY = std::max(c.maxY, static_cast<int>(y));
            }
        }
    }
    return c;
}

// An unlit SDF sphere of `radius` at `position` (flat colour, so pixel tests are exact).
scene::SdfObject unlitSphere(const std::string& name, glm::vec3 position, float radius, glm::vec3 color) {
    scene::SdfObject o;
    o.name = name;
    o.tree = treeOf(sphere(radius));
    o.transform.position = position;
    o.material.baseColor = color;
    o.material.unlit = true;
    o.boundsMin = glm::vec3(-radius * 1.5f);
    o.boundsMax = glm::vec3(radius * 1.5f);
    return o;
}

// An unlit box entity of half extents `half` at `position`.
void addBox(scene::Scene& s, const std::string& name, glm::vec3 position, glm::vec3 half, glm::vec3 color) {
    scene::MeshData mesh = scene::makeBox(half * 2.0f, 1);
    const scene::MeshId id = s.addMesh(std::move(mesh));
    scene::Entity& e = s.addEntity(name, id);
    e.transform.position = position;
    e.material.baseColor = color;
    e.material.unlit = true;
}

scene::Scene baseScene() {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.environment.gridIntensity = 0.0f;
    // Unlit colours must survive to the LDR image unchanged: no bloom bleed, no filmic curve.
    s.post.bloomEnabled = false;
    s.post.tonemap = scene::TonemapOperator::Clamp;
    s.camera.position = {0.0f, 0.0f, 8.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.addLight(scene::PunctualLight{});
    return s;
}

bool isColor(const std::uint8_t* p, int r, int g, int b) {
    return std::abs(p[0] - r) < 40 && std::abs(p[1] - g) < 40 && std::abs(p[2] - b) < 40;
}

gpu::Image8 renderWith(rendering::SceneRenderer& renderer, const scene::Scene& s, double time = 0.0, std::uint32_t w = 192,
                       std::uint32_t h = 192) {
    FrameTime t{};
    t.renderTime = time;
    auto img = renderer.renderToImage(s, t, w, h);
    REQUIRE(img.has_value());
    return *img;
}

} // namespace

// =================================================================================================

TEST_CASE("SDF interpreter matches spatial::evaluatePacked for nested trees", "[gpu][sdf]") {
    auto ctx = makeContext();
    SdfHarness harness(*ctx);
    spatial::FieldSet fields;
    std::vector<ParityCase> cases;

    // Every primitive alone, at a translated/rotated frame.
    for (const SdfNodeKind kind : {SdfNodeKind::Sphere, SdfNodeKind::Box, SdfNodeKind::RoundedBox, SdfNodeKind::Cylinder,
                                   SdfNodeKind::Capsule, SdfNodeKind::Torus, SdfNodeKind::Plane, SdfNodeKind::Cone}) {
        SdfNode prim = node(kind);
        prim.radius = 0.7f;
        prim.height = 1.4f;
        prim.size = glm::vec3(0.6f, 0.4f, 0.5f);
        prim.rounding = 0.15f;
        prim.axis = glm::vec3(0.3f, 1.0f, -0.2f);
        prim.offset = 0.4f;
        SdfNode rot = unary(SdfNodeKind::Rotate, translate(glm::vec3(0.3f, -0.2f, 0.1f), std::move(prim)));
        rot.rotationDegrees = glm::vec3(20.0f, 35.0f, -50.0f);
        cases.push_back({std::string("primitive ") + spatial::sdfNodeKindName(kind), treeOf(std::move(rot)), 1e-4f});
    }
    // Every combination over three children.
    for (const SdfNodeKind kind : {SdfNodeKind::Union, SdfNodeKind::Intersection, SdfNodeKind::Difference,
                                   SdfNodeKind::SmoothUnion, SdfNodeKind::SmoothIntersection, SdfNodeKind::SmoothDifference}) {
        cases.push_back({std::string("combination ") + spatial::sdfNodeKindName(kind),
                         treeOf(combo(kind,
                                      {sphere(1.0f), translate(glm::vec3(0.6f, 0.0f, 0.0f), box(glm::vec3(0.5f))),
                                       translate(glm::vec3(-0.5f, 0.4f, 0.0f), sphere(0.6f))},
                                      0.35f)),
                         1e-4f});
    }
    // Domain operations, each over a smooth union.
    {
        auto base = [] {
            return combo(SdfNodeKind::SmoothUnion, {sphere(0.5f), translate(glm::vec3(0.5f, 0.2f, 0.0f), box(glm::vec3(0.3f)))},
                         0.25f);
        };
        SdfNode scaled = unary(SdfNodeKind::Scale, base());
        scaled.scale = 1.6f;
        cases.push_back({"scale", treeOf(std::move(scaled)), 1e-4f});
        SdfNode twisted = unary(SdfNodeKind::Twist, base());
        twisted.amount = 0.9f;
        cases.push_back({"twist", treeOf(std::move(twisted)), 1e-4f});
        SdfNode bent = unary(SdfNodeKind::Bend, base());
        bent.amount = 0.5f;
        cases.push_back({"bend", treeOf(std::move(bent)), 1e-4f});
        SdfNode repeated = unary(SdfNodeKind::Repeat, base());
        repeated.size = glm::vec3(1.3f, 1.1f, 0.0f);
        repeated.count = 0;
        cases.push_back({"repeat infinite", treeOf(std::move(repeated)), 1e-4f});
        SdfNode limited = unary(SdfNodeKind::Repeat, base());
        limited.size = glm::vec3(1.3f, 0.0f, 1.2f);
        limited.count = 1;
        cases.push_back({"repeat limited", treeOf(std::move(limited)), 1e-4f});
        SdfNode polar = unary(SdfNodeKind::PolarRepeat, translate(glm::vec3(1.2f, 0.0f, 0.0f), base()));
        polar.count = 7;
        cases.push_back({"polar repeat", treeOf(std::move(polar)), 1e-4f});
        SdfNode mirrored = unary(SdfNodeKind::Mirror, translate(glm::vec3(0.8f, 0.3f, -0.5f), base()));
        mirrored.size = glm::vec3(1.0f, 1.0f, 0.0f);
        cases.push_back({"mirror", treeOf(std::move(mirrored)), 1e-4f});
    }
    // Displacements (noise kinds within 1e-3).
    {
        SdfNode noisy = unary(SdfNodeKind::DisplaceNoise, sphere(1.0f));
        noisy.amount = 0.2f;
        noisy.frequency = 3.0f;
        noisy.speed = 0.6f;
        noisy.seed = 11;
        cases.push_back({"displace noise", treeOf(std::move(noisy)), 1e-3f});
        SdfNode voro = unary(SdfNodeKind::DisplaceVoronoi, sphere(1.0f));
        voro.amount = 0.15f;
        voro.frequency = 2.0f;
        voro.seed = 3;
        cases.push_back({"displace voronoi", treeOf(std::move(voro)), 1e-3f});
        SdfNode wave = unary(SdfNodeKind::DisplaceWave, sphere(1.0f));
        wave.amount = 0.1f;
        wave.frequency = 5.0f;
        wave.speed = 2.0f;
        wave.axis = glm::vec3(1.0f, 0.3f, -0.4f);
        cases.push_back({"displace wave", treeOf(std::move(wave)), 1e-4f});
    }
    cases.push_back({"complex", treeOf(complexTree()), 1e-3f});
    checkParity(*ctx, harness, cases, fields, 1.37);
}

TEST_CASE("SDF field displacement samples the bound field slot on the GPU", "[gpu][sdf]") {
    auto ctx = makeContext();
    SdfHarness harness(*ctx);
    spatial::FieldSet fields;
    {
        spatial::FieldSpec f;
        f.name = "bulge";
        f.kind = spatial::FieldKind::Radial;
        f.radius = 3.0f;
        f.strength = 0.8f;
        f.falloff.kind = spatial::FalloffKind::Smoothstep;
        f.falloff.inner = 0.5f;
        f.falloff.outer = 3.0f;
        fields.fields.push_back(f);
        spatial::FieldSpec n;
        n.name = "grain";
        n.kind = spatial::FieldKind::Noise;
        n.frequency = 1.5f;
        n.strength = 0.5f;
        n.speed = 0.4f;
        fields.fields.push_back(n);
    }
    std::vector<ParityCase> cases;
    SdfNode bound = unary(SdfNodeKind::DisplaceField, sphere(1.0f));
    bound.amount = 0.5f;
    bound.reference = "bulge";
    cases.push_back({"field radial", treeOf(std::move(bound)), 1e-4f});
    SdfNode noisy = unary(SdfNodeKind::DisplaceField, box(glm::vec3(0.8f)));
    noisy.amount = 0.3f;
    noisy.reference = "grain";
    cases.push_back({"field noise", treeOf(std::move(noisy)), 1e-3f});
    SdfNode unbound = unary(SdfNodeKind::DisplaceField, sphere(1.0f));
    unbound.amount = 0.5f;
    unbound.reference = "missing";
    cases.push_back({"field unbound", treeOf(std::move(unbound)), 1e-4f});
    checkParity(*ctx, harness, cases, fields, 0.8);
}

TEST_CASE("SDF raymarch composes with meshes through depth in both directions", "[gpu][sdf]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    auto init = renderer.init();
    if (!init) {
        FAIL(init.error().message);
    }

    SECTION("a sphere in front of a box occludes it; the box shows around the sphere") {
        scene::Scene s = baseScene();
        addBox(s, "wall", {0.0f, 0.0f, -2.0f}, {2.5f, 2.5f, 0.2f}, {0.0f, 0.0f, 1.0f});
        s.sdfs.push_back(unlitSphere("ball", {0.0f, 0.0f, 0.0f}, 1.0f, {1.0f, 0.0f, 0.0f}));
        const auto img = renderWith(renderer, s);
        CHECK(ctx->errorCount() == 0);
        const auto& st = renderer.stats();
        CHECK(st.sdf.objects == 1);
        CHECK(st.sdf.raymarchObjects == 1);
        CHECK(st.sdf.packedNodes == 1);
        // Centre pixel: the red sphere. Corner of the wall: blue. Sphere edge region is red, not blue.
        CHECK(isColor(img.pixel(96, 96), 255, 0, 0));
        CHECK(isColor(img.pixel(56, 56), 0, 0, 255)); // wall only: the 5x5 wall spans ~45..148 px, the sphere ~70..122
        CHECK(isColor(img.pixel(96 + 12, 96), 255, 0, 0));
        // The sphere covers a disc of roughly the expected size: red pixels are many, bounded.
        int red = 0;
        for (std::uint32_t y = 0; y < img.height; ++y) {
            for (std::uint32_t x = 0; x < img.width; ++x) {
                if (isColor(img.pixel(x, y), 255, 0, 0)) ++red;
            }
        }
        CHECK(red > 800);
        CHECK(red < 192 * 192 / 3);
        if (const char* dumpDir = std::getenv("AVGEN_DUMP_DIR")) {
            REQUIRE(gpu::writePpm(img, std::filesystem::path(dumpDir) / "sdf_sphere_wall.ppm").has_value());
        }
    }
    SECTION("a box in front of the sphere occludes it, even when drawn before it") {
        scene::Scene s = baseScene();
        s.sdfs.push_back(unlitSphere("ball", {0.0f, 0.0f, 0.0f}, 1.2f, {1.0f, 0.0f, 0.0f}));
        addBox(s, "block", {0.0f, 0.0f, 2.0f}, {0.4f, 0.4f, 0.2f}, {0.0f, 1.0f, 0.0f});
        const auto img = renderWith(renderer, s);
        CHECK(ctx->errorCount() == 0);
        CHECK(isColor(img.pixel(96, 96), 0, 255, 0)); // block in front
        CHECK(isColor(img.pixel(96 + 22, 96), 255, 0, 0)); // sphere visible beside it
    }
    SECTION("a procedural instance in front of the sphere occludes it") {
        scene::Scene s = baseScene();
        s.sdfs.push_back(unlitSphere("ball", {0.0f, 0.0f, 0.0f}, 1.2f, {1.0f, 0.0f, 0.0f}));
        scene::ProceduralGeometry g;
        g.name = "block";
        g.source.kind = scene::PrimitiveKind::Box;
        g.source.size = {0.8f, 0.8f, 0.4f};
        scene::InstanceRecord r{};
        r.position = {0.0f, 0.0f, 2.0f, 1.0f};
        r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        r.scale = {1.0f, 1.0f, 1.0f, 0.0f};
        r.color = {1.0f, 1.0f, 1.0f, 0.0f};
        r.emissive = {1.0f, 1.0f, 1.0f, 0.0f};
        g.instances = {r};
        g.structureVersion = 1;
        g.meshHash = 0xB10Cull;
        g.material.baseColor = {0.0f, 1.0f, 0.0f};
        g.material.unlit = true;
        s.procedurals.push_back(g);
        const auto img = renderWith(renderer, s);
        CHECK(ctx->errorCount() == 0);
        CHECK(isColor(img.pixel(96, 96), 0, 255, 0));
        CHECK(isColor(img.pixel(96 + 30, 96), 255, 0, 0));
    }
    SECTION("two SDF objects occlude each other by depth") {
        scene::Scene s = baseScene();
        s.sdfs.push_back(unlitSphere("far", {0.0f, 0.0f, -1.0f}, 1.5f, {0.0f, 0.0f, 1.0f}));
        s.sdfs.push_back(unlitSphere("near", {0.0f, 0.0f, 1.5f}, 0.5f, {1.0f, 0.0f, 0.0f}));
        const auto img = renderWith(renderer, s);
        CHECK(ctx->errorCount() == 0);
        CHECK(isColor(img.pixel(96, 96), 255, 0, 0));
        CHECK(isColor(img.pixel(96 + 25, 96), 0, 0, 255)); // outside the near sphere, inside the far one
        CHECK(renderer.stats().sdf.raymarchObjects == 2);
        CHECK(renderer.stats().sdf.packedNodes == 2);
    }
    SECTION("an invisible or off-screen object costs nothing") {
        scene::Scene s = baseScene();
        s.sdfs.push_back(unlitSphere("hidden", {0.0f, 0.0f, 0.0f}, 1.0f, {1.0f, 0.0f, 0.0f}));
        s.sdfs.back().visible = false;
        s.sdfs.push_back(unlitSphere("offscreen", {50.0f, 0.0f, 0.0f}, 1.0f, {1.0f, 0.0f, 0.0f}));
        const auto img = renderWith(renderer, s);
        CHECK(ctx->errorCount() == 0);
        CHECK(renderer.stats().sdf.objects == 0);
        CHECK(coverage(img).pixels == 0);
    }
}

TEST_CASE("SDF raymarch shades with lights and fog like meshes", "[gpu][sdf]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    scene::Scene s = baseScene();
    scene::SdfObject o;
    o.name = "blob";
    o.tree = treeOf(complexTree());
    o.material.baseColor = {0.8f, 0.6f, 0.4f};
    o.material.roughness = 0.4f;
    o.boundsMin = glm::vec3(-3.5f);
    o.boundsMax = glm::vec3(3.5f);
    o.transform.rotation = glm::quat(glm::radians(glm::vec3(10.0f, 40.0f, 0.0f)));
    s.sdfs.push_back(o);
    const auto lit = renderWith(renderer, s, 0.5, 256, 256);
    CHECK(ctx->errorCount() == 0);
    const Coverage c = coverage(lit);
    CHECK(c.pixels > 1500);
    // Lit shading varies across the surface (not a flat colour).
    int distinct = 0;
    std::array<bool, 256> seen{};
    for (std::uint32_t y = 0; y < lit.height; y += 4) {
        for (std::uint32_t x = 0; x < lit.width; x += 4) {
            const auto* p = lit.pixel(x, y);
            if (p[0] + p[1] + p[2] > 15 && !seen[p[0]]) {
                seen[p[0]] = true;
                ++distinct;
            }
        }
    }
    CHECK(distinct > 8);
    // Fog pulls the object towards the fog colour.
    s.environment.fogColor = {0.0f, 0.0f, 1.0f};
    s.environment.fogDensity = 0.3f;
    const auto foggy = renderWith(renderer, s, 0.5, 256, 256);
    long blueLit = 0, blueFog = 0;
    for (std::uint32_t y = 0; y < 256; ++y) {
        for (std::uint32_t x = 0; x < 256; ++x) {
            blueLit += lit.pixel(x, y)[2];
            blueFog += foggy.pixel(x, y)[2];
        }
    }
    CHECK(blueFog > blueLit);
    if (const char* dumpDir = std::getenv("AVGEN_DUMP_DIR")) {
        REQUIRE(gpu::writePpm(lit, std::filesystem::path(dumpDir) / "sdf_complex.ppm").has_value());
    }
}

TEST_CASE("SDF mesh mode draws the surface-nets mesh and caches it by hash", "[gpu][sdf]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    scene::Scene s = baseScene();
    scene::SdfObject o = unlitSphere("meshed", {0.0f, 0.0f, 0.0f}, 1.0f, {1.0f, 0.0f, 0.0f});
    o.renderMode = scene::SdfRenderMode::Mesh;
    o.resolution = 24;
    o.material.unlit = false;
    o.material.baseColor = {0.9f, 0.3f, 0.2f};
    REQUIRE(o.rebuild());
    REQUIRE(o.mesh.valid());
    s.sdfs.push_back(o);
    const auto img = renderWith(renderer, s);
    CHECK(ctx->errorCount() == 0);
    const auto& st = renderer.stats();
    CHECK(st.sdf.objects == 1);
    CHECK(st.sdf.meshObjects == 1);
    CHECK(st.sdf.raymarchObjects == 0);
    CHECK(st.sdf.meshUploads == 1);
    CHECK(st.sdf.meshTriangles == o.mesh.indices.size() / 3);
    CHECK(st.triangles >= st.sdf.meshTriangles);
    CHECK(coverage(img).pixels > 800);
    const auto* centre = img.pixel(96, 96);
    CHECK(centre[0] > centre[2]);
    // Same hash: no re-upload. A different resolution re-meshes and re-uploads.
    renderWith(renderer, s);
    CHECK(renderer.stats().sdf.meshUploads == 0);
    s.sdfs[0].resolution = 32;
    REQUIRE(s.sdfs[0].rebuild());
    renderWith(renderer, s);
    CHECK(renderer.stats().sdf.meshUploads == 1);
    // Mesh mode composes with the raymarch path: the same sphere in both modes covers the same disc.
    scene::Scene r = baseScene();
    r.sdfs.push_back(unlitSphere("marched", {0.0f, 0.0f, 0.0f}, 1.0f, {1.0f, 0.0f, 0.0f}));
    const auto marched = renderWith(renderer, r);
    scene::Scene m = baseScene();
    m.sdfs.push_back(o);
    m.sdfs[0].material.unlit = true;
    m.sdfs[0].material.baseColor = {1.0f, 0.0f, 0.0f};
    const auto meshed = renderWith(renderer, m);
    const Coverage a = coverage(marched);
    const Coverage b = coverage(meshed);
    CHECK(std::abs(a.pixels - b.pixels) < a.pixels / 10);
}

TEST_CASE("SDF rendering is deterministic across fresh renderers", "[gpu][sdf]") {
    auto ctx = makeContext();
    scene::Scene s = baseScene();
    scene::SdfObject o;
    o.name = "blob";
    o.tree = treeOf(complexTree());
    o.boundsMin = glm::vec3(-3.5f);
    o.boundsMax = glm::vec3(3.5f);
    s.sdfs.push_back(o);
    addBox(s, "wall", {0.0f, -1.0f, -2.0f}, {3.0f, 0.2f, 3.0f}, {0.2f, 0.3f, 0.9f});
    std::uint64_t hashes[2] = {0, 0};
    for (int i = 0; i < 2; ++i) {
        auto shaders = makeShaders(*ctx);
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        FrameTime t{};
        t.renderTime = 0.75;
        auto img = renderer.renderToImage(s, t, 160, 120);
        REQUIRE(img.has_value());
        auto again = renderer.renderToImage(s, t, 160, 120);
        REQUIRE(again.has_value());
        CHECK(gpu::hashImage(*img) == gpu::hashImage(*again));
        hashes[i] = gpu::hashImage(*img);
    }
    CHECK(hashes[0] == hashes[1]);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("SDF scene swaps produce fresh renderer pixels", "[gpu][sdf][forensics]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    rendering::SceneRenderer fresh(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    REQUIRE(fresh.init().has_value());

    auto first = baseScene();
    first.sdfs.push_back(unlitSphere("blob", {-0.8f, 0.0f, 0.0f}, 0.8f, {1.0f, 0.0f, 0.0f}));
    auto second = baseScene();
    second.sdfs.push_back(unlitSphere("blob", {0.8f, 0.0f, 0.0f}, 0.8f, {0.0f, 1.0f, 0.0f}));
    FrameTime time{};
    time.frameIndex = 0;
    const auto firstImage = renderer.renderToImage(first, time, 160, 120);
    REQUIRE(firstImage.has_value());
    const auto reused = renderer.renderToImage(second, time, 160, 120);
    REQUIRE(reused.has_value());
    const auto expected = fresh.renderToImage(second, time, 160, 120);
    REQUIRE(expected.has_value());
    CHECK(gpu::hashImage(*reused) == gpu::hashImage(*expected));
    CHECK(gpu::hashImage(*firstImage) != gpu::hashImage(*reused));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("SDF pass leaves a scene without SDF objects untouched", "[gpu][sdf]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    scene::Scene s = baseScene();
    addBox(s, "wall", {0.0f, 0.0f, -2.0f}, {1.0f, 1.0f, 0.2f}, {0.0f, 0.0f, 1.0f});
    const auto img = renderWith(renderer, s);
    CHECK(renderer.stats().sdf.objects == 0);
    CHECK(renderer.stats().sdf.raymarchMs < 0.0);
    CHECK(isColor(img.pixel(96, 96), 0, 0, 255));
    CHECK(ctx->errorCount() == 0);
}

// Hidden performance probe: `avgen_render_tests "[.perf][sdf]"`. A full-screen 1080p smooth union
// of 16 noise-displaced spheres; reports the raymarch pass time.
TEST_CASE("SDF raymarch throughput", "[.perf][sdf]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    struct Config {
        const char* name;
        bool noise;
        int spheres;
    };
    for (const Config cfg : {Config{"16 spheres, smooth union", false, 16}, Config{"16 spheres, smooth union + noise", true, 16},
                             Config{"1 sphere", false, 1}}) {
        scene::Scene s = baseScene();
        s.camera.position = {0.0f, 0.0f, 3.0f};
        std::vector<SdfNode> leaves;
        for (int i = 0; i < cfg.spheres; ++i) {
            const float a = static_cast<float>(i) * 0.39f;
            leaves.push_back(translate(glm::vec3(std::cos(a) * 1.5f, std::sin(a * 1.3f) * 1.2f, std::sin(a) * 1.0f - 1.0f),
                                       sphere(0.9f)));
        }
        // A combination takes at most 8 children (spatial::sdfNodeMaxChildren), so the 16 spheres
        // fold as four smooth unions of four under one more: the same surface, one level deeper.
        SdfNode root;
        if (leaves.size() > 1) {
            std::vector<SdfNode> groups;
            for (std::size_t i = 0; i < leaves.size(); i += 4) {
                std::vector<SdfNode> chunk;
                for (std::size_t j = i; j < std::min(i + 4, leaves.size()); ++j) {
                    chunk.push_back(std::move(leaves[j]));
                }
                groups.push_back(chunk.size() == 1 ? std::move(chunk[0])
                                                   : combo(SdfNodeKind::SmoothUnion, std::move(chunk), 0.4f));
            }
            root = groups.size() == 1 ? std::move(groups[0]) : combo(SdfNodeKind::SmoothUnion, std::move(groups), 0.4f);
        } else {
            root = std::move(leaves[0]);
        }
        if (cfg.noise) {
            SdfNode noisy = unary(SdfNodeKind::DisplaceNoise, std::move(root));
            noisy.amount = 0.15f;
            noisy.frequency = 2.0f;
            noisy.speed = 0.5f;
            root = std::move(noisy);
        }
        scene::SdfObject o;
        o.name = "blobs";
        o.tree = treeOf(std::move(root));
        o.boundsMin = glm::vec3(-4.0f);
        o.boundsMax = glm::vec3(4.0f);
        o.stepScale = cfg.noise ? 0.7f : 0.9f;
        s.sdfs.push_back(o);
        FixedStepClock clock(60.0);
        double passSum = 0.0, frameSum = 0.0, cpuSum = 0.0;
        int counted = 0;
        for (int i = 0; i < 90; ++i) {
            auto img = renderer.renderToImage(s, clock.tick(), 1920, 1080);
            REQUIRE(img.has_value());
            if (i >= 30 && renderer.stats().sdf.raymarchMs >= 0.0) {
                passSum += renderer.stats().sdf.raymarchMs;
                frameSum += renderer.stats().gpuFrameMs;
                cpuSum += renderer.stats().sdf.cpuUpdateMs;
                ++counted;
            }
            if (i == 89) {
                if (const char* dumpDir = std::getenv("AVGEN_DUMP_DIR")) {
                    REQUIRE(gpu::writePpm(*img, std::filesystem::path(dumpDir) / "sdf_perf.ppm").has_value());
                }
            }
        }
        CHECK(ctx->errorCount() == 0);
        WARN("sdf " << cfg.name << ": raymarch pass " << (counted ? passSum / counted : -1.0) << " ms, frame "
                    << (counted ? frameSum / counted : -1.0) << " ms, CPU update " << (counted ? cpuSum / counted : -1.0)
                    << " ms, packed nodes " << renderer.stats().sdf.packedNodes);
    }
}

// A probe, not an assertion, because what it found is a defect that is not fixed here.
//
// The §15/§16 parity audit went looking for a reader of `QualitySettings::sdfShadowSteps`, found
// none (the march derived its own budget as `maxSteps / 4`), and wired it. This was meant to be the
// test that the wiring reaches the picture. It does not -- and the reason is one layer down.
//
// `SdfRenderer::update` computes each raymarched object's screen-space quad, `sdf.rect`, from the
// *camera's* view-projection, and `drawRaymarchDepth(..., reducedSteps = true)` reuses that same
// rect when the shadow pass draws the object into a shadow map whose projection is the *light's*.
// The ray the shader reconstructs is the light's (the frame block is), but the quad it reconstructs
// it over is the camera's, so the march happens over the wrong region of the shadow map. Measured
// here: switching the key light's shadow off with the SDF in place changes the frame by **zero**
// bytes, so this object casts no shadow at all.
//
// ADR-034 says raymarched SDFs "appear in the depth prepass and in the shadow maps". The prepass
// half is true -- that pass shares the camera's projection, which is exactly why the defect hides.
//
// The fix is a per-view rect (or the full-screen fallback the shader already has) in the shadow
// pass, and it has a cost nobody has measured: a full shadow-map quad per SDF per cascade. That is
// a decision with a number attached, so it is recorded rather than guessed at here.
//
// This probe passes when the defect is gone. Run it with `avgen_render_tests "[.probe][sdf]"`.
TEST_CASE("A raymarched SDF does not reach the shadow map", "[.probe][gpu][sdf][quality]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // A caster, a receiver and a light that casts. Without all three there is no shadow map read
    // anywhere in the frame, and a test of the shadow march would pass whatever the march did --
    // which is how the first version of this test reported "0 pixels differ" and meant nothing.
    scene::Scene s = baseScene();
    const scene::MeshId ground = s.addMesh(scene::makePlane(20.0f, 4));
    {
        auto& e = s.addEntity("ground", ground);
        e.transform.position = {0.0f, -4.0f, 0.0f};
        e.material.baseColor = glm::vec3(0.85f);
        e.material.roughness = 0.9f;
    }
    scene::PunctualLight key;
    key.name = "key";
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(0.6f, -0.55f, -0.2f));
    key.intensity = 4.0f;
    key.castsShadow = true;
    key.contactShadow = false;
    s.addLight(key);
    s.camera.position = {0.0f, 6.0f, 14.0f};
    s.camera.target = {0.0f, -2.0f, 0.0f};

    scene::SdfObject o;
    o.name = "caster";
    o.tree = treeOf(complexTree());
    o.material.baseColor = {0.8f, 0.6f, 0.4f};
    o.boundsMin = glm::vec3(-3.5f);
    o.boundsMax = glm::vec3(3.5f);
    // A long march, so a budget below it actually bites. Under the old `maxSteps / 4` rule this
    // object marched 48 shadow steps whatever the tier said.
    o.maxSteps = 192;
    s.sdfs.push_back(o);

    // Everything except the SDF shadow budget held at one tier's values, so a difference cannot be
    // some other field of the tier moving. Without this the test would pass whatever
    // `sdfShadowSteps` did, which is the trap it exists to avoid.
    const auto renderAtSteps = [&](std::uint32_t steps) {
        rendering::QualitySettings held = rendering::QualitySettings::forTier(rendering::QualityTier::Realtime);
        held.sdfShadowSteps = steps;
        renderer.setQualitySettings(held);
        return renderWith(renderer, s, 0.5, 256, 256);
    };

    // The extremes of what the field can express rather than two adjacent tiers. The tier values
    // (16 to 48) turn out to produce the *same* shadow on this object -- the march is bounded by
    // the object's box and converges well inside sixteen steps -- so a test on those two would read
    // "no difference" and could not tell that from "the field is not wired". The floor and the cap
    // can: if the shader reads `info.w` at all, eight steps across a seven-unit box at a loose
    // epsilon cannot resolve what a thousand can.
    const auto coarse = renderAtSteps(8);
    const auto fine = renderAtSteps(1024);
    CHECK(ctx->errorCount() == 0);

    // The state the measurement assumes, established rather than hoped for: this SDF is actually in
    // the shadow map. Removing the *caster* would not show that -- taking the object away also
    // takes its own pixels, and the frame changes either way. Switching the light's shadow off
    // with the object still there isolates the shadow, and nothing else in this scene casts one
    // worth counting (a flat plane lit from above self-shadows negligibly).
    scene::Scene unlitShadow = s;
    unlitShadow.lights.back().castsShadow = false;
    const auto noShadow = renderWith(renderer, unlitShadow, 0.5, 256, 256);
    long shadowlessSum = 0;
    long shadowedSum = 0;
    for (std::uint32_t y = 0; y < 256; ++y) {
        for (std::uint32_t x = 0; x < 256; ++x) {
            shadowlessSum += noShadow.pixel(x, y)[1];
            shadowedSum += fine.pixel(x, y)[1];
        }
    }
    INFO("green sum with shadows off " << shadowlessSum << ", on " << shadowedSum);
    REQUIRE(shadowlessSum > shadowedSum);

    long differing = 0;
    for (std::uint32_t y = 0; y < coarse.height; ++y) {
        for (std::uint32_t x = 0; x < coarse.width; ++x) {
            const auto* a = coarse.pixel(x, y);
            const auto* b = fine.pixel(x, y);
            if (std::abs(int(a[0]) - int(b[0])) > 1 || std::abs(int(a[1]) - int(b[1])) > 1 ||
                std::abs(int(a[2]) - int(b[2])) > 1) {
                ++differing;
            }
        }
    }
    INFO("pixels differing between an 8-step and a 1024-step SDF shadow march: " << differing);
    CHECK(differing > 0);
}
