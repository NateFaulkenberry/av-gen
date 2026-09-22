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
                                     PositionalRetargetStats* stats, const PositionalRootRescale& root,
                                     const PositionalReachCap& cap) {
    PositionalRetargetStats local;
    PositionalRetargetStats& out = stats != nullptr ? *stats : local;
    out = PositionalRetargetStats{};
    struct Ids {
        int sHip, sKnee, sAnkle, tHip, tKnee, tFoot;
        float scale;
        glm::vec3 restCorrection{0.0f};
        float length = 0.0f; // the target leg's rest length, hip to knee to foot
        float upper = 0.0f;  // its two parts at rest
        float lower = 0.0f;
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
        i.length = tLen;
        i.upper = glm::length(at(tRest, i.tKnee) - at(tRest, i.tHip));
        i.lower = glm::length(at(tRest, i.tFoot) - at(tRest, i.tKnee));
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
        // **The travel belongs to the whole body, not to one joint of it.** The rotation retarget
        // moves the travel joint (`root.x`), and on the alien the spine, hands, backpack, antenna
        // and knees are children of `rig`, above it. So a retargeted walk left the upper body where
        // the clip began: the spine 1.2-2.4 from the hips on 100STYLE's sidesteps, against 0.29 in
        // the alien's own clips. The travel and heading are moved up to the skeleton's root, the way
        // §21's augmentation carries them, and the travel joint keeps exactly the model pose it had.
        if (rootIndex >= 0 && targetSkeleton.joints[static_cast<std::size_t>(rootIndex)].parent >= 0) {
            poseToModel(targetSkeleton, tPose, tModel);
            const auto r = static_cast<std::size_t>(rootIndex);
            const glm::mat4 rootModel = tModel[r];
            const glm::vec3 restRoot = at(tRest, rootIndex);
            const glm::vec3 now = at(tModel, rootIndex);
            const glm::vec3 fh = facing[std::min(f, facing.size() - 1)];
            const float bodyYaw = std::atan2(fh.x, fh.z);
            const glm::mat4 body = glm::translate(glm::mat4(1.0f), now) *
                                   glm::rotate(glm::mat4(1.0f), bodyYaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
                                   glm::translate(glm::mat4(1.0f), -restRoot);
            for (std::size_t j = 0; j < targetSkeleton.joints.size(); ++j) {
                if (targetSkeleton.joints[j].parent < 0) {
                    tPose.local[j] = Transform::fromMatrix(body * tPose.local[j].matrix());
                }
            }
            poseToModel(targetSkeleton, tPose, tModel);
            const int parent = targetSkeleton.joints[r].parent;
            tPose.local[r] = Transform::fromMatrix(glm::inverse(tModel[static_cast<std::size_t>(parent)]) * rootModel);
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
        poseToModel(targetSkeleton, tPose, tModel);
        std::vector<glm::vec3> targets(ids.size());
        for (std::size_t l = 0; l < ids.size(); ++l) {
            const Ids& leg = ids[l];
            const glm::mat3 withHeading = glm::mat3(glm::rotate(glm::mat4(1.0f), anchorHeading[l], glm::vec3(0.0f, 1.0f, 0.0f)));
            const glm::vec3 offset = restTurn * (at(sModel, leg.sAnkle) - at(sModel, sBody)) * leg.scale;
            targets[l] = at(tModel, tBody) + offset + (withHeading * leg.restCorrection);
        }
        // **The reach cap, in the retarget and not after it** (owner ruling, 22 Sep). A foot target
        // further from its hip than the alien ever reaches is brought within reach. A post-hoc clamp
        // in the pose layers would move a foot this retarget had already planted, after the slide
        // gate. Two ways, measured against that gate (ADR-624):
        //   * lower the body, so the hips come down to the feet and the feet stay where they were.
        //     A planted foot cannot slide, because it does not move. The body dips on the longest
        //     strides, as a human pelvis does;
        //   * pull the foot in along the hip-to-foot line. A planted foot then follows its moving
        //     hip, which is slide: +53% on Neutral_FW.
        // The body goes down first; whatever a drop cannot fix (a foot further out horizontally than
        // the whole capped leg) is pulled.
        const bool capping = cap.maxReach > 0.0f;
        if (capping && cap.lowerBody && tBody >= 0) {
            float drop = 0.0f;
            for (std::size_t l = 0; l < ids.size(); ++l) {
                const float limit = cap.maxReach * ids[l].length;
                const glm::vec3 d = at(tModel, ids[l].tHip) - targets[l];
                const float horizontal2 = (d.x * d.x) + (d.z * d.z);
                if ((horizontal2 + (d.y * d.y)) <= limit * limit || horizontal2 >= limit * limit) {
                    continue;
                }
                drop = std::max(drop, d.y - std::sqrt((limit * limit) - horizontal2));
            }
            if (drop > 0.0f) {
                // The whole body goes down, from the skeleton's root, so the upper body comes with
                // the hips (see the travel above).
                const glm::mat4 down = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -drop, 0.0f));
                for (std::size_t j = 0; j < targetSkeleton.joints.size(); ++j) {
                    if (targetSkeleton.joints[j].parent < 0) {
                        tPose.local[j] = Transform::fromMatrix(down * tPose.local[j].matrix());
                    }
                }
                poseToModel(targetSkeleton, tPose, tModel);
                ++out.droppedFrames;
                out.meanDrop += drop;
                out.worstDrop = std::max(out.worstDrop, drop);
            }
        }
        for (std::size_t l = 0; l < ids.size(); ++l) {
            const Ids& leg = ids[l];
            glm::vec3 target = targets[l];
            ++out.legFrames;
            // **The target's bone lengths, not the retargeted pose's.** The alien's hip, knee and foot
            // are siblings (ADR-543), so the knee's distance from the hip is whatever the rotation
            // retarget's translations left it at, and the two-bone solve takes its lengths from the
            // pose it is handed. **The alien's knee is a child of the armature, not of the travel
            // joint** (hip and foot are children of `root.x`, `leg_stretch` of joint 0), so as the body
            // travels the knee is left behind: 1.1 from the hip at the first frame of a walk and 2.6
            // six seconds later, against a rest 0.28. The solve then worked with a thigh four to nine
            // times its length, and put the knee there. The foot still landed near its target, which
            // is why ADR-624's reach and slide numbers did not show it; the reach cap did, because
            // with that thigh the leg's shortest reach was past the cap. The knee and foot are put
            // back at their rest distances along their current directions before the solve.
            if (leg.upper > 1e-6f && leg.lower > 1e-6f) {
                poseToModel(targetSkeleton, tPose, tModel);
                const auto place = [&](int joint, const glm::vec3& worldPos) {
                    const auto j = static_cast<std::size_t>(joint);
                    const int parent = targetSkeleton.joints[j].parent;
                    const glm::mat4 parentModel =
                        parent >= 0 ? tModel[static_cast<std::size_t>(parent)] : glm::mat4(1.0f);
                    tPose.local[j].position = glm::vec3(glm::inverse(parentModel) * glm::vec4(worldPos, 1.0f));
                    poseToModel(targetSkeleton, tPose, tModel);
                };
                const glm::vec3 hip = at(tModel, leg.tHip);
                glm::vec3 knee = at(tModel, leg.tKnee);
                const float upperNow = glm::length(knee - hip);
                if (std::abs(upperNow - leg.upper) > 0.01f * leg.upper && upperNow > 1e-6f) {
                    knee = hip + ((knee - hip) * (leg.upper / upperNow));
                    place(leg.tKnee, knee);
                    ++out.relengthedLegFrames;
                }
                const glm::vec3 foot = at(tModel, leg.tFoot);
                const float lowerNow = glm::length(foot - knee);
                if (std::abs(lowerNow - leg.lower) > 0.01f * leg.lower && lowerNow > 1e-6f) {
                    place(leg.tFoot, knee + ((foot - knee) * (leg.lower / lowerNow)));
                }
            }
            if (capping && leg.length > 1e-6f) {
                const glm::vec3 hip = at(tModel, leg.tHip);
                const glm::vec3 toFoot = target - hip;
                const float reach = glm::length(toFoot);
                const float limit = cap.maxReach * leg.length;
                if (reach > limit * 1.0001f) {
                    const glm::vec3 capped = hip + (toFoot * (limit / reach));
                    const float pull = glm::length(target - capped);
                    ++out.cappedLegFrames;
                    out.meanCapPull += pull;
                    out.worstCapPull = std::max(out.worstCapPull, pull);
                    target = capped;
                }
            }
            const glm::mat3 footRotation(tModel[static_cast<std::size_t>(leg.tFoot)]);
            LegSolve solved = solveLegInPose(targetSkeleton, tPose, leg.tHip, leg.tKnee, leg.tFoot, target,
                                             footRotation, tForward);
            // The solve can land the foot further out than its target: it works with the bone lengths
            // of the pose it is handed, and the rotation retarget's pose is not the rest pose the cap
            // is measured against (seen on `Strutting_FR`: 0.979 with every target at 0.956). So the
            // cap is checked on the solved foot, and a foot still past it is pulled in by the excess.
            for (int pass = 0; capping && pass < 3; ++pass) {
                std::vector<glm::mat4> solvedModel;
                poseToModel(targetSkeleton, tPose, solvedModel);
                const glm::vec3 hip = at(solvedModel, leg.tHip);
                const glm::vec3 foot = at(solvedModel, leg.tFoot);
                const float reach = glm::length(foot - hip);
                const float limit = cap.maxReach * leg.length;
                if (reach <= limit * 1.001f) {
                    break;
                }
                const glm::vec3 pulled = target - (glm::normalize(foot - hip) * (reach - limit));
                if (pass == 0) {
                    ++out.resolvedLegFrames;
                }
                out.worstCapPull = std::max(out.worstCapPull, glm::length(pulled - targets[l]));
                target = pulled;
                solved = solveLegInPose(targetSkeleton, tPose, leg.tHip, leg.tKnee, leg.tFoot, target, footRotation,
                                        tForward);
            }
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
    if (out.cappedLegFrames > 0) {
        out.meanCapPull /= static_cast<float>(out.cappedLegFrames);
    }
    if (out.droppedFrames > 0) {
        out.meanDrop /= static_cast<float>(out.droppedFrames);
    }
    clip.channels = std::move(channels);
    // Only what moves, so the travel joint is still the travel joint (ADR-337; `pruneRestChannels`).
    pruneRestChannels(clip, targetSkeleton);
    return clip;
}

} // namespace avgen::scene
