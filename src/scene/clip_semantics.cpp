#include "scene/clip_semantics.hpp"

#include "scene/animation.hpp"
#include "scene/motion_analysis.hpp"
#include "scene/retarget.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

namespace avgen::scene {
namespace {

// A maximal run of unsupported samples, [begin, end) in sample indices. `wraps` when a looping
// clip's run crosses its loop point: `begin` is near the end and `end` near the start.
struct Run {
    std::size_t begin = 0;
    std::size_t end = 0;
    bool wraps = false;
    [[nodiscard]] std::size_t length(std::size_t n) const { return wraps ? (n - begin) + end : end - begin; }
};

std::vector<Run> unsupportedRuns(const std::vector<bool>& supported, bool loops) {
    const std::size_t n = supported.size();
    std::vector<Run> runs;
    for (std::size_t i = 0; i < n;) {
        if (supported[i]) {
            ++i;
            continue;
        }
        Run r;
        r.begin = i;
        while (i < n && !supported[i]) {
            ++i;
        }
        r.end = i;
        runs.push_back(r);
    }
    // A looping clip's first and last runs are one run across the loop point, exactly as a contact
    // span wraps (`ContactSettings::looping`). Not when the whole clip is one run.
    if (loops && runs.size() >= 2 && runs.front().begin == 0 && runs.back().end == n) {
        Run merged;
        merged.begin = runs.back().begin;
        merged.end = runs.front().end;
        merged.wraps = true;
        runs.pop_back();
        runs.front() = merged;
    }
    return runs;
}

} // namespace

const char* clipGroundName(ClipGround ground) {
    switch (ground) {
    case ClipGround::Grounded: return "grounded";
    case ClipGround::Leaves: return "leaves";
    case ClipGround::Airborne: return "airborne";
    }
    return "?";
}

const ClipEvent* ClipSemantics::event(std::string_view name) const {
    for (const ClipEvent& e : events) {
        if (e.name == name) {
            return &e;
        }
    }
    return nullptr;
}

bool ClipSemantics::interruptibleAt(float seconds) const {
    for (const auto& [from, to] : committed) {
        if (seconds > from && seconds < to) {
            return false;
        }
    }
    return true;
}

const ClipSemantics* ClipSemanticsTable::find(std::string_view clip) const {
    for (const ClipSemantics& c : clips) {
        if (c.clip == clip) {
            return &c;
        }
    }
    return nullptr;
}

std::vector<int> footJoints(const Skeleton& skeleton) {
    Pose rest;
    std::vector<glm::mat4> model;
    setRestPose(skeleton, rest);
    poseToModel(skeleton, rest, model);
    int left = -1;
    int right = -1;
    for (std::size_t i = 0; i < skeleton.joints.size(); ++i) {
        const HumanoidRole role = roleForJointName(skeleton.joints[i].name);
        int* slot = role == HumanoidRole::LeftFoot ? &left : role == HumanoidRole::RightFoot ? &right : nullptr;
        if (slot == nullptr) {
            continue;
        }
        // Several joints can read as a foot (a deform bone and a controller); the one that stands
        // on the ground is the lowest in the rest pose.
        if (*slot < 0 || model[i][3].y < model[static_cast<std::size_t>(*slot)][3].y) {
            *slot = static_cast<int>(i);
        }
    }
    std::vector<int> out;
    if (left >= 0) {
        out.push_back(left);
    }
    if (right >= 0) {
        out.push_back(right);
    }
    return out;
}

ClipSemanticsTable clipSemantics(const Skeleton& skeleton, const std::vector<AnimationClip>& clips,
                                 const ClipSemanticsSettings& settings) {
    ClipSemanticsTable table;
    table.restHeight = skeletonRestHeight(skeleton);
    const std::vector<int> feet = footJoints(skeleton);
    std::vector<ContactJoint> contactJoints;
    for (const int f : feet) {
        table.feet.push_back(skeleton.joints[static_cast<std::size_t>(f)].name);
        contactJoints.push_back(ContactJoint{skeleton.joints[static_cast<std::size_t>(f)].name, ContactKind::Foot});
    }
    Pose pose;
    std::vector<glm::mat4> model;
    setRestPose(skeleton, pose);
    poseToModel(skeleton, pose, model);
    if (feet.empty()) {
        table.warnings.push_back("no joint reads as a foot, so nothing about the ground is known: every clip "
                                 "is reported grounded, with no flight and no contact events");
    } else {
        table.groundDatum = model[static_cast<std::size_t>(feet.front())][3].y;
        for (const int f : feet) {
            table.groundDatum = std::min(table.groundDatum, model[static_cast<std::size_t>(f)][3].y);
        }
    }
    // What can touch the ground: the feet, and the rest of the body's extremities. Feet alone read a
    // body lying on its back as airborne -- measured: the three deaths came back as "leaves" with a
    // negative peak, because the legs rise as the torso goes down. Hands, head and hips are found by
    // the same role guess the feet are; a rig that names none of them is judged on its feet.
    std::vector<int> touching = feet;
    for (std::size_t i = 0; i < skeleton.joints.size(); ++i) {
        const HumanoidRole role = roleForJointName(skeleton.joints[i].name);
        if (role == HumanoidRole::LeftHand || role == HumanoidRole::RightHand || role == HumanoidRole::Head ||
            role == HumanoidRole::Hips) {
            touching.push_back(static_cast<int>(i));
        }
    }
    const float band = settings.supportFraction * table.restHeight;
    const float dt = 1.0f / std::max(settings.sampleRate, 1.0f);

    table.clips.reserve(clips.size());
    for (const AnimationClip& clip : clips) {
        ClipSemantics s;
        const std::string_view full = clip.name;
        const std::size_t bar = full.rfind('|');
        s.clip = std::string(bar == std::string_view::npos ? full : full.substr(bar + 1));
        s.length = clip.length();
        const LoopClosure closure = measureLoopClosure(skeleton, clip, settings.sampleRate);
        s.loops = closure.loops;
        s.loopGapSteps = closure.typicalStep > 0.0f ? closure.gap / closure.typicalStep : 0.0f;

        ContactSettings contact;
        contact.sampleRate = settings.sampleRate;
        contact.looping = s.loops;
        const ClipAnalysis analysis = analyseClip(skeleton, clip, contactJoints, 0, contact);
        s.travels = analysis.travels;
        s.rootTravel = analysis.rootTravel;
        s.groundSpeed = analysis.groundSpeed;

        // The ground, sample by sample, against the rig's one datum.
        const int bodyJoint = clipTravelJoint(skeleton, clip);
        const auto samples = static_cast<std::size_t>(std::lround(s.length * settings.sampleRate)) + 1;
        std::vector<bool> supported(samples, true);
        std::vector<float> body(samples, 0.0f);
        float restBody = 0.0f;
        if (bodyJoint >= 0) {
            setRestPose(skeleton, pose);
            poseToModel(skeleton, pose, model);
            restBody = model[static_cast<std::size_t>(bodyJoint)][3].y;
        }
        setRestPose(skeleton, pose);
        for (std::size_t i = 0; i < samples; ++i) {
            const float t = clip.start + std::min(static_cast<float>(i) * dt, s.length);
            sampleClip(clip, t, pose);
            poseToModel(skeleton, pose, model);
            if (!feet.empty()) {
                float lowest = model[static_cast<std::size_t>(touching.front())][3].y;
                for (const int f : touching) {
                    lowest = std::min(lowest, model[static_cast<std::size_t>(f)][3].y);
                }
                supported[i] = lowest <= table.groundDatum + band;
            }
            if (bodyJoint >= 0) {
                body[i] = model[static_cast<std::size_t>(bodyJoint)][3].y;
            }
        }
        // A looping clip's last sample IS its first; counting both would weight the loop point twice.
        const std::size_t counted = s.loops && samples > 1 ? samples - 1 : samples;
        std::size_t supportedCount = 0;
        for (std::size_t i = 0; i < counted; ++i) {
            supportedCount += supported[i] ? 1u : 0u;
        }
        s.supportedFraction = counted > 0 ? static_cast<float>(supportedCount) / static_cast<float>(counted) : 1.0f;
        s.startsSupported = supported.front();
        s.endsSupported = supported.back();

        const std::vector<bool> ring(supported.begin(), supported.begin() + static_cast<std::ptrdiff_t>(counted));
        const std::vector<Run> runs = unsupportedRuns(ring, s.loops);
        const Run* principal = nullptr;
        for (const Run& r : runs) {
            if (static_cast<float>(r.length(counted)) * dt < settings.minFlightSeconds) {
                continue;
            }
            if (principal == nullptr || r.length(counted) > principal->length(counted)) {
                principal = &r;
            }
        }
        if (s.supportedFraction < settings.airborneBelow) {
            s.ground = ClipGround::Airborne;
        } else if (principal != nullptr) {
            s.ground = ClipGround::Leaves;
        }
        if (principal != nullptr) {
            s.flightStart = static_cast<float>(principal->begin) * dt;
            s.flightEnd = static_cast<float>(principal->end) * dt;
            // Takeoff is the first sample off the ground; touchdown the first back on it. A flight
            // that runs off either end of a one-shot has no takeoff or no touchdown in this clip,
            // and says so by omission rather than by an invented time.
            if (principal->wraps || principal->begin > 0) {
                s.events.push_back({"takeoff", s.flightStart});
            }
            if (principal->wraps || principal->end < counted) {
                s.events.push_back({"touchdown", s.flightEnd});
            }
            std::size_t best = principal->begin % counted;
            for (std::size_t k = 0; k < principal->length(counted); ++k) {
                const std::size_t i = (principal->begin + k) % counted;
                if (body[i] > body[best]) {
                    best = i;
                }
            }
            // A peak is a jump's: the top of a flight the body left the ground for. In a clip that is
            // airborne throughout (a fall loop, a float) the highest sample is a bob, not an event.
            if (s.ground == ClipGround::Leaves) {
                s.events.push_back({"peak", static_cast<float>(best) * dt});
                s.peakHeight = body[best] - restBody;
            }
            if (!s.loops) {
                s.committed.emplace_back(s.flightStart, s.flightEnd);
            }
        }
        for (const ContactTrack& track : analysis.contacts) {
            for (const ContactSpan& span : track.spans) {
                // A span that is the whole clip is standing, not a plant anybody could time a cue to.
                if (span.duration() >= s.length - dt) {
                    continue;
                }
                s.events.push_back({"plant." + track.joint, span.start});
                s.events.push_back({"release." + track.joint, span.end});
            }
        }
        std::stable_sort(s.events.begin(), s.events.end(),
                         [](const ClipEvent& a, const ClipEvent& b) { return a.seconds < b.seconds; });
        table.clips.push_back(std::move(s));
    }
    return table;
}

nlohmann::json clipSemanticsJson(const ClipSemanticsTable& table) {
    nlohmann::json clips = nlohmann::json::array();
    for (const ClipSemantics& c : table.clips) {
        nlohmann::json events = nlohmann::json::array();
        for (const ClipEvent& e : c.events) {
            events.push_back({{"name", e.name}, {"seconds", e.seconds}});
        }
        nlohmann::json committed = nlohmann::json::array();
        for (const auto& [from, to] : c.committed) {
            committed.push_back({from, to});
        }
        clips.push_back({{"clip", c.clip},
                         {"length", c.length},
                         {"loops", c.loops},
                         {"playback", c.loops ? "loop" : "once"},
                         {"loopGapSteps", c.loopGapSteps},
                         {"travels", c.travels},
                         {"rootTravel", {c.rootTravel.x, c.rootTravel.y, c.rootTravel.z}},
                         {"groundSpeed", c.groundSpeed},
                         {"ground", clipGroundName(c.ground)},
                         {"supportedFraction", c.supportedFraction},
                         {"startsSupported", c.startsSupported},
                         {"endsSupported", c.endsSupported},
                         {"peakHeight", c.peakHeight},
                         {"events", std::move(events)},
                         {"committed", std::move(committed)}});
    }
    return {{"feet", table.feet},
            {"groundDatum", table.groundDatum},
            {"restHeight", table.restHeight},
            {"warnings", table.warnings},
            {"clips", std::move(clips)}};
}

const ClipSemanticsTable& ClipSemanticsCache::get(const Skeleton& skeleton, const std::vector<AnimationClip>& clips) {
    std::call_once(once_, [&] { table_ = clipSemantics(skeleton, clips); });
    return table_;
}

} // namespace avgen::scene
