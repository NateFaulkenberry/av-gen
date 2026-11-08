// ADR-350: the day/night cycle was a system the application ran and did not keep.
//
// Every field of `DayNightSettings` -- the cycle length, the phase, the sun's arc -- was
// reachable only by hand-editing the scene JSON. No parameter, so no UI, no
// modulation, no keyframing, no scrub. And the scene writer never emitted the block at all, so a
// scene that carried a cycle and was saved lost it outright.
//
// ADR-225 states the rule this broke: a setting the application does not keep is not a setting.
// These are the two assertions that would have caught it, and neither needs a GPU or a frame —
// which is the uncomfortable part: nothing was stopping them from existing.

#include "assets/asset_registry.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/day_night.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Catch::Approx;
using json = nlohmann::json;
using namespace avgen;

// `ParameterSet` has no `contains`; a registered path is one `find` resolves.
static bool registered(const params::ParameterSet& set, std::string_view path) {
    return set.find(path) != nullptr;
}

namespace {

std::filesystem::path scratch() {
    auto dir = std::filesystem::temp_directory_path() / "avgen_daynight_params";
    std::filesystem::create_directories(dir);
    return dir;
}

// A scene with the cycle on and one deliberately un-default colour stop. The stop is not a
// parameter, but it still has to survive a save -- that is the writer half of this defect.
std::filesystem::path writeScene(const std::string& name, const glm::vec3& noonZenith) {
    json doc;
    doc["format"] = "avgen-scene";
    doc["version"] = 1;
    doc["name"] = "daynight";
    doc["environment"] = {
        {"dayNight",
         {{"enabled", true},
          {"cycleSeconds", 123.5},
          {"dayPhase", 0.375},
          {"paused", true},
          {"sunLight", "key"},
          {"zenithColor",
           json::array({json{{"phase", 0.0}, {"value", json::array({0.01, 0.02, 0.05})}},
                        json{{"phase", 0.5},
                             {"value", json::array({noonZenith.x, noonZenith.y, noonZenith.z})}}})}}}};
    doc["nodes"] = json::array();
    const auto path = scratch() / name;
    std::ofstream(path) << doc.dump(1);
    return path;
}

} // namespace

TEST_CASE("Every day/night setting is a real parameter", "[daynight][parameters][environment]") {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "composition");
    comp.attach(params, modulator);

    // The scalars an author reaches for. `dayPhase` first: it is the scrub.
    for (const char* path : {"env/dayNight/enabled", "env/dayNight/paused", "env/dayNight/dayPhase",
                             "env/dayNight/cycleSeconds", "env/dayNight/phaseOffset",
                             "env/dayNight/sun/peakElevationDeg", "env/dayNight/sun/azimuthAtDawnDeg",
                             "env/dayNight/sun/azimuthSweepDeg", "env/dayNight/sun/intensityScale",
                             "env/dayNight/moon/intensityScale", "env/dayNight/stars/brightnessScale",
                             "env/dayNight/hdri/intensityScale", "env/dayNight/glow/influence",
                             "env/dayNight/fog/horizonBlend"}) {
        INFO(path);
        CHECK(registered(params, path));
    }

    // Deliberately NOT parameters: the colour curves. The owner asked for them to stay
    // scene-authored, and a wall of per-stop colour swatches is a control surface nobody reaches
    // for while looking at the scene. Asserted so the decision is recorded rather than assumed,
    // and so re-adding them has to be a deliberate act.
    CHECK_FALSE(registered(params, "env/dayNight/sky/zenith/noon"));
    CHECK_FALSE(registered(params, "env/dayNight/sun/color/sunset"));

    // The rest of the sweep. These were all read from the scene file and reachable from nowhere
    // else, which is the same defect in four more places.
    for (const char* path : {"env/skybox", "env/proceduralSkyBackground", "env/lightFromEnvironment",
                             "env/skyBloom"}) {
        INFO(path);
        CHECK(registered(params, path));
    }

    // THE CONTROL. `find` on a path nobody registered must be null, or every loop above proves
    // nothing -- a ParameterSet that answered yes to everything would pass all of it.
    CHECK_FALSE(registered(params, "env/dayNight/nonesuch"));
    CHECK_FALSE(registered(params, "env/nonesuch"));
}

// ADR-099 chose six water properties as "the ones worth moving". The water-world spec asks for a
// different nine -- how clear the water is, what colour it goes with depth, how much sky it
// reflects -- and every one of them was unreachable: 6 of 23 `WaterSettings` fields had a
// parameter. This is the audit result as an assertion.
TEST_CASE("The water properties the spec names are reachable", "[water][parameters][environment]") {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "composition");
    comp.attach(params, modulator);

    scene::CompositionNode node;
    node.name = "sea";
    node.kind = scene::NodeKind::Terrain;
    node.terrain.water.enabled = true;
    REQUIRE(comp.addNode(std::move(node)).has_value());

    for (const char* leaf : {"clarity", "maxOpacity", "fresnel", "reflection", "roughness",
                             "refraction", "rippleScale", "shallowDepth", "shallowColor",
                             "deepColor"}) {
        const std::string path = std::string("nodes/sea/water/") + leaf;
        INFO(path);
        CHECK(registered(params, path));
    }
    // ADR-099's original six must still be there: this extended the set, it did not replace it.
    for (const char* leaf : {"glow", "sparkle", "ripple", "flowSpeed", "swell", "foam"}) {
        const std::string path = std::string("nodes/sea/water/") + leaf;
        INFO(path);
        CHECK(registered(params, path));
    }
    CHECK_FALSE(registered(params, "nodes/sea/water/nonesuch"));
}

TEST_CASE("A saved scene keeps its day/night cycle", "[daynight][parameters][environment]") {
    const glm::vec3 noon{0.42f, 0.17f, 0.66f}; // nothing like the default noon zenith
    const auto source = writeScene("in.scene.json", noon);

    assets::AssetRegistry registry;
    registry.setBaseDirectory(scratch());
    params::ParameterSet params;
    params::Modulator modulator;

    auto loaded = scene::Composition::loadFile(source, registry);
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;
    comp.attach(params, modulator);

    const auto out = scratch() / "out.scene.json";
    REQUIRE(comp.saveFile(out).has_value());

    json round;
    std::ifstream(out) >> round;
    REQUIRE(round.contains("environment"));
    const json& env = round.at("environment");

    // The block existed before this and was never written: a scene that carried a cycle and was
    // saved lost it. This is the assertion that fails on that.
    REQUIRE(env.contains("dayNight"));
    const json& d = env.at("dayNight");
    CHECK(d.value("enabled", false));
    CHECK(d.value("cycleSeconds", 0.0f) == Approx(123.5f));
    CHECK(d.value("dayPhase", 0.0f) == Approx(0.375f));
    CHECK(d.value("paused", false));
    CHECK(d.value("sunLight", std::string{}) == "key");

    // And the curve, which is the part a colour parameter edits. Written as keyframes and read
    // back as keyframes, or an author's edited stop is lost on the next save.
    REQUIRE(d.contains("zenithColor"));
    REQUIRE(d.at("zenithColor").is_array());
    bool foundNoon = false;
    for (const json& k : d.at("zenithColor")) {
        if (k.value("phase", -1.0f) == Approx(0.5f)) {
            foundNoon = true;
            const json& v = k.at("value");
            CHECK(v[0].get<float>() == Approx(noon.x));
            CHECK(v[1].get<float>() == Approx(noon.y));
            CHECK(v[2].get<float>() == Approx(noon.z));
        }
    }
    INFO("the noon zenith stop written by the scene must come back unchanged");
    CHECK(foundNoon);

    // THE CONTROL on the round trip: reloading the SAVED file must give the same cycle, not the
    // defaults. Writing a block that the reader then ignores is the same defect one layer along,
    // and it is exactly what `applyDefaults` would do to an unparsed curve.
    auto again = scene::Composition::loadFile(out, registry);
    REQUIRE(again.has_value());
    json second;
    REQUIRE((*again)->saveFile(scratch() / "out2.scene.json").has_value());
    std::ifstream(scratch() / "out2.scene.json") >> second;
    const json& d2 = second.at("environment").at("dayNight");
    CHECK(d2.value("cycleSeconds", 0.0f) == Approx(123.5f));
    bool noonSurvivedTwice = false;
    for (const json& k : d2.at("zenithColor")) {
        if (k.value("phase", -1.0f) == Approx(0.5f) && k.at("value")[0].get<float>() == Approx(noon.x)) {
            noonSurvivedTwice = true;
        }
    }
    CHECK(noonSurvivedTwice);
}

// ADR-1961: the sun placed per palette state (a phase-keyed offset curve) or along the timeline (a
// keyable scalar), independent of the arc. Empty and zero, nothing moves.
TEST_CASE("The sun can be turned and raised per state and along the timeline", "[daynight][parameters][environment]") {
    scene::DayNightSettings arc;
    arc.enabled = true;
    const float dawn = 0.27f;
    const float noon = 0.5f;
    const auto azimuthOf = [](const glm::vec3& travel) { return std::atan2(-travel.x, -travel.z); };
    const auto elevationOf = [](const glm::vec3& travel) { return std::asin(std::clamp(-travel.y, -1.0f, 1.0f)); };

    // Unset: the arc exactly as before.
    scene::DayNightSettings same = arc;
    same.sunAzimuthOffset = {};
    CHECK(scene::resolveDayNight(same, dawn).sunDirection == scene::resolveDayNight(arc, dawn).sunDirection);

    // Keyed per state: dawn turned 1.2 rad and raised 0.1 rad, noon left alone.
    scene::DayNightSettings keyed = arc;
    keyed.sunAzimuthOffset = {{dawn, 1.2f}, {0.4f, 0.0f}, {0.6f, 0.0f}};
    keyed.sunElevationOffset = {{dawn, 0.1f}, {0.4f, 0.0f}, {0.6f, 0.0f}};
    const glm::vec3 a0 = scene::resolveDayNight(arc, dawn).sunDirection;
    const glm::vec3 a1 = scene::resolveDayNight(keyed, dawn).sunDirection;
    CHECK(std::remainder(azimuthOf(a1) - azimuthOf(a0), 6.2831853f) == Approx(1.2f).margin(1e-4));
    CHECK(elevationOf(a1) - elevationOf(a0) == Approx(0.1f).margin(1e-4));
    const glm::vec3 n0 = scene::resolveDayNight(arc, noon).sunDirection;
    const glm::vec3 n1 = scene::resolveDayNight(keyed, noon).sunDirection;
    CHECK(glm::length(n1 - n0) < 1e-5f);

    // The timeline scalar: a parameter, in degrees on the panel, added on top.
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    scene::Composition comp(registry, "composition");
    comp.attach(params, modulator);
    CHECK(registered(params, "env/dayNight/sun/azimuthOffsetDeg"));
    CHECK(registered(params, "env/dayNight/sun/elevationOffsetDeg"));
    scene::DayNightSettings biased = arc;
    biased.sunAzimuthBias = -0.5f;
    const glm::vec3 b1 = scene::resolveDayNight(biased, noon).sunDirection;
    CHECK(std::remainder(azimuthOf(b1) - azimuthOf(n0), 6.2831853f) == Approx(-0.5f).margin(1e-4));
    CHECK(biased.hash() != arc.hash());
    CHECK(keyed.hash() != arc.hash());

    // A scene file carries both and a save keeps them.
    json doc;
    doc["format"] = "avgen-scene";
    doc["version"] = 1;
    doc["name"] = "sunturn";
    doc["environment"] = {
        {"dayNight",
         {{"enabled", true},
          {"sunLight", "key"},
          {"sunAzimuthOffset", 0.25},
          {"sunElevationOffsetCurve", json::array({json{{"phase", 0.27}, {"value", 0.15}}, json{{"phase", 0.5}, {"value", 0.0}}})},
          {"sunAzimuthOffsetCurve", json::array({json{{"phase", 0.27}, {"value", 1.1}}, json{{"phase", 0.5}, {"value", 0.0}}})}}}};
    doc["nodes"] = json::array();
    const auto path = scratch() / "sunturn.scene.json";
    std::ofstream(path) << doc.dump(1);
    registry.setBaseDirectory(scratch());
    auto loaded = scene::Composition::loadFile(path, registry);
    REQUIRE(loaded.has_value());
    const auto out = scratch() / "sunturn.out.scene.json";
    REQUIRE((*loaded)->saveFile(out).has_value());
    json round;
    std::ifstream(out) >> round;
    const json& d = round.at("environment").at("dayNight");
    CHECK(d.value("sunAzimuthOffset", 0.0f) == Approx(0.25f));
    REQUIRE(d.contains("sunAzimuthOffsetCurve"));
    REQUIRE(d.contains("sunElevationOffsetCurve"));
    CHECK(d.at("sunAzimuthOffsetCurve")[0].value("value", 0.0f) == Approx(1.1f));
    CHECK(d.at("sunElevationOffsetCurve")[0].value("value", 0.0f) == Approx(0.15f));
}
