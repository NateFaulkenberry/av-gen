// Ground-following fog through the whole renderer (ADR-715, ADR-575 §18).
//
// `test_height_fog_gpu.cpp` holds the MATHS of the two readers together from the shipped WGSL.
// This file holds the PLUMBING: that the scene's baked terrain reaches both readers through the
// frame group, that `fogGroundFollow` 0 is the frame there was before -- bit for bit, not to within a
// level -- and that a scene with no terrain is unchanged at any value.
//
// Why a byte comparison rather than a tolerance: every scene in the repository has
// `fogGroundFollow` 0, so "0 changes nothing" is a claim about all of them at once, and a change of
// one ULP in the fog would pass any tolerance while moving every volumetric checkpoint hash in
// the suite -- which nobody would re-baseline for a control advertised as off (ADR-568's point).
//
// **How it fails.** Drop the `follow > 0.0` guard in `applyFog` (so the flat branch is replaced by
// `fogGroundMean` even at 0) and the first case fails on thousands of pixels; forget to zero
// `fogShape.z` / `heightFog.z` when the scene has no terrain and the second fails; bind the
// placeholder instead of the bake (or never set `terrainMap1.w`) and the third finds no movement.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"
#include "world/terrain_height.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>

using namespace avgen;

namespace {

constexpr std::uint32_t kSize = 128;

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

// A valley running along Z: the floor at x = 0, the walls climbing 0.25 m per metre to 50 m at
// the edges of a 400 m square. The flat layer (top 3 m) fills only the floor; a layer that follows
// the ground climbs the walls.
float valleyHeight(float x, float /*z*/) { return 0.25f * std::abs(x); }

scene::MeshData valleyMesh() {
    scene::MeshData m;
    constexpr int kQuads = 100;
    constexpr float kHalf = 200.0f;
    const float step = 2.0f * kHalf / kQuads;
    for (int j = 0; j <= kQuads; ++j) {
        for (int i = 0; i <= kQuads; ++i) {
            const float x = -kHalf + static_cast<float>(i) * step;
            const float z = -kHalf + static_cast<float>(j) * step;
            const float dx = x >= 0.0f ? 0.25f : -0.25f;
            m.vertices.push_back({glm::vec3(x, valleyHeight(x, z), z), glm::normalize(glm::vec3(-dx, 1.0f, 0.0f)),
                                  glm::vec2(0.0f)});
        }
    }
    for (int j = 0; j < kQuads; ++j) {
        for (int i = 0; i < kQuads; ++i) {
            const auto a = static_cast<std::uint32_t>(j * (kQuads + 1) + i);
            const auto b = a + 1;
            const auto c = a + static_cast<std::uint32_t>(kQuads + 1);
            const auto d = c + 1;
            m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
        }
    }
    return m;
}

world::TerrainGround bakeOf(float (*height)(float, float)) {
    auto field = std::make_shared<world::TerrainHeightField>();
    field->origin = glm::vec2(-200.0f);
    field->spacing = 2.0f;
    field->width = field->depth = 201;
    field->hash = reinterpret_cast<std::uintptr_t>(height);
    field->heights.resize(201u * 201u);
    for (std::uint32_t j = 0; j < 201; ++j) {
        for (std::uint32_t i = 0; i < 201; ++i) {
            const glm::vec2 p = field->origin + glm::vec2(static_cast<float>(i), static_cast<float>(j)) * 2.0f;
            field->heights[j * 201u + i] = height(p.x, p.y);
        }
    }
    world::poolTerrainHeight(*field); // ADR-717: the basin, as the bake makes it
    return world::placeTerrainGround(field, glm::vec3(0.0f), glm::vec3(1.0f), false);
}

float nonsense(float x, float z) { return 40.0f * std::sin(x * 0.37f) * std::cos(z * 0.11f) + 17.0f; }

// Looking down the valley from above its floor, both walls in frame. Surface fog with the height
// coupling on, and the volumetric march, both reading the one layer.
scene::Scene valleyScene(bool terrain) {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    // Fog brighter than the ground, so more fog is a brighter pixel.
    s.environment.fogColor = glm::vec3(0.9f);
    // ADR-705: one density; the march carries the first `volumeMaxDistance` metres and the surface
    // pass the rest, so both readers see this layer.
    s.environment.fogHeightAmount = 1.0f;
    s.environment.fogHeight = 3.0f;
    s.environment.fogHeightFalloff = 0.3f;
    s.environment.volumeDensity = 0.02f;
    s.environment.volumeScattering = 0.8f;
    s.environment.volumeAbsorption = 1.0f;
    s.environment.volumeSteps = 48;
    s.environment.volumeMaxDistance = 250.0f;
    s.environment.volumeJitter = 0.0f;
    s.camera.position = {0.0f, 22.0f, 150.0f};
    s.camera.target = {0.0f, 8.0f, 0.0f};
    s.camera.fovYRadians = 1.0f;
    s.camera.nearPlane = 0.1f;
    s.camera.farPlane = 600.0f;
    const auto mesh = s.addMesh(valleyMesh());
    auto& e = s.addEntity("valley", mesh);
    e.material.unlit = true;
    e.material.baseColor = glm::vec3(0.05f);
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(0.2f, -1.0f, -0.3f));
    key.intensity = 2.0f;
    s.addLight(key);
    if (terrain) {
        s.terrainGround = bakeOf(&valleyHeight);
    }
    return s;
}

gpu::ImageF render(rendering::SceneRenderer& renderer, const scene::Scene& s) {
    FrameTime time{};
    time.renderTime = 1.0;
    time.deltaTime = 1.0 / 60.0;
    time.frameIndex = 0;
    auto image = renderer.renderToImageFloat(s, time, kSize, kSize);
    REQUIRE(image.has_value());
    return std::move(*image);
}

bool identical(const gpu::ImageF& a, const gpu::ImageF& b) {
    return a.rgba.size() == b.rgba.size() &&
           std::memcmp(a.rgba.data(), b.rgba.data(), a.rgba.size() * sizeof(float)) == 0;
}

std::size_t pixelsDiffering(const gpu::ImageF& a, const gpu::ImageF& b, float by) {
    std::size_t n = 0;
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        const float d = std::abs(a.rgba[i] - b.rgba[i]) + std::abs(a.rgba[i + 1] - b.rgba[i + 1]) +
                        std::abs(a.rgba[i + 2] - b.rgba[i + 2]);
        n += d > by ? 1 : 0;
    }
    return n;
}

float meanLuminance(const gpu::ImageF& image) {
    double sum = 0.0;
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        sum += 0.2126 * image.rgba[i] + 0.7152 * image.rgba[i + 1] + 0.0722 * image.rgba[i + 2];
    }
    return static_cast<float>(sum / static_cast<double>(image.width * image.height));
}

} // namespace

TEST_CASE("fogGroundFollow 0 is the frame without a terrain, bit for bit", "[gpu][fog][volume][terrain][height]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = std::make_unique<rendering::SceneRenderer>(*ctx, shaders);
    REQUIRE(renderer->init().has_value());

    scene::Scene with = valleyScene(true);
    REQUIRE(with.terrainGround.valid());
    with.environment.fogGroundFollow = 0.0f;
    const gpu::ImageF a = render(*renderer, with);

    // The same scene with no terrain at all: the placeholder bound, the lanes zero.
    const gpu::ImageF b = render(*renderer, valleyScene(false));
    CHECK(identical(a, b));
    CHECK(pixelsDiffering(a, b, 0.0f) == 0);

    // And with a DIFFERENT terrain bound: nothing at 0 may read it. If anything did, this terrain
    // -- 40 m of noise over the valley -- would move the fog everywhere.
    scene::Scene other = valleyScene(false);
    other.terrainGround = bakeOf(&nonsense);
    const gpu::ImageF c = render(*renderer, other);
    CHECK(identical(a, c));

    // The instrument: the frame has fog in it, so "identical" is not two frames with nothing to
    // differ in. Most of the frame moves when both readers are switched off.
    scene::Scene clear = valleyScene(true);
    clear.environment.volumeDensity = 0.0f;
    CHECK(pixelsDiffering(a, render(*renderer, clear), 1.0f / 255.0f) > kSize * kSize / 2);
}

TEST_CASE("without a terrain, fogGroundFollow changes nothing", "[gpu][fog][volume][terrain][height]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = std::make_unique<rendering::SceneRenderer>(*ctx, shaders);
    REQUIRE(renderer->init().has_value());

    scene::Scene s = valleyScene(false);
    s.environment.fogGroundFollow = 0.0f;
    const gpu::ImageF off = render(*renderer, s);
    for (const float follow : {0.5f, 1.0f}) {
        s.environment.fogGroundFollow = follow;
        INFO("follow " << follow);
        CHECK(identical(off, render(*renderer, s)));
    }
}

TEST_CASE("with a terrain, fogGroundFollow moves the fog in both readers", "[gpu][fog][volume][terrain][height]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = std::make_unique<rendering::SceneRenderer>(*ctx, shaders);
    REQUIRE(renderer->init().has_value());

    // Each reader alone, so a pass that ignored the control cannot hide behind the other.
    // ADR-705: which reader carries the ray is `volumeMaxDistance` -- 0 is no march (the surface pass
    // integrates the whole ray), and a reach past the far plane leaves the surface pass nothing.
    struct Arm {
        const char* name;
        float marchReach;
    };
    for (const Arm arm : {Arm{"surface fog only", 0.0f}, Arm{"march only", 100000.0f}}) {
        INFO(arm.name);
        scene::Scene s = valleyScene(true);
        s.environment.volumeMaxDistance = arm.marchReach;
        s.environment.fogGroundFollow = 0.0f;
        const gpu::ImageF flat = render(*renderer, s);
        s.environment.fogGroundFollow = 1.0f;
        const gpu::ImageF follows = render(*renderer, s);
        const std::size_t moved = pixelsDiffering(flat, follows, 1.0f / 255.0f);
        INFO("pixels moved by more than a level: " << moved << " mean " << meanLuminance(flat) << " -> "
                                                   << meanLuminance(follows));
        // The walls climb out of the flat layer and into the followed one: a large part of the
        // frame, and in the direction of MORE fog, since the fog is the bright thing here.
        CHECK(moved > kSize * kSize / 10);
        CHECK(meanLuminance(follows) > meanLuminance(flat));
    }
}

// ---- the surface pass at 0 is the surface pass before ADR-715 ---------------------------------
//
// The two cases above compare new frames with new frames, so they cannot see a change that moves
// EVERY frame alike -- routing follow 0 through `fogGroundMean` with a zero ground, for instance,
// which sums eight difference quotients where there used to be one and differs by an ULP. This
// holds the SHIPPED `applyFog` (compiled from common.wgsl, the file the renderer loads) against the
// function as it stood before ADR-715 -- since ADR-705 merged, ADR-705's one-law applyFog, with
// Horizon Density read from `fogShape.w` -- restated here verbatim, the move ADR-568's bit-identity
// case makes for the profile. At follow 0 the two must agree to the bit on every input; at 0.5 they
// must not, which is the control that the comparison can see anything at all.
//
// The march needs no twin: its follow-0 line IS the pre-ADR-715 line, untouched and guarded, and
// the first case above is what catches it reading the ground when it should not.

namespace {

constexpr const char* kApplyFogKernel = R"(
fn applyFogBeforeAdr715(color: vec3<f32>, worldPos: vec3<f32>) -> vec3<f32> {
    let extinction = frame.fogParams.w;
    if (extinction <= 0.0) {
        return color;
    }
    let dist = distance(frame.cameraPos.xyz, worldPos);
    let start = frame.fogHeight.w;
    if (dist <= start) {
        return color; // the march has this whole ray
    }
    var travel = dist - start;
    let amount = frame.fogHeight.z;
    let falloff = frame.fogHeight.y;
    if (amount > 0.0 && falloff > 0.0) {
        let yEye = frame.cameraPos.y - frame.fogHeight.x;
        let y1 = worldPos.y - frame.fogHeight.x;
        // Where the segment starts: the point the march hands over at. `start` 0 is the eye itself,
        // exactly -- `(y1 - yEye) * 0 / dist` is 0 and adds nothing.
        let y0 = yEye + (y1 - yEye) * (start / dist);
        let rise = y1 - y0;
        // The mean of the layer's density along the segment. The difference quotient is the whole
        // integral because the ray climbs at a constant rate: metres of mist per metre travelled.
        var mean = 1.0;
        let upper = frame.fogShape.x;
        let curve = frame.fogShape.y;
        if (max(y0, y1) > 0.0) {
            // Both endpoints below the layer's top puts the whole segment below it, so the mean is
            // exactly one and this branch is skipped. Leaving it to the quotient would give 1.0
            // only to within rounding, because the numerator and the denominator are the same
            // subtraction written twice and the compiler is free to fuse one and not the other.
            mean = fogHeightProfile(y0, falloff, upper, curve); // a level ray never leaves its altitude
            if (abs(rise) > 1e-3) {
                mean = (fogHeightIntegral(y1, falloff, upper, curve) -
                        fogHeightIntegral(y0, falloff, upper, curve)) / rise;
            }
        }
        travel = travel * mix(1.0, mean, amount);
    }
    var depth = extinction * travel;
    let horizon = frame.fogShape.w; // ADR-705 (moved from z when ADR-715 took z)
    if (horizon > 0.0) {
        depth = depth * (1.0 + horizon * (start + dist) * 0.0005);
    }
    let f = exp(-depth);
    return mix(frame.fogParams.rgb, color, f);
}

@group(1) @binding(5) var<storage, read> fogPoints: array<vec4<f32>>;
@group(1) @binding(6) var<storage, read_write> fogOut: array<vec4<f32>>;

@compute @workgroup_size(64)
fn cs_apply_fog(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= arrayLength(&fogPoints)) { return; }
    let p = fogPoints[i].xyz;
    let c = vec3<f32>(0.31, 0.52, 0.07);
    fogOut[i * 2u] = vec4<f32>(applyFog(c, p), 0.0);
    fogOut[i * 2u + 1u] = vec4<f32>(applyFogBeforeAdr715(c, p), 0.0);
}
)";

struct FogPair {
    std::vector<glm::vec4> shipped;
    std::vector<glm::vec4> before;
};

FogPair runApplyFog(gpu::Context& ctx, const rendering::FrameUniforms& frame, const world::TerrainGround& ground,
                    const std::vector<glm::vec4>& points) {
    gpu::ShaderLibrary shaders(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto src = shaders.loadSource("common.wgsl");
    REQUIRE(src.has_value());
    auto module = shaders.compile(*src + kApplyFogKernel, "apply-fog-identity");
    REQUIRE(module.has_value());
    const auto& device = ctx.device();

    const world::TerrainHeightField& field = *ground.field;
    wgpu::TextureDescriptor tdesc{};
    tdesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    tdesc.size = {field.width, field.depth, 1};
    // The renderer's layout (ADR-717): r = the ground, g = the basin.
    tdesc.format = wgpu::TextureFormat::RG32Float;
    wgpu::Texture texture = device.CreateTexture(&tdesc);
    wgpu::TexelCopyTextureInfo dst{};
    dst.texture = texture;
    wgpu::TexelCopyBufferLayout tl{};
    tl.bytesPerRow = field.width * 8;
    tl.rowsPerImage = field.depth;
    const wgpu::Extent3D extent = {field.width, field.depth, 1};
    REQUIRE(field.pooled());
    std::vector<float> texels(field.heights.size() * 2);
    for (std::size_t k = 0; k < field.heights.size(); ++k) {
        texels[k * 2] = field.heights[k];
        texels[k * 2 + 1] = field.basin[k];
    }
    ctx.queue().WriteTexture(&dst, texels.data(), texels.size() * sizeof(float), &tl, &extent);

    std::array<wgpu::BindGroupLayoutEntry, 2> e0{};
    e0[0].binding = 0;
    e0[0].visibility = wgpu::ShaderStage::Compute;
    e0[0].buffer.type = wgpu::BufferBindingType::Uniform;
    e0[1].binding = 12;
    e0[1].visibility = wgpu::ShaderStage::Compute;
    e0[1].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
    e0[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;
    std::array<wgpu::BindGroupLayoutEntry, 2> e1{};
    e1[0].binding = 5;
    e1[0].visibility = wgpu::ShaderStage::Compute;
    e1[0].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    e1[1].binding = 6;
    e1[1].visibility = wgpu::ShaderStage::Compute;
    e1[1].buffer.type = wgpu::BufferBindingType::Storage;
    wgpu::BindGroupLayoutDescriptor l0{};
    l0.entryCount = e0.size();
    l0.entries = e0.data();
    wgpu::BindGroupLayoutDescriptor l1{};
    l1.entryCount = e1.size();
    l1.entries = e1.data();
    const std::array<wgpu::BindGroupLayout, 2> layouts{device.CreateBindGroupLayout(&l0),
                                                       device.CreateBindGroupLayout(&l1)};
    wgpu::PipelineLayoutDescriptor pdesc{};
    pdesc.bindGroupLayoutCount = layouts.size();
    pdesc.bindGroupLayouts = layouts.data();
    wgpu::ComputePipelineDescriptor cdesc{};
    cdesc.layout = device.CreatePipelineLayout(&pdesc);
    cdesc.compute.module = *module;
    cdesc.compute.entryPoint = "cs_apply_fog";
    wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&cdesc);
    REQUIRE(pipeline != nullptr);

    wgpu::BufferDescriptor fdesc{};
    fdesc.size = sizeof(rendering::FrameUniforms);
    fdesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer frameBuf = device.CreateBuffer(&fdesc);
    ctx.queue().WriteBuffer(frameBuf, 0, &frame, sizeof(frame));
    wgpu::BufferDescriptor idesc{};
    idesc.size = points.size() * sizeof(glm::vec4);
    idesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer in = device.CreateBuffer(&idesc);
    ctx.queue().WriteBuffer(in, 0, points.data(), idesc.size);
    wgpu::BufferDescriptor odesc{};
    odesc.size = points.size() * 2 * sizeof(glm::vec4);
    odesc.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc;
    wgpu::Buffer out = device.CreateBuffer(&odesc);

    std::array<wgpu::BindGroupEntry, 2> b0{};
    b0[0].binding = 0;
    b0[0].buffer = frameBuf;
    b0[0].size = fdesc.size;
    b0[1].binding = 12;
    b0[1].textureView = texture.CreateView();
    std::array<wgpu::BindGroupEntry, 2> b1{};
    b1[0].binding = 5;
    b1[0].buffer = in;
    b1[0].size = idesc.size;
    b1[1].binding = 6;
    b1[1].buffer = out;
    b1[1].size = odesc.size;
    wgpu::BindGroupDescriptor g0{};
    g0.layout = layouts[0];
    g0.entryCount = b0.size();
    g0.entries = b0.data();
    wgpu::BindGroupDescriptor g1{};
    g1.layout = layouts[1];
    g1.entryCount = b1.size();
    g1.entries = b1.data();
    const wgpu::BindGroup group0 = device.CreateBindGroup(&g0);
    const wgpu::BindGroup group1 = device.CreateBindGroup(&g1);

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
    pass.SetPipeline(pipeline);
    pass.SetBindGroup(0, group0);
    pass.SetBindGroup(1, group1);
    pass.DispatchWorkgroups(static_cast<std::uint32_t>((points.size() + 63) / 64));
    pass.End();
    wgpu::CommandBuffer commands = encoder.Finish();
    ctx.queue().Submit(1, &commands);
    auto bytes = gpu::readBuffer(ctx, out, 0, odesc.size);
    REQUIRE(bytes.has_value());
    std::vector<glm::vec4> raw(points.size() * 2);
    std::memcpy(raw.data(), bytes->data(), odesc.size);
    FogPair pair;
    for (std::size_t i = 0; i < points.size(); ++i) {
        pair.shipped.push_back(raw[i * 2]);
        pair.before.push_back(raw[i * 2 + 1]);
    }
    return pair;
}

} // namespace

TEST_CASE("applyFog at fogGroundFollow 0 is applyFog before ADR-715, bit for bit", "[gpu][fog][terrain][height]") {
    auto ctx = makeContext();
    const world::TerrainGround ground = bakeOf(&valleyHeight);
    std::vector<glm::vec4> points;
    for (int j = 0; j < 24; ++j) {
        for (int i = 0; i < 24; ++i) {
            // Surfaces across the valley, from below the layer to far above it, near and far.
            const float x = -190.0f + 16.5f * static_cast<float>(i);
            const float z = -190.0f + 16.5f * static_cast<float>(j);
            points.emplace_back(x, valleyHeight(x, z) + static_cast<float>((i * 7 + j * 3) % 11) * 1.7f, z, 0.0f);
        }
    }
    struct Layer {
        float top, falloff, upper, curve, amount;
    };
    int compared = 0;
    std::size_t differed = 0;
    for (const Layer l : {Layer{3.0f, 0.3f, 0.0f, 0.0f, 1.0f}, Layer{12.0f, 0.05f, 0.2f, 0.75f, 0.6f},
                          Layer{1.0f, 0.6f, 0.0f, 1.0f, 1.0f}, Layer{40.0f, 0.1f, 0.35f, 0.0f, 1.0f}}) {
        rendering::FrameUniforms frame{};
        frame.cameraPos = glm::vec4(0.0f, 22.0f, 150.0f, 1.0f);
        frame.fogParams = glm::vec4(0.9f, 0.85f, 0.8f, 0.012f);
        frame.fogHeight = glm::vec4(l.top, l.falloff, l.amount, 0.0f);
        frame.fogShape = glm::vec4(l.upper, l.curve, 0.0f, 0.0f);
        // The terrain IS bound and placed: at follow 0 nothing may read it.
        frame.terrainMap0 = ground.map0();
        frame.terrainMap1 = ground.map1();
        const FogPair zero = runApplyFog(*ctx, frame, ground, points);
        for (std::size_t i = 0; i < points.size(); ++i) {
            INFO("layer top " << l.top << " point " << i);
            REQUIRE(std::memcmp(&zero.shipped[i], &zero.before[i], sizeof(glm::vec4)) == 0);
            ++compared;
        }
        // The control: at 0.5 the shipped function follows the ground and the old one cannot.
        frame.fogShape.z = 0.5f;
        const FogPair half = runApplyFog(*ctx, frame, ground, points);
        std::size_t differ = 0;
        for (std::size_t i = 0; i < points.size(); ++i) {
            differ += std::memcmp(&half.shipped[i], &half.before[i], sizeof(glm::vec4)) != 0 ? 1 : 0;
        }
        INFO("layer top " << l.top << ": " << differ << " of " << points.size() << " differ at follow 0.5");
        // Every layer moves somewhere. Not everywhere: a surface whose whole ray stays above a
        // compact layer's definite top sees no mist at either setting, which is correct.
        CHECK(differ > 0);
        differed += differ;
        // And with NO terrain the shipped function takes the flat branch at any follow, so a scene
        // without one is the frame it always was -- decided in the shader, not only by the CPU
        // zeroing `fogShape.z`. Eight pieces of a flat layer summed are the one quotient to within
        // an ULP, and an ULP is exactly what this comparison is for.
        frame.terrainMap0 = glm::vec4(0.0f);
        frame.terrainMap1 = glm::vec4(0.0f);
        frame.fogShape.z = 1.0f;
        const FogPair none = runApplyFog(*ctx, frame, ground, points);
        for (std::size_t i = 0; i < points.size(); ++i) {
            INFO("no terrain, follow 1, layer top " << l.top << " point " << i);
            REQUIRE(std::memcmp(&none.shipped[i], &none.before[i], sizeof(glm::vec4)) == 0);
        }
    }
    CHECK(compared == 4 * 24 * 24);
    CHECK(differed > static_cast<std::size_t>(compared) / 4);
}

// ---- ADR-717: pooling through the whole renderer ------------------------------------------------
//
// The maths is `test_height_fog_gpu.cpp`'s. This is the plumbing: the frame's `fogPool` lane reaches
// the surface fog AND the march (which reads the frame lane rather than a copy of its own), a scene
// without a terrain is unchanged at any pooling, and pooling 0 with follow on is the follow frame.
//
// **How it fails.** Leave `frame.fogPool` unset and the third case finds no movement in either
// arm; have the march read `vol.heightFog.z` in place of the pool lane and its arm finds none;
// forget `poolingLane`'s terrain gate and the no-terrain case is still held by the shader's own
// `terrainMap1.w` gate (the ADR-715 lesson), so the second case checks the lane itself as well.

TEST_CASE("without a terrain, fogPooling changes nothing", "[gpu][fog][volume][terrain][height]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = std::make_unique<rendering::SceneRenderer>(*ctx, shaders);
    REQUIRE(renderer->init().has_value());

    scene::Scene s = valleyScene(false);
    const gpu::ImageF off = render(*renderer, s);
    for (const float pooling : {0.5f, 1.0f}) {
        s.environment.fogPooling = pooling;
        INFO("pooling " << pooling);
        CHECK(identical(off, render(*renderer, s)));
        // And the lane the three readers get is 0, decided on the CPU as well as in the shader.
        CHECK(s.terrainGround.poolingLane(pooling) == 0.0f);
    }
}

TEST_CASE("fogPooling 0 is the ground-following frame, bit for bit", "[gpu][fog][volume][terrain][height]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = std::make_unique<rendering::SceneRenderer>(*ctx, shaders);
    REQUIRE(renderer->init().has_value());

    // Follow on, pooling 0, against the same scene with a terrain whose BASIN is nonsense and whose
    // ground is the valley: at pooling 0 nothing may read the basin channel.
    scene::Scene s = valleyScene(true);
    s.environment.fogGroundFollow = 0.6f;
    const gpu::ImageF a = render(*renderer, s);
    scene::Scene odd = valleyScene(true);
    odd.environment.fogGroundFollow = 0.6f;
    auto field = std::make_shared<world::TerrainHeightField>(*odd.terrainGround.field);
    for (std::size_t k = 0; k < field->basin.size(); ++k) {
        field->basin[k] = 40.0f * std::sin(static_cast<float>(k) * 0.37f) + 17.0f;
    }
    field->hash = 99; // a different upload
    odd.terrainGround.field = field;
    CHECK(identical(a, render(*renderer, odd)));
    // The control: at pooling 1 that basin is read, and the frame moves.
    odd.environment.fogPooling = 1.0f;
    CHECK(pixelsDiffering(a, render(*renderer, odd), 1.0f / 255.0f) > kSize * kSize / 10);
}

TEST_CASE("with a terrain, fogPooling moves the fog in both readers", "[gpu][fog][volume][terrain][height]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = std::make_unique<rendering::SceneRenderer>(*ctx, shaders);
    REQUIRE(renderer->init().has_value());

    struct Arm {
        const char* name;
        float marchReach;
    };
    for (const Arm arm : {Arm{"surface fog only", 0.0f}, Arm{"march only", 100000.0f}}) {
        INFO(arm.name);
        scene::Scene s = valleyScene(true);
        s.environment.volumeMaxDistance = arm.marchReach;
        const gpu::ImageF flat = render(*renderer, s);
        s.environment.fogPooling = 1.0f;
        const gpu::ImageF pooled = render(*renderer, s);
        const std::size_t moved = pixelsDiffering(flat, pooled, 1.0f / 255.0f);
        INFO("pixels moved by more than a level: " << moved << " mean " << meanLuminance(flat) << " -> "
                                                   << meanLuminance(pooled));
        // On a V valley the basin sits above the floor and on the walls (linear under the kernel)
        // is the wall itself, so the layer deepens over the floor and climbs the walls: more fog.
        CHECK(moved > kSize * kSize / 10);
        CHECK(meanLuminance(pooled) > meanLuminance(flat));
    }
}
