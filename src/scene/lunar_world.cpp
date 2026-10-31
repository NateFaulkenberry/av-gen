#include "scene/lunar_world.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>

namespace avgen::scene {

namespace {

using json = nlohmann::json;
constexpr float kMoonRadius = 1737.4e3f;
constexpr int kChunksPerSide = 16;

std::uint32_t hashU(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

float hash01(std::int32_t x, std::int32_t y, std::uint32_t seed) {
    return static_cast<float>(hashU(static_cast<std::uint32_t>(x) * 73856093U ^ static_cast<std::uint32_t>(y) * 19349663U ^
                                    hashU(seed)) &
                              0xFFFFFF) /
           16777215.0f;
}

float valueNoise(float u, float v, std::uint32_t seed) {
    const float fu = std::floor(u), fv = std::floor(v);
    const auto iu = static_cast<std::int32_t>(fu), iv = static_cast<std::int32_t>(fv);
    float a = u - fu, b = v - fv;
    a = a * a * (3.0f - 2.0f * a);
    b = b * b * (3.0f - 2.0f * b);
    const float h00 = hash01(iu, iv, seed), h10 = hash01(iu + 1, iv, seed), h01 = hash01(iu, iv + 1, seed),
                h11 = hash01(iu + 1, iv + 1, seed);
    return 2.0f * (h00 * (1 - a) * (1 - b) + h10 * a * (1 - b) + h01 * (1 - a) * b + h11 * a * b) - 1.0f;
}

float fbm(float x, float z, int octaves, float base, float amp, float gain, std::uint32_t seed) {
    float h = 0.0f, f = 1.0f / base, a = amp;
    for (int o = 0; o < octaves; ++o) {
        const float th = static_cast<float>(o) * 1.7f + static_cast<float>(seed % 97);
        const float c = std::cos(th), s = std::sin(th);
        h += a * valueNoise((c * x - s * z) * f + 13.1f * o, (s * x + c * z) * f + 7.7f * o, seed * 31u + o);
        f *= 2.03f;
        a *= gain;
    }
    return h;
}

float smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// A crater's profile at r = distance / radius (LROC NAC DTM of Linne: depth 0.21 D, rim 0.037 D, bowl exponent 1.9,
// ejecta falling as r^-4.5), with age: an old crater is shallower, its rim lower and softer, its bowl rounder.
float craterProfile(float r, float diameter, float fresh) {
    const float depth = 0.21f * diameter * (0.35f + 0.65f * fresh);
    const float rim = 0.037f * diameter * (0.25f + 0.75f * fresh);
    const float bowl = 1.0f + 0.9f * fresh; // 1.9 fresh .. 1.0 old (a parabola)
    const float inside = rim - (depth + rim) * std::pow(std::max(1.0f - r * r, 0.0f), bowl);
    const float outside = rim * std::pow(std::max(r, 1.0f), -(3.0f + 1.5f * fresh));
    const float w = 0.06f + 0.25f * (1.0f - fresh);
    const float t = smooth(1.0f - w, 1.0f + w, r);
    return inside * (1.0f - t) + outside * t;
}

struct Field {
    int n = 0;
    float extent = 0.0f, spacing = 0.0f;
    std::vector<float> h, albedo, ejecta;
    [[nodiscard]] float x(int i) const { return -extent * 0.5f + static_cast<float>(i) * spacing; }
};

void applyCraters(Field& f, const std::vector<LunarCrater>& craters, std::uint32_t seed) {
    for (const LunarCrater& c : craters) {
        const float R = c.diameter * 0.5f;
        if (c.diameter < 2.5f * f.spacing) continue;
        const float reach = 3.0f * R;
        const int i0 = std::max(0, static_cast<int>((c.x - reach + f.extent * 0.5f) / f.spacing));
        const int i1 = std::min(f.n - 1, static_cast<int>((c.x + reach + f.extent * 0.5f) / f.spacing) + 1);
        const int j0 = std::max(0, static_cast<int>((c.z - reach + f.extent * 0.5f) / f.spacing));
        const int j1 = std::min(f.n - 1, static_cast<int>((c.z + reach + f.extent * 0.5f) / f.spacing) + 1);
        if (i0 > i1 || j0 > j1) continue;
        // the local mean the bowl replaces
        double sum = 0.0;
        int cnt = 0;
        for (int j = j0; j <= j1; j += std::max(1, (j1 - j0) / 8)) {
            for (int i = i0; i <= i1; i += std::max(1, (i1 - i0) / 8)) {
                sum += f.h[static_cast<std::size_t>(j) * f.n + i];
                ++cnt;
            }
        }
        const float mean = cnt > 0 ? static_cast<float>(sum / cnt) : 0.0f;
        const std::uint32_t cs = hashU(static_cast<std::uint32_t>(c.x * 13.0f) ^ static_cast<std::uint32_t>(c.z * 7.0f) ^ seed);
        for (int j = j0; j <= j1; ++j) {
            for (int i = i0; i <= i1; ++i) {
                const float dx = f.x(i) - c.x, dz = f.x(j) - c.z;
                const float ang = std::atan2(dz, dx);
                const float rr0 = std::sqrt(dx * dx + dz * dz) / R;
                // the rim wanders, the bowl does not
                const float wob = 1.0f + std::clamp(rr0 - 0.6f, 0.0f, 1.0f) *
                                             (0.025f * std::sin(2.0f * ang + c.x) + 0.018f * std::sin(5.0f * ang + c.z) +
                                              0.03f * valueNoise(std::cos(ang) * 3.0f + c.x, std::sin(ang) * 3.0f + c.z, cs));
                const float r = rr0 / wob;
                if (r > 3.0f) continue;
                const float fade = std::clamp((3.0f - r) / 0.6f, 0.0f, 1.0f);
                const float kin = std::sqrt(std::clamp(1.15f - r, 0.0f, 1.0f));
                float& h = f.h[static_cast<std::size_t>(j) * f.n + i];
                h = h * (1.0f - 0.8f * kin) + mean * 0.8f * kin + craterProfile(r, c.diameter, c.fresh) * fade;
                if (c.fresh > 0.55f) {
                    const float ray = 0.5f + 0.5f * std::sin(ang * static_cast<float>(9 + static_cast<int>(c.diameter) % 7) + c.diameter);
                    float halo = c.fresh * std::pow(std::max(r, 0.05f), -2.2f) * (0.6f + 0.6f * ray * ray * ray);
                    if (r < 0.85f) halo = c.fresh * 0.9f;
                    const std::size_t k = static_cast<std::size_t>(j) * f.n + i;
                    f.albedo[k] += 0.07f * std::min(halo, 1.2f) * fade;
                    f.ejecta[k] = std::max(f.ejecta[k], std::clamp(c.fresh * (r < 1.0f ? 0.6f : std::pow(r, -2.0f)), 0.0f, 1.0f));
                }
            }
        }
    }
}

std::vector<LunarCrater> population(const LunarGen& g, float area, float dmin, float dmax, std::mt19937& rng) {
    const auto count = static_cast<std::size_t>(g.craters * area / 1e6f * std::pow(dmin / 100.0f, -2.0f));
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    const float half = std::sqrt(area) * 0.5f;
    std::vector<LunarCrater> out(count);
    for (LunarCrater& c : out) {
        const float u = u01(rng);
        c.diameter = 1.0f / std::sqrt(1.0f / (dmin * dmin) - u * (1.0f / (dmin * dmin) - 1.0f / (dmax * dmax)));
        c.x = (2.0f * u01(rng) - 1.0f) * half;
        c.z = (2.0f * u01(rng) - 1.0f) * half;
        c.fresh = std::pow(u01(rng), 2.5f); // most are old
    }
    std::sort(out.begin(), out.end(), [](const LunarCrater& a, const LunarCrater& b) { return a.fresh < b.fresh; });
    return out;
}

Field buildField(const LunarGen& g, float extent, float spacing, const std::vector<LunarCrater>& craters) {
    Field f;
    f.extent = extent;
    f.spacing = spacing;
    f.n = static_cast<int>(std::lround(extent / spacing)) + 1;
    const std::size_t N = static_cast<std::size_t>(f.n) * f.n;
    f.h.resize(N);
    f.albedo.resize(N);
    f.ejecta.assign(N, 0.0f);
    for (int j = 0; j < f.n; ++j) {
        for (int i = 0; i < f.n; ++i) {
            const float x = f.x(i), z = f.x(j);
            const std::size_t k = static_cast<std::size_t>(j) * f.n + i;
            f.h[k] = fbm(x, z, 7, 5000.0f, g.relief, 0.5f, g.seed + 1) + fbm(x, z, 5, 600.0f, g.relief * 0.1f, 0.55f, g.seed + 2);
            f.albedo[k] = 0.115f + 0.02f * fbm(x, z, 4, 3000.0f, 1.0f, 0.5f, g.seed + 3) +
                          0.01f * fbm(x, z, 3, 200.0f, 1.0f, 0.6f, g.seed + 4);
        }
    }
    applyCraters(f, craters, g.seed);
    for (int j = 0; j < f.n; ++j) {
        for (int i = 0; i < f.n; ++i) {
            const float x = f.x(i), z = f.x(j);
            float& h = f.h[static_cast<std::size_t>(j) * f.n + i];
            h += fbm(x, z, 3, 40.0f, 0.6f, 0.5f, g.seed + 5);
            h -= (x * x + z * z) / (2.0f * kMoonRadius);
        }
    }
    return f;
}

void appendGrid(LunarChunk& ch, const Field& f, int i0, int i1, int j0, int j1, float yOffset, float rockFlag) {
    const auto base = static_cast<std::uint32_t>(ch.positions.size());
    const int w = i1 - i0 + 1;
    for (int j = j0; j <= j1; ++j) {
        for (int i = i0; i <= i1; ++i) {
            const auto at = [&](int a, int b) {
                a = std::clamp(a, 0, f.n - 1);
                b = std::clamp(b, 0, f.n - 1);
                return f.h[static_cast<std::size_t>(b) * f.n + a];
            };
            const std::size_t k = static_cast<std::size_t>(j) * f.n + i;
            ch.positions.emplace_back(f.x(i), f.h[k] + yOffset, f.x(j));
            const float gx = (at(i + 1, j) - at(i - 1, j)) / (2.0f * f.spacing);
            const float gz = (at(i, j + 1) - at(i, j - 1)) / (2.0f * f.spacing);
            ch.normals.push_back(glm::normalize(glm::vec3(-gx, 1.0f, -gz)));
            ch.uvs.emplace_back(std::clamp(f.albedo[k], 0.02f, 0.5f), rockFlag);
        }
    }
    for (int j = 0; j < j1 - j0; ++j) {
        for (int i = 0; i < i1 - i0; ++i) {
            const std::uint32_t a = base + static_cast<std::uint32_t>(j * w + i), b = a + 1,
                                c = a + static_cast<std::uint32_t>(w), d = c + 1;
            // counter-clockwise seen from above (+y): x right, z toward the viewer
            ch.indices.insert(ch.indices.end(), {a, c, b, b, c, d});
        }
    }
}

// A boulder: an icosphere (level 1), displaced by hashed noise, squashed, turned, set into the ground.
void appendRock(LunarChunk& ch, glm::vec3 centre, float radius, std::uint32_t seed, float albedo) {
    static const std::vector<glm::vec3> kVerts = [] {
        const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
        std::vector<glm::vec3> v = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                                    {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
        for (auto& p : v) p = glm::normalize(p);
        return v;
    }();
    static const std::vector<std::array<std::uint32_t, 3>> kFaces = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    // subdivide once
    std::vector<glm::vec3> v = kVerts;
    std::vector<std::array<std::uint32_t, 3>> faces;
    const auto mid = [&](std::uint32_t a, std::uint32_t b) {
        v.push_back(glm::normalize(v[a] + v[b]));
        return static_cast<std::uint32_t>(v.size() - 1);
    };
    for (const auto& f : kFaces) {
        const std::uint32_t ab = mid(f[0], f[1]), bc = mid(f[1], f[2]), ca = mid(f[2], f[0]);
        faces.push_back({f[0], ab, ca});
        faces.push_back({f[1], bc, ab});
        faces.push_back({f[2], ca, bc});
        faces.push_back({ab, bc, ca});
    }
    const float squash = 0.55f + 0.35f * hash01(static_cast<std::int32_t>(seed), 1, 91);
    const float yaw = 6.2831853f * hash01(static_cast<std::int32_t>(seed), 2, 92);
    const float c = std::cos(yaw), s = std::sin(yaw);
    const float sx = 0.8f + 0.5f * hash01(static_cast<std::int32_t>(seed), 3, 93);
    // flat-shaded (each face its own vertices): rock is faceted, and the facets catch the hard sun
    for (const auto& f : faces) {
        std::array<glm::vec3, 3> p{};
        for (int k = 0; k < 3; ++k) {
            glm::vec3 q = v[f[static_cast<std::size_t>(k)]];
            // the same direction must displace the same way in every face that shares it
            const float d = 0.75f + 0.5f * valueNoise(q.x * 2.3f + q.z * 1.7f + static_cast<float>(seed % 1000),
                                                       q.y * 2.9f - q.x * 0.7f, seed);
            q *= d;
            q = glm::vec3(q.x * sx, q.y * squash, q.z);
            q = glm::vec3(c * q.x - s * q.z, q.y, s * q.x + c * q.z);
            p[static_cast<std::size_t>(k)] = centre + q * radius;
        }
        const glm::vec3 n = glm::normalize(glm::cross(p[1] - p[0], p[2] - p[0]) + glm::vec3(0.0f, 1e-9f, 0.0f));
        const auto base = static_cast<std::uint32_t>(ch.positions.size());
        for (int k = 0; k < 3; ++k) {
            ch.positions.push_back(p[static_cast<std::size_t>(k)]);
            ch.normals.push_back(n);
            ch.uvs.emplace_back(albedo, 1.0f);
        }
        ch.indices.insert(ch.indices.end(), {base, base + 1, base + 2});
    }
}

} // namespace

Result<void> LunarGen::validate() const {
    if (!(extent >= 200.0f && extent <= 20000.0f) || !(spacing >= 0.5f && spacing <= 50.0f) ||
        extent / spacing > 4096.0f) {
        return fail("lunarWorld: extent in [200, 20000] m, spacing in [0.5, 50] m, at most 4096 cells a side");
    }
    if (!(skirtExtent >= extent && skirtExtent <= 200000.0f) || !(skirtSpacing >= spacing && skirtExtent / skirtSpacing <= 4096.0f)) {
        return fail("lunarWorld: skirtExtent at least extent (at most 200 km), skirtSpacing at least spacing");
    }
    if (!(craters >= 0.0f && craters <= 100.0f) || !(rocks >= 0.0f && rocks <= 0.1f) || !(relief >= 0.0f && relief <= 1000.0f)) {
        return fail("lunarWorld: craters in [0, 100], rocks in [0, 0.1], relief in [0, 1000]");
    }
    for (const LunarCrater& c : heroes) {
        if (!(c.diameter > 0.0f) || !(c.fresh >= 0.0f && c.fresh <= 1.0f)) return fail("lunarWorld: a hero needs a diameter and fresh in [0, 1]");
    }
    return {};
}

std::uint64_t LunarGen::hash() const {
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&](float v) {
        std::uint32_t b = 0;
        std::memcpy(&b, &v, 4);
        h = (h ^ b) * 1099511628211ull;
    };
    h = (h ^ seed) * 1099511628211ull;
    for (float v : {extent, spacing, skirtExtent, skirtSpacing, craters, rocks, relief}) mix(v);
    for (const LunarCrater& c : heroes) {
        for (float v : {c.x, c.z, c.diameter, c.fresh}) mix(v);
    }
    return h;
}

float LunarWorld::sample(float x, float z) const {
    if (n < 2 || height.empty()) return 0.0f;
    const float sp = gen.spacing, half = gen.extent * 0.5f;
    const float fx = std::clamp((x + half) / sp, 0.0f, static_cast<float>(n - 1) - 1e-3f);
    const float fz = std::clamp((z + half) / sp, 0.0f, static_cast<float>(n - 1) - 1e-3f);
    const int i = static_cast<int>(fx), j = static_cast<int>(fz);
    const float a = fx - static_cast<float>(i), b = fz - static_cast<float>(j);
    const auto at = [&](int u, int v) { return height[static_cast<std::size_t>(v) * n + u]; };
    return (at(i, j) * (1 - a) + at(i + 1, j) * a) * (1 - b) + (at(i, j + 1) * (1 - a) + at(i + 1, j + 1) * a) * b;
}

std::shared_ptr<const LunarWorld> generateLunarWorld(const LunarGen& gen) {
    const auto t0 = std::chrono::steady_clock::now();
    auto w = std::make_shared<LunarWorld>();
    w->gen = gen;
    w->hash = gen.hash();
    std::mt19937 rng(gen.seed);
    // the fine tile: the small population over a margin around it, the heroes first so the small overprint them
    const float area = (gen.extent * 1.2f) * (gen.extent * 1.2f);
    std::vector<LunarCrater> craters = gen.heroes;
    const auto small = population(gen, area, 2.5f * gen.spacing, 400.0f, rng);
    craters.insert(craters.end(), small.begin(), small.end());
    Field fine = buildField(gen, gen.extent, gen.spacing, craters);
    // the skirt: the big craters only, and many more out to the horizon, sunk under the tile
    std::vector<LunarCrater> big;
    for (const LunarCrater& c : craters) {
        if (c.diameter > 3.0f * gen.skirtSpacing) big.push_back(c);
    }
    {
        std::uniform_real_distribution<float> u01(0.0f, 1.0f);
        const float half = gen.skirtExtent * 0.5f;
        for (int k = 0; k < 260; ++k) {
            LunarCrater c;
            c.x = (2.0f * u01(rng) - 1.0f) * half;
            c.z = (2.0f * u01(rng) - 1.0f) * half;
            c.diameter = 60.0f + 1400.0f * std::pow(u01(rng), 3.0f);
            c.fresh = u01(rng) * u01(rng);
            if (std::max(std::abs(c.x), std::abs(c.z)) > gen.extent * 0.5f) big.push_back(c);
        }
    }
    Field skirt = buildField(gen, gen.skirtExtent, gen.skirtSpacing, big);
    for (int j = 0; j < skirt.n; ++j) {
        for (int i = 0; i < skirt.n; ++i) {
            const float edge = std::max(std::abs(skirt.x(i)), std::abs(skirt.x(j))) - (gen.extent * 0.5f - 100.0f);
            const float inner = std::clamp(edge / 60.0f, 0.0f, 1.0f);
            skirt.h[static_cast<std::size_t>(j) * skirt.n + i] -= 0.6f + 400.0f * (1.0f - inner);
        }
    }
    w->n = fine.n;
    w->height = fine.h;
    // chunks: the fine tile in 16 x 16, the skirt in 8 x 8
    const auto chunked = [&](const Field& f, int per) {
        const int cells = f.n - 1;
        for (int cj = 0; cj < per; ++cj) {
            for (int ci = 0; ci < per; ++ci) {
                LunarChunk ch;
                appendGrid(ch, f, ci * cells / per, (ci + 1) * cells / per, cj * cells / per, (cj + 1) * cells / per, 0.0f, 0.0f);
                w->triangles += ch.indices.size() / 3;
                w->chunks.push_back(std::move(ch));
            }
        }
    };
    chunked(fine, kChunksPerSide);
    chunked(skirt, 8);
    // boulders, per fine chunk
    if (gen.rocks > 0.0f) {
        std::vector<LunarChunk> rocks(kChunksPerSide * kChunksPerSide);
        std::uniform_real_distribution<float> u01(0.0f, 1.0f);
        const auto candidates = static_cast<std::size_t>(gen.rocks * gen.extent * gen.extent);
        const float half = gen.extent * 0.5f - 4.0f;
        for (std::size_t k = 0; k < candidates; ++k) {
            const float x = (2.0f * u01(rng) - 1.0f) * half, z = (2.0f * u01(rng) - 1.0f) * half;
            const float rmax = u01(rng);
            const float rad = std::min(0.32f * std::pow(std::max(u01(rng), 1e-4f), -1.0f / 1.6f), 4.5f);
            const int i = std::clamp(static_cast<int>((x + gen.extent * 0.5f) / gen.spacing), 0, fine.n - 1);
            const int j = std::clamp(static_cast<int>((z + gen.extent * 0.5f) / gen.spacing), 0, fine.n - 1);
            const float ej = fine.ejecta[static_cast<std::size_t>(j) * fine.n + i];
            if (rmax > 0.12f + 0.88f * ej) continue; // boulders gather on fresh rims and ejecta
            const float ground = w->sample(x, z);
            const int ci = std::clamp(static_cast<int>((x + gen.extent * 0.5f) / gen.extent * kChunksPerSide), 0, kChunksPerSide - 1);
            const int cj = std::clamp(static_cast<int>((z + gen.extent * 0.5f) / gen.extent * kChunksPerSide), 0, kChunksPerSide - 1);
            appendRock(rocks[static_cast<std::size_t>(cj * kChunksPerSide + ci)], glm::vec3(x, ground - 0.3f * rad, z), rad,
                       static_cast<std::uint32_t>(k) ^ gen.seed, 0.13f + 0.06f * u01(rng));
            ++w->rockCount;
        }
        for (LunarChunk& ch : rocks) {
            if (ch.indices.empty()) continue;
            w->triangles += ch.indices.size() / 3;
            w->chunks.push_back(std::move(ch));
        }
    }
    w->generateSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return w;
}

Result<LunarWorldBlock> lunarWorldFromJson(const json& j) try {
    if (!j.is_object()) return fail("'lunarWorld' must be an object");
    static const std::array<const char*, 11> kKeys = {"enabled", "seed", "extent", "spacing", "skirtExtent", "skirtSpacing",
                                                       "craters", "rocks", "relief", "heroes", "program"};
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (std::find_if(kKeys.begin(), kKeys.end(), [&](const char* k) { return it.key() == k; }) == kKeys.end()) {
            return fail("lunarWorld: unknown key '{}'", it.key());
        }
    }
    LunarWorldBlock b;
    b.enabled = j.value("enabled", true);
    LunarGen& g = b.gen;
    g.seed = j.value("seed", g.seed);
    g.extent = j.value("extent", g.extent);
    g.spacing = j.value("spacing", g.spacing);
    g.skirtExtent = j.value("skirtExtent", g.skirtExtent);
    g.skirtSpacing = j.value("skirtSpacing", g.skirtSpacing);
    g.craters = j.value("craters", g.craters);
    g.rocks = j.value("rocks", g.rocks);
    g.relief = j.value("relief", g.relief);
    b.program = j.value("program", b.program);
    if (j.contains("heroes")) {
        for (const json& h : j.at("heroes")) {
            LunarCrater c;
            c.x = h.value("x", 0.0f);
            c.z = h.value("z", 0.0f);
            c.diameter = h.value("diameter", 100.0f);
            c.fresh = h.value("fresh", 0.9f);
            g.heroes.push_back(c);
        }
    }
    if (auto ok = g.validate(); !ok) return std::unexpected(ok.error());
    return b;
} catch (const json::exception& e) {
    return fail("lunarWorld: {}", e.what());
}

json lunarWorldToJson(const LunarWorldBlock& b) {
    const LunarGen& g = b.gen;
    json heroes = json::array();
    for (const LunarCrater& c : g.heroes) heroes.push_back({{"x", c.x}, {"z", c.z}, {"diameter", c.diameter}, {"fresh", c.fresh}});
    return json{{"enabled", b.enabled}, {"seed", g.seed},       {"extent", g.extent},   {"spacing", g.spacing},
                {"skirtExtent", g.skirtExtent}, {"skirtSpacing", g.skirtSpacing}, {"craters", g.craters},
                {"rocks", g.rocks},     {"relief", g.relief},   {"heroes", heroes},     {"program", b.program}};
}

} // namespace avgen::scene
