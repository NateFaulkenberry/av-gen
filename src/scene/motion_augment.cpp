#include "scene/motion_augment.hpp"

#include "scene/ik.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace avgen::scene {

const char* augmentKindName(AugmentKind kind) {
    switch (kind) {
    case AugmentKind::Plant: return "plant";
    case AugmentKind::Stride: return "stride";
    case AugmentKind::Direction: return "direction";
    case AugmentKind::Turn: return "turn";
    case AugmentKind::Start: return "start";
    case AugmentKind::Stop: return "stop";
    case AugmentKind::Mirror: return "mirror";
    }
    return "plant";
}

namespace {

// The travel joint, by the rule the rest of the pipeline uses: the lowest-indexed joint with a
// translation track (ADR-337).
int travelJoint(const AnimationClip& clip) {
    int root = -1;
    for (const AnimationChannel& c : clip.channels) {
        if (c.path == AnimationPath::Translation && (root < 0 || static_cast<int>(c.joint) < root)) {
            root = static_cast<int>(c.joint);
        }
    }
    return root < 0 ? 0 : root;
}

bool plantedAt(const ContactTrack& track, float local) {
    for (const ContactSpan& span : track.spans) {
        if (span.wraps() ? (local >= span.start || local <= span.end)
                         : (local >= span.start && local <= span.end)) {
            return true;
        }
    }
    return false;
}

// A clip as frames of local poses.
struct Baked {
    std::vector<Pose> frames;
    float dt = 1.0f / 30.0f;
    float start = 0.0f;
    [[nodiscard]] std::size_t count() const { return frames.size(); }
    [[nodiscard]] float timeOf(std::size_t f) const { return static_cast<float>(f) * dt; }
};

Baked bake(const Skeleton& skeleton, const AnimationClip& clip, float rate) {
    Baked out;
    out.dt = 1.0f / std::max(rate, 1.0f);
    out.start = clip.start;
    const auto frames =
        static_cast<std::size_t>(std::max(2.0f, std::floor(clip.length() * rate + 0.5f) + 1.0f));
    out.frames.resize(frames);
    for (std::size_t f = 0; f < frames; ++f) {
        setRestPose(skeleton, out.frames[f]);
        sampleClip(clip, std::min(clip.start + out.timeOf(f), clip.duration), out.frames[f]);
    }
    return out;
}

AnimationClip unbake(const Skeleton& skeleton, const Baked& baked, std::string name) {
    AnimationClip clip;
    clip.name = std::move(name);
    clip.start = 0.0f;
    clip.duration = baked.timeOf(baked.count() - 1);
    for (std::size_t j = 0; j < skeleton.joints.size(); ++j) {
        AnimationChannel t;
        t.joint = static_cast<std::uint32_t>(j);
        t.path = AnimationPath::Translation;
        t.interpolation = Interpolation::Linear;
        AnimationChannel r = t;
        r.path = AnimationPath::Rotation;
        AnimationChannel s = t;
        s.path = AnimationPath::Scale;
        for (std::size_t f = 0; f < baked.count(); ++f) {
            const Transform& x = baked.frames[f].local[j];
            const float time = baked.timeOf(f);
            t.times.push_back(time);
            t.values.emplace_back(x.position, 0.0f);
            r.times.push_back(time);
            r.values.emplace_back(x.rotation.x, x.rotation.y, x.rotation.z, x.rotation.w);
            s.times.push_back(time);
            s.values.emplace_back(x.scale, 0.0f);
        }
        clip.channels.push_back(std::move(t));
        clip.channels.push_back(std::move(r));
        clip.channels.push_back(std::move(s));
    }
    return clip;
}

glm::vec3 flat(const glm::vec3& v) { return {v.x, 0.0f, v.z}; }

glm::vec3 positionOf(const glm::mat4& m) { return glm::vec3(m[3]); }

// Premultiply every top-level joint by `w`, which moves the whole body by `w` in model space.
void transformBody(const Skeleton& skeleton, Pose& pose, const glm::mat4& w) {
    for (std::size_t j = 0; j < skeleton.joints.size(); ++j) {
        if (skeleton.joints[j].parent < 0) {
            pose.local[j] = Transform::fromMatrix(w * pose.local[j].matrix());
        }
    }
}

// Set joint `j`'s local so that its model matrix becomes `desired`, given the current pose.
void setModel(const Skeleton& skeleton, Pose& pose, std::size_t j, const glm::mat4& desired) {
    std::vector<glm::mat4> model;
    poseToModel(skeleton, pose, model);
    const int parent = skeleton.joints[j].parent;
    const glm::mat4 parentModel = parent < 0 ? glm::mat4(1.0f) : model[static_cast<std::size_t>(parent)];
    pose.local[j] = Transform::fromMatrix(glm::inverse(parentModel) * desired);
}

struct LegIds {
    int root = -1;
    int mid = -1;
    int tip = -1;
    int track = -1; // index into the contacts for this tip
};

// One affine map per frame from the planted clip's horizontal plane to the variant's:
// x -> centre + linear * (x - origin), with the vertical untouched.
struct FrameMap {
    glm::vec3 origin{0.0f};
    glm::vec3 centre{0.0f};
    glm::mat3 linear{1.0f};
    float yaw = 0.0f; // the heading the body is turned by at this frame
    [[nodiscard]] glm::vec3 apply(const glm::vec3& x) const {
        const glm::vec3 d = linear * flat(x - origin);
        return {centre.x + d.x, x.y, centre.z + d.z};
    }
};

glm::mat3 yawMatrix(float yaw) {
    return glm::mat3(glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
}

float smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - (2.0f * t));
}

} // namespace

std::vector<glm::vec3> plantedVelocity(const Skeleton& skeleton, const AnimationClip& clip,
                                       const std::vector<ContactTrack>& contacts, float sampleRate,
                                       bool loop) {
    const Baked baked = bake(skeleton, clip, sampleRate);
    const std::size_t n = baked.count();
    std::vector<std::optional<glm::vec3>> known(n);
    std::vector<glm::mat4> a;
    std::vector<glm::mat4> b;
    for (std::size_t f = 0; f + 1 < n; ++f) {
        poseToModel(skeleton, baked.frames[f], a);
        poseToModel(skeleton, baked.frames[f + 1], b);
        glm::vec3 sum(0.0f);
        int count = 0;
        for (const ContactTrack& track : contacts) {
            const int j = skeleton.find(track.joint);
            if (j < 0 || track.kind != ContactKind::Foot || !plantedAt(track, baked.timeOf(f)) ||
                !plantedAt(track, baked.timeOf(f + 1))) {
                continue;
            }
            const auto jj = static_cast<std::size_t>(j);
            sum -= flat(positionOf(b[jj]) - positionOf(a[jj])) / baked.dt;
            ++count;
        }
        if (count > 0) {
            known[f] = sum / static_cast<float>(count);
        }
    }
    // The last frame: a loop's is its first (the same instant), anything else repeats the frame
    // before it.
    if (n >= 2) {
        known[n - 1] = loop ? known[0] : known[n - 2];
    }
    // Fill the gaps (flight, a scuff, a frame the detector missed) by interpolating between the
    // nearest known neighbours, around the loop when there is one.
    std::vector<glm::vec3> out(n, glm::vec3(0.0f));
    std::vector<std::size_t> have;
    for (std::size_t f = 0; f < n; ++f) {
        if (known[f]) {
            have.push_back(f);
        }
    }
    if (have.empty()) {
        return out;
    }
    for (std::size_t f = 0; f < n; ++f) {
        if (known[f]) {
            out[f] = *known[f];
            continue;
        }
        const auto next = std::upper_bound(have.begin(), have.end(), f);
        const bool hasNext = next != have.end();
        const bool hasPrev = next != have.begin();
        if (hasPrev && hasNext) {
            const std::size_t p = *(next - 1);
            const std::size_t q = *next;
            const float s = static_cast<float>(f - p) / static_cast<float>(q - p);
            out[f] = glm::mix(*known[p], *known[q], s);
        } else if (loop && have.size() > 1) {
            const std::size_t p = have.back();
            const std::size_t q = have.front();
            const std::size_t span = (n - 1 - p) + q;
            const std::size_t into = f > p ? f - p : (n - 1 - p) + f;
            out[f] = glm::mix(*known[p], *known[q], static_cast<float>(into) / static_cast<float>(std::max<std::size_t>(span, 1)));
        } else {
            out[f] = *known[hasPrev ? *(next - 1) : *next];
        }
    }
    return out;
}

namespace {

AugmentResult mirrorClip(const Skeleton& skeleton, const AnimationClip& source, const AugmentOptions& options) {
    AugmentResult out;
    out.kind = AugmentKind::Mirror;
    // Pair every joint with its twin by name. A joint with no twin (the spine, the head) is its
    // own.
    const std::size_t n = skeleton.joints.size();
    std::vector<std::size_t> twin(n);
    std::size_t pairs = 0;
    for (std::size_t j = 0; j < n; ++j) {
        twin[j] = j;
        const std::string& name = skeleton.joints[j].name;
        for (const auto& [left, right] : options.mirrorSuffixes) {
            for (const auto& [from, to] : {std::pair{left, right}, std::pair{right, left}}) {
                if (name.size() > from.size() && name.compare(name.size() - from.size(), from.size(), from) == 0) {
                    const int other = skeleton.find(name.substr(0, name.size() - from.size()) + to);
                    if (other >= 0 && twin[j] == j) {
                        twin[j] = static_cast<std::size_t>(other);
                        ++pairs;
                    }
                }
            }
        }
    }
    if (pairs == 0) {
        out.refusal = "no joint has a left/right twin by name, so there is nothing to exchange";
        return out;
    }
    const Baked baked = bake(skeleton, source, options.sampleRate);
    std::vector<glm::mat4> rest;
    poseToModel(skeleton, restPose(skeleton), rest);
    const glm::mat3 s(glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    const auto rotationOf = [](const glm::mat4& m) {
        return glm::mat3(glm::normalize(glm::vec3(m[0])), glm::normalize(glm::vec3(m[1])),
                         glm::normalize(glm::vec3(m[2])));
    };
    const auto scaleOf = [](const glm::mat4& m) {
        return glm::vec3(glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])));
    };
    Baked mirrored = baked;
    std::vector<glm::mat4> model;
    std::vector<glm::mat4> desired(n);
    for (std::size_t f = 0; f < baked.count(); ++f) {
        poseToModel(skeleton, baked.frames[f], model);
        for (std::size_t j = 0; j < n; ++j) {
            const std::size_t p = twin[j];
            // Reflect the twin's rotation, then correct for the rest pose, so a joint at rest maps
            // to itself even on a rig whose left and right bind frames are not mirror images.
            const glm::mat3 reflected = s * rotationOf(model[p]) * s;
            const glm::mat3 restReflected = s * rotationOf(rest[p]) * s;
            const glm::mat3 r = reflected * glm::transpose(restReflected) * rotationOf(rest[j]);
            const glm::vec3 q = (s * positionOf(model[p])) + (positionOf(rest[j]) - (s * positionOf(rest[p])));
            const glm::vec3 sc = scaleOf(model[j]);
            glm::mat4 d(1.0f);
            d[0] = glm::vec4(r[0] * sc.x, 0.0f);
            d[1] = glm::vec4(r[1] * sc.y, 0.0f);
            d[2] = glm::vec4(r[2] * sc.z, 0.0f);
            d[3] = glm::vec4(q, 1.0f);
            desired[j] = d;
        }
        for (std::size_t j = 0; j < n; ++j) {
            const int parent = skeleton.joints[j].parent;
            const glm::mat4 pm = parent < 0 ? glm::mat4(1.0f) : desired[static_cast<std::size_t>(parent)];
            mirrored.frames[f].local[j] = Transform::fromMatrix(glm::inverse(pm) * desired[j]);
        }
    }
    out.clip = unbake(skeleton, mirrored, fmt::format("{}~mirror", source.name));
    out.heading.assign(mirrored.count(), 0.0f);
    out.accepted = true;
    return out;
}

} // namespace

AugmentResult augmentClip(const Skeleton& skeleton, const AnimationClip& source, bool loop,
                          AugmentKind kind, float parameter, const AugmentOptions& options) {
    if (kind == AugmentKind::Mirror) {
        return mirrorClip(skeleton, source, options);
    }
    AugmentResult out;
    out.kind = kind;
    out.parameter = parameter;
    if (source.length() <= 0.0f) {
        out.refusal = "the source clip has no length";
        return out;
    }
    Baked baked = bake(skeleton, source, options.sampleRate);
    const std::size_t n = baked.count();
    const auto root = static_cast<std::size_t>(travelJoint(source));

    // ---- plant: the treadmill becomes travel ------------------------------------------------
    //
    // Measured and applied only where the planted feet imply motion. A clip whose root already
    // travels has feet that are already still, so the implied velocity is near zero and this
    // leaves it alone.
    const std::vector<glm::vec3> velocity =
        plantedVelocity(skeleton, source, options.contacts, options.sampleRate, loop);
    glm::vec3 offset(0.0f);
    for (std::size_t f = 0; f < n; ++f) {
        transformBody(skeleton, baked.frames[f], glm::translate(glm::mat4(1.0f), offset));
        offset += velocity[f] * baked.dt;
    }

    // The planted clip's models, and where its travel joint is.
    std::vector<std::vector<glm::mat4>> model(n);
    std::vector<glm::vec3> path(n);
    for (std::size_t f = 0; f < n; ++f) {
        poseToModel(skeleton, baked.frames[f], model[f]);
        path[f] = flat(positionOf(model[f][root]));
    }

    // ---- the per-frame map ----------------------------------------------------------------
    std::vector<FrameMap> maps(n);
    const float duration = baked.timeOf(n - 1);
    for (std::size_t f = 0; f < n; ++f) {
        maps[f].origin = path[f];
    }
    switch (kind) {
    case AugmentKind::Plant:
        for (std::size_t f = 0; f < n; ++f) {
            maps[f].centre = path[f];
        }
        break;
    case AugmentKind::Stride:
        for (std::size_t f = 0; f < n; ++f) {
            maps[f].centre = path[f] * parameter;
            maps[f].linear = glm::mat3(glm::vec3(parameter, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, parameter));
        }
        break;
    case AugmentKind::Direction:
        for (std::size_t f = 0; f < n; ++f) {
            maps[f].linear = yawMatrix(parameter);
            maps[f].centre = maps[f].linear * path[f];
        }
        break;
    case AugmentKind::Turn:
    case AugmentKind::Start:
    case AugmentKind::Stop: {
        // Time-varying: integrate the planted path's steps through the map at each step, so the
        // new path is continuous and a planted foot can be anchored to the map of its stance.
        glm::vec3 centre = path[0];
        for (std::size_t f = 0; f < n; ++f) {
            const float t = baked.timeOf(f);
            float gain = 1.0f;
            if (kind == AugmentKind::Start) {
                const float ramp = std::max(options.rampShare * duration, baked.dt);
                gain = options.rampFloor + ((1.0f - options.rampFloor) * smooth(t / ramp));
            } else if (kind == AugmentKind::Stop) {
                const float ramp = std::max(options.rampShare * duration, baked.dt);
                gain = options.rampFloor + ((1.0f - options.rampFloor) * smooth((duration - t) / ramp));
            }
            const float yaw = kind == AugmentKind::Turn ? parameter * t : 0.0f;
            maps[f].yaw = yaw;
            maps[f].linear = yawMatrix(yaw) * glm::mat3(glm::vec3(gain, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, gain));
            maps[f].centre = centre;
            if (f + 1 < n) {
                centre += maps[f].linear * (path[f + 1] - path[f]);
            }
        }
        break;
    }
    case AugmentKind::Mirror:
        break;
    }

    // ---- the legs ---------------------------------------------------------------------------
    std::vector<LegIds> legs;
    for (const AugmentLeg& leg : options.legs) {
        LegIds ids;
        ids.root = skeleton.find(leg.root);
        ids.mid = skeleton.find(leg.mid);
        ids.tip = skeleton.find(leg.tip);
        for (std::size_t k = 0; k < options.contacts.size(); ++k) {
            if (options.contacts[k].joint == leg.tip) {
                ids.track = static_cast<int>(k);
            }
        }
        if (ids.root < 0 || ids.mid < 0 || ids.tip < 0) {
            out.refusal = fmt::format("leg {}/{}/{} is not on this skeleton", leg.root, leg.mid, leg.tip);
            return out;
        }
        legs.push_back(ids);
    }

    // For each leg and frame: in stance, the stance's first frame, whose map holds the foot where
    // it landed. In swing, the stance frames either side of it (lift-off `lift`, touch-down
    // `land`) and how far between them.
    //
    // **A swing foot follows the frame's own map, corrected toward its anchors.** Mapping a swing
    // foot through a stance frame's map (the obvious choice) puts it wherever that frame's body was.
    // Under a time-varying map (a start's growing stride, a turn's growing heading) that is up to a
    // stride away from where the body is now, and the scout's leg cannot reach it. So a swing target
    // is the foot mapped by *this* frame's map, plus the difference each anchor would have made at
    // its own planted position, blended from the lift-off's to the touch-down's. That equals the
    // stance target exactly at both ends and never strays from the body in between.
    struct Anchor {
        bool stance = false;
        std::size_t at = 0;    // stance: the frame whose map holds the foot
        std::size_t lift = 0;  // swing: last planted frame before, or n for none
        std::size_t land = 0;  // swing: first planted frame after, or n for none
        float liftWeight = 0.0f;
        float landWeight = 0.0f;
    };
    const auto anchorsFor = [&](const LegIds& leg) {
        std::vector<Anchor> a(n);
        std::vector<bool> planted(n, false);
        if (leg.track >= 0) {
            for (std::size_t f = 0; f < n; ++f) {
                planted[f] = plantedAt(options.contacts[static_cast<std::size_t>(leg.track)], baked.timeOf(f));
            }
        }
        std::vector<std::size_t> stanceStart(n, n);
        for (std::size_t f = 0; f < n; ++f) {
            if (planted[f]) {
                stanceStart[f] = (f > 0 && planted[f - 1]) ? stanceStart[f - 1] : f;
            }
        }
        for (std::size_t f = 0; f < n; ++f) {
            Anchor& x = a[f];
            if (planted[f]) {
                x.stance = true;
                x.at = stanceStart[f];
                continue;
            }
            x.lift = n;
            for (std::size_t g = f; g-- > 0;) {
                if (planted[g]) {
                    x.lift = g;
                    break;
                }
            }
            x.land = n;
            for (std::size_t g = f + 1; g < n; ++g) {
                if (planted[g]) {
                    x.land = g;
                    break;
                }
            }
            if (x.lift < n && x.land < n) {
                const float s = smooth(static_cast<float>(f - x.lift) / static_cast<float>(x.land - x.lift));
                x.liftWeight = 1.0f - s;
                x.landWeight = s;
            } else if (x.lift < n) {
                // After the last stance of a clip that does not loop: the correction fades out.
                x.liftWeight = 1.0f - smooth(static_cast<float>(f - x.lift) /
                                             static_cast<float>(std::max<std::size_t>(n - 1 - x.lift, 1)));
            } else if (x.land < n) {
                // Before the first stance: the correction fades in.
                x.landWeight = smooth(static_cast<float>(f) / static_cast<float>(std::max<std::size_t>(x.land, 1)));
            }
        }
        return a;
    };

    std::vector<std::vector<Anchor>> anchors;
    for (const LegIds& leg : legs) {
        anchors.push_back(anchorsFor(leg));
    }

    Baked result = baked;
    std::vector<glm::mat4> now;
    std::vector<glm::vec3> planted0(legs.size());
    for (std::size_t f = 0; f < n; ++f) {
        Pose& pose = result.frames[f];
        // The body: its travel joint moved from the planted path to the map's centre, and turned
        // by the map's yaw about its own vertical.
        const glm::vec3 from = path[f];
        const glm::mat4 body = glm::translate(glm::mat4(1.0f), maps[f].centre) *
                               glm::rotate(glm::mat4(1.0f), maps[f].yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
                               glm::translate(glm::mat4(1.0f), -from);
        transformBody(skeleton, pose, body);

        for (std::size_t l = 0; l < legs.size(); ++l) {
            const LegIds& leg = legs[l];
            const Anchor& a = anchors[l][f];
            const auto tipIndex = static_cast<std::size_t>(leg.tip);
            const glm::vec3 foot = positionOf(model[f][tipIndex]);
            glm::vec3 target;
            float yaw;
            if (a.stance) {
                target = maps[a.at].apply(foot);
                yaw = maps[a.at].yaw;
            } else {
                target = maps[f].apply(foot);
                yaw = maps[f].yaw;
                for (const auto& [anchorFrame, weight] : {std::pair{a.lift, a.liftWeight}, std::pair{a.land, a.landWeight}}) {
                    if (anchorFrame < n && weight > 0.0f) {
                        const glm::vec3 there = positionOf(model[anchorFrame][tipIndex]);
                        target += weight * (maps[anchorFrame].apply(there) - maps[f].apply(there));
                        yaw += weight * (maps[anchorFrame].yaw - maps[f].yaw);
                    }
                }
            }

            poseToModel(skeleton, pose, now);
            TwoBoneChain chain;
            chain.root = positionOf(now[static_cast<std::size_t>(leg.root)]);
            chain.mid = positionOf(now[static_cast<std::size_t>(leg.mid)]);
            chain.tip = positionOf(now[static_cast<std::size_t>(leg.tip)]);
            // A pole in front of the knee, for the one case the solve needs it: a leg that is
            // straight now, whose bend plane the current pose cannot name. A bent leg keeps its own
            // plane whatever the pole says. Forward is the body's, turned by this frame's heading,
            // which is where a biped's knee points.
            const glm::vec3 forward = yawMatrix(maps[f].yaw) * glm::vec3(0.0f, 0.0f, 1.0f);
            const float reach = glm::length(chain.mid - chain.root) + glm::length(chain.tip - chain.mid);
            const glm::vec3 pole = (0.5f * (chain.root + target)) + (forward * reach);
            const TwoBoneSolution sol = solveTwoBone(chain, target, pole, true);
            if (sol.status == IkStatus::DegenerateBone || sol.status == IkStatus::DegenerateTarget) {
                out.refusal = fmt::format("the solve for {} was degenerate at frame {}", skeleton.joints[static_cast<std::size_t>(leg.tip)].name, f);
                return out;
            }
            const float length = sol.upperLength + sol.lowerLength;
            if (length > 0.0f) {
                out.worstShortfall = std::max(out.worstShortfall, glm::length(target - sol.tip) / length);
            }
            // The two model-space pre-rotations `pose_layers` applies, about the hip and the knee.
            const auto pre = [](const glm::vec3& pivot, const glm::quat& q, const glm::mat4& m) {
                return glm::translate(glm::mat4(1.0f), pivot) * glm::mat4_cast(q) *
                       glm::translate(glm::mat4(1.0f), -pivot) * m;
            };
            const glm::mat4 hipModel = pre(chain.root, sol.rootDelta, now[static_cast<std::size_t>(leg.root)]);
            const glm::vec3 knee = chain.root + (sol.rootDelta * (chain.mid - chain.root));
            const glm::quat bendHere = sol.rootDelta * sol.midBend * glm::conjugate(sol.rootDelta);
            const glm::mat4 kneeModel =
                pre(knee, bendHere, pre(chain.root, sol.rootDelta, now[static_cast<std::size_t>(leg.mid)]));
            // The foot keeps the orientation it had, turned by the heading of its anchor.
            const glm::mat4 sourceFoot = model[f][static_cast<std::size_t>(leg.tip)];
            glm::mat4 footModel = glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f)) * sourceFoot;
            footModel[3] = glm::vec4(sol.tip, 1.0f);

            setModel(skeleton, pose, static_cast<std::size_t>(leg.root), hipModel);
            setModel(skeleton, pose, static_cast<std::size_t>(leg.mid), kneeModel);
            setModel(skeleton, pose, static_cast<std::size_t>(leg.tip), footModel);
        }
    }

    // How still a planted foot actually stays, measured on the output rather than assumed.
    std::vector<std::vector<glm::mat4>> outModel(n);
    for (std::size_t f = 0; f < n; ++f) {
        poseToModel(skeleton, result.frames[f], outModel[f]);
    }
    for (std::size_t l = 0; l < legs.size(); ++l) {
        for (std::size_t f = 1; f < n; ++f) {
            const Anchor& a = anchors[l][f];
            const Anchor& b = anchors[l][f - 1];
            if (a.stance && b.stance && a.at == b.at) {
                const auto tip = static_cast<std::size_t>(legs[l].tip);
                const float drift = glm::length(flat(positionOf(outModel[f][tip]) - positionOf(outModel[f - 1][tip])));
                // The source's own residual slide is carried through; this reports the variant's.
                out.worstPlantedDrift = std::max(out.worstPlantedDrift, drift);
            }
        }
    }

    out.clip = unbake(skeleton, result, fmt::format("{}~{}{:+.2f}", source.name, augmentKindName(kind), parameter));
    out.heading.resize(n);
    for (std::size_t f = 0; f < n; ++f) {
        out.heading[f] = maps[f].yaw;
    }
    out.accepted = out.worstShortfall <= options.maxShortfall;
    if (!out.accepted) {
        out.refusal = fmt::format("a foot misses its target by {:.1f}% of the leg, over the {:.1f}% limit",
                                  out.worstShortfall * 100.0f, options.maxShortfall * 100.0f);
    }
    return out;
}

namespace {

// Did `after` improve on `before` anywhere? A category improves when its grade rises, or when it
// was below good and gained windows (a limited category that doubles is still limited, and it is
// still the gap §21 exists to fill).
std::vector<std::string> coverageGains(const MotionCategoryReport& before, const MotionCategoryReport& after) {
    std::vector<std::string> gains;
    for (std::size_t c = 0; c < before.categories.size() && c < after.categories.size(); ++c) {
        const MotionCategoryCoverage& b = before.categories[c];
        const MotionCategoryCoverage& a = after.categories[c];
        const bool gradeUp = static_cast<int>(a.grade) > static_cast<int>(b.grade);
        const bool fills = b.grade != CoverageGrade::Good && a.windows > b.windows;
        if (gradeUp || fills) {
            gains.push_back(fmt::format("{}: {} {} -> {} {}", motionCategoryName(a.category),
                                        coverageGradeName(b.grade), b.windows, coverageGradeName(a.grade),
                                        a.windows));
        }
    }
    return gains;
}

} // namespace

std::string AugmentPackResult::report() const {
    std::string out = fmt::format("augmentation: {} planned, {} generated, {} kept\n", decisions.size(),
                                  std::count_if(decisions.begin(), decisions.end(),
                                                [](const AugmentDecision& d) { return d.generated; }),
                                  std::count_if(decisions.begin(), decisions.end(),
                                                [](const AugmentDecision& d) { return d.kept; }));
    for (const AugmentDecision& d : decisions) {
        out += fmt::format("  {:<5} {:<10} {:+.2f} from {:<18} {}", d.kept ? "KEEP" : "drop",
                           augmentKindName(d.item.kind), d.item.parameter, d.item.clip, d.reason);
        for (const std::string& g : d.gains) {
            out += "  [" + g + "]";
        }
        out += "\n";
    }
    out += "coverage before -> after:\n";
    for (std::size_t c = 0; c < before.categories.size() && c < after.categories.size(); ++c) {
        out += fmt::format("  {:<22} {:<8} {:>4}  ->  {:<8} {:>4}\n", motionCategoryName(before.categories[c].category),
                           coverageGradeName(before.categories[c].grade), before.categories[c].windows,
                           coverageGradeName(after.categories[c].grade), after.categories[c].windows);
    }
    return out;
}

Result<AugmentPackResult> augmentPack(const MotionPack& pack, const std::vector<AugmentPlanItem>& plan,
                                      const AugmentPackOptions& options) {
    AugmentPackResult out;
    out.pack = pack;
    auto baseDb = buildMotionDatabase(out.pack, options.database);
    if (!baseDb) {
        return fail("augmentation: the source pack's database did not build: {}", baseDb.error().message);
    }
    out.before = measureMotionCategories(*baseDb, options.categories);
    MotionCategoryReport current = out.before;

    for (const AugmentPlanItem& item : plan) {
        AugmentDecision d;
        d.item = item;
        const int index = findClip(pack.animation, item.clip);
        if (index < 0 || static_cast<std::size_t>(index) >= pack.clips.size()) {
            d.reason = "no such clip in the pack";
            out.decisions.push_back(std::move(d));
            continue;
        }
        const PackClip& sourceMeta = pack.clips[static_cast<std::size_t>(index)];
        AugmentOptions augment = options.augment;
        augment.contacts = sourceMeta.contacts;
        AugmentResult variant = augmentClip(pack.skeleton, pack.animation[static_cast<std::size_t>(index)],
                                            sourceMeta.loop, item.kind, item.parameter, augment);
        if (!variant.accepted) {
            d.reason = "refused by its own gate: " + variant.refusal;
            out.decisions.push_back(std::move(d));
            continue;
        }
        d.generated = true;
        d.name = variant.clip.name;

        // The variant as a pack clip: its own analysis, its generator's heading, and a provenance
        // that says what it came from and what was done to it (§43), under the source's licence.
        MotionPack candidate = out.pack;
        PackClip meta;
        meta.name = variant.clip.name;
        meta.length = variant.clip.length();
        meta.sampleRate = augment.sampleRate;
        meta.frames = static_cast<std::uint32_t>(variant.heading.size());
        // A start or a stop is a one-off by construction. So is a turn: it ends facing somewhere
        // else, and a wrap that carried it back into its own start would put a turn the other way
        // into every look ahead that crosses the seam (seen as a right turn in a left-turn variant).
        meta.loop = sourceMeta.loop && item.kind != AugmentKind::Start && item.kind != AugmentKind::Stop &&
                    item.kind != AugmentKind::Turn;
        meta.tags = sourceMeta.tags;
        meta.heading = variant.heading;
        std::vector<ContactJoint> joints;
        for (const ContactTrack& track : sourceMeta.contacts) {
            joints.push_back(ContactJoint{track.joint, track.kind});
        }
        ContactSettings contactSettings;
        contactSettings.sampleRate = augment.sampleRate;
        contactSettings.looping = meta.loop;
        ClipAnalysis analysis = analyseClip(pack.skeleton, variant.clip, joints, 0, contactSettings);
        meta.contacts = std::move(analysis.contacts);
        for (ContactTrack& track : meta.contacts) {
            track.jointIndex = pack.skeleton.find(track.joint);
        }
        meta.phase = std::move(analysis.phase);
        meta.rootTravel = analysis.rootTravel;
        meta.groundSpeed = analysis.groundSpeed;
        Provenance provenance = sourceMeta.provenance < pack.provenance.size()
                                    ? pack.provenance[sourceMeta.provenance]
                                    : Provenance{};
        provenance.processing.push_back(fmt::format("Phase C §21 {} {:+.3f} from '{}'", augmentKindName(item.kind),
                                                    item.parameter, item.clip));
        candidate.provenance.push_back(std::move(provenance));
        meta.provenance = static_cast<std::uint32_t>(candidate.provenance.size() - 1);
        candidate.clips.push_back(std::move(meta));
        candidate.animation.push_back(std::move(variant.clip));

        auto db = buildMotionDatabase(candidate, options.database);
        if (!db) {
            d.reason = "its database did not build: " + db.error().message;
            out.decisions.push_back(std::move(d));
            continue;
        }
        const MotionCategoryReport after = measureMotionCategories(*db, options.categories);
        d.gains = coverageGains(current, after);
        if (d.gains.empty()) {
            d.reason = "redundant: no category gained";
        } else {
            d.kept = true;
            d.reason = "adds coverage";
            out.pack = std::move(candidate);
            current = after;
        }
        out.decisions.push_back(std::move(d));
    }
    out.after = current;
    return out;
}

} // namespace avgen::scene
