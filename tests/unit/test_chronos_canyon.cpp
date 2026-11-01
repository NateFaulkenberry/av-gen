// ADR-1933: CHRONOS's canyon block -- deterministic, watertight, the river in its channel, and a JSON round trip.

#include "scene/chronos_canyon.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <map>
#include <utility>

using namespace avgen;

namespace {

scene::ChronosCanyonGen smallGen(std::uint32_t seed) {
    scene::ChronosCanyonGen g;
    g.seed = seed;
    g.resolution = 512;
    g.extent = 4000.0f;
    g.river = {{-1800.0f, -300.0f}, {-600.0f, 200.0f}, {300.0f, -250.0f}, {1800.0f, 150.0f}};
    g.rimHalfWidth = 300.0f;
    g.depth = 160.0f;
    g.rangeStart = 900.0f;
    g.rangeEnd = 1800.0f;
    g.spires = {{{-600.0f, 420.0f}, 50.0f, 15.0f}};
    g.meshSpacing = 8.0f;
    return g;
}

std::uint64_t meshHash(const scene::ChronosCanyonMesh& m) {
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](const void* data, std::size_t bytes) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < bytes; ++i) h = (h ^ p[i]) * 1099511628211ull;
    };
    for (const auto& ch : m.chunks) {
        mix(ch.positions.data(), ch.positions.size() * sizeof(glm::vec3));
        mix(ch.uvs.data(), ch.uvs.size() * sizeof(glm::vec2));
        mix(ch.indices.data(), ch.indices.size() * sizeof(std::uint32_t));
    }
    return h;
}

} // namespace

TEST_CASE("the canyon is a pure function of its knobs", "[chronos-canyon][adr1933]") {
    const auto a = scene::generateChronosCanyon(smallGen(5));
    const auto b = scene::generateChronosCanyon(smallGen(5));
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(a->mesh != nullptr);
    CHECK(a->height == b->height);
    CHECK(meshHash(*a->mesh) == meshHash(*b->mesh));
    const auto c = scene::generateChronosCanyon(smallGen(6));
    REQUIRE(c != nullptr);
    CHECK(a->height != c->height);
    CHECK(smallGen(5).hash() == smallGen(5).hash());
    CHECK(smallGen(5).hash() != smallGen(6).hash());
    // the cache hands back the same world for the same knobs without generating
    const auto before = scene::chronosCanyonGenerations();
    const auto x = scene::acquireChronosCanyon(smallGen(9));
    const auto y = scene::acquireChronosCanyon(smallGen(9));
    CHECK(x == y);
    CHECK(scene::chronosCanyonGenerations() == before + 1);
}

TEST_CASE("the canyon mesh is watertight and wound upward", "[chronos-canyon][adr1933]") {
    const auto w = scene::generateChronosCanyon(smallGen(3));
    REQUIRE(w != nullptr);
    const auto& m = *w->mesh;
    CHECK(m.triangles > 1000u);
    std::map<std::pair<std::uint64_t, std::uint64_t>, int> uses;
    const auto key = [](const glm::vec3& p) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(std::lround(p.x * 4.0f))) << 32) |
               static_cast<std::uint32_t>(std::lround(p.z * 4.0f));
    };
    const float lo = -0.5f * w->extent, hi = 0.5f * w->extent;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::pair<glm::vec3, glm::vec3>> ends;
    std::size_t downward = 0;
    for (const auto& ch : m.chunks) {
        for (std::size_t i = 0; i + 2 < ch.indices.size(); i += 3) {
            const glm::vec3 a = ch.positions[ch.indices[i]], b = ch.positions[ch.indices[i + 1]], c = ch.positions[ch.indices[i + 2]];
            if (glm::cross(b - a, c - a).y < 0.0f) ++downward;
            for (int e = 0; e < 3; ++e) {
                const glm::vec3 p = ch.positions[ch.indices[i + e]], q = ch.positions[ch.indices[i + (e + 1) % 3]];
                std::uint64_t ka = key(p), kb = key(q);
                if (ka > kb) std::swap(ka, kb);
                ++uses[{ka, kb}];
                ends[{ka, kb}] = {p, q};
            }
        }
    }
    CHECK(downward == 0u);
    std::size_t open = 0;
    for (const auto& [edge, n] : uses) {
        const auto& [a, b] = ends[edge];
        const bool border = (std::abs(a.x - lo) < 0.5f && std::abs(b.x - lo) < 0.5f) || (std::abs(a.x - hi) < 0.5f && std::abs(b.x - hi) < 0.5f) ||
                            (std::abs(a.z - lo) < 0.5f && std::abs(b.z - lo) < 0.5f) || (std::abs(a.z - hi) < 0.5f && std::abs(b.z - hi) < 0.5f);
        if (!border && n != 2) ++open;
    }
    CHECK(open == 0u);
}

TEST_CASE("the river runs in its channel and the walls rise from it", "[chronos-canyon][adr1933]") {
    const auto w = scene::generateChronosCanyon(smallGen(4));
    REQUIRE(w != nullptr);
    const auto& g = w->gen;
    // every centreline point is under water; the rim beside it is far above
    std::size_t dry = 0;
    for (std::size_t i = 0; i < w->riverLine.size(); i += 10) {
        const glm::vec2 p = w->riverLine[i];
        if (std::abs(p.x) > 0.45f * w->extent || std::abs(p.y) > 0.45f * w->extent) continue;
        if (w->sample(p) >= g.waterLevel) ++dry;
    }
    CHECK(dry == 0u);
    const glm::vec2 mid = w->riverLine[w->riverLine.size() / 2];
    const glm::vec2 next = w->riverLine[w->riverLine.size() / 2 + 5];
    const glm::vec2 side = glm::normalize(glm::vec2(-(next - mid).y, (next - mid).x));
    CHECK(w->sample(mid + side * (g.rimHalfWidth * 1.4f)) > g.waterLevel + 0.6f * g.depth);
    CHECK(w->sample(mid - side * (g.rimHalfWidth * 1.4f)) > g.waterLevel + 0.6f * g.depth);
    // the water sheet lies at the water line
    REQUIRE_FALSE(w->mesh->riverPositions.empty());
    for (const glm::vec3& p : w->mesh->riverPositions) CHECK(p.y == g.waterLevel);
    // the spire stands above its surroundings
    const glm::vec2 at = g.spires.front().at;
    CHECK(w->sample(at) > w->sample(at + glm::vec2(40.0f, 0.0f)) + 25.0f);
}

TEST_CASE("the chronosCanyon block round-trips and refuses what it does not know", "[chronos-canyon][adr1933]") {
    scene::ChronosCanyonBlock b;
    b.enabled = true;
    b.gen = smallGen(2);
    b.water.reflection = 1.25f;
    const nlohmann::json j = scene::chronosCanyonToJson(b);
    const auto back = scene::chronosCanyonFromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->gen.hash() == b.gen.hash());
    CHECK(back->water.reflection == 1.25f);
    CHECK(scene::chronosCanyonToJson(*back) == j);
    nlohmann::json bad = j;
    bad["nonesuch"] = 1;
    CHECK_FALSE(scene::chronosCanyonFromJson(bad).has_value());
    bad = j;
    bad["river"] = nlohmann::json::array({{0, 0}});
    CHECK_FALSE(scene::chronosCanyonFromJson(bad).has_value());
    bad = j;
    bad["water"]["nonesuch"] = 1.0;
    CHECK_FALSE(scene::chronosCanyonFromJson(bad).has_value());
}
