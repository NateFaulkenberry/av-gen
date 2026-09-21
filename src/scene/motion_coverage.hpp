#pragma once

// Phase C §22: an offline analyzer for what a motion corpus actually covers.
//
// §22 asks for "coverage across speed, direction, acceleration, turn rate, phase, locomotion mode.
// **Identify gaps.**" Phase B already shipped a coverage measure and it is one-dimensional -- it
// takes a list of target *speeds* and reports what fraction of them a variant set reaches. Six
// axes is a different instrument, and this is it.
//
// **The denominator is the design decision, and it is chosen before anything is measured** (§14's
// lesson, and ADR-609's). The obvious reading of "coverage over six axes" is a six-dimensional
// grid, and it is the wrong one: at eight bins per axis that is 262,144 cells, and a corpus of
// 1,738 samples can occupy at most 1,738 of them. Such a measure would report **under 0.7%
// coverage for any corpus of this size, however complete it was** -- it would be measuring the
// dimensionality, not the content, and every improvement to the corpus would move it by a rounding
// error.
//
// So coverage is reported **per axis, marginally**: each axis is binned on its own, and an axis's
// coverage is the fraction of its bins that any sample occupies. That answers the question §22
// actually asks -- "is there a speed we cannot do, a turn rate we have never seen" -- and it
// answers it with a denominator that is a property of the *question* rather than of the corpus's
// size. The joint distribution is not ignored: `jointOccupancy` reports how many distinct
// (speed, turn) pairs are occupied, which is the one pairing where a gap means something concrete
// (a fast turn), and it is reported as a count rather than dressed up as a percentage.

#include "scene/motion_database.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::scene {

enum class CoverageAxis : std::uint8_t {
    Speed,
    Direction,
    Acceleration,
    TurnRate,
    Phase,
    LocomotionMode,
    Count,
};
[[nodiscard]] const char* coverageAxisName(CoverageAxis axis);

struct AxisCoverage {
    CoverageAxis axis = CoverageAxis::Speed;
    std::uint32_t bins = 0;
    std::uint32_t occupied = 0;
    // The bins nothing reached, as [low, high) in the axis's own units. **This is the deliverable**:
    // §22 says "identify gaps", and a coverage percentage identifies nothing.
    std::vector<std::pair<float, float>> gaps;
    float low = 0.0f;
    float high = 0.0f;
    [[nodiscard]] float fraction() const {
        return bins > 0 ? static_cast<float>(occupied) / static_cast<float>(bins) : 0.0f;
    }
};

// **What the marginal report is, after calibration.** Measured on the real corpus: at 8 bins every
// axis reads 100% with no gaps, at 512 bins the six axes show 788 empty bins that are mostly
// sampling sparsity rather than absent motion, and the bin width the *matcher* can actually act on
// -- the speed change needed before the search returns a different sample -- is **1.083 m/s**,
// which justifies about three bins over a 0..3 m/s axis.
//
// So a marginal percentage here is not a coverage measure and must not be read as one. It is a
// **populated-ness check**: every axis has motion somewhere along it. `AxisCoverage::fraction` is
// kept because the gap list needs the bins, and `report()` labels it accordingly rather than
// printing a number that reads like coverage. The informative measure is `jointOccupancy`.
struct MotionCoverageReport {
    std::vector<AxisCoverage> axes;
    std::uint32_t samples = 0;
    // Distinct occupied (speed, turn-rate) cells, and how many that grid has. Reported as a count
    // because a joint-occupancy *percentage* is the trap this header exists to avoid.
    std::uint32_t jointOccupancy = 0;
    std::uint32_t jointCells = 0;
    [[nodiscard]] std::string report() const;
};

struct MotionCoverageOptions {
    std::uint32_t bins = 8;
    float maxSpeed = 3.0f;       // m/s; the top of the speed axis
    float maxAcceleration = 6.0f;// m/s^2
    float maxTurnRate = 3.0f;    // rad/s
};

// Reads the axes out of the database's own feature vectors and metadata, so the analyzer measures
// what the matcher will actually search rather than a second derivation of the source clips.
[[nodiscard]] MotionCoverageReport measureMotionCoverage(const MotionDatabase& db,
                                                         const MotionCoverageOptions& options = {});

} // namespace avgen::scene
