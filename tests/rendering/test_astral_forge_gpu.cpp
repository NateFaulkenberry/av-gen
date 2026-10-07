// THE ASTRAL FORGE in production (ADR-1221, ADR-1222), the GPU half. Each claim has a control that must come out the
// other way (ADR-182):
//   * the block draws a god into the scene's HDR and depth -- AND a disabled block records nothing at all;
//   * at the offline tier a seek lands exactly where play does -- AND play at 30 fps simulates what play at 60 does;
//   * a lower tier simulates fewer particles and still draws the god.
// The CPU half (block, parameters, conductors, MIDI, the ladder) is tests/unit/test_astral_forge.cpp.

#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/astral_renderer.hpp"
#include "rendering/render_quality.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;

namespace {

std::vector<std::filesystem::path> shaderDirs() {
    std::vector<std::filesystem::path> dirs;
    if (const char* env = std::getenv("AVGEN_SHADER_DIR")) {
        dirs.emplace_back(env);
    }
    dirs.emplace_back(AVGEN_SHADER_SOURCE_DIR);
    return dirs;
}

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

FrameTime frameAt(double t, double dt) {
    FrameTime f{};
    f.renderTime = t;
    f.deltaTime = dt;
    f.frameIndex = static_cast<std::uint64_t>(std::llround(t / dt));
    return f;
}

scene::Scene blackScene() {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.environment.gridIntensity = 0.0f;
    s.environment.environmentIntensity = 0.0f;
    s.environment.sky.intensity = 0.0f;
    s.post.bloomEnabled = false;
    s.post.tonemap = scene::TonemapOperator::Clamp;
    s.camera.position = {0.0f, 0.4f, 16.0f};
    s.camera.target = {0.0f, 0.2f, 0.0f};
    s.camera.fovYRadians = 0.52f;
    return s;
}

// A small god: 256k particles in a 96-cell grid, a second of pre-roll.
scene::AstralForge smallGod() {
    scene::AstralForge a;
    a.enabled = true;
    a.particles = 262144;
    a.gridRes = 96;
    a.gridSize = 20.0f;
    a.preroll = 1.0f;
    a.live = a.controls;
    return a;
}

// The song conductor over a synthetic 120-bpm song (pure in t, so the renderer conducts its own sub-steps).
std::shared_ptr<const scene::AstralSong> syntheticSong(double seconds) {
    auto song = std::make_shared<scene::AstralSong>();
    astral::SongAnalysis& s = song->song;
    s.duration = seconds;
    s.tempoBpm = 120.0f;
    s.hopRate = 93.75f;
    s.hops = static_cast<int>(seconds * s.hopRate);
    s.env0.assign(static_cast<std::size_t>(s.hops), glm::vec4(0.5f));
    s.env1.assign(static_cast<std::size_t>(s.hops), glm::vec4(0.5f));
    s.centroid.assign(static_cast<std::size_t>(s.hops), 0.5f);
    for (double t = 0.0; t < seconds; t += 0.5) {
        s.beats.push_back(t);
        s.kickT.push_back(static_cast<float>(t));
        s.kickS.push_back(0.3f);
    }
    s.sections.push_back(astral::Section{0.0, seconds, 0, 0.8f, 0.8f, "verse"});
    song->score = astral::buildScore(s);
    return song;
}

// The frame's block as the Composition would write it: the conductor's state at t, and its camera.
void conduct(scene::Scene& s, double t) {
    scene::AstralForge& a = s.astral;
    a.time = t;
    a.live.intro = false;
    if (a.song) {
        a.state = astral::conductSong(t, a.song->song, a.song->score, a.live);
    } else {
        astral::State st;
        st.archA = st.archB = astral::kSeraph;
        st.C = 0.95f;
        astral::defaults(st);
        a.state = st;
    }
}

double meanLuma(const gpu::Image8& img) {
    double sum = 0.0;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const std::uint8_t* p = img.pixel(x, y);
            sum += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
        }
    }
    return sum / (static_cast<double>(img.width) * img.height);
}

long differing(const gpu::Image8& a, const gpu::Image8& b) {
    long n = 0;
    for (std::uint32_t y = 0; y < a.height; ++y) {
        for (std::uint32_t x = 0; x < a.width; ++x) {
            for (int c = 0; c < 3; ++c) {
                n += a.pixel(x, y)[c] != b.pixel(x, y)[c] ? 1 : 0;
            }
        }
    }
    return n;
}

// Plays from 0 to `until` at `fps`, returning the last frame.
gpu::Image8 play(rendering::SceneRenderer& r, scene::Scene& s, double until, double fps, std::uint32_t w, std::uint32_t h) {
    gpu::Image8 last;
    const int frames = static_cast<int>(std::llround(until * fps));
    for (int i = 0; i <= frames; ++i) {
        const double t = i / fps;
        conduct(s, t);
        auto img = r.renderToImage(s, frameAt(t, 1.0 / fps), w, h);
        REQUIRE(img.has_value());
        last = std::move(*img);
    }
    return last;
}

} // namespace

TEST_CASE("the astral block draws a god into the scene; a disabled block records nothing", "[gpu][astral][adr1221]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, shaderDirs());
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    scene::Scene s = blackScene();
    s.astral = smallGod();
    const gpu::Image8 god = play(renderer, s, 1.5, 30.0, 256, 160);
    CHECK(renderer.astral().stats().particlesActive == 262144u);
    CHECK(meanLuma(god) > 2.0); // metal and flakes in the void

    rendering::SceneRenderer plain(*ctx, shaders);
    REQUIRE(plain.init().has_value());
    scene::Scene none = blackScene();
    scene::Scene off = blackScene();
    off.astral = smallGod();
    off.astral.enabled = false;
    auto a = plain.renderToImage(none, frameAt(0.5, 1.0 / 30.0), 256, 160);
    auto b = plain.renderToImage(off, frameAt(0.5, 1.0 / 30.0), 256, 160);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(differing(*a, *b) == 0); // the gate: byte-identical to a scene without the block
    CHECK(meanLuma(*a) < 0.5);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("at the offline tier a seek lands exactly where play does, at any frame rate", "[gpu][astral][adr1221][seek]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, shaderDirs());
    const auto offline = rendering::QualitySettings::forTier(rendering::QualityTier::Offline);
    scene::Scene s = blackScene();
    s.astral = smallGod();
    s.astral.song = syntheticSong(10.0);
    s.astral.driveCamera = false; // the test's fixed camera
    const double at = 2.0;

    rendering::SceneRenderer played(*ctx, shaders);
    REQUIRE(played.init().has_value());
    played.setQualitySettings(offline);
    const gpu::Image8 byPlay = play(played, s, at, 30.0, 256, 160);

    rendering::SceneRenderer sixty(*ctx, shaders);
    REQUIRE(sixty.init().has_value());
    sixty.setQualitySettings(offline);
    const gpu::Image8 byPlay60 = play(sixty, s, at, 60.0, 256, 160);

    rendering::SceneRenderer seeked(*ctx, shaders);
    REQUIRE(seeked.init().has_value());
    seeked.setQualitySettings(offline);
    conduct(s, at);
    auto bySeek = seeked.renderToImage(s, frameAt(at, 1.0 / 30.0), 256, 160);
    REQUIRE(bySeek.has_value());
    CHECK(seeked.astral().stats().reset);
    CHECK(meanLuma(byPlay) > 1.0);
    CHECK(differing(*bySeek, byPlay) == 0);
    CHECK(differing(byPlay60, byPlay) == 0); // the simulation steps at 60 Hz whatever the frame rate

    // the control: the live tier's pre-roll is visually equivalent, not exact
    rendering::SceneRenderer window(*ctx, shaders);
    REQUIRE(window.init().has_value());
    auto byWindow = window.renderToImage(s, frameAt(at, 1.0 / 30.0), 256, 160);
    REQUIRE(byWindow.has_value());
    CHECK(differing(*byWindow, byPlay) > 0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("a lower astral tier simulates fewer particles and still draws the god", "[gpu][astral][adr1222]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, shaderDirs());
    for (std::uint32_t tier : {1u, 3u, 4u}) {
        INFO("tier " << tier);
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        auto q = rendering::QualitySettings::forTier(rendering::QualityTier::Realtime);
        q.astralTier = tier;
        renderer.setQualitySettings(q);
        scene::Scene s = blackScene();
        s.astral = smallGod();
        const gpu::Image8 img = play(renderer, s, 1.0, 30.0, 192, 120);
        const auto& st = renderer.astral().stats();
        CHECK(st.tier == tier);
        const std::uint32_t expect = tier == 1 ? 262144u : tier == 3 ? 131072u : 86272u;
        CHECK(st.particlesActive == expect);
        CHECK(meanLuma(img) > 1.0);
    }
    CHECK(ctx->errorCount() == 0);
}
