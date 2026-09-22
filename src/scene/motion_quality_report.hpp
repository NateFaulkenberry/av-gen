#pragma once

// Phase C §23: an offline quality report for a motion database.
//
// "Potential metrics: **duplicate sample percentage**, feature density, directional coverage, speed
// coverage, turn coverage, start coverage, stop coverage, contact quality, foot sliding,
// root-motion discontinuities, joint-limit violations. Output something **machine-readable and
// human-readable**."
//
// §23 is a list of candidates, not a specification, and the audit question is which of them this
// repository can answer **from the database** rather than by re-deriving from the source clips --
// because §13 has just shown what re-deriving costs when the inputs are not the ones the pack used.
// So the report is deliberately narrow:
//
//   * duplicates and feature density come from the feature vectors themselves;
//   * the coverage axes are already `scene::motion_coverage`'s job and are cited, not recomputed;
//   * foot sliding, contact quality and joint limits are **Phase B's** `measureMotionQuality`,
//     which works on a clip and a skeleton, and belong to the pack build rather than to the
//     database -- so they are named here as "measured elsewhere" rather than reimplemented against
//     a feature vector that does not contain a pose.
//
// Naming what a report does NOT cover, and where that thing is covered instead, is the part that
// stops the next person reimplementing foot slide against 33 floats that never contained a foot.

#include "scene/motion_coverage.hpp"
#include "scene/motion_database.hpp"

#include <cstdint>
#include <string>

namespace avgen::scene {

struct MotionQualitySummary {
    std::uint32_t samples = 0;
    std::uint32_t dimension = 0;

    // §23's first named metric. Two samples whose feature vectors are within `duplicateEpsilon`
    // are indistinguishable to the search: one of them can never be chosen, so it is memory and
    // scan time spent on an outcome that cannot happen.
    std::uint32_t duplicates = 0;
    float duplicateFraction = 0.0f;
    float duplicateRadius = 0.0f;   // the radius actually used
    bool radiusDerived = false;     // whether it was derived from this matcher or pinned

    // Feature density: how much of the normalised feature space the corpus actually occupies,
    // reported as the mean nearest-neighbour distance. Small means the corpus is tightly clustered
    // and the matcher is choosing between near-identical options; large means it is sparse and a
    // query will often be far from anything.
    float meanNearestNeighbour = 0.0f;
    float maxNearestNeighbour = 0.0f;

    // Dimensions whose standard deviation was too small to normalise. A dead dimension is carried,
    // scanned and weighted on every query and contributes nothing to any distance.
    std::uint32_t deadDimensions = 0;

    // Cited from `measureMotionCoverage` rather than recomputed, so the two cannot disagree.
    float speedResolution = 0.0f;
    std::uint32_t jointOccupancy = 0;
    std::uint32_t jointCells = 0;

    [[nodiscard]] std::string humanReadable() const;
    // §23 asks for machine-readable output as well. JSON, because every other report in this
    // repository that crosses a tool boundary is JSON.
    [[nodiscard]] std::string json() const;
};

struct MotionQualitySummaryOptions {
    // **0 means "derive it from the matcher in hand", and that is the default.**
    //
    // A duplicate count at a fixed radius is extremely sensitive to that radius -- more so than a
    // coverage bin count, because it is a threshold on a continuous distance rather than a
    // partition. At 0.01 the count is near zero and at 0.2 it could be most of the corpus, so a
    // bare percentage at a chosen radius is a property of the choice. And the claim it supports is
    // a strong one -- "these samples can never be chosen" is an argument for deleting them -- so an
    // arbitrary radius makes an arbitrary deletion.
    //
    // The radius that means something is the one **inside which the search cannot tell two samples
    // apart**: two samples are duplicates *to the matcher* exactly when swapping one for the other
    // changes no selection. That is the same probe §22 uses to derive its bin width, pointed at
    // feature distance instead of requested speed, and it carries the same ADR-389 property -- it
    // moves when the weights move, so it cannot go stale.
    float duplicateEpsilon = 0.0f;
    // Nearest-neighbour search is O(n^2); on a million samples that is not a report, it is a job.
    // Above this many samples the density figures are computed on a stride and said to be.
    std::uint32_t exactBelow = 20000;
};

[[nodiscard]] MotionQualitySummary analyseMotionQuality(
    const MotionDatabase& db, const MotionQualitySummaryOptions& options = {});

} // namespace avgen::scene
