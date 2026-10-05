#include "scene/generator.hpp"

#include "spatial/detail.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace avgen::scene {

namespace {

constexpr float kTwoPi = 6.283185307179586f;

// lowbias32 (Wellons): the kernel's `genMix`, operation for operation.
std::uint32_t mix32(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float lerpf(float a, float b, float t) { return a + (b - a) * t; }

float valueNoise(std::uint32_t seed, float x, float z) {
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
    return lerpf(lerpf(c00, c10, sx), lerpf(c01, c11, sx), sz);
}

std::uint32_t presence16(const GeneratorSpec& s) {
    return static_cast<std::uint32_t>(std::lround(std::clamp(s.presence, 0.0f, 1.0f) * 65535.0f));
}

std::uint32_t contrast16(const GeneratorSpec& s) {
    return static_cast<std::uint32_t>(std::lround(std::clamp(s.clusterContrast, 0.0f, 1.0f) * 65535.0f));
}

glm::quat axisAngle(const glm::vec3& axis, float angle) {
    const float h = angle * 0.5f;
    const float s = std::sin(h);
    return glm::quat(std::cos(h), axis.x * s, axis.y * s, axis.z * s);
}

} // namespace

Result<void> GeneratorSpec::validate() const {
    if (name != kGeneratorCells) {
        return fail("generator '{}' is unknown; the only generator is '{}'", name, kGeneratorCells);
    }
    if (version != kGeneratorCellsVersion) {
        return fail("generator '{}' version {} is not this build's ({})", name, version, kGeneratorCellsVersion);
    }
    if (!(cellSize > 0.0f) || !(viewDistance > 0.0f)) {
        return fail("generator: cellSize and viewDistance must be > 0");
    }
    if (presence < 0.0f || presence > 1.0f || clusterContrast < 0.0f || clusterContrast > 1.0f || jitter < 0.0f ||
        jitter > 1.0f) {
        return fail("generator: presence, clusterContrast and jitter must be in [0, 1]");
    }
    if (clusterSize < 0.0f || clusterSize / cellSize > 16384.0f) {
        return fail("generator: clusterSize must be >= 0 and at most 16384 cells");
    }
    if (sizeMin < 0.0f || sizeMax < sizeMin) {
        return fail("generator: sizes need 0 <= sizeMin <= sizeMax");
    }
    if (bounded && (regionMax.x < regionMin.x || regionMax.y < regionMin.y)) {
        return fail("generator: regionMax must be >= regionMin");
    }
    if (regionRadius < 0.0f || groundFrequency < 0.0f) {
        return fail("generator: regionRadius and groundFrequency must be >= 0");
    }
    // The largest window must fit: it is what the GPU buffer is sized for.
    const double side = 2.0 * std::ceil(static_cast<double>(viewDistance) / cellSize) + 1.0;
    double cells = side * side;
    if (bounded) {
        const GeneratorRegionCells r = generatorRegionCells(*this);
        cells = std::min(side, static_cast<double>(r.maxX - r.minX + 1)) *
                std::min(side, static_cast<double>(r.maxZ - r.minZ + 1));
    }
    if (cells > static_cast<double>(kMaxGeneratorCells)) {
        return fail("generator: a window of {:.0f} cells (viewDistance {} m / cellSize {} m) is over the {} cell "
                    "ceiling; raise cellSize, lower viewDistance or bound the region",
                    cells, viewDistance, cellSize, kMaxGeneratorCells);
    }
    return {};
}

std::uint64_t GeneratorSpec::structuralHash() const {
    spatial::detail::Fnv h;
    h.str(name);
    h.u32(version);
    h.f32(cellSize);
    h.f32(viewDistance);
    h.boolean(bounded);
    if (bounded) {
        h.v2(regionMin);
        h.v2(regionMax);
    }
    return h.value();
}

std::uint64_t GeneratorSpec::hash() const {
    spatial::detail::Fnv h;
    h.u64(structuralHash());
    h.f32(presence);
    h.f32(clusterSize);
    h.f32(clusterContrast);
    h.f32(jitter);
    h.f32(sizeMin);
    h.f32(sizeMax);
    h.f32(tilt);
    h.f32(regionRadius);
    h.f32(groundHeight);
    h.f32(groundAmplitude);
    h.f32(groundFrequency);
    h.u32(groundSeed);
    return h.value();
}

std::uint32_t generatorHash(std::int32_t ix, std::int32_t iz, std::uint32_t seed, std::uint32_t channel) {
    const std::uint32_t s = mix32(seed + channel * 0x9e3779b9u);
    const std::uint32_t z = mix32(static_cast<std::uint32_t>(iz) * 0xd8163841u ^ s);
    return mix32(static_cast<std::uint32_t>(ix) * 0x8da6b343u ^ z);
}

float generatorUnit(std::uint32_t hash) {
    return static_cast<float>(hash >> 8) * (1.0f / 16777216.0f);
}

std::int32_t generatorFloorDiv(std::int32_t a, std::int32_t b) {
    return a < 0 ? (a - b + 1) / b : a / b;
}

std::int32_t generatorClusterCells(const GeneratorSpec& spec) {
    if (!(spec.clusterSize > 0.0f)) {
        return 0;
    }
    return std::max(1, static_cast<std::int32_t>(std::lround(spec.clusterSize / spec.cellSize)));
}

std::uint32_t generatorThreshold(const GeneratorSpec& spec, std::uint32_t seed, std::int32_t ix, std::int32_t iz) {
    std::uint32_t fertility = 65535u;
    const std::int32_t cc = generatorClusterCells(spec);
    if (cc > 0) {
        const std::int32_t cx = generatorFloorDiv(ix, cc);
        const std::int32_t cz = generatorFloorDiv(iz, cc);
        const auto fx = static_cast<std::uint32_t>(ix - cx * cc);
        const auto fz = static_cast<std::uint32_t>(iz - cz * cc);
        const auto ucc = static_cast<std::uint32_t>(cc);
        const std::uint32_t c00 = generatorHash(cx, cz, seed, 7u) >> 16;
        const std::uint32_t c10 = generatorHash(cx + 1, cz, seed, 7u) >> 16;
        const std::uint32_t c01 = generatorHash(cx, cz + 1, seed, 7u) >> 16;
        const std::uint32_t c11 = generatorHash(cx + 1, cz + 1, seed, 7u) >> 16;
        const std::uint32_t a = (c00 * (ucc - fx) + c10 * fx) / ucc;
        const std::uint32_t b = (c01 * (ucc - fx) + c11 * fx) / ucc;
        const std::uint32_t v = (a * (ucc - fz) + b * fz) / ucc;
        fertility = 65535u - ((contrast16(spec) * (65535u - v)) >> 16);
    }
    return presence16(spec) * fertility;
}

GeneratorRegionCells generatorRegionCells(const GeneratorSpec& spec) {
    GeneratorRegionCells r;
    // A cell belongs to the region when its centre does: floor(min / c - 0.5) + 1 .. floor(max / c - 0.5).
    r.minX = static_cast<std::int32_t>(std::floor(spec.regionMin.x / spec.cellSize - 0.5f)) + 1;
    r.minZ = static_cast<std::int32_t>(std::floor(spec.regionMin.y / spec.cellSize - 0.5f)) + 1;
    r.maxX = static_cast<std::int32_t>(std::floor(spec.regionMax.x / spec.cellSize - 0.5f));
    r.maxZ = static_cast<std::int32_t>(std::floor(spec.regionMax.y / spec.cellSize - 0.5f));
    return r;
}

float generatorGroundHeight(const GeneratorSpec& spec, float x, float z) {
    if (spec.groundAmplitude == 0.0f) {
        return spec.groundHeight;
    }
    const std::uint32_t seed = spec.groundSeed;
    const float f = spec.groundFrequency;
    const float n = (valueNoise(seed, x * f, z * f) + 0.5f * valueNoise(seed, x * f * 2.0f + 17.3f, z * f * 2.0f - 9.1f)) /
                    1.5f;
    return spec.groundHeight + spec.groundAmplitude * (n * 2.0f - 1.0f);
}

GeneratorWindow generatorWindow(const GeneratorSpec& spec, const glm::vec3& cameraGen, float reach) {
    GeneratorWindow w;
    const float half = spec.viewDistance * std::clamp(reach, 0.0f, 1.0f);
    const auto n = static_cast<std::int32_t>(std::ceil(half / spec.cellSize));
    const auto cx = static_cast<std::int32_t>(std::floor(cameraGen.x / spec.cellSize));
    const auto cz = static_cast<std::int32_t>(std::floor(cameraGen.z / spec.cellSize));
    std::int32_t loX = cx - n;
    std::int32_t loZ = cz - n;
    std::int32_t hiX = cx + n;
    std::int32_t hiZ = cz + n;
    if (spec.bounded) {
        const GeneratorRegionCells r = generatorRegionCells(spec);
        loX = std::max(loX, r.minX);
        loZ = std::max(loZ, r.minZ);
        hiX = std::min(hiX, r.maxX);
        hiZ = std::min(hiZ, r.maxZ);
    }
    w.originX = loX;
    w.originZ = loZ;
    w.countX = std::max(0, hiX - loX + 1);
    w.countZ = std::max(0, hiZ - loZ + 1);
    return w;
}

GeneratorWindow generatorCapacity(const GeneratorSpec& spec) {
    GeneratorWindow w;
    const auto side = 2 * static_cast<std::int32_t>(std::ceil(spec.viewDistance / spec.cellSize)) + 1;
    w.countX = side;
    w.countZ = side;
    if (spec.bounded) {
        const GeneratorRegionCells r = generatorRegionCells(spec);
        w.countX = std::min(side, std::max(0, r.maxX - r.minX + 1));
        w.countZ = std::min(side, std::max(0, r.maxZ - r.minZ + 1));
    }
    return w;
}

std::optional<GeneratedElement> generatorElement(const GeneratorSpec& spec, std::uint32_t seed,
                                                const GeneratorVariation& variation, std::int32_t ix, std::int32_t iz) {
    if (spec.bounded) {
        const GeneratorRegionCells r = generatorRegionCells(spec);
        if (ix < r.minX || ix > r.maxX || iz < r.minZ || iz > r.maxZ) {
            return std::nullopt;
        }
        if (spec.regionRadius > 0.0f) {
            const float ccx = (static_cast<float>(ix) + 0.5f) * spec.cellSize;
            const float ccz = (static_cast<float>(iz) + 0.5f) * spec.cellSize;
            const float dx = ccx - (spec.regionMin.x + spec.regionMax.x) * 0.5f;
            const float dz = ccz - (spec.regionMin.y + spec.regionMax.y) * 0.5f;
            if (dx * dx + dz * dz > spec.regionRadius * spec.regionRadius) {
                return std::nullopt;
            }
        }
    }
    if (generatorHash(ix, iz, seed, 0u) >= generatorThreshold(spec, seed, ix, iz)) {
        return std::nullopt;
    }
    GeneratedElement e;
    e.ix = ix;
    e.iz = iz;
    const float u0 = generatorUnit(generatorHash(ix, iz, seed, 1u));
    const float u1 = generatorUnit(generatorHash(ix, iz, seed, 2u));
    const float x = (static_cast<float>(ix) + 0.5f + (u0 - 0.5f) * spec.jitter) * spec.cellSize;
    const float z = (static_cast<float>(iz) + 0.5f + (u1 - 0.5f) * spec.jitter) * spec.cellSize;
    e.position = glm::vec3(x, generatorGroundHeight(spec, x, z), z);
    e.size = lerpf(spec.sizeMin, spec.sizeMax, generatorUnit(generatorHash(ix, iz, seed, 3u)));
    const float yaw = generatorUnit(generatorHash(ix, iz, seed, 4u)) * kTwoPi;
    const float lean = generatorUnit(generatorHash(ix, iz, seed, 5u)) * spec.tilt;
    const float leanDir = generatorUnit(generatorHash(ix, iz, seed, 6u)) * kTwoPi;
    const glm::quat qYaw = axisAngle(glm::vec3(0.0f, 1.0f, 0.0f), yaw);
    const glm::quat qLean = axisAngle(glm::vec3(std::cos(leanDir), 0.0f, std::sin(leanDir)), lean);
    e.rotation = qLean * qYaw;
    e.random = glm::vec4(generatorUnit(generatorHash(ix, iz, seed, 7u + 1u)),
                         generatorUnit(generatorHash(ix, iz, seed, 9u)), generatorUnit(generatorHash(ix, iz, seed, 10u)),
                         generatorUnit(generatorHash(ix, iz, seed, 11u)));
    e.value = 1.0f + variation.valueRandom * (generatorUnit(generatorHash(ix, iz, seed, 12u)) * 2.0f - 1.0f);
    e.emission = generatorUnit(generatorHash(ix, iz, seed, 13u)) < variation.emissiveSparsity
                     ? 0.0f
                     : 1.0f + variation.emissiveRandom * (generatorUnit(generatorHash(ix, iz, seed, 14u)) * 2.0f - 1.0f);
    return e;
}

std::vector<GeneratedElement> generatorQueryRegion(const GeneratorSpec& spec, std::uint32_t seed,
                                                  const GeneratorVariation& variation, const glm::vec2& xzMin,
                                                  const glm::vec2& xzMax, std::size_t cap) {
    std::vector<GeneratedElement> out;
    const auto loX = static_cast<std::int32_t>(std::floor(xzMin.x / spec.cellSize - 0.5f)) + 1;
    const auto loZ = static_cast<std::int32_t>(std::floor(xzMin.y / spec.cellSize - 0.5f)) + 1;
    const auto hiX = static_cast<std::int32_t>(std::floor(xzMax.x / spec.cellSize - 0.5f));
    const auto hiZ = static_cast<std::int32_t>(std::floor(xzMax.y / spec.cellSize - 0.5f));
    for (std::int32_t iz = loZ; iz <= hiZ; ++iz) {
        for (std::int32_t ix = loX; ix <= hiX; ++ix) {
            if (auto e = generatorElement(spec, seed, variation, ix, iz)) {
                out.push_back(*e);
                if (cap > 0 && out.size() >= cap) {
                    return out;
                }
            }
        }
    }
    return out;
}

std::uint64_t generatorPresentCount(const GeneratorSpec& spec, std::uint32_t seed, const GeneratorWindow& window) {
    std::uint64_t n = 0;
    const GeneratorVariation none;
    for (std::int32_t z = 0; z < window.countZ; ++z) {
        for (std::int32_t x = 0; x < window.countX; ++x) {
            n += generatorElement(spec, seed, none, window.originX + x, window.originZ + z) ? 1u : 0u;
        }
    }
    return n;
}

std::optional<GeneratorHit> generatorRaycast(const GeneratorSpec& spec, std::uint32_t seed,
                                             const GeneratorVariation& variation, const glm::vec3& origin,
                                             const glm::vec3& direction, float maxDistance, float sourceRadius,
                                             const glm::vec3& sourceCentre) {
    const float len = glm::length(direction);
    if (!(len > 0.0f) || !(maxDistance > 0.0f)) {
        return std::nullopt;
    }
    const glm::vec3 d = direction / len;
    const float reach = std::max(sourceRadius * spec.sizeMax + glm::length(sourceCentre) * spec.sizeMax, 1e-3f);
    const std::int32_t k = static_cast<std::int32_t>(std::ceil(reach / spec.cellSize)) + 1;
    const float step = spec.cellSize * 0.5f;
    std::optional<GeneratorHit> best;
    std::int32_t lastX = std::numeric_limits<std::int32_t>::min();
    std::int32_t lastZ = std::numeric_limits<std::int32_t>::min();
    for (float t = 0.0f; t <= maxDistance + reach; t += step) {
        if (best && t > best->distance + reach) {
            break; // nothing further along can be nearer
        }
        const glm::vec3 p = origin + d * t;
        const auto cx = static_cast<std::int32_t>(std::floor(p.x / spec.cellSize));
        const auto cz = static_cast<std::int32_t>(std::floor(p.z / spec.cellSize));
        if (cx == lastX && cz == lastZ) {
            continue;
        }
        lastX = cx;
        lastZ = cz;
        for (std::int32_t iz = cz - k; iz <= cz + k; ++iz) {
            for (std::int32_t ix = cx - k; ix <= cx + k; ++ix) {
                const auto e = generatorElement(spec, seed, variation, ix, iz);
                if (!e) {
                    continue;
                }
                const glm::vec3 centre = e->position + e->rotation * (sourceCentre * e->size);
                const float radius = sourceRadius * e->size;
                const glm::vec3 oc = origin - centre;
                const float b = glm::dot(oc, d);
                const float c = glm::dot(oc, oc) - radius * radius;
                const float disc = b * b - c;
                if (disc < 0.0f) {
                    continue;
                }
                const float sq = std::sqrt(disc);
                float hit = -b - sq;
                if (hit < 0.0f) {
                    hit = -b + sq; // inside the sphere
                }
                if (hit < 0.0f || hit > maxDistance) {
                    continue;
                }
                if (!best || hit < best->distance) {
                    best = GeneratorHit{*e, hit};
                }
            }
        }
    }
    return best;
}

std::optional<GeneratedElement> generatorNearest(const GeneratorSpec& spec, std::uint32_t seed,
                                                const GeneratorVariation& variation, const glm::vec3& pointGen,
                                                float sourceRadius, float slack) {
    const float reach = sourceRadius * spec.sizeMax + slack;
    const auto elements = generatorQueryRegion(spec, seed, variation, glm::vec2(pointGen.x, pointGen.z) - glm::vec2(reach + spec.cellSize),
                                               glm::vec2(pointGen.x, pointGen.z) + glm::vec2(reach + spec.cellSize));
    std::optional<GeneratedElement> best;
    float bestGap = std::numeric_limits<float>::max();
    for (const GeneratedElement& e : elements) {
        // The gap between the point and the element's sphere (negative inside it).
        const float gap = glm::length(pointGen - e.position) - sourceRadius * e.size;
        if (gap <= slack && gap < bestGap) {
            bestGap = gap;
            best = e;
        }
    }
    return best;
}

std::vector<Transform> generatorBake(const GeneratorSpec& spec, std::uint32_t seed, const glm::vec2& xzMin,
                                     const glm::vec2& xzMax, std::size_t cap) {
    std::vector<Transform> out;
    for (const GeneratedElement& e : generatorQueryRegion(spec, seed, {}, xzMin, xzMax, cap)) {
        Transform t;
        t.position = e.position;
        t.rotation = e.rotation;
        t.scale = glm::vec3(e.size);
        out.push_back(t);
    }
    return out;
}

} // namespace avgen::scene
