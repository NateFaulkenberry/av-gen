#pragma once

// Phase C §57: a developer's view of one motion database -- what is in it, what it costs, and where
// it came from. CLI output first (`avgen-motion inspect-db`), as §57 asks; no UI.
//
// **Every figure is read, not re-derived.** The coverage lines come from `measureMotionCategories`
// and `measureMotionCoverage`, the memory from the arrays' own sizes, the provenance from the
// database's build record and the pack's -- so the inspector cannot disagree with the tools it
// summarises about the same database (the lesson §23 recorded for the quality report).

#include "scene/motion_coverage.hpp"
#include "scene/motion_database.hpp"
#include "scene/motion_pack.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace avgen::scene {

struct MotionDatabaseInspection {
    std::string name;
    std::uint32_t clips = 0;
    std::uint32_t samples = 0;
    std::uint32_t dimension = 0;
    // Dimensions per cost term, in `MotionFeatureGroup` order.
    std::array<std::uint32_t, static_cast<std::size_t>(MotionFeatureGroup::Count)> dimensionsByGroup{};
    std::vector<float> trajectoryHorizons;

    // ---- memory (§6) ----
    std::size_t featureBytes = 0;
    std::size_t metadataBytes = 0;
    std::size_t normalizationBytes = 0; // mean + scale
    std::size_t clipNameBytes = 0;
    [[nodiscard]] std::size_t totalBytes() const {
        return featureBytes + metadataBytes + normalizationBytes + clipNameBytes;
    }

    // ---- contacts: per contact joint, the fraction of samples planted. Read from the pack's
    // contact spans when a pack is supplied; empty otherwise. ----
    std::vector<std::string> contactJoints;
    std::vector<float> contactPlanted;

    // ---- phase: how many samples carry a phase, and their distribution over ten bins ----
    std::uint32_t phasedSamples = 0;
    std::array<std::uint32_t, 10> phaseHistogram{};

    // ---- tags: samples carrying each `MotionTag` bit, bit order ----
    std::vector<std::pair<std::string, std::uint32_t>> tagCounts;

    MotionCategoryReport categories;
    std::uint32_t jointOccupancy = 0;
    std::uint32_t jointCells = 0;
    float speedResolution = 0.0f;

    // ---- search structure ----
    std::string searchStructure;

    // ---- provenance ----
    MotionDatabaseBuildInfo build;
    std::uint64_t identity = 0;
    std::string skeletonDigest;
    std::vector<std::string> provenance; // one line per pack provenance record, when a pack is given

    [[nodiscard]] std::string report() const;
};

// `pack` is optional: without it the contact distribution and the provenance records are omitted
// (and the report says so) rather than guessed.
[[nodiscard]] MotionDatabaseInspection inspectMotionDatabase(const MotionDatabase& db,
                                                             const MotionPack* pack = nullptr);

} // namespace avgen::scene
