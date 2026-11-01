#include "scene/chronos_canyon.hpp"

#include "scene/terrain_rtin.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace avgen::scene {

using json = nlohmann::json;
using rtin::parallelRows;

namespace {

// ---- noise: 2D gradient noise and its sums (scene-local, deterministic in the seed) --------------------------------

std::uint32_t hashU(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

glm::vec2 gradAt(std::int32_t x, std::int32_t y, std::uint32_t seed) {
    const std::uint32_t h = hashU(static_cast<std::uint32_t>(x) * 0x8da6b343U ^ static_cast<std::uint32_t>(y) * 0xd8163841U ^
                                  seed * 0xcb1ab31fU);
    const float a = static_cast<float>(h & 0xffffu) * (6.28318530718f / 65536.0f);
    return {std::cos(a), std::sin(a)};
}

// -1..1 (roughly)
float gnoise(glm::vec2 p, std::uint32_t seed) {
    const glm::vec2 i = glm::floor(p);
    const glm::vec2 f = p - i;
    const auto ix = static_cast<std::int32_t>(i.x), iy = static_cast<std::int32_t>(i.y);
    const glm::vec2 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    const float a = glm::dot(gradAt(ix, iy, seed), f);
    const float b = glm::dot(gradAt(ix + 1, iy, seed), f - glm::vec2(1, 0));
    const float c = glm::dot(gradAt(ix, iy + 1, seed), f - glm::vec2(0, 1));
    const float d = glm::dot(gradAt(ix + 1, iy + 1, seed), f - glm::vec2(1, 1));
    return 1.4f * glm::mix(glm::mix(a, b, u.x), glm::mix(c, d, u.x), u.y);
}

float fbm(glm::vec2 p, int octaves, std::uint32_t seed, float gain = 0.5f) {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * gnoise(p, seed + static_cast<std::uint32_t>(o) * 101u);
        norm += amp;
        amp *= gain;
        p = glm::vec2(p.x * 1.97f - p.y * 0.31f, p.x * 0.31f + p.y * 1.97f) + glm::vec2(17.3f, -9.1f);
    }
    return sum / norm;
}

// 0..1, sharp ridges (Musgrave's ridged multifractal, simplified)
float ridged(glm::vec2 p, int octaves, std::uint32_t seed) {
    float sum = 0.0f, amp = 0.5f, weight = 1.0f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        float n = 1.0f - std::abs(gnoise(p, seed + static_cast<std::uint32_t>(o) * 131u));
        n *= n * weight;
        weight = std::clamp(n * 2.0f, 0.0f, 1.0f);
        sum += n * amp;
        norm += amp;
        amp *= 0.5f;
        p = glm::vec2(p.x * 2.03f + p.y * 0.27f, -p.x * 0.27f + p.y * 2.03f) + glm::vec2(-5.7f, 11.9f);
    }
    return sum / norm;
}

float smoothstepf(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

glm::vec2 catmull(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

std::uint64_t mixHash(std::uint64_t h, std::uint64_t v) {
    return (h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2))) * 1099511628211ull;
}
std::uint64_t mixHashF(std::uint64_t h, float v) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return mixHash(h, bits);
}

// The river's centreline: Catmull-Rom through the control points (ends extended straight), resampled at `step`
// metres of arc.
std::vector<glm::vec2> riverCentreline(const std::vector<glm::vec2>& pts, float step) {
    std::vector<glm::vec2> dense;
    const std::size_t n = pts.size();
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const glm::vec2 p0 = i == 0 ? 2.0f * pts[0] - pts[1] : pts[i - 1];
        const glm::vec2 p3 = i + 2 >= n ? 2.0f * pts[n - 1] - pts[n - 2] : pts[i + 2];
        const int sub = 64;
        for (int k = 0; k < sub; ++k) dense.push_back(catmull(p0, pts[i], pts[i + 1], p3, static_cast<float>(k) / sub));
    }
    dense.push_back(pts.back());
    std::vector<glm::vec2> out{dense.front()};
    float carry = 0.0f;
    for (std::size_t i = 1; i < dense.size(); ++i) {
        glm::vec2 a = dense[i - 1];
        const glm::vec2 b = dense[i];
        float seg = glm::length(b - a);
        while (carry + seg >= step) {
            const float t = (step - carry) / seg;
            a = glm::mix(a, b, t);
            out.push_back(a);
            seg = glm::length(b - a);
            carry = 0.0f;
        }
        carry += seg;
    }
    if (glm::length(out.back() - dense.back()) > 0.25f * step) out.push_back(dense.back());
    return out;
}

// Nearest point on the centreline: distance and arc length. A uniform grid of segment buckets answers near queries
// exactly; beyond `kNear` a coarse field (bilinear) answers, which is smooth there and far cheaper.
struct RiverDistance {
    const std::vector<glm::vec2>& line;
    std::vector<float> arc;
    float cell = 64.0f;
    glm::vec2 lo{0.0f};
    int nx = 0, ny = 0;
    std::vector<std::vector<int>> buckets;
    static constexpr float kNear = 1200.0f;
    // the coarse field
    int D = 256;
    glm::vec2 corigin{0.0f};
    float cstep = 0.0f;
    std::vector<glm::vec2> coarse; // x distance, y arc

    RiverDistance(const std::vector<glm::vec2>& l, glm::vec2 origin, float extent) : line(l) {
        arc.resize(line.size(), 0.0f);
        for (std::size_t i = 1; i < line.size(); ++i) arc[i] = arc[i - 1] + glm::length(line[i] - line[i - 1]);
        lo = origin;
        nx = ny = static_cast<int>(std::ceil(extent / cell)) + 1;
        buckets.resize(static_cast<std::size_t>(nx) * ny);
        for (int s = 0; s + 1 < static_cast<int>(line.size()); ++s) {
            const glm::vec2 a = line[static_cast<std::size_t>(s)], b = line[static_cast<std::size_t>(s) + 1];
            const glm::ivec2 c0 = glm::clamp(glm::ivec2(glm::floor((glm::min(a, b) - lo) / cell)), glm::ivec2(0), glm::ivec2(nx - 1, ny - 1));
            const glm::ivec2 c1 = glm::clamp(glm::ivec2(glm::floor((glm::max(a, b) - lo) / cell)), glm::ivec2(0), glm::ivec2(nx - 1, ny - 1));
            for (int y = c0.y; y <= c1.y; ++y)
                for (int x = c0.x; x <= c1.x; ++x) buckets[static_cast<std::size_t>(y) * nx + x].push_back(s);
        }
        corigin = origin;
        cstep = extent / static_cast<float>(D - 1);
        coarse.resize(static_cast<std::size_t>(D) * D);
        parallelRows(D, [&](int y) {
            for (int x = 0; x < D; ++x) {
                const glm::vec2 p = corigin + glm::vec2(x, y) * cstep;
                glm::vec2 best(1e30f, 0.0f);
                for (std::size_t s = 0; s + 1 < line.size(); s += 4) {
                    const std::size_t e = std::min(s + 4, line.size() - 1);
                    const glm::vec2 r = segment(p, s, e);
                    if (r.x < best.x) best = r;
                }
                coarse[static_cast<std::size_t>(y) * D + x] = best;
            }
        });
    }
    [[nodiscard]] glm::vec2 segment(glm::vec2 p, std::size_t s, std::size_t e) const {
        const glm::vec2 a = line[s], b = line[e];
        const glm::vec2 ab = b - a;
        const float t = std::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
        return {glm::length(p - (a + ab * t)), arc[s] + t * (arc[e] - arc[s])};
    }
    [[nodiscard]] glm::vec2 coarseAt(glm::vec2 p) const {
        const glm::vec2 u = glm::clamp((p - corigin) / cstep, glm::vec2(0.0f), glm::vec2(static_cast<float>(D - 1) - 1e-3f));
        const glm::ivec2 i(glm::floor(u));
        const glm::vec2 f = u - glm::vec2(i);
        const auto at = [&](int x, int y) { return coarse[static_cast<std::size_t>(y) * D + x]; };
        const int x1 = std::min(i.x + 1, D - 1), y1 = std::min(i.y + 1, D - 1);
        return glm::mix(glm::mix(at(i.x, i.y), at(x1, i.y), f.x), glm::mix(at(i.x, y1), at(x1, y1), f.x), f.y);
    }
    // x distance, y arc length
    [[nodiscard]] glm::vec2 query(glm::vec2 p) const {
        const glm::vec2 c = coarseAt(p);
        if (c.x > kNear + 2.0f * cstep) return c;
        const glm::ivec2 home = glm::ivec2(glm::floor((p - lo) / cell));
        glm::vec2 best(1e30f, 0.0f);
        const int maxRing = static_cast<int>(std::ceil((kNear + 4.0f * cstep) / cell)) + 1;
        for (int ring = 0; ring <= maxRing; ++ring) {
            if (best.x < (static_cast<float>(ring) - 1.0f) * cell) break; // nothing in this ring can be nearer
            for (int y = home.y - ring; y <= home.y + ring; ++y) {
                for (int x = home.x - ring; x <= home.x + ring; ++x) {
                    if (std::max(std::abs(x - home.x), std::abs(y - home.y)) != ring) continue;
                    if (x < 0 || y < 0 || x >= nx || y >= ny) continue;
                    for (int s : buckets[static_cast<std::size_t>(y) * nx + x]) {
                        const glm::vec2 r = segment(p, static_cast<std::size_t>(s), static_cast<std::size_t>(s) + 1);
                        if (r.x < best.x) best = r;
                    }
                }
            }
        }
        return best.x < 1e29f ? best : c;
    }
};

// The baked field, sampled smoothly (a uniform cubic B-spline, C2: the mesh's surface is the field's smooth surface).
struct FieldSampler {
    const ChronosCanyonWorld& w;
    float texel;
    explicit FieldSampler(const ChronosCanyonWorld& world) : w(world), texel(world.extent / static_cast<float>(world.res)) {}
    static glm::vec4 bw(float f) {
        const float g = 1.0f - f;
        return glm::vec4(g * g * g, 3.0f * f * f * f - 6.0f * f * f + 4.0f, -3.0f * f * f * f + 3.0f * f * f + 3.0f * f + 1.0f,
                         f * f * f) / 6.0f;
    }
    // x height, y strata
    [[nodiscard]] glm::vec2 smooth(glm::vec2 xz) const {
        const glm::vec2 t = (xz - w.origin) / texel - 0.5f;
        const glm::vec2 fl = glm::floor(t);
        const glm::ivec2 i(fl);
        const glm::vec2 f = t - fl;
        const glm::vec4 wx = bw(f.x), wy = bw(f.y);
        glm::vec2 v(0.0f);
        const int n = w.res;
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                const int cx = std::clamp(i.x + x - 1, 0, n - 1), cy = std::clamp(i.y + y - 1, 0, n - 1);
                const std::size_t k = static_cast<std::size_t>(cy) * n + cx;
                v += glm::vec2(w.height[k], w.strata[k]) * (wx[x] * wy[y]);
            }
        }
        return v;
    }
};

// Baked ambient visibility from the height grid: an eight-direction horizon search out to 256 m (the method of
// ADR-1859, which replaced GTAO on RTIN walls; GTAO draws fins along long triangles).
float horizonVisibility(const std::vector<float>& H, int gn, int gx, int gy, float spacing, glm::vec3 n) {
    static constexpr int kDirs[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
    const float h0 = H[static_cast<std::size_t>(gy) * gn + gx];
    float occluded = 0.0f;
    for (const auto& d : kDirs) {
        const glm::vec2 dir = glm::normalize(glm::vec2(static_cast<float>(d[0]), static_cast<float>(d[1])));
        const float slope = -(n.x * dir.x + n.z * dir.y) / std::max(n.y, 1e-4f);
        const float sinT = slope / std::sqrt(1.0f + slope * slope);
        float sinH = -1.0f;
        for (int step = 1; step * spacing <= 256.0f; step *= 2) {
            const int x = gx + d[0] * step, y = gy + d[1] * step;
            if (x < 0 || y < 0 || x >= gn || y >= gn) break;
            const float run = spacing * static_cast<float>(step) * glm::length(glm::vec2(d[0], d[1]));
            const float rise = H[static_cast<std::size_t>(y) * gn + x] - h0 - 0.5f;
            sinH = std::max(sinH, rise / std::sqrt(run * run + rise * rise));
        }
        const float floorAngle = std::max(sinT, 0.0f);
        occluded += std::clamp((sinH - floorAngle) / std::max(1.0f - floorAngle, 1e-3f), 0.0f, 1.0f);
    }
    return 1.0f - occluded / 8.0f;
}

std::shared_ptr<const ChronosCanyonMesh> buildMesh(const ChronosCanyonWorld& w, const RiverDistance& river) {
    const auto t0 = std::chrono::steady_clock::now();
    auto out = std::make_shared<ChronosCanyonMesh>();
    const ChronosCanyonGen& g = w.gen;
    const FieldSampler sampler(w);
    const rtin::Grid grid = rtin::gridFor(w.extent, g.meshSpacing);
    const int gn = grid.gn;
    const float spacing = grid.spacing;
    out->sampleSpacing = spacing;
    std::vector<float> H(static_cast<std::size_t>(gn) * gn), T(H.size());
    parallelRows(gn, [&](int y) {
        for (int x = 0; x < gn; ++x) {
            const glm::vec2 v = sampler.smooth(w.origin + glm::vec2(x, y) * spacing);
            H[grid.index(x, y)] = v.x;
            T[grid.index(x, y)] = v.y;
        }
    });
    // the tolerance grows with the distance from the river, which the camera follows
    const auto tolAt = [&](int gx, int gy) {
        const float d = river.coarseAt(w.origin + glm::vec2(gx, gy) * spacing).x;
        return std::max(g.meshMinError, g.meshError * d);
    };
    const auto localError = [&](int ax, int ay, int bx, int by, int mx, int my) {
        const float tol = tolAt(mx, my);
        float e = std::abs(0.5f * (H[grid.index(ax, ay)] + H[grid.index(bx, by)]) - H[grid.index(mx, my)]) / tol;
        // the strata coordinate is interpolated linearly by the rasteriser, so the triangulation carries it too
        e = std::max(e, std::abs(0.5f * (T[grid.index(ax, ay)] + T[grid.index(bx, by)]) - T[grid.index(mx, my)]) /
                            (0.25f * tol / g.meshMinError));
        return e;
    };
    const std::vector<float> E = rtin::propagateErrors(grid, localError);
    const std::vector<glm::ivec3> tris = rtin::extractTriangles(grid, E);
    constexpr int C = 16;
    const std::vector<std::vector<int>> byChunk = rtin::chunkTriangles(grid, tris, C);
    out->chunks.resize(byChunk.size());
    parallelRows(C * C, [&](int c) {
        ChronosTerrainChunk& ch = out->chunks[static_cast<std::size_t>(c)];
        std::unordered_map<int, std::uint32_t> remap;
        remap.reserve(byChunk[static_cast<std::size_t>(c)].size());
        glm::vec3 lo(1e30f), hi(-1e30f);
        for (int t : byChunk[static_cast<std::size_t>(c)]) {
            const glm::ivec3 tr = tris[static_cast<std::size_t>(t)];
            for (int v : {tr.x, tr.y, tr.z}) {
                auto it = remap.find(v);
                if (it == remap.end()) {
                    const int gx = v % gn, gy = v / gn;
                    const glm::vec2 xz = w.origin + glm::vec2(gx, gy) * spacing;
                    const float h = H[static_cast<std::size_t>(v)];
                    // the normal from the continuous surface, so decimation never shows as facets
                    const float e = std::max(0.75f, 0.5f * spacing);
                    const float hx = sampler.smooth(xz + glm::vec2(e, 0)).x - sampler.smooth(xz - glm::vec2(e, 0)).x;
                    const float hz = sampler.smooth(xz + glm::vec2(0, e)).x - sampler.smooth(xz - glm::vec2(0, e)).x;
                    const glm::vec3 nrm = glm::normalize(glm::vec3(-hx / (2.0f * e), 1.0f, -hz / (2.0f * e)));
                    // the floor: flat, low ground near the river is sand (canyonRock's playa level)
                    const float flat = smoothstepf(0.88f, 0.97f, nrm.y);
                    const float low = 1.0f - smoothstepf(g.waterLevel + 2.0f, g.waterLevel + 9.0f, h);
                    const float level = std::round(flat * low * static_cast<float>(kChronosFloorLevels));
                    const float ao = horizonVisibility(H, gn, gx, gy, spacing, nrm);
                    const auto ni = static_cast<std::uint32_t>(ch.positions.size());
                    ch.positions.emplace_back(xz.x, h, xz.y);
                    ch.normals.push_back(nrm);
                    ch.uvs.emplace_back(T[static_cast<std::size_t>(v)], level + kChronosOcclusionScale * ao);
                    lo = glm::min(lo, ch.positions.back());
                    hi = glm::max(hi, ch.positions.back());
                    it = remap.emplace(v, ni).first;
                }
                ch.indices.push_back(it->second);
            }
        }
        ch.boundsMin = lo;
        ch.boundsMax = hi;
    });
    for (ChronosTerrainChunk& ch : out->chunks) {
        rtin::windUpward(ch.positions, ch.indices);
        out->vertices += ch.positions.size();
        out->triangles += ch.indices.size() / 3;
    }
    // the river: one flat sheet at the water line over every cell (4 x 4 grid cells) whose ground dips below it
    {
        const int step = 4;
        const float cs = spacing * static_cast<float>(step);
        for (int y = 0; y + step <= grid.size; y += step) {
            for (int x = 0; x + step <= grid.size; x += step) {
                float lowest = 1e9f;
                for (int yy = y; yy <= y + step; ++yy)
                    for (int xx = x; xx <= x + step; ++xx) lowest = std::min(lowest, H[grid.index(xx, yy)]);
                if (lowest >= g.waterLevel) continue;
                const glm::vec2 p = w.origin + glm::vec2(x, y) * spacing;
                const auto b = static_cast<std::uint32_t>(out->riverPositions.size());
                out->riverPositions.emplace_back(p.x, g.waterLevel, p.y);
                out->riverPositions.emplace_back(p.x + cs, g.waterLevel, p.y);
                out->riverPositions.emplace_back(p.x + cs, g.waterLevel, p.y + cs);
                out->riverPositions.emplace_back(p.x, g.waterLevel, p.y + cs);
                for (std::uint32_t q : {0u, 2u, 1u, 0u, 3u, 2u}) out->riverIndices.push_back(b + q);
            }
        }
    }
    out->buildSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return out;
}

std::mutex gCacheMutex;
std::shared_ptr<const ChronosCanyonWorld> gCache;
std::atomic<std::uint64_t> gGenerations{0};

} // namespace

Result<void> ChronosCanyonGen::validate() const {
    if (resolution < 256 || resolution > 4096) return fail("chronosCanyon: resolution must be 256..4096");
    if (!(extent >= 1000.0f && extent <= 32000.0f)) return fail("chronosCanyon: extent must be 1000..32000 m");
    if (river.size() < 2) return fail("chronosCanyon: 'river' needs at least two control points");
    if (!(floorHalfWidth > 1.0f) || !(rimHalfWidth > floorHalfWidth * 1.5f)) {
        return fail("chronosCanyon: rimHalfWidth must exceed 1.5 x floorHalfWidth (> 1 m)");
    }
    if (!(depth > 0.0f) || !(tierHeight > 1.0f) || !(band > 0.1f)) return fail("chronosCanyon: depth, tierHeight, band must be positive");
    if (!(riserShare > 0.0f && riserShare < 1.0f)) return fail("chronosCanyon: riserShare must be in (0, 1)");
    if (!(rangeEnd > rangeStart)) return fail("chronosCanyon: rangeEnd must exceed rangeStart");
    if (!(meshSpacing >= 0.5f && meshSpacing <= 32.0f)) return fail("chronosCanyon: meshSpacing must be 0.5..32 m");
    if (!(meshError > 0.0f) || !(meshMinError > 0.0f)) return fail("chronosCanyon: mesh errors must be positive");
    return {};
}

std::uint64_t ChronosCanyonGen::hash() const {
    std::uint64_t h = 1469598103934665603ull;
    h = mixHash(h, seed);
    h = mixHash(h, static_cast<std::uint64_t>(resolution));
    for (float v : {extent, floorHalfWidth, rimHalfWidth, depth, tierHeight, riserShare, terraces, widthVariation, alcoves,
                    gullies, plateauRelief, rangeHeight, rangeStart, rangeEnd, band, strataTilt, waterLevel, bedDepth,
                    meshSpacing, meshError, meshMinError}) {
        h = mixHashF(h, v);
    }
    for (const glm::vec2& p : river) h = mixHashF(mixHashF(h, p.x), p.y);
    for (const Spire& s : spires) h = mixHashF(mixHashF(mixHashF(mixHashF(h, s.at.x), s.at.y), s.height), s.radius);
    return h;
}

float ChronosCanyonWorld::sample(glm::vec2 xz) const {
    if (height.empty()) return 0.0f;
    const float texel = extent / static_cast<float>(res);
    const glm::vec2 u = glm::clamp((xz - origin) / texel - 0.5f, glm::vec2(0.0f), glm::vec2(static_cast<float>(res - 1) - 1e-3f));
    const glm::ivec2 i(glm::floor(u));
    const glm::vec2 f = u - glm::vec2(i);
    const auto at = [&](int x, int y) { return height[static_cast<std::size_t>(y) * res + x]; };
    const int x1 = std::min(i.x + 1, res - 1), y1 = std::min(i.y + 1, res - 1);
    return glm::mix(glm::mix(at(i.x, i.y), at(x1, i.y), f.x), glm::mix(at(i.x, y1), at(x1, y1), f.x), f.y);
}

std::shared_ptr<const ChronosCanyonWorld> generateChronosCanyon(const ChronosCanyonGen& gen) {
    if (!gen.validate()) return nullptr;
    const auto t0 = std::chrono::steady_clock::now();
    auto w = std::make_shared<ChronosCanyonWorld>();
    w->gen = gen;
    w->hash = gen.hash();
    w->res = gen.resolution;
    w->extent = gen.extent;
    w->origin = glm::vec2(-0.5f * gen.extent);
    w->riverLine = riverCentreline(gen.river, 4.0f);
    const RiverDistance river(w->riverLine, w->origin, gen.extent);
    w->riverLength = river.arc.back();
    const std::uint32_t seed = gen.seed * 7919u + 13u;
    const float strataAngle = static_cast<float>(hashU(seed) & 0xffffu) * (6.28318530718f / 65536.0f);
    const glm::vec2 dip(std::cos(strataAngle), std::sin(strataAngle));
    const int n = gen.resolution;
    const float texel = gen.extent / static_cast<float>(n);
    w->height.resize(static_cast<std::size_t>(n) * n);
    w->strata.resize(w->height.size());
    parallelRows(n, [&](int y) {
        for (int x = 0; x < n; ++x) {
            const glm::vec2 p = w->origin + (glm::vec2(x, y) + 0.5f) * texel;
            const glm::vec2 ds = river.query(p);
            const float s = ds.y;
            // the walls' bays and buttresses: the distance itself is pushed in and out -- broad bays, and a smaller
            // buttress rhythm along the wall (two octaves each: more and the bench edges scallop into pillows)
            const float d = std::max(ds.x + gen.alcoves * (120.0f * fbm(p / 900.0f, 2, seed + 1u) +
                                                           18.0f * fbm(p / 240.0f, 2, seed + 11u)),
                                     0.0f);
            // the canyon swells and pinches along the river
            const float swell = 1.0f + gen.widthVariation * 0.6f * fbm(glm::vec2(s / 1100.0f, 0.5f), 3, seed + 2u);
            const float rimW = gen.rimHalfWidth * swell;
            const float floorW = gen.floorHalfWidth * (1.0f + 0.5f * gen.widthVariation * gnoise(glm::vec2(s / 600.0f, 3.1f), seed + 3u));
            const float bankW = floorW * 0.55f;
            float h = 0.0f;
            if (d < floorW) {
                // the bed: a parabola from the bank (at the water line) down to `bedDepth` at the centre
                const float q = d / floorW;
                h = gen.waterLevel - gen.bedDepth * (1.0f - q * q) + 0.3f;
            } else if (d < floorW + bankW) {
                h = gen.waterLevel + 0.3f + 1.6f * smoothstepf(floorW, floorW + bankW, d) +
                    0.4f * gnoise(p / 23.0f, seed + 4u);
            } else {
                const float wall0 = floorW + bankW;
                const float t = std::clamp((d - wall0) / std::max(rimW - wall0, 1.0f), 0.0f, 1.0f);
                // the base profile: a sheer inner gorge (the first fifth of the wall climbs a third of the depth),
                // easing into the upper walls
                const float gorge = smoothstepf(0.0f, 0.2f, t);
                const float hb = gen.depth * (0.34f * gorge + 0.66f * (1.0f - (1.0f - t) * (1.0f - t)) * smoothstepf(0.05f, 1.0f, t));
                // uneven tiers: the profile is warped monotonically before it is quantised, so the steps come in
                // thick and thin formations rather than a regular staircase; the boundaries also wander along the wall
                const float warped = hb + 0.24f * gen.tierHeight * std::sin(hb / (0.61f * gen.tierHeight) + 1.3f) +
                                     0.1f * gen.tierHeight * std::sin(hb / (1.7f * gen.tierHeight) + 0.4f);
                const float x0 = (warped + 6.0f * fbm(p / 520.0f, 2, seed + 5u)) / gen.tierHeight;
                const float i0 = std::floor(x0);
                const float f0 = x0 - i0;
                const float stepped = gen.tierHeight * (i0 + smoothstepf(1.0f - gen.riserShare, 1.0f, f0));
                h = gen.waterLevel + 1.9f + glm::mix(hb, stepped, gen.terraces);
                // gullies down the walls: channels running across the river's direction
                const float wall = 4.0f * t * (1.0f - t);
                const float gully = std::pow(1.0f - std::abs(gnoise(glm::vec2(s / 65.0f, d / 260.0f), seed + 6u)), 7.0f);
                h -= gen.gullies * 26.0f * wall * gully;
                // the plateau above the rim and the ranges that close the horizon
                const float rim = smoothstepf(0.82f, 1.0f, t);
                h += rim * gen.plateauRelief * fbm(p / 1600.0f, 4, seed + 7u);
                h += gen.rangeHeight * smoothstepf(gen.rangeStart, gen.rangeEnd, d) * ridged(p / 2600.0f, 5, seed + 8u);
            }
            // spires: a steep capped column standing on the local ground
            for (const ChronosCanyonGen::Spire& sp : gen.spires) {
                const float r = glm::length(p - sp.at) * (1.0f + 0.18f * gnoise(p / 9.0f, seed + 9u));
                if (r < sp.radius) { // (radius: the skirt's; the column is 0.84 of it)
                    // sheer sides, a flat cap, and a skirt of talus at the foot
                    const float shape = 0.82f * (1.0f - smoothstepf(0.78f * sp.radius, 0.9f * sp.radius, r)) +
                                        0.18f * (1.0f - smoothstepf(0.5f * sp.radius, sp.radius, r));
                    h += sp.height * shape;
                }
            }
            const std::size_t k = static_cast<std::size_t>(y) * n + x;
            w->height[k] = h;
            w->strata[k] = (h + gen.strataTilt * glm::dot(p, dip) + 2.5f * fbm(p / 900.0f, 2, seed + 10u)) / gen.band;
        }
    });
    const auto [mn, mx] = std::minmax_element(w->height.begin(), w->height.end());
    w->minHeight = *mn;
    w->maxHeight = *mx;
    w->mesh = buildMesh(*w, river);
    w->generateSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    gGenerations.fetch_add(1);
    return w;
}

std::shared_ptr<const ChronosCanyonWorld> acquireChronosCanyon(const ChronosCanyonGen& gen) {
    const std::uint64_t want = gen.hash();
    {
        std::lock_guard<std::mutex> lock(gCacheMutex);
        if (gCache && gCache->hash == want) return gCache;
    }
    auto w = generateChronosCanyon(gen);
    std::lock_guard<std::mutex> lock(gCacheMutex);
    if (w) gCache = w;
    return w;
}

std::uint64_t chronosCanyonGenerations() noexcept { return gGenerations.load(); }

// ---- JSON ---------------------------------------------------------------------------------------------------------

namespace {

constexpr const char* kKeys[] = {"enabled", "seed", "resolution", "extent", "river", "floorHalfWidth", "rimHalfWidth",
                                 "depth", "tierHeight", "riserShare", "terraces", "widthVariation", "alcoves", "gullies",
                                 "plateauRelief", "rangeHeight", "rangeStart", "rangeEnd", "band", "strataTilt",
                                 "waterLevel", "bedDepth", "spires", "meshSpacing", "meshError", "meshMinError",
                                 "terrainProgram", "water"};

constexpr std::pair<const char*, float WaterSettings::*> kWaterFloats[] = {
    {"shallow", &WaterSettings::shallow},       {"roughness", &WaterSettings::roughness},
    {"clarity", &WaterSettings::clarity},       {"maxOpacity", &WaterSettings::maxOpacity},
    {"edgeFade", &WaterSettings::edgeFade},     {"fresnel", &WaterSettings::fresnel},
    {"reflection", &WaterSettings::reflection}, {"specular", &WaterSettings::specular},
    {"ripple", &WaterSettings::ripple},         {"rippleScale", &WaterSettings::rippleScale},
    {"rippleSpeed", &WaterSettings::rippleSpeed}, {"foam", &WaterSettings::foam},
    {"ssr", &WaterSettings::ssr},               {"ssrDistance", &WaterSettings::ssrDistance},
    {"ssrSteps", &WaterSettings::ssrSteps},     {"ssrGain", &WaterSettings::ssrGain},
    {"skyMirror", &WaterSettings::skyMirror},   {"swell", &WaterSettings::swell},
};

glm::vec2 vec2Of(const json& v, const char* what) {
    if (!v.is_array() || v.size() != 2) throw std::runtime_error(std::string(what) + " must be [x, z]");
    return {v[0].get<float>(), v[1].get<float>()};
}

} // namespace

Result<ChronosCanyonBlock> chronosCanyonFromJson(const json& j) try {
    if (!j.is_object()) return fail("'chronosCanyon' must be an object");
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (std::find_if(std::begin(kKeys), std::end(kKeys), [&](const char* k) { return it.key() == k; }) == std::end(kKeys)) {
            return fail("chronosCanyon: unknown key '{}'", it.key());
        }
    }
    ChronosCanyonBlock b;
    b.enabled = j.value("enabled", true);
    ChronosCanyonGen& g = b.gen;
    g.seed = j.value("seed", g.seed);
    g.resolution = j.value("resolution", g.resolution);
    g.extent = j.value("extent", g.extent);
    if (j.contains("river")) {
        const json& r = j.at("river");
        if (!r.is_array()) return fail("chronosCanyon: 'river' must be an array of [x, z]");
        for (const json& p : r) g.river.push_back(vec2Of(p, "chronosCanyon: a river point"));
    }
    g.floorHalfWidth = j.value("floorHalfWidth", g.floorHalfWidth);
    g.rimHalfWidth = j.value("rimHalfWidth", g.rimHalfWidth);
    g.depth = j.value("depth", g.depth);
    g.tierHeight = j.value("tierHeight", g.tierHeight);
    g.riserShare = j.value("riserShare", g.riserShare);
    g.terraces = j.value("terraces", g.terraces);
    g.widthVariation = j.value("widthVariation", g.widthVariation);
    g.alcoves = j.value("alcoves", g.alcoves);
    g.gullies = j.value("gullies", g.gullies);
    g.plateauRelief = j.value("plateauRelief", g.plateauRelief);
    g.rangeHeight = j.value("rangeHeight", g.rangeHeight);
    g.rangeStart = j.value("rangeStart", g.rangeStart);
    g.rangeEnd = j.value("rangeEnd", g.rangeEnd);
    g.band = j.value("band", g.band);
    g.strataTilt = j.value("strataTilt", g.strataTilt);
    g.waterLevel = j.value("waterLevel", g.waterLevel);
    g.bedDepth = j.value("bedDepth", g.bedDepth);
    if (j.contains("spires")) {
        const json& sj = j.at("spires");
        if (!sj.is_array()) return fail("chronosCanyon: 'spires' must be an array");
        for (const json& s : sj) {
            if (!s.is_object()) return fail("chronosCanyon: a spire must be an object {at, height, radius}");
            for (auto it = s.begin(); it != s.end(); ++it) {
                if (it.key() != "at" && it.key() != "height" && it.key() != "radius") {
                    return fail("chronosCanyon: a spire has no key '{}'", it.key());
                }
            }
            ChronosCanyonGen::Spire sp;
            sp.at = vec2Of(s.at("at"), "chronosCanyon: spire.at");
            sp.height = s.value("height", sp.height);
            sp.radius = s.value("radius", sp.radius);
            g.spires.push_back(sp);
        }
    }
    g.meshSpacing = j.value("meshSpacing", g.meshSpacing);
    g.meshError = j.value("meshError", g.meshError);
    g.meshMinError = j.value("meshMinError", g.meshMinError);
    if (auto ok = g.validate(); !ok) return std::unexpected(ok.error());
    b.terrainProgram = j.value("terrainProgram", b.terrainProgram);
    if (j.contains("water")) {
        const json& wj = j.at("water");
        if (!wj.is_object()) return fail("chronosCanyon: 'water' must be an object");
        WaterSettings& ws = b.water;
        for (auto it = wj.begin(); it != wj.end(); ++it) {
            const std::string& key = it.key();
            if (key == "shallowColor" || key == "deepColor" || key == "reflectionTint") {
                const json& v = *it;
                if (!v.is_array() || v.size() != 3) return fail("chronosCanyon: water '{}' must be [r, g, b]", key);
                const glm::vec3 c(v[0].get<float>(), v[1].get<float>(), v[2].get<float>());
                (key == "shallowColor" ? ws.shallowColor : key == "deepColor" ? ws.deepColor : ws.reflectionTint) = c;
                continue;
            }
            bool known = false;
            for (const auto& [name, member] : kWaterFloats) {
                if (key == name) {
                    known = true;
                    if (!it->is_number()) return fail("chronosCanyon: water '{}' must be a number", key);
                    ws.*member = it->get<float>();
                }
            }
            if (!known) return fail("chronosCanyon: unknown water key '{}'", key);
        }
    }
    return b;
} catch (const std::exception& e) {
    return fail("chronosCanyon: {}", e.what());
}

json chronosCanyonToJson(const ChronosCanyonBlock& b) {
    const ChronosCanyonGen& g = b.gen;
    json j;
    j["enabled"] = b.enabled;
    j["seed"] = g.seed;
    j["resolution"] = g.resolution;
    j["extent"] = g.extent;
    json river = json::array();
    for (const glm::vec2& p : g.river) river.push_back({p.x, p.y});
    j["river"] = std::move(river);
    j["floorHalfWidth"] = g.floorHalfWidth;
    j["rimHalfWidth"] = g.rimHalfWidth;
    j["depth"] = g.depth;
    j["tierHeight"] = g.tierHeight;
    j["riserShare"] = g.riserShare;
    j["terraces"] = g.terraces;
    j["widthVariation"] = g.widthVariation;
    j["alcoves"] = g.alcoves;
    j["gullies"] = g.gullies;
    j["plateauRelief"] = g.plateauRelief;
    j["rangeHeight"] = g.rangeHeight;
    j["rangeStart"] = g.rangeStart;
    j["rangeEnd"] = g.rangeEnd;
    j["band"] = g.band;
    j["strataTilt"] = g.strataTilt;
    j["waterLevel"] = g.waterLevel;
    j["bedDepth"] = g.bedDepth;
    json spires = json::array();
    for (const auto& s : g.spires) spires.push_back({{"at", {s.at.x, s.at.y}}, {"height", s.height}, {"radius", s.radius}});
    j["spires"] = std::move(spires);
    j["meshSpacing"] = g.meshSpacing;
    j["meshError"] = g.meshError;
    j["meshMinError"] = g.meshMinError;
    j["terrainProgram"] = b.terrainProgram;
    json w;
    const WaterSettings& ws = b.water;
    w["shallowColor"] = {ws.shallowColor.x, ws.shallowColor.y, ws.shallowColor.z};
    w["deepColor"] = {ws.deepColor.x, ws.deepColor.y, ws.deepColor.z};
    w["reflectionTint"] = {ws.reflectionTint.x, ws.reflectionTint.y, ws.reflectionTint.z};
    for (const auto& [name, member] : kWaterFloats) w[name] = ws.*member;
    j["water"] = std::move(w);
    return j;
}

} // namespace avgen::scene
