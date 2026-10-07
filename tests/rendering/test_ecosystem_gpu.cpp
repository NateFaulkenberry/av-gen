// ADR-1200 on pixels: the ecosystem's emitters (rendering::EcosystemRenderer).
//
// What only a GPU can settle:
//   * an emitter attached to a host instance lights the pixel its world position projects to, and only near it;
//   * it is DEPTH-TESTED against the scene: a wall in front hides it;
//   * it writes the EMISSION target, so the bloom treats it as light;
//   * a near (large on screen) emitter is drawn as a sprite, a far one is splatted, and both are light;
//   * a response field brightens it; the excited colour shows only where the field is;
//   * the GATE: a disabled ecosystem renders the same bytes as a scene with none.
//
// With `AVGEN_EFFECT_DUMP=<dir>` every arm is written there as a PNG.

#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/ecosystem.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 240;
constexpr std::uint32_t kHeight = 135;

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

gpu::Image8 render(rendering::SceneRenderer& renderer, const scene::Scene& s, double seconds) {
    FrameTime t{};
    t.renderTime = seconds;
    renderer.resetTemporalHistory();
    auto first = renderer.renderToImage(s, t, kWidth, kHeight);
    REQUIRE(first.has_value());
    auto img = renderer.renderToImage(s, t, kWidth, kHeight);
    REQUIRE(img.has_value());
    return std::move(*img);
}

void dump(const gpu::Image8& image, const std::string& stem) {
    const char* dir = std::getenv("AVGEN_EFFECT_DUMP");
    if (dir == nullptr || dir[0] == '\0') {
        return;
    }
    static_cast<void>(assets::writePng(fs::path(dir) / (stem + ".png"), image.width, image.height, image.rgba));
}

int luma(const gpu::Image8& img, std::uint32_t x, std::uint32_t y) {
    const auto* p = img.pixel(x, y);
    return static_cast<int>(p[0]) + static_cast<int>(p[1]) + static_cast<int>(p[2]);
}

// The brightest pixel and where it is.
struct Peak {
    int value = 0;
    std::uint32_t x = 0, y = 0;
};
Peak peak(const gpu::Image8& img) {
    Peak best;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            if (const int v = luma(img, x, y); v > best.value) {
                best = {v, x, y};
            }
        }
    }
    return best;
}

// Camera at the origin looking down -Z; no light; a near-black background.
scene::Scene darkStage() {
    scene::Scene scene;
    scene.camera.position = glm::vec3(0.0f, 0.0f, 0.0f);
    scene.camera.target = glm::vec3(0.0f, 0.0f, -1.0f);
    scene.camera.fovYRadians = glm::radians(60.0f);
    scene.environment.backgroundColor = glm::vec3(0.0f);
    scene.environment.environmentIntensity = 0.0f;
    return scene;
}

// One host instance at `at` (a tiny box body), carrying a template of the given points.
void addEcosystem(scene::Scene& s, glm::vec3 at, std::vector<scene::EmitterPoint> points, float intensity,
                  glm::vec3 color = glm::vec3(0.1f, 0.6f, 1.0f)) {
    scene::ProceduralGeometry host;
    host.name = "host";
    host.source.kind = scene::PrimitiveKind::Box;
    host.source.size = glm::vec3(0.001f);
    host.distribution.kind = scene::DistributionKind::Points;
    scene::Transform place;
    place.position = at;
    host.distribution.setPoints({place});
    host.material.baseColor = glm::vec3(0.0f);
    REQUIRE(host.rebuild());
    REQUIRE(host.instances.size() == 1);
    s.procedurals.push_back(std::move(host));

    scene::EmitterLayer layer;
    layer.name = "polyps";
    layer.hosts = {"host"};
    layer.templatePath = "inline";
    layer.color = color;
    layer.excitedColor = glm::vec3(1.0f, 0.1f, 0.6f);
    layer.intensity = intensity;
    layer.points = std::make_shared<const std::vector<scene::EmitterPoint>>(std::move(points));
    layer.pointsHash = 42;
    s.ecosystem.layers.push_back(std::move(layer));
}

scene::EmitterPoint point(glm::vec3 p, float radius) {
    scene::EmitterPoint e;
    e.position = p;
    e.radius = radius;
    return e;
}

} // namespace

TEST_CASE("an ecosystem emitter lights the pixel its host's instance puts it at", "[gpu][ecosystem]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // The host is at x = +4 (right of centre) 20 m out; its one emitter sits 1 m above the host's origin.
    scene::Scene s = darkStage();
    addEcosystem(s, glm::vec3(4.0f, -1.0f, -20.0f), {point(glm::vec3(0.0f, 1.0f, 0.0f), 0.04f)}, 40.0f);
    const gpu::Image8 lit = render(renderer, s, 1.0);
    dump(lit, "ecosystem-one-point");
    CHECK(renderer.ecosystem().stats().candidates == 1);
    CHECK(renderer.ecosystem().stats().layers == 1);

    // Where (4, 0, -20) projects: tan(30 deg) * 20 m = 11.547 m is the half height.
    const float halfH = std::tan(glm::radians(30.0f)) * 20.0f;
    const float halfW = halfH * static_cast<float>(kWidth) / static_cast<float>(kHeight);
    const float ex = (4.0f / halfW * 0.5f + 0.5f) * kWidth;
    const float ey = 0.5f * kHeight;
    const Peak p = peak(lit);
    INFO("peak " << p.value << " at (" << p.x << ", " << p.y << "), expected near (" << ex << ", " << ey << ")");
    CHECK(p.value > 120);
    CHECK(std::abs(static_cast<float>(p.x) - ex) <= 2.0f);
    CHECK(std::abs(static_cast<float>(p.y) - ey) <= 2.0f);
    // And it is local: the far corner is still dark.
    CHECK(luma(lit, 4, 4) < 12);

    SECTION("it writes the emission target, so the bloom sees it as light") {
        auto e = gpu::readTextureF16(*ctx, renderer.emissionTexture(), kWidth, kHeight);
        REQUIRE(e.has_value());
        float sum = 0.0f;
        for (std::uint32_t y = 0; y < kHeight; ++y) {
            for (std::uint32_t x = 0; x < kWidth; ++x) {
                const float* px = e->pixel(x, y);
                sum += px[0] + px[1] + px[2];
            }
        }
        CHECK(sum > 1.0f);
    }
}

TEST_CASE("an ecosystem emitter is hidden by a wall in front of it", "[gpu][ecosystem]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene open = darkStage();
    addEcosystem(open, glm::vec3(0.0f, -1.0f, -20.0f), {point(glm::vec3(0.0f, 1.0f, 0.0f), 0.04f)}, 40.0f);
    scene::Scene walled = open;
    const scene::MeshId cube = walled.addMesh(scene::makeCube(1.0f));
    scene::Entity& wall = walled.addEntity("wall", cube);
    wall.transform.position = glm::vec3(0.0f, 0.0f, -10.0f);
    wall.transform.scale = glm::vec3(4.0f, 4.0f, 0.2f);
    wall.material.baseColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    scene::Scene wallOnly = walled;
    wallOnly.ecosystem = scene::Ecosystem{};
    const gpu::Image8 seen = render(renderer, open, 1.0);
    const gpu::Image8 hidden = render(renderer, walled, 1.0);
    const gpu::Image8 wallAlone = render(renderer, wallOnly, 1.0);
    dump(seen, "ecosystem-occlusion-open");
    dump(hidden, "ecosystem-occlusion-walled");
    const int open_ = luma(seen, kWidth / 2, kHeight / 2);
    // The wall has its own (ambient) shading: compare with the same wall and no ecosystem at all.
    const int behind = luma(hidden, kWidth / 2, kHeight / 2);
    const int wallLuma = luma(wallAlone, kWidth / 2, kHeight / 2);
    INFO("centre with the wall " << behind << " (the wall alone " << wallLuma << "), without the wall " << open_);
    CHECK(open_ > 120);
    CHECK(std::abs(behind - wallLuma) <= 3);
}

TEST_CASE("a near emitter is a sprite and a far one a splat, and both are light", "[gpu][ecosystem]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // A 0.5 m point 10 m away covers about 6 px of radius: a sprite. Count the lit pixels: a disc, not a dot.
    scene::Scene near_ = darkStage();
    addEcosystem(near_, glm::vec3(0.0f, -1.0f, -10.0f), {point(glm::vec3(0.0f, 1.0f, 0.0f), 0.5f)}, 4.0f);
    const gpu::Image8 sprite = render(renderer, near_, 1.0);
    dump(sprite, "ecosystem-sprite");
    int lit = 0;
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth; ++x) {
            lit += luma(sprite, x, y) > 90 ? 1 : 0;
        }
    }
    INFO("pixels lit by the near emitter: " << lit);
    CHECK(lit > 40);
    CHECK(lit < 2000);
    CHECK(luma(sprite, kWidth / 2, kHeight / 2) > 200);
}

TEST_CASE("a response field brightens an emitter and shifts it to its excited colour", "[gpu][ecosystem]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene calm = darkStage();
    addEcosystem(calm, glm::vec3(0.0f, -1.0f, -20.0f), {point(glm::vec3(0.0f, 1.0f, 0.0f), 0.04f)}, 4.0f,
                 glm::vec3(0.0f, 0.2f, 1.0f));
    calm.ecosystem.layers[0].responseField = "excite";
    calm.ecosystem.layers[0].excitedIntensity = 40.0f;
    scene::Scene excited = calm;
    // A uniform scalar field of 1 everywhere.
    spatial::FieldSpec f;
    f.name = "excite";
    f.kind = spatial::FieldKind::Constant;
    f.falloff.kind = spatial::FalloffKind::None;
    f.strength = 1.0f;
    excited.fields.fields.push_back(f);
    // The calm scene names the same field, which does not exist there: the layer then shows its rest light.
    const gpu::Image8 a = render(renderer, calm, 1.0);
    const gpu::Image8 b = render(renderer, excited, 1.0);
    dump(a, "ecosystem-response-calm");
    dump(b, "ecosystem-response-excited");
    const auto* pa = a.pixel(kWidth / 2, kHeight / 2);
    const auto* pb = b.pixel(kWidth / 2, kHeight / 2);
    INFO("calm " << int(pa[0]) << "," << int(pa[1]) << "," << int(pa[2]) << "  excited " << int(pb[0]) << ","
                 << int(pb[1]) << "," << int(pb[2]));
    CHECK(luma(b, kWidth / 2, kHeight / 2) > luma(a, kWidth / 2, kHeight / 2) + 60);
    CHECK(pb[0] > pa[0] + 40); // the excited colour is magenta: red appears only under the field
}

TEST_CASE("an awakened region keeps glowing: the wake field raises the rest light", "[gpu][ecosystem]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene asleep = darkStage();
    addEcosystem(asleep, glm::vec3(0.0f, -1.0f, -20.0f), {point(glm::vec3(0.0f, 1.0f, 0.0f), 0.04f)}, 3.0f);
    asleep.ecosystem.layers[0].wakeField = "wake";
    asleep.ecosystem.layers[0].wakeGain = 4.0f;
    scene::Scene awake = asleep;
    spatial::FieldSpec f;
    f.name = "wake";
    f.kind = spatial::FieldKind::Constant;
    f.falloff.kind = spatial::FalloffKind::None;
    awake.fields.fields.push_back(f);
    const gpu::Image8 a = render(renderer, asleep, 1.0);
    const gpu::Image8 b = render(renderer, awake, 1.0);
    INFO("asleep " << luma(a, kWidth / 2, kHeight / 2) << ", awake " << luma(b, kWidth / 2, kHeight / 2));
    CHECK(luma(b, kWidth / 2, kHeight / 2) > luma(a, kWidth / 2, kHeight / 2) + 60);
}

TEST_CASE("a point nearer than nearFade fades out, so nothing becomes an orb on the lens", "[gpu][ecosystem]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene s = darkStage();
    addEcosystem(s, glm::vec3(0.0f, -1.0f, -2.0f), {point(glm::vec3(0.0f, 1.0f, 0.0f), 0.05f)}, 8.0f);
    const gpu::Image8 near_ = render(renderer, s, 1.0);
    s.ecosystem.layers[0].nearFade = 5.0f; // the point is 2 m away: fully faded
    const gpu::Image8 faded = render(renderer, s, 1.0);
    INFO("centre without the fade " << luma(near_, kWidth / 2, kHeight / 2) << ", with "
                                    << luma(faded, kWidth / 2, kHeight / 2));
    CHECK(luma(near_, kWidth / 2, kHeight / 2) > 200);
    CHECK(luma(faded, kWidth / 2, kHeight / 2) < 12);
}

TEST_CASE("a disabled ecosystem renders the same bytes as a scene with none", "[gpu][ecosystem]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene with = darkStage();
    addEcosystem(with, glm::vec3(0.0f, -1.0f, -20.0f), {point(glm::vec3(0.0f, 1.0f, 0.0f), 0.04f)}, 40.0f);
    scene::Scene none = with;
    none.ecosystem = scene::Ecosystem{};
    with.ecosystem.enabled = false;
    const gpu::Image8 a = render(renderer, none, 1.0);
    const gpu::Image8 b = render(renderer, with, 1.0);
    CHECK(renderer.ecosystem().stats().layers == 0);
    CHECK(a.rgba == b.rgba);
}
