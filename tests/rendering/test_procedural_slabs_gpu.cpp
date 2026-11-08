// ADR-1959: the slab fast path draws what the general path draws. A lattice of boxes at varied heights (and a few
// turned about y) is rendered twice -- `slabs` off and on -- from above, from the side and from below, with a
// shadow-casting key and the depth prepass, and the two frames must agree. They are not bit-identical by design: a
// turned quad covers the same face as makeBox's opposite face but splits it along the other diagonal, and its uv runs
// the other way; neither changes a lit, untextured surface by more than rounding. What would show here is a wrong face
// (a hole where a side should be), a face turned to the camera in a shadow view (light leaking through the casters),
// or a depth disagreement between the prepass and the lit pass (missing surfaces).

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/ao_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <vector>

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

scene::ProceduralGeometry lattice(bool slabs) {
    scene::ProceduralGeometry g;
    g.name = "tiles";
    g.slabs = slabs;
    g.source.kind = scene::PrimitiveKind::Box;
    g.source.size = {0.9f, 0.3f, 0.9f};
    const int n = 12;
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            scene::InstanceRecord r{};
            const float x = static_cast<float>(i) - 0.5f * static_cast<float>(n - 1);
            const float z = static_cast<float>(j) - 0.5f * static_cast<float>(n - 1);
            const float y = 0.8f * std::sin(0.6f * x) * std::cos(0.45f * z);
            r.position = {x, y, z, 1.0f};
            // every seventh tile turned about y, so the side choice is tested off the world axes too
            const glm::quat q = ((i + j * n) % 7 == 0) ? glm::angleAxis(0.5f, glm::vec3(0.0f, 1.0f, 0.0f))
                                                       : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            r.rotation = {q.x, q.y, q.z, q.w};
            r.scale = {1.0f, 1.0f, 1.0f, static_cast<float>(i + j * n) / static_cast<float>(n * n - 1)};
            r.color = {1.0f, 1.0f, 1.0f, static_cast<float>(i + j * n)};
            r.emissive = {1.0f, 1.0f, 1.0f, 0.0f};
            g.instances.push_back(r);
        }
    }
    g.structureVersion = 1;
    g.meshHash = 0x5EED1959u;
    g.material.baseColor = {0.8f, 0.6f, 0.7f};
    g.material.roughness = 0.5f;
    return g;
}

scene::Scene latticeScene(bool slabs, glm::vec3 eye) {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.environment.depthPrepass = true;
    s.camera.position = eye;
    s.camera.target = {0.0f, 0.0f, 0.0f};
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.5f, -0.6f, -0.35f));
    key.intensity = 3.0f;
    key.castsShadow = true;
    s.addLight(key);
    s.procedurals.push_back(lattice(slabs));
    return s;
}

gpu::Image8 render(gpu::Context& ctx, const scene::Scene& s) {
    gpu::ShaderLibrary shaders(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime t{};
    t.renderTime = 1.0;
    // Large enough that every tile is well over 8 px wide: under that, the slab path deliberately shades a far side
    // like its top (ADR-1959's amendment), and the two paths should differ there.
    auto img = renderer.renderToImage(s, t, 960, 720);
    REQUIRE(img.has_value());
    return *img;
}

struct Diff {
    int lit = 0;       // pixels the general path drew (not background)
    int differ = 0;    // pixels whose channels differ by more than 6 levels
};

Diff compare(const gpu::Image8& a, const gpu::Image8& b) {
    Diff d;
    for (std::uint32_t y = 0; y < a.height; ++y) {
        for (std::uint32_t x = 0; x < a.width; ++x) {
            const auto* p = a.pixel(x, y);
            const auto* q = b.pixel(x, y);
            if (p[0] + p[1] + p[2] > 15) {
                ++d.lit;
            }
            for (int c = 0; c < 3; ++c) {
                if (std::abs(static_cast<int>(p[c]) - static_cast<int>(q[c])) > 6) {
                    ++d.differ;
                    break;
                }
            }
        }
    }
    return d;
}

} // namespace

TEST_CASE("The slab fast path draws the box lattice the general path draws", "[gpu][procedural][geography][adr1959]") {
    auto ctx = makeContext();
    const std::vector<glm::vec3> eyes{
        {3.0f, 9.0f, 11.0f},   // above, oblique: tops and two sides
        {-12.0f, 1.5f, -4.0f}, // low from the other quadrant: the opposite sides
        {2.0f, -6.0f, 9.0f},   // below: the bottoms
    };
    for (const glm::vec3& eye : eyes) {
        CAPTURE(eye.x, eye.y, eye.z);
        const auto general = render(*ctx, latticeScene(false, eye));
        const auto slab = render(*ctx, latticeScene(true, eye));
        const Diff d = compare(general, slab);
        CAPTURE(d.lit, d.differ);
        CHECK(d.lit > 4000);
        // Rounding along a face diagonal and at silhouette edges only: under half a percent of the frame.
        CHECK(d.differ < static_cast<int>(general.width * general.height / 200));
    }
    CHECK(ctx->errorCount() == 0);
}

namespace {

scene::Scene ringScene(float farPixels) {
    scene::ProceduralGeometry pg;
    pg.name = "ring";
    pg.slabs = true;
    pg.slabFarPixels = farPixels;
    pg.source.kind = scene::PrimitiveKind::Box;
    pg.source.size = {0.8f, 0.2f, 0.8f}; // a wide gap: 20% of the pitch
    pg.distribution.kind = scene::DistributionKind::Generator;
    scene::GeneratorSpec& g = pg.distribution.generator;
    g.name = scene::kGeneratorTiles;
    g.cellSize = 1.0f;
    g.viewDistance = 40.0f;
    g.align = 4;
    g.tiles.hillHeight = 0.0f; // flat: only the gaps differ between the two renders
    g.tiles.shoreRise = 0.0f;
    g.tiles.landThreshold = 0.0f;
    REQUIRE(pg.validate().has_value());
    REQUIRE(pg.rebuild());
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.camera.position = {0.3f, 30.0f, 0.3f};
    s.camera.target = {0.3f, 0.0f, -0.2f}; // looking down: every tile a few dozen pixels across
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.2f));
    key.intensity = 3.0f;
    s.addLight(key);
    s.procedurals.push_back(pg);
    return s;
}

int blackPixels(const gpu::Image8& img) {
    int n = 0;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const auto* p = img.pixel(x, y);
            n += (p[0] + p[1] + p[2]) <= 6 ? 1 : 0;
        }
    }
    return n;
}

} // namespace

TEST_CASE("The far-tile rule closes a ring's gaps below its pixel threshold", "[gpu][procedural][geography][adr1959]") {
    // Tiles about 10 px across: with the rule off (0) the 20% gaps show the black background; with a threshold far
    // above the tiles' size (1000 px) every footprint eases out to the pitch and the gaps close.
    auto ctx = makeContext();
    const auto open = render(*ctx, ringScene(0.0f));
    const auto closed = render(*ctx, ringScene(1000.0f));
    const int openBlack = blackPixels(open);
    const int closedBlack = blackPixels(closed);
    CAPTURE(openBlack, closedBlack);
    CHECK(openBlack > 5000);
    CHECK(closedBlack < openBlack / 10);
    CHECK(ctx->errorCount() == 0);
}

namespace {

scene::Scene steppedScene(bool ao, float fadeStart, float fadeEnd) {
    scene::ProceduralGeometry pg;
    pg.name = "steps";
    pg.slabs = true;
    pg.source.kind = scene::PrimitiveKind::Box;
    pg.source.size = {1.0f, 0.3f, 1.0f};
    pg.distribution.kind = scene::DistributionKind::Generator;
    scene::GeneratorSpec& g = pg.distribution.generator;
    g.name = scene::kGeneratorTiles;
    g.cellSize = 1.0f;
    g.viewDistance = 48.0f;
    g.align = 4;
    g.skirt = 3.0f;
    g.tiles.landThreshold = 0.0f;  // all land
    g.tiles.hillWavelength = 30.0f; // steep enough for many steps in view
    g.tiles.hillHeight = 12.0f;
    REQUIRE(pg.validate().has_value());
    REQUIRE(pg.rebuild());
    scene::Scene s;
    s.environment.backgroundColor = {0.2f, 0.3f, 0.5f};
    s.environment.showSkybox = false;
    s.environment.ambientOcclusion = ao;
    s.environment.aoFadeStart = fadeStart;
    s.environment.aoFadeEnd = fadeEnd;
    s.camera.position = {0.5f, 26.0f, 20.0f};
    s.camera.target = {0.5f, 6.0f, -10.0f};
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    key.intensity = 3.0f;
    s.addLight(key);
    s.procedurals.push_back(pg);
    return s;
}

double meanDifference(const gpu::Image8& a, const gpu::Image8& b) {
    double sum = 0.0;
    for (std::uint32_t y = 0; y < a.height; ++y) {
        for (std::uint32_t x = 0; x < a.width; ++x) {
            const auto* p = a.pixel(x, y);
            const auto* q = b.pixel(x, y);
            for (int c = 0; c < 3; ++c) {
                sum += std::abs(static_cast<int>(p[c]) - static_cast<int>(q[c]));
            }
        }
    }
    return sum / (3.0 * a.width * a.height);
}

} // namespace

TEST_CASE("The AO distance fade eases occlusion out with depth and is off by default", "[gpu][ao][geography]") {
    // The CPU twin of the shader's fade.
    CHECK(rendering::aoDistanceFade(50.0f, 0.0f, 0.0f) == 0.0f);    // off
    CHECK(rendering::aoDistanceFade(5.0f, 10.0f, 30.0f) == 0.0f);   // before the band: full AO
    CHECK(rendering::aoDistanceFade(40.0f, 10.0f, 30.0f) == 1.0f);  // past it: none
    CHECK(rendering::aoDistanceFade(20.0f, 10.0f, 30.0f) == Catch::Approx(0.5f));
    // On the GPU: a stepped slab field, whose risers GTAO reads as creases.
    auto ctx = makeContext();
    const auto off = render(*ctx, steppedScene(false, 0.0f, 0.0f));
    const auto on = render(*ctx, steppedScene(true, 0.0f, 0.0f));
    const auto onNoFade = render(*ctx, steppedScene(true, 50.0f, 40.0f)); // end <= start: off, as the default
    const auto faded = render(*ctx, steppedScene(true, 0.0f, 0.01f));     // the whole frame past the band
    const double aoEffect = meanDifference(on, off);
    const double fadedResidue = meanDifference(faded, off);
    CAPTURE(aoEffect, fadedResidue);
    CHECK(aoEffect > 0.5);                                // AO does darken this field
    CHECK(fadedResidue < aoEffect * 0.1);                 // fully faded, it is (almost) AO off
    CHECK(gpu::hashImage(onNoFade) == gpu::hashImage(on)); // a disabled band changes nothing
    CHECK(ctx->errorCount() == 0);
}
