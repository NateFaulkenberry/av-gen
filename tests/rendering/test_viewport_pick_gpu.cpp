// Viewport picking on a real device (ADR-068).
//
// Picking reuses two targets the scene pass already writes: the identifier target says which entity
// covered a pixel, and the linear-depth target says how far away its surface was. What has to hold
// is that those two answers agree with the geometry that was drawn -- a pick that returns a
// plausible-looking position which is not actually on the object is exactly the kind of bug that
// survives a demo and ruins placement, because everything lands slightly in front of or behind
// where it was dropped.
//
// So these tests render boxes at known positions and check the reconstructed world position against
// the geometry, not against a golden number.

#include "app/viewport_pick.hpp"
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>
#include <memory>

using namespace avgen;

namespace {

constexpr std::uint32_t kWidth = 400;
constexpr std::uint32_t kHeight = 300;

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

scene::MeshData boxMesh(float h) {
    scene::MeshData m;
    const glm::vec3 normals[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (const glm::vec3 normal : normals) {
        const glm::vec3 u = std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::cross(glm::vec3(0, 1, 0), normal);
        const glm::vec3 v = glm::cross(normal, u);
        const auto base = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({normal * h - u * h - v * h, normal, {0, 0}});
        m.vertices.push_back({normal * h + u * h - v * h, normal, {1, 0}});
        m.vertices.push_back({normal * h + u * h + v * h, normal, {1, 1}});
        m.vertices.push_back({normal * h - u * h + v * h, normal, {0, 1}});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return m;
}

// One unit box centred on the origin, seen head-on from +Z. The near face is therefore the plane
// z = 1, which is a fact the reconstruction has to reproduce.
scene::Scene oneBoxScene() {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.camera.position = {0.0f, 0.0f, 6.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.camera.nearPlane = 0.1f;
    s.camera.farPlane = 200.0f;
    const auto mesh = s.addMesh(boxMesh(1.0f));
    auto& e = s.addEntity("box", mesh);
    e.material.baseColor = glm::vec3(0.7f);
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    key.intensity = 3.0f;
    s.addLight(key);
    return s;
}

app::PickView viewFor(const scene::Scene& s) {
    app::PickView view;
    view.size = glm::uvec2(kWidth, kHeight);
    const float aspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
    view.invViewProj = glm::inverse(s.camera.projection(aspect) * s.camera.view());
    view.cameraPosition = s.camera.position;
    view.cameraForward = glm::normalize(s.camera.target - s.camera.position);
    return view;
}

void renderOnce(rendering::SceneRenderer& renderer, const scene::Scene& s) {
    FrameTime time{};
    time.renderTime = 0.0;
    time.deltaTime = 1.0 / 60.0;
    time.frameIndex = 0;
    REQUIRE(renderer.renderFrame(s, time, kWidth, kHeight).has_value());
}
} // namespace

TEST_CASE("A pick lands on the surface that was drawn", "[viewport][pick]") {
    auto context = makeContext();
    gpu::ShaderLibrary shaders(*context, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*context, shaders);
    REQUIRE(renderer.init().has_value());

    const scene::Scene s = oneBoxScene();
    renderOnce(renderer, s);
    const app::PickView view = viewFor(s);

    const glm::uvec2 centre(kWidth / 2, kHeight / 2);
    auto hit = app::pickAt(*context, renderer.identifierTexture(), renderer.linearDepthTexture(), view, centre);
    REQUIRE(hit.has_value());
    REQUIRE(hit->hit);

    // The camera is six units out on +Z looking at the origin, and the box's near face is the plane
    // z = 1. Five units of travel, and the reconstructed point is on that face.
    CHECK_THAT(hit->distance, Catch::Matchers::WithinAbs(5.0, 0.05));
    CHECK_THAT(hit->position.z, Catch::Matchers::WithinAbs(1.0, 0.05));
    CHECK_THAT(hit->position.x, Catch::Matchers::WithinAbs(0.0, 0.05));
    CHECK_THAT(hit->position.y, Catch::Matchers::WithinAbs(0.0, 0.05));
    // The one entity in the scene.
    CHECK(hit->objectId == 0u);
}

TEST_CASE("Picking off-centre corrects for the angle of the ray", "[viewport][pick]") {
    auto context = makeContext();
    gpu::ShaderLibrary shaders(*context, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*context, shaders);
    REQUIRE(renderer.init().has_value());

    // A box wide enough to cover the frame, so every pixel lands on one flat plane at z = 1. That
    // makes the test independent of where exactly the geometry is: whatever the ray angle, the
    // reconstructed z must be the same.
    scene::Scene s = oneBoxScene();
    s.entities[0].transform.scale = glm::vec3(8.0f, 8.0f, 1.0f);
    renderOnce(renderer, s);
    const app::PickView view = viewFor(s);

    // The linear-depth target holds distance along the camera's forward axis, not along the ray.
    // Travelling that distance *along the ray* instead lands short by a cosine that is zero at the
    // centre of frame and grows toward the corners -- so a missing division here reads as "picking
    // is slightly off near the edges" rather than as an obviously wrong result.
    for (const glm::uvec2 pixel : {glm::uvec2(kWidth / 2, kHeight / 2), glm::uvec2(20, 20),
                                   glm::uvec2(kWidth - 20, 20), glm::uvec2(20, kHeight - 20),
                                   glm::uvec2(kWidth - 20, kHeight - 20)}) {
        auto hit = app::pickAt(*context, renderer.identifierTexture(), renderer.linearDepthTexture(), view, pixel);
        REQUIRE(hit.has_value());
        INFO("pixel " << pixel.x << "," << pixel.y);
        REQUIRE(hit->hit);
        CHECK_THAT(hit->position.z, Catch::Matchers::WithinAbs(1.0, 0.05));
    }

    // The corners of a flat plane really are further from the camera than its centre along the ray,
    // so this is not a test that would pass on a constant.
    auto centre = app::pickAt(*context, renderer.identifierTexture(), renderer.linearDepthTexture(), view,
                              glm::uvec2(kWidth / 2, kHeight / 2));
    auto corner = app::pickAt(*context, renderer.identifierTexture(), renderer.linearDepthTexture(), view,
                              glm::uvec2(20, 20));
    REQUIRE(centre.has_value());
    REQUIRE(corner.has_value());
    CHECK(glm::length(corner->position - view.cameraPosition) >
          glm::length(centre->position - view.cameraPosition) + 0.05f);
}

TEST_CASE("Clicking the sky is a miss, not an error", "[viewport][pick]") {
    auto context = makeContext();
    gpu::ShaderLibrary shaders(*context, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*context, shaders);
    REQUIRE(renderer.init().has_value());

    const scene::Scene s = oneBoxScene();
    renderOnce(renderer, s);
    const app::PickView view = viewFor(s);

    // The top-left corner: the box is one unit across, six units away, nowhere near it.
    auto miss = app::pickAt(*context, renderer.identifierTexture(), renderer.linearDepthTexture(), view,
                            glm::uvec2(2, 2));
    REQUIRE(miss.has_value());   // clicking nothing is ordinary, and means "deselect"
    CHECK(!miss->hit);
}

TEST_CASE("Picking outside the target is refused", "[viewport][pick]") {
    auto context = makeContext();
    gpu::ShaderLibrary shaders(*context, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*context, shaders);
    REQUIRE(renderer.init().has_value());

    const scene::Scene s = oneBoxScene();
    renderOnce(renderer, s);
    const app::PickView view = viewFor(s);

    // Out of range is a caller error rather than a miss: it means the pixel was computed wrongly,
    // most likely by forgetting that SDL reports points and the targets are in pixels.
    CHECK(!app::pickAt(*context, renderer.identifierTexture(), renderer.linearDepthTexture(), view,
                       glm::uvec2(kWidth, 10))
               .has_value());
}

TEST_CASE("A pick says which numbering its id belongs to", "[viewport][pick]") {
    // The end of the bug this encodes. Three renderers write into one 16-bit object id and each
    // counts from zero, so the number alone cannot say what it counts. The picker resolved every id
    // as an entity index -- and a scene like Glowmere is almost entirely procedural, so almost every
    // click selected whichever node owned that entity, or nothing.
    //
    // Drawn with no entity at all, so the procedural's id is 0 in its own numbering. Before the tag
    // that was indistinguishable from "entity 0", which is precisely the collision.
    auto context = makeContext();
    gpu::ShaderLibrary shaders(*context, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*context, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.camera.position = {0.0f, 0.0f, 6.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.camera.nearPlane = 0.1f;
    s.camera.farPlane = 200.0f;
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    key.intensity = 3.0f;
    s.addLight(key);

    // Instances built here rather than described: the *distribution* is evaluated by the
    // Composition, and the renderer draws the records it is handed. A bare description renders
    // nothing, which is how the first version of this test came to report a miss.
    scene::ProceduralGeometry pg;
    pg.name = "scattered";
    pg.source.kind = scene::PrimitiveKind::Box;
    pg.source.size = {2.0f, 2.0f, 2.0f};
    pg.source.subdivisions = 1;
    pg.meshHash = 0xBEEF1234ull;
    pg.structureVersion = 1;
    pg.material.baseColor = {0.8f, 0.7f, 0.6f};
    scene::InstanceRecord r{};
    r.position = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    r.scale = {1.0f, 1.0f, 1.0f, 0.0f};
    r.random = {0.25f, 0.5f, 0.75f, 0.125f};
    r.color = {1.0f, 1.0f, 1.0f, 0.0f};
    r.emissive = {1.0f, 1.0f, 1.0f, 0.0f};
    pg.instances.push_back(r);
    s.procedurals.push_back(std::move(pg));
    REQUIRE(s.entities.empty());

    // A few frames: the procedural path builds its buffers and culls on the GPU, and its stats lag
    // a frame or two behind the draw.
    for (int i = 0; i < 4; ++i) {
        renderOnce(renderer, s);
    }
    // No stats gate here: `visibleInstances` counts what survived the *cull* pass, and this object
    // is not culled, so it draws while that number stays zero. The pick is the assertion.
    const app::PickView view = viewFor(s);
    auto hit = app::pickAt(*context, renderer.identifierTexture(), renderer.linearDepthTexture(),
                           view, glm::uvec2(kWidth / 2, kHeight / 2));
    REQUIRE(hit.has_value());
    REQUIRE(hit->hit);

    INFO("objectId " << hit->objectId << " -> space "
                     << static_cast<int>(scene::pickSpaceOf(hit->objectId)) << " index "
                     << scene::pickIndexOf(hit->objectId));
    CHECK(scene::pickSpaceOf(hit->objectId) == scene::PickSpace::Procedural);
    CHECK(scene::pickIndexOf(hit->objectId) == 0u);
    // And it is not the bare index it used to be, or nothing has changed where it counts.
    CHECK(hit->objectId != 0u);
}
