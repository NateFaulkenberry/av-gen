// ADR-1956: the flight over a tiles ground.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/flight.hpp"
#include "scene/tile_terrain.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>

using namespace avgen;

namespace {

scene::TileTerrainSpec hills() {
    scene::TileTerrainSpec t;
    t.landThreshold = 0.2f; // mostly land, some sea
    t.hillHeight = 30.0f;
    t.hillWavelength = 180.0f;
    return t;
}

scene::TileDynamics swell() {
    scene::TileDynamics d;
    scene::TileWave w;
    w.amplitude = 1.5f;
    w.wavelength = 40.0f;
    w.speed = 8.0f;
    d.waves = {w};
    d.rings.amplitude = 2.0f;
    d.events = {{4.0f, {120.0f, -60.0f}, 1.0f}};
    return d;
}

scene::FlightSpec route() {
    scene::FlightSpec f;
    f.enabled = true;
    f.duration = 40.0f;
    f.speed = 24.0f;
    f.route = {{{0.0f, 0.0f}, 8.0f, -1.0f},
               {{200.0f, -120.0f}, 6.0f, -1.0f},   // low
               {{420.0f, 60.0f}, 60.0f, 30.0f},    // climb to a reveal, faster
               {{600.0f, -200.0f}, 10.0f, -1.0f},  // dive back down
               {{820.0f, 40.0f}, 7.0f, -1.0f}};
    return f;
}

float angleDeg(glm::vec2 a, glm::vec2 b) {
    const float la = glm::length(a);
    const float lb = glm::length(b);
    if (la < 1e-6f || lb < 1e-6f) return 0.0f;
    return glm::degrees(std::acos(std::clamp(glm::dot(a, b) / (la * lb), -1.0f, 1.0f)));
}

} // namespace

TEST_CASE("the flight clears the moving ground at every sample", "[flight][geography]") {
    const auto tiles = hills();
    const auto dyn = swell();
    const auto f = route();
    const scene::FlightGround ground = scene::tileFlightGround(tiles, dyn, 1.0f, 1.0f);
    const scene::FlightTable table = scene::buildFlightTable(f, ground, 0.0f);
    REQUIRE(table.valid());
    REQUIRE(table.eye.size() == static_cast<std::size_t>(f.duration * scene::kFlightRate) + 1);
    float worst = 1e9f;
    for (std::size_t i = 0; i < table.eye.size(); ++i) {
        const float t = static_cast<float>(i) / scene::kFlightRate;
        const glm::vec3 e(table.eye[i]);
        worst = std::min(worst, e.y - ground(e.x, e.z, t));
    }
    CAPTURE(worst);
    CHECK(worst >= f.minClearance - 1e-3f);
}

TEST_CASE("the flight is continuous: position, velocity and bank", "[flight][geography]") {
    const auto f = route();
    const scene::FlightTable table =
        scene::buildFlightTable(f, scene::tileFlightGround(hills(), swell(), 1.0f, 1.0f), 0.0f);
    REQUIRE(table.valid());
    const float dt = 1.0f / scene::kFlightRate;
    float maxSpeed = 0.0f;
    float maxAccel = 0.0f;
    float maxBankRate = 0.0f;
    float maxBank = 0.0f;
    for (std::size_t i = 1; i + 1 < table.eye.size(); ++i) {
        const glm::vec3 v0 = (glm::vec3(table.eye[i]) - glm::vec3(table.eye[i - 1])) / dt;
        const glm::vec3 v1 = (glm::vec3(table.eye[i + 1]) - glm::vec3(table.eye[i])) / dt;
        maxSpeed = std::max(maxSpeed, glm::length(v1));
        maxAccel = std::max(maxAccel, glm::length(v1 - v0) / dt);
        maxBankRate = std::max(maxBankRate, std::abs(table.eye[i + 1].w - table.eye[i].w) / dt);
        maxBank = std::max(maxBank, std::abs(table.eye[i].w));
    }
    CAPTURE(maxSpeed, maxAccel, glm::degrees(maxBankRate), glm::degrees(maxBank));
    CHECK(maxSpeed < 60.0f);                         // no jump anywhere
    CHECK(maxAccel < 30.0f);                         // m/s^2: no kink
    CHECK(glm::degrees(maxBankRate) < 20.0f);        // degrees per second
    CHECK(glm::degrees(maxBank) <= f.bankLimit + 1e-3f);
    CHECK(glm::degrees(maxBank) > 1.0f);             // the control: it does bank through the turns
}

TEST_CASE("the flight looks where it travels", "[flight][geography]") {
    const auto f = route();
    const scene::FlightTable table =
        scene::buildFlightTable(f, scene::tileFlightGround(hills(), swell(), 1.0f, 1.0f), 0.0f);
    float worst = 0.0f;
    float worstPitch = -90.0f;
    float lowestPitch = 90.0f;
    for (std::size_t i = 1; i + 1 < table.eye.size(); ++i) {
        if (table.nadir[i] > 0.0f) continue; // the top-down leg is the one exception
        const glm::vec3 travel = glm::vec3(table.eye[i + 1]) - glm::vec3(table.eye[i - 1]);
        const glm::vec3 a = table.aim[i];
        worst = std::max(worst, angleDeg({a.x, a.z}, {travel.x, travel.z}));
        const float pitch = glm::degrees(std::asin(a.y));
        worstPitch = std::max(worstPitch, pitch);
        lowestPitch = std::min(lowestPitch, pitch);
    }
    CAPTURE(worst, worstPitch, lowestPitch);
    CHECK(worst < 45.0f); // never a head turn: the aim stays within the turn's own lead of the travel
    CHECK(worstPitch <= f.pitchMax + 1e-3f);
    CHECK(lowestPitch >= f.pitchMin - 1e-3f);
}

TEST_CASE("the flight follows the terrain low and holds a reveal high", "[flight][geography]") {
    const auto tiles = hills();
    const auto dyn = swell();
    const scene::FlightGround ground = scene::tileFlightGround(tiles, dyn, 1.0f, 1.0f);
    for (const float altitude : {10.0f, 80.0f}) {
        scene::FlightSpec f;
        f.enabled = true;
        f.duration = 30.0f;
        f.route = {{{0.0f, 0.0f}, altitude, -1.0f}, {{700.0f, 50.0f}, altitude, -1.0f}};
        const scene::FlightTable table = scene::buildFlightTable(f, ground, 0.0f);
        double sum = 0.0;
        float lowest = 1e9f;
        int count = 0;
        for (std::size_t i = 60; i + 60 < table.eye.size(); ++i) {
            const glm::vec3 e(table.eye[i]);
            const float above = e.y - ground(e.x, e.z, static_cast<float>(i) / scene::kFlightRate);
            sum += above;
            lowest = std::min(lowest, above);
            ++count;
        }
        const double mean = sum / count;
        CAPTURE(altitude, mean, lowest);
        CHECK(mean > 0.8 * altitude);           // it holds its altitude over the land
        CHECK(mean < 1.0 * altitude + 15.0);    // and does not drift up off it (the clearance lift is bounded)
        CHECK(lowest >= f.minClearance - 1e-3f);
    }
}

TEST_CASE("the flight is the same flight whatever the tile size", "[flight][geography]") {
    const auto tiles = hills();
    const auto f = route();
    const scene::FlightTable one = scene::buildFlightTable(f, scene::tileFlightGround(tiles, {}, 1.0f, 1.0f), 0.0f);
    const scene::FlightTable four = scene::buildFlightTable(f, scene::tileFlightGround(tiles, {}, 4.0f, 1.0f), 0.0f);
    REQUIRE(one.eye.size() == four.eye.size());
    double dy = 0.0;
    float dxz = 0.0f;
    for (std::size_t i = 0; i < one.eye.size(); ++i) {
        dy += std::abs(one.eye[i].y - four.eye[i].y);
        dxz = std::max(dxz, glm::length(glm::vec2(one.eye[i].x - four.eye[i].x, one.eye[i].z - four.eye[i].z)));
    }
    dy /= static_cast<double>(one.eye.size());
    CAPTURE(dy, dxz);
    CHECK(dxz < 1e-4f);  // the route is in metres: the tiles never move it
    CHECK(dy < 2.5);     // only the ground's quantisation under it differs
    // and the 4 m ground is cleared too
    const scene::FlightGround g4 = scene::tileFlightGround(tiles, {}, 4.0f, 1.0f);
    for (std::size_t i = 0; i < four.eye.size(); ++i) {
        const glm::vec3 e(four.eye[i]);
        CHECK(e.y - g4(e.x, e.z, static_cast<float>(i) / scene::kFlightRate) >= f.minClearance - 1e-3f);
    }
}

TEST_CASE("the flight is deterministic and seek equals play", "[flight][geography]") {
    const auto f = route();
    const auto g = scene::tileFlightGround(hills(), swell(), 1.0f, 1.0f);
    const scene::FlightTable a = scene::buildFlightTable(f, g, 0.0f);
    const scene::FlightTable b = scene::buildFlightTable(f, g, 0.0f);
    REQUIRE(a.eye.size() == b.eye.size());
    for (std::size_t i = 0; i < a.eye.size(); ++i) {
        CHECK(a.eye[i] == b.eye[i]);
    }
    scene::FlightPose playing;
    for (int k = 0; k <= 700; ++k) playing = scene::flightPoseAt(f, a, k / 60.0);
    const scene::FlightPose seeked = scene::flightPoseAt(f, a, 700 / 60.0);
    CHECK(playing.eye == seeked.eye);
    CHECK(playing.target == seeked.target);
    CHECK(playing.rollDegrees == seeked.rollDegrees);
}

TEST_CASE("a scene's flight drives its camera", "[flight][geography][composition]") {
    const auto json = nlohmann::json::parse(R"({
        "format": "avgen-scene", "version": 1, "name": "flown",
        "nodes": [ {"name": "ground", "kind": "procedural", "procedural": {
            "source": {"kind": "box", "size": [0.9, 0.3, 0.9]},
            "distribution": {"kind": "generator", "generator": {"name": "tiles", "version": 1, "cellSize": 1.0,
                             "viewDistance": 40.0, "tiles": {"hillHeight": 20.0}}},
            "slabs": true}} ],
        "flight": {"ground": "ground", "duration": 20, "speed": 20,
                   "route": [ {"position": [0, 0], "altitude": 12}, {"position": [300, 80], "altitude": 30} ]}
    })");
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto c = scene::Composition::fromJson(json, registry);
    REQUIRE(c.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*c)->attach(params, modulator);
    for (const double t : {0.0, 5.0, 12.5}) {
        params.resetFinals();
        FrameTime ft{};
        ft.renderTime = t;
        (*c)->update(ft);
        REQUIRE((*c)->flightTable().valid());
        const scene::FlightPose pose = scene::flightPoseAt((*c)->flight(), (*c)->flightTable(), t);
        const auto& cam = (*c)->scene().camera;
        CHECK(glm::length(cam.position - pose.eye) < 1e-3f);
        CHECK(glm::length(glm::normalize(cam.target - cam.position) - glm::normalize(pose.target - pose.eye)) < 1e-3f);
    }
    const auto saved = (*c)->toJson();
    REQUIRE(saved.contains("flight"));
    CHECK(saved.at("flight").at("route").size() == 2);
}

TEST_CASE("the flight reads from JSON and refuses what it does not know", "[flight][geography]") {
    auto f = scene::flightFromJson(nlohmann::json::parse(R"({"ground": "g", "loop": true, "speed": 30,
        "route": [ {"position": [0, 0], "altitude": 5}, {"position": [10, 0], "altitude": 5, "speed": 12} ]})"));
    REQUIRE(f.has_value());
    CHECK(f->loop);
    CHECK(f->route[1].speed == 12.0f);
    auto again = scene::flightFromJson(scene::flightToJson(*f));
    REQUIRE(again.has_value());
    CHECK(scene::flightToJson(*again) == scene::flightToJson(*f));
    CHECK_FALSE(scene::flightFromJson(nlohmann::json::parse(R"({"route": [ {"position": [0, 0]} ]})")).has_value());
    CHECK_FALSE(scene::flightFromJson(nlohmann::json::parse(
                    R"({"route": [ {"position": [0, 0]}, {"position": [1, 0]} ], "spede": 3})")).has_value());
}

TEST_CASE("a waypoint can follow the path or the river and snap to the shore or a crown", "[flight][geography]") {
    scene::TileTerrainSpec tiles = hills();
    tiles.path = {{0.0f, 0.0f}, {100.0f, -40.0f}, {220.0f, -30.0f}, {300.0f, 60.0f}};
    tiles.rivers = {scene::TileRiver{.name = "brook", .points = {{-50.0f, 200.0f}, {150.0f, 260.0f}}}};
    scene::FlightSpec f;
    f.enabled = true;
    scene::FlightWaypoint along;
    along.follow = "path";
    along.from = 0.25f;
    along.to = 0.9f;
    along.offset = 6.0f;
    along.altitude = 9.0f;
    // a hint 120 m off a real shoreline (found by a scan, so the test does not depend on where the coast falls)
    glm::vec2 coast(0.0f);
    bool found = false;
    for (float x = 0.0f; x < 20000.0f && !found; x += 4.0f) {
        if (scene::tileSample(tiles, x, 37.0f).shore > 0.95f) {
            coast = {x, 37.0f};
            found = true;
        }
    }
    REQUIRE(found);
    scene::FlightWaypoint shore;
    shore.snap = "shore";
    shore.position = coast + glm::vec2(90.0f, 80.0f);
    shore.snapRadius = 300.0f;
    f.route = {along, shore};
    auto r = scene::resolveFlightRoute(f, tiles);
    REQUIRE(r.has_value());
    REQUIRE(r->route.size() > 5);
    // every follow point is 6 m from the path (to its right), and none of them follows anything any more
    std::vector<float> arc(tiles.path.size(), 0.0f);
    for (std::size_t i = 1; i < arc.size(); ++i) arc[i] = arc[i - 1] + glm::length(tiles.path[i] - tiles.path[i - 1]);
    const auto distToPath = [&](glm::vec2 p) {
        float best = 1e9f;
        for (std::size_t i = 0; i + 1 < tiles.path.size(); ++i) {
            const glm::vec2 a = tiles.path[i];
            const glm::vec2 ab = tiles.path[i + 1] - a;
            const float t = std::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
            best = std::min(best, glm::length(p - (a + ab * t)));
        }
        return best;
    };
    for (std::size_t i = 0; i + 1 < r->route.size(); ++i) {
        CHECK(r->route[i].follow.empty());
        CHECK(distToPath(r->route[i].position) == Catch::Approx(6.0f).margin(1.0f)); // nearer at the corners
        CHECK(r->route[i].altitude == 9.0f);
    }
    // the snapped waypoint sits on the shoreline
    const scene::FlightWaypoint& s = r->route.back();
    CHECK(s.snap.empty());
    CHECK(scene::tileSample(tiles, s.position.x, s.position.y).shore > 0.9f);
    // a follow leg on a ground with no river is refused
    scene::TileTerrainSpec dry = tiles;
    dry.rivers.clear();
    scene::FlightSpec g = f;
    g.route[0].follow = "river";
    CHECK_FALSE(scene::resolveFlightRoute(g, dry).has_value());
    // ADR-1965: a river by name or index; a river that is not there is refused
    g.route[0].follow = "brook";
    CHECK(scene::resolveFlightRoute(g, tiles).has_value());
    g.route[0].follow = "river0";
    CHECK(scene::resolveFlightRoute(g, tiles).has_value());
    g.route[0].follow = "river2";
    CHECK_FALSE(scene::resolveFlightRoute(g, tiles).has_value());
}

TEST_CASE("the top-down leg looks straight down with no flip, then looks ahead again", "[flight][geography]") {
    auto f = route();
    f.route[2].nadir = 4.0f;
    const auto g = scene::tileFlightGround(hills(), {}, 1.0f, 1.0f);
    const scene::FlightTable table = scene::buildFlightTable(f, g, 0.0f);
    REQUIRE(table.valid());
    // the deepest sample of the leg looks straight down
    std::size_t deepest = 0;
    for (std::size_t i = 0; i < table.nadir.size(); ++i) {
        if (table.nadir[i] > table.nadir[deepest]) deepest = i;
    }
    CHECK(table.nadir[deepest] == Catch::Approx(1.0f));
    CHECK(table.aim[deepest].y == Catch::Approx(-1.0f).margin(1e-4));
    // the leg lasts its 4 s (the eased part included) and nothing else is touched
    int inLeg = 0;
    for (const float w : table.nadir) inLeg += w > 0.0f ? 1 : 0;
    CHECK(inLeg <= static_cast<int>(4.0f * scene::kFlightRate) + 1);
    CHECK(inLeg >= static_cast<int>(3.0f * scene::kFlightRate));
    // screen-up is continuous through it (no flip at -90 degrees), and at the nadir it is the travel direction
    scene::FlightPose prev = scene::flightPoseAt(f, table, 0.0);
    float worst = 1.0f;
    for (int k = 1; k < static_cast<int>(table.eye.size()); ++k) {
        const scene::FlightPose p = scene::flightPoseAt(f, table, k / scene::kFlightRate);
        worst = std::min(worst, glm::dot(p.up, prev.up));
        CHECK(std::abs(glm::dot(p.up, glm::normalize(p.target - p.eye))) < 1e-3f); // up is perpendicular to the aim
        prev = p;
    }
    CAPTURE(worst);
    CHECK(worst > 0.99f);
    const scene::FlightPose down = scene::flightPoseAt(f, table, deepest / scene::kFlightRate);
    const glm::vec2 travel = glm::normalize(glm::vec2(table.eye[deepest + 1].x - table.eye[deepest - 1].x,
                                                      table.eye[deepest + 1].z - table.eye[deepest - 1].z));
    CHECK(glm::dot(glm::vec2(down.up.x, down.up.z), travel) > 0.9f);
    CHECK(std::abs(table.eye[deepest].w) < 1e-4f); // no bank at the nadir
}
