// Musical Geography Phase 6: characters on the tiles. The landing springs (ADR-1954), the ground seam and TileGround
// (ADR-1955), TileNav (ADR-1957) and the hop behaviour (ADR-1958). The springs' GPU twin is in test_tile_terrain_gpu.

#include "assets/asset_registry.hpp"
#include "core/rng.hpp"
#include "core/time.hpp"
#include "entity/behavior.hpp"
#include "entity/entity.hpp"
#include "entity/tile_nav.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "params/serialization.hpp"
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"
#include "scene/tile_ground.hpp"
#include "scene/tile_terrain.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <map>

using namespace avgen;
using Catch::Approx;

namespace {

scene::TileDynamics oneSpring(float t0 = 1.0f) {
    scene::TileDynamics d;
    d.springs = {{t0, {2.0f, 2.0f}, 1.0f}};
    return d;
}

float springAt(const scene::TileDynamics& d, float x, float z, float t) {
    return scene::tileSpring(d.spring, d.springsAt(t), x, z, t);
}

// A hand-made lattice for TileNav: heights per tile, water and flat per tile, everything else level ground at 0.
struct Lattice {
    std::map<std::pair<int, int>, world::GroundSample> cells;
    [[nodiscard]] world::GroundSample at(glm::ivec2 t) const {
        const auto it = cells.find({t.x, t.y});
        return it != cells.end() ? it->second : world::GroundSample{};
    }
    void set(int x, int z, float h, float water = 0.0f) {
        world::GroundSample s;
        s.height = h;
        s.water = water;
        cells[{x, z}] = s;
    }
};

// A pitch-4 tiles ground with motion; landed where the test says.
scene::TileDynamics movingGround() {
    scene::TileDynamics d;
    scene::TileWave w;
    w.amplitude = 0.5f;
    w.wavelength = 40.0f;
    w.speed = 5.0f;
    w.direction = 20.0f;
    d.waves = {w};
    d.breathingAmplitude = 0.15f;
    d.breathingPeriod = 8.0f;
    return d;
}

} // namespace

// ---- springs (ADR-1954) -------------------------------------------------------------------------------------------

TEST_CASE("a landing spring starts at rest, first dips, and settles by springSeconds", "[tiles][springs][geography]") {
    const scene::TileDynamics d = oneSpring();
    CHECK(d.active());
    CHECK(springAt(d, 2.0f, 2.0f, 1.0f) == Approx(0.0f).margin(1e-6)); // no jump at the landing
    CHECK(springAt(d, 2.0f, 2.0f, 0.5f) == 0.0f);                      // and nothing before it
    // the first extremum is downward
    float first = 0.0f;
    float prev = 0.0f;
    for (float t = 1.0f; t < 1.0f + d.springSeconds(); t += 1.0f / 240.0f) {
        const float h = springAt(d, 2.0f, 2.0f, t);
        if (std::abs(h) < std::abs(prev) && std::abs(prev) > 1e-3f) {
            first = prev;
            break;
        }
        prev = h;
    }
    CAPTURE(first);
    CHECK(first < -0.2f);
    // settled: below a millimetre (a metre of depth at 1e-3) at springSeconds, and dropped after
    const float end = 1.0f + d.springSeconds();
    float tail = 0.0f;
    for (float t = end - 0.5f; t < end; t += 1.0f / 240.0f) {
        tail = std::max(tail, std::abs(springAt(d, 2.0f, 2.0f, t)));
    }
    CHECK(tail < d.spring.depth * 1.5e-3f);
    CHECK(d.springsAt(end + 0.01f).empty());
}

TEST_CASE("a landing spring's envelope never grows and its neighbours move less", "[tiles][springs][geography]") {
    const scene::TileDynamics d = oneSpring();
    const float w = 6.2831853f * d.spring.frequency;
    const float zeta = d.spring.damping;
    float last = 1e9f;
    for (float a = 0.0f; a < d.springSeconds(); a += 0.05f) {
        const float env = d.spring.depth * std::exp(-zeta * w * a);
        CHECK(env <= last);
        last = env;
        CHECK(std::abs(springAt(d, 2.0f, 2.0f, 1.0f + a)) <= env + 1e-5f);
    }
    float source = 0.0f;
    float neighbour = 0.0f;
    for (float t = 1.0f; t < 1.0f + d.springSeconds(); t += 1.0f / 240.0f) {
        source = std::max(source, std::abs(springAt(d, 2.0f, 2.0f, t)));
        neighbour = std::max(neighbour, std::abs(springAt(d, 6.0f, 2.0f, t))); // one 4 m tile over
    }
    CAPTURE(source, neighbour);
    CHECK(neighbour < source);
    CHECK(neighbour > 0.0f); // the ripple reaches it
}

TEST_CASE("the springs keep every landing still ringing, up to the cap, newest first beyond it",
          "[tiles][springs][geography]") {
    scene::TileDynamics d;
    const std::size_t n = scene::kMaxTileSprings + 40;
    for (std::size_t i = 0; i < n; ++i) {
        d.springs.push_back({0.001f * static_cast<float>(i), {static_cast<float>(i), 0.0f}, 1.0f});
    }
    const float now = 0.001f * static_cast<float>(n);
    const auto kept = d.springsAt(now);
    REQUIRE(kept.size() == scene::kMaxTileSprings);
    CHECK(kept.front().time == Approx(0.001f * static_cast<float>(n - 1)));
    // under the cap, nothing still ringing is dropped (CP-D r2: 104 at once in draft3)
    scene::TileDynamics few;
    for (int i = 0; i < 120; ++i) {
        few.springs.push_back({0.04f * static_cast<float>(i), {4.0f * static_cast<float>(i % 11), 0.0f}, 1.0f});
    }
    REQUIRE(few.springSeconds() > 4.8f);
    CHECK(few.springsAt(4.8f).size() == 120);
    CHECK(few.springsAt(0.025f).size() == 1);
}

// ---- TileGround (ADR-1955) -----------------------------------------------------------------------------------------

TEST_CASE("TileGround answers what the moving ground says at the cell centre", "[tiles][ground][geography]") {
    const scene::TileTerrainSpec spec;
    const scene::TileDynamics d = movingGround();
    const glm::vec3 offset(10.0f, -3.0f, -6.0f);
    scene::TileGround g;
    g.set(spec, d, 4.0f, offset);
    REQUIRE(g.valid());
    for (int i = 0; i < 60; ++i) {
        const float x = -80.0f + 2.91f * static_cast<float>(i);
        const float z = 40.0f - 1.77f * static_cast<float>(i);
        const double t = 0.3 * i;
        const glm::vec2 c = g.cellCentre(x, z);
        CHECK(std::abs(c.x - x) <= 2.0f + 1e-4f);
        CHECK(std::abs(c.y - z) <= 2.0f + 1e-4f);
        const scene::TileSample want =
            scene::tileSampleMoving(spec, d, c.x - offset.x, c.y - offset.z, static_cast<float>(t), 4.0f);
        const world::GroundSample got = g.at(x, z, t);
        CHECK(got.height == Approx(want.height + offset.y).margin(1e-5));
        CHECK(got.water == want.water);
    }
}

TEST_CASE("TileGround springs under a landing, once, and forgets it on a seek back", "[tiles][ground][geography]") {
    const scene::TileTerrainSpec spec;
    scene::TileGround g;
    g.set(spec, scene::TileDynamics{}, 4.0f, glm::vec3(0.0f));
    const glm::vec2 c = g.cellCentre(5.0f, 5.0f);
    const float before = g.at(c.x, c.y, 2.15).height; // ~ the first dip (a quarter period of 1.6 Hz)
    g.landed(c, 2.0, 1.0f);
    g.landed(c, 2.0, 1.0f); // a replay of the same landing
    CHECK(g.landings().size() == 1);
    CHECK(g.at(c.x, c.y, 2.15).height < before - 0.1f);
    g.forgetAfter(1.5);
    CHECK(g.landings().empty());
    g.set(spec, scene::TileDynamics{}, 4.0f, glm::vec3(0.0f));
    CHECK(g.at(c.x, c.y, 2.15).height == Approx(before));
}

// ---- TileNav (ADR-1957) --------------------------------------------------------------------------------------------

TEST_CASE("TileNav walks a level run and hops a riser it cannot step", "[tiles][nav][geography]") {
    Lattice l;
    for (int x = 3; x <= 4; ++x) {
        for (int z = -20; z <= 20; ++z) {
            l.set(x, z, 2.0f); // a 2 m wall two tiles thick across the way
        }
    }
    const entity::TileNavRules rules;
    const auto q = [&](glm::ivec2 t) { return l.at(t); };
    const entity::TilePath p = entity::planTiles(q, {0, 0}, {8, 0}, rules);
    REQUIRE(p.found);
    REQUIRE(p.steps.back().tile == glm::ivec2(8, 0));
    glm::ivec2 at{0, 0};
    bool hopped = false;
    for (const entity::TileStep& s : p.steps) {
        const world::GroundSample a = l.at(at);
        const world::GroundSample b = l.at(s.tile);
        if (s.edge == entity::TileEdge::Walk) {
            CHECK(std::abs(b.height - a.height) <= rules.stepHeight); // never walks a riser over stepHeight
            CHECK(std::abs(s.tile.x - at.x) + std::abs(s.tile.y - at.y) == 1);
        } else {
            hopped = true;
            CHECK(b.height - a.height <= rules.jumpRise);
            CHECK(a.height - b.height <= rules.jumpDrop);
        }
        at = s.tile;
    }
    CHECK(hopped);
}

TEST_CASE("TileNav finds a reachable crown and refuses one over the jump envelope", "[tiles][nav][geography]") {
    entity::TileNavRules rules;
    const auto q0 = [](glm::ivec2) { return world::GroundSample{}; };
    // a crown 2.5 m up, one tile over: a hop
    Lattice ok;
    ok.set(1, 0, 2.5f);
    const auto q1 = [&](glm::ivec2 t) { return ok.at(t); };
    const entity::TilePath reach = entity::planTiles(q1, {0, 0}, {1, 0}, rules);
    REQUIRE(reach.found);
    CHECK(reach.steps.size() == 1);
    CHECK(reach.steps[0].edge == entity::TileEdge::Jump);
    // a pillar 6 m up with nothing in between: no edge rises that far
    Lattice tall;
    tall.set(5, 5, 6.0f);
    const auto q2 = [&](glm::ivec2 t) { return tall.at(t); };
    CHECK_FALSE(entity::planTiles(q2, {0, 0}, {5, 5}, rules).found);
    // and a chasm four tiles wide and 20 m deep is not jumped (three tiles is the farthest hop) nor dropped into
    Lattice moat;
    for (int x = 1; x <= 4; ++x) {
        for (int z = -60; z <= 60; ++z) {
            moat.set(x, z, -20.0f);
        }
    }
    const auto q3 = [&](glm::ivec2 t) { return moat.at(t); };
    const entity::TilePath none = entity::planTiles(q3, {0, 0}, {6, 0}, rules);
    CHECK_FALSE(none.found);
    // sea slabs are hopped onto, never waded
    Lattice sea;
    for (int z = -60; z <= 60; ++z) {
        sea.set(1, z, -0.3f, 1.0f);
    }
    const auto q4 = [&](glm::ivec2 t) { return sea.at(t); };
    const entity::TilePath across = entity::planTiles(q4, {0, 0}, {2, 0}, rules);
    REQUIRE(across.found);
    for (const entity::TileStep& s : across.steps) {
        if (sea.at(s.tile).water > 0.5f) {
            CHECK(s.edge == entity::TileEdge::Jump);
        }
    }
    // envelope rules directly
    world::GroundSample lo;
    world::GroundSample hi;
    hi.height = rules.jumpRise + 0.01f;
    CHECK_FALSE(entity::tileJumpAllowed(lo, hi, 1, rules));
    CHECK_FALSE(entity::tileJumpAllowed(lo, lo, rules.jumpTiles + 1, rules));
    CHECK(entity::planTiles(q0, {0, 0}, {3, 4}, rules).found);
}

TEST_CASE("TileNav's every landing is on a tile the rules allow", "[tiles][nav][geography]") {
    // A real tiles ground at 4 m: random goals, every step checked against the same ground.
    const scene::TileTerrainSpec spec;
    scene::TileGround g;
    g.set(spec, scene::TileDynamics{}, 4.0f, glm::vec3(0.0f));
    const glm::vec2 c00 = g.cellCentre(0.0f, 0.0f);
    const auto q = [&](glm::ivec2 t) {
        const glm::vec2 c = c00 + glm::vec2(t) * 4.0f;
        return g.at(c.x, c.y, 0.0);
    };
    const entity::TileNavRules rules;
    Rng rng(7);
    int found = 0;
    for (int k = 0; k < 40; ++k) {
        const glm::ivec2 from(static_cast<int>(rng.range(-40.0f, 40.0f)), static_cast<int>(rng.range(-40.0f, 40.0f)));
        const glm::ivec2 to = from + glm::ivec2(static_cast<int>(rng.range(-10.0f, 10.0f)),
                                                static_cast<int>(rng.range(-10.0f, 10.0f)));
        const entity::TilePath p = entity::planTiles(q, from, to, rules);
        if (!p.found) {
            continue;
        }
        ++found;
        glm::ivec2 at = from;
        for (const entity::TileStep& s : p.steps) {
            const world::GroundSample a = q(at);
            const world::GroundSample b = q(s.tile);
            if (s.edge == entity::TileEdge::Walk) {
                CHECK(entity::tileWalkAllowed(a, b, rules));
            } else {
                const glm::ivec2 d = glm::abs(s.tile - at);
                CHECK(entity::tileJumpAllowed(a, b, std::max(d.x, d.y), rules));
            }
            at = s.tile;
        }
    }
    CAPTURE(found);
    CHECK(found > 10);
}

// ---- the hop behaviour (ADR-1958) ----------------------------------------------------------------------------------

namespace {

struct HopRun {
    std::vector<glm::vec3> positions;
    std::vector<entity::Activity> activity;
    std::vector<float> ground;
    std::size_t landings = 0;
};

HopRun runHop(const scene::TileDynamics& dyn, double seconds, std::uint64_t seed) {
    const scene::TileTerrainSpec spec;
    scene::TileGround g;
    g.set(spec, dyn, 4.0f, glm::vec3(0.0f));
    nlohmann::json settings = {{"hopChance", 0.4}};
    auto hop = entity::makeBehavior("hop", &settings);
    REQUIRE(hop != nullptr);
    params::ParameterSet params;
    hop->registerParameters(params, "e/hop/");
    Rng rng(seed);
    hop->reset(rng);
    entity::EntityState state;
    state.anchor = glm::vec3(6.0f, 0.0f, -10.0f);
    entity::MotionOffset offset;
    HopRun out;
    const double dt = 1.0 / 60.0;
    for (int f = 0; f * dt < seconds; ++f) {
        g.set(spec, dyn, 4.0f, glm::vec3(0.0f)); // the composition's per-frame refresh
        entity::BehaviorContext ctx;
        ctx.time = f * dt;
        ctx.dt = dt;
        ctx.rng = &rng;
        ctx.ground = &g;
        hop->update(ctx, state, offset);
        const glm::vec3 p = state.position();
        out.positions.push_back(p);
        out.activity.push_back(state.activity);
        out.ground.push_back(g.at(p.x, p.z, ctx.time).height);
    }
    out.landings = g.landings().size();
    return out;
}

} // namespace

TEST_CASE("a hopping body stands on its moving, sprung tile and goes places", "[tiles][hop][geography]") {
    const HopRun r = runHop(movingGround(), 40.0, 3);
    float worstBelow = 0.0f;
    float worstAboveStanding = 0.0f;
    float travelled = 0.0f;
    int airFrames = 0;
    for (std::size_t i = 0; i < r.positions.size(); ++i) {
        const bool air = r.activity[i] == entity::Activity::Jump || r.activity[i] == entity::Activity::Fall;
        airFrames += air ? 1 : 0;
        const float gap = r.positions[i].y - r.ground[i];
        if (!air && r.activity[i] != entity::Activity::Walk && r.activity[i] != entity::Activity::Run) {
            worstBelow = std::min(worstBelow, gap);
            worstAboveStanding = std::max(worstAboveStanding, std::abs(gap));
        }
        if (i > 0) {
            travelled += glm::length(glm::vec2(r.positions[i].x - r.positions[i - 1].x,
                                               r.positions[i].z - r.positions[i - 1].z));
        }
    }
    CAPTURE(worstBelow, worstAboveStanding, travelled, airFrames, r.landings);
    CHECK(worstAboveStanding < 1e-3f); // standing and landing, it is on the tile's top as it moves and springs
    CHECK(travelled > 12.0f);          // it went somewhere
    CHECK(airFrames > 0);              // and hopped on the way
    CHECK(r.landings > 0);             // and the ground felt it
    // a standing body's height follows its tile without jitter: frame-to-frame change bounded by the ground's own
    float jitter = 0.0f;
    for (std::size_t i = 1; i < r.positions.size(); ++i) {
        if (r.activity[i] == entity::Activity::Idle && r.activity[i - 1] == entity::Activity::Idle) {
            jitter = std::max(jitter, std::abs((r.positions[i].y - r.positions[i - 1].y) -
                                               (r.ground[i] - r.ground[i - 1])));
        }
    }
    CHECK(jitter < 1e-3f);
}

TEST_CASE("the hop behaviour is deterministic", "[tiles][hop][geography]") {
    const HopRun a = runHop(movingGround(), 20.0, 11);
    const HopRun b = runHop(movingGround(), 20.0, 11);
    REQUIRE(a.positions.size() == b.positions.size());
    for (std::size_t i = 0; i < a.positions.size(); ++i) {
        REQUIRE(a.positions[i] == b.positions[i]);
    }
    CHECK(a.landings == b.landings);
}

TEST_CASE("a hop with no ground stands where the scene put it", "[tiles][hop][geography]") {
    auto hop = entity::makeBehavior("hop", nullptr);
    REQUIRE(hop != nullptr);
    params::ParameterSet params;
    hop->registerParameters(params, "e/hop/");
    Rng rng(1);
    hop->reset(rng);
    entity::EntityState state;
    state.anchor = glm::vec3(1.0f, 2.0f, 3.0f);
    entity::MotionOffset offset;
    entity::BehaviorContext ctx;
    ctx.dt = 1.0 / 60.0;
    ctx.rng = &rng;
    for (int f = 0; f < 120; ++f) {
        ctx.time = f / 60.0;
        hop->update(ctx, state, offset);
    }
    CHECK(state.position() == glm::vec3(1.0f, 2.0f, 3.0f));
}

// ---- the composition: the ground the bodies stand on is the ground drawn, and a seek is a play -------------------

namespace {

struct HopScene {
    assets::AssetRegistry registry{testsupport::processTempDir()};
    std::unique_ptr<scene::Composition> comp;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;

    HopScene() {
        const auto json = nlohmann::json::parse(R"({
            "format": "avgen-scene", "version": 1, "name": "hops",
            "camera": {"position": [0, 30, 40], "target": [0, 0, 0]},
            "environment": {"tileDynamics": {
                "waves": [ {"amplitude": 0.4, "wavelength": 36, "speed": 5, "onLand": 1} ],
                "spring": {"depth": 0.8}
            }},
            "nodes": [
                {"name": "ground", "kind": "procedural", "position": [2, 0, -1],
                 "procedural": {"source": {"kind": "box", "size": [3.66, 1.2, 3.66]},
                    "distribution": {"kind": "generator", "generator": {"name": "tiles", "version": 1,
                        "cellSize": 4.0, "viewDistance": 120.0, "tiles": {"seed": 3, "landThreshold": 0.2}}},
                    "slabs": true}},
                {"name": "a", "kind": "orb", "position": [6, 0, 6]},
                {"name": "b", "kind": "orb", "position": [-10, 0, 3]}
            ],
            "entities": [
                {"name": "a", "node": "a", "seed": 5, "behaviors": [{"kind": "hop", "hopChance": 0.5, "pauseMax": 1.5, "tossThreshold": 0.25}]},
                {"name": "b", "node": "b", "seed": 9, "behaviors": [{"kind": "hop", "hopChance": 0.5, "pauseMax": 1.5, "tossThreshold": 0.25}]}
            ]})");
        auto c = scene::Composition::fromJson(json, registry);
        REQUIRE(c.has_value());
        comp = std::move(*c);
        comp->attach(params, modulator);
    }
    void tick(double t, double dt, std::uint64_t f) {
        FrameTime time;
        time.renderTime = t;
        time.deltaTime = dt;
        time.frameIndex = f;
        params.resetFinals();
        comp->updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp->updateBehaviour(time, bus);
        comp->update(time);
    }
    void play(double seconds) {
        const auto last = static_cast<long long>(std::llround(seconds * 60.0));
        for (long long f = 0; f <= last; ++f) {
            tick(static_cast<double>(f) / 60.0, f == 0 ? 0.0 : 1.0 / 60.0, static_cast<std::uint64_t>(f));
        }
    }
    void seek(double seconds) {
        comp->seekWithDirector(seconds, params,
                               entity::SeekBudget{.maxSeconds = 90.0,
                                                  .maxBodySteps = entity::SeekBudget::kEditorBodySteps,
                                                  .mode = entity::SeekMode::Checkpointed},
                               1.0 / 60.0);
    }
    [[nodiscard]] std::vector<glm::vec3> bodies() const {
        std::vector<glm::vec3> out;
        for (const auto& e : comp->entityWorld().entities()) {
            out.push_back(e->state().position());
        }
        return out;
    }
};

} // namespace

TEST_CASE("characters hop on the drawn tiles, spring them, and a seek lands where a play does",
          "[tiles][hop][geography][composition][seek]") {
    HopScene played;
    played.play(14.0);
    const auto springs = played.comp->scene().environment.tileDynamics.springs;
    CAPTURE(springs.size());
    REQUIRE_FALSE(springs.empty()); // landings reached the drawn ground's spring list
    REQUIRE(played.comp->entityWorld().ground() != nullptr);
    const auto at = played.bodies();
    // standing bodies are on the ground the composition built from the drawn node (offset (2, 0, -1))
    for (const auto& e : played.comp->entityWorld().entities()) {
        const entity::EntityState& st = e->state();
        if (st.activity == entity::Activity::Idle) {
            const glm::vec3 p = st.position();
            const world::GroundSample g = played.comp->entityWorld().ground()->at(p.x, p.z, 14.0);
            CHECK(p.y == Approx(g.height).margin(1e-4));
        }
    }
    HopScene sought;
    sought.seek(14.0);
    const auto there = sought.bodies();
    REQUIRE(there.size() == at.size());
    for (std::size_t i = 0; i < at.size(); ++i) {
        CAPTURE(i, at[i].x, at[i].y, at[i].z, there[i].x, there[i].y, there[i].z);
        CHECK(glm::length(at[i] - there[i]) < 1e-3f);
    }
    // and a scrub back on the played one forgets the landings after it, and lands where a play to there does
    played.seek(6.0);
    played.tick(6.0, 0.0, 0); // the frame the seek lands on
    for (const scene::TileEvent& e : played.comp->scene().environment.tileDynamics.springs) {
        CHECK(e.time <= 6.0f + 1e-4f);
    }
    HopScene six;
    six.play(6.0);
    const auto a = played.bodies();
    const auto b = six.bodies();
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(glm::length(a[i] - b[i]) < 1e-3f);
    }
}

// ---- Phase 7: what the music reaches (ADR-1963, ADR-1964) ---------------------------------------------------------

TEST_CASE("where a body goes does not depend on what the routes do to the ground", "[tiles][hop][geography][audio]") {
    // ADR-1964: the plan reads the planning surface (the parameters' bases), the stance the drawn one. Two runs whose
    // drawn grounds differ by a tripled, audio-sized amplitude go the same way over the lattice.
    const scene::TileTerrainSpec spec;
    const scene::TileDynamics base = movingGround();
    scene::TileDynamics loud = base;
    loud.amplitude = 3.0f;
    const auto run = [&](const scene::TileDynamics& drawn) {
        scene::TileGround g;
        nlohmann::json settings = {{"hopChance", 0.4}};
        auto hop = entity::makeBehavior("hop", &settings);
        params::ParameterSet params;
        hop->registerParameters(params, "e/hop/");
        Rng rng(21);
        hop->reset(rng);
        entity::EntityState state;
        state.anchor = glm::vec3(-6.0f, 0.0f, 14.0f);
        entity::MotionOffset offset;
        std::vector<glm::vec2> xz;
        for (int f = 0; f < 1800; ++f) {
            g.set(spec, drawn, base, 4.0f, glm::vec3(0.0f));
            entity::BehaviorContext ctx;
            ctx.time = f / 60.0;
            ctx.dt = 1.0 / 60.0;
            ctx.rng = &rng;
            ctx.ground = &g;
            hop->update(ctx, state, offset);
            xz.emplace_back(state.position().x, state.position().z);
        }
        return xz;
    };
    const auto quiet = run(base);
    const auto noisy = run(loud);
    float worst = 0.0f;
    float travelled = 0.0f;
    for (std::size_t i = 0; i < quiet.size(); ++i) {
        worst = std::max(worst, glm::length(quiet[i] - noisy[i]));
        if (i > 0) travelled += glm::length(quiet[i] - quiet[i - 1]);
    }
    CAPTURE(worst, travelled);
    CHECK(travelled > 10.0f);
    CHECK(worst < 1e-4f);
}

TEST_CASE("the tile glow gains are parameters that reach the drawn scene, and silence leaves them as authored",
          "[tiles][dynamics][geography][audio][composition]") {
    const auto json = nlohmann::json::parse(R"({
        "format": "avgen-scene", "version": 1, "name": "glow",
        "camera": {"position": [0, 50, 0], "target": [0, 0, -100]},
        "environment": {"tileDynamics": {
            "waves": [ {"amplitude": 0.5, "wavelength": 30, "speed": 4} ],
            "glow": {"path": 1.5, "contour": 0.5}
        }},
        "nodes": []})");
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto c = scene::Composition::fromJson(json, registry);
    REQUIRE(c.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*c)->attach(params, modulator);
    for (const char* name : {"path", "contour", "crest", "pads"}) {
        CHECK(params.find(std::string("env/tiles/glow/") + name) != nullptr);
    }
    // A route from a band onto the path glow and the amplitude; the bus is silent: what is drawn is what is authored.
    params::ModRoute toPath;
    toPath.source = "response.mid";
    toPath.target = "env/tiles/glow/path";
    toPath.amount = 2.0f;
    params::ModRoute toAmp;
    toAmp.source = "response.low";
    toAmp.target = "env/tiles/amplitude";
    toAmp.amount = 0.6f;
    modulator.addRoute(toPath);
    modulator.addRoute(toAmp);
    signals::SignalBus bus;
    const signals::SignalId mid = bus.declare("response.mid");
    (void)bus.declare("response.low");
    (void)modulator.bind(bus, params); // the composition's default routes read signals this bus lacks
    for (const params::ModRoute& r : modulator.routes()) {
        if (r.source == "response.mid" || r.source == "response.low") REQUIRE(r.targetParam != nullptr);
    }
    const auto frame = [&](double t) {
        params.resetFinals();
        modulator.applyRoutes(bus, params, 1.0 / 60.0);
        FrameTime ft{};
        ft.renderTime = t;
        (*c)->update(ft);
        return (*c)->scene().environment.tileDynamics;
    };
    const scene::TileDynamics silent = frame(0.5);
    CHECK(silent.glow == glm::vec4(1.5f, 0.5f, 1.0f, 1.0f));
    CHECK(silent.amplitude == 1.0f);
    // a sound moves it, through the route
    bus.set(mid, 0.5f);
    CHECK(frame(0.75).glow.x == Catch::Approx(1.5f + 2.0f * 0.5f));
    bus.set(mid, 0.0f);
    // the base moves it (the same path a route's output takes)
    params.find("env/tiles/glow/contour")->setBaseComponent(0, 2.0f);
    CHECK(frame(1.0).glow.y == 2.0f);
    // and a save keeps the authored glow
    const auto saved = (*c)->toJson();
    CHECK(saved.at("environment").at("tileDynamics").at("glow").at("path") == 1.5f);
}

TEST_CASE("a seek that lands mid-hop puts the body where a play does, with a route moving the ground",
          "[tiles][hop][geography][composition][seek][audio]") {
    // A real project route swells the ground (as response.low does in make_audio.py); a seek's replay does not re-run project routes,
    // so the replay's ground differs from the play's. The arc must not remember it.
    const auto swell = [](double t) { return static_cast<float>(0.6 + 0.4 * std::sin(t * 1.3)); };
    const auto wire = [](HopScene& h) {
        const signals::SignalId id = h.bus.declare("test.swell");
        params::ModRoute r;
        r.source = "test.swell";
        r.target = "env/tiles/amplitude"; // the swell (this scene's waves move the land too)
        r.amount = 2.0f;
        h.modulator.addRoute(r);
        (void)h.modulator.bind(h.bus, h.params); // the composition's own default routes read signals this bus lacks
        REQUIRE(h.modulator.routes().back().targetParam != nullptr);
        return id;
    };
    HopScene played;
    const signals::SignalId s1 = wire(played);
    std::vector<std::pair<long long, std::size_t>> airborne;
    for (long long f = 0; f <= 60 * 16; ++f) {
        const double t = static_cast<double>(f) / 60.0;
        played.bus.set(s1, swell(t));
        played.tick(t, f == 0 ? 0.0 : 1.0 / 60.0, static_cast<std::uint64_t>(f));
        if (f > 60 * 6) {
            const auto& es = played.comp->entityWorld().entities();
            for (std::size_t i = 0; i < es.size(); ++i) {
                if (es[i]->state().airborne) airborne.emplace_back(f, i);
            }
        }
    }
    REQUIRE_FALSE(airborne.empty());
    const std::size_t who = airborne.front().second;
    const long long first = airborne.front().first;
    long long last = first;
    for (const auto& [f, i] : airborne) {
        if (i == who && f == last + 1) last = f;
    }
    const long long mid = (first + last) / 2;
    const double T = static_cast<double>(mid) / 60.0;
    CAPTURE(first, last, mid);
    HopScene again; // the play, to exactly that frame
    const signals::SignalId s2 = wire(again);
    for (long long f = 0; f <= mid; ++f) {
        const double t = static_cast<double>(f) / 60.0;
        again.bus.set(s2, swell(t));
        again.tick(t, f == 0 ? 0.0 : 1.0 / 60.0, static_cast<std::uint64_t>(f));
    }
    const entity::EntityState& play = again.comp->entityWorld().entities()[who]->state();
    REQUIRE(play.airborne);
    const glm::vec3 want = play.position();
    HopScene sought; // a cold seek there, then the frame it lands on with that instant's routes
    const signals::SignalId s3 = wire(sought);
    sought.seek(T);
    sought.bus.set(s3, swell(T));
    sought.tick(T, 0.0, static_cast<std::uint64_t>(mid));
    const entity::EntityState& got = sought.comp->entityWorld().entities()[who]->state();
    CAPTURE(want.x, want.y, want.z, got.position().x, got.position().y, got.position().z);
    CHECK(got.airborne);
    CHECK(glm::length(got.position() - want) < 1e-3f);
}

TEST_CASE("a landed tile settles smoothly while many other landings arrive (no early cutoff)",
          "[tiles][springs][geography]") {
    // CP-D r2 canary as a test: draft3 had ~100 landings ringing at once. One tile lands at t = 1; then a landing every
    // 0.1 s elsewhere (14-30 m away). The tile's displacement over time must be the whole damped tail -- no frame-to-
    // frame jump bigger than the spring's own motion, and still ringing after 2 s (with 8 springs it went flat at once).
    scene::TileDynamics d;
    d.spring = {1.1f, 1.4f, 0.16f, 16.0f, 14.0f}; // draft3's spring
    d.springs.push_back({1.0f, {0.0f, 0.0f}, 1.0f});
    for (int i = 1; i <= 80; ++i) {
        const float ang = 0.7f * static_cast<float>(i);
        const float rad = 60.0f + 5.0f * static_cast<float>(i % 7); // beyond the reach of the tracked tile
        d.springs.push_back({1.0f + 0.1f * static_cast<float>(i), {rad * std::cos(ang), rad * std::sin(ang)}, 1.0f});
    }
    float worstStep = 0.0f;
    float late = 0.0f;
    float prev = 0.0f;
    for (float t = 1.0f; t < 1.0f + d.springSeconds(); t += 1.0f / 60.0f) {
        const float h = scene::tileSpring(d.spring, d.springsAt(t), 0.0f, 0.0f, t);
        worstStep = std::max(worstStep, std::abs(h - prev));
        if (t > 3.0f && t < 3.5f) late = std::max(late, std::abs(h));
        prev = h;
    }
    // the spring alone, for the bound on a frame's step
    const scene::TileDynamics alone = [] {
        scene::TileDynamics a;
        a.spring = {1.1f, 1.4f, 0.16f, 16.0f, 14.0f};
        a.springs.push_back({1.0f, {0.0f, 0.0f}, 1.0f});
        return a;
    }();
    float aloneStep = 0.0f;
    prev = 0.0f;
    for (float t = 1.0f; t < 1.0f + alone.springSeconds(); t += 1.0f / 60.0f) {
        const float h = scene::tileSpring(alone.spring, alone.springsAt(t), 0.0f, 0.0f, t);
        aloneStep = std::max(aloneStep, std::abs(h - prev));
        prev = h;
    }
    CAPTURE(worstStep, aloneStep, late);
    CHECK(worstStep <= aloneStep + 1e-4f);
    CHECK(late > 0.02f); // still settling two seconds on
}

TEST_CASE("a spring's reach ends smoothly", "[tiles][springs][geography]") {
    scene::TileDynamics d;
    d.springs.push_back({0.0f, {0.0f, 0.0f}, 1.0f});
    const float rr = d.spring.rippleRadius;
    float worst = 0.0f;
    const float x0 = (scene::kTileSpringReachStart - 0.5f) * rr;
    const float t0 = 0.4f + x0 / d.spring.rippleSpeed;
    float prev = scene::tileSpring(d.spring, d.springsAt(t0), x0, 0.0f, t0);
    for (float x = (scene::kTileSpringReachStart - 0.5f) * rr; x < (scene::kTileSpringReach + 0.5f) * rr; x += 0.01f) {
        const float t = 0.4f + x / d.spring.rippleSpeed; // ride the ripple's front so the term is at its largest
        const float h = scene::tileSpring(d.spring, d.springsAt(t), x, 0.0f, t);
        worst = std::max(worst, std::abs(h - prev));
        prev = h;
    }
    CHECK(worst < 1e-4f);
    CHECK(scene::tileSpring(d.spring, d.springsAt(2.0f), scene::kTileSpringReach * rr + 0.1f, 0.0f, 2.0f) == 0.0f);
}

// ---- ADR-1960: the landing glow -----------------------------------------------------------------------------------

TEST_CASE("a landing lights its tiles from the centre out and fades with the spring's own envelope",
          "[tiles][springs][geography][adr1960]") {
    scene::TileDynamics d;
    d.spring = {1.1f, 1.4f, 0.16f, 16.0f, 14.0f};
    d.springs.push_back({1.0f, {0.0f, 0.0f}, 1.0f});
    const auto glowAt = [&](float x, float t) { return scene::tileLanding(d.spring, d.springsAt(t), x, 0.0f, t); };
    CHECK(glowAt(0.0f, 0.99f) == 0.0f);   // nothing before
    CHECK(glowAt(0.0f, 1.0f) == 0.0f);    // starts at 0 (a 60 ms rise, no pop)
    CHECK(glowAt(0.0f, 1.25f) > 0.5f);    // lit
    // smooth falloff from the impact outward, at a fixed time after each tile's ripple arrives
    float prev = 2.0f;
    for (float x = 0.0f; x < 6.0f * d.spring.rippleRadius; x += 4.0f) {
        const float g = glowAt(x, 1.25f + x / d.spring.rippleSpeed);
        CHECK(g <= prev + 1e-6f);
        prev = g;
    }
    // it fades with the envelope, without a frame-to-frame step, to nothing by springSeconds
    float worst = 0.0f;
    float last = glowAt(0.0f, 1.0f);
    for (float t = 1.0f; t < 1.0f + d.springSeconds() + 0.5f; t += 1.0f / 60.0f) {
        const float g = glowAt(0.0f, t);
        worst = std::max(worst, std::abs(g - last));
        last = g;
    }
    CAPTURE(worst);
    CHECK(worst < 0.25f);                  // the rise (60 ms) is the fastest change
    CHECK(glowAt(0.0f, 3.0f) > 0.02f);     // still visible two seconds on
    CHECK(glowAt(0.0f, 1.0f + d.springSeconds() + 0.1f) == 0.0f);
    // the moving sample carries it, and the record packs it the way the op decodes it
    const scene::TileTerrainSpec spec;
    CHECK(scene::tileSampleMoving(spec, d, 0.0f, 0.0f, 1.4f, 4.0f).landing == Approx(glowAt(0.0f, 1.4f)));
    const float w = scene::tilePackedEmissiveW(0.4f, 5u, 0.3f);
    CHECK(std::floor(w / 32.0f) == std::round(0.3f * 255.0f));
}

TEST_CASE("the landing glow round-trips with its gain and colour, and they are parameters", "[tiles][geography][adr1960][composition]") {
    const auto json = nlohmann::json::parse(R"({
        "format": "avgen-scene", "version": 1, "name": "lglow",
        "camera": {"position": [0, 50, 0], "target": [0, 0, -100]},
        "environment": {"tileDynamics": {
            "waves": [ {"amplitude": 0.5, "wavelength": 30, "speed": 4} ],
            "glow": {"landing": 3.0, "landingColor": [1.0, 0.5, 0.25]}
        }},
        "nodes": []})");
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto c = scene::Composition::fromJson(json, registry);
    REQUIRE(c.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    (*c)->attach(params, modulator);
    auto* gain = params.find("env/tiles/glow/landing");
    auto* colour = params.find("env/tiles/glow/landingColor");
    REQUIRE(gain != nullptr);
    REQUIRE(colour != nullptr);
    params.resetFinals();
    FrameTime ft{};
    ft.renderTime = 0.5;
    (*c)->update(ft);
    CHECK((*c)->scene().environment.tileDynamics.landingGain == 3.0f);
    CHECK((*c)->scene().environment.tileDynamics.landingColor == glm::vec3(1.0f, 0.5f, 0.25f));
    gain->setBaseComponent(0, 5.0f);
    params.resetFinals();
    ft.renderTime = 0.6;
    (*c)->update(ft);
    CHECK((*c)->scene().environment.tileDynamics.landingGain == 5.0f);
    const auto saved = (*c)->toJson();
    CHECK(saved.at("environment").at("tileDynamics").at("glow").at("landing") == 3.0f);
}

TEST_CASE("a route refuses a processor-chain key written on the route itself", "[params][routes][geography]") {
    // CP-D r2: draft3 wrote attackMs/decayMs next to source/target; the reader dropped them and no smoothing ran.
    const auto flat = nlohmann::json::parse(
        R"({"source": "response.mid", "target": "env/tiles/glow/path", "amount": 1.0, "attackMs": 300.0})");
    const auto bad = params::routeFromJson(flat);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().message.find("attackMs") != std::string::npos);
    CHECK(bad.error().message.find("chain") != std::string::npos);
    const auto nested = nlohmann::json::parse(
        R"({"source": "response.mid", "target": "env/tiles/glow/path", "amount": 1.0, "chain": {"attackMs": 300.0}})");
    const auto good = params::routeFromJson(nested);
    REQUIRE(good.has_value());
    CHECK(good->chain.attackMs == 300.0f);
    // an unknown non-chain key is warned about, not refused (ADR-278); a leading underscore is a comment
    const auto odd = nlohmann::json::parse(
        R"({"source": "a", "target": "b", "_note": "x", "colour": 1})");
    CHECK(params::routeFromJson(odd).has_value());
}

// ---- ADR-1997: the crest toss, walking off water, and a fixed step ------------------------------------------------

namespace {

struct TossRun {
    std::vector<double> launches;          // when it left the ground on a throw
    std::vector<float> apexes;             // the height above its launch it reached
    std::vector<glm::vec3> landings;       // where each throw came down
    std::vector<std::pair<double, glm::vec3>> track; // (t, position) every frame
    float worstLanding = 0.0f;             // |position.y - the drawn top| at each landing
};

// A body resting (a pause far longer than the run) on a ground with one wave train, updated at `fps`.
TossRun runTosses(const scene::TileDynamics& d, nlohmann::json settings, double fps, double seconds,
                  glm::vec3 anchor = glm::vec3(6.0f, 0.0f, -10.0f), const scene::TileTerrainSpec& spec = {}) {
    scene::TileGround g;
    g.set(spec, d, 4.0f, glm::vec3(0.0f));
    settings["pauseMin"] = 1000.0;
    settings["pauseMax"] = 1000.0;
    auto hop = entity::makeBehavior("hop", &settings);
    params::ParameterSet params;
    hop->registerParameters(params, "e/hop/");
    Rng rng(7);
    hop->reset(rng);
    entity::EntityState state;
    state.anchor = anchor;
    entity::MotionOffset offset;
    TossRun out;
    bool wasAir = false;
    float launchY = 0.0f;
    float top = 0.0f;
    const auto frames = static_cast<long long>(seconds * fps);
    for (long long f = 0; f <= frames; ++f) {
        const double t = static_cast<double>(f) / fps;
        params.resetFinals();
        entity::BehaviorContext ctx;
        ctx.time = t;
        ctx.dt = f == 0 ? 0.0 : 1.0 / fps;
        ctx.rng = &rng;
        ctx.ground = &g;
        hop->update(ctx, state, offset);
        const glm::vec3 p = state.position();
        out.track.emplace_back(t, p);
        if (state.airborne && !wasAir) {
            out.launches.push_back(t);
            launchY = p.y;
            top = p.y;
        }
        if (state.airborne) top = std::max(top, p.y);
        if (!state.airborne && wasAir) {
            out.apexes.push_back(top - launchY);
            out.landings.push_back(p);
            out.worstLanding = std::max(out.worstLanding, std::abs(p.y - g.at(p.x, p.z, t).height));
        }
        wasAir = state.airborne;
    }
    return out;
}

scene::TileDynamics swell(float amplitude) {
    scene::TileDynamics d;
    scene::TileWave w;
    w.amplitude = amplitude;
    w.wavelength = 36.0f;
    w.speed = 5.0f; // peak rise = amplitude x 2 pi 5 / 36 = 0.87 x amplitude m/s
    w.direction = 20.0f;
    d.waves = {w};
    return d;
}

} // namespace

TEST_CASE("a crest throws a resting body, higher for a bigger crest, at most once per cooldown",
          "[tiles][hop][geography][adr1997]") {
    const nlohmann::json toss = {{"hopChance", 0.0}, {"tossThreshold", 0.3}, {"tossGain", 8.0}, {"tossCooldown", 2.0}};
    const TossRun small = runTosses(swell(0.5f), toss, 60.0, 40.0);  // peak rise ~0.44 m/s
    const TossRun big = runTosses(swell(1.0f), toss, 60.0, 40.0);    // ~0.87 m/s
    const TossRun calm = runTosses(swell(0.2f), toss, 60.0, 40.0);   // ~0.17 m/s: under the threshold
    const TossRun off = runTosses(swell(1.0f), {{"hopChance", 0.0}}, 60.0, 40.0); // tossThreshold 0: off
    CAPTURE(small.launches.size(), big.launches.size(), calm.launches.size());
    CHECK(calm.launches.empty());
    CHECK(off.launches.empty());
    REQUIRE(small.launches.size() >= 3); // the wave passes every 7.2 s
    REQUIRE(big.launches.size() >= 3);
    const auto mean = [](const std::vector<float>& v) {
        float s = 0.0f;
        for (const float x : v) s += x;
        return s / static_cast<float>(std::max<std::size_t>(v.size(), 1));
    };
    CAPTURE(mean(small.apexes), mean(big.apexes));
    CHECK(mean(big.apexes) > 2.0f * mean(small.apexes)); // v0 doubles: the climb is ballistic, about 4x
    for (std::size_t i = 1; i < big.launches.size(); ++i) {
        CHECK(big.launches[i] - big.launches[i - 1] >= 2.0 - 1e-6); // the cooldown
    }
    // it lands on the drawn top (ADR-1991), on dry land
    CHECK(big.worstLanding < 1e-3f);
    CHECK(small.worstLanding < 1e-3f);
}

TEST_CASE("the hop runs on a fixed step: 30, 45 and 60 fps throw and walk alike", "[tiles][hop][geography][adr1997]") {
    // a walking, tossed body; positions compared at the instants all three rates share (every 1/15 s)
    const nlohmann::json walker = {{"hopChance", 0.2}, {"tossThreshold", 0.3}, {"tossGain", 8.0}, {"tossCooldown", 2.0},
                                   {"pauseMin", 0.5}, {"pauseMax", 1.5}};
    const auto run = [&](double fps) {
        scene::TileGround g;
        g.set(scene::TileTerrainSpec{}, swell(0.8f), 4.0f, glm::vec3(0.0f));
        auto hop = entity::makeBehavior("hop", &walker);
        params::ParameterSet params;
        hop->registerParameters(params, "e/hop/");
        Rng rng(11);
        hop->reset(rng);
        entity::EntityState state;
        state.anchor = glm::vec3(-6.0f, 0.0f, 14.0f);
        entity::MotionOffset offset;
        std::vector<glm::vec3> at;
        const auto frames = static_cast<long long>(30.0 * fps);
        for (long long f = 0; f <= frames; ++f) {
            params.resetFinals();
            entity::BehaviorContext ctx;
            ctx.time = static_cast<double>(f) / fps;
            ctx.dt = f == 0 ? 0.0 : 1.0 / fps;
            ctx.rng = &rng;
            ctx.ground = &g;
            hop->update(ctx, state, offset);
            if (f % static_cast<long long>(fps / 15.0) == 0) at.push_back(state.position());
        }
        return at;
    };
    const auto a = run(30.0);
    const auto b = run(45.0);
    const auto c = run(60.0);
    REQUIRE(a.size() == b.size());
    REQUIRE(a.size() == c.size());
    float worst = 0.0f;
    float travelled = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max({worst, glm::length(a[i] - b[i]), glm::length(a[i] - c[i])});
        if (i > 0) travelled += glm::length(glm::vec2(a[i].x - a[i - 1].x, a[i].z - a[i - 1].z));
    }
    CAPTURE(worst, travelled);
    CHECK(travelled > 10.0f); // the control: it walks
    CHECK(worst < 1e-3f);
}

TEST_CASE("bodies keep off water: a start on water moves ashore, and no walk, hop or throw ends on it",
          "[tiles][hop][geography][adr1997]") {
    // a coast in the window: the default spec's continent field gives sea and land within a few tiles
    scene::TileTerrainSpec spec;
    spec.continentWavelength = 120.0f;
    scene::TileGround g;
    g.set(spec, swell(0.9f), 4.0f, glm::vec3(0.0f));
    // find a sea tile with land within 12 tiles
    glm::vec2 sea{0.0f};
    bool found = false;
    for (int z = -20; z <= 20 && !found; ++z) {
        for (int x = -20; x <= 20 && !found; ++x) {
            const glm::vec2 p = g.cellCentre(x * 4.0f, z * 4.0f);
            if (g.planned(p.x, p.y, 0.0).water < 0.5f) continue;
            for (int r = 1; r <= 6 && !found; ++r) {
                if (g.planned(p.x + 4.0f * r, p.y, 0.0).water < 0.5f) {
                    sea = p;
                    found = true;
                }
            }
        }
    }
    REQUIRE(found);
    const nlohmann::json settings = {{"hopChance", 0.5}, {"tossThreshold", 0.3}, {"tossGain", 8.0},
                                     {"tossCooldown", 1.5}, {"pauseMin", 0.3}, {"pauseMax", 1.0}, {"homeRadius", 40.0}};
    auto hop = entity::makeBehavior("hop", &settings);
    params::ParameterSet params;
    hop->registerParameters(params, "e/hop/");
    Rng rng(3);
    hop->reset(rng);
    entity::EntityState state;
    state.anchor = glm::vec3(sea.x, 0.0f, sea.y);
    entity::MotionOffset offset;
    int grounded = 0;
    int wetFrames = 0;
    float travelled = 0.0f;
    glm::vec3 last{0.0f};
    for (int f = 0; f <= 60 * 60; ++f) {
        params.resetFinals();
        entity::BehaviorContext ctx;
        ctx.time = f / 60.0;
        ctx.dt = f == 0 ? 0.0 : 1.0 / 60.0;
        ctx.rng = &rng;
        ctx.ground = &g;
        hop->update(ctx, state, offset);
        const glm::vec3 p = state.position();
        if (f > 0) travelled += glm::length(p - last);
        last = p;
        if (!state.airborne && (state.activity == entity::Activity::Idle || state.activity == entity::Activity::Land)) {
            ++grounded;
            wetFrames += g.planned(p.x, p.z, ctx.time).water >= 0.5f ? 1 : 0;
        }
    }
    CAPTURE(grounded, wetFrames, travelled);
    CHECK(grounded > 600);
    CHECK(travelled > 10.0f);
    CHECK(wetFrames == 0);
}

// ---- ADR-1991: no skinned mesh sinks into a tile ------------------------------------------------------------------

TEST_CASE("a hopping alien's mesh never sinks into the tile under any of its vertices", "[tiles][hop][geography][feet]") {
    // The owner's regression: 06-characters/cpd-r2-feet-close-landings at 2.0 s -- the landing crouch kneels below the
    // root's plane, feet and shins inside the landed tile. Every vertex of the drawn (skinned, lifted) mesh is checked
    // against the drawn top of the tile under THAT vertex, every frame through several landings.
    const std::filesystem::path aliens = std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "aliens";
    if (!std::filesystem::exists(aliens / "alien-scout.glb")) {
        SKIP("assets/aliens is not present (commercial assets, never committed)");
    }
    nlohmann::json json = nlohmann::json::parse(R"({
        "format": "avgen-scene", "version": 1, "name": "sink",
        "camera": {"position": [0, 6, 14], "target": [0, 0, 0]},
        "environment": {"tileDynamics": {
            "waves": [ {"amplitude": 0.5, "wavelength": 36, "speed": 5, "onLand": 0.5} ],
            "spring": {"depth": 1.1, "frequency": 1.4, "damping": 0.16, "rippleSpeed": 16, "rippleRadius": 14}
        }},
        "nodes": [
            {"name": "ground", "kind": "procedural", "position": [0, 0, 0],
             "procedural": {"source": {"kind": "box", "size": [3.66, 1.2, 3.66]},
                "distribution": {"kind": "generator", "generator": {"name": "tiles", "version": 1,
                    "cellSize": 4.0, "viewDistance": 60.0, "tiles": {"seed": 3, "landThreshold": 0.2}}},
                "slabs": true}},
            {"name": "alien", "kind": "gltf", "position": [2, 0, 2], "scale": [1.94, 1.94, 1.94],
             "animation": {"state": "Idle", "blend": 0.25, "speed": 1.0}}
        ],
        "entities": [
            {"name": "alien", "node": "alien", "seed": 5,
             "clips": {"idle": "Idle", "walk": "Walking", "run": "Running", "jump": "Jumping", "fall": "Floating",
                       "land": "Landing"},
             "behaviors": [{"kind": "hop", "hopChance": 0.8, "pauseMin": 0.3, "pauseMax": 0.8, "airTime": 1.2,
                            "apex": 2.4, "strength": 1.2, "homeRadius": 14, "goalMin": 1, "goalMax": 3}]}
        ]})");
    json["nodes"][1]["asset"] = (aliens / "alien-scout.glb").string();
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto c = scene::Composition::fromJson(json, registry);
    REQUIRE(c.has_value());
    scene::Composition& comp = **c;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    comp.attach(params, modulator);
    float worst = 0.0f;
    float worstAt = 0.0f;
    float liftMax = 0.0f;
    int landings = 0;
    for (int f = 0; f <= 30 * 40; ++f) { // ADR-1997: 40 s (the fixed step moved the landings; more of them)
        FrameTime t{};
        t.renderTime = f / 30.0;
        t.deltaTime = f == 0 ? 0.0 : 1.0 / 30.0;
        t.frameIndex = static_cast<std::uint64_t>(f);
        params.resetFinals();
        comp.updateFields(t, bus, modulator);
        modulator.applyRoutes(bus, params, t.deltaTime);
        comp.updateBehaviour(t, bus);
        comp.update(t);
        if (f < 30) continue;
        liftMax = std::max(liftMax, comp.lastCharacterLift());
        const scene::Scene& s = comp.scene();
        landings = static_cast<int>(s.environment.tileDynamics.springs.size());
        const scene::ProceduralGeometry* g = nullptr;
        for (const auto& p : s.procedurals) {
            if (p.distribution.generator.isTiles()) g = &p;
        }
        REQUIRE(g != nullptr);
        const float pitch = g->distribution.generator.cellSize;
        for (const scene::Entity& e : s.entities) {
            if (e.rig == scene::kInvalidRig || e.mesh >= s.meshes.size() || !s.meshes[e.mesh].skinned()) continue;
            const scene::MeshData& mesh = s.meshes[e.mesh];
            const scene::SkinnedRig& rig = s.rigs[e.rig];
            const glm::mat4 m = e.transform.matrix();
            for (std::size_t v = 0; v < mesh.vertices.size(); ++v) { // every vertex, not the pass's subset
                glm::vec3 p(0.0f);
                for (int k = 0; k < 4; ++k) {
                    const float w = mesh.skin[v].weights[k];
                    if (w > 0.0f && mesh.skin[v].joints[k] < rig.palette.size()) {
                        p += glm::vec3(rig.palette[mesh.skin[v].joints[k]] * glm::vec4(mesh.vertices[v].position, 1.0f)) * w;
                    }
                }
                const glm::vec3 world = glm::vec3(m * glm::vec4(p, 1.0f));
                const glm::vec2 cc = scene::tileCentre(pitch, scene::tileIndexAt(pitch, world.x, world.z));
                const float top = scene::tileSampleMoving(g->distribution.generator.tiles, s.environment.tileDynamics,
                                                          cc.x, cc.y, static_cast<float>(t.renderTime), pitch).height;
                if (top - world.y > worst) {
                    worst = top - world.y;
                    worstAt = static_cast<float>(t.renderTime);
                }
            }
        }
    }
    CAPTURE(worst, worstAt, liftMax, landings);
    WARN("ADR-1991: deepest pose below its tile before the lift " << liftMax * 100.0f << " cm; after, any vertex "
                                                                  << worst * 100.0f << " cm");
    CHECK(landings > 0);
    // the canary: without the pass the pose does sink (a landing crouch, a ringing tile). ADR-2006 (CPD-R13): a walker
    // now rises onto a higher tile before its feet reach the riser, so the deepest sink in this scene fell from 28 cm
    // to 1.9 cm; the pass is still exercised.
    CHECK(liftMax > 0.01f);
    CHECK(worst <= 0.02f);    // with it, no vertex is more than 2 cm inside the tile under it (the subset's margin)
}

// ADR-1997 (measure, hidden): the ground's rise under one alien against its throws, on a real scene. AVGEN_TOSS_SCENE is
// a scene file, AVGEN_TOSS_ENTITY the entity's index, AVGEN_TOSS_CSV where to write (t, rise m/s, airborne, throw).
TEST_CASE("crest throws on a real scene (measure)", "[.][tiles][hop][measure][adr1997]") {
    const char* scenePath = std::getenv("AVGEN_TOSS_SCENE");
    const char* csvPath = std::getenv("AVGEN_TOSS_CSV");
    if (scenePath == nullptr || csvPath == nullptr) SKIP("set AVGEN_TOSS_SCENE and AVGEN_TOSS_CSV");
    const int who = std::getenv("AVGEN_TOSS_ENTITY") != nullptr ? std::atoi(std::getenv("AVGEN_TOSS_ENTITY")) : 0;
    const double t0 = std::getenv("AVGEN_TOSS_FROM") != nullptr ? std::atof(std::getenv("AVGEN_TOSS_FROM")) : 0.0;
    const double t1 = std::getenv("AVGEN_TOSS_TO") != nullptr ? std::atof(std::getenv("AVGEN_TOSS_TO")) : 60.0;
    std::ifstream in(scenePath);
    REQUIRE(in);
    nlohmann::json json = nlohmann::json::parse(in);
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto c = scene::Composition::fromJson(json, registry);
    REQUIRE(c.has_value());
    scene::Composition& comp = **c;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    comp.attach(params, modulator);
    std::ofstream out(csvPath);
    out << "t,rise,airborne,activity,throw\n";
    bool wasAir = false;
    for (long long f = 0; f <= static_cast<long long>(t1 * 60.0); ++f) {
        FrameTime t{};
        t.renderTime = static_cast<double>(f) / 60.0;
        t.deltaTime = f == 0 ? 0.0 : 1.0 / 60.0;
        t.frameIndex = static_cast<std::uint64_t>(f);
        params.resetFinals();
        comp.updateFields(t, bus, modulator);
        modulator.applyRoutes(bus, params, t.deltaTime);
        comp.updateBehaviour(t, bus);
        comp.update(t);
        if (t.renderTime < t0) continue;
        const auto& es = comp.entityWorld().entities();
        REQUIRE(static_cast<std::size_t>(who) < es.size());
        const entity::EntityState& st = es[static_cast<std::size_t>(who)]->state();
        const glm::vec3 p = st.position();
        const world::GroundSource* g = comp.entityWorld().ground();
        REQUIRE(g != nullptr);
        const double h = 1.0 / 120.0;
        const double rise = (g->landscape(p.x, p.z, t.renderTime + h).height - g->landscape(p.x, p.z, t.renderTime - h).height) /
                            (2.0 * h);
        const bool thrown = st.airborne && !wasAir && st.activity == entity::Activity::Jump;
        out << t.renderTime << "," << rise << "," << (st.airborne ? 1 : 0) << "," << static_cast<int>(st.activity) << ","
            << (thrown ? 1 : 0) << "\n";
        wasAir = st.airborne;
    }
}

// ---- ADR-1999 (CPD-R4): a walker can keep off inland water's banks ------------------------------------------------

TEST_CASE("with avoidBanks a body keeps off a pond's bank; without it, it may stand there", "[tiles][hop][geography][adr1999]") {
    // a pond in the land beside the body's start
    scene::TileTerrainSpec spec;
    spec.continentWavelength = 4000.0f;
    spec.landThreshold = 0.05f; // land everywhere near the origin
    spec.ponds = {scene::TilePond{.name = "p", .centre = {12.0f, 0.0f}, .radius = 10.0f, .basinWidth = 12.0f}};
    spec.bankWidth = 8.0f;
    REQUIRE(spec.validate().has_value());
    spec.resolveWater();
    scene::TileGround g;
    g.set(spec, scene::TileDynamics{}, 4.0f, glm::vec3(0.0f));
    REQUIRE(g.planned(12.0f, 0.0f, 0.0).water > 0.99f);
    REQUIRE(g.planned(22.0f, 0.0f, 0.0).bank > 0.5f);
    const auto run = [&](float avoid) {
        nlohmann::json settings = {{"hopChance", 0.3}, {"pauseMin", 0.2}, {"pauseMax", 0.6}, {"homeRadius", 28.0},
                                   {"goalMin", 2}, {"goalMax", 5}};
        if (avoid > 0.0f) settings["avoidBanks"] = avoid;
        auto hop = entity::makeBehavior("hop", &settings);
        params::ParameterSet params;
        hop->registerParameters(params, "e/hop/");
        Rng rng(7);
        hop->reset(rng);
        entity::EntityState state;
        state.anchor = glm::vec3(-2.0f, 0.0f, 2.0f);
        entity::MotionOffset offset;
        int onBank = 0;
        int grounded = 0;
        for (int f = 0; f <= 60 * 90; ++f) {
            params.resetFinals();
            entity::BehaviorContext ctx;
            ctx.time = f / 60.0;
            ctx.dt = f == 0 ? 0.0 : 1.0 / 60.0;
            ctx.rng = &rng;
            ctx.ground = &g;
            hop->update(ctx, state, offset);
            const glm::vec3 p = state.position();
            if (!state.airborne) {
                ++grounded;
                onBank += g.planned(p.x, p.z, ctx.time).bank >= 0.3f ? 1 : 0;
            }
        }
        return std::pair{onBank, grounded};
    };
    const auto [free, freeGrounded] = run(0.0f);
    const auto [kept, keptGrounded] = run(0.3f);
    CAPTURE(free, freeGrounded, kept, keptGrounded);
    CHECK(freeGrounded > 600);
    CHECK(keptGrounded > 600);
    CHECK(free > 0);  // the bank is ground like any other without the setting
    CHECK(kept == 0); // and is kept off with it
}

// ---- CPD-R6: the trace's water column is the walker's own check ----------------------------------------------------

TEST_CASE("the water body a trace reports is the walker's own water check", "[tiles][nav][geography][cpdr6]") {
    // a coast (sea and land) with a pond and a river in the land, sampled the way the hop samples (planned, at the tile)
    scene::TileTerrainSpec spec;
    spec.continentWavelength = 4000.0f;
    spec.landThreshold = 0.05f;
    spec.ponds = {scene::TilePond{.name = "p", .centre = {40.0f, 0.0f}, .radius = 12.0f, .basinWidth = 10.0f}};
    spec.rivers = {scene::TileRiver{.name = "r", .points = {{-60.0f, -80.0f}, {-60.0f, 80.0f}}, .width = 10.0f,
                                    .valleyWidth = 30.0f, .bedDepth = 1.0f}};
    REQUIRE(spec.validate().has_value());
    spec.resolveWater();
    scene::TileGround g;
    g.set(spec, scene::TileDynamics{}, 4.0f, glm::vec3(0.0f));
    const entity::TileNavRules rules;
    const auto body = [&](float x, float z) { return entity::tileWaterBody(g.planned(x, z, 0.0), rules); };
    CHECK(body(40.0f, 0.0f) == 3);  // the pond
    CHECK(body(-60.0f, 0.0f) == 2); // the river
    CHECK(body(0.0f, 0.0f) == 0);   // land between them
    // a sea: the default continent near the origin has both
    scene::TileTerrainSpec coast;
    coast.continentWavelength = 120.0f;
    scene::TileGround sea;
    sea.set(coast, scene::TileDynamics{}, 4.0f, glm::vec3(0.0f));
    int seaTiles = 0;
    int dryTiles = 0;
    // and everywhere, on both grounds: wet (the walker's check) exactly when the body is not 0
    for (const scene::TileGround* ground : {&g, &sea}) {
        for (float z = -100.0f; z <= 100.0f; z += 4.0f) {
            for (float x = -100.0f; x <= 100.0f; x += 4.0f) {
                const world::GroundSample s = ground->planned(x, z, 0.0);
                const int b = entity::tileWaterBody(s, rules);
                CHECK(entity::tileWet(s, rules) == (b != 0));
                CHECK(entity::tileWalkAllowed(s, s, rules) == (b == 0));
                if (ground == &sea) {
                    seaTiles += b == 1 ? 1 : 0;
                    dryTiles += b == 0 ? 1 : 0;
                }
            }
        }
    }
    CHECK(seaTiles > 20);
    CHECK(dryTiles > 20);
}
