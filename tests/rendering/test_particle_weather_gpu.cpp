// ADR-520: the three behaviours that live only in the shader.
//
// Each pair is shaped the way ADR-182 requires: an ARM where the feature must change the picture,
// and a CONTROL where it must change NOTHING and "nothing" is checked as byte equality.
//
// The scatter pair exists because of a real regression found by eye and then measured. The gain
// was `1 + strength * (4*pi*hg - 1)` -- the correct normalisation of a phase function, and an
// artist control that multiplies by a NEGATIVE number at the strengths the effect wants. The
// dust-motes scene rendered 74 pixels above 200 at strength 0 and 0 pixels at strength 1: the
// motes had not dimmed, they had been clamped away. Nothing in the suite could have caught it,
// which is why this file exists.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "support/image_diff.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>

using namespace avgen;

// ADR-362: never compare two frames with CHECK(a == b); Catch2 stringifies both and the process
// dies inside the assertion handler, printing a FAILED with no `with expansion:`.
#define CHECK_IDENTICAL(a, b)                                                                      \
    do {                                                                                           \
        const auto d__ = ::avgen::testing::byteDiff((a).rgba, (b).rgba);                           \
        INFO(d__.describe());                                                                      \
        CHECK(d__.identical());                                                                    \
    } while (false)

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

long totalBrightness(const gpu::Image8& img) {
    long sum = 0;
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
        sum += img.rgba[i] + img.rgba[i + 1] + img.rgba[i + 2];
    }
    return sum;
}

std::size_t litPixels(const gpu::Image8& img, int threshold) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
        if (img.rgba[i] > threshold) {
            ++n;
        }
    }
    return n;
}

// A still cloud in front of the camera on black. Still, because what is being measured is the
// shading and not the simulation.
scene::Scene cloudScene() {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.skyIntensity = 0.0f;
    s.camera.position = {0.0f, 0.0f, 6.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    // The key light comes from behind the cloud and slightly to one side, so the camera is looking
    // roughly INTO it -- which is the geometry the forward-scattering lobe is about.
    s.environment.sky.sunDirection = glm::normalize(glm::vec3(0.25f, 0.10f, 1.0f));
    s.environment.sky.sunColor = {1.0f, 0.95f, 0.85f};
    s.environment.sky.useKeyLight = false;

    scene::ParticleSystem sys;
    sys.name = "cloud";
    sys.capacity = 2048;
    sys.shape = scene::EmitterShape::Box;
    sys.position = {0.0f, 0.0f, 0.0f};
    sys.extent = {1.6f, 1.6f, 0.35f};
    sys.spawnRate = 8000.0f;
    sys.lifetimeMin = 10.0f;
    sys.lifetimeMax = 10.0f;
    sys.speedMin = 0.0f;
    sys.speedMax = 0.0f;
    sys.spread = 0.0f;
    sys.gravity = {0.0f, 0.0f, 0.0f};
    sys.turbulence = 0.0f;
    sys.drag = 0.0f;
    sys.sizeStart = 0.12f;
    sys.sizeEnd = 0.12f;
    sys.colorStart = {0.5f, 0.5f, 0.5f, 1.0f};
    sys.colorEnd = {0.5f, 0.5f, 0.5f, 1.0f};
    sys.emissive = 1.0f;
    sys.softness = 0.0f;
    sys.fogCoupling = 0.0f;
    s.particles.push_back(std::move(sys));
    return s;
}

// A FRESH renderer per arm, for the reason test_soft_particles_gpu.cpp documents: the pool is
// reset when the `scene::Scene*` identity changes, and two stack temporaries can share an address.
gpu::Image8 render(gpu::Context& ctx, gpu::ShaderLibrary& shaders, const scene::Scene& scene, int frames = 6) {
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FixedStepClock clock(60.0);
    gpu::Image8 last;
    for (int i = 0; i < frames; ++i) {
        auto img = renderer.renderToImage(scene, clock.tick(), 128, 128);
        REQUIRE(img.has_value());
        last = std::move(*img);
    }
    return last;
}

} // namespace

TEST_CASE("ADR-520 scattering ADDS light to a mote and never removes it", "[gpu][particles][weather][scatter]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});

    scene::Scene off = cloudScene();
    scene::Scene low = cloudScene();
    low.particles[0].scatterStrength = 1.0f;
    low.particles[0].scatterAnisotropy = 0.7f;
    scene::Scene high = cloudScene();
    high.particles[0].scatterStrength = 4.0f;
    high.particles[0].scatterAnisotropy = 0.7f;

    const gpu::Image8 a = render(*ctx, shaders, off);
    const gpu::Image8 b = render(*ctx, shaders, low);
    const gpu::Image8 c = render(*ctx, shaders, high);
    CHECK(ctx->errorCount() == 0);

    // The premise: the cloud is actually on screen. Without it every comparison below holds for a
    // black frame (ADR-182, and "a black frame and a nearly-black frame are identical in a hash").
    REQUIRE(litPixels(a, 20) > 200);

    const long sumOff = totalBrightness(a);
    const long sumLow = totalBrightness(b);
    const long sumHigh = totalBrightness(c);
    INFO("off " << sumOff << "  strength 1 " << sumLow << "  strength 4 " << sumHigh);

    // THE REGRESSION. The old formula made `strength 1` DIMMER than off across most of the frame
    // and `strength 4` black. Monotone non-decreasing is the property the control has to have.
    CHECK(sumLow >= sumOff);
    CHECK(sumHigh >= sumLow);
    // ...and it must actually do something, or "never removes light" is satisfied by doing nothing.
    CHECK(static_cast<double>(sumHigh) > 1.05 * static_cast<double>(sumOff));
    CHECK(litPixels(c, 20) >= litPixels(a, 20));
}

TEST_CASE("ADR-520 scatterStrength 0 is byte-identical to no scattering at all",
          "[gpu][particles][weather][scatter][determinism]") {
    // THE CONTROL. Every system in the repository has scatterStrength 0, so this is the case that
    // says none of them moved a pixel when the feature landed. Byte equality, not a tolerance:
    // `scatterGain` returns before it touches the sun.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});

    scene::Scene plain = cloudScene();
    scene::Scene alsoPlain = cloudScene();
    alsoPlain.particles[0].scatterStrength = 0.0f;
    alsoPlain.particles[0].scatterAnisotropy = -0.9f; // set, and must be ignored
    const gpu::Image8 a = render(*ctx, shaders, plain);
    const gpu::Image8 b = render(*ctx, shaders, alsoPlain);
    REQUIRE(litPixels(a, 20) > 200);
    CHECK_IDENTICAL(a, b);
}

TEST_CASE("ADR-520 a wrapping volume keeps its particles inside the box", "[gpu][particles][weather][wrap]") {
    // A thin slab of fast-falling particles. Without wrapping they leave the slab within a few
    // frames and the frame goes dark; with it they re-enter on the opposite face and the picture
    // is still populated after a second of simulation.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});

    const auto fallingScene = [](bool wrap) {
        scene::Scene s = cloudScene();
        scene::ParticleSystem& p = s.particles[0];
        p.extent = {1.6f, 0.9f, 0.35f};
        p.speedMin = 4.0f;
        p.speedMax = 4.0f;
        p.direction = {0.0f, -1.0f, 0.0f};
        p.spread = 0.0f;
        p.lifetimeMin = 10.0f;
        p.lifetimeMax = 10.0f;
        p.spawnRate = 4000.0f;
        p.volumeWrap = wrap;
        return s;
    };
    const scene::Scene freeFall = fallingScene(false);
    const scene::Scene wrapped = fallingScene(true);

    // 60 frames: a second, which at 4 m/s is 4 m -- far outside a 1.8 m box.
    const gpu::Image8 a = render(*ctx, shaders, freeFall, 60);
    const gpu::Image8 b = render(*ctx, shaders, wrapped, 60);
    CHECK(ctx->errorCount() == 0);

    const std::size_t freeLit = litPixels(a, 20);
    const std::size_t wrapLit = litPixels(b, 20);
    INFO("free fall lit " << freeLit << "  wrapped lit " << wrapLit);
    // The premise: the wrapped arm really is still full. Without it "more than the other one"
    // would be satisfied by two nearly-black frames.
    REQUIRE(wrapLit > 300);
    CHECK(wrapLit > freeLit * 2);
}

TEST_CASE("ADR-520 volumeWrap off is byte-identical to before it existed",
          "[gpu][particles][weather][wrap][determinism]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    scene::Scene a = cloudScene();
    scene::Scene b = cloudScene();
    b.particles[0].volumeWrap = false;
    b.particles[0].volumeFollow = glm::vec3(0.0f);
    const gpu::Image8 ia = render(*ctx, shaders, a);
    const gpu::Image8 ib = render(*ctx, shaders, b);
    REQUIRE(litPixels(ia, 20) > 200);
    CHECK_IDENTICAL(ia, ib);
}

TEST_CASE("ADR-520 a splash response turns a drop into a ground ring", "[gpu][particles][weather][splash]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});

    const auto dropScene = [](scene::CollisionResponse response) {
        scene::Scene s = cloudScene();
        s.camera.position = {0.0f, 1.2f, 4.0f};
        s.camera.target = {0.0f, -0.4f, 0.0f};
        scene::ParticleSystem& p = s.particles[0];
        p.position = {0.0f, 1.2f, 0.0f};
        p.extent = {1.4f, 0.1f, 1.0f};
        p.direction = {0.0f, -1.0f, 0.0f};
        p.spread = 0.0f;
        p.speedMin = 3.0f;
        p.speedMax = 3.0f;
        p.lifetimeMin = 0.6f;
        p.lifetimeMax = 0.6f;
        p.spawnRate = 2000.0f;
        p.sizeStart = 0.02f;
        p.sizeEnd = 0.02f;
        p.collision = response;
        p.collisionHeight = 0.0f;
        p.splashLifetime = 0.5f;
        p.splashSize = 14.0f;
        p.ringThickness = 0.25f;
        return s;
    };
    // Kill and Splash differ in exactly one thing: whether the drop leaves something behind. So
    // the extra light below is the ring and nothing else -- not the drops, which both arms have.
    const scene::Scene killed = dropScene(scene::CollisionResponse::Kill);
    const scene::Scene splashed = dropScene(scene::CollisionResponse::Splash);
    const gpu::Image8 a = render(*ctx, shaders, killed, 40);
    const gpu::Image8 b = render(*ctx, shaders, splashed, 40);
    CHECK(ctx->errorCount() == 0);

    const long sumKill = totalBrightness(a);
    const long sumSplash = totalBrightness(b);
    INFO("kill " << sumKill << "  splash " << sumSplash);
    REQUIRE(litPixels(a, 20) > 100); // the premise: the drops themselves are on screen in both
    CHECK(sumSplash > sumKill);
    CHECK(static_cast<double>(sumSplash) > 1.10 * static_cast<double>(sumKill));
}

TEST_CASE("ADR-520 CollisionResponse::None is byte-identical to no collision code at all",
          "[gpu][particles][weather][splash][determinism]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    scene::Scene a = cloudScene();
    scene::Scene b = cloudScene();
    b.particles[0].collision = scene::CollisionResponse::None;
    b.particles[0].collisionHeight = 5.0f;  // set high enough to kill everything, and ignored
    b.particles[0].splashSize = 99.0f;
    const gpu::Image8 ia = render(*ctx, shaders, a);
    const gpu::Image8 ib = render(*ctx, shaders, b);
    REQUIRE(litPixels(ia, 20) > 200);
    CHECK_IDENTICAL(ia, ib);
}

TEST_CASE("ADR-520 a synchronised pulse takes the whole field bright and then dark",
          "[gpu][particles][weather][pulse]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});

    // The measurement is the RATIO of the pulsed arm to the unpulsed one at the same frame, not
    // the change in either of them over time. The first version of this case compared brightness
    // at two times within one arm and failed honestly: the pool is still filling over the first
    // frames, so BOTH arms brightened by about the same amount (2.35M -> 4.06M pulsed,
    // 2.58M -> 4.16M unpulsed) and the pulse was a rounding error on top of the fill. Two arms at
    // one frame have the identical pool -- the pulse is a shading term and changes no simulation
    // state -- so the fill cancels exactly and what is left is the gain.
    //
    // sync 1, depth 1, sharpness 1, rate 1 Hz: gain(t) = 0.5 + 0.5 sin(2*pi*t), so t = 0.25 s is
    // the peak and t = 0.75 s is the trough, and the trough is zero.
    const auto pulsed = [](float rate) {
        scene::Scene s = cloudScene();
        s.particles[0].pulseRate = rate;
        s.particles[0].pulseDepth = 1.0f;
        s.particles[0].pulseSync = 1.0f;
        s.particles[0].pulseSharpness = 1.0f;
        return s;
    };
    const scene::Scene on = pulsed(1.0f);
    const scene::Scene off = pulsed(0.0f);
    // FixedStepClock's first tick is t = 0, so n ticks ends at t = (n - 1) / 60.
    constexpr int kPeak = 16;   // t = 0.25 s
    constexpr int kTrough = 46; // t = 0.75 s

    const double onPeak = static_cast<double>(totalBrightness(render(*ctx, shaders, on, kPeak)));
    const double onTrough = static_cast<double>(totalBrightness(render(*ctx, shaders, on, kTrough)));
    const double offPeak = static_cast<double>(totalBrightness(render(*ctx, shaders, off, kPeak)));
    const double offTrough = static_cast<double>(totalBrightness(render(*ctx, shaders, off, kTrough)));
    CHECK(ctx->errorCount() == 0);

    // The premise: the unpulsed cloud is on screen at both times, so neither ratio is 0/0.
    REQUIRE(offPeak > 1.0e5);
    REQUIRE(offTrough > 1.0e5);

    const double atPeak = onPeak / offPeak;
    const double atTrough = onTrough / offTrough;
    INFO("gain at the peak " << atPeak << ", at the trough " << atTrough);
    CHECK(atPeak > 0.75);   // near 1: the pulse is at full brightness
    CHECK(atTrough < 0.25); // near 0: the field is dark
}

TEST_CASE("ADR-520 pulseRate 0 is byte-identical to no pulse at all",
          "[gpu][particles][weather][pulse][determinism]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    scene::Scene a = cloudScene();
    scene::Scene b = cloudScene();
    b.particles[0].pulseRate = 0.0f;
    b.particles[0].pulseDepth = 1.0f;   // set, and must be ignored
    b.particles[0].pulseSync = 1.0f;
    b.particles[0].pulseSharpness = 12.0f;
    const gpu::Image8 ia = render(*ctx, shaders, a);
    const gpu::Image8 ib = render(*ctx, shaders, b);
    REQUIRE(litPixels(ia, 20) > 200);
    CHECK_IDENTICAL(ia, ib);
}
