#include "scene/retarget.hpp"

#include "scene/animation.hpp"

#include <fmt/format.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace avgen::scene {
namespace {

struct RoleNames {
    HumanoidRole role;
    const char* canonical;
};

constexpr std::array<RoleNames, static_cast<std::size_t>(HumanoidRole::Count)> kRoleNames{{
    {HumanoidRole::None, "none"},
    {HumanoidRole::Hips, "hips"},
    {HumanoidRole::Spine, "spine"},
    {HumanoidRole::Chest, "chest"},
    {HumanoidRole::Neck, "neck"},
    {HumanoidRole::Head, "head"},
    {HumanoidRole::LeftShoulder, "leftShoulder"},
    {HumanoidRole::LeftUpperArm, "leftUpperArm"},
    {HumanoidRole::LeftLowerArm, "leftLowerArm"},
    {HumanoidRole::LeftHand, "leftHand"},
    {HumanoidRole::RightShoulder, "rightShoulder"},
    {HumanoidRole::RightUpperArm, "rightUpperArm"},
    {HumanoidRole::RightLowerArm, "rightLowerArm"},
    {HumanoidRole::RightHand, "rightHand"},
    {HumanoidRole::LeftUpperLeg, "leftUpperLeg"},
    {HumanoidRole::LeftLowerLeg, "leftLowerLeg"},
    {HumanoidRole::LeftFoot, "leftFoot"},
    {HumanoidRole::LeftToe, "leftToe"},
    {HumanoidRole::RightUpperLeg, "rightUpperLeg"},
    {HumanoidRole::RightLowerLeg, "rightLowerLeg"},
    {HumanoidRole::RightFoot, "rightFoot"},
    {HumanoidRole::RightToe, "rightToe"},
}};

std::string lowered(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool has(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

// Which side a name is talking about. Deliberately conservative: `.l`, `_l`, `left`, and a trailing
// `l` after a separator all count, and anything ambiguous counts as neither.
enum class Side { None, Left, Right };

Side sideOf(const std::string& n) {
    const bool left = has(n, "left") || has(n, ".l") || has(n, "_l") || has(n, "-l") ||
                      (n.size() >= 2 && n.back() == 'l' && !std::isalpha(static_cast<unsigned char>(n[n.size() - 2])));
    const bool right = has(n, "right") || has(n, ".r") || has(n, "_r") || has(n, "-r") ||
                       (n.size() >= 2 && n.back() == 'r' && !std::isalpha(static_cast<unsigned char>(n[n.size() - 2])));
    if (left && !right) {
        return Side::Left;
    }
    if (right && !left) {
        return Side::Right;
    }
    return Side::None;
}

HumanoidRole sided(Side side, HumanoidRole left, HumanoidRole right) {
    if (side == Side::Left) {
        return left;
    }
    if (side == Side::Right) {
        return right;
    }
    return HumanoidRole::None;
}

// Model-space rotation of a joint under a pose, and its model-space position.
void modelRotations(const Skeleton& skeleton, const Pose& pose, std::vector<glm::mat4>& scratch,
                    std::vector<glm::quat>& rotations, std::vector<glm::vec3>& positions) {
    poseToModel(skeleton, pose, scratch);
    rotations.resize(scratch.size());
    positions.resize(scratch.size());
    for (std::size_t i = 0; i < scratch.size(); ++i) {
        positions[i] = glm::vec3(scratch[i][3]);
        // The basis may carry a scale; normalise the columns before reading a rotation out of it,
        // or a rig authored at anything but 1.0 comes back with a quaternion that is not one.
        glm::mat3 basis(scratch[i]);
        for (int c = 0; c < 3; ++c) {
            const float len = glm::length(basis[c]);
            basis[c] = len > 1e-8f ? basis[c] / len : glm::vec3(c == 0, c == 1, c == 2);
        }
        rotations[i] = glm::normalize(glm::quat_cast(basis));
    }
}

float degreesBetween(const glm::quat& a, const glm::quat& b) {
    const float dot = std::clamp(std::fabs(glm::dot(glm::normalize(a), glm::normalize(b))), 0.0f, 1.0f);
    return glm::degrees(2.0f * std::acos(dot));
}

} // namespace

const char* humanoidRoleName(HumanoidRole role) {
    const auto i = static_cast<std::size_t>(role);
    return i < kRoleNames.size() ? kRoleNames[i].canonical : "?";
}

bool humanoidRoleFromName(std::string_view name, HumanoidRole& out) {
    for (const RoleNames& entry : kRoleNames) {
        if (name == entry.canonical) {
            out = entry.role;
            return true;
        }
    }
    return false;
}

HumanoidRole roleForJointName(std::string_view raw) {
    const std::string n = lowered(raw);
    const Side side = sideOf(n);

    // Order matters: the more specific patterns first, because "upperleg" contains "leg" and
    // "lowerarm" contains "arm". Every arm of this is a guess and the caller is told so.
    if (has(n, "toe") || has(n, "ball")) {
        return sided(side, HumanoidRole::LeftToe, HumanoidRole::RightToe);
    }
    if (has(n, "foot") || has(n, "hoof") || has(n, "ankle")) {
        return sided(side, HumanoidRole::LeftFoot, HumanoidRole::RightFoot);
    }
    if (has(n, "upleg") || has(n, "upperleg") || has(n, "thigh")) {
        return sided(side, HumanoidRole::LeftUpperLeg, HumanoidRole::RightUpperLeg);
    }
    if (has(n, "lowerleg") || has(n, "calf") || has(n, "shin") || has(n, "knee")) {
        return sided(side, HumanoidRole::LeftLowerLeg, HumanoidRole::RightLowerLeg);
    }
    if (has(n, "hand") || has(n, "wrist")) {
        return sided(side, HumanoidRole::LeftHand, HumanoidRole::RightHand);
    }
    if (has(n, "forearm") || has(n, "lowerarm") || has(n, "elbow")) {
        return sided(side, HumanoidRole::LeftLowerArm, HumanoidRole::RightLowerArm);
    }
    if (has(n, "upperarm") || has(n, "uparm")) {
        return sided(side, HumanoidRole::LeftUpperArm, HumanoidRole::RightUpperArm);
    }
    if (has(n, "shoulder") || has(n, "clavicle")) {
        return sided(side, HumanoidRole::LeftShoulder, HumanoidRole::RightShoulder);
    }
    if (has(n, "head")) {
        return HumanoidRole::Head;
    }
    if (has(n, "neck")) {
        return HumanoidRole::Neck;
    }
    if (has(n, "chest") || has(n, "upperchest")) {
        return HumanoidRole::Chest;
    }
    if (has(n, "hips") || has(n, "pelvis")) {
        return HumanoidRole::Hips;
    }
    if (has(n, "spine")) {
        return HumanoidRole::Spine;
    }
    // A bare "leg" or "arm" with a side, only after the specific forms have had their chance.
    if (has(n, "leg")) {
        return sided(side, HumanoidRole::LeftLowerLeg, HumanoidRole::RightLowerLeg);
    }
    if (has(n, "arm")) {
        return sided(side, HumanoidRole::LeftLowerArm, HumanoidRole::RightLowerArm);
    }
    return HumanoidRole::None;
}

RoleGuess guessRetargetProfile(const Skeleton& source, const Skeleton& target) {
    RoleGuess out;
    out.profile.name = "guessed";
    out.profile.sourceSkeleton = source.name;
    out.profile.targetSkeleton = target.name;

    const auto firstOf = [](const Skeleton& sk, HumanoidRole role) -> int {
        // The FIRST joint with this role, in skeleton order -- parents precede children, so on a
        // rig with several joints that answer to "spine" this takes the highest, which is the one a
        // mapping wants.
        for (std::size_t i = 0; i < sk.joints.size(); ++i) {
            if (roleForJointName(sk.joints[i].name) == role) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };

    for (std::size_t r = 1; r < static_cast<std::size_t>(HumanoidRole::Count); ++r) {
        const auto role = static_cast<HumanoidRole>(r);
        const int s = firstOf(source, role);
        const int t = firstOf(target, role);
        if (s < 0) {
            out.unmatchedSource.push_back(role);
        }
        if (t < 0) {
            out.unmatchedTarget.push_back(role);
        }
        if (s < 0 || t < 0) {
            continue;
        }
        JointMapping mapping;
        mapping.source = source.joints[static_cast<std::size_t>(s)].name;
        mapping.target = target.joints[static_cast<std::size_t>(t)].name;
        mapping.role = role;
        out.profile.joints.push_back(std::move(mapping));
    }
    return out;
}

RetargetBinding bindRetarget(const Skeleton& source, const Skeleton& target,
                             const RetargetProfile& profile) {
    RetargetBinding binding;
    if (!source.valid() || !target.valid()) {
        binding.problems.emplace_back("retarget: one of the skeletons is not valid");
        return binding;
    }

    // Rest poses in model space: the whole of what a bind-pose reconciliation needs.
    Pose sourceRest = restPose(source);
    Pose targetRest = restPose(target);
    std::vector<glm::mat4> scratch;
    std::vector<glm::quat> sourceRot;
    std::vector<glm::vec3> sourcePos;
    std::vector<glm::quat> targetRot;
    std::vector<glm::vec3> targetPos;
    modelRotations(source, sourceRest, scratch, sourceRot, sourcePos);
    modelRotations(target, targetRest, scratch, targetRot, targetPos);

    for (const JointMapping& mapping : profile.joints) {
        const int s = source.find(mapping.source);
        const int t = target.find(mapping.target);
        if (s < 0) {
            binding.problems.push_back(
                fmt::format("retarget: the source skeleton has no joint '{}'", mapping.source));
            continue;
        }
        if (t < 0) {
            binding.problems.push_back(
                fmt::format("retarget: the target skeleton has no joint '{}'", mapping.target));
            continue;
        }
        RetargetBinding::Link link;
        link.source = s;
        link.target = t;
        link.role = mapping.role;
        // The rest correction. A retarget copies MODEL-space orientation, so what has to be carried
        // across is the difference between the two rigs' ideas of "this bone, unrotated":
        //
        //     targetModel(t) = sourceModel(t) * inverse(sourceRestModel) * targetRestModel
        //
        // and `delta` is that trailing pair. On identical skeletons it is the identity and the
        // retarget is exactly the identity too, which is the arm the tests lead with.
        link.delta = glm::normalize(glm::conjugate(sourceRot[static_cast<std::size_t>(s)]) *
                                    targetRot[static_cast<std::size_t>(t)]);
        binding.links.push_back(link);
    }

    // Parent-first. `poseToModel` needs a joint's parent to be final before the joint is written,
    // and the skeleton is topologically ordered, so sorting the links by TARGET index once here is
    // what makes the per-frame write-back correct without a second traversal.
    std::sort(binding.links.begin(), binding.links.end(),
              [](const RetargetBinding::Link& a, const RetargetBinding::Link& b) {
                  return a.target < b.target;
              });

    if (binding.links.empty()) {
        binding.problems.emplace_back(
            "retarget: no mapping resolved against both skeletons, so nothing can be transferred");
        return binding;
    }

    // The root: the joint whose translation becomes travel.
    int rootLink = -1;
    if (!profile.rootJoint.empty()) {
        for (std::size_t i = 0; i < binding.links.size(); ++i) {
            if (source.joints[static_cast<std::size_t>(binding.links[i].source)].name == profile.rootJoint) {
                rootLink = static_cast<int>(i);
                break;
            }
        }
        if (rootLink < 0) {
            binding.problems.push_back(fmt::format(
                "retarget: rootJoint '{}' is not one of the mapped source joints; falling back to the "
                "hips mapping",
                profile.rootJoint));
        }
    }
    if (rootLink < 0) {
        for (std::size_t i = 0; i < binding.links.size(); ++i) {
            if (binding.links[i].role == HumanoidRole::Hips) {
                rootLink = static_cast<int>(i);
                break;
            }
        }
    }
    if (rootLink < 0) {
        rootLink = 0;
    }
    binding.rootLink = rootLink;

    // The translation scale. Derived from the two rest poses rather than authored: the ratio of the
    // mapped root's height above its own skeleton's lowest mapped joint. That is a measurable proxy
    // for leg length, and leg length is what decides how far a stride carries a body.
    binding.rootTranslationOnly = profile.rootTranslationOnly;
    binding.rootScale = profile.rootScale;
    if (binding.rootScale <= 0.0f) {
        const auto spanOf = [&](const std::vector<glm::vec3>& pos, bool useSource) {
            float lowest = std::numeric_limits<float>::max();
            for (const RetargetBinding::Link& link : binding.links) {
                const int j = useSource ? link.source : link.target;
                lowest = std::min(lowest, pos[static_cast<std::size_t>(j)].y);
            }
            const int r = useSource ? binding.links[static_cast<std::size_t>(rootLink)].source
                                    : binding.links[static_cast<std::size_t>(rootLink)].target;
            return pos[static_cast<std::size_t>(r)].y - lowest;
        };
        const float sourceSpan = spanOf(sourcePos, true);
        const float targetSpan = spanOf(targetPos, false);
        binding.rootScale = (sourceSpan > 1e-5f) ? (targetSpan / sourceSpan) : 1.0f;
        if (!(binding.rootScale > 0.0f) || !std::isfinite(binding.rootScale)) {
            binding.rootScale = 1.0f;
            binding.problems.emplace_back(
                "retarget: the two rest poses give no usable height ratio; translation is unscaled");
        }
    }
    // Per-joint translation scale. Computed after `rootScale`, because a joint sitting exactly on
    // its parent has no rest offset to take a ratio of and falls back to the whole-rig figure.
    for (RetargetBinding::Link& link : binding.links) {
        const float sourceOffset = glm::length(sourceRest.local[static_cast<std::size_t>(link.source)].position);
        const float targetOffset = glm::length(targetRest.local[static_cast<std::size_t>(link.target)].position);
        link.translationScale = (sourceOffset > 1e-5f && targetOffset > 1e-5f)
                                    ? (targetOffset / sourceOffset)
                                    : binding.rootScale;
    }
    return binding;
}

AnimationClip retargetClip(const AnimationClip& clip, const Skeleton& source, const Skeleton& target,
                           const RetargetBinding& binding, RetargetStats* stats) {
    AnimationClip out;
    out.name = clip.name;
    if (!binding.usable()) {
        return out;
    }
    const float length = clip.length();
    const float rate = 30.0f;
    const auto samples =
        static_cast<std::size_t>(std::max(2.0f, std::floor(length * rate + 0.5f) + 1.0f));
    out.start = 0.0f;
    out.duration = length;

    // One rotation channel per mapped target joint, plus one translation channel for the root.
    std::vector<AnimationChannel> rotations(binding.links.size());
    for (std::size_t i = 0; i < binding.links.size(); ++i) {
        rotations[i].joint = static_cast<std::uint32_t>(binding.links[i].target);
        rotations[i].path = AnimationPath::Rotation;
        rotations[i].interpolation = Interpolation::Linear;
        rotations[i].times.reserve(samples);
        rotations[i].values.reserve(samples);
    }
    // A translation channel per mapped joint. Most will be dropped at the end -- a channel whose
    // values never leave the target's rest position carries no information and is not written --
    // so a rotation-driven rig still produces rotation-only output.
    std::vector<AnimationChannel> translations(binding.links.size());
    for (std::size_t i = 0; i < binding.links.size(); ++i) {
        translations[i].joint = static_cast<std::uint32_t>(binding.links[i].target);
        translations[i].path = AnimationPath::Translation;
        translations[i].interpolation = Interpolation::Linear;
        translations[i].times.reserve(samples);
        translations[i].values.reserve(samples);
    }

    Pose sourcePose;
    Pose targetPose = restPose(target);
    const Pose targetRest = targetPose;
    std::vector<glm::mat4> scratch;
    std::vector<glm::quat> sourceRot;
    std::vector<glm::vec3> sourcePos;
    std::vector<glm::quat> wantRot(binding.links.size());

    // The source's rest pose: every translation is transferred as a DEVIATION from it rather than
    // as an absolute position in the source's units.
    const Pose sourceRest = restPose(source);

    const bool profileRootOnly = binding.rootTranslationOnly;
    double errorSum = 0.0;
    std::uint32_t errorCount = 0;
    float worstError = 0.0f;
    float worstBone = 0.0f;

    for (std::size_t s = 0; s < samples; ++s) {
        const float t = clip.start + (static_cast<float>(s) / rate);
        setRestPose(source, sourcePose);
        sampleClip(clip, std::min(t, clip.duration), sourcePose);
        modelRotations(source, sourcePose, scratch, sourceRot, sourcePos);

        // The model-space orientation each target joint should adopt.
        for (std::size_t i = 0; i < binding.links.size(); ++i) {
            wantRot[i] = glm::normalize(sourceRot[static_cast<std::size_t>(binding.links[i].source)] *
                                        binding.links[i].delta);
        }

        // Convert to locals, parent-first. `targetPose` starts as the rest pose and is overwritten
        // joint by joint; because the skeleton is topologically ordered and the links are visited
        // in skeleton order, a joint's parent is already final when the joint is reached.
        //
        // Sorting the links by target index is what guarantees that, and it is done once below.
        std::vector<glm::mat4> targetModel;
        poseToModel(target, targetPose, targetModel);
        for (std::size_t i = 0; i < binding.links.size(); ++i) {
            const auto tj = static_cast<std::size_t>(binding.links[i].target);
            const int parent = target.joints[tj].parent;
            glm::quat parentRot(1.0f, 0.0f, 0.0f, 0.0f);
            if (parent >= 0) {
                glm::mat3 basis(targetModel[static_cast<std::size_t>(parent)]);
                for (int c = 0; c < 3; ++c) {
                    const float len = glm::length(basis[c]);
                    basis[c] = len > 1e-8f ? basis[c] / len : glm::vec3(c == 0, c == 1, c == 2);
                }
                parentRot = glm::normalize(glm::quat_cast(basis));
            }
            // **Rotation only.** The target keeps its own rest translation at every joint but the
            // root, which is what preserves its bone lengths. A retarget that copied translation
            // channels would stretch the target onto the source's proportions -- the failure mode
            // that makes a retargeted character look melted rather than merely mis-mapped.
            targetPose.local[tj].rotation = glm::normalize(glm::conjugate(parentRot) * wantRot[i]);
            // Translation as a DEVIATION from rest, scaled to this joint's own bone-length ratio.
            // Identical rigs reproduce exactly; a joint the source never translates contributes
            // nothing; and the target's proportions are its own, because the deviation is added to
            // the TARGET's rest offset rather than replacing it with the source's absolute value.
            const auto sj = static_cast<std::size_t>(binding.links[i].source);
            glm::vec3 position = targetRest.local[tj].position;
            if (!profileRootOnly || static_cast<int>(i) == binding.rootLink) {
                // The ROOT is scaled by the whole-rig figure rather than by its own bone ratio,
                // because its translation is the body's travel and travel scales with the body, not
                // with the length of one bone. Everything else uses its own joint's ratio.
                //
                // Both go through the same local-deviation rule. An earlier version took the root's
                // displacement in MODEL space instead, which is the textbook formulation and is
                // wrong here: the alien's `root.x` hangs off an armature wrapper, so its model
                // displacement is not its local one, and an identity retarget came back 0.08 model
                // units off at the feet. A deviation from rest, in the joint's own frame, is exact
                // for the identity case by construction.
                const float scale = (static_cast<int>(i) == binding.rootLink)
                                        ? binding.rootScale
                                        : binding.links[i].translationScale;
                const glm::vec3 deviation = sourcePose.local[sj].position - sourceRest.local[sj].position;
                position += deviation * scale;
            }
            targetPose.local[tj].position = position;
            targetPose.local[tj].scale = targetRest.local[tj].scale;
            poseToModel(target, targetPose, targetModel);
        }

        // Record.
        for (std::size_t i = 0; i < binding.links.size(); ++i) {
            const auto tj = static_cast<std::size_t>(binding.links[i].target);
            rotations[i].times.push_back(static_cast<float>(s) / rate);
            const glm::quat& q = targetPose.local[tj].rotation;
            rotations[i].values.emplace_back(q.x, q.y, q.z, q.w);
            translations[i].times.push_back(static_cast<float>(s) / rate);
            const glm::vec3& p = targetPose.local[tj].position;
            translations[i].values.emplace_back(p.x, p.y, p.z, 0.0f);
        }

        // Error: how far the target's achieved model orientation is from what was asked for.
        if (stats != nullptr) {
            std::vector<glm::quat> gotRot;
            std::vector<glm::vec3> gotPos;
            modelRotations(target, targetPose, scratch, gotRot, gotPos);
            for (std::size_t i = 0; i < binding.links.size(); ++i) {
                const auto tj = static_cast<std::size_t>(binding.links[i].target);
                const float err = degreesBetween(gotRot[tj], wantRot[i]);
                worstError = std::max(worstError, err);
                errorSum += err;
                ++errorCount;
                // Bone length: the distance from this joint to its parent, against the rest pose.
                // The ROOT link is excluded: its translation is the body's travel, so its distance
                // from its parent is supposed to change and counting it would make every travelling
                // clip look like a stretched skeleton.
                const int parent = target.joints[tj].parent;
                if (parent >= 0 && static_cast<int>(i) != binding.rootLink) {
                    const float now = glm::length(gotPos[tj] - gotPos[static_cast<std::size_t>(parent)]);
                    const float rest = glm::length(targetRest.local[tj].position);
                    worstBone = std::max(worstBone, std::fabs(now - rest));
                }
            }
        }
    }

    for (AnimationChannel& channel : rotations) {
        if (channel.times.size() >= 2) {
            out.channels.push_back(std::move(channel));
        }
    }
    for (std::size_t i = 0; i < translations.size(); ++i) {
        AnimationChannel& channel = translations[i];
        if (channel.times.size() < 2) {
            continue;
        }
        // Drop a channel that never leaves its first value. On a rotation-driven rig that is every
        // joint but the root, and writing 89 constant channels per clip is how an asset ends up
        // 86% animation data (see `assets/aliens/ATTRIBUTION.md`).
        bool varies = false;
        for (std::size_t k = 1; k < channel.values.size() && !varies; ++k) {
            varies = glm::length(glm::vec3(channel.values[k]) - glm::vec3(channel.values[0])) > 1e-6f;
        }
        if (varies || static_cast<int>(i) == binding.rootLink) {
            out.channels.push_back(std::move(channel));
        }
    }
    if (stats != nullptr) {
        stats->frames = static_cast<std::uint32_t>(samples);
        stats->channels = static_cast<std::uint32_t>(out.channels.size());
        stats->worstOrientationError = worstError;
        stats->meanOrientationError =
            errorCount > 0 ? static_cast<float>(errorSum / static_cast<double>(errorCount)) : 0.0f;
        stats->worstBoneLengthError = worstBone;
    }
    return out;
}

} // namespace avgen::scene
