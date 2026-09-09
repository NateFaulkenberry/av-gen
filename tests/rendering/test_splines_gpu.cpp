// Splines on the GPU (ADR-026): the SplineBuffers tables, the Path deformer against the CPU
// reference (deformPointWith) through the rendered silhouette, the Spline distribution against
// the CPU cloud, and the Spline particle emitter.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "rendering/spline_buffers.hpp"
#include "scene/scene.hpp"
#include "spatial/spline.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
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

gpu::ShaderLibrary makeShaders(gpu::Context& ctx) {
    return gpu::ShaderLibrary(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
}

struct Coverage {
    int pixels = 0;
    int minX = 1 << 30, minY = 1 << 30, maxX = -1, maxY = -1;
};

// A covered pixel: the scenes here are white/emissive geometry on black, so a solid threshold
// well above the tone-mapped ambient floor keeps the silhouette bounds meaningful.
bool lit(const gpu::Image8& img, int x, int y) {
    if (x < 0 || y < 0 || x >= static_cast<int>(img.width) || y >= static_cast<int>(img.height)) {
        return false;
    }
    const auto* p = img.pixel(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
    return p[0] + p[1] + p[2] > 150;
}

Coverage coverage(const gpu::Image8& img) {
    Coverage c;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            if (lit(img, static_cast<int>(x), static_cast<int>(y))) {
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

// Pixel coordinates of a world point through the scene camera (top-left origin).
glm::vec2 project(const scene::Scene& s, glm::vec3 p, std::uint32_t w, std::uint32_t h) {
    const glm::mat4 viewProj = s.camera.projection(static_cast<float>(w) / static_cast<float>(h)) * s.camera.view();
    const glm::vec4 clip = viewProj * glm::vec4(p, 1.0f);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return {(ndc.x * 0.5f + 0.5f) * static_cast<float>(w), (0.5f - ndc.y * 0.5f) * static_cast<float>(h)};
}

scene::Scene darkScene() {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.post.bloomEnabled = false; // bloom would smear the silhouettes these tests measure
    s.post.bloomIntensity = 0.0f;
    s.camera.position = {0.0f, 9.0f, 14.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.6f));
    key.intensity = 3.0f;
    s.addLight(key);
    return s;
}

// A white unlit material, so every covered pixel is bright regardless of lighting.
void unlitWhite(scene::ProceduralGeometry& g) {
    g.material.baseColor = {1.0f, 1.0f, 1.0f};
    g.material.unlit = true;
    g.material.emissiveIntensity = 0.0f;
}

spatial::Spline circleSpline(const std::string& name, float radius) {
    spatial::Spline s;
    s.name = name;
    s.generator = spatial::SplineGenerator::Circle;
    s.closed = true;
    s.radius = radius;
    s.count = 48;
    return s;
}

spatial::Spline helixSpline(const std::string& name) {
    spatial::Spline s;
    s.name = name;
    s.generator = spatial::SplineGenerator::Helix;
    s.radius = 3.0f;
    s.height = 4.0f;
    s.turns = 1.5f;
    s.count = 48;
    s.center = {0.0f, -2.0f, 0.0f};
    return s;
}

gpu::Image8 renderOnce(gpu::Context& ctx, const scene::Scene& s, double time, std::uint32_t w = 256,
                       std::uint32_t h = 256, rendering::RenderStats* statsOut = nullptr) {
    auto shaders = makeShaders(ctx);
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime t{};
    t.renderTime = time;
    auto img = renderer.renderToImage(s, t, w, h);
    REQUIRE(img.has_value());
    if (statsOut != nullptr) {
        *statsOut = renderer.stats();
    }
    return *img;
}

} // namespace

TEST_CASE("SplineBuffers packs the scene splines into slots and re-uploads only on change", "[gpu][spline]") {
    auto ctx = makeContext();
    rendering::SplineBuffers buffers(*ctx);
    CHECK(buffers.count() == 0);
    CHECK(buffers.uploads() == 0);
    spatial::SplineSet set;
    set.splines.push_back(circleSpline("ring", 2.0f));
    set.splines.push_back(helixSpline("helix"));
    CHECK(buffers.update(set));
    CHECK(buffers.uploads() == 1);
    CHECK(buffers.count() == 2);
    CHECK(buffers.slotOf("ring") == 0);
    CHECK(buffers.slotOf("helix") == 1);
    CHECK(buffers.slotOf("missing") == -1);
    const auto& header = buffers.header();
    CHECK(header.info[0].x == set.splines[0].length());
    CHECK(header.info[0].y == 1.0f); // closed
    CHECK(header.info[0].z == static_cast<float>(spatial::kSplineGpuSamples));
    CHECK(header.info[0].w == 1.0f);
    CHECK(header.info[1].y == 0.0f);
    CHECK(header.info[2].w == 0.0f); // unused slot
    REQUIRE(buffers.table(0).size() == static_cast<std::size_t>(spatial::kSplineGpuSamples));
    CHECK(buffers.table(0).front().position.w == 0.0f);
    CHECK(buffers.table(0).back().position.w < set.splines[0].length()); // closed: no duplicate end
    CHECK(buffers.table(5).empty());
    // Unchanged set: no upload. Edited spline: one upload. Renamed: one upload.
    CHECK_FALSE(buffers.update(set));
    CHECK(buffers.uploads() == 1);
    set.splines[0].radius = 3.0f;
    CHECK(buffers.update(set));
    CHECK(buffers.uploads() == 2);
    CHECK(header.info[0].x == set.splines[0].length());
    set.splines[1].name = "coil";
    CHECK(buffers.update(set));
    CHECK(buffers.slotOf("coil") == 1);
    CHECK(buffers.slotOf("helix") == -1);
    // More than the limit: the extras are not available.
    for (int i = 0; i < 20; ++i) {
        set.splines.push_back(circleSpline("extra" + std::to_string(i), 1.0f));
    }
    CHECK(buffers.update(set));
    CHECK(buffers.count() == static_cast<std::uint32_t>(rendering::kMaxGpuSplines));
    CHECK(buffers.slotOf("extra13") == 15);
    CHECK(buffers.slotOf("extra14") == -1);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Spline distribution: GPU instances sit where the CPU cloud puts them", "[gpu][spline][procedural]") {
    auto ctx = makeContext();
    scene::Scene s = darkScene();
    s.splines.splines.push_back(helixSpline("helix"));
    scene::ProceduralGeometry g;
    g.name = "beads";
    g.source.kind = scene::PrimitiveKind::Box;
    g.source.size = {0.5f, 0.5f, 0.5f};
    g.distribution.kind = scene::DistributionKind::Spline;
    g.distribution.spline = "helix";
    g.distribution.count = 24;
    unlitWhite(g);
    scene::GenerationContext ctx2;
    ctx2.splines = &s.splines;
    REQUIRE(g.rebuild(ctx2));
    REQUIRE(g.instances.size() == 24);
    s.procedurals.push_back(g);

    rendering::RenderStats stats;
    const auto img = renderOnce(*ctx, s, 0.0, 256, 256, &stats);
    CHECK(stats.procedural.instances == 24);
    const Coverage cov = coverage(img);
    CHECK(cov.pixels > 200);
    // Every instance origin projects onto a lit pixel (a solid box around it).
    int hit = 0;
    for (const auto& r : g.instances) {
        const glm::vec2 px = project(s, glm::vec3(r.position), 256, 256);
        const int x = static_cast<int>(std::floor(px.x));
        const int y = static_cast<int>(std::floor(px.y));
        if (lit(img, x, y)) {
            ++hit;
        }
    }
    CHECK(hit == 24);
    // The same records given explicitly (kind Single, instances copied) render the same bytes.
    scene::Scene explicitScene = s;
    explicitScene.procedurals[0].distribution.kind = scene::DistributionKind::Single;
    explicitScene.splines.splines.clear();
    CHECK(gpu::hashImage(renderOnce(*ctx, explicitScene, 0.0)) == gpu::hashImage(img));
    // Deterministic across contexts.
    auto ctxB = makeContext();
    CHECK(gpu::hashImage(renderOnce(*ctxB, s, 0.0)) == gpu::hashImage(img));
    if (const char* dumpDir = std::getenv("AVGEN_DUMP_DIR")) {
        REQUIRE(gpu::writePpm(img, std::filesystem::path(dumpDir) / "spline_distribution.ppm").has_value());
    }
    CHECK(ctx->errorCount() == 0);
    CHECK(ctxB->errorCount() == 0);
}

TEST_CASE("Path deformer: the GPU silhouette matches deformPointWith on the source vertices",
          "[gpu][spline][procedural]") {
    auto ctx = makeContext();
    scene::Scene s = darkScene();
    s.camera.position = {0.0f, 12.0f, 0.01f}; // looking straight down: the ring is a circle
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.splines.splines.push_back(circleSpline("ring", 3.0f));
    scene::ProceduralGeometry g;
    g.name = "bar";
    g.source.kind = scene::PrimitiveKind::Box;
    g.source.size = {0.6f, 8.0f, 0.6f};
    g.source.subdivisions = 16;
    g.distribution.kind = scene::DistributionKind::Single;
    unlitWhite(g);
    REQUIRE(g.rebuild());
    s.procedurals.push_back(g);
    const std::uint32_t w = 256, h = 256;
    rendering::RenderStats stats;
    const auto straight = renderOnce(*ctx, s, 0.0, w, h, &stats);
    const Coverage barCov = coverage(straight);
    CHECK(barCov.pixels > 100);
    CHECK(stats.procedural.pathDeformers == 0);

    scene::Deformer path;
    path.kind = scene::DeformerKind::Path;
    path.spline = "ring";
    path.axis = {0.0f, 1.0f, 0.0f};
    path.amount = 1.0f;
    path.pathScale = 0.0f; // fit: the 8-unit bar wraps once around the ring
    scene::Scene bent = s;
    bent.procedurals[0].deformers.push_back(path);
    const auto ring = renderOnce(*ctx, bent, 0.0, w, h, &stats);
    CHECK(stats.procedural.pathDeformers == 1);
    const Coverage ringCov = coverage(ring);
    CHECK(ringCov.pixels > 100);
    CHECK(gpu::hashImage(ring) != gpu::hashImage(straight));
    // A ring seen from above is round with a hole in the middle; the bar was a thin strip.
    CHECK(std::abs((ringCov.maxX - ringCov.minX) - (ringCov.maxY - ringCov.minY)) < 6);
    CHECK_FALSE(lit(ring, static_cast<int>(w / 2), static_cast<int>(h / 2)));
    CHECK(barCov.maxX - barCov.minX < (ringCov.maxX - ringCov.minX) / 3);

    // CPU reference: the source vertices through deformPointWith, projected; their pixel bounds
    // are the rendered bounds (the box corners are vertices) within a couple of pixels.
    auto mesh = scene::makeSourceMesh(bent.procedurals[0].source);
    REQUIRE(mesh.has_value());
    const auto [lo, hi] = mesh->bounds();
    scene::DeformContext dctx;
    dctx.splines = &bent.splines;
    dctx.sourceExtent = hi.y - lo.y;
    const glm::mat4 world = bent.procedurals[0].instanceMatrix(0);
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (const auto& v : mesh->vertices) {
        const glm::vec3 p = scene::deformPointWith(bent.procedurals[0].deformers, v.position, world, 0.0, dctx, v.normal);
        CHECK(std::abs(glm::length(glm::vec2(p.x, p.z)) - 3.0f) < 0.35f); // on the ring +- half the bar width
        const glm::vec2 px = project(bent, p, w, h);
        minX = std::min(minX, px.x);
        maxX = std::max(maxX, px.x);
        minY = std::min(minY, px.y);
        maxY = std::max(maxY, px.y);
    }
    INFO("cpu " << minX << "," << minY << " - " << maxX << "," << maxY << " gpu " << ringCov.minX << "," << ringCov.minY
                << " - " << ringCov.maxX << "," << ringCov.maxY);
    CHECK(std::abs(minX - static_cast<float>(ringCov.minX)) <= 2.5f);
    CHECK(std::abs(maxX - static_cast<float>(ringCov.maxX)) <= 2.5f);
    CHECK(std::abs(minY - static_cast<float>(ringCov.minY)) <= 2.5f);
    CHECK(std::abs(maxY - static_cast<float>(ringCov.maxY)) <= 2.5f);

    // Amount 0, a missing spline and a world-space Path deformer are all the identity.
    scene::Scene zero = bent;
    zero.procedurals[0].deformers[0].amount = 0.0f;
    CHECK(gpu::hashImage(renderOnce(*ctx, zero, 0.0, w, h)) == gpu::hashImage(straight));
    scene::Scene missing = bent;
    missing.procedurals[0].deformers[0].spline = "nope";
    CHECK(gpu::hashImage(renderOnce(*ctx, missing, 0.0, w, h, &stats)) == gpu::hashImage(straight));
    CHECK(stats.procedural.pathDeformers == 0);
    scene::Scene worldSpace = bent;
    worldSpace.procedurals[0].deformers[0].space = scene::DeformSpace::World;
    CHECK(gpu::hashImage(renderOnce(*ctx, worldSpace, 0.0, w, h)) == gpu::hashImage(straight));
    // Deterministic across contexts; an edited spline changes the image.
    auto ctxB = makeContext();
    CHECK(gpu::hashImage(renderOnce(*ctxB, bent, 0.0, w, h)) == gpu::hashImage(ring));
    scene::Scene bigger = bent;
    bigger.splines.splines[0].radius = 4.0f;
    const auto biggerImg = renderOnce(*ctx, bigger, 0.0, w, h);
    CHECK(gpu::hashImage(biggerImg) != gpu::hashImage(ring));
    CHECK(coverage(biggerImg).maxX > ringCov.maxX);
    if (const char* dumpDir = std::getenv("AVGEN_DUMP_DIR")) {
        REQUIRE(gpu::writePpm(ring, std::filesystem::path(dumpDir) / "spline_path_ring.ppm").has_value());
    }
    CHECK(ctx->errorCount() == 0);
    CHECK(ctxB->errorCount() == 0);
}

TEST_CASE("Spline particle emitter spawns along the curve, deterministically", "[gpu][spline][particles]") {
    auto ctx = makeContext();
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.post.bloomEnabled = false;
    s.post.bloomIntensity = 0.0f;
    s.camera.position = {0.0f, 12.0f, 0.01f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.splines.splines.push_back(circleSpline("ring", 3.0f));
    scene::ParticleSystem sys;
    sys.name = "sparks";
    sys.capacity = 8192;
    sys.shape = scene::EmitterShape::Spline;
    sys.spline = "ring";
    sys.position = {0.0f, 0.0f, 0.0f};
    sys.extent = {0.05f, 0.05f, 0.05f};
    sys.spawnRate = 30000.0f;
    sys.lifetimeMin = 0.3f;
    sys.lifetimeMax = 0.3f;
    sys.speedMin = 0.0f;
    sys.speedMax = 0.0f;
    sys.gravity = {0.0f, 0.0f, 0.0f};
    sys.turbulence = 0.0f;
    sys.sizeStart = 0.12f;
    sys.sizeEnd = 0.12f;
    sys.colorStart = {1.0f, 0.8f, 0.4f, 1.0f};
    sys.colorEnd = {1.0f, 0.8f, 0.4f, 1.0f};
    sys.emissive = 3.0f;
    s.particles.push_back(sys);

    auto run = [&](gpu::Context& c, const scene::Scene& sc) {
        auto shaders = makeShaders(c);
        rendering::SceneRenderer renderer(c, shaders);
        REQUIRE(renderer.init().has_value());
        FixedStepClock clock(60.0);
        gpu::Image8 last;
        for (int i = 0; i < 12; ++i) {
            auto img = renderer.renderToImage(sc, clock.tick(), 128, 128);
            REQUIRE(img.has_value());
            last = *img;
        }
        return last;
    };
    const auto ring = run(*ctx, s);
    const Coverage cov = coverage(ring);
    CHECK(cov.pixels > 50);
    // Spread around the whole ring: wide in both directions, dark centre.
    CHECK(cov.maxX - cov.minX > 60);
    CHECK(cov.maxY - cov.minY > 60);
    CHECK_FALSE(lit(ring, 64, 64));
    // Everything lit is near the ring (radius 3 -> ~27 px at this camera).
    const glm::vec2 centre = project(s, glm::vec3(0.0f), 128, 128);
    const float ringPx = glm::length(project(s, glm::vec3(3.0f, 0.0f, 0.0f), 128, 128) - centre);
    int offRing = 0;
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            if (lit(ring, x, y) && std::abs(glm::length(glm::vec2(x + 0.5f, y + 0.5f) - centre) - ringPx) > 6.0f) {
                ++offRing;
            }
        }
    }
    CHECK(offRing == 0);
    // Deterministic across contexts.
    auto ctxB = makeContext();
    CHECK(gpu::hashImage(run(*ctxB, s)) == gpu::hashImage(ring));
    // A missing spline falls back to the Point shape at the emitter position: a compact blob.
    scene::Scene missing = s;
    missing.particles[0].spline = "nope";
    const auto blob = run(*ctx, missing);
    const Coverage blobCov = coverage(blob);
    CHECK(blobCov.pixels > 0);
    CHECK(blobCov.maxX - blobCov.minX < 20);
    CHECK(lit(blob, 64, 64));
    if (const char* dumpDir = std::getenv("AVGEN_DUMP_DIR")) {
        REQUIRE(gpu::writePpm(ring, std::filesystem::path(dumpDir) / "spline_emitter.ppm").has_value());
    }
    CHECK(ctx->errorCount() == 0);
    CHECK(ctxB->errorCount() == 0);
}
