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
constexpr int kChunksPerSide = 16;

std::uint32_t hashU(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// A crater position as a hash term. Converting a float that no unsigned int can hold is undefined,
// and every crater west or south of the origin gave one: `static_cast<std::uint32_t>(-392.7f)`.
// Apple silicon's conversion instruction (fcvtzu) clamps, so negatives and NaN read 0 and anything
// past the top reads the top. That clamp is what every Moon and Mars frame was made and approved
// with, so it is written out here rather than replaced with a wrap that would re-seed the rim of
// three craters in four. It also makes an x86 build, whose conversion wraps, draw the same craters.
std::uint32_t craterHashTerm(float v) {
    if (!(v > 0.0f)) {
        return 0u;
    }
    if (v >= 4294967296.0f) {
        return 0xFFFFFFFFu;
    }
    return static_cast<std::uint32_t>(v);
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

// ADR-1912: inside `clear` metres of a hero's centre, each texel becomes the mean of its ring round that centre, blended
// back to the ground over the outer 30%: whatever overprinted the floor is gone, and the bowl's own shape stays.
void clearHeroFloor(Field& f, const LunarCrater& c) {
    const float dr = f.spacing;
    const int bins = static_cast<int>(c.clear / dr) + 2;
    std::vector<double> sum(static_cast<std::size_t>(bins), 0.0);
    std::vector<int> count(static_cast<std::size_t>(bins), 0);
    const int i0 = std::max(0, static_cast<int>((c.x - c.clear + f.extent * 0.5f) / f.spacing));
    const int i1 = std::min(f.n - 1, static_cast<int>((c.x + c.clear + f.extent * 0.5f) / f.spacing) + 1);
    const int j0 = std::max(0, static_cast<int>((c.z - c.clear + f.extent * 0.5f) / f.spacing));
    const int j1 = std::min(f.n - 1, static_cast<int>((c.z + c.clear + f.extent * 0.5f) / f.spacing) + 1);
    for (int pass = 0; pass < 2; ++pass) {
        for (int j = j0; j <= j1; ++j) {
            for (int i = i0; i <= i1; ++i) {
                const float r = std::hypot(f.x(i) - c.x, f.x(j) - c.z);
                if (r > c.clear) continue;
                const int b = static_cast<int>(r / dr);
                float& h = f.h[static_cast<std::size_t>(j) * f.n + i];
                if (pass == 0) {
                    sum[static_cast<std::size_t>(b)] += h;
                    ++count[static_cast<std::size_t>(b)];
                } else {
                    const float mean = static_cast<float>(sum[static_cast<std::size_t>(b)] / std::max(count[static_cast<std::size_t>(b)], 1));
                    const float t = std::clamp((c.clear - r) / (0.3f * c.clear), 0.0f, 1.0f);
                    h += (mean - h) * t * t * (3.0f - 2.0f * t);
                }
            }
        }
    }
}

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
        const std::uint32_t cs = hashU(craterHashTerm(c.x * 13.0f) ^ craterHashTerm(c.z * 7.0f) ^ seed);
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
                    // rays: narrow bright streaks at irregular angles, broad ones and fine ones, fading outward
                    const float a1 = 0.5f + 0.5f * valueNoise(std::cos(ang) * 4.0f + c.x * 0.01f, std::sin(ang) * 4.0f + c.z * 0.01f, cs + 7u);
                    const float a2 = 0.5f + 0.5f * valueNoise(std::cos(ang) * 13.0f + r * 0.6f, std::sin(ang) * 13.0f - r * 0.4f, cs + 9u);
                    const float ray = std::pow(a1, 3.0f) * 0.6f + std::pow(a2, 4.0f) * 0.8f;
                    float halo = c.fresh * std::pow(std::max(r, 0.05f), -1.8f) * (0.35f + 1.3f * ray);
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

// ADR-1896: the mesas and the yardangs at (x, z), metres to add to the ground; dusts the albedo in the troughs and at
// the mesas' feet (the material program reads it as the regolith's brightness)
float marsRelief(const LunarGen& g, float x, float z, float& albedo) {
    float add = 0.0f;
    float onMesa = 0.0f; // how much of this point belongs to a mesa (its plateau, wall or apron)
    for (const LunarMesa& m : g.mesas) {
        const float a = glm::radians(m.angle);
        const float dx = x - m.x, dz = z - m.z;
        const float u = (dx * std::cos(a) + dz * std::sin(a)) / m.rx, v = (-dx * std::sin(a) + dz * std::cos(a)) / m.rz;
        if (std::abs(u) > 2.0f || std::abs(v) > 2.0f) continue;
        // a rounded-rectangle outline (p = 3), eroded by noise on the distance at two unrelated scales (M3: one
        // scale cut the walls into evenly spaced gullies)
        float d = std::pow(std::pow(std::abs(u), 3.0f) + std::pow(std::abs(v), 3.0f), 1.0f / 3.0f);
        d += 0.08f * fbm(x, z, 4, 173.0f, 1.0f, 0.55f, g.seed + 31) + 0.035f * fbm(x, z, 3, 47.0f, 1.0f, 0.6f, g.seed + 32);
        // the wall: a steep fall from the plateau, cut into terraces of uneven height; the talus apron below it
        const float t = 1.0f - smooth(0.98f, 1.14f, d);
        const float levels = static_cast<float>(std::max(m.terraces, 1));
        const float tl = t * levels + 0.35f * fbm(x, z, 2, 260.0f, 1.0f, 0.5f, g.seed + 34);
        const float terr = std::clamp((std::floor(tl) + smooth(0.55f, 1.0f, tl - std::floor(tl))) / levels, 0.0f, 1.0f) * (t > 0.0f ? 1.0f : 0.0f);
        const float cap = 1.0f - 0.04f * (fbm(x, z, 3, 90.0f, 1.0f, 0.5f, g.seed + 33) + 1.0f) * 0.5f;
        const float apron = 0.22f * (1.0f - smooth(1.08f, 1.55f, d));
        add += m.height * std::max(terr * cap, apron * apron * 4.5f);
        onMesa = std::max(onMesa, 1.0f - smooth(1.0f, 1.45f, d));
        albedo += 0.015f * apron; // sand piled at the foot
    }
    if (g.yardangs > 0.0f) {
        const float a = glm::radians(g.wind);
        // a slow warp so the ridges wander instead of running ruler-straight (M3)
        const float w = 60.0f * fbm(x, z, 3, 700.0f, 1.0f, 0.5f, g.seed + 23);
        const float u = x * std::cos(a) + z * std::sin(a), v = -x * std::sin(a) + z * std::cos(a) + w;
        // long along the wind, narrow across: ridges where a stretched noise crosses its middle
        const float n = valueNoise(u / 520.0f, v / 26.0f, g.seed + 21) * 0.55f + valueNoise(u / 210.0f, v / 11.0f, g.seed + 22) * 0.3f +
                        valueNoise(u / 95.0f, v / 6.3f, g.seed + 24) * 0.15f;
        const float ridge = 1.0f - std::abs(2.0f * n - 1.0f);
        add += g.yardangs * (ridge * ridge - 0.35f) * (1.0f - onMesa); // M3: not across the mesas' walls
        albedo += 0.02f * (0.5f - ridge) * (1.0f - onMesa);           // dust settles in the troughs
    }
    return add;
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
            h += marsRelief(g, x, z, f.albedo[static_cast<std::size_t>(j) * f.n + i]); // ADR-1896 (0 on the Moon)
            h += fbm(x, z, 3, 40.0f, 0.6f, 0.5f, g.seed + 5);
            for (const LunarPad& p : g.pads) { // ADR-1896: a footing, before the curvature (a level on the sphere)
                const float d = std::hypot(x - p.x, z - p.z);
                if (d < p.radius) h += (p.level - h) * (1.0f - smooth(0.8f * p.radius, p.radius, d));
            }
            h -= (x * x + z * z) / (2.0f * g.curvature);
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

float valueNoise3(glm::vec3 p, std::uint32_t seed) {
    const glm::vec3 f0 = glm::floor(p);
    const glm::ivec3 i0(f0);
    glm::vec3 t = p - f0;
    t = t * t * (3.0f - 2.0f * t);
    const auto h = [&](int x, int y, int z) {
        return static_cast<float>(hashU(static_cast<std::uint32_t>(x) * 73856093U ^ static_cast<std::uint32_t>(y) * 19349663U ^
                                        static_cast<std::uint32_t>(z) * 83492791U ^ hashU(seed)) &
                                  0xFFFFFF) /
               16777215.0f;
    };
    const float x00 = glm::mix(h(i0.x, i0.y, i0.z), h(i0.x + 1, i0.y, i0.z), t.x);
    const float x10 = glm::mix(h(i0.x, i0.y + 1, i0.z), h(i0.x + 1, i0.y + 1, i0.z), t.x);
    const float x01 = glm::mix(h(i0.x, i0.y, i0.z + 1), h(i0.x + 1, i0.y, i0.z + 1), t.x);
    const float x11 = glm::mix(h(i0.x, i0.y + 1, i0.z + 1), h(i0.x + 1, i0.y + 1, i0.z + 1), t.x);
    return 2.0f * glm::mix(glm::mix(x00, x10, t.y), glm::mix(x01, x11, t.y), t.z) - 1.0f;
}

// A unit icosphere at `level` subdivisions, shared vertices (smooth), built once per level.
struct IcoMesh {
    std::vector<glm::vec3> v;
    std::vector<std::array<std::uint32_t, 3>> f;
};
const IcoMesh& icosphere(int level) {
    static const std::array<IcoMesh, 4> kMeshes = [] {
        std::array<IcoMesh, 4> out{};
        const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
        IcoMesh m;
        m.v = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
               {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
        for (auto& p : m.v) p = glm::normalize(p);
        m.f = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
               {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
        out[0] = m;
        for (int l = 1; l < 4; ++l) {
            IcoMesh n;
            n.v = out[static_cast<std::size_t>(l - 1)].v;
            std::vector<std::pair<std::uint64_t, std::uint32_t>> cache;
            const auto mid = [&](std::uint32_t a, std::uint32_t b) {
                const std::uint64_t key = (static_cast<std::uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
                for (const auto& [k, idx] : cache) {
                    if (k == key) return idx;
                }
                n.v.push_back(glm::normalize(n.v[a] + n.v[b]));
                const auto idx = static_cast<std::uint32_t>(n.v.size() - 1);
                cache.emplace_back(key, idx);
                return idx;
            };
            for (const auto& f : out[static_cast<std::size_t>(l - 1)].f) {
                const std::uint32_t ab = mid(f[0], f[1]), bc = mid(f[1], f[2]), ca = mid(f[2], f[0]);
                n.f.push_back({f[0], ab, ca});
                n.f.push_back({f[1], bc, ab});
                n.f.push_back({f[2], ca, bc});
                n.f.push_back({ab, bc, ca});
            }
            out[static_cast<std::size_t>(l)] = std::move(n);
        }
        return out;
    }();
    return kMeshes[static_cast<std::size_t>(std::clamp(level, 0, 3))];
}

// A boulder (Moon polish): a subdivided icosphere, rounded and irregular -- a few broad lobes, a flattened face or two
// where it split, then finer lumps -- squashed, turned, tilted and set well into the ground (lunar boulders sit in
// their own regolith). Smooth normals: weathered rock is rounded, not faceted. Finer meshes for the bigger rocks.
void appendRock(LunarChunk& ch, glm::vec3 centre, float radius, std::uint32_t seed, float albedo) {
    const int level = radius < 0.5f ? 1 : (radius < 2.2f ? 2 : 3);
    const IcoMesh& ico = icosphere(level);
    const auto hs = [&](int k) { return hash01(static_cast<std::int32_t>(seed), k, 90 + k); };
    const float squash = 0.5f + 0.35f * hs(1);
    const float yaw = 6.2831853f * hs(2);
    const float sx = 0.8f + 0.45f * hs(3);
    const float tiltX = (hs(4) - 0.5f) * 0.5f, tiltZ = (hs(5) - 0.5f) * 0.5f;
    // one or two split faces: planes that cut the lumpy sphere flat
    const glm::vec3 cut1 = glm::normalize(glm::vec3(hs(6) - 0.5f, (hs(7) - 0.3f), hs(8) - 0.5f) + glm::vec3(1e-4f));
    const glm::vec3 cut2 = glm::normalize(glm::vec3(hs(9) - 0.5f, (hs(10) - 0.5f), hs(11) - 0.5f) + glm::vec3(1e-4f));
    const float cutAt1 = 0.55f + 0.3f * hs(12), cutAt2 = hs(13) < 0.5f ? 0.65f + 0.25f * hs(14) : 9.0f;
    const auto so = static_cast<float>(seed % 977u);
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const auto base = static_cast<std::uint32_t>(ch.positions.size());
    std::vector<glm::vec3> p(ico.v.size());
    for (std::size_t i = 0; i < ico.v.size(); ++i) {
        const glm::vec3 d = ico.v[i];
        float r = 1.0f + 0.28f * valueNoise3(d * 1.3f + glm::vec3(so), seed) + 0.12f * valueNoise3(d * 3.1f + glm::vec3(so * 0.7f), seed + 1u) +
                  0.05f * valueNoise3(d * 7.3f + glm::vec3(so * 0.3f), seed + 2u);
        // a split face: points past the plane are pulled back onto it (softly)
        for (const auto& [n, at] : {std::pair{cut1, cutAt1}, std::pair{cut2, cutAt2}}) {
            const float along = glm::dot(d * r, n);
            if (along > at) r -= (along - at) * 0.85f / std::max(glm::dot(d, n), 0.2f);
        }
        glm::vec3 q = d * std::max(r, 0.35f);
        q = glm::vec3(q.x * sx, q.y * squash, q.z);
        q = glm::vec3(q.x, q.y * std::cos(tiltX) - q.z * std::sin(tiltX), q.y * std::sin(tiltX) + q.z * std::cos(tiltX));
        q = glm::vec3(q.x * std::cos(tiltZ) - q.y * std::sin(tiltZ), q.x * std::sin(tiltZ) + q.y * std::cos(tiltZ), q.z);
        q = glm::vec3(cy * q.x - sy * q.z, q.y, sy * q.x + cy * q.z);
        p[i] = centre + q * radius;
    }
    std::vector<glm::vec3> n(p.size(), glm::vec3(0.0f));
    for (const auto& f : ico.f) {
        const glm::vec3 fn = glm::cross(p[f[1]] - p[f[0]], p[f[2]] - p[f[0]]); // area-weighted
        n[f[0]] += fn;
        n[f[1]] += fn;
        n[f[2]] += fn;
    }
    for (std::size_t i = 0; i < p.size(); ++i) {
        ch.positions.push_back(p[i]);
        ch.normals.push_back(glm::normalize(n[i] + glm::vec3(0.0f, 1e-9f, 0.0f)));
        ch.uvs.emplace_back(albedo, 1.0f);
    }
    for (const auto& f : ico.f) ch.indices.insert(ch.indices.end(), {base + f[0], base + f[1], base + f[2]});
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
    if (!(curvature >= 1000.0f && curvature <= 1.0e7f)) return fail("lunarWorld: curvature in [1 km, 10000 km]");
    if (!(craters >= 0.0f && craters <= 100.0f) || !(rocks >= 0.0f && rocks <= 0.1f) || !(relief >= 0.0f && relief <= 1000.0f)) {
        return fail("lunarWorld: craters in [0, 100], rocks in [0, 0.1], relief in [0, 1000]");
    }
    for (const LunarCrater& c : heroes) {
        if (!(c.diameter > 0.0f) || !(c.fresh >= 0.0f && c.fresh <= 1.0f)) return fail("lunarWorld: a hero needs a diameter and fresh in [0, 1]");
    }
    for (const LunarMesa& m : mesas) {
        if (!(m.rx > 0.0f && m.rz > 0.0f && m.height >= 0.0f && m.height <= 2000.0f) || m.terraces < 1 || m.terraces > 12) {
            return fail("lunarWorld: a mesa needs rx, rz > 0, height in [0, 2000] m and 1-12 terraces");
        }
    }
    if (!(yardangs >= 0.0f && yardangs <= 100.0f)) return fail("lunarWorld: yardangs in [0, 100] m");
    for (const LunarPad& p : pads) {
        if (!(p.radius > 0.0f)) return fail("lunarWorld: a pad needs a radius");
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
    if (curvature != 1737.4e3f) mix(curvature);
    for (const LunarCrater& c : heroes) {
        for (float v : {c.x, c.z, c.diameter, c.fresh}) mix(v);
        if (c.clear != 0.0f) mix(c.clear); // ADR-1912: absent, the hash is the Moon's as before
    }
    // ADR-1896: absent, the hash is the Moon's as before
    for (const LunarMesa& m : mesas) {
        for (float v : {m.x, m.z, m.rx, m.rz, m.angle, m.height, static_cast<float>(m.terraces)}) mix(v);
    }
    if (yardangs != 0.0f) {
        mix(yardangs);
        mix(wind);
    }
    for (const LunarPad& p : pads) {
        for (float v : {p.x, p.z, p.radius, p.level}) mix(v);
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
    for (const LunarCrater& c : gen.heroes) {
        if (c.clear > 0.0f) clearHeroFloor(fine, c); // ADR-1912
    }
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
            // cumulative N(>r) ~ r^-1.6 (lunar boulder counts): many small, few large
            const float rad = std::min(0.22f * std::pow(std::max(u01(rng), 1e-4f), -1.0f / 1.6f), 5.0f);
            const int i = std::clamp(static_cast<int>((x + gen.extent * 0.5f) / gen.spacing), 0, fine.n - 1);
            const int j = std::clamp(static_cast<int>((z + gen.extent * 0.5f) / gen.spacing), 0, fine.n - 1);
            const float ej = fine.ejecta[static_cast<std::size_t>(j) * fine.n + i];
            if (rmax > 0.12f + 0.88f * ej) continue; // boulders gather on fresh rims and ejecta
            const float ground = w->sample(x, z);
            const int ci = std::clamp(static_cast<int>((x + gen.extent * 0.5f) / gen.extent * kChunksPerSide), 0, kChunksPerSide - 1);
            const int cj = std::clamp(static_cast<int>((z + gen.extent * 0.5f) / gen.extent * kChunksPerSide), 0, kChunksPerSide - 1);
            // partly buried (a third to a half of the height), the regolith's own grey: rock and soil share a surface here
            const float bury = 0.15f + 0.2f * u01(rng);
            const float alb = fine.albedo[static_cast<std::size_t>(j) * fine.n + i] * (0.95f + 0.15f * u01(rng));
            appendRock(rocks[static_cast<std::size_t>(cj * kChunksPerSide + ci)], glm::vec3(x, ground - bury * rad, z), rad,
                       static_cast<std::uint32_t>(k) ^ gen.seed, alb);
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
    static const std::array<const char*, 16> kKeys = {"enabled", "seed", "extent", "spacing", "skirtExtent", "skirtSpacing",
                                                       "craters", "rocks", "relief", "heroes", "program", "curvature",
                                                       "mesas", "yardangs", "wind", "pads"};
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
    g.curvature = j.value("curvature", g.curvature);
    b.program = j.value("program", b.program);
    if (j.contains("heroes")) {
        for (const json& h : j.at("heroes")) {
            LunarCrater c;
            c.x = h.value("x", 0.0f);
            c.z = h.value("z", 0.0f);
            c.diameter = h.value("diameter", 100.0f);
            c.fresh = h.value("fresh", 0.9f);
            c.clear = h.value("clear", 0.0f); // ADR-1912
            g.heroes.push_back(c);
        }
    }
    if (j.contains("mesas")) { // ADR-1896
        for (const json& mj : j.at("mesas")) {
            LunarMesa m;
            m.x = mj.value("x", 0.0f);
            m.z = mj.value("z", 0.0f);
            m.rx = mj.value("rx", m.rx);
            m.rz = mj.value("rz", m.rz);
            m.angle = mj.value("angle", 0.0f);
            m.height = mj.value("height", m.height);
            m.terraces = mj.value("terraces", m.terraces);
            g.mesas.push_back(m);
        }
    }
    if (j.contains("pads")) {
        for (const json& pj : j.at("pads")) {
            g.pads.push_back(LunarPad{pj.value("x", 0.0f), pj.value("z", 0.0f), pj.value("radius", 200.0f), pj.value("level", 0.0f)});
        }
    }
    g.yardangs = j.value("yardangs", g.yardangs);
    g.wind = j.value("wind", g.wind);
    if (auto ok = g.validate(); !ok) return std::unexpected(ok.error());
    return b;
} catch (const json::exception& e) {
    return fail("lunarWorld: {}", e.what());
}

json lunarWorldToJson(const LunarWorldBlock& b) {
    const LunarGen& g = b.gen;
    json heroes = json::array();
    for (const LunarCrater& c : g.heroes) {
        json h = {{"x", c.x}, {"z", c.z}, {"diameter", c.diameter}, {"fresh", c.fresh}};
        if (c.clear != 0.0f) h["clear"] = c.clear; // ADR-1912
        heroes.push_back(std::move(h));
    }
    json out{{"enabled", b.enabled}, {"seed", g.seed},       {"extent", g.extent},   {"spacing", g.spacing},
                {"skirtExtent", g.skirtExtent}, {"skirtSpacing", g.skirtSpacing}, {"craters", g.craters},
                {"rocks", g.rocks},     {"relief", g.relief},   {"heroes", heroes},     {"program", b.program},
                {"curvature", g.curvature}};
    if (!g.mesas.empty()) {
        json mesas = json::array();
        for (const LunarMesa& m : g.mesas) {
            mesas.push_back({{"x", m.x}, {"z", m.z}, {"rx", m.rx}, {"rz", m.rz}, {"angle", m.angle}, {"height", m.height},
                             {"terraces", m.terraces}});
        }
        out["mesas"] = mesas;
    }
    if (!g.pads.empty()) {
        json pads = json::array();
        for (const LunarPad& p : g.pads) pads.push_back({{"x", p.x}, {"z", p.z}, {"radius", p.radius}, {"level", p.level}});
        out["pads"] = pads;
    }
    if (g.yardangs != 0.0f) {
        out["yardangs"] = g.yardangs;
        out["wind"] = g.wind;
    }
    return out;
}

} // namespace avgen::scene
