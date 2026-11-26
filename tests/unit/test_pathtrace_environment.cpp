// ADR-2240 (the environment map), ADR-2241 (procedural records are world space) and ADR-2245 (sorted EXR channels):
// the three ways a Neon Dreams trace drew a different scene from the one the raster draws.

#include "assets/exr.hpp"
#include "pathtrace/environment.hpp"
#include "pathtrace/lights.hpp"
#include "pathtrace/path_tracer.hpp"
#include "pathtrace/sampler.hpp"
#include "pathtrace/snapshot.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/quaternion.hpp>
#include <tinyexr.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

constexpr float kPi = 3.14159265358979323846f;

// An equirectangular RGBA32F map whose texel (x, y) is `f(x, y)`.
template <class F>
scene::TextureData hdrMap(std::uint32_t w, std::uint32_t h, F f) {
    scene::TextureData t;
    t.name = "test-env";
    t.width = w;
    t.height = h;
    t.format = scene::TextureFormat::Rgba32Float;
    t.data.resize(static_cast<std::size_t>(w) * h * 16);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const glm::vec3 c = f(x, y);
            const float rgba[4] = {c.x, c.y, c.z, 1.0f};
            std::memcpy(t.data.data() + (static_cast<std::size_t>(y) * w + x) * 16, rgba, 16);
        }
    }
    return t;
}

float luminance(const glm::vec3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

// A scene with a sun (so the analytic sky, if it were used, would be a bright day) and an HDR map.
scene::Scene mappedScene(glm::vec3 mapColour) {
    scene::Scene s;
    s.environment.sky.enabled = true; // the default -- and the trap: the analytic sky must NOT stand in for the map
    scene::PunctualLight sun;
    sun.type = scene::PunctualLight::Type::Directional;
    sun.direction = glm::normalize(glm::vec3(0.3f, -1.0f, 0.2f));
    sun.intensity = 5.0f;
    s.addLight(sun);
    s.environment.environmentMap = s.addTexture(hdrMap(64, 32, [&](auto, auto) { return mapColour; }));
    return s;
}

} // namespace

TEST_CASE("an HDR map is the environment: never replaced by the analytic sky", "[unit][pathtrace][environment]") {
    scene::Scene s = mappedScene(glm::vec3(0.002f, 0.001f, 0.005f)); // a night sky
    s.environment.skyIntensity = 0.5f;
    s.environment.skyTint = glm::vec3(1.0f, 0.8f, 1.0f);
    s.environment.environmentIntensity = 3.0f;
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
    REQUIRE(snap.environment.lighting() == pathtrace::Environment::Lighting::Map);
    REQUIRE(snap.environment.background() == pathtrace::Environment::Background::Map);

    for (const glm::vec3 d : {glm::vec3(0, 1, 0), glm::normalize(glm::vec3(1, 0.2f, 0)), glm::vec3(0, 0, -1)}) {
        // What the camera sees: the map at the visible sky's intensity and tint (sky_background.wgsl).
        const glm::vec3 bg = snap.environment.backgroundRadiance(d);
        CHECK(bg.x == Approx(0.002f * 0.5f * 1.0f).epsilon(1e-4));
        CHECK(bg.y == Approx(0.001f * 0.5f * 0.8f).epsilon(1e-4));
        CHECK(bg.z == Approx(0.005f * 0.5f * 1.0f).epsilon(1e-4));
        // What lights the scene: the map at environmentIntensity.
        const glm::vec3 light = snap.environment.radiance(d);
        CHECK(light.z == Approx(0.005f * 3.0f).epsilon(1e-4));
        // The control: the analytic daylight sky this replaced is a different, much brighter thing.
        const scene::SkyRuntime sky = scene::resolveSky(s.environment.sky, s.lights);
        CHECK(luminance(scene::skyRadiance(sky, d)) > 10.0f * luminance(light));
    }
    // Reported as the real thing now, not as a degradation.
    bool reported = false;
    for (const auto& c : snap.capabilities.entries) {
        if (c.feature == "environment map") {
            reported = true;
            CHECK(c.support == pathtrace::Support::Full);
        }
    }
    CHECK(reported);
}

TEST_CASE("the camera sees the background the raster draws", "[unit][pathtrace][environment]") {
    SECTION("showSkybox off: the flat background colour, with the map still lighting") {
        scene::Scene s = mappedScene(glm::vec3(1.0f));
        s.environment.showSkybox = false;
        s.environment.backgroundColor = glm::vec3(0.1f, 0.2f, 0.3f);
        const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
        CHECK(snap.environment.background() == pathtrace::Environment::Background::FlatColour);
        CHECK(snap.environment.backgroundRadiance(glm::vec3(0, 1, 0)) == glm::vec3(0.1f, 0.2f, 0.3f));
        CHECK(snap.environment.lighting() == pathtrace::Environment::Lighting::Map);
    }
    SECTION("no map, sky lighting but not shown (the default): flat colour behind, sky as light") {
        scene::Scene s;
        s.environment.backgroundColor = glm::vec3(0.05f);
        const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
        CHECK(snap.environment.lighting() == pathtrace::Environment::Lighting::AnalyticSky);
        CHECK(snap.environment.background() == pathtrace::Environment::Background::FlatColour);
    }
    SECTION("no map, sky shown: the sky behind the scene") {
        scene::Scene s;
        s.environment.sky.showBackground = true;
        const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
        CHECK(snap.environment.background() == pathtrace::Environment::Background::AnalyticSky);
    }
}

TEST_CASE("the environment rotation is the raster's envRotate", "[unit][pathtrace][environment]") {
    // A map with a single bright column, so a rotation is visible as where the brightness lands.
    const auto map = hdrMap(64, 32, [](std::uint32_t x, std::uint32_t) {
        return x == 40 ? glm::vec3(10.0f) : glm::vec3(0.1f);
    });
    std::vector<scene::TextureData> textures{map};
    scene::Environment env;
    env.environmentMap = 0;
    env.environmentRotation = 0.0f;
    const auto flat = pathtrace::Environment::build(env, {}, textures);
    env.environmentRotation = 1.1f;
    const auto turned = pathtrace::Environment::build(env, {}, textures);

    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    for (int i = 0; i < 200; ++i) {
        const glm::vec3 d = glm::normalize(glm::vec3(u(rng), u(rng), u(rng)));
        const glm::vec3 back = turned.toWorld(turned.toMap(d));
        CHECK(glm::length(back - d) < 1e-5f);
        // envRotate(dir) from shaders/common.wgsl, written out.
        const float c = std::cos(1.1f);
        const float s = std::sin(1.1f);
        const glm::vec3 wgsl{c * d.x + s * d.z, d.y, -s * d.x + c * d.z};
        CHECK(glm::length(turned.toMap(d) - wgsl) < 1e-6f);
        CHECK(turned.radiance(d) == flat.radiance(wgsl));
    }
}

TEST_CASE("the map's sampler is a density, and agrees with its pdf", "[unit][pathtrace][environment]") {
    // Strongly non-uniform: a hot spot, a gradient, a black band.
    const auto map = hdrMap(48, 24, [](std::uint32_t x, std::uint32_t y) {
        if (y > 16) return glm::vec3(0.0f);
        const float hot = (x > 30 && x < 34 && y > 4 && y < 8) ? 200.0f : 0.0f;
        return glm::vec3(0.2f + 0.05f * static_cast<float>(x) + hot);
    });
    std::vector<scene::TextureData> textures{map};
    scene::Environment env;
    env.environmentMap = 0;
    env.environmentRotation = 0.4f;
    const auto e = pathtrace::Environment::build(env, {}, textures);
    REQUIRE(e.sampled());

    // The integral of the pdf over the sphere is 1 (uniform-sphere Monte Carlo).
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    double integral = 0.0;
    const int n = 400000;
    for (int i = 0; i < n; ++i) {
        const float z = 1.0f - 2.0f * u01(rng);
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float phi = 2.0f * kPi * u01(rng);
        integral += e.pdf(glm::vec3(r * std::cos(phi), z, r * std::sin(phi)));
    }
    integral *= 4.0 * kPi / n;
    CHECK(integral == Approx(1.0).epsilon(0.03));

    // Every sample's density and radiance are the ones `pdf` and `radiance` give for its direction, and the black
    // band is never chosen.
    pathtrace::Sampler sampler(123, 0, 0);
    int hot = 0;
    for (int i = 0; i < 2000; ++i) {
        const auto s = e.sample(sampler.next2D());
        REQUIRE(s.valid);
        CHECK(s.pdf == e.pdf(s.direction));
        CHECK(s.radiance == e.radiance(s.direction));
        CHECK(luminance(s.radiance) > 0.0f);
        hot += luminance(s.radiance) > 100.0f ? 1 : 0;
    }
    CHECK(hot > 200); // the hot spot is drawn far more often than its area alone would give
}

TEST_CASE("map lighting: light sampling, BSDF sampling and MIS converge to one answer",
          "[unit][pathtrace][environment][mis]") {
    // A diffuse floor under a uniform map. Every estimator sees the same integral.
    scene::Scene s = mappedScene(glm::vec3(0.5f, 0.6f, 0.7f));
    s.environment.environmentIntensity = 2.0f;
    const scene::MeshId floorId = s.addMesh(scene::makePlane(50.0f, 1));
    scene::Entity& floor = s.addEntity("floor", floorId);
    floor.material.baseColor = glm::vec3(0.5f);
    floor.material.roughness = 0.9f;
    floor.material.metallic = 0.0f;
    s.lights.clear(); // the map alone
    s.camera.position = glm::vec3(0.0f, 3.0f, 0.0f);
    s.camera.target = glm::vec3(0.0f, 0.0f, -0.01f);
    s.camera.lens.useExplicitFov = true;
    s.camera.fovYRadians = 0.5f;

    pathtrace::TraceSettings t;
    t.width = 32;
    t.height = 32;
    t.samplesPerPixel = 128;
    t.maxDepth = 1;
    t.russianRouletteDepth = 0;
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
    const auto meanWith = [&](pathtrace::TraceSettings::Strategy strategy) {
        pathtrace::TraceSettings arm = t;
        arm.strategy = strategy;
        pathtrace::PathTracer tracer;
        pathtrace::Framebuffer fb;
        REQUIRE(tracer.render(snap, arm, fb).has_value());
        return fb.meanLuminance();
    };
    const double mis = meanWith(pathtrace::TraceSettings::Strategy::Mis);
    const double light = meanWith(pathtrace::TraceSettings::Strategy::LightOnly);
    const double bsdf = meanWith(pathtrace::TraceSettings::Strategy::BsdfOnly);
    CHECK(light == Approx(mis).epsilon(0.03));
    CHECK(bsdf == Approx(mis).epsilon(0.03));
    // Lambert under uniform radiance L: outgoing = albedo * L (plus the glTF specular's few percent).
    const double expected = 0.5 * 2.0 * luminance(glm::vec3(0.5f, 0.6f, 0.7f));
    CHECK(mis == Approx(expected).epsilon(0.15));
}

TEST_CASE("procedural records are world space: traced where the raster draws them",
          "[unit][pathtrace][procedural][adr2241]") {
    // A real generation, with a distribution transform that is NOT the identity -- the case every earlier test
    // skipped by placing records by hand.
    scene::ProceduralGeometry g;
    g.name = "moved";
    g.source.kind = scene::PrimitiveKind::Box;
    g.source.size = glm::vec3(1.0f);
    g.distribution.kind = scene::DistributionKind::Grid;
    g.distribution.gridCount = glm::ivec3(3, 1, 2);
    g.distributionTransform.position = glm::vec3(40.0f, 5.0f, -12.0f);
    g.distributionTransform.rotation = glm::angleAxis(0.7f, glm::vec3(0.0f, 1.0f, 0.0f));
    REQUIRE(g.rebuild({}));
    REQUIRE(g.instances.size() == 6);

    scene::Scene s;
    s.procedurals.push_back(g);
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
    REQUIRE(snap.instanced.size() == 1);
    const auto& obj = snap.instanced[0];
    REQUIRE(obj.transforms.size() == g.instances.size());
    for (std::size_t i = 0; i < g.instances.size(); ++i) {
        const glm::vec3 traced = glm::vec3(obj.transforms[i][3]);
        const glm::vec3 drawn = glm::vec3(g.instances[i].position); // what procedural.wgsl puts at the origin
        INFO("instance " << i << " traced " << traced.x << "," << traced.y << "," << traced.z << " drawn "
                         << drawn.x << "," << drawn.y << "," << drawn.z);
        CHECK(glm::length(traced - drawn) < 1e-4f);
        // Inside the bounds the raster culls with.
        CHECK(traced.x >= g.boundsMin.x - 1e-3f);
        CHECK(traced.x <= g.boundsMax.x + 1e-3f);
    }
    // The control: the records really do carry the transform (else this test could not tell the bug from the fix).
    CHECK(glm::length(glm::vec3(g.instances[0].position)) > 20.0f);
}

TEST_CASE("multi-layer EXR channels are written in OpenEXR's sorted order", "[unit][pathtrace][exr][adr2245]") {
    const auto path = std::filesystem::temp_directory_path() / "adr2245-layers.exr";
    const std::vector<float> one(4, 1.0f);
    const std::vector<float> two(4, 2.0f);
    const std::vector<float> three(4, 3.0f);
    // Given in the order the tracer's AOV writer uses: beauty first, then the layers.
    const std::vector<assets::ExrChannel> channels{
        {"R", one, false}, {"G", two, false}, {"B", three, false}, {"albedo.R", two, true}, {"depth.Z", one, false}};
    REQUIRE(assets::writeExrLayers(path, 2, 2, channels).has_value());

    EXRVersion version;
    REQUIRE(ParseEXRVersionFromFile(&version, path.string().c_str()) == TINYEXR_SUCCESS);
    EXRHeader header;
    InitEXRHeader(&header);
    const char* err = nullptr;
    REQUIRE(ParseEXRHeaderFromFile(&header, &version, path.string().c_str(), &err) == TINYEXR_SUCCESS);
    std::vector<std::string> names;
    for (int i = 0; i < header.num_channels; ++i) names.emplace_back(header.channels[i].name);
    // Byte-wise sorted, as OpenEXR's channel map is: upper case before lower case.
    CHECK(names == std::vector<std::string>{"B", "G", "R", "albedo.R", "depth.Z"});

    // The data moved with its name: read in file order, the channel called "B" holds what was given as "B".
    for (int i = 0; i < header.num_channels; ++i) header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT;
    EXRImage image;
    InitEXRImage(&image);
    REQUIRE(LoadEXRImageFromFile(&image, &header, path.string().c_str(), &err) == TINYEXR_SUCCESS);
    const auto first = [&](int c) { return reinterpret_cast<const float*>(image.images[c])[0]; };
    CHECK(first(0) == 3.0f); // B
    CHECK(first(1) == 2.0f); // G
    CHECK(first(2) == 1.0f); // R
    CHECK(first(3) == 2.0f); // albedo.R
    CHECK(first(4) == 1.0f); // depth.Z
    FreeEXRImage(&image);
    FreeEXRHeader(&header);
}
