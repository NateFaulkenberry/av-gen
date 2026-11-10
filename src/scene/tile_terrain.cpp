#include "scene/tile_terrain.hpp"

#include "scene/generator.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <initializer_list>
#include <string>
#include <utility>

// The CPU half of ADR-1950's one rule. Its twin is `tileSample` in shaders/generator.wgsl: every operation here has a
// partner there, in the same order. Change one, change both, and the parity test (test_tile_terrain_gpu) says so.

namespace avgen::scene {

namespace {

// Seeds of the separate fields, xor-ed into the terrain's seed (the same constants in WGSL).
constexpr std::uint32_t kRegionSeed = 0xA5A5A5A5u;
constexpr std::uint32_t kContinentSeed = 0x00C0FFEEu;
constexpr std::uint32_t kWarpXSeed = 0x01234567u;
constexpr std::uint32_t kWarpZSeed = 0x07654321u;
constexpr std::uint32_t kHillSeed = 0x0000BEEFu;
constexpr std::uint32_t kBreathSeed = 0x00B4EA7Bu; // ADR-1953
constexpr std::uint32_t kPondSeed = 0x00B0D1E5u;  // ADR-1965: the ponds' shore noise
constexpr float kTwoPi = 6.28318530717958647692f;

float smooth01(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / std::max(e1 - e0, 1e-6f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

float tileValueNoise(std::uint32_t seed, float x, float z) {
    const float xf = std::floor(x);
    const float zf = std::floor(z);
    const auto ix = static_cast<std::int32_t>(xf);
    const auto iz = static_cast<std::int32_t>(zf);
    const float fx = x - xf;
    const float fz = z - zf;
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sz = fz * fz * (3.0f - 2.0f * fz);
    const float c00 = generatorUnit(generatorHash(ix, iz, seed, 101u));
    const float c10 = generatorUnit(generatorHash(ix + 1, iz, seed, 101u));
    const float c01 = generatorUnit(generatorHash(ix, iz + 1, seed, 101u));
    const float c11 = generatorUnit(generatorHash(ix + 1, iz + 1, seed, 101u));
    const float a = c00 + (c10 - c00) * sx;
    const float b = c01 + (c11 - c01) * sx;
    return a + (b - a) * sz;
}

float tileFbm(std::uint32_t seed, float x, float z, int octaves) {
    float sum = 0.0f;
    float norm = 0.0f;
    float amp = 1.0f;
    float freq = 1.0f;
    const int n = std::clamp(octaves, 1, 6);
    for (int o = 0; o < n; ++o) {
        const auto uo = static_cast<std::uint32_t>(o);
        const float fo = static_cast<float>(o);
        sum += amp * tileValueNoise(seed + uo * 1013u, x * freq + fo * 17.31f, z * freq - fo * 9.17f);
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return sum / norm;
}

namespace {

// The distance from (x, z) to a polyline of at most `cap` points, and (ADR-1965) the arc length along it to the nearest
// point. The same loop as the kernel's `tilePolyline` (ties go to the earlier segment).
glm::vec2 polylineNearest(const glm::vec2* line, std::size_t count, float x, float z) {
    float best = 1e9f;
    float along = 0.0f;
    if (count == 0) {
        return {best, 0.0f};
    }
    if (count == 1) {
        return {glm::length(glm::vec2(x, z) - line[0]), 0.0f};
    }
    float walked = 0.0f;
    for (std::size_t i = 0; i + 1 < count; ++i) {
        const glm::vec2 a = line[i];
        const glm::vec2 ab = line[i + 1] - a;
        const glm::vec2 ap = glm::vec2(x, z) - a;
        const float len2 = std::max(glm::dot(ab, ab), 1e-6f);
        const float t = std::clamp(glm::dot(ap, ab) / len2, 0.0f, 1.0f);
        const float d = glm::length(ap - ab * t);
        const float len = std::sqrt(len2);
        if (d < best) {
            best = d;
            along = walked + t * len;
        }
        walked += len;
    }
    return {best, along};
}

float polylineDistance(const std::vector<glm::vec2>& line, float x, float z, std::size_t cap) {
    return polylineNearest(line.data(), std::min(line.size(), cap), x, z).x;
}

float polylineLength(const std::vector<glm::vec2>& line, std::size_t cap) {
    float len = 0.0f;
    const std::size_t n = std::min(line.size(), cap);
    for (std::size_t i = 0; i + 1 < n; ++i) {
        len += std::sqrt(std::max(glm::dot(line[i + 1] - line[i], line[i + 1] - line[i]), 1e-6f));
    }
    return len;
}

} // namespace

float tilePathDistance(const TileTerrainSpec& spec, float x, float z) {
    return polylineDistance(spec.path, x, z, kMaxTilePathPoints);
}

float tileRiverDistance(const TileTerrainSpec& spec, float x, float z, std::size_t river) {
    return river < spec.rivers.size() ? polylineDistance(spec.rivers[river].points, x, z, kMaxTileRiverPoints) : 1e9f;
}

namespace {

// The land before the water bodies and the valleys (ADR-1965 splits it out so the "auto" levels can read it).
struct LandPart {
    float height = 0.0f; // above the sea level, before the shore mix
    float land = 0.0f;   // the continent's land share
    float c = 0.0f;      // the continent field
    float shaped = 0.0f;
    float inland = 0.0f;
};

LandPart landPart(const TileTerrainSpec& s, float x, float z, float t, float rollX, float rollZ, float breathAmp,
                  float breathPeriod, float breathWavelength) {
    const std::uint32_t seed = s.seed;
    // ADR-1953: the rolling formations -- the hills' domain (and its warp) drifts; the coast, the path and the rivers do
    // not.
    const float hx = x - rollX * t;
    const float hz = z - rollZ * t;
    const float region = tileValueNoise(seed ^ kRegionSeed, x / s.regionWavelength, z / s.regionWavelength);
    const float k = 1.0f + s.regionVariation * (2.0f * region - 1.0f);
    const float c = tileFbm(seed ^ kContinentSeed, x / s.continentWavelength, z / s.continentWavelength, 3);
    LandPart out;
    out.c = c;
    out.land = smooth01(s.landThreshold - s.shoreBand, s.landThreshold + s.shoreBand, c);
    out.inland = smooth01(s.landThreshold, s.landThreshold + s.inlandBand, c);
    const float wx =
        (tileFbm(seed ^ kWarpXSeed, hx / s.warpWavelength, hz / s.warpWavelength, 2) * 2.0f - 1.0f) * s.hillWarp;
    const float wz =
        (tileFbm(seed ^ kWarpZSeed, hx / s.warpWavelength + 31.7f, hz / s.warpWavelength - 11.3f, 2) * 2.0f - 1.0f) *
        s.hillWarp;
    const float hwl = s.hillWavelength * k;
    const float hn = tileFbm(seed ^ kHillSeed, (hx + wx) / hwl, (hz + wz) / hwl, s.hillOctaves);
    out.shaped = smooth01(s.hillLow, s.hillHigh, hn);
    const float excess = std::max(out.shaped - s.crownThreshold, 0.0f);
    const float hill = out.shaped - excess * s.crownFlatten;
    // ADR-1953: the breathing -- the relief swells and settles by region, each region a little out of step.
    float breath = 1.0f;
    if (breathAmp != 0.0f) {
        const float n = tileValueNoise(seed ^ kBreathSeed, x / breathWavelength, z / breathWavelength);
        breath = 1.0f + breathAmp * std::sin(kTwoPi * (t / breathPeriod + n));
    }
    out.height = s.shoreRise + s.hillHeight * k * hill * out.inland * s.heightScale * breath;
    // ADR-1992: the raised land. Each adds height * its profile; where the profile passes about a half, the land
    // channel is lifted to 1 (a ridge across the sea is a causeway of land, a rise an island).
    for (const TileRidge& r : s.ridges) {
        const float d = polylineNearest(r.points.data(), std::min(r.points.size(), kMaxTileRidgePoints), x, z).x;
        const float w = 1.0f - smooth01(0.0f, 0.5f * r.width, d);
        if (w <= 0.0f) continue;
        out.height += r.height * w;
        out.land = std::max(out.land, smooth01(0.35f, 0.65f, w));
    }
    for (const TileRise& r : s.rises) {
        const float d = glm::length(glm::vec2(x, z) - r.centre);
        const float w = 1.0f - smooth01(r.radius, r.radius + r.edge, d);
        if (w <= 0.0f) continue;
        out.height += r.height * w;
        out.land = std::max(out.land, smooth01(0.35f, 0.65f, w));
    }
    return out;
}

// The surface (tile top) a land height gives, after the shore's mix toward the sea floor.
float mixedTop(const TileTerrainSpec& s, float landHeight, float land) {
    return (s.seaLevel - s.seaDepth) + ((s.seaLevel + landHeight) - (s.seaLevel - s.seaDepth)) * land;
}

// ADR-1965: the static land's surface at (x, z) with the dry valleys and the rivers cut in but no ponds -- what the
// "auto" levels read. `upTo` limits the rivers applied (a river's source reads the land before it).
float staticSurface(const TileTerrainSpec& s, float x, float z, std::size_t riversUpTo, bool valleys);

float riverSourceLevel(const TileTerrainSpec& s, std::size_t i) {
    const TileRiver& r = s.rivers[i];
    if (r.bedDepth < 0.0f) {
        return s.seaLevel - s.seaDepth; // the original single river: at the sea floor's level
    }
    if (r.sourceLevel) {
        return *r.sourceLevel;
    }
    if (s.waterResolved && i < s.resolved.riverSource.size()) {
        return s.resolved.riverSource[i];
    }
    const glm::vec2 p = r.points.empty() ? glm::vec2(0.0f) : r.points.front();
    return staticSurface(s, p.x, p.y, i, true) - r.bedDepth;
}

float riverLength(const TileTerrainSpec& s, std::size_t i) {
    if (s.waterResolved && i < s.resolved.riverLength.size()) {
        return s.resolved.riverLength[i];
    }
    return polylineLength(s.rivers[i].points, kMaxTileRiverPoints);
}

float pondLevel(const TileTerrainSpec& s, std::size_t i) {
    const TilePond& p = s.ponds[i];
    if (p.level) {
        return *p.level;
    }
    if (s.waterResolved && i < s.resolved.pondLevel.size()) {
        return s.resolved.pondLevel[i];
    }
    // the rim: 32 points on the basin's outer edge, the lowest land there, less a metre
    float lowest = 1e9f;
    const float rim = p.radius + p.basinWidth;
    for (int k = 0; k < 32; ++k) {
        const float a = kTwoPi * static_cast<float>(k) / 32.0f;
        lowest = std::min(lowest, staticSurface(s, p.centre.x + rim * std::cos(a), p.centre.y + rim * std::sin(a),
                                                s.rivers.size(), true));
    }
    return lowest - 1.0f;
}

// A pond's effective shoreline radius at (x, z): `shoreNoise` wobbles it by up to half, by a value noise on the scale
// of the pond.
float pondShoreRadius(const TileTerrainSpec& s, std::size_t i, float x, float z) {
    const TilePond& p = s.ponds[i];
    if (p.shoreNoise <= 0.0f) {
        return p.radius;
    }
    const float scale = std::max(p.radius * 0.6f, 1.0f);
    const float n = tileValueNoise((s.seed ^ kPondSeed) + static_cast<std::uint32_t>(i) * 7919u, x / scale, z / scale);
    return p.radius * (1.0f + p.shoreNoise * 0.5f * (2.0f * n - 1.0f));
}

// ADR-1965: the dry valleys, then the rivers, then the ponds, on the land part. Writes the land height (relative to
// the sea level, the shore mix still to come) and the water channels.
struct Features {
    float landHeight = 0.0f;
    float river = 0.0f;
    float pond = 0.0f;
    float riverS = 0.0f;
};

Features applyFeatures(const TileTerrainSpec& s, float x, float z, float landHeight, std::size_t riversUpTo,
                       bool valleys, bool ponds) {
    Features f;
    f.landHeight = landHeight;
    if (valleys) {
        for (const TileValley& v : s.valleys) {
            const float d = polylineNearest(v.points.data(), v.points.size(), x, z).x;
            const float w = 1.0f - smooth01(0.0f, 0.5f * v.valleyWidth, d);
            if (w <= 0.0f) continue;
            const float relief = std::max(f.landHeight - s.shoreRise, 0.0f);
            const float depth = v.depthFraction > 0.0f ? v.depthFraction * relief : v.depth;
            f.landHeight = std::max(f.landHeight - depth * w, std::min(f.landHeight, s.shoreRise));
        }
    }
    float bestRiver = 1e9f;
    const std::size_t nr = std::min(riversUpTo, s.rivers.size());
    for (std::size_t i = 0; i < nr; ++i) {
        const TileRiver& r = s.rivers[i];
        const glm::vec2 near = polylineNearest(r.points.data(), std::min(r.points.size(), kMaxTileRiverPoints), x, z);
        const float d = near.x;
        if (d >= 0.5f * r.valleyWidth) continue;
        const float len = riverLength(s, i);
        const float u = len > 1e-3f ? std::clamp(near.y / len, 0.0f, 1.0f) : 0.0f;
        const float source = riverSourceLevel(s, i);
        const float mouth = s.seaLevel - s.seaDepth;
        const float ease = u * u * (3.0f - 2.0f * u);
        const float level = source + (mouth - source) * ease - s.seaLevel; // relative to the sea level, as landHeight
        const float v = 1.0f - smooth01(0.5f * r.width, 0.5f * r.valleyWidth, d);
        f.landHeight = f.landHeight + (level - f.landHeight) * v;
        const float channel = 1.0f - smooth01(0.5f * r.width - 0.5f, 0.5f * r.width + 0.5f, d);
        if (channel > f.river || (channel == f.river && d < bestRiver)) {
            f.river = channel;
            f.riverS = near.y;
            bestRiver = d;
        }
    }
    if (ponds) {
        for (std::size_t i = 0; i < s.ponds.size(); ++i) {
            const TilePond& p = s.ponds[i];
            const float d = glm::length(glm::vec2(x, z) - p.centre);
            if (d >= p.radius * 1.5f + p.basinWidth) continue;
            const float shore = pondShoreRadius(s, i, x, z);
            const float level = pondLevel(s, i) - s.seaLevel;
            const float v = 1.0f - smooth01(shore, shore + p.basinWidth, d);
            f.landHeight = f.landHeight + (level - f.landHeight) * v;
            f.pond = std::max(f.pond, 1.0f - smooth01(shore - 0.5f, shore + 0.5f, d));
        }
    }
    return f;
}

float staticSurface(const TileTerrainSpec& s, float x, float z, std::size_t riversUpTo, bool valleys) {
    const LandPart l = landPart(s, x, z, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
    const Features f = applyFeatures(s, x, z, l.height, riversUpTo, valleys, false);
    return mixedTop(s, f.landHeight, l.land);
}

// The one body of the base: `t`, `rollX`, `rollZ`, `breath` all at their rest values (0, 0, 0, 0) give the static
// ground bit for bit -- x - 0 * t is x, and a relief multiplied by 1 + 0 * s is the relief.
TileSample baseSample(const TileTerrainSpec& s, float x, float z, float t, float rollX, float rollZ, float breathAmp,
                      float breathPeriod, float breathWavelength) {
    const LandPart l = landPart(s, x, z, t, rollX, rollZ, breathAmp, breathPeriod, breathWavelength);
    const Features f = applyFeatures(s, x, z, l.height, s.rivers.size(), true, true);
    const float land = l.land;
    TileSample out;
    out.height = mixedTop(s, f.landHeight, land);
    const float inland = std::max(f.river, f.pond);
    out.water = 1.0f - land + inland * land;
    out.river = f.river * land;
    out.pond = f.pond * land * (1.0f - f.river);
    out.riverS = f.riverS;
    out.shore = 1.0f - smooth01(0.0f, 3.0f * s.shoreBand, std::abs(l.c - s.landThreshold));
    out.flat = smooth01(s.crownThreshold, s.crownThreshold + 0.06f, l.shaped) * s.crownFlatten * l.inland * land;
    if (!s.path.empty()) {
        const float d = tilePathDistance(s, x, z);
        const float p = (1.0f - smooth01(0.5f * s.pathWidth, 0.5f * s.pathWidth + 0.75f, d)) * land;
        out.height += s.pathLift * p;
        out.path = p;
    }
    return out;
}

float waveAliasFade(float wavelength, float cellSize) {
    return smooth01(2.0f * cellSize, 4.0f * cellSize, wavelength);
}

} // namespace

float tileRiverSourceLevel(const TileTerrainSpec& spec, std::size_t i) {
    return riverSourceLevel(spec, i);
}

float tileRiverMouthLevel(const TileTerrainSpec& spec) {
    return spec.seaLevel - spec.seaDepth;
}

float tileRiverLength(const TileTerrainSpec& spec, std::size_t i) {
    return riverLength(spec, i);
}

float tilePondLevel(const TileTerrainSpec& spec, std::size_t i) {
    return pondLevel(spec, i);
}

TileSample tileSample(const TileTerrainSpec& s, float x, float z) {
    return baseSample(s, x, z, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
}

TileSample tileSample(const TileTerrainSpec& s, float x, float z, float t, const TileDynamics& dyn) {
    return baseSample(s, x, z, t, dyn.rolling.x, dyn.rolling.y, dyn.breathingAmplitude, dyn.breathingPeriod,
                      dyn.breathingWavelength);
}

bool TileDynamics::active() const {
    if (breathingAmplitude != 0.0f || rolling.x != 0.0f || rolling.y != 0.0f) {
        return true;
    }
    for (const TileWave& w : waves) {
        if (w.amplitude != 0.0f || w.crest != 0.0f) {
            return true;
        }
    }
    if (!springs.empty() && spring.depth != 0.0f) {
        return true;
    }
    if (riverDriftAmplitude != 0.0f) {
        return true;
    }
    return (rings.amplitude != 0.0f || rings.crest != 0.0f) && (!events.empty() || !live.empty());
}

float TileDynamics::springSeconds() const {
    // e^(-zeta w a) = 1e-3, plus the time the ripple takes to cross three ripple radii
    const float w = kTwoPi * std::max(spring.frequency, 1e-3f);
    const float decay = 6.9078f / std::max(spring.damping * w, 1e-3f);
    return decay + 3.0f * spring.rippleRadius / std::max(spring.rippleSpeed, 1e-3f);
}

std::vector<TileEvent> TileDynamics::springsAt(float t) const {
    std::vector<TileEvent> out;
    const float horizon = springSeconds();
    for (const TileEvent& e : springs) {
        const float age = t - e.time;
        if (age >= 0.0f && age <= horizon) {
            out.push_back(e);
        }
    }
    if (out.size() > kMaxTileSprings) { // only then is an order needed: the newest are kept
        std::stable_sort(out.begin(), out.end(), [](const TileEvent& a, const TileEvent& b) { return a.time > b.time; });
        out.resize(kMaxTileSprings);
    }
    return out;
}

float tileSpring(const TileSpringSpec& s, const std::vector<TileEvent>& springs, float x, float z, float t) {
    float h = 0.0f;
    const float w = kTwoPi * s.frequency;
    const float zeta = std::clamp(s.damping, 0.0f, 0.999f);
    const float wd = w * std::sqrt(1.0f - zeta * zeta);
    for (const TileEvent& e : springs) {
        const float r = std::sqrt((x - e.position.x) * (x - e.position.x) + (z - e.position.y) * (z - e.position.y));
        const float reach = r / std::max(s.rippleRadius, 1e-3f);
        if (reach >= kTileSpringReach) {
            continue;
        }
        const float a = t - e.time - r / s.rippleSpeed;
        if (a < 0.0f) {
            continue;
        }
        const float k = std::clamp((reach - kTileSpringReachStart) / (kTileSpringReach - kTileSpringReachStart), 0.0f, 1.0f);
        const float window = 1.0f - k * k * (3.0f - 2.0f * k); // CP-D r2: the reach eases out, no edge
        const float amp = s.depth * e.strength * std::exp(-r / s.rippleRadius) * window;
        h -= amp * std::exp(-zeta * w * a) * std::sin(wd * a); // an impulse: starts at rest, first goes down
    }
    return h;
}

float tileLanding(const TileSpringSpec& s, const std::vector<TileEvent>& springs, float x, float z, float t) {
    float l = 0.0f;
    const float w = kTwoPi * s.frequency;
    const float zeta = std::clamp(s.damping, 0.0f, 0.999f);
    for (const TileEvent& e : springs) {
        const float r = std::sqrt((x - e.position.x) * (x - e.position.x) + (z - e.position.y) * (z - e.position.y));
        const float glowR = std::clamp(s.glowRadius, 1e-3f, s.rippleRadius); // within the spring's reach
        const float reach = r / glowR;
        if (reach >= kTileSpringReach) {
            continue;
        }
        const float a = t - e.time - r / s.rippleSpeed;
        if (a < 0.0f) {
            continue;
        }
        const float k = std::clamp((reach - kTileSpringReachStart) / (kTileSpringReach - kTileSpringReachStart), 0.0f, 1.0f);
        const float window = 1.0f - k * k * (3.0f - 2.0f * k);
        l += e.strength * std::exp(-r / glowR) * window * std::exp(-zeta * w * a) * (1.0f - std::exp(-a / 0.06f));
    }
    return std::clamp(l, 0.0f, 1.0f);
}

std::vector<TileEvent> TileDynamics::eventsAt(float t) const {
    std::vector<TileEvent> out;
    const float horizon = 6.0f * rings.decay;
    for (const auto* list : {&events, &live}) {
        for (const TileEvent& e : *list) {
            const float age = t - e.time;
            if (age >= 0.0f && age <= horizon) {
                out.push_back(e);
            }
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const TileEvent& a, const TileEvent& b) { return a.time > b.time; });
    if (out.size() > kMaxTileEvents) {
        out.resize(kMaxTileEvents);
    }
    return out;
}

glm::vec2 tileMotion(const TileDynamics& dyn, const std::vector<TileEvent>& events, float x, float z, float t,
                     const TileSample& tile, float cellSize) {
    float height = 0.0f;
    float crest = 0.0f;
    // ADR-1965: a wave's water weight by body -- the sea's 1, a river's and a pond's their own
    const float water = tile.water;
    const float sea = std::max(water - tile.river - tile.pond, 0.0f);
    const float waveWater = sea + tile.river * dyn.riverWaves + tile.pond * dyn.pondWaves;
    const std::size_t waveCount = std::min(dyn.waves.size(), kMaxTileWaves);
    for (std::size_t i = 0; i < waveCount; ++i) {
        const TileWave& w = dyn.waves[i];
        float d = 0.0f;
        if (w.circular) {
            d = std::sqrt((x - w.centre.x) * (x - w.centre.x) + (z - w.centre.y) * (z - w.centre.y));
        } else {
            const float a = w.direction * (kTwoPi / 360.0f);
            d = x * std::cos(a) + z * std::sin(a);
        }
        const float theta = kTwoPi * ((d - w.speed * t) / w.wavelength + w.phase);
        const float u = 0.5f + 0.5f * std::sin(theta);
        const float c = std::pow(u, 1.0f + 7.0f * w.sharpness);
        const float mask = (w.onLand * (1.0f - water) + w.onWater * waveWater) * waveAliasFade(w.wavelength, cellSize);
        height += w.amplitude * (2.0f * c - 1.0f) * mask;
        crest += w.crest * c * mask;
    }
    const TileRingSpec& r = dyn.rings;
    const float ringFade = waveAliasFade(std::min(r.wavelength, r.width), cellSize);
    for (const TileEvent& e : events) {
        const float age = t - e.time;
        if (age < 0.0f || age > 6.0f * r.decay) {
            continue;
        }
        const float dist = std::sqrt((x - e.position.x) * (x - e.position.x) + (z - e.position.y) * (z - e.position.y));
        const float off = dist - r.speed * age;
        const float g = off / r.width;
        const float env = std::exp(-age / r.decay) * std::exp(-g * g) * e.strength * ringFade;
        height += r.amplitude * env * std::cos(kTwoPi * off / r.wavelength);
        crest += r.crest * env;
    }
    // ADR-1965: a slow ripple drifting downstream along a river (no crest)
    if (dyn.riverDriftAmplitude != 0.0f && tile.river > 0.0f) {
        const float fade = waveAliasFade(dyn.riverDriftWavelength, cellSize);
        height += dyn.riverDriftAmplitude * tile.river * fade *
                  std::sin(kTwoPi * (tile.riverS - dyn.riverDriftSpeed * t) / std::max(dyn.riverDriftWavelength, 0.5f));
    }
    return {dyn.amplitude * height, std::clamp(crest, 0.0f, 1.0f)};
}

TileSample tileSampleMoving(const TileTerrainSpec& spec, const TileDynamics& dyn, float x, float z, float t,
                            float cellSize) {
    TileSample out = tileSample(spec, x, z, t, dyn);
    const glm::vec2 m = tileMotion(dyn, dyn.eventsAt(t), x, z, t, out, cellSize);
    const std::vector<TileEvent> springs = dyn.springsAt(t);
    out.height += m.x + tileSpring(dyn.spring, springs, x, z, t); // ADR-1954
    out.crest = m.y;
    out.landing = tileLanding(dyn.spring, springs, x, z, t); // ADR-1960
    return out;
}

glm::ivec2 tileIndexAt(float pitch, float x, float z) {
    return {static_cast<std::int32_t>(std::floor(x / pitch)), static_cast<std::int32_t>(std::floor(z / pitch))};
}

glm::vec2 tileCentre(float pitch, glm::ivec2 index) {
    return {(static_cast<float>(index.x) + 0.5f) * pitch, (static_cast<float>(index.y) + 0.5f) * pitch};
}

float tileHeightAt(const TileTerrainSpec& spec, float pitch, float x, float z) {
    const glm::vec2 c = tileCentre(pitch, tileIndexAt(pitch, x, z));
    return tileSample(spec, c.x, c.y).height;
}

float tileHeightAt(const TileTerrainSpec& spec, const TileDynamics& dyn, float pitch, float x, float z, float t) {
    const glm::vec2 c = tileCentre(pitch, tileIndexAt(pitch, x, z));
    return tileSampleMoving(spec, dyn, c.x, c.y, t, pitch).height;
}

Result<void> TileTerrainSpec::validate() const {
    const auto positive = [](float v) { return std::isfinite(v) && v > 0.0f; };
    if (!positive(continentWavelength) || !positive(hillWavelength) || !positive(warpWavelength) ||
        !positive(regionWavelength)) {
        return fail("tiles: every wavelength must be a positive number");
    }
    if (hillOctaves < 1 || hillOctaves > 6) {
        return fail("tiles: hillOctaves must be 1..6 (got {})", hillOctaves);
    }
    if (!(hillHigh > hillLow)) {
        return fail("tiles: hillHigh must be above hillLow");
    }
    if (!(shoreBand > 0.0f) || !(inlandBand > 0.0f)) {
        return fail("tiles: shoreBand and inlandBand must be positive");
    }
    if (regionVariation < 0.0f || regionVariation > 0.9f) {
        return fail("tiles: regionVariation must be 0..0.9");
    }
    if (path.size() > kMaxTilePathPoints) {
        return fail("tiles: the path has at most {} points", kMaxTilePathPoints);
    }
    if (!(heightScale >= 0.0f) || heightScale > 8.0f) {
        return fail("tiles: heightScale must be 0..8");
    }
    // ADR-1965
    // ADR-1992
    if (ridges.size() > kMaxTileRidges || rises.size() > kMaxTileRises) {
        return fail("tiles: at most {} ridges and {} rises", kMaxTileRidges, kMaxTileRises);
    }
    std::size_t ridgePool = 0;
    for (const TileRidge& r : ridges) {
        if (r.points.size() < 2 || r.points.size() > kMaxTileRidgePoints || !(r.width > 0.0f) ||
            !std::isfinite(r.height) || !std::isfinite(r.width)) {
            return fail("tiles: ridge '{}': 2..{} points, a positive width and a finite height", r.name,
                        kMaxTileRidgePoints);
        }
        ridgePool += r.points.size();
    }
    if (ridgePool > kMaxTileRidgePool) {
        return fail("tiles: the ridges have at most {} points together (got {})", kMaxTileRidgePool, ridgePool);
    }
    for (const TileRise& r : rises) {
        if (!(r.radius >= 0.0f) || !(r.edge > 0.0f) || !std::isfinite(r.height) || !std::isfinite(r.centre.x) ||
            !std::isfinite(r.centre.y) || !std::isfinite(r.radius) || !std::isfinite(r.edge)) {
            return fail("tiles: rise '{}': radius >= 0, a positive edge, finite centre and height", r.name);
        }
    }
    if (rivers.size() > kMaxTileRivers || ponds.size() > kMaxTilePonds || valleys.size() > kMaxTileValleys) {
        return fail("tiles: at most {} rivers, {} ponds and {} valleys", kMaxTileRivers, kMaxTilePonds, kMaxTileValleys);
    }
    std::size_t riverPool = 0;
    for (const TileRiver& r : rivers) {
        if (r.points.size() < 2 || r.points.size() > kMaxTileRiverPoints) {
            return fail("tiles: a river has 2..{} points", kMaxTileRiverPoints);
        }
        if (!(r.width > 0.0f) || !(r.valleyWidth > r.width) || !std::isfinite(r.bedDepth) ||
            (r.sourceLevel && !std::isfinite(*r.sourceLevel))) {
            return fail("tiles: river '{}': width must be positive, valleyWidth above it, the levels finite", r.name);
        }
        riverPool += r.points.size();
    }
    if (riverPool > kMaxTileRiverPool) {
        return fail("tiles: the rivers have at most {} points together (got {})", kMaxTileRiverPool, riverPool);
    }
    for (const TilePond& p : ponds) {
        if (!(p.radius > 0.0f) || !(p.basinWidth > 0.0f) || p.shoreNoise < 0.0f || p.shoreNoise > 1.0f ||
            (p.level && !std::isfinite(*p.level)) || !std::isfinite(p.centre.x) || !std::isfinite(p.centre.y)) {
            return fail("tiles: pond '{}': radius and basinWidth positive, shoreNoise 0..1, level finite", p.name);
        }
    }
    std::size_t valleyPool = 0;
    for (const TileValley& v : valleys) {
        if (v.points.size() < 2 || !(v.valleyWidth > 0.0f) || !(v.depth >= 0.0f) || v.depthFraction < 0.0f ||
            v.depthFraction > 1.0f) {
            return fail("tiles: valley '{}': 2+ points, valleyWidth positive, depth >= 0, depthFraction 0..1", v.name);
        }
        valleyPool += v.points.size();
    }
    if (valleyPool > kMaxTileValleyPool) {
        return fail("tiles: the valleys have at most {} points together (got {})", kMaxTileValleyPool, valleyPool);
    }
    if (!(pathWidth > 0.0f)) {
        return fail("tiles: pathWidth must be positive");
    }
    for (const float v : {landThreshold, shoreBand, inlandBand, seaLevel, seaDepth, shoreRise, hillHeight, hillWarp,
                          hillLow, hillHigh, crownThreshold, crownFlatten, pathLift}) {
        if (!std::isfinite(v)) {
            return fail("tiles: a value is not finite");
        }
    }
    return {};
}

std::uint64_t TileTerrainSpec::hash() const {
    std::uint64_t h = 1469598103934665603ull;
    const auto mix = [&h](std::uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    const auto f = [&](float v) { mix(std::bit_cast<std::uint32_t>(v)); };
    mix(seed);
    for (const float v : {continentWavelength, landThreshold, shoreBand, inlandBand, seaLevel, seaDepth, shoreRise,
                          hillWavelength, hillHeight, hillWarp, warpWavelength, hillLow, hillHigh, crownThreshold,
                          crownFlatten, regionWavelength, regionVariation, pathWidth, pathLift, heightScale}) {
        f(v);
    }
    // ADR-1965: the rivers, ponds and valleys (their resolved levels follow from these)
    for (const TileRiver& r : rivers) {
        mix(0x52495645u); // "RIVE"
        for (const glm::vec2& p : r.points) {
            f(p.x);
            f(p.y);
        }
        for (const float v : {r.width, r.valleyWidth, r.bedDepth, r.sourceLevel.value_or(-12345.0f)}) f(v);
    }
    for (const TilePond& p : ponds) {
        mix(0x504F4E44u); // "POND"
        for (const float v : {p.centre.x, p.centre.y, p.radius, p.basinWidth, p.level.value_or(-12345.0f),
                              p.shoreNoise}) {
            f(v);
        }
    }
    for (const TileValley& v : valleys) {
        mix(0x56414C4Cu); // "VALL"
        for (const glm::vec2& p : v.points) {
            f(p.x);
            f(p.y);
        }
        for (const float w : {v.valleyWidth, v.depth, v.depthFraction}) f(w);
    }
    for (const TileRidge& r : ridges) { // ADR-1992
        mix(0x52494447u); // "RIDG"
        for (const glm::vec2& p : r.points) {
            f(p.x);
            f(p.y);
        }
        f(r.height);
        f(r.width);
    }
    for (const TileRise& r : rises) {
        mix(0x52495345u); // "RISE"
        for (const float v : {r.centre.x, r.centre.y, r.radius, r.height, r.edge}) f(v);
    }
    mix(static_cast<std::uint64_t>(hillOctaves));
    for (const glm::vec2& p : path) {
        f(p.x);
        f(p.y);
    }
    return h;
}

namespace {

struct FloatKey {
    const char* key;
    float TileTerrainSpec::* field;
};
constexpr FloatKey kFloatKeys[] = {
    {"continentWavelength", &TileTerrainSpec::continentWavelength},
    {"landThreshold", &TileTerrainSpec::landThreshold},
    {"shoreBand", &TileTerrainSpec::shoreBand},
    {"inlandBand", &TileTerrainSpec::inlandBand},
    {"seaLevel", &TileTerrainSpec::seaLevel},
    {"seaDepth", &TileTerrainSpec::seaDepth},
    {"shoreRise", &TileTerrainSpec::shoreRise},
    {"hillWavelength", &TileTerrainSpec::hillWavelength},
    {"hillHeight", &TileTerrainSpec::hillHeight},
    {"hillWarp", &TileTerrainSpec::hillWarp},
    {"warpWavelength", &TileTerrainSpec::warpWavelength},
    {"hillLow", &TileTerrainSpec::hillLow},
    {"hillHigh", &TileTerrainSpec::hillHigh},
    {"crownThreshold", &TileTerrainSpec::crownThreshold},
    {"crownFlatten", &TileTerrainSpec::crownFlatten},
    {"regionWavelength", &TileTerrainSpec::regionWavelength},
    {"regionVariation", &TileTerrainSpec::regionVariation},
    {"pathWidth", &TileTerrainSpec::pathWidth},
    {"pathLift", &TileTerrainSpec::pathLift},
    {"heightScale", &TileTerrainSpec::heightScale},
};

} // namespace

nlohmann::json tileTerrainToJson(const TileTerrainSpec& spec) {
    nlohmann::json j = nlohmann::json::object();
    j["seed"] = spec.seed;
    j["hillOctaves"] = spec.hillOctaves;
    for (const FloatKey& k : kFloatKeys) {
        j[k.key] = spec.*(k.field);
    }
    nlohmann::json path = nlohmann::json::array();
    for (const glm::vec2& p : spec.path) {
        path.push_back(nlohmann::json::array({p.x, p.y}));
    }
    j["path"] = std::move(path);
    const auto line = [](const std::vector<glm::vec2>& pts) {
        nlohmann::json a = nlohmann::json::array();
        for (const glm::vec2& p : pts) a.push_back(nlohmann::json::array({p.x, p.y}));
        return a;
    };
    if (!spec.rivers.empty()) {
        nlohmann::json rivers = nlohmann::json::array();
        for (const TileRiver& r : spec.rivers) {
            nlohmann::json o = {{"points", line(r.points)}, {"width", r.width}, {"valleyWidth", r.valleyWidth},
                                {"bedDepth", r.bedDepth}};
            if (!r.name.empty()) o["name"] = r.name;
            o["sourceLevel"] = r.sourceLevel ? nlohmann::json(*r.sourceLevel) : nlohmann::json("auto");
            rivers.push_back(std::move(o));
        }
        j["rivers"] = std::move(rivers);
    }
    if (!spec.ponds.empty()) {
        nlohmann::json ponds = nlohmann::json::array();
        for (const TilePond& p : spec.ponds) {
            nlohmann::json o = {{"centre", nlohmann::json::array({p.centre.x, p.centre.y})},
                                {"radius", p.radius},
                                {"basinWidth", p.basinWidth},
                                {"shoreNoise", p.shoreNoise}};
            if (!p.name.empty()) o["name"] = p.name;
            o["level"] = p.level ? nlohmann::json(*p.level) : nlohmann::json("auto");
            ponds.push_back(std::move(o));
        }
        j["ponds"] = std::move(ponds);
    }
    if (!spec.valleys.empty()) {
        nlohmann::json valleys = nlohmann::json::array();
        for (const TileValley& v : spec.valleys) {
            nlohmann::json o = {{"points", line(v.points)}, {"valleyWidth", v.valleyWidth}};
            if (v.depthFraction > 0.0f) o["depthFraction"] = v.depthFraction; else o["depth"] = v.depth;
            if (!v.name.empty()) o["name"] = v.name;
            valleys.push_back(std::move(o));
        }
        j["valleys"] = std::move(valleys);
    }
    if (!spec.ridges.empty()) { // ADR-1992
        nlohmann::json ridges = nlohmann::json::array();
        for (const TileRidge& r : spec.ridges) {
            nlohmann::json o = {{"points", line(r.points)}, {"height", r.height}, {"width", r.width}};
            if (!r.name.empty()) o["name"] = r.name;
            ridges.push_back(std::move(o));
        }
        j["ridges"] = std::move(ridges);
    }
    if (!spec.rises.empty()) {
        nlohmann::json rises = nlohmann::json::array();
        for (const TileRise& r : spec.rises) {
            nlohmann::json o = {{"centre", nlohmann::json::array({r.centre.x, r.centre.y})},
                                {"radius", r.radius},
                                {"height", r.height},
                                {"edge", r.edge}};
            if (!r.name.empty()) o["name"] = r.name;
            rises.push_back(std::move(o));
        }
        j["rises"] = std::move(rises);
    }
    return j;
}

namespace {

Result<std::vector<glm::vec2>> readLine(const nlohmann::json& value, const std::string& where) {
    if (!value.is_array()) {
        return fail("'{}' must be an array of [x, z]", where);
    }
    std::vector<glm::vec2> line;
    for (const auto& p : value) {
        if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number()) {
            return fail("'{}' must be an array of [x, z]", where);
        }
        line.emplace_back(p[0].get<float>(), p[1].get<float>());
    }
    return line;
}

// A number, or "auto" (nullopt).
Result<std::optional<float>> readLevel(const nlohmann::json& value, const std::string& where) {
    if (value.is_string() && value.get<std::string>() == "auto") {
        return std::optional<float>{};
    }
    if (!value.is_number()) {
        return fail("'{}' must be a number of metres or \"auto\"", where);
    }
    return std::optional<float>(value.get<float>());
}

Result<float> readNum(const nlohmann::json& value, const std::string& where) {
    if (!value.is_number()) {
        return fail("'{}' must be a number", where);
    }
    return value.get<float>();
}

Result<TileRiver> readRiver(const nlohmann::json& o, const std::string& where) {
    if (!o.is_object()) return fail("'{}' must be an object", where);
    TileRiver r;
    for (const auto& [k, v] : o.items()) {
        const std::string at = where + "." + k;
        if (k == "name" && v.is_string()) {
            r.name = v.get<std::string>();
        } else if (k == "points") {
            auto l = readLine(v, at);
            if (!l) return std::unexpected(l.error());
            r.points = std::move(*l);
        } else if (k == "width" || k == "valleyWidth" || k == "bedDepth") {
            auto n = readNum(v, at);
            if (!n) return std::unexpected(n.error());
            (k == "width" ? r.width : k == "valleyWidth" ? r.valleyWidth : r.bedDepth) = *n;
        } else if (k == "sourceLevel") {
            auto l = readLevel(v, at);
            if (!l) return std::unexpected(l.error());
            r.sourceLevel = *l;
        } else {
            return fail("'{}' is not a key this build reads", at);
        }
    }
    return r;
}

Result<TilePond> readPond(const nlohmann::json& o, const std::string& where) {
    if (!o.is_object()) return fail("'{}' must be an object", where);
    TilePond p;
    for (const auto& [k, v] : o.items()) {
        const std::string at = where + "." + k;
        if (k == "name" && v.is_string()) {
            p.name = v.get<std::string>();
        } else if (k == "centre") {
            if (!v.is_array() || v.size() != 2 || !v[0].is_number() || !v[1].is_number()) {
                return fail("'{}' must be [x, z]", at);
            }
            p.centre = {v[0].get<float>(), v[1].get<float>()};
        } else if (k == "radius" || k == "basinWidth" || k == "shoreNoise") {
            auto n = readNum(v, at);
            if (!n) return std::unexpected(n.error());
            (k == "radius" ? p.radius : k == "basinWidth" ? p.basinWidth : p.shoreNoise) = *n;
        } else if (k == "level") {
            auto l = readLevel(v, at);
            if (!l) return std::unexpected(l.error());
            p.level = *l;
        } else {
            return fail("'{}' is not a key this build reads", at);
        }
    }
    return p;
}

Result<TileRidge> readRidge(const nlohmann::json& o, const std::string& where) { // ADR-1992
    if (!o.is_object()) return fail("'{}' must be an object", where);
    TileRidge r;
    for (const auto& [k, val] : o.items()) {
        const std::string at = where + "." + k;
        if (k == "name" && val.is_string()) {
            r.name = val.get<std::string>();
        } else if (k == "points") {
            auto l = readLine(val, at);
            if (!l) return std::unexpected(l.error());
            r.points = std::move(*l);
        } else if (k == "height" || k == "width") {
            auto n = readNum(val, at);
            if (!n) return std::unexpected(n.error());
            (k == "height" ? r.height : r.width) = *n;
        } else {
            return fail("'{}' is not a key this build reads", at);
        }
    }
    return r;
}

Result<TileRise> readRise(const nlohmann::json& o, const std::string& where) { // ADR-1992
    if (!o.is_object()) return fail("'{}' must be an object", where);
    TileRise r;
    for (const auto& [k, val] : o.items()) {
        const std::string at = where + "." + k;
        if (k == "name" && val.is_string()) {
            r.name = val.get<std::string>();
        } else if (k == "centre") {
            if (!val.is_array() || val.size() != 2 || !val[0].is_number() || !val[1].is_number()) {
                return fail("'{}' must be [x, z]", at);
            }
            r.centre = {val[0].get<float>(), val[1].get<float>()};
        } else if (k == "radius" || k == "height" || k == "edge") {
            auto n = readNum(val, at);
            if (!n) return std::unexpected(n.error());
            (k == "radius" ? r.radius : k == "height" ? r.height : r.edge) = *n;
        } else {
            return fail("'{}' is not a key this build reads", at);
        }
    }
    return r;
}

Result<TileValley> readValley(const nlohmann::json& o, const std::string& where) {
    if (!o.is_object()) return fail("'{}' must be an object", where);
    TileValley v;
    for (const auto& [k, val] : o.items()) {
        const std::string at = where + "." + k;
        if (k == "name" && val.is_string()) {
            v.name = val.get<std::string>();
        } else if (k == "points") {
            auto l = readLine(val, at);
            if (!l) return std::unexpected(l.error());
            v.points = std::move(*l);
        } else if (k == "valleyWidth" || k == "depth" || k == "depthFraction") {
            auto n = readNum(val, at);
            if (!n) return std::unexpected(n.error());
            (k == "valleyWidth" ? v.valleyWidth : k == "depth" ? v.depth : v.depthFraction) = *n;
        } else {
            return fail("'{}' is not a key this build reads", at);
        }
    }
    return v;
}

} // namespace

Result<TileTerrainSpec> tileTerrainFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("'tiles' must be an object");
    }
    TileTerrainSpec spec;
    for (const auto& [key, value] : j.items()) {
        if (key == "ridges" || key == "rises") { // ADR-1992
            if (!value.is_array()) return fail("'tiles.{}' must be an array", key);
            for (std::size_t i = 0; i < value.size(); ++i) {
                const std::string where = fmt::format("tiles.{}[{}]", key, i);
                if (key == "ridges") {
                    auto r = readRidge(value[i], where);
                    if (!r) return std::unexpected(r.error());
                    spec.ridges.push_back(std::move(*r));
                } else {
                    auto r = readRise(value[i], where);
                    if (!r) return std::unexpected(r.error());
                    spec.rises.push_back(std::move(*r));
                }
            }
            continue;
        }
        if (key == "rivers" || key == "ponds" || key == "valleys") {
            if (!value.is_array()) return fail("'tiles.{}' must be an array", key);
            for (std::size_t i = 0; i < value.size(); ++i) {
                const std::string where = fmt::format("tiles.{}[{}]", key, i);
                if (key == "rivers") {
                    auto r = readRiver(value[i], where);
                    if (!r) return std::unexpected(r.error());
                    spec.rivers.push_back(std::move(*r));
                } else if (key == "ponds") {
                    auto p = readPond(value[i], where);
                    if (!p) return std::unexpected(p.error());
                    spec.ponds.push_back(std::move(*p));
                } else {
                    auto v = readValley(value[i], where);
                    if (!v) return std::unexpected(v.error());
                    spec.valleys.push_back(std::move(*v));
                }
            }
            continue;
        }
        if (key == "seed") {
            if (!value.is_number_unsigned() && !(value.is_number_integer() && value.get<std::int64_t>() >= 0)) {
                return fail("'tiles.seed' must be a non-negative integer");
            }
            spec.seed = value.get<std::uint32_t>();
            continue;
        }
        if (key == "hillOctaves") {
            if (!value.is_number_integer()) {
                return fail("'tiles.hillOctaves' must be an integer");
            }
            spec.hillOctaves = value.get<int>();
            continue;
        }
        if (key == "path") {
            auto l = readLine(value, "tiles.path");
            if (!l) return std::unexpected(l.error());
            spec.path = std::move(*l);
            continue;
        }
        bool found = false;
        for (const FloatKey& k : kFloatKeys) {
            if (key == k.key) {
                if (!value.is_number()) {
                    return fail("'tiles.{}' must be a number", key);
                }
                spec.*(k.field) = value.get<float>();
                found = true;
                break;
            }
        }
        if (!found) {
            return fail("'tiles.{}' is not a key this build reads", key);
        }
    }
    if (auto r = spec.validate(); !r) {
        return std::unexpected(r.error());
    }
    spec.resolveWater();
    return spec;
}

void TileTerrainSpec::resolveWater() {
    // in order: each river's source reads the land with the rivers before it (already resolved, so read from here); the
    // ponds read every river. While filling, an index not yet filled is computed.
    waterResolved = true;
    resolved.riverSource.clear();
    resolved.riverLength.clear();
    resolved.pondLevel.clear();
    for (std::size_t i = 0; i < rivers.size(); ++i) {
        resolved.riverLength.push_back(polylineLength(rivers[i].points, kMaxTileRiverPoints));
    }
    for (std::size_t i = 0; i < rivers.size(); ++i) {
        resolved.riverSource.push_back(riverSourceLevel(*this, i));
    }
    for (std::size_t i = 0; i < ponds.size(); ++i) {
        resolved.pondLevel.push_back(pondLevel(*this, i));
    }
}

int TileTerrainSpec::riverIndex(const std::string& name) const {
    for (std::size_t i = 0; i < rivers.size(); ++i) {
        if (!rivers[i].name.empty() && rivers[i].name == name) return static_cast<int>(i);
    }
    if (name == "river") return rivers.empty() ? -1 : 0;
    std::string digits = name;
    if (digits.rfind("river", 0) == 0) digits = digits.substr(5);
    if (!digits.empty() && digits.size() <= 3 &&
        std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        const int i = std::stoi(digits);
        return i < static_cast<int>(rivers.size()) ? i : -1;
    }
    return -1;
}

} // namespace avgen::scene

// ---- ADR-1953: the moving ground's validation and JSON ----

namespace avgen::scene {

Result<void> validateTileDynamics(const TileDynamics& d) {
    const auto finite = [](float v) { return std::isfinite(v); };
    if (d.waves.size() > kMaxTileWaves) {
        return fail("tileDynamics: at most {} waves (got {})", kMaxTileWaves, d.waves.size());
    }
    if (d.events.size() > kMaxTileEvents) {
        return fail("tileDynamics: at most {} authored events (got {})", kMaxTileEvents, d.events.size());
    }
    for (const TileWave& w : d.waves) {
        if (!(w.wavelength > 0.0f) || !finite(w.wavelength) || !finite(w.amplitude) || !finite(w.speed) ||
            !finite(w.direction) || !finite(w.phase) || !finite(w.crest) || !finite(w.onLand) || !finite(w.onWater) ||
            !finite(w.centre.x) || !finite(w.centre.y)) {
            return fail("tileDynamics: a wave needs a positive wavelength and finite values");
        }
        if (w.sharpness < 0.0f || w.sharpness > 1.0f) {
            return fail("tileDynamics: a wave's sharpness is 0..1");
        }
    }
    if (!(d.breathingPeriod > 0.0f) || !(d.breathingWavelength > 0.0f) || !finite(d.breathingAmplitude) ||
        std::abs(d.breathingAmplitude) > 4.0f) {
        return fail("tileDynamics: breathing needs a positive period and wavelength and an amplitude within 4");
    }
    {
        const TileSpringSpec& s = d.spring;
        if (!finite(s.depth) || !(s.frequency > 0.0f) || !(s.damping > 0.0f) || !(s.damping < 1.0f) ||
            !(s.rippleSpeed > 0.0f) || !(s.rippleRadius > 0.0f)) {
            return fail("tileDynamics: a spring needs a finite depth, a positive frequency, damping in (0, 1) and a "
                        "positive ripple speed and radius");
        }
    }
    if (!(d.contourInterval >= 0.0f) || !finite(d.contourInterval) || !finite(d.contourOffset)) {
        return fail("tileDynamics: the contour interval is >= 0 and the offset finite");
    }
    if (!finite(d.riverWaves) || !finite(d.pondWaves) || !finite(d.riverDriftAmplitude) ||
        !(d.riverDriftWavelength > 0.0f) || !finite(d.riverDriftWavelength) || !finite(d.riverDriftSpeed)) {
        return fail("tileDynamics: water's wave weights, drift amplitude and speed must be finite, its drift wavelength "
                    "positive"); // ADR-1965
    }
    if (!finite(d.rolling.x) || !finite(d.rolling.y) || !finite(d.amplitude)) {
        return fail("tileDynamics: rolling and amplitude must be finite");
    }
    if (!(d.rings.speed >= 0.0f) || !(d.rings.wavelength > 0.0f) || !(d.rings.width > 0.0f) || !(d.rings.decay > 0.0f) ||
        !finite(d.rings.amplitude) || !finite(d.rings.crest) || !finite(d.rings.speed)) {
        return fail("tileDynamics: rings need a speed >= 0 and a positive wavelength, width and decay");
    }
    for (const TileEvent& e : d.events) {
        if (!finite(e.time) || !finite(e.position.x) || !finite(e.position.y) || !finite(e.strength)) {
            return fail("tileDynamics: an event's time, position and strength must be finite");
        }
    }
    return {};
}

namespace {

Result<float> readNumber(const nlohmann::json& j, const std::string& where) {
    if (!j.is_number()) {
        return fail("'{}' must be a number", where);
    }
    return j.get<float>();
}

Result<glm::vec2> readPair(const nlohmann::json& j, const std::string& where) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number()) {
        return fail("'{}' must be [x, z]", where);
    }
    return glm::vec2(j[0].get<float>(), j[1].get<float>());
}

// Reads the numbers of `obj` named in `keys` into the given fields; refuses any other key except `extra` ones.
template <typename T>
Result<void> readFields(const nlohmann::json& obj, const std::string& where,
                        std::initializer_list<std::pair<const char*, float T::*>> keys, T& out,
                        std::initializer_list<const char*> extra = {}) {
    if (!obj.is_object()) {
        return fail("'{}' must be an object", where);
    }
    for (const auto& [key, value] : obj.items()) {
        bool found = false;
        for (const auto& [name, field] : keys) {
            if (key == name) {
                auto v = readNumber(value, where + "." + key);
                if (!v) return std::unexpected(v.error());
                out.*field = *v;
                found = true;
                break;
            }
        }
        for (const char* name : extra) {
            found = found || key == name;
        }
        if (!found) {
            return fail("'{}.{}' is not a key this build reads", where, key);
        }
    }
    return {};
}

} // namespace

nlohmann::json tileDynamicsToJson(const TileDynamics& d) {
    nlohmann::json j = nlohmann::json::object();
    const TileDynamics def;
    if (d.amplitude != def.amplitude) j["amplitude"] = d.amplitude;
    if (!d.waves.empty()) {
        nlohmann::json waves = nlohmann::json::array();
        for (const TileWave& w : d.waves) {
            nlohmann::json o = {{"kind", w.circular ? "circular" : "planar"},
                                {"amplitude", w.amplitude},
                                {"wavelength", w.wavelength},
                                {"speed", w.speed},
                                {"sharpness", w.sharpness},
                                {"crest", w.crest},
                                {"onLand", w.onLand},
                                {"onWater", w.onWater},
                                {"phase", w.phase}};
            if (w.circular) {
                o["centre"] = nlohmann::json::array({w.centre.x, w.centre.y});
            } else {
                o["direction"] = w.direction;
            }
            waves.push_back(std::move(o));
        }
        j["waves"] = std::move(waves);
    }
    if (d.breathingAmplitude != def.breathingAmplitude || d.breathingPeriod != def.breathingPeriod ||
        d.breathingWavelength != def.breathingWavelength) {
        j["breathing"] = {{"amplitude", d.breathingAmplitude},
                          {"period", d.breathingPeriod},
                          {"wavelength", d.breathingWavelength}};
    }
    if (d.rolling != def.rolling) j["rolling"] = nlohmann::json::array({d.rolling.x, d.rolling.y});
    if (d.riverWaves != def.riverWaves || d.pondWaves != def.pondWaves ||
        d.riverDriftAmplitude != def.riverDriftAmplitude || d.riverDriftWavelength != def.riverDriftWavelength ||
        d.riverDriftSpeed != def.riverDriftSpeed) { // ADR-1965
        j["water"] = {{"riverWaves", d.riverWaves},
                      {"pondWaves", d.pondWaves},
                      {"driftAmplitude", d.riverDriftAmplitude},
                      {"driftWavelength", d.riverDriftWavelength},
                      {"driftSpeed", d.riverDriftSpeed}};
    }
    {
        const TileSpringSpec& s = d.spring;
        const TileSpringSpec ds;
        if (s.depth != ds.depth || s.frequency != ds.frequency || s.damping != ds.damping ||
            s.rippleSpeed != ds.rippleSpeed || s.rippleRadius != ds.rippleRadius || s.glowRadius != ds.glowRadius) {
            j["spring"] = {{"depth", s.depth},
                           {"frequency", s.frequency},
                           {"damping", s.damping},
                           {"rippleSpeed", s.rippleSpeed},
                           {"rippleRadius", s.rippleRadius},
                           {"glowRadius", s.glowRadius}};
        }
    }
    if (d.contourInterval != def.contourInterval || d.contourOffset != def.contourOffset) {
        j["contour"] = {{"interval", d.contourInterval}, {"offset", d.contourOffset}};
    }
    if (d.glow != def.glow || d.landingGain != def.landingGain || d.landingColor != def.landingColor) {
        j["glow"] = {{"path", d.glow.x},       {"contour", d.glow.y}, {"crest", d.glow.z},
                     {"pads", d.glow.w},       {"landing", d.landingGain},
                     {"landingColor", nlohmann::json::array({d.landingColor.x, d.landingColor.y, d.landingColor.z})}};
    }
    const TileRingSpec& r = d.rings;
    j["rings"] = {{"amplitude", r.amplitude}, {"speed", r.speed},   {"wavelength", r.wavelength},
                  {"width", r.width},         {"decay", r.decay},   {"crest", r.crest}};
    if (!d.events.empty()) {
        nlohmann::json events = nlohmann::json::array();
        for (const TileEvent& e : d.events) {
            events.push_back({{"time", e.time},
                              {"position", nlohmann::json::array({e.position.x, e.position.y})},
                              {"strength", e.strength}});
        }
        j["events"] = std::move(events);
    }
    return j;
}

Result<TileDynamics> tileDynamicsFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("'tileDynamics' must be an object");
    }
    TileDynamics d;
    for (const auto& [key, value] : j.items()) {
        const std::string where = "tileDynamics." + key;
        if (key == "amplitude") {
            auto v = readNumber(value, where);
            if (!v) return std::unexpected(v.error());
            d.amplitude = *v;
        } else if (key == "rolling") {
            auto v = readPair(value, where);
            if (!v) return std::unexpected(v.error());
            d.rolling = *v;
        } else if (key == "breathing") {
            struct B {
                float amplitude, period, wavelength;
            } b{d.breathingAmplitude, d.breathingPeriod, d.breathingWavelength};
            if (auto r = readFields<B>(value, where, {{"amplitude", &B::amplitude}, {"period", &B::period},
                                                      {"wavelength", &B::wavelength}}, b); !r) {
                return std::unexpected(r.error());
            }
            d.breathingAmplitude = b.amplitude;
            d.breathingPeriod = b.period;
            d.breathingWavelength = b.wavelength;
        } else if (key == "spring") {
            if (auto r = readFields<TileSpringSpec>(value, where,
                                                    {{"depth", &TileSpringSpec::depth},
                                                     {"frequency", &TileSpringSpec::frequency},
                                                     {"damping", &TileSpringSpec::damping},
                                                     {"rippleSpeed", &TileSpringSpec::rippleSpeed},
                                                     {"rippleRadius", &TileSpringSpec::rippleRadius},
                                                     {"glowRadius", &TileSpringSpec::glowRadius}},
                                                    d.spring);
                !r) {
                return std::unexpected(r.error());
            }
        } else if (key == "water") { // ADR-1965: the waves by water body and the rivers' drift
            struct W {
                float riverWaves, pondWaves, driftAmplitude, driftWavelength, driftSpeed;
            } w{d.riverWaves, d.pondWaves, d.riverDriftAmplitude, d.riverDriftWavelength, d.riverDriftSpeed};
            if (auto r = readFields<W>(value, where,
                                       {{"riverWaves", &W::riverWaves}, {"pondWaves", &W::pondWaves},
                                        {"driftAmplitude", &W::driftAmplitude},
                                        {"driftWavelength", &W::driftWavelength}, {"driftSpeed", &W::driftSpeed}},
                                       w);
                !r) {
                return std::unexpected(r.error());
            }
            d.riverWaves = w.riverWaves;
            d.pondWaves = w.pondWaves;
            d.riverDriftAmplitude = w.driftAmplitude;
            d.riverDriftWavelength = w.driftWavelength;
            d.riverDriftSpeed = w.driftSpeed;
        } else if (key == "glow") { // ADR-1963
            struct G {
                float path, contour, crest, pads, landing;
            } g{d.glow.x, d.glow.y, d.glow.z, d.glow.w, d.landingGain};
            if (auto r = readFields<G>(value, where,
                                       {{"path", &G::path}, {"contour", &G::contour}, {"crest", &G::crest},
                                        {"pads", &G::pads}, {"landing", &G::landing}},
                                       g, {"landingColor"});
                !r) {
                return std::unexpected(r.error());
            }
            d.glow = {g.path, g.contour, g.crest, g.pads};
            d.landingGain = g.landing; // ADR-1960
            if (value.contains("landingColor")) {
                const auto& c = value["landingColor"];
                if (!c.is_array() || c.size() != 3 || !c[0].is_number() || !c[1].is_number() || !c[2].is_number()) {
                    return fail("'{}.landingColor' must be [r, g, b]", where);
                }
                d.landingColor = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>()};
            }
        } else if (key == "contour") {
            struct C {
                float interval, offset;
            } c{d.contourInterval, d.contourOffset};
            if (auto r = readFields<C>(value, where, {{"interval", &C::interval}, {"offset", &C::offset}}, c); !r) {
                return std::unexpected(r.error());
            }
            d.contourInterval = c.interval;
            d.contourOffset = c.offset;
        } else if (key == "rings") {
            if (auto r = readFields<TileRingSpec>(value, where,
                                                  {{"amplitude", &TileRingSpec::amplitude},
                                                   {"speed", &TileRingSpec::speed},
                                                   {"wavelength", &TileRingSpec::wavelength},
                                                   {"width", &TileRingSpec::width},
                                                   {"decay", &TileRingSpec::decay},
                                                   {"crest", &TileRingSpec::crest}},
                                                  d.rings);
                !r) {
                return std::unexpected(r.error());
            }
        } else if (key == "waves") {
            if (!value.is_array()) return fail("'{}' must be an array", where);
            for (const auto& item : value) {
                TileWave w;
                if (auto r = readFields<TileWave>(item, where,
                                                  {{"amplitude", &TileWave::amplitude},
                                                   {"wavelength", &TileWave::wavelength},
                                                   {"speed", &TileWave::speed},
                                                   {"direction", &TileWave::direction},
                                                   {"sharpness", &TileWave::sharpness},
                                                   {"crest", &TileWave::crest},
                                                   {"onLand", &TileWave::onLand},
                                                   {"onWater", &TileWave::onWater},
                                                   {"phase", &TileWave::phase}},
                                                  w, {"kind", "centre"});
                    !r) {
                    return std::unexpected(r.error());
                }
                if (item.contains("kind")) {
                    const auto& k = item.at("kind");
                    if (!k.is_string() || (k != "planar" && k != "circular")) {
                        return fail("'{}.kind' must be \"planar\" or \"circular\"", where);
                    }
                    w.circular = k == "circular";
                }
                if (item.contains("centre")) {
                    auto c = readPair(item.at("centre"), where + ".centre");
                    if (!c) return std::unexpected(c.error());
                    w.centre = *c;
                }
                d.waves.push_back(w);
            }
        } else if (key == "events") {
            if (!value.is_array()) return fail("'{}' must be an array", where);
            for (const auto& item : value) {
                TileEvent e;
                if (auto r = readFields<TileEvent>(item, where, {{"time", &TileEvent::time}, {"strength", &TileEvent::strength}},
                                                   e, {"position"});
                    !r) {
                    return std::unexpected(r.error());
                }
                if (!item.contains("position")) return fail("'{}' needs a position [x, z]", where);
                auto p = readPair(item.at("position"), where + ".position");
                if (!p) return std::unexpected(p.error());
                e.position = *p;
                d.events.push_back(e);
            }
        } else {
            return fail("'{}' is not a key this build reads", where);
        }
    }
    if (auto r = validateTileDynamics(d); !r) {
        return std::unexpected(r.error());
    }
    return d;
}

} // namespace avgen::scene
