// ADR-1959: the slab fast path draws what the general path draws. A lattice of boxes at varied heights (and a few
// turned about y) is rendered twice -- `slabs` off and on -- from above, from the side and from below, with a
// shadow-casting key and the depth prepass, and the two frames must agree. They are not bit-identical by design: a
// turned quad covers the same face as makeBox's opposite face but splits it along the other diagonal, and its uv runs
// the other way; neither changes a lit, untextured surface by more than rounding. What would show here is a wrong face
// (a hole where a side should be), a face turned to the camera in a shadow view (light leaking through the casters),
// or a depth disagreement between the prepass and the lit pass (missing surfaces).

#include "assets/image.hpp"
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

#include <algorithm>
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

// `velocityOnDemand`: the live editor's and the live profile's mode, where a frame that nothing reads the velocity of
// skips it (ADR-1492), and the slab vertex stage takes its early return on the lit pass too.
gpu::Image8 render(gpu::Context& ctx, const scene::Scene& s, bool velocityOnDemand = false) {
    gpu::ShaderLibrary shaders(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    renderer.setVelocityOnDemand(velocityOnDemand);
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

TEST_CASE("The side rule's normal ease reaches the lit pass", "[gpu][procedural][geography][adr1959]") {
    // CP-A r8: the ease sat after the vertex stage's early return, which also takes the lit pass whenever nothing
    // reads the velocity target, so a flattened side kept its own normal and lit like a riser under a low sun. Here a
    // stepped field under a low side sun, with every side flattened (a threshold far above any tile) against none.
    auto scene = [](float sidePixels) {
        scene::Scene s = steppedScene(false, 0.0f, 0.0f);
        s.procedurals[0].slabSidePixels = sidePixels;
        s.procedurals[0].slabFarPixels = 0.001f; // the rule on, but no gap closing
        s.lights.clear();
        scene::PunctualLight sun;
        sun.direction = glm::normalize(glm::vec3(0.0f, -0.12f, 1.0f)); // low, square onto the camera-facing risers
        sun.intensity = 4.0f;
        s.addLight(sun);
        return s;
    };
    auto ctx = makeContext();
    for (const bool onDemand : {false, true}) {
        CAPTURE(onDemand);
        const auto risers = render(*ctx, scene(0.002f), onDemand);
        const auto flattened = render(*ctx, scene(100000.0f), onDemand);
        const double change = meanDifference(risers, flattened);
        CAPTURE(change);
        CHECK(change > 0.5); // the flattened sides shade as the tops: the lit pass saw the ease
    }
    CHECK(ctx->errorCount() == 0);
}

namespace {

// ADR-2004: a flat "tiles" ring seen low down a column gap, which runs away from the camera at the image's centre. Its
// walls (x sides) are near edge-on, and with the rule off the slits between consecutive tiles' walls open onto the
// z sides deeper in the gap. The faces are coloured by the slab debug mode (x sides green, z sides blue, tops red).
scene::Scene gapScene(float edgeOn) {
    scene::Scene s = ringScene(0.0f);
    scene::ProceduralGeometry& pg = s.procedurals[0];
    pg.slabEdgeOn = edgeOn;
    pg.slabDebugFaces = true;
    pg.source.size = {0.9f, 0.6f, 0.9f}; // the MG proportions: a gap a tenth of the pitch, tall sides
    pg.distribution.generator.tiles.hillHeight = 3.0f; // steps, so a wall stands above the next tile
    REQUIRE(pg.rebuild());
    s.camera.position = {0.0f, 2.5f, 14.0f};
    s.camera.target = {0.0f, 0.0f, -20.0f};
    return s;
}

// A z side's pixel between two x side pixels along a row or a column: a hole in a wall, through to the gap behind.
// Counted in the columns [x0, x1): the camera stands over the gap at x = 0 and looks straight down it, so its walls
// project within a few pixels of the image's centre column all the way to the horizon.
int holes(const gpu::Image8& img, std::uint32_t x0, std::uint32_t x1) {
    const auto face = [&](std::uint32_t x, std::uint32_t y) {
        const auto* p = img.pixel(x, y);
        if (p[1] > p[0] + 8 && p[1] > p[2] + 8) return 1; // x side
        if (p[2] > p[0] + 8 && p[2] > p[1] + 8) return 2; // z side
        return 0;
    };
    int n = 0;
    for (std::uint32_t y = 1; y + 1 < img.height; ++y) {
        for (std::uint32_t x = std::max(x0, 1u); x < x1 && x + 1 < img.width; ++x) {
            if (face(x, y) != 2) continue;
            if ((face(x - 1, y) == 1 && face(x + 1, y) == 1) || (face(x, y - 1) == 1 && face(x, y + 1) == 1)) ++n;
        }
    }
    return n;
}

} // namespace

TEST_CASE("An edge-on side spans the gap to its neighbour, so a gap's walls do not open into holes",
          "[gpu][procedural][geography][adr2004]") {
    // CPD-R11: the dotted line was the walls of a column gap seen edge-on, holed by the slits between tiles. With the
    // rule off (0) they show as isolated z-side pixels in the x-side walls; with the default (0.1) they close.
    auto ctx = makeContext();
    const auto off = render(*ctx, gapScene(0.0f));
    const auto on = render(*ctx, gapScene(0.1f));
    const std::uint32_t mid = off.width / 2;
    const int before = holes(off, mid - 16, mid + 16);
    const int after = holes(on, mid - 16, mid + 16);
    const Diff d = compare(off, on);
    CAPTURE(before, after, d.lit, d.differ);
    if (const char* dump = std::getenv("AVGEN_SLAB_TEST_DUMP"); dump != nullptr) { // a look at the two frames
        REQUIRE(assets::writePng(std::filesystem::path(dump) / "gap-off.png", off.width, off.height, off.rgba).has_value());
        REQUIRE(assets::writePng(std::filesystem::path(dump) / "gap-on.png", on.width, on.height, on.rgba).has_value());
    }
    CHECK(before >= 5); // measured: 7 holes in the gap under the camera with the rule off, 0 with it on
    CHECK(after * 5 < before);
    CHECK(d.differ < static_cast<int>(off.width * off.height / 50)); // nothing else moves
    CHECK(ctx->errorCount() == 0);
}

namespace {

// ADR-2009 (CPD-R15): a bright, cyan "day" tile field seen from above with one beach-ball impact at its centre, lime
// glow only (no sparks). `impact` false: no impact at all, to find the outline pixels the impact lights.
scene::Scene dayImpactScene(bool impact, float tint) {
    scene::Scene s = ringScene(0.0f);
    s.procedurals[0].material.baseColor = {0.45f, 0.95f, 1.0f};
    s.lights[0].intensity = 4.0f; // a bright day: light cyan tops, as MG's day tiles
    s.camera.position = {0.3f, 14.0f, 0.3f};
    scene::TileBallSpec& b = s.environment.tileDynamics.balls;
    b.glowColor = {0.3f, 1.0f, 0.0f};
    b.glowGain = 4.0f;
    b.glowRadius = 3.0f;
    b.sparkGain = 0.0f;
    b.edgeBand = 0.08f;
    b.tint = tint;
    if (impact) s.environment.tileDynamics.ballImpacts.push_back(scene::TileEvent{0.6f, {0.0f, 0.0f}, -1.0f});
    return s;
}

} // namespace

TEST_CASE("A ball's impact keeps its own hue on a bright day tile when it tints", "[gpu][procedural][geography][balls][adr2009]") {
    // CPD-R15: added on top of a bright cyan tile (tint 0, ADR-2008) the lime glow washes out to a pale cyan-white;
    // tinting (1) takes the tile's colour out under the light first, so the outline reads lime. Measured on the
    // pixels the impact changes: green over blue, the lime's mark (cyan has as much blue as green).
    auto ctx = makeContext();
    const auto none = render(*ctx, dayImpactScene(false, 1.0f));
    const auto add = render(*ctx, dayImpactScene(true, 0.0f));
    const auto tint = render(*ctx, dayImpactScene(true, 1.0f));
    const auto lime = [&](const gpu::Image8& img, int& count) {
        double sum = 0.0;
        count = 0;
        for (std::uint32_t y = 0; y < img.height; ++y) {
            for (std::uint32_t x = 0; x < img.width; ++x) {
                const double dx = static_cast<double>(x) - 0.5 * img.width, dy = static_cast<double>(y) - 0.5 * img.height;
                if (dx * dx + dy * dy > 120.0 * 120.0) continue; // the struck tiles, about two tiles round the impact
                const auto* p = none.pixel(x, y);
                const auto* q = tint.pixel(x, y); // the same pixel set for both arms: where the tint lights
                int d = 0;
                for (int c = 0; c < 3; ++c) d += std::abs(static_cast<int>(p[c]) - static_cast<int>(q[c]));
                if (d < 30) continue;
                const auto* r = img.pixel(x, y);
                sum += static_cast<double>(r[1]) - static_cast<double>(r[2]);
                ++count;
            }
        }
        return count > 0 ? sum / count : 0.0;
    };
    int nAdd = 0, nTint = 0, nNone = 0;
    const double limeNone = lime(none, nNone);
    const double limeAdd = lime(add, nAdd);
    const double limeTint = lime(tint, nTint);
    CAPTURE(nTint, limeNone, limeAdd, limeTint);
    if (const char* dump = std::getenv("AVGEN_SLAB_TEST_DUMP"); dump != nullptr) {
        for (const auto& [name, img] : {std::pair<const char*, const gpu::Image8*>{"impact-none.png", &none},
                                        {"impact-add.png", &add}, {"impact-tint.png", &tint}}) {
            REQUIRE(assets::writePng(std::filesystem::path(dump) / name, img->width, img->height, img->rgba).has_value());
        }
    }
    CHECK(nTint > 300);                // the outlines around the impact light up
    // and read lime, where the added light stays near the tile's cyan. Measured: green over blue -4 (no impact),
    // 17 (added), 49 (tinted).
    CHECK(limeTint > limeAdd + 20.0);
    CHECK(limeTint > 2.0 * limeAdd);
    CHECK(ctx->errorCount() == 0);
}
