// A fading body's shadow fades with it.
//
// The owner's report, on the UFO abduction: "there's also an interesting bug where the shadow of
// the animal will pop out of existence before the animal does". It did, and by a measured amount.
// ADR-385 fades an abducted animal out over the last 1.2 s of its 4.6 s lift by driving
// `nodes/<animal>/opacity`, and `Composition::update` promotes a node under 1.0 opacity to
// `AlphaMode::Blend` for those frames -- because `pbr_shade.wgsl` throws an OPAQUE material's
// alpha away, so the number alone renders a solid cow. `SceneRenderer` then sorted every entity
// into exactly one of three buckets, and only the opaque one reached `shadowCasters`:
//
//     } else if (entity.material.alphaMode == scene::AlphaMode::Blend) {
//         blended.push_back(*item);           // <- and nothing else
//     } else {
//         opaque.push_back(*item);
//         if (entity.castsShadow) { shadowCasters.push_back(*item); }
//     }
//
// So the promotion that makes the fade visible is the same act that deletes the shadow, on the
// frame the fade *starts*. Measured on the shipped film at the project's own render settings
// (1920x1080, tier offline, 2x supersample), through the film's own camera: `shadows.entityDraws`
// fell from 31 to 30 at t = 13.717 s, at an opacity of **0.998**, and the animal was not gone
// until t = 14.85 s. One and a eighth seconds of an animal standing in a beam with no shadow.
//
// The fix is in two halves and this file asserts the pair of them on pixels:
//
//   * `fs_depth` (pbr.wgsl) gives a blended caster stochastic transparency -- an ordered 4x4
//     dither discards texels it does not "own" -- so a depth map, which can only say blocked or
//     not blocked per texel, carries a partial shadow as a fraction of its texels. The 16-tap
//     Poisson disc in shadows.wgsl averages that back into a smooth term.
//   * `SceneRenderer` offers blended entities to the shadow pass, asking `casterEligibility` --
//     the Shadow Lab's own function -- rather than a second copy of the rule.
//
// The scene is the trivial one test_shadows_gpu.cpp uses for the same reason it does: a floor, a
// box above it, one directional light straight down, so a named pixel can be asserted. The box's
// opacity is swept and two things are read on every arm -- the floor under the box (the shadow)
// and the box itself (the body). Both, because a shadow that tracks a body that is not fading
// proves nothing: the body's own trace is the positive control (ADR-182).

#include "app/engine.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "params/parameter_set.hpp"
#include "rendering/shadow_math.hpp"
#include "scene/composition.hpp"
#include "scene/scene.hpp"
#include "stage/staging.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kSize = 192;

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
    const glm::vec3 n[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (const glm::vec3 normal : n) {
        const glm::vec3 u =
            std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::cross(glm::vec3(0, 1, 0), normal);
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

// `opacity` < 0 means "leave it opaque": the arm the fade is compared against.
scene::Scene fadeScene(float opacity) {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    // Nearly level with the box, so the box is seen against the black background and its shadow
    // is seen on the floor below it: two separate places in the frame, which is what lets the
    // body and its shadow be read as two numbers. Standing over the pair -- which is what
    // test_shadows_gpu.cpp does, and it is right for what that file asks -- puts the body's own
    // pixels in front of its own shadow, and then neither reading is about one thing.
    s.camera.position = {0.0f, 6.0f, 18.0f};
    s.camera.target = {0.0f, 4.0f, 0.0f};
    s.camera.fovYRadians = 0.9f;
    s.camera.nearPlane = 0.5f;
    s.camera.farPlane = 120.0f;

    const auto floor = s.addMesh(boxMesh({20.0f, 0.25f, 20.0f}));
    const auto box = s.addMesh(boxMesh({2.0f, 2.0f, 2.0f}));
    {
        auto& e = s.addEntity("floor", floor);
        e.transform.position = {0.0f, -0.25f, 0.0f};
        e.material.baseColor = glm::vec3(0.8f);
        e.material.roughness = 0.9f;
        e.material.metallic = 0.0f;
    }
    {
        auto& e = s.addEntity("box", box);
        e.transform.position = {0.0f, 6.0f, 0.0f};
        e.material.baseColor = glm::vec3(0.8f);
        e.material.roughness = 0.9f;
        if (opacity >= 0.0f) {
            // Exactly what `Composition::update` does to a node the director is fading: the
            // number, and the promotion without which `pbr_shade.wgsl` discards the number.
            e.material.opacity = opacity;
            e.material.alphaMode = scene::AlphaMode::Blend;
        }
    }
    scene::PunctualLight key;
    key.name = "key";
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(0.0f, -1.0f, 0.0f));
    key.color = glm::vec3(1.0f);
    key.intensity = 4.0f;
    key.castsShadow = true;
    key.contactShadow = false;
    // Hard-edged, so the reading under the box is the shadow and not a penumbra gradient.
    key.softness = 0.05f;
    s.addLight(key);
    // A fill from where the camera stands, casting nothing. Without it the key comes straight
    // down, the faces of the box the camera can see have N.L = 0, and the body reads black
    // against a black background at every opacity -- a positive control that cannot move.
    scene::PunctualLight fill;
    fill.name = "fill";
    fill.type = scene::PunctualLight::Type::Directional;
    fill.direction = glm::normalize(glm::vec3(0.0f, -0.15f, -1.0f));
    fill.color = glm::vec3(1.0f);
    fill.intensity = 3.0f;
    fill.castsShadow = false;
    fill.contactShadow = false;
    s.addLight(fill);
    return s;
}

float luminanceAt(const gpu::Image8& image, std::uint32_t x, std::uint32_t y) {
    const std::size_t i = (static_cast<std::size_t>(y) * image.width + x) * 4;
    REQUIRE(i + 2 < image.rgba.size());
    return (0.2126f * static_cast<float>(image.rgba[i]) +
            0.7152f * static_cast<float>(image.rgba[i + 1]) +
            0.0722f * static_cast<float>(image.rgba[i + 2])) /
           255.0f;
}

// A mean over a block, because the dither is a 4x4 pattern and a single texel of it is a coin
// toss. Everything this file asserts about a partial shadow is a statement about an area.
float meanLuminance(const gpu::Image8& image, std::uint32_t cx, std::uint32_t cy,
                    std::uint32_t half) {
    float sum = 0.0f;
    int n = 0;
    for (std::uint32_t y = cy - half; y <= cy + half; ++y) {
        for (std::uint32_t x = cx - half; x <= cx + half; ++x) {
            sum += luminanceAt(image, x, y);
            ++n;
        }
    }
    return sum / static_cast<float>(n);
}

FrameTime frameAt(std::uint64_t index) {
    FrameTime t;
    t.frameIndex = index;
    t.renderTime = static_cast<double>(index) / 60.0;
    t.deltaTime = 1.0 / 60.0;
    return t;
}

} // namespace

TEST_CASE("a fading body's shadow fades with it instead of popping", "[gpu][shadows][abduction]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // Projected by hand from the camera above, and each one checked by the control readings below:
    // the box's centre, the floor directly beneath it, and open floor on the same row.
    const std::uint32_t bodyX = kSize / 2;   // (96, 74): the box, against the background
    const std::uint32_t bodyY = 74;
    const std::uint32_t shadowX = kSize / 2; // (96, 139): the floor under it
    const std::uint32_t shadowY = 139;
    const std::uint32_t openX = 24;          // open floor, same row

    struct Arm {
        float opacity;
        float shadow = 0.0f; // the floor under the box
        float open = 0.0f;   // open floor, the control that the light did not change
        float body = 0.0f;   // the box's own pixels
        std::uint32_t casters = 0;
    };

    const auto measure = [&](float opacity) {
        const scene::Scene s = fadeScene(opacity);
        auto image = renderer.renderToImage(s, frameAt(3), kSize, kSize);
        REQUIRE(image.has_value());
        Arm a;
        a.opacity = opacity;
        a.shadow = meanLuminance(*image, shadowX, shadowY, 8);
        a.open = meanLuminance(*image, openX, shadowY, 6);
        a.body = meanLuminance(*image, bodyX, bodyY, 8);
        a.casters = renderer.stats().shadowCasters;
        return a;
    };

    // The reference: the same box, opaque, which is what every frame of the film looks like until
    // the fade starts. Its shadow is the one the fade has to leave from.
    const Arm opaque = measure(-1.0f);
    // And the floor with nothing over it at all, which is what "no shadow" reads as. Taken by
    // fading the box to nothing rather than by deleting it, so the two frames differ in one value.
    const Arm gone = measure(0.0f);

    INFO(fmt::format("opaque: shadow {:.4f} body {:.4f} casters {}; faded out: shadow {:.4f} body "
                     "{:.4f} casters {}",
                     opaque.shadow, opaque.body, opaque.casters, gone.shadow, gone.body,
                     gone.casters));
    // The instrument works: an opaque box really does darken the floor under it, and a box that
    // has faded to nothing really does leave it alone. Without this pair every reading below
    // could be explained by a light that never cast (ADR-182).
    REQUIRE(opaque.shadow < gone.shadow * 0.75f);
    REQUIRE(gone.shadow > 0.05f);
    REQUIRE(opaque.body > 0.05f);

    std::vector<Arm> arms;
    for (const float opacity : {1.0f, 0.75f, 0.5f, 0.25f, 0.0f}) {
        arms.push_back(measure(opacity));
    }
    for (const Arm& a : arms) {
        fmt::print("  opacity {:.2f}: shadow {:.4f}  open floor {:.4f}  body {:.4f}  casters {}\n",
                   a.opacity, a.shadow, a.open, a.body, a.casters);
    }

    // ---- the body, which is the positive control -------------------------------------------------
    //
    // It has to actually be fading, or "the shadow follows the body" is satisfied by a body that
    // never moves. This is the half that was never broken, and it is asserted so that the half
    // that was cannot be read wrong.
    for (std::size_t i = 1; i < arms.size(); ++i) {
        INFO(fmt::format("body at opacity {:.2f} = {:.4f}, at {:.2f} = {:.4f}", arms[i - 1].opacity,
                         arms[i - 1].body, arms[i].opacity, arms[i].body));
        CHECK(arms[i].body < arms[i - 1].body);
    }

    // ---- the shadow --------------------------------------------------------------------------
    //
    // A blended body at full opacity casts the shadow an opaque one does. This is the assertion
    // that fails hardest on the old code: there, the *first frame* of a fade had no shadow at all,
    // so this read the open floor.
    INFO(fmt::format("blend at opacity 1.00 casts {:.4f}; opaque casts {:.4f}; no caster is {:.4f}",
                     arms.front().shadow, opaque.shadow, gone.shadow));
    CHECK(std::abs(arms.front().shadow - opaque.shadow) < 0.04f);

    // It gets lighter as the body does, every step of the way, and it is not a step function: at
    // half opacity the floor is genuinely half way between shadowed and open, which is what
    // "fades with its caster" means and what a threshold could not produce.
    for (std::size_t i = 1; i < arms.size(); ++i) {
        INFO(fmt::format("shadow at opacity {:.2f} = {:.4f}, at {:.2f} = {:.4f}", arms[i - 1].opacity,
                         arms[i - 1].shadow, arms[i].opacity, arms[i].shadow));
        CHECK(arms[i].shadow > arms[i - 1].shadow);
    }
    const Arm& half = arms[2];
    const float span = gone.shadow - opaque.shadow;
    REQUIRE(span > 0.02f);
    const float part = (half.shadow - opaque.shadow) / span;
    // A wide band on purpose, and the width is the sRGB in the reading rather than slack. What
    // half the depth texels buys is half the *direct radiance*, and this number is a tone-mapped
    // luminance: measured, 0.5 opacity lands at 70% of the way from shadowed to open, and the
    // encoding accounts for all of it. Half of a nonlinear scale is not the scale of a half. What
    // is asserted is the thing a threshold could never produce -- a reading genuinely between the
    // two ends rather than at one of them.
    INFO(fmt::format("at opacity 0.50 the floor is {:.1f}% of the way from shadowed ({:.4f}) to "
                     "open ({:.4f}): {:.4f}",
                     100.0f * part, opaque.shadow, gone.shadow, half.shadow));
    CHECK(part > 0.15f);
    CHECK(part < 0.88f);

    // And at nothing it is gone, so the promotion does not leave a shadow behind a body nobody
    // can see -- the failure the old exclusion was originally written to avoid.
    INFO(fmt::format("faded out: {:.4f} against an open floor of {:.4f}", arms.back().shadow,
                     gone.shadow));
    CHECK(arms.back().shadow > gone.shadow * 0.97f);
    CHECK(arms.back().casters < arms.front().casters);

    // Nothing else in the frame moved: the open floor is the same under every arm, so what the
    // numbers above describe is the shadow and not the exposure.
    for (const Arm& a : arms) {
        INFO(fmt::format("open floor at opacity {:.2f}: {:.4f} against {:.4f}", a.opacity, a.open,
                         opaque.open));
        CHECK(std::abs(a.open - opaque.open) < 0.02f);
    }
    CHECK(ctx->errorCount() == 0);
}

// The Shadow Lab's own verdict function, on the same four cases. It is a *second* copy of nothing:
// `SceneRenderer` asks this function about a blended entity rather than restating the rule, so
// this asserts the rule itself and the test above asserts that the pixels obey it.
TEST_CASE("casterEligibility admits a fading body and refuses a vanished one",
          "[shadows][abduction]") {
    scene::Scene s;
    const auto mesh = s.addMesh(boxMesh({1.0f, 1.0f, 1.0f}));
    scene::Entity e;
    e.mesh = mesh;
    e.visible = true;
    e.castsShadow = true;

    CHECK(rendering::casterEligibility(e) == rendering::CasterState::Caster);

    e.material.alphaMode = scene::AlphaMode::Blend;
    for (const float opacity : {1.0f, 0.5f, 0.01f}) {
        e.material.opacity = opacity;
        INFO(fmt::format("a blended caster at opacity {:.2f}", opacity));
        CHECK(rendering::casterEligibility(e) == rendering::CasterState::Caster);
    }
    e.material.opacity = 0.0f;
    CHECK(rendering::casterEligibility(e) == rendering::CasterState::StyleExcluded);

    // The exclusions that stay: a wireframe and a water surface are not casters whatever their
    // alpha, and neither is a body the scene turned off.
    e.material.alphaMode = scene::AlphaMode::Opaque;
    e.material.opacity = 1.0f;
    e.style = scene::MeshStyle::Water;
    CHECK(rendering::casterEligibility(e) == rendering::CasterState::StyleExcluded);
    e.style = scene::MeshStyle::Grid;
    CHECK(rendering::casterEligibility(e) == rendering::CasterState::StyleExcluded);
    e.style = scene::MeshStyle::Lit;
    e.castsShadow = false;
    CHECK(rendering::casterEligibility(e) == rendering::CasterState::ShadowDisabled);
}

// ---- the film itself -----------------------------------------------------------------------------
//
// The instrument that produced the number in the header, kept because it is what a person looks at
// and because a synthetic box is not the shipped film. It plays the multicam project to the first
// abduction at the project's OWN render block -- 1920x1080, tier offline, 2x supersample, limits
// unlimited -- renders every frame of the moment the fade starts, and reports `shadows.entityDraws`
// beside the animal's opacity. Before the fix that pair read "0.998, and one caster fewer".
//
// Hidden: it writes frames rather than asserting pixels, and it costs a project load plus 830
// simulated frames.
TEST_CASE("capture: the abduction fade, on the film", "[.fadecapture]") {
    if (!fs::is_regular_file(fs::path(AVGEN_SOURCE_DIR) / "assets" / "farm" / "cow.glb")) {
        SKIP("assets/farm is not present (the GLBs are gitignored)");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine
                .loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" /
                             "glowmere-valley-2-multicam.json")
                .has_value());
    scene::Composition* comp = engine.composition();
    REQUIRE(comp != nullptr);

    constexpr std::uint32_t kW = 1920;
    constexpr std::uint32_t kH = 1080;
    engine.setDetailLimits(scene::DetailLimits::unlimited());
    renderer.setQuality(rendering::QualityTier::Offline);
    {
        rendering::QualitySettings q = renderer.qualitySettings();
        q.renderScale = 2.0f;
        renderer.setQualitySettings(q);
    }
    engine.setViewport(kW, kH);
    REQUIRE(renderer.resize(kW, kH).has_value());
    const fs::path outDir = fs::path(AVGEN_SOURCE_DIR) / "build" / "abduction-fade";
    std::error_code ec;
    fs::create_directories(outDir, ec);

    // The first lift runs 10.25 s .. 14.85 s and the fade is its last 1.2 s; the camera cuts to
    // "UFO Watch" at 13.50 s, so the whole of it is on screen.
    const double first = 13.60;
    const double last = 14.95;
    FrameTime time;
    const double hz = 60.0;
    int written = 0;
    for (int i = 0; i <= static_cast<int>(last * hz); ++i) {
        time.renderTime = static_cast<double>(i) / hz;
        time.deltaTime = i == 0 ? 0.0 : 1.0 / hz;
        time.frameIndex = static_cast<std::uint64_t>(i);
        engine.update(time);
        if (time.renderTime < first) {
            continue;
        }
        const std::string target(comp->director().binding("abduction", "target"));
        float opacity = -1.0f;
        if (!target.empty()) {
            if (const params::IParameter* p = engine.params().find("nodes/" + target + "/opacity")) {
                opacity = p->baseComponent(0);
            }
        }
        auto image = renderer.renderToImage(comp->scene(), time, kW, kH);
        REQUIRE(image.has_value());
        const std::string name = fmt::format("f{:04d}-t{:06.3f}-op{:05.3f}.png", i, time.renderTime,
                                             std::max(0.0f, opacity));
        REQUIRE(
            assets::writePng(outDir / name, image->width, image->height, image->rgba).has_value());
        fmt::print("  {} target={:<10} opacity={:6.3f} casters={} shadowDraws={}\n", name, target,
                   opacity, renderer.stats().shadowCasters, renderer.stats().shadows.entityDraws);
        std::fflush(stdout);
        ++written;
    }
    fmt::print("{} frame(s) written to {}\n", written, outDir.string());
    CHECK(written > 0);
}
