// Motion quality (ADR-040): velocity-aligned stretching, ribbon trails, lifetime curves,
// tile-based motion blur over the velocity target, and the determinism of all of it.
//
// The blur tests move one emissive box between frames and measure the lit footprint. The velocity
// target is what the blur reads, so a smear there proves *object* motion blurs - which the old
// depth-reprojection pass could never do, because it only ever saw the camera.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

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

scene::MeshData boxMesh(glm::vec3 half) {
    scene::MeshData m;
    const glm::vec3 normals[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (const glm::vec3 normal : normals) {
        const glm::vec3 u = std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::cross(glm::vec3(0, 1, 0), normal);
        const glm::vec3 v = glm::cross(normal, u);
        const glm::vec3 c = normal * half;
        const glm::vec3 du = u * half;
        const glm::vec3 dv = v * half;
        const auto base = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({c - du - dv, normal, {0, 0}});
        m.vertices.push_back({c + du - dv, normal, {1, 0}});
        m.vertices.push_back({c + du + dv, normal, {1, 1}});
        m.vertices.push_back({c - du + dv, normal, {0, 1}});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return m;
}

// One small self-lit box on black: no lights, no fog, no bloom, so every lit pixel belongs to it.
scene::Scene movingBox() {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    s.camera.position = {0.0f, 0.0f, 6.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.camera.lens.shutterAngle = 180.0f;
    const auto box = s.addMesh(boxMesh({0.25f, 0.25f, 0.25f}));
    auto& e = s.addEntity("mover", box);
    e.material.baseColor = glm::vec3(0.0f);
    e.material.emissiveColor = glm::vec3(1.0f);
    e.material.emissiveIntensity = 6.0f;
    e.material.unlit = true;
    return s;
}

struct Footprint {
    int columns = 0;
    int rows = 0;
    int lit = 0;
};

Footprint footprint(const gpu::Image8& img, int threshold = 24) {
    std::vector<bool> col(img.width, false);
    std::vector<bool> row(img.height, false);
    Footprint f;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4;
            if (img.rgba[i] > threshold) {
                col[x] = true;
                row[y] = true;
                ++f.lit;
            }
        }
    }
    f.columns = static_cast<int>(std::count(col.begin(), col.end(), true));
    f.rows = static_cast<int>(std::count(row.begin(), row.end(), true));
    return f;
}

// Renders the box at `from` then at `to`, so the second frame has a real velocity to blur.
// A fresh renderer per measurement: no temporal state carries between the cases being compared.
Footprint renderMoved(gpu::Context& ctx, gpu::ShaderLibrary& shaders, scene::Scene& s, glm::vec3 from,
                      glm::vec3 to, gpu::Image8* out = nullptr) {
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime time;
    time.deltaTime = 1.0 / 30.0;
    s.entities[0].transform.position = from;
    time.frameIndex = 0;
    time.renderTime = 0.0;
    auto first = renderer.renderToImage(s, time, 256, 256);
    REQUIRE(first.has_value());
    s.entities[0].transform.position = to;
    time.frameIndex = 1;
    time.renderTime = 1.0 / 30.0;
    auto second = renderer.renderToImage(s, time, 256, 256);
    REQUIRE(second.has_value());
    if (out != nullptr) {
        *out = *second;
    }
    return footprint(*second);
}

} // namespace

TEST_CASE("Motion blur smears along a moving object's velocity and vanishes at a zero shutter",
          "[gpu][motion][blur]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});

    scene::Scene s = movingBox();
    // 0.8 m of travel in one frame at 30 fps: about 86 px at this framing, and a 180 degree
    // shutter blurs half of it.
    const glm::vec3 rest{0.0f, 0.0f, 0.0f};
    const glm::vec3 left{-0.8f, 0.0f, 0.0f};
    const glm::vec3 below{0.0f, -0.8f, 0.0f};

    s.post.motionBlurAmount = 0.0f;
    const Footprint sharp = renderMoved(*ctx, shaders, s, left, rest);
    REQUIRE(sharp.columns > 8);
    REQUIRE(sharp.rows > 8);

    s.post.motionBlurAmount = 1.0f;
    s.post.motionBlurSamples = 16;
    const Footprint sideways = renderMoved(*ctx, shaders, s, left, rest);
    const Footprint upwards = renderMoved(*ctx, shaders, s, below, rest);

    // The smear runs along the motion and only along it: horizontal travel widens the footprint
    // and leaves its height alone, vertical travel does the opposite. That is the whole claim of
    // reading the velocity target rather than reprojecting depth.
    CHECK(sideways.columns > sharp.columns + 4);
    CHECK(upwards.rows > sharp.rows + 4);
    // Anisotropy: the growth along the motion is several times the growth across it.
    CHECK(sideways.columns - sharp.columns > 2 * (sideways.rows - sharp.rows));
    CHECK(upwards.rows - sharp.rows > 2 * (upwards.columns - sharp.columns));
    CHECK(sideways.lit > sharp.lit);
    CHECK(upwards.lit > sharp.lit);

    // ADR-037: the shutter sets the length, so a closed shutter is exactly no blur.
    s.camera.lens.shutterAngle = 0.0f;
    const Footprint closed = renderMoved(*ctx, shaders, s, left, rest);
    CHECK(closed.columns == sharp.columns);
    CHECK(closed.rows == sharp.rows);
    CHECK(closed.lit == sharp.lit);

    // A still object does not smear however wide the shutter is open.
    s.camera.lens.shutterAngle = 360.0f;
    const Footprint still = renderMoved(*ctx, shaders, s, rest, rest);
    s.post.motionBlurAmount = 0.0f;
    const Footprint stillSharp = renderMoved(*ctx, shaders, s, rest, rest);
    CHECK(still.columns == stillSharp.columns);
    CHECK(still.rows == stillSharp.rows);
    CHECK(still.lit == stillSharp.lit);
}

TEST_CASE("Motion blur is deterministic: two fresh renderers produce identical frames",
          "[gpu][motion][blur][determinism]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    scene::Scene s = movingBox();
    s.post.motionBlurAmount = 1.0f;
    s.post.motionBlurSamples = 12;

    gpu::Image8 a;
    gpu::Image8 b;
    renderMoved(*ctx, shaders, s, glm::vec3(-0.8f, 0.0f, 0.0f), glm::vec3(0.0f), &a);
    renderMoved(*ctx, shaders, s, glm::vec3(-0.8f, 0.0f, 0.0f), glm::vec3(0.0f), &b);
    REQUIRE(a.rgba.size() == b.rgba.size());
    CHECK(a.rgba == b.rgba);
}

TEST_CASE("camera-culling history does not create a false re-entry velocity", "[gpu][motion][blur]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    scene::Scene sequence = movingBox();
    sequence.post.motionBlurAmount = 1.0f;
    sequence.post.motionBlurSamples = 12;

    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FrameTime time;
    time.deltaTime = 1.0 / 30.0;
    time.frameIndex = 0;
    time.renderTime = 0.0;
    REQUIRE(renderer.renderToImage(sequence, time, 256, 256).has_value());

    sequence.entities[0].transform.position = {-0.8f, 0.0f, 0.0f};
    sequence.entities[0].cameraCulled = true;
    time.frameIndex = 1;
    time.renderTime = 1.0 / 30.0;
    REQUIRE(renderer.renderToImage(sequence, time, 256, 256).has_value());

    // The object re-enters at the same position it had while culled. A stale previous-model entry
    // would incorrectly report motion from the first frame's origin and smear this frame.
    sequence.entities[0].cameraCulled = false;
    time.frameIndex = 2;
    time.renderTime = 2.0 / 30.0;
    const auto reentered = renderer.renderToImage(sequence, time, 256, 256);
    REQUIRE(reentered.has_value());

    rendering::SceneRenderer fresh(*ctx, shaders);
    REQUIRE(fresh.init().has_value());
    const auto expected = fresh.renderToImage(sequence, time, 256, 256);
    REQUIRE(expected.has_value());
    CHECK(gpu::hashImage(*reentered) == gpu::hashImage(*expected));
}

TEST_CASE("reverse timeline reuse matches a fresh renderer", "[gpu][motion][determinism]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    scene::Scene sequence = movingBox();
    sequence.post.motionBlurAmount = 1.0f;
    sequence.post.motionBlurSamples = 12;
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    FrameTime time;
    time.deltaTime = 1.0 / 30.0;
    time.frameIndex = 0;
    time.renderTime = 0.0;
    sequence.entities[0].transform.position = {-0.8f, 0.0f, 0.0f};
    REQUIRE(renderer.renderToImage(sequence, time, 256, 256).has_value());
    time.frameIndex = 1;
    time.renderTime = 1.0 / 30.0;
    sequence.entities[0].transform.position = {0.0f, 0.0f, 0.0f};
    REQUIRE(renderer.renderToImage(sequence, time, 256, 256).has_value());

    // Seek backward and render a state already visited by this renderer. Temporal buffers must not
    // leak the forward path into the reversed frame.
    time.frameIndex = 2;
    time.renderTime = 0.0;
    sequence.entities[0].transform.position = {-0.8f, 0.0f, 0.0f};
    const auto reversed = renderer.renderToImage(sequence, time, 256, 256);
    REQUIRE(reversed.has_value());

    rendering::SceneRenderer fresh(*ctx, shaders);
    REQUIRE(fresh.init().has_value());
    const auto expected = fresh.renderToImage(sequence, time, 256, 256);
    REQUIRE(expected.has_value());
    CHECK(gpu::hashImage(*reversed) == gpu::hashImage(*expected));
}

namespace {

// A hero emitter: few particles, a long life, no randomness in the forces, trails on.
scene::ParticleSystem trailSystem() {
    scene::ParticleSystem sys;
    sys.name = "arcs";
    sys.capacity = 1024;
    sys.seed = 3;
    sys.shape = scene::EmitterShape::Sphere;
    sys.position = {0.0f, 0.0f, 0.0f};
    sys.extent = glm::vec3(0.4f);
    sys.spawnRate = 400.0f;
    sys.lifetimeMin = 2.0f;
    sys.lifetimeMax = 2.0f;
    sys.speedMin = 1.5f;
    sys.speedMax = 2.5f;
    sys.spread = 1.0f;
    sys.gravity = glm::vec3(0.0f);
    sys.turbulence = 0.5f;
    sys.drag = 0.0f;
    sys.sizeStart = 0.05f;
    sys.sizeEnd = 0.05f;
    sys.emissive = 4.0f;
    sys.trailEnabled = true;
    sys.trailLength = 12;
    sys.trailWidth = 0.8f;
    sys.trailTaper = 0.1f;
    sys.trailFade = 0.0f;
    return sys;
}

scene::Scene particleScene(const scene::ParticleSystem& sys) {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    s.camera.position = {0.0f, 0.0f, 7.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.particles.push_back(sys);
    return s;
}

void advance(rendering::SceneRenderer& renderer, scene::Scene& s, int frames) {
    FrameTime time;
    time.deltaTime = 1.0 / 30.0;
    for (int i = 0; i < frames; ++i) {
        time.frameIndex = static_cast<std::uint64_t>(i);
        time.renderTime = static_cast<double>(i) / 30.0;
        auto img = renderer.renderToImage(s, time, 128, 128);
        REQUIRE(img.has_value());
    }
}

} // namespace

TEST_CASE("Ribbon geometry: one quad per segment, drawn in alive-slot order", "[gpu][motion][trails]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::ParticleSystem sys = trailSystem();
    scene::Scene s = particleScene(sys);
    advance(renderer, s, 6);

    auto counts = renderer.particles().readCounts(0);
    REQUIRE(counts.has_value());
    REQUIRE(counts->alive > 0);
    auto args = renderer.particles().readDrawArgs(0);
    REQUIRE(args.has_value());
    // Billboards: one quad per particle.
    CHECK((*args)[0] == 6);
    CHECK((*args)[1] == counts->alive);
    CHECK((*args)[2] == 0);
    CHECK((*args)[3] == 0);
    // Ribbons: trailLength - 1 segments, six vertices each, one instance per alive particle, and
    // the same instance order (aliveList is in slot order, which is what makes the draw stable).
    const std::uint32_t segments = sys.trailLength - 1;
    CHECK((*args)[4] == segments * 6);
    CHECK((*args)[5] == counts->alive);
    CHECK((*args)[6] == 0);
    CHECK((*args)[7] == 0);
    CHECK(renderer.particles().stats().ribbonSystems == 1);
    CHECK(renderer.particles().stats().trailBytes == 1024ull * segments * scene::kTrailBytesPerPoint);

    // Trails off: the ribbon draw disappears entirely and costs no memory.
    scene::ParticleSystem plain = sys;
    plain.trailEnabled = false;
    scene::Scene bare = particleScene(plain);
    rendering::SceneRenderer other(*ctx, shaders);
    REQUIRE(other.init().has_value());
    advance(other, bare, 4);
    auto bareArgs = other.particles().readDrawArgs(0);
    REQUIRE(bareArgs.has_value());
    CHECK((*bareArgs)[4] == 0);
    CHECK((*bareArgs)[5] == 0);
    CHECK(other.particles().stats().trailBytes == 0);
}

TEST_CASE("Trail history is simulation state: identical across two runs", "[gpu][motion][trails][determinism]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    scene::ParticleSystem sys = trailSystem();

    auto run = [&]() {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        scene::Scene s = particleScene(sys);
        advance(renderer, s, 10);
        auto history = renderer.particles().readTrailHistory(0, 512);
        REQUIRE(history.has_value());
        return *history;
    };
    const std::vector<float> first = run();
    const std::vector<float> second = run();
    REQUIRE(first.size() == second.size());
    CHECK(first == second);
    // The ring really was written: some entry is a non-origin position.
    const bool written = std::any_of(first.begin(), first.end(), [](float v) { return v != 0.0f; });
    CHECK(written);
}

TEST_CASE("Stretched particles cover more of the image along their velocity", "[gpu][motion][stretch]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // Every particle travels straight up at a fixed speed, so the stretch is purely vertical.
    scene::ParticleSystem sys;
    sys.name = "jets";
    sys.capacity = 2048;
    sys.seed = 11;
    sys.shape = scene::EmitterShape::Point;
    sys.position = {0.0f, -1.5f, 0.0f};
    sys.spawnRate = 2000.0f;
    sys.lifetimeMin = 1.5f;
    sys.lifetimeMax = 1.5f;
    sys.direction = {0.0f, 1.0f, 0.0f};
    sys.spread = 0.0f;
    sys.speedMin = 6.0f;
    sys.speedMax = 6.0f;
    sys.gravity = glm::vec3(0.0f);
    sys.turbulence = 0.0f;
    sys.drag = 0.0f;
    sys.sizeStart = 0.03f;
    sys.sizeEnd = 0.03f;
    sys.emissive = 6.0f;

    auto litPixels = [&](const scene::ParticleSystem& system) {
        rendering::SceneRenderer r(*ctx, shaders);
        REQUIRE(r.init().has_value());
        scene::Scene s = particleScene(system);
        s.camera.lens.shutterAngle = 180.0f;
        FrameTime time;
        time.deltaTime = 1.0 / 30.0;
        gpu::Image8 last;
        for (int i = 0; i < 8; ++i) {
            time.frameIndex = static_cast<std::uint64_t>(i);
            time.renderTime = static_cast<double>(i) / 30.0;
            auto img = r.renderToImage(s, time, 256, 256);
            REQUIRE(img.has_value());
            last = *img;
        }
        return footprint(last, 12);
    };

    const Footprint round = litPixels(sys);
    scene::ParticleSystem streaks = sys;
    streaks.velocityStretch = 4.0f;
    streaks.stretchMax = 1.0f;
    const Footprint stretched = litPixels(streaks);
    // 6 m/s over a 1/60 s shutter, times 4, is 0.4 m of extra length against a 0.06 m diameter:
    // the same particles must light noticeably more pixels, all of it vertical.
    CHECK(stretched.lit > round.lit * 3 / 2);
    CHECK(stretched.rows >= round.rows);
}

TEST_CASE("Emissive particles reduce to one aggregate the volume can light with", "[gpu][motion][atmosphere]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::ParticleSystem sys = trailSystem();
    sys.trailEnabled = false;
    sys.position = {2.0f, 1.0f, 0.0f};
    sys.volumeGlow = 1.0f;
    scene::Scene s = particleScene(sys);
    s.environment.volumeDensity = 0.05f;
    s.environment.volumeAbsorption = 0.8f;
    s.environment.volumeMaxDistance = 60.0f;
    advance(renderer, s, 8);

    CHECK(renderer.particles().stats().glowSystems == 1);
    auto glow = renderer.particles().readGlow();
    REQUIRE(glow.has_value());
    // Slot 0: (centre.xyz, spread radius) then (colour.rgb, power).
    const float radius = (*glow)[3];
    const float power = (*glow)[7];
    CHECK(power > 0.0f);
    CHECK(radius > 0.0f);
    CHECK(radius < 8.0f); // the cloud has not run away from its emitter
    const glm::vec3 centre((*glow)[0], (*glow)[1], (*glow)[2]);
    CHECK(glm::length(centre - sys.position) < 3.0f);
    // Every unused slot stays zero, so the volume march can iterate the whole table.
    for (std::size_t i = 8; i < glow->size(); ++i) {
        CHECK((*glow)[i] == 0.0f);
    }
}

// ---- performance probes (ADR-040) -----------------------------------------------------------
// Hidden behind [.perf] so they never run in CI:
//   avgen_render_tests "[.perf][motion]"    (Release build)
// Reports median whole-frame GPU time at 1080p. Numbers live in docs/performance/motion.md.

namespace {

// Median of the whole-frame GPU timer over `measured` frames after `warmup` frames.
double medianFrameMs(rendering::SceneRenderer& renderer, scene::Scene& s, std::uint32_t width,
                     std::uint32_t height, int warmup, int measured) {
    std::vector<double> samples;
    FrameTime time;
    time.deltaTime = 1.0 / 60.0;
    for (int i = 0; i < warmup + measured; ++i) {
        time.frameIndex = static_cast<std::uint64_t>(i);
        time.renderTime = static_cast<double>(i) / 60.0;
        auto img = renderer.renderToImage(s, time, width, height);
        REQUIRE(img.has_value());
        if (i >= warmup && renderer.stats().gpuFrameMs >= 0.0) {
            samples.push_back(renderer.stats().gpuFrameMs);
        }
    }
    if (samples.empty()) {
        return -1.0;
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

scene::ParticleSystem perfSystem(std::uint32_t capacity, float spawnRate) {
    scene::ParticleSystem sys;
    sys.name = "perf";
    sys.capacity = capacity;
    sys.shape = scene::EmitterShape::Sphere;
    sys.extent = glm::vec3(2.0f);
    sys.spawnRate = spawnRate;
    sys.lifetimeMin = 2.0f;
    sys.lifetimeMax = 3.0f;
    sys.speedMin = 1.0f;
    sys.speedMax = 4.0f;
    sys.spread = 1.0f;
    sys.turbulence = 1.0f;
    sys.sizeStart = 0.012f;
    sys.sizeEnd = 0.0f;
    sys.emissive = 3.0f;
    return sys;
}

} // namespace

TEST_CASE("Motion quality cost at 1080p", "[.perf][motion]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    constexpr std::uint32_t kW = 1920;
    constexpr std::uint32_t kH = 1080;
    constexpr int kWarmup = 20;
    constexpr int kMeasured = 60;

    auto run = [&](const scene::ParticleSystem& sys, bool blur, const char* label) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        scene::Scene s = particleScene(sys);
        s.camera.position = {0.0f, 0.0f, 9.0f};
        s.post.motionBlurAmount = blur ? 1.0f : 0.0f;
        s.post.motionBlurSamples = 16;
        const double ms = medianFrameMs(renderer, s, kW, kH, kWarmup, kMeasured);
        WARN(label << ": median GPU " << ms << " ms/frame at 1920x1080; trail history "
                   << (renderer.particles().stats().trailBytes >> 20) << " MiB");
        return ms;
    };

    // 256 k stretched billboards, the case velocity stretching is meant for.
    scene::ParticleSystem round = perfSystem(256u * 1024, 120000.0f);
    scene::ParticleSystem streaks = round;
    streaks.velocityStretch = 3.0f;
    streaks.stretchMax = 0.3f;
    run(round, false, "256k round billboards");
    run(streaks, false, "256k stretched billboards");
    run(streaks, true, "256k stretched billboards + motion blur");

    // 32 k ribbons: the hero-emitter case, and the one that costs memory.
    scene::ParticleSystem ribbons = perfSystem(32u * 1024, 12000.0f);
    ribbons.sizeStart = 0.03f;
    ribbons.sizeEnd = 0.01f;
    scene::ParticleSystem noRibbons = ribbons;
    ribbons.trailEnabled = true;
    ribbons.trailLength = 32;
    ribbons.trailWidth = 0.8f;
    ribbons.trailTaper = 0.1f;
    run(noRibbons, false, "32k billboards (ribbon baseline)");
    run(ribbons, false, "32k ribbons, 32 points");

    // The motion blur pass on its own, over a scene with no particles at all: a moving box, so
    // the velocity target is non-trivial and every tile does real work.
    {
        scene::Scene box = movingBox();
        box.entities[0].transform.position.x = 0.3f;
        rendering::SceneRenderer off(*ctx, shaders);
        REQUIRE(off.init().has_value());
        const double without = medianFrameMs(off, box, kW, kH, kWarmup, kMeasured);
        box.post.motionBlurAmount = 1.0f;
        box.post.motionBlurSamples = 16;
        rendering::SceneRenderer on(*ctx, shaders);
        REQUIRE(on.init().has_value());
        const double with = medianFrameMs(on, box, kW, kH, kWarmup, kMeasured);
        WARN("motion blur pass: " << without << " -> " << with << " ms/frame at 1920x1080 (delta "
                                  << (with - without) << " ms, 16 taps, 20 px tiles)");
    }
    CHECK(ctx->errorCount() == 0);
}
