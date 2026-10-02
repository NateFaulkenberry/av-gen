// ADR-1071 (cel lighting) and ADR-1072 (the screen-space outline) on pixels, with no commercial assets:
// generated meshes, a procedural node and an SDF loaded from text.
//
//   * cel lighting cuts a lit sphere into its bands: the shading collapses onto a handful of tones where
//     the PBR control spreads across dozens; the shadow tone takes the shadow colour; the rim and the
//     hard highlight add light where they should; and the same instant renders the same bytes twice;
//   * the toon parameters of a procedural node and an SDF object reach their pixels;
//   * the outline is off (byte-identical) at amount 0 whatever its shape parameters say, draws round a
//     silhouette when on, draws nothing across a flat floor seen at a grazing angle, follows its width,
//     and keeps the creases out in silhouette mode.
//
// With AVGEN_TOON_DUMP=<dir> every arm is written as a PNG.

#include "assets/asset_registry.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "signals/signal_bus.hpp"
#include "support/image_diff.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kW = 256;
constexpr std::uint32_t kH = 192;

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

void dump(const gpu::Image8& image, const std::string& name) {
    const char* dir = std::getenv("AVGEN_TOON_DUMP");
    if (dir == nullptr || dir[0] == '\0') {
        return;
    }
    static_cast<void>(assets::writePng(fs::path(dir) / (name + ".png"), image.width, image.height, image.rgba));
}

struct Harness {
    std::unique_ptr<gpu::Context> ctx = makeContext();
    gpu::ShaderLibrary shaders{*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)}};
    rendering::SceneRenderer renderer{*ctx, shaders};
    Harness() { REQUIRE(renderer.init().has_value()); }
    gpu::Image8 render(const scene::Scene& s, double seconds = 1.0) {
        FrameTime t{};
        t.renderTime = seconds;
        renderer.resetTemporalHistory();
        auto img = renderer.renderToImage(s, t, kW, kH);
        REQUIRE(img.has_value());
        return std::move(*img);
    }
};

// A white sphere at the origin, lit from the upper left by one directional light, on black. No bloom,
// no fog, a fixed exposure: what is left is the shading.
scene::Scene litSphere() {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.camera.position = {0.0f, 0.0f, 4.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    const auto mesh = s.addMesh(scene::makeIcosphere(1.0f, 5));
    auto& e = s.addEntity("ball", mesh);
    e.material.baseColor = {0.8f, 0.8f, 0.8f};
    e.material.roughness = 0.6f;
    scene::PunctualLight key;
    key.name = "key";
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(1.0f, -0.6f, -0.5f));
    key.intensity = 3.0f;
    s.addLight(key);
    s.post.bloomEnabled = false;
    s.post.bloomIntensity = 0.0f;
    return s;
}

float lum(const gpu::Image8& img, std::uint32_t x, std::uint32_t y) {
    const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4;
    return 0.2126f * img.rgba[i] + 0.7152f * img.rgba[i + 1] + 0.0722f * img.rgba[i + 2];
}
glm::vec3 rgb(const gpu::Image8& img, std::uint32_t x, std::uint32_t y) {
    const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4;
    return {static_cast<float>(img.rgba[i]), static_cast<float>(img.rgba[i + 1]), static_cast<float>(img.rgba[i + 2])};
}

// The pixels well inside the sphere's disc (the camera sees radius ~0.55 of the frame height at z=4).
template <typename F>
void forInterior(const gpu::Image8& img, float shrink, F&& f) {
    const float cx = img.width * 0.5f;
    const float cy = img.height * 0.5f;
    const float r = img.height * 0.28f * shrink;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const float dx = x + 0.5f - cx;
            const float dy = y + 0.5f - cy;
            if (dx * dx + dy * dy < r * r) {
                f(x, y);
            }
        }
    }
}

// How many distinct 8-bit luminances cover 90% of the sphere's interior.
int tonesCovering90(const gpu::Image8& img) {
    std::map<int, int> histogram;
    int total = 0;
    forInterior(img, 0.85f, [&](std::uint32_t x, std::uint32_t y) {
        ++histogram[static_cast<int>(std::lround(lum(img, x, y)))];
        ++total;
    });
    std::vector<int> counts;
    for (const auto& [_, n] : histogram) {
        counts.push_back(n);
    }
    std::sort(counts.rbegin(), counts.rend());
    int covered = 0;
    int tones = 0;
    for (int n : counts) {
        covered += n;
        ++tones;
        if (covered * 10 >= total * 9) {
            break;
        }
    }
    return tones;
}

struct Loaded {
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    std::unique_ptr<scene::Composition> comp;
    void frame(double seconds = 1.0) {
        params.resetFinals();
        modulator.applyRoutes(bus, params, 1.0 / 60.0);
        FrameTime t{};
        t.renderTime = seconds;
        comp->update(t);
    }
    params::Parameter<float>& param(const std::string& path) {
        auto* p = params.findAs<float>(path);
        INFO(path);
        REQUIRE(p != nullptr);
        return *p;
    }
};

std::unique_ptr<Loaded> load(const std::string& text, assets::AssetRegistry& registry) {
    auto out = std::make_unique<Loaded>();
    auto comp = scene::Composition::fromJson(nlohmann::json::parse(text), registry);
    if (!comp) {
        FAIL(comp.error().message);
    }
    out->comp = std::move(*comp);
    out->comp->attach(out->params, out->modulator);
    out->frame();
    return out;
}

// A procedural sphere on the left and an SDF sphere on the right, one key light.
const char* kTwoKinds = R"({
  "format": "avgen-scene", "version": 1, "name": "toon",
  "camera": { "mode": 1, "position": [0, 0, 9], "target": [0, 0, 0], "fov": 45.0 },
  "environment": { "background": [0, 0, 0], "intensity": 0.0 },
  "lights": [ { "id": "key", "name": "key", "type": "directional", "direction": [0.8, -0.5, -0.4],
                "color": [1, 1, 1], "intensity": 3.0 } ],
  "nodes": [
    { "name": "ball", "kind": "procedural", "position": [-2.2, 0, 0], "procedural": {
        "source": { "kind": "sphere", "radius": 1.5, "segments": 96, "rings": 64 }, "distribution": { "kind": "single" },
        "material": { "baseColor": [0.8, 0.8, 0.8], "roughness": 0.6 } } },
    { "name": "orb", "kind": "sdf", "position": [2.2, 0, 0], "sdf": {
        "tree": { "root": { "kind": "sphere", "radius": 1.5 } },
        "boundsMin": [-2, -2, -2], "boundsMax": [2, 2, 2],
        "material": { "baseColor": [0.8, 0.8, 0.8], "roughness": 0.6, "toon": { "bands": 0 } } } }
  ]
})";

int tonesInColumns(const gpu::Image8& img, std::uint32_t x0, std::uint32_t x1) {
    std::map<int, int> histogram;
    int total = 0;
    for (std::uint32_t y = img.height / 4; y < img.height * 3 / 4; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            const float l = lum(img, x, y);
            if (l < 2.0f) {
                continue; // the background
            }
            ++histogram[static_cast<int>(std::lround(l))];
            ++total;
        }
    }
    std::vector<int> counts;
    for (const auto& [_, n] : histogram) {
        counts.push_back(n);
    }
    std::sort(counts.rbegin(), counts.rend());
    int covered = 0;
    int tones = 0;
    for (int n : counts) {
        covered += n;
        ++tones;
        if (covered * 10 >= total * 9) {
            break;
        }
    }
    return tones;
}

} // namespace

TEST_CASE("Cel lighting cuts a lit sphere into bands, tints its shadow, and adds a rim and a hard highlight",
          "[gpu][toon][adr1071]") {
    Harness h;
    const scene::Scene pbr = litSphere();
    const gpu::Image8 control = h.render(pbr);
    dump(control, "toon-control-pbr");

    scene::Scene toon = pbr;
    toon.entities[0].material.toon.bands = 2.0f;
    toon.entities[0].material.toon.softness = 0.01f;
    toon.entities[0].material.toon.shadowColor = {0.3f, 0.3f, 0.3f};
    toon.entities[0].material.toon.ambient = 0.5f;
    const gpu::Image8 banded = h.render(toon);
    dump(banded, "toon-bands2");
    const int pbrTones = tonesCovering90(control);
    const int toonTones = tonesCovering90(banded);
    INFO("tones covering 90% of the sphere: PBR " << pbrTones << ", toon " << toonTones);
    CHECK(pbrTones > 20);
    CHECK(toonTones <= 4);

    SECTION("the same instant renders the same bytes in a fresh renderer") {
        rendering::SceneRenderer fresh(*h.ctx, h.shaders);
        REQUIRE(fresh.init().has_value());
        FrameTime t{};
        t.renderTime = 1.0;
        auto again = fresh.renderToImage(toon, t, kW, kH);
        REQUIRE(again.has_value());
        CHECK(testing::byteDiff(banded.rgba, again->rgba).identical());
    }

    SECTION("the shadow tone is the albedo times the shadow colour times the ambient floor") {
        scene::Scene red = toon;
        red.entities[0].material.toon.shadowColor = {1.0f, 0.0f, 0.0f};
        red.entities[0].material.toon.ambient = 0.6f;
        const gpu::Image8 img = h.render(red);
        dump(img, "toon-shadow-red");
        // The lower right of the disc faces away from the light.
        const glm::vec3 dark = rgb(img, kW / 2 + 30, kH / 2 + 25);
        INFO("shadow side rgb " << dark.r << " " << dark.g << " " << dark.b);
        CHECK(dark.r > 40.0f);
        CHECK(dark.g < 0.25f * dark.r);
        CHECK(dark.b < 0.25f * dark.r);
    }

    SECTION("the rim lights the silhouette and nothing in the middle") {
        scene::Scene rim = toon;
        rim.entities[0].material.toon.rimWidth = 0.25f;
        rim.entities[0].material.toon.rimColor = {0.0f, 1.0f, 0.0f};
        rim.entities[0].material.toon.rimIntensity = 2.0f;
        const gpu::Image8 img = h.render(rim);
        dump(img, "toon-rim");
        // Near the edge on the shadow side (the rim is not masked by the light), and at the centre.
        const glm::vec3 edgeBefore = rgb(banded, kW / 2 + 49, kH / 2);
        const glm::vec3 edgeAfter = rgb(img, kW / 2 + 49, kH / 2);
        INFO("edge green " << edgeBefore.g << " -> " << edgeAfter.g);
        CHECK(edgeAfter.g > edgeBefore.g + 40.0f);
        CHECK(rgb(img, kW / 2, kH / 2) == rgb(banded, kW / 2, kH / 2));
    }

    SECTION("the hard highlight is a disc of one brightness, off at strength 0") {
        scene::Scene spec = toon;
        spec.entities[0].material.toon.specular = 3.0f;
        spec.entities[0].material.toon.specularSize = 0.3f;
        const gpu::Image8 img = h.render(spec);
        dump(img, "toon-highlight");
        float before = 0.0f;
        float after = 0.0f;
        forInterior(img, 1.0f, [&](std::uint32_t x, std::uint32_t y) {
            before = std::max(before, lum(banded, x, y));
            after = std::max(after, lum(img, x, y));
        });
        INFO("brightest sphere pixel " << before << " -> " << after);
        CHECK(after > before + 10.0f);
        scene::Scene sizeOnly = toon;
        sizeOnly.entities[0].material.toon.specularSize = 0.3f;
        CHECK(testing::byteDiff(h.render(sizeOnly).rgba, banded.rgba).identical());
    }
    CHECK(h.ctx->errorCount() == 0);
}

TEST_CASE("The toon parameters of a procedural node and an SDF object reach their pixels", "[gpu][toon][adr1071]") {
    Harness h;
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto scene = load(kTwoKinds, registry);
    const gpu::Image8 before = h.render(scene->comp->scene());
    dump(before, "toon-kinds-pbr");
    const int procBefore = tonesInColumns(before, 20, 120);
    const int sdfBefore = tonesInColumns(before, 136, 236);
    scene->param("procedural/ball/toon/bands").setBase(2.0f);
    scene->param("sdf/orb/toon/bands").setBase(2.0f);
    scene->frame();
    const gpu::Image8 after = h.render(scene->comp->scene());
    dump(after, "toon-kinds-bands2");
    const int procAfter = tonesInColumns(after, 20, 120);
    const int sdfAfter = tonesInColumns(after, 136, 236);
    INFO("procedural tones " << procBefore << " -> " << procAfter << "; sdf tones " << sdfBefore << " -> " << sdfAfter);
    CHECK(procBefore > 15);
    CHECK(sdfBefore > 15);
    CHECK(procAfter <= 5);
    CHECK(sdfAfter <= 5);
    CHECK(h.ctx->errorCount() == 0);
}

namespace {

// A cube sitting on a large floor, seen from a low camera: the floor runs off to a grazing angle (where a
// naive depth test would draw lines everywhere), and the cube has silhouettes and creases.
scene::Scene cubeOnFloor() {
    scene::Scene s;
    s.environment.backgroundColor = {0.6f, 0.6f, 0.7f};
    s.camera.position = {2.5f, 1.2f, 4.0f};
    s.camera.target = {0.0f, 0.4f, 0.0f};
    const auto floor = s.addMesh(scene::makePlane(40.0f, 8));
    auto& f = s.addEntity("floor", floor);
    f.material.baseColor = {0.7f, 0.7f, 0.7f};
    const auto cube = s.addMesh(scene::makeCube(0.5f));
    auto& c = s.addEntity("cube", cube);
    c.transform.position = {0.0f, 0.5f, 0.0f};
    c.material.baseColor = {0.9f, 0.5f, 0.3f};
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
    key.intensity = 2.0f;
    s.addLight(key);
    s.post.bloomEnabled = false;
    s.post.bloomIntensity = 0.0f;
    return s;
}

// Pixels that turned (near) pure line colour -- red, here -- from the control.
int linePixels(const gpu::Image8& control, const gpu::Image8& img, std::uint32_t y0 = 0, std::uint32_t y1 = kH) {
    int n = 0;
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const glm::vec3 a = rgb(img, x, y);
            const glm::vec3 b = rgb(control, x, y);
            if (a.r > 150.0f && a.g < 60.0f && a.b < 60.0f && !(b.r > 150.0f && b.g < 60.0f)) {
                ++n;
            }
        }
    }
    return n;
}

} // namespace

TEST_CASE("The outline is off at amount 0, draws round silhouettes, ignores a grazing floor and follows its width",
          "[gpu][outline][adr1072]") {
    Harness h;
    const scene::Scene base = cubeOnFloor();
    const gpu::Image8 control = h.render(base);
    dump(control, "outline-control");

    scene::Scene shaped = base;
    shaped.post.outline.width = 4.0f;
    shaped.post.outline.color = {1.0f, 0.0f, 0.0f};
    shaped.post.outline.silhouette = 1.0f;
    shaped.post.outline.fadeEnd = 10.0f;
    CHECK(testing::byteDiff(h.render(shaped).rgba, control.rgba).identical());

    scene::Scene on = base;
    on.post.outline.amount = 1.0f;
    on.post.outline.color = {1.0f, 0.0f, 0.0f};
    on.post.outline.intensity = 1.0f;
    on.post.outline.width = 12.0f; // pixels at 1080 lines: about 2 of this 192-line frame
    const gpu::Image8 lined = h.render(on);
    dump(lined, "outline-on");
    const int lines = linePixels(control, lined);
    INFO("line pixels at 12 px (1080 lines): " << lines);
    CHECK(lines > 150);
    // The floor's far half (the top rows below the horizon are floor at a grazing angle) draws nothing:
    // the second difference of 1 / depth is zero across a plane.
    const int horizonBand = linePixels(control, lined, 0, kH / 5);
    INFO("line pixels in the top fifth (grazing floor and sky): " << horizonBand);
    CHECK(horizonBand < 20);

    scene::Scene wide = on;
    wide.post.outline.width = 36.0f;
    const gpu::Image8 wider = h.render(wide);
    dump(wider, "outline-wide");
    const int wideLines = linePixels(control, wider);
    INFO("line pixels at 36 px (1080 lines): " << wideLines);
    CHECK(wideLines > lines * 2);

    scene::Scene sil = on;
    sil.post.outline.silhouette = 1.0f;
    const gpu::Image8 silhouette = h.render(sil);
    dump(silhouette, "outline-silhouette");
    const int silLines = linePixels(control, silhouette);
    INFO("line pixels, silhouette only: " << silLines);
    CHECK(silLines > 50);
    CHECK(silLines < lines);

    scene::Scene faded = on;
    faded.post.outline.fadeStart = 0.5f;
    faded.post.outline.fadeEnd = 1.0f; // everything is further than a metre: nothing left
    CHECK(linePixels(control, h.render(faded)) == 0);

    // A fresh renderer draws the same bytes.
    rendering::SceneRenderer fresh(*h.ctx, h.shaders);
    REQUIRE(fresh.init().has_value());
    FrameTime t{};
    t.renderTime = 1.0;
    auto again = fresh.renderToImage(on, t, kW, kH);
    REQUIRE(again.has_value());
    CHECK(testing::byteDiff(lined.rgba, again->rgba).identical());
    CHECK(h.ctx->errorCount() == 0);
}

// ---- ADR-1073: wire lines ------------------------------------------------------------------------------

namespace {

// A grid of 3 x 3 boxes, twisted by a deformer, on black: the lines must follow the deformed, instanced
// surface. The key light is dim so the surfaces are dark and the lines read.
const char* kWireScene = R"({
  "format": "avgen-scene", "version": 1, "name": "wire",
  "camera": { "mode": 1, "position": [0, 4, 9], "target": [0, 0, 0], "fov": 45.0 },
  "environment": { "background": [0, 0, 0], "intensity": 0.0 },
  "lights": [ { "id": "key", "name": "key", "type": "directional", "direction": [0.3, -1, -0.4],
                "color": [1, 1, 1], "intensity": 0.5 } ],
  "nodes": [
    { "name": "boxes", "kind": "procedural", "procedural": {
        "source": { "kind": "box", "size": [1.2, 1.2, 1.2] },
        "distribution": { "kind": "grid", "gridCount": [3, 1, 3], "gridSpacing": [2.2, 1, 2.2] },
        "deformers": [ { "kind": "twist", "amount": 0.6 } ],
        "material": { "baseColor": [0.1, 0.1, 0.1], "roughness": 0.8,
                      "wire": { "mode": 0, "color": [0, 1, 0], "intensity": 1.0, "width": 12 } } } }
  ]
})";

int greenPixels(const gpu::Image8& img) {
    int n = 0;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const glm::vec3 c = rgb(img, x, y);
            if (c.g > 120.0f && c.r < 0.6f * c.g && c.b < 0.6f * c.g) {
                ++n;
            }
        }
    }
    return n;
}

} // namespace

TEST_CASE("Wire lines draw a procedural node's feature edges, follow its width, and can stand alone",
          "[gpu][wire][adr1073]") {
    Harness h;
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto scene = load(kWireScene, registry);
    const gpu::Image8 off = h.render(scene->comp->scene());
    dump(off, "wire-off");
    CHECK(greenPixels(off) == 0);

    scene->param("procedural/boxes/wire/mode").setBase(1.0f);
    scene->frame();
    const gpu::Image8 feature = h.render(scene->comp->scene());
    dump(feature, "wire-feature");
    const int featureLines = greenPixels(feature);
    INFO("green pixels, feature edges at 12 px (1080 lines): " << featureLines);
    CHECK(featureLines > 400);

    scene->param("procedural/boxes/wire/width").setBase(30.0f);
    scene->frame();
    const gpu::Image8 wide = h.render(scene->comp->scene());
    dump(wide, "wire-wide");
    const int wideLines = greenPixels(wide);
    INFO("green pixels at 30 px: " << wideLines);
    CHECK(wideLines > featureLines * 3 / 2);

    // Hidden lines: the far edges of each box are behind its own faces. X-ray shows them.
    scene->param("procedural/boxes/wire/width").setBase(12.0f);
    scene->param("procedural/boxes/wire/occlude").setBase(0.0f);
    scene->frame();
    const gpu::Image8 xray = h.render(scene->comp->scene());
    dump(xray, "wire-xray");
    INFO("green pixels, x-ray: " << greenPixels(xray));
    CHECK(greenPixels(xray) > featureLines);

    // Lines only: the surface is gone (the dark faces become background) and the lines remain.
    scene->param("procedural/boxes/wire/occlude").setBase(1.0f);
    scene->param("procedural/boxes/wire/fill").setBase(0.0f);
    scene->frame();
    const gpu::Image8 alone = h.render(scene->comp->scene());
    dump(alone, "wire-alone");
    CHECK(greenPixels(alone) > featureLines);
    int litSurface = 0;
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const glm::vec3 c = rgb(alone, x, y);
            if (c.r > 4.0f && c.g < 60.0f) {
                ++litSurface;
            }
        }
    }
    CHECK(litSurface < 20);

    // Seek-deterministic: a fresh renderer, the same instant, the same bytes.
    rendering::SceneRenderer fresh(*h.ctx, h.shaders);
    REQUIRE(fresh.init().has_value());
    FrameTime t{};
    t.renderTime = 1.0;
    auto again = fresh.renderToImage(scene->comp->scene(), t, kW, kH);
    REQUIRE(again.has_value());
    CHECK(testing::byteDiff(alone.rgba, again->rgba).identical());
    CHECK(h.ctx->errorCount() == 0);
}
