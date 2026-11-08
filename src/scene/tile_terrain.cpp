#include "scene/tile_terrain.hpp"

#include "scene/generator.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>

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

float tilePathDistance(const TileTerrainSpec& spec, float x, float z) {
    float best = 1e9f;
    const std::size_t n = std::min(spec.path.size(), kMaxTilePathPoints);
    if (n == 1) {
        return glm::length(glm::vec2(x, z) - spec.path[0]);
    }
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const glm::vec2 a = spec.path[i];
        const glm::vec2 ab = spec.path[i + 1] - a;
        const glm::vec2 ap = glm::vec2(x, z) - a;
        const float t = std::clamp(glm::dot(ap, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
        best = std::min(best, glm::length(ap - ab * t));
    }
    return best;
}

TileSample tileSample(const TileTerrainSpec& s, float x, float z) {
    const std::uint32_t seed = s.seed;
    const float region = tileValueNoise(seed ^ kRegionSeed, x / s.regionWavelength, z / s.regionWavelength);
    const float k = 1.0f + s.regionVariation * (2.0f * region - 1.0f);
    const float c = tileFbm(seed ^ kContinentSeed, x / s.continentWavelength, z / s.continentWavelength, 3);
    const float land = smooth01(s.landThreshold - s.shoreBand, s.landThreshold + s.shoreBand, c);
    const float inland = smooth01(s.landThreshold, s.landThreshold + s.inlandBand, c);
    const float wx = (tileFbm(seed ^ kWarpXSeed, x / s.warpWavelength, z / s.warpWavelength, 2) * 2.0f - 1.0f) * s.hillWarp;
    const float wz =
        (tileFbm(seed ^ kWarpZSeed, x / s.warpWavelength + 31.7f, z / s.warpWavelength - 11.3f, 2) * 2.0f - 1.0f) *
        s.hillWarp;
    const float hwl = s.hillWavelength * k;
    const float hn = tileFbm(seed ^ kHillSeed, (x + wx) / hwl, (z + wz) / hwl, s.hillOctaves);
    const float shaped = smooth01(s.hillLow, s.hillHigh, hn);
    const float excess = std::max(shaped - s.crownThreshold, 0.0f);
    const float hill = shaped - excess * s.crownFlatten;
    const float landHeight = s.shoreRise + s.hillHeight * k * hill * inland;
    TileSample out;
    out.height = (s.seaLevel - s.seaDepth) + ((s.seaLevel + landHeight) - (s.seaLevel - s.seaDepth)) * land;
    out.water = 1.0f - land;
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
        return fail("tiles: the path has {} points; at most {}", path.size(), kMaxTilePathPoints);
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
                          crownFlatten, regionWavelength, regionVariation, pathWidth, pathLift}) {
        f(v);
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
        if (key == "path") {
            if (!value.is_array()) {
                return fail("'tiles.path' must be an array of [x, z]");
            }
            spec.path.clear();
            for (const auto& p : value) {
                if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number()) {
                    return fail("'tiles.path' must be an array of [x, z]");
                }
                spec.path.emplace_back(p[0].get<float>(), p[1].get<float>());
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
