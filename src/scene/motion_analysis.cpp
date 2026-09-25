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

// The rest height of a skeleton: the tallest joint with everything at its bind transform. This is
// the body's own scale, and it is what root extent is measured against so that the same rule holds
// for a 1.66 m alien and a 1.79 m human without a number in metres anywhere.
float restHeightOf(const Skeleton& skeleton) {
    Pose pose;
    std::vector<glm::mat4> model;
    setRestPose(skeleton, pose);
    poseToModel(skeleton, pose, model);
    float lowest = 0.0f;
    float highest = 0.0f;
    bool first = true;
    for (const glm::mat4& m : model) {
        const float y = m[3].y;
        if (first) {
            lowest = y;
            highest = y;
            first = false;
        }
        lowest = std::min(lowest, y);
        highest = std::max(highest, y);
    }
    return std::max(highest - lowest, 1e-4f);
}

// The joint whose track answers "where did the body go": **the lowest-indexed joint this clip gives
// a translation channel to**. ADR-337 established and paid for this rule: on the alien, `rig` is an
// armature wrapper no clip animates and `root.x` is what moves, so measuring the parentless joint
// reports a stationary body for a clip that crosses the room.
int travelJointOf(const Skeleton& skeleton, const AnimationClip& clip) {
    int best = -1;
    for (const AnimationChannel& channel : clip.channels) {
        if (channel.path != AnimationPath::Translation) {
            continue;
        }
        const int j = static_cast<int>(channel.joint);
        if (best < 0 || j < best) {
            best = j;
        }
    }
    return best >= 0 ? best : rootJoint(skeleton);
}

// What the body did, read off its travel joint's track.
struct RootMotion {
    glm::vec3 net{0.0f};   // last minus first
    float path = 0.0f;     // distance actually covered, horizontally
    float extent = 0.0f;   // diagonal of the horizontal bounding box of the whole track
    bool travels = false;
};

// **Three readings of "did this clip travel", and only the third one works.** Two of the three look
// obviously right, which is why both shipped, so all three are recorded here.
//
//   * **Net displacement** -- last position minus first -- is what this file shipped with. It is
//     correct for a clip that goes one way and wrong for almost every motion-capture take ever
//     recorded, because a subject in a capture volume turns round and comes back. Measured on
//     100STYLE's `Neutral_FW`, **131 seconds of continuous forward walking**: net **0.508 m**,
//     which over the duration is 0.004 m/s. Against any sane threshold that is standing still, so
//     the largest shippable locomotion corpus in existence classified as in-place, file after file,
//     and the horizontal arm below never ran on the only data that needed it.
//   * **Path length** -- the distance actually covered -- fixes that half and breaks the other. An
//     in-place cycle's root does not sit still: it sways, bobs and leans, and the sway has a
//     length. Measured on this repository's own clips, which ADR-540 established are all authored
//     in place: `Running` **0.330 m/s**, `Fight_leg_kick_1` **0.535 m/s**. Both read as travelling,
//     and a travelling clip gets the horizontal contact test, which on an in-place clip finds the
//     swing foot and calls it the plant -- the exact failure recorded further down this file.
//   * **Extent against the body's own height** -- how far apart the two most distant places the
//     body stood are, over how tall the body is -- separates them, because it is the only one of
//     the three that asks the right question. An in-place cycle's root is *bounded*: it can sway
//     for an hour and never leave a box the size of the body. A travelling take's root is not.
//
// The threshold is one body height, and it is a ratio rather than a distance so that the alien and
// the human are judged by the same rule. Measured separation: this repository's in-place clips top
// out at a fraction of a body height and 100STYLE's walks run to many, so nothing sits near it.
RootMotion measureRoot(const Skeleton& skeleton, const AnimationClip& clip, int root, float rate) {
    RootMotion out;
    const float length = clip.length();
    if (root < 0 || length <= 0.0f || skeleton.joints.empty() ||
        static_cast<std::size_t>(root) >= skeleton.joints.size()) {
        return out;
    }
    Pose pose;
    std::vector<glm::mat4> model;
    const auto sampleRoot = [&](float t) {
        setRestPose(skeleton, pose);
        sampleClip(clip, std::min(t, clip.duration), pose);
        poseToModel(skeleton, pose, model);
        return glm::vec3(model[static_cast<std::size_t>(root)][3]);
    };
    const float hz = std::max(rate, 1.0f);
    // Rounded, not floored -- see `detectContacts` for the frame this otherwise drops.
    const auto steps =
        static_cast<std::size_t>(std::max(2.0f, std::floor(length * hz + 0.5f) + 1.0f));
    const glm::vec3 first = sampleRoot(clip.start);
    glm::vec3 previous = first;
    glm::vec3 last = first;
    glm::vec2 lo(first.x, first.z);
    glm::vec2 hi = lo;
    double path = 0.0;
    for (std::size_t i = 1; i < steps; ++i) {
        const glm::vec3 now = sampleRoot(clip.start + (static_cast<float>(i) / hz));
        const glm::vec3 d = now - previous;
        path += std::sqrt(static_cast<double>((d.x * d.x) + (d.z * d.z)));
        lo = glm::min(lo, glm::vec2(now.x, now.z));
        hi = glm::max(hi, glm::vec2(now.x, now.z));
        previous = now;
        last = now;
    }
    out.net = last - first;
    out.path = static_cast<float>(path);
    const glm::vec2 box = hi - lo;
    out.extent = std::sqrt((box.x * box.x) + (box.y * box.y));
    out.travels = out.extent > restHeightOf(skeleton);
    return out;
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
    const int root = travelJointOf(skeleton, clip);

    // Sample the clip once and keep every watched joint's ground-frame position per sample.
    Pose pose;
    std::vector<glm::mat4> model;
    std::vector<std::vector<glm::vec3>> positions(tracks.size(), std::vector<glm::vec3>(samples));
    for (std::size_t s = 0; s < samples; ++s) {
        const float t = clip.start + (static_cast<float>(s) / rate);
        setRestPose(skeleton, pose);
        sampleClip(clip, std::min(t, clip.duration), pose);
        poseToModel(skeleton, pose, model);
        glm::vec3 rootNow(0.0f);
        if (root >= 0) {
            rootNow = glm::vec3(model[static_cast<std::size_t>(root)][3]);
        }
        // **No root subtraction. Model space IS the ground, in both kinds of clip.**
        //
        // The first version of this removed the root's horizontal travel, on the reasoning that a
        // "ground frame" is what a planted foot is stationary in. That reasoning is wrong twice
        // over, and it took the first travelling content in this repository to show it:
        //
        //   * in a TRAVELLING clip a planted foot is stationary in the world, so subtracting the
        //     root's advance makes it move backwards at the travel speed -- and the horizontal
        //     contact test then rejects every genuine plant. Measured on a hand-built walk: 0 spans
        //     found where there are 3;
        //   * in an IN-PLACE clip there is no root travel to subtract, so the subtraction was a
        //     no-op and the code path had never actually run.
        //
        // So it was harmless on all existing content and wrong on all future content, which is the
        // worst combination: a branch that cannot be caught until the data arrives. Model space is
        // the right frame for both -- a travelling clip's world is the ground, and an in-place
        // clip's planted foot moves backwards in it either way, which is why ADR-546's vertical
        // test exists.
        for (std::size_t k = 0; k < tracks.size(); ++k) {
            if (tracks[k].jointIndex < 0) {
                continue;
            }
            positions[k][s] = glm::vec3(model[static_cast<std::size_t>(tracks[k].jointIndex)][3]);
        }
    }

    const float dt = 1.0f / rate;
    // Does this clip's body actually go anywhere? `measureRoot` is the single place that decides,
    // and its comment records the two readings that got this wrong before the corpus arrived.
    const bool horizontalMatters = measureRoot(skeleton, clip, root, rate).travels;
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

        // Foot sliding: how far this joint moves horizontally *while it is in contact*. Measured
        // from the samples already taken, per run, as the diagonal of the run's horizontal bounding
        // box -- which is the distance between the two most separated places the "planted" foot
        // was, rather than a start-to-end reading that a foot sliding out and back would hide.
        //
        // Runs, not spans, because a wrapped span is two runs and its slide is the span of both.
        double slideSum = 0.0;
        std::size_t slideCount = 0;
        const auto slideOf = [&](std::size_t begin, std::size_t end) {
            if (end <= begin) {
                return 0.0f;
            }
            glm::vec2 lo(p[begin].x, p[begin].z);
            glm::vec2 hi = lo;
            for (std::size_t i = begin; i < end; ++i) {
                lo = glm::min(lo, glm::vec2(p[i].x, p[i].z));
                hi = glm::max(hi, glm::vec2(p[i].x, p[i].z));
            }
            const glm::vec2 box = hi - lo;
            return std::sqrt((box.x * box.x) + (box.y * box.y));
        };
        for (const Run& run : runs) {
            const float slide = slideOf(run.begin, run.end);
            track.worstSlide = std::max(track.worstSlide, slide);
            slideSum += slide;
            ++slideCount;
        }
        track.meanSlide = slideCount > 0 ? static_cast<float>(slideSum / static_cast<double>(slideCount)) : 0.0f;
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

float PhaseTrack::timeAt(float target) const {
    if (phase.empty()) {
        return 0.0f;
    }
    const float rate = std::max(sampleRate, 1.0f);
    float want = target - std::floor(target);
    // Walk the grid and take the first interval that brackets the target, following the same
    // shortest-way-round rule `at()` uses so the two are inverses of one another across the wrap.
    for (std::size_t i = 0; i + 1 < phase.size(); ++i) {
        const float a = phase[i];
        float d = phase[i + 1] - a;
        if (d > 0.5f) {
            d -= 1.0f;
        } else if (d < -0.5f) {
            d += 1.0f;
        }
        if (std::fabs(d) < 1e-9f) {
            continue;
        }
        // Where `want` sits inside this interval, in the interval's own direction.
        float delta = want - a;
        if (delta > 0.5f) {
            delta -= 1.0f;
        } else if (delta < -0.5f) {
            delta += 1.0f;
        }
        const float u = delta / d;
        if (u >= 0.0f && u <= 1.0f) {
            return (static_cast<float>(i) + u) / rate;
        }
    }
    // No interval contained it, which happens on a track that does not cover the whole range --
    // an acyclic one asked for a phase past its end. The nearest sample is the honest answer.
    std::size_t best = 0;
    float bestErr = 2.0f;
    for (std::size_t i = 0; i < phase.size(); ++i) {
        float e = std::fabs(phase[i] - want);
        e = std::min(e, 1.0f - e);
        if (e < bestErr) {
            bestErr = e;
            best = i;
        }
    }
    return static_cast<float>(best) / rate;
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

    // The same rule the contact detector uses, from the same place -- two answers to "where did the
    // body go" is how ADR-260 started.
    const int root = travelJointOf(skeleton, clip);
    if (root >= 0 && out.length > 0.0f) {
        const RootMotion motion = measureRoot(skeleton, clip, root, settings.sampleRate);
        out.rootTravel = motion.net;
        out.rootPathLength = motion.path;
        out.rootExtent = motion.extent;
        out.restHeight = restHeightOf(skeleton);
        out.travels = motion.travels;
        const float ratio = out.restHeight > 0.0f ? out.rootExtent / out.restHeight : 0.0f;
        out.travelAmbiguous = ratio > 0.7f && ratio < 1.4f;
        // Reported, not thresholded. `travels` is the switch, and it is not this number.
        out.groundSpeed = out.rootPathLength / out.length;
    }
    return out;
}


LoopClosure measureLoopClosure(const Skeleton& skeleton, const AnimationClip& clip, float rate) {
    LoopClosure out;
    const float length = clip.length();
    const int root = travelJointOf(skeleton, clip);
    if (root < 0 || length <= 0.0f || skeleton.joints.empty()) {
        return out;
    }
    const auto r = static_cast<std::size_t>(root);
    Pose pose;
    std::vector<glm::mat4> model;
    // Every joint relative to the travel joint's ground point: the body's shape and its height,
    // with its horizontal travel removed, so a walk that has moved on still matches its start.
    const auto shape = [&](float t, std::vector<glm::vec3>& into) {
        setRestPose(skeleton, pose);
        sampleClip(clip, std::min(t, clip.duration), pose);
        poseToModel(skeleton, pose, model);
        const glm::vec3 base(model[r][3].x, 0.0f, model[r][3].z);
        into.resize(model.size());
        for (std::size_t j = 0; j < model.size(); ++j) {
            into[j] = glm::vec3(model[j][3]) - base;
        }
    };
    const auto distance = [](const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
        float worst = 0.0f;
        for (std::size_t j = 0; j < a.size() && j < b.size(); ++j) {
            worst = std::max(worst, glm::length(a[j] - b[j]));
        }
        return worst;
    };
    const float hz = std::max(rate, 1.0f);
    const auto steps = static_cast<std::size_t>(std::max(2.0f, std::floor(length * hz + 0.5f) + 1.0f));
    std::vector<glm::vec3> first;
    std::vector<glm::vec3> previous;
    std::vector<glm::vec3> now;
    shape(clip.start, first);
    previous = first;
    std::vector<float> step;
    step.reserve(steps);
    for (std::size_t i = 1; i < steps; ++i) {
        shape(clip.start + (static_cast<float>(i) / hz), now);
        step.push_back(distance(previous, now));
        std::swap(previous, now);
    }
    // `previous` now holds the last frame.
    out.gap = distance(previous, first);
    if (!step.empty()) {
        std::vector<float> sorted = step;
        std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2),
                         sorted.end());
        out.typicalStep = sorted[sorted.size() / 2];
    }
    out.loops = out.gap <= kLoopClosureSteps * std::max(out.typicalStep, 1e-5f);
    return out;
}

float skeletonRestHeight(const Skeleton& skeleton) {
    return restHeightOf(skeleton);
}

int clipTravelJoint(const Skeleton& skeleton, const AnimationClip& clip) {
    return travelJointOf(skeleton, clip);
}

} // namespace avgen::scene
