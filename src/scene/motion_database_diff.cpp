#include "scene/motion_database_diff.hpp"

#include "scene/motion_database_io.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace avgen::scene {
namespace {

// A sample's identity across rebuilds: its clip's name and its time, in whole microseconds so two
// builds' float times for the same frame compare equal.
using SampleKey = std::pair<std::string, std::int64_t>;

std::map<SampleKey, std::uint32_t> keyed(const MotionDatabase& db) {
    std::map<SampleKey, std::uint32_t> out;
    for (std::uint32_t s = 0; s < db.sampleCount(); ++s) {
        const std::uint32_t c = db.sampleClip[s];
        const std::string& clip = c < db.clipNames.size() ? db.clipNames[c] : std::string();
        out.emplace(SampleKey{clip, std::llround(static_cast<double>(db.sampleTime[s]) * 1e6)}, s);
    }
    return out;
}

std::string joinFloats(const std::vector<float>& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        out += fmt::format("{}{:g}", i ? "," : "", v[i]);
    }
    return out.empty() ? std::string("none") : out;
}

std::string joinNames(const std::vector<std::string>& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        out += (i ? "," : "") + v[i];
    }
    return out.empty() ? std::string("none") : out;
}

} // namespace

MotionDatabaseDiff diffMotionDatabases(const MotionDatabase& a, const MotionDatabase& b, float tolerance) {
    MotionDatabaseDiff out;
    out.samplesA = a.sampleCount();
    out.samplesB = b.sampleCount();
    out.identical = a.identity != 0 && a.identity == b.identity;
    out.schemaChanged = motionFeatureSchemaDigest(a.config) != motionFeatureSchemaDigest(b.config) ||
                        a.dimension != b.dimension;
    out.skeletonChanged = a.skeletonDigest != b.skeletonDigest;
    out.sourceChanged = a.build.sourcePackDigest != b.build.sourcePackDigest;

    const MotionFeatureConfig& ca = a.config;
    const MotionFeatureConfig& cb = b.config;
    if (ca.joints != cb.joints) {
        out.configChanges.push_back(fmt::format("joints {} -> {}", joinNames(ca.joints), joinNames(cb.joints)));
    }
    if (ca.trajectoryTimes != cb.trajectoryTimes) {
        out.configChanges.push_back(fmt::format("trajectoryTimes {} -> {}", joinFloats(ca.trajectoryTimes),
                                                joinFloats(cb.trajectoryTimes)));
    }
    const std::pair<const char*, std::pair<float, float>> weights[] = {
        {"jointPositionWeight", {ca.jointPositionWeight, cb.jointPositionWeight}},
        {"jointVelocityWeight", {ca.jointVelocityWeight, cb.jointVelocityWeight}},
        {"trajectoryPositionWeight", {ca.trajectoryPositionWeight, cb.trajectoryPositionWeight}},
        {"trajectoryFacingWeight", {ca.trajectoryFacingWeight, cb.trajectoryFacingWeight}},
        {"rootVelocityWeight", {ca.rootVelocityWeight, cb.rootVelocityWeight}},
        {"phaseWeight", {ca.phaseWeight, cb.phaseWeight}},
        {"contactWeight", {ca.contactWeight, cb.contactWeight}},
    };
    for (const auto& [name, pair] : weights) {
        if (pair.first != pair.second) {
            out.weightsChanged = true;
            out.configChanges.push_back(fmt::format("{} {:g} -> {:g}", name, pair.first, pair.second));
        }
    }

    const std::set<std::string> clipsA(a.clipNames.begin(), a.clipNames.end());
    const std::set<std::string> clipsB(b.clipNames.begin(), b.clipNames.end());
    std::set_difference(clipsB.begin(), clipsB.end(), clipsA.begin(), clipsA.end(),
                        std::back_inserter(out.clipsAdded));
    std::set_difference(clipsA.begin(), clipsA.end(), clipsB.begin(), clipsB.end(),
                        std::back_inserter(out.clipsRemoved));

    const auto ka = keyed(a);
    const auto kb = keyed(b);
    const std::size_t dim = a.dimension;
    const auto raw = [](const MotionDatabase& db, std::uint32_t s, std::size_t d) {
        const float f = db.featuresFor(s)[d];
        return db.scale[d] != 0.0f ? (f / db.scale[d]) + db.mean[d] : f;
    };
    for (const auto& [key, sa] : ka) {
        const auto it = kb.find(key);
        if (it == kb.end()) {
            ++out.removed;
            continue;
        }
        if (out.schemaChanged) {
            // Different dimensions mean different things; presence is all that can be compared.
            ++out.unchanged;
            continue;
        }
        float worst = 0.0f;
        for (std::size_t d = 0; d < dim; ++d) {
            worst = std::max(worst, std::abs(raw(a, sa, d) - raw(b, it->second, d)));
        }
        if (worst > tolerance) {
            ++out.changed;
            if (worst > out.largestChange) {
                out.largestChange = worst;
                out.largestChangeAt = fmt::format("{} @ {:.3f}s", key.first,
                                                  static_cast<double>(key.second) * 1e-6);
            }
        } else {
            ++out.unchanged;
        }
    }
    for (const auto& [key, sb] : kb) {
        if (ka.find(key) == ka.end()) {
            ++out.added;
        }
    }
    return out;
}

std::string MotionDatabaseDiff::report(const std::string& nameA, const std::string& nameB) const {
    std::string out;
    out += fmt::format("Database {}: {} samples\n", nameA, samplesA);
    out += fmt::format("Database {}: {} samples\n", nameB, samplesB);
    if (identical) {
        out += "identical (same content identity)\n";
        return out;
    }
    out += fmt::format("Added:     {}\nRemoved:   {}\nChanged:   {}\nUnchanged: {}\n", added, removed,
                       changed, unchanged);
    if (changed > 0) {
        out += fmt::format("  largest raw feature change {:.6f} at {}\n", largestChange, largestChangeAt);
    }
    if (!clipsAdded.empty()) {
        out += fmt::format("Clips added:   {}\n", joinNames(clipsAdded));
    }
    if (!clipsRemoved.empty()) {
        out += fmt::format("Clips removed: {}\n", joinNames(clipsRemoved));
    }
    out += fmt::format("Feature schema: {}\n",
                       schemaChanged ? "CHANGED (values not compared; samples matched by presence only)"
                                     : "unchanged");
    out += fmt::format("Weights: {}\n", weightsChanged ? "changed" : "unchanged");
    out += fmt::format("Skeleton: {}\n", skeletonChanged ? "CHANGED" : "unchanged");
    out += fmt::format("Source pack: {}\n", sourceChanged ? "changed" : "unchanged");
    for (const std::string& c : configChanges) {
        out += fmt::format("  {}\n", c);
    }
    return out;
}

} // namespace avgen::scene
