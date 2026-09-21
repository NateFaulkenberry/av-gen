#pragma once

// Phase C §83: what changed between two motion databases.
//
// **Samples are matched by what they are, not where they are.** A sample's index moves whenever a
// clip is added before it, so an index-by-index comparison would report every sample after the
// first insertion as changed. Here a sample is identified by (clip name, time in clip), which is
// stable across rebuilds, and the comparison reports samples added, removed, unchanged and --
// when the two feature schemas agree -- changed in value.
//
// **Values are compared in raw units.** Each database is normalised by its own mean and spread, so
// adding one clip to a corpus changes every stored feature while changing no motion at all. The
// diff undoes each database's own normalization before comparing, which is the only comparison
// that means "this motion changed".

#include "scene/motion_database.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace avgen::scene {

struct MotionDatabaseDiff {
    std::uint32_t samplesA = 0;
    std::uint32_t samplesB = 0;
    std::uint32_t added = 0;     // in B, not in A
    std::uint32_t removed = 0;   // in A, not in B
    std::uint32_t unchanged = 0; // in both, raw features within tolerance (or schema differs)
    std::uint32_t changed = 0;   // in both, raw features differ (only when schemas agree)
    std::vector<std::string> clipsAdded;
    std::vector<std::string> clipsRemoved;
    bool schemaChanged = false;
    bool weightsChanged = false;
    bool skeletonChanged = false;
    bool sourceChanged = false;
    bool identical = false;      // same identity: nothing to diff
    // The largest raw feature difference seen on a sample in both, and where.
    float largestChange = 0.0f;
    std::string largestChangeAt;
    std::vector<std::string> configChanges; // "trajectoryTimes 0.2,0.4 -> 0.2,0.4,0.6", ...
    [[nodiscard]] std::string report(const std::string& nameA = "A", const std::string& nameB = "B") const;
};

// `tolerance` is in the features' raw units (metres, m/s): below it a value is unchanged.
[[nodiscard]] MotionDatabaseDiff diffMotionDatabases(const MotionDatabase& a, const MotionDatabase& b,
                                                     float tolerance = 1e-4f);

} // namespace avgen::scene
