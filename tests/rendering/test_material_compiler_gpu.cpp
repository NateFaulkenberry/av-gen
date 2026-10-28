// ADR-1493: a compiled material program renders what the interpreter renders.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/procedural_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <thread>

using namespace avgen;

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

scene::MaterialOp op(scene::MaterialOpKind kind, int dst, int a = 0, int b = 0, int c = 0) {
    scene::MaterialOp o;
    o.kind = kind;
    o.dst = dst;
    o.srcA = a;
    o.srcB = b;
    o.srcC = c;
    return o;
}

// A program over most op families -- inputs, noise, cells, ramps, remaps, arithmetic, a palette, an OKLab ramp, a
// swizzle -- with a layer on top, on a lit, slowly deformed sphere field.
scene::Scene programScene() {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.camera.position = {0.0f, 3.0f, 9.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.6f));
    key.intensity = 3.0f;
    s.addLight(key);

    scene::MaterialProgram p;
    p.name = "rich";
    scene::MaterialOp world = op(scene::MaterialOpKind::Input, 0);
    world.input = scene::MaterialInput::WorldPosition;
    p.ops.push_back(world);
    scene::MaterialOp noise = op(scene::MaterialOpKind::Noise, 1, 0);
    noise.value = 1.7f;
    p.ops.push_back(noise);
    scene::MaterialOp cells = op(scene::MaterialOpKind::Voronoi, 2, 0);
    cells.value = 2.3f;
    p.ops.push_back(cells);
    scene::MaterialOp ramp = op(scene::MaterialOpKind::Ramp, 3, 1);
    ramp.constant = {0.9f, 0.2f, 0.1f, 0.0f};
    ramp.constant2 = {0.1f, 0.6f, 0.9f, 0.5f};
    ramp.constant3 = {0.95f, 0.9f, 0.3f, 1.0f};
    p.ops.push_back(ramp);
    scene::MaterialOp remap = op(scene::MaterialOpKind::Remap, 4, 2);
    remap.constant = {0.0f, 0.6f, 0.2f, 0.9f};
    remap.value = 1.0f;
    p.ops.push_back(remap);
    p.ops.push_back(op(scene::MaterialOpKind::Multiply, 5, 3, 4));
    scene::MaterialOp fresnel = op(scene::MaterialOpKind::Fresnel, 6);
    fresnel.value = 3.0f;
    p.ops.push_back(fresnel);
    scene::MaterialOp oklab = op(scene::MaterialOpKind::OklabRamp, 7, 6);
    oklab.constant = {0.02f, 0.05f, 0.4f, 1.0f};
    oklab.constant2 = {0.5f, 0.1f, 0.6f, 1.0f};
    oklab.constant3 = {0.9f, 0.4f, 0.2f, 1.0f};
    oklab.constant4 = {1.0f, 0.9f, 0.7f, 1.0f};
    p.ops.push_back(oklab);
    p.ops.push_back(op(scene::MaterialOpKind::Add, 5, 5, 7));
    scene::MaterialOp smooth = op(scene::MaterialOpKind::Smoothstep, 6, 1);
    smooth.constant = {0.3f, 0.7f, 0.0f, 0.0f};
    p.ops.push_back(smooth);
    scene::MaterialOp swizzle = op(scene::MaterialOpKind::Swizzle, 4, 0);
    swizzle.constant = {1.0f, 1.0f, 1.0f, 1.0f};
    p.ops.push_back(swizzle);
    scene::MaterialOp palette = op(scene::MaterialOpKind::Palette, 2, 4);
    palette.constant = {0.5f, 0.5f, 0.5f, 0.0f};
    palette.constant2 = {0.5f, 0.5f, 0.5f, 0.0f};
    palette.constant3 = {1.0f, 1.0f, 1.0f, 0.0f};
    palette.constant4 = {0.0f, 0.33f, 0.67f, 0.0f};
    palette.value = 0.2f;
    p.ops.push_back(palette);
    p.baseColorRegister = 5;
    p.roughnessRegister = 6;
    p.emissionRegister = 2;
    p.emissionIntensity = 0.3f;
    scene::MaterialLayer layer;
    scene::MaterialOp mask = op(scene::MaterialOpKind::Power, 3, 1);
    mask.value = 2.0f;
    layer.ops.push_back(mask);
    layer.maskRegister = 3;
    layer.baseColorRegister = 7;
    layer.blendRange = 0.2f;
    p.layers.push_back(layer);
    s.materialPrograms.push_back(p);

    scene::ProceduralGeometry g;
    g.name = "spheres";
    g.source.kind = scene::PrimitiveKind::Sphere;
    g.source.radius = 1.2f;
    g.source.segments = 48;
    g.source.rings = 24;
    for (int i = 0; i < 6; ++i) {
        scene::InstanceRecord r{};
        r.position = {static_cast<float>(i % 3) * 2.8f - 2.8f, 0.0f, static_cast<float>(i / 3) * 2.8f - 1.4f, 1.0f};
        r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        r.scale = {1.0f, 1.0f, 1.0f, static_cast<float>(i)};
        r.random = {0.1f * static_cast<float>(i), 0.5f, 0.5f, 0.5f};
        r.color = {1.0f, 1.0f, 1.0f, static_cast<float>(i)};
        r.emissive = {1.0f, 1.0f, 1.0f, 0.0f};
        g.instances.push_back(r);
    }
    g.structureVersion = 1;
    g.meshHash = 0x1493;
    g.material.program = "rich";
    g.material.roughness = 0.5f;
    s.procedurals.push_back(g);
    return s;
}

} // namespace

TEST_CASE("A compiled material program renders what the interpreter renders (ADR-1493)",
          "[gpu][procedural][material][adr1493]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const scene::Scene s = programScene();
    FrameTime t{};
    t.renderTime = 1.5;

    rendering::SceneRenderer interpreted(*ctx, shaders);
    REQUIRE(interpreted.init().has_value());
    auto reference = interpreted.renderToImage(s, t, 256, 256);
    REQUIRE(reference.has_value());

    rendering::SceneRenderer compiled(*ctx, shaders);
    REQUIRE(compiled.init().has_value());
    compiled.setCompiledMaterials(rendering::CompiledMaterialsMode::Async);
    auto first = compiled.renderToImage(s, t, 256, 256); // asks for the variant; drawn interpreted meanwhile
    REQUIRE(first.has_value());
    CHECK(gpu::hashImage(*first) == gpu::hashImage(*reference));
    const auto start = std::chrono::steady_clock::now();
    while (compiled.procedurals().pendingCompiles() > 0 &&
           std::chrono::steady_clock::now() - start < std::chrono::seconds(120)) {
        ctx->processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    auto image = compiled.renderToImage(s, t, 256, 256);
    REQUIRE(image.has_value());
    REQUIRE(compiled.procedurals().compiledMaterialsActive());

    // The same arithmetic in the same order; the compiler may contract a multiply-add the interpreter's register
    // file kept apart, so a value can move by one in 255 on a rare pixel -- never more.
    int differing = 0;
    int worst = 0;
    int lit = 0;
    for (std::uint32_t y = 0; y < image->height; ++y) {
        for (std::uint32_t x = 0; x < image->width; ++x) {
            const auto* a = image->pixel(x, y);
            const auto* b = reference->pixel(x, y);
            int d = 0;
            for (int c = 0; c < 3; ++c) {
                d = std::max(d, std::abs(static_cast<int>(a[c]) - static_cast<int>(b[c])));
            }
            differing += d > 0 ? 1 : 0;
            worst = std::max(worst, d);
            lit += (b[0] + b[1] + b[2]) > 30 ? 1 : 0;
        }
    }
    CHECK(lit > 4000);
    CHECK(worst <= 1);
    CHECK(differing < 64);

    // A route on a constant reaches the compiled program with no recompile.
    scene::Scene moved = s;
    moved.materialPrograms[0].ops[3].constant = {0.1f, 0.9f, 0.2f, 0.0f};
    auto movedCompiled = compiled.renderToImage(moved, t, 256, 256);
    REQUIRE(movedCompiled.has_value());
    CHECK(compiled.procedurals().pendingCompiles() == 0);
    CHECK(compiled.procedurals().compiledMaterialsActive());
    auto movedReference = interpreted.renderToImage(moved, t, 256, 256);
    REQUIRE(movedReference.has_value());
    CHECK(gpu::hashImage(*movedCompiled) != gpu::hashImage(*image));
    int movedWorst = 0;
    for (std::uint32_t y = 0; y < movedCompiled->height; ++y) {
        for (std::uint32_t x = 0; x < movedCompiled->width; ++x) {
            for (int c = 0; c < 3; ++c) {
                movedWorst = std::max(movedWorst, std::abs(static_cast<int>(movedCompiled->pixel(x, y)[c]) -
                                                           static_cast<int>(movedReference->pixel(x, y)[c])));
            }
        }
    }
    CHECK(movedWorst <= 1);
    CHECK(ctx->errorCount() == 0);
}

namespace {

// ADR-1812: the same program on entity meshes (the path Aurora's fish, Talking Heads' busts and Rift's rocks take),
// one double-sided and one blended, beside the procedural spheres.
scene::Scene entityProgramScene() {
    scene::Scene s = programScene();
    s.procedurals.clear();
    const scene::MeshId ball = s.addMesh(scene::makeIcosphere(1.2f, 4));
    for (int i = 0; i < 3; ++i) {
        scene::Entity& e = s.addEntity("ball" + std::to_string(i), ball);
        e.transform.position = glm::vec3(static_cast<float>(i) * 2.8f - 2.8f, 0.0f, 0.0f);
        e.material.program = "rich";
        e.material.roughness = 0.5f;
        e.material.doubleSided = i == 1;
        if (i == 2) {
            e.material.alphaMode = scene::AlphaMode::Blend;
            e.material.opacity = 0.6f;
        }
    }
    return s;
}

struct Diff {
    int differing = 0;
    int worst = 0;
    int lit = 0;
};

Diff diff(const gpu::Image8& a, const gpu::Image8& b) {
    Diff d;
    for (std::uint32_t y = 0; y < a.height; ++y) {
        for (std::uint32_t x = 0; x < a.width; ++x) {
            int m = 0;
            for (int c = 0; c < 3; ++c) {
                m = std::max(m, std::abs(static_cast<int>(a.pixel(x, y)[c]) - static_cast<int>(b.pixel(x, y)[c])));
            }
            d.differing += m > 0 ? 1 : 0;
            d.worst = std::max(d.worst, m);
            d.lit += (b.pixel(x, y)[0] + b.pixel(x, y)[1] + b.pixel(x, y)[2]) > 30 ? 1 : 0;
        }
    }
    return d;
}

} // namespace

TEST_CASE("Entity meshes draw their material programs compiled, from the first offline frame (ADR-1812)",
          "[gpu][material][adr1812]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const scene::Scene s = entityProgramScene();
    FrameTime t{};
    t.renderTime = 1.5;

    rendering::SceneRenderer interpreted(*ctx, shaders);
    REQUIRE(interpreted.init().has_value());
    auto reference = interpreted.renderToImage(s, t, 256, 256);
    REQUIRE(reference.has_value());

    // Blocking (an offline render): the first frame waits for the variant, so it is already drawn compiled.
    rendering::SceneRenderer compiled(*ctx, shaders);
    REQUIRE(compiled.init().has_value());
    compiled.setCompiledMaterials(rendering::CompiledMaterialsMode::Blocking);
    auto image = compiled.renderToImage(s, t, 256, 256);
    REQUIRE(image.has_value());
    CHECK(compiled.compiledMaterialsPending() == 0);
    REQUIRE(compiled.compiledMaterialsActive());
    const Diff d = diff(*image, *reference);
    INFO("differing " << d.differing << ", worst " << d.worst << ", lit " << d.lit);
    CHECK(d.lit > 4000);
    CHECK(d.worst <= 1);
    CHECK(d.differing < 64);

    // The `interpreted` diagnostic arm draws the interpreter again, byte for byte, in the same renderer.
    rendering::QualitySettings q = compiled.qualitySettings();
    q.materialProgramsInterpreted = true;
    compiled.setQualitySettings(q);
    auto armed = compiled.renderToImage(s, t, 256, 256);
    REQUIRE(armed.has_value());
    CHECK(gpu::hashImage(*armed) == gpu::hashImage(*reference));

    // A constant moved by a route reaches the compiled program without a rebuild.
    q.materialProgramsInterpreted = false;
    compiled.setQualitySettings(q);
    scene::Scene moved = s;
    moved.materialPrograms[0].ops[3].constant = {0.1f, 0.9f, 0.2f, 0.0f};
    auto movedCompiled = compiled.renderToImage(moved, t, 256, 256);
    REQUIRE(movedCompiled.has_value());
    CHECK(compiled.compiledMaterialsPending() == 0);
    auto movedReference = interpreted.renderToImage(moved, t, 256, 256);
    REQUIRE(movedReference.has_value());
    CHECK(gpu::hashImage(*movedCompiled) != gpu::hashImage(*image));
    CHECK(diff(*movedCompiled, *movedReference).worst <= 1);
    CHECK(ctx->errorCount() == 0);
}
