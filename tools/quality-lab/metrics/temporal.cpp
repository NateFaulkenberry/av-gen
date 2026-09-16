#include "metrics/temporal.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::quality {
namespace {

double percentileOf(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0.0;
    }
    const auto index = static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index),
                     values.end());
    return values[index];
}

// Bilinear sample of a luma plane at a continuous pixel coordinate. Returns false when the sample
// point is outside the grid of texel centres: a partial tap is a prediction built from a clamp, and
// a clamped prediction is an invented one.
//
// **The right-hand tap is clamped and the coordinate is not**, and the difference matters. Rejecting
// every sample whose `x0 + 1` lands off the edge rejects the ENTIRE last row and column even at zero
// velocity -- which on a 96x64 frame is 2.6% of the pixels reported as "disoccluded" by a static
// camera looking at a static scene. That was measured on the zero-velocity control arm, which is
// exactly what that arm is for: a mask that fires on a frame where nothing happened is a mask that
// will fire on every frame. Where the coordinate is exactly on the last texel the extra tap carries
// zero weight, so clamping it changes no value and costs no correctness.
bool sampleBilinear(const std::vector<float>& plane, std::uint32_t w, std::uint32_t h, double x,
                    double y, double& out) {
    if (!(x >= 0.0) || !(y >= 0.0) || x > static_cast<double>(w) - 1.0 ||
        y > static_cast<double>(h) - 1.0) {
        return false;
    }
    const double fx = std::floor(x);
    const double fy = std::floor(y);
    const auto x0 = static_cast<long long>(fx);
    const auto y0 = static_cast<long long>(fy);
    const long long x1 = std::min<long long>(x0 + 1, static_cast<long long>(w) - 1);
    const long long y1 = std::min<long long>(y0 + 1, static_cast<long long>(h) - 1);
    const double tx = x - fx;
    const double ty = y - fy;
    const auto tap = [&](long long px, long long py) {
        return static_cast<double>(
            plane[static_cast<std::size_t>(py) * w + static_cast<std::size_t>(px)]);
    };
    const double top = tap(x0, y0) * (1.0 - tx) + tap(x1, y0) * tx;
    const double bottom = tap(x0, y1) * (1.0 - tx) + tap(x1, y1) * tx;
    out = top * (1.0 - ty) + bottom * ty;
    return true;
}

// Nearest-neighbour sample of an AOV channel. **Nearest and not bilinear, on purpose**: averaging
// two identifiers is a third object and averaging two depths across a silhouette is a surface that
// is not there. That is ADR-242's reason for refusing a supersampled AOV, and it applies just as
// exactly to a warp lookup.
bool sampleNearest(const Plane& plane, double x, double y, int channel, double& out) {
    const auto px = static_cast<long long>(std::llround(x));
    const auto py = static_cast<long long>(std::llround(y));
    if (px < 0 || py < 0 || px >= static_cast<long long>(plane.width) ||
        py >= static_cast<long long>(plane.height)) {
        return false;
    }
    out = static_cast<double>(plane.at(static_cast<std::uint32_t>(px),
                                       static_cast<std::uint32_t>(py))[channel]);
    return true;
}

} // namespace

AlternationStats temporalAlternation(const Frame& previous, const Frame& current, const Frame& next,
                                     double threshold) {
    AlternationStats stats;
    if (!previous.valid() || !current.valid() || !next.valid() ||
        !previous.sameShapeAs(current) || !current.sameShapeAs(next)) {
        return stats;
    }
    const std::vector<float> a = luma(previous);
    const std::vector<float> b = luma(current);
    const std::vector<float> c = luma(next);
    double total = 0.0;
    std::size_t exceeding = 0;
    for (std::size_t i = 0; i < b.size(); ++i) {
        const double second = std::abs(static_cast<double>(c[i]) - 2.0 * static_cast<double>(b[i]) +
                                       static_cast<double>(a[i]));
        total += second;
        stats.peak = std::max(stats.peak, second);
        if (second > threshold) {
            ++exceeding;
        }
    }
    const auto n = static_cast<double>(b.size());
    stats.mean = n > 0.0 ? total / n : 0.0;
    stats.exceedingFraction = n > 0.0 ? static_cast<double>(exceeding) / n : 0.0;
    return stats;
}

MotionResidual::Gated MotionResidual::over(const std::vector<std::uint8_t>& mask) const {
    Gated gated;
    if (mask.size() != valid.size() || valid.empty()) {
        return gated;
    }
    double total = 0.0;
    for (std::size_t i = 0; i < valid.size(); ++i) {
        if (valid[i] == 0 || mask[i] == 0) {
            continue;
        }
        total += static_cast<double>(residualMap[i]);
        ++gated.pixels;
    }
    gated.residual = gated.pixels > 0 ? total / static_cast<double>(gated.pixels) : 0.0;
    gated.coverage = static_cast<double>(gated.pixels) / static_cast<double>(valid.size());
    return gated;
}

MotionResidual motionCompensatedResidual(const MotionInputs& inputs,
                                         const DisocclusionPolicy& policy) {
    MotionResidual result;
    if (inputs.previous == nullptr || inputs.current == nullptr || inputs.velocity == nullptr) {
        return result;
    }
    const Frame& previous = *inputs.previous;
    const Frame& current = *inputs.current;
    const Plane& velocity = *inputs.velocity;
    if (!previous.valid() || !current.valid() || !velocity.valid() ||
        !previous.sameShapeAs(current) || velocity.width != current.width ||
        velocity.height != current.height) {
        return result;
    }

    const std::uint32_t w = current.width;
    const std::uint32_t h = current.height;
    result.width = w;
    result.height = h;
    const std::vector<float> lumaPrevious = luma(previous);
    const std::vector<float> lumaCurrent = luma(current);
    result.residualMap.assign(static_cast<std::size_t>(w) * h, 0.0f);
    result.valid.assign(static_cast<std::size_t>(w) * h, 0);

    const bool useId = policy.useIdentifier && inputs.idPrevious != nullptr &&
                       inputs.idCurrent != nullptr && inputs.idPrevious->valid() &&
                       inputs.idCurrent->valid() && inputs.idCurrent->width == w &&
                       inputs.idCurrent->height == h;
    const bool useDepth = policy.useDepth && inputs.depthPrevious != nullptr &&
                          inputs.depthCurrent != nullptr && inputs.depthPrevious->valid() &&
                          inputs.depthCurrent->valid() && inputs.depthCurrent->width == w &&
                          inputs.depthCurrent->height == h;

    std::vector<double> residuals;
    residuals.reserve(static_cast<std::size_t>(w) * h);
    std::size_t offFrame = 0;
    std::size_t byId = 0;
    std::size_t byDepth = 0;
    double total = 0.0;

    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * w + x;
            const float* v = velocity.at(x, y);
            // `screenVelocityAt` writes `nowUv - beforeUv`, so the pixel's previous position is
            // its own UV minus the velocity. Pixel centres, hence the +0.5/-0.5 round trip.
            const double sourceX = (static_cast<double>(x) + 0.5) -
                                   static_cast<double>(v[0]) * static_cast<double>(w) - 0.5;
            const double sourceY = (static_cast<double>(y) + 0.5) -
                                   static_cast<double>(v[1]) * static_cast<double>(h) - 0.5;

            double predicted = 0.0;
            if (!sampleBilinear(lumaPrevious, w, h, sourceX, sourceY, predicted)) {
                ++offFrame;
                continue;
            }
            if (useId) {
                double thereId = 0.0;
                double hereId = 0.0;
                if (!sampleNearest(*inputs.idPrevious, sourceX, sourceY, 0, thereId) ||
                    !sampleNearest(*inputs.idCurrent, static_cast<double>(x),
                                   static_cast<double>(y), 0, hereId)) {
                    ++offFrame;
                    continue;
                }
                if (thereId != hereId) {
                    ++byId;
                    continue;
                }
            }
            if (useDepth) {
                double thereDepth = 0.0;
                double hereDepth = 0.0;
                if (!sampleNearest(*inputs.depthPrevious, sourceX, sourceY, 0, thereDepth) ||
                    !sampleNearest(*inputs.depthCurrent, static_cast<double>(x),
                                   static_cast<double>(y), 0, hereDepth)) {
                    ++offFrame;
                    continue;
                }
                // Relative to the pixel's own depth, because a 5 cm disagreement at 2 m is a
                // silhouette and at 200 m is precision.
                const double scale = std::max(std::abs(hereDepth), 1e-4);
                if (std::abs(hereDepth - thereDepth) / scale > policy.depthRelativeThreshold) {
                    ++byDepth;
                    continue;
                }
            }
            const double residual = std::abs(static_cast<double>(lumaCurrent[i]) - predicted);
            result.residualMap[i] = static_cast<float>(residual);
            result.valid[i] = 1;
            residuals.push_back(residual);
            total += residual;
        }
    }

    const auto n = static_cast<double>(static_cast<std::size_t>(w) * h);
    const auto validCount = static_cast<double>(residuals.size());
    result.residual = validCount > 0.0 ? total / validCount : 0.0;
    result.residualP95 = percentileOf(residuals, 0.95);
    result.offFrameFraction = static_cast<double>(offFrame) / n;
    result.identifierFraction = static_cast<double>(byId) / n;
    result.depthFraction = static_cast<double>(byDepth) / n;
    result.disocclusionFraction = 1.0 - validCount / n;
    result.validFraction = validCount / n;
    return result;
}

double warpFloor(const Frame& frame, const Plane& velocity) {
    if (!frame.valid() || !velocity.valid() || velocity.width != frame.width ||
        velocity.height != frame.height) {
        return 0.0;
    }
    const std::uint32_t w = frame.width;
    const std::uint32_t h = frame.height;
    const std::vector<float> source = luma(frame);
    // Warp back, then forward, and compare with the original. Two bilinear taps is the same number
    // of resamples the residual pays across one frame pair, and the renderer is not in the loop, so
    // whatever this reports is the instrument's own cost.
    std::vector<float> once(source.size(), 0.0f);
    std::vector<std::uint8_t> ok(source.size(), 0);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const float* v = velocity.at(x, y);
            double sampled = 0.0;
            if (sampleBilinear(source, w, h,
                               (static_cast<double>(x) + 0.5) -
                                   static_cast<double>(v[0]) * static_cast<double>(w) - 0.5,
                               (static_cast<double>(y) + 0.5) -
                                   static_cast<double>(v[1]) * static_cast<double>(h) - 0.5,
                               sampled)) {
                once[static_cast<std::size_t>(y) * w + x] = static_cast<float>(sampled);
                ok[static_cast<std::size_t>(y) * w + x] = 1;
            }
        }
    }
    double total = 0.0;
    std::size_t counted = 0;
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * w + x;
            if (ok[i] == 0) {
                continue;
            }
            const float* v = velocity.at(x, y);
            double back = 0.0;
            if (!sampleBilinear(once, w, h,
                                (static_cast<double>(x) + 0.5) +
                                    static_cast<double>(v[0]) * static_cast<double>(w) - 0.5,
                                (static_cast<double>(y) + 0.5) +
                                    static_cast<double>(v[1]) * static_cast<double>(h) - 0.5,
                                back)) {
                continue;
            }
            total += std::abs(static_cast<double>(source[i]) - back);
            ++counted;
        }
    }
    return counted > 0 ? total / static_cast<double>(counted) : 0.0;
}

} // namespace avgen::quality
