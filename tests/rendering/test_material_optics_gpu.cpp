// ADR-1143 (thin-film interference and anisotropic highlights in pbr_shade) on pixels, with no
// commercial assets: a generated metal sphere, and a procedural node, an SDF object and an orb loaded
// from text.
//
//   * defaults are byte-identical: a material with the new fields absent and one with them explicitly
//     zero (with non-default ior and rotation) render the same bytes, on every drawable kind;
//   * a 300 nm film on a metal sphere changes its hue, and the scene-linear ratio film / bare at the
//     centre of the sphere matches scene::thinFilmTint (the CPU twin) within tolerance;
//   * anisotropy 0.8 stretches the highlight along the tangent (second-moment aspect ratio > 1.5,
//     major axis along the meridian the reference tangent follows) where strength 0 leaves it round,
//     and rotation and a negative strength turn the stretch a quarter turn;
//   * the procedural node's and the SDF object's parameters reach their pixels.
//
// With AVGEN_OPTICS_DUMP=<dir> every arm is written as a PNG.

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
#include "scene/material_optics.hpp"
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
#include <memory>
#include <string>

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

const char* dumpDir() {
    const char* dir = std::getenv("AVGEN_OPTICS_DUMP");
    return (dir == nullptr || dir[0] == '\0') ? nullptr : dir;
}

void dump(const gpu::Image8& image, const std::string& name) {
    if (const char* dir = dumpDir()) {
        static_cast<void>(assets::writePng(fs::path(dir) / (name + ".png"), image.width, image.height, image.rgba));
    }
}

struct Harness {
    std::unique_ptr<gpu::Context> ctx = makeContext();
    gpu::ShaderLibrary shaders{*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)}};
    rendering::SceneRenderer renderer{*ctx, shaders};
    Harness() { REQUIRE(renderer.init().has_value()); }
    gpu::Image8 render(const scene::Scene& s, std::uint32_t w, std::uint32_t h) {
        FrameTime t{};
        t.renderTime = 1.0;
        renderer.resetTemporalHistory();
        auto img = renderer.renderToImage(s, t, w, h);
        REQUIRE(img.has_value());
        return std::move(*img);
    }
    gpu::ImageF renderLinear(const scene::Scene& s, std::uint32_t w, std::uint32_t h) {
        FrameTime t{};
        t.renderTime = 1.0;
        t.deltaTime = 1.0 / 60.0;
        renderer.resetTemporalHistory();
        auto img = renderer.renderToImageFloat(s, t, w, h);
        REQUIRE(img.has_value());
        return std::move(*img);
    }
};

// A polished steel sphere (f0 0.56, metallic) at the origin, lit by one directional light travelling
// straight away from the camera, on black: the highlight sits at the centre of the disc, where N.V = 1
// and V.H = 1, so the Fresnel there is exactly f0. No bloom; the default manual exposure.
scene::Scene steelSphere() {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.camera.position = {0.0f, 0.0f, 3.2f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    const auto mesh = s.addMesh(scene::makeIcosphere(1.0f, 6));
    auto& e = s.addEntity("ball", mesh);
    e.material.baseColor = {0.56f, 0.56f, 0.56f};
    e.material.metallic = 1.0f;
    e.material.roughness = 0.5f;
    scene::PunctualLight key;
    key.name = "key";
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = {0.0f, 0.0f, -1.0f};
    key.intensity = 3.0f;
    s.addLight(key);
    s.post.bloomEnabled = false;
    s.post.bloomIntensity = 0.0f;
    return s;
}

glm::vec3 meanCentre(const gpu::ImageF& img, float radiusPx) {
    const float cx = img.width * 0.5f;
    const float cy = img.height * 0.5f;
    glm::dvec3 sum(0.0);
    int n = 0;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const float dx = x + 0.5f - cx;
            const float dy = y + 0.5f - cy;
            if (dx * dx + dy * dy < radiusPx * radiusPx) {
                const float* p = img.pixel(x, y);
                sum += glm::dvec3(p[0], p[1], p[2]);
                ++n;
            }
        }
    }
    REQUIRE(n > 0);
    return glm::vec3(sum / static_cast<double>(n));
}

// The highlight's shape: the luminance-weighted second moments of every pixel brighter than a quarter of
// the peak (weight = how far above that floor). Returns {aspect = sqrt(major / minor eigenvalue),
// cov_xx, cov_yy}.
struct Moments {
    double aspect = 0.0;
    double xx = 0.0;
    double yy = 0.0;
};
Moments highlightMoments(const gpu::ImageF& img) {
    const auto lum = [&](std::uint32_t x, std::uint32_t y) {
        const float* p = img.pixel(x, y);
        return 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
    };
    double peak = 0.0;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            peak = std::max(peak, lum(x, y));
        }
    }
    const double floor = 0.25 * peak;
    double w = 0.0;
    double mx = 0.0;
    double my = 0.0;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const double v = lum(x, y) - floor;
            if (v > 0.0) {
                w += v;
                mx += v * x;
                my += v * y;
            }
        }
    }
    REQUIRE(w > 0.0);
    mx /= w;
    my /= w;
    double xx = 0.0;
    double yy = 0.0;
    double xy = 0.0;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const double v = lum(x, y) - floor;
            if (v > 0.0) {
                xx += v * (x - mx) * (x - mx);
                yy += v * (y - my) * (y - my);
                xy += v * (x - mx) * (y - my);
            }
        }
    }
    xx /= w;
    yy /= w;
    xy /= w;
    const double tr = xx + yy;
    const double det = xx * yy - xy * xy;
    const double disc = std::sqrt(std::max(tr * tr / 4.0 - det, 0.0));
    const double l1 = tr / 2.0 + disc;
    const double l2 = std::max(tr / 2.0 - disc, 1e-9);
    return {std::sqrt(l1 / l2), xx, yy};
}

gpu::Image8 toImage8(const gpu::ImageF& f) {
    gpu::Image8 out;
    out.width = f.width;
    out.height = f.height;
    out.rgba.resize(static_cast<std::size_t>(f.width) * f.height * 4);
    for (std::size_t i = 0; i < out.rgba.size(); ++i) {
        const float v = (i % 4 == 3) ? 1.0f : f.rgba[i] / (1.0f + f.rgba[i]); // Reinhard, for looking
        out.rgba[i] = static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
    }
    return out;
}

struct Loaded {
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    std::unique_ptr<scene::Composition> comp;
    void frame() {
        params.resetFinals();
        modulator.applyRoutes(bus, params, 1.0 / 60.0);
        FrameTime t{};
        t.renderTime = 1.0;
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

// A procedural sphere, an SDF sphere and an orb, metal, one key light. `%OPTICS%` is spliced into every
// material: empty for the control, explicit zeros for the defaults arm.
std::string threeKinds(const std::string& optics) {
    std::string text = R"({
  "format": "avgen-scene", "version": 1, "name": "optics",
  "camera": { "mode": 1, "position": [0, 0, 11], "target": [0, 0, 0], "fov": 45.0 },
  "environment": { "background": [0, 0, 0], "intensity": 0.0 },
  "lights": [ { "id": "key", "name": "key", "type": "directional", "direction": [0.3, -0.4, -1.0],
                "color": [1, 1, 1], "intensity": 3.0 },
              { "id": "fill", "name": "fill", "type": "point", "position": [0, 3, 4],
                "color": [0.6, 0.7, 1], "intensity": 40.0 } ],
  "nodes": [
    { "name": "ball", "kind": "procedural", "position": [-3.2, 0, 0], "procedural": {
        "source": { "kind": "sphere", "radius": 1.4, "segments": 96, "rings": 64 }, "distribution": { "kind": "single" },
        "material": { "baseColor": [0.56, 0.56, 0.56], "metallic": 1, "roughness": 0.4 %OPTICS% } } },
    { "name": "orb", "kind": "sdf", "position": [0, 0, 0], "sdf": {
        "tree": { "root": { "kind": "sphere", "radius": 1.4 } },
        "boundsMin": [-2, -2, -2], "boundsMax": [2, 2, 2],
        "material": { "baseColor": [0.56, 0.56, 0.56], "metallic": 1, "roughness": 0.4 %OPTICS% } } },
    { "name": "bead", "kind": "orb", "position": [3.2, 0, 0], "scale": [1.4, 1.4, 1.4],
      "material": { "baseColor": [0.56, 0.56, 0.56], "metallic": 1, "roughness": 0.4,
                    "emissiveIntensity": 0 %OPTICS% } }
  ]
})";
    const std::string key = "%OPTICS%";
    for (std::size_t at = text.find(key); at != std::string::npos; at = text.find(key)) {
        text.replace(at, key.size(), optics);
    }
    return text;
}

} // namespace

TEST_CASE("ADR-1143: a material without thin film or anisotropy renders the bytes it always did",
          "[gpu][material_optics][adr1143]") {
    Harness h;
    SECTION("an entity: absent fields and explicit zeros with a non-default ior and rotation") {
        const scene::Scene control = steelSphere();
        scene::Scene zeros = control;
        zeros.entities[0].material.thinFilm.thickness = 0.0f;
        zeros.entities[0].material.thinFilm.ior = 1.7f;
        zeros.entities[0].material.anisotropy.strength = 0.0f;
        zeros.entities[0].material.anisotropy.rotation = 1.2f;
        const gpu::Image8 a = h.render(control, 256, 192);
        const gpu::Image8 b = h.render(zeros, 256, 192);
        dump(a, "optics-defaults-entity");
        const auto diff = testing::byteDiff(a.rgba, b.rgba);
        INFO("differing bytes: " << diff.differing);
        CHECK(diff.identical());
    }
    SECTION("a procedural node, an SDF object and an orb loaded from a scene file") {
        assets::AssetRegistry registry{testsupport::processTempDir()};
        auto control = load(threeKinds(""), registry);
        auto zeros = load(threeKinds(R"(, "thinFilm": {"thickness": 0, "ior": 1.7},
                                         "anisotropy": {"strength": 0, "rotation": 1.2})"),
                          registry);
        const gpu::Image8 a = h.render(control->comp->scene(), 384, 160);
        const gpu::Image8 b = h.render(zeros->comp->scene(), 384, 160);
        dump(a, "optics-defaults-kinds");
        const auto diff = testing::byteDiff(a.rgba, b.rgba);
        INFO("differing bytes: " << diff.differing);
        CHECK(diff.identical());
        // And the arm is not vacuous: a film on the same three surfaces does change them.
        auto film = load(threeKinds(R"(, "thinFilm": {"thickness": 60})"), registry);
        const gpu::Image8 c = h.render(film->comp->scene(), 384, 160);
        dump(c, "optics-film-kinds");
        CHECK_FALSE(testing::byteDiff(a.rgba, c.rgba).identical());
    }
    CHECK(h.ctx->errorCount() == 0);
}

TEST_CASE("ADR-1143: a 300 nm film turns a steel sphere's hue, and matches the CPU tint at its centre",
          "[gpu][material_optics][adr1143]") {
    Harness h;
    constexpr std::uint32_t kSize = 256;
    const scene::Scene bare = steelSphere();
    scene::Scene filmed = bare;
    filmed.entities[0].material.thinFilm.thickness = 300.0f;
    const gpu::ImageF a = h.renderLinear(bare, kSize, kSize);
    const gpu::ImageF b = h.renderLinear(filmed, kSize, kSize);
    dump(toImage8(a), "optics-film-0nm");
    dump(toImage8(b), "optics-film-300nm");

    // Within 6 px of the centre the sphere's N.V is above 0.995 (its radius is ~85 px here).
    const glm::vec3 bareMean = meanCentre(a, 6.0f);
    const glm::vec3 filmMean = meanCentre(b, 6.0f);
    INFO("bare " << bareMean.r << " " << bareMean.g << " " << bareMean.b);
    INFO("film " << filmMean.r << " " << filmMean.g << " " << filmMean.b);
    REQUIRE(bareMean.g > 0.05f);
    // The bare steel is grey; the filmed steel is not.
    CHECK(std::abs(bareMean.r - bareMean.b) < 0.02f * bareMean.g);
    CHECK(filmMean.b > 1.4f * filmMean.r);

    // Metal has no diffuse term, and at N.V = V.H = 1 the Fresnel is f0, so film / bare is the tint.
    const glm::vec3 ratio = filmMean / bareMean;
    const glm::vec3 expected = scene::thinFilmTint(1.0f, 300.0f, 2.4f, 0.56f, 1.0f);
    INFO("ratio " << ratio.r << " " << ratio.g << " " << ratio.b);
    INFO("CPU tint " << expected.r << " " << expected.g << " " << expected.b);
    for (int c = 0; c < 3; ++c) {
        CHECK(std::abs(ratio[c] - expected[c]) < 0.04f * expected[c] + 0.01f);
    }

    SECTION("a first-order straw is warm, a first-order blue is blue") {
        scene::Scene straw = bare;
        straw.entities[0].material.thinFilm.thickness = 30.0f;
        scene::Scene blue = bare;
        blue.entities[0].material.thinFilm.thickness = 70.0f;
        const glm::vec3 s = meanCentre(h.renderLinear(straw, kSize, kSize), 6.0f) / bareMean;
        const glm::vec3 bl = meanCentre(h.renderLinear(blue, kSize, kSize), 6.0f) / bareMean;
        INFO("straw " << s.r << " " << s.g << " " << s.b << "  blue " << bl.r << " " << bl.g << " " << bl.b);
        CHECK(s.r > s.g);
        CHECK(s.g > s.b);
        CHECK(bl.b > bl.g);
        CHECK(bl.g > bl.r);
    }
    CHECK(h.ctx->errorCount() == 0);
}

TEST_CASE("ADR-1143: anisotropy stretches the highlight along the tangent", "[gpu][material_optics][adr1143]") {
    Harness h;
    constexpr std::uint32_t kSize = 512;
    const scene::Scene round = steelSphere();
    const gpu::ImageF iso = h.renderLinear(round, kSize, kSize);
    dump(toImage8(iso), "optics-aniso-0");
    const Moments m0 = highlightMoments(iso);
    INFO("isotropic aspect " << m0.aspect << " (xx " << m0.xx << ", yy " << m0.yy << ")");
    CHECK(m0.aspect < 1.15);

    scene::Scene stretched = round;
    stretched.entities[0].material.anisotropy.strength = 0.8f;
    const gpu::ImageF an = h.renderLinear(stretched, kSize, kSize);
    dump(toImage8(an), "optics-aniso-0.8");
    const Moments m8 = highlightMoments(an);
    INFO("anisotropic aspect " << m8.aspect << " (xx " << m8.xx << ", yy " << m8.yy << ")");
    CHECK(m8.aspect > 1.5);
    // The reference tangent is the object's +Y projected onto the sphere: at the centre of the disc it
    // is the screen's vertical, so the highlight runs top to bottom.
    CHECK(m8.yy > 2.0 * m8.xx);

    SECTION("a quarter turn of rotation, or a negative strength, lays it on its side") {
        scene::Scene turned = stretched;
        turned.entities[0].material.anisotropy.rotation = 1.5707963f;
        const gpu::ImageF t = h.renderLinear(turned, kSize, kSize);
        dump(toImage8(t), "optics-aniso-0.8-rot90");
        const Moments mt = highlightMoments(t);
        INFO("rotated aspect " << mt.aspect << " (xx " << mt.xx << ", yy " << mt.yy << ")");
        CHECK(mt.aspect > 1.5);
        CHECK(mt.xx > 2.0 * mt.yy);

        scene::Scene negative = round;
        negative.entities[0].material.anisotropy.strength = -0.8f;
        const Moments mn = highlightMoments(h.renderLinear(negative, kSize, kSize));
        INFO("negative aspect " << mn.aspect << " (xx " << mn.xx << ", yy " << mn.yy << ")");
        CHECK(mn.aspect > 1.5);
        CHECK(mn.xx > 2.0 * mn.yy);
    }
    CHECK(h.ctx->errorCount() == 0);
}

TEST_CASE("ADR-1143: the procedural node's and the SDF object's optics parameters reach their pixels",
          "[gpu][material_optics][adr1143]") {
    Harness h;
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto loaded = load(threeKinds(""), registry);
    const gpu::Image8 before = h.render(loaded->comp->scene(), 384, 160);
    loaded->param("procedural/ball/material/thinFilm/thickness").setBase(65.0f);
    loaded->param("sdf/orb/material/anisotropy/strength").setBase(0.9f);
    loaded->frame();
    const gpu::Image8 after = h.render(loaded->comp->scene(), 384, 160);
    dump(after, "optics-params");
    // Compare the three thirds of the frame: the procedural third and the SDF third change, the orb's
    // third (its material was not touched) does not.
    const auto thirdDiffers = [&](int third) {
        int differing = 0;
        for (std::uint32_t y = 0; y < before.height; ++y) {
            for (std::uint32_t x = third * 128u; x < (third + 1) * 128u; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * before.width + x) * 4;
                for (int c = 0; c < 3; ++c) {
                    differing += before.rgba[i + c] != after.rgba[i + c] ? 1 : 0;
                }
            }
        }
        return differing;
    };
    const int proc = thirdDiffers(0);
    const int sdf = thirdDiffers(1);
    const int orb = thirdDiffers(2);
    INFO("differing channels: procedural " << proc << ", sdf " << sdf << ", orb " << orb);
    CHECK(proc > 500);
    CHECK(sdf > 200);
    CHECK(orb == 0);
    CHECK(h.ctx->errorCount() == 0);
}
