#include "scene/motion_database.hpp"
#include "scene/motion_database_io.hpp"

#include "scene/animation.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>
#include <utility>

namespace avgen::scene {
namespace {

// Lowercase, for tag inference from a clip's own name. Inference only fills in what the pack did
// not say; an authored tag always wins.
bool contains(const std::string& haystack, std::string_view needle) {
    if (needle.size() > haystack.size()) {
        return false;
    }
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool ok = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(haystack[i + j])));
            if (a != needle[j]) {
                ok = false;
                break;
            }
        }
        if (ok) {
            return true;
        }
    }
    return false;
}


} // namespace

std::vector<glm::vec3> clipFacing(const Skeleton& skeleton, const AnimationClip& clip, int root,
                                  std::uint32_t frames, float dt, bool loop, float window) {
    const auto rotationOf = [](const glm::mat4& m) {
        return glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])),
                         glm::normalize(glm::vec3(m[2])));
    };
    Pose pose;
    std::vector<glm::mat4> model;
    setRestPose(skeleton, pose);
    poseToModel(skeleton, pose, model);
    const glm::mat3 restInverse = glm::transpose(rotationOf(model[static_cast<std::size_t>(root)]));

    std::vector<glm::vec3> raw(frames, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::vec3 previous(0.0f, 0.0f, 1.0f);
    for (std::uint32_t f = 0; f < frames; ++f) {
        const float t = std::min(clip.start + (static_cast<float>(f) * dt), clip.duration);
        setRestPose(skeleton, pose);
        sampleClip(clip, t, pose);
        poseToModel(skeleton, pose, model);
        const glm::vec3 forward =
            rotationOf(model[static_cast<std::size_t>(root)]) * (restInverse * glm::vec3(0.0f, 0.0f, 1.0f));
        const float len = std::sqrt((forward.x * forward.x) + (forward.z * forward.z));
        // A body pitched straight up or down has no planar facing; it keeps the last one it had.
        previous = len > 1e-4f ? glm::vec3(forward.x / len, 0.0f, forward.z / len) : previous;
        raw[f] = previous;
    }
    const auto half = static_cast<int>(std::lround((std::max(window, 0.0f) * 0.5f) / dt));
    if (half == 0 || frames < 2) {
        return raw;
    }
    // A looping clip's first and last samples are the same instant, so the period is one less.
    const int period = loop ? static_cast<int>(frames) - 1 : static_cast<int>(frames);
    std::vector<glm::vec3> out(frames);
    for (int f = 0; f < static_cast<int>(frames); ++f) {
        glm::vec3 sum(0.0f);
        for (int o = -half; o <= half; ++o) {
            int i = f + o;
            if (loop) {
                i = ((i % period) + period) % period;
            } else {
                i = std::clamp(i, 0, static_cast<int>(frames) - 1);
            }
            sum += raw[static_cast<std::size_t>(i)];
        }
        const float len = std::sqrt((sum.x * sum.x) + (sum.z * sum.z));
        out[static_cast<std::size_t>(f)] = len > 1e-4f ? glm::vec3(sum.x / len, 0.0f, sum.z / len) : raw[static_cast<std::size_t>(f)];
    }
    return out;
}


std::uint32_t motionTagsFor(const PackClip& clip, const ClipAnalysis& analysis) {
    std::uint32_t tags = 0;
    const auto set = [&](MotionTag t) { tags |= static_cast<std::uint32_t>(t); };

    // Measured facts first, because they cannot be wrong about the clip they came from.
    if (analysis.phase.cyclic) {
        set(MotionTag::Cyclic);
    }
    if (analysis.travels) {
        set(MotionTag::Travelling);
    }
    if (!clip.loop) {
        set(MotionTag::OneShot);
    }

    // Then the clip's own tags, which a pack author wrote.
    for (const std::string& tag : clip.tags) {
        if (contains(tag, "walk")) { set(MotionTag::Walk); set(MotionTag::Locomotion); }
        if (contains(tag, "run")) { set(MotionTag::Run); set(MotionTag::Locomotion); }
        if (contains(tag, "idle")) { set(MotionTag::Idle); }
        if (contains(tag, "turn")) { set(MotionTag::Turn); }
        if (contains(tag, "jump") || contains(tag, "fall")) { set(MotionTag::Airborne); }
    }
    // And finally the name, which is the weakest source and is only consulted for what nothing
    // above supplied. A clip called "Walking" with no tags is still a walk, and refusing to read
    // that would make every existing asset untagged.
    const std::string& n = clip.name;
    if ((tags & static_cast<std::uint32_t>(MotionTag::Locomotion)) == 0) {
        if (contains(n, "walk")) { set(MotionTag::Walk); set(MotionTag::Locomotion); }
        else if (contains(n, "run")) { set(MotionTag::Run); set(MotionTag::Locomotion); }
    }
    if (contains(n, "idle")) { set(MotionTag::Idle); }
    if (contains(n, "turn")) { set(MotionTag::Turn); }
    if (contains(n, "jump") || contains(n, "fall") || contains(n, "land")) { set(MotionTag::Airborne); }
    return tags;
}

std::string motionTagNames(std::uint32_t tags) {
    static constexpr std::pair<MotionTag, const char*> kNames[] = {
        {MotionTag::Locomotion, "locomotion"}, {MotionTag::Idle, "idle"},
        {MotionTag::Walk, "walk"},             {MotionTag::Run, "run"},
        {MotionTag::Turn, "turn"},             {MotionTag::Airborne, "airborne"},
        {MotionTag::Cyclic, "cyclic"},         {MotionTag::Travelling, "travelling"},
        {MotionTag::OneShot, "oneshot"},
    };
    std::string out;
    for (const auto& [tag, name] : kNames) {
        if ((tags & static_cast<std::uint32_t>(tag)) != 0) {
            if (!out.empty()) {
                out += '+';
            }
            out += name;
        }
    }
    return out.empty() ? std::string("-") : out;
}

std::uint32_t MotionFeatureConfig::dimension() const {
    // Per joint: position (3) and velocity (3), in the body's own frame.
    std::uint32_t d = static_cast<std::uint32_t>(joints.size()) * 6u;
    // Per trajectory sample: a planar position (2) and a planar facing (2).
    d += static_cast<std::uint32_t>(trajectoryTimes.size()) * 4u;
    d += 3u; // root velocity
    if (phaseWeight > 0.0f) {
        d += 2u; // phase as (cos, sin), so 0.99 and 0.01 are adjacent rather than a unit apart
    }
    if (contactWeight > 0.0f) {
        // One flag per CONTACT joint, not per feature joint: a head does not plant.
        d += static_cast<std::uint32_t>(contactJointNames().size());
    }
    return d;
}

const char* motionFeatureGroupName(MotionFeatureGroup group) {
    switch (group) {
    case MotionFeatureGroup::JointPosition: return "jointPosition";
    case MotionFeatureGroup::JointVelocity: return "jointVelocity";
    case MotionFeatureGroup::TrajectoryPosition: return "trajectoryPosition";
    case MotionFeatureGroup::TrajectoryFacing: return "trajectoryFacing";
    case MotionFeatureGroup::RootVelocity: return "rootVelocity";
    case MotionFeatureGroup::Phase: return "phase";
    case MotionFeatureGroup::Contact: return "contact";
    case MotionFeatureGroup::Count: break;
    }
    return "?";
}

// The layout `buildMotionDatabase` writes, stated once so the weights and the breakdown cannot
// drift from it. `dimension()` above computes the same total; this says what each slot *is*.
void motionFeatureLayoutInto(const MotionFeatureConfig& config, std::vector<MotionFeatureGroup>& out) {
    out.clear();
    out.reserve(config.dimension());
    for (std::size_t j = 0; j < config.joints.size(); ++j) {
        out.insert(out.end(), 3u, MotionFeatureGroup::JointPosition);
        out.insert(out.end(), 3u, MotionFeatureGroup::JointVelocity);
    }
    for (std::size_t t = 0; t < config.trajectoryTimes.size(); ++t) {
        out.insert(out.end(), 2u, MotionFeatureGroup::TrajectoryPosition);
        out.insert(out.end(), 2u, MotionFeatureGroup::TrajectoryFacing);
    }
    out.insert(out.end(), 3u, MotionFeatureGroup::RootVelocity);
    if (config.phaseWeight > 0.0f) {
        out.insert(out.end(), 2u, MotionFeatureGroup::Phase);
    }
    if (config.contactWeight > 0.0f) {
        out.insert(out.end(), config.contactJointNames().size(), MotionFeatureGroup::Contact);
    }
}

std::vector<MotionFeatureGroup> motionFeatureLayout(const MotionFeatureConfig& config) {
    std::vector<MotionFeatureGroup> out;
    motionFeatureLayoutInto(config, out);
    return out;
}

void motionFeatureWeightsInto(const MotionFeatureConfig& config, std::vector<float>& out) {
    // Its own scratch, so a caller may pass the layout it also wants without the two aliasing.
    thread_local std::vector<MotionFeatureGroup> layout;
    motionFeatureLayoutInto(config, layout);
    out.clear();
    out.reserve(layout.size());
    for (const MotionFeatureGroup group : layout) {
        switch (group) {
        case MotionFeatureGroup::JointPosition: out.push_back(config.jointPositionWeight); break;
        case MotionFeatureGroup::JointVelocity: out.push_back(config.jointVelocityWeight); break;
        case MotionFeatureGroup::TrajectoryPosition:
            out.push_back(config.trajectoryPositionWeight);
            break;
        case MotionFeatureGroup::TrajectoryFacing:
            out.push_back(config.trajectoryFacingWeight);
            break;
        case MotionFeatureGroup::RootVelocity: out.push_back(config.rootVelocityWeight); break;
        case MotionFeatureGroup::Phase: out.push_back(config.phaseWeight); break;
        case MotionFeatureGroup::Contact: out.push_back(config.contactWeight); break;
        case MotionFeatureGroup::Count: out.push_back(1.0f); break;
        }
    }
}

std::vector<float> motionFeatureWeights(const MotionFeatureConfig& config) {
    std::vector<float> out;
    motionFeatureWeightsInto(config, out);
    return out;
}

std::string MotionCostBreakdown::report() const {
    std::string out;
    for (std::size_t g = 0; g < static_cast<std::size_t>(MotionFeatureGroup::Count); ++g) {
        if (terms[g] == 0.0f) {
            continue;
        }
        out += fmt::format("{}={:.4f} ", motionFeatureGroupName(static_cast<MotionFeatureGroup>(g)),
                           terms[g]);
    }
    if (continuity != 0.0f) {
        out += fmt::format("continuity={:.4f} ", continuity);
    }
    if (transition != 0.0f) {
        out += fmt::format("transition={:.4f} ", transition);
    }
    out += fmt::format("| total={:.4f}", total());
    return out;
}

MotionFeatureConfig defaultBipedConfig(std::string leftFoot, std::string rightFoot, std::string head) {
    MotionFeatureConfig config;
    // The feet carry contacts; the head does not. Named explicitly rather than inherited from the
    // feature joints, which is what put a contact flag on a head.
    config.contactJoints = {leftFoot, rightFoot};
    config.joints = {std::move(leftFoot), std::move(rightFoot), std::move(head)};
    // 0.2 / 0.4 / 0.6 s, the spacing the literature converges on: far enough apart to describe a
    // turn, near enough that the last one is still a prediction rather than a guess.
    config.trajectoryTimes = {0.2f, 0.4f, 0.6f};
    return config;
}

std::string MotionDatabaseStats::report() const {
    std::string out = fmt::format(
        "{} clip(s), {} sample(s), {} dimension(s)\n"
        "  features {:.2f} MB   metadata {:.2f} MB   total {:.2f} MB   ({:.1f} B/sample)\n"
        "  dead dimensions: {}{}\n",
        clips, samples, dimension, static_cast<double>(featureBytes) / 1048576.0,
        static_cast<double>(metadataBytes) / 1048576.0,
        static_cast<double>(totalBytes()) / 1048576.0,
        samples > 0 ? static_cast<double>(totalBytes()) / samples : 0.0, deadDimensions,
        deadDimensions > 0 ? "  <-- these contribute nothing to any comparison" : "");
    // The statistic a dead-dimension count cannot supply. Near zero means the limb does not
    // articulate in this corpus, whatever its raw coordinates do (ADR-553).
    for (std::size_t j = 0; j < jointNames.size() && j < jointRadiusSpread.size(); ++j) {
        out += fmt::format("  {:<16} distance-from-body spread {:.4f}{}\n", jointNames[j],
                           jointRadiusSpread[j],
                           jointRadiusSpread[j] < 0.005f
                               ? "  <-- this limb does not articulate; only the body moves it"
                               : "");
    }
    return out;
}

Result<MotionDatabase> buildMotionDatabase(const MotionPack& pack,
                                           const MotionDatabaseOptions& options) {
    if (pack.skeleton.joints.empty()) {
        return fail("motion database: the pack has no skeleton");
    }
    if (options.config.joints.empty()) {
        return fail("motion database: the feature config names no joints, so every sample would "
                    "have the same feature vector and the search would return the first one");
    }
    MotionDatabase db;
    db.name = pack.name;
    db.skeletonDigest = pack.skeletonDigest;
    db.config = options.config;
    db.dimension = options.config.dimension();

    // Resolve the feature joints once. A joint the rig lacks is fatal rather than skipped: a
    // database silently built on two of three joints would score against a different vector from
    // the one the query builds, and every match would be wrong in a way nothing reports.
    std::vector<int> jointIndex;
    jointIndex.reserve(options.config.joints.size());
    for (const std::string& name : options.config.joints) {
        const int index = pack.skeleton.find(name);
        if (index < 0) {
            return fail("motion database: the feature config names joint '{}', which this pack's "
                        "skeleton does not have",
                        name);
        }
        jointIndex.push_back(index);
    }

    const float rate = std::max(options.sampleRate, 1.0f);
    const float dt = 1.0f / rate;
    const auto dim = static_cast<std::size_t>(db.dimension);

    // Gathered during the build: the spread of each feature joint's distance from the body.
    std::vector<double> radiusSum(jointIndex.size(), 0.0);
    std::vector<double> radiusSumSq(jointIndex.size(), 0.0);
    std::uint64_t radiusCount = 0;

    Pose pose;
    Pose poseAhead;
    std::vector<glm::mat4> model;
    std::vector<glm::mat4> modelAhead;

    for (std::size_t c = 0; c < pack.animation.size() && c < pack.clips.size(); ++c) {
        const AnimationClip& clip = pack.animation[c];
        const PackClip& meta = pack.clips[c];
        db.clipNames.push_back(meta.name);
        const float length = clip.length();
        if (length <= 0.0f) {
            continue;
        }
        // ADR-337's travel joint: which joint carries the body. Not joint 0.
        int root = -1;
        for (const AnimationChannel& channel : clip.channels) {
            if (channel.path == AnimationPath::Translation &&
                (root < 0 || static_cast<int>(channel.joint) < root)) {
                root = static_cast<int>(channel.joint);
            }
        }
        if (root < 0) {
            root = 0;
        }
        // **The pack already knows this, and re-deriving it here was silently wrong.** This line
        // used to call `analyseClip(pack.skeleton, clip, {}, 0, ContactSettings{})` -- with an
        // empty contact-joint list -- so the phase analysis had no foot plants to work from and
        // `cyclic` came back false for every clip in every pack. 25 of the alien's 26 clips are
        // cyclic in the pack, and **zero samples carried `MotionTag::Cyclic` in the database.**
        //
        // Found by §14 printing the tag distribution before using it, which is the only reason
        // anyone looked: a filter on `Cyclic` would not have been inert, it would have emptied the
        // candidate set and returned a confident answer computed over nothing.
        //
        // `buildMotionPack` computed this correctly, with the real contact joints, and stored it.
        // Reading what the pack stored is both correct and cheaper -- one statement of a fact,
        // several readers, the same rule `motionFeatureLayout` follows.
        ClipAnalysis analysis;
        analysis.phase = meta.phase;
        // `travels` is NOT recoverable from what `PackClip` stores: ADR-552 defines travel as the
        // root's extent against the body's rest height, and the pack keeps `rootTravel`, which is
        // net displacement -- the very measure ADR-552 rejected, because a 131-second walk that
        // returns to its start has a net displacement of 0.508 m against a path length of 93 m.
        // So it is re-derived here.
        //
        // **The empty contact list does not affect it, and an earlier version of this comment said
        // it did.** `analyseClip` computes `travels` from `measureRoot(skeleton, clip, root,
        // rate)`, whose only inputs are the travel joint's translation track and the rest height:
        // `travels = extent > restHeight`. Contacts are not an argument to it. The sole thing
        // `ContactSettings{}` contributes here is `sampleRate`, whose default of 30 Hz is the
        // alien pack's authored rate, so this verdict is computed correctly.
        //
        // `MotionTag::Travelling` therefore reads 0% on this corpus for the **right reason**:
        // ADR-540, every locomotion clip in this repository is authored in place. The five clips
        // whose root moves more than 5 cm are all deaths, which fall a long way short of a rest
        // height. The tag is reachable and working.
        //
        // What is genuinely wrong here is the cost: this call runs `detectContacts` and
        // `extractPhase` over the whole clip, discards both, and keeps one boolean. `measureRoot`
        // alone would answer it, and is not currently exported. The clean fix is for the pack to
        // store the verdict it already computed -- the same fix `phase` just received above -- and
        // that is a note for §37's offline/runtime boundary rather than a change made here.
        const ClipAnalysis derived = analyseClip(pack.skeleton, clip, {}, 0, ContactSettings{});
        analysis.travels = derived.travels;
        const std::uint32_t tags = motionTagsFor(meta, analysis);

        const auto frames =
            static_cast<std::uint32_t>(std::max(2.0f, std::floor(length * rate + 0.5f) + 1.0f));
        const std::uint32_t firstSample = db.sampleCount();

        // ---- what lies past the clip's last key ------------------------------------------------
        //
        // **Every look ahead used to be clamped at the clip's end**, so the last sample of every
        // clip read a root velocity of zero and its trajectory shrank to nothing over the final
        // horizon. That included looping walks. Each moving clip therefore ended in a fictitious
        // stop, and a query landing on a clip's last frame saw a body at rest. `agent/anim-cinfra`
        // found it while building §58's category report.
        //
        // The honest continuation depends on what the clip is:
        //   * a **looping** clip continues into its own start, one cycle's travel further on.
        //     Anything else would contradict `sampleNext`, which already says so;
        //   * a **non-looping** clip continues at the velocity it ended with. That is the least
        //     assumption, and a clip that ends at rest (every death here) extrapolates to rest,
        //     so it is unchanged.
        const auto rootAt = [&](float time) {
            setRestPose(pack.skeleton, poseAhead);
            sampleClip(clip, time, poseAhead);
            poseToModel(pack.skeleton, poseAhead, modelAhead);
            return glm::vec3(modelAhead[static_cast<std::size_t>(root)][3]);
        };
        const glm::vec3 rootFirst = rootAt(clip.start);
        const glm::vec3 rootLast = rootAt(clip.duration);
        const glm::vec3 rootBeforeLast = rootAt(std::max(clip.start, clip.duration - dt));
        const glm::vec3 cycleTravel =
            meta.loop ? glm::vec3(rootLast.x - rootFirst.x, 0.0f, rootLast.z - rootFirst.z)
                      : glm::vec3(0.0f);
        const glm::vec3 endVelocity =
            meta.loop ? glm::vec3(0.0f)
                      : glm::vec3(rootLast.x - rootBeforeLast.x, 0.0f,
                                  rootLast.z - rootBeforeLast.z) / dt;
        // Pose `modelAhead` at `time`, which may lie past the end, and return the planar offset
        // the continuation adds to everything in it. The offset moves the whole body, so it
        // cancels in body-relative quantities and appears only in the root's own travel.
        const auto sampleAhead = [&](float time) {
            float local = time;
            glm::vec3 offset(0.0f);
            if (time > clip.duration) {
                if (meta.loop && length > 0.0f) {
                    const float over = time - clip.start;
                    const float cycles = std::floor(over / length);
                    local = clip.start + (over - (cycles * length));
                    offset = cycleTravel * cycles;
                } else {
                    local = clip.duration;
                    offset = endVelocity * (time - clip.duration);
                }
            }
            setRestPose(pack.skeleton, poseAhead);
            sampleClip(clip, local, poseAhead);
            poseToModel(pack.skeleton, poseAhead, modelAhead);
            return offset;
        };

        // ---- the body's facing, per frame (§7) --------------------------------------------------
        //
        // §7 says the features are in the body's own frame. The builder removed the body's
        // position and never its heading, so a walk facing east and the same walk facing north
        // were different motions to the search. Every Glowmere clip is authored facing +Z, which
        // is why nothing noticed. 100STYLE turns, and so does any character in a scene.
        const std::vector<glm::vec3> facing =
            clipFacing(pack.skeleton, clip, root, frames, dt, meta.loop, options.config.facingWindow);

        for (std::uint32_t f = 0; f < frames; ++f) {
            const float t = std::min(clip.start + (static_cast<float>(f) * dt), clip.duration);
            const glm::vec3 heading = facing[f];
            setRestPose(pack.skeleton, pose);
            sampleClip(clip, t, pose);
            poseToModel(pack.skeleton, pose, model);
            // One step ahead, for velocities. A forward difference rather than a central one so
            // that the first sample of a clip is not a special case; the error is one frame of
            // acceleration, which is below the noise in the source.
            const glm::vec3 aheadOffset = sampleAhead(t + dt);

            const glm::vec3 body(model[static_cast<std::size_t>(root)][3]);
            // Body-relative quantities use the pose as sampled; the continuation's offset moves
            // the whole body and would cancel anyway.
            const glm::vec3 bodyAhead(modelAhead[static_cast<std::size_t>(root)][3]);
            const glm::vec3 rootVelocity = ((bodyAhead + aheadOffset) - body) / dt;

            const std::size_t base = db.features.size();
            db.features.resize(base + dim);
            float* out = db.features.data() + base;
            std::size_t k = 0;

            // ---- joints, in the body's own frame ----
            // Body-relative, because motion matching compares *shapes* and an absolute position
            // would make two identical walks at different places look unlike each other.
            for (std::size_t jn = 0; jn < jointIndex.size(); ++jn) {
                const auto ji = static_cast<std::size_t>(jointIndex[jn]);
                const glm::vec3 pWorld = glm::vec3(model[ji][3]) - body;
                const glm::vec3 p = toFacingFrame(pWorld, heading);
                const glm::vec3 v = toFacingFrame(
                    ((glm::vec3(modelAhead[ji][3]) - bodyAhead) - pWorld) / dt, heading);
                out[k++] = p.x; out[k++] = p.y; out[k++] = p.z;
                out[k++] = v.x; out[k++] = v.y; out[k++] = v.z;
                const double radius = static_cast<double>(glm::length(p));
                radiusSum[jn] += radius;
                radiusSumSq[jn] += radius * radius;
            }
            ++radiusCount;

            // ---- future trajectory ----
            // **Identically zero on in-place content** (ADR-540), which is every clip in this
            // repository. Built anyway because it is correct and because 100STYLE exercises it;
            // `deadDimensions` below reports when it contributed nothing.
            for (const float ahead : options.config.trajectoryTimes) {
                const glm::vec3 offset = sampleAhead(t + ahead);
                const glm::vec3 future =
                    glm::vec3(modelAhead[static_cast<std::size_t>(root)][3]) + offset;
                const glm::vec3 delta = toFacingFrame(future - body, heading);
                out[k++] = delta.x;
                out[k++] = delta.z;
                // Facing: the direction it is heading at that moment, or zero when it is not
                // moving -- which is honest rather than a default of "forward".
                const float len = std::sqrt((delta.x * delta.x) + (delta.z * delta.z));
                out[k++] = len > 1e-5f ? delta.x / len : 0.0f;
                out[k++] = len > 1e-5f ? delta.z / len : 0.0f;
            }

            const glm::vec3 bodyVelocity = toFacingFrame(rootVelocity, heading);
            out[k++] = bodyVelocity.x;
            out[k++] = bodyVelocity.y;
            out[k++] = bodyVelocity.z;

            if (options.config.phaseWeight > 0.0f) {
                const float phase = meta.phase.empty() ? 0.0f : meta.phase.at(t - clip.start);
                // As a point on a circle, so phase 0.99 and phase 0.01 are neighbours. A raw 0..1
                // scalar makes the loop point the most distant pair in the database.
                out[k++] = std::cos(6.283185307179586f * phase);
                out[k++] = std::sin(6.283185307179586f * phase);
            }
            if (options.config.contactWeight > 0.0f) {
                for (std::size_t ji = 0; ji < jointIndex.size(); ++ji) {
                    bool planted = false;
                    if (ji < meta.contacts.size()) {
                        for (const ContactSpan& span : meta.contacts[ji].spans) {
                            const float local = t - clip.start;
                            if (span.wraps() ? (local >= span.start || local <= span.end)
                                             : (local >= span.start && local <= span.end)) {
                                planted = true;
                                break;
                            }
                        }
                    }
                    out[k++] = planted ? 1.0f : 0.0f;
                }
            }

            db.sampleClip.push_back(static_cast<std::uint32_t>(c));
            db.sampleTime.push_back(t);
            db.samplePhase.push_back(meta.phase.empty() ? 0.0f : meta.phase.at(t - clip.start));
            db.sampleTags.push_back(tags);
            // Filled below, once the clip's extent is known.
            db.sampleNext.push_back(MotionDatabase::kInvalid);
        }

        // The continuation index. A looping clip's last sample continues at its first, which is
        // what lets a cyclic walk play forever without ever searching.
        const std::uint32_t lastSample = db.sampleCount();
        for (std::uint32_t i = firstSample; i + 1 < lastSample; ++i) {
            db.sampleNext[i] = i + 1;
        }
        if (lastSample > firstSample) {
            db.sampleNext[lastSample - 1] = meta.loop ? firstSample : MotionDatabase::kInvalid;
        }
    }

    if (db.sampleCount() == 0) {
        return fail("motion database: the pack yielded no samples");
    }

    // ---- normalization (§9): zero mean, unit standard deviation, per dimension ----
    db.mean.assign(dim, 0.0f);
    db.scale.assign(dim, 1.0f);
    const auto count = static_cast<double>(db.sampleCount());
    std::vector<double> sum(dim, 0.0);
    std::vector<double> sumSq(dim, 0.0);
    for (std::uint32_t s = 0; s < db.sampleCount(); ++s) {
        const float* f = db.featuresFor(s);
        for (std::size_t d = 0; d < dim; ++d) {
            sum[d] += f[d];
            sumSq[d] += static_cast<double>(f[d]) * f[d];
        }
    }
    db.stats.deadDimensions = 0;
    for (std::size_t d = 0; d < dim; ++d) {
        const double mean = sum[d] / count;
        const double variance = std::max(0.0, (sumSq[d] / count) - (mean * mean));
        const double stddev = std::sqrt(variance);
        db.mean[d] = static_cast<float>(mean);
        // A dimension that never varies cannot discriminate. Its scale is left at 1 and its
        // contribution is therefore always zero after centring -- correct, and counted, because a
        // block of dead dimensions is a fact about the corpus that the report should carry.
        if (stddev < 1e-6) {
            db.scale[d] = 1.0f;
            ++db.stats.deadDimensions;
        } else {
            db.scale[d] = static_cast<float>(1.0 / stddev);
        }
    }
    // Standardise the stored features once, so a query is a plain distance and the inner loop has
    // no per-dimension arithmetic beyond a subtract and a multiply-add.
    for (std::uint32_t s = 0; s < db.sampleCount(); ++s) {
        float* f = db.features.data() + (static_cast<std::size_t>(s) * dim);
        for (std::size_t d = 0; d < dim; ++d) {
            f[d] = (f[d] - db.mean[d]) * db.scale[d];
        }
    }

    // §16/§28's scale, measured once here rather than by each consumer. Sparse -- a few dozen
    // probes is enough for a scale, and this runs on every database build.
    {
        double total = 0.0;
        int counted = 0;
        // **Weighted, because everything measured against this scale is weighted.** `costSpread`
        // is the denominator for §16's search severity and for §28's switch margin, both of which
        // compare *weighted* costs -- computing the scale unweighted would put the numerator and
        // the denominator in different units, which is the same asymmetry the hysteresis had.
        // Found by enumerating every feature-distance site rather than by tripping over it.
        const std::vector<float> spreadWeight = motionFeatureWeights(db.config);
        const bool spreadWeighted = spreadWeight.size() == dim;
        const std::uint32_t probe = std::max(db.sampleCount() / 32u, 1u);
        for (std::uint32_t s = 0; s < db.sampleCount(); s += probe) {
            const float* a = db.featuresFor(s);
            const float* b = db.featuresFor((s + db.sampleCount() / 2u) % db.sampleCount());
            float typical = 0.0f;
            for (std::size_t d = 0; d < dim; ++d) {
                const float delta = a[d] - b[d];
                typical += delta * delta * (spreadWeighted ? spreadWeight[d] : 1.0f);
            }
            total += typical;
            ++counted;
        }
        db.stats.costSpread = counted > 0 ? static_cast<float>(total / counted) : 0.0f;
    }

    // The rotation-invariant statistic: how much each feature joint's DISTANCE from the body
    // moved. Computed from the raw features before they were standardised, which is why it is
    // gathered in the loop above rather than here -- see `radiusSum`.
    db.stats.jointNames = options.config.joints;
    db.stats.jointRadiusSpread.assign(jointIndex.size(), 0.0f);
    for (std::size_t j = 0; j < jointIndex.size(); ++j) {
        const double n = static_cast<double>(radiusCount);
        if (n <= 1.0) {
            continue;
        }
        const double m = radiusSum[j] / n;
        const double var = std::max(0.0, (radiusSumSq[j] / n) - (m * m));
        db.stats.jointRadiusSpread[j] = static_cast<float>(std::sqrt(var));
    }

    db.stats.clips = static_cast<std::uint32_t>(db.clipNames.size());
    db.stats.samples = db.sampleCount();
    db.stats.dimension = db.dimension;
    db.stats.featureBytes = db.features.size() * sizeof(float);
    db.stats.metadataBytes =
        (db.sampleClip.size() * sizeof(std::uint32_t)) + (db.sampleTime.size() * sizeof(float)) +
        (db.samplePhase.size() * sizeof(float)) + (db.sampleTags.size() * sizeof(std::uint32_t)) +
        (db.sampleNext.size() * sizeof(std::uint32_t));
    // §38/§82: record what this was built from, and the identity a hot swap is checked against.
    stampMotionDatabase(db, pack, options);
    return db;
}

void normaliseQuery(const MotionDatabase& db, std::vector<float>& features) {
    const std::size_t dim = db.dimension;
    features.resize(dim, 0.0f);
    for (std::size_t d = 0; d < dim; ++d) {
        features[d] = (features[d] - db.mean[d]) * db.scale[d];
    }
}

MotionMatch searchMotion(const MotionDatabase& db, const MotionQuery& query,
                         const MotionCostWeights& weights) {
    MotionMatch best;
    const std::size_t dim = db.dimension;
    if (dim == 0 || query.features.size() != dim || db.sampleCount() == 0) {
        return best;
    }
    const float* q = query.features.data();
    // §10. Per-dimension weights, derived from the config rather than baked into the features, so
    // a weight is tunable **without rebuilding the database**. Recomputed per search rather than
    // cached: it is a few dozen floats against a scan of up to a million samples.
    //
    // §75: into per-thread scratch rather than fresh vectors, so a search allocates nothing once a
    // thread has searched once. Recomputed every time, still -- the weights stay tunable without a
    // rebuild, and a cache keyed on the config would be one more thing to invalidate.
    thread_local std::vector<MotionFeatureGroup> layout;
    thread_local std::vector<float> dimWeight;
    motionFeatureLayoutInto(db.config, layout);
    motionFeatureWeightsInto(db.config, dimWeight);
    const bool weighted = dimWeight.size() == dim && layout.size() == dim;

    // The family of whatever is playing, for the transition cost (§12). Read from tags and never
    // from a clip name, which §12 is explicit about.
    const std::uint32_t currentTags =
        query.current != MotionDatabase::kInvalid && query.current < db.sampleCount()
            ? db.sampleTags[query.current]
            : 0u;
    const std::uint32_t currentClip =
        query.current != MotionDatabase::kInvalid && query.current < db.sampleCount()
            ? db.sampleClip[query.current]
            : MotionDatabase::kInvalid;

    float bestCost = 0.0f;
    for (std::uint32_t s = 0; s < db.sampleCount(); ++s) {
        // §14: filtering before scoring. One AND per sample, and it removes whole clips at a time.
        const std::uint32_t tags = db.sampleTags[s];
        if ((query.requireTags != 0 && (tags & query.requireTags) != query.requireTags) ||
            (query.rejectTags != 0 && (tags & query.rejectTags) != 0)) {
            ++best.rejected;
            continue;
        }
        const float* f = db.featuresFor(s);
        // Squared distance, with an early out. **Benchmarked on real motion rather than a
        // synthetic fixture**, for the reason ADR-540 paid to learn: white noise made the early
        // out look 2.23x slower, and on real near queries it is a 0.80x win, because a real query
        // is close to its answer and far from everything else.
        float cost = 0.0f;
        for (std::size_t d = 0; d < dim; ++d) {
            const float delta = q[d] - f[d];
            cost += motionFeatureTerm(delta, weighted ? dimWeight[d] : 1.0f);
            if (best.found() && cost >= bestCost) {
                break;
            }
        }
        if (best.found() && cost >= bestCost) {
            ++best.considered;
            continue;
        }

        // §11 continuity: prefer carrying on. The penalty is on *not* being the continuation,
        // which is the term that stops the search hopping between unrelated clips whenever two
        // frames happen to rhyme.
        if (query.current != MotionDatabase::kInvalid) {
            const bool continues =
                query.current < db.sampleNext.size() && db.sampleNext[query.current] == s;
            if (!continues) {
                // §11: graded inside the clip, flat outside it. A candidate a few frames
                // further on in the same clip is a small skip; a candidate in another clip is a
                // cut, and only the second should cost what a cut costs.
                if (weights.continuityPerSecond > 0.0f && query.current < db.sampleClip.size() &&
                    db.sampleClip[s] == db.sampleClip[query.current]) {
                    const float gap = std::abs(db.sampleTime[s] - db.sampleTime[query.current]);
                    cost += std::min(gap * weights.continuityPerSecond, weights.continuity);
                } else {
                    cost += weights.continuity;
                }
                // §12 transition: an extra penalty for leaving the motion family, over and above
                // leaving the clip. A walk finding another walk is cheaper than a walk finding a
                // fall, even when the poses rhyme.
                if (db.sampleClip[s] != currentClip) {
                    const std::uint32_t shared = tags & currentTags;
                    const std::uint32_t wanted = currentTags;
                    if (wanted != 0 && shared != wanted) {
                        cost += weights.transition;
                    }
                }
            }
        }
        ++best.considered;
        if (!best.found() || cost < bestCost) {
            best.sample = s;
            bestCost = cost;
        }
    }
    best.cost = bestCost;

    // §10's breakdown, computed **once for the winner** rather than accumulated per candidate. Per
    // candidate it would cost seven accumulators on every one of a million samples to produce a
    // number thrown away for all but one of them -- and the early out means a losing candidate's
    // partial sums would be wrong anyway. Recomputing the winner in full is one extra pass over
    // `dim` floats, and it is the only one anything reads.
    if (best.found() && weighted) {
        const float* f = db.featuresFor(best.sample);
        for (std::size_t d = 0; d < dim; ++d) {
            const float delta = q[d] - f[d];
            best.breakdown.terms[static_cast<std::size_t>(layout[d])] +=
                delta * delta * dimWeight[d];
        }
        if (query.current != MotionDatabase::kInvalid) {
            const bool continues = query.current < db.sampleNext.size() &&
                                   db.sampleNext[query.current] == best.sample;
            if (!continues) {
                best.breakdown.continuity = weights.continuity;
                if (weights.continuityPerSecond > 0.0f && query.current < db.sampleClip.size() &&
                    db.sampleClip[best.sample] == db.sampleClip[query.current]) {
                    const float gap =
                        std::abs(db.sampleTime[best.sample] - db.sampleTime[query.current]);
                    best.breakdown.continuity =
                        std::min(gap * weights.continuityPerSecond, weights.continuity);
                }
                if (db.sampleClip[best.sample] != currentClip) {
                    const std::uint32_t shared = db.sampleTags[best.sample] & currentTags;
                    if (currentTags != 0 && shared != currentTags) {
                        best.breakdown.transition = weights.transition;
                    }
                }
            }
        }
    }
    return best;
}


MotionMatch searchMotionStaged(const MotionDatabase& db, const MotionQuery& query,
                               const MotionCostWeights& weights, const MotionSearchPlan& plan) {
    // An exhaustive plan is the linear scan, by delegation rather than by a parallel
    // implementation that has to be kept in step. §16 says "do not prematurely overengineer it",
    // and two copies of a cost function is the first way that goes wrong.
    if (plan.exhaustive()) {
        MotionMatch out = searchMotion(db, query, weights);
        out.coarseConsidered = out.considered;
        out.fullyScored = out.considered;
        return out;
    }

    MotionMatch best;
    const std::size_t dim = db.dimension;
    if (dim == 0 || query.features.size() != dim || db.sampleCount() == 0) {
        return best;
    }
    const std::vector<float> dimWeight = motionFeatureWeights(db.config);
    const bool weighted = dimWeight.size() == dim;
    const std::size_t prefix =
        plan.prefixDimensions == 0 ? dim : std::min<std::size_t>(plan.prefixDimensions, dim);
    const float* q = query.features.data();

    const auto passesTags = [&](std::uint32_t s) {
        const std::uint32_t tags = db.sampleTags[s];
        return !((query.requireTags != 0 && (tags & query.requireTags) != query.requireTags) ||
                 (query.rejectTags != 0 && (tags & query.rejectTags) != 0));
    };

    // Stage one: a strided pass over a prefix of the feature vector. No early-out here -- it needs
    // a running best to be worth anything, and the point of this pass is that every candidate is
    // cheap rather than that some are skipped.
    std::vector<std::pair<float, std::uint32_t>> shortlist;
    shortlist.reserve(plan.shortlist + 1u);
    const std::uint32_t stride = std::max(plan.stride, 1u);
    for (std::uint32_t s = 0; s < db.sampleCount(); s += stride) {
        if (!passesTags(s)) {
            ++best.rejected;
            continue;
        }
        ++best.coarseConsidered;
        const float* f = db.featuresFor(s);
        float cost = 0.0f;
        for (std::size_t d = 0; d < prefix; ++d) {
            const float delta = q[d] - f[d];
            cost += motionFeatureTerm(delta, weighted ? dimWeight[d] : 1.0f);
        }
        if (shortlist.size() < plan.shortlist) {
            shortlist.emplace_back(cost, s);
            std::push_heap(shortlist.begin(), shortlist.end());
        } else if (!shortlist.empty() && cost < shortlist.front().first) {
            std::pop_heap(shortlist.begin(), shortlist.end());
            shortlist.back() = {cost, s};
            std::push_heap(shortlist.begin(), shortlist.end());
        }
    }

    // Stage two: the full weighted cost, including continuity and transition, on the shortlist and
    // on the neighbours the coarse pass stepped over. **The neighbourhood is what stops striding
    // from permanently hiding an answer**: with stride 8 and neighbourhood 8 every sample in the
    // database is reachable from some shortlisted one, so the plan trades work for a chance of
    // missing rather than for a guarantee of it.
    std::vector<std::uint32_t> candidates;
    candidates.reserve(shortlist.size() * (2u * plan.neighbourhood + 1u));
    for (const auto& [cost, s] : shortlist) {
        const std::uint32_t lo = s > plan.neighbourhood ? s - plan.neighbourhood : 0u;
        const std::uint32_t hi = std::min(s + plan.neighbourhood, db.sampleCount() - 1u);
        for (std::uint32_t n = lo; n <= hi; ++n) {
            candidates.push_back(n);
        }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    MotionQuery narrowed = query;
    float bestCost = 0.0f;
    for (const std::uint32_t s : candidates) {
        if (!passesTags(s)) {
            continue;
        }
        ++best.fullyScored;
        const float* f = db.featuresFor(s);
        float cost = 0.0f;
        for (std::size_t d = 0; d < dim; ++d) {
            const float delta = q[d] - f[d];
            cost += motionFeatureTerm(delta, weighted ? dimWeight[d] : 1.0f);
        }
        if (query.current != MotionDatabase::kInvalid) {
            const bool continues =
                query.current < db.sampleNext.size() && db.sampleNext[query.current] == s;
            if (!continues) {
                // §11: graded inside the clip, flat outside it. A candidate a few frames
                // further on in the same clip is a small skip; a candidate in another clip is a
                // cut, and only the second should cost what a cut costs.
                if (weights.continuityPerSecond > 0.0f && query.current < db.sampleClip.size() &&
                    db.sampleClip[s] == db.sampleClip[query.current]) {
                    const float gap = std::abs(db.sampleTime[s] - db.sampleTime[query.current]);
                    cost += std::min(gap * weights.continuityPerSecond, weights.continuity);
                } else {
                    cost += weights.continuity;
                }
                const std::uint32_t currentClip = query.current < db.sampleClip.size()
                                                      ? db.sampleClip[query.current]
                                                      : MotionDatabase::kInvalid;
                if (db.sampleClip[s] != currentClip) {
                    const std::uint32_t currentTags = query.current < db.sampleTags.size()
                                                          ? db.sampleTags[query.current]
                                                          : 0u;
                    const std::uint32_t shared = db.sampleTags[s] & currentTags;
                    if (currentTags != 0 && shared != currentTags) {
                        cost += weights.transition;
                    }
                }
            }
        }
        if (!best.found() || cost < bestCost) {
            best.sample = s;
            bestCost = cost;
        }
    }
    best.considered = best.fullyScored;
    best.cost = bestCost;
    (void)narrowed;
    return best;
}

} // namespace avgen::scene
