// ADR-945 on pixels: glow pools 100-250 m from the lens.
//
// A dark field, 480 m square, carpeted with a faint glowing grass (0.05) and dotted with bright
// fungi. The camera stands 30 m up and looks across it. Three arms of the same frame:
//
//   * dark     -- `scene/ecologyLight` 0: no ecology lights, only what the surfaces emit themselves;
//   * chosen   -- the engine's choice (ADR-945): the fungi that put the most light into the picture,
//                 each a pool on the ground round it;
//   * control  -- the rule it replaced, rebuilt here from the same clusters: the nearest patches
//                 whatever their power, each a light at half its layer's height reaching 4x its
//                 spread, as many as the engine made.
//
// At the ground under every chosen light 100-250 m out the chosen frame is brighter than the dark
// one, and the control's is not. Then `scene/glow-pools/reach` on pixels: a wider reach lights more
// of the ground. With AVGEN_ECOLOGY_DUMP=<dir> every arm is written as a PNG.

#include "assets/asset_registry.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"
#include "scene/scene.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 480;
constexpr std::uint32_t kHeight = 270;

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

struct Harness {
    std::unique_ptr<gpu::Context> ctx = makeContext();
    gpu::ShaderLibrary shaders{*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)}};
    rendering::SceneRenderer renderer{*ctx, shaders};
    Harness() { REQUIRE(renderer.init().has_value()); }
    gpu::Image8 render(const scene::Scene& s) {
        FrameTime t{};
        t.renderTime = 1.0;
        renderer.resetTemporalHistory();
        auto first = renderer.renderToImage(s, t, kWidth, kHeight);
        REQUIRE(first.has_value());
        auto img = renderer.renderToImage(s, t, kWidth, kHeight);
        REQUIRE(img.has_value());
        return std::move(*img);
    }
};

void dump(const gpu::Image8& image, const std::string& name) {
    const char* dir = std::getenv("AVGEN_ECOLOGY_DUMP");
    if (dir == nullptr || dir[0] == '\0') {
        return;
    }
    static_cast<void>(assets::writePng(fs::path(dir) / (name + ".png"), image.width, image.height, image.rgba));
}

std::string field(const fs::path& asset) {
    std::string text = R"({
      "format": "avgen-scene", "version": 1, "name": "field",
      "camera": { "mode": 1, "position": [0, 30, 120], "target": [0, 0, -60], "fov": 50.0, "orbitSpeed": 0.0 },
      "environment": { "background": [0.004, 0.005, 0.01], "intensity": 0.0,
                       "ecologyLight": 8.0, "ecologyLightRange": 300.0 },
      "nodes": [
        { "name": "ground", "kind": "terrain", "world": { "name": "wide", "size": [480, 480], "features": [] },
          "terrain": { "chunkSize": 60.0, "resolution": 16, "lodLevels": 1, "viewDistance": 600.0 },
          "material": { "baseColor": [0.4, 0.4, 0.4], "roughness": 0.9 },
          "scatter": [
            { "name": "fungi", "asset": "@ASSET@", "densities": { "meadow": 0.01, "forest": 0.01 },
              "height": 0.3, "emissiveIntensity": 6.0, "emissiveColor": [0.34, 0.08, 1.0], "castsShadow": false },
            { "name": "grass", "asset": "@ASSET@", "densities": { "meadow": 0.03, "forest": 0.03 },
              "height": 0.5, "emissiveIntensity": 0.05, "emissiveColor": [0.05, 1.0, 0.55], "castsShadow": false } ] }
      ]
    })";
    for (int i = 0; i < 2; ++i) {
        text.replace(text.find("@ASSET@"), 7, asset.string());
    }
    return text;
}

struct Loaded {
    params::ParameterSet params;
    params::Modulator modulator;
    std::unique_ptr<scene::Composition> comp;
    void frame() {
        params.resetFinals();
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
    out->comp->setViewport(kWidth, kHeight);
    out->comp->attach(out->params, out->modulator);
    out->frame();
    return out;
}

bool isEcology(const scene::PunctualLight& l) { return l.name.starts_with("ecology.glow."); }

glm::vec2 project(const scene::Camera& camera, const glm::vec3& p) {
    const glm::mat4 vp = camera.projection(static_cast<float>(kWidth) / static_cast<float>(kHeight)) * camera.view();
    const glm::vec4 c = vp * glm::vec4(p, 1.0f);
    const glm::vec2 ndc = glm::vec2(c) / c.w;
    return {(ndc.x * 0.5f + 0.5f) * static_cast<float>(kWidth), (0.5f - ndc.y * 0.5f) * static_cast<float>(kHeight)};
}

// Summed rgb (0..765) of a 3x3 window, or -1 when it is off the frame.
double windowSum(const gpu::Image8& img, glm::vec2 c) {
    double sum = 0.0;
    for (int y = static_cast<int>(c.y) - 1; y <= static_cast<int>(c.y) + 1; ++y) {
        for (int x = static_cast<int>(c.x) - 1; x <= static_cast<int>(c.x) + 1; ++x) {
            if (x < 0 || y < 0 || x >= static_cast<int>(kWidth) || y >= static_cast<int>(kHeight)) {
                return -1.0;
            }
            const std::uint8_t* p = img.pixel(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            sum += p[0] + p[1] + p[2];
        }
    }
    return sum / 9.0;
}

std::size_t brighter(const gpu::Image8& a, const gpu::Image8& b, int threshold = 6) {
    std::size_t n = 0;
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        const int da = a.rgba[i] + a.rgba[i + 1] + a.rgba[i + 2];
        const int db = b.rgba[i] + b.rgba[i + 1] + b.rgba[i + 2];
        n += da - db > threshold ? 1u : 0u;
    }
    return n;
}

} // namespace

TEST_CASE("Glow pools 100-250 m out: the chosen lights make them, the nearest-first rule does not",
          "[gpu][ecology][ecologylight][adr945]") {
    const fs::path asset = fs::path(AVGEN_SOURCE_DIR) / "assets/quaternius/glTF/Mushroom_Common.gltf";
    if (!fs::exists(asset)) {
        SKIP("the Quaternius library is not present in this checkout");
    }
    Harness h;
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto scene = load(field(asset), registry);
    const scene::CompositionNode* ground = scene->comp->findNode("ground");
    REQUIRE(ground != nullptr);
    const float gain = scene->param("scene/ecologyLight").base();

    const scene::Scene chosenScene = scene->comp->scene();
    const scene::Camera camera = chosenScene.camera;
    const gpu::Image8 chosen = h.render(chosenScene);

    // The control, from the same clusters: nearest first within the range, the old placement.
    scene::Scene controlScene = chosenScene;
    std::erase_if(controlScene.lights, isEcology);
    const std::size_t made = static_cast<std::size_t>(
        std::count_if(chosenScene.lights.begin(), chosenScene.lights.end(), isEcology));
    REQUIRE(made > 100);
    {
        std::vector<const world::GlowCluster*> order;
        for (const world::GlowCluster& g : ground->glow) {
            if (glm::length(g.position - camera.position) <= 300.0f) order.push_back(&g);
        }
        std::sort(order.begin(), order.end(), [&](const auto* a, const auto* b) {
            return glm::length(a->position - camera.position) < glm::length(b->position - camera.position);
        });
        order.resize(std::min(order.size(), made));
        for (std::size_t i = 0; i < order.size(); ++i) {
            const world::GlowCluster& g = *order[i];
            scene::PunctualLight l;
            l.name = "ecology.glow." + std::to_string(i);
            l.type = scene::PunctualLight::Type::Point;
            l.role = scene::PunctualLight::Role::Practical;
            l.position = g.position;
            l.color = g.color;
            l.intensity = g.power * gain;
            l.radius = std::max(g.radius, 0.25f);
            l.range = g.radius * 4.0f;
            l.castsShadow = false;
            l.contactShadow = false;
            l.volumetricStrength = 0.0f;
            controlScene.lights.push_back(l);
        }
    }
    const gpu::Image8 control = h.render(controlScene);

    scene->param("scene/ecologyLight").setBase(0.0f);
    scene->frame();
    const gpu::Image8 dark = h.render(scene->comp->scene());
    dump(dark, "pools-dark");
    dump(chosen, "pools-chosen");
    dump(control, "pools-control-nearest-first");

    // The ground under each chosen light 100-250 m out.
    std::size_t far = 0;
    std::size_t litChosen = 0;
    std::size_t litControl = 0;
    double riseChosen = 0.0;
    double riseControl = 0.0;
    for (const scene::PunctualLight& l : chosenScene.lights) {
        if (!isEcology(l)) continue;
        const float d = glm::length(glm::vec2(l.position.x - camera.position.x, l.position.z - camera.position.z));
        if (d < 100.0f || d > 250.0f) continue;
        const glm::vec3 underfoot = l.position - glm::vec3(0.0f, l.range * 0.25f, 0.0f);
        const glm::vec2 px = project(camera, underfoot);
        const double base = windowSum(dark, px);
        if (base < 0.0) continue;
        ++far;
        const double a = windowSum(chosen, px) - base;
        const double b = windowSum(control, px) - base;
        riseChosen += a;
        riseControl += b;
        litChosen += a > 6.0 ? 1 : 0;
        litControl += b > 6.0 ? 1 : 0;
    }
    INFO(made << " lights; " << far << " on screen 100-250 m out; ground lit under " << litChosen
              << " (chosen) against " << litControl << " (nearest-first); mean rise "
              << (far > 0 ? riseChosen / static_cast<double>(far) : 0.0) << " against "
              << (far > 0 ? riseControl / static_cast<double>(far) : 0.0));
    REQUIRE(far > 30);
    CHECK(litChosen > far * 3 / 4);
    CHECK(litControl < far / 20);
    CHECK(riseChosen > riseControl * 10.0 + 1.0);

    SECTION("scene/glow-pools/reach: a wider pool lights more of the ground") {
        scene->param("scene/ecologyLight").setBase(gain);
        scene->param("scene/glow-pools/reach").setBase(3.0f);
        scene->frame();
        const gpu::Image8 small = h.render(scene->comp->scene());
        scene->param("scene/glow-pools/reach").setBase(14.0f);
        scene->frame();
        const gpu::Image8 wide = h.render(scene->comp->scene());
        dump(small, "pools-reach-3");
        dump(wide, "pools-reach-14");
        const std::size_t a = brighter(small, dark);
        const std::size_t b = brighter(wide, dark);
        INFO("pixels brighter than dark: reach 3 m " << a << ", reach 14 m " << b);
        CHECK(b > a * 3 / 2);
        CHECK(a > 200);
    }

    SECTION("scene/glow-pools/faintest: at zero the faint grass casts pools too") {
        scene->param("scene/ecologyLight").setBase(gain);
        scene->param("scene/glow-pools/faintest").setBase(0.0f);
        scene->frame();
        const gpu::Image8 all = h.render(scene->comp->scene());
        dump(all, "pools-faintest-0");
        std::size_t grass = 0;
        for (const scene::PunctualLight& l : scene->comp->scene().lights) {
            grass += isEcology(l) && l.color.y > l.color.z ? 1 : 0;
        }
        INFO(grass << " grass lights; pixels differing from the default choice: " << brighter(all, chosen) + brighter(chosen, all));
        CHECK(grass > 0);
        CHECK(brighter(all, chosen) + brighter(chosen, all) > 200);
    }
}
