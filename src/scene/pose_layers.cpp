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
    }
    return "aim";
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
    soleUp_.clear();
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
    soleUp_.assign(layers_.size(), glm::vec3(0.0f, 1.0f, 0.0f));
    ikStatus_.assign(layers_.size(), IkStatus::Solved);
    results_.assign(layers_.size(), LayerResolution::Inactive);
    masks_.reserve(layers_.size());
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        const PoseLayer& layer = layers_[i];
        // A foot layer's joint set *is* its chain, and the mask is derived from it rather than
        // authored. ADR-344: a two-bone solve is not maskable per joint -- half a knee does not
        // reach half a target -- so an authored mask on one could only be a silent no-op, which is
        // the failure this whole unit exists to stop repeating. It is reported instead.
        JointMaskSpec spec = layer.mask;
        if (layer.kind == PoseLayerKind::Foot) {
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
        } else if (layer.kind == PoseLayerKind::Foot) {
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
                    for (int at = skeleton.joints[static_cast<std::size_t>(from)].parent; at >= 0;
                         at = skeleton.joints[static_cast<std::size_t>(at)].parent) {
                        if (at == ancestor) {
                            return true;
                        }
                    }
                    return false;
                };
                if (root == mid || mid == tip || root == tip) {
                    problems.push_back(fmt::format(
                        "layer '{}': its chain names the same joint twice ('{}', '{}', '{}'), which is not a "
                        "two-bone chain",
                        layer.name, layer.chainRoot, layer.chainMid, layer.chainTip));
                } else if (!descends(mid, root)) {
                    problems.push_back(fmt::format(
                        "layer '{}': '{}' is not below '{}' in this rig, so those two names are not a bone",
                        layer.name, layer.chainMid, layer.chainRoot));
                } else if (!descends(tip, mid)) {
                    problems.push_back(fmt::format(
                        "layer '{}': '{}' is not below '{}' in this rig, so those two names are not a bone",
                        layer.name, layer.chainTip, layer.chainMid));
                } else {
                    chain_[i] = glm::ivec3(root, mid, tip);
                    // The sole's up, resolved against the rest pose once. `reference_` is the
                    // scratch pose the additive path also uses; nothing here runs per frame.
                    setRestPose(skeleton, reference_);
                    poseToModel(skeleton, reference_, model_);
                    const glm::mat3 bind(model_[static_cast<std::size_t>(tip)]);
                    const glm::vec3 authored = glm::dot(layer.soleUp, layer.soleUp) > 1e-8f
                                                   ? glm::normalize(layer.soleUp)
                                                   : glm::inverse(bind) * glm::vec3(0.0f, 1.0f, 0.0f);
                    soleUp_[i] = safeNormalize(authored);
                    if (glm::dot(soleUp_[i], soleUp_[i]) < 0.5f) {
                        problems.push_back(fmt::format(
                            "layer '{}': joint '{}' has a degenerate rest transform, so there is no "
                            "direction for the sole to face",
                            layer.name, layer.chainTip));
                        soleUp_[i] = glm::vec3(0.0f, 1.0f, 0.0f);
                    }
                }
            }
        } else {
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

PoseLayerStats PoseLayerStack::apply(const Skeleton& skeleton, const std::vector<AnimationClip>& clips,
                                     double now, Pose& pose) {
    PoseLayerStats stats;
    stats.layers = static_cast<std::uint32_t>(layers_.size());
    // `layers()` hands out a mutable reference so the seam can write this frame's intent through it.
    // A caller that adds or removes a layer through it has not re-resolved the masks, and the four
    // parallel vectors would disagree: refuse rather than index past one of them.
    if (layers_.empty() || masks_.size() != layers_.size() || results_.size() != layers_.size() ||
        clipIndex_.size() != layers_.size() || pivotIndex_.size() != layers_.size() ||
        chain_.size() != layers_.size() || ikStatus_.size() != layers_.size() ||
        soleUp_.size() != layers_.size() ||
        pose.size() != skeleton.jointCount()) {
        return stats;
    }
    const std::size_t count = skeleton.joints.size();
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        PoseLayer& layer = layers_[i];
        const JointMask& mask = masks_[i];
        LayerResolution& result = results_[i];
        if (layer.weight <= 0.0f) {
            result = LayerResolution::Inactive;
            continue;
        }
        if (mask.empty()) {
            result = LayerResolution::NoJoints;
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
            poseToModel(skeleton, pose, model_);
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
                const float w = std::min(mask.weight[j] * layer.weight, 1.0f);
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
        if (layer.kind == PoseLayerKind::Foot) {
            ikStatus_[i] = IkStatus::Solved;
            const glm::ivec3 ids = chain_[i];
            if (ids.x < 0 || ids.y < 0 || ids.z < 0) {
                result = LayerResolution::NoChain;
                continue;
            }
            const auto r = static_cast<std::size_t>(ids.x);
            const auto m = static_cast<std::size_t>(ids.y);
            const auto t = static_cast<std::size_t>(ids.z);
            poseToModel(skeleton, pose, model_);
            const TwoBoneChain chain{glm::vec3(model_[r][3]), glm::vec3(model_[m][3]),
                                     glm::vec3(model_[t][3])};
            // An explicit target beats the plane, so a test or a timeline can drive one foot by
            // hand while the others plant; the plane is the per-frame form, and a layer with
            // neither has been given no work rather than asked to invent some.
            glm::vec3 target(0.0f);
            if (layer.hasTarget) {
                target = layer.target;
            } else if (layer.hasGround) {
                target = plantOnPlane(chain.tip, layer.groundPoint, layer.groundNormal, layer.groundOffset);
            } else {
                result = LayerResolution::NoTarget;
                continue;
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
            const float w = std::min(layer.weight, 1.0f);
            // The two increments are blended separately, not the composed pair. ADR-344: slerping
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
            const auto parentModel = [&](std::size_t joint, const auto& transform) {
                const int parent = skeleton.joints[joint].parent;
                return parent < 0 ? glm::mat4(1.0f) : transform(model_[static_cast<std::size_t>(parent)]);
            };
            const auto afterRoot = [&](const glm::mat4& mm) { return pre(hip, rootTurn, mm); };
            const glm::vec3 knee = hip + (rootTurn * (chain.mid - hip));
            // The bend is authored in the pre-aim frame, so in the final frame it is conjugated by
            // the aim -- the same identity the solver's own derivation uses to report `tip`.
            const glm::quat bendHere = rootTurn * bendTurn * glm::conjugate(rootTurn);
            const auto afterBend = [&](const glm::mat4& mm) { return pre(knee, bendHere, afterRoot(mm)); };

            const glm::mat4 rootWorld = afterRoot(model_[r]);
            pose.local[r] = Transform::fromMatrix(
                glm::inverse(parentModel(r, [](const glm::mat4& mm) { return mm; })) * rootWorld);
            const glm::mat4 midWorld = afterBend(model_[m]);
            pose.local[m] = Transform::fromMatrix(glm::inverse(parentModel(m, afterRoot)) * midWorld);

            std::uint32_t wrote = 2;
            const float align = std::clamp(layer.footAlign, 0.0f, 1.0f) * w;
            if (align > 0.0f && layer.hasGround) {
                // The sole, laid on the slope. `soleUp` is an axis of the tip joint's own bind
                // frame, so the direction it currently points is read out of the posed matrix
                // rather than assumed -- a hoof that the clip has already rotated is a hoof that
                // has already moved its sole.
                const glm::mat4 tipWorld = afterBend(model_[t]);
                const glm::vec3 have = safeNormalize(glm::mat3(tipWorld) * soleUp_[i]);
                const glm::vec3 want = safeNormalize(layer.groundNormal);
                if (glm::dot(have, have) > 0.5f && glm::dot(want, want) > 0.5f) {
                    const glm::quat full = shortestArc(have, want, glm::vec3(1.0f, 0.0f, 0.0f));
                    const glm::quat turn = align >= 1.0f ? full : glm::slerp(kIdentity, full, align);
                    // The tip's *actual* place, not the solver's `tip`: at partial weight they are
                    // different points, and pivoting a rotation about a point the joint is not at
                    // translates it.
                    const glm::mat4 aligned = pre(glm::vec3(tipWorld[3]), turn, tipWorld);
                    pose.local[t] =
                        Transform::fromMatrix(glm::inverse(parentModel(t, afterBend)) * aligned);
                    ++wrote;
                }
            }
            result = sol.status == IkStatus::Clamped ? LayerResolution::Clamped : LayerResolution::Applied;
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
            const float w = std::min(mask.weight[j] * layer.weight, 1.0f);
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
