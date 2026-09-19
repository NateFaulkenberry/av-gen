// The cosmic key: a scene's own shadow range, and an authored light's live angles (ADR-354).
//
// ---- the defect these are here to keep fixed ---------------------------------------------------
//
// `examples/treeisland/tree-of-life-floating-island` ships a warm directional key with
// `castsShadow: true`, and rendered no shadow at all -- not on the island, not under the canopy,
// not on the underside. `--aov shadow` on it returned R = G = B = 1.0 with min equal to max over
// every one of 1920x1080 pixels: the constant the renderer itself refuses to write for a scene
// whose directional light does not cast.
//
// The cause is not the light. ADR-112 picks the shadowed depth range as the one keeping the
// coarsest cascade texel under `shadowTexelTarget`, and since both that target and
// `kShadowRangeReference` are constants, the answer is the same number for every scene in the
// repository -- about 77 m. The deliverable's camera stands 219 m off its subject. Everything in
// the frame sat past the last cascade, where `shadowLookup` reports the lookup invalid and the
// fragment reads as lit.
//
// `SHADOW_RANGE_IS_A_CONSTANT` below is that measurement, kept as an assertion, because it is the
// thing that will silently come back: the automatic range does not depend on the scene, so no
// amount of authoring in the scene file can move it.

#include "assets/asset_registry.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "rendering/render_quality.hpp"
#include "rendering/shadow_math.hpp"
#include "scene/composition.hpp"
#include "scene/scene_types.hpp"
#include "signals/signal_bus.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace fs = std::filesystem;
using namespace avgen;
using Catch::Approx;
using nlohmann::json;

namespace {

fs::path scratch() {
    const fs::path dir = fs::temp_directory_path() / "avgen-cosmic-key";
    fs::create_directories(dir);
    return dir;
}

fs::path writeScene(const std::string& name, json body) {
    body["format"] = "avgen-scene";
    body["version"] = 1;
    body["name"] = name;
    const fs::path out = scratch() / (name + ".scene.json");
    std::ofstream os(out);
    os << body.dump(1);
    return out;
}

struct Built {
    std::unique_ptr<scene::Composition> comp;
    params::ParameterSet parameters;
    params::Modulator modulator;
    signals::SignalBus bus;

    void step(double seconds) {
        FrameTime time;
        time.renderTime = seconds;
        time.deltaTime = 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(seconds * 60.0);
        parameters.resetFinals();
        comp->updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, parameters, time.deltaTime);
        comp->updateBehaviour(time, bus);
        comp->update(time);
    }
    const scene::PunctualLight* light(std::string_view name) const {
        for (const scene::PunctualLight& l : comp->scene().lights) {
            if (l.name == name) {
                return &l;
            }
        }
        return nullptr;
    }
};

void setVec3(params::ParameterSet& params, const char* path, float x, float y, float z) {
    params::IParameter* p = params.find(path);
    REQUIRE(p != nullptr);
    p->setBaseComponent(0, x);
    p->setBaseComponent(1, y);
    p->setBaseComponent(2, z);
}

std::unique_ptr<Built> build(const fs::path& file) {
    auto built = std::make_unique<Built>();
    assets::AssetRegistry registry(file.parent_path());
    auto loaded = scene::Composition::loadFile(file, registry);
    REQUIRE(loaded.has_value());
    built->comp = std::move(*loaded);
    built->comp->attach(built->parameters, built->modulator);
    built->comp->setViewport(1280, 800);
    built->step(0.0);
    return built;
}

// One directional key, world space, aimed the way the deliverable's used to be.
json oneKeyScene() {
    return json{{"lights", json::array({json{{"name", "celestial-key"},
                                             {"type", "directional"},
                                             {"role", "key"},
                                             {"direction", json::array({0.387, -0.806, -0.472})},
                                             {"color", json::array({1.0, 0.92, 0.78})},
                                             {"intensity", 2.45},
                                             {"castsShadow", true},
                                             {"shadowStrength", 0.85},
                                             {"softness", 3.5}}})},
                {"nodes", json::array({json{{"name", "world-root"}, {"kind", "group"}}})}};
}

} // namespace

// ------------------------------------------------------------------------------------------------
// Why the shipping scene cast nothing.
// ------------------------------------------------------------------------------------------------

TEST_CASE("ADR-112's automatic shadow range is a constant, whatever the scene", "[shadow][cosmickey]") {
    // The deliverable's own numbers: near plane 0.5 (the clamp in `Composition::rebuild`), far
    // plane 135 km (radius * 50, and the radius is set by a star shell 1580 units out).
    const float near = 0.5f;
    const float far = 135000.0f;
    const float texelTarget = rendering::QualitySettings{}.shadowTexelTarget;

    // Three scene radii apart, from an object on a table to a star field -- the automatic range
    // does not move, because `resolvable` is `texelTarget * kShadowRangeReference / 2.12` and both
    // of those are constants. This is the measurement, not a re-derivation of it.
    const float tiny = rendering::directionalShadowRange(near, far, 5.0f, rendering::kShadowRangeReference, texelTarget);
    const float world = rendering::directionalShadowRange(near, far, 300.0f, rendering::kShadowRangeReference, texelTarget);
    const float cosmos = rendering::directionalShadowRange(near, far, 2737.0f, rendering::kShadowRangeReference, texelTarget);
    CHECK(world == Approx(cosmos));
    CHECK(world == Approx(77.28f).margin(0.5f));
    // A world small enough that three radii is the shorter rule still gets three radii -- the
    // control, so "it is always 77" is not what is being asserted.
    CHECK(tiny < world);
    CHECK(tiny == Approx(15.0f).margin(0.5f));

    // And the number that made it a defect: the deliverable's camera stands here.
    constexpr float kCameraStandoff = 219.3f;  // |(158, 6, 152) - (0, 13, 0)|
    constexpr float kSubjectRadius = 90.0f;    // the island is about 170 units across
    CHECK(world < kCameraStandoff - kSubjectRadius);

    // Switching the rule off is the pre-ADR-112 behaviour and reaches the subject. It is not the
    // fix -- it makes every far scene's texels coarse -- but it is the control that says the range
    // and not the light is what the subject was missing.
    const float unlimited = rendering::directionalShadowRange(near, far, 2737.0f, rendering::kShadowRangeReference, 0.0f);
    CHECK(unlimited > kCameraStandoff + kSubjectRadius);
}

TEST_CASE("A scene may say how far its shadows reach, and one that does not is unchanged",
          "[shadow][scene][cosmickey]") {
    json body = oneKeyScene();
    body["environment"] = json{{"shadowRange", 420.0}};
    auto with = build(writeScene("cosmickey-range", body));
    CHECK(with->comp->scene().environment.shadowRange == Approx(420.0f));

    // It survives the save, or it is a setting the application does not keep (ADR-225).
    const json saved = with->comp->toJson();
    REQUIRE(saved.at("environment").contains("shadowRange"));
    CHECK(saved.at("environment").at("shadowRange").get<float>() == Approx(420.0f));
    auto again = build(writeScene("cosmickey-range-again", saved));
    CHECK(again->comp->scene().environment.shadowRange == Approx(420.0f));

    // The parameter, and that turning it moves the scene the renderer is handed.
    auto* p = with->parameters.find("scene/shadowRange");
    REQUIRE(p != nullptr);
    with->parameters.find("scene/shadowRange")->setBaseComponent(0, 900.0f);
    with->step(0.1);
    CHECK(with->comp->scene().environment.shadowRange == Approx(900.0f));

    // The control, and it is the whole of "opt-in": a scene that never mentions it gets zero,
    // which is the automatic rule, which is what every other scene in the repository gets.
    auto without = build(writeScene("cosmickey-range-none", oneKeyScene()));
    CHECK(without->comp->scene().environment.shadowRange == Approx(0.0f));
}

// ------------------------------------------------------------------------------------------------
// The light's own angles.
// ------------------------------------------------------------------------------------------------

TEST_CASE("A light's azimuth and elevation say where it comes from", "[scene][lights][cosmickey]") {
    // The sign is the whole content of this case. `direction` is where the light GOES; a light
    // falling from overhead comes from elevation +90, and an implementation that forgot the
    // negation reports -90 -- which round-trips perfectly and is upside down.
    float azimuth = 0.0f;
    float elevation = 0.0f;
    scene::Composition::lightAngles(glm::vec3(0.0f, -1.0f, 0.0f), azimuth, elevation);
    CHECK(elevation == Approx(90.0f).margin(0.01f));
    scene::Composition::lightAngles(glm::vec3(0.0f, 1.0f, 0.0f), azimuth, elevation);
    CHECK(elevation == Approx(-90.0f).margin(0.01f));

    // Azimuth 0 is +Z on the source side, +90 is +X: a light at azimuth 90 travels towards -X.
    const glm::vec3 fromEast = scene::Composition::lightDirectionFromAngles(90.0f, 0.0f);
    CHECK(fromEast.x == Approx(-1.0f).margin(1e-4));
    CHECK(fromEast.z == Approx(0.0f).margin(1e-4));

    for (float a = -170.0f; a <= 170.0f; a += 17.0f) {
        for (float e = -85.0f; e <= 85.0f; e += 13.0f) {
            const glm::vec3 d = scene::Composition::lightDirectionFromAngles(a, e);
            CHECK(glm::length(d) == Approx(1.0f).margin(1e-4));
            float ra = 0.0f;
            float re = 0.0f;
            scene::Composition::lightAngles(d, ra, re);
            INFO("azimuth " << a << " elevation " << e);
            CHECK(ra == Approx(a).margin(0.01));
            CHECK(re == Approx(e).margin(0.01));
        }
    }
}

TEST_CASE("An authored light's parameters reach the frame and survive the save",
          "[scene][lights][cosmickey]") {
    auto built = build(writeScene("cosmickey-params", oneKeyScene()));

    // Registered at the light's own authored values, so attaching changes no picture. That is what
    // makes registering these for every scene safe, and it is the first thing that would break.
    float azimuth = 0.0f;
    float elevation = 0.0f;
    scene::Composition::lightAngles(glm::normalize(glm::vec3(0.387f, -0.806f, -0.472f)), azimuth, elevation);
    auto* pa = built->parameters.find("lights/celestial-key/azimuth");
    auto* pe = built->parameters.find("lights/celestial-key/elevation");
    auto* pi = built->parameters.find("lights/celestial-key/intensity");
    auto* ps = built->parameters.find("lights/celestial-key/angularSize");
    REQUIRE(pa != nullptr);
    REQUIRE(pe != nullptr);
    REQUIRE(pi != nullptr);
    REQUIRE(ps != nullptr);
    CHECK(pa->baseComponent(0) == Approx(azimuth).margin(0.01));
    CHECK(pe->baseComponent(0) == Approx(elevation).margin(0.01));
    CHECK(pi->baseComponent(0) == Approx(2.45f));
    CHECK(ps->baseComponent(0) == Approx(3.5f));

    const scene::PunctualLight* before = built->light("celestial-key");
    REQUIRE(before != nullptr);
    const glm::vec3 wasDirection = before->direction;

    built->parameters.find("lights/celestial-key/azimuth")->setBaseComponent(0, -50.0f);
    built->parameters.find("lights/celestial-key/elevation")->setBaseComponent(0, 31.0f);
    built->parameters.find("lights/celestial-key/intensity")->setBaseComponent(0, 5.0f);
    built->parameters.find("lights/celestial-key/angularSize")->setBaseComponent(0, 1.8f);
    built->step(0.1);

    const scene::PunctualLight* after = built->light("celestial-key");
    REQUIRE(after != nullptr);
    CHECK(after->intensity == Approx(5.0f));
    CHECK(after->softness == Approx(1.8f));
    const glm::vec3 want = scene::Composition::lightDirectionFromAngles(-50.0f, 31.0f);
    CHECK(after->direction.x == Approx(want.x).margin(1e-4));
    CHECK(after->direction.y == Approx(want.y).margin(1e-4));
    CHECK(after->direction.z == Approx(want.z).margin(1e-4));
    // The control: it moved. A parameter that happened to reproduce the authored direction would
    // pass every assertion above and prove nothing.
    CHECK(glm::length(after->direction - wasDirection) > 0.1f);

    // ADR-225: what the application does not keep is not a setting.
    const json saved = built->comp->toJson();
    REQUIRE(saved.at("lights").size() == 1);
    const json& light = saved.at("lights").at(0);
    CHECK(light.at("intensity").get<float>() == Approx(5.0f));
    CHECK(light.at("softness").get<float>() == Approx(1.8f));
    auto reloaded = build(writeScene("cosmickey-params-again", saved));
    const scene::PunctualLight* back = reloaded->light("celestial-key");
    REQUIRE(back != nullptr);
    CHECK(back->intensity == Approx(5.0f));
    CHECK(back->direction.x == Approx(want.x).margin(1e-3));
    CHECK(back->direction.y == Approx(want.y).margin(1e-3));
    CHECK(back->direction.z == Approx(want.z).margin(1e-3));
    CHECK(reloaded->parameters.find("lights/celestial-key/azimuth")->baseComponent(0)
          == Approx(-50.0f).margin(0.01));
}

TEST_CASE("The celestial key does not ride the island", "[scene][lights][cosmickey]") {
    // Brief section 16. The island bobs and turns under an LFO; the key is a world source and the
    // point of the shot is that the island drifts THROUGH a fixed cosmic environment. A key
    // parented to the island keeps its highlight nailed to the same leaf for ever, which is the
    // instinct the brief forbids.
    json body = oneKeyScene();
    body["nodes"] = json::array({json{{"name", "world-root"}, {"kind", "group"}},
                                 json{{"name", "floating-island"},
                                      {"kind", "group"},
                                      {"parent", "world-root"},
                                      {"rotation", json::array({0.0, 0.0, 0.0})}}});
    auto built = build(writeScene("cosmickey-fixed", body));
    const glm::vec3 at0 = built->light("celestial-key")->direction;

    // Turn the island a long way and step on.
    setVec3(built->parameters, "nodes/floating-island/rotation", 4.0f, 137.0f, 3.0f);
    built->step(4.0);
    const glm::vec3 at4 = built->light("celestial-key")->direction;
    CHECK(glm::length(at4 - at0) == Approx(0.0f).margin(1e-5));

    // The control, in the same file: a light that names the node DOES ride it, so the assertion
    // above is about this light's independence and not about lights being immovable.
    json rider = body;
    rider["lights"].at(0)["name"] = "lantern";
    rider["lights"].at(0)["node"] = "floating-island";
    auto ridden = build(writeScene("cosmickey-ridden", rider));
    const glm::vec3 rode0 = ridden->light("lantern")->direction;
    setVec3(ridden->parameters, "nodes/floating-island/rotation", 4.0f, 137.0f, 3.0f);
    ridden->step(4.0);
    const glm::vec3 rode4 = ridden->light("lantern")->direction;
    CHECK(glm::length(rode4 - rode0) > 0.5f);
}
