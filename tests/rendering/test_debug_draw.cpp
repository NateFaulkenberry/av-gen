// Debug drawing (ADR-031): the geometry builder is pure, so it is tested without a GPU; the
// renderer is exercised through a real device to prove the pipelines compile and draw.

#include "rendering/debug_visualizer.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "scene/mesh_generators.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace avgen;
namespace fs = std::filesystem;

namespace {
scene::Scene makeScene() {
    scene::Scene s;
    const auto mesh = s.addMesh(scene::makeCube(0.5f));
    auto& entity = s.addEntity("debug-entity", mesh);
    entity.transform.position = {2.0f, 0.5f, -1.0f};
    scene::ProceduralGeometry pg;
    pg.name = "grid";
    pg.source.kind = scene::PrimitiveKind::Box;
    pg.distribution.kind = scene::DistributionKind::Grid;
    pg.distribution.gridCount = glm::ivec3(4, 1, 4);
    pg.rebuild();
    s.procedurals.push_back(std::move(pg));

    spatial::FieldSpec field;
    field.name = "swirl";
    field.kind = spatial::FieldKind::Vortex;
    field.strength = 1.0f;
    field.falloff.kind = spatial::FalloffKind::Smoothstep;
    field.falloff.outer = 8.0f;
    s.fields.fields.push_back(field);

    spatial::Spline spline;
    spline.name = "rail";
    spline.generator = spatial::SplineGenerator::Circle;
    spline.radius = 5.0f;
    spline.count = 12;
    s.splines.splines.push_back(spline);
    return s;
}
} // namespace

TEST_CASE("The debug builder emits only what the options ask for", "[debug]") {
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available");
    }
    gpu::ShaderLibrary shaders(**ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    // The builder never touches the GPU, so this instance is intentionally left uninitialised.
    rendering::DebugDraw draw(**ctx, shaders);
    const scene::Scene scene = makeScene();
    rendering::DebugViewOptions options;
    // Nothing enabled: nothing drawn.
    rendering::buildDebugGeometry(draw, scene, options, 0.0);
    CHECK(draw.empty());

    options.points = true;
    rendering::buildDebugGeometry(draw, scene, options, 0.0);
    CHECK(draw.pointVertexCount() == 16); // one per instance
    CHECK(draw.lineVertexCount() == 0);

    draw.clear();
    options.points = false;
    options.bounds = true;
    options.entityBounds = true;
    rendering::buildDebugGeometry(draw, scene, options, 0.0);
    CHECK(draw.lineVertexCount() == 48); // procedural and entity boxes
    CHECK(draw.pointVertexCount() == 0);

    draw.clear();
    options = rendering::DebugViewOptions{};
    options.entityOrigins = true;
    options.selectedEntity = "debug-entity";
    rendering::buildDebugGeometry(draw, scene, options, 0.0);
    CHECK(draw.pointVertexCount() == 1);
    CHECK(draw.lineVertexCount() == 6); // three origin axes

    draw.clear();
    options.bounds = false;
    options.splines = true;
    rendering::buildDebugGeometry(draw, scene, options, 0.0);
    CHECK(draw.lineVertexCount() > 100); // polyline plus frames

    draw.clear();
    options.splines = false;
    options.fieldVectors = true;
    options.fieldGrid = 4;
    rendering::buildDebugGeometry(draw, scene, options, 0.25);
    CHECK(draw.lineVertexCount() > 0); // arrows sampled on the grid

    // The point budget is respected.
    draw.clear();
    options = rendering::DebugViewOptions{};
    options.points = true;
    options.maxPoints = 5;
    rendering::buildDebugGeometry(draw, scene, options, 0.0);
    CHECK(draw.pointVertexCount() == 5);
}

TEST_CASE("Debug drawing uploads and renders through a real device", "[debug][gpu]") {
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available");
    }
    gpu::ShaderLibrary shaders(**ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});

    // A frame layout matching the scene renderer's group 0.
    wgpu::BindGroupLayoutEntry entry{};
    entry.binding = 0;
    entry.visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    entry.buffer.type = wgpu::BufferBindingType::Uniform;
    wgpu::BindGroupLayoutDescriptor layoutDesc{};
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &entry;
    wgpu::BindGroupLayout frameLayout = (*ctx)->device().CreateBindGroupLayout(&layoutDesc);

    rendering::DebugDraw draw(**ctx, shaders);
    REQUIRE(draw.init(wgpu::TextureFormat::RGBA16Float, wgpu::TextureFormat::Depth24Plus, frameLayout).has_value());
    const scene::Scene scene = makeScene();
    rendering::DebugViewOptions options;
    options.points = true;
    options.bounds = true;
    options.splines = true;
    rendering::buildDebugGeometry(draw, scene, options, 0.0);
    CHECK_FALSE(draw.empty());
    draw.upload();
    CHECK((*ctx)->errorCount() == 0);
    // Uploading twice with a bigger set grows the buffer without errors.
    for (int i = 0; i < 40; ++i) {
        rendering::buildDebugGeometry(draw, scene, options, 0.1 * i);
    }
    draw.upload();
    CHECK((*ctx)->errorCount() == 0);
}
