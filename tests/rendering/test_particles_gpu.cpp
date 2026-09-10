// GPU particle system: emission, simulation and indirect drawing produce visible, bounded output.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
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

int brightness(const gpu::Image8& img) {
    long sum = 0;
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
        sum += img.rgba[i] + img.rgba[i + 1] + img.rgba[i + 2];
    }
    return static_cast<int>(sum / static_cast<long>(img.width * img.height));
}
// The bounding box of pixels brighter than `threshold`, as (width, height) in pixels. Zero when
// nothing is lit. Used to ask what shape a cloud of particles actually is on screen.
std::pair<int, int> litExtent(const gpu::Image8& img, int threshold = 24) {
    int minX = static_cast<int>(img.width);
    int minY = static_cast<int>(img.height);
    int maxX = -1;
    int maxY = -1;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4;
            const int v = std::max({img.rgba[i], img.rgba[i + 1], img.rgba[i + 2]});
            if (v >= threshold) {
                minX = std::min(minX, static_cast<int>(x));
                maxX = std::max(maxX, static_cast<int>(x));
                minY = std::min(minY, static_cast<int>(y));
                maxY = std::max(maxY, static_cast<int>(y));
            }
        }
    }
    return maxX < 0 ? std::pair{0, 0} : std::pair{maxX - minX + 1, maxY - minY + 1};
}

} // namespace

TEST_CASE("Particles emit, live, and die according to their parameters", "[gpu][particles]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.camera.position = {0.0f, 0.0f, 6.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    scene::ParticleSystem sys;
    sys.name = "test";
    sys.capacity = 4096;
    sys.shape = scene::EmitterShape::Sphere;
    sys.position = {0.0f, 0.0f, 0.0f};
    sys.extent = {1.0f, 1.0f, 1.0f};
    sys.spawnRate = 20000.0f;
    sys.lifetimeMin = 0.5f;
    sys.lifetimeMax = 0.5f;
    sys.speedMin = 1.0f;
    sys.speedMax = 2.0f;
    sys.spread = 1.0f;
    sys.gravity = {0.0f, 0.0f, 0.0f};
    sys.turbulence = 0.0f;
    sys.sizeStart = 0.15f;
    sys.sizeEnd = 0.15f;
    sys.colorStart = {1.0f, 0.5f, 0.1f, 1.0f};
    sys.colorEnd = {1.0f, 0.5f, 0.1f, 1.0f};
    sys.emissive = 2.0f;
    s.particles.push_back(sys);

    FixedStepClock clock(60.0);
    // Frame 0: nothing alive yet (first tick has dt = 0 -> no emission).
    auto first = renderer.renderToImage(s, clock.tick(), 64, 64);
    REQUIRE(first.has_value());
    CHECK(ctx->errorCount() == 0);
    const int b0 = brightness(*first);

    int bMid = 0;
    for (int i = 0; i < 12; ++i) {
        auto img = renderer.renderToImage(s, clock.tick(), 64, 64);
        REQUIRE(img.has_value());
        bMid = brightness(*img);
    }
    CHECK(bMid > b0 + 5);
    CHECK(renderer.stats().particles.systems == 1);
    CHECK(renderer.stats().particles.capacity == 4096);
    CHECK(renderer.stats().particles.emittedThisFrame > 0);

    // Stop emitting: after more than one lifetime everything is dead again.
    s.particles[0].spawnRate = 0.0f;
    int bEnd = bMid;
    for (int i = 0; i < 45; ++i) { // 0.75 s > 0.5 s lifetime
        auto img = renderer.renderToImage(s, clock.tick(), 64, 64);
        REQUIRE(img.has_value());
        bEnd = brightness(*img);
    }
    CHECK(bEnd <= b0 + 1);
    CHECK(ctx->errorCount() == 0);

    // Disabled systems draw nothing and are not simulated.
    s.particles[0].spawnRate = 20000.0f;
    s.particles[0].enabled = false;
    auto off = renderer.renderToImage(s, clock.tick(), 64, 64);
    REQUIRE(off.has_value());
    CHECK(renderer.stats().particles.systems == 0);
}

TEST_CASE("Particle burst produces an immediate flash and capacity bounds emission", "[gpu][particles]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.camera.position = {0.0f, 0.0f, 6.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    scene::ParticleSystem sys;
    sys.capacity = 256;
    sys.shape = scene::EmitterShape::Sphere;
    sys.extent = {0.5f, 0.5f, 0.5f};
    sys.spawnRate = 0.0f;
    sys.lifetimeMin = sys.lifetimeMax = 2.0f;
    sys.turbulence = 0.0f;
    sys.gravity = {0.0f, 0.0f, 0.0f};
    sys.sizeStart = sys.sizeEnd = 0.2f;
    sys.emissive = 3.0f;
    s.particles.push_back(sys);
    FixedStepClock clock(60.0);
    auto quiet = renderer.renderToImage(s, clock.tick(), 48, 48);
    REQUIRE(quiet.has_value());
    s.particles[0].burst = 100000.0f; // far beyond capacity: clamped, no GPU errors
    auto flash = renderer.renderToImage(s, clock.tick(), 48, 48);
    REQUIRE(flash.has_value());
    CHECK(renderer.stats().particles.emittedThisFrame == 256);
    CHECK(brightness(*flash) > brightness(*quiet) + 10);
    s.particles[0].burst = 0.0f;
    auto after = renderer.renderToImage(s, clock.tick(), 48, 48);
    REQUIRE(after.has_value());
    CHECK(renderer.stats().particles.emittedThisFrame == 0);
    CHECK(brightness(*after) > brightness(*quiet) + 10); // still alive
    CHECK(ctx->errorCount() == 0);
    if (const char* dumpDir = std::getenv("AVGEN_DUMP_DIR")) {
        REQUIRE(gpu::writePpm(*after, std::filesystem::path(dumpDir) / "particles.ppm").has_value());
    }
}

namespace {
// A pool that recycles slots heavily: 2000/s into 4096 slots with a 1 s life, big overlapping
// additive sprites, and every force on so the full simulate path runs.
scene::ParticleSystem recyclingSystem() {
    scene::ParticleSystem sys;
    sys.name = "det";
    sys.capacity = 4096;
    sys.seed = 7;
    sys.shape = scene::EmitterShape::Sphere;
    sys.position = {0.0f, 0.0f, 0.0f};
    sys.extent = {1.0f, 1.0f, 1.0f};
    sys.spawnRate = 2000.0f;
    sys.lifetimeMin = sys.lifetimeMax = 1.0f;
    sys.speedMin = 0.5f;
    sys.speedMax = 2.0f;
    sys.spread = 1.0f;
    sys.gravity = {0.0f, -0.3f, 0.0f};
    sys.drag = 0.3f;
    sys.turbulence = 1.0f;
    sys.attractorPosition = {0.0f, 0.0f, 0.0f};
    sys.attractorStrength = 1.5f;
    sys.attractorRadius = 4.0f;
    sys.orbit = 1.0f;
    sys.sizeStart = 0.35f;
    sys.sizeEnd = 0.1f;
    sys.colorStart = {1.0f, 0.6f, 0.2f, 1.0f};
    sys.colorEnd = {0.2f, 0.4f, 1.0f, 0.0f};
    sys.emissive = 2.0f;
    sys.blend = scene::ParticleBlend::Additive;
    return sys;
}

scene::Scene sceneWith(const scene::ParticleSystem& sys) {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.camera.position = {0.0f, 0.0f, 6.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.particles.push_back(sys);
    return s;
}

// Renders `frames` frames with a fresh renderer and returns every frame's readback hash.
std::vector<std::uint64_t> hashSequence(gpu::Context& ctx, const scene::Scene& s, int frames, double fps) {
    gpu::ShaderLibrary shaders(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FixedStepClock clock(fps);
    std::vector<std::uint64_t> hashes;
    hashes.reserve(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        auto img = renderer.renderToImage(s, clock.tick(), 96, 96);
        REQUIRE(img.has_value());
        hashes.push_back(gpu::hashImage(*img));
    }
    return hashes;
}

// CPU mirror of the pool's occupancy: same fractional-carry emission as ParticleRenderer, the
// GPU's clamp to the free slots, and the f32 age accumulation of cs_simulate. Requires
// lifetimeMin == lifetimeMax so every particle's life is exactly that value.
class PoolModel {
public:
    explicit PoolModel(std::uint32_t capacity) : capacity_(capacity) {}

    void step(const scene::ParticleSystem& sys, const FrameTime& time) {
        const double dt = std::clamp(time.deltaTime, 0.0, 0.1);
        carry_ += static_cast<double>(sys.spawnRate) * dt;
        auto requested = static_cast<std::uint32_t>(std::floor(carry_));
        carry_ -= requested;
        requested += static_cast<std::uint32_t>(std::max(0.0f, sys.burst));
        requested = std::min(requested, capacity_);
        const std::uint32_t spawned = std::min(requested, capacity_ - alive_); // GPU clamp
        if (spawned > 0) {
            cohorts_.push_back({0.0f, sys.lifetimeMin, spawned});
            alive_ += spawned;
        }
        const auto dtF = static_cast<float>(dt);
        for (auto& c : cohorts_) {
            c.age += dtF;
        }
        while (!cohorts_.empty() && cohorts_.front().age >= cohorts_.front().life) {
            alive_ -= cohorts_.front().count;
            cohorts_.pop_front();
        }
    }
    [[nodiscard]] std::uint32_t alive() const { return alive_; }

private:
    struct Cohort {
        float age;
        float life;
        std::uint32_t count;
    };
    std::uint32_t capacity_;
    double carry_ = 0.0;
    std::uint32_t alive_ = 0;
    std::deque<Cohort> cohorts_; // oldest first; equal lifetimes so deaths are in emission order
};
} // namespace

TEST_CASE("Particle emission, compaction and draw order are bit-deterministic across runs", "[gpu][particles]") {
    constexpr int kFrames = 200;
    auto ctx = makeContext();
    const scene::Scene s = sceneWith(recyclingSystem());
    const auto first = hashSequence(*ctx, s, kFrames, 60.0);
    const auto second = hashSequence(*ctx, s, kFrames, 60.0);
    REQUIRE(first.size() == second.size());
    // Something must actually be drawn and change over time for the comparison to mean anything.
    CHECK(first.front() != first[kFrames / 2]);
    CHECK(first[kFrames / 2] != first.back());
    for (int i = 0; i < kFrames; ++i) {
        INFO("frame " << i);
        REQUIRE(first[static_cast<std::size_t>(i)] == second[static_cast<std::size_t>(i)]);
    }
    CHECK(ctx->errorCount() == 0);

    // Unrelated GPU work in between (a differently configured pool) and a fresh context.
    {
        auto other = recyclingSystem();
        other.capacity = 1000; // not a multiple of the scan block
        other.spawnRate = 9000.0f;
        other.turbulence = 0.0f;
        (void)hashSequence(*ctx, sceneWith(other), 40, 30.0);
    }
    auto ctx2 = makeContext();
    const auto third = hashSequence(*ctx2, s, kFrames, 60.0);
    REQUIRE(third.size() == first.size());
    for (int i = 0; i < kFrames; ++i) {
        INFO("frame " << i);
        REQUIRE(first[static_cast<std::size_t>(i)] == third[static_cast<std::size_t>(i)]);
    }
    CHECK(ctx2->errorCount() == 0);
}

TEST_CASE("Particle alive and dead counts match a CPU model exactly", "[gpu][particles]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    auto sys = recyclingSystem();
    sys.capacity = 1000; // 31.25 spawns/frame x 64 frames of life > 1000: the free-slot clamp binds
    scene::Scene s = sceneWith(sys);
    // 64 fps: dt = 1/64 is exact in f32, so a 1 s life is exactly 64 simulate steps.
    FixedStepClock clock(64.0);
    PoolModel model(sys.capacity);

    auto stepAndCheck = [&](int frame) {
        const FrameTime time = clock.tick();
        auto img = renderer.renderToImage(s, time, 32, 32);
        REQUIRE(img.has_value());
        model.step(s.particles[0], time);
        auto counts = renderer.particles().readCounts(0);
        REQUIRE(counts.has_value());
        INFO("frame " << frame << " model alive " << model.alive());
        CHECK(counts->alive == model.alive());
        CHECK(counts->alive + counts->dead == sys.capacity);
        return counts->alive;
    };

    // Fill up: the pool must hit capacity exactly and stay there.
    std::uint32_t peak = 0;
    for (int f = 0; f < 100; ++f) {
        peak = std::max(peak, stepAndCheck(f));
    }
    CHECK(peak == sys.capacity);
    // Continuous emission alone never blocks: the exact steady-state count is the model's.
    CHECK(model.alive() > 0);

    // Stop emitting: counts drain in emission order down to exactly zero.
    s.particles[0].spawnRate = 0.0f;
    std::uint32_t last = 1;
    for (int f = 100; f < 170; ++f) {
        last = stepAndCheck(f);
    }
    CHECK(last == 0);

    // A burst larger than the pool is clamped to the free slots (capacity, then nothing).
    s.particles[0].burst = 100000.0f;
    CHECK(stepAndCheck(170) == sys.capacity);
    CHECK(renderer.stats().particles.emittedThisFrame == sys.capacity);
    s.particles[0].burst = 0.0f;
    CHECK(stepAndCheck(171) == sys.capacity);
    s.particles[0].burst = 5.0f;
    CHECK(stepAndCheck(172) == sys.capacity); // still full: the spawns are dropped
    CHECK(ctx->errorCount() == 0);
}

// Hidden performance probe: run with `avgen_render_tests "[.perf]"`. Reports GPU time per frame
// for a one-million-particle pool at 1280x720; no assertions beyond validity.
TEST_CASE("One million particles simulate and draw", "[.perf][particles]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    scene::Scene s;
    s.camera.position = {0.0f, 0.0f, 8.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    scene::ParticleSystem sys;
    sys.capacity = 1u << 20;
    sys.shape = scene::EmitterShape::Sphere;
    sys.extent = {2.0f, 2.0f, 2.0f};
    sys.spawnRate = 400000.0f;
    sys.lifetimeMin = 2.0f;
    sys.lifetimeMax = 3.0f;
    sys.turbulence = 1.0f;
    sys.sizeStart = 0.01f;
    sys.sizeEnd = 0.0f;
    s.particles.push_back(sys);
    FixedStepClock clock(60.0);
    double gpuSum = 0.0;
    int counted = 0;
    for (int i = 0; i < 90; ++i) {
        auto img = renderer.renderToImage(s, clock.tick(), 1280, 720);
        REQUIRE(img.has_value());
        if (i >= 30 && renderer.stats().gpuFrameMs >= 0.0) {
            gpuSum += renderer.stats().gpuFrameMs;
            ++counted;
        }
    }
    CHECK(ctx->errorCount() == 0);
    WARN("1M particles: mean GPU " << (counted ? gpuSum / counted : -1.0) << " ms/frame at 1280x720 (steady state ~"
                                    << std::min<double>(sys.capacity, 400000.0 * 2.5) << " alive)");
}

TEST_CASE("a disc emitter faces its direction and has two radii", "[gpu][particles]") {
    // The disc used to be nailed to the XZ plane and to read only extent.x, so it could neither
    // point anywhere nor be an ellipse. The shipped scenes had been authored as though it could:
    // `[11, 1, 11]` and `[46, 2, 46]` are two radii and a thickness written into a field that was
    // using one of them.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const auto emit = [&](glm::vec3 direction, glm::vec3 extent, glm::vec3 cameraPos) {
        scene::Scene s;
        s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
        s.environment.showSkybox = false;
        s.environment.environmentIntensity = 0.0f;
        s.post.bloomEnabled = false; // bloom smears the cloud across the frame and hides its shape
        s.post.tonemap = scene::TonemapOperator::Clamp;
        s.camera.position = cameraPos;
        s.camera.target = {0.0f, 0.0f, 0.0f};
        s.camera.fovYRadians = 0.9f;
        scene::ParticleSystem sys;
        sys.name = "disc";
        sys.capacity = 8192;
        sys.shape = scene::EmitterShape::Disc;
        sys.position = {0.0f, 0.0f, 0.0f};
        sys.direction = direction;
        sys.extent = extent;
        sys.spawnRate = 60000.0f;
        sys.lifetimeMin = sys.lifetimeMax = 4.0f;
        sys.speedMin = sys.speedMax = 0.0f; // stay where they were emitted: the disc *is* the cloud
        sys.spread = 0.0f;
        sys.gravity = {0.0f, 0.0f, 0.0f};
        sys.turbulence = 0.0f;
        sys.sizeStart = sys.sizeEnd = 0.05f;
        sys.colorStart = sys.colorEnd = {1.0f, 1.0f, 1.0f, 1.0f};
        sys.emissive = 4.0f;
        s.particles.push_back(sys);
        FixedStepClock clock(60.0);
        gpu::Image8 last;
        for (int i = 0; i < 12; ++i) {
            auto img = renderer.renderToImage(s, clock.tick(), 128, 128);
            REQUIRE(img.has_value());
            last = std::move(*img);
        }
        return litExtent(last);
    };

    SECTION("the disc lies in the plane its direction is normal to") {
        // Facing +Z: a disc in XY. Seen from +Z it is a filled circle; from +Y it is edge on.
        const auto faceOn = emit({0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 9.0f});
        const auto edgeOn = emit({0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 9.0f, 0.0f});
        INFO("face-on " << faceOn.first << "x" << faceOn.second << ", edge-on " << edgeOn.first << "x"
                        << edgeOn.second);
        REQUIRE(faceOn.second > 0);
        CHECK(faceOn.second > edgeOn.second * 2); // a circle from the front, a line from above
    }

    SECTION("extent.x and extent.z are the two radii") {
        // Facing +Y, the old orientation, so this section isolates the second radius from the
        // orientation change. Seen from above, x = 2 and z = 0.5 must be four times as wide as tall.
        const auto wide = emit({0.0f, 1.0f, 0.0f}, {2.0f, 1.0f, 0.5f}, {0.0f, 14.0f, 0.01f});
        INFO("wide disc from above: " << wide.first << "x" << wide.second);
        REQUIRE(wide.first > 0);
        CHECK(wide.first > wide.second * 2);
    }
}
