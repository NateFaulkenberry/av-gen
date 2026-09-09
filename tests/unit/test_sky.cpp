// Procedural sky environment (ADR-036): the analytic model, the key-light sun, the rebuild hash
// and the deterministic irradiance quadrature the GPU chain is fed from.
#include "scene/sky.hpp"

#include "scene/scene_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using namespace avgen;
using namespace avgen::scene;
using Catch::Matchers::WithinAbs;

namespace {

double d(float v) {
    return static_cast<double>(v);
}

float luminance(const glm::vec3& c) {
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

PunctualLight directional(const glm::vec3& direction, PunctualLight::Role role) {
    PunctualLight l;
    l.type = PunctualLight::Type::Directional;
    l.role = role;
    l.direction = direction;
    return l;
}

SkyRuntime defaultSky() {
    return resolveSky(SkySettings{}, {});
}

} // namespace

TEST_CASE("resolveSky: the sun comes from the key light", "[sky]") {
    SkySettings settings;
    const glm::vec3 travel = glm::normalize(glm::vec3(-0.3f, -0.9f, -0.25f));

    SECTION("a key directional light places the sun opposite its travel direction") {
        std::vector<PunctualLight> lights = {directional(glm::vec3(0.0f, -1.0f, 0.0f), PunctualLight::Role::Fill),
                                             directional(travel, PunctualLight::Role::Key)};
        const SkyRuntime sky = resolveSky(settings, lights);
        const glm::vec3 expected = -travel;
        CHECK_THAT(d(sky.sunDirection.x), WithinAbs(d(expected.x), 1e-5));
        CHECK_THAT(d(sky.sunDirection.y), WithinAbs(d(expected.y), 1e-5));
        CHECK_THAT(d(sky.sunDirection.z), WithinAbs(d(expected.z), 1e-5));
        CHECK_THAT(d(glm::length(sky.sunDirection)), WithinAbs(1.0, 1e-5));
        CHECK(skyKeyLight(lights) == &lights[1]);
    }

    SECTION("without a Key role the first enabled directional light stands in") {
        std::vector<PunctualLight> lights = {directional(travel, PunctualLight::Role::Rim)};
        CHECK(skyKeyLight(lights) == &lights[0]);
        const SkyRuntime sky = resolveSky(settings, lights);
        CHECK_THAT(d(sky.sunDirection.y), WithinAbs(d(-glm::normalize(travel).y), 1e-5));
    }

    SECTION("disabled and non-directional lights are ignored") {
        std::vector<PunctualLight> lights;
        PunctualLight off = directional(travel, PunctualLight::Role::Key);
        off.enabled = false;
        lights.push_back(off);
        PunctualLight point = directional(travel, PunctualLight::Role::Key);
        point.type = PunctualLight::Type::Point;
        lights.push_back(point);
        CHECK(skyKeyLight(lights) == nullptr);
        const SkyRuntime sky = resolveSky(settings, lights);
        CHECK_THAT(d(sky.sunDirection.y), WithinAbs(d(glm::normalize(settings.sunDirection).y), 1e-5));
    }

    SECTION("useKeyLight off keeps the authored direction") {
        settings.useKeyLight = false;
        settings.sunDirection = {0.0f, 1.0f, 0.0f};
        const std::vector<PunctualLight> lights = {directional(travel, PunctualLight::Role::Key)};
        const SkyRuntime sky = resolveSky(settings, lights);
        CHECK_THAT(d(sky.sunDirection.y), WithinAbs(1.0, 1e-5));
    }

    SECTION("the key light's colour tints the sun without changing its brightness") {
        std::vector<PunctualLight> lights = {directional(travel, PunctualLight::Role::Key)};
        lights[0].color = {1.0f, 0.4f, 0.2f};
        lights[0].intensity = 50.0f; // intensity must not leak into the sky
        const SkyRuntime sky = resolveSky(settings, lights);
        const SkyRuntime neutral = resolveSky(settings, {});
        CHECK(sky.sunColor.r > sky.sunColor.b);
        // Normalising by luminance keeps the brightness within a few percent (exactly equal only
        // when the authored sun colour is neutral, which the default is not).
        CHECK_THAT(d(luminance(sky.sunColor)), WithinAbs(d(luminance(neutral.sunColor)), 0.05));
    }
}

TEST_CASE("skyRadiance: gradient, ground, horizon band and sun disc", "[sky]") {
    const SkyRuntime sky = defaultSky();

    const glm::vec3 zenith = skyRadiance(sky, {0.0f, 1.0f, 0.0f});
    const glm::vec3 horizon = skyRadiance(sky, glm::normalize(glm::vec3(1.0f, 0.02f, 0.0f)));
    const glm::vec3 ground = skyRadiance(sky, {0.0f, -1.0f, 0.0f});

    // The horizon is the hazy, brighter part of the dome; the ground is the darkest.
    CHECK(luminance(horizon) > luminance(zenith));
    CHECK(luminance(ground) < luminance(zenith));
    // Zenith and ground read back the authored colours: straight up, only the haze tail of the
    // horizon colour is left (exp(-1 / hazeWidth)); straight down, nothing but the ground.
    CHECK_THAT(d(zenith.b), WithinAbs(d(sky.zenithColor.b * sky.intensity), 0.02));
    CHECK_THAT(d(ground.r), WithinAbs(d(sky.groundColor.r * sky.intensity), 1e-5));

    // The sun disc is far brighter than the sky around it, and the aureole sits between them.
    const glm::vec3 disc = skyRadiance(sky, sky.sunDirection);
    const glm::vec3 nearSun = skyRadiance(sky, glm::normalize(sky.sunDirection + glm::vec3(0.25f, 0.0f, 0.0f)));
    const glm::vec3 away = skyRadiance(sky, glm::normalize(glm::vec3(-sky.sunDirection.x, 0.6f, -sky.sunDirection.z)));
    CHECK(luminance(disc) > luminance(nearSun) * 4.0f);
    CHECK(luminance(nearSun) > luminance(away));

    // Every direction is non-negative and finite.
    for (const glm::vec3& dir : {glm::vec3(1, 0, 0), glm::vec3(0, -0.3f, 1), glm::vec3(-0.5f, 0.8f, 0.2f)}) {
        const glm::vec3 c = skyRadiance(sky, dir);
        CHECK(c.r >= 0.0f);
        CHECK(std::isfinite(c.r));
        CHECK(std::isfinite(c.g));
        CHECK(std::isfinite(c.b));
    }

    // Widening the disc for a coarse mip conserves its energy: radiance falls as the square of the
    // widening, which is what keeps the sun from brightening as the cube is filtered down.
    const glm::vec3 wide = skyRadiance(sky, sky.sunDirection, sky.sunAngularRadius * 3.0f);
    CHECK(luminance(wide) < luminance(disc));
    CHECK(luminance(wide) > luminance(nearSun));
}

TEST_CASE("skyIrradiance is deterministic and follows the sun", "[sky]") {
    const SkyRuntime sky = defaultSky();
    const glm::vec3 up{0.0f, 1.0f, 0.0f};

    const glm::vec3 a = skyIrradiance(sky, up);
    const glm::vec3 b = skyIrradiance(sky, up);
    CHECK(a == b); // bit-identical: a fixed quadrature over an analytic sky

    // Facing the sun collects more than facing away from it, and facing down collects the ground.
    const glm::vec3 towards = skyIrradiance(sky, sky.sunDirection);
    const glm::vec3 away = skyIrradiance(sky, -sky.sunDirection);
    const glm::vec3 down = skyIrradiance(sky, -up);
    CHECK(luminance(towards) > luminance(away));
    CHECK(luminance(a) > luminance(down));
    CHECK(luminance(down) > 0.0f);

    // Sample count changes the estimate but not by much: the quadrature has converged.
    const glm::vec3 coarse = skyIrradiance(sky, up, 128);
    const glm::vec3 fine = skyIrradiance(sky, up, 2048);
    CHECK_THAT(d(luminance(coarse)), WithinAbs(d(luminance(fine)), 0.05));

    // Scaling the sky scales the irradiance exactly.
    SkyRuntime brighter = sky;
    brighter.intensity = sky.intensity * 3.0f;
    CHECK_THAT(d(luminance(skyIrradiance(brighter, up))), WithinAbs(d(luminance(a)) * 3.0, 1e-4));
}

TEST_CASE("SkyRuntime::hash changes exactly when a rebuild is needed", "[sky]") {
    const SkyRuntime base = defaultSky();
    CHECK(base.hash() == defaultSky().hash());
    const auto differs = [&base](auto&& mutate) {
        SkyRuntime s = base;
        mutate(s);
        return s.hash() != base.hash();
    };
    CHECK(differs([](SkyRuntime& s) { s.zenithColor.g += 0.01f; }));
    CHECK(differs([](SkyRuntime& s) { s.horizonColor.r += 0.01f; }));
    CHECK(differs([](SkyRuntime& s) { s.groundColor.b += 0.01f; }));
    CHECK(differs([](SkyRuntime& s) { s.hazeWidth += 0.01f; }));
    CHECK(differs([](SkyRuntime& s) { s.sunColor.r += 0.01f; }));
    CHECK(differs([](SkyRuntime& s) { s.sunIntensity += 1.0f; }));
    CHECK(differs([](SkyRuntime& s) { s.sunAngularRadius += 0.01f; }));
    CHECK(differs([](SkyRuntime& s) { s.sunGlowWidth += 0.01f; }));
    CHECK(differs([](SkyRuntime& s) { s.intensity += 0.1f; }));
    CHECK(differs([](SkyRuntime& s) { s.sunDirection = glm::normalize(glm::vec3(1.0f, 0.2f, 0.0f)); }));
    // Moving the key light moves the sun, so the hash tracks the light too.
    const SkyRuntime a = resolveSky(SkySettings{}, {directional({0.0f, -1.0f, 0.0f}, PunctualLight::Role::Key)});
    const SkyRuntime b = resolveSky(SkySettings{}, {directional({-0.5f, -1.0f, 0.0f}, PunctualLight::Role::Key)});
    CHECK(a.hash() != b.hash());
}

TEST_CASE("resolveSky clamps the values a build would divide by", "[sky]") {
    SkySettings settings;
    settings.hazeWidth = 0.0f;
    settings.sunAngularRadius = 0.0f;
    settings.sunGlowWidth = -1.0f;
    settings.intensity = -2.0f;
    settings.sunIntensity = -3.0f;
    settings.zenithColor = {-1.0f, 0.5f, 0.0f};
    settings.sunDirection = {0.0f, 0.0f, 0.0f};
    const SkyRuntime sky = resolveSky(settings, {});
    CHECK(sky.hazeWidth > 0.0f);
    CHECK(sky.sunAngularRadius > 0.0f);
    CHECK(sky.sunGlowWidth > 0.0f);
    CHECK(sky.intensity == 0.0f);
    CHECK(sky.sunIntensity == 0.0f);
    CHECK(sky.zenithColor.r == 0.0f);
    CHECK_THAT(d(glm::length(sky.sunDirection)), WithinAbs(1.0, 1e-5));
    const glm::vec3 c = skyRadiance(sky, {0.0f, 1.0f, 0.0f});
    CHECK(std::isfinite(c.r));
}
