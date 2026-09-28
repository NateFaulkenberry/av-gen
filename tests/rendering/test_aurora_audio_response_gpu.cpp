// ADR-939: the aurora's "Audio response" is the master of everything the aurora does with the music.
//
// gv3-look turned "Audio response" to 0 on Glowmere Valley 3 and the sky still pulsed with the kick
// (+12-16% beat-locked in the drop): the per-bearing spectrum still set every curtain's top, and the
// glints still followed the high band. The control read as a master and was one of three. The CPU tests
// (test_atmospherics.cpp) check the packing; this is the pixel question -- does "Audio response" at 0
// leave the sky exactly as it is with no music playing, and do the controls it gates still reach it.
//
// "Music" is a loud analysis frame on a beat with a strongly shaped sixteen-bin spectrum; "silence" is
// no analysis frame and no spectrum, which is what the engine renders with no track. Every arm is held
// against a frame it must equal and one it must differ from (ADR-182): an identity nobody checked could
// fail is vacuous.

#include "analysis/analyzer.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "world/atmospherics.hpp"
#include "world/effects/effect_instance.hpp"

#include <glm/trigonometric.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 480;
constexpr std::uint32_t kHeight = 300;

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

// The night sky over a low ridge from test_atmospherics_gpu.cpp, looked at from just above it.
scene::Scene skyScene() {
    scene::Scene scene;
    const scene::MeshId ridge = scene.addMesh(scene::makeCube(1.0f));
    scene::Entity& hill = scene.addEntity("ridge", ridge);
    hill.transform.position = glm::vec3(0.0f, -18.0f, -90.0f);
    hill.transform.scale = glm::vec3(400.0f, 30.0f, 60.0f);
    hill.material.baseColor = glm::vec4(0.05f, 0.06f, 0.08f, 1.0f);
    hill.material.roughness = 0.95f;
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.3f, -0.8f, -0.4f));
    key.color = glm::vec3(0.5f, 0.6f, 0.9f);
    key.intensity = 0.35f;
    scene.addLight(key);
    scene.camera.position = glm::vec3(0.0f, 6.0f, 40.0f);
    scene.camera.target = glm::vec3(0.0f, 34.0f, -120.0f);
    scene.camera.fovYRadians = glm::radians(50.0f);
    scene.environment.backgroundColor = glm::vec3(0.004f, 0.006f, 0.02f);
    return scene;
}

// The aurora's audio-facing settings. Everything else is the Glowmere preset, placed near and bright
// so the curtains, their tops and their glints cover a good part of a small frame.
struct Aurora {
    float sensitivity = 1.0f;   // "Audio response"
    float spectrumShape = 0.75f; // "Spectrum shape"
    float sparkle = 3.0f;       // "Glints": the glints' own amount
    float glints = 1.0f;        // "High -> glints": how much the high band brightens them (ADR-939)
};

world::AtmosphericFrame auroraFrame(const Aurora& a, std::span<const float> spectrum) {
    world::EffectInstance e = world::glowmereAurora("sky");
    e.aurora.shape.anchor = world::SkyAnchor::World;
    e.aurora.shape.anchorPosition = glm::vec3(0.0f);
    e.aurora.shape.radius = 900.0f;
    e.aurora.shape.baseHeight = -60.0f;
    e.aurora.shape.curtainHeight = 700.0f;
    e.aurora.appearance.intensity = 4.0f;
    e.aurora.appearance.sparkle = a.sparkle;
    e.aurora.audio.sensitivity = a.sensitivity;
    e.aurora.audio.glints = a.glints;
    e.aurora.audio.spectrumShape = a.spectrumShape;
    e.timing.fadeIn = 0.0;
    e.ground.mode = world::GroundGlow::Off;
    const std::array<world::EffectInstance, 1> set{e};
    world::EffectContext ctx;
    ctx.seconds = 3.0;
    ctx.spectrum = spectrum;
    world::AtmosphericFrame frame;
    world::buildAtmosphericFrame(set, ctx, frame);
    return frame;
}

// A loud frame, on the beat: every band the aurora reads is high, and the beat's pulse is at its top.
analysis::AnalysisFrame loudFrame() {
    analysis::AnalysisFrame f;
    f.rms = 0.8f;
    f.peak = 0.95f;
    f.bandCount = 8;
    for (std::size_t i = 0; i < 8; ++i) {
        f.bands[i] = 0.9f;
        f.bandsRaw[i] = 0.9f;
    }
    f.centroidNorm = 0.7f;
    f.flux = 0.6f;
    f.onsetStrength = 2.0f;
    f.beatPhase = 0.0f; // the pulse, 1 - phase, is 1
    f.beat = true;
    return f;
}

// Sixteen bins that alternate loud and quiet: a spectrum no curtain top can hide.
constexpr std::array<float, world::kAuroraBands> kShapedSpectrum{0.05f, 0.95f, 0.05f, 0.95f, 0.05f, 0.95f,
                                                                0.05f, 0.95f, 0.05f, 0.95f, 0.05f, 0.95f,
                                                                0.05f, 0.95f, 0.05f, 0.95f};

std::size_t differingPixels(const gpu::Image8& a, const gpu::Image8& b) {
    REQUIRE(a.rgba.size() == b.rgba.size());
    std::size_t differ = 0;
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        if (a.rgba[i] != b.rgba[i] || a.rgba[i + 1] != b.rgba[i + 1] || a.rgba[i + 2] != b.rgba[i + 2]) {
            ++differ;
        }
    }
    return differ;
}

// With AVGEN_EFFECT_DUMP set: both frames and their difference (x8, so a glint's shift shows), for a
// person to look at. The assertions never depend on it.
void dump(const std::string& name, const gpu::Image8& a, const gpu::Image8& b) {
    const char* dir = std::getenv("AVGEN_EFFECT_DUMP");
    if (dir == nullptr || *dir == '\0') {
        return;
    }
    std::vector<std::uint8_t> diff(a.rgba.size(), 255);
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        for (std::size_t c = 0; c < 3; ++c) {
            const int d = std::abs(static_cast<int>(a.rgba[i + c]) - static_cast<int>(b.rgba[i + c]));
            diff[i + c] = static_cast<std::uint8_t>(std::min(255, d * 8));
        }
    }
    const fs::path out(dir);
    std::error_code ec;
    fs::create_directories(out, ec);
    static_cast<void>(assets::writePng(out / ("aurora-" + name + "-a.png"), kWidth, kHeight, a.rgba));
    static_cast<void>(assets::writePng(out / ("aurora-" + name + "-b.png"), kWidth, kHeight, b.rgba));
    static_cast<void>(assets::writePng(out / ("aurora-" + name + "-diff.png"), kWidth, kHeight, diff));
}

struct Harness {
    std::unique_ptr<gpu::Context> ctx = makeContext();
    gpu::ShaderLibrary shaders{*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)}};
    rendering::SceneRenderer renderer{*ctx, shaders};
    scene::Scene scene = skyScene();
    analysis::AnalysisFrame loud = loudFrame();

    Harness() { REQUIRE(renderer.init().has_value()); }

    gpu::Image8 render(const world::AtmosphericFrame& sky, const analysis::AnalysisFrame* audio) {
        scene.atmospherics = sky;
        const FrameTime time{.renderTime = 3.0, .deltaTime = 1.0 / 60.0, .frameIndex = 180};
        rendering::ShaderFrameInputs inputs;
        inputs.frame = audio;
        auto image = renderer.renderToImage(scene, time, kWidth, kHeight, audio != nullptr ? &inputs : nullptr);
        REQUIRE(image.has_value());
        return std::move(*image);
    }
    gpu::Image8 withMusic(const Aurora& a) { return render(auroraFrame(a, kShapedSpectrum), &loud); }
    gpu::Image8 inSilence(const Aurora& a) { return render(auroraFrame(a, {}), nullptr); }
};

} // namespace

TEST_CASE("the aurora's audio response at 0 is the sky with no music, pixel for pixel",
          "[gpu][atmospherics][aurora][adr939]") {
    Harness h;

    SECTION("with the spectrum shaping the tops and bright glints") {
        const Aurora off{.sensitivity = 0.0f, .spectrumShape = 0.75f, .sparkle = 3.0f};
        const gpu::Image8 music = h.withMusic(off);
        const gpu::Image8 silence = h.inSilence(off);
        const std::size_t differ = differingPixels(music, silence);
        dump("off-music-vs-silence", music, silence);
        INFO("pixels the music moved with audio response at 0: " << differ);
        CHECK(differ == 0);

        // The control: at 1 the music reaches the sky, so the identity above is a finding and not a
        // frame in which the music could never have shown.
        const Aurora on{.sensitivity = 1.0f, .spectrumShape = 0.75f, .sparkle = 3.0f};
        const gpu::Image8 musicOn = h.withMusic(on);
        const gpu::Image8 silenceOn = h.inSilence(on);
        const std::size_t differOn = differingPixels(musicOn, silenceOn);
        dump("on-music-vs-silence", musicOn, silenceOn);
        INFO("pixels the music moved with audio response at 1: " << differOn);
        CHECK(differOn > kWidth * kHeight / 50);
        // ...and in silence the response has nothing to scale, so 0 and 1 are the same sky.
        CHECK(differingPixels(silence, silenceOn) == 0);
    }

    SECTION("GV3's aurora: steady, and its base glints still drawn") {
        // Glowmere Valley 3 runs with audio response 0, spectrum shape 0 and glints at 0.3.
        const Aurora gv3{.sensitivity = 0.0f, .spectrumShape = 0.0f, .sparkle = 0.3f};
        const gpu::Image8 music = h.withMusic(gv3);
        const gpu::Image8 silence = h.inSilence(gv3);
        const std::size_t differ = differingPixels(music, silence);
        dump("gv3-music-vs-silence", music, silence);
        INFO("pixels the music moved in GV3's configuration: " << differ);
        CHECK(differ == 0);
        // The glints are there: the same sky with none of them is a different sky.
        const Aurora noGlints{.sensitivity = 0.0f, .spectrumShape = 0.0f, .sparkle = 0.0f};
        const gpu::Image8 bare = h.withMusic(noGlints);
        const std::size_t glints = differingPixels(music, bare);
        dump("gv3-glints-vs-none", music, bare);
        INFO("pixels GV3's glints light: " << glints);
        CHECK(glints > 0);
    }
}

TEST_CASE("the aurora's High -> glints depth reaches the sky, and only with music",
          "[gpu][atmospherics][aurora][adr939]") {
    // ADR-939 made the glints' share of the high band a control ("High -> glints"). It reaches the
    // picture: with music, 0 and 2 are different skies. Without music the high band is 0 and the depth
    // has nothing to scale -- the same sky -- which is the control that the difference is the band's.
    Harness h;
    const Aurora none{.sensitivity = 1.0f, .spectrumShape = 0.0f, .sparkle = 3.0f, .glints = 0.0f};
    const Aurora strong{.sensitivity = 1.0f, .spectrumShape = 0.0f, .sparkle = 3.0f, .glints = 2.0f};
    const gpu::Image8 musicNone = h.withMusic(none);
    const gpu::Image8 musicStrong = h.withMusic(strong);
    const std::size_t differ = differingPixels(musicNone, musicStrong);
    dump("glints-depth-0-vs-2", musicNone, musicStrong);
    INFO("pixels High -> glints moved with music: " << differ);
    CHECK(differ > kWidth * kHeight / 100);
    CHECK(differingPixels(h.inSilence(none), h.inSilence(strong)) == 0);

    // And its master: at audio response 0 the depth is inert whatever the music does.
    const Aurora offNone{.sensitivity = 0.0f, .spectrumShape = 0.0f, .sparkle = 3.0f, .glints = 0.0f};
    const Aurora offStrong{.sensitivity = 0.0f, .spectrumShape = 0.0f, .sparkle = 3.0f, .glints = 2.0f};
    CHECK(differingPixels(h.withMusic(offNone), h.withMusic(offStrong)) == 0);
}
