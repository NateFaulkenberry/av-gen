#include "scene/motion_coverage.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

namespace avgen::scene {

namespace {

// How each sample is moving: speed, direction relative to the body's facing, and turn rate.
//
// **From the trajectory, not the root velocity, whenever the database has one.** The root velocity
// is one frame's difference and carries every sway of the pelvis. On the scout that sway alone
// produced 52 "starts" and 53 "stops" in clips that never start or stop (agent/anim-cinfra's
// finding, seen again from §21's side). The trajectory block says where the body will be at each
// horizon, so speed and direction read off the furthest horizon are a mean over it. And with every
// feature in the body's frame (§7), the turn rate is the path's own curvature: the direction of its
// last segment against its first, over the time between them. A root-velocity heading difference
// stopped measuring turning the moment the features began turning with the body.
struct SampleMotion {
    std::vector<float> speed;
    std::vector<float> heading;
    std::vector<float> turn;        // rad/s, positive left
    std::vector<bool> turnKnown;
    bool fromTrajectory = false;
};

SampleMotion sampleMotion(const MotionDatabase& db) {
    SampleMotion out;
    const std::uint32_t n = db.sampleCount();
    out.speed.assign(n, 0.0f);
    out.heading.assign(n, 0.0f);
    out.turn.assign(n, 0.0f);
    out.turnKnown.assign(n, false);
    std::vector<MotionFeatureGroup> layout;
    motionFeatureLayoutInto(db.config, layout);
    std::size_t tp = layout.size();
    std::size_t rv = layout.size();
    for (std::size_t d = 0; d < layout.size(); ++d) {
        if (tp == layout.size() && layout[d] == MotionFeatureGroup::TrajectoryPosition) {
            tp = d;
        }
        if (rv == layout.size() && layout[d] == MotionFeatureGroup::RootVelocity) {
            rv = d;
        }
    }
    const auto raw = [&](const float* f, std::size_t d) {
        return db.scale[d] != 0.0f ? (f[d] / db.scale[d]) + db.mean[d] : f[d];
    };
    const auto wrap = [](float a) {
        while (a > 3.14159265f) { a -= 6.28318531f; }
        while (a < -3.14159265f) { a += 6.28318531f; }
        return a;
    };
    const std::vector<float>& horizons = db.config.trajectoryTimes;
    if (tp < layout.size() && !horizons.empty() && horizons.back() > 0.0f) {
        out.fromTrajectory = true;
        const std::size_t last = horizons.size() - 1;
        for (std::uint32_t s = 0; s < n; ++s) {
            const float* f = db.featuresFor(s);
            const auto at = [&](std::size_t k) {
                return glm::vec2(raw(f, tp + (k * 4u)), raw(f, tp + (k * 4u) + 1u));
            };
            const glm::vec2 end = at(last);
            out.speed[s] = glm::length(end) / horizons[last];
            out.heading[s] = std::atan2(end.x, end.y);
            if (last >= 1) {
                const glm::vec2 first = at(0);
                const glm::vec2 tail = end - at(last - 1);
                if (glm::length(first) > 1e-4f && glm::length(tail) > 1e-4f) {
                    const float span = (0.5f * (horizons[last] + horizons[last - 1])) - (0.5f * horizons[0]);
                    out.turn[s] = wrap(std::atan2(tail.x, tail.y) - std::atan2(first.x, first.y)) / std::max(span, 1e-3f);
                    out.turnKnown[s] = true;
                }
            }
        }
        return out;
    }
    if (rv + 2u >= db.dimension) {
        return out;
    }
    for (std::uint32_t s = 0; s < n; ++s) {
        const float* f = db.featuresFor(s);
        const float x = raw(f, rv);
        const float z = raw(f, rv + 2u);
        out.speed[s] = std::sqrt((x * x) + (z * z));
        out.heading[s] = std::atan2(x, z);
        if (s > 0 && db.sampleClip[s] == db.sampleClip[s - 1u]) {
            const float dt = std::max(db.sampleTime[s] - db.sampleTime[s - 1u], 1e-4f);
            out.turn[s] = wrap(out.heading[s] - out.heading[s - 1u]) / dt;
            out.turnKnown[s] = true;
        }
    }
    return out;
}

} // namespace

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
        "coverage over {} samples (per-axis figures are a POPULATED-NESS check, not coverage: a "
        "fine-binned marginal reports sampling sparsity and a coarse one cannot report a gap at "
        "all)\n",
        samples);
    if (resolutionDerived) {
        out += fmt::format("  bin width derived from THIS matcher: {:.3f} m/s of speed changes its "
                           "answer, with the weights this database carries\n",
                           speedResolution);
    }
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

namespace {

// How much the requested forward speed must change before the search returns a different sample,
// with the weights this database currently carries. Probed rather than assumed, and probed sparsely
// -- a few dozen samples is enough for a width, and this runs on every analysis.
float deriveSpeedResolution(const MotionDatabase& db, std::size_t forward) {
    if (forward >= db.dimension || db.sampleCount() == 0) {
        return 0.0f;
    }
    const MotionCostWeights weights;
    double total = 0.0;
    int measured = 0;
    const std::uint32_t stride = std::max(db.sampleCount() / 32u, 1u);
    for (std::uint32_t s = 0; s < db.sampleCount(); s += stride) {
        MotionQuery query;
        query.features.assign(db.dimension, 0.0f);
        const float* f = db.featuresFor(s);
        std::copy(f, f + db.dimension, query.features.begin());
        const MotionMatch baseline = searchMotion(db, query, weights);
        if (!baseline.found()) {
            continue;
        }
        for (int step = 1; step <= 300; ++step) {
            const float metres = static_cast<float>(step) * 0.01f;
            query.features[forward] = f[forward] + metres * db.scale[forward];
            const MotionMatch moved = searchMotion(db, query, weights);
            if (moved.found() && moved.sample != baseline.sample) {
                total += metres;
                ++measured;
                break;
            }
        }
    }
    return measured > 0 ? static_cast<float>(total / measured) : 0.0f;
}

} // namespace

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

    // Speed, direction and turn from the trajectory when there is one (see `sampleMotion`).
    const SampleMotion motion = sampleMotion(db);
    for (std::uint32_t s = 0; s < db.sampleCount(); ++s) {
        const float planar = motion.speed[s];
        speed.push_back(planar);
        direction.push_back(motion.heading[s]);
        phase.push_back(db.samplePhase[s]);
        const std::uint32_t tags = db.sampleTags[s];
        mode.push_back(static_cast<float>(
            (tags & static_cast<std::uint32_t>(MotionTag::Run)) != 0u      ? 2
            : (tags & static_cast<std::uint32_t>(MotionTag::Walk)) != 0u   ? 1
            : (tags & static_cast<std::uint32_t>(MotionTag::Turn)) != 0u   ? 3
                                                                           : 0));
        // Acceleration is a difference along a clip, so it exists only where the previous sample
        // is in the same clip. Turn rate comes with the sample when the trajectory gives it.
        if (s > 0 && db.sampleClip[s] == db.sampleClip[s - 1u]) {
            const float dt = std::max(db.sampleTime[s] - db.sampleTime[s - 1u], 1e-4f);
            acceleration.push_back((planar - speed[s - 1u]) / dt);
        }
        if (motion.turnKnown[s]) {
            turn.push_back(motion.turn[s]);
        }
    }

    // Derive the bin count from the matcher's own resolution unless the caller pinned one.
    std::uint32_t bins = options.bins;
    if (bins == 0u) {
        out.speedResolution = deriveSpeedResolution(db, rootVelocity + 2u);
        out.resolutionDerived = out.speedResolution > 0.0f;
        bins = out.resolutionDerived
                   ? static_cast<std::uint32_t>(std::max(
                         1.0f, std::round(options.maxSpeed / out.speedResolution)))
                   : 8u;
    }

    out.axes.push_back(binAxis(CoverageAxis::Speed, speed, bins, 0.0f, options.maxSpeed));
    out.axes.push_back(
        binAxis(CoverageAxis::Direction, direction, bins, -3.14159265f, 3.14159265f));
    out.axes.push_back(binAxis(CoverageAxis::Acceleration, acceleration, bins,
                               -options.maxAcceleration, options.maxAcceleration));
    out.axes.push_back(
        binAxis(CoverageAxis::TurnRate, turn, bins, -options.maxTurnRate, options.maxTurnRate));
    out.axes.push_back(binAxis(CoverageAxis::Phase, phase, bins, 0.0f, 1.0f));
    out.axes.push_back(binAxis(CoverageAxis::LocomotionMode, mode, 4u, 0.0f, 4.0f));

    // The one pairing where a gap means something concrete -- a fast turn -- reported as a count,
    // because a joint-occupancy percentage over six axes is the trap this analyzer exists to avoid.
    const std::uint32_t grid = std::max(bins, 1u);
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

// ---- §58 ------------------------------------------------------------------------------------------

namespace avgen::scene {

const char* motionCategoryName(MotionCategory category) {
    switch (category) {
    case MotionCategory::Idle: return "idle";
    case MotionCategory::Walk: return "walk";
    case MotionCategory::Run: return "run";
    case MotionCategory::Strafe: return "strafe";
    case MotionCategory::Reverse: return "reverse locomotion";
    case MotionCategory::LeftTurn: return "left turn";
    case MotionCategory::RightTurn: return "right turn";
    case MotionCategory::FastLeftTurn: return "high-speed left turn";
    case MotionCategory::FastRightTurn: return "high-speed right turn";
    case MotionCategory::Start: return "start transitions";
    case MotionCategory::Stop: return "stop transitions";
    case MotionCategory::Count: break;
    }
    return "?";
}

const char* coverageGradeName(CoverageGrade grade) {
    switch (grade) {
    case CoverageGrade::Poor: return "poor";
    case CoverageGrade::Limited: return "limited";
    case CoverageGrade::Moderate: return "moderate";
    case CoverageGrade::Good: return "good";
    }
    return "?";
}

namespace {

CoverageGrade gradeOf(std::uint32_t count, std::uint32_t clips, std::uint32_t moderateAt,
                      std::uint32_t goodAt, std::uint32_t goodClips) {
    if (count == 0) {
        return CoverageGrade::Poor;
    }
    if (count >= goodAt && clips >= goodClips) {
        return CoverageGrade::Good;
    }
    // A category from one clip is capped at moderate however long it is: every search into it
    // lands in the same take.
    if (count >= moderateAt) {
        return CoverageGrade::Moderate;
    }
    return CoverageGrade::Limited;
}

} // namespace

std::string MotionCategoryReport::report() const {
    std::string out = fmt::format(
        "motion coverage by category, {} samples at {:.1f} Hz (measured from root velocity, not from "
        "clip names)\n",
        samples, sampleRate);
    out += fmt::format(
        "  thresholds: idle < {:.2f} m/s <= walk < {:.2f} m/s <= run; turn > {:.2f} rad/s; one "
        "window = {:.2f} s (one matcher commitment)\n",
        options.idleSpeed, options.runSpeed, options.turnRate, options.commitmentSeconds);
    out += fmt::format(
        "  grades: poor = none; limited = at least 1; moderate = {}+ windows ({}+ events); good = "
        "{}+ windows ({}+ events) from {}+ clips\n",
        options.moderateWindows, options.moderateEvents, options.goodWindows, options.goodEvents,
        options.goodClips);
    for (const MotionCategoryCoverage& c : categories) {
        if (c.event) {
            out += fmt::format("  {:<22} {:<8} {:>4} events from {} clip(s)", motionCategoryName(c.category),
                               coverageGradeName(c.grade), c.windows, c.clips);
        } else {
            out += fmt::format("  {:<22} {:<8} {:>4} windows ({:6.2f} s) from {} clip(s)",
                               motionCategoryName(c.category), coverageGradeName(c.grade), c.windows,
                               c.seconds, c.clips);
        }
        if (c.taggedSeconds >= 0.0f) {
            out += fmt::format("   [tagged {:.2f} s, of which {:.2f} s measured {}]", c.taggedSeconds,
                               c.taggedAgreeing, motionCategoryName(c.category));
            // The disagreement is the finding: content named for this category that the matcher
            // cannot reach by the quantity it searches on.
            if (c.taggedSeconds >= options.commitmentSeconds && c.taggedAgreeing < 0.5f * c.taggedSeconds) {
                out += "  <-- most tagged content is not this by root velocity (in place?)";
            }
        }
        out += "\n";
    }
    return out;
}

MotionCategoryReport measureMotionCategories(const MotionDatabase& db,
                                             const MotionCategoryOptions& options) {
    MotionCategoryReport out;
    out.options = options;
    out.samples = db.sampleCount();
    const auto count = static_cast<std::size_t>(MotionCategory::Count);
    out.categories.resize(count);
    for (std::size_t c = 0; c < count; ++c) {
        out.categories[c].category = static_cast<MotionCategory>(c);
        out.categories[c].event =
            c == static_cast<std::size_t>(MotionCategory::Start) ||
            c == static_cast<std::size_t>(MotionCategory::Stop);
    }
    if (db.sampleCount() == 0 || db.dimension == 0) {
        return out;
    }

    // The sample rate the database was built at; for a database that never recorded one, the
    // spacing of its first two same-clip samples.
    float rate = db.build.sampleRate;
    if (rate <= 0.0f) {
        for (std::uint32_t s = 1; s < db.sampleCount(); ++s) {
            if (db.sampleClip[s] == db.sampleClip[s - 1u] && db.sampleTime[s] > db.sampleTime[s - 1u]) {
                rate = 1.0f / (db.sampleTime[s] - db.sampleTime[s - 1u]);
                break;
            }
        }
    }
    rate = rate > 0.0f ? rate : 30.0f;
    out.sampleRate = rate;
    const float secondsPerSample = 1.0f / rate;

    std::vector<MotionFeatureGroup> layout;
    motionFeatureLayoutInto(db.config, layout);
    std::size_t rv = layout.size();
    for (std::size_t d = 0; d < layout.size(); ++d) {
        if (layout[d] == MotionFeatureGroup::RootVelocity) {
            rv = d;
            break;
        }
    }
    if (rv + 2u >= db.dimension) {
        return out;
    }

    const SampleMotion motion = sampleMotion(db);
    const std::vector<float>& speed = motion.speed;
    const std::vector<float>& heading = motion.heading;

    std::vector<std::vector<bool>> clipsIn(count, std::vector<bool>(db.clipNames.size() + 1u, false));
    const auto add = [&](MotionCategory c, std::uint32_t s) {
        MotionCategoryCoverage& cov = out.categories[static_cast<std::size_t>(c)];
        ++cov.samples;
        const std::uint32_t clip = std::min<std::uint32_t>(db.sampleClip[s],
                                                           static_cast<std::uint32_t>(db.clipNames.size()));
        clipsIn[static_cast<std::size_t>(c)][clip] = true;
    };

    // Every sample is counted, the last of each clip included. This report used to exclude clip-final
    // samples because the builder clamped its look ahead at the clip's end and they all read zero
    // velocity. Feature extraction version 2 fixed that at the source, and an exclusion kept after
    // the fix would drop real motion.
    constexpr float kQuarter = 0.785398163f;
    // Each sample's speed-and-direction category, for the per-sample tag agreement below.
    std::vector<MotionCategory> primary(db.sampleCount(), MotionCategory::Count);
    for (std::uint32_t s = 0; s < db.sampleCount(); ++s) {
        const bool sameClip = s > 0 && db.sampleClip[s] == db.sampleClip[s - 1u];
        if (speed[s] < options.idleSpeed) {
            primary[s] = MotionCategory::Idle;
        } else {
            const float off = std::abs(heading[s]);
            if (off <= kQuarter) {
                primary[s] = speed[s] >= options.runSpeed ? MotionCategory::Run : MotionCategory::Walk;
            } else if (off <= 3.0f * kQuarter) {
                primary[s] = MotionCategory::Strafe;
            } else {
                primary[s] = MotionCategory::Reverse;
            }
        }
        add(primary[s], s);
        if (speed[s] >= options.idleSpeed && motion.turnKnown[s]) {
            const float turn = motion.turn[s];
            const bool fast = speed[s] >= options.runSpeed;
            if (turn > options.turnRate) {
                add(fast ? MotionCategory::FastLeftTurn : MotionCategory::LeftTurn, s);
                if (fast) { add(MotionCategory::LeftTurn, s); }
            } else if (turn < -options.turnRate) {
                add(fast ? MotionCategory::FastRightTurn : MotionCategory::RightTurn, s);
                if (fast) { add(MotionCategory::RightTurn, s); }
            }
        }
        if (sameClip) {
            const bool was = speed[s - 1u] >= options.idleSpeed;
            const bool is = speed[s] >= options.idleSpeed;
            if (!was && is) {
                add(MotionCategory::Start, s);
            } else if (was && !is) {
                add(MotionCategory::Stop, s);
            }
        }
    }

    const auto crossCheck = [&](MotionCategory category, MotionTag tag) {
        std::uint32_t tagged = 0;
        std::uint32_t agreeing = 0;
        for (std::uint32_t s = 0; s < db.sampleCount(); ++s) {
            if ((db.sampleTags[s] & static_cast<std::uint32_t>(tag)) == 0u) {
                continue;
            }
            ++tagged;
            agreeing += primary[s] == category ? 1u : 0u;
        }
        MotionCategoryCoverage& cov = out.categories[static_cast<std::size_t>(category)];
        cov.taggedSeconds = static_cast<float>(tagged) * secondsPerSample;
        cov.taggedAgreeing = static_cast<float>(agreeing) * secondsPerSample;
    };

    for (std::size_t c = 0; c < count; ++c) {
        MotionCategoryCoverage& cov = out.categories[c];
        cov.clips = static_cast<std::uint32_t>(
            std::count(clipsIn[c].begin(), clipsIn[c].end(), true));
        if (cov.event) {
            cov.seconds = 0.0f;
            cov.windows = cov.samples;
            cov.grade = gradeOf(cov.windows, cov.clips, options.moderateEvents, options.goodEvents,
                                options.goodClips);
        } else {
            cov.seconds = static_cast<float>(cov.samples) * secondsPerSample;
            cov.windows = static_cast<std::uint32_t>(
                std::floor(cov.seconds / std::max(options.commitmentSeconds, 1e-3f) + 1e-4f));
            cov.grade = gradeOf(cov.windows, cov.clips, options.moderateWindows, options.goodWindows,
                                options.goodClips);
        }
    }
    crossCheck(MotionCategory::Idle, MotionTag::Idle);
    crossCheck(MotionCategory::Walk, MotionTag::Walk);
    crossCheck(MotionCategory::Run, MotionTag::Run);
    return out;
}

} // namespace avgen::scene
