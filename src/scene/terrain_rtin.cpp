#include "scene/terrain_rtin.hpp"

#include <cstdlib>
#include <utility>

namespace avgen::scene::rtin {

Grid gridFor(float extent, float maxSpacing, int maxK) {
    Grid g;
    int k = 1;
    while (static_cast<float>(1 << k) * maxSpacing < extent && k < maxK) ++k;
    g.k = k;
    g.size = 1 << k;
    g.gn = g.size + 1;
    g.spacing = extent / static_cast<float>(g.size);
    return g;
}

std::vector<glm::ivec3> extractTriangles(const Grid& grid, const std::vector<float>& E) {
    std::vector<glm::ivec3> tris;
    const auto emit = [&](auto&& self, int ax, int ay, int bx, int by, int cx, int cy) -> void {
        const int mx = (ax + bx) / 2, my = (ay + by) / 2;
        if (std::abs(ax - cx) + std::abs(ay - cy) > 1 && E[grid.index(mx, my)] > 1.0f) {
            self(self, cx, cy, ax, ay, mx, my);
            self(self, bx, by, cx, cy, mx, my);
        } else {
            tris.emplace_back(static_cast<int>(grid.index(ax, ay)), static_cast<int>(grid.index(bx, by)),
                              static_cast<int>(grid.index(cx, cy)));
        }
    };
    const int size = grid.size;
    emit(emit, 0, 0, size, size, size, 0);
    emit(emit, size, size, 0, 0, 0, size);
    return tris;
}

std::vector<std::vector<int>> chunkTriangles(const Grid& grid, const std::vector<glm::ivec3>& tris, int C) {
    const int gn = grid.gn;
    std::vector<std::vector<int>> byChunk(static_cast<std::size_t>(C) * C);
    for (int t = 0; t < static_cast<int>(tris.size()); ++t) {
        const glm::ivec3 tr = tris[static_cast<std::size_t>(t)];
        const int cx = ((tr.x % gn) + (tr.y % gn) + (tr.z % gn)) / 3, cy = ((tr.x / gn) + (tr.y / gn) + (tr.z / gn)) / 3;
        const int ix = std::min(C - 1, cx * C / gn), iy = std::min(C - 1, cy * C / gn);
        byChunk[static_cast<std::size_t>(iy) * C + ix].push_back(t);
    }
    return byChunk;
}

void windUpward(const std::vector<glm::vec3>& positions, std::vector<std::uint32_t>& indices) {
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        const glm::vec3 a = positions[indices[i]], b = positions[indices[i + 1]], c = positions[indices[i + 2]];
        if (glm::cross(b - a, c - a).y < 0.0f) std::swap(indices[i + 1], indices[i + 2]);
    }
}

} // namespace avgen::scene::rtin
