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

// ---- interior versus silhouette (ADR-257) ------------------------------------------------------

namespace {

Mask boxFilter(const Mask& mask, std::uint32_t width, std::uint32_t height, int radius, bool minimum) {
    const std::uint8_t seed = minimum ? 1 : 0;
    const std::uint8_t outside = 0; // out of bounds is NOT set, for both operations
    Mask horizontal(mask.size(), 0);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint8_t value = seed;
            for (int d = -radius; d <= radius; ++d) {
                const long sx = static_cast<long>(x) + d;
                const std::uint8_t sample =
                    (sx < 0 || sx >= static_cast<long>(width))
                        ? outside
                        : mask[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(sx)];
                value = minimum ? std::min(value, sample) : std::max(value, sample);
            }
            horizontal[static_cast<std::size_t>(y) * width + x] = value;
        }
    }
    Mask out(mask.size(), 0);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint8_t value = seed;
            for (int d = -radius; d <= radius; ++d) {
                const long sy = static_cast<long>(y) + d;
                const std::uint8_t sample =
                    (sy < 0 || sy >= static_cast<long>(height))
                        ? outside
                        : horizontal[static_cast<std::size_t>(sy) * width + x];
                value = minimum ? std::min(value, sample) : std::max(value, sample);
            }
            out[static_cast<std::size_t>(y) * width + x] = value;
        }
    }
    return out;
}

} // namespace

Mask erode(const Mask& mask, std::uint32_t width, std::uint32_t height, int radius) {
    if (radius <= 0 || mask.size() != static_cast<std::size_t>(width) * height) {
        return mask;
    }
    return boxFilter(mask, width, height, radius, true);
}

Mask dilate(const Mask& mask, std::uint32_t width, std::uint32_t height, int radius) {
    if (radius <= 0 || mask.size() != static_cast<std::size_t>(width) * height) {
        return mask;
    }
    return boxFilter(mask, width, height, radius, false);
}

SilhouetteSplit splitSilhouette(const Plane& id, std::uint32_t objectId, int radius) {
    SilhouetteSplit split;
    if (!id.valid()) {
        return split;
    }
    const std::size_t pixels = static_cast<std::size_t>(id.width) * id.height;
    split.object.assign(pixels, 0);
    for (std::size_t i = 0; i < pixels; ++i) {
        const float packed = id.rgba[i * 4];
        // A packed identifier of exactly 0 is "no geometry wrote here", which is a sky matte and
        // not object 0. Objects with an id of 0 in a non-default pick space are still reachable,
        // because their packed word carries a material id in its high bits.
        if (packed != 0.0f && objectIdOf(packed) == objectId) {
            split.object[i] = 1;
        }
    }
    split.interior = erode(split.object, id.width, id.height, radius);
    const Mask outer = dilate(split.object, id.width, id.height, radius);
    split.band.assign(pixels, 0);
    split.elsewhere.assign(pixels, 0);
    for (std::size_t i = 0; i < pixels; ++i) {
        split.band[i] = (outer[i] != 0 && split.interior[i] == 0) ? 1 : 0;
        split.elsewhere[i] = outer[i] == 0 ? 1 : 0;
    }
    return split;
}

MaskedDifference maskedLumaDifference(const Frame& a, const Frame& b, const Mask& mask) {
    MaskedDifference result;
    if (!a.valid() || !a.sameShapeAs(b) ||
        mask.size() != static_cast<std::size_t>(a.width) * a.height) {
        return result;
    }
    // `luma()` returns 0..255 and not 0..1 -- its own header said 0..1 until a caller believed it
    // and produced a table of luma differences in the tens of thousands.
    const std::vector<float> la = luma(a);
    const std::vector<float> lb = luma(b);
    double sum = 0.0;
    for (std::size_t i = 0; i < mask.size(); ++i) {
        if (mask[i] == 0) {
            continue;
        }
        const double d = std::abs(static_cast<double>(la[i]) - static_cast<double>(lb[i]));
        sum += d;
        result.max = std::max(result.max, d);
        ++result.pixels;
    }
    result.mean = result.pixels == 0 ? 0.0 : sum / static_cast<double>(result.pixels);
    return result;
}

} // namespace avgen::quality
