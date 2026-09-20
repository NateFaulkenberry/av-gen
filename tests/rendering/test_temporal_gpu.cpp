// ADR-400: the temporal history ring and frame echo.
//
// Four properties, and the order matters because each makes the next mean something:
//
//   1. A scene that asks for no temporal effect renders EXACTLY as it did before the stage
//      existed. Proved differentially -- off against off -- because there is no "before" build to
//      compare with (ADR-368).
//   2. The echo is cold on the first frame and warm once the ring has filled. This is the
//      disclosure the whole design turns on: a history that has not filled produces a SHORTER
//      echo, never a wrong one, and never one built from a frame before the seek.
//   3. The same second from a fresh renderer is the same frame, twice. ADR-091's guarantee, which
//      a temporal effect is the most likely thing in the engine to break.
//   4. Re-rendering the same frame index reproduces it. The repeat case AoRenderer solved and
//      SYM-TERRAIN-1 got wrong; a ring that advances its write cursor on a repeat fails this.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "assets/image.hpp"
#include "support/image_diff.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <memory>

using namespace avgen;

// byteDiff, never REQUIRE over the buffer: a multi-MB comparison kills Catch2 and prints FAILED
// with no expansion, destroying the evidence of the difference it found (ADR-362).
#define CHECK_IDENTICAL(a, b)                                                                      \
    do {                                                                                           \
        const auto d__ = ::avgen::testing::byteDiff((a).rgba, (b).rgba);                           \
        INFO(d__.describe());                                                                      \
        CHECK(d__.identical());                                                                    \
    } while (false)
#define CHECK_DIFFERS(a, b)                                                                        \
    do {                                                                                           \
        const auto d__ = ::avgen::testing::byteDiff((a).rgba, (b).rgba);                           \
        INFO(d__.describe());                                                                      \
        CHECK_FALSE(d__.identical());                                                              \
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

// A bright object crossing a dark frame. An echo is a trail of where a thing WAS, so the fixture
// has to have somewhere it was: a static scene would give an echo identical to the current frame
// and every assertion below would pass against a completely broken implementation.
scene::Scene movingLight() {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.camera.position = {0.0f, 0.0f, 12.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};

    scene::Entity e;
    e.name = "mover";
    e.mesh = s.addMesh(scene::makeIcosphere(0.6f, 2));
    e.material.baseColor = {1.0f, 1.0f, 1.0f};
    e.material.emissiveColor = {1.0f, 0.85f, 0.6f};
    e.material.emissiveIntensity = 6.0f;
    s.entities.push_back(std::move(e));
    return s;
}

// Positions the mover as a function of the frame, so successive frames genuinely differ.
void placeAt(scene::Scene& s, int frame) {
    s.entities.front().transform.position = {-4.0f + 0.55f * static_cast<float>(frame), 0.0f, 0.0f};
}

struct Shot {
    gpu::Image8 image;
    rendering::TemporalStats stats;
};

// Plays `frames` frames forward from t0 on ONE renderer -- which is the point: the ring only fills
// by frames going past it.
Shot play(gpu::Context& ctx, gpu::ShaderLibrary& shaders, scene::Scene s, int frames, double t0 = 0.0) {
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FixedStepClock clock(60.0);
    clock.restartAt(t0);
    Shot out;
    for (int i = 0; i < frames; ++i) {
        placeAt(s, i);
        auto img = renderer.renderToImage(s, clock.tick(), 160, 160);
        REQUIRE(img.has_value());
        out.image = std::move(*img);
    }
    out.stats = renderer.stats().temporal;
    return out;
}

scene::Scene withEcho(scene::Scene s, int frames, float strength) {
    s.temporal.echo.enabled = true;
    s.temporal.echo.frames = frames;
    s.temporal.echo.strength = strength;
    s.temporal.echo.decay = 0.8f;
    return s;
}

} // namespace

TEST_CASE("a scene that asks for no temporal effect is unchanged by the stage", "[gpu][temporal]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});

    const scene::Scene plain = movingLight();
    CHECK_FALSE(plain.temporal.anyEnabled());
    CHECK(plain.temporal.historyFrames() == 0u);

    const Shot a = play(*ctx, shaders, plain, 8);
    const Shot b = play(*ctx, shaders, plain, 8);
    CHECK_IDENTICAL(a.image, b.image);

    // Nothing allocated. §8's rule is not "keep the ring small", it is "do not keep it at all when
    // nobody asked" -- and a stat is how that is checked rather than asserted.
    CHECK(a.stats.historyBytes == 0u);
    CHECK(a.stats.framesNeeded == 0u);
    CHECK(ctx->errorCount() == 0);

    // The control: the same fixture WITH an echo must differ, or the identity above is telling us
    // only that the fixture is static.
    const Shot lit = play(*ctx, shaders, withEcho(plain, 6, 0.9f), 8);
    CHECK_DIFFERS(a.image, lit.image);
    CHECK(lit.stats.historyBytes > 0u);
}

TEST_CASE("the echo is cold on the first frame and warm once the ring has filled", "[gpu][temporal]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const scene::Scene echo = withEcho(movingLight(), 6, 0.9f);

    // One frame: the ring holds nothing, so there is nothing to echo. The picture must equal the
    // no-echo picture EXACTLY. This is the property that makes a cold history honest -- it gives a
    // shorter echo, not a wrong one.
    const Shot coldEcho = play(*ctx, shaders, echo, 1);
    const Shot coldPlain = play(*ctx, shaders, movingLight(), 1);
    CHECK_IDENTICAL(coldEcho.image, coldPlain.image);
    CHECK(coldEcho.stats.framesValid == 1u); // captured this frame, but read none
    CHECK(coldEcho.stats.framesNeeded == 6u);
    CHECK(coldEcho.stats.settling);
    CHECK_FALSE(coldEcho.stats.stalled);

    // Eight frames: the ring is full, the echo is reading, the picture differs.
    const Shot warmEcho = play(*ctx, shaders, echo, 8);
    const Shot warmPlain = play(*ctx, shaders, movingLight(), 8);
    CHECK_DIFFERS(warmEcho.image, warmPlain.image);
    CHECK(warmEcho.stats.framesValid == 6u);
    CHECK_FALSE(warmEcho.stats.settling);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("the echo reaches further the more frames it is given", "[gpu][temporal]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});

    // A longer history is a longer trail. Without this the frame count could be bookkeeping about
    // a ring nobody samples -- a correct value that is not a reached value (ADR-387).
    const Shot two = play(*ctx, shaders, withEcho(movingLight(), 2, 0.9f), 12);
    const Shot ten = play(*ctx, shaders, withEcho(movingLight(), 10, 0.9f), 12);
    CHECK_DIFFERS(two.image, ten.image);
    CHECK(two.stats.framesValid == 2u);
    CHECK(ten.stats.framesValid == 10u);
    // And the ring that holds more costs more, measured rather than assumed.
    CHECK(ten.stats.historyBytes > two.stats.historyBytes);

    // Strength zero is the identity: the pass still runs, and must add nothing.
    const Shot silent = play(*ctx, shaders, withEcho(movingLight(), 10, 0.0f), 12);
    const Shot plain = play(*ctx, shaders, movingLight(), 12);
    CHECK_IDENTICAL(silent.image, plain.image);
}

TEST_CASE("the same second is the same frame however many times it is rendered", "[gpu][temporal][determinism]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const scene::Scene echo = withEcho(movingLight(), 6, 0.9f);

    // Two fresh renderers, same frames, same result. A ring seeded from anything but the frames
    // that went past it fails here.
    CHECK_IDENTICAL(play(*ctx, shaders, echo, 10).image, play(*ctx, shaders, echo, 10).image);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("re-rendering a frame index reproduces it", "[gpu][temporal][determinism]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    scene::Scene s = withEcho(movingLight(), 6, 0.9f);

    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FixedStepClock clock(60.0);
    clock.restartAt(0.0);
    FrameTime last{};
    for (int i = 0; i < 8; ++i) {
        placeAt(s, i);
        last = clock.tick();
        auto img = renderer.renderToImage(s, last, 160, 160);
        REQUIRE(img.has_value());
    }
    // The same FrameTime twice. The ring must restore the write cursor and the valid count it had
    // at the start of this index; advancing them would make the second render read a different set
    // of layers and produce a different picture from the first. That is precisely the bug
    // AoRenderer's `repeat` branch exists to prevent.
    auto first = renderer.renderToImage(s, last, 160, 160);
    REQUIRE(first.has_value());
    auto second = renderer.renderToImage(s, last, 160, 160);
    REQUIRE(second.has_value());
    CHECK_IDENTICAL(*first, *second);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("a discontinuity drops the history rather than smearing across it", "[gpu][temporal][determinism]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    scene::Scene s = withEcho(movingLight(), 6, 0.9f);

    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FixedStepClock clock(60.0);
    clock.restartAt(0.0);
    for (int i = 0; i < 10; ++i) {
        placeAt(s, i);
        REQUIRE(renderer.renderToImage(s, clock.tick(), 160, 160).has_value());
    }
    CHECK(renderer.stats().temporal.framesValid == 6u);

    // A seek. The ring must be empty afterwards, and the next frame must therefore match a frame
    // from a renderer that never played at all -- not a frame carrying ten frames of pre-seek
    // trail. A ring that survived this would put the mover's old positions into a post-seek frame,
    // which is the silent divergence the whole ADR exists to prevent.
    renderer.resetTemporalHistory();
    CHECK(renderer.stats().temporal.framesValid == 6u); // stats are last frame's until the next render

    clock.restartAt(0.0);
    placeAt(s, 0);
    auto afterSeek = renderer.renderToImage(s, clock.tick(), 160, 160);
    REQUIRE(afterSeek.has_value());
    CHECK(renderer.stats().temporal.framesValid == 1u);

    const Shot fresh = play(*ctx, shaders, s, 1);
    CHECK_IDENTICAL(*afterSeek, fresh.image);
    CHECK(ctx->errorCount() == 0);
}

// ---- a render to LOOK at ------------------------------------------------------------------
//
// Hidden ([.capture]) because it writes files rather than asserting. It exists because a hash is
// blind to the thing a temporal effect actually gets wrong: a black frame and a nearly-black frame
// are identical in a hash, and an echo that is subtly drifting, doubled, or one frame off looks
// exactly like an echo that is correct until somebody looks at it.
//
//   tools/gpu-lock.sh ./build/release/tests/avgen_render_tests "[.capture][temporal]"
TEST_CASE("capture: the echo, the plain frame and the ring itself", "[.capture][gpu][temporal]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const std::filesystem::path out =
        std::filesystem::path(std::getenv("AVGEN_CAPTURE_DIR") ? std::getenv("AVGEN_CAPTURE_DIR") : ".");

    const int w = 420;
    const int h = 300;
    auto shoot = [&](const scene::Scene& base, int frames, const char* name) {
        scene::Scene s = base;
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        FixedStepClock clock(60.0);
        clock.restartAt(0.0);
        gpu::Image8 last;
        for (int i = 0; i < frames; ++i) {
            placeAt(s, i);
            auto img = renderer.renderToImage(s, clock.tick(), w, h);
            REQUIRE(img.has_value());
            last = std::move(*img);
        }
        const auto path = out / name;
        REQUIRE(assets::writePng(path, last.width, last.height, last.rgba).has_value());
        const auto& st = renderer.stats().temporal;
        UNSCOPED_INFO(name << ": framesValid=" << st.framesValid << " needed=" << st.framesNeeded
                           << " bytes=" << st.historyBytes << " ring=" << st.historyWidth << "x"
                           << st.historyHeight);
    };

    shoot(movingLight(), 14, "temporal-plain.png");
    shoot(withEcho(movingLight(), 10, 0.85f), 14, "temporal-echo-10.png");
    shoot(withEcho(movingLight(), 3, 0.85f), 14, "temporal-echo-3.png");
    // One frame in: the ring is empty, so this must look exactly like the plain frame. If it does
    // not, a cold history is being read and the whole disclosure argument is false.
    shoot(withEcho(movingLight(), 10, 0.85f), 1, "temporal-echo-cold.png");
}
