// ADR-1662: a surface wave that travels along a terrain feature's polyline (a river's course). The CPU half:
// the path geometry, where the front is and which way it goes, the chain the GPU receives (it must lie on the
// polyline round a bend), the packing, the JSON, and that every other wave packs exactly as it did.
// tests/rendering/test_wave_path_gpu.cpp has the pixels.
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/wave_effect.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

// An L: 1 km east along x, then 1 km along +z. And a closed square of 4 km.
const std::vector<glm::vec3> kL{{0.0f, 0.0f, 0.0f}, {1000.0f, 0.0f, 0.0f}, {1000.0f, 0.0f, 1000.0f}};
const std::vector<glm::vec3> kSquare{{0.0f, 0.0f, 0.0f},       {1000.0f, 0.0f, 0.0f}, {1000.0f, 0.0f, 1000.0f},
                                     {0.0f, 0.0f, 1000.0f},    {0.0f, 0.0f, 0.0f}};

class PathScene final : public world::EffectSceneQuery {
public:
    [[nodiscard]] bool nodePosition(std::string_view, glm::vec3&) const override { return false; }
    [[nodiscard]] bool featurePath(std::string_view node, std::string_view feature, std::span<const glm::vec3>& points,
                                   glm::mat4& transform) const override {
        if (node != "river") {
            return false;
        }
        if (feature == "L") {
            points = kL;
        } else if (feature == "square") {
            points = kSquare;
        } else {
            return false;
        }
        transform = glm::mat4(1.0f);
        return true;
    }
};

// Distance from p (xz) to the polyline.
float offPolyline(const std::vector<glm::vec3>& line, glm::vec2 p) {
    float best = std::numeric_limits<float>::max();
    for (std::size_t i = 1; i < line.size(); ++i) {
        const glm::vec2 a{line[i - 1].x, line[i - 1].z};
        const glm::vec2 b{line[i].x, line[i].z};
        const float t = std::clamp(glm::dot(p - a, b - a) / glm::dot(b - a, b - a), 0.0f, 1.0f);
        best = std::min(best, glm::distance(p, a + (b - a) * t));
    }
    return best;
}

world::EffectInstance pathWave(const char* feature = "L") {
    world::EffectInstance e = world::makeEffect(world::EffectKind::GroundPulse, "beam");
    e.activation = world::Activation::Always;
    e.timing = world::Timing{};
    e.wave.source.kind = world::SourceKind::Camera;
    e.wave.source.groundOffset = 65.0f;
    e.wave.propagation.kind = world::PropagationKind::PathWave;
    e.wave.propagation.path.node = "river";
    e.wave.propagation.path.feature = feature;
    e.wave.propagation.path.ease = 0.0f;
    e.wave.propagation.path.wakeGrowth = 0.0f;
    e.wave.propagation.speed = 1000.0f;
    e.wave.propagation.range = 1500.0f;
    e.wave.propagation.startOffset = 100.0f;
    e.wave.propagation.trailLength = 500.0f;
    e.wave.propagation.frontWidth = 10.0f;
    e.wave.appearance.width = 1.0f;
    return e;
}

world::EffectContext at(double t, const PathScene& scene, glm::vec3 camera = {0.0f, 65.0f, 0.0f},
                        glm::vec3 forward = {1.0f, 0.0f, 0.0f}) {
    world::EffectContext ctx;
    ctx.seconds = t;
    ctx.cameraPosition = camera;
    ctx.cameraForward = forward;
    ctx.cameraTarget = camera + forward;
    ctx.scene = &scene;
    return ctx;
}

} // namespace

TEST_CASE("ADR-1662: path geometry projects, walks, wraps and clamps", "[world][effects][waves][adr1662]") {
    world::PathGeometry open{kL};
    CHECK_FALSE(open.closed());
    CHECK_THAT(open.length(), WithinAbs(2000.0, 1e-3));
    glm::vec2 tan{0.0f};
    CHECK_THAT(open.arcOf({400.0f, 50.0f, 30.0f}, &tan), WithinAbs(400.0, 1e-3));
    CHECK(tan == glm::vec2(1.0f, 0.0f));
    CHECK_THAT(open.arcOf({1020.0f, 0.0f, 700.0f}, &tan), WithinAbs(1700.0, 1e-3));
    CHECK(tan == glm::vec2(0.0f, 1.0f));
    CHECK(open.pointAt(1250.0f) == glm::vec2(1000.0f, 250.0f));
    CHECK(open.pointAt(-50.0f) == glm::vec2(0.0f, 0.0f));          // an open path clamps
    CHECK(open.pointAt(2500.0f) == glm::vec2(1000.0f, 1000.0f));

    world::PathGeometry loop{kSquare};
    CHECK(loop.closed());
    CHECK_THAT(loop.length(), WithinAbs(4000.0, 1e-3));
    CHECK(loop.pointAt(4100.0f) == glm::vec2(100.0f, 0.0f));       // a closed path wraps, both ways
    CHECK(loop.pointAt(-100.0f) == glm::vec2(0.0f, 100.0f));
    // a transform moves the path
    world::PathGeometry moved{kL, glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, -7.0f))};
    CHECK(moved.pointAt(0.0f) == glm::vec2(5.0f, -7.0f));
}

TEST_CASE("ADR-1662: the front travels along the path away from the camera, and its chain follows the bend",
          "[world][effects][waves][adr1662]") {
    const PathScene scene;
    const world::EffectInstance beam = pathWave();
    REQUIRE(beam.wave.validate().has_value());

    SECTION("on the first leg: start offset + speed x time, on the polyline, the tangent downstream") {
        const auto r = world::resolveWave(beam, at(0.5, scene));
        REQUIRE(r.has_value());
        CHECK_THAT(r->frontDistance, WithinAbs(600.0, 1e-2));
        CHECK_THAT(r->origin.x, WithinAbs(600.0, 1e-2));
        CHECK_THAT(r->origin.z, WithinAbs(0.0, 1e-2));
        CHECK_THAT(r->origin.y, WithinAbs(0.0, 1e-3)); // the source's height: the camera dropped to the water
        CHECK(r->axis == glm::vec3(1.0f, 0.0f, 0.0f));
    }
    SECTION("past the bend: the front and every chain point lie on the path, round the corner") {
        const auto r = world::resolveWave(beam, at(1.2, scene)); // 100 + 1000 x 1.2 = 1300 m: 300 m up the second leg
        REQUIRE(r.has_value());
        CHECK_THAT(r->origin.x, WithinAbs(1000.0, 1e-2));
        CHECK_THAT(r->origin.z, WithinAbs(300.0, 1e-2));
        CHECK(r->axis == glm::vec3(0.0f, 0.0f, 1.0f));
        CHECK_THAT(r->wake, WithinAbs(500.0, 1e-3));
        bool onFirstLeg = false;
        for (const glm::vec2& p : r->path) {
            INFO("chain point " << p.x << ", " << p.y);
            CHECK(offPolyline(kL, p) < 1e-2f);
            onFirstLeg = onFirstLeg || (p.y < 1e-2f && p.x < 999.0f);
        }
        CHECK(onFirstLeg); // the wake reaches back round the bend (front at 1300 m, wake 500 m)
        CHECK(r->path[1] == glm::vec2(1000.0f, 300.0f)); // point 1 is the front
        CHECK(r->path[0].y > 300.0f);                    // point 0 just ahead of it
        CHECK(r->path[7] == glm::vec2(800.0f, 0.0f));    // point 7 the wake's end, 500 m back
        // a straight-axis beam would have put the front at (1300, 0): the path wave is 300 m from there
        CHECK(glm::distance(glm::vec2(r->origin.x, r->origin.z), glm::vec2(1300.0f, 0.0f)) > 400.0f);
    }
    SECTION("the front goes the way the camera faces") {
        const auto r = world::resolveWave(beam, at(0.3, scene, {1000.0f, 65.0f, 900.0f}, {0.0f, 0.0f, -1.0f}));
        REQUIRE(r.has_value());
        CHECK_THAT(r->origin.x, WithinAbs(1000.0, 1e-2));
        CHECK_THAT(r->origin.z, WithinAbs(900.0 - 400.0, 1e-2));
        CHECK(r->axis == glm::vec3(0.0f, 0.0f, -1.0f));
    }
    SECTION("on a closed path the front wraps past the start") {
        const world::EffectInstance loop = pathWave("square");
        const auto r = world::resolveWave(loop, at(0.4, scene, {0.0f, 65.0f, 500.0f}, {0.0f, 0.0f, -1.0f}));
        REQUIRE(r.has_value());
        // the camera projects to arc 3500 (the fourth side, heading -z); 500 m on is 100 m along the first side
        CHECK_THAT(r->origin.x, WithinAbs(0.0, 1e-2));
        CHECK_THAT(r->origin.z, WithinAbs(0.0, 1e-2));
        const auto r2 = world::resolveWave(loop, at(0.9, scene, {0.0f, 65.0f, 500.0f}, {0.0f, 0.0f, -1.0f}));
        REQUIRE(r2.has_value());
        CHECK_THAT(r2->origin.x, WithinAbs(500.0, 1e-2));
        CHECK_THAT(r2->origin.z, WithinAbs(0.0, 1e-2));
    }
    SECTION("ease: fast near the camera, slower far, then it holds while the fade takes it") {
        world::EffectInstance eased = pathWave();
        eased.wave.propagation.path.ease = 1.0f;
        const auto mid = world::resolveWave(eased, at(0.75, scene)); // a = 0.5 -> 1 - 0.25 = 0.75 of the range
        REQUIRE(mid.has_value());
        CHECK_THAT(mid->frontDistance, WithinAbs(100.0 + 1500.0 * 0.75, 1e-2));
        CHECK_THAT(mid->frontSpeed, WithinAbs(1000.0, 1e-2)); // 2 x mean x (1 - a)
        CHECK_THAT(mid->meanSpeed, WithinAbs(1000.0, 1e-2));
        const auto late = world::resolveWave(eased, at(3.0, scene));
        REQUIRE(late.has_value());
        CHECK_THAT(late->frontDistance, WithinAbs(1600.0, 1e-2));
        CHECK(late->frontSpeed == 0.0f);
    }
    SECTION("the wake grows with distance travelled") {
        world::EffectInstance grow = pathWave();
        grow.wave.propagation.path.wakeGrowth = 0.25f;
        const auto r = world::resolveWave(grow, at(0.5, scene));
        REQUIRE(r.has_value());
        CHECK_THAT(r->wake, WithinAbs(500.0 + 0.25 * 600.0, 1e-2));
    }
    SECTION("resolution is a pure function of the second (seek equals play)") {
        const auto a = world::resolveWave(beam, at(1.2, scene));
        (void)world::resolveWave(beam, at(0.1, scene));
        const auto b = world::resolveWave(beam, at(1.2, scene));
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        const world::WaveGpu ga = world::packWave(*a);
        const world::WaveGpu gb = world::packWave(*b);
        CHECK(std::memcmp(&ga, &gb, sizeof(ga)) == 0);
    }
    SECTION("no such path: inactive, not an error") {
        world::EffectInstance lost = pathWave("nowhere");
        CHECK(lost.wave.validate().has_value());
        CHECK_FALSE(world::resolveWave(lost, at(0.5, scene)).has_value());
        world::EffectInstance unnamed = pathWave();
        unnamed.wave.propagation.path.node.clear();
        CHECK(unnamed.wave.validate().has_value());
        CHECK_FALSE(world::resolveWave(unnamed, at(0.5, scene)).has_value());
        world::EffectContext noScene = at(0.5, scene);
        noScene.scene = nullptr;
        CHECK_FALSE(world::resolveWave(beam, noScene).has_value());
    }
}

TEST_CASE("ADR-1662: a path wave packs its chain and shape; every other wave packs as before",
          "[world][effects][waves][gpu][adr1662]") {
    const PathScene scene;
    world::EffectInstance beam = pathWave();
    world::PathShape& q = beam.wave.propagation.path;
    q.coreWidth = 6.0f;
    q.coreOffset = 10.0f;
    q.haloWidth = 35.0f;
    q.reach = 260.0f;
    q.reachHeight = 90.0f;
    q.land = 0.5f;
    q.bed = 1.25f;
    q.embed = 0.625f;
    q.disturb = 0.04f;
    q.nearFade = 300.0f;
    beam.wave.propagation.verticalExtent = 25.0f;
    beam.wave.appearance.color = glm::vec3(1.0f, 0.2f, 0.6f);
    beam.wave.appearance.intensity = 2.0f;
    const auto r = world::resolveWave(beam, at(1.2, scene));
    REQUIRE(r.has_value());
    const world::WaveGpu g = world::packWave(*r);
    CHECK(g.originKind == glm::vec4(1000.0f, 0.0f, 300.0f, 2.0f));
    CHECK(g.axisFront == glm::vec4(0.0f, 0.0f, 1.0f, 1300.0f));
    CHECK(g.shape == glm::vec4(10.0f, 500.0f, 300.0f, beam.wave.propagation.falloff));
    CHECK_THAT(g.vertical.x, WithinAbs(1.2, 1e-6)); // ADR-1663: seconds since launch
    CHECK(glm::vec3(g.vertical.y, g.vertical.z, g.vertical.w) == glm::vec3(90.0f, 260.0f, 35.0f));
    CHECK(g.color == glm::vec4(2.0f, 0.4f, 1.2f, 6.0f));
    CHECK(g.edge.w == 10.0f);
    CHECK(g.rainbow.x == 0.625f);
    CHECK_THAT(g.rainbow.y, WithinAbs(0.04, 1e-6));
    CHECK(g.rainbow.z == 0.5f);
    CHECK(g.rainbow.w == 1.25f);
    CHECK(g.sparkle == glm::vec4(0.0f, 1000.0f, 1000.0f, 0.0f));
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(g.path[i] == glm::vec4(r->path[2 * i], r->path[2 * i + 1]));
    }

    // The off half: a directional and a radial wave leave the chain zero, and their nine lanes are what they
    // always were (the GPU test holds a frame with them to the same hash with path waves beside them).
    for (const world::PropagationKind kind : {world::PropagationKind::DirectionalWave, world::PropagationKind::RadialWave}) {
        world::EffectInstance other = world::makeEffect(world::EffectKind::GroundPulse, "other");
        other.wave.propagation.kind = kind;
        world::ResolvedWave rw;
        rw.effect = &other;
        rw.origin = {1.0f, 2.0f, 3.0f};
        rw.axis = {0.0f, 0.0f, -1.0f};
        rw.frontDistance = 12.0f;
        rw.envelope = 0.5f;
        rw.elapsed = 1.5;
        rw.color = other.wave.appearance.color;
        const world::WaveGpu og = world::packWave(rw);
        for (const glm::vec4& v : og.path) {
            CHECK(v == glm::vec4(0.0f));
        }
        CHECK(og.originKind.w == static_cast<float>(kind));
        CHECK(og.shape.z == other.wave.propagation.range);
        CHECK(og.vertical.w == other.wave.propagation.beamRadius);
        CHECK(og.rainbow.x == other.wave.appearance.rainbowScale);
    }
}

TEST_CASE("ADR-1662: a path wave's JSON carries its path; no other wave's file changes",
          "[world][effects][waves][json][adr1662]") {
    const world::EffectInstance plain = world::makeEffect(world::EffectKind::GroundPulse, "plain");
    const nlohmann::json pj = plain.toJson();
    const nlohmann::json& prop = pj.at("parameters").at("propagation");
    CHECK_FALSE(prop.contains("path")); // written only by a path wave (EffectField::written)
    CHECK(prop.at("kind") == "radial");

    world::EffectInstance beam = pathWave();
    beam.wave.propagation.path.coreOffset = 10.0f;
    beam.wave.propagation.path.disturb = 0.04f;
    const nlohmann::json bj = beam.toJson();
    const nlohmann::json& bp = bj.at("parameters").at("propagation");
    CHECK(bp.at("kind") == "path");
    CHECK(bp.at("path").at("node") == "river");
    CHECK(bp.at("path").at("feature") == "L");
    CHECK(bp.at("path").at("coreOffset").get<float>() == 10.0f);
    const auto back = world::EffectInstance::fromJson(bj);
    REQUIRE(back.has_value());
    CHECK(back->wave.propagation.kind == world::PropagationKind::PathWave);
    CHECK(back->wave.propagation.path.node == "river");
    CHECK(back->wave.propagation.path.feature == "L");
    CHECK(back->wave.propagation.path.coreOffset == 10.0f);
    CHECK(back->wave.propagation.path.disturb == 0.04f);
    CHECK(back->toJson() == bj);
}

TEST_CASE("ADR-1663: a path wave runs on its lane, each hit keeps its jittered lane, and packs its splash",
          "[world][effects][waves][adr1663]") {
    const PathScene scene;
    world::EffectInstance beam = pathWave();
    beam.wave.propagation.path.lane = 40.0f;
    beam.wave.propagation.path.splash = 0.3f;
    beam.wave.propagation.path.nearWake = 3.0f;
    // on the first leg, downstream is +x: its right (forward x up, the engine's convention) is +z
    const auto r = world::resolveWave(beam, at(0.5, scene));
    REQUIRE(r.has_value());
    CHECK_THAT(r->origin.x, WithinAbs(600.0, 1e-2));
    CHECK_THAT(r->origin.z, WithinAbs(40.0, 1e-2));
    for (const glm::vec2& p : r->path) {
        CHECK_THAT(offPolyline(kL, p), WithinAbs(40.0, 0.5)); // every chain point 40 m off the centreline
    }
    CHECK(r->launch == glm::vec2(100.0f, 40.0f));       // the start offset, on the lane
    const world::WaveGpu g = world::packWave(*r);
    CHECK(g.extra == glm::vec4(100.0f, 40.0f, 0.3f, 3.0f));
    // past the bend (downstream +z), right is -x
    const auto r2 = world::resolveWave(beam, at(1.2, scene));
    REQUIRE(r2.has_value());
    CHECK_THAT(r2->origin.x, WithinAbs(960.0, 1e-2));
    // a jittered lane: fixed per hit (the launch second), within the jitter, and seek equals play
    world::EffectInstance jit = pathWave();
    jit.wave.propagation.path.laneJitter = 60.0f;
    const auto j1 = world::resolveWave(jit, at(0.5, scene));
    const auto j2 = world::resolveWave(jit, at(0.9, scene));
    REQUIRE(j1.has_value());
    REQUIRE(j2.has_value());
    CHECK(std::abs(j1->origin.z) <= 60.0f);
    CHECK_THAT(j1->origin.z, WithinAbs(j2->origin.z, 1e-3)); // the same hit stays on its lane
    // other kinds leave `extra` zero
    world::EffectInstance ring = world::makeEffect(world::EffectKind::GroundPulse, "ring");
    world::ResolvedWave rw;
    rw.effect = &ring;
    CHECK(world::packWave(rw).extra == glm::vec4(0.0f));
}

TEST_CASE("ADR-1663: a light ball flies its camera-relative arc and packs its light and core",
          "[world][effects][waves][adr1663]") {
    const PathScene scene;
    world::EffectInstance ball = world::makeEffect(world::EffectKind::GroundPulse, "ball");
    ball.activation = world::Activation::Always;
    ball.timing = world::Timing{};
    ball.timing.fadeIn = 0.0;
    ball.timing.fadeOut = 0.0;
    ball.wave.source.kind = world::SourceKind::Camera;
    ball.wave.propagation.kind = world::PropagationKind::LightBall;
    ball.wave.propagation.path.ease = 0.0f;
    world::BallShape& b = ball.wave.propagation.ball;
    b.launch = {-70.0f, -60.0f, 200.0f};
    b.target = {50.0f, 140.0f, 2200.0f};
    b.travel = 1.0f;
    b.arc = 100.0f;
    b.lightRadius = 250.0f;
    b.range = 1200.0f;
    b.coreSize = 8.0f;
    b.haloSize = 50.0f;
    ball.wave.appearance.color = {1.0f, 0.5f, 0.8f};
    ball.wave.appearance.intensity = 6.0f;
    ball.wave.appearance.edgeColor = {1.0f, 1.0f, 1.0f};
    ball.wave.appearance.edgeIntensity = 30.0f;
    REQUIRE(ball.wave.validate().has_value());
    // the camera at the origin looking down -z: right is +x
    const glm::vec3 cam{0.0f, 65.0f, 0.0f};
    const glm::vec3 fwd{0.0f, 0.0f, -1.0f};
    const auto start = world::resolveWave(ball, at(1.0e-4, scene, cam, fwd));
    REQUIRE(start.has_value());
    CHECK_THAT(start->origin.x, WithinAbs(-70.0, 0.05));
    CHECK_THAT(start->origin.y, WithinAbs(5.0, 0.1));
    CHECK_THAT(start->origin.z, WithinAbs(-200.0, 0.3));
    const auto mid = world::resolveWave(ball, at(0.5, scene, cam, fwd));
    REQUIRE(mid.has_value());
    CHECK_THAT(mid->origin.y, WithinAbs(65.0 + 40.0 + 100.0, 1e-2)); // the chord's midpoint, bowed up 100 m
    CHECK_THAT(mid->origin.z, WithinAbs(-1200.0, 1e-2));
    const auto end = world::resolveWave(ball, at(3.0, scene, cam, fwd));
    REQUIRE(end.has_value());
    CHECK_THAT(end->origin.x, WithinAbs(50.0, 1e-3)); // holds at the target
    const world::WaveGpu g = world::packWave(*mid);
    CHECK(g.originKind.w == 3.0f);
    CHECK(g.color == glm::vec4(6.0f, 3.0f, 4.8f, 250.0f));
    CHECK(g.edge == glm::vec4(30.0f, 30.0f, 30.0f, 8.0f));
    CHECK(g.shape == glm::vec4(50.0f, 1200.0f, 0.0f, 0.0f));
    CHECK(g.rainbow == glm::vec4(0.0f, 0.0f, 0.0f, 80.0f)); // no corona: the plain ball
    CHECK(g.vertical.x == 0.0f);
    // the fireball's corona, flicker and trail (pass 4) pack in their lanes
    world::EffectInstance fire = ball;
    fire.wave.propagation.ball.coronaColor = {1.0f, 0.4f, 0.1f};
    fire.wave.propagation.ball.coronaIntensity = 2.0f;
    fire.wave.propagation.ball.coronaSize = 55.0f;
    fire.wave.propagation.ball.flicker = 0.8f;
    fire.wave.propagation.ball.trail = 150.0f;
    const auto fm = world::resolveWave(fire, at(0.5, scene, cam, fwd));
    REQUIRE(fm.has_value());
    const world::WaveGpu fg = world::packWave(*fm);
    CHECK(fg.rainbow == glm::vec4(2.0f, 0.8f, 0.2f, 55.0f));
    CHECK(fg.vertical.x == 150.0f);
    CHECK(fg.vertical.y == 0.8f);
    for (const glm::vec4& v : g.path) {
        CHECK(v == glm::vec4(0.0f));
    }
    // JSON: the flight round-trips; a ball writes no path rows and a path wave no ball rows
    const nlohmann::json j = ball.toJson();
    CHECK(j.at("parameters").at("propagation").at("kind") == "ball");
    CHECK_FALSE(j.at("parameters").at("propagation").contains("path"));
    const auto back = world::EffectInstance::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->wave.propagation.ball.target == b.target);
    CHECK(back->wave.propagation.ball.arc == 100.0f);
    CHECK_FALSE(pathWave().toJson().at("parameters").at("propagation").contains("ball"));
}

TEST_CASE("ADR-1665: a fireball's look packs its smoke, embers and seed; look 0 packs byte for byte as ADR-1663's",
          "[world][effects][waves][adr1665]") {
    const glm::vec3 cam{0.0f, 10.0f, 0.0f};
    const glm::vec3 fwd{0.0f, 0.0f, -1.0f};
    const PathScene scene;
    world::EffectInstance ball = world::makeEffect(world::EffectKind::GroundPulse, "ball");
    ball.activation = world::Activation::Always;
    ball.timing = world::Timing{};
    ball.timing.fadeIn = 0.0;
    ball.timing.fadeOut = 0.0;
    ball.wave.source.kind = world::SourceKind::Camera;
    ball.wave.propagation.kind = world::PropagationKind::LightBall;
    ball.wave.propagation.ball.coronaIntensity = 2.0f;
    ball.wave.propagation.ball.trail = 150.0f;
    REQUIRE(ball.wave.validate().has_value());
    const auto plain = world::resolveWave(ball, at(0.5, scene, cam, fwd));
    REQUIRE(plain.has_value());
    const world::WaveGpu g0 = world::packWave(*plain);
    CHECK(g0.extra == glm::vec4(0.0f));
    // look 0 with smoke and embers set: they are ignored, every byte as before
    world::EffectInstance off = ball;
    off.wave.propagation.ball.smoke = 0.7f;
    off.wave.propagation.ball.embers = 0.8f;
    const world::WaveGpu g1 = world::packWave(*world::resolveWave(off, at(0.5, scene, cam, fwd)));
    CHECK(std::memcmp(&g0, &g1, sizeof(world::WaveGpu)) == 0);
    // and nothing new in its file
    const nlohmann::json j = off.toJson();
    const nlohmann::json& bj = j.at("parameters").at("propagation").at("ball");
    CHECK_FALSE(bj.contains("look"));
    // look 1: the look, the smoke (with the envelope), the embers and a seed from the launch second
    world::EffectInstance fire = off;
    fire.wave.propagation.ball.look = 1;
    REQUIRE(fire.wave.validate().has_value());
    const world::WaveGpu g2 = world::packWave(*world::resolveWave(fire, at(0.5, scene, cam, fwd)));
    CHECK(g2.extra.x == 1.0f);
    CHECK(g2.extra.y == 0.7f);
    CHECK(g2.extra.z == 0.8f);
    const nlohmann::json fj = fire.toJson();
    const nlohmann::json& fb = fj.at("parameters").at("propagation").at("ball");
    CHECK(fb.at("look") == 1.0);
    const auto back = world::EffectInstance::fromJson(fj);
    REQUIRE(back.has_value());
    CHECK(back->wave.propagation.ball.look == 1);
    CHECK(back->wave.propagation.ball.smoke == 0.7f);
    CHECK(back->wave.propagation.ball.embers == 0.8f);
    // out of range is refused
    fire.wave.propagation.ball.look = 3;
    CHECK_FALSE(fire.wave.validate().has_value());
    fire.wave.propagation.ball.look = 2;
    fire.wave.propagation.ball.smoke = 1.5f;
    CHECK_FALSE(fire.wave.validate().has_value());
}
