// ADR-2007/2008 (CPD-R12): beach balls on the tiles. A light ball bounces on the drawn tops, takes their motion, never
// settles, and a seek puts it where a play does; its impacts spring the tile without the aliens' landing glow and reach
// the renderer as the balls' own impact list; the look's parameters round-trip through the scene format.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "scene/composition.hpp"
#include "scene/tile_ground.hpp"
#include "scene/tile_terrain.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace avgen;

namespace {

// A moving tiles ground (two trains, no audio) and three balls placed around the origin.
nlohmann::json ballScene() {
    nlohmann::json j = nlohmann::json::parse(R"({
        "format": "avgen-scene", "version": 1, "name": "beach-balls",
        "camera": {"position": [0, 30, 40], "target": [0, 0, 0]},
        "environment": {"tileDynamics": {
            "amplitude": 1.0,
            "waves": [ {"amplitude": 1.2, "wavelength": 36, "speed": 6, "onLand": 1},
                       {"amplitude": 0.5, "wavelength": 22, "speed": 4, "direction": 70, "onLand": 1} ],
            "balls": {"glowColor": [0.2, 0.9, 1.0], "sparkGain": 12.0}
        }},
        "nodes": [
            {"name": "ground", "kind": "procedural", "position": [2, 0, -1],
             "procedural": {"source": {"kind": "box", "size": [3.66, 1.2, 3.66]},
                "distribution": {"kind": "generator", "generator": {"name": "tiles", "version": 1,
                    "cellSize": 4.0, "viewDistance": 120.0, "tiles": {"seed": 3, "landThreshold": 0.2}}},
                "slabs": true}},
            {"name": "b0", "kind": "orb", "position": [4, 6, 2], "scale": [0.6, 0.6, 0.6]},
            {"name": "b1", "kind": "orb", "position": [-12, 8, 5], "scale": [0.6, 0.6, 0.6]},
            {"name": "b2", "kind": "orb", "position": [10, 5, -14], "scale": [0.6, 0.6, 0.6]}
        ],
        "entities": [
            {"name": "b0", "node": "b0", "seed": 1, "behaviors": [{"kind": "ball", "velocity": [3, 4, 1]}]},
            {"name": "b1", "node": "b1", "seed": 2, "behaviors": [{"kind": "ball", "velocity": [-2, 2, 3]}]},
            {"name": "b2", "node": "b2", "seed": 3, "behaviors": [{"kind": "ball", "velocity": [1, 6, -2]}]}
        ]})");
    return j;
}

struct Rig {
    app::Engine engine{app::EngineMode::Offline};
    Rig() { REQUIRE(engine.setCompositionJson(ballScene()).has_value()); }
    void frame(double t, double dt, std::uint64_t k) { engine.update(FrameTime{t, dt, k}); }
    void playTo(double seconds, double fps) {
        const auto last = static_cast<long long>(std::llround(seconds * fps));
        for (long long k = 0; k <= last; ++k) {
            frame(static_cast<double>(k) / fps, k == 0 ? 0.0 : 1.0 / fps, static_cast<std::uint64_t>(k));
        }
    }
    [[nodiscard]] scene::Composition& comp() { return *engine.composition(); }
    [[nodiscard]] std::vector<glm::vec3> balls() {
        std::vector<glm::vec3> out;
        for (const auto& e : comp().entityWorld().entities()) out.push_back(e->state().position());
        return out;
    }
};

} // namespace

TEST_CASE("a beach ball bounces on the drawn tiles, never settles, and a seek lands it where a play does",
          "[tiles][geography][seek][balls][adr2007]") {
    Rig play;
    const double step = 1.0 / 60.0;
    int below = 0;
    std::vector<float> high(3, 0.0f); // the highest each got above the ground in the last 4 s
    std::vector<int> bounces(3, 0);
    std::vector<float> lastY(3, 0.0f);
    std::vector<float> lastDy(3, 0.0f);
    for (long long k = 0; k <= 16 * 60; ++k) {
        const double t = static_cast<double>(k) * step;
        play.frame(t, k == 0 ? 0.0 : step, static_cast<std::uint64_t>(k));
        const world::GroundSource* g = play.comp().entityWorld().ground();
        REQUIRE(g != nullptr);
        const auto ps = play.balls();
        for (std::size_t i = 0; i < ps.size(); ++i) {
            const float above = ps[i].y - g->at(ps[i].x, ps[i].z, t).height;
            below += above < 0.6f - 0.05f ? 1 : 0; // never inside the tile (radius 0.6, the drawn top)
            if (t >= 12.0) high[i] = std::max(high[i], above);
            const float dy = ps[i].y - lastY[i];
            if (k > 1 && lastDy[i] < 0.0f && dy > 0.0f) ++bounces[i];
            lastDy[i] = dy;
            lastY[i] = ps[i].y;
        }
    }
    CAPTURE(below, high[0], high[1], high[2], bounces[0], bounces[1], bounces[2]);
    CHECK(below < 30); // the arc between steps may graze a rising tile for a frame
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(bounces[i] >= 3); // big, slow bounces (gravity x 0.35)
        CHECK(high[i] > 2.0f); // still lively at the end (minBounce 3 m)
    }
    // the impacts: landings with a negative strength (a spring without the aliens' glow), and the renderer's list
    const auto* tg = dynamic_cast<const scene::TileGround*>(play.comp().entityWorld().ground());
    REQUIRE(tg != nullptr);
    int ballLandings = 0;
    for (const scene::TileEvent& e : tg->landings()) ballLandings += e.strength < 0.0f ? 1 : 0;
    CHECK(ballLandings > 0);
    CHECK(!play.comp().scene().environment.tileDynamics.ballImpacts.empty());

    // a 30 fps play and a cold seek agree with the 60 fps play to a millimetre
    const double T = 16.0;
    const auto want = play.balls();
    Rig thirty;
    thirty.playTo(T, 30.0);
    Rig sought;
    sought.engine.seekSeconds(T);
    sought.frame(T, 0.0, 0);
    const auto a = thirty.balls();
    const auto b = sought.balls();
    for (std::size_t i = 0; i < want.size(); ++i) {
        CAPTURE(i, want[i].x, want[i].y, want[i].z, a[i].y, b[i].y);
        CHECK(glm::length(want[i] - a[i]) < 1e-3f);
        CHECK(glm::length(want[i] - b[i]) < 1e-3f);
    }
}

TEST_CASE("a ball's impact springs the tile but lights no landing glow; its look round-trips",
          "[tiles][geography][balls][adr2008]") {
    scene::TileDynamics d;
    d.spring = scene::TileSpringSpec{};
    const scene::TileEvent alien{1.0f, {0.0f, 0.0f}, 0.5f};
    const scene::TileEvent ball{1.0f, {0.0f, 0.0f}, -0.5f};
    const float t = 1.3f;
    // the same spring, either sign
    CHECK(scene::tileSpring(d.spring, {ball}, 0.0f, 0.0f, t) == scene::tileSpring(d.spring, {alien}, 0.0f, 0.0f, t));
    CHECK(scene::tileSpring(d.spring, {ball}, 0.0f, 0.0f, t) != 0.0f);
    // the aliens' landing glow from the alien only
    CHECK(scene::tileLanding(d.spring, {alien}, 0.0f, 0.0f, t) > 0.0f);
    CHECK(scene::tileLanding(d.spring, {ball}, 0.0f, 0.0f, t) == 0.0f);

    scene::TileDynamics look;
    look.balls.glowColor = {0.1f, 0.2f, 0.3f};
    look.balls.sparkGain = 3.5f;
    look.balls.sparkSpeed = 9.0f;
    look.balls.edgeBand = 0.2f;
    const nlohmann::json j = scene::tileDynamicsToJson(look);
    REQUIRE(j.contains("balls"));
    const auto back = scene::tileDynamicsFromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->balls.glowColor == look.balls.glowColor);
    CHECK(back->balls.sparkGain == look.balls.sparkGain);
    CHECK(back->balls.sparkSpeed == look.balls.sparkSpeed);
    CHECK(back->balls.edgeBand == look.balls.edgeBand);
    CHECK(!scene::tileDynamicsToJson(scene::TileDynamics{}).contains("balls")); // written only when changed
    nlohmann::json bad = j;
    bad["balls"]["sparkColour"] = 1.0;
    CHECK(!scene::tileDynamicsFromJson(bad).has_value()); // an unknown key is refused
}
