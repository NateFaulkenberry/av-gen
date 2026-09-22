#include "scene/retarget_positional.hpp"

#include "scene/motion_database.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

namespace avgen::scene {

namespace {

glm::vec3 at(const std::vector<glm::mat4>& model, int j) {
    return glm::vec3(model[static_cast<std::size_t>(j)][3]);
}

// The body's forward from its two hips: left minus right is the body's +X, and forward is that
// crossed with up. Horizontal and unit, or +Z when the hips coincide.
glm::vec3 forwardFrom(const glm::vec3& left, const glm::vec3& right) {
    const glm::vec3 r = left - right;
    glm::vec3 f = glm::cross(r, glm::vec3(0.0f, 1.0f, 0.0f));
    f.y = 0.0f;
    const float len = glm::length(f);
    return len > 1e-6f ? f / len : glm::vec3(0.0f, 0.0f, 1.0f);
}

} // namespace

AnimationClip retargetLegsPositional(const AnimationClip& source, const Skeleton& sourceSkeleton,
                                     const AnimationClip& retargeted, const Skeleton& targetSkeleton,
                                     const std::vector<PositionalLeg>& legs, float sampleRate,
                                     PositionalRetargetStats* stats, const PositionalRootRescale& root) {
    PositionalRetargetStats local;
    PositionalRetargetStats& out = stats != nullptr ? *stats : local;
    out = PositionalRetargetStats{};
    struct Ids {
        int sHip, sKnee, sAnkle, tHip, tKnee, tFoot;
        float scale;
        glm::vec3 restCorrection{0.0f};
    };
    std::vector<Ids> ids;
    Pose rest;
    std::vector<glm::mat4> sRest;
    std::vector<glm::mat4> tRest;
    setRestPose(sourceSkeleton, rest);
    poseToModel(sourceSkeleton, rest, sRest);
    setRestPose(targetSkeleton, rest);
    poseToModel(targetSkeleton, rest, tRest);
    for (const PositionalLeg& leg : legs) {
        Ids i{sourceSkeleton.find(leg.sourceHip), sourceSkeleton.find(leg.sourceKnee), sourceSkeleton.find(leg.sourceAnkle),
              targetSkeleton.find(leg.target.root), targetSkeleton.find(leg.target.mid), targetSkeleton.find(leg.target.tip), 1.0f};
        if (i.sHip < 0 || i.sKnee < 0 || i.sAnkle < 0 || i.tHip < 0 || i.tKnee < 0 || i.tFoot < 0) {
            out.problem = fmt::format("leg {}/{}/{} -> {}/{}/{} does not resolve", leg.sourceHip, leg.sourceKnee,
                                      leg.sourceAnkle, leg.target.root, leg.target.mid, leg.target.tip);
            return retargeted;
        }
        const float sLen = glm::length(at(sRest, i.sKnee) - at(sRest, i.sHip)) + glm::length(at(sRest, i.sAnkle) - at(sRest, i.sKnee));
        const float tLen = glm::length(at(tRest, i.tKnee) - at(tRest, i.tHip)) + glm::length(at(tRest, i.tFoot) - at(tRest, i.tKnee));
        i.scale = sLen > 1e-6f ? tLen / sLen : 1.0f;
        out.legScale.push_back(i.scale);
        ids.push_back(i);
    }
    const int rootIndex = root.targetRoot.empty() ? -1 : targetSkeleton.find(root.targetRoot);
    // **From the source's frame into the target's, once, at rest.** The rotation retarget turns the
    // two bodies together, so the turn between them is the turn between their rest poses. Measured
    // per frame from each body's hip axis, it followed the pelvis's sway, and the feet slid a third
    // faster than the source's (0.027 against 0.019 m/s on Neutral_FW).
    glm::mat3 restTurn(1.0f);
    if (ids.size() >= 2) {
        const glm::vec3 sF = forwardFrom(at(sRest, ids[0].sHip), at(sRest, ids[1].sHip));
        const glm::vec3 tF = forwardFrom(at(tRest, ids[0].tHip), at(tRest, ids[1].tHip));
        const float turn = std::atan2(tF.x, tF.z) - std::atan2(sF.x, sF.z);
        restTurn = glm::mat3(glm::rotate(glm::mat4(1.0f), turn, glm::vec3(0.0f, 1.0f, 0.0f)));
    }
    // **Relative to the body, not the hip.** The foot's offset is taken from the body joint (the
    // source hip's parent, and the target's travel joint), which carries the travel the feet must
    // cancel. Taken from the hip, the pelvis's rotation moved each hip by a different amount on the
    // two rigs (their hip widths are not in the legs' ratio), and a planted foot slid a third faster
    // than the source's. The difference in proportions is a constant, measured at rest, so a planted
    // foot stays planted and the rest pose lands where the target's own rest puts its feet.
    const int sBody = ids.empty() ? -1 : sourceSkeleton.joints[static_cast<std::size_t>(ids.front().sHip)].parent;
    const int tBody = rootIndex >= 0 ? rootIndex
                                     : (ids.empty() ? -1 : targetSkeleton.joints[static_cast<std::size_t>(ids.front().tHip)].parent);
    if (sBody < 0 || tBody < 0) {
        out.problem = "the legs' body joints do not resolve";
        return retargeted;
    }
    for (Ids& leg : ids) {
        const glm::vec3 mapped = at(tRest, tBody) + (restTurn * (at(sRest, leg.sAnkle) - at(sRest, sBody)) * leg.scale);
        leg.restCorrection = at(tRest, leg.tFoot) - mapped;
    }

    const float rate = std::max(sampleRate, 1.0f);
    const auto frames = static_cast<std::size_t>(std::max(2.0f, std::floor(retargeted.length() * rate + 0.5f) + 1.0f));
    out.frames = static_cast<std::uint32_t>(frames);

    AnimationClip clip;
    clip.name = retargeted.name;
    clip.start = 0.0f;
    clip.duration = static_cast<float>(frames - 1) / rate;
    std::vector<AnimationChannel> channels;
    for (std::size_t j = 0; j < targetSkeleton.joints.size(); ++j) {
        for (const AnimationPath path : {AnimationPath::Translation, AnimationPath::Rotation, AnimationPath::Scale}) {
            AnimationChannel c;
            c.joint = static_cast<std::uint32_t>(j);
            c.path = path;
            c.interpolation = Interpolation::Linear;
            channels.push_back(std::move(c));
        }
    }

    const std::vector<glm::vec3> facing =
        clipFacing(targetSkeleton, retargeted, tBody, static_cast<std::uint32_t>(frames), 1.0f / rate, false, 1.0f);
    std::vector<float> anchorHeading(ids.size(), 0.0f);
    std::vector<glm::vec3> previousAnkle(ids.size(), glm::vec3(0.0f));
    Pose sPose;
    Pose tPose;
    std::vector<glm::mat4> sModel;
    std::vector<glm::mat4> tModel;
    for (std::size_t f = 0; f < frames; ++f) {
        const float t = static_cast<float>(f) / rate;
        setRestPose(sourceSkeleton, sPose);
        sampleClip(source, std::min(source.start + t, source.duration), sPose);
        poseToModel(sourceSkeleton, sPose, sModel);
        setRestPose(targetSkeleton, tPose);
        sampleClip(retargeted, std::min(retargeted.start + t, retargeted.duration), tPose);
        if (rootIndex >= 0 && root.rootScale > 1e-6f && !ids.empty()) {
            // The travel joint's deviation from rest, from the rotation retarget's scale to the legs'.
            const auto r = static_cast<std::size_t>(rootIndex);
            const glm::vec3 restPosition = targetSkeleton.joints[r].rest.position;
            tPose.local[r].position = restPosition + ((tPose.local[r].position - restPosition) * (ids.front().scale / root.rootScale));
        }
        poseToModel(targetSkeleton, tPose, tModel);

        // The target body's forward, for the knee's pole: from its own two hips this frame.
        glm::vec3 tForward(0.0f, 0.0f, 1.0f);
        if (ids.size() >= 2) {
            tForward = forwardFrom(at(tModel, ids[0].tHip), at(tModel, ids[1].tHip));
        }

        // The rest correction turns with the body's heading (the travel joint's, relative to its
        // rest, averaged over a second: `clipFacing`), but **only while the foot is in the air**.
        // Turned during a stance, it swung a planted foot around by the correction's own length:
        // 0.042 m/s of slide on a gently curving walk, against the source's 0.019. Held through the
        // stance and caught up in the swing, the foot lands where the turned body puts it and then
        // stays there.
        const glm::vec3 h = facing[std::min(f, facing.size() - 1)];
        const float headingNow = std::atan2(h.x, h.z);
        for (std::size_t l = 0; l < ids.size(); ++l) {
            const Ids& leg = ids[l];
            if (f > 0) {
                const glm::vec3 now = at(sModel, leg.sAnkle);
                const float speed = std::hypot(now.x - previousAnkle[l].x, now.z - previousAnkle[l].z) * rate;
                if (speed > 0.15f) {
                    anchorHeading[l] = headingNow;
                }
            } else {
                anchorHeading[l] = headingNow;
            }
            previousAnkle[l] = at(sModel, leg.sAnkle);
        }
        for (std::size_t l = 0; l < ids.size(); ++l) {
            const Ids& leg = ids[l];
            const glm::mat3 withHeading = glm::mat3(glm::rotate(glm::mat4(1.0f), anchorHeading[l], glm::vec3(0.0f, 1.0f, 0.0f)));
            const glm::vec3 offset = restTurn * (at(sModel, leg.sAnkle) - at(sModel, sBody)) * leg.scale;
            poseToModel(targetSkeleton, tPose, tModel);
            const glm::vec3 target = at(tModel, tBody) + offset + (withHeading * leg.restCorrection);
            const glm::mat3 footRotation(tModel[static_cast<std::size_t>(leg.tFoot)]);
            const LegSolve solved = solveLegInPose(targetSkeleton, tPose, leg.tHip, leg.tKnee, leg.tFoot, target,
                                                   footRotation, tForward);
            out.worstShortfall = std::max(out.worstShortfall, solved.shortfall);
        }
        for (std::size_t j = 0; j < targetSkeleton.joints.size(); ++j) {
            const Transform& x = tPose.local[j];
            AnimationChannel* c = &channels[j * 3u];
            c[0].times.push_back(t);
            c[0].values.emplace_back(x.position, 0.0f);
            c[1].times.push_back(t);
            c[1].values.emplace_back(x.rotation.x, x.rotation.y, x.rotation.z, x.rotation.w);
            c[2].times.push_back(t);
            c[2].values.emplace_back(x.scale, 0.0f);
        }
    }
    clip.channels = std::move(channels);
    // Only what moves, so the travel joint is still the travel joint (ADR-337; `pruneRestChannels`).
    pruneRestChannels(clip, targetSkeleton);
    return clip;
}

} // namespace avgen::scene
