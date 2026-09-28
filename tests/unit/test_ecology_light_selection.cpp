// ADR-945: the ecology lights are the patches that put the most light into the picture, each a pool
// on the ground round its patch, and the three things an artist sets about them are parameters that
// reach the lights.
//
//   * the pure choice: power over squared distance, inside the frustum and the distance limit, never
//     the nearest-first rule it replaced (whose own answer on the same candidates is the control);
//   * through a composition: a faint layer near the camera no longer takes the budget, so the fungi
//     100-250 m out get lights -- where the old rule, rebuilt here from the same clusters, gives none;
//   * `scene/glow-pools/reach`, `faintest` and `distance` each move the lights, and name themselves in
//     the Parameters panel's "glow-pools" section;
//   * a seek lands on the lights play reached.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/ecology_lights.hpp"
#include "support/temp_dir.hpp"
#include "ui/ui_logic.hpp"
#include "world/terrain.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;
namespace fs = std::filesystem;

namespace {

// Everything visible: a frustum whose planes never reject.
scene::EcologyLightView openView(glm::vec3 eye, float farthest) {
    scene::EcologyLightView v;
    v.eye = eye;
    v.farthest = farthest;
    for (glm::vec4& p : v.planes) {
        p = glm::vec4(0.0f, 1.0f, 0.0f, 1e6f);
    }
    return v;
}

} // namespace

TEST_CASE("Ecology lights are chosen by what they put into the picture, not by how near they are",
          "[ecology][ecologylight][adr945]") {
    // 300 faint patches 10-40 m away, and 40 bright ones 150-190 m away; room for 100 lights.
    std::vector<scene::EcologyLightCandidate> c;
    for (int i = 0; i < 300; ++i) {
        const float d = 10.0f + 30.0f * static_cast<float>(i) / 300.0f;
        c.push_back({glm::vec3(d, 0.0f, 0.0f), 4.0f, 0.5f});
    }
    for (int i = 0; i < 40; ++i) {
        const float d = 150.0f + static_cast<float>(i);
        c.push_back({glm::vec3(0.0f, 0.0f, -d), 4.0f, 5000.0f});
    }
    const scene::EcologyLightView view = openView(glm::vec3(0.0f), 300.0f);
    const auto chosen = scene::chooseEcologyLights(c, view, 100);
    REQUIRE(chosen.size() == 100);
    const auto far = std::count_if(chosen.begin(), chosen.end(), [](const auto& ch) { return ch.index >= 300; });
    CHECK(far == 40);
    // Highest first, so the clustered pass keeps the ones that matter where a froxel is crowded.
    for (std::size_t i = 1; i < chosen.size(); ++i) {
        CHECK(chosen[i - 1].score >= chosen[i].score);
    }

    // The control: the rule this replaced -- the 100 nearest -- takes none of the far patches.
    std::vector<std::size_t> nearest(c.size());
    for (std::size_t i = 0; i < c.size(); ++i) nearest[i] = i;
    std::sort(nearest.begin(), nearest.end(), [&](std::size_t a, std::size_t b) {
        return glm::length(c[a].position) < glm::length(c[b].position);
    });
    nearest.resize(100);
    CHECK(std::count_if(nearest.begin(), nearest.end(), [](std::size_t i) { return i >= 300; }) == 0);

    SECTION("nothing off screen or past the distance limit is chosen") {
        scene::EcologyLightView looking = view;
        // One plane: only what is in front (-z) of the eye is visible.
        looking.planes[0] = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
        const auto front = scene::chooseEcologyLights(c, looking, 100);
        for (const auto& ch : front) {
            // The faint patches stand on +x, at the plane; their 4 m reach crosses it, so they may be
            // chosen -- but only while their reach does.
            CHECK(c[ch.index].position.z <= c[ch.index].range);
        }
        scene::EcologyLightView near = view;
        near.farthest = 100.0f;
        const auto close = scene::chooseEcologyLights(c, near, 400);
        CHECK(close.size() == 300);
        for (const auto& ch : close) {
            CHECK(ch.index < 300);
        }
    }

    SECTION("a light arrives at the budget's cut and at the distance limit from nothing") {
        // The last one in scores exactly what the first one out does -> weight 0; well above it -> 1.
        std::vector<scene::EcologyLightCandidate> two = {{glm::vec3(0, 0, -20), 1.0f, 100.0f},
                                                         {glm::vec3(0, 0, -20), 1.0f, 100.0f},
                                                         {glm::vec3(0, 0, -10), 1.0f, 100.0f}};
        const auto cut = scene::chooseEcologyLights(two, view, 2);
        REQUIRE(cut.size() == 2);
        CHECK(cut[0].index == 2);
        CHECK(cut[0].weight == 1.0f);
        CHECK(cut[1].weight == 0.0f); // its twin was left out
        std::vector<scene::EcologyLightCandidate> edge = {{glm::vec3(0, 0, -299.0f), 1.0f, 100.0f}};
        const auto faded = scene::chooseEcologyLights(edge, view, 10);
        REQUIRE(faded.size() == 1);
        CHECK(faded[0].weight < 0.05f);
    }
}

TEST_CASE("A glow pool's light stands at half its reach over the patch's ground", "[ecology][ecologylight][adr945]") {
    world::GlowCluster g;
    g.position = glm::vec3(10.0f, 5.15f, -3.0f); // the build lifted it 0.15 m, half a 0.3 m mushroom
    g.radius = 2.0f;
    const scene::GlowPool pool = scene::glowPool(g, 0.15f, 8.0f);
    CHECK_THAT(pool.position.y, WithinAbs(5.0 + 4.0, 1e-5));
    CHECK(pool.position.x == 10.0f);
    CHECK(pool.range == 16.0f);
    // Never smaller than the patch's spread, never below its glowing organ.
    CHECK(scene::glowPool(g, 0.15f, 0.5f).radius == 2.0f);
    CHECK_THAT(scene::glowPool(g, 4.0f, 1.0f).position.y, WithinAbs(5.15, 1e-5));
}

// ---- through a composition ---------------------------------------------------------------------------

namespace {

std::string field(const fs::path& asset) {
    std::string text = R"({
      "format": "avgen-scene", "version": 1, "name": "field",
      "camera": { "mode": 1, "position": [0, 30, 120], "target": [0, 0, -60], "fov": 50.0, "orbitSpeed": 0.0 },
      "environment": { "ecologyLight": 1.4, "ecologyLightRange": 300.0 },
      "nodes": [
        { "name": "ground", "kind": "terrain", "world": { "name": "wide", "size": [480, 480], "features": [] },
          "terrain": { "chunkSize": 60.0, "resolution": 8, "lodLevels": 1, "viewDistance": 600.0 },
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
    void frame(double seconds = 1.0) {
        params.resetFinals();
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
    std::vector<scene::PunctualLight> lights() const {
        std::vector<scene::PunctualLight> out;
        for (const scene::PunctualLight& l : comp->scene().lights) {
            if (l.name.starts_with("ecology.glow.")) out.push_back(l);
        }
        return out;
    }
};

std::unique_ptr<Loaded> load(const std::string& text, assets::AssetRegistry& registry) {
    auto out = std::make_unique<Loaded>();
    auto comp = scene::Composition::fromJson(nlohmann::json::parse(text), registry);
    if (!comp) {
        FAIL(comp.error().message);
    }
    out->comp = std::move(*comp);
    out->comp->setViewport(960, 540);
    out->comp->attach(out->params, out->modulator);
    out->frame();
    return out;
}

bool isGrass(const scene::PunctualLight& l) { return l.color.y > l.color.z; }

float groundDistance(const scene::PunctualLight& l, glm::vec3 eye) {
    return glm::length(glm::vec2(l.position.x - eye.x, l.position.z - eye.z));
}

} // namespace

TEST_CASE("Through a composition: the fungi far out get the lights the faint grass near the lens used to take",
          "[ecology][ecologylight][adr945][composition]") {
    const fs::path asset = fs::path(AVGEN_SOURCE_DIR) / "assets/quaternius/glTF/Mushroom_Common.gltf";
    if (!fs::exists(asset)) {
        SKIP("the Quaternius library is not present in this checkout");
    }
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto scene = load(field(asset), registry);
    const glm::vec3 eye(0.0f, 30.0f, 120.0f);
    const scene::CompositionNode* ground = scene->comp->findNode("ground");
    REQUIRE(ground != nullptr);
    REQUIRE(ground->glow.size() > 600);

    const auto lights = scene->lights();
    REQUIRE_FALSE(lights.empty());
    std::size_t far = 0;
    for (const auto& l : lights) {
        CHECK_FALSE(isGrass(l)); // 0.05 glows below the default faintest (0.1)
        const float d = groundDistance(l, eye);
        far += d > 100.0f && d < 250.0f ? 1 : 0;
    }
    INFO(lights.size() << " lights, " << far << " of them 100-250 m out; " << ground->glow.size() << " clusters");
    CHECK(far > 50);

    // The control: the old rule on the same clusters -- the nearest within the range, whatever their
    // power, as many as this frame's lights -- lights nothing 100-250 m out.
    {
        std::vector<float> d;
        for (const world::GlowCluster& g : ground->glow) {
            const float dist = glm::length(g.position - eye);
            if (dist <= 300.0f) d.push_back(dist);
        }
        std::sort(d.begin(), d.end());
        REQUIRE(d.size() > lights.size());
        d.resize(lights.size());
        INFO("old rule: the " << d.size() << " nearest reach " << d.back() << " m");
        CHECK(d.back() < 100.0f);
    }

    SECTION("faintest: at zero the grass casts pools again") {
        scene->param("scene/glow-pools/faintest").setBase(0.0f);
        scene->frame();
        const auto all = scene->lights();
        CHECK(std::count_if(all.begin(), all.end(), isGrass) > 0);
        scene->param("scene/glow-pools/faintest").setBase(1000.0f);
        scene->frame();
        CHECK(scene->lights().empty()); // nothing glows that strongly
    }

    SECTION("reach: the light stands higher and reaches further") {
        scene->param("scene/glow-pools/reach").setBase(3.0f);
        scene->frame();
        const auto small = scene->lights();
        scene->param("scene/glow-pools/reach").setBase(20.0f);
        scene->frame();
        const auto large = scene->lights();
        REQUIRE_FALSE(small.empty());
        REQUIRE_FALSE(large.empty());
        const auto maxRange = [](const std::vector<scene::PunctualLight>& ls) {
            float r = 0.0f;
            for (const auto& l : ls) r = std::max(r, l.range);
            return r;
        };
        const auto minRange = [](const std::vector<scene::PunctualLight>& ls) {
            float r = 1e9f;
            for (const auto& l : ls) r = std::min(r, l.range);
            return r;
        };
        CHECK(minRange(large) == 40.0f);
        CHECK(maxRange(small) < 40.0f);
        CHECK(minRange(small) >= 6.0f);
    }

    SECTION("distance: no pool past it") {
        scene->param("scene/glow-pools/distance").setBase(80.0f);
        scene->frame();
        const auto near = scene->lights();
        REQUIRE_FALSE(near.empty());
        for (const auto& l : near) {
            CHECK(glm::length(l.position - eye) <= 80.0f);
        }
    }

    SECTION("the controls are where an artist looks: scene -> glow-pools, in viewer words") {
        for (const char* leaf : {"reach", "faintest", "distance"}) {
            const std::string path = std::string("scene/glow-pools/") + leaf;
            auto* p = scene->params.find(path);
            INFO(path);
            REQUIRE(p != nullptr);
            CHECK(p->flags().exposed);
            CHECK(p->group() == "scene");
            CHECK(ui::parameterSubGroup(p->path(), p->group()) == "glow-pools");
            CHECK(p->label().find("glow") != std::string::npos);
        }
    }

    SECTION("saved and read back") {
        scene->param("scene/glow-pools/reach").setBase(11.0f);
        scene->param("scene/glow-pools/faintest").setBase(0.3f);
        scene->param("scene/glow-pools/distance").setBase(260.0f);
        const nlohmann::json saved = scene->comp->toJson();
        CHECK(saved.at("environment").at("ecologyPoolReach") == 11.0f);
        CHECK(saved.at("environment").at("ecologyPoolFaintest") == 0.3f);
        CHECK(saved.at("environment").at("ecologyLightRange") == 260.0f);
        auto again = load(saved.dump(), registry);
        CHECK(again->param("scene/glow-pools/reach").base() == 11.0f);
        CHECK(again->param("scene/glow-pools/faintest").base() == 0.3f);
        CHECK(again->param("scene/glow-pools/distance").base() == 260.0f);
    }
}

TEST_CASE("Ecology lights: a seek lands on the lights play reached", "[ecology][ecologylight][adr945][determinism]") {
    const fs::path asset = fs::path(AVGEN_SOURCE_DIR) / "assets/quaternius/glTF/Mushroom_Common.gltf";
    if (!fs::exists(asset)) {
        SKIP("the Quaternius library is not present in this checkout");
    }
    assets::AssetRegistry registry{testsupport::processTempDir()};
    // A camera flying across the field, so the choice changes from frame to frame.
    const auto at = [](double t) {
        return glm::vec3(static_cast<float>(-120.0 + 60.0 * t), 25.0f, static_cast<float>(200.0 - 40.0 * t));
    };
    const auto place = [&](Loaded& s, double t) {
        auto* position = s.params.findAs<glm::vec3>("camera/position");
        REQUIRE(position != nullptr);
        position->setBase(at(t));
        s.frame(t);
    };
    auto played = load(field(asset), registry);
    std::size_t changes = 0;
    std::vector<scene::PunctualLight> previous;
    for (int f = 0; f <= 150; ++f) {
        place(*played, static_cast<double>(f) / 60.0);
        const auto now = played->lights();
        const bool same = now.size() == previous.size() &&
                          std::equal(now.begin(), now.end(), previous.begin(), [](const auto& x, const auto& y) {
                              return x.position == y.position && x.intensity == y.intensity;
                          });
        changes += !previous.empty() && !same ? 1 : 0;
        previous = now;
    }
    CHECK(changes > 10); // the choice really did move with the camera
    auto sought = load(field(asset), registry);
    place(*sought, 150.0 / 60.0);
    const auto a = played->lights();
    const auto b = sought->lights();
    REQUIRE(a.size() == b.size());
    REQUIRE_FALSE(a.empty());
    for (std::size_t i = 0; i < a.size(); ++i) {
        INFO(i);
        CHECK(a[i].name == b[i].name);
        CHECK(a[i].position == b[i].position);
        CHECK(a[i].intensity == b[i].intensity);
        CHECK(a[i].range == b[i].range);
        CHECK(a[i].color == b[i].color);
    }
}
