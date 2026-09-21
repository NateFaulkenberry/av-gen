#include "scene/motion_database_inspect.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

namespace avgen::scene {

MotionDatabaseInspection inspectMotionDatabase(const MotionDatabase& db, const MotionPack* pack) {
    MotionDatabaseInspection out;
    out.name = db.name;
    out.clips = static_cast<std::uint32_t>(db.clipNames.size());
    out.samples = db.sampleCount();
    out.dimension = db.dimension;
    for (const MotionFeatureGroup g : motionFeatureLayout(db.config)) {
        ++out.dimensionsByGroup[static_cast<std::size_t>(g)];
    }
    out.trajectoryHorizons = db.config.trajectoryTimes;

    out.featureBytes = db.features.size() * sizeof(float);
    out.metadataBytes = (db.sampleClip.size() + db.sampleTags.size() + db.sampleNext.size()) *
                            sizeof(std::uint32_t) +
                        (db.sampleTime.size() + db.samplePhase.size()) * sizeof(float);
    out.normalizationBytes = (db.mean.size() + db.scale.size()) * sizeof(float);
    for (const std::string& n : db.clipNames) {
        out.clipNameBytes += n.size();
    }

    // Phase: a sample carries one when its clip is cyclic, which the tag records.
    const auto cyclic = static_cast<std::uint32_t>(MotionTag::Cyclic);
    for (std::uint32_t s = 0; s < db.sampleCount(); ++s) {
        if ((db.sampleTags[s] & cyclic) == 0u) {
            continue;
        }
        ++out.phasedSamples;
        const float p = std::clamp(db.samplePhase[s], 0.0f, 0.99999f);
        ++out.phaseHistogram[static_cast<std::size_t>(p * 10.0f)];
    }

    for (std::uint32_t bit = 0; bit < 32; ++bit) {
        const std::uint32_t mask = 1u << bit;
        const std::string name = motionTagNames(mask);
        if (name == "-") {
            continue;
        }
        std::uint32_t n = 0;
        for (const std::uint32_t t : db.sampleTags) {
            n += (t & mask) != 0u ? 1u : 0u;
        }
        out.tagCounts.emplace_back(name, n);
    }

    out.categories = measureMotionCategories(db);
    const MotionCoverageReport coverage = measureMotionCoverage(db);
    out.jointOccupancy = coverage.jointOccupancy;
    out.jointCells = coverage.jointCells;
    out.speedResolution = coverage.speedResolution;

    // There is no index: §15's linear scan is the search, and §16's staged plan is a way of walking
    // the same arrays. Said plainly, because "search structure" in a report invites the assumption
    // that one exists.
    out.searchStructure = fmt::format(
        "none -- linear scan over {} samples x {} dims ({:.1f} KB/query touched); the staged plan "
        "(§16) walks the same arrays",
        db.sampleCount(), db.dimension, static_cast<double>(out.featureBytes) / 1024.0);

    out.build = db.build;
    out.identity = db.identity;
    out.skeletonDigest = db.skeletonDigest;

    if (pack != nullptr) {
        // Contacts, from the pack's spans: which fraction of samples has each joint planted.
        std::vector<std::string> joints;
        for (const PackClip& clip : pack->clips) {
            for (const ContactTrack& track : clip.contacts) {
                if (std::find(joints.begin(), joints.end(), track.joint) == joints.end()) {
                    joints.push_back(track.joint);
                }
            }
        }
        out.contactJoints = joints;
        out.contactPlanted.assign(joints.size(), 0.0f);
        std::vector<std::uint32_t> planted(joints.size(), 0u);
        for (std::uint32_t s = 0; s < db.sampleCount(); ++s) {
            const std::uint32_t c = db.sampleClip[s];
            if (c >= pack->clips.size()) {
                continue;
            }
            const PackClip& clip = pack->clips[c];
            const float local =
                db.sampleTime[s] - (c < pack->animation.size() ? pack->animation[c].start : 0.0f);
            for (const ContactTrack& track : clip.contacts) {
                const auto j = static_cast<std::size_t>(
                    std::find(joints.begin(), joints.end(), track.joint) - joints.begin());
                for (const ContactSpan& span : track.spans) {
                    if (span.wraps() ? (local >= span.start || local <= span.end)
                                     : (local >= span.start && local <= span.end)) {
                        ++planted[j];
                        break;
                    }
                }
            }
        }
        for (std::size_t j = 0; j < joints.size(); ++j) {
            out.contactPlanted[j] = db.sampleCount() > 0
                                        ? static_cast<float>(planted[j]) / static_cast<float>(db.sampleCount())
                                        : 0.0f;
        }
        for (const Provenance& p : pack->provenance) {
            out.provenance.push_back(fmt::format(
                "{} ({}) licence {} -- {}, derived data {}, training {}", p.source,
                p.sourceFile.empty() ? "-" : p.sourceFile, p.license.empty() ? "NONE" : p.license,
                redistributionName(p.redistribution), p.derivedDataAllowed ? "allowed" : "not allowed",
                p.trainingAllowed ? "allowed" : "not allowed"));
        }
    }
    return out;
}

std::string MotionDatabaseInspection::report() const {
    const auto mb = [](std::size_t b) { return static_cast<double>(b) / (1024.0 * 1024.0); };
    std::string out = fmt::format("motion database '{}'\n", name);
    out += fmt::format("  clips {}   samples {}   dimensions {}\n", clips, samples, dimension);
    out += "  dimensions by term:";
    for (std::size_t g = 0; g < dimensionsByGroup.size(); ++g) {
        if (dimensionsByGroup[g] > 0) {
            out += fmt::format(" {} {}", motionFeatureGroupName(static_cast<MotionFeatureGroup>(g)),
                               dimensionsByGroup[g]);
        }
    }
    out += "\n";
    if (trajectoryHorizons.empty()) {
        out += "  trajectory horizons: none\n";
    } else {
        out += "  trajectory horizons:";
        for (const float t : trajectoryHorizons) {
            out += fmt::format(" {:.2f}s", t);
        }
        out += "\n";
    }
    out += fmt::format("  memory {:.3f} MB: features {:.3f}, per-sample metadata {:.3f}, "
                       "normalization {:.4f}, clip names {:.4f}  ({:.1f} B/sample)\n",
                       mb(totalBytes()), mb(featureBytes), mb(metadataBytes), mb(normalizationBytes),
                       mb(clipNameBytes),
                       samples > 0 ? static_cast<double>(totalBytes()) / samples : 0.0);
    if (contactJoints.empty()) {
        out += "  contacts: no pack given, or the pack has no contact tracks\n";
    } else {
        out += "  contacts (fraction of samples planted):";
        for (std::size_t j = 0; j < contactJoints.size(); ++j) {
            out += fmt::format(" {} {:.1f}%", contactJoints[j], 100.0f * contactPlanted[j]);
        }
        out += "\n";
    }
    out += fmt::format("  phase: {} of {} samples carry one; tenths:", phasedSamples, samples);
    for (const std::uint32_t n : phaseHistogram) {
        out += fmt::format(" {}", n);
    }
    out += "\n  tags:";
    for (const auto& [tag, n] : tagCounts) {
        out += fmt::format(" {} {}", tag, n);
    }
    out += "\n";
    out += fmt::format("  coverage: (speed x turn) {} of {} cells occupied, matcher speed resolution "
                       "{:.3f} m/s\n",
                       jointOccupancy, jointCells, speedResolution);
    for (const MotionCategoryCoverage& c : categories.categories) {
        out += fmt::format("    {:<22} {}\n", motionCategoryName(c.category), coverageGradeName(c.grade));
    }
    out += fmt::format("  search structure: {}\n", searchStructure);
    out += fmt::format("  provenance: identity {:016x}  skeleton {}\n", identity, skeletonDigest);
    out += fmt::format("    built by {} at {:.1f} Hz from pack content {}\n",
                       build.toolVersion.empty() ? "(unrecorded)" : build.toolVersion, build.sampleRate,
                       build.sourcePackDigest.empty() ? "(unrecorded)" : build.sourcePackDigest);
    out += fmt::format("    feature schema {}  build key {}\n", build.featureSchema, build.buildKey);
    for (const std::string& line : provenance) {
        out += fmt::format("    source: {}\n", line);
    }
    return out;
}

} // namespace avgen::scene
