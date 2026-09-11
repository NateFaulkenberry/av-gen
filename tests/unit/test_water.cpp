// Water (ADR-091, spec §46): the claims the surface, the flow and the floating layers are built on.
//
// What is worth testing here is not "does it compile" but the four properties the rest of the
// system depends on and that are easy to break silently:
//   * downstream is the direction the authored level falls, whichever end the path was drawn from
//   * a body with no fall in it is still water, not a river running at zero
//   * the flow baked into a water vertex agrees with the body it was baked from
//   * a floating layer is a pure function of the clock: same second, same placement, always, and
//     nothing inside the bank

#include "scene/floaters.hpp"
#include "scene/water_surface.hpp"
#include "world/terrain.hpp"
#include "world/water.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

// A straight river descending from -Z to +Z, five metres of fall over a hundred of course.
world::WorldMap descendingRiver(bool reversed = false) {
    world::WorldMap map;
    map.name = "test";
    map.size = {200.0f, 200.0f};
    map.layers = {{0.02f, 2.0f, 0.0f, 0.0f}};
    world::Feature f;
    f.name = "run";
    f.kind = world::FeatureKind::River;
    f.width = 6.0f;
    f.amplitude = 3.0f;
    f.falloff = 1.2f;
    f.flatten = 0.0f;
    f.roughness = 0.2f;
    f.smoothing = 0;
    f.water = true;
    f.path = {{0.0f, 4.0f, -50.0f}, {0.0f, 2.0f, 0.0f}, {0.0f, -1.0f, 50.0f}};
    if (reversed) {
        std::reverse(f.path.begin(), f.path.end());
    }
    map.features.push_back(f);
    map.prepare();
    return map;
}

} // namespace

TEST_CASE("downstream is the direction the bed falls, whichever end it was drawn from", "[unit][water]") {
    // The one thing a river's flow direction must not depend on is which end an artist started
    // drawing from. Both of these are the same watercourse; both must run the same way.
    for (const bool reversed : {false, true}) {
        const world::WorldMap map = descendingRiver(reversed);
        const world::WaterBodySet set = world::waterBodies(map);
        REQUIRE(set.bodies.size() == 1);
        const world::WaterBody& body = set.bodies.front();
        CHECK(body.flowing);
        CHECK(body.course.kind == world::WaterKind::River);
        CHECK(body.course.descent > 4.0f); // five metres of fall, whichever way it was authored
        // Mid-channel, the flow points toward +Z, which is downhill.
        const world::FlowSample s = body.flowAt(glm::vec2(0.0f, 0.0f));
        INFO("reversed=" << reversed << " direction (" << s.direction.x << ", " << s.direction.y << ")");
        CHECK(s.direction.y > 0.9f);
        CHECK(s.inside);
        CHECK(s.speed > 0.0f);
        // And the head is upstream of the mouth.
        CHECK(body.centre().front().y > body.centre().back().y);
    }
}

TEST_CASE("a body with no fall in it is still water, not a river at zero", "[unit][water]") {
    // The tarn: a Valley feature holding water at one level. Nothing in its shape says "river", and
    // the derivation has to notice that rather than produce a direction out of float noise.
    world::WorldMap map;
    map.size = {200.0f, 200.0f};
    map.layers = {{0.02f, 2.0f, 0.0f, 0.0f}};
    world::Feature pond;
    pond.name = "tarn";
    pond.kind = world::FeatureKind::Flat;
    pond.path = {{0.0f, -3.0f, 0.0f}};
    pond.width = 20.0f;
    pond.amplitude = 6.0f;
    pond.water = true;
    pond.waterDepth = 1.2f;
    map.features.push_back(pond);
    map.prepare();

    const world::WaterBodySet set = world::waterBodies(map);
    REQUIRE(set.bodies.size() == 1);
    CHECK(!set.bodies.front().flowing);
    CHECK(set.bodies.front().course.kind == world::WaterKind::Pond);
    // A still body moves, slowly, in the wind's direction rather than in none at all: a mirror
    // reads as glass and the surface shader needs something to carry its pattern along.
    const world::FlowSample s = set.bodies.front().flowAt(glm::vec2(0.0f, 0.0f));
    CHECK(glm::length(s.direction) == Approx(1.0f).margin(1e-3f));
    CHECK(s.speed > 0.0f);
    CHECK(s.speed < set.settings.stillSpeed * 0.5f);
}

TEST_CASE("the water is fastest in the channel and slowest at the bank", "[unit][water]") {
    const world::WorldMap map = descendingRiver();
    const world::WaterBodySet set = world::waterBodies(map);
    const world::WaterBody& body = set.bodies.front();
    const float centre = body.flowAt(glm::vec2(0.0f, 0.0f)).speed;
    const float bank = body.flowAt(glm::vec2(body.halfWidth() * 0.95f, 0.0f)).speed;
    INFO("centre " << centre << " m/s, bank " << bank << " m/s");
    CHECK(centre > bank);
    CHECK(bank >= 0.0f);
    // And the sign of `across` says which bank, so a floating layer can sit on one side. Right is
    // downstream x up, which with +Y up and the course running toward +Z puts the right bank at
    // negative x -- the convention only has to be consistent, and this is where it is pinned.
    CHECK(body.flowAt(glm::vec2(-3.0f, 0.0f)).across > 0.0f);
    CHECK(body.flowAt(glm::vec2(3.0f, 0.0f)).across < 0.0f);
    CHECK(body.flowAt(glm::vec2(0.0f, 0.0f)).across == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("a water vertex carries the flow of the body under it", "[unit][water]") {
    // The seam between the geometry and the shader. buildChunkWater packs the downstream direction
    // into the vertex normal's xz and the speed fraction into its y, and the surface shader reads
    // it back with no other channel to check it against -- so this is the only place the two sides
    // can be compared.
    const world::WorldMap map = descendingRiver();
    const world::WaterBodySet bodies = world::waterBodies(map);
    world::TerrainSettings settings;
    settings.resolution = 16;
    settings.chunkSize = 40.0f;

    int checked = 0;
    float worstAngle = 0.0f;
    for (const glm::ivec2 coord : world::chunkGrid(map, settings)) {
        const scene::MeshData mesh = world::buildChunkWater(map, settings, coord, nullptr, &bodies);
        if (!mesh.valid()) {
            continue;
        }
        for (const scene::Vertex& v : mesh.vertices) {
            const glm::vec2 flow(v.normal.x, v.normal.z);
            if (glm::length(flow) < 1e-3f) {
                continue; // outside the channel: no flow was baked, which is a legal answer
            }
            const world::FlowSample s = bodies.flowAt(glm::vec2(v.position.x, v.position.z));
            const float dot = glm::dot(glm::normalize(flow), s.direction);
            worstAngle = std::max(worstAngle, 1.0f - dot);
            CHECK(v.normal.y >= 0.0f);
            CHECK(v.normal.y <= 1.0f);
            ++checked;
        }
    }
    INFO("checked " << checked << " vertices, worst 1 - cos " << worstAngle);
    CHECK(checked > 100);
    CHECK(worstAngle < 1e-3f);
}

TEST_CASE("without a body set the water vertex is the one it always was", "[unit][water]") {
    // Every world that predates ADR-091 passes no bodies, and its water must come out exactly as
    // it did: normal +Y, and no flow to carry a pattern along.
    const world::WorldMap map = world::defaultWorld();
    world::TerrainSettings settings;
    settings.resolution = 16;
    bool sawAny = false;
    for (const glm::ivec2 coord : world::chunkGrid(map, settings)) {
        const scene::MeshData mesh = world::buildChunkWater(map, settings, coord, nullptr, nullptr);
        if (!mesh.valid()) {
            continue;
        }
        sawAny = true;
        for (const scene::Vertex& v : mesh.vertices) {
            CHECK(v.normal == glm::vec3(0.0f, 1.0f, 0.0f));
        }
    }
    CHECK(sawAny);
}

TEST_CASE("a water vertex's depth is the metres of water over its bed", "[unit][water]") {
    const world::WorldMap map = world::defaultWorld();
    world::TerrainSettings settings;
    settings.resolution = 16;
    float deepest = 0.0f;
    for (const glm::ivec2 coord : world::chunkGrid(map, settings)) {
        const scene::MeshData mesh = world::buildChunkWater(map, settings, coord, nullptr, nullptr);
        if (!mesh.valid()) {
            continue;
        }
        for (const scene::Vertex& v : mesh.vertices) {
            CHECK(v.uv.x >= 0.0f);
            const float surface = map.waterSurface(glm::vec2(v.position.x, v.position.z));
            if (std::isfinite(surface) && v.uv.x > 0.01f) {
                // The vertex sits on the surface, and its depth lane is how far the bed is below.
                const float bed = map.height(glm::vec2(v.position.x, v.position.z));
                CHECK(v.uv.x == Approx(std::max(v.position.y - bed, 0.0f)).margin(0.05f));
            }
            deepest = std::max(deepest, v.uv.x);
        }
    }
    INFO("deepest water in the shipped world: " << deepest << " m");
    CHECK(deepest > 0.5f); // the depth lane is in metres now, not a 0..1 ratio
}

TEST_CASE("floating objects are a pure function of the timeline second", "[unit][water][floaters]") {
    // The property the whole placement is written for. An offline render starts wherever the range
    // says and must land on the same water as a live playback that got there by running; an
    // accumulating drift cannot promise it and this can.
    const world::WorldMap map = descendingRiver();
    const world::WaterBodySet bodies = world::waterBodies(map);
    scene::FloatSpec spec;
    spec.water = "terrain";
    spec.count = 64;
    spec.seed = 4242;

    std::vector<scene::Floater> a;
    std::vector<scene::Floater> b;
    scene::evaluateFloaters(bodies, spec, 37.5f, a);
    scene::evaluateFloaters(bodies, spec, 37.5f, b);
    REQUIRE(a.size() == 64);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].position == b[i].position);
        CHECK(a[i].scale == b[i].scale);
    }

    // And they move: the same instances at a later second are somewhere else.
    std::vector<scene::Floater> later;
    scene::evaluateFloaters(bodies, spec, 45.0f, later);
    std::size_t moved = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (glm::distance(a[i].position, later[i].position) > 0.25f) {
            ++moved;
        }
    }
    INFO(moved << " of " << a.size() << " moved over 7.5 s");
    CHECK(moved > a.size() / 2);
}

TEST_CASE("floating objects drift downstream and not in lockstep", "[unit][water][floaters]") {
    const world::WorldMap map = descendingRiver();
    const world::WaterBodySet bodies = world::waterBodies(map);
    scene::FloatSpec spec;
    spec.water = "terrain";
    spec.count = 120;
    spec.seed = 77;
    spec.clustering = 0.0f; // measure the drift, not the clumping

    std::vector<scene::Floater> a;
    std::vector<scene::Floater> b;
    scene::evaluateFloaters(bodies, spec, 0.0f, a);
    scene::evaluateFloaters(bodies, spec, 4.0f, b);

    // Every one of them travelled toward +Z, which is downhill on this course. Instances that
    // wrapped at the mouth are excluded rather than counted as travelling backwards a hundred
    // metres, which is what a wrap looks like from here.
    int forward = 0;
    int considered = 0;
    std::vector<float> distances;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const float dz = b[i].position.z - a[i].position.z;
        if (std::fabs(dz) > 20.0f) {
            continue;
        }
        ++considered;
        if (dz > 0.0f) {
            ++forward;
        }
        distances.push_back(dz);
    }
    REQUIRE(considered > 80);
    CHECK(forward == considered);

    // Not in lockstep: the spread of how far they went is a decent fraction of how far they went.
    const float mean =
        std::accumulate(distances.begin(), distances.end(), 0.0f) / static_cast<float>(distances.size());
    float spread = 0.0f;
    for (const float d : distances) {
        spread = std::max(spread, std::fabs(d - mean));
    }
    INFO("mean travel " << mean << " m in 4 s, worst deviation " << spread << " m");
    CHECK(mean > 0.5f);
    CHECK(spread > mean * 0.15f);
}

TEST_CASE("floating objects stay inside the water they float on", "[unit][water][floaters]") {
    const world::WorldMap map = descendingRiver();
    const world::WaterBodySet bodies = world::waterBodies(map);
    scene::FloatSpec spec;
    spec.water = "terrain";
    spec.count = 240;
    spec.seed = 5150;
    spec.lateral = 0.9f;
    spec.margin = 0.2f;

    const world::WaterBody& body = bodies.bodies.front();
    for (const float t : {0.0f, 3.5f, 11.0f, 60.0f, 240.0f}) {
        std::vector<scene::Floater> f;
        scene::evaluateFloaters(bodies, spec, t, f);
        REQUIRE(f.size() == 240);
        for (const scene::Floater& one : f) {
            const world::FlowSample s = body.flowAt(glm::vec2(one.position.x, one.position.z));
            INFO("t=" << t << " at (" << one.position.x << ", " << one.position.z << ") distance "
                      << s.distance << " of half width " << body.halfWidth());
            // Inside the nominal channel, with the authored margin kept clear of the bank.
            CHECK(s.distance <= body.halfWidth() * (spec.lateral - spec.margin) + 1e-3f);
            // And sitting on the surface, not under the bed or in the air over it.
            CHECK(std::fabs(one.position.y - (s.surface - spec.sink)) <= spec.bob + 1e-3f);
        }
    }
}

TEST_CASE("a floating layer with no water places nothing and says why", "[unit][water][floaters]") {
    // The failure this codebase keeps repeating is a system that runs, places nothing and reports
    // success. A spec with no water named is rejected at load rather than at the first empty frame.
    scene::FloatSpec spec;
    CHECK(!spec.validate());
    spec.water = "valley";
    CHECK(spec.validate());

    // And with a valid spec but no bodies, the result is empty rather than undefined.
    std::vector<scene::Floater> f;
    scene::evaluateFloaters(world::WaterBodySet{}, spec, 1.0f, f);
    CHECK(f.empty());
}

TEST_CASE("water settings round-trip and reject nonsense", "[unit][water]") {
    scene::WaterSettings w;
    CHECK(w.validate());
    w.clarity = 0.0f;
    CHECK(!w.validate());
    w = scene::WaterSettings{};
    w.glowCoverage = 1.5f;
    CHECK(!w.validate());

    world::WaterFlowSettings flow;
    CHECK(flow.validate());
    flow.bankShear = 2.0f;
    CHECK(!flow.validate());

    const nlohmann::json j = world::waterFlowToJson(world::WaterFlowSettings{});
    auto parsed = world::waterFlowFromJson(j);
    REQUIRE(parsed);
    CHECK(parsed->speedScale == Approx(world::WaterFlowSettings{}.speedScale));
    CHECK(parsed->structuralHash() == world::WaterFlowSettings{}.structuralHash());
}

TEST_CASE("a float spec round-trips through JSON", "[unit][water][floaters]") {
    scene::FloatSpec spec;
    spec.water = "valley";
    spec.body = "glowmere-run";
    spec.count = 33;
    spec.clustering = 0.4f;
    spec.driftSpread = 0.5f;
    auto parsed = scene::FloatSpec::fromJson(spec.toJson());
    REQUIRE(parsed);
    CHECK(parsed->structuralHash() == spec.structuralHash());
}
