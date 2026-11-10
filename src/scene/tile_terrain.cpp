#include "scene/tile_terrain.hpp"

#include "scene/generator.hpp"

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

float polylineDistance(const std::vector<glm::vec2>& line, float x, float z) {
    float best = 1e9f;
    const std::size_t n = std::min(line.size(), kMaxTilePathPoints);
    if (n == 1) {
        return glm::length(glm::vec2(x, z) - line[0]);
    }
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const glm::vec2 a = line[i];
        const glm::vec2 ab = line[i + 1] - a;
        const glm::vec2 ap = glm::vec2(x, z) - a;
        const float t = std::clamp(glm::dot(ap, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
        best = std::min(best, glm::length(ap - ab * t));
    }
    return best;
}

} // namespace

float tilePathDistance(const TileTerrainSpec& spec, float x, float z) { return polylineDistance(spec.path, x, z); }

float tileRiverDistance(const TileTerrainSpec& spec, float x, float z) { return polylineDistance(spec.river, x, z); }

namespace {

// The one body of the base: `t`, `rollX`, `rollZ`, `breath` all at their rest values (0, 0, 0, 0) give the static
// ground bit for bit -- x - 0 * t is x, and a relief multiplied by 1 + 0 * s is the relief.
TileSample baseSample(const TileTerrainSpec& s, float x, float z, float t, float rollX, float rollZ, float breathAmp,
                      float breathPeriod, float breathWavelength) {
    const std::uint32_t seed = s.seed;
    // ADR-1953: the rolling formations -- the hills' domain (and its warp) drifts; the coast, the path and the river do
    // not.
    const float hx = x - rollX * t;
    const float hz = z - rollZ * t;
    const float region = tileValueNoise(seed ^ kRegionSeed, x / s.regionWavelength, z / s.regionWavelength);
    const float k = 1.0f + s.regionVariation * (2.0f * region - 1.0f);
    const float c = tileFbm(seed ^ kContinentSeed, x / s.continentWavelength, z / s.continentWavelength, 3);
    const float land = smooth01(s.landThreshold - s.shoreBand, s.landThreshold + s.shoreBand, c);
    const float inland = smooth01(s.landThreshold, s.landThreshold + s.inlandBand, c);
    const float wx =
        (tileFbm(seed ^ kWarpXSeed, hx / s.warpWavelength, hz / s.warpWavelength, 2) * 2.0f - 1.0f) * s.hillWarp;
    const float wz =
        (tileFbm(seed ^ kWarpZSeed, hx / s.warpWavelength + 31.7f, hz / s.warpWavelength - 11.3f, 2) * 2.0f - 1.0f) *
        s.hillWarp;
    const float hwl = s.hillWavelength * k;
    const float hn = tileFbm(seed ^ kHillSeed, (hx + wx) / hwl, (hz + wz) / hwl, s.hillOctaves);
    const float shaped = smooth01(s.hillLow, s.hillHigh, hn);
    const float excess = std::max(shaped - s.crownThreshold, 0.0f);
    const float hill = shaped - excess * s.crownFlatten;
    // ADR-1953: the breathing -- the relief swells and settles by region, each region a little out of step.
    float breath = 1.0f;
    if (breathAmp != 0.0f) {
        const float n = tileValueNoise(seed ^ kBreathSeed, x / breathWavelength, z / breathWavelength);
        breath = 1.0f + breathAmp * std::sin(kTwoPi * (t / breathPeriod + n));
    }
    float landHeight = s.shoreRise + s.hillHeight * k * hill * inland * s.heightScale * breath;
    float channel = 0.0f;
    if (!s.river.empty()) {
        // The valley: the land eases to the bed (the sea floor's level) over the valley's half-width, the channel water.
        const float d = tileRiverDistance(s, x, z);
        const float v = 1.0f - smooth01(0.5f * s.riverWidth, 0.5f * s.valleyWidth, d);
        landHeight = landHeight + (-s.seaDepth - landHeight) * v;
        channel = 1.0f - smooth01(0.5f * s.riverWidth - 0.5f, 0.5f * s.riverWidth + 0.5f, d);
    }
    TileSample out;
    out.height = (s.seaLevel - s.seaDepth) + ((s.seaLevel + landHeight) - (s.seaLevel - s.seaDepth)) * land;
    out.water = 1.0f - land + channel * land;
    out.shore = 1.0f - smooth01(0.0f, 3.0f * s.shoreBand, std::abs(c - s.landThreshold));
    out.flat = smooth01(s.crownThreshold, s.crownThreshold + 0.06f, shaped) * s.crownFlatten * inland * land;
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
                     float water, float cellSize) {
    float height = 0.0f;
    float crest = 0.0f;
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
        const float mask = (w.onLand + (w.onWater - w.onLand) * water) * waveAliasFade(w.wavelength, cellSize);
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
    return {dyn.amplitude * height, std::clamp(crest, 0.0f, 1.0f)};
}

TileSample tileSampleMoving(const TileTerrainSpec& spec, const TileDynamics& dyn, float x, float z, float t,
                            float cellSize) {
    TileSample out = tileSample(spec, x, z, t, dyn);
    const glm::vec2 m = tileMotion(dyn, dyn.eventsAt(t), x, z, t, out.water, cellSize);
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
    if (path.size() > kMaxTilePathPoints || river.size() > kMaxTilePathPoints) {
        return fail("tiles: the path and the river have at most {} points each", kMaxTilePathPoints);
    }
    if (!(riverWidth > 0.0f) || !(valleyWidth > riverWidth) || !(heightScale >= 0.0f) || heightScale > 8.0f) {
        return fail("tiles: riverWidth must be positive, valleyWidth above it, heightScale 0..8");
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
                          crownFlatten, regionWavelength, regionVariation, pathWidth, pathLift, riverWidth,
                          valleyWidth, heightScale}) {
        f(v);
    }
    mix(0x52495645u); // "RIVE": separates the path's points from the river's
    for (const glm::vec2& p : river) {
        f(p.x);
        f(p.y);
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
    {"riverWidth", &TileTerrainSpec::riverWidth},
    {"valleyWidth", &TileTerrainSpec::valleyWidth},
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
    nlohmann::json river = nlohmann::json::array();
    for (const glm::vec2& p : spec.river) {
        river.push_back(nlohmann::json::array({p.x, p.y}));
    }
    j["river"] = std::move(river);
    return j;
}

Result<TileTerrainSpec> tileTerrainFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("'tiles' must be an object");
    }
    TileTerrainSpec spec;
    for (const auto& [key, value] : j.items()) {
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
        if (key == "path" || key == "river") {
            if (!value.is_array()) {
                return fail("'tiles.{}' must be an array of [x, z]", key);
            }
            std::vector<glm::vec2>& line = key == "path" ? spec.path : spec.river;
            line.clear();
            for (const auto& p : value) {
                if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number()) {
                    return fail("'tiles.{}' must be an array of [x, z]", key);
                }
                line.emplace_back(p[0].get<float>(), p[1].get<float>());
            }
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
    return spec;
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
