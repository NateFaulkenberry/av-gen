// Light rigs (ADR-033): the placement frame, the photometric conversion, validation, the JSON
// round trip, the structural hash, the parameters, and the six rigs shipped under
// examples/lightrigs/.

#include "params/parameter_set.hpp"
#include "scene/light_rig.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <numbers>

using namespace avgen;
using Catch::Approx;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

scene::RigLight makeLight(std::string name, scene::PunctualLight::Role role, float azimuth, float elevation) {
    scene::RigLight l;
    l.name = std::move(name);
    l.role = role;
    l.type = scene::PunctualLight::Type::Directional;
    l.azimuthDegrees = azimuth;
    l.elevationDegrees = elevation;
    l.distanceRadii = 2.0f;
    l.intensity = 1.0f;
    return l;
}

scene::LightRig threePoint() {
    scene::LightRig rig;
    rig.name = "three-point";
    rig.keyIntensity = 4.0f;
    rig.ambientIntensity = 0.2f;
    rig.lights = {makeLight("key", scene::PunctualLight::Role::Key, 40.0f, 25.0f),
                  makeLight("fill", scene::PunctualLight::Role::Fill, -40.0f, 5.0f),
                  makeLight("rim", scene::PunctualLight::Role::Rim, 160.0f, 35.0f)};
    rig.lights[1].intensity = 0.25f;
    rig.lights[2].intensity = 0.6f;
    return rig;
}

std::filesystem::path rigDirectory() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "lightrigs";
}

// The angle, in degrees, between two directions.
float angleBetween(const glm::vec3& a, const glm::vec3& b) {
    return std::acos(std::clamp(glm::dot(glm::normalize(a), glm::normalize(b)), -1.0f, 1.0f)) * 180.0f / kPi;
}

} // namespace

TEST_CASE("a rig places its lights in the camera's frame", "[lightrig]") {
    const scene::LightRig rig = threePoint();
    const glm::vec3 subject(0.0f);
    const glm::vec3 camera(0.0f, 0.0f, 20.0f); // looking down -Z
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const auto lights = rig.expand(subject, 4.0f, camera, up);
    REQUIRE(lights.size() == 3);

    // Azimuth is measured from the camera's view direction about the subject's up axis: 0 puts the
    // light behind the camera, +90 to the camera's right.
    const glm::vec3 key = lights[0].position;
    CHECK(key.z > 0.0f); // on the camera's side of the subject
    CHECK(key.x > 0.0f); // +40 degrees is to the camera's right
    CHECK(key.y > 0.0f); // 25 degrees of elevation is above the horizon
    // Distance is in subject radii: 2 radii of a 4-unit subject is 8 units.
    CHECK(glm::length(key - subject) == Approx(8.0f).epsilon(1e-4));
    // The elevation is exactly what was asked for.
    const glm::vec3 flat(key.x, 0.0f, key.z);
    CHECK(angleBetween(key, flat) == Approx(25.0f).epsilon(1e-3));
    // And so is the azimuth, measured from the direction back towards the camera.
    CHECK(angleBetween(flat, glm::vec3(0.0f, 0.0f, 1.0f)) == Approx(40.0f).epsilon(1e-3));

    // The fill is on the other side, the rim behind the subject.
    CHECK(lights[1].position.x < 0.0f);
    CHECK(lights[2].position.z < 0.0f);
    // Every light points at the subject.
    for (const scene::PunctualLight& l : lights) {
        CHECK(angleBetween(l.direction, subject - l.position) == Approx(0.0f).margin(1e-2f));
    }
    // Roles and names survive expansion.
    CHECK(lights[0].role == scene::PunctualLight::Role::Key);
    CHECK(lights[2].name == "rim");
}

TEST_CASE("a rig follows the camera, or the world, as asked", "[lightrig]") {
    scene::LightRig rig = threePoint();
    rig.lights.resize(1);
    const glm::vec3 subject(0.0f);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);

    // followCamera: moving the camera around the subject carries the key with it.
    const auto fromFront = rig.expand(subject, 1.0f, {0.0f, 0.0f, 10.0f}, up);
    const auto fromSide = rig.expand(subject, 1.0f, {10.0f, 0.0f, 0.0f}, up);
    CHECK(fromFront[0].position.z > 0.0f);
    CHECK(fromSide[0].position.x > 0.0f);
    CHECK(fromSide[0].position.z < fromFront[0].position.z);

    // Without it, the azimuth is measured from world +Z whatever the camera does.
    rig.lights[0].followCamera = false;
    const auto worldA = rig.expand(subject, 1.0f, {0.0f, 0.0f, 10.0f}, up);
    const auto worldB = rig.expand(subject, 1.0f, {10.0f, 0.0f, 0.0f}, up);
    CHECK(worldA[0].position.x == Approx(worldB[0].position.x).margin(1e-4f));
    CHECK(worldA[0].position.z == Approx(worldB[0].position.z).margin(1e-4f));
}

TEST_CASE("a rig scales its photometry to the subject", "[lightrig]") {
    scene::LightRig rig;
    rig.name = "scaling";
    rig.keyIntensity = 2.0f;
    rig.ambientIntensity = 0.5f;
    rig.ambientColor = glm::vec3(0.2f, 0.3f, 0.9f);
    rig.ambientTemperature = 9000.0f;

    scene::RigLight sun = makeLight("sun", scene::PunctualLight::Role::Key, 0.0f, 45.0f);
    scene::RigLight bulb = makeLight("bulb", scene::PunctualLight::Role::Practical, 90.0f, 0.0f);
    bulb.type = scene::PunctualLight::Type::Point;
    scene::RigLight panel = makeLight("panel", scene::PunctualLight::Role::Fill, -90.0f, 10.0f);
    panel.type = scene::PunctualLight::Type::Rect;
    panel.sizeRadii = 1.0f;
    scene::RigLight sky = makeLight("sky", scene::PunctualLight::Role::Ambient, 0.0f, 85.0f);
    rig.lights = {sun, bulb, panel, sky};

    const auto small = rig.expand(glm::vec3(0.0f), 1.0f, {0.0f, 0.0f, 5.0f}, {0.0f, 1.0f, 0.0f});
    const auto large = rig.expand(glm::vec3(0.0f), 10.0f, {0.0f, 0.0f, 50.0f}, {0.0f, 1.0f, 0.0f});
    REQUIRE(small.size() == 4);

    // A directional light is already an illuminance: the scale does not touch it.
    CHECK(small[0].intensity == Approx(2.0f));
    CHECK(large[0].intensity == Approx(2.0f));
    // A point light is an intensity, so it grows with the square of the distance and gives the
    // same illuminance at the subject in both worlds.
    CHECK(small[1].intensity == Approx(2.0f * 2.0f * 2.0f)); // base * d^2, d = 2 radii of radius 1
    CHECK(large[1].intensity / small[1].intensity == Approx(100.0f).epsilon(1e-3));
    CHECK(small[1].intensity / (2.0f * 2.0f) == Approx(large[1].intensity / (20.0f * 20.0f)).epsilon(1e-3));
    // An area light is a radiance, so it also divides by the emitter's area: a bigger panel at the
    // same rig ratio is not brighter, it is softer.
    CHECK(large[2].intensity / small[2].intensity == Approx(1.0f).epsilon(1e-3));
    CHECK(large[2].width / small[2].width == Approx(10.0f).epsilon(1e-3));
    // The ambient role takes the rig's own colour, temperature and intensity.
    CHECK(small[3].intensity == Approx(0.5f));
    CHECK(small[3].color == rig.ambientColor);
    CHECK(small[3].temperature == Approx(9000.0f));
}

TEST_CASE("a rig validates its own data", "[lightrig]") {
    scene::LightRig rig = threePoint();
    CHECK(rig.validate().has_value());

    auto broken = [&](auto mutate) {
        scene::LightRig copy = threePoint();
        mutate(copy);
        return copy.validate().has_value();
    };
    CHECK_FALSE(broken([](scene::LightRig& r) { r.name.clear(); }));
    CHECK_FALSE(broken([](scene::LightRig& r) { r.lights.clear(); }));
    CHECK_FALSE(broken([](scene::LightRig& r) { r.lights[1].name = r.lights[0].name; }));
    CHECK_FALSE(broken([](scene::LightRig& r) { r.lights[0].name.clear(); }));
    CHECK_FALSE(broken([](scene::LightRig& r) { r.lights[0].distanceRadii = 0.0f; }));
    CHECK_FALSE(broken([](scene::LightRig& r) { r.lights[0].elevationDegrees = 120.0f; }));
    CHECK_FALSE(broken([](scene::LightRig& r) { r.lights[0].temperature = 100.0f; }));
    CHECK_FALSE(broken([](scene::LightRig& r) { r.lights[0].aspect = 0.0f; }));
    CHECK_FALSE(broken([](scene::LightRig& r) { r.lights[0].coneDegrees = 200.0f; }));
    CHECK_FALSE(broken([](scene::LightRig& r) { r.keyIntensity = -1.0f; }));
    CHECK_FALSE(broken([](scene::LightRig& r) { r.ambientTemperature = 100000.0f; }));
}

TEST_CASE("a rig round-trips through JSON", "[lightrig]") {
    scene::LightRig rig = threePoint();
    rig.description = "a test rig";
    rig.lights[0].type = scene::PunctualLight::Type::Rect;
    rig.lights[0].castsShadow = true;
    rig.lights[0].softness = 1.7f;
    rig.lights[0].sizeRadii = 0.8f;
    rig.lights[0].aspect = 2.5f;
    rig.lights[0].tint = 0.2f;
    rig.lights[0].volumetricStrength = 0.4f;
    rig.lights[1].followCamera = false;
    rig.lights[2].type = scene::PunctualLight::Type::Spot;
    rig.lights[2].coneDegrees = 33.0f;

    const nlohmann::json j = rig.toJson();
    const auto back = scene::LightRig::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->structuralHash() == rig.structuralHash());
    CHECK(back->description == rig.description);
    REQUIRE(back->lights.size() == rig.lights.size());
    for (std::size_t i = 0; i < rig.lights.size(); ++i) {
        CHECK(back->lights[i].name == rig.lights[i].name);
        CHECK(back->lights[i].type == rig.lights[i].type);
        CHECK(back->lights[i].role == rig.lights[i].role);
        CHECK(back->lights[i].castsShadow == rig.lights[i].castsShadow);
        CHECK(back->lights[i].followCamera == rig.lights[i].followCamera);
        CHECK(back->lights[i].coneDegrees == Approx(rig.lights[i].coneDegrees));
    }
    // The hash notices a change anywhere.
    scene::LightRig moved = rig;
    moved.lights[0].azimuthDegrees += 1.0f;
    CHECK(moved.structuralHash() != rig.structuralHash());

    // Bad documents are errors, not silent defaults.
    CHECK_FALSE(scene::LightRig::fromJson(nlohmann::json::array()).has_value());
    CHECK_FALSE(scene::LightRig::fromJson(nlohmann::json{{"format", "something-else"}}).has_value());
    nlohmann::json noLights = j;
    noLights.erase("lights");
    CHECK_FALSE(scene::LightRig::fromJson(noLights).has_value());
    nlohmann::json badType = j;
    badType["lights"][0]["type"] = "hexagon";
    CHECK_FALSE(scene::LightRig::fromJson(badType).has_value());
}

TEST_CASE("the shipped rigs load, validate and expand", "[lightrig]") {
    const std::filesystem::path dir = rigDirectory();
    REQUIRE(std::filesystem::is_directory(dir));
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        INFO("rig " << entry.path().string());
        auto rig = scene::LightRig::loadFile(entry.path());
        REQUIRE(rig.has_value());
        REQUIRE(rig->validate().has_value());
        names.push_back(rig->name);
        // Every rig produces usable lights around any subject.
        const auto lights = rig->expand(glm::vec3(3.0f, 1.0f, -2.0f), 12.0f, {0.0f, 8.0f, 40.0f},
                                        {0.0f, 1.0f, 0.0f});
        REQUIRE(lights.size() == rig->lights.size());
        for (const scene::PunctualLight& l : lights) {
            CHECK(std::isfinite(l.intensity));
            CHECK(l.intensity >= 0.0f);
            CHECK(glm::length(l.direction) == Approx(1.0f).margin(1e-3f));
            CHECK(l.temperature >= 1500.0f);
            CHECK(l.temperature <= 12000.0f);
        }
        // The file round-trips through its own serialiser.
        const auto again = scene::LightRig::fromJson(rig->toJson());
        REQUIRE(again.has_value());
        CHECK(again->structuralHash() == rig->structuralHash());
    }
    // Not an exact list. What matters is that every shipped rig loads and that names stay unique,
    // because the name is the parameter path ("lightrig/<name>/..."): two rigs sharing one would
    // collide silently. Enumerating them only made authoring a new rig break an unrelated test.
    std::sort(names.begin(), names.end());
    CHECK(names.size() >= 6);
    CHECK(std::adjacent_find(names.begin(), names.end()) == names.end());
    for (const std::string& name : names) {
        INFO("rig name " << name);
        CHECK_FALSE(name.empty());
        CHECK(name.find(' ') == std::string::npos);
        CHECK(name.find('/') == std::string::npos);
    }
}

TEST_CASE("rig parameters register, apply and unregister", "[lightrig]") {
    const scene::LightRig rest = threePoint();
    params::ParameterSet params;
    const std::size_t before = params.size();
    scene::LightRigParameters p = scene::registerLightRigParameters(params, rest, "");
    CHECK(params.size() > before);
    REQUIRE(p.keyIntensity != nullptr);
    REQUIRE(p.lightIntensity.size() == rest.lights.size());
    CHECK(params.find("lightrig/three-point/keyIntensity") != nullptr);
    CHECK(params.find("lightrig/three-point/key/elevation") != nullptr);

    // Applying with the defaults reproduces the authored rig exactly.
    scene::LightRig live;
    scene::applyLightRigParameters(p, rest, live);
    CHECK(live.structuralHash() == rest.structuralHash());

    // Moving a parameter moves the expanded light.
    p.keyIntensity->setBase(8.0f);
    params.resetFinals();
    scene::applyLightRigParameters(p, rest, live);
    CHECK(live.keyIntensity == Approx(8.0f));
    CHECK(live.expand(glm::vec3(0.0f), 1.0f, {0.0f, 0.0f, 5.0f}, {0.0f, 1.0f, 0.0f})[0].intensity ==
          Approx(8.0f));

    scene::unregisterLightRigParameters(params, p);
    CHECK(params.size() == before);
    CHECK(params.find("lightrig/three-point/keyIntensity") == nullptr);
}
