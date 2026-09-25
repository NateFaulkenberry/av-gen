#include "scene/animation.hpp"

#include "scene/scene.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>

namespace avgen::scene {

namespace {

// The part of a clip name after the last '|'. Exporters prefix clips with the rig they came off
// ("Alien_Low_Green|Walk"); the file keeps its own name and a scene asks for "Walk".
std::string_view shortName(std::string_view name) {
    const auto bar = name.rfind('|');
    return bar == std::string_view::npos ? name : name.substr(bar + 1);
}

// Index of the last key at or before `time`, given at least two keys.
std::size_t keyBefore(const std::vector<float>& times, float time) {
    const auto it = std::upper_bound(times.begin(), times.end(), time);
    if (it == times.begin()) {
        return 0;
    }
    const auto index = static_cast<std::size_t>(std::distance(times.begin(), it)) - 1;
    return std::min(index, times.size() - 2);
}

glm::vec4 cubicSpline(const glm::vec4& v0, const glm::vec4& out0, const glm::vec4& in1, const glm::vec4& v1,
                      float dt, float t) {
    // glTF 2.0 animation, "Cubic Spline Interpolation": the Hermite basis with the tangents scaled
    // by the key interval.
    const float t2 = t * t;
    const float t3 = t2 * t;
    return (2.0f * t3 - 3.0f * t2 + 1.0f) * v0 + dt * (t3 - 2.0f * t2 + t) * out0 +
           (-2.0f * t3 + 3.0f * t2) * v1 + dt * (t3 - t2) * in1;
}

void applyValue(Transform& transform, AnimationPath path, const glm::vec4& value) {
    switch (path) {
    case AnimationPath::Translation:
        transform.position = glm::vec3(value);
        break;
    case AnimationPath::Scale:
        transform.scale = glm::vec3(value);
        break;
    case AnimationPath::Rotation:
        transform.rotation = glm::normalize(glm::quat(value.w, value.x, value.y, value.z));
        break;
    }
}

} // namespace

const char* animationPathName(AnimationPath path) {
    switch (path) {
    case AnimationPath::Translation:
        return "translation";
    case AnimationPath::Rotation:
        return "rotation";
    case AnimationPath::Scale:
        return "scale";
    }
    return "translation";
}

const char* interpolationName(Interpolation interpolation) {
    switch (interpolation) {
    case Interpolation::Linear:
        return "LINEAR";
    case Interpolation::Step:
        return "STEP";
    case Interpolation::CubicSpline:
        return "CUBICSPLINE";
    }
    return "LINEAR";
}

bool AnimationChannel::valid() const {
    if (times.empty()) {
        return false;
    }
    const std::size_t expected = interpolation == Interpolation::CubicSpline ? times.size() * 3 : times.size();
    if (values.size() != expected) {
        return false;
    }
    return std::is_sorted(times.begin(), times.end());
}

bool AnimationClip::valid() const {
    return !channels.empty() && duration >= 0.0f &&
           std::all_of(channels.begin(), channels.end(), [](const AnimationChannel& c) { return c.valid(); });
}

float wrapTime(float time, float duration) {
    if (duration <= 0.0f) {
        return 0.0f;
    }
    float t = std::fmod(time, duration);
    if (t < 0.0f) {
        t += duration; // a clip played backwards wraps to the end, not to a negative time
    }
    return t;
}

void sampleClip(const AnimationClip& clip, float time, Pose& pose) {
    // Clamped to the range the keys actually cover. `start` is not always zero (see the header),
    // and clamping to zero instead put the sampler in a stretch before the first key where every
    // channel holds its first value -- a pose the file does not contain.
    const float t = std::clamp(time, std::min(clip.start, clip.duration), clip.duration);
    for (const AnimationChannel& channel : clip.channels) {
        if (channel.joint >= pose.local.size() || channel.times.empty() || channel.values.empty()) {
            continue;
        }
        Transform& target = pose.local[channel.joint];
        const std::size_t keys = channel.times.size();
        const bool cubic = channel.interpolation == Interpolation::CubicSpline;
        if (keys == 1 || t <= channel.times.front()) {
            applyValue(target, channel.path, channel.values[cubic ? 1 : 0]);
            continue;
        }
        if (t >= channel.times.back()) {
            const std::size_t last = keys - 1;
            applyValue(target, channel.path, channel.values[cubic ? last * 3 + 1 : last]);
            continue;
        }
        const std::size_t k = keyBefore(channel.times, t);
        const float t0 = channel.times[k];
        const float t1 = channel.times[k + 1];
        const float span = t1 - t0;
        const float u = span > 0.0f ? (t - t0) / span : 0.0f;
        switch (channel.interpolation) {
        case Interpolation::Step:
            applyValue(target, channel.path, channel.values[k]);
            break;
        case Interpolation::Linear: {
            const glm::vec4& a = channel.values[k];
            const glm::vec4& b = channel.values[k + 1];
            if (channel.path == AnimationPath::Rotation) {
                // Quaternions slerp, and along the short arc: glTF says so, and the alternative is
                // a joint that swings the wrong way round between two adjacent keys.
                glm::quat qa(a.w, a.x, a.y, a.z);
                glm::quat qb(b.w, b.x, b.y, b.z);
                if (glm::dot(qa, qb) < 0.0f) {
                    qb = -qb;
                }
                target.rotation = glm::normalize(glm::slerp(qa, qb, u));
            } else {
                applyValue(target, channel.path, glm::mix(a, b, u));
            }
            break;
        }
        case Interpolation::CubicSpline: {
            const glm::vec4& v0 = channel.values[k * 3 + 1];
            const glm::vec4& out0 = channel.values[k * 3 + 2];
            const glm::vec4& in1 = channel.values[(k + 1) * 3];
            const glm::vec4& v1 = channel.values[(k + 1) * 3 + 1];
            applyValue(target, channel.path, cubicSpline(v0, out0, in1, v1, span, u));
            break;
        }
        }
    }
}

// ---- AnimationPlayer ---------------------------------------------------------------------------

void AnimationPlayer::addState(AnimationState state) {
    const int existing = stateIndex(state.name);
    if (existing >= 0) {
        states_[static_cast<std::size_t>(existing)] = std::move(state);
        return;
    }
    states_.push_back(std::move(state));
    if (current_.state < 0) {
        current_.state = static_cast<int>(states_.size()) - 1;
        current_.speed = states_.back().speed;
        current_.start = 0.0;
    }
}

void AnimationPlayer::addTransition(AnimationTransition transition) {
    transitions_.push_back(std::move(transition));
}

void AnimationPlayer::clear() {
    states_.clear();
    transitions_.clear();
    current_ = Playing{};
    previous_ = Playing{};
    blendStart_ = 0.0;
    blendDuration_ = 0.0f;
}

const AnimationState* AnimationPlayer::findState(std::string_view name) const {
    const int index = stateIndex(name);
    return index < 0 ? nullptr : &states_[static_cast<std::size_t>(index)];
}

int AnimationPlayer::stateIndex(std::string_view name) const {
    for (std::size_t i = 0; i < states_.size(); ++i) {
        if (states_[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

float AnimationPlayer::blendTimeFor(std::string_view from, std::string_view to) const {
    const AnimationTransition* wildcard = nullptr;
    for (const AnimationTransition& t : transitions_) {
        if (t.to != to) {
            continue;
        }
        if (t.from == from) {
            return std::max(0.0f, t.blend);
        }
        if (t.from.empty() && wildcard == nullptr) {
            wildcard = &t;
        }
    }
    if (wildcard != nullptr) {
        return std::max(0.0f, wildcard->blend);
    }
    const AnimationState* target = findState(to);
    return target != nullptr ? std::max(0.0f, target->blendIn) : 0.0f;
}

bool AnimationPlayer::play(std::string_view name, double now) {
    return play(name, now, blendTimeFor(currentState(), name));
}

bool AnimationPlayer::play(std::string_view name, double now, float blendSeconds) {
    const int index = stateIndex(name);
    if (index < 0) {
        return false;
    }
    if (current_.state == index) {
        return true; // already playing: do not restart the clip, so a behaviour may call every frame
    }
    // A change made while a cross-fade is still running takes the *outgoing* slot for whatever was
    // current, and drops the one it was fading from. Three-way fades buy nothing here: the third
    // pose is already mostly gone, and keeping it would make the pose depend on how many changes
    // happened rather than on the timeline.
    previous_ = current_;
    current_.state = index;
    current_.start = now;
    current_.speed = states_[static_cast<std::size_t>(index)].speed;
    current_.loop = -1; // ADR-821: a new play starts from the state's own looping
    blendStart_ = now;
    blendDuration_ = previous_.state < 0 ? 0.0f : std::max(0.0f, blendSeconds);
    return true;
}

bool AnimationPlayer::play(std::string_view name, double now, float blendSeconds, const PhaseMatch& match) {
    const int index = stateIndex(name);
    if (index < 0 || current_.state == index) {
        return play(name, now, blendSeconds); // no change, or no such state: the plain path answers
    }
    const AnimationState& target = states_[static_cast<std::size_t>(index)];
    // The outgoing state, captured before `play` overwrites it.
    const Playing outgoing = current_;
    if (!play(name, now, blendSeconds)) {
        return false;
    }
    if (!target.matchPhase || !match.usable() || outgoing.state < 0) {
        return true;
    }
    const std::vector<AnimationClip>& clips = *match.clips;
    const std::vector<PhaseTrack>& phases = *match.phases;
    const auto phaseFor = [&](int state) -> const PhaseTrack* {
        if (state < 0) {
            return nullptr;
        }
        const std::size_t clip = states_[static_cast<std::size_t>(state)].clip;
        if (clip >= phases.size() || phases[clip].empty()) {
            return nullptr;
        }
        return &phases[clip];
    };
    const PhaseTrack* from = phaseFor(outgoing.state);
    const PhaseTrack* to = phaseFor(index);
    if (from == nullptr || to == nullptr) {
        return true; // one of them was never analysed; frame zero is the honest fallback
    }
    // Where the outgoing clip is in its own cycle right now, and the instant in the incoming clip
    // that sits at the same place in its cycle.
    const float want = from->at(localTime(outgoing, clips, now));
    const float land = to->timeAt(want);
    // Rebase the incoming clock so that `now` lands on `land` rather than on the clip's start.
    // Speed divides because `localTime` multiplies by it: the clock is in timeline seconds and the
    // landing point is in clip seconds.
    const float speed = current_.speed != 0.0f ? current_.speed : 1.0f;
    current_.start = now - static_cast<double>(land) / static_cast<double>(speed);
    return true;
}

void AnimationPlayer::restart(double now) {
    current_.start = now;
}

void AnimationPlayer::setSpeed(float speed, double now) {
    if (current_.state < 0) {
        return;
    }
    // Rebase so the local clip time is continuous: without this a speed change is a jump cut.
    const double elapsed = (now - current_.start) * static_cast<double>(current_.speed);
    current_.speed = speed;
    current_.start = speed != 0.0f ? now - elapsed / static_cast<double>(speed) : now;
}

std::string_view AnimationPlayer::currentState() const {
    return current_.state >= 0 ? std::string_view(states_[static_cast<std::size_t>(current_.state)].name)
                               : std::string_view();
}

std::string_view AnimationPlayer::fadingState() const {
    if (previous_.state < 0 || blendDuration_ <= 0.0f) {
        return {};
    }
    return states_[static_cast<std::size_t>(previous_.state)].name;
}

float AnimationPlayer::blendWeight(double now) const {
    if (previous_.state < 0 || blendDuration_ <= 0.0f) {
        return 1.0f;
    }
    const auto w = static_cast<float>((now - blendStart_) / static_cast<double>(blendDuration_));
    return std::clamp(w, 0.0f, 1.0f);
}

float AnimationPlayer::stateTime(double now) const {
    if (current_.state < 0) {
        return 0.0f;
    }
    return static_cast<float>((now - current_.start) * static_cast<double>(current_.speed));
}

float AnimationPlayer::stateTime(const std::vector<AnimationClip>& clips, double now) const {
    return localTime(current_, clips, now);
}

bool AnimationPlayer::finished(const std::vector<AnimationClip>& clips, double now) const {
    if (current_.state < 0) {
        return false;
    }
    const AnimationState& state = states_[static_cast<std::size_t>(current_.state)];
    if (looping(current_)) {
        return false;
    }
    // Against the clip's playable length, not its last key time: a one-shot whose keys start at
    // 1/30 s is over 1/30 s sooner than its last key time says.
    const float length = state.clip < clips.size() ? clips[state.clip].length() : 0.0f;
    return stateTime(now) >= length;
}

float AnimationPlayer::localTime(const Playing& playing, const std::vector<AnimationClip>& clips,
                                 double now) const {
    if (playing.state < 0) {
        return 0.0f;
    }
    const AnimationState& state = states_[static_cast<std::size_t>(playing.state)];
    const AnimationClip* clip = state.clip < clips.size() ? &clips[state.clip] : nullptr;
    // A state's local clock starts at zero; the *clip* it plays starts wherever its keys do. The
    // two are not the same number, and treating them as one is what made every loop 1/30 s too
    // long on any take exported from frame 1 rather than frame 0 -- a stretch of held first pose at
    // the top of every cycle, and a stride the feet crossed 3.2% slower than the file says.
    const float start = clip != nullptr ? clip->start : 0.0f;
    const float length = clip != nullptr ? clip->length() : 0.0f;
    const auto raw = static_cast<float>((now - playing.start) * static_cast<double>(playing.speed));
    return start + (looping(playing) ? wrapTime(raw, length) : std::clamp(raw, 0.0f, length));
}

bool AnimationPlayer::looping(const Playing& playing) const {
    if (playing.loop >= 0) {
        return playing.loop != 0;
    }
    return playing.state >= 0 && states_[static_cast<std::size_t>(playing.state)].loop;
}

void AnimationPlayer::setLooping(std::optional<bool> loop) {
    current_.loop = loop ? static_cast<std::int8_t>(*loop ? 1 : 0) : std::int8_t{-1};
}

void AnimationPlayer::sampleInto(const Playing& playing, const std::vector<AnimationClip>& clips,
                                 const Skeleton& skeleton, double now, Pose& pose) const {
    setRestPose(skeleton, pose);
    if (playing.state < 0) {
        return;
    }
    const AnimationState& state = states_[static_cast<std::size_t>(playing.state)];
    if (state.clip >= clips.size()) {
        return;
    }
    sampleClip(clips[state.clip], localTime(playing, clips, now), pose);
}

void AnimationPlayer::transitionOffset(const std::vector<AnimationClip>& clips, const Skeleton& skeleton,
                                       Pose& outgoing, Pose& incoming) const {
    // Both states, sampled at the instant the transition began. Recomputed rather than remembered:
    // it is a pure function of (previous, previous start, current, current start, clips), all of
    // which the player already holds, so a scrub that lands mid-transition reconstructs the same
    // offset instead of inheriting one from wherever the playhead came from.
    sampleInto(previous_, clips, skeleton, blendStart_, outgoing);
    sampleInto(current_, clips, skeleton, blendStart_, incoming);
}

void AnimationPlayer::evaluate(const std::vector<AnimationClip>& clips, const Skeleton& skeleton, double now,
                               Pose& pose, Pose& scratch) const {
    sampleInto(current_, clips, skeleton, now, pose);
    const float weight = blendWeight(now);
    if (weight >= 1.0f || previous_.state < 0) {
        return;
    }
    if (inertializeHalflife <= 0.0f) {
        sampleInto(previous_, clips, skeleton, now, scratch);
        // The outgoing pose is `a`, the incoming one `b`, and `weight` walks from one to the other.
        blendPose(scratch, pose, weight, pose);
        return;
    }
    // ADR-547. Inertialization: the incoming clip is the pose, and what is added to it is the
    // *difference the transition introduced*, decaying to nothing. Only one clip is evaluated for
    // the body of the transition, so the cost does not double while one is running.
    //
    // A critically damped decay rather than Bollo's quintic, for the reason ADR-541 gives about
    // where state may live: the spring's closed form is a pure function of (offset, elapsed,
    // halflife) with no seed and no accumulator, and this class's whole contract is that a pose is
    // a function of when a state was entered rather than of how long it has been running.
    Pose& outgoingAtStart = scratch;
    transitionOffset(clips, skeleton, outgoingAtStart, inertScratch_);
    const auto elapsed = static_cast<float>(now - blendStart_);
    // y = 2 * ln(2) / halflife is the decay of a critically damped spring released from rest;
    // `(1 + y t) e^{-y t}` is its exact solution, and it starts at exactly 1 with zero slope, so
    // the pose at the transition instant is exactly the outgoing pose and there is no kink.
    const float y = (2.0f * 0.6931472f) / std::max(inertializeHalflife, 1e-4f);
    const float decay = (1.0f + y * elapsed) * std::exp(-y * elapsed);
    if (decay <= 1e-4f) {
        return; // the offset has gone; the incoming clip is the answer
    }
    const std::size_t joints = std::min(pose.size(), std::min(outgoingAtStart.size(), inertScratch_.size()));
    for (std::size_t j = 0; j < joints; ++j) {
        const Transform& was = outgoingAtStart.local[j];
        const Transform& became = inertScratch_.local[j];
        Transform& out = pose.local[j];
        out.position += (was.position - became.position) * decay;
        out.scale += (was.scale - became.scale) * decay;
        // The rotational offset is a rotation, composed rather than added, and slerped from
        // identity by the decay so a half-decayed offset is half the angle rather than half the
        // quaternion.
        const glm::quat offset = was.rotation * glm::conjugate(became.rotation);
        static const glm::quat kIdentity(1.0f, 0.0f, 0.0f, 0.0f);
        out.rotation = glm::normalize(glm::slerp(kIdentity, offset, decay) * out.rotation);
    }
}

// ---- SkinnedRig --------------------------------------------------------------------------------

int findClip(const std::vector<AnimationClip>& clips, std::string_view name) {
    for (std::size_t i = 0; i < clips.size(); ++i) {
        if (clips[i].name == name) {
            return static_cast<int>(i);
        }
    }
    for (std::size_t i = 0; i < clips.size(); ++i) {
        if (shortName(clips[i].name) == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int SkinnedRig::findClip(std::string_view clipName) const { return scene::findClip(clips, clipName); }

std::uint32_t SkinnedRig::analyse(const ContactSettings& settings) {
    clipPhases.assign(clips.size(), PhaseTrack{});
    clipContacts.assign(clips.size(), {});
    if (contactJoints.empty()) {
        return 0;
    }
    std::uint32_t cyclic = 0;
    for (std::size_t i = 0; i < clips.size(); ++i) {
        // The reference joint is the first one named. A caller ordering its feet left-then-right
        // gets phase anchored on the left, which is the convention every locomotion pipeline uses
        // and which this code does not itself assume -- it just takes the first.
        ClipAnalysis analysis = analyseClip(skeleton, clips[i], contactJoints, 0, settings);
        clipContacts[i] = std::move(analysis.contacts);
        clipPhases[i] = std::move(analysis.phase);
        cyclic += clipPhases[i].cyclic ? 1u : 0u;
    }
    return cyclic;
}

void SkinnedRig::addDefaultStates(float blendSeconds) {
    for (std::size_t i = 0; i < clips.size(); ++i) {
        AnimationState state;
        state.name = std::string(shortName(clips[i].name));
        state.clip = static_cast<std::uint32_t>(i);
        state.loop = true;
        state.blendIn = blendSeconds;
        player.addState(std::move(state));
    }
}

double SkinnedRig::sampleTime(double now, float hz) {
    if (hz <= 0.0f) {
        return now;
    }
    const double step = 1.0 / static_cast<double>(hz);
    return std::floor(now / step) * step;
}

float SkinnedRig::rateFor(float distance) const {
    if (cullDistance > 0.0f && distance > cullDistance) {
        return -1.0f;
    }
    if (distance <= nearDistance || farHz <= 0.0f) {
        return updateHz;
    }
    // Past nearDistance the far rate applies, but a rig that already asked for something slower
    // keeps it: the authored rate is a ceiling, not a suggestion.
    return updateHz > 0.0f ? std::min(updateHz, farHz) : farHz;
}

void SkinnedRig::hold() {
    // A rig that is not posed this frame has not moved, and the velocity target has to be told so.
    // The version bump is what makes the renderer re-upload the (now equal) previous palette; skip
    // it and a rate-limited character smears along the last step it took, every frame, for ever.
    if (previousPalette != palette) {
        previousPalette = palette;
        ++paletteVersion;
    }
}

RootMotionSample SkinnedRig::rootMotionAt(double now) const {
    RootMotionSample sample;
    if (rootMotion.empty() || !player.active()) {
        return sample;
    }
    const int stateIndex = player.currentStateIndex();
    if (stateIndex < 0 || static_cast<std::size_t>(stateIndex) >= player.states().size()) {
        return sample;
    }
    const auto clipIndex = static_cast<int>(player.states()[static_cast<std::size_t>(stateIndex)].clip);
    const RootMotionBinding* binding = rootMotion.find(clipIndex);
    if (binding == nullptr) {
        return sample; // 163 of the 168 clips leave here, having done nothing at all
    }
    sample.displacement = rootMotionDisplacement(skeleton, clips, *binding,
                                                 player.stateTime(clips, now), rootMotionScratch);
    sample.active = true;
    // The run this displacement belongs to: which state, entered when. Two samples may only be
    // subtracted from one another when these agree. Hashed rather than carried as a pair because
    // the consumer's only question is "is this the same run", and a `double` start second compared
    // for equality across a seek is the shape of bug this repository has paid for before.
    const auto bits = static_cast<std::uint64_t>(
        std::bit_cast<std::uint64_t>(player.currentStart()));
    sample.generation = (bits * 1099511628211ull) ^ (static_cast<std::uint64_t>(stateIndex) + 1ull);
    return sample;
}

bool SkinnedRig::evaluate(double now, float hz) {
    if (!skeleton.valid()) {
        return false;
    }
    const double t = sampleTime(now, hz);
    if (!palette.empty() && paletteTime >= 0.0 && t == paletteTime) {
        hold();
        return false; // the grid has not moved on: the same pose, and therefore no motion
    }
    const bool reseed = reseedPrevious;
    reseedPrevious = false;
    if (!reseed) {
        previousPalette = palette;
    }
    // Phase B: an externally supplied base pose replaces the clip player's, and nothing else
    // changes. Consumed rather than latched -- see `hasExternalPose` -- so a driver that stops
    // driving hands the body back to its clips on the next frame instead of freezing it.
    if (hasExternalPose && externalPose.size() == skeleton.joints.size()) {
        pose = externalPose;
        hasExternalPose = false;
        ++externalPoseFrames;
    } else {
        hasExternalPose = false;
        player.evaluate(clips, skeleton, t, pose, scratchPose);
    }
    // ADR-300, and the order is the whole of it: the player first, the layers on top of what it
    // produced, and only then the palette. `t` rather than `now`, so a rate-limited rig's layers
    // move on the same fixed grid its clips do -- a look that updated every frame on a rig posed at
    // 20 Hz would be a head sliding against a body that steps.
    //
    // ADR-260: this writes `pose`, which becomes `palette`, which the renderer draws. It does not
    // write the simulation position, the motion offset or the node transform, and cannot: the layer
    // module has no way to reach any of them.
    // ADR-337, and it goes *before* the layers rather than after, because it is a statement about
    // where the body is and they are statements about what parts of it are doing. A head turning
    // to look at something must turn on top of a body that has already been put where the
    // simulation now says it is; compensating afterwards would move an aim layer's pivot out from
    // under the rotation it just computed.
    //
    // ADR-260, both halves, said out loud: this writes `pose`, which becomes `palette`, which the
    // renderer draws -- **and** the same number is read back out through `rootMotionAt` into
    // `EntityState::travel`, which is `MotionAuthority::Simulation`. It is the only thing in this
    // file that touches the second one, it can only do it for a clip a scene named, and the two
    // writes are equal and opposite so the drawn body does not move.
    rootMotionApplied = false;
    if (!rootMotion.empty()) {
        const RootMotionSample sample = rootMotionAt(t);
        if (sample.active) {
            const int stateIndex = player.currentStateIndex();
            const auto* binding = rootMotion.find(
                stateIndex >= 0 ? static_cast<int>(player.states()[static_cast<std::size_t>(stateIndex)].clip)
                                : -1);
            if (binding != nullptr) {
                applyRootMotionCompensation(*binding, sample.displacement, pose);
                rootMotionApplied = true;
            }
        }
    }
    layerStats = layers.apply(skeleton, clips, t, pose);
    skinningPalette(skeleton, pose, scratchModel, palette);
    if (reseed || previousPalette.size() != palette.size()) {
        // A discontinuity, or the first evaluation: nothing moved to get here.
        previousPalette = palette;
    }
    paletteTime = t;
    ++paletteVersion;
    return true;
}

// ---- the scene-wide update ---------------------------------------------------------------------

RigStats updateRigs(Scene& scene, const FrameTime& time) {
    RigStats stats;
    stats.rigs = static_cast<std::uint32_t>(scene.rigs.size());
    if (scene.rigs.empty()) {
        return stats;
    }
    const auto begin = std::chrono::steady_clock::now();
    // Nearest authored-visible entity per rig. Camera culling only suppresses drawing; it must not
    // suppress timeline evaluation or a character freezes at the frustum boundary and pops when it
    // comes back. The explicit rig cullDistance remains the policy for skipping distant animation.
    std::vector<float> nearest(scene.rigs.size(), std::numeric_limits<float>::max());
    const glm::vec3 eye = scene.camera.position;
    for (const Entity& entity : scene.entities) {
        if (entity.rig >= scene.rigs.size() || !entity.visible) {
            continue;
        }
        const float distance = glm::distance(eye, entity.transform.position);
        float& best = nearest[entity.rig];
        best = std::min(best, distance);
    }
    for (std::size_t i = 0; i < scene.rigs.size(); ++i) {
        SkinnedRig& rig = scene.rigs[i];
        if (!rig.enabled || !rig.valid() || nearest[i] == std::numeric_limits<float>::max()) {
            rig.hold();
            ++stats.culled;
            continue;
        }
        // ADR-186: with the distance rate lifted, every rig is posed at its authored `updateHz`
        // however far away it is. `rateFor(0)` rather than a bare `updateHz` so the one rule that
        // decides a rate stays in one place -- at zero distance it is the near band by definition.
        const float hz = scene.detailLimits.rigDistanceRate ? rig.rateFor(nearest[i]) : rig.rateFor(0.0f);
        if (hz < 0.0f) {
            rig.hold();
            ++stats.culled;
            continue;
        }
        if (rig.evaluate(time.renderTime, hz)) {
            ++stats.posed;
            stats.joints += static_cast<std::uint32_t>(rig.skeleton.jointCount());
            stats.layers += rig.layerStats.applied;
            stats.layerJoints += rig.layerStats.joints;
            stats.rootMotion += rig.rootMotionApplied ? 1u : 0u;
        } else {
            ++stats.rateLimited;
        }
    }
    stats.cpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    return stats;
}


void pruneRestChannels(AnimationClip& clip, const Skeleton& skeleton, float tolerance) {
    std::vector<AnimationChannel> kept;
    kept.reserve(clip.channels.size());
    for (AnimationChannel& channel : clip.channels) {
        if (channel.joint >= skeleton.joints.size()) {
            kept.push_back(std::move(channel));
            continue;
        }
        const Transform& rest = skeleton.joints[channel.joint].rest;
        glm::vec4 restValue;
        switch (channel.path) {
        case AnimationPath::Translation: restValue = glm::vec4(rest.position, 0.0f); break;
        case AnimationPath::Rotation: restValue = glm::vec4(rest.rotation.x, rest.rotation.y, rest.rotation.z, rest.rotation.w); break;
        case AnimationPath::Scale: restValue = glm::vec4(rest.scale, 0.0f); break;
        }
        bool atRest = channel.interpolation != Interpolation::CubicSpline;
        for (const glm::vec4& v : channel.values) {
            if (!atRest) {
                break;
            }
            glm::vec4 d = v - restValue;
            if (channel.path == AnimationPath::Rotation && glm::dot(v, restValue) < 0.0f) {
                d = v + restValue; // q and -q are one rotation
            }
            atRest = std::abs(d.x) <= tolerance && std::abs(d.y) <= tolerance && std::abs(d.z) <= tolerance &&
                     std::abs(d.w) <= tolerance;
        }
        if (!atRest) {
            kept.push_back(std::move(channel));
        }
    }
    clip.channels = std::move(kept);
}

} // namespace avgen::scene
