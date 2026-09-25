// BOLT (Effect Library Wave 3): the seeded fractal path generator and the four types drawn from it.
//
// The generator's contract: the same key is the same path bit for bit (however often, in whatever
// order, cached or not); a lower depth is a prefix of a higher one; the main channel's ends are hit
// exactly; the vertex cap and the branch bounds hold for every parameter; and the four types report
// their budgets -- ribbon vertices and strips, LIGHTMOD's 16 lights, FXL's exclusive vein block --
// as Dropped or Partial with a reason. The play = scrub proof on a whole engine is at the end.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "world/effects/bolt_path.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/effects/effect_trigger.hpp"
#include "world/effects/entity_fx.hpp"
#include "world/effects/history_bank.hpp"
#include "world/effects/ribbon_frame.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace avgen;

namespace {

bool sameBytes(const world::BoltPath& a, const world::BoltPath& b) {
    return a.vertices.size() == b.vertices.size() && a.paths.size() == b.paths.size() &&
           std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(world::BoltVertex)) == 0 &&
           std::memcmp(a.paths.data(), b.paths.data(), a.paths.size() * sizeof(world::BoltBranch)) == 0;
}

world::BoltParams busy() {
    world::BoltParams p;
    p.depth = 8;
    p.jaggedness = 0.3f;
    p.branchProbability = 1.0f;
    p.branchDecay = 0.7f;
    p.generations = 3;
    return p;
}

} // namespace

TEST_CASE("a bolt is a pure function of its key: the same key is the same path, bit for bit", "[bolt]") {
    const world::BoltParams p = busy();
    world::BoltPath a;
    world::BoltPath b;
    world::generateBolt(p, 1234, 7, a);
    // Ask for others in between, and ask again: nothing carries over from one call to the next.
    world::BoltPath other;
    world::generateBolt(p, 99, 3, other);
    world::generateBolt(p, 1234, 8, other);
    world::generateBolt(p, 1234, 7, b);
    REQUIRE(a.vertices.size() > 100);
    CHECK(sameBytes(a, b));

    // Through the cache: a miss, a hit and a miss after eviction all give the same bytes.
    world::BoltCache cache;
    const world::BoltPath& first = cache.get(p, 1234, 7);
    CHECK(sameBytes(first, a));
    CHECK(cache.misses() == 1);
    CHECK(sameBytes(cache.get(p, 1234, 7), a));
    CHECK(cache.hits() == 1);
    for (std::uint32_t i = 0; i < world::BoltCache::kCapacity + 5; ++i) {
        static_cast<void>(cache.get(p, 1234, 1000 + i)); // evicts the first entry
    }
    CHECK(sameBytes(cache.get(p, 1234, 7), a));

    // A different seed, index or parameter is a different bolt.
    world::BoltPath c;
    world::generateBolt(p, 1235, 7, c);
    CHECK_FALSE(sameBytes(a, c));
    world::generateBolt(p, 1234, 6, c);
    CHECK_FALSE(sameBytes(a, c));
    world::BoltParams q = p;
    q.jaggedness = 0.31f;
    world::generateBolt(q, 1234, 7, c);
    CHECK_FALSE(sameBytes(a, c));
    // ...and the cache keeps two parameter sets apart even for the same (seed, index).
    CHECK(sameBytes(cache.get(q, 1234, 7), c));
    CHECK(sameBytes(cache.get(p, 1234, 7), a));
}

TEST_CASE("a bolt's main channel starts and ends exactly on its endpoints", "[bolt]") {
    for (std::uint32_t index = 0; index < 20; ++index) {
        world::BoltPath path;
        world::generateBolt(busy(), 42, index, path);
        const std::span<const world::BoltVertex> main = path.path(0);
        CHECK(main.front().position == glm::vec3(0.0f, 0.0f, 0.0f));
        CHECK(main.back().position == glm::vec3(0.0f, 0.0f, 1.0f));
        // Placed, bit for bit on the world endpoints, whatever the chord's direction.
        world::BoltPlacement placement;
        placement.start = glm::vec3(3.25f, 90.5f, -7.0f);
        placement.end = glm::vec3(-11.0f + static_cast<float>(index), 0.125f, 40.0f);
        placement.bow = glm::vec3(0.0f, -4.0f, 0.0f);
        CHECK(placement.place(main.front().position) == placement.start);
        CHECK(placement.place(main.back().position) == placement.end);
        // Every branch starts ON the channel it leaves.
        for (std::size_t i = 1; i < path.paths.size(); ++i) {
            const world::BoltBranch& br = path.paths[i];
            CHECK(path.vertices[br.first].position == path.vertices[br.root].position);
        }
    }
}

TEST_CASE("a bolt never exceeds its vertex cap, and its branches stay inside their bounds", "[bolt]") {
    std::size_t most = 0;
    std::size_t mostBranches = 0;
    for (int depth = 1; depth <= 8; ++depth) {
        for (int gens = 0; gens <= 3; ++gens) {
            for (const float prob : {0.0f, 0.3f, 1.0f}) {
                for (const std::uint32_t cap : {world::kBoltMaxVertices, 300u, 40u}) {
                    world::BoltParams p;
                    p.depth = depth;
                    p.generations = gens;
                    p.branchProbability = prob;
                    p.branchDecay = 0.9f;
                    p.jaggedness = 0.4f;
                    p.maxVertices = cap;
                    for (std::uint64_t seed = 1; seed <= 6; ++seed) {
                        world::BoltPath path;
                        world::generateBolt(p, seed, 3, path);
                        INFO("depth " << depth << " generations " << gens << " p " << prob << " cap " << cap);
                        const std::uint32_t mainCount = (1u << depth) + 1u;
                        REQUIRE(path.paths.size() >= 1);
                        CHECK(path.paths[0].count == mainCount); // the channel always fits
                        CHECK(path.vertices.size() <= std::max<std::size_t>(cap, mainCount));
                        CHECK(path.vertices.size() <= world::kBoltMaxVertices);
                        CHECK(path.paths.size() - 1 <= world::kBoltMaxBranches);
                        if (prob == 0.0f || gens == 0) {
                            CHECK(path.paths.size() == 1);
                        }
                        std::size_t total = 0;
                        for (const world::BoltBranch& br : path.paths) {
                            CHECK(static_cast<int>(br.generation) <= gens);
                            total += br.count;
                        }
                        CHECK(total == path.vertices.size());
                        most = std::max(most, path.vertices.size());
                        mostBranches = std::max(mostBranches, path.paths.size() - 1);
                    }
                }
            }
        }
    }
    // The control: the bounds are reached, so the checks above are about real limits.
    CHECK(most > 400);
    CHECK(mostBranches >= 8);
}

namespace {

// The shape rules a bolt must obey to read as lightning rather than a random-walk scribble. Every
// path (the channel and every branch) advances along its own start -> end chord at every vertex, and
// no two consecutive segments turn by more than `kMaxTurnDegrees`. Returns the first violation.
constexpr float kMaxTurnDegrees = 75.0f;

std::string shapeViolation(const world::BoltPath& path) {
    const float cosCap = std::cos(glm::radians(kMaxTurnDegrees));
    for (std::size_t i = 0; i < path.paths.size(); ++i) {
        const std::span<const world::BoltVertex> vs = path.path(i);
        const glm::vec3 chord = vs.back().position - vs.front().position;
        const float len2 = glm::dot(chord, chord);
        if (len2 <= 0.0f) {
            return "path " + std::to_string(i) + " has no chord";
        }
        float last = -1.0f;
        for (std::size_t k = 0; k < vs.size(); ++k) {
            const float u = glm::dot(vs[k].position - vs.front().position, chord) / len2;
            if (k > 0 && !(u > last)) {
                return "path " + std::to_string(i) + " vertex " + std::to_string(k) + " goes back along its chord (" +
                       std::to_string(last) + " -> " + std::to_string(u) + ")";
            }
            last = u;
        }
        for (std::size_t k = 1; k + 1 < vs.size(); ++k) {
            const glm::vec3 a = glm::normalize(vs[k].position - vs[k - 1].position);
            const glm::vec3 b = glm::normalize(vs[k + 1].position - vs[k].position);
            if (glm::dot(a, b) < cosCap) {
                return "path " + std::to_string(i) + " turns " +
                       std::to_string(glm::degrees(std::acos(std::clamp(glm::dot(a, b), -1.0f, 1.0f)))) +
                       " degrees at vertex " + std::to_string(k);
            }
        }
    }
    return {};
}

} // namespace

TEST_CASE("a bolt always advances towards its end and never turns sharply (no backtracking or loops)",
          "[bolt]") {
    std::size_t checked = 0;
    for (int depth = 2; depth <= 8; ++depth) {
        for (const float jag : {0.1f, 0.26f, 0.5f, 1.0f}) {
            world::BoltParams p;
            p.depth = depth;
            p.jaggedness = jag;
            p.branchProbability = 0.8f;
            p.branchDecay = 0.6f;
            p.generations = 2;
            for (std::uint64_t seed = 1; seed <= 40; ++seed) {
                world::BoltPath path;
                world::generateBolt(p, seed, static_cast<std::uint32_t>(seed * 7), path);
                const std::string bad = shapeViolation(path);
                INFO("depth " << depth << " jaggedness " << jag << " seed " << seed << ": " << bad);
                CHECK(bad.empty());
                checked += path.paths.size();
            }
        }
    }
    // An Arc's cross-fade between two shapes obeys the same rules at every instant of the blend.
    world::BoltParams arc;
    arc.depth = 6;
    arc.jaggedness = 0.3f;
    arc.branchProbability = 0.3f;
    arc.generations = 1;
    for (std::uint32_t k = 0; k < 20; ++k) {
        world::BoltPath a;
        world::BoltPath b;
        world::BoltPath out;
        world::generateBolt(arc, 11, k, a);
        world::generateBolt(arc, 11, k + 1, b);
        for (const float f : {0.25f, 0.5f, 0.75f}) {
            world::blendBolts(a, b, f, out);
            const std::string bad = shapeViolation(out);
            INFO("arc step " << k << " blend " << f << ": " << bad);
            CHECK(bad.empty());
        }
    }
    CHECK(checked > 1000); // the control: many paths, branches included
}

TEST_CASE("a lower depth is a prefix of a higher one: level of detail keeps the shape", "[bolt]") {
    // Below the vertex cap (which, when it binds, decides how many branches fit at each depth).
    world::BoltParams fine = busy();
    fine.depth = 7;
    fine.branchProbability = 0.45f;
    fine.generations = 1;
    world::BoltParams coarse = fine;
    coarse.depth = 4;
    world::BoltPath a;
    world::BoltPath b;
    world::generateBolt(fine, 77, 5, a);
    world::generateBolt(coarse, 77, 5, b);
    const std::span<const world::BoltVertex> ma = a.path(0);
    const std::span<const world::BoltVertex> mb = b.path(0);
    REQUIRE(mb.size() == 17);
    for (std::size_t k = 0; k < mb.size(); ++k) {
        INFO("coarse vertex " << k);
        CHECK(mb[k].position == ma[k * 8].position);
    }
    // The same branches spring from the same places.
    std::vector<glm::vec3> rootsA;
    std::vector<glm::vec3> rootsB;
    for (std::size_t i = 1; i < a.paths.size(); ++i) {
        rootsA.push_back(a.vertices[a.paths[i].root].position);
    }
    for (std::size_t i = 1; i < b.paths.size(); ++i) {
        rootsB.push_back(b.vertices[b.paths[i].root].position);
    }
    REQUIRE_FALSE(rootsA.empty());
    CHECK(rootsA == rootsB);
}

TEST_CASE("an arc's cross-fade holds the displacement's size and ends on each shape", "[bolt]") {
    world::BoltParams p;
    p.depth = 6;
    p.jaggedness = 0.25f;
    p.branchProbability = 0.0f;
    world::BoltPath a;
    world::BoltPath b;
    world::generateBolt(p, 5, 1, a);
    world::generateBolt(p, 5, 2, b);
    world::BoltPath out;
    world::blendBolts(a, b, 0.0f, out);
    for (std::size_t k = 0; k < a.vertices.size(); ++k) {
        CHECK(glm::length(out.vertices[k].position - a.vertices[k].position) < 1e-6f);
    }
    world::blendBolts(a, b, 1.0f, out);
    for (std::size_t k = 0; k < b.vertices.size(); ++k) {
        CHECK(glm::length(out.vertices[k].position - b.vertices[k].position) < 1e-6f);
    }
    const auto rms = [](const world::BoltPath& path) {
        double sum = 0.0;
        const std::size_t n = path.paths[0].count;
        for (std::size_t k = 0; k < n; ++k) {
            const glm::vec3 d = path.vertices[k].position - glm::vec3(0.0f, 0.0f, float(k) / float(n - 1));
            sum += glm::dot(d, d);
        }
        return std::sqrt(sum / static_cast<double>(n));
    };
    world::blendBolts(a, b, 0.5f, out);
    const double ends = 0.5 * (rms(a) + rms(b));
    INFO("rms displacement: a " << rms(a) << ", b " << rms(b) << ", half-way " << rms(out));
    CHECK(rms(out) > 0.8 * ends); // a plain lerp would be ~0.71 of it
}

// ---- the types ------------------------------------------------------------------------------------

namespace {

// An EffectSceneQuery with one node, `craft`, at a fixed place with a 2 m box and one entity.
class OneNode final : public world::EffectSceneQuery {
public:
    glm::vec3 at{0.0f, 5.0f, 0.0f};
    [[nodiscard]] bool nodePosition(std::string_view name, glm::vec3& out) const override {
        if (name != "craft") {
            return false;
        }
        out = at;
        return true;
    }
    [[nodiscard]] bool nodeView(std::string_view name, world::NodeView& out) const override {
        if (name != "craft") {
            return false;
        }
        out = world::NodeView{};
        out.world[3] = glm::vec4(at, 1.0f);
        out.boundsMin = at - glm::vec3(1.0f);
        out.boundsMax = at + glm::vec3(1.0f);
        out.hasBounds = true;
        out.entityCount = 1;
        return true;
    }
};

world::EffectInstance boltEffect(world::EffectKind kind, std::string id, world::EffectOwner owner) {
    world::EffectInstance e = world::makeEffect(kind, id);
    e.id = std::move(id);
    e.owner = std::move(owner);
    // A ready-made instance is made for the World, whose Owner source becomes the focus hero
    // (`adaptEffectToOwner`); re-attached to an entity, it strikes its owner again.
    e.wave.source.kind = world::SourceKind::Owner;
    e.activation = world::Activation::Always;
    e.timing = world::Timing{};
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    return e;
}

struct Frame {
    std::vector<world::EffectStatus> status;
    std::vector<std::string> reasons;
    world::EntityFxFrame fx;
    world::RibbonFrame ribbons;
};

// The two builders the bolt types reach, in the engine's order: FXL (lanes and the light pool), then
// RIBBON. Reasons start empty, as the engine clears them every frame.
Frame build(const std::vector<world::EffectInstance>& effects, const world::EffectContext& ctx) {
    Frame f;
    f.status.assign(effects.size(), world::EffectStatus::Dormant);
    f.reasons.assign(effects.size(), std::string());
    world::HistoryBank history;
    world::buildEntityFxFrame(effects, ctx, f.fx, {}, f.status, f.reasons);
    world::buildRibbonFrame(effects, ctx, history, f.ribbons, {}, f.status, f.reasons);
    return f;
}

} // namespace

TEST_CASE("the four bolt types draw through RIBBON, and a lightning strike flashes through LIGHTMOD", "[bolt]") {
    OneNode scene;
    world::EffectContext ctx;
    ctx.seconds = 0.12; // the return stroke of a strike released at 0
    ctx.scene = &scene;
    ctx.cameraPosition = glm::vec3(0.0f, 20.0f, 80.0f);
    const world::EffectOwner craft = world::EffectOwner::entity("craft");
    std::vector<world::EffectInstance> effects{
        boltEffect(world::EffectKind::Lightning, "strike", craft),
        boltEffect(world::EffectKind::Arc, "arc", craft),
        boltEffect(world::EffectKind::ElectricField, "field", craft),
        boltEffect(world::EffectKind::Discharge, "burst", craft),
    };
    const Frame f = build(effects, ctx);
    for (std::size_t i = 0; i < effects.size(); ++i) {
        INFO(effects[i].id << ": " << world::effectStatusName(f.status[i]) << " " << f.reasons[i]);
        CHECK(f.status[i] == world::EffectStatus::Drawn);
    }
    // Two strips per bolt (glow and core) at least, and the field's crackle is one FXL record.
    CHECK(f.ribbons.strips.size() >= 8);
    CHECK(f.fx.records.size() >= 2);
    CHECK((static_cast<std::uint32_t>(f.fx.records[1].lanes[world::kFxLaneA].z) & world::kFxVeins) != 0);
    // The strike's and the discharge's flashes are in the pool.
    CHECK(f.fx.lights.count == 2);
    // The strike lands on the craft's box, not at its centre: the bolt's lowest point is its top.
    float lowest = 1e9f;
    for (const world::RibbonVertex& v : f.ribbons.vertices) {
        lowest = std::min(lowest, v.positionSide.y);
    }
    CHECK(lowest < 6.5f); // reaches down to the craft (top of the box at y = 6)...
}

TEST_CASE("a bolt type out of ribbon room is Dropped, and one whose flash lost is Partial, each with a reason",
          "[bolt]") {
    world::EffectContext ctx;
    ctx.seconds = 0.12;
    ctx.cameraPosition = glm::vec3(0.0f, 20.0f, 80.0f);
    // 140 World-owned strikes: two strips each, so the 129th finds the 256 strips taken.
    std::vector<world::EffectInstance> effects;
    for (int i = 0; i < 140; ++i) {
        world::EffectInstance e = boltEffect(world::EffectKind::Lightning, "s" + std::to_string(i), world::EffectOwner::world());
        world::EffectEndpoint src;
        src.kind = world::SourceKind::World;
        src.position = glm::vec3(static_cast<float>(i) * 3.0f, 0.0f, 0.0f);
        e.wave.source = src;
        e.values.setFloat("lightning/depth", 5.0f); // small, so strips run out before vertices do
        e.values.setFloat("lightning/branchProbability", 0.0f);
        effects.push_back(e);
    }
    const Frame f = build(effects, ctx);
    std::size_t drawn = 0;
    std::size_t partial = 0;
    std::size_t dropped = 0;
    for (std::size_t i = 0; i < effects.size(); ++i) {
        switch (f.status[i]) {
        case world::EffectStatus::Drawn: ++drawn; break;
        case world::EffectStatus::Partial:
            ++partial;
            CHECK(f.reasons[i].find("light budget") != std::string::npos);
            break;
        case world::EffectStatus::Dropped:
            ++dropped;
            CHECK(f.reasons[i].find("ribbon budget") != std::string::npos);
            break;
        default: FAIL("status " << world::effectStatusName(f.status[i]) << " for " << effects[i].id);
        }
    }
    INFO(drawn << " drawn, " << partial << " partial, " << dropped << " dropped");
    CHECK(f.fx.lights.count == world::kEffectLightBudget);
    CHECK(drawn == world::kEffectLightBudget); // the 16 whose flash won, and whose strips fitted
    CHECK(dropped == 140 - world::kMaxRibbonStrips / 2);
    CHECK(partial == 140 - dropped - drawn);
    CHECK(f.ribbons.strips.size() == world::kMaxRibbonStrips);
}

TEST_CASE("an Electric Field on an owner whose vein block is taken keeps its arcs and says why it is Partial",
          "[bolt]") {
    OneNode scene;
    world::EffectContext ctx;
    ctx.seconds = 0.03;
    ctx.scene = &scene;
    const world::EffectOwner craft = world::EffectOwner::entity("craft");
    std::vector<world::EffectInstance> effects{
        boltEffect(world::EffectKind::PulsingVeins, "veins", craft),
        boltEffect(world::EffectKind::ElectricField, "field", craft),
    };
    effects[0].order = 0;
    effects[1].order = 1;
    const Frame f = build(effects, ctx);
    CHECK(f.status[0] == world::EffectStatus::Drawn);
    INFO(f.reasons[1]);
    CHECK(f.status[1] == world::EffectStatus::Partial);
    CHECK(f.reasons[1].find("veins") != std::string::npos);
}

TEST_CASE("a triggered bolt type waiting for its first event is Dormant with the reason", "[bolt]") {
    OneNode scene;
    world::EffectContext ctx;
    ctx.seconds = 3.0;
    ctx.scene = &scene;
    world::TriggerClock clock; // no beats: nothing has fired
    clock.setFrame(3.0);
    ctx.triggers = &clock;
    world::EffectInstance e = world::makeEffect(world::EffectKind::Discharge, "burst");
    e.id = "burst";
    e.owner = world::EffectOwner::entity("craft");
    const Frame f = build({e}, ctx);
    CHECK(f.status[0] == world::EffectStatus::Dormant);
    CHECK_FALSE(f.reasons[0].empty());
    CHECK(f.ribbons.strips.empty());
    CHECK(f.fx.lights.count == 0);
}

// ---- play = scrub ------------------------------------------------------------------------------------

namespace {

void frameAt(app::Engine& engine, long long frame) {
    engine.update(FrameTime{static_cast<double>(frame) / 60.0, frame == 0 ? 0.0 : 1.0 / 60.0,
                            static_cast<std::uint64_t>(frame)});
}

// A craft flown by a keyed position track (motion the Engine's seek replays exactly), struck by a
// Lightning on a Repeat trigger and carrying a Discharge on another.
void installStruckCraft(app::Engine& engine) {
    REQUIRE(engine.setCompositionJson(nlohmann::json::parse(R"({ "format": "avgen-scene", "version": 1,
        "name": "struck",
        "camera": { "mode": 1, "position": [0.0, 12.0, 60.0], "target": [0.0, 6.0, 0.0], "fov": 50.0, "orbitSpeed": 0.0 },
        "nodes": [ { "kind": "orb", "name": "craft", "position": [0, 5, 0] } ] })"))
                .has_value());
    const world::EffectOwner craft = world::EffectOwner::entity("craft");
    std::vector<world::EffectInstance> list;
    world::EffectInstance strike = world::makeEffect(world::EffectKind::Lightning, "Lightning");
    strike.id.clear();
    strike.owner = craft;
    strike.wave.source.kind = world::SourceKind::Owner;
    REQUIRE(world::applyEffectStyle(strike, world::EffectKind::Lightning, "Storm Strike"));
    strike.timing.trigger.source = world::TriggerSource::Repeat;
    strike.timing.trigger.period = 1.3;
    strike.timing.trigger.phase = 0.2;
    world::EffectInstance burst = world::makeEffect(world::EffectKind::Discharge, "Discharge");
    burst.id.clear();
    burst.owner = craft;
    burst.wave.source.kind = world::SourceKind::Owner;
    REQUIRE(world::applyEffectStyle(burst, world::EffectKind::Discharge, "Overload"));
    burst.timing.trigger.source = world::TriggerSource::Repeat;
    burst.timing.trigger.period = 0.9;
    burst.timing.trigger.phase = 0.1;
    REQUIRE(world::insertEffect(list, std::move(strike)).has_value());
    REQUIRE(world::insertEffect(list, std::move(burst)).has_value());
    REQUIRE(engine.setEffects(list).has_value());
    params::Track fly;
    fly.target = "nodes/craft/position";
    fly.keys.push_back(params::Key{.time = 0.0, .value = {0.0f, 5.0f, 0.0f, 0.0f}});
    fly.keys.push_back(params::Key{.time = 4.0, .value = {30.0f, 9.0f, -10.0f, 0.0f}});
    fly.keys.push_back(params::Key{.time = 8.0, .value = {-20.0f, 6.0f, 25.0f, 0.0f}});
    engine.timeline().addTrack(fly);
    REQUIRE(engine.timeline().bind(engine.params()).has_value());
}

template <typename T>
bool sameVector(const std::vector<T>& a, const std::vector<T>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
}

} // namespace

// The Wave 3 exactness proof: a triggered strike (and a discharge's bolts and ballistic sparks, released
// where the moving craft WAS) is the same ribbon, byte for byte, and the same flash, at a second
// reached by playing and by scrubbing.
TEST_CASE("a triggered Lightning and Discharge on a moving owner are the same played and scrubbed",
          "[bolt][seek][determinism]") {
    for (const long long target : {331LL, 337LL, 407LL}) { // strokes, a restrike, sparks mid-flight
        INFO("frame " << target);
        app::Engine played(app::EngineMode::Offline);
        installStruckCraft(played);
        for (long long f = 0; f <= target; ++f) {
            frameAt(played, f);
        }
        app::Engine scrubbed(app::EngineMode::Offline);
        installStruckCraft(scrubbed);
        frameAt(scrubbed, 0);
        scrubbed.seekSeconds(static_cast<double>(target - 1) / 60.0);
        frameAt(scrubbed, target);

        const scene::Scene& a = played.scene();
        const scene::Scene& b = scrubbed.scene();
        // The camera is an input (a bolt's level of detail and its glow's end-on fade read it), and
        // this fixture's is fixed, so both arrive at the same one.
        REQUIRE(a.camera.position == b.camera.position);
        REQUIRE_FALSE(a.ribbons.strips.empty()); // the strike or the burst is live: not a trivial equality
        for (const world::EffectInstance& e : played.effects()) {
            INFO(e.id << " " << world::effectStatusName(played.effectStatus(e.id)));
            CHECK(played.effectStatus(e.id) == scrubbed.effectStatus(e.id));
        }
        CHECK(sameVector(a.ribbons.strips, b.ribbons.strips));
        CHECK(sameVector(a.ribbons.vertices, b.ribbons.vertices));
        CHECK(a.entityFx.lights.count == b.entityFx.lights.count);
        CHECK(std::memcmp(a.entityFx.lights.lights.data(), b.entityFx.lights.lights.data(),
                          sizeof(world::EffectLight) * a.entityFx.lights.count) == 0);
    }
}
