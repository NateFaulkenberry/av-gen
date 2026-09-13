// Debug drawing (ADR-031): the geometry builder is pure, so it is tested without a GPU; the
// renderer is exercised through a real device to prove the pipelines compile and draw.

#include "rendering/debug_visualizer.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/skeleton.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

// Renderer forensics Phase 4.3. The plan's rule for every diagnostic is that it must isolate or show
// the thing it names, so each control below is asserted against the case it is *not* for: the world
// axes are not the entity's axes, the submitted filter drops what the cull dropped and keeps what it
// kept, the id colouring changes with the id, and the frustum follows the camera. A control that
// drew the same picture either way would pass a test that only enabled it.
TEST_CASE("The forensic geometry controls each draw only their own case", "[debug][forensics]") {
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available");
    }
    gpu::ShaderLibrary shaders(**ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::DebugDraw draw(**ctx, shaders);
    scene::Scene scene = makeScene();
    scene.camera.position = {0.0f, 2.0f, 10.0f};
    scene.camera.target = {0.0f, 0.0f, 0.0f};

    SECTION("world axes are drawn at the origin, and only when asked for") {
        rendering::DebugViewOptions options;
        rendering::buildDebugGeometry(draw, scene, options, 0.0);
        REQUIRE(draw.empty());

        options.worldAxes = true;
        rendering::buildDebugGeometry(draw, scene, options, 0.0);
        CHECK(draw.lineVertexCount() == 6); // three axes
        CHECK(draw.pointVertexCount() == 1);

        // They are the world's axes, not the selected entity's: they start at zero, they do not
        // move when the entity does, and a selection filter that matches nothing does not remove
        // them. The entity here sits at (2, 0.5, -1), so an origin claim is falsifiable.
        CHECK(draw.pointVertices().front().position == glm::vec3(0.0f));
        for (std::size_t i = 0; i < 6; i += 2) {
            CHECK(draw.lineVertices()[i].position == glm::vec3(0.0f));
        }
        const std::size_t lines = draw.lineVertexCount();
        draw.clear();
        options.selectedEntity = "no-such-entity";
        scene.entities[0].transform.position = {50.0f, 50.0f, 50.0f};
        rendering::buildDebugGeometry(draw, scene, options, 0.0);
        CHECK(draw.lineVertexCount() == lines);
        CHECK(draw.pointVertices().front().position == glm::vec3(0.0f));
    }

    SECTION("the submitted filter follows the cull, in both directions") {
        rendering::DebugViewOptions options;
        options.entityBounds = true;
        options.selectedEntity = "debug-entity";
        rendering::buildDebugGeometry(draw, scene, options, 0.0);
        const std::size_t drawnWhenVisible = draw.lineVertexCount();
        REQUIRE(drawnWhenVisible == 24); // one box

        // Culled, but the filter is off: still drawn, in the culled colour. This is the control --
        // without it the next assertion would pass for a filter that simply drew nothing.
        draw.clear();
        scene.entities[0].cameraCulled = true;
        rendering::buildDebugGeometry(draw, scene, options, 0.0);
        CHECK(draw.lineVertexCount() == drawnWhenVisible);

        draw.clear();
        options.submittedOnly = true;
        rendering::buildDebugGeometry(draw, scene, options, 0.0);
        CHECK(draw.lineVertexCount() == 0);

        draw.clear();
        scene.entities[0].cameraCulled = false;
        rendering::buildDebugGeometry(draw, scene, options, 0.0);
        CHECK(draw.lineVertexCount() == drawnWhenVisible);
    }

    SECTION("entity ids colour by the pick id, so two entities differ and one is stable") {
        scene::Entity& second = scene.addEntity("second", scene.entities[0].mesh);
        second.transform.position = {-3.0f, 0.5f, 1.0f};

        // The control first: with plain bounds, both boxes are the same colour, so a count alone
        // could never tell the two controls apart.
        rendering::DebugViewOptions options;
        options.entityBounds = true;
        rendering::buildDebugGeometry(draw, scene, options, 0.0);
        REQUIRE(draw.lineVertexCount() == 48); // two boxes
        CHECK(draw.lineVertices().front().color == draw.lineVertices()[24].color);

        draw.clear();
        options = rendering::DebugViewOptions{};
        options.entityIds = true;
        rendering::buildDebugGeometry(draw, scene, options, 0.0);
        REQUIRE(draw.lineVertexCount() == 48); // boxed without entityBounds being set
        const glm::vec4 firstColour = draw.lineVertices().front().color;
        const glm::vec4 secondColour = draw.lineVertices()[24].color;
        CHECK(firstColour != secondColour);

        // Culling does not repaint an id: the id view answers "which object is this", and a red box
        // would be a second meaning on the same colour.
        draw.clear();
        scene.entities[0].cameraCulled = true;
        rendering::buildDebugGeometry(draw, scene, options, 0.0);
        CHECK(draw.lineVertices().front().color == firstColour);

        // And the colour is the one the pick id produces, not the loop index: the two agree here
        // only because entity 0 is pick index 0, so the check that matters is the second entity's.
        draw.clear();
        scene.entities[0].cameraCulled = false;
        scene.entities[0].visible = false; // now the second entity is the first thing drawn
        rendering::buildDebugGeometry(draw, scene, options, 0.0);
        REQUIRE(draw.lineVertexCount() == 24);
        CHECK(draw.lineVertices().front().color == secondColour);
    }

    SECTION("the frustum follows the camera and the aspect") {
        rendering::DebugViewOptions options;
        options.frustum = true;
        options.frustumAspect = 16.0f / 9.0f;
        rendering::buildDebugGeometry(draw, scene, options, 0.0);
        REQUIRE(draw.lineVertexCount() == 24 + 6); // twelve edges and the basis
        // The near-plane corners sit at the camera, a near-plane's distance away.
        const glm::vec3 nearCorner = draw.lineVertices().front().position;
        CHECK(std::abs(glm::length(nearCorner - scene.camera.position) - scene.camera.nearPlane) < 0.05f);

        // Move the camera: the box moves with it. Without this the count above would pass for a
        // frustum drawn from the identity.
        draw.clear();
        scene.camera.position = {20.0f, 2.0f, 10.0f};
        rendering::buildDebugGeometry(draw, scene, options, 0.0);
        const glm::vec3 movedCorner = draw.lineVertices().front().position;
        CHECK(glm::length(movedCorner - nearCorner) > 15.0f);

        // Widen the aspect: the far corners spread horizontally and the box is a different shape.
        // Index 2 is the first far-face vertex -- each of the four iterations emits a near edge, a
        // far edge and the connecting edge, in that order.
        const glm::vec3 farCorner = draw.lineVertices()[2].position;
        CHECK(glm::length(farCorner - scene.camera.position) > scene.camera.farPlane * 0.9f);
        draw.clear();
        options.frustumAspect = 4.0f;
        rendering::buildDebugGeometry(draw, scene, options, 0.0);
        CHECK(std::abs(draw.lineVertices()[2].position.x - farCorner.x) > 10.0f);

        // A degenerate projection draws nothing rather than a box at infinity.
        draw.clear();
        scene.camera.nearPlane = scene.camera.farPlane;
        rendering::buildDebugGeometry(draw, scene, options, 0.0);
        CHECK(draw.empty());
    }

    SECTION("the skeleton is drawn from joint positions, not from the GPU palette") {
        // A rig with three joints in a chain, posed so the model-space matrices and the skinning
        // palette have *different* translations. That difference is the whole point: the palette is
        // `model * inverseBind`, so its translation is not where the joint is, and an overlay that
        // read bone positions out of it would draw a plausible skeleton in the wrong place.
        scene::SkinnedRig rig;
        rig.name = "chain";
        rig.skeleton.joints = {
            {"root", -1, {}},
            {"mid", 0, {}},
            {"tip", 1, {}},
        };
        rig.skeleton.joints[1].rest.position = {0.0f, 1.0f, 0.0f};
        rig.skeleton.joints[2].rest.position = {0.0f, 1.0f, 0.0f};
        rig.skeleton.palette = {0, 1, 2};
        rig.skeleton.inverseBind.assign(3, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -5.0f, 0.0f)));
        rig.pose = scene::restPose(rig.skeleton);
        scene::skinningPalette(rig.skeleton, rig.pose, rig.scratchModel, rig.palette);
        REQUIRE(rig.scratchModel.size() == 3);
        // The two really do disagree, or this section proves nothing.
        REQUIRE(std::fabs(rig.scratchModel[2][3][1] - rig.palette[2][3][1]) > 1.0f);

        scene.rigs.push_back(std::move(rig));
        scene::Entity& skinned = scene.addEntity("skinned", scene.entities[0].mesh);
        skinned.rig = 0;
        skinned.transform.position = {10.0f, 0.0f, 0.0f};

        rendering::DebugViewOptions options;
        options.skeletons = true;
        options.selectedEntity = "skinned";
        rendering::buildDebugGeometry(draw, scene, options, 0.0);
        CHECK(draw.pointVertexCount() == 3); // one per joint
        CHECK(draw.lineVertexCount() == 4);  // two bones: root-mid and mid-tip

        // Each joint is at the entity's transform times its model-space matrix. The tip is two
        // units above the root in model space and the entity is ten along x, so this is a claim
        // about both halves of that product.
        std::vector<glm::vec3> drawn;
        for (const rendering::DebugVertex& v : draw.pointVertices()) {
            drawn.push_back(v.position);
        }
        std::sort(drawn.begin(), drawn.end(), [](const glm::vec3& a, const glm::vec3& b) { return a.y < b.y; });
        CHECK(drawn.front() == glm::vec3(10.0f, 0.0f, 0.0f));
        CHECK(drawn.back() == glm::vec3(10.0f, 2.0f, 0.0f));

        // Moving the entity moves the skeleton with it: the overlay is in world space.
        draw.clear();
        scene.entities.back().transform.position = {-4.0f, 3.0f, 0.0f};
        rendering::buildDebugGeometry(draw, scene, options, 0.0);
        CHECK(draw.pointVertices().front().position.x == -4.0f);

        // A rig that has never been evaluated draws nothing rather than a rest pose the frame is
        // not using -- two answers to "where are the joints" is worse than none.
        draw.clear();
        scene.rigs[0].scratchModel.clear();
        rendering::buildDebugGeometry(draw, scene, options, 0.0);
        CHECK(draw.empty());
    }

    SECTION("the trail draws the recorded path and nothing without a history") {
        rendering::DebugViewOptions options;
        options.transformTrail = true;
        rendering::buildDebugGeometry(draw, scene, options, 0.0);
        CHECK(draw.empty()); // no history: nothing claimed

        rendering::TransformHistory history;
        history.setSubject("debug-entity");
        rendering::buildDebugGeometry(draw, scene, options, 0.0, &history);
        CHECK(draw.empty()); // an empty history is not a trail of one point at the origin

        for (int i = 0; i < 5; ++i) {
            rendering::RendererDiagnosticFrame frame;
            frame.frameIndex = static_cast<std::uint64_t>(i);
            frame.viewProjection = glm::mat4(1.0f);
            rendering::RenderObjectDiagnostic object;
            object.name = "debug-entity";
            object.worldPosition = {static_cast<float>(i), 0.0f, 0.0f};
            object.worldMatrix = glm::translate(glm::mat4(1.0f), object.worldPosition);
            frame.objects.push_back(object);
            history.record(frame);
        }
        draw.clear();
        rendering::buildDebugGeometry(draw, scene, options, 0.0, &history);
        CHECK(draw.lineVertexCount() == 8);  // four segments
        CHECK(draw.pointVertexCount() == 5); // one per sample

        // Off again is off: the history staying alive does not keep drawing.
        draw.clear();
        options.transformTrail = false;
        rendering::buildDebugGeometry(draw, scene, options, 0.0, &history);
        CHECK(draw.empty());
    }
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
