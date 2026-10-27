#include "scene/attractor_world.hpp"

#include "core/rng.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <atomic>
#include <cstring>
#include <mutex>
#include <string_view>
#include <thread>

namespace avgen::scene {

using json = nlohmann::json;

// ---- noise (scene-local: 2D gradient noise, its fbm and Musgrave's ridged multifractal) ------------------------

namespace {

constexpr float kPi = 3.14159265358979f;

std::uint32_t hashU(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

std::uint32_t hash2i(std::int32_t x, std::int32_t y, std::uint32_t seed) {
    return hashU(static_cast<std::uint32_t>(x) * 0x8da6b343U ^ hashU(static_cast<std::uint32_t>(y) * 0xd8163841U ^ seed));
}

float hash01(std::int32_t x, std::int32_t y, std::uint32_t seed) {
    return static_cast<float>(hash2i(x, y, seed) >> 8) * (1.0f / 16777216.0f);
}

glm::vec2 gradAt(std::int32_t x, std::int32_t y, std::uint32_t seed) {
    const float a = hash01(x, y, seed) * 2.0f * kPi;
    return {std::cos(a), std::sin(a)};
}

// Gradient noise in about [-0.7, 0.7], quintic fade.
float gnoise(glm::vec2 p, std::uint32_t seed) {
    const float fx = std::floor(p.x), fy = std::floor(p.y);
    const auto ix = static_cast<std::int32_t>(fx), iy = static_cast<std::int32_t>(fy);
    const glm::vec2 f(p.x - fx, p.y - fy);
    const glm::vec2 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    const float a = glm::dot(gradAt(ix, iy, seed), f);
    const float b = glm::dot(gradAt(ix + 1, iy, seed), f - glm::vec2(1, 0));
    const float c = glm::dot(gradAt(ix, iy + 1, seed), f - glm::vec2(0, 1));
    const float d = glm::dot(gradAt(ix + 1, iy + 1, seed), f - glm::vec2(1, 1));
    return a + (b - a) * u.x + (c - a) * u.y + (a - b - c + d) * u.x * u.y;
}

const glm::mat2 kOct{0.80f, 0.60f, -0.60f, 0.80f}; // rotation between octaves (Quilez), breaks axis alignment

float fbm(glm::vec2 p, int octaves, std::uint32_t seed, float gain = 0.5f) {
    float sum = 0.0f, amp = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * gnoise(p, seed + static_cast<std::uint32_t>(i) * 101u);
        norm += amp;
        amp *= gain;
        p = kOct * p * 2.03f;
    }
    return sum / norm; // about [-0.7, 0.7]
}

// Musgrave's ridged multifractal (Texturing & Modeling, ch. 16): each octave is weighted by the previous one's
// signal, so detail gathers on the ridges and the valleys between them stay smooth. Returns about [0, 1].
float ridged(glm::vec2 p, int octaves, std::uint32_t seed, float gain = 2.0f, float offset = 1.0f) {
    float sum = 0.0f, weight = 1.0f, amp = 1.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        float s = offset - std::abs(gnoise(p, seed + static_cast<std::uint32_t>(i) * 131u) * 1.4f);
        s *= s;
        s *= weight;
        weight = std::clamp(s * gain, 0.0f, 1.0f);
        sum += s * amp;
        norm += amp;
        amp *= 0.5f;
        p = kOct * p * 2.1f;
    }
    return sum / norm;
}

float smoothstepf(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float segDist(glm::vec2 p, glm::vec2 a, glm::vec2 b, float& along) {
    const glm::vec2 ab = b - a;
    const float t = std::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
    along = t;
    return glm::length(p - (a + ab * t));
}

// Runs `fn(row)` for every row on all cores. Each row is independent and writes only its own texels, so the result
// does not depend on the thread count.
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

std::uint64_t mixHash(std::uint64_t h, std::uint64_t v) {
    return (h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2))) * 1099511628211ull;
}
std::uint64_t mixHashF(std::uint64_t h, float v) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, 4);
    return mixHash(h, bits);
}

// The strata's band profile (shared, line for line, with world.wgsl's `bandProfile`): the coordinate `s` mapped to the
// height the rock keeps. Each band is hard or soft by a per-band hash. A hard band (a cliff-former) rises in the last
// fifth of its thickness and leaves a ledge; a soft band (a slope-former) keeps the slope it was given.
float bandProfile(float s, float band, std::uint32_t seed) {
    const float t = s / band;
    const float i = std::floor(t);
    const float f = t - i;
    const float hard = smoothstepf(0.35f, 0.75f, hash01(static_cast<std::int32_t>(i), 7, seed));
    const float riser = 0.2f;
    const float gg = std::clamp((f - (1.0f - riser)) / riser, 0.0f, 1.0f);
    const float step = gg * gg * (3.0f - 2.0f * gg);
    return (i + f + (step - f) * hard) * band;
}

// ---- the layout ---------------------------------------------------------------------------------------------------

struct Frame {
    glm::vec2 origin; // camera xz
    glm::vec2 fwd;    // unit
    glm::vec2 right;  // unit
    [[nodiscard]] glm::vec2 world(float u, float v) const { return origin + right * u + fwd * v; }
};

glm::vec2 catmull(glm::vec2 p0, glm::vec2 p1, glm::vec2 p2, glm::vec2 p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

} // namespace

// ---- the flythrough (ADR-1853) ------------------------------------------------------------------------------------

namespace {

// Gaussian smoothing of a sampled signal, clamped at its ends.
template <class T>
std::vector<T> smoothSamples(const std::vector<T>& in, float sigmaSamples) {
    if (in.size() < 3 || !(sigmaSamples > 0.0f)) return in;
    const int r = static_cast<int>(std::ceil(3.0f * sigmaSamples));
    std::vector<float> k(static_cast<std::size_t>(2 * r + 1));
    float sum = 0.0f;
    for (int i = -r; i <= r; ++i) {
        k[static_cast<std::size_t>(i + r)] = std::exp(-0.5f * (i * i) / (sigmaSamples * sigmaSamples));
        sum += k[static_cast<std::size_t>(i + r)];
    }
    std::vector<T> out(in.size());
    const int n = static_cast<int>(in.size());
    for (int i = 0; i < n; ++i) {
        T acc{};
        for (int j = -r; j <= r; ++j) {
            acc += in[static_cast<std::size_t>(std::clamp(i + j, 0, n - 1))] * (k[static_cast<std::size_t>(j + r)] / sum);
        }
        out[static_cast<std::size_t>(i)] = acc;
    }
    return out;
}

// The flight, in five movements over `flightSeconds`:
//   * the vista: the composed view, pushing gently forward;
//   * the dive: off the lip and down into the inner gorge;
//   * the river: low along the gorge's centreline, 25-45 m above the floor, following every bend;
//   * the climb: up the gorge's walls and the basin's cliffs to above the plateau;
//   * the reveal: slow and level over the canyon country toward the far wall.
// The path is smoothed, then lifted clear of the terrain (22 m around it, 14 m above it) and smoothed again until it
// stays clear, so it never touches rock. Banking follows the turn rate.
void buildFlight(AttractorWorld& w, const AttractorGen& g, float Lb, float L1) {
    w.flight.clear();
    if (w.gorgeLine.size() < 8) return;
    const float S = g.terrainScale;
    const float D = g.flightSeconds;
    const float rate = AttractorWorld::kFlightRate;
    const int n = static_cast<int>(D * rate) + 1;
    // the gorge by arc length
    std::vector<float> arc(w.gorgeLine.size(), 0.0f);
    for (std::size_t i = 1; i < w.gorgeLine.size(); ++i) arc[i] = arc[i - 1] + glm::length(w.gorgeLine[i] - w.gorgeLine[i - 1]);
    const auto gorgeAt = [&](float sa) {
        sa = std::clamp(sa, 0.0f, arc.back());
        std::size_t i = 1;
        while (i + 1 < arc.size() && arc[i] < sa) ++i;
        const float t = arc[i] > arc[i - 1] ? (sa - arc[i - 1]) / (arc[i] - arc[i - 1]) : 0.0f;
        return glm::mix(w.gorgeLine[i - 1], w.gorgeLine[i], std::clamp(t, 0.0f, 1.0f));
    };
    // where the gorge passes closest in front of the vista's eye
    const glm::vec2 eyeXZ(w.eye.x, w.eye.z);
    const glm::vec2 fwd = glm::normalize(glm::vec2(w.target.x - w.eye.x, w.target.z - w.eye.z));
    float s0 = 0.0f, bestD = 1e30f;
    for (std::size_t i = 0; i < w.gorgeLine.size(); ++i) {
        const glm::vec2 d = w.gorgeLine[i] - eyeXZ;
        const float along = glm::dot(d, fwd);
        if (along < 150.0f * S) continue;
        const float dist = glm::length(d) + 0.5f * std::abs(glm::dot(d, glm::vec2(-fwd.y, fwd.x)));
        if (dist < bestD) {
            bestD = dist;
            s0 = arc[i];
        }
    }
    const float plateau = L1 + Lb; // the plateau's height above the gorge floor (about)
    const auto phase = [&](float t, float a, float b) { return smoothstepf(a * D, b * D, t); };
    std::vector<glm::vec3> raw(static_cast<std::size_t>(n));
    float sa = s0;
    for (int k = 0; k < n; ++k) {
        const float t = static_cast<float>(k) / rate;
        // speed along the gorge: slow in the dive, fast on the river, slowing for the reveal
        const float v = (45.0f + 35.0f * phase(t, 0.12f, 0.25f) - 10.0f * phase(t, 0.62f, 0.75f) -
                         40.0f * phase(t, 0.82f, 1.0f)) * S;
        if (t > 0.14f * D) sa += v / rate * phase(t, 0.14f, 0.24f);
        const glm::vec2 gxz = gorgeAt(sa);
        // height above the floor: low on the river with a slow swell, rising through the climb and the reveal
        float alt = (34.0f + 9.0f * std::sin(t * 0.55f)) * S;
        alt += (plateau + 70.0f * S - alt) * phase(t, 0.66f, 0.84f);
        alt += 40.0f * S * phase(t, 0.84f, 1.0f);
        const glm::vec3 river(gxz.x, w.sample(gxz) + alt, gxz.y);
        // the vista: the composed eye, easing forward
        const glm::vec3 vista = w.eye + glm::vec3(fwd.x, 0.0f, fwd.y) * (30.0f * t);
        const float b = phase(t, 0.04f, 0.28f); // the dive: about thirteen seconds off the lip into the gorge
        raw[static_cast<std::size_t>(k)] = glm::mix(vista, river, b);
    }
    // smooth, then keep clear of the land: lift above everything within 22 m, smooth again, until it holds
    // smooth (faded in from the start, so the first frame is the composed vista exactly)
    const std::vector<glm::vec3> smoothed = smoothSamples(raw, 0.6f * rate);
    std::vector<glm::vec3> path(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        path[i] = glm::mix(raw[i], smoothed[i], phase(static_cast<float>(i) / rate, 0.0f, 0.06f));
    }
    const auto clearance = [&](glm::vec3 p) {
        float hmax = w.sample(glm::vec2(p.x, p.z));
        for (int a = 0; a < 8; ++a) {
            const float ang = static_cast<float>(a) * 0.7853982f;
            hmax = std::max(hmax, w.sample(glm::vec2(p.x, p.z) + 22.0f * glm::vec2(std::cos(ang), std::sin(ang))));
        }
        return hmax + 14.0f;
    };
    // Height: the wanted height or the clearance, whichever is higher, lifted by a cone envelope --
    // y_i = max_j (c_j - k |i - j|) -- which is above every c_j and climbs or falls at most k m/s; then a light
    // smoothing, allowed to settle at most 5 m into the 14 m margin.
    std::vector<float> c(path.size());
    for (std::size_t i = 0; i < path.size(); ++i) c[i] = std::max(path[i].y, clearance(path[i]));
    const float kRise = 28.0f * S / rate; // metres per sample
    std::vector<float> ys(c);
    for (std::size_t i = 1; i < ys.size(); ++i) ys[i] = std::max(ys[i], ys[i - 1] - kRise);
    for (std::size_t i = ys.size() - 1; i-- > 0;) ys[i] = std::max(ys[i], ys[i + 1] - kRise);
    ys = smoothSamples(ys, 0.35f * rate);
    for (std::size_t i = 0; i < ys.size(); ++i) ys[i] = std::max(ys[i], c[i] - 5.0f);
    ys[0] = path[0].y;
    // bank: proportional to the turn rate, toward the turn
    std::vector<float> heading(path.size(), 0.0f);
    for (std::size_t i = 0; i < path.size(); ++i) {
        const std::size_t a = i == 0 ? 0 : i - 1, b2 = std::min(i + 1, path.size() - 1);
        heading[i] = std::atan2(path[b2].z - path[a].z, path[b2].x - path[a].x);
    }
    std::vector<float> turn(path.size(), 0.0f);
    for (std::size_t i = 1; i + 1 < path.size(); ++i) {
        float dh = heading[i + 1] - heading[i - 1];
        while (dh > kPi) dh -= 2.0f * kPi;
        while (dh < -kPi) dh += 2.0f * kPi;
        turn[i] = dh * rate * 0.5f; // radians per second
    }
    turn = smoothSamples(turn, 0.8f * rate);
    w.flight.resize(path.size());
    for (std::size_t i = 0; i < path.size(); ++i) {
        const float roll = std::clamp(-1.6f * turn[i], -0.2f, 0.2f) * phase(static_cast<float>(i) / rate, 0.1f, 0.25f);
        w.flight[i] = glm::vec4(path[i].x, ys[i], path[i].z, roll);
    }
}

} // namespace

// The last world generated, and the count (acquireAttractorWorld, attractorWorldGenerations).
namespace {
std::mutex gWorldCacheMutex;
std::shared_ptr<const AttractorWorld> gWorldCache;
std::atomic<std::uint64_t> gGenerations{0};
} // namespace

// ---- validation, hashing ----------------------------------------------------------------------------------------

Result<void> AttractorGen::validate() const {
    if (resolution < 256 || resolution > 4096) return fail("attractorWorld: resolution must be in [256, 4096]");
    if (!(extent >= 2000.0f && extent <= 40000.0f)) return fail("attractorWorld: extent must be in [2000, 40000] m");
    const auto unit = [](float v) { return std::isfinite(v) && v >= 0.0f && v <= 1.0f; };
    if (!unit(roughness) || !unit(ridgeStrength) || !unit(domainWarp) || !unit(terraces) || !unit(erosion) ||
        !unit(strataFold)) {
        return fail("attractorWorld: roughness, ridgeStrength, domainWarp, terraces, erosion and strataFold must be in [0, 1]");
    }
    if (!(terrainScale >= 0.5f && terrainScale <= 1.6f)) return fail("attractorWorld: terrainScale must be in [0.5, 1.6]");
    if (!unit(branchingDensity)) return fail("attractorWorld: branchingDensity must be in [0, 1]");
    if (!(flightSeconds >= 20.0f && flightSeconds <= 120.0f)) return fail("attractorWorld: flightSeconds must be in [20, 120]");
    return {};
}

std::uint64_t AttractorGen::hash() const {
    std::uint64_t h = 1469598103934665603ull;
    h = mixHash(h, seed);
    h = mixHash(h, static_cast<std::uint64_t>(resolution));
    for (float v : {extent, terrainScale, roughness, ridgeStrength, domainWarp, terraces, erosion, strataFold,
                    branchingDensity, flightSeconds}) {
        h = mixHashF(h, v);
    }
    return h;
}

float AttractorWorld::sample(glm::vec2 xz) const {
    if (res <= 0 || height.empty()) return 0.0f;
    const float texel = extent / static_cast<float>(res);
    const glm::vec2 t = (xz - origin) / texel - 0.5f;
    const float fx = std::clamp(t.x, 0.0f, static_cast<float>(res - 1));
    const float fy = std::clamp(t.y, 0.0f, static_cast<float>(res - 1));
    const int x0 = std::min(static_cast<int>(fx), res - 2), y0 = std::min(static_cast<int>(fy), res - 2);
    const float ax = fx - static_cast<float>(x0), ay = fy - static_cast<float>(y0);
    const auto at = [&](int x, int y) { return height[static_cast<std::size_t>(y) * res + x]; };
    const float a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * ax;
    const float b = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * ax;
    return a + (b - a) * ay;
}

// ---- generation -------------------------------------------------------------------------------------------------

std::shared_ptr<const AttractorWorld> generateAttractorWorld(const AttractorGen& gen) {
    const auto t0 = std::chrono::steady_clock::now();
    auto world = std::make_shared<AttractorWorld>();
    AttractorWorld& w = *world;
    AttractorGen g = gen;
    if (!g.validate()) {
        g = AttractorGen{}; // an invalid set never reaches here through the parameters; this keeps it safe anyway
    }
    w.gen = g;
    w.hash = g.hash();
    w.res = g.resolution;
    w.extent = g.extent;
    w.origin = glm::vec2(-0.5f * g.extent);
    const float S = g.terrainScale;
    Rng rng(0x5eed0000ull + g.seed, 0x5a77ac70ull);
    const std::uint32_t nseed = hashU(g.seed * 0x9e3779b1u + 17u);

    // The composition frame: the camera stands on a promontory toward the near edge and looks across the square.
    const float az = rng.range(-0.22f, 0.22f); // view azimuth jitter (radians) about -Z
    Frame fr;
    fr.fwd = glm::vec2(std::sin(az), -std::cos(az));
    fr.right = glm::vec2(-fr.fwd.y, fr.fwd.x);
    fr.origin = glm::vec2(0.0f, 0.30f * g.extent);
    const float side = rng.nextFloat() < 0.5f ? -1.0f : 1.0f; // which side the near escarpment stands on
    w.viewAzimuth = std::atan2(fr.fwd.x, fr.fwd.y);
    w.noiseSeed = nseed;
    w.sunSide = -side; // the escarpment's valley face looks toward -side

    // Canyon country (02-plan.md §terrain): a plateau dissected by an erosion front. One domain-warped field F decides
    // where the plateau stands (F > 0) and where it has been eaten back to the basin floor (F < 0); the plateau's
    // edge is F's zero contour, so the escarpments, fingers, amphitheatres and stranded buttes are all one fractal
    // coastline. A handful of composition terms bias F so that every seed frames the same kind of picture: the
    // camera's promontory, a basin along the valley, the near escarpment, the mid massif, and the far wall.
    const float L1 = rng.range(240.0f, 320.0f) * S;       // the plateau
    const float L2 = L1 + rng.range(150.0f, 230.0f) * S;  // the upper tier, mostly far away
    const float Lb = rng.range(55.0f, 85.0f) * S;         // the bench the inner gorge is cut into
    const float camU = -side * rng.range(150.0f, 320.0f);
    const glm::vec2 camXZ = fr.world(camU, 0.0f);

    // The valley's trunk: from below the promontory out through the far wall.
    std::vector<glm::vec2> ctrl;
    {
        const float bend = (rng.nextFloat() < 0.5f ? -1.0f : 1.0f) * rng.range(300.0f, 800.0f);
        const float ph = rng.range(-0.5f, 0.5f);
        const float vs[] = {-1500.0f, -300.0f, 900.0f, 2300.0f, 3900.0f, 5600.0f, 7600.0f, 10500.0f};
        for (int i = 0; i < 8; ++i) {
            const float v = vs[i] * S;
            const float k = std::max(0.0f, v) / 7000.0f;
            const float u = camU + side * 420.0f * std::min(1.0f, std::max(0.0f, (v + 300.0f) / 1500.0f)) * S +
                            bend * std::sin(k * 3.0f + ph) * k + rng.range(-150.0f, 150.0f) * k;
            ctrl.push_back(fr.world(u, v));
        }
        for (int i = 0; i + 3 < static_cast<int>(ctrl.size()); ++i) {
            for (int st = 0; st < 24; ++st) {
                w.valley.push_back(catmull(ctrl[i], ctrl[i + 1], ctrl[i + 2], ctrl[i + 3], st / 24.0f));
            }
        }
        w.valley.push_back(ctrl[ctrl.size() - 2]);
    }

    // The composition's masses (recorded for the landmarks of later stages; F carries them).
    const float alongAngle = std::atan2(fr.fwd.y, fr.fwd.x);
    const auto addMass = [&](float u, float v, float along, float across, float angleOffset, float height, int kind,
                             float strength) {
        AttractorMass m;
        m.centre = fr.world(u * S, v * S);
        m.axes = glm::vec2(along, across) * S;
        m.angle = alongAngle + angleOffset;
        m.height = height;
        m.kind = kind;
        w.masses.push_back(m);
        return strength;
    };
    std::vector<float> massStrength;
    // 0: the near escarpment, on `side`, running out along the valley.
    massStrength.push_back(addMass(side * rng.range(650.0f, 1050.0f), rng.range(1500.0f, 2300.0f),
                                   rng.range(1300.0f, 1900.0f), rng.range(350.0f, 520.0f), rng.range(-0.3f, 0.3f), L1,
                                   0, rng.range(0.9f, 1.2f)));
    // 1: the mid massif opposite: an island the basin has stranded; it will carry the recursive landmarks.
    massStrength.push_back(addMass(-side * rng.range(500.0f, 1000.0f), rng.range(3000.0f, 4300.0f),
                                   rng.range(600.0f, 900.0f), rng.range(420.0f, 650.0f), rng.range(-0.8f, 0.8f), L2, 2,
                                   rng.range(1.2f, 1.6f)));
    // 2: the far wall across the view, the upper tier's edge.
    massStrength.push_back(addMass(rng.range(-800.0f, 800.0f), rng.range(7200.0f, 8400.0f), 6000.0f,
                                   rng.range(900.0f, 1400.0f), kPi * 0.5f + rng.range(-0.2f, 0.2f), L2, 3, 1.4f));
    const float warpGain = 1.0f + 3.0f * g.domainWarp;
    const float frontScale = rng.range(2000.0f, 2800.0f) * S; // metres per unit of the erosion front's noise
    const float tier2 = rng.range(0.42f, 0.62f);

    // Strata: a tilted, folded stack (shared with the shader, which colours by the same coordinate).
    for (auto& st : w.strataSeed) {
        st = glm::vec4(rng.range(0.0f, 2.0f * kPi), rng.range(0.03f, 0.09f), rng.range(19.0f, 27.0f) * S,
                       rng.range(0.0f, 100.0f));
    }
    // [1]: the fold -- x direction angle, y wavelength (m), z amplitude (m) at `strataFold` 1. A wave in the strata
    // coordinate that bends the terraces and the colour bands together: anticlines no sediment would make.
    w.strataSeed[1] = glm::vec4(rng.range(0.0f, 2.0f * kPi), rng.range(900.0f, 1700.0f) * S, 110.0f * S * g.strataFold, 0.0f);
    const glm::vec4 strata = w.strataSeed[0];
    const glm::vec2 tiltDir(std::cos(strata.x), std::sin(strata.x));
    const glm::vec4 fold = w.strataSeed[1];
    const glm::vec2 foldDir(std::cos(fold.x), std::sin(fold.x));

    // ---- pass 1: the height field and the rock and playa masks ----
    const int N = w.res;
    const float texel = g.extent / static_cast<float>(N);
    w.height.assign(static_cast<std::size_t>(N) * N, 0.0f);
    std::vector<float> rock(static_cast<std::size_t>(N) * N, 0.0f), playa(rock.size(), 0.0f);
    std::vector<float> strataOffset(rock.size(), 0.0f);
    const float half = 0.5f * g.extent;
    // Pass 1a: the erosion front F and the valley distances, per texel.
    std::vector<float> fieldF(static_cast<std::size_t>(N) * N), trunkD(fieldF.size()), meanderD(fieldF.size());
    parallelRows(N, [&](int y) {
        for (int x = 0; x < N; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * N + x;
            const glm::vec2 p = w.origin + (glm::vec2(x, y) + 0.5f) * texel;
            const float v = glm::dot(p - fr.origin, fr.fwd);
            // Quilez's nested domain warp, so the plateau's edge folds and flows.
            const glm::vec2 q = p / frontScale;
            const glm::vec2 w1(fbm(q, 4, nseed + 21u), fbm(q + glm::vec2(5.2f, 1.3f), 4, nseed + 22u));
            const glm::vec2 w2(fbm(q + 3.0f * w1 + glm::vec2(1.7f, 9.2f), 4, nseed + 23u),
                               fbm(q + 3.0f * w1 + glm::vec2(8.3f, 2.8f), 4, nseed + 24u));
            float F = 1.5f * fbm(q + warpGain * 0.5f * w2, 6, nseed + 25u) + 0.12f;
            float along = 0.0f, best = 1e9f;
            for (std::size_t i = 0; i + 1 < w.valley.size(); ++i) {
                best = std::min(best, segDist(p, w.valley[i], w.valley[i + 1], along));
            }
            const float vpos = std::max(v, 0.0f);
            const float basinW = (380.0f + 0.14f * vpos) * S;
            F -= 1.25f * std::exp(-(best * best) / (basinW * basinW));
            const float dc = glm::length(p - camXZ) / (340.0f * S); // the camera's promontory
            F += 1.6f * std::exp(-dc * dc);
            for (std::size_t m = 0; m < w.masses.size(); ++m) { // the composed masses
                const AttractorMass& ms = w.masses[m];
                const float c = std::cos(ms.angle), sn = std::sin(ms.angle);
                const glm::vec2 d = p - ms.centre;
                const glm::vec2 n((c * d.x + sn * d.y) / ms.axes.x, (-sn * d.x + c * d.y) / ms.axes.y);
                F += massStrength[m] * std::exp(-glm::dot(n, n));
            }
            F += 0.9f * smoothstepf(6500.0f * S, 9500.0f * S, v); // the far ramp up to the upper tier
            const float edge = std::max(std::abs(p.x), std::abs(p.y)) / half;
            F += 1.4f * smoothstepf(0.82f, 0.95f, edge); // the upper tier near the square's edge hides it
            fieldF[idx] = F;
            trunkD[idx] = best;
            const glm::vec2 mp = p + 260.0f * S * glm::vec2(fbm(p / 900.0f, 3, nseed + 41u), fbm(p / 900.0f, 3, nseed + 42u));
            float bestM = 1e9f;
            for (std::size_t i = 0; i + 1 < w.valley.size(); ++i) {
                bestM = std::min(bestM, segDist(mp, w.valley[i], w.valley[i + 1], along));
            }
            meanderD[idx] = bestM;
        }
    });

    // Pass 1b: heights. F's contours become metric: `F / |grad F|` is (to first order) the signed distance in metres
    // to the plateau's edge, so every cliff has a set width whatever the field's local steepness -- no cliff thinner
    // than the texels that carry it, which is what drew saw-teeth along diagonal faces.
    parallelRows(N, [&](int y) {
        for (int x = 0; x < N; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * N + x;
            const glm::vec2 p = w.origin + (glm::vec2(x, y) + 0.5f) * texel;
            const float v = glm::dot(p - fr.origin, fr.fwd);
            const float vpos = std::max(v, 0.0f);
            const auto Fat = [&](int xx, int yy) {
                return fieldF[static_cast<std::size_t>(std::clamp(yy, 0, N - 1)) * N + std::clamp(xx, 0, N - 1)];
            };
            const float F = fieldF[idx];
            // a wide stencil (the field's fine octaves would make the gradient, and so the distance, noisy) and a floor
            // (where F is nearly flat the ratio is meaningless: there the plateau is far from any edge anyway)
            const int k = std::max(2, static_cast<int>(std::lround(24.0f / texel)));
            const glm::vec2 gF((Fat(x + k, y) - Fat(x - k, y)) / (2.0f * k * texel), (Fat(x, y + k) - Fat(x, y - k)) / (2.0f * k * texel));
            const float gl = std::max(glm::length(gF), 0.0015f / S);
            const auto metres = [&](float level) { return std::clamp((F - level) / gl, -4000.0f, 4000.0f); };
            const float d1 = metres(0.0f), d2 = metres(tier2);
            const float tier3 = 1.0f + 0.25f * fbm(p / 700.0f, 3, nseed + 35u);
            const float d3 = metres(tier3);
            const float Wc = 20.0f * S; // half the cliff's horizontal width
            const float Wt = 150.0f * S; // the talus apron's reach
            // Height: a sharp cliff over a talus apron, a second tier, stranded buttes on top.
            const float cliff = smoothstepf(-Wc, Wc, d1);
            const float talus = 0.32f * smoothstepf(-Wt, 0.0f, d1);
            const float upper = smoothstepf(-Wc, Wc, d2);
            const float upperTalus = 0.3f * smoothstepf(-Wt, 0.0f, d2);
            float h = L1 * std::max(cliff, talus) + (L2 - L1) * std::max(upper, upperTalus);
            h += 0.45f * (L2 - L1) * smoothstepf(-Wc, Wc, d3);
            // plateau tops: a broken caprock; the basin floor: low swells
            const float top = std::max(cliff, upper);
            h += top * (3.0f + 9.0f * g.roughness) * fbm(p / 300.0f, 4, nseed + 31u);
            h += (1.0f - top) * (10.0f * fbm(p / 900.0f, 3, nseed + 32u) + 3.0f * g.roughness * fbm(p / 120.0f, 3, nseed + 33u));
            // ridged relief on the cliffs and talus (what the ridge strength controls)
            const float faceBand = smoothstepf(-120.0f * S, -15.0f * S, d1) * (1.0f - smoothstepf(5.0f * S, 40.0f * S, d1));
            h += faceBand * g.ridgeStrength * 22.0f * S * fbm(p / 420.0f, 3, nseed + 34u); // rounded: ridged crests notched near rims into teeth
            float r = smoothstepf(-170.0f * S, -5.0f * S, d1);

            // The inner gorge: the basin floor is a bench, and the valley's channel is cut into it as a narrow gorge
            // with stepped walls, wandering on its own meander about the trunk.
            const float chW = (55.0f + 0.010f * vpos) * S;
            const float best = meanderD[idx];
            const float onBasin = 1.0f - smoothstepf(-40.0f * S, 40.0f * S, d1);
            h += Lb;
            const float gorge = smoothstepf(chW * 1.35f, chW * 0.85f, best) * onBasin;
            h -= (Lb + 2.0f) * gorge;
            const float wall = smoothstepf(chW * 1.9f, chW * 1.2f, best) * (1.0f - smoothstepf(chW * 1.0f, chW * 0.75f, best));
            r = std::max(r, wall * onBasin);
            // Terraces: steps cut along the tilted, warped, folded strata coordinate, on the cliffs only. The offset
            // (strata coordinate minus height) is baked for the shader, which cuts the same bands again at
            // sub-texel scale on the steep faces (world.wgsl, `bandProfile`).
            const float offset = strata.y * glm::dot(p, tiltDir) +
                                 (10.0f + 60.0f * g.domainWarp) * fbm(p / 1300.0f, 3, nseed + 3u) +
                                 fold.z * std::sin(glm::dot(p, foldDir) * (2.0f * kPi / fold.y));
            strataOffset[idx] = offset;
            if (g.terraces > 0.0f) {
                const float sc = h + offset;
                const float T = bandProfile(sc, strata.z, nseed);
                const float cliffBand = smoothstepf(-110.0f * S, -12.0f * S, d1) * (1.0f - 0.7f * top);
                h += (T - sc) * g.terraces * std::max(cliffBand, wall * onBasin);
            }
            playa[idx] = smoothstepf(chW * 0.85f, chW * 0.55f, best) * (1.0f - r);
            w.height[idx] = h;
            rock[idx] = r;
        }
    });
    (void)camXZ;

    // ---- pass 1c: the branching network (ADR-1852) ----
    // Tributary gorges grown off the inner gorge across the bench: junctions at uneven spacing, mostly alternating
    // sides, each branch a random walk with a slow turn, tapering, stopping at the cliffs' talus; a second level grows
    // off the first. They are carved with the gorge's own profile, floors falling to meet the trunk's.
    w.networkArc.assign(static_cast<std::size_t>(N) * N, -1.0f);
    {
        Rng brng(0xb7a9c400ull + g.seed, 0x77aa11ull);
        const auto meanderWarp = [&](glm::vec2 p) {
            return 260.0f * S * glm::vec2(fbm(p / 900.0f, 3, nseed + 41u), fbm(p / 900.0f, 3, nseed + 42u));
        };
        const auto texelOf = [&](glm::vec2 p) {
            const glm::ivec2 t = glm::ivec2(glm::floor((p - w.origin) / texel));
            return glm::clamp(t, glm::ivec2(0), glm::ivec2(N - 1));
        };
        const auto rockAt = [&](glm::vec2 p) {
            const glm::ivec2 t = texelOf(p);
            return rock[static_cast<std::size_t>(t.y) * N + t.x];
        };
        // the gorge's centreline: x with x + warp(x) = q for points q along the trunk (a fixed-point solve)
        std::vector<glm::vec2> gorge;
        std::vector<float> gorgeArc;
        {
            float arc = 0.0f;
            for (std::size_t i = 0; i + 1 < w.valley.size(); ++i) {
                const float len = glm::length(w.valley[i + 1] - w.valley[i]);
                const int steps = std::max(1, static_cast<int>(len / 40.0f));
                for (int k = 0; k < steps; ++k) {
                    const glm::vec2 q = glm::mix(w.valley[i], w.valley[i + 1], static_cast<float>(k) / steps);
                    glm::vec2 x = q;
                    for (int it = 0; it < 4; ++it) x = q - meanderWarp(x);
                    if (!gorge.empty()) arc += glm::length(x - gorge.back());
                    gorge.push_back(x);
                    gorgeArc.push_back(arc);
                }
            }
        }
        w.gorgeLine = gorge;
        const float totalArc = gorgeArc.empty() ? 0.0f : gorgeArc.back();
        // `floor0` is the floor's height at the junction; it rises to the plateau's level at the branch's head, so a
        // side canyon cuts back through the talus and into the cliffs, notching the escarpment as real ones do.
        const auto grow = [&](auto&& self, glm::vec2 start, glm::vec2 dir, float width, float floor0, float length,
                              float startArc, int level) -> void {
            if (static_cast<int>(w.branches.size()) >= kAttractorMaxBranches) return;
            NetworkBranch b;
            b.level = level;
            b.startArc = startArc;
            float heading = std::atan2(dir.y, dir.x);
            const float turn = brng.range(-0.012f, 0.012f); // a slow, steady curl
            glm::vec2 p = start;
            const float step = 25.0f * S;
            const int count = std::max(2, static_cast<int>(length / step));
            for (int k = 0; k <= count; ++k) {
                const float frac = static_cast<float>(k) / count;
                b.points.push_back(p);
                b.widths.push_back(width * (1.0f - 0.7f * frac));
                b.depths.push_back(floor0 + (L1 + Lb - floor0) * 0.92f * std::pow(frac, 1.6f)); // the floor's height
                heading += turn + brng.range(-0.16f, 0.16f);
                p += step * glm::vec2(std::cos(heading), std::sin(heading));
                if (std::max(std::abs(p.x), std::abs(p.y)) > 0.8f * half) break;
            }
            if (b.points.size() < 4) return;
            w.branches.push_back(b);
            const NetworkBranch made = w.branches.back();
            if (level == 1 && g.branchingDensity > 0.25f) {
                const int kids = 1 + (brng.nextFloat() < g.branchingDensity ? 1 : 0);
                for (int c = 0; c < kids; ++c) {
                    const std::size_t at = static_cast<std::size_t>(made.points.size() * brng.range(0.25f, 0.7f));
                    if (at + 1 >= made.points.size()) continue;
                    const glm::vec2 t = glm::normalize(made.points[at + 1] - made.points[at]);
                    const float a = (brng.nextFloat() < 0.5f ? -1.0f : 1.0f) * brng.range(0.5f, 1.05f);
                    const glm::vec2 d(t.x * std::cos(a) - t.y * std::sin(a), t.x * std::sin(a) + t.y * std::cos(a));
                    self(self, made.points[at], d, made.widths[at] * 0.6f, made.depths[at],
                         length * brng.range(0.35f, 0.6f), startArc + at * step, 2);
                }
            }
        };
        if (g.branchingDensity > 0.0f && gorge.size() > 8) {
            float arc = brng.range(250.0f, 550.0f) * S;
            float lastSide = brng.nextFloat() < 0.5f ? -1.0f : 1.0f;
            std::size_t gi = 0;
            while (arc < totalArc - 300.0f) {
                while (gi + 1 < gorge.size() && gorgeArc[gi] < arc) ++gi;
                if (gi + 1 >= gorge.size()) break;
                const glm::vec2 tangent = glm::normalize(gorge[gi + 1] - gorge[gi]);
                const float sideSign = brng.nextFloat() < 0.7f ? -lastSide : lastSide;
                lastSide = sideSign;
                // pointing back toward the near end: tributaries join downstream, so they lean upstream
                const float a = sideSign * brng.range(0.65f, 1.25f);
                const glm::vec2 back = -tangent;
                const glm::vec2 d(back.x * std::cos(a) - back.y * std::sin(a), back.x * std::sin(a) + back.y * std::cos(a));
                const float vpos = std::max(0.0f, glm::dot(gorge[gi] - fr.origin, fr.fwd));
                const float chW = (55.0f + 0.010f * vpos) * S;
                grow(grow, gorge[gi], d, chW * brng.range(0.4f, 0.6f), -2.0f, brng.range(900.0f, 2200.0f) * S, arc, 1);
                arc += brng.range(380.0f, 900.0f) * S / (0.35f + g.branchingDensity);
            }
        }
        // carve: each segment lowers the land to its floor plus steep walls (h <= floor + 2.6 (d - w)); the lowest wins
        constexpr float kWallSlope = 2.6f;
        const float reach = (L1 + Lb) / kWallSlope;
        std::vector<float> carveTo(static_cast<std::size_t>(N) * N, 1e30f);
        for (const NetworkBranch& b : w.branches) {
            float along = b.startArc;
            for (std::size_t k = 0; k + 1 < b.points.size(); ++k) {
                const glm::vec2 a = b.points[k], c = b.points[k + 1];
                const float pad = std::max(b.widths[k], b.widths[k + 1]) + reach;
                const glm::ivec2 lo = texelOf(glm::min(a, c) - pad), hi = texelOf(glm::max(a, c) + pad);
                const float segLen = glm::length(c - a);
                for (int y = lo.y; y <= hi.y; ++y) {
                    for (int x = lo.x; x <= hi.x; ++x) {
                        const glm::vec2 p = w.origin + (glm::vec2(x, y) + 0.5f) * texel;
                        float t = 0.0f;
                        const float d = segDist(p, a, c, t);
                        const float wd = glm::mix(b.widths[k], b.widths[k + 1], t);
                        const float fl = glm::mix(b.depths[k], b.depths[k + 1], t);
                        const std::size_t idx = static_cast<std::size_t>(y) * N + x;
                        const float to = fl + kWallSlope * std::max(0.0f, d - wd) + 2.0f * smoothstepf(0.0f, wd, d);
                        if (to < carveTo[idx]) carveTo[idx] = to;
                        if (d < wd * 0.8f && fl < w.height[idx]) {
                            const float arcHere = along + t * segLen;
                            float& na = w.networkArc[idx];
                            na = na < 0.0f ? arcHere : std::min(na, arcHere);
                            playa[idx] = std::max(playa[idx], smoothstepf(wd * 0.8f, wd * 0.45f, d));
                        }
                    }
                }
                along += segLen;
            }
        }
        std::vector<std::uint8_t> touched(carveTo.size(), 0);
        for (std::size_t i = 0; i < carveTo.size(); ++i) {
            if (carveTo[i] < w.height[i]) {
                const float cut = w.height[i] - std::max(carveTo[i], -2.0f);
                w.height[i] -= cut;
                rock[i] = std::max(rock[i], smoothstepf(4.0f, 30.0f, cut)); // fresh walls are rock
                touched[i] = 1;
            }
        }
        // Where canyons meet or run side by side the carve leaves knife ridges narrower than a texel, which bilinear
        // sampling draws as rows of pinnacles. Two 3x3 passes over the carved texels (and their neighbours) take out
        // anything under about two texels and leave the canyons themselves as they were.
        for (int pass = 0; pass < 2; ++pass) {
            const std::vector<float> src = w.height;
            parallelRows(N, [&](int y) {
                for (int x = 0; x < N; ++x) {
                    bool near = false;
                    for (int j = -1; j <= 1 && !near; ++j) {
                        for (int i = -1; i <= 1 && !near; ++i) {
                            const int xx = std::clamp(x + i, 0, N - 1), yy = std::clamp(y + j, 0, N - 1);
                            near = touched[static_cast<std::size_t>(yy) * N + xx] != 0;
                        }
                    }
                    if (!near) continue;
                    float sum = 0.0f, wsum = 0.0f;
                    for (int j = -1; j <= 1; ++j) {
                        for (int i = -1; i <= 1; ++i) {
                            const int xx = std::clamp(x + i, 0, N - 1), yy = std::clamp(y + j, 0, N - 1);
                            const float wt = (i == 0 ? 2.0f : 1.0f) * (j == 0 ? 2.0f : 1.0f);
                            sum += wt * src[static_cast<std::size_t>(yy) * N + xx];
                            wsum += wt;
                        }
                    }
                    w.height[static_cast<std::size_t>(y) * N + x] = sum / wsum;
                }
            });
        }
    }

    // ---- pass 2: erosion gullies on the steep faces ----
    // A filter in the spirit of runevision's "Fast and Gorgeous Erosion Filter" (2026): stripes that run downhill,
    // phased from per-cell pivots and blended between neighbouring cells, added where the slope is steep. Three
    // octaves; each texel is independent, so it reads a copy of the field.
    std::vector<float> ridge(static_cast<std::size_t>(N) * N, 0.5f);
    if (g.erosion > 0.0f) {
        const std::vector<float> src = w.height;
        const auto H = [&](int x, int y) {
            x = std::clamp(x, 0, N - 1);
            y = std::clamp(y, 0, N - 1);
            return src[static_cast<std::size_t>(y) * N + x];
        };
        parallelRows(N, [&](int y) {
            for (int x = 0; x < N; ++x) {
                const glm::vec2 p = w.origin + (glm::vec2(x, y) + 0.5f) * texel;
                const glm::vec2 grad((H(x + 1, y) - H(x - 1, y)) / (2.0f * texel),
                                     (H(x, y + 1) - H(x, y - 1)) / (2.0f * texel));
                const float slope = glm::length(grad);
                const float steep = smoothstepf(0.25f, 0.9f, slope);
                if (steep <= 0.0f) {
                    continue;
                }
                const glm::vec2 dir = grad / std::max(slope, 1e-5f);
                const glm::vec2 across(-dir.y, dir.x);
                float offset = 0.0f;
                float period = 150.0f * S;
                float amp = 4.5f * S;
                for (int o = 0; o < 3 && period >= std::max(90.0f * S, 6.0f * texel); ++o) { // in metres, the same at every resolution: finer grooves read as vertical streaks and notch the rims
                    const float cell = period * 1.6f;
                    const glm::vec2 cp = p / cell;
                    const int cx = static_cast<int>(std::floor(cp.x)), cy = static_cast<int>(std::floor(cp.y));
                    float c = 0.0f, sn = 0.0f, wsum = 0.0f;
                    for (int j = -1; j <= 1; ++j) {
                        for (int i = -1; i <= 1; ++i) {
                            const std::uint32_t cs = nseed + 77u * static_cast<std::uint32_t>(o + 1);
                            const glm::vec2 pivot = (glm::vec2(cx + i, cy + j) +
                                                     glm::vec2(hash01(cx + i, cy + j, cs), hash01(cx + i, cy + j, cs + 9u))) *
                                                    cell;
                            const float dd = glm::length(p - pivot) / cell;
                            const float wt = std::pow(std::max(0.0f, 1.0f - dd / 1.5f), 3.0f);
                            const float ph = glm::dot(p - pivot, across) * (2.0f * kPi / period);
                            c += wt * std::cos(ph);
                            sn += wt * std::sin(ph);
                            wsum += wt;
                        }
                    }
                    if (wsum > 0.0f) {
                        c /= wsum;
                        sn /= wsum;
                        const float len = std::sqrt(c * c + sn * sn);
                        if (len > 1e-4f) {
                            c *= std::min(1.0f, 2.0f * len) / len; // runevision's normalisation: unit beyond 0.5
                        }
                    }
                    offset += amp * c;
                    if (o == 0) {
                        ridge[static_cast<std::size_t>(y) * N + x] = 0.5f + 0.5f * std::clamp(c, -1.0f, 1.0f);
                    }
                    period *= 0.5f;
                    amp *= 0.45f;
                }
                w.height[static_cast<std::size_t>(y) * N + x] += g.erosion * steep * offset;
            }
        });
    }

    // ---- pass 3: cavity (height minus a blurred height), then the packed masks ----
    std::vector<float> blur = w.height, tmp(w.height.size());
    const int R = 6;
    for (int pass = 0; pass < 2; ++pass) {
        const std::vector<float>& in = pass == 0 ? w.height : tmp;
        std::vector<float>& out = pass == 0 ? tmp : blur;
        parallelRows(N, [&](int y) {
            for (int x = 0; x < N; ++x) {
                float s = 0.0f;
                for (int k = -R; k <= R; ++k) {
                    const int xx = pass == 0 ? std::clamp(x + k, 0, N - 1) : x;
                    const int yy = pass == 0 ? y : std::clamp(y + k, 0, N - 1);
                    s += in[static_cast<std::size_t>(yy) * N + xx];
                }
                out[static_cast<std::size_t>(y) * N + x] = s / static_cast<float>(2 * R + 1);
            }
        });
    }
    w.strataOffset = std::move(strataOffset);
    w.masks.assign(w.height.size(), 0u);
    float lo = 1e30f, hi = -1e30f;
    for (std::size_t i = 0; i < w.height.size(); ++i) {
        lo = std::min(lo, w.height[i]);
        hi = std::max(hi, w.height[i]);
        const auto q = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        const float cav = 0.5f + (w.height[i] - blur[i]) / 40.0f;
        w.masks[i] = q(rock[i]) | (q(playa[i]) << 8) | (q(cav) << 16) | (q(ridge[i]) << 24);
    }
    w.minHeight = lo;
    w.maxHeight = hi;

    // ---- the composed camera ----
    // A deterministic search: stand on the plateau near the promontory, at a lip with an open view over the basin
    // toward the mid massif. The score wants a big drop just ahead and nothing rising into the view.
    {
        const glm::vec2 aimXZ = 0.6f * w.masses[1].centre + 0.4f * fr.world(0.0f, 6500.0f * S);
        const float eyeLift = 32.0f * S;
        float bestScore = -1e30f;
        glm::vec2 bestXZ = camXZ;
        float bestY = w.sample(camXZ) + eyeLift;
        for (float v = -400.0f; v <= 700.0f; v += 25.0f) {
            for (float u = -900.0f; u <= 900.0f; u += 25.0f) {
                const glm::vec2 c = fr.world(u * S, v * S);
                const float h0 = w.sample(c);
                if (h0 < 0.85f * L1 + Lb || h0 > L1 + Lb + 60.0f * S) {
                    continue; // on the plateau, not on a butte above it
                }
                const float eyeY = h0 + eyeLift;
                const glm::vec2 dir = glm::normalize(aimXZ - c);
                float drop = 0.0f, block = 0.0f;
                for (float d = 40.0f; d <= 1800.0f; d += 20.0f) {
                    const float hg = w.sample(c + dir * d);
                    if (d >= 200.0f && d <= 900.0f) drop += (eyeY - hg) / 36.0f;
                    // anything above a line 3 degrees below the eye blocks the view
                    block += std::max(0.0f, hg - (eyeY - d * 0.05f));
                }
                const float lipDist = [&] {
                    for (float d = 20.0f; d <= 600.0f; d += 20.0f) {
                        if (w.sample(c + dir * d) < h0 - 60.0f * S) return d;
                    }
                    return 600.0f;
                }();
                // nothing tall at the frame's sides close by: sample two rays 22 degrees either side
                float sideBlock = 0.0f;
                for (float sgn : {-1.0f, 1.0f}) {
                    const float a = sgn * 0.38f;
                    const glm::vec2 sd(dir.x * std::cos(a) - dir.y * std::sin(a), dir.x * std::sin(a) + dir.y * std::cos(a));
                    for (float d = 30.0f; d <= 500.0f; d += 20.0f) {
                        sideBlock += std::max(0.0f, w.sample(c + sd * d) - (eyeY - 10.0f));
                    }
                }
                // the lower corners: no mound rising above the ground the camera stands on, close by (it fills a
                // corner with a shapeless mass -- Gate 1's lower-left lump)
                float cornerMass = 0.0f;
                for (float sgn : {-1.0f, 1.0f}) {
                    for (float a = 0.25f; a <= 0.65f; a += 0.2f) {
                        const float ang = sgn * a;
                        const glm::vec2 sd(dir.x * std::cos(ang) - dir.y * std::sin(ang), dir.x * std::sin(ang) + dir.y * std::cos(ang));
                        for (float d = 30.0f; d <= 450.0f; d += 30.0f) {
                            cornerMass += std::max(0.0f, w.sample(c + sd * d) - (h0 + 8.0f));
                        }
                    }
                }
                const float score = drop - 4.0f * block - 1.5f * sideBlock - 0.8f * cornerMass - 0.25f * lipDist -
                                    0.05f * std::abs(u);
                if (score > bestScore) {
                    bestScore = score;
                    bestXZ = c;
                    bestY = eyeY;
                }
            }
        }
        const glm::vec2 dirXZ = glm::normalize(aimXZ - bestXZ);
        const float pitch = -rng.range(6.0f, 8.0f) * kPi / 180.0f;
        const glm::vec3 dir(dirXZ.x * std::cos(pitch), std::sin(pitch), dirXZ.y * std::cos(pitch));
        w.eye = glm::vec3(bestXZ.x, bestY, bestXZ.y);
        w.target = w.eye + dir * 2500.0f;
        w.fovDeg = rng.range(32.0f, 34.0f); // a longer lens: the layers compress and the corners crop
        w.viewAzimuth = std::atan2(dirXZ.x, dirXZ.y);
    }

    // ---- the flythrough (ADR-1853) ----
    buildFlight(w, g, Lb, L1);

    w.generateSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    ++gGenerations;
    return world;
}

std::shared_ptr<const AttractorWorld> acquireAttractorWorld(const AttractorGen& gen) {
    const std::lock_guard<std::mutex> lock(gWorldCacheMutex);
    if (gWorldCache && gWorldCache->hash == gen.hash()) {
        return gWorldCache;
    }
    gWorldCache = generateAttractorWorld(gen);
    return gWorldCache;
}

std::uint64_t attractorWorldGenerations() noexcept { return gGenerations.load(); }

AttractorCameraPose attractorCamera(const AttractorWorld& world, AttractorCameraMode mode, double seconds) {
    AttractorCameraPose pose{world.eye, world.target, 0.0f};
    if (mode != AttractorCameraMode::Flight || world.flight.size() < 2) {
        return pose;
    }
    // Linear between the 30 Hz samples (the path itself is already smooth); the aim looks a second and a half ahead
    // along the path, a little below it.
    const auto n = static_cast<int>(world.flight.size());
    const auto at = [&](double t) {
        const double f = std::clamp(t * AttractorWorld::kFlightRate, 0.0, static_cast<double>(n - 1));
        const int i = std::min(static_cast<int>(f), n - 2);
        const auto a = static_cast<float>(f - i);
        return glm::mix(world.flight[static_cast<std::size_t>(i)], world.flight[static_cast<std::size_t>(i) + 1], a);
    };
    const glm::vec4 e = at(seconds);
    glm::vec3 ahead = glm::vec3(at(seconds + 1.5));
    const glm::vec3 eye(e);
    if (glm::length(ahead - eye) < 1.0f) { // at the very end the path stops: keep looking the way it was going
        const glm::vec3 back(at(seconds - 1.5));
        ahead = eye + (eye - back);
    }
    pose.eye = eye;
    pose.target = ahead - glm::vec3(0.0f, 0.18f * glm::length(ahead - eye), 0.0f);
    pose.rollDegrees = glm::degrees(e.w);
    return pose;
}

// ---- JSON ---------------------------------------------------------------------------------------------------------

namespace {

constexpr std::string_view kKeys[] = {"enabled", "camera", "seed", "resolution", "extent", "terrainScale",
                                      "roughness", "ridgeStrength", "domainWarp", "terraces", "erosion", "strataFold",
                                      "branchingDensity", "flightSeconds", "look"};
constexpr std::string_view kLookKeys[] = {"palette", "sunAzimuth", "sunElevation", "sunIntensity", "ambient",
                                          "atmosphere", "strata", "exposure", "detail"};

} // namespace

Result<AttractorWorldBlock> attractorWorldFromJson(const json& j) try {
    if (!j.is_object()) return fail("'attractorWorld' must be an object");
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (std::find(std::begin(kKeys), std::end(kKeys), it.key()) == std::end(kKeys)) {
            return fail("attractorWorld: unknown key '{}'", it.key());
        }
    }
    AttractorWorldBlock a;
    a.enabled = j.value("enabled", true);
    if (j.contains("camera")) {
        const std::string mode = j.at("camera").get<std::string>();
        if (mode == "scene") a.camera = AttractorCameraMode::Scene;
        else if (mode == "composed") a.camera = AttractorCameraMode::Composed;
        else if (mode == "flight") a.camera = AttractorCameraMode::Flight;
        else return fail("attractorWorld: 'camera' must be \"scene\", \"composed\" or \"flight\"");
    }
    AttractorGen& g = a.gen;
    g.seed = j.value("seed", g.seed);
    g.resolution = j.value("resolution", g.resolution);
    g.extent = j.value("extent", g.extent);
    g.terrainScale = j.value("terrainScale", g.terrainScale);
    g.roughness = j.value("roughness", g.roughness);
    g.ridgeStrength = j.value("ridgeStrength", g.ridgeStrength);
    g.domainWarp = j.value("domainWarp", g.domainWarp);
    g.terraces = j.value("terraces", g.terraces);
    g.erosion = j.value("erosion", g.erosion);
    g.strataFold = j.value("strataFold", g.strataFold);
    g.branchingDensity = j.value("branchingDensity", g.branchingDensity);
    g.flightSeconds = j.value("flightSeconds", g.flightSeconds);
    if (auto ok = g.validate(); !ok) return std::unexpected(ok.error());
    if (j.contains("look")) {
        const json& l = j.at("look");
        if (!l.is_object()) return fail("attractorWorld: 'look' must be an object");
        for (auto it = l.begin(); it != l.end(); ++it) {
            if (std::find(std::begin(kLookKeys), std::end(kLookKeys), it.key()) == std::end(kLookKeys)) {
                return fail("attractorWorld: unknown look key '{}'", it.key());
            }
        }
        AttractorLook& k = a.look;
        k.palette = l.value("palette", k.palette);
        k.sunAzimuth = l.value("sunAzimuth", k.sunAzimuth);
        k.sunElevation = l.value("sunElevation", k.sunElevation);
        k.sunIntensity = l.value("sunIntensity", k.sunIntensity);
        k.ambient = l.value("ambient", k.ambient);
        k.atmosphere = l.value("atmosphere", k.atmosphere);
        k.strata = l.value("strata", k.strata);
        k.exposure = l.value("exposure", k.exposure);
        k.detail = l.value("detail", k.detail);
    }
    a.liveGen = a.gen;
    a.live = a.look;
    a.cameraMode = static_cast<int>(a.camera);
    return a;
} catch (const nlohmann::json::exception& e) {
    return fail("attractorWorld: {}", e.what()); // a key of the wrong type
}

json attractorWorldToJson(const AttractorWorldBlock& a) {
    const AttractorGen& g = a.gen;
    const AttractorLook& k = a.look;
    return json{{"enabled", a.enabled},
                {"camera", a.camera == AttractorCameraMode::Scene      ? "scene"
                           : a.camera == AttractorCameraMode::Flight ? "flight"
                                                                     : "composed"},
                {"seed", g.seed},
                {"resolution", g.resolution},
                {"extent", g.extent},
                {"terrainScale", g.terrainScale},
                {"roughness", g.roughness},
                {"ridgeStrength", g.ridgeStrength},
                {"domainWarp", g.domainWarp},
                {"terraces", g.terraces},
                {"erosion", g.erosion},
                {"strataFold", g.strataFold},
                {"branchingDensity", g.branchingDensity},
                {"flightSeconds", g.flightSeconds},
                {"look",
                 {{"palette", k.palette}, {"sunAzimuth", k.sunAzimuth}, {"sunElevation", k.sunElevation},
                  {"sunIntensity", k.sunIntensity}, {"ambient", k.ambient}, {"atmosphere", k.atmosphere},
                  {"strata", k.strata}, {"exposure", k.exposure}, {"detail", k.detail}}}};
}

// ---- parameters -------------------------------------------------------------------------------------------------

namespace {

struct Registrar {
    params::ParameterSet& params;
    AttractorWorldParameters& out;
    void f(const char* rel, const char* label, const char* group, float def, float lo, float hi) {
        params::ParamDesc<float> d;
        d.path = out.prefix + rel;
        d.label = label;
        d.group = group;
        d.defaultValue = def;
        d.hardMin = lo;
        d.hardMax = hi;
        d.softMin = lo;
        d.softMax = hi;
        out.all.push_back(&params.add(std::move(d)));
    }
    void i(const char* rel, const char* label, const char* group, int def, int lo, int hi) {
        params::ParamDesc<int> d;
        d.path = out.prefix + rel;
        d.label = label;
        d.group = group;
        d.defaultValue = def;
        d.hardMin = lo;
        d.hardMax = hi;
        d.softMin = lo;
        d.softMax = hi;
        out.all.push_back(&params.add(std::move(d)));
    }
};

params::IParameter* findLeaf(const AttractorWorldParameters& p, std::string_view leaf) {
    const std::size_t n = p.prefix.size();
    for (params::IParameter* ip : p.all) {
        const std::string& path = ip->path();
        if (path.size() == n + leaf.size() && path.compare(n, leaf.size(), leaf) == 0) {
            return ip;
        }
    }
    return nullptr;
}
float fval(const AttractorWorldParameters& p, std::string_view leaf, float fallback) {
    if (auto* fp = dynamic_cast<params::Parameter<float>*>(findLeaf(p, leaf))) return fp->value();
    return fallback;
}
int ival(const AttractorWorldParameters& p, std::string_view leaf, int fallback) {
    if (auto* ip = dynamic_cast<params::Parameter<int>*>(findLeaf(p, leaf))) return ip->value();
    return fallback;
}

} // namespace

AttractorWorldParameters registerAttractorWorldParameters(params::ParameterSet& params, const AttractorWorldBlock& rest,
                                                          const std::string& prefix) {
    AttractorWorldParameters p;
    p.prefix = prefix;
    Registrar r{params, p};
    const AttractorGen& g = rest.gen;
    const AttractorLook& k = rest.look;
    // World generation (structural: a change regenerates the world).
    r.i("seed", "world seed", "World Generation", static_cast<int>(g.seed), 0, 1 << 20);
    r.f("terrainScale", "terrain scale", "World Generation", g.terrainScale, 0.5f, 1.6f);
    r.f("roughness", "terrain roughness", "World Generation", g.roughness, 0.0f, 1.0f);
    r.f("ridgeStrength", "ridge strength", "World Generation", g.ridgeStrength, 0.0f, 1.0f);
    r.f("domainWarp", "domain warp", "World Generation", g.domainWarp, 0.0f, 1.0f);
    r.f("terraces", "strata terraces", "World Generation", g.terraces, 0.0f, 1.0f);
    r.f("erosion", "erosion gullies", "World Generation", g.erosion, 0.0f, 1.0f);
    r.f("strataFold", "strata fold", "World Generation", g.strataFold, 0.0f, 1.0f);
    // Geometry (structural)
    r.f("branchingDensity", "branching density (side canyons)", "Geometry", g.branchingDensity, 0.0f, 1.0f);
    r.f("flightSeconds", "flythrough length (s)", "Animation and Camera", g.flightSeconds, 20.0f, 120.0f);
    // Materials and atmosphere (continuous).
    r.f("palette", "palette (0 Dusk Mineral, 1 Nocturne, 2 Chromatic)", "Materials and Atmosphere", k.palette, 0.0f,
        2.0f);
    r.f("sunAzimuth", "sun azimuth (deg from the view)", "Materials and Atmosphere", k.sunAzimuth, -180.0f, 180.0f);
    r.f("sunElevation", "sun elevation (deg)", "Materials and Atmosphere", k.sunElevation, -4.0f, 60.0f);
    r.f("sunIntensity", "key light", "Materials and Atmosphere", k.sunIntensity, 0.0f, 3.0f);
    r.f("ambient", "ambient", "Materials and Atmosphere", k.ambient, 0.0f, 3.0f);
    r.f("atmosphere", "atmospheric depth", "Materials and Atmosphere", k.atmosphere, 0.0f, 4.0f);
    r.f("strata", "strata bands", "Materials and Atmosphere", k.strata, 0.0f, 2.0f);
    r.f("exposure", "exposure", "Materials and Atmosphere", k.exposure, 0.1f, 4.0f);
    r.f("detail", "near detail", "Quality and Rendering", k.detail, 0.0f, 2.0f);
    r.i("camera", "camera (0 the scene's, 1 the composed vista, 2 the flythrough)", "Animation and Camera",
        static_cast<int>(rest.camera), 0, 2);
    return p;
}

void applyAttractorWorldParameters(const AttractorWorldParameters& p, const AttractorWorldBlock& rest,
                                   AttractorWorldBlock& live) {
    AttractorGen g = rest.gen;
    g.seed = static_cast<std::uint32_t>(std::max(0, ival(p, "seed", static_cast<int>(g.seed))));
    g.terrainScale = fval(p, "terrainScale", g.terrainScale);
    g.roughness = fval(p, "roughness", g.roughness);
    g.ridgeStrength = fval(p, "ridgeStrength", g.ridgeStrength);
    g.domainWarp = fval(p, "domainWarp", g.domainWarp);
    g.terraces = fval(p, "terraces", g.terraces);
    g.erosion = fval(p, "erosion", g.erosion);
    g.strataFold = fval(p, "strataFold", g.strataFold);
    g.branchingDensity = fval(p, "branchingDensity", g.branchingDensity);
    g.flightSeconds = fval(p, "flightSeconds", g.flightSeconds);
    live.liveGen = g;
    AttractorLook k = rest.look;
    k.palette = fval(p, "palette", k.palette);
    k.sunAzimuth = fval(p, "sunAzimuth", k.sunAzimuth);
    k.sunElevation = fval(p, "sunElevation", k.sunElevation);
    k.sunIntensity = fval(p, "sunIntensity", k.sunIntensity);
    k.ambient = fval(p, "ambient", k.ambient);
    k.atmosphere = fval(p, "atmosphere", k.atmosphere);
    k.strata = fval(p, "strata", k.strata);
    k.exposure = fval(p, "exposure", k.exposure);
    k.detail = fval(p, "detail", k.detail);
    live.live = k;
    live.cameraMode = std::clamp(ival(p, "camera", static_cast<int>(rest.camera)), 0, 2);
}

} // namespace avgen::scene
