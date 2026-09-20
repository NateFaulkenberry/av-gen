#include "scene/motion_analysis.hpp"

#include "scene/animation.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::scene {
namespace {

// The root of a skeleton, for the ground frame: the first joint with no parent. Not `joints[0]`
// by assumption -- it happens to be joint 0 on every rig here, and asserting it costs nothing.
int rootJoint(const Skeleton& skeleton) {
    for (std::size_t i = 0; i < skeleton.joints.size(); ++i) {
        if (skeleton.joints[i].parent < 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// A run of `true` in a boolean signal, after bridging short gaps and dropping short runs. Returned
// as [start, end) sample indices.
struct Run {
    std::size_t begin = 0;
    std::size_t end = 0;
};

std::vector<Run> runsOf(std::vector<char>& flags, std::uint32_t bridge, std::uint32_t minRun) {
    // Bridge first, then drop: the other order deletes a plant that a single bad sample split in
    // two, and then there is nothing left to bridge.
    if (bridge > 0) {
        std::size_t i = 0;
        while (i < flags.size()) {
            if (flags[i] != 0) {
                ++i;
                continue;
            }
            std::size_t j = i;
            while (j < flags.size() && flags[j] == 0) {
                ++j;
            }
            const bool interior = i > 0 && j < flags.size();
            if (interior && (j - i) <= bridge) {
                for (std::size_t k = i; k < j; ++k) {
                    flags[k] = 1;
                }
            }
            i = j;
        }
    }
    std::vector<Run> runs;
    std::size_t i = 0;
    while (i < flags.size()) {
        if (flags[i] == 0) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < flags.size() && flags[j] != 0) {
            ++j;
        }
        if ((j - i) >= std::max<std::size_t>(minRun, 1)) {
            runs.push_back({i, j});
        }
        i = j;
    }
    return runs;
}

} // namespace

const char* contactKindName(ContactKind kind) {
    switch (kind) {
    case ContactKind::Foot: return "foot";
    case ContactKind::Hand: return "hand";
    case ContactKind::Body: return "body";
    case ContactKind::Custom: return "custom";
    }
    return "?";
}

bool contactKindFromName(std::string_view name, ContactKind& out) {
    if (name == "foot") { out = ContactKind::Foot; return true; }
    if (name == "hand") { out = ContactKind::Hand; return true; }
    if (name == "body") { out = ContactKind::Body; return true; }
    if (name == "custom") { out = ContactKind::Custom; return true; }
    return false;
}

const char* phaseLandmarkKindName(PhaseLandmarkKind kind) {
    return kind == PhaseLandmarkKind::Plant ? "plant" : "release";
}

std::vector<ContactTrack> detectContacts(const Skeleton& skeleton, const AnimationClip& clip,
                                         std::span<const ContactJoint> joints,
                                         const ContactSettings& settings) {
    std::vector<ContactTrack> tracks;
    tracks.reserve(joints.size());
    for (const ContactJoint& j : joints) {
        ContactTrack track;
        track.joint = j.joint;
        track.kind = j.kind;
        track.jointIndex = skeleton.find(j.joint);
        tracks.push_back(std::move(track));
    }
    const float length = clip.length();
    const float rate = std::max(settings.sampleRate, 1.0f);
    // Rounded, not floored. `length * rate` for a clip whose last key is at 19/30 s comes out as
    // 18.999998 in float, and flooring it silently drops the final key -- which on a looping clip
    // is the sample that closes the wrap, so the tail of a stance comes back a frame short.
    const auto samples =
        static_cast<std::size_t>(std::max(2.0f, std::floor(length * rate + 0.5f) + 1.0f));
    if (length <= 0.0f || skeleton.joints.empty()) {
        return tracks;
    }
    const int root = rootJoint(skeleton);

    // Sample the clip once and keep every watched joint's ground-frame position per sample.
    Pose pose;
    std::vector<glm::mat4> model;
    std::vector<std::vector<glm::vec3>> positions(tracks.size(), std::vector<glm::vec3>(samples));
    glm::vec3 rootFirst(0.0f);
    for (std::size_t s = 0; s < samples; ++s) {
        const float t = clip.start + (static_cast<float>(s) / rate);
        setRestPose(skeleton, pose);
        sampleClip(clip, std::min(t, clip.duration), pose);
        poseToModel(skeleton, pose, model);
        glm::vec3 rootNow(0.0f);
        if (root >= 0) {
            rootNow = glm::vec3(model[static_cast<std::size_t>(root)][3]);
        }
        if (s == 0) {
            rootFirst = rootNow;
        }
        // The ground frame removes the root's HORIZONTAL travel only. Vertical root motion is the
        // body bobbing or landing, and a foot that stays put while the hips drop is still planted.
        const glm::vec3 shift(rootNow.x - rootFirst.x, 0.0f, rootNow.z - rootFirst.z);
        for (std::size_t k = 0; k < tracks.size(); ++k) {
            if (tracks[k].jointIndex < 0) {
                continue;
            }
            const glm::vec3 p(model[static_cast<std::size_t>(tracks[k].jointIndex)][3]);
            positions[k][s] = p - shift;
        }
    }

    const float dt = 1.0f / rate;
    // Does this clip's root actually travel? `rootFirst` is the first sample and the loop above
    // left `model` holding the last, so the answer is one subtraction. The threshold is a *speed*
    // so that a long clip and a short one are judged the same way: 0.05 model units per second on
    // a 1.66 m character is about a knuckle's width per second, which is drift, not travel.
    bool horizontalMatters = false;
    if (root >= 0) {
        const glm::vec3 last(model[static_cast<std::size_t>(root)][3]);
        const float planar = std::sqrt((last.x - rootFirst.x) * (last.x - rootFirst.x) +
                                       (last.z - rootFirst.z) * (last.z - rootFirst.z));
        horizontalMatters = (planar / length) > 0.05f;
    }
    for (std::size_t k = 0; k < tracks.size(); ++k) {
        ContactTrack& track = tracks[k];
        if (track.jointIndex < 0) {
            continue;
        }
        const std::vector<glm::vec3>& p = positions[k];
        track.lowest = p.front().y;
        float highest = p.front().y;
        for (const glm::vec3& v : p) {
            track.lowest = std::min(track.lowest, v.y);
            highest = std::max(highest, v.y);
        }
        // The height band, as a fraction of this joint's own vertical travel in this clip. See
        // `ContactSettings::heightFraction` for why it is not an absolute distance.
        const float band =
            std::max(settings.heightFloor, settings.heightFraction * (highest - track.lowest));
        // Planted = low, AND not changing height. Speed by central difference where there is one,
        // so the first and last samples are not systematically different from the rest.
        //
        // **The horizontal component is deliberately not in this test, and the first version of
        // this file had it.** "Planted means stationary" is true on the ground and false in an
        // in-place clip, which is what every locomotion clip in this repository is (ADR-540). In an
        // in-place cycle the body does not travel, so the *stance foot* is the thing that moves:
        // it sweeps backwards under the hips at exactly the authored stride speed while the swing
        // foot comes forward. Testing horizontal speed therefore finds the swing and calls it the
        // plant, and the measurement said so -- `Walking` came back with **zero** left contacts and
        // a 6% duty cycle on the right, while `Idle` and `Flying_jet` came back at 100% on both.
        //
        // Height and vertical speed have no such problem: a stance foot is at its lowest and stays
        // there whether or not the clip travels, and a swing foot is either rising, falling, or at
        // an apex the height test excludes. So the vertical pair is the test for both kinds of clip
        // and the horizontal one is used only where it means something -- see `horizontalMatters`.
        std::vector<char> flags(samples, 0);
        for (std::size_t s = 0; s < samples; ++s) {
            const std::size_t a = s == 0 ? 0 : s - 1;
            const std::size_t b = s + 1 < samples ? s + 1 : samples - 1;
            const float span = static_cast<float>(b - a) * dt;
            const float rise = span > 0.0f ? std::fabs(p[b].y - p[a].y) / span : 0.0f;
            bool still = rise <= settings.speedThreshold;
            if (horizontalMatters && still) {
                // The clip genuinely travels, so the ground frame is real and a planted foot is
                // stationary in it -- both components, as the textbook says.
                const glm::vec3 d = p[b] - p[a];
                const float flat = span > 0.0f ? std::sqrt(d.x * d.x + d.z * d.z) / span : 0.0f;
                still = flat <= settings.speedThreshold;
            }
            const bool low = (p[s].y - track.lowest) <= band;
            flags[s] = (still && low) ? 1 : 0;
        }
        std::size_t contactSamples = 0;
        std::vector<Run> runs = runsOf(flags, settings.bridgeSamples, settings.minSamples);
        for (const Run& run : runs) {
            contactSamples += run.end - run.begin;
        }
        // A looping clip's contacts wrap. If one run starts at the first sample and another ends at
        // the last, they are the two halves of one stance seen through a cut, and reporting them as
        // two contacts makes a one-cycle clip look like a two-cycle one.
        bool merged = false;
        if (settings.looping && runs.size() >= 2 && runs.front().begin == 0 &&
            runs.back().end == samples) {
            ContactSpan span;
            span.start = static_cast<float>(runs.back().begin) * dt;
            span.end = static_cast<float>(runs.front().end - 1) * dt;
            span.clipLength = length;
            track.spans.push_back(span);
            for (std::size_t r = 1; r + 1 < runs.size(); ++r) {
                ContactSpan mid;
                mid.start = static_cast<float>(runs[r].begin) * dt;
                mid.end = static_cast<float>(runs[r].end - 1) * dt;
                mid.clipLength = length;
                track.spans.push_back(mid);
            }
            std::sort(track.spans.begin(), track.spans.end(),
                      [](const ContactSpan& a, const ContactSpan& b) { return a.start < b.start; });
            merged = true;
        }
        if (!merged) {
            for (const Run& run : runs) {
                ContactSpan span;
                span.start = static_cast<float>(run.begin) * dt;
                // The run's last sample is `end - 1`; the contact lasts until the next sample begins.
                span.end = static_cast<float>(run.end - 1) * dt;
                span.clipLength = length;
                track.spans.push_back(span);
            }
        }
        track.dutyCycle = static_cast<float>(contactSamples) / static_cast<float>(samples);
    }
    return tracks;
}

float PhaseTrack::at(float clipSeconds) const {
    if (phase.empty()) {
        return 0.0f;
    }
    const float rate = std::max(sampleRate, 1.0f);
    const float x = std::clamp(clipSeconds * rate, 0.0f, static_cast<float>(phase.size() - 1));
    const auto lo = static_cast<std::size_t>(std::floor(x));
    const std::size_t hi = std::min(lo + 1, phase.size() - 1);
    const float a = x - static_cast<float>(lo);
    // Phase wraps, so interpolating 0.98 -> 0.02 the short way is the only correct reading.
    float d = phase[hi] - phase[lo];
    if (d > 0.5f) {
        d -= 1.0f;
    } else if (d < -0.5f) {
        d += 1.0f;
    }
    float out = phase[lo] + d * a;
    out -= std::floor(out);
    return out;
}

PhaseTrack extractPhase(std::span<const ContactTrack> tracks, int referenceTrack, float clipLength,
                        const ContactSettings& settings) {
    PhaseTrack out;
    const float rate = std::max(settings.sampleRate, 1.0f);
    out.sampleRate = rate;
    const auto samples =
        static_cast<std::size_t>(std::max(2.0f, std::floor(clipLength * rate + 0.5f) + 1.0f));
    out.phase.assign(samples, 0.0f);
    if (clipLength <= 0.0f) {
        return out;
    }

    // Acyclic fallback: phase is normalised time. Every clip gets a phase, including a one-shot --
    // `Landing` is exactly the clip a transition most wants one for, and a system that only spoke
    // about cycles would have nothing to say about it.
    const auto fillLinear = [&]() {
        for (std::size_t s = 0; s < samples; ++s) {
            out.phase[s] = std::min(0.999999f, static_cast<float>(s) / static_cast<float>(samples - 1));
        }
    };

    if (referenceTrack < 0 || static_cast<std::size_t>(referenceTrack) >= tracks.size()) {
        fillLinear();
        return out;
    }
    const ContactTrack& ref = tracks[static_cast<std::size_t>(referenceTrack)];
    if (ref.spans.empty()) {
        fillLinear();
        return out;
    }
    if (ref.spans.size() == 1) {
        // One plant. On a LOOPING clip that is a complete cycle -- the foot plants once per lap,
        // which is what a one-cycle walk is -- so phase runs 0..1 from that plant round to itself.
        // On a one-shot it is not a cycle at all and the linear fallback is the honest answer.
        if (!settings.looping || clipLength <= 1e-4f) {
            fillLinear();
            return out;
        }
        out.cyclic = true;
        out.cycleSeconds = clipLength;
        out.cycleVariance = 0.0f;
        const float anchor = ref.spans.front().start;
        for (std::size_t s = 0; s < samples; ++s) {
            const float t = static_cast<float>(s) / rate;
            float ph = (t - anchor) / clipLength;
            ph -= std::floor(ph);
            out.phase[s] = ph;
        }
        for (std::size_t k = 0; k < tracks.size(); ++k) {
            for (const ContactSpan& span : tracks[k].spans) {
                PhaseLandmark plant;
                plant.time = span.start;
                plant.contactTrack = static_cast<int>(k);
                plant.kind = PhaseLandmarkKind::Plant;
                plant.phase = out.at(span.start);
                out.landmarks.push_back(plant);
                PhaseLandmark release = plant;
                release.time = span.end;
                release.kind = PhaseLandmarkKind::Release;
                release.phase = out.at(span.end);
                out.landmarks.push_back(release);
            }
        }
        std::sort(out.landmarks.begin(), out.landmarks.end(),
                  [](const PhaseLandmark& a, const PhaseLandmark& b) { return a.time < b.time; });
        return out;
    }

    // Anchors: the instant each plant begins. Phase is 0 at every one of them and rises linearly to
    // 1 at the next, so it is monotone within a cycle and wraps exactly on contact.
    std::vector<float> anchors;
    anchors.reserve(ref.spans.size());
    for (const ContactSpan& span : ref.spans) {
        anchors.push_back(span.start);
    }
    out.cyclic = true;

    float total = 0.0f;
    for (std::size_t i = 1; i < anchors.size(); ++i) {
        total += anchors[i] - anchors[i - 1];
    }
    out.cycleSeconds = total / static_cast<float>(anchors.size() - 1);
    if (out.cycleSeconds > 1e-4f) {
        float worst = 0.0f;
        for (std::size_t i = 1; i < anchors.size(); ++i) {
            worst = std::max(worst, std::fabs((anchors[i] - anchors[i - 1]) - out.cycleSeconds));
        }
        out.cycleVariance = worst / out.cycleSeconds;
    }

    for (std::size_t s = 0; s < samples; ++s) {
        const float t = static_cast<float>(s) / rate;
        if (t < anchors.front()) {
            // Before the first plant: run the cycle backwards from it, so the head of a clip has a
            // phase continuous with the body of it rather than a flat zero.
            const float back = (anchors.front() - t) / std::max(out.cycleSeconds, 1e-4f);
            out.phase[s] = 1.0f - std::fmod(back, 1.0f);
            if (out.phase[s] >= 1.0f) {
                out.phase[s] = 0.0f;
            }
            continue;
        }
        if (t >= anchors.back()) {
            const float on = (t - anchors.back()) / std::max(out.cycleSeconds, 1e-4f);
            out.phase[s] = std::fmod(on, 1.0f);
            continue;
        }
        const auto upper = std::upper_bound(anchors.begin(), anchors.end(), t);
        const std::size_t hi = static_cast<std::size_t>(upper - anchors.begin());
        const std::size_t lo = hi - 1;
        const float span = anchors[hi] - anchors[lo];
        out.phase[s] = span > 1e-6f ? (t - anchors[lo]) / span : 0.0f;
    }

    for (std::size_t k = 0; k < tracks.size(); ++k) {
        for (const ContactSpan& span : tracks[k].spans) {
            PhaseLandmark plant;
            plant.time = span.start;
            plant.contactTrack = static_cast<int>(k);
            plant.kind = PhaseLandmarkKind::Plant;
            plant.phase = out.at(span.start);
            out.landmarks.push_back(plant);
            PhaseLandmark release = plant;
            release.time = span.end;
            release.kind = PhaseLandmarkKind::Release;
            release.phase = out.at(span.end);
            out.landmarks.push_back(release);
        }
    }
    std::sort(out.landmarks.begin(), out.landmarks.end(),
              [](const PhaseLandmark& a, const PhaseLandmark& b) { return a.time < b.time; });
    return out;
}

ClipAnalysis analyseClip(const Skeleton& skeleton, const AnimationClip& clip,
                         std::span<const ContactJoint> joints, int referenceJoint,
                         const ContactSettings& settings) {
    ClipAnalysis out;
    out.clip = clip.name;
    out.length = clip.length();
    out.contacts = detectContacts(skeleton, clip, joints, settings);
    out.phase = extractPhase(out.contacts, referenceJoint, out.length, settings);

    const int root = rootJoint(skeleton);
    if (root >= 0 && out.length > 0.0f) {
        Pose pose;
        std::vector<glm::mat4> model;
        const auto sampleRoot = [&](float t) {
            setRestPose(skeleton, pose);
            sampleClip(clip, t, pose);
            poseToModel(skeleton, pose, model);
            return glm::vec3(model[static_cast<std::size_t>(root)][3]);
        };
        out.rootTravel = sampleRoot(clip.duration) - sampleRoot(clip.start);
        const float planar = std::sqrt(out.rootTravel.x * out.rootTravel.x + out.rootTravel.z * out.rootTravel.z);
        out.groundSpeed = planar / out.length;
    }
    return out;
}

} // namespace avgen::scene
