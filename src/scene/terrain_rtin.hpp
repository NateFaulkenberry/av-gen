#pragma once

// ADR-1930: the error-bounded right-triangulated irregular network (RTIN) that ADR-1857 / ADR-1867 built for the
// Strange Attractor's canyon, lifted out of `attractor_world.cpp` so another height-field scene triangulates with the
// same crack-free core instead of a second copy.
//
// Evans, Kirkpatrick & Townsend 2001, in the "Martini" formulation: the (2^k + 1)^2 grid's triangle bintree is refined
// wherever a hypotenuse's midpoint error exceeds 1. The caller supplies the error *normalised by its own tolerance*
// (so the tolerance may vary in space), and may add any attribute the rasteriser interpolates linearly. Errors are
// propagated finest level first (ADR-1867): every triangle of a level before any of the level above, the shared
// midpoints written by an atomic max. That ordering is what makes the result watertight; a depth-first pass reads a
// child's midpoint before the neighbour across its hypotenuse has written it and leaves T-junctions.
//
// What stays with the caller: sampling the surface, the tolerance field, vertex attributes, chunk contents. What is
// here: the grid, the propagation, the extraction, the chunk partition and the winding.

#include <glm/glm.hpp>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <thread>
#include <vector>

namespace avgen::scene::rtin {

// Runs `fn(row)` for every row on up to 16 cores. Each row must write only its own outputs, so the result does not
// depend on the thread count.
template <class Fn>
void parallelRows(int rows, Fn&& fn) {
    const unsigned n = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    pool.reserve(n);
    for (unsigned t = 0; t < n; ++t) {
        pool.emplace_back([&, t] {
            for (int r = static_cast<int>(t); r < rows; r += static_cast<int>(n)) {
                fn(r);
            }
        });
    }
    for (auto& th : pool) {
        th.join();
    }
}

// The sampling grid: 2^k + 1 samples a side over `extent`, the smallest k (up to `maxK`) whose cell is no larger than
// `maxSpacing`.
struct Grid {
    int k = 1;
    int size = 2;     // 2^k cells a side
    int gn = 3;       // size + 1 samples a side
    float spacing = 0.0f;
    [[nodiscard]] std::size_t index(int x, int y) const { return static_cast<std::size_t>(y) * gn + x; }
};
[[nodiscard]] Grid gridFor(float extent, float maxSpacing, int maxK = 14);

// The propagated, normalised error at every grid vertex (a hypotenuse midpoint). `localError(ax, ay, bx, by, mx, my)`
// returns the error of the triangle with hypotenuse a-b and midpoint m, divided by the tolerance there; it is called
// once per triangle of the bintree, concurrently, so it must be thread-safe and pure.
template <class LocalError>
[[nodiscard]] std::vector<float> propagateErrors(const Grid& grid, LocalError&& localError) {
    const int size = grid.size;
    std::vector<float> E(static_cast<std::size_t>(grid.gn) * grid.gn, 0.0f);
    const auto atomicMax = [&](std::size_t i, float v) {
        std::atomic_ref<std::uint32_t> slot(*reinterpret_cast<std::uint32_t*>(&E[i]));
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(v);
        std::uint32_t cur = slot.load(std::memory_order_relaxed);
        while (bits > cur && !slot.compare_exchange_weak(cur, bits, std::memory_order_relaxed)) {
        }
    };
    // levels: the two roots are heap ids 2 and 3 (level 1); a level-L triangle's ids are [2^L, 2^(L+1)); the leaves
    // (legs one cell long) are level 2k
    const int levels = 2 * grid.k;
    for (int level = levels; level >= 1; --level) {
        const std::uint64_t first = 1ull << level, count = 1ull << level;
        const bool parent = level < levels;
        const int workers = 64;
        parallelRows(workers, [&](int w) {
            const std::uint64_t lo = first + count * static_cast<std::uint64_t>(w) / workers;
            const std::uint64_t hi = first + count * static_cast<std::uint64_t>(w + 1) / workers;
            for (std::uint64_t id0 = lo; id0 < hi; ++id0) {
                // decode the heap id into the triangle's corners (Martini): the root by the bit below the leading one,
                // then one bit per level down
                int ax, ay, bx, by, cx, cy;
                const int depth = level; // bits after the leading 1
                if ((id0 >> (depth - 1)) & 1u) { // root 3: a (size, size), b (0, 0), c (0, size)
                    ax = size; ay = size; bx = 0; by = 0; cx = 0; cy = size;
                } else {                          // root 2: a (0, 0), b (size, size), c (size, 0)
                    ax = 0; ay = 0; bx = size; by = size; cx = size; cy = 0;
                }
                for (int bit = depth - 2; bit >= 0; --bit) {
                    const int mx = (ax + bx) / 2, my = (ay + by) / 2;
                    if (((id0 >> bit) & 1u) == 0u) { // first child: (c, a, m)
                        bx = ax; by = ay; ax = cx; ay = cy;
                    } else {                          // second child: (b, c, m)
                        ax = bx; ay = by; bx = cx; by = cy;
                    }
                    cx = mx; cy = my;
                }
                const int mx = (ax + bx) / 2, my = (ay + by) / 2;
                float e = localError(ax, ay, bx, by, mx, my);
                if (parent) {
                    e = std::max({e, E[grid.index((ax + cx) / 2, (ay + cy) / 2)],
                                  E[grid.index((bx + cx) / 2, (by + cy) / 2)]});
                }
                atomicMax(grid.index(mx, my), e);
            }
        });
    }
    return E;
}

// The triangles whose error does not exceed 1, as grid-vertex indices, in the bintree's order.
[[nodiscard]] std::vector<glm::ivec3> extractTriangles(const Grid& grid, const std::vector<float>& E);

// The triangles of each of `C` x `C` square chunks over the grid, a triangle by its centroid.
[[nodiscard]] std::vector<std::vector<int>> chunkTriangles(const Grid& grid, const std::vector<glm::ivec3>& tris,
                                                           int C);

// Makes every triangle of an index list counter-clockwise seen from above (+y).
void windUpward(const std::vector<glm::vec3>& positions, std::vector<std::uint32_t>& indices);

} // namespace avgen::scene::rtin
