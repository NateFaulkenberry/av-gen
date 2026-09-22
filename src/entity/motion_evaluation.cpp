#include "entity/motion_evaluation.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

namespace avgen::entity {

std::string MotionEvaluation::report() const {
    return fmt::format("{:<28} {:>5} steps  continuity {:5.1f}%  trajectory {:.3f}  velocity {:.3f}  pose {:.3f}  "
                       "contact {:5.1f}%  phase {:.3f}  switches {:.2f}/s",
                       clip, steps, 100.0f * continuity, trajectoryError, velocityError, poseError,
                       100.0f * contactMismatch, phaseMismatch, transitionsPerSecond);
}

namespace {

bool plantedAt(const scene::ContactTrack& track, float local) {
    for (const scene::ContactSpan& span : track.spans) {
        if (span.wraps() ? (local >= span.start || local <= span.end) : (local >= span.start && local <= span.end)) {
            return true;
        }
    }
    return false;
}

float raw(const scene::MotionDatabase& db, std::uint32_t s, std::size_t d) {
    const float v = db.featuresFor(s)[d];
    return db.scale[d] != 0.0f ? (v / db.scale[d]) + db.mean[d] : v;
}

} // namespace

MotionEvaluation evaluateMatcher(const scene::MotionPack& pack, std::size_t groundTruth, const scene::MotionDatabase& db,
                                 const std::vector<scene::AnimationClip>& clips, const MotionEvaluationOptions& options) {
    MotionEvaluation out;
    if (groundTruth >= pack.clips.size() || groundTruth >= pack.animation.size() || db.sampleCount() == 0) {
        return out;
    }
    out.clip = pack.clips[groundTruth].name;
    // The ground truth, described in exactly the features the matcher compares: a database of the
    // one clip, built with the evaluated database's own config and sample rate. Every measure is
    // then a distance in the body frame, in raw units, and the two sides cannot be describing the
    // motion differently.
    scene::MotionPack single = pack;
    single.clips = {pack.clips[groundTruth]};
    single.animation = {pack.animation[groundTruth]};
    scene::MotionDatabaseOptions truthOptions;
    truthOptions.config = db.config;
    truthOptions.sampleRate = db.build.sampleRate > 0.0f ? db.build.sampleRate : 30.0f;
    auto truth = scene::buildMotionDatabase(single, truthOptions);
    if (!truth || truth->sampleCount() == 0) {
        return out;
    }
    const auto layout = scene::motionFeatureLayout(db.config);
    std::size_t rv = layout.size();
    std::size_t tp = layout.size();
    std::vector<std::size_t> jointDims;
    for (std::size_t d = 0; d < layout.size(); ++d) {
        if (layout[d] == scene::MotionFeatureGroup::RootVelocity && rv == layout.size()) {
            rv = d;
        }
        if (layout[d] == scene::MotionFeatureGroup::TrajectoryPosition && tp == layout.size()) {
            tp = d;
        }
        if (layout[d] == scene::MotionFeatureGroup::JointPosition) {
            jointDims.push_back(d);
        }
    }
    const scene::PackClip& meta = pack.clips[groundTruth];
    const float length = pack.animation[groundTruth].length();
    const float seconds = options.seconds > 0.0f ? std::min(options.seconds, length) : length;
    const float rate = std::max(options.rate, 1.0f);
    const float dt = 1.0f / rate;
    const auto steps = static_cast<std::uint32_t>(std::floor(seconds * rate));

    MatchMotionProvider matcher(&db, &clips, "evaluated");
    matcher.setSettings(options.settings);
    matcher.setWorldScale(options.worldScale);
    MotionMemory memory;
    const float truthRate = truthOptions.sampleRate;
    double trajectory = 0.0;
    double velocity = 0.0;
    double pose = 0.0;
    double phase = 0.0;
    std::uint32_t contactPairs = 0;
    std::uint32_t contactMiss = 0;
    const std::uint64_t switchesBefore = matcher.counters().switches;
    std::uint32_t continued = 0;
    for (std::uint32_t k = 0; k < steps; ++k) {
        const float t = static_cast<float>(k) * dt;
        const auto truthSample = std::min<std::uint32_t>(static_cast<std::uint32_t>(std::lround(t * truthRate)),
                                                         truth->sampleCount() - 1u);
        // The request is what the ground truth was doing, in its own frame, handed to the matcher as
        // a body facing +Z: the truth's features are already in its body frame, so this is the same
        // question in the same frame.
        const glm::vec3 v(raw(*truth, truthSample, rv), raw(*truth, truthSample, rv + 1), raw(*truth, truthSample, rv + 2));
        MotionRequest request;
        request.desiredVelocity = v * options.worldScale;
        request.bodyVelocity = request.desiredVelocity;
        request.bodyVelocityKnown = true;
        MotionMemory next;
        const std::uint64_t before = matcher.counters().switches;
        const MotionResult r = matcher.advance(request, memory, static_cast<double>(t), dt, next);
        if (!r.ok()) {
            continue;
        }
        const bool switched = matcher.counters().switches > before;
        continued += switched ? 0u : 1u;
        memory = next;
        const std::uint32_t s = memory.selection;
        ++out.steps;

        double tErr = 0.0;
        for (std::size_t h = 0; h < db.config.trajectoryTimes.size(); ++h) {
            const std::size_t d = tp + (h * 4u);
            tErr += std::hypot(raw(db, s, d) - raw(*truth, truthSample, d), raw(db, s, d + 1) - raw(*truth, truthSample, d + 1));
        }
        trajectory += db.config.trajectoryTimes.empty() ? 0.0 : tErr / static_cast<double>(db.config.trajectoryTimes.size());
        velocity += std::hypot(raw(db, s, rv) - v.x, raw(db, s, rv + 2) - v.z);
        double pErr = 0.0;
        for (std::size_t j = 0; j + 2 < jointDims.size(); j += 3) {
            const std::size_t d = jointDims[j];
            pErr += std::sqrt(std::pow(raw(db, s, d) - raw(*truth, truthSample, d), 2.0) +
                              std::pow(raw(db, s, d + 1) - raw(*truth, truthSample, d + 1), 2.0) +
                              std::pow(raw(db, s, d + 2) - raw(*truth, truthSample, d + 2), 2.0));
        }
        pose += jointDims.size() >= 3 ? pErr / static_cast<double>(jointDims.size() / 3) : 0.0;
        double dPhase = std::abs(static_cast<double>(db.samplePhase[s]) - truth->samplePhase[truthSample]);
        dPhase = std::min(dPhase, 1.0 - dPhase);
        phase += dPhase;
        // Contacts from the packs themselves, by foot name, at the two samples' clip times.
        if (db.sampleClip[s] < pack.clips.size()) {
            const scene::PackClip* chosen = nullptr;
            for (const scene::PackClip& c : pack.clips) {
                if (c.name == db.clipNames[db.sampleClip[s]]) {
                    chosen = &c;
                    break;
                }
            }
            if (chosen != nullptr) {
                for (const scene::ContactTrack& truthTrack : meta.contacts) {
                    for (const scene::ContactTrack& chosenTrack : chosen->contacts) {
                        if (chosenTrack.joint == truthTrack.joint) {
                            const bool a = plantedAt(truthTrack, t);
                            const bool b = plantedAt(chosenTrack, db.sampleTime[s] - clips[db.sampleClip[s]].start);
                            ++contactPairs;
                            contactMiss += a != b ? 1u : 0u;
                        }
                    }
                }
            }
        }
    }
    if (out.steps > 0) {
        const auto n = static_cast<double>(out.steps);
        out.continuity = static_cast<float>(continued) / static_cast<float>(out.steps);
        out.trajectoryError = static_cast<float>(trajectory / n);
        out.velocityError = static_cast<float>(velocity / n);
        out.poseError = static_cast<float>(pose / n);
        out.phaseMismatch = static_cast<float>(phase / n);
        out.contactMismatch = contactPairs > 0 ? static_cast<float>(contactMiss) / static_cast<float>(contactPairs) : 0.0f;
        out.transitionsPerSecond =
            static_cast<float>(matcher.counters().switches - switchesBefore) / (static_cast<float>(out.steps) * dt);
    }
    return out;
}

} // namespace avgen::entity
