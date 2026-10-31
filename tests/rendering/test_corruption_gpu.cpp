// ADR-1873: the corruption on pixels. Two claims only a GPU can settle:
//   * the GATE: a block that does not act (no field, the opening shut) renders the bytes of no block;
//   * acting, it moves the target's own geometry (the picture changes where the rock is, and only for the target).

#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <filesystem>
#include <memory>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 240;
constexpr std::uint32_t kHeight = 135;

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

gpu::Image8 render(rendering::SceneRenderer& renderer, const scene::Scene& s) {
    FrameTime t{};
    t.renderTime = 2.0;
    renderer.resetTemporalHistory();
    auto first = renderer.renderToImage(s, t, kWidth, kHeight);
    REQUIRE(first.has_value());
    auto img = renderer.renderToImage(s, t, kWidth, kHeight);
    REQUIRE(img.has_value());
    return std::move(*img);
}

std::size_t changed(const gpu::Image8& a, const gpu::Image8& b) {
    std::size_t n = 0;
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            int sum = 0;
            for (int c = 0; c < 3; ++c) {
                sum += std::abs(static_cast<int>(a.pixel(x, y)[c]) - static_cast<int>(b.pixel(x, y)[c]));
            }
            n += sum > 12 ? 1 : 0;
        }
    }
    return n;
}

// A lit ground the camera looks down onto, dense enough for the corruption to move metre-scale blocks of it.
scene::Scene stage() {
    scene::Scene s;
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    key.intensity = 2.0f;
    s.addLight(key);
    s.camera.position = glm::vec3(0.0f, 9.0f, 9.0f);
    s.camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
    s.camera.fovYRadians = glm::radians(60.0f);
    s.environment.backgroundColor = glm::vec3(0.01f, 0.012f, 0.02f);
    const scene::MeshId plane = s.addMesh(scene::makePlane(10.0f, 60));
    scene::Entity& ground = s.addEntity("ground", plane);
    ground.material.baseColor = glm::vec4(0.6f, 0.4f, 0.3f, 1.0f);
    ground.corruptTarget = true; // what the Composition marks for `corruption.targets`
    return s;
}

void corrupt(scene::Scene& s, float amount, float open) {
    scene::Corruption& c = s.corruption;
    c.targets = {"ground"};
    c.focus = glm::vec3(0.0f);
    c.axis = glm::vec3(0.0f, -1.0f, 0.0f);
    c.radius = 8.0f;
    c.inner = 0.3f;
    c.block = 1.0f;
    c.share = 0.9f;
    c.slip = 0.6f;
    c.lift = 1.2f; // out of the ground: the blocks stand up and throw shadows
    c.strip = 1.0f;
    c.stripSlip = 0.3f;
    c.pull = 0.5f;
    c.width = 2.0f;
    c.height = 2.0f;
    c.depth = 1.5f;
    c.amount = amount;
    c.liveAmount = amount;
    c.liveOpen = open;
    c.prevTime = 2.0f;
}

} // namespace

TEST_CASE("corruption: the gate -- a block that does not act renders the bytes of no block", "[gpu][corruption][adr1873]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    scene::Scene bare = stage();
    bare.entities.back().corruptTarget = false;
    scene::Scene idle = stage();
    corrupt(idle, 0.0f, 0.0f);
    REQUIRE_FALSE(idle.corruption.active());
    const gpu::Image8 a = render(renderer, bare);
    const gpu::Image8 b = render(renderer, idle);
    CHECK(a.rgba == b.rgba);
}

TEST_CASE("corruption: acting, it moves the target's geometry, and opens it", "[gpu][corruption][adr1873]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    const gpu::Image8 rest = render(renderer, stage());
    scene::Scene field = stage();
    corrupt(field, 1.0f, 0.0f);
    REQUIRE(field.corruption.active());
    const gpu::Image8 moved = render(renderer, field);
    const std::size_t byField = changed(rest, moved);
    INFO("pixels the field changed: " << byField);
    CHECK(byField > 300); // a slip along a flat plane is invisible; the lifted blocks are what shows
    scene::Scene opened = field;
    opened.corruption.liveOpen = 1.0f;
    const gpu::Image8 open = render(renderer, opened);
    const std::size_t byOpening = changed(moved, open);
    INFO("pixels the opening changed: " << byOpening);
    CHECK(byOpening > 300);
    // the same scene with the target unmarked: the field reaches nothing
    scene::Scene untargeted = field;
    untargeted.entities.back().corruptTarget = false;
    CHECK(changed(rest, render(renderer, untargeted)) == 0);
}

// ADR-1879: the portal view on pixels -- inside the opening, the other world; outside it, this one; crossed, the other
// world everywhere.
TEST_CASE("portal: the other world shows inside the opening, and fills the frame once crossed", "[gpu][portal][adr1879]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    // this world: a dark wall facing the camera
    scene::Scene here;
    here.camera.position = glm::vec3(0.0f, 0.0f, 12.0f);
    here.camera.target = glm::vec3(0.0f);
    here.camera.fovYRadians = glm::radians(60.0f);
    here.environment.backgroundColor = glm::vec3(0.0f);
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(0.0f, -0.3f, -1.0f));
    key.intensity = 1.0f;
    here.addLight(key);
    const scene::MeshId plane = here.addMesh(scene::makePlane(20.0f, 20));
    scene::Entity& wall = here.addEntity("wall", plane);
    wall.transform.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    wall.material.baseColor = glm::vec4(0.05f, 0.05f, 0.05f, 1.0f);
    wall.corruptTarget = true;
    scene::Corruption& c = here.corruption;
    c.targets = {"wall"};
    c.focus = glm::vec3(0.0f);
    c.axis = glm::vec3(0.0f, 0.0f, -1.0f);
    c.width = 3.0f;
    c.height = 3.0f;
    c.depth = 0.0f;
    c.liveOpen = 1.0f;
    // the other world: nothing but a bright background
    scene::Scene there;
    there.camera = here.camera;
    there.environment.backgroundColor = glm::vec3(0.9f, 0.3f, 0.1f);
    const gpu::Image8 alone = render(renderer, here);
    here.portal.world = &there;
    const gpu::Image8 opened = render(renderer, here);
    const auto centre = opened.pixel(kWidth / 2, kHeight / 2);
    const auto corner = opened.pixel(4, 4);
    INFO("centre " << int(centre[0]) << "," << int(centre[1]) << "," << int(centre[2]));
    CHECK(centre[0] > centre[2] + 60); // the other world's orange
    CHECK(corner[0] == alone.pixel(4, 4)[0]); // this world outside the opening
    here.portal.crossed = true;
    const gpu::Image8 crossed = render(renderer, here);
    CHECK(crossed.pixel(4, 4)[0] > crossed.pixel(4, 4)[2] + 60);
    // Gate C: portals nest. The other world holds a portal of its own, already crossed: the frame is the third world's
    scene::Scene third;
    third.camera = here.camera;
    third.environment.backgroundColor = glm::vec3(0.1f, 0.8f, 0.2f);
    there.portal.world = &third;
    there.portal.crossed = true;
    const gpu::Image8 nested = render(renderer, here);
    const auto px = nested.pixel(4, 4);
    INFO("nested corner " << int(px[0]) << "," << int(px[1]) << "," << int(px[2]));
    CHECK(px[1] > px[0] + 40); // the tonemap desaturates a pure green
    CHECK(px[1] > px[2] + 40);
}
