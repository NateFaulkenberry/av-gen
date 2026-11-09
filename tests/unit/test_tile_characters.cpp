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
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"
#include "scene/tile_ground.hpp"
#include "scene/tile_terrain.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
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

TEST_CASE("the springs keep only the newest eight that are still ringing", "[tiles][springs][geography]") {
    scene::TileDynamics d;
    for (int i = 0; i < 12; ++i) {
        d.springs.push_back({0.1f * static_cast<float>(i), {static_cast<float>(i), 0.0f}, 1.0f});
    }
    const auto now = d.springsAt(1.15f);
    REQUIRE(now.size() == scene::kMaxTileSprings);
    CHECK(now.front().time == Approx(1.1f));
    CHECK(d.springsAt(0.05f).size() == 1);
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
                "waves": [ {"amplitude": 0.4, "wavelength": 36, "speed": 5} ],
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
                {"name": "a", "node": "a", "seed": 5, "behaviors": [{"kind": "hop", "hopChance": 0.5, "pauseMax": 1.5}]},
                {"name": "b", "node": "b", "seed": 9, "behaviors": [{"kind": "hop", "hopChance": 0.5, "pauseMax": 1.5}]}
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
