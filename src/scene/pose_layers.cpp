#include "scene/pose_layers.hpp"

#include "scene/animation.hpp"

#include <fmt/format.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::scene {
namespace {

constexpr float kDegrees = 57.2957795131f;
constexpr glm::quat kIdentity{1.0f, 0.0f, 0.0f, 0.0f};

[[nodiscard]] glm::vec3 safeNormalize(const glm::vec3& v) {
    const float len2 = glm::dot(v, v);
    return len2 > 1e-12f ? v * (1.0f / std::sqrt(len2)) : glm::vec3(0.0f);
}

[[nodiscard]] float wrapPi(float a) {
    while (a > 3.14159265358979f) {
        a -= 6.28318530717959f;
    }
    while (a < -3.14159265358979f) {
        a += 6.28318530717959f;
    }
    return a;
}

// The clip second an additive layer samples at. Wrapped in double and only then narrowed, because
// a timeline is minutes long and a float loses a millisecond of a walk cycle somewhere past twenty
// minutes -- which is exactly the sort of drift that shows up as "the offline render does not match
// the window" and gets blamed on the renderer.
[[nodiscard]] float additivePhase(const AnimationClip& clip, double now, float rate) {
    const double span = static_cast<double>(clip.length());
    if (span <= 0.0) {
        return clip.start;
    }
    double u = std::fmod(now * static_cast<double>(rate), span);
    if (u < 0.0) {
        u += span;
    }
    return clip.start + static_cast<float>(u);
}

} // namespace

const char* poseLayerKindName(PoseLayerKind kind) {
    switch (kind) {
    case PoseLayerKind::Aim: return "aim";
    case PoseLayerKind::Additive: return "additive";
    case PoseLayerKind::Foot: return "foot";
    case PoseLayerKind::Stride: return "stride";
    case PoseLayerKind::Secondary: return "secondary";
    case PoseLayerKind::Lean: return "lean";
    case PoseLayerKind::Reach: return "reach";
    }
    return "aim";
}

std::uint32_t seedFromName(std::string_view name) {
    std::uint32_t h = 2166136261u;
    for (const char c : name) {
        h ^= static_cast<std::uint32_t>(static_cast<unsigned char>(c));
        h *= 16777619u;
    }
    return h;
}

float seedPhase(std::uint32_t characterSeed, std::uint32_t layerSeed) {
    // Zero and zero means "unseeded", and it has to come out as exactly zero rather than as some
    // arbitrary hash of two zeroes -- otherwise adding this field would have silently shifted
    // every existing secondary layer in the repository, and the first anyone would know is that a
    // render no longer matched.
    if (characterSeed == 0u && layerSeed == 0u) {
        return 0.0f;
    }
    // A bit-mixer, not a random number generator: no state, no sequence, no order dependence.
    std::uint32_t h = characterSeed * 2654435761u;
    h ^= layerSeed + 2654435769u + (h << 6) + (h >> 2);
    h ^= h >> 16;
    h *= 2246822507u;
    h ^= h >> 13;
    h *= 3266489909u;
    h ^= h >> 16;
    return static_cast<float>(h) / 4294967296.0f;
}

int poseLayerStage(PoseLayerKind kind) {
    // §5's chain, as numbers. The gaps are deliberate: a kind added between two of these needs a
    // number, and a dense sequence would force renumbering the ones around it.
    switch (kind) {
    case PoseLayerKind::Stride:    return 10;  // stride adjustment, on the base pose
    case PoseLayerKind::Lean:      return 20;  // turn and acceleration adaptation of the body
    case PoseLayerKind::Secondary: return 30;  // breathing and idle life, on the adapted body
    case PoseLayerKind::Aim:       return 40;  // look, which is upper body and independent
    case PoseLayerKind::Additive:  return 50;  // a reaction played on top of all of it
    // **The two IK solves run last, and that is the whole point of this function.** They put an
    // end effector at a place in the world; anything that moved the body afterwards would move the
    // effector off it. A foot planted and then displaced by a stride warp is planted nowhere.
    case PoseLayerKind::Foot:      return 60;
    case PoseLayerKind::Reach:     return 70;
    }
    return 100;
}

bool poseLayerKindFromName(std::string_view name, PoseLayerKind& out) {
    if (name == "aim") {
        out = PoseLayerKind::Aim;
        return true;
    }
    if (name == "additive") {
        out = PoseLayerKind::Additive;
        return true;
    }
    if (name == "foot") {
        out = PoseLayerKind::Foot;
        return true;
    }
    if (name == "stride") {
        out = PoseLayerKind::Stride;
        return true;
    }
    if (name == "secondary") {
        out = PoseLayerKind::Secondary;
        return true;
    }
    if (name == "lean") {
        out = PoseLayerKind::Lean;
        return true;
    }
    if (name == "reach") {
        out = PoseLayerKind::Reach;
        return true;
    }
    return false;
}

const char* poseLayerDriveName(PoseLayerDrive drive) {
    switch (drive) {
    case PoseLayerDrive::Manual: return "manual";
    case PoseLayerDrive::Look: return "look";
    case PoseLayerDrive::Reaction: return "reaction";
    case PoseLayerDrive::Ground: return "ground";
    }
    return "manual";
}

bool poseLayerDriveFromName(std::string_view name, PoseLayerDrive& out) {
    if (name == "manual") {
        out = PoseLayerDrive::Manual;
        return true;
    }
    if (name == "look") {
        out = PoseLayerDrive::Look;
        return true;
    }
    if (name == "reaction") {
        out = PoseLayerDrive::Reaction;
        return true;
    }
    if (name == "ground") {
        out = PoseLayerDrive::Ground;
        return true;
    }
    return false;
}

const char* layerResolutionName(LayerResolution r) {
    switch (r) {
    case LayerResolution::Inactive: return "inactive";
    case LayerResolution::NoJoints: return "no-joints";
    case LayerResolution::NoPivot: return "no-pivot";
    case LayerResolution::NoSource: return "no-source";
    case LayerResolution::NoTarget: return "no-target";
    case LayerResolution::NoChain: return "no-chain";
    case LayerResolution::Clamped: return "clamped";
    case LayerResolution::Degenerate: return "degenerate";
    case LayerResolution::Applied: return "applied";
    }
    return "inactive";
}

// The aim, as a direction decision rather than an axis decision.
//
// Written as "clamp the wanted direction, then take the shortest arc to it" rather than as
// "decompose the rotation and clamp its components", because the second needs a right-hand axis and
// a composition order, and this repository has already paid for exactly that mistake once: ADR-260's
// slope lean resolves pitch in the body frame and roll in the world frame because the Euler triple
// it writes into composes Rz*Ry*Rx, and a quarter of all headings gimbal-lock. A direction has no
// composition order to get wrong.
glm::quat aimRotation(const glm::vec3& from, const glm::vec3& to, float maxYaw, float maxPitch) {
    const glm::vec3 f = safeNormalize(from);
    const glm::vec3 t = safeNormalize(to);
    if (glm::dot(f, f) < 0.5f || glm::dot(t, t) < 0.5f) {
        return kIdentity;
    }
    // Model space is Y-up: glTF says so, and `poseToModel` lands in the file's own scene space.
    const float e0 = std::asin(std::clamp(f.y, -1.0f, 1.0f));
    const float e1 = std::asin(std::clamp(t.y, -1.0f, 1.0f));
    const float a0 = std::atan2(f.x, f.z);
    const float a1 = std::atan2(t.x, t.z);
    const float elevation = e0 + std::clamp(e1 - e0, -maxPitch, maxPitch);
    const float azimuth = a0 + std::clamp(wrapPi(a1 - a0), -maxYaw, maxYaw);
    const glm::vec3 wanted(std::cos(elevation) * std::sin(azimuth), std::sin(elevation),
                           std::cos(elevation) * std::cos(azimuth));
    const float d = std::clamp(glm::dot(f, wanted), -1.0f, 1.0f);
    if (d > 0.9999999f) {
        return kIdentity;
    }
    const glm::vec3 axis = glm::cross(f, wanted);
    const float len = glm::length(axis);
    if (len < 1e-7f) {
        return kIdentity; // antiparallel is unreachable under any sane clamp; refuse rather than spin
    }
    return glm::normalize(glm::angleAxis(std::acos(d), axis * (1.0f / len)));
}

glm::vec3 plantOnPlane(const glm::vec3& tip, const glm::vec3& planePoint, const glm::vec3& planeNormal,
                       float offset) {
    const glm::vec3 n = safeNormalize(planeNormal);
    // A normal with no vertical component describes a wall, and a vertical drop onto a wall has no
    // answer -- the ray is parallel to the surface. Leaving the foot where the animation put it is
    // the only honest response, and it is what a near-vertical cliff face should produce.
    if (n.y < 1e-3f) {
        return tip;
    }
    // Solve n . (tip + (h - tip.y) * Y - planePoint) = 0 for h: the height at which a vertical drop
    // from the foot meets the plane.
    const float h = planePoint.y - (n.x * (tip.x - planePoint.x) + n.z * (tip.z - planePoint.z)) / n.y;
    return glm::vec3(tip.x, h + offset, tip.z);
}

PoseLayer* PoseLayerStack::find(PoseLayerDrive drive) {
    for (PoseLayer& layer : layers_) {
        if (layer.drive == drive) {
            return &layer;
        }
    }
    return nullptr;
}

const PoseLayer* PoseLayerStack::find(std::string_view name) const {
    for (const PoseLayer& layer : layers_) {
        if (layer.name == name) {
            return &layer;
        }
    }
    return nullptr;
}

void PoseLayerStack::clear() {
    layers_.clear();
    masks_.clear();
    results_.clear();
    clipIndex_.clear();
    pivotIndex_.clear();
    chain_.clear();
    chainLinked_.clear();
    soleUp_.clear();
    restTipHeight_.clear();
    ikStatus_.clear();
}

std::vector<std::string> PoseLayerStack::bind(std::vector<PoseLayer> layers, const Skeleton& skeleton,
                                              const std::vector<AnimationClip>& clips) {
    layers_ = std::move(layers);
    return rebind(skeleton, clips);
}

std::vector<std::string> PoseLayerStack::rebind(const Skeleton& skeleton,
                                                const std::vector<AnimationClip>& clips) {
    std::vector<std::string> problems;
    masks_.clear();
    clipIndex_.assign(layers_.size(), -1);
    pivotIndex_.assign(layers_.size(), -1);
    chain_.assign(layers_.size(), glm::ivec3(-1));
    chainLinked_.assign(layers_.size(), glm::ivec2(0));
    soleUp_.assign(layers_.size(), glm::vec3(0.0f, 1.0f, 0.0f));
    stride_.assign(layers_.size(), glm::ivec2(-1));
    order_.clear();
    restTipHeight_.assign(layers_.size(), 0.0f);
    ikStatus_.assign(layers_.size(), IkStatus::Solved);
    bodyResult_ = BodyCompensation{};
    bodyJoint_ = -1;
    if (bodySpec_.enabled) {
        if (bodySpec_.joint.empty()) {
            bodyJoint_ = skeleton.jointCount() > 0 ? 0 : -1;
        } else {
            bodyJoint_ = skeleton.find(bodySpec_.joint);
            if (bodyJoint_ < 0) {
                problems.push_back(fmt::format(
                    "body compensation: this rig has no joint '{}', so no limb that cannot reach will "
                    "ever be helped",
                    bodySpec_.joint));
            }
        }
    }
    results_.assign(layers_.size(), LayerResolution::Inactive);
    // §5. The order `apply` will run these in, by pipeline stage rather than by the order the
    // scene file happened to list them. Stable within a stage, so two foot layers stay left then
    // right.
    order_.resize(layers_.size());
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        order_[i] = static_cast<std::uint32_t>(i);
    }
    std::stable_sort(order_.begin(), order_.end(), [this](std::uint32_t a, std::uint32_t b) {
        return poseLayerStage(layers_[a].kind) < poseLayerStage(layers_[b].kind);
    });
    masks_.reserve(layers_.size());
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        const PoseLayer& layer = layers_[i];
        // A foot layer's joint set *is* its chain, and the mask is derived from it rather than
        // authored. ADR-359: a two-bone solve is not maskable per joint -- half a knee does not
        // reach half a target -- so an authored mask on one could only be a silent no-op, which is
        // the failure this whole unit exists to stop repeating. It is reported instead.
        // ---- the tripwire -------------------------------------------------------------------
        //
        // **Four times in this programme a branch saying "anything that is not X" has silently
        // swallowed a new `PoseLayerKind`:** `rebind`'s additive clip lookup, the parser's
        // hand-written kind list, the scene validator's mask check, and `apply`'s own `kind == Foot`.
        //
        // A catch-all is a claim about every value that will ever be added to the enum, and the
        // author of the next value pays. This switch exists only so the compiler makes them pay
        // at build time instead: `-Wall` implies `-Wswitch`, so adding a kind without visiting
        // this function is a build error rather than a silent no-op.
        switch (layer.kind) {
        case PoseLayerKind::Aim:
        case PoseLayerKind::Additive:
        case PoseLayerKind::Foot:
        case PoseLayerKind::Stride:
        case PoseLayerKind::Secondary:
        case PoseLayerKind::Lean:
        case PoseLayerKind::Reach:
            break;
        }

        JointMaskSpec spec = layer.mask;
        if (layer.kind == PoseLayerKind::Stride && spec.joints.empty() && !layer.strideJoint.empty()) {
            // A stride layer's joint set IS its named joint, the same way a foot layer's is its
            // chain. Derived rather than authored so that naming the joint twice cannot disagree
            // with itself.
            spec.joints = {layer.strideJoint};
        }
        if (layer.kind == PoseLayerKind::Foot || layer.kind == PoseLayerKind::Reach) {
            if (!spec.joints.empty()) {
                problems.push_back(fmt::format(
                    "layer '{}': a foot layer is driven by its chain and ignores the {} joint(s) its mask "
                    "names; a two-bone solve cannot be applied to some of its joints and not others",
                    layer.name, spec.joints.size()));
            }
            spec = JointMaskSpec{};
            spec.joints = {layer.chainRoot, layer.chainMid, layer.chainTip};
        }
        JointMask mask = resolveJointMask(skeleton, spec);
        for (const std::string& missing : mask.missing) {
            problems.push_back(fmt::format("layer '{}': this rig has no joint '{}'", layer.name, missing));
        }
        if (mask.empty()) {
            problems.push_back(fmt::format(
                "layer '{}': its mask named {} joint(s) and this rig carries none of them, so the layer "
                "can never do anything",
                layer.name, mask.named));
        }
        if (mask.nested > 0 && layer.kind == PoseLayerKind::Aim) {
            problems.push_back(fmt::format(
                "layer '{}': {} of its {} masked joints sit inside another masked joint, so an aim "
                "rotation is applied to them twice",
                layer.name, mask.nested, mask.joints));
        }
        if (layer.kind == PoseLayerKind::Aim) {
            if (!layer.pivot.empty()) {
                pivotIndex_[i] = skeleton.find(layer.pivot);
                if (pivotIndex_[i] < 0) {
                    problems.push_back(
                        fmt::format("layer '{}': pivot joint '{}' is not in this rig", layer.name, layer.pivot));
                }
            } else {
                // The first masked joint, in skeleton order. Stated rather than left to chance:
                // parents precede children, so this is the highest joint of the group.
                for (std::size_t j = 0; j < mask.weight.size(); ++j) {
                    if (mask.weight[j] > 0.0f) {
                        pivotIndex_[i] = static_cast<int>(j);
                        break;
                    }
                }
            }
        } else if (layer.kind == PoseLayerKind::Stride) {
            const int joint = skeleton.find(layer.strideJoint);
            if (joint < 0) {
                problems.push_back(fmt::format(
                    "layer '{}': stride joint '{}' is not in this rig, so no step can be shortened",
                    layer.name, layer.strideJoint));
            }
            // The body the excursion is measured from. Empty means ADR-337's rule -- the joint the
            // clips actually translate -- and NOT joint 0, because on `alien-scout.glb` joint 0 is
            // `rig`, an armature wrapper no clip animates. Measuring from it would make the whole
            // body's travel look like stride and scale the character into the ground.
            int origin = -1;
            if (!layer.strideOrigin.empty()) {
                origin = skeleton.find(layer.strideOrigin);
                if (origin < 0) {
                    problems.push_back(fmt::format(
                        "layer '{}': stride origin '{}' is not in this rig", layer.name,
                        layer.strideOrigin));
                }
            } else {
                for (const AnimationClip& clip : clips) {
                    for (const AnimationChannel& channel : clip.channels) {
                        if (channel.path == AnimationPath::Translation &&
                            (origin < 0 || static_cast<int>(channel.joint) < origin)) {
                            origin = static_cast<int>(channel.joint);
                        }
                    }
                }
                if (origin < 0) {
                    origin = skeleton.jointCount() > 0 ? 0 : -1;
                }
            }
            if (joint >= 0 && origin >= 0 && joint == origin) {
                problems.push_back(fmt::format(
                    "layer '{}': its stride joint and its origin are both '{}', so the excursion it "
                    "scales is always zero",
                    layer.name, layer.strideJoint));
                origin = -1;
            }
            stride_[i] = glm::ivec2(joint, origin);
        } else if (layer.kind == PoseLayerKind::Foot || layer.kind == PoseLayerKind::Reach) {
            const int root = skeleton.find(layer.chainRoot);
            const int mid = skeleton.find(layer.chainMid);
            const int tip = skeleton.find(layer.chainTip);
            // `resolveJointMask` has already reported any of the three this rig does not carry.
            if (root >= 0 && mid >= 0 && tip >= 0) {
                // The part that is not a name lookup. Three joints that all exist are not a chain:
                // on `alien-scout.glb` the obvious three -- `thigh_stretch.l`, `leg_stretch.l`,
                // `foot.l` -- are three separate branches under two different parents, and a solver
                // handed them would happily produce rotations for a limb that does not exist.
                const auto descends = [&skeleton](int from, int ancestor) {
                    return descendsFrom(skeleton, from, ancestor);
                };
                if (root == mid || mid == tip || root == tip) {
                    problems.push_back(fmt::format(
                        "layer '{}': its chain names the same joint twice ('{}', '{}', '{}'), which is not a "
                        "two-bone chain",
                        layer.name, layer.chainRoot, layer.chainMid, layer.chainTip));
                } else {
                    // ADR-543. Ancestry is RECORDED, not required. It used to be a refusal, and the
                    // refusal was measured wrong: `tools/motion_probe.cpp` solved the alien's three
                    // detached leg joints and wrote them back faithful to 1.2e-7, with bone lengths
                    // preserved to 0.000000. What ancestry actually decides is which model-space
                    // transform each written joint's PARENT has undergone by the time the local is
                    // recovered through it, and `apply` now works that out per joint instead of
                    // assuming it.
                    //
                    // Kept as data because it is the difference between "the intermediate joints
                    // ride along" and "they do not", which is a fact about the rig a reader of a
                    // stats panel should be able to see rather than infer.
                    chainLinked_[i] = glm::ivec2(descends(mid, root) ? 1 : 0, descends(tip, mid) ? 1 : 0);
                    chain_[i] = glm::ivec3(root, mid, tip);
                    // The sole's up, resolved against the rest pose once. `reference_` is the
                    // scratch pose the additive path also uses; nothing here runs per frame.
                    setRestPose(skeleton, reference_);
                    poseToModel(skeleton, reference_, model_);
                    // `model_` now holds the REST pose, not whatever `modelPose_` last recorded.
                    // Clearing the snapshot forces the next `ensureModel` to rebuild in full;
                    // without it a rig re-bound mid-run would solve against rest positions.
                    modelPose_.local.clear();
                    const glm::mat3 bind(model_[static_cast<std::size_t>(tip)]);
                    const glm::vec3 authored = glm::dot(layer.soleUp, layer.soleUp) > 1e-8f
                                                   ? glm::normalize(layer.soleUp)
                                                   : glm::inverse(bind) * glm::vec3(0.0f, 1.0f, 0.0f);
                    soleUp_[i] = safeNormalize(authored);
                    restTipHeight_[i] = model_[static_cast<std::size_t>(tip)][3].y;
                    if (glm::dot(soleUp_[i], soleUp_[i]) < 0.5f) {
                        problems.push_back(fmt::format(
                            "layer '{}': joint '{}' has a degenerate rest transform, so there is no "
                            "direction for the sole to face",
                            layer.name, layer.chainTip));
                        soleUp_[i] = glm::vec3(0.0f, 1.0f, 0.0f);
                    }
                }
            }
        } else if (layer.kind == PoseLayerKind::Additive) {
            // **Named explicitly, and it used to be a bare `else`.** That was correct while the
            // only remaining kind was `Additive`; the moment a kind arrived that plays no clip, a
            // catch-all branch demanded one and refused the layer with "this rig has no clip ''".
            // A fall-through `else` over an enum is a statement about every value that will ever
            // be added to it.
            for (std::size_t c = 0; c < clips.size(); ++c) {
                const std::string& full = clips[c].name;
                const std::size_t bar = full.find_last_of('|');
                const std::string_view shortName =
                    bar == std::string::npos ? std::string_view(full) : std::string_view(full).substr(bar + 1);
                if (full == layer.clip || shortName == layer.clip) {
                    clipIndex_[i] = static_cast<int>(c);
                    break;
                }
            }
            if (clipIndex_[i] < 0) {
                problems.push_back(
                    fmt::format("layer '{}': this rig has no clip '{}'", layer.name, layer.clip));
            }
        }
        masks_.push_back(std::move(mask));
    }
    return problems;
}

// ADR-543. Is `joint` beneath `ancestor`? Shared by `rebind` (which records it) and `apply` (which
// acts on it), because two copies of an ancestry test is two chances to disagree about a rig.
bool descendsFrom(const Skeleton& skeleton, int joint, int ancestor) {
    if (joint < 0 || ancestor < 0) {
        return false;
    }
    for (int at = skeleton.joints[static_cast<std::size_t>(joint)].parent; at >= 0;
         at = skeleton.joints[static_cast<std::size_t>(at)].parent) {
        if (at == ancestor) {
            return true;
        }
    }
    return false;
}

void PoseLayerStack::ensureModel(const Skeleton& skeleton, const Pose& pose) {
    const std::size_t count = skeleton.joints.size();
    if (modelPose_.local.size() != pose.local.size() || model_.size() != count ||
        pose.local.size() != count) {
        poseToModel(skeleton, pose, model_);
        modelPose_ = pose;
        return;
    }
    modelDirty_.assign(count, 0u);
    bool any = false;
    for (std::size_t j = 0; j < count; ++j) {
        const Transform& was = modelPose_.local[j];
        const Transform& now = pose.local[j];
        if (was.position != now.position || was.rotation != now.rotation || was.scale != now.scale) {
            modelDirty_[j] = 1u;
            any = true;
        }
    }
    if (!any) {
        return;
    }
    for (std::size_t j = 0; j < count; ++j) {
        const int parent = skeleton.joints[j].parent;
        const bool parentDirty =
            parent >= 0 && static_cast<std::size_t>(parent) < j && modelDirty_[static_cast<std::size_t>(parent)] != 0u;
        if (parentDirty) {
            modelDirty_[j] = 1u;
        }
        if (modelDirty_[j] == 0u) {
            continue;
        }
        const glm::mat4 local = pose.local[j].matrix();
        model_[j] = parent >= 0 && static_cast<std::size_t>(parent) < j
                        ? model_[static_cast<std::size_t>(parent)] * local
                        : local;
        modelPose_.local[j] = pose.local[j];
    }
}

PoseLayerStats PoseLayerStack::apply(const Skeleton& skeleton, const std::vector<AnimationClip>& clips,
                                     double now, Pose& pose) {
    PoseLayerStats stats;
    stats.layers = static_cast<std::uint32_t>(layers_.size());
    // `layers()` hands out a mutable reference so the seam can write this frame's intent through it.
    // A caller that adds or removes a layer through it has not re-resolved the masks, and the four
    // parallel vectors would disagree: refuse rather than index past one of them.
    if (layers_.empty() || masks_.size() != layers_.size() || results_.size() != layers_.size() ||
        clipIndex_.size() != layers_.size() || pivotIndex_.size() != layers_.size() ||
        chain_.size() != layers_.size() || chainLinked_.size() != layers_.size() ||
        ikStatus_.size() != layers_.size() ||
        soleUp_.size() != layers_.size() || restTipHeight_.size() != layers_.size() ||
        stride_.size() != layers_.size() || order_.size() != layers_.size() ||
        pose.size() != skeleton.jointCount()) {
        return stats;
    }
    const std::size_t count = skeleton.joints.size();

    // ---- reachable contact solving, before any limb runs (ADR-544) ------------------------------
    //
    // Every foot layer on this rig is asking the same body to be somewhere, and the body has one
    // answer. So the demands are gathered once, solved once, and the body is translated once --
    // and then the limbs solve as they always did, against targets that have not moved, which is
    // exactly what makes this composable rather than a second solver.
    //
    // The alien is the reason it exists: its leg binds at 98.5% extension with 0.0098 of slack
    // (ADR-543), so without this a foot layer on it can only ever push feet upward.
    bodyResult_ = BodyCompensation{};
    if (bodyJoint_ >= 0 && static_cast<std::size_t>(bodyJoint_) < count) {
        demands_.clear();
        ensureModel(skeleton, pose);
        for (std::size_t i = 0; i < layers_.size(); ++i) {
            const PoseLayer& layer = layers_[i];
            // The same realized weight the apply loop will use. Reading `layer.weight` here
            // instead would let the body compensation see a foot the solve is still blending in,
            // which is one pipeline stage disagreeing with the next about whether a layer is on.
            if (layer.kind != PoseLayerKind::Foot || layer.effectiveWeight() <= 0.0f) {
                continue;
            }
            const glm::ivec3 ids = chain_[i];
            if (ids.x < 0 || ids.y < 0 || ids.z < 0) {
                continue;
            }
            const glm::vec3 root(model_[static_cast<std::size_t>(ids.x)][3]);
            const glm::vec3 mid(model_[static_cast<std::size_t>(ids.y)][3]);
            const glm::vec3 tip(model_[static_cast<std::size_t>(ids.z)][3]);
            glm::vec3 target(0.0f);
            if (layer.hasTarget) {
                target = layer.target;
            } else if (layer.hasGround) {
                target = plantOnPlane(tip, layer.groundPoint, layer.groundNormal,
                                      layer.groundOffset + restTipHeight_[i]);
            } else {
                continue; // asked for nothing; it cannot be short of anything
            }
            ReachDemand demand;
            demand.root = root;
            demand.target = target;
            demand.reach = (glm::length(mid - root) + glm::length(tip - mid)) *
                           std::clamp(layer.extension, 0.0f, 1.0f);
            demands_.push_back(demand);
        }
        bodyResult_ = solveBodyCompensation(demands_, bodySpec_.limits);
        if (glm::dot(bodyResult_.translation, bodyResult_.translation) > 0.0f) {
            // The translation is in MODEL space and a local position is in the parent's. On the
            // default body joint -- the parentless root -- those are the same thing, which is the
            // reason that default exists; a named pelvis needs the conversion, and it is the basis
            // rather than the full inverse because a translation is a vector.
            const auto b = static_cast<std::size_t>(bodyJoint_);
            const int parent = skeleton.joints[b].parent;
            glm::vec3 localDelta = bodyResult_.translation;
            if (parent >= 0) {
                const glm::mat3 basis(model_[static_cast<std::size_t>(parent)]);
                localDelta = glm::inverse(basis) * bodyResult_.translation;
            }
            pose.local[b].position += localDelta;
            stats.bodyCompensations = 1;
        }
    }

    // §5: pipeline order, not file order. `order_` is sorted by `poseLayerStage`.
    for (const std::uint32_t slot : order_) {
        const auto i = static_cast<std::size_t>(slot);
        PoseLayer& layer = layers_[i];
        const JointMask& mask = masks_[i];
        LayerResolution& result = results_[i];
        // §46: one read of the realized weight per layer per frame, derived from `now`. Every use
        // below is of this, not of `layerWeight`, so a blend cannot apply to some of a layer's
        // effects and not others -- which is the shape of `docs/testing.md` #25.
        const float layerWeight = layer.effectiveWeight();
        if (layerWeight <= 0.0f) {
            result = LayerResolution::Inactive;
            continue;
        }
        if (mask.empty()) {
            // A foot layer's mask is synthesised from its chain, so an empty one means this rig
            // carries none of the three names -- which is `NoChain` and not `NoJoints`. The two
            // answers send a reader to different places: one is a mask that missed, the other is a
            // limb this rig does not have.
            result = layer.kind == PoseLayerKind::Foot ? LayerResolution::NoChain
                                                       : LayerResolution::NoJoints;
            continue;
        }
        // ---- the tripwire -------------------------------------------------------------------
        //
        // **Four times in this programme a branch saying "anything that is not X" has silently
        // swallowed a new `PoseLayerKind`:** `rebind`'s additive clip lookup, the parser's
        // hand-written kind list, the scene validator's mask check, and this function's own
        // `kind == Foot` -- the last of which made every `Reach` layer report Solved having done
        // nothing, with an unreachable target coming back successful and the hand at rest.
        //
        // A catch-all is a claim about every value that will ever be added to the enum, and the
        // author of the next value pays. This switch exists only so the compiler makes them pay
        // at build time instead: `-Wall` implies `-Wswitch`, so adding a kind without visiting
        // this function is a build error rather than a silent no-op.
        switch (layer.kind) {
        case PoseLayerKind::Aim:
        case PoseLayerKind::Additive:
        case PoseLayerKind::Foot:
        case PoseLayerKind::Stride:
        case PoseLayerKind::Secondary:
        case PoseLayerKind::Lean:
        case PoseLayerKind::Reach:
            break;
        }

        if (layer.kind == PoseLayerKind::Lean) {
            // **Into the force, in the body's own frame.**
            //
            // The acceleration arrives already converted to the rig's model space, so +Z is the
            // body's forward and +X its right whichever way it is facing in the world. That is
            // what makes one set of gains work for a character walking north and the same one
            // walking south -- and getting it wrong is invisible on a body that only ever walks
            // one way, which is why the test drives it round a circle.
            //
            // Pitch from the forward component, roll from the lateral one plus the turn rate. A
            // body cornering leans into the inside of the turn, and that is a different input
            // from its lateral acceleration even though the two usually agree.
            // §40. A body leans into a hill, and only into an *uphill* one: the sign comes from
            // how much of the downhill direction points behind the body. On the flat
            // `bodyDownhill` is zero and this term vanishes, which is why it needs no branch.
            // **The sign follows the acceleration term's convention, and the first version had
            // it backwards.** Negative `pitchDegrees` is a forward lean (see the accel term just
            // below, where accelerating forward gives a negative). Ascending means downhill is
            // *behind* the body -- `bodyDownhill.z` negative -- and ascending should lean
            // forward, so the term is `+downhill.z` and not `-`. Descending then leans back,
            // which is what a body going downhill actually does.
            const float uphill = layer.bodyDownhill.z * layer.bodySlope * layer.leanSlopeDegrees;
            const float pitchDegrees =
                (-layer.bodyAcceleration.z * layer.leanDegreesPerAccel) + uphill;
            const float rollDegrees = (layer.bodyAcceleration.x * layer.leanDegreesPerAccel) +
                                      (layer.bodyTurnRate * layer.leanDegreesPerTurn);
            const float magnitude =
                std::sqrt((pitchDegrees * pitchDegrees) + (rollDegrees * rollDegrees));
            const float limit = std::max(layer.leanMaxDegrees, 0.0f);
            const bool clamped = magnitude > limit + 1e-4f;
            // Scaled as a pair rather than clamped per axis, so a body accelerating diagonally
            // leans diagonally instead of squaring off against the limit.
            const float scale = clamped && magnitude > 1e-6f ? limit / magnitude : 1.0f;
            const float w = std::clamp(layerWeight, 0.0f, 1.0f);
            const float pitch = glm::radians(pitchDegrees * scale) * w;
            const float roll = glm::radians(rollDegrees * scale) * w;
            if (std::abs(pitch) < 1e-6f && std::abs(roll) < 1e-6f) {
                // Standing still, or braking exactly as hard as it is turning. Applied, not
                // inactive: the layer did what it was asked and the answer was nothing.
                result = clamped ? LayerResolution::Clamped : LayerResolution::Applied;
                stats.applied += 1u;
                continue;
            }
            std::uint32_t moved = 0;
            for (std::size_t j = 0; j < mask.weight.size(); ++j) {
                const float jw = mask.weight[j];
                if (jw <= 0.0f) {
                    continue;
                }
                // Spread across the masked joints by their weights, so a spine leans along its
                // length instead of hinging at one vertebra.
                const glm::quat tilt =
                    glm::angleAxis(pitch * jw, glm::vec3(1.0f, 0.0f, 0.0f)) *
                    glm::angleAxis(roll * jw, glm::vec3(0.0f, 0.0f, 1.0f));
                pose.local[j].rotation = glm::normalize(pose.local[j].rotation * tilt);
                ++moved;
            }
            result = moved == 0   ? LayerResolution::NoJoints
                     : clamped    ? LayerResolution::Clamped
                                  : LayerResolution::Applied;
            stats.applied += moved > 0 ? 1u : 0u;
            continue;
        }
        if (layer.kind == PoseLayerKind::Secondary) {
            // **Deterministic by construction.** Everything below is a function of `now`, the
            // layer's own constants and the joint's index. No accumulator, no RNG, nothing carried
            // between frames -- so a scrubbed frame and a played one give the same pose, which is
            // ADR-360 and is the whole reason this is a sine and not a noise field.
            const float period = std::max(layer.secondaryPeriod, 1e-3f);
            // Fade with travel: idle life is what a standing body does.
            float fade = 1.0f;
            if (layer.secondaryStillness > 1e-4f) {
                fade = 1.0f - std::clamp(layer.bodySpeed / layer.secondaryStillness, 0.0f, 1.0f);
            }
            const float amplitude = glm::radians(layer.secondaryDegrees) * fade *
                                    std::clamp(layerWeight, 0.0f, 1.0f);
            if (amplitude <= 1e-6f) {
                // Faded out rather than switched off: the layer is doing what it was asked to.
                result = LayerResolution::Applied;
                stats.applied += 1u;
                continue;
            }
            const glm::vec3 axis = glm::length(layer.secondaryAxis) > 1e-6f
                                       ? glm::normalize(layer.secondaryAxis)
                                       : glm::vec3(1.0f, 0.0f, 0.0f);
            std::uint32_t moved = 0;
            std::uint32_t ordinal = 0;
            for (std::size_t j = 0; j < mask.weight.size(); ++j) {
                const float w = mask.weight[j];
                if (w <= 0.0f) {
                    continue;
                }
                // Each successive masked joint lags the one before, so the motion travels up the
                // body instead of moving it as one rigid block.
                // §49: authored phase, plus the seeded offset, plus the per-joint spread. Three
                // terms that are each a pure function of their inputs, summed -- so the whole
                // thing is reconstructable at frame N without having run frame N-1.
                const float phase = layer.secondaryPhase +
                                    seedPhase(layer.characterSeed, layer.layerSeed) +
                                    (static_cast<float>(ordinal) * layer.secondarySpread);
                ++ordinal;
                const auto cycles = static_cast<float>(now / static_cast<double>(period));
                const float angle =
                    amplitude * w * std::sin(6.283185307179586f * (cycles + phase));
                pose.local[j].rotation =
                    glm::normalize(pose.local[j].rotation * glm::angleAxis(angle, axis));
                ++moved;
            }
            result = moved > 0 ? LayerResolution::Applied : LayerResolution::NoJoints;
            stats.applied += moved > 0 ? 1u : 0u;
            continue;
        }
        if (layer.kind == PoseLayerKind::Stride) {
            // **Scale how far the foot reaches from the body, not where the body is.**
            //
            // The excursion is measured in MODEL space from the origin joint, because that is the
            // frame a step actually happens in: the foot swings forward of the hips and back
            // behind them, and the hips are what travel. Scaling the joint's LOCAL translation
            // instead would scale its offset from whatever its parent happens to be, which on this
            // alien is a sibling relationship and means nothing (ADR-543).
            const glm::ivec2 ids = stride_[i];
            if (ids.x < 0 || ids.y < 0) {
                result = LayerResolution::NoJoints;
                continue;
            }
            const float wanted = layer.strideRatio;
            if (!(wanted > 0.0f) || !std::isfinite(wanted)) {
                result = LayerResolution::NoTarget; // nobody told it how far the body is going
                continue;
            }
            // §40. A slope shortens the step whichever way it runs -- climbing and descending
            // both cost stride -- so the magnitude of the incline is what counts and the floor is
            // what stops it becoming a mince.
            const float slopeScale =
                std::max(1.0f - (std::abs(layer.bodySlope) * layer.strideSlopeGain),
                         layer.strideSlopeFloor);
            const float scale =
                std::clamp(wanted * slopeScale, layer.strideMin, layer.strideMax);
            const bool clamped = std::abs(scale - wanted) > 1e-4f;
            // A ratio of 1 is the authored stride, and doing the arithmetic anyway would be a
            // float round-trip on every joint of every character for no change.
            if (std::abs(scale - 1.0f) < 1e-4f) {
                result = clamped ? LayerResolution::Clamped : LayerResolution::Applied;
                stats.applied += 1u;
                continue;
            }
            ensureModel(skeleton, pose);
            const auto jointIndex = static_cast<std::size_t>(ids.x);
            const auto originIndex = static_cast<std::size_t>(ids.y);
            const glm::vec3 jointModel(model_[jointIndex][3]);
            const glm::vec3 originModel(model_[originIndex][3]);
            glm::vec3 excursion = jointModel - originModel;
            // Horizontal by `scale`, vertical by `strideLift` of it: a shorter step does not lift
            // the foot as high, and shortening the reach while leaving the lift alone is what
            // makes a shortened walk read as a march.
            const float lift = 1.0f + ((scale - 1.0f) * std::clamp(layer.strideLift, 0.0f, 1.0f));
            const glm::vec3 wantedModel =
                originModel + glm::vec3(excursion.x * scale, excursion.y * lift, excursion.z * scale);
            // Blended by the layer's weight, like every other correction here, so a scene can fade
            // it in rather than snap it (§63).
            const glm::vec3 finalModel = glm::mix(jointModel, wantedModel, std::clamp(layerWeight, 0.0f, 1.0f));

            // Back to the joint's own parent frame. A model position is in the rig's space and a
            // local translation is in the parent's, so the parent's model transform has to come
            // out -- the full inverse and not just the basis, because this is a point.
            const int parent = skeleton.joints[jointIndex].parent;
            const glm::mat4 parentModel =
                parent >= 0 ? model_[static_cast<std::size_t>(parent)] : glm::mat4(1.0f);
            pose.local[jointIndex].position =
                glm::vec3(glm::inverse(parentModel) * glm::vec4(finalModel, 1.0f));
            result = clamped ? LayerResolution::Clamped : LayerResolution::Applied;
            stats.applied += 1u;
            continue;
        }
        if (layer.kind == PoseLayerKind::Aim) {
            if (pivotIndex_[i] < 0) {
                result = LayerResolution::NoPivot;
                continue;
            }
            if (!layer.hasTarget) {
                result = LayerResolution::NoTarget;
                continue;
            }
            // The pivot is read from the pose as the base layers left it, and it stays fixed for the
            // whole of this layer. When the mask spreads the turn over a chain the pivot therefore
            // moves under the joints that already turned; the residual that leaves is measured in
            // tests/unit/test_character_lab_layers.cpp rather than asserted away.
            ensureModel(skeleton, pose);
            const glm::vec3 pivot = glm::vec3(model_[static_cast<std::size_t>(pivotIndex_[i])][3]);
            const glm::vec3 toTarget = layer.target - pivot;
            if (glm::dot(toTarget, toTarget) < 1e-8f) {
                result = LayerResolution::NoTarget; // standing inside its own target: no direction exists
                continue;
            }
            const glm::quat full = aimRotation(layer.forward, toTarget, layer.maxYawDegrees / kDegrees,
                                               layer.maxPitchDegrees / kDegrees);
            const glm::mat4 toPivot = glm::translate(glm::mat4(1.0f), pivot);
            const glm::mat4 fromPivot = glm::translate(glm::mat4(1.0f), -pivot);
            updated_.resize(count);
            std::uint32_t wrote = 0;
            // One forward pass, parents before children (Skeleton::valid enforces it). A masked
            // joint is pre-rotated in *model* space about the shared pivot, which is what makes a
            // group of siblings turn as one body part -- the case the alien pack forces, where a
            // head's eyes and mouth are its siblings and not its children.
            for (std::size_t j = 0; j < count; ++j) {
                const int parent = skeleton.joints[j].parent;
                const glm::mat4 parentModel = parent >= 0 && static_cast<std::size_t>(parent) < j
                                                  ? updated_[static_cast<std::size_t>(parent)]
                                                  : glm::mat4(1.0f);
                glm::mat4 world = parentModel * pose.local[j].matrix();
                const float w = std::min(mask.weight[j] * layerWeight, 1.0f);
                if (w > 0.0f) {
                    const glm::quat turn = w >= 1.0f ? full : glm::slerp(kIdentity, full, w);
                    world = toPivot * glm::mat4_cast(turn) * fromPivot * world;
                    // Back to a local transform through the parent this joint actually has, which
                    // for a masked joint under a masked one is the parent *after* its own turn --
                    // which is why the pass is a single forward sweep rather than a per-joint fix-up.
                    //
                    // `fromMatrix` discards shear, and conjugating a rotation by a parent carrying a
                    // **non-uniform** scale produces some. Every rig this engine loads has uniformly
                    // scaled joints and the aim arms land on their asked-for angle to 0.031 degrees,
                    // so it is a statement of the assumption rather than a known loss; the day a rig
                    // arrives with squashed bones this is the line that will be quietly wrong.
                    pose.local[j] = Transform::fromMatrix(glm::inverse(parentModel) * world);
                    ++wrote;
                }
                updated_[j] = world;
            }
            result = LayerResolution::Applied;
            ++stats.applied;
            stats.joints += wrote;
            continue;
        }
        if (layer.kind == PoseLayerKind::Foot || layer.kind == PoseLayerKind::Reach) {
            ikStatus_[i] = IkStatus::Solved;
            const glm::ivec3 ids = chain_[i];
            if (ids.x < 0 || ids.y < 0 || ids.z < 0) {
                result = LayerResolution::NoChain;
                continue;
            }
            const auto r = static_cast<std::size_t>(ids.x);
            const auto m = static_cast<std::size_t>(ids.y);
            const auto t = static_cast<std::size_t>(ids.z);
            ensureModel(skeleton, pose);
            const TwoBoneChain chain{glm::vec3(model_[r][3]), glm::vec3(model_[m][3]),
                                     glm::vec3(model_[t][3])};
            // An explicit target beats the plane, so a test or a timeline can drive one foot by
            // hand while the others plant; the plane is the per-frame form, and a layer with
            // neither has been given no work rather than asked to invent some.
            glm::vec3 target(0.0f);
            if (layer.hasTarget) {
                target = layer.target;
            } else if (layer.kind == PoseLayerKind::Reach) {
                // §23/§24. A reach with nowhere to reach is not a failure and not an invention:
                // it is a hand that has been given no work. `NoTarget` says exactly that, and the
                // arm keeps whatever the animation had it doing. A reach never falls back to the
                // ground plane -- a hand planted on the floor under the shoulder is not a reach.
                result = LayerResolution::NoTarget;
                continue;
            } else if (layer.hasGround) {
                target = plantOnPlane(chain.tip, layer.groundPoint, layer.groundNormal,
                                      layer.groundOffset + restTipHeight_[i]);
            } else {
                result = LayerResolution::NoTarget;
                continue;
            }

            // ---- §14/§15: hold the foot where it landed, and let it go gracefully ---------------
            //
            // In the body's own frame a planted foot slides **backwards at the body's speed**.
            // Holding it in the world therefore means offsetting the plant target by
            // `-velocity * elapsed`, which needs no memory: `elapsed` comes from the contact track
            // the clip already carries, and the velocity from the seam. See `PoseLayer::footLock`
            // for why an accumulated anchor could not survive a scrub.
            if (layer.footLock > 0.0f && layer.inContact) {
                // §15's approach and release. A lock that switched on at the span boundary is the
                // "foot locked, then teleports" failure named outright in the spec, so both edges
                // ease -- and the ease is over the *time to the edge*, not over the span's length,
                // so a long stance and a short one release the same way.
                const float blend = std::max(layer.lockBlendSeconds, 1e-4f);
                const float rampIn = std::clamp(layer.contactElapsed / blend, 0.0f, 1.0f);
                const float rampOut = std::clamp(layer.contactRemaining / blend, 0.0f, 1.0f);
                const auto smooth = [](float t) { return t * t * (3.0f - (2.0f * t)); };
                const float hold = std::clamp(layer.footLock, 0.0f, 1.0f) *
                                   smooth(std::min(rampIn, rampOut));
                if (hold > 1e-4f) {
                    const glm::vec3 slid =
                        target - (layer.bodyVelocity * layer.contactElapsed * hold);
                    // Only the horizontal component is held. The vertical one is the ground's
                    // answer and holding it would lift a foot off a slope it is walking down.
                    target = glm::vec3(slid.x, target.y, slid.z);
                }
            }

            // The pole direction becomes a pole *position* here, out from the midpoint of the hip
            // and the target by one limb length. Only the component perpendicular to the
            // hip->target axis does anything, and building it this way makes that component exactly
            // the authored direction's -- so "the knee points +Z" means the same thing on a body
            // standing on a slope as on the flat.
            const bool hasPole = glm::dot(layer.poleDirection, layer.poleDirection) > 1e-8f;
            glm::vec3 pole(0.0f);
            if (hasPole) {
                const float span =
                    glm::length(chain.mid - chain.root) + glm::length(chain.tip - chain.mid);
                pole = 0.5f * (chain.root + target) + safeNormalize(layer.poleDirection) * span;
            }
            const TwoBoneSolution sol = solveTwoBone(chain, target, pole, hasPole, layer.extension);
            ikStatus_[i] = sol.status;
            if (sol.status == IkStatus::DegenerateBone || sol.status == IkStatus::DegenerateTarget ||
                sol.status == IkStatus::DegenerateBend) {
                result = LayerResolution::Degenerate;
                continue;
            }
            const float w = std::min(layerWeight, 1.0f);
            // The two increments are blended separately, not the composed pair. ADR-359: slerping
            // the mid's *total* rotation makes the knee's share of a half-weight solve depend on
            // the hip's, and it reads as the knee lagging the leg.
            const glm::quat rootTurn = w >= 1.0f ? sol.rootDelta : glm::slerp(kIdentity, sol.rootDelta, w);
            const glm::quat bendTurn = w >= 1.0f ? sol.midBend : glm::slerp(kIdentity, sol.midBend, w);

            // Two model-space pre-rotations about two fixed pivots, written back as three locals.
            // Every joint *between* the named three keeps its own local and rides along -- which is
            // the whole reason a goat's `AnkleB.L` between its knee and its foot is not a problem.
            const glm::vec3 hip = chain.root;
            const auto pre = [](const glm::vec3& pivot, const glm::quat& q, const glm::mat4& m) {
                return glm::translate(glm::mat4(1.0f), pivot) * glm::mat4_cast(q) *
                       glm::translate(glm::mat4(1.0f), -pivot) * m;
            };
            const auto afterRoot = [&](const glm::mat4& mm) { return pre(hip, rootTurn, mm); };
            const glm::vec3 knee = hip + (rootTurn * (chain.mid - hip));
            // The bend is authored in the pre-aim frame, so in the final frame it is conjugated by
            // the aim -- the same identity the solver's own derivation uses to report `tip`.
            const glm::quat bendHere = rootTurn * bendTurn * glm::conjugate(rootTurn);
            const auto afterBend = [&](const glm::mat4& mm) { return pre(knee, bendHere, afterRoot(mm)); };

            // ADR-543. Which model-space transform a joint's PARENT has undergone, worked out
            // rather than assumed. Only three locals are written -- the hip, the knee and the tip --
            // so every other joint's final model matrix is its old one with exactly one of
            // {identity, afterRoot, afterBend} applied, decided by which of the three it hangs
            // beneath. Tip first, then mid, then root, because on a properly nested rig the tip's
            // subtree is inside the mid's and the mid's inside the root's, and the innermost wins.
            //
            // On an ancestor chain this reduces to exactly what shipped before -- the parent of the
            // knee is under the hip so it gets `afterRoot`, the parent of the tip is under the knee
            // so it gets `afterBend`, and the hip's parent is under nothing so it gets identity.
            // The old code was this rule with the answers hardcoded; it was correct for the rigs it
            // was written against and silently wrong for the alien, where it moved no foot at all.
            const auto parentModel = [&](std::size_t joint) {
                const int parent = skeleton.joints[joint].parent;
                if (parent < 0) {
                    return glm::mat4(1.0f);
                }
                const auto p = static_cast<std::size_t>(parent);
                const glm::mat4& pm = model_[p];
                if (parent == ids.z || descendsFrom(skeleton, parent, ids.z)) {
                    return afterBend(pm);
                }
                if (parent == ids.y || descendsFrom(skeleton, parent, ids.y)) {
                    return afterBend(pm);
                }
                if (parent == ids.x || descendsFrom(skeleton, parent, ids.x)) {
                    return afterRoot(pm);
                }
                return pm;
            };
            const glm::mat4 rootWorld = afterRoot(model_[r]);
            pose.local[r] = Transform::fromMatrix(glm::inverse(parentModel(r)) * rootWorld);
            const glm::mat4 midWorld = afterBend(model_[m]);
            pose.local[m] = Transform::fromMatrix(glm::inverse(parentModel(m)) * midWorld);
            // ADR-543. The tip is written UNCONDITIONALLY, which it was not before.
            //
            // On an ancestor chain this is exactly a no-op and can be shown to be: the tip's parent
            // is under the knee, so it receives the same rigid pre-multiply the tip does, and
            // `inverse(M*P) * M*T` is `inverse(P) * T` -- the local it already had. That is why
            // leaving it out was invisible for as long as every rig was nested.
            //
            // On a detached chain it is the whole difference between a solve and a no-op. The
            // alien's `foot.l` hangs off `root.x`, which is beneath neither the hip nor the knee
            // and therefore does not move; without an explicit write the foot would sit exactly
            // where the clip left it while the knee bent away from it.
            const glm::mat4 tipBase = afterBend(model_[t]);
            pose.local[t] = Transform::fromMatrix(glm::inverse(parentModel(t)) * tipBase);

            std::uint32_t wrote = 3;
            const float align = std::clamp(layer.footAlign, 0.0f, 1.0f) * w;
            if (align > 0.0f && layer.hasGround) {
                // The sole, laid on the slope. `soleUp` is an axis of the tip joint's own bind
                // frame, so the direction it currently points is read out of the posed matrix
                // rather than assumed -- a hoof that the clip has already rotated is a hoof that
                // has already moved its sole.
                const glm::mat4& tipWorld = tipBase;
                const glm::vec3 have = safeNormalize(glm::mat3(tipWorld) * soleUp_[i]);
                const glm::vec3 want = safeNormalize(layer.groundNormal);
                if (glm::dot(have, have) > 0.5f && glm::dot(want, want) > 0.5f) {
                    const glm::quat full = shortestArc(have, want, glm::vec3(1.0f, 0.0f, 0.0f));
                    const glm::quat turn = align >= 1.0f ? full : glm::slerp(kIdentity, full, align);
                    // The tip's *actual* place, not the solver's `tip`: at partial weight they are
                    // different points, and pivoting a rotation about a point the joint is not at
                    // translates it.
                    const glm::mat4 aligned = pre(glm::vec3(tipWorld[3]), turn, tipWorld);
                    pose.local[t] = Transform::fromMatrix(glm::inverse(parentModel(t)) * aligned);
                }
            }
            result = sol.status == IkStatus::Clamped ? LayerResolution::Clamped : LayerResolution::Applied;
            // ADR-543: a chain with at least one detached link solved this frame. Zero on every
            // properly nested rig, which is what makes it a number worth printing.
            if (chainLinked_[i].x == 0 || chainLinked_[i].y == 0) {
                ++stats.detachedChains;
            }
            ++stats.applied;
            stats.joints += wrote;
            continue;
        }
        // Additive.
        if (clipIndex_[i] < 0 || static_cast<std::size_t>(clipIndex_[i]) >= clips.size()) {
            result = LayerResolution::NoSource;
            continue;
        }
        const AnimationClip& clip = clips[static_cast<std::size_t>(clipIndex_[i])];
        // The clip's own first frame is the reference, so what is added is the clip's *displacement*
        // rather than the clip's pose. Adding the pose would overwrite the gait with a second one at
        // partial weight, which is a cross-fade -- the thing the engine already had.
        setRestPose(skeleton, reference_);
        sampleClip(clip, clip.start, reference_);
        setRestPose(skeleton, sampled_);
        sampleClip(clip, additivePhase(clip, now, layer.clipRate), sampled_);
        std::uint32_t wrote = 0;
        for (std::size_t j = 0; j < count; ++j) {
            const float w = std::min(mask.weight[j] * layerWeight, 1.0f);
            if (w <= 0.0f) {
                continue;
            }
            const Transform& ref = reference_.local[j];
            const Transform& cur = sampled_.local[j];
            glm::quat delta = cur.rotation * glm::inverse(ref.rotation);
            if (delta.w < 0.0f) {
                delta = -delta; // the short arc, for the same reason blendTransform takes it
            }
            pose.local[j].rotation =
                glm::normalize(glm::slerp(kIdentity, glm::normalize(delta), w) * pose.local[j].rotation);
            pose.local[j].position += w * (cur.position - ref.position);
            // Scale is deliberately not added. No clip in the 168 this engine loads animates a joint
            // scale, and an additive scale has no identity that is not a division.
            ++wrote;
        }
        result = LayerResolution::Applied;
        ++stats.applied;
        stats.joints += wrote;
    }
    return stats;
}

} // namespace avgen::scene
