#include "artifacts/masks.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace avgen::quality {
namespace {

constexpr double kExactFloatInteger = 16777216.0; // 2^24

std::uint32_t toPacked(float value) {
    if (!(value >= 0.0f)) {
        return 0;
    }
    return static_cast<std::uint32_t>(std::llround(static_cast<double>(value)));
}

} // namespace

double coverage(const Mask& mask) {
    if (mask.empty()) {
        return 0.0;
    }
    std::size_t set = 0;
    for (const std::uint8_t v : mask) {
        set += v != 0 ? 1u : 0u;
    }
    return static_cast<double>(set) / static_cast<double>(mask.size());
}

std::uint32_t materialIdOf(float packed) {
    return toPacked(packed) >> 16u;
}

std::uint32_t objectIdOf(float packed) {
    return toPacked(packed) & 0xFFFFu;
}

Mask everything(std::uint32_t width, std::uint32_t height) {
    return Mask(static_cast<std::size_t>(width) * height, 1);
}

IdentifierSurvey surveyIdentifiers(const Plane& id) {
    IdentifierSurvey survey;
    if (!id.valid()) {
        return survey;
    }
    std::unordered_set<std::uint32_t> distinct;
    std::size_t background = 0;
    for (std::size_t i = 0; i < id.rgba.size(); i += 4) {
        const float value = id.rgba[i];
        if (value == 0.0f) {
            ++background;
        }
        if (std::abs(static_cast<double>(value)) >= kExactFloatInteger) {
            survey.exceedsExactFloatRange = true;
        }
        distinct.insert(toPacked(value));
    }
    survey.distinctValues = distinct.size();
    const auto pixels = static_cast<double>(id.rgba.size() / 4);
    survey.backgroundFraction = pixels > 0.0 ? static_cast<double>(background) / pixels : 0.0;
    return survey;
}

Mask specularMask(const Plane* emission, const Plane* normal, double emissionLuminanceThreshold,
                  double maxRoughness) {
    const Plane* shape = emission != nullptr && emission->valid() ? emission : nullptr;
    if (shape == nullptr && normal != nullptr && normal->valid()) {
        shape = normal;
    }
    if (shape == nullptr) {
        return {};
    }
    const std::size_t pixels = static_cast<std::size_t>(shape->width) * shape->height;
    Mask mask(pixels, 0);
    const bool useEmission = emission != nullptr && emission->valid() &&
                             static_cast<std::size_t>(emission->width) * emission->height == pixels;
    const bool useNormal = normal != nullptr && normal->valid() &&
                           static_cast<std::size_t>(normal->width) * normal->height == pixels;
    for (std::size_t i = 0; i < pixels; ++i) {
        bool in = false;
        if (useEmission) {
            const float* e = emission->rgba.data() + i * 4;
            const double luminance = 0.2126 * static_cast<double>(e[0]) +
                                     0.7152 * static_cast<double>(e[1]) +
                                     0.0722 * static_cast<double>(e[2]);
            in = in || luminance > emissionLuminanceThreshold;
        }
        if (useNormal) {
            const float* n = normal->rgba.data() + i * 4;
            // (0,0,0,0) is the sky matte the decode writes where no geometry drew. A roughness of
            // zero there is not a mirror, it is an absence, and a mask that counted it would make
            // "specular instability" a measurement of the sky.
            const bool hasSurface = n[0] != 0.0f || n[1] != 0.0f || n[2] != 0.0f;
            in = in || (hasSurface && static_cast<double>(n[3]) < maxRoughness);
        }
        mask[i] = in ? 1 : 0;
    }
    return mask;
}

Mask normalUnchangedMask(const Plane& previous, const Plane& current, double cosThreshold) {
    if (!previous.valid() || !current.valid() || previous.width != current.width ||
        previous.height != current.height) {
        return {};
    }
    const std::size_t pixels = static_cast<std::size_t>(current.width) * current.height;
    Mask mask(pixels, 0);
    for (std::size_t i = 0; i < pixels; ++i) {
        const float* a = previous.rgba.data() + i * 4;
        const float* b = current.rgba.data() + i * 4;
        const bool surfaceA = a[0] != 0.0f || a[1] != 0.0f || a[2] != 0.0f;
        const bool surfaceB = b[0] != 0.0f || b[1] != 0.0f || b[2] != 0.0f;
        if (!surfaceA || !surfaceB) {
            continue; // sky in either frame: there is no normal to compare
        }
        const double dot = static_cast<double>(a[0]) * static_cast<double>(b[0]) +
                           static_cast<double>(a[1]) * static_cast<double>(b[1]) +
                           static_cast<double>(a[2]) * static_cast<double>(b[2]);
        mask[i] = dot >= cosThreshold ? 1 : 0;
    }
    return mask;
}

Mask identifierChurnMask(const Plane& idPrevious, const Plane& idCurrent, const Plane& velocity,
                         const Plane* depthPrevious, const Plane* depthCurrent,
                         double staticVelocityPixels, double depthRelativeThreshold) {
    if (!idPrevious.valid() || !idCurrent.valid() || !velocity.valid() ||
        idPrevious.width != idCurrent.width || idPrevious.height != idCurrent.height ||
        velocity.width != idCurrent.width || velocity.height != idCurrent.height) {
        return {};
    }
    const std::uint32_t w = idCurrent.width;
    const std::uint32_t h = idCurrent.height;
    Mask mask(static_cast<std::size_t>(w) * h, 0);
    const bool useDepth = depthPrevious != nullptr && depthCurrent != nullptr &&
                          depthPrevious->valid() && depthCurrent->valid() &&
                          depthCurrent->width == w && depthCurrent->height == h;
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * w + x;
            const float* v = velocity.at(x, y);
            const double dx = static_cast<double>(v[0]) * static_cast<double>(w);
            const double dy = static_cast<double>(v[1]) * static_cast<double>(h);
            if (std::abs(dx) > staticVelocityPixels || std::abs(dy) > staticVelocityPixels) {
                continue; // the surface moved; a changed id here is ordinary occlusion
            }
            if (idPrevious.rgba[i * 4] == idCurrent.rgba[i * 4]) {
                continue;
            }
            if (useDepth) {
                const double here = static_cast<double>(depthCurrent->rgba[i * 4]);
                const double there = static_cast<double>(depthPrevious->rgba[i * 4]);
                const double scale = std::max(std::abs(here), 1e-4);
                if (std::abs(here - there) / scale > depthRelativeThreshold) {
                    continue; // a different surface arrived, which is not a level swap
                }
            }
            mask[i] = 1;
        }
    }
    return mask;
}

Mask materialClassMask(const Plane& id, const std::vector<std::uint32_t>& materialIds) {
    if (!id.valid() || materialIds.empty()) {
        return {};
    }
    const std::unordered_set<std::uint32_t> wanted(materialIds.begin(), materialIds.end());
    const std::size_t pixels = static_cast<std::size_t>(id.width) * id.height;
    Mask mask(pixels, 0);
    for (std::size_t i = 0; i < pixels; ++i) {
        mask[i] = wanted.count(materialIdOf(id.rgba[i * 4])) != 0 ? 1 : 0;
    }
    return mask;
}

} // namespace avgen::quality
