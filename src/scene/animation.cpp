#include "scene/animation.hpp"

#include "scene/scene.hpp"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
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
    const float t = std::clamp(time, 0.0f, clip.duration);
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
    blendStart_ = now;
    blendDuration_ = previous_.state < 0 ? 0.0f : std::max(0.0f, blendSeconds);
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
    if (state.loop) {
        return false;
    }
    const float duration = state.clip < clips.size() ? clips[state.clip].duration : 0.0f;
    return stateTime(now) >= duration;
}

float AnimationPlayer::localTime(const Playing& playing, const std::vector<AnimationClip>& clips,
                                 double now) const {
    if (playing.state < 0) {
        return 0.0f;
    }
    const AnimationState& state = states_[static_cast<std::size_t>(playing.state)];
    const float duration = state.clip < clips.size() ? clips[state.clip].duration : 0.0f;
    const auto raw = static_cast<float>((now - playing.start) * static_cast<double>(playing.speed));
    return state.loop ? wrapTime(raw, duration) : std::clamp(raw, 0.0f, duration);
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

void AnimationPlayer::evaluate(const std::vector<AnimationClip>& clips, const Skeleton& skeleton, double now,
                               Pose& pose, Pose& scratch) const {
    sampleInto(current_, clips, skeleton, now, pose);
    const float weight = blendWeight(now);
    if (weight >= 1.0f || previous_.state < 0) {
        return;
    }
    sampleInto(previous_, clips, skeleton, now, scratch);
    // The outgoing pose is `a`, the incoming one `b`, and `weight` walks from one to the other.
    blendPose(scratch, pose, weight, pose);
}

// ---- SkinnedRig --------------------------------------------------------------------------------

int SkinnedRig::findClip(std::string_view clipName) const {
    for (std::size_t i = 0; i < clips.size(); ++i) {
        if (clips[i].name == clipName) {
            return static_cast<int>(i);
        }
    }
    for (std::size_t i = 0; i < clips.size(); ++i) {
        if (shortName(clips[i].name) == clipName) {
            return static_cast<int>(i);
        }
    }
    return -1;
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

bool SkinnedRig::evaluate(double now, float hz) {
    if (!skeleton.valid()) {
        return false;
    }
    const double t = sampleTime(now, hz);
    if (!palette.empty() && paletteTime >= 0.0 && t == paletteTime) {
        hold();
        return false; // the grid has not moved on: the same pose, and therefore no motion
    }
    previousPalette = palette;
    player.evaluate(clips, skeleton, t, pose, scratchPose);
    skinningPalette(skeleton, pose, scratchModel, palette);
    if (previousPalette.size() != palette.size()) {
        previousPalette = palette; // first evaluation: nothing moved yet
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
        const float hz = rig.rateFor(nearest[i]);
        if (hz < 0.0f) {
            rig.hold();
            ++stats.culled;
            continue;
        }
        if (rig.evaluate(time.renderTime, hz)) {
            ++stats.posed;
            stats.joints += static_cast<std::uint32_t>(rig.skeleton.jointCount());
        } else {
            ++stats.rateLimited;
        }
    }
    stats.cpuMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    return stats;
}

} // namespace avgen::scene
