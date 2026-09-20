// The Lights panel's half of ADR-375's measurement, and the panel-logic decisions beside it.
//
// The important case here is the first one, and its shape is ADR-375's: **a hand-written panel asks
// for a parameter by string.** A wrong path -- a typo, or a rename somewhere else -- does not fail
// to compile and does not throw. The row simply does not draw, and a section degrades into an empty
// box indistinguishable from "this light has no such control". So every leaf the panel asks for is
// asserted to resolve against a light of the kind that should have it, each loop carrying a
// `nonesuch` control so it cannot pass against a parameter set that answers yes to everything.
//
// This is the half ADR-350 never covered: it asserted parameters were *registered*, never that
// anything pointed at them.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"
#include "ui/lights_panel_logic.hpp"
#include "ui/world_probe.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace avgen;
using nlohmann::json;
using Catch::Matchers::WithinAbs;

namespace {

struct Built {
    std::unique_ptr<scene::Composition> comp;
    params::ParameterSet parameters;
    params::Modulator modulator;
    signals::SignalBus bus;
};

// A scene with one light of `type`, attached and stepped, which is the state the panel draws.
std::unique_ptr<Built> sceneWithLight(const fs::path& dir, const char* type) {
    fs::create_directories(dir);
    const json doc = json{{"format", "avgen-scene"},
                          {"version", 1},
                          {"name", "panel"},
                          {"nodes", json::array()},
                          {"lights", json::array({json{{"name", "Key Light"}, {"type", type}}})}};
    const fs::path file = dir / "panel.scene.json";
    {
        std::ofstream out(file);
        out << doc.dump(1);
    }
    auto built = std::make_unique<Built>();
    assets::AssetRegistry registry(dir);
    auto loaded = scene::Composition::loadFile(file, registry);
    REQUIRE(loaded.has_value());
    built->comp = std::move(*loaded);
    built->comp->attach(built->parameters, built->modulator);
    FrameTime time;
    time.renderTime = 0.0;
    time.deltaTime = 1.0 / 60.0;
    built->parameters.resetFinals();
    built->comp->update(time);
    return built;
}

bool registered(const params::ParameterSet& set, const std::string& path) {
    return set.find(path) != nullptr;
}

} // namespace

TEST_CASE("every control the Lights panel draws points at a real parameter", "[ui][lights][panel]") {
    const fs::path dir = fs::temp_directory_path() / "avgen-lights-panel";
    fs::remove_all(dir);

    struct Arm {
        const char* json;
        scene::PunctualLight::Type type;
    };
    const Arm arms[] = {{"point", scene::PunctualLight::Type::Point},
                        {"spot", scene::PunctualLight::Type::Spot},
                        {"directional", scene::PunctualLight::Type::Directional},
                        {"rect", scene::PunctualLight::Type::Rect}};

    for (const Arm& arm : arms) {
        INFO(arm.json);
        auto built = sceneWithLight(dir / arm.json, arm.json);
        REQUIRE(built->comp->authoredLights().size() == 1);
        // **The panel's own arithmetic, not a string this test happens to agree with.**
        //
        // It was `"lights/Key_Light/"` written out here, which asserts that the parameters exist
        // and says nothing about whether the panel can reach them: change how an id is derived and
        // the panel would ask under a prefix that resolves to nothing, every row would silently
        // fail to draw, and this test would still pass. That is the Tree panel's defect, where a
        // wrongly computed prefix rendered two sections as an empty box indistinguishable from a
        // scene without the feature.
        const std::string base = ui::lightParameterBase(built->comp->authoredLights()[0]);

        // Every leaf the panel asks for, for this kind.
        for (const std::string_view leaf : ui::lightParameterLeaves(arm.type)) {
            INFO(leaf);
            CHECK(registered(built->parameters, base + std::string(leaf)));
        }

        // THE CONTROL. Without it the loop above passes against any `find` that returns non-null,
        // and against a light that registered every leaf for every kind.
        CHECK_FALSE(registered(built->parameters, base + "nonesuch"));

        // And the per-kind exclusions, which are what makes the list a description of this light
        // rather than of all lights. A control that cannot move the picture should not be drawn.
        if (arm.type == scene::PunctualLight::Type::Directional) {
            CHECK_FALSE(registered(built->parameters, base + "range"));
        }
        if (arm.type != scene::PunctualLight::Type::Spot) {
            CHECK_FALSE(registered(built->parameters, base + "outerCone"));
        }
        if (arm.type != scene::PunctualLight::Type::Rect) {
            CHECK_FALSE(registered(built->parameters, base + "width"));
        }
    }
}

TEST_CASE("the panel asks under the id, so a rename does not silence every row",
          "[ui][lights][panel]") {
    // The other half of the arithmetic check. The panel's prefix must follow the *id*; if it
    // followed the display name, renaming a light would re-path every row it draws and the whole
    // property section would go blank while the parameters sat there untouched.
    const fs::path dir = fs::temp_directory_path() / "avgen-lights-panel-rename";
    fs::remove_all(dir);
    auto built = sceneWithLight(dir, "spot");

    const std::string before = ui::lightParameterBase(built->comp->authoredLights()[0]);
    REQUIRE(registered(built->parameters, before + "intensity"));

    std::vector<scene::Composition::AuthoredLight> lights = built->comp->authoredLights();
    lights[0].light.name = "Moon Key";
    REQUIRE(built->comp->setAuthoredLights(std::move(lights)).has_value());

    const std::string after = ui::lightParameterBase(built->comp->authoredLights()[0]);
    CHECK(after == before);                                  // the prefix did not move...
    CHECK(registered(built->parameters, after + "intensity")); // ...and it still resolves
    // THE CONTROL: the display name is not a prefix. If the panel ever went back to computing one
    // from the name, this is the path it would ask under and it does not exist.
    CHECK_FALSE(registered(built->parameters, "lights/Moon_Key/intensity"));
}

TEST_CASE("a new light lands where the camera is looking", "[ui][lights][panel]") {
    // The brief's §4: not the world origin. A light created at zero in a valley the camera is
    // watching from 200 m away is a light the user has to go and find.
    const glm::vec3 eye(0.0f, 10.0f, 100.0f);
    const glm::vec3 target(0.0f, 10.0f, 0.0f);

    const ui::LightPlacement spot = ui::placeNewLight(eye, target, scene::PunctualLight::Type::Spot);
    // In front of the camera, along its view, and nearer the subject than the camera is.
    CHECK(spot.position.z < eye.z);
    CHECK(spot.position.z > target.z);
    // Aimed the way the camera is looking, so "add a spot" lights what you were looking at.
    CHECK_THAT(spot.direction.z, WithinAbs(-1.0, 1e-4));

    // THE CONTROL: a camera with no forward must not produce a NaN direction, because
    // `setAuthoredLights` refuses one and the button would fail rather than merely place a light
    // somewhere arbitrary.
    const ui::LightPlacement degenerate =
        ui::placeNewLight(eye, eye, scene::PunctualLight::Type::Point);
    CHECK(std::isfinite(degenerate.direction.x));
    CHECK(std::isfinite(degenerate.position.y));

    // The stand-off scales with how far the camera is looking, or a light lands 40 m away in a 3 m
    // still life.
    const ui::LightPlacement near =
        ui::placeNewLight(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f), scene::PunctualLight::Type::Point);
    CHECK(glm::length(near.position - glm::vec3(0.0f, 0.0f, 3.0f)) < 3.0f);
}

TEST_CASE("a new light is bright enough to see", "[ui][lights][panel]") {
    // ADR-375's rule, which it learned from a wind body that shipped at strength 0: a button that
    // does nothing when pressed is worse than no button. A spot created at intensity 1 in a lit
    // scene is indistinguishable from one that failed to be created.
    const ui::LightPlacement p{glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)};
    for (const scene::PunctualLight::Type type : ui::creatableLightTypes()) {
        const auto light = ui::makeLight(type, p, "L", "l");
        INFO(scene::lightTypeName(type));
        CHECK(light.light.intensity > 1.0f);
        CHECK(light.light.enabled);
    }
    // A spot's candela and a directional's lux are different units, and the defaults respect that
    // rather than using one number for both.
    const auto spot = ui::makeLight(scene::PunctualLight::Type::Spot, p, "S", "s");
    const auto sun = ui::makeLight(scene::PunctualLight::Type::Directional, p, "D", "d");
    CHECK(spot.light.intensity > sun.light.intensity);
    // A hero spot and a sun cast; a fill does not have to.
    CHECK(spot.light.castsShadow);
    CHECK(sun.light.castsShadow);
    // A spot needs a cone, or it is a point light that pretends.
    CHECK(spot.light.outerConeAngle > spot.light.innerConeAngle);
}

TEST_CASE("names and ids stay unique as lights are added and copied", "[ui][lights][panel]") {
    std::vector<std::string> taken{"Key Light"};
    CHECK(ui::uniqueLightName("Key Light", taken) == "Key Light 2");
    CHECK(ui::uniqueLightName("Fill", taken) == "Fill");

    // The brief's own example: the first duplicate is "Copy", not "Copy 2".
    CHECK(ui::duplicateLightName("Key Light", taken) == "Key Light Copy");
    taken.emplace_back("Key Light Copy");
    CHECK(ui::duplicateLightName("Key Light", taken) == "Key Light Copy 2");

    // An empty name still yields something addressable rather than an empty row.
    CHECK_FALSE(ui::uniqueLightName("", {}).empty());
}

TEST_CASE("aiming a light at a point is undefined only when they coincide", "[ui][lights][panel]") {
    glm::vec3 out(0.0f);
    REQUIRE(ui::aimDirection(glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f), out));
    CHECK_THAT(out.y, WithinAbs(-1.0, 1e-4));
    // THE CONTROL: a light asked to aim at itself reports failure rather than returning a NaN the
    // caller would write into the scene.
    CHECK_FALSE(ui::aimDirection(glm::vec3(1.0f), glm::vec3(1.0f), out));
}

TEST_CASE("the panel offers the four kinds the brief names", "[ui][lights][panel]") {
    const auto types = ui::creatableLightTypes();
    REQUIRE(types.size() == 4);
    CHECK(types[0] == scene::PunctualLight::Type::Point);
    CHECK(types[1] == scene::PunctualLight::Type::Spot);
    CHECK(types[2] == scene::PunctualLight::Type::Directional);
    // "Area" in the menu is a Rect: Disk, Tube and Sphere are real kinds the renderer shades, but
    // they are a shape chosen after the light exists rather than four create-menu entries.
    CHECK(types[3] == scene::PunctualLight::Type::Rect);
}

// ---- picking a light in the viewport -------------------------------------------------------------

TEST_CASE("a light is picked in screen space, nearest to the cursor", "[ui][lights][pick]") {
    scene::Camera camera;
    camera.position = glm::vec3(0.0f, 0.0f, 10.0f);
    camera.target = glm::vec3(0.0f);
    const float aspect = 16.0f / 9.0f;

    const std::vector<glm::vec3> lights{glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(4.0f, 0.0f, 0.0f)};

    // Dead centre picks the one at the origin.
    CHECK(ui::pickProjectedPoint(lights, camera, aspect, glm::vec2(0.0f, 0.0f),
                                 ui::kHelperPickRadius) == 0);

    // THE CONTROL: a click well away from both picks nothing, so the function is not simply
    // returning the first entry.
    CHECK(ui::pickProjectedPoint(lights, camera, aspect, glm::vec2(-0.9f, -0.9f),
                                 ui::kHelperPickRadius) == -1);

    // A light behind the eye is never picked. It projects to a mirrored position that looks
    // entirely plausible, which is how a click on empty sky selects something standing behind you.
    const std::vector<glm::vec3> behind{glm::vec3(0.0f, 0.0f, 30.0f)};
    CHECK(ui::pickProjectedPoint(behind, camera, aspect, glm::vec2(0.0f), 2.0f) == -1);

    // Two lights on one sight line: the near one wins.
    const std::vector<glm::vec3> stacked{glm::vec3(0.0f, 0.0f, -20.0f), glm::vec3(0.0f, 0.0f, 0.0f)};
    CHECK(ui::pickProjectedPoint(stacked, camera, aspect, glm::vec2(0.0f), ui::kHelperPickRadius) == 1);

    // An empty scene picks nothing rather than indexing an empty list.
    CHECK(ui::pickProjectedPoint({}, camera, aspect, glm::vec2(0.0f), ui::kHelperPickRadius) == -1);
}
