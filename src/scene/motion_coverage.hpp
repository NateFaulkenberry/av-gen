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
    // The speed change needed before the search returns a different sample, measured on this
    // database with its current weights. The bin count is derived from it when `bins` is 0.
    float speedResolution = 0.0f;
    bool resolutionDerived = false;
    [[nodiscard]] std::string report() const;
};

struct MotionCoverageOptions {
    // **0 means "derive it from the matcher in hand", and that is the default for a reason.**
    //
    // The bin width that makes a gap meaningful is the difference the matcher can act on, and that
    // is a property of the **weight vector**, not a constant. Phase C §10 made those weights
    // actually do something for the first time -- before that they were read by nothing -- so the
    // first person to tune them would silently invalidate any stored resolution. That is ADR-389:
    // a coefficient tuned against a quantity is invalidated by a change to that quantity's
    // distribution, and a stored constant with no check is the thing that goes stale.
    //
    // Deriving it per run costs a few dozen probe searches, which is nothing beside building the
    // database, and it makes the resolution a **reported property of this run** rather than a fixed
    // number -- which is more honest anyway: it tells the reader how finely this matcher can be
    // interrogated today.
    std::uint32_t bins = 0;
    float maxSpeed = 3.0f;       // m/s; the top of the speed axis
    float maxAcceleration = 6.0f;// m/s^2
    float maxTurnRate = 3.0f;    // rad/s
};

// Reads the axes out of the database's own feature vectors and metadata, so the analyzer measures
// what the matcher will actually search rather than a second derivation of the source clips.
[[nodiscard]] MotionCoverageReport measureMotionCoverage(const MotionDatabase& db,
                                                         const MotionCoverageOptions& options = {});


// ---- §58: the coverage report in words, with the thresholds that produce the words -------------
//
// §58 asks for statements like "Walk: good coverage / Reverse locomotion: poor coverage", measured
// from the data, and forbids subjective labels without defined thresholds. So every category below
// is a **predicate over quantities the database already carries** -- the root velocity in its
// feature vectors, that velocity's change along a clip, the clip's tags -- and every grade is a
// **count compared with a stated number**, printed beside the verdict.
//
// **What a grade counts.** Not seconds, and not samples, because neither is what the matcher can
// use. The matcher commits to a choice for `minimumContinuation` (0.2 s by default, §29), so the
// unit of usable content is one commitment's worth: a category with 0.1 s of motion cannot be
// played even once without running out of it. `windows` is the category's seconds divided by that
// commitment; transitions (starts, stops) are counted as **events** instead, because one start is
// one entry point however many frames it spans. And a category that lives in one clip is graded
// no better than moderate however long it is: every search into it lands in the same take.
//
// **Categories are measured by what the body does, and cross-checked by what the clip is called.**
// A walk is a sample whose root moves forward at walking speed; the Walk *tag* is reported beside
// it. When the two disagree -- an in-place corpus (ADR-540) tags walks the matcher can never find
// by speed -- the report says so, because that disagreement is the finding.
enum class MotionCategory : std::uint8_t {
    Idle,          // planar speed below `idleSpeed`
    Walk,          // forward, idleSpeed .. runSpeed
    Run,           // forward, at least runSpeed
    Strafe,        // moving, direction 45..135 degrees off forward
    Reverse,       // moving, direction more than 135 degrees off forward
    LeftTurn,      // moving, turning left faster than `turnRate`
    RightTurn,     // moving, turning right faster than `turnRate`
    FastLeftTurn,  // at least runSpeed, turning left faster than `turnRate`
    FastRightTurn, // at least runSpeed, turning right faster than `turnRate`
    Start,         // an event: speed rising through idleSpeed within a clip
    Stop,          // an event: speed falling through idleSpeed within a clip
    Count,
};
[[nodiscard]] const char* motionCategoryName(MotionCategory category);

enum class CoverageGrade : std::uint8_t { Poor, Limited, Moderate, Good };
[[nodiscard]] const char* coverageGradeName(CoverageGrade grade);

struct MotionCategoryOptions {
    // Speed bands, metres per second of planar root velocity.
    float idleSpeed = 0.2f;
    float runSpeed = 2.0f;
    // rad/s of change in the direction of travel. Positive is a LEFT turn: forward is +z, left is
    // +x, and a heading swinging from +z toward +x increases atan2(x, z).
    float turnRate = 1.0f;
    // One matcher commitment (`MatchSettings::minimumContinuation`), the unit of usable content.
    float commitmentSeconds = 0.2f;
    // Grade thresholds, in commitments for continuous categories and in events for transitions.
    // Limited = at least one, moderate = at least `moderateAt`, good = at least `goodAt` AND from
    // at least `goodClips` distinct clips.
    std::uint32_t moderateWindows = 5;
    std::uint32_t goodWindows = 15;
    std::uint32_t moderateEvents = 2;
    std::uint32_t goodEvents = 4;
    std::uint32_t goodClips = 2;
};

struct MotionCategoryCoverage {
    MotionCategory category = MotionCategory::Idle;
    bool event = false;           // counted in events, not seconds
    std::uint32_t samples = 0;    // samples in the category (for an event, samples AT the event)
    float seconds = 0.0f;
    std::uint32_t windows = 0;    // seconds / commitmentSeconds, or the event count
    std::uint32_t clips = 0;      // distinct clips contributing
    CoverageGrade grade = CoverageGrade::Poor;
    // The same category read from tags, in seconds, where a tag exists for it (-1 where none does).
    float taggedSeconds = -1.0f;
    // Of those tagged seconds, how many the root velocity ALSO puts in this category. Totals can
    // agree by coincidence -- root sway on an in-place idle can add up to as many "walking" seconds
    // as the walk clips hold -- so agreement is measured sample by sample.
    float taggedAgreeing = 0.0f;
};

struct MotionCategoryReport {
    std::vector<MotionCategoryCoverage> categories;
    MotionCategoryOptions options;
    float sampleRate = 0.0f;
    std::uint32_t samples = 0;
    // Samples left out because their root velocity is a build artefact (see the .cpp).
    std::uint32_t excludedClipFinal = 0;
    [[nodiscard]] const MotionCategoryCoverage& at(MotionCategory c) const {
        return categories[static_cast<std::size_t>(c)];
    }
    [[nodiscard]] std::string report() const;
};

[[nodiscard]] MotionCategoryReport measureMotionCategories(const MotionDatabase& db,
                                                           const MotionCategoryOptions& options = {});

} // namespace avgen::scene
