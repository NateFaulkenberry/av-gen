// ADR-1930: the shared RTIN mesher, on its own (the canyon's use of it is pinned in test_attractor_world.cpp).

#include "scene/terrain_rtin.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>
#include <utility>

using namespace avgen;

namespace {

// Every interior edge shared by exactly two triangles: no T-junction, no crack.
std::size_t openInteriorEdges(const scene::rtin::Grid& grid, const std::vector<glm::ivec3>& tris) {
    std::map<std::pair<int, int>, int> uses;
    for (const glm::ivec3& t : tris) {
        const int v[3] = {t.x, t.y, t.z};
        for (int e = 0; e < 3; ++e) {
            int a = v[e], b = v[(e + 1) % 3];
            if (a > b) std::swap(a, b);
            ++uses[{a, b}];
        }
    }
    std::size_t open = 0;
    for (const auto& [edge, n] : uses) {
        const int ax = edge.first % grid.gn, ay = edge.first / grid.gn, bx = edge.second % grid.gn, by = edge.second / grid.gn;
        const bool border = (ax == 0 && bx == 0) || (ay == 0 && by == 0) || (ax == grid.size && bx == grid.size) ||
                            (ay == grid.size && by == grid.size);
        if (!border && n != 2) ++open;
    }
    return open;
}

} // namespace

TEST_CASE("the RTIN grid is the smallest power of two that meets the spacing", "[terrain-rtin][adr1930]") {
    const auto g = scene::rtin::gridFor(1000.0f, 10.0f);
    CHECK(g.size == 128); // 64 cells would be 15.6 m
    CHECK(g.gn == 129);
    CHECK(g.spacing == 1000.0f / 128.0f);
    CHECK(scene::rtin::gridFor(1.0e6f, 0.01f, 6).k == 6);
}

TEST_CASE("a plane needs two triangles; a bump refines around itself and stays watertight", "[terrain-rtin][adr1930]") {
    const auto grid = scene::rtin::gridFor(256.0f, 1.0f);
    std::vector<float> H(static_cast<std::size_t>(grid.gn) * grid.gn, 0.0f);
    const auto planeError = [&](int ax, int ay, int bx, int by, int mx, int my) {
        return std::abs(0.5f * (H[grid.index(ax, ay)] + H[grid.index(bx, by)]) - H[grid.index(mx, my)]) / 0.1f;
    };
    {
        const auto E = scene::rtin::propagateErrors(grid, planeError);
        CHECK(scene::rtin::extractTriangles(grid, E).size() == 2);
    }
    for (int y = 0; y < grid.gn; ++y) {
        for (int x = 0; x < grid.gn; ++x) {
            const float dx = static_cast<float>(x - 90), dy = static_cast<float>(y - 170);
            H[grid.index(x, y)] = 20.0f * std::exp(-(dx * dx + dy * dy) / 300.0f);
        }
    }
    const auto E = scene::rtin::propagateErrors(grid, planeError);
    const auto tris = scene::rtin::extractTriangles(grid, E);
    CHECK(tris.size() > 100);
    CHECK(tris.size() < static_cast<std::size_t>(2 * grid.size * grid.size));
    CHECK(openInteriorEdges(grid, tris) == 0);
    // deterministic, whatever the thread schedule
    CHECK(scene::rtin::extractTriangles(grid, scene::rtin::propagateErrors(grid, planeError)) == tris);
    // every triangle lands in exactly one chunk
    const auto chunks = scene::rtin::chunkTriangles(grid, tris, 8);
    std::size_t total = 0;
    for (const auto& c : chunks) total += c.size();
    CHECK(total == tris.size());
}

TEST_CASE("windUpward makes every triangle counter-clockwise from above", "[terrain-rtin][adr1930]") {
    const std::vector<glm::vec3> p{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
    std::vector<std::uint32_t> idx{0, 1, 2, 0, 2, 1};
    scene::rtin::windUpward(p, idx);
    for (std::size_t i = 0; i < idx.size(); i += 3) {
        CHECK(glm::cross(p[idx[i + 1]] - p[idx[i]], p[idx[i + 2]] - p[idx[i]]).y >= 0.0f);
    }
}
