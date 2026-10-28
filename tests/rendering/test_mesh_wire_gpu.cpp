// ADR-1862: the mesh wire on pixels. Three claims only a GPU can settle:
//   * the GATE: an inactive block (nothing to show) renders the same bytes as no block, and issues no draw;
//   * an active block draws the target mesh's edges, and only the target's;
//   * lines are depth-tested: a wall in front of the mesh hides its lines there and only there.

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
#include <cstdlib>
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
    t.renderTime = 1.0;
    renderer.resetTemporalHistory();
    auto first = renderer.renderToImage(s, t, kWidth, kHeight);
    REQUIRE(first.has_value());
    auto img = renderer.renderToImage(s, t, kWidth, kHeight);
    REQUIRE(img.has_value());
    return std::move(*img);
}

std::size_t changed(const gpu::Image8& a, const gpu::Image8& b, std::uint32_t x0 = 0, std::uint32_t x1 = kWidth) {
    std::size_t n = 0;
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            int sum = 0;
            for (int c = 0; c < 3; ++c) {
                sum += std::abs(static_cast<int>(a.pixel(x, y)[c]) - static_cast<int>(b.pixel(x, y)[c]));
            }
            n += sum > 24 ? 1 : 0;
        }
    }
    return n;
}

// A dark stage: a coarse ground plane ("ground", 20 m, 4 x 4 cells) the camera looks down onto, and a
// second plane ("other") off to the side that the wire does not target.
scene::Scene stage() {
    scene::Scene s;
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.5f));
    key.intensity = 0.3f;
    s.addLight(key);
    s.camera.position = glm::vec3(0.0f, 8.0f, 12.0f);
    s.camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
    s.camera.fovYRadians = glm::radians(60.0f);
    s.environment.backgroundColor = glm::vec3(0.01f, 0.012f, 0.02f);
    const scene::MeshId plane = s.addMesh(scene::makePlane(10.0f, 4));
    scene::Entity& ground = s.addEntity("ground", plane);
    ground.material.baseColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f);
    return s;
}

void wire(scene::Scene& s, float intensity) {
    s.meshWire.targets = {"ground"};
    s.meshWire.color = glm::vec3(1.0f, 0.5f, 0.2f);
    s.meshWire.intensity = intensity;
    s.meshWire.reveal = 1.0f;
    s.meshWire.width = 2.0f;
    s.meshWire.minPixels = 0.0f;
    s.meshWire.fadeNear = 1000.0f;
    s.meshWire.fadeFar = 2000.0f;
}

} // namespace

TEST_CASE("mesh wire: the gate -- an inactive block renders the bytes of no block and draws nothing",
          "[gpu][mesh-wire][adr1862]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    const scene::Scene bare = stage();
    scene::Scene off = stage();
    wire(off, 0.0f); // targets, but nothing to show
    REQUIRE_FALSE(off.meshWire.active());
    const gpu::Image8 a = render(renderer, bare);
    const gpu::Image8 b = render(renderer, off);
    CHECK(renderer.meshWire().stats().draws == 0);
    CHECK(a.rgba == b.rgba);
}

TEST_CASE("mesh wire: an active block draws the target's edges, and a wall in front hides them there",
          "[gpu][mesh-wire][adr1862]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene bare = stage();
    scene::Scene lit = stage();
    wire(lit, 4.0f);
    const gpu::Image8 none = render(renderer, bare);
    const gpu::Image8 lines = render(renderer, lit);
    CHECK(renderer.meshWire().stats().draws == 1);
    CHECK(renderer.meshWire().stats().triangles == 32u); // 4 x 4 cells, two triangles each
    const std::size_t drawn = changed(none, lines);
    INFO("pixels the lines changed: " << drawn);
    CHECK(drawn > 200);
    CHECK(drawn < kWidth * kHeight / 3); // lines, not a fill

    // A target that names nothing draws nothing.
    scene::Scene elsewhere = lit;
    elsewhere.meshWire.targets = {"nothing"};
    static_cast<void>(render(renderer, elsewhere));
    CHECK(renderer.meshWire().stats().draws == 0);

    // A wall between the camera and the left half of the ground hides the lines there and only there.
    const auto addWall = [](scene::Scene& s) {
        const scene::MeshId cube = s.addMesh(scene::makeCube(1.0f));
        scene::Entity& wall = s.addEntity("wall", cube);
        wall.transform.position = glm::vec3(-3.5f, 5.0f, 7.0f);
        wall.transform.scale = glm::vec3(3.5f, 4.0f, 0.2f);
        wall.material.baseColor = glm::vec4(0.05f, 0.05f, 0.05f, 1.0f);
    };
    scene::Scene walled = bare;
    addWall(walled);
    scene::Scene walledLit = lit;
    addWall(walledLit);
    const gpu::Image8 w0 = render(renderer, walled);
    const gpu::Image8 w1 = render(renderer, walledLit);
    // Where the wall is drawn (the walled frame differs from the bare one), the lines must change nothing; where
    // the ground is open (no wall) they must change a lot.
    std::size_t behind = 0, open = 0, wallPixels = 0;
    const auto differs = [](const gpu::Image8& p, const gpu::Image8& q, std::uint32_t x, std::uint32_t y) {
        int sum = 0;
        for (int c = 0; c < 3; ++c) {
            sum += std::abs(static_cast<int>(p.pixel(x, y)[c]) - static_cast<int>(q.pixel(x, y)[c]));
        }
        return sum > 24;
    };
    for (std::uint32_t y = 2; y + 2 < kHeight; ++y) {
        for (std::uint32_t x = 2; x + 2 < kWidth; ++x) {
            bool wallHere = true; // the wall here and on every neighbour (not its rim, where a line may graze)
            for (int dy = -2; dy <= 2 && wallHere; ++dy) {
                for (int dx = -2; dx <= 2 && wallHere; ++dx) {
                    wallHere = differs(none, w0, x + dx, y + dy);
                }
            }
            const bool lineHere = differs(w0, w1, x, y);
            if (wallHere) {
                ++wallPixels;
                behind += lineHere ? 1 : 0;
            } else if (!differs(none, w0, x, y)) {
                open += lineHere ? 1 : 0;
            }
        }
    }
    INFO("wall pixels " << wallPixels << ": lines there " << behind << "; lines on open ground " << open);
    CHECK(wallPixels > 1000);
    CHECK(open > 200);
    CHECK(behind == 0);
}
