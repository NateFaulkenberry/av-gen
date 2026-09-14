#include "scene/tree_rig.hpp"

#include "core/noise.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::scene {
namespace {

enum Channel : std::uint32_t { kJointPhase = 500 };

constexpr float kEpsilon = 1e-6f;

struct TierResponse {
    float omega;
    float zeta;
    float compliance;
};

TierResponse responseFor(BranchTier tier, const TreeRigSettings& s) {
    switch (tier) {
    case BranchTier::Trunk: return {s.trunkOmega, s.trunkZeta, s.trunkCompliance};
    case BranchTier::Primary: return {s.primaryOmega, s.primaryZeta, s.primaryCompliance};
    default: return {s.secondaryOmega, s.secondaryZeta, s.secondaryCompliance};
    }
}

} // namespace

Result<TreeRig> buildTreeRig(const TreeGraph& graph, const TreeRigSettings& settings) {
    if (graph.nodes.empty() || graph.axes.empty()) {
        return fail("tree rig: the graph is empty");
    }
    TreeRig rig;
    rig.skeleton.name = "tree";

    // Which axes get joints, in priority order: the trunk first, then primaries by substance, then
    // secondaries by substance until the palette is full. Sorting by base radius rather than by
    // index means that when the budget runs out it is the thinnest limbs that go without, which is
    // the same principle the foliage placement uses.
    std::vector<std::uint32_t> chosen;
    std::vector<std::uint32_t> byTier[3];
    for (const TreeAxis& axis : graph.axes) {
        if (axis.nodes.empty()) {
            continue;
        }
        if (axis.tier == BranchTier::Trunk) {
            byTier[0].push_back(axis.id);
        } else if (axis.tier == BranchTier::Primary) {
            byTier[1].push_back(axis.id);
        } else if (axis.tier == BranchTier::Secondary && settings.jointsOnSecondary) {
            byTier[2].push_back(axis.id);
        }
    }
    for (auto& list : byTier) {
        std::sort(list.begin(), list.end(), [&graph](std::uint32_t a, std::uint32_t b) {
            const float ra = graph.axes[a].baseRadius;
            const float rb = graph.axes[b].baseRadius;
            return ra != rb ? ra > rb : a < b;
        });
    }

    // An axis's joint chain must be able to hang off its parent's, so an axis is only usable if its
    // parent was chosen. Walking tier by tier guarantees that for trunk and primary; a secondary
    // whose primary did not fit is skipped rather than reparented to the trunk, because a limb
    // rigidly attached to the trunk while its neighbours flex is more visible than one that does
    // not flex at all.
    std::vector<std::uint8_t> axisChosen(graph.axes.size(), 0);
    axisChosen[0] = 1;
    int budget = std::min(settings.maxJoints, static_cast<int>(kMaxPaletteJoints));
    int used = 0;
    const auto jointsNeeded = [&](std::uint32_t axisId) {
        return std::max(2, static_cast<int>(graph.axes[axisId].length / std::max(settings.jointSpacing, 0.2f)) + 1);
    };
    for (int tier = 0; tier < 3; ++tier) {
        for (std::uint32_t axisId : byTier[tier]) {
            const TreeAxis& axis = graph.axes[axisId];
            if (axis.parentAxis != kNoNode && axisChosen[axis.parentAxis] == 0) {
                continue;
            }
            const int need = jointsNeeded(axisId);
            if (used + need > budget) {
                continue;
            }
            used += need;
            axisChosen[axisId] = 1;
            chosen.push_back(axisId);
        }
    }

    // Build the joints. Parents always precede children because `chosen` runs trunk-first and a
    // child axis is only in it when its parent already was -- which is the ordering a Skeleton
    // requires and the reason `valid()` will accept this.
    std::vector<std::vector<std::uint32_t>> jointsOfAxis(graph.axes.size());
    std::vector<glm::vec3> world;
    for (std::uint32_t axisId : chosen) {
        const TreeAxis& axis = graph.axes[axisId];
        const int count = jointsNeeded(axisId);
        // The first joint of a child axis is parented to the nearest joint on its parent axis, so
        // the fork is a real hierarchy edge rather than two chains that happen to start together.
        int parent = -1;
        if (axis.parentAxis != kNoNode && !jointsOfAxis[axis.parentAxis].empty()) {
            const glm::vec3 forkAt = graph.nodes[axis.firstNode].position;
            float best = std::numeric_limits<float>::max();
            for (std::uint32_t candidate : jointsOfAxis[axis.parentAxis]) {
                const float d = glm::distance(world[candidate], forkAt);
                if (d < best) {
                    best = d;
                    parent = static_cast<int>(candidate);
                }
            }
        }
        for (int i = 0; i < count; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(count - 1);
            const auto nodeIndex =
                static_cast<std::size_t>(t * static_cast<float>(axis.nodes.size() - 1) + 0.5f);
            const std::uint32_t node = axis.nodes[std::min(nodeIndex, axis.nodes.size() - 1)];
            const glm::vec3 p = graph.nodes[node].position;

            Joint joint;
            joint.name = "a" + std::to_string(axisId) + "_" + std::to_string(i);
            joint.parent = parent;
            // Rest locals are pure translations. The animator's whole job is then to write a
            // rotation into each one, and `poseToModel` turns that into inheritance for free.
            joint.rest.position = parent < 0 ? p : p - world[static_cast<std::size_t>(parent)];
            const auto id = static_cast<std::uint32_t>(rig.skeleton.joints.size());
            rig.skeleton.joints.push_back(joint);
            world.push_back(p);
            jointsOfAxis[axisId].push_back(id);
            rig.jointNode.push_back(node);
            rig.jointAxis.push_back(axisId);
            rig.jointTier.push_back(axis.tier);
            const TierResponse response = responseFor(axis.tier, settings);
            // The joint at the base of a chain is anchored: a limb bends along its length, it does
            // not pivot rigidly at its socket.
            rig.compliance.push_back(response.compliance * graph.nodes[node].animationWeight *
                                     (i == 0 ? 0.25f : 1.0f));
            rig.phase.push_back(noise::hashIndex(graph.params.seed, node, kJointPhase) * glm::two_pi<float>());
            parent = static_cast<int>(id);
        }
    }
    rig.restWorld = world;
    if (rig.skeleton.joints.empty()) {
        return fail("tree rig: no joints were placed");
    }
    if (rig.skeleton.joints.size() > kMaxPaletteJoints) {
        return fail("tree rig: {} joints exceeds the palette ceiling of {}", rig.skeleton.joints.size(),
                    kMaxPaletteJoints);
    }

    // Every joint is in the palette, in joint order, and the inverse binds come from the rest pose.
    rig.skeleton.palette.resize(rig.skeleton.joints.size());
    for (std::uint32_t i = 0; i < rig.skeleton.joints.size(); ++i) {
        rig.skeleton.palette[i] = i;
    }
    std::vector<glm::mat4> model;
    poseToModel(rig.skeleton, restPose(rig.skeleton), model);
    rig.skeleton.inverseBind.resize(model.size());
    for (std::size_t i = 0; i < model.size(); ++i) {
        rig.skeleton.inverseBind[i] = glm::inverse(model[i]);
    }
    if (!rig.skeleton.valid()) {
        return fail("tree rig: the skeleton did not validate");
    }
    return rig;
}

Result<void> skinTreeMeshes(const TreeGraph& graph, const TreeRig& rig, TreeMeshes& meshes) {
    if (rig.size() == 0) {
        return fail("tree rig: cannot skin against an empty rig");
    }
    // Joints reachable from an axis: its own chain plus every ancestor axis's. A vertex binds only
    // within that set, which is what stops a twig binding to the limb it happens to hang beside.
    std::vector<std::vector<std::uint32_t>> reachable(graph.axes.size());
    std::vector<std::vector<std::uint32_t>> own(graph.axes.size());
    for (std::uint32_t j = 0; j < rig.size(); ++j) {
        own[rig.jointAxis[j]].push_back(j);
    }
    for (std::uint32_t a = 0; a < graph.axes.size(); ++a) {
        std::uint32_t walk = a;
        for (int depth = 0; depth < 16; ++depth) {
            const auto& list = own[walk];
            reachable[a].insert(reachable[a].end(), list.begin(), list.end());
            if (graph.axes[walk].parentAxis == kNoNode || graph.axes[walk].parentAxis == walk) {
                break;
            }
            walk = graph.axes[walk].parentAxis;
        }
        if (reachable[a].empty()) {
            reachable[a] = own[0]; // the trunk always has joints; a stray axis rides it
        }
    }

    const auto parts = meshes.parts();
    for (std::size_t p = 0; p < parts.size(); ++p) {
        MeshData* mesh = meshes.meshFor(parts[p].first);
        if (mesh == nullptr || mesh->vertices.empty()) {
            continue;
        }
        const std::vector<std::uint32_t>& axisOf = meshes.vertexAxis[p];
        const std::vector<glm::vec3>& bindAt = meshes.vertexBind[p];
        if (axisOf.size() != mesh->vertices.size()) {
            return fail("tree rig: part '{}' has {} vertices but {} recorded axes", parts[p].first,
                        mesh->vertices.size(), axisOf.size());
        }
        mesh->skin.assign(mesh->vertices.size(), SkinInfluence{});
        for (std::size_t v = 0; v < mesh->vertices.size(); ++v) {
            const std::vector<std::uint32_t>& candidates =
                reachable[std::min<std::size_t>(axisOf[v], reachable.size() - 1)];
            // Two nearest joints along the chain, weighted by inverse distance. Two rather than one
            // because a single binding puts a hard crease wherever the nearest joint changes, and
            // rather than four because a tree's joints are strung along a line: the third and
            // fourth nearest are behind the first two and only smear the bend.
            const glm::vec3 bindPos =
                bindAt.size() == mesh->vertices.size() ? bindAt[v] : mesh->vertices[v].position;
            std::uint32_t best = candidates.front();
            std::uint32_t second = candidates.front();
            float bestD = std::numeric_limits<float>::max();
            float secondD = std::numeric_limits<float>::max();
            for (std::uint32_t j : candidates) {
                const float d = glm::distance(rig.restWorld[j], bindPos);
                if (d < bestD) {
                    secondD = bestD;
                    second = best;
                    bestD = d;
                    best = j;
                } else if (d < secondD) {
                    secondD = d;
                    second = j;
                }
            }
            SkinInfluence& influence = mesh->skin[v];
            const float wa = 1.0f / std::max(bestD, 1e-3f);
            const float wb = secondD < std::numeric_limits<float>::max() ? 1.0f / std::max(secondD, 1e-3f) : 0.0f;
            const float sum = wa + wb;
            influence.joints[0] = static_cast<std::uint16_t>(best);
            influence.joints[1] = static_cast<std::uint16_t>(second);
            influence.weights = glm::vec4(wa / sum, wb / sum, 0.0f, 0.0f);
        }
    }
    return {};
}

void TreeAnimator::reset(const TreeRig& rig) {
    bend_.assign(rig.size(), glm::vec2(0.0f));
    velocity_.assign(rig.size(), glm::vec2(0.0f));
}

void TreeAnimator::step(const TreeRig& rig, const TreeMotionInputs& inputs, float dt, float limitRadians) {
    if (bend_.size() != rig.size()) {
        reset(rig);
    }
    if (dt <= 0.0f) {
        return;
    }
    // The substep comes from the STIFFEST joint, not from the frame. A spring integrated at a step
    // near its own period does not merely lose accuracy, it gains energy and diverges, and the
    // stiffest joint is the one that decides when that happens. Sizing it this way also makes the
    // same wall-clock second integrate identically at 24 and at 120 fps.
    float fastest = 0.0f;
    for (std::size_t i = 0; i < rig.size(); ++i) {
        fastest = std::max(fastest, rig.jointTier[i] == BranchTier::Trunk ? 0.6f : 2.4f);
    }
    const float maxStep = 0.12f / std::max(fastest, 0.1f);
    const int substeps = std::clamp(static_cast<int>(std::ceil(dt / maxStep)), 1, 16);
    const float h = dt / static_cast<float>(substeps);

    // Per JOINT, not per tree. A chain of twelve joints each bent by the same angle compounds into
    // a crown displaced several metres sideways, which is what the first animated render showed:
    // the tree did not tear, it leaned over like a palm in a storm. The limit belongs on the joint
    // because that is where the compounding starts.
    const float limit = std::max(limitRadians, 0.005f);
    const glm::vec3 wind = glm::length(inputs.windDirection) > kEpsilon ? glm::normalize(inputs.windDirection)
                                                                       : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec2 windXZ(wind.x, wind.z);

    for (int s = 0; s < substeps; ++s) {
        const auto t = static_cast<float>(inputs.time) + static_cast<float>(s) * h;
        for (std::size_t i = 0; i < rig.size(); ++i) {
            const bool trunk = rig.jointTier[i] == BranchTier::Trunk;
            const float omega = trunk ? 0.55f : (rig.jointTier[i] == BranchTier::Primary ? 1.1f : 2.2f);
            const float zeta = trunk ? 0.85f : (rig.jointTier[i] == BranchTier::Primary ? 0.55f : 0.38f);

            // The target is where the wind WANTS this joint, not where it puts it. Audio moves the
            // target; the spring decides how the joint gets there. That indirection is the whole
            // reason the tree cannot look like a branch wired to a level meter.
            const float swell = 0.55f + 0.45f * std::sin(t * 0.37f + rig.phase[i]);
            const float gust = inputs.gust * (0.6f + 0.4f * std::sin(t * 0.9f + rig.phase[i] * 1.7f));
            const float fine =
                inputs.flutter * 0.35f * std::sin(t * 5.3f + rig.phase[i] * 3.1f) * (trunk ? 0.0f : 1.0f);
            const float drive = rig.compliance[i] * (inputs.windSpeed * swell + gust + fine);
            glm::vec2 target = windXZ * drive;
            // A cross-wind component from the joint's own phase, so limbs do not all lean along one
            // line -- which is the single most recognisable tell of fake wind.
            target += glm::vec2(-windXZ.y, windXZ.x) * drive * 0.4f * std::sin(t * 0.61f + rig.phase[i]);
            target = glm::clamp(target, glm::vec2(-limit), glm::vec2(limit));

            const glm::vec2 accel =
                omega * omega * (target - bend_[i]) - 2.0f * zeta * omega * velocity_[i] +
                windXZ * (inputs.impulse * rig.compliance[i] * (trunk ? 0.1f : 1.0f) / std::max(h, 1e-4f) * 0.02f);
            velocity_[i] += accel * h;
            bend_[i] += velocity_[i] * h;
            bend_[i] = glm::clamp(bend_[i], glm::vec2(-limit * 1.2f), glm::vec2(limit * 1.2f));
        }
    }
}

void TreeAnimator::settle(const TreeRig& rig, const TreeMotionInputs& inputs, float seconds) {
    reset(rig);
    TreeMotionInputs local = inputs;
    const float h = 1.0f / 60.0f;
    for (float t = 0.0f; t < seconds; t += h) {
        local.time = inputs.time - static_cast<double>(seconds - t);
        local.impulse = 0.0f;
        step(rig, local, h);
    }
}

void TreeAnimator::pose(const TreeRig& rig, Pose& out) const {
    setRestPose(rig.skeleton, out);
    if (bend_.size() != rig.size()) {
        return;
    }
    for (std::size_t i = 0; i < rig.size(); ++i) {
        const glm::vec2 b = bend_[i];
        const float amount = glm::length(b);
        if (amount < 1e-5f) {
            continue;
        }
        // Tip the joint's local +Y toward the bend direction. The rest locals are pure
        // translations, so this rotation is the entire animation and it composes down the chain.
        const glm::vec3 dir = glm::normalize(glm::vec3(b.x, 0.0f, b.y));
        const glm::vec3 axis = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), dir);
        if (glm::length(axis) < 1e-5f) {
            continue;
        }
        out.local[i].rotation = glm::angleAxis(amount, glm::normalize(axis)) * out.local[i].rotation;
    }
}

SkinnedRig makeSkinnedRig(const TreeRig& rig) {
    SkinnedRig out;
    out.name = "tree";
    out.skeleton = rig.skeleton;
    // Disabled on purpose. `updateRigs` evaluates an animation player, and this rig has no clips --
    // letting it run would overwrite the pose the animator just produced with a rest pose. The
    // header's contract for a disabled rig is that its palette "is left exactly as it is".
    out.enabled = false;
    out.cullDistance = 0.0f;
    out.pose = restPose(rig.skeleton);
    std::vector<glm::mat4> scratch;
    skinningPalette(rig.skeleton, out.pose, scratch, out.palette);
    out.previousPalette = out.palette;
    return out;
}

void applyTreePose(const TreeRig& rig, const TreeAnimator& animator, SkinnedRig& out) {
    animator.pose(rig, out.pose);
    out.previousPalette = out.palette;
    std::vector<glm::mat4> scratch;
    skinningPalette(rig.skeleton, out.pose, scratch, out.palette);
    ++out.paletteVersion;
}

} // namespace avgen::scene
