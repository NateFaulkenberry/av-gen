#include "scene/motion_coverage.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

namespace avgen::scene {

const char* coverageAxisName(CoverageAxis axis) {
    switch (axis) {
    case CoverageAxis::Speed: return "speed";
    case CoverageAxis::Direction: return "direction";
    case CoverageAxis::Acceleration: return "acceleration";
    case CoverageAxis::TurnRate: return "turnRate";
    case CoverageAxis::Phase: return "phase";
    case CoverageAxis::LocomotionMode: return "locomotionMode";
    case CoverageAxis::Count: break;
    }
    return "?";
}

namespace {

// One axis, binned over [low, high). Values outside are clamped into the end bins rather than
// dropped: a corpus containing something faster than the axis's top is a fact about the corpus, and
// silently discarding it would make the fastest bin look empty.
AxisCoverage binAxis(CoverageAxis axis, const std::vector<float>& values, std::uint32_t bins,
                     float low, float high) {
    AxisCoverage out;
    out.axis = axis;
    out.bins = std::max(bins, 1u);
    out.low = low;
    out.high = high;
    std::vector<bool> hit(out.bins, false);
    const float span = high > low ? high - low : 1.0f;
    for (const float v : values) {
        const float t = std::clamp((v - low) / span, 0.0f, 0.9999f);
        hit[static_cast<std::size_t>(t * static_cast<float>(out.bins))] = true;
    }
    for (std::uint32_t b = 0; b < out.bins; ++b) {
        if (hit[b]) {
            ++out.occupied;
        } else {
            const float binLow = low + span * static_cast<float>(b) / static_cast<float>(out.bins);
            const float binHigh =
                low + span * static_cast<float>(b + 1u) / static_cast<float>(out.bins);
            out.gaps.emplace_back(binLow, binHigh);
        }
    }
    return out;
}

} // namespace

std::string MotionCoverageReport::report() const {
    std::string out = fmt::format(
        "coverage over {} samples (per-axis figures are a POPULATED-NESS check, not coverage: the "
        "matcher's own resolution is ~1.08 m/s of speed, so a fine-binned marginal reports "
        "sampling sparsity and a coarse one cannot report a gap at all)\n",
        samples);
    for (const AxisCoverage& axis : axes) {
        out += fmt::format("  {:<15} {:>3}/{:<3} bins populated", coverageAxisName(axis.axis),
                           axis.occupied, axis.bins);
        if (axis.gaps.empty()) {
            out += "  no gaps\n";
        } else {
            out += fmt::format("  gaps:");
            for (std::size_t i = 0; i < axis.gaps.size() && i < 4; ++i) {
                out += fmt::format(" [{:.2f},{:.2f})", axis.gaps[i].first, axis.gaps[i].second);
            }
            if (axis.gaps.size() > 4) {
                out += fmt::format(" +{} more", axis.gaps.size() - 4);
            }
            out += "\n";
        }
    }
    out += fmt::format("  (speed x turn): {} of {} cells occupied  <-- the informative measure\n",
                       jointOccupancy, jointCells);
    return out;
}

MotionCoverageReport measureMotionCoverage(const MotionDatabase& db,
                                           const MotionCoverageOptions& options) {
    MotionCoverageReport out;
    out.samples = db.sampleCount();
    if (db.sampleCount() == 0 || db.dimension == 0) {
        return out;
    }

    // The root-velocity triple is the last three dimensions before phase and contacts; the layout
    // is stated once in `motionFeatureLayout` and read here rather than recomputed, for the reason
    // §13 just paid for.
    const std::vector<MotionFeatureGroup> layout = motionFeatureLayout(db.config);
    std::size_t rootVelocity = layout.size();
    for (std::size_t d = 0; d < layout.size(); ++d) {
        if (layout[d] == MotionFeatureGroup::RootVelocity) {
            rootVelocity = d;
            break;
        }
    }

    std::vector<float> speed;
    std::vector<float> direction;
    std::vector<float> acceleration;
    std::vector<float> turn;
    std::vector<float> phase;
    std::vector<float> mode;
    speed.reserve(db.sampleCount());
    std::vector<float> previousSpeed;

    for (std::uint32_t s = 0; s < db.sampleCount(); ++s) {
        const float* f = db.featuresFor(s);
        glm::vec3 v{0.0f};
        if (rootVelocity + 2u < db.dimension) {
            // Features are normalised; undo it so the axes are in metres per second and a gap can
            // be reported in units a person can act on.
            const auto raw = [&](std::size_t d) {
                return db.scale[d] != 0.0f ? (f[d] / db.scale[d]) + db.mean[d] : f[d];
            };
            v = glm::vec3(raw(rootVelocity), raw(rootVelocity + 1u), raw(rootVelocity + 2u));
        }
        const float planar = std::sqrt((v.x * v.x) + (v.z * v.z));
        speed.push_back(planar);
        direction.push_back(std::atan2(v.x, v.z));
        phase.push_back(db.samplePhase[s]);
        const std::uint32_t tags = db.sampleTags[s];
        mode.push_back(static_cast<float>(
            (tags & static_cast<std::uint32_t>(MotionTag::Run)) != 0u      ? 2
            : (tags & static_cast<std::uint32_t>(MotionTag::Walk)) != 0u   ? 1
            : (tags & static_cast<std::uint32_t>(MotionTag::Turn)) != 0u   ? 3
                                                                           : 0));
        // Acceleration and turn rate are differences along a clip, so they exist only where the
        // previous sample is in the same clip -- at a clip boundary there is no previous frame and
        // inventing one would put a fictitious spike in the corpus's own statistics.
        if (s > 0 && db.sampleClip[s] == db.sampleClip[s - 1u]) {
            const float dt = std::max(db.sampleTime[s] - db.sampleTime[s - 1u], 1e-4f);
            acceleration.push_back((planar - speed[s - 1u]) / dt);
            float dTheta = direction[s] - direction[s - 1u];
            while (dTheta > 3.14159265f) { dTheta -= 6.28318531f; }
            while (dTheta < -3.14159265f) { dTheta += 6.28318531f; }
            turn.push_back(dTheta / dt);
        }
    }

    out.axes.push_back(binAxis(CoverageAxis::Speed, speed, options.bins, 0.0f, options.maxSpeed));
    out.axes.push_back(
        binAxis(CoverageAxis::Direction, direction, options.bins, -3.14159265f, 3.14159265f));
    out.axes.push_back(binAxis(CoverageAxis::Acceleration, acceleration, options.bins,
                               -options.maxAcceleration, options.maxAcceleration));
    out.axes.push_back(
        binAxis(CoverageAxis::TurnRate, turn, options.bins, -options.maxTurnRate, options.maxTurnRate));
    out.axes.push_back(binAxis(CoverageAxis::Phase, phase, options.bins, 0.0f, 1.0f));
    out.axes.push_back(binAxis(CoverageAxis::LocomotionMode, mode, 4u, 0.0f, 4.0f));

    // The one pairing where a gap means something concrete -- a fast turn -- reported as a count,
    // because a joint-occupancy percentage over six axes is the trap this analyzer exists to avoid.
    const std::uint32_t grid = std::max(options.bins, 1u);
    out.jointCells = grid * grid;
    std::vector<bool> cells(out.jointCells, false);
    for (std::size_t i = 0; i < turn.size() && i + 1 < speed.size(); ++i) {
        const float sT = std::clamp(speed[i + 1] / std::max(options.maxSpeed, 1e-4f), 0.0f, 0.9999f);
        const float tT = std::clamp((turn[i] + options.maxTurnRate) / (2.0f * options.maxTurnRate),
                                    0.0f, 0.9999f);
        cells[static_cast<std::size_t>(sT * grid) * grid +
              static_cast<std::size_t>(tT * grid)] = true;
    }
    out.jointOccupancy = static_cast<std::uint32_t>(std::count(cells.begin(), cells.end(), true));
    return out;
}

} // namespace avgen::scene
