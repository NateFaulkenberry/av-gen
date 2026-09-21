#include "scene/motion_quality_report.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace avgen::scene {

std::string MotionQualitySummary::humanReadable() const {
    std::string out = fmt::format("motion database quality: {} samples x {} dimensions\n", samples,
                                  dimension);
    out += fmt::format("  duplicates            {} ({:.2f}%)\n", duplicates,
                       100.0f * duplicateFraction);
    out += fmt::format("  nearest neighbour     mean {:.4f}, max {:.4f} (normalised units)\n",
                       meanNearestNeighbour, maxNearestNeighbour);
    out += fmt::format("  dead dimensions       {} of {}\n", deadDimensions, dimension);
    out += fmt::format("  matcher resolution    {:.3f} m/s of speed changes its answer\n",
                       speedResolution);
    out += fmt::format("  speed x turn cells    {} of {} occupied\n", jointOccupancy, jointCells);
    out += "  NOT covered here: foot sliding, contact quality and joint limits need a pose, which\n"
           "  a feature vector does not contain. Those are Phase B's measureMotionQuality, run at\n"
           "  pack build time on the clip and the skeleton.\n";
    return out;
}

std::string MotionQualitySummary::json() const {
    return fmt::format(
        "{{\"samples\":{},\"dimension\":{},\"duplicates\":{},\"duplicateFraction\":{:.6f},"
        "\"meanNearestNeighbour\":{:.6f},\"maxNearestNeighbour\":{:.6f},\"deadDimensions\":{},"
        "\"speedResolution\":{:.6f},\"jointOccupancy\":{},\"jointCells\":{}}}",
        samples, dimension, duplicates, duplicateFraction, meanNearestNeighbour,
        maxNearestNeighbour, deadDimensions, speedResolution, jointOccupancy, jointCells);
}

MotionQualitySummary analyseMotionQuality(const MotionDatabase& db,
                                          const MotionQualitySummaryOptions& options) {
    MotionQualitySummary out;
    out.samples = db.sampleCount();
    out.dimension = db.dimension;
    out.deadDimensions = db.stats.deadDimensions;
    if (db.sampleCount() < 2u || db.dimension == 0u) {
        return out;
    }

    const std::uint32_t stride =
        db.sampleCount() > options.exactBelow ? db.sampleCount() / options.exactBelow : 1u;
    double total = 0.0;
    int counted = 0;
    for (std::uint32_t a = 0; a < db.sampleCount(); a += stride) {
        const float* fa = db.featuresFor(a);
        float nearest = std::numeric_limits<float>::max();
        for (std::uint32_t b = 0; b < db.sampleCount(); ++b) {
            if (b == a) {
                continue;
            }
            // **Adjacent samples of the same clip are not duplicates**, they are consecutive
            // frames, and counting them would report a duplicate rate that rises with the sample
            // rate -- a number that measures the sampler rather than the corpus.
            if (db.sampleClip[b] == db.sampleClip[a] &&
                std::abs(db.sampleTime[b] - db.sampleTime[a]) < 0.1f) {
                continue;
            }
            const float* fb = db.featuresFor(b);
            float d = 0.0f;
            for (std::size_t k = 0; k < db.dimension; ++k) {
                const float delta = fa[k] - fb[k];
                d += delta * delta;
                if (d >= nearest) {
                    break;
                }
            }
            nearest = std::min(nearest, d);
        }
        if (nearest == std::numeric_limits<float>::max()) {
            continue;
        }
        const float distance = std::sqrt(nearest);
        total += distance;
        out.maxNearestNeighbour = std::max(out.maxNearestNeighbour, distance);
        if (distance < options.duplicateEpsilon) {
            ++out.duplicates;
        }
        ++counted;
    }
    if (counted > 0) {
        out.meanNearestNeighbour = static_cast<float>(total / counted);
        out.duplicateFraction = static_cast<float>(out.duplicates) / static_cast<float>(counted);
    }

    // Cited rather than recomputed, so the report and the coverage analyzer cannot disagree about
    // the same corpus -- §13's lesson, one section later.
    const MotionCoverageReport coverage = measureMotionCoverage(db);
    out.speedResolution = coverage.speedResolution;
    out.jointOccupancy = coverage.jointOccupancy;
    out.jointCells = coverage.jointCells;
    return out;
}

} // namespace avgen::scene
