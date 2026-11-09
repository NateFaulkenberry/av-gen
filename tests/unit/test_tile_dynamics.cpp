// ADR-1953: Musical Geography's moving ground -- the CPU rule (scene/tile_terrain.hpp). The GPU twin's parity is
// test_tile_terrain_gpu.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/tile_terrain.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstring>
#include <limits>

using namespace avgen;
using Catch::Approx;

namespace {

scene::TileDynamics busy() {
    scene::TileDynamics d;
    scene::TileWave a;
    a.amplitude = 0.8f;
    a.wavelength = 36.0f;
    a.speed = 7.0f;
    a.direction = 30.0f;
    a.sharpness = 0.5f;
    scene::TileWave b;
    b.circular = true;
    b.amplitude = 0.5f;
    b.wavelength = 22.0f;
    b.speed = -4.0f;
    b.centre = {40.0f, -60.0f};
    d.waves = {a, b};
    d.breathingAmplitude = 0.2f;
    d.breathingPeriod = 12.0f;
    d.rolling = {1.5f, -0.5f};
    d.rings.amplitude = 1.2f;
    d.events = {{2.0f, {10.0f, 10.0f}, 1.0f}, {5.0f, {-30.0f, 25.0f}, 0.6f}};
    return d;
}

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

} // namespace

TEST_CASE("an inactive moving ground is the static ground, bit for bit", "[tiles][dynamics][geography]") {
    const scene::TileTerrainSpec spec;
    const scene::TileDynamics still;
    CHECK_FALSE(still.active());
    for (int i = 0; i < 200; ++i) {
        const float x = -300.0f + 7.31f * static_cast<float>(i);
        const float z = 120.0f - 3.77f * static_cast<float>(i);
        const float t = 0.37f * static_cast<float>(i);
        const scene::TileSample a = scene::tileSample(spec, x, z);
        const scene::TileSample b = scene::tileSampleMoving(spec, still, x, z, t, 1.0f);
        CHECK(sameBits(a.height, b.height));
        CHECK(sameBits(a.water, b.water));
        CHECK(b.crest == 0.0f);
    }
}

TEST_CASE("the moving ground is continuous in time", "[tiles][dynamics][geography]") {
    // A bounded derivative: over 1/120 s no tile moves more than the fastest term allows (generous bound).
    const scene::TileTerrainSpec spec;
    const scene::TileDynamics d = busy();
    const float dt = 1.0f / 120.0f;
    float worst = 0.0f;
    for (int i = 0; i < 400; ++i) {
        const float x = -100.0f + 0.53f * static_cast<float>(i);
        const float z = 30.0f + 0.29f * static_cast<float>(i);
        const float t = 0.05f * static_cast<float>(i);
        const float h0 = scene::tileSampleMoving(spec, d, x, z, t, 1.0f).height;
        const float h1 = scene::tileSampleMoving(spec, d, x, z, t + dt, 1.0f).height;
        REQUIRE(std::isfinite(h0));
        worst = std::max(worst, std::abs(h1 - h0));
    }
    CAPTURE(worst);
    CHECK(worst < 0.5f);
}

TEST_CASE("wave trains and rings add", "[tiles][dynamics][geography]") {
    scene::TileDynamics a = busy();
    a.waves.resize(1);
    a.events.clear();
    scene::TileDynamics b = busy();
    b.waves.erase(b.waves.begin());
    scene::TileDynamics both = busy();
    for (int i = 0; i < 100; ++i) {
        const float x = 3.1f * static_cast<float>(i) - 150.0f;
        const float z = -2.3f * static_cast<float>(i) + 40.0f;
        const float t = 3.0f + 0.07f * static_cast<float>(i);
        const float ha = scene::tileMotion(a, a.eventsAt(t), x, z, t, 0.0f, 1.0f).x;
        const float hb = scene::tileMotion(b, b.eventsAt(t), x, z, t, 0.0f, 1.0f).x;
        const float hab = scene::tileMotion(both, both.eventsAt(t), x, z, t, 0.0f, 1.0f).x;
        CHECK(hab == Approx(ha + hb).margin(1e-5));
    }
}

TEST_CASE("a ring fades below epsilon and is gone after six decays", "[tiles][dynamics][geography]") {
    scene::TileDynamics d;
    d.rings.amplitude = 3.0f;
    d.rings.decay = 1.5f;
    d.events = {{1.0f, {0.0f, 0.0f}, 1.0f}};
    float peak = 0.0f;
    for (float x = 0.0f; x < 400.0f; x += 0.25f) {
        peak = std::max(peak, std::abs(scene::tileMotion(d, d.eventsAt(1.5f), x, 0.0f, 1.5f, 0.0f, 1.0f).x));
    }
    CHECK(peak > 1.0f); // early on, it is there
    const float late = 1.0f + 5.0f * d.rings.decay;
    float tail = 0.0f;
    for (float x = 0.0f; x < 600.0f; x += 0.25f) {
        tail = std::max(tail, std::abs(scene::tileMotion(d, d.eventsAt(late), x, 0.0f, late, 0.0f, 1.0f).x));
    }
    CHECK(tail < 3.0f * std::exp(-5.0f) + 1e-6f);
    CHECK(d.eventsAt(1.0f + 6.01f * d.rings.decay).empty());
    CHECK(d.eventsAt(0.99f).empty()); // and nothing before it happened: seek back past it and it is not there
}

TEST_CASE("seek equals play on the moving ground", "[tiles][dynamics][geography]") {
    // Every term is a function of t: evaluating a time cold gives what a frame-by-frame run reaches.
    const scene::TileTerrainSpec spec;
    const scene::TileDynamics d = busy();
    float playing = 0.0f;
    for (int f = 0; f <= 300; ++f) {
        playing = scene::tileSampleMoving(spec, d, 12.5f, -7.5f, static_cast<float>(f) / 60.0f, 1.0f).height;
    }
    const float seeked = scene::tileSampleMoving(spec, d, 12.5f, -7.5f, 300.0f / 60.0f, 1.0f).height;
    CHECK(sameBits(playing, seeked));
}

TEST_CASE("the rolling formations drift the hills across the lattice", "[tiles][dynamics][geography]") {
    scene::TileTerrainSpec spec;
    spec.landThreshold = -1.0f; // land everywhere, fully inland: only the hills remain
    spec.regionVariation = 0.0f;
    scene::TileDynamics d;
    d.rolling = {2.0f, -1.0f};
    const float t = 9.0f;
    for (int i = 0; i < 50; ++i) {
        const float x = 5.3f * static_cast<float>(i);
        const float z = -4.1f * static_cast<float>(i);
        const float moved = scene::tileSample(spec, x, z, t, d).height;
        const float source = scene::tileSample(spec, x - 2.0f * t, z + 1.0f * t).height;
        CHECK(moved == Approx(source).margin(1e-4));
    }
}

TEST_CASE("a coarse ring fades out a wave shorter than two of its cells", "[tiles][dynamics][geography]") {
    scene::TileDynamics d;
    scene::TileWave w;
    w.amplitude = 1.0f;
    w.wavelength = 6.0f;
    d.waves = {w};
    float fine = 0.0f;
    float coarse = 0.0f;
    for (float x = 0.0f; x < 12.0f; x += 0.1f) {
        fine = std::max(fine, std::abs(scene::tileMotion(d, {}, x, 0.0f, 0.0f, 0.0f, 1.0f).x));
        coarse = std::max(coarse, std::abs(scene::tileMotion(d, {}, x, 0.0f, 0.0f, 0.0f, 4.0f).x));
    }
    CHECK(fine == Approx(1.0f).margin(0.01));
    CHECK(coarse == 0.0f);
}

TEST_CASE("the crest channel is 0..1 and follows the crests", "[tiles][dynamics][geography]") {
    scene::TileDynamics d;
    scene::TileWave w;
    w.amplitude = 1.0f;
    w.wavelength = 20.0f;
    w.sharpness = 1.0f;
    d.waves = {w, w};
    for (float x = 0.0f; x < 40.0f; x += 0.5f) {
        const glm::vec2 m = scene::tileMotion(d, {}, x, 0.0f, 0.0f, 0.0f, 1.0f);
        CHECK(m.y >= 0.0f);
        CHECK(m.y <= 1.0f);
    }
    // At a crest (sin = 1: x = wavelength / 4) the crest is full; at a trough it is 0.
    CHECK(scene::tileMotion(d, {}, 5.0f, 0.0f, 0.0f, 0.0f, 1.0f).y == Approx(1.0f));
    CHECK(scene::tileMotion(d, {}, 15.0f, 0.0f, 0.0f, 0.0f, 1.0f).y == Approx(0.0f).margin(1e-6));
}

TEST_CASE("no NaN at the extremes the validation allows", "[tiles][dynamics][geography]") {
    const scene::TileTerrainSpec spec;
    scene::TileDynamics d;
    scene::TileWave w;
    w.amplitude = 1e4f;
    w.wavelength = 1e-3f;
    w.speed = 1e5f;
    w.sharpness = 1.0f;
    d.waves = {w, w, w, w};
    d.breathingAmplitude = 4.0f;
    d.breathingPeriod = 1e-3f;
    d.rolling = {1e4f, -1e4f};
    d.rings.amplitude = 1e4f;
    d.rings.width = 1e-3f;
    d.rings.wavelength = 1e-3f;
    d.rings.decay = 1e-3f;
    d.amplitude = 100.0f;
    d.events = {{0.0f, {0.0f, 0.0f}, 1e3f}};
    REQUIRE(scene::validateTileDynamics(d).has_value());
    for (int i = 0; i < 200; ++i) {
        const scene::TileSample s = scene::tileSampleMoving(spec, d, 17.0f * static_cast<float>(i), 3.0f,
                                                            0.013f * static_cast<float>(i), 1.0f);
        CHECK(std::isfinite(s.height));
        CHECK(std::isfinite(s.crest));
    }
    scene::TileDynamics bad;
    bad.waves = {w};
    bad.waves[0].wavelength = 0.0f;
    CHECK_FALSE(scene::validateTileDynamics(bad).has_value());
    bad = {};
    bad.rings.decay = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(scene::validateTileDynamics(bad).has_value());
    bad = {};
    bad.waves.resize(5);
    CHECK_FALSE(scene::validateTileDynamics(bad).has_value());
}

TEST_CASE("the moving ground reads from JSON and round-trips", "[tiles][dynamics][geography]") {
    const auto j = nlohmann::json::parse(R"({
        "amplitude": 0.75,
        "waves": [ {"kind": "planar", "amplitude": 0.6, "wavelength": 30, "speed": 5, "direction": 45, "sharpness": 0.3},
                   {"kind": "circular", "amplitude": 0.4, "wavelength": 18, "speed": -3, "centre": [10, -20], "onLand": 0} ],
        "breathing": {"amplitude": 0.15, "period": 20, "wavelength": 600},
        "rolling": [1.0, 0.5],
        "rings": {"amplitude": 1.5, "speed": 35, "wavelength": 9, "width": 6, "decay": 2, "crest": 0.8},
        "events": [ {"time": 4.5, "position": [12, 34], "strength": 0.9} ]
    })");
    auto d = scene::tileDynamicsFromJson(j);
    REQUIRE(d.has_value());
    CHECK(d->amplitude == 0.75f);
    REQUIRE(d->waves.size() == 2);
    CHECK(d->waves[1].circular);
    CHECK(d->waves[1].centre == glm::vec2(10.0f, -20.0f));
    CHECK(d->waves[1].onLand == 0.0f);
    CHECK(d->breathingPeriod == 20.0f);
    CHECK(d->rolling == glm::vec2(1.0f, 0.5f));
    CHECK(d->rings.decay == 2.0f);
    REQUIRE(d->events.size() == 1);
    CHECK(d->events[0].position == glm::vec2(12.0f, 34.0f));
    auto again = scene::tileDynamicsFromJson(scene::tileDynamicsToJson(*d));
    REQUIRE(again.has_value());
    CHECK(scene::tileDynamicsToJson(*again) == scene::tileDynamicsToJson(*d));
    CHECK_FALSE(scene::tileDynamicsFromJson(nlohmann::json::parse(R"({"wavez": []})")).has_value());
    CHECK_FALSE(scene::tileDynamicsFromJson(nlohmann::json::parse(R"({"waves": [{"amplitde": 1}]})")).has_value());
}

TEST_CASE("the moving ground is a set of parameters and rings fire on a rising edge", "[tiles][dynamics][geography][composition]") {
    const auto json = nlohmann::json::parse(R"({
        "format": "avgen-scene", "version": 1, "name": "moving",
        "camera": {"position": [0, 50, 0], "target": [0, 0, -100]},
        "environment": {"tileDynamics": {
            "waves": [ {"amplitude": 0.5, "wavelength": 30, "speed": 4} ],
            "rings": {"amplitude": 1.0, "decay": 2.0}
        }},
        "nodes": []})");
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto c = scene::Composition::fromJson(json, registry);
    REQUIRE(c.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*c)->attach(params, modulator);
    auto* amp = params.find("env/tiles/wave1/amplitude");
    auto* trigger = params.find("env/tiles/rings/trigger");
    auto* ahead = params.find("env/tiles/rings/ahead");
    REQUIRE(amp != nullptr);
    REQUIRE(trigger != nullptr);
    REQUIRE(ahead != nullptr);
    CHECK(params.find("env/tiles/nonesuch") == nullptr);
    const auto frame = [&](double t) {
        params.resetFinals();
        FrameTime ft{};
        ft.renderTime = t;
        (*c)->update(ft);
        return (*c)->scene().environment.tileDynamics;
    };
    CHECK(frame(0.0).waves.at(0).amplitude == 0.5f);
    // A route (here, the base value) moves the wave.
    amp->setBaseComponent(0, 2.0f);
    ahead->setBaseComponent(0, 120.0f);
    CHECK(frame(0.5).waves.at(0).amplitude == 2.0f);
    CHECK(frame(1.0).live.empty());
    // Rising edge: one ring, 120 m ahead along the camera's heading (-Z), stamped with the time.
    trigger->setBaseComponent(0, 1.0f);
    auto d = frame(1.5);
    REQUIRE(d.live.size() == 1);
    CHECK(d.live[0].time == 1.5f);
    {
        const auto& cam = (*c)->scene().camera;
        const glm::vec2 eye(cam.position.x, cam.position.z);
        const glm::vec2 heading = glm::normalize(glm::vec2(cam.target.x, cam.target.z) - eye);
        const glm::vec2 expected = eye + heading * 120.0f;
        CHECK(d.live[0].position.x == Catch::Approx(expected.x).margin(1e-3));
        CHECK(d.live[0].position.y == Catch::Approx(expected.y).margin(1e-3));
        CHECK(glm::length(d.live[0].position - eye) == Catch::Approx(120.0f));
    }
    // Held high: no second ring. Low then high again: a second.
    CHECK(frame(1.6).live.size() == 1);
    trigger->setBaseComponent(0, 0.0f);
    CHECK(frame(1.7).live.size() == 1);
    trigger->setBaseComponent(0, 1.0f);
    CHECK(frame(2.0).live.size() == 2);
    // Seek back before the second ring: it has not happened, so it is gone; the first stays.
    trigger->setBaseComponent(0, 0.0f);
    d = frame(1.8);
    REQUIRE(d.live.size() == 1);
    CHECK(d.live[0].time == 1.5f);
    // Past six decays (12 s), the first is gone too.
    CHECK(frame(14.0).live.empty());
    // And a save keeps the authored block (without the live rings).
    const auto saved = (*c)->toJson();
    REQUIRE(saved.at("environment").contains("tileDynamics"));
    CHECK(saved.at("environment").at("tileDynamics").at("waves").size() == 1);
    CHECK_FALSE(saved.at("environment").at("tileDynamics").contains("live"));
}
