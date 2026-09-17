#include "seq/sequence.hpp"

#include "song/from_analysis.hpp"

#include "seq/song_structure.hpp"


#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <unordered_map>
#include <utility>

namespace avgen::seq {
namespace {
using nlohmann::json;

constexpr std::array<std::pair<MarkerKind, const char*>, 3> kMarkerKinds{{
    {MarkerKind::Section, "section"},
    {MarkerKind::Cue, "cue"},
    {MarkerKind::Beat, "beat"},
}};

constexpr std::array<std::pair<CameraKind, const char*>, 4> kCameraKinds{{
    {CameraKind::Inherit, "inherit"},
    {CameraKind::Move, "move"},
    {CameraKind::Keys, "keys"},
    {CameraKind::Behavior, "behavior"},
}};

constexpr std::array<std::pair<TransitionKind, const char*>, 4> kTransitionKinds{{
    {TransitionKind::Cut, "cut"},
    {TransitionKind::FadeIn, "fadeIn"},
    {TransitionKind::FadeOut, "fadeOut"},
    {TransitionKind::MatchCut, "matchCut"},
}};

constexpr std::array<std::pair<CameraAim, const char*>, 3> kCameraAims{{
    {CameraAim::Subject, "subject"},
    {CameraAim::Travel, "travel"},
    {CameraAim::Custom, "custom"},
}};

constexpr std::array<std::pair<CameraBehaviorKind, const char*>, 3> kCameraBehaviors{{
    {CameraBehaviorKind::Chase, "chase"},
    {CameraBehaviorKind::Orbit, "orbit"},
    {CameraBehaviorKind::Pov, "pov"},
}};

constexpr std::array<std::pair<CameraPreset, const char*>, 10> kCameraPresets{{
    {CameraPreset::Isometric, "isometric"},
    {CameraPreset::Follow, "follow"},
    {CameraPreset::Wide, "wide"},
    {CameraPreset::Close, "close"},
    {CameraPreset::TopDown, "topDown"},
    {CameraPreset::Tracking, "tracking"},
    {CameraPreset::Reveal, "reveal"},
    {CameraPreset::Chase, "chase"},
    {CameraPreset::Orbit, "orbit"},
    {CameraPreset::Pov, "pov"},
}};

constexpr std::array<std::pair<SnapMode, const char*>, 4> kSnapModes{{
    {SnapMode::Off, "off"},
    {SnapMode::Frames, "frames"},
    {SnapMode::Beats, "beats"},
    {SnapMode::Markers, "markers"},
}};

template <typename E, std::size_t N>
const char* nameOf(const std::array<std::pair<E, const char*>, N>& table, E value) {
    for (const auto& [e, n] : table) {
        if (e == value) {
            return n;
        }
    }
    return table[0].second;
}

template <typename E, std::size_t N>
std::optional<E> valueOf(const std::array<std::pair<E, const char*>, N>& table, std::string_view name) {
    for (const auto& [e, n] : table) {
        if (name == n) {
            return e;
        }
    }
    return std::nullopt;
}

// The camera's distance in radii is the whole reason a shot is reusable; true isometric is 35.264
// degrees above the horizon, which in this parameterisation (y = elevation * radius, xz = radius)
// is tan(35.264 deg).
constexpr float kIsometricElevation = 0.70711f;

// ---- track accumulation -----------------------------------------------------------------------

// Keys land on named tracks from six different places (scene switching, transitions, camera moves,
// shot automation, actor transforms, clip poses), and two of them legitimately write the same
// target -- a fade at the end of one shot and a fade at the start of the next both key
// `scene/brightness`. So there is one builder, and every producer goes through it.
class TrackBuilder {
public:
    params::Track& track(const std::string& target, int component, params::TrackMode mode) {
        const std::string key = target + "#" + std::to_string(component) + "#" +
                                std::to_string(static_cast<int>(mode));
        const auto it = index_.find(key);
        if (it != index_.end()) {
            return tracks_[it->second];
        }
        params::Track t;
        t.target = target;
        t.component = component;
        t.timeBase = params::TimeBase::Seconds;
        t.mode = mode;
        index_.emplace(key, tracks_.size());
        tracks_.push_back(std::move(t));
        return tracks_.back();
    }

    void key(const std::string& target, double time, float value,
             params::KeyInterp interp = params::KeyInterp::Linear,
             params::TrackMode mode = params::TrackMode::Replace) {
        params::Key k;
        k.time = time;
        k.value[0] = value;
        k.interp = interp;
        track(target, -1, mode).addKey(k);
    }

    void key3(const std::string& target, double time, const glm::vec3& value,
              params::KeyInterp interp = params::KeyInterp::Linear,
              params::TrackMode mode = params::TrackMode::Replace) {
        params::Key k;
        k.time = time;
        k.value[0] = value.x;
        k.value[1] = value.y;
        k.value[2] = value.z;
        k.interp = interp;
        track(target, -1, mode).addKey(k);
    }

    // A key on an explicit component, with an explicit mode and an arbitrary number of values.
    // The event bake needs all three; the shot bake never did, which is why they were not here.
    void keyN(const std::string& target, int component, params::TrackMode mode, double time,
              const params::KeyValue& value, params::KeyInterp interp) {
        params::Key k;
        k.time = time;
        k.value = value;
        k.interp = interp;
        track(target, component, mode).addKey(k);
    }

    // What the track being built would evaluate to at `time`, or nothing when no key at or before
    // `time` states it. A track holds its first key's value backwards forever, so "there is a key
    // before this one" is exactly the condition under which a ramp has something to ramp *from*.
    [[nodiscard]] std::optional<params::KeyValue> valueAt(const std::string& target, int component,
                                                          params::TrackMode mode,
                                                          double time) const {
        const std::string key = target + "#" + std::to_string(component) + "#" +
                                std::to_string(static_cast<int>(mode));
        const auto it = index_.find(key);
        if (it == index_.end()) {
            return std::nullopt;
        }
        const params::Track& t = tracks_[it->second];
        if (t.keys.empty() || t.keys.front().time > time + 1e-9) {
            return std::nullopt;
        }
        return t.evaluate(time);
    }

    void merge(params::Track incoming, double timeOffset) {
        params::Track& dst = track(incoming.target, incoming.component, incoming.mode);
        dst.loopLength = incoming.loopLength;
        dst.enabled = incoming.enabled;
        for (params::Key k : incoming.keys) {
            k.time += timeOffset;
            dst.addKey(k);
        }
    }

    [[nodiscard]] std::vector<params::Track> take() { return std::move(tracks_); }
    [[nodiscard]] bool has(const std::string& target) const {
        return std::any_of(tracks_.begin(), tracks_.end(),
                           [&](const params::Track& t) { return t.target == target; });
    }

private:
    std::vector<params::Track> tracks_;
    std::unordered_map<std::string, std::size_t> index_;
};

// ---- events (section 17 of the cinematic world brief) -----------------------------------------

// A change is instantaneous when the key before it holds. `Track::addKey` replaces a key within a
// microsecond, so a millisecond is short enough that no frame rate this engine renders at can see
// the step and long enough that both values survive -- the same reasoning, and the same number, the
// camera bake already uses for a cut.
constexpr double kStepSeconds = 1e-3;

// The camera shake block (section 14). Ordinary parameters, so a beat can drive the amplitude
// through a modulation route exactly like anything else; `start` is a *time*, which is the trick
// ADR-089 already used for a clip cue's phase origin. Holding the impulse's origin in a Step-keyed
// parameter is what lets the decay be a pure function of the playhead instead of a timer.
constexpr const char* kShakeStart = "camera/shake/start";
constexpr const char* kShakeAmplitude = "camera/shake/amplitude";
constexpr const char* kShakeFrequency = "camera/shake/frequency";
constexpr const char* kShakeDecay = "camera/shake/decay";
constexpr const char* kShakeRotation = "camera/shake/rotation";

params::KeyValue scalar(float v) { return params::KeyValue{v, v, v, v}; }

float headingDegrees(const glm::vec3& direction) {
    if (glm::length(glm::vec2(direction.x, direction.z)) < 1e-5f) {
        return 0.0f;
    }
    // +Z is the character's forward axis (the convention makeWalkerScene builds to), so a heading
    // of zero faces +Z and rotation is about +Y.
    return std::atan2(direction.x, direction.z) * 180.0f / std::numbers::pi_v<float>;
}

glm::vec3 readVec3(const json& j, const char* key, glm::vec3 fallback) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() != 3) {
        return fallback;
    }
    glm::vec3 out = fallback;
    for (std::size_t i = 0; i < 3; ++i) {
        if ((*it)[i].is_number()) {
            out[static_cast<glm::length_t>(i)] = (*it)[i].get<float>();
        }
    }
    return out;
}

double readNumber(const json& j, const char* key, double fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<double>() : fallback;
}

std::string readString(const json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

// params::Track has no per-track JSON of its own; Timeline owns the serialiser. Going through a
// scratch timeline rather than writing a second one is what keeps a baked track and a hand-authored
// track the same kind of object -- the lesson ADR-075 already paid for.
json tracksToJson(std::vector<params::Track> tracks) {
    params::Timeline scratch;
    for (auto& t : tracks) {
        scratch.addTrack(std::move(t));
    }
    json doc = scratch.toJson();
    return doc["tracks"];
}

std::vector<params::Track> tracksFromJson(const json& j, const char* what) {
    params::Timeline scratch;
    json doc{{"enabled", true}, {"tracks", j}, {"cues", json::array()}};
    if (auto ok = scratch.fromJson(doc); !ok) {
        // Caller turns this into an error with context; the message already names the track.
        (void)what;
        return {};
    }
    return std::move(scratch.tracks());
}

// One value change on one parameter path, with its ramp, its mode and its optional return. This is
// the whole of "an event changes something": a light, a material, a particle rate, the fog and an
// overlay's opacity all arrive here, because in this engine all of them are a parameter path.
void writeValueChange(TrackBuilder& builder, const std::string& path, int component,
                      const EventAction& action, double time, const std::string& id,
                      std::vector<std::string>& warnings) {
    const params::TrackMode mode = action.mode;
    const params::KeyValue value{action.amount.x, action.amount.y, action.amount.z, action.amount.w};
    const float identity = mode == params::TrackMode::Multiply ? 1.0f : 0.0f;

    std::optional<params::KeyValue> before = builder.valueAt(path, component, mode, time);
    if (!before && mode != params::TrackMode::Replace) {
        // Add and Multiply know their own identity, so the baseline needs no knowledge of the
        // scene: nothing before the event, the delta afterwards, whatever the author set the
        // property to. This is the mode an event that means "a change" should be authored in.
        builder.keyN(path, component, mode, 0.0, scalar(identity), params::KeyInterp::Step);
        before = scalar(identity);
    }
    if (!before && action.seconds <= 0.0) {
        warnings.push_back(fmt::format(
            "event '{}' replaces '{}' at {:.3f}s and nothing states its value before then, so the "
            "new value also holds backwards from t = 0; key a baseline, or author the event in add "
            "or multiply mode where the identity is known",
            id, path, time));
    }
    const double arriveAt = time + std::max(action.seconds, 0.0);
    if (action.seconds > 0.0) {
        if (before) {
            builder.keyN(path, component, mode, time, *before, action.interp);
        } else {
            warnings.push_back(fmt::format(
                "event '{}' ramps '{}' over {:.3f}s from {:.3f}s, but nothing states the value it "
                "should ramp from, so the destination value holds backwards from t = 0 and there "
                "is no ramp; key a baseline, or author the event in add or multiply mode where the "
                "identity is known",
                id, path, action.seconds, time));
        }
        builder.keyN(path, component, mode, arriveAt, value, action.interp);
    } else {
        if (before) {
            builder.keyN(path, component, mode, time - kStepSeconds, *before, params::KeyInterp::Step);
        }
        builder.keyN(path, component, mode, time, value, params::KeyInterp::Step);
    }
    if (action.holdSeconds > 0.0) {
        const double holdUntil = arriveAt + action.holdSeconds;
        if (!before) {
            warnings.push_back(fmt::format(
                "event '{}' holds '{}' for {:.3f}s and then returns, but nothing states what it "
                "returns to; the change is permanent",
                id, path, action.holdSeconds));
            return;
        }
        builder.keyN(path, component, mode, holdUntil, value,
                     action.seconds > 0.0 ? action.interp : params::KeyInterp::Step);
        builder.keyN(path, component, mode,
                     holdUntil + (action.seconds > 0.0 ? action.seconds : kStepSeconds), *before,
                     action.interp);
    }
}

// What a cue says an overlay property is *before* any event moves it. A layer's authored anchor,
// scale and rotation are facts the sequence already holds, so an event that moves a pointer across
// the frame has something to move it from -- without which the move's single key would hold
// backwards and the pointer would start the piece already at its destination.
std::optional<float> overlayAuthoredValue(const OverlayCue& cue, std::string_view property) {
    if (property == overlay_property::kPositionX) {
        return cue.anchor.x;
    }
    if (property == overlay_property::kPositionY) {
        return cue.anchor.y;
    }
    if (property == overlay_property::kScaleX || property == overlay_property::kScaleY) {
        return 1.0f; // the layer's own scale; `size` is type size and is not animated here
    }
    if (property == overlay_property::kRotation) {
        return cue.rotationDegrees;
    }
    return std::nullopt; // opacity is already keyed by the cue's preset
}

// The baked tier: every firing whose action is a value over time, folded onto the track builder in
// ascending time order. After this function there is no event left -- only keys, and a frame is an
// evaluation. That sentence is the whole design (seq/events.hpp).
void bakeEventFirings(const Sequence& sequence, const EventSchedule& schedule,
                      TrackBuilder& builder, const LayerSink& sink,
                      const std::vector<OverlayBinding>& overlayBindings,
                      std::vector<std::string>& warnings) {
    bool shakeBaseline = false;
    for (const Firing& firing : schedule.baked) {
        if (firing.eventIndex >= sequence.events.size()) {
            continue;
        }
        const SequenceEvent& event = sequence.events[firing.eventIndex];
        const EventAction& action = event.what;
        const std::string id = event.id.empty() ? fmt::format("#{}", firing.eventIndex) : event.id;
        const double time = firing.timeSeconds;
        switch (action.kind) {
        case EventActionKind::SetParameter: {
            if (action.target.empty()) {
                warnings.push_back(fmt::format("event '{}' sets no parameter path", id));
                break;
            }
            writeValueChange(builder, action.target, action.component, action, time, id, warnings);
            break;
        }
        case EventActionKind::CameraShake: {
            if (!shakeBaseline) {
                // Still before the first shake, whatever the playhead does. Without this the
                // amplitude track's first key would hold backwards and the piece would open shaking.
                builder.keyN(kShakeAmplitude, -1, params::TrackMode::Replace, 0.0, scalar(0.0f),
                             params::KeyInterp::Step);
                builder.keyN(kShakeStart, -1, params::TrackMode::Replace, 0.0, scalar(0.0f),
                             params::KeyInterp::Step);
                shakeBaseline = true;
            }
            builder.keyN(kShakeStart, -1, params::TrackMode::Replace, time,
                         scalar(static_cast<float>(time)), params::KeyInterp::Step);
            builder.keyN(kShakeAmplitude, -1, params::TrackMode::Replace, time,
                         scalar(action.amount.x), params::KeyInterp::Step);
            if (action.amount.y > 0.0f) {
                builder.keyN(kShakeFrequency, -1, params::TrackMode::Replace, time,
                             scalar(action.amount.y), params::KeyInterp::Step);
            }
            if (action.amount.z > 0.0f) {
                builder.keyN(kShakeRotation, -1, params::TrackMode::Replace, time,
                             scalar(action.amount.z), params::KeyInterp::Step);
            }
            if (action.seconds > 0.0) {
                builder.keyN(kShakeDecay, -1, params::TrackMode::Replace, time,
                             scalar(static_cast<float>(action.seconds)), params::KeyInterp::Step);
            }
            break;
        }
        case EventActionKind::PlayClip: {
            // Already a `ScheduledClip` in the schedule: a clip is not a track (ADR-089). The only
            // thing left to do here is say so when the actor does not exist, because a clip cue
            // aimed at nobody is the silent no-op ADR-075 exists to record.
            if (sequence.actorNamed(action.target) == nullptr) {
                warnings.push_back(fmt::format(
                    "event '{}' plays clip '{}' on actor '{}', which the sequence does not have", id,
                    action.value, action.target));
            }
            break;
        }
        case EventActionKind::Overlay: {
            const auto binding = std::find_if(
                overlayBindings.begin(), overlayBindings.end(),
                [&](const OverlayBinding& b) { return b.cueId == action.target; });
            if (binding == overlayBindings.end()) {
                warnings.push_back(fmt::format(
                    "event '{}' addresses overlay '{}', which is not a cue in this sequence", id,
                    action.target));
                break;
            }
            if (binding->layerId.empty()) {
                warnings.push_back(fmt::format(
                    "event '{}' addresses overlay '{}', which no layer system realised", id,
                    action.target));
                break;
            }
            const std::string property =
                action.value.empty() ? std::string(overlay_property::kOpacity) : action.value;
            const LayerTarget target = sink.target(binding->layerId, property);
            if (!target.valid()) {
                warnings.push_back(fmt::format(
                    "event '{}' addresses overlay property '{}', which the layer system does not "
                    "expose",
                    id, property));
                break;
            }
            // Seed the property with what the cue authored, once, so a move has something to move
            // from. Without it the move's single key would hold backwards and the pointer would
            // open the piece already at its destination.
            const auto cue = std::find_if(sequence.overlays.begin(), sequence.overlays.end(),
                                          [&](const OverlayCue& c) { return c.id == action.target; });
            if (cue != sequence.overlays.end() && action.mode == params::TrackMode::Replace &&
                !builder.valueAt(target.path, target.component, action.mode, time)) {
                if (const auto authored = overlayAuthoredValue(*cue, property)) {
                    builder.keyN(target.path, target.component, action.mode, 0.0, scalar(*authored),
                                 params::KeyInterp::Step);
                }
            }
            writeValueChange(builder, target.path, target.component, action, time, id, warnings);
            break;
        }
        case EventActionKind::SceneTransition: {
            const SceneSlot* slot = sequence.slotNamed(action.target);
            if (slot == nullptr) {
                warnings.push_back(fmt::format(
                    "event '{}' cuts to scene slot '{}', which does not exist", id, action.target));
                break;
            }
            const TransitionKind kind =
                transitionKindFromName(action.value).value_or(TransitionKind::Cut);
            for (const SceneSlot& candidate : sequence.scenes) {
                builder.key("nodes/" + candidate.nodeName() + "/visible", time,
                            candidate.id == slot->id ? 1.0f : 0.0f, params::KeyInterp::Step);
            }
            // The dip, spelled the way the shot bake spells it, on the same track.
            if (kind == TransitionKind::FadeOut && action.seconds > 0.0) {
                builder.key("scene/brightness", time - action.seconds, 1.0f, params::KeyInterp::EaseIn);
                builder.key("scene/brightness", time, 0.0f);
            } else if (kind == TransitionKind::FadeIn && action.seconds > 0.0) {
                builder.key("scene/brightness", time, 0.0f);
                builder.key("scene/brightness", time + action.seconds, 1.0f, params::KeyInterp::EaseOut);
            }
            break;
        }
        case EventActionKind::EntityAction:
        case EventActionKind::Notify:
            // Not baked, and not reached: `resolveEvents` puts these in `dispatches`. Listed so the
            // switch stays exhaustive and a new action kind cannot be added without deciding.
            break;
        }
    }
}

} // namespace

const char* markerKindName(MarkerKind kind) { return nameOf(kMarkerKinds, kind); }
std::optional<MarkerKind> markerKindFromName(std::string_view name) { return valueOf(kMarkerKinds, name); }
const char* cameraKindName(CameraKind kind) { return nameOf(kCameraKinds, kind); }
std::optional<CameraKind> cameraKindFromName(std::string_view name) { return valueOf(kCameraKinds, name); }
const char* transitionKindName(TransitionKind kind) { return nameOf(kTransitionKinds, kind); }
std::optional<TransitionKind> transitionKindFromName(std::string_view name) {
    return valueOf(kTransitionKinds, name);
}
const char* cameraPresetName(CameraPreset preset) { return nameOf(kCameraPresets, preset); }

std::span<const CameraPreset> allCameraPresets() {
    static constexpr std::array<CameraPreset, 10> kAll{
        CameraPreset::Isometric, CameraPreset::Follow,   CameraPreset::Wide,
        CameraPreset::Close,     CameraPreset::TopDown,  CameraPreset::Tracking,
        CameraPreset::Reveal,    CameraPreset::Chase,    CameraPreset::Orbit,
        CameraPreset::Pov};
    return kAll;
}

const char* cameraAimName(CameraAim aim) { return nameOf(kCameraAims, aim); }
std::optional<CameraAim> cameraAimFromName(std::string_view name) {
    return valueOf(kCameraAims, name);
}
const char* cameraBehaviorName(CameraBehaviorKind kind) { return nameOf(kCameraBehaviors, kind); }
std::optional<CameraBehaviorKind> cameraBehaviorFromName(std::string_view name) {
    return valueOf(kCameraBehaviors, name);
}

// ---- the behaviour evaluator -------------------------------------------------------------------

glm::vec3 ActorPose::forward() const {
    const float r = glm::radians(headingDegrees);
    // +Z forward, rotation about +Y -- the same convention `headingDegrees` builds to, so a heading
    // of zero faces +Z.
    return {std::sin(r), 0.0f, std::cos(r)};
}

glm::vec3 ActorPose::right() const {
    // cross(up, forward), not cross(forward, up): with +Z forward and +Y up the first gives +X and
    // the second gives -X, and a chase offset of "+0.5 lateral" has to mean the performer's right.
    return glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward()));
}

BehaviorPose cameraPoseFor(const CameraBehavior& behavior, float t01, const ActorPose& eyeRef,
                           const ActorPose& aimRef) {
    // How far in front of the performer a `Travel` aim looks. A look-at target is a direction and not
    // a distance, so any positive number does; ten metres keeps it clear of the near plane and of the
    // performer's own geometry without being so far that a turn reads as a lag.
    constexpr float kTravelAimAhead = 10.0f;

    BehaviorPose out;
    const glm::vec3 fwd = eyeRef.forward();
    const glm::vec3 right = eyeRef.right();
    constexpr glm::vec3 kUp{0.0f, 1.0f, 0.0f};

    switch (behavior.kind) {
    case CameraBehaviorKind::Chase: {
        const glm::vec3 delta = behavior.actorSpace
                                    ? right * behavior.offset.x + kUp * behavior.offset.y +
                                          fwd * behavior.offset.z
                                    : behavior.offset;
        out.eye = eyeRef.position + delta;
        break;
    }
    case CameraBehaviorKind::Orbit: {
        // Eased on the angle rather than on the clock, so the arc starts and stops smoothly without
        // the shot's own timing being touched. smoothstep, spelled out rather than pulled in.
        const float w = std::clamp(t01, 0.0f, 1.0f);
        const float eased = behavior.easeInOut ? w * w * (3.0f - 2.0f * w) : w;
        const float a = glm::radians(std::lerp(behavior.startDegrees, behavior.endDegrees, eased));
        out.eye = eyeRef.position +
                  glm::vec3(std::sin(a) * behavior.radius, behavior.height,
                            std::cos(a) * behavior.radius);
        break;
    }
    case CameraBehaviorKind::Pov:
        out.eye = eyeRef.position + right * behavior.eyeOffset.x + kUp * behavior.eyeOffset.y +
                  fwd * behavior.eyeOffset.z;
        break;
    }

    switch (behavior.aim) {
    case CameraAim::Subject:
        out.target = aimRef.position + behavior.aimOffset;
        break;
    case CameraAim::Travel:
        // Ahead of the *performer*, not ahead of the camera. From a POV eye that is looking where
        // they are looking; from a chase it is looking where they are going, past them.
        out.target = aimRef.position + aimRef.forward() * kTravelAimAhead + behavior.aimOffset;
        break;
    case CameraAim::Custom:
        out.target = behavior.aimPoint;
        break;
    }
    return out;
}
std::optional<CameraPreset> cameraPresetFromName(std::string_view name) {
    return valueOf(kCameraPresets, name);
}
const char* snapModeName(SnapMode mode) { return nameOf(kSnapModes, mode); }
std::optional<SnapMode> snapModeFromName(std::string_view name) { return valueOf(kSnapModes, name); }

// ---- camera presets (spec 11) -----------------------------------------------------------------

ShotCamera cameraFromPreset(CameraPreset preset, const app::FocalTarget& subject) {
    ShotCamera cam;
    cam.kind = CameraKind::Move;
    app::Shot& m = cam.move;
    m.subject = subject;
    m.easeIn = true;
    m.easeOut = true;
    switch (preset) {
    case CameraPreset::Isometric:
        // The diorama shot: a fixed three-quarter view from above, holding still. It is the one
        // preset that does not move, because an isometric view that drifts stops being isometric.
        m.kind = app::ShotKind::Establish;
        m.startDistance = m.endDistance = 9.0f;
        m.startAzimuth = m.endAzimuth = 0.785398f; // 45 degrees
        m.startElevation = m.endElevation = kIsometricElevation;
        m.composition.focalLength = 55.0f;
        cam.samples = 2;
        break;
    case CameraPreset::Follow:
        // **The eye is authored and static; only the aim follows.** That is the contract, and it is
        // what `startAzimuth == endAzimuth` buys: no swing around the subject, no travel at all.
        //
        // It used to swing 0.9 -> 0.55 -- a 20 degree arc, 3.48 m on a 2 m subject. Nobody noticed,
        // and the reason is measured: with `lookAtActor` set, the aim is dragged across the world by
        // the actor while the eye creeps, and at constant distance and constant height there is no
        // looming and no vertical parallax to give the arc away. The ratio was 11.5 to 1. A camera
        // that reads as still to the person operating it should be still.
        //
        // The swing is not gone, it is `drift` in the shot inspector -- a per-shot value on the
        // azimuth pair, which every shot has already serialised. See
        // docs/investigations/follow-chase-discrepancy.md.
        m.kind = app::ShotKind::Track;
        m.startDistance = m.endDistance = 5.0f;
        m.startAzimuth = m.endAzimuth = 0.9f;
        m.startElevation = m.endElevation = 0.42f;
        m.composition.focalLength = 42.0f;
        m.look = app::LookMode::Subject;
        break;
    case CameraPreset::Wide:
        m.kind = app::ShotKind::Establish;
        m.startDistance = 14.0f;
        m.endDistance = 12.0f;
        m.startAzimuth = m.endAzimuth = 0.62f;
        m.startElevation = m.endElevation = 0.5f;
        m.composition.focalLength = 24.0f;
        break;
    case CameraPreset::Close:
        m.kind = app::ShotKind::Approach;
        m.startDistance = 3.4f;
        m.endDistance = 2.4f;
        m.startAzimuth = m.endAzimuth = 0.35f;
        m.startElevation = m.endElevation = 0.16f;
        m.composition.focalLength = 70.0f;
        break;
    case CameraPreset::TopDown:
        m.kind = app::ShotKind::Establish;
        m.startDistance = m.endDistance = 1.2f;
        m.startAzimuth = m.endAzimuth = 0.0f;
        m.startElevation = m.endElevation = 7.0f; // straight down, at 7 radii of height
        m.composition.focalLength = 40.0f;
        cam.samples = 2;
        break;
    case CameraPreset::Tracking:
        // Lateral travel with the aim held: parallax, not a pan (ADR-071's Drift).
        m.kind = app::ShotKind::Drift;
        m.startDistance = m.endDistance = 6.0f;
        m.startAzimuth = 1.25f;
        m.endAzimuth = 0.25f;
        m.startElevation = m.endElevation = 0.3f;
        m.curve = app::MovementCurve::Straight;
        m.composition.focalLength = 45.0f;
        break;
    case CameraPreset::Chase:
    case CameraPreset::Orbit:
    case CameraPreset::Pov: {
        // A behaviour, not a move: the camera is composed against a performer at each sample rather
        // than against a point fixed at the cut. `actor` is left empty on purpose -- the panel fills
        // it from the shot's `lookAtActor` if there is one, and an unset behaviour warns at bake
        // instead of quietly shooting the origin.
        cam.kind = CameraKind::Behavior;
        cam.move = app::Shot{}; // nothing here reads it; do not leave a stale move behind
        CameraBehavior& b = cam.behavior;
        b.actor.clear();
        if (preset == CameraPreset::Chase) {
            b.kind = CameraBehaviorKind::Chase;
            // Four behind, two above, level with them laterally: the over-the-shoulder default.
            b.offset = {0.0f, 2.0f, -4.0f};
            b.actorSpace = true;
            b.lagSeconds = 0.25;
            b.aim = CameraAim::Subject;
            b.aimOffset = {0.0f, 1.2f, 0.0f};
            b.clearance = 0.8f;
        } else if (preset == CameraPreset::Orbit) {
            b.kind = CameraBehaviorKind::Orbit;
            // A quarter turn over the shot, at a distance that reads for a human-scaled subject.
            b.radius = std::max(subject.radius * 4.0f, 4.0f);
            b.height = std::max(subject.radius * 1.2f, 1.5f);
            b.startDegrees = 0.0f;
            b.endDegrees = 90.0f;
            b.easeInOut = true;
            b.aim = CameraAim::Subject;
            b.aimOffset = {0.0f, subject.radius * 0.5f, 0.0f};
            b.clearance = 0.8f;
        } else {
            b.kind = CameraBehaviorKind::Pov;
            b.eyeOffset = {0.0f, 1.7f, 0.0f};
            // Looking where they are looking. Aiming a POV camera *at* its own performer would put
            // the target inside the eye, which is the one aim mode this preset must not take.
            b.aim = CameraAim::Travel;
            b.aimOffset = {0.0f, 1.6f, 0.0f};
            b.clearance = 0.0f; // the performer is already standing on the ground
        }
        cam.samples = 48; // a behaviour follows something that moves; two keys will not describe it
        break;
    }
    case CameraPreset::Reveal:
        m.kind = app::ShotKind::Reveal;
        m.startDistance = 3.0f;
        m.endDistance = 16.0f;
        m.startAzimuth = 0.5f;
        m.endAzimuth = 0.95f;
        m.startElevation = 0.2f;
        m.endElevation = 0.85f;
        m.curve = app::MovementCurve::Rise;
        m.composition.focalLength = 28.0f;
        break;
    }
    return cam;
}

// ---- actor evaluation -------------------------------------------------------------------------

glm::vec3 Actor::positionAt(double seconds) const {
    if (path.active && path.endSeconds > path.startSeconds) {
        const double clamped = std::clamp(seconds, path.startSeconds, path.endSeconds);
        const double span = path.endSeconds - path.startSeconds;
        const float w = static_cast<float>((clamped - path.startSeconds) / span);
        const float u = std::lerp(path.startU, path.endU, w);
        // By distance rather than by parameter: a Catmull-Rom's parameter is not its arc length, so
        // a character walking by `t` speeds up through the tight corners and slows on the straights.
        const float length = path.spline.length();
        return path.spline.sampleByDistance(u * length).position;
    }
    if (keys.empty()) {
        return glm::vec3(0.0f);
    }
    if (seconds <= keys.front().timeSeconds) {
        return keys.front().position;
    }
    if (seconds >= keys.back().timeSeconds) {
        return keys.back().position;
    }
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (seconds <= keys[i].timeSeconds) {
            const double a = keys[i - 1].timeSeconds;
            const double b = keys[i].timeSeconds;
            const float t = b > a ? static_cast<float>((seconds - a) / (b - a)) : 0.0f;
            // Smoothstep, matching the Smooth default the keys are emitted with, so the position a
            // look-at camera aims at agrees with the position the baked track will produce.
            const float s = keys[i - 1].interp == params::KeyInterp::Linear ? t : t * t * (3.0f - 2.0f * t);
            return glm::mix(keys[i - 1].position, keys[i].position, s);
        }
    }
    return keys.back().position;
}

float Actor::headingAt(double seconds) const {
    if (path.active && path.endSeconds > path.startSeconds && path.faceTangent) {
        const double clamped = std::clamp(seconds, path.startSeconds, path.endSeconds);
        const double span = path.endSeconds - path.startSeconds;
        const float w = static_cast<float>((clamped - path.startSeconds) / span);
        const float u = std::lerp(path.startU, path.endU, w);
        const float length = path.spline.length();
        glm::vec3 tangent = path.spline.sampleByDistance(u * length).tangent;
        if (path.endU < path.startU) {
            tangent = -tangent;
        }
        return headingDegrees(tangent);
    }
    if (keys.size() < 2) {
        return keys.empty() ? 0.0f : 0.0f;
    }
    // The direction between the surrounding keys: a character walking from A to B faces B.
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (seconds <= keys[i].timeSeconds || i + 1 == keys.size()) {
            return headingDegrees(keys[i].position - keys[i - 1].position);
        }
    }
    return 0.0f;
}

const ClipCue* Actor::clipAt(double seconds) const {
    const ClipCue* current = nullptr;
    for (const auto& c : clips) {
        if (c.timeSeconds <= seconds) {
            current = &c;
        } else {
            break;
        }
    }
    return current;
}

double Actor::endSeconds() const {
    double end = 0.0;
    for (const auto& k : keys) {
        end = std::max(end, k.timeSeconds);
    }
    for (const auto& c : clips) {
        end = std::max(end, c.timeSeconds);
    }
    if (path.active) {
        end = std::max(end, path.endSeconds);
    }
    return end;
}

// ---- sequence ---------------------------------------------------------------------------------

Result<void> Sequence::validate() const {
    // ADR-247: the authored timeline's own invariants -- ordered, gapless, covering -- are part of
    // the sequence being valid, so a malformed one is refused at the same door as everything else
    // rather than reaching the director.
    if (auto ok = sectionTimeline.validate(); !ok) {
        return fail("sequence '{}': {}", name, ok.error().message);
    }
    for (std::size_t i = 0; i < scenes.size(); ++i) {
        if (scenes[i].id.empty()) {
            return fail("sequence '{}': scene slot {} has no id", name, i);
        }
        for (std::size_t j = i + 1; j < scenes.size(); ++j) {
            if (scenes[i].id == scenes[j].id) {
                return fail("sequence '{}': two scene slots are both called '{}'", name, scenes[i].id);
            }
        }
    }
    double previousEnd = -1.0;
    for (std::size_t i = 0; i < shots.size(); ++i) {
        const Shot& s = shots[i];
        if (!std::isfinite(s.startSeconds) || !std::isfinite(s.durationSeconds)) {
            return fail("sequence '{}': shot '{}' has a non-finite time", name, s.name);
        }
        if (s.durationSeconds <= 0.0) {
            return fail("sequence '{}': shot '{}' lasts {:.3f}s", name, s.name, s.durationSeconds);
        }
        if (s.startSeconds < previousEnd - 1e-6) {
            // Two cameras at once is not something a single-camera engine can honour, and quietly
            // picking one is worse than refusing. The same rule app::Sequence already applies.
            return fail("sequence '{}': shot '{}' starts at {:.3f}s, inside the shot before it",
                        name, s.name, s.startSeconds);
        }
        previousEnd = s.endSeconds();
        if (!s.scene.empty() && slotNamed(s.scene) == nullptr) {
            return fail("sequence '{}': shot '{}' names scene '{}', which is not a slot", name,
                        s.name, s.scene);
        }
        if (!s.camera.lookAtActor.empty() && actorNamed(s.camera.lookAtActor) == nullptr) {
            return fail("sequence '{}': shot '{}' looks at actor '{}', which does not exist", name,
                        s.name, s.camera.lookAtActor);
        }
        if (s.camera.kind == CameraKind::Keys && s.camera.keys.empty()) {
            return fail("sequence '{}': shot '{}' has a keyed camera with no keys", name, s.name);
        }
        if (s.camera.kind == CameraKind::Behavior) {
            // A behaviour is a relationship, so it needs the other end of it. Refused rather than
            // warned, unlike the bake's own message: a sequence that reaches `install` with an
            // unresolvable behaviour has already been saved, and the point of failing here is that
            // it cannot be.
            const CameraBehavior& b = s.camera.behavior;
            if (b.actor.empty()) {
                return fail("sequence '{}': shot '{}' has a {} camera that names no performer", name,
                            s.name, cameraBehaviorName(b.kind));
            }
            if (actorNamed(b.actor) == nullptr) {
                return fail("sequence '{}': shot '{}' has a {} camera on performer '{}', which does "
                            "not exist", name, s.name, cameraBehaviorName(b.kind), b.actor);
            }
            if (b.kind == CameraBehaviorKind::Orbit && b.radius <= 0.0f) {
                return fail("sequence '{}': shot '{}' orbits at radius {:.3f}; an orbit of zero "
                            "radius is a camera inside its subject", name, s.name, b.radius);
            }
            if (b.lagSeconds < 0.0) {
                // A negative lag would sample the performer in their own future, which the bake can
                // actually do -- and which is a different feature wearing this one's name.
                return fail("sequence '{}': shot '{}' has a camera lag of {:.3f}s; a lag is how far "
                            "*behind* the performer the camera stands", name, s.name, b.lagSeconds);
            }
        }
    }
    for (std::size_t i = 0; i < actors.size(); ++i) {
        if (actors[i].id.empty()) {
            return fail("sequence '{}': actor {} has no id", name, i);
        }
        for (std::size_t j = i + 1; j < actors.size(); ++j) {
            if (actors[i].id == actors[j].id) {
                return fail("sequence '{}': two actors are both called '{}'", name, actors[i].id);
            }
        }
        if (actors[i].path.active) {
            if (auto ok = actors[i].path.spline.validate(); !ok) {
                return fail("sequence '{}': actor '{}' path: {}", name, actors[i].id,
                            ok.error().message);
            }
            if (actors[i].path.endSeconds <= actors[i].path.startSeconds) {
                return fail("sequence '{}': actor '{}' walks its path in {:.3f}s", name,
                            actors[i].id, actors[i].path.endSeconds - actors[i].path.startSeconds);
            }
        }
    }
    for (std::size_t i = 0; i < overlays.size(); ++i) {
        if (auto ok = overlays[i].validate(); !ok) {
            return fail("sequence '{}': {}", name, ok.error().message);
        }
        for (std::size_t j = i + 1; j < overlays.size(); ++j) {
            if (overlays[i].id == overlays[j].id) {
                return fail("sequence '{}': two overlays are both called '{}'", name, overlays[i].id);
            }
        }
    }
    return {};
}

double Sequence::duration() const {
    if (durationSeconds > 0.0) {
        return durationSeconds;
    }
    double end = 0.0;
    for (const auto& s : shots) {
        end = std::max(end, s.endSeconds());
    }
    for (const auto& a : actors) {
        end = std::max(end, a.endSeconds());
    }
    for (const auto& o : overlays) {
        end = std::max(end, o.endSeconds);
    }
    for (const auto& t : tracks) {
        if (t.timeBase == params::TimeBase::Seconds && !t.keys.empty()) {
            end = std::max(end, t.keys.back().time);
        }
    }
    for (const auto& e : events) {
        // Only an absolute time can extend the piece. Every other trigger is derived from something
        // already counted above (a shot edge, a marker, an actor's clip), so counting those again
        // would be circular.
        if (e.enabled && e.when.kind == TriggerKind::Time) {
            end = std::max(end, e.when.timeSeconds + e.when.delaySeconds);
        }
    }
    return end;
}

// ---- editing shots ------------------------------------------------------------------------------
//
// Moved out of `SequencePanel` unchanged. Each one is the arithmetic a gesture already performed;
// the point of the move is that a test can now reach it.

void moveShot(Shot& shot, double newStart) { shot.startSeconds = std::max(0.0, newStart); }

void trimShotEnd(Shot& shot, double newEnd, double minSeconds) {
    shot.durationSeconds = std::max(minSeconds, newEnd - shot.startSeconds);
}

void trimShotStart(Shot& shot, double newStart, double fixedEnd, double minSeconds) {
    // The end is the caller's, not the shot's: see the header. Clamped so the shot cannot be trimmed
    // through its own end and out the other side.
    const double start = std::clamp(newStart, 0.0, fixedEnd - minSeconds);
    shot.startSeconds = start;
    shot.durationSeconds = fixedEnd - start;
}

namespace {
// Nudges `value` onto `edge` when it is already within `snap` of it.
double snapTo(double value, double edge, double snap) {
    return std::fabs(value - edge) <= snap ? edge : value;
}
} // namespace

void moveShot(std::vector<Shot>& shots, std::size_t index, double newStart, double snapSeconds) {
    if (index >= shots.size()) {
        return;
    }
    Shot& shot = shots[index];
    const double length = shot.durationSeconds;
    double start = std::max(0.0, newStart);
    // The room this shot has, which is the gap its neighbours leave. A shot cannot be dragged past
    // a neighbour -- reordering is done by moving the others, not by swapping underneath them.
    const double floorAt = index > 0 ? shots[index - 1].endSeconds() : 0.0;
    const double ceilAt = index + 1 < shots.size() ? shots[index + 1].startSeconds
                                                   : std::numeric_limits<double>::max();
    start = snapTo(start, floorAt, snapSeconds);
    if (ceilAt != std::numeric_limits<double>::max()) {
        start = snapTo(start + length, ceilAt, snapSeconds) - length;
    }
    start = std::max(start, floorAt);
    if (ceilAt != std::numeric_limits<double>::max()) {
        start = std::min(start, ceilAt - length);
    }
    shot.startSeconds = std::max(0.0, start);
}

void trimShotEnd(std::vector<Shot>& shots, std::size_t index, double newEnd, double minSeconds,
                 double snapSeconds) {
    if (index >= shots.size()) {
        return;
    }
    Shot& shot = shots[index];
    double end = newEnd;
    if (index + 1 < shots.size()) {
        const double next = shots[index + 1].startSeconds;
        end = std::min(snapTo(end, next, snapSeconds), next);
    }
    trimShotEnd(shot, end, minSeconds);
}

void trimShotStart(std::vector<Shot>& shots, std::size_t index, double newStart, double fixedEnd,
                   double minSeconds, double snapSeconds) {
    if (index >= shots.size()) {
        return;
    }
    double start = newStart;
    const double floorAt = index > 0 ? shots[index - 1].endSeconds() : 0.0;
    start = std::max(snapTo(start, floorAt, snapSeconds), floorAt);
    trimShotStart(shots[index], start, fixedEnd, minSeconds);
}

std::optional<std::size_t> splitShot(std::vector<Shot>& shots, std::size_t index, double seconds,
                                     double minSeconds) {
    if (index >= shots.size()) {
        return std::nullopt;
    }
    const Shot& shot = shots[index];
    if (seconds <= shot.startSeconds + minSeconds || seconds >= shot.endSeconds() - minSeconds) {
        return std::nullopt;
    }
    Shot tail = shot;
    tail.name = fmt::format("{} b", shot.name);
    tail.startSeconds = seconds;
    tail.durationSeconds = shot.endSeconds() - seconds;
    tail.in = Transition{TransitionKind::Cut, 0.0};
    shots[index].durationSeconds = seconds - shot.startSeconds;
    shots[index].out = Transition{TransitionKind::Cut, 0.0};
    shots.insert(shots.begin() + static_cast<std::ptrdiff_t>(index) + 1, std::move(tail));
    return index + 1;
}

std::optional<std::size_t> duplicateShot(std::vector<Shot>& shots, std::size_t index) {
    if (index >= shots.size()) {
        return std::nullopt;
    }
    Shot copy = shots[index];
    copy.name = fmt::format("{} copy", shots[index].name);
    // Placed AFTER the original rather than on top of it. A duplicate that lands on the same span
    // would be invisible -- `shotAt` takes the first match, so the copy would never play and the
    // gesture would look like it had done nothing.
    copy.startSeconds = shots[index].endSeconds();
    shots.insert(shots.begin() + static_cast<std::ptrdiff_t>(index) + 1, std::move(copy));
    return index + 1;
}

bool removeShot(std::vector<Shot>& shots, std::size_t index) {
    if (index >= shots.size()) {
        return false;
    }
    shots.erase(shots.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

const Shot* Sequence::shotAt(double seconds) const {
    for (const auto& s : shots) {
        if (seconds >= s.startSeconds && seconds < s.endSeconds()) {
            return &s;
        }
    }
    return nullptr;
}

const Shot* Sequence::shotNamed(std::string_view n) const {
    for (const auto& s : shots) {
        if (s.name == n) {
            return &s;
        }
    }
    return nullptr;
}

const Actor* Sequence::actorNamed(std::string_view id) const {
    for (const auto& a : actors) {
        if (a.id == id) {
            return &a;
        }
    }
    return nullptr;
}

const SceneSlot* Sequence::slotNamed(std::string_view id) const {
    for (const auto& s : scenes) {
        if (s.id == id) {
            return &s;
        }
    }
    return nullptr;
}

const SceneSlot* Sequence::sceneAt(double seconds) const {
    const SceneSlot* current = nullptr;
    for (const auto& s : shots) {
        if (!s.scene.empty()) {
            const SceneSlot* slot = slotNamed(s.scene);
            if (slot != nullptr && s.startSeconds <= seconds) {
                current = slot;
            }
            if (slot != nullptr && current == nullptr && seconds < s.startSeconds) {
                // Before the first shot the first named slot is what will be showing.
                return slot;
            }
        }
        if (s.startSeconds > seconds) {
            break;
        }
    }
    if (current == nullptr) {
        for (const auto& s : shots) {
            if (!s.scene.empty()) {
                return slotNamed(s.scene);
            }
        }
    }
    return current;
}

void Sequence::setSectionMarkers(const signals::MusicalStructure& folded) {
    std::erase_if(markers, [](const Marker& m) { return m.kind == MarkerKind::Section; });
    for (const auto& s : folded.sections) {
        markers.push_back(
            Marker{s.startSeconds, signals::musicalSectionName(s.kind), MarkerKind::Section});
    }
    std::stable_sort(markers.begin(), markers.end(),
                     [](const Marker& a, const Marker& b) { return a.timeSeconds < b.timeSeconds; });
}

void Sequence::setSectionMarkers(const analysis::SongStructure& songStructure) {
    std::erase_if(markers, [](const Marker& m) { return m.kind == MarkerKind::Section; });
    for (const auto& s : songStructure.sections) {
        markers.push_back(Marker{s.startSeconds, sectionDisplayName(s), MarkerKind::Section});
    }
    std::stable_sort(markers.begin(), markers.end(),
                     [](const Marker& a, const Marker& b) { return a.timeSeconds < b.timeSeconds; });
}

void Sequence::refreshSectionMarkers() { setSectionMarkers(structure); }

void Sequence::setBeatMarkers(std::span<const double> beatTimes) {
    std::erase_if(markers, [](const Marker& m) { return m.kind == MarkerKind::Beat; });
    markers.reserve(markers.size() + beatTimes.size());
    for (std::size_t i = 0; i < beatTimes.size(); ++i) {
        markers.push_back(Marker{beatTimes[i], std::to_string(i + 1), MarkerKind::Beat});
    }
    std::stable_sort(markers.begin(), markers.end(),
                     [](const Marker& a, const Marker& b) { return a.timeSeconds < b.timeSeconds; });
}

std::vector<double> Sequence::markerTimes(MarkerKind kind) const {
    std::vector<double> out;
    for (const auto& m : markers) {
        if (m.kind == kind) {
            out.push_back(m.timeSeconds);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ---- the bake ---------------------------------------------------------------------------------

std::vector<AnimationCue> Sequence::animationAt(double seconds) const {
    std::vector<AnimationCue> out;
    out.reserve(actors.size());
    for (const Actor& actor : actors) {
        const ClipCue* cue = actor.clipAt(seconds);
        if (cue == nullptr || cue->clip.empty()) {
            continue;
        }
        out.push_back(AnimationCue{.node = actor.nodeName(),
                                   .clip = cue->clip,
                                   .startSeconds = cue->timeSeconds,
                                   .speed = cue->speed,
                                   .blendSeconds = cue->blendSeconds});
    }
    return out;
}

std::vector<AnimationCue> Sequence::animationAt(double seconds,
                                                std::span<const ScheduledClip> scheduled) const {
    std::vector<AnimationCue> out = animationAt(seconds);
    if (scheduled.empty()) {
        return out;
    }
    for (const Actor& actor : actors) {
        const ScheduledClip* latest = nullptr;
        for (const ScheduledClip& c : scheduled) {
            if (c.actor != actor.id || c.timeSeconds > seconds) {
                continue;
            }
            if (latest == nullptr || c.timeSeconds >= latest->timeSeconds) {
                latest = &c;
            }
        }
        if (latest == nullptr) {
            continue;
        }
        // A scheduled clip and an authored cue are the same object, so the later of the two is
        // simply the actor's state. Still one pure function of the second: both lists are sorted
        // and neither depends on how the playhead arrived.
        const ClipCue* authored = actor.clipAt(seconds);
        if (authored != nullptr && authored->timeSeconds > latest->timeSeconds) {
            continue;
        }
        const std::string node = actor.nodeName();
        std::erase_if(out, [&](const AnimationCue& c) { return c.node == node; });
        if (latest->clip.empty()) {
            continue;
        }
        out.push_back(AnimationCue{.node = node,
                                   .clip = latest->clip,
                                   .startSeconds = latest->timeSeconds,
                                   .speed = latest->speed,
                                   .blendSeconds = latest->blendSeconds});
    }
    return out;
}

TriggerContext Sequence::triggerContext(int beatsPerBar) const {
    TriggerContext ctx;
    ctx.durationSeconds = duration();
    ctx.beatTimes = markerTimes(MarkerKind::Beat);
    // A bar is every Nth beat. The analysis has its own bar counter, but a sequence carries beats
    // and not bars, and folding here means the editor's ruler and an event agree about where bar 9
    // is rather than each deriving it.
    const std::size_t per = static_cast<std::size_t>(std::max(1, beatsPerBar));
    for (std::size_t i = 0; i < ctx.beatTimes.size(); i += per) {
        ctx.barTimes.push_back(ctx.beatTimes[i]);
    }
    for (const Marker& m : markers) {
        if (m.kind == MarkerKind::Section) {
            ctx.sections.push_back(TriggerContext::NamedSpan{m.name, m.timeSeconds, 0.0});
        } else if (m.kind == MarkerKind::Cue) {
            ctx.cues.emplace_back(m.name, m.timeSeconds);
        }
    }
    for (std::size_t i = 0; i < ctx.sections.size(); ++i) {
        ctx.sections[i].endSeconds = i + 1 < ctx.sections.size()
                                         ? ctx.sections[i + 1].startSeconds
                                         : ctx.durationSeconds;
    }
    for (const Shot& s : shots) {
        ctx.shots.push_back(TriggerContext::NamedSpan{s.name, s.startSeconds, s.endSeconds()});
    }
    for (const Actor& a : actors) {
        for (std::size_t i = 0; i < a.clips.size(); ++i) {
            const ClipCue& c = a.clips[i];
            if (c.clip.empty()) {
                continue;
            }
            // The sequence knows when a clip stops being the actor's state only when a following
            // cue says so. It never knows how long the clip itself is -- that is the asset's fact,
            // not the piece's -- so an unbounded cue is reported as unbounded rather than guessed.
            const double end = i + 1 < a.clips.size() ? a.clips[i + 1].timeSeconds : -1.0;
            ctx.clips.push_back(
                TriggerContext::ClipSpan{a.id, c.clip, c.timeSeconds, end});
        }
    }
    return ctx;
}

Result<BakeResult> Sequence::bake(LayerSink& sink, const BakeOptions& options) const {
    if (auto ok = validate(); !ok) {
        return std::unexpected(ok.error());
    }
    BakeResult result;
    TrackBuilder builder;

    // A composition ignores `camera/position` and `camera/target` entirely unless `camera/mode` is
    // 1 (free); in orbit mode the tracks bind, evaluate, write their values and change nothing you
    // can see. That is the exact failure ADR-075 exists to record, so the mode is part of the bake
    // rather than something an author has to remember.
    const bool anyCamera = std::any_of(shots.begin(), shots.end(), [](const Shot& s) {
        return s.camera.kind != CameraKind::Inherit;
    });
    if (options.emitCameraMode && anyCamera) {
        builder.key("camera/mode", 0.0, 1.0f, params::KeyInterp::Step);
    }
    if (!shots.empty() && shots.front().camera.kind == CameraKind::Inherit) {
        result.warnings.push_back(fmt::format(
            "shot '{}' is the first shot and inherits its camera, so nothing places the camera "
            "before it",
            shots.front().name));
    }

    // ---- scene switching (spec 6) -------------------------------------------------------------
    // One Step-keyed visibility track per slot. Step because a cut is a cut: an interpolated bool
    // would have both scenes half-visible for a frame, which in a bool parameter means one of them
    // flickers.
    if (!scenes.empty() && !shots.empty()) {
        std::string active;
        // What is showing before the first shot: the slot that shot names, so a scrub to 0 is not a
        // black frame.
        for (const auto& s : shots) {
            if (!s.scene.empty()) {
                active = s.scene;
                break;
            }
        }
        for (const auto& slot : scenes) {
            builder.key("nodes/" + slot.nodeName() + "/visible", 0.0,
                        slot.id == active ? 1.0f : 0.0f, params::KeyInterp::Step);
        }
        for (const auto& s : shots) {
            if (s.scene.empty() || s.scene == active) {
                continue;
            }
            active = s.scene;
            for (const auto& slot : scenes) {
                builder.key("nodes/" + slot.nodeName() + "/visible", s.startSeconds,
                            slot.id == active ? 1.0f : 0.0f, params::KeyInterp::Step);
            }
        }
    }

    // ---- transitions (spec 7) -----------------------------------------------------------------
    // Only the two that touch `scene/brightness`. A match cut is a hard cut that changes where the
    // *camera* starts, so it must not pull a brightness track into a sequence that has no fades.
    const auto dips = [](TransitionKind k) {
        return k == TransitionKind::FadeIn || k == TransitionKind::FadeOut;
    };
    const bool anyFade = std::any_of(shots.begin(), shots.end(), [&](const Shot& s) {
        return dips(s.in.kind) || dips(s.out.kind);
    });
    if (anyFade) {
        builder.key("scene/brightness", 0.0, 1.0f, params::KeyInterp::Linear);
        for (const auto& s : shots) {
            if (s.in.kind == TransitionKind::FadeIn && s.in.seconds > 0.0) {
                builder.key("scene/brightness", s.startSeconds, options.fadeFloor);
                builder.key("scene/brightness", s.startSeconds + s.in.seconds, 1.0f,
                            params::KeyInterp::EaseOut);
            }
            if (s.out.kind == TransitionKind::FadeOut && s.out.seconds > 0.0) {
                builder.key("scene/brightness", std::max(s.startSeconds, s.endSeconds() - s.out.seconds),
                            1.0f, params::KeyInterp::EaseIn);
                builder.key("scene/brightness", s.endSeconds(), options.fadeFloor);
            }
        }
    }

    // ---- camera (spec 8-11) -------------------------------------------------------------------
    //
    // A cut is two shots meeting at one second, and a track holds one key per time: `Track::addKey`
    // replaces a key within a microsecond of an existing one, so an outgoing shot's final pose and
    // an incoming shot's opening pose written at the same second would be one key, and the outgoing
    // shot would spend its whole length gliding towards the *next* shot's opening frame. The last
    // key of a shot that is cut away from therefore lands a millisecond early: short enough that no
    // frame rate this engine renders at can see the ramp, long enough that both poses survive.
    constexpr double kCutSeconds = 1e-3;
    for (std::size_t si = 0; si < shots.size(); ++si) {
        const Shot& s = shots[si];
        ShotCamera cam = s.camera;
        // ---- match cut (section 35) -----------------------------------------------------------
        // A match cut is a hard cut whose two frames rhyme: the incoming subject lands at the same
        // apparent size and the same place in frame as the outgoing one, so the eye reads
        // continuity across a change of subject, scene and lighting. `subjectCoverageAt` already
        // states the apparent size as a fraction of frame height, and a shot's opening distance is
        // in radii -- so the match is one inversion of that expression, decided here, costing the
        // renderer nothing. Crossfade is still not implemented; ADR-098 records why a second look
        // did not change the answer.
        if (si > 0 && s.in.kind == TransitionKind::MatchCut) {
            const ShotCamera& previous = shots[si - 1].camera;
            if (cam.kind != CameraKind::Move || previous.kind != CameraKind::Move) {
                result.warnings.push_back(fmt::format(
                    "shot '{}' asks for a match cut, but a match cut is a statement about two "
                    "subject-relative moves and one of the two shots is not one",
                    s.name));
            } else if (cam.move.startPosition || cam.move.subject.radius <= 0.0f) {
                result.warnings.push_back(fmt::format(
                    "shot '{}' asks for a match cut, but its opening is an authored position (or "
                    "its subject has no radius), so there is no distance to match with",
                    s.name));
            } else {
                const float target = previous.move.subjectCoverageAt(1.0f);
                // coverage = atan(radius / distance) / atan(sensorHalfHeight / focal), and the
                // distance the shot carries is in radii -- so the radius cancels and the match is a
                // function of the incoming lens alone.
                const float halfFrame =
                    std::atan(12.0f / std::max(cam.move.composition.focalLength, 1.0f));
                const float angle = std::clamp(target * halfFrame, 1e-4f, 1.5f);
                cam.move.startDistance = std::clamp(1.0f / std::tan(angle), 0.2f, 400.0f);
                // The framing offset is the other half of "the same place in frame".
                cam.move.composition.framing = previous.move.composition.framing;
                cam.move.composition.headroom = previous.move.composition.headroom;
                const float achieved = cam.move.subjectCoverageAt(0.0f);
                if (target > 1e-3f && std::abs(achieved - target) > target * 0.15f) {
                    // `preferredDistance` is a bound the director honours (ADR-071), so a subject
                    // that states a stand-off can refuse to be matched. Said out loud rather than
                    // leaving the author to wonder why the cut does not read.
                    result.warnings.push_back(fmt::format(
                        "shot '{}' match-cuts to {:.3f} of frame height but lands at {:.3f}; the "
                        "subject's preferred distance is bounding the match",
                        s.name, target, achieved));
                }
            }
        }
        // Only when something actually cuts here. A following shot that inherits its camera is an
        // author saying "keep going", and nudging the key would put a stutter in a continuous move.
        const bool cutAfter = si + 1 < shots.size() &&
                              shots[si + 1].camera.kind != CameraKind::Inherit &&
                              shots[si + 1].startSeconds <= s.endSeconds() + 1e-9;
        const double cameraEnd = cutAfter ? s.endSeconds() - kCutSeconds : s.endSeconds();
        const Actor* lookAt = cam.lookAtActor.empty() ? nullptr : actorNamed(cam.lookAtActor);
        const float weight = std::clamp(cam.lookAtWeight, 0.0f, 1.0f);
        const auto aim = [&](double time, glm::vec3 derived) {
            if (lookAt == nullptr || weight <= 0.0f) {
                return derived;
            }
            const glm::vec3 subject = lookAt->positionAt(time) + glm::vec3(0.0f, cam.lookAtHeight, 0.0f);
            return glm::mix(derived, subject, weight);
        };
        if (cam.kind == CameraKind::Move) {
            int samples = cam.samples > 0 ? cam.samples : options.cameraSamplesPerShot;
            samples = std::clamp(samples, 2, 256);
            for (int i = 0; i < samples; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(samples - 1);
                const double time = std::lerp(s.startSeconds, cameraEnd, static_cast<double>(t));
                // The keys carry eased values, so the interpolation between them is linear and the
                // shape of the move is entirely the shot's own -- handing eased values to a smooth
                // interpolator eases them twice (the same reasoning as cinematic.cpp).
                builder.key3("camera/position", time, cam.move.cameraAt(t));
                builder.key3("camera/target", time, aim(time, cam.move.targetAt(t)));
            }
            // The lens is per shot: a focal length that slides through a shot is a zoom, and a zoom
            // is something you ask for rather than something you get by accident.
            if (cam.move.composition.focalLength > 0.0f) {
                builder.key("camera/lens/focalLength", s.startSeconds, cam.move.composition.focalLength,
                            params::KeyInterp::Step);
            }
        } else if (cam.kind == CameraKind::Behavior) {
            // ---- a behaviour, resolved against the performer at each sample --------------------
            //
            // This is the whole of "live target, deterministic camera". `Actor::positionAt` and
            // `::headingAt` are pure functions of time, so asking them at each sample is asking a
            // fact rather than running a simulation -- and what comes out is an ordinary key track,
            // indistinguishable from a hand-authored one and just as scrub-exact.
            const Actor* subject = cam.behavior.actor.empty() ? nullptr
                                                              : actorNamed(cam.behavior.actor);
            if (subject == nullptr) {
                // Named nothing, or named a performer that is not in the piece. Warned rather than
                // defaulted: a camera behaviour with no subject would sit at the world origin
                // looking at the world origin, which is a shot that renders and means nothing.
                result.warnings.push_back(fmt::format(
                    "shot '{}': camera behaviour '{}' names {} -- no camera keys emitted",
                    s.name, cameraBehaviorName(cam.behavior.kind),
                    cam.behavior.actor.empty() ? "no performer"
                                               : "performer '" + cam.behavior.actor + "'"));
            } else {
                int samples = cam.samples > 0 ? cam.samples : options.cameraSamplesPerShot;
                samples = std::clamp(samples, 2, 256);
                const CameraBehavior& b = cam.behavior;
                for (int i = 0; i < samples; ++i) {
                    const float t01 = static_cast<float>(i) / static_cast<float>(samples - 1);
                    const double time = std::lerp(s.startSeconds, cameraEnd, static_cast<double>(t01));
                    // Two sample times, and the split is what lets a chase trail while still looking
                    // where the performer is going. The lag is clamped to the shot's own start:
                    // sampling before a shot begins is legitimate (the performer existed then) but
                    // sampling before the performer's first key just repeats it, which is the
                    // correct and quiet behaviour rather than something to guard against.
                    const double eyeTime = time - (b.kind == CameraBehaviorKind::Chase
                                                       ? std::max(0.0, b.lagSeconds)
                                                       : 0.0);
                    const double aimTime = time + std::max(0.0, b.lookAheadSeconds);
                    const ActorPose eyeRef{.position = subject->positionAt(eyeTime),
                                           .headingDegrees = subject->headingAt(eyeTime)};
                    const ActorPose aimRef{.position = subject->positionAt(aimTime),
                                           .headingDegrees = subject->headingAt(aimTime)};
                    BehaviorPose pose = cameraPoseFor(b, t01, eyeRef, aimRef);

                    // Clearance, applied here because the bake is the only place the whole path is
                    // known at once -- a chase that would have crossed a hill is lifted over it
                    // before a frame is rendered. Raised, never lowered: a camera legitimately above
                    // the hill it is crossing must not be dragged down onto it.
                    if (b.clearance > 0.0f && options.groundHeightAt) {
                        const float floorY = options.groundHeightAt(pose.eye.x, pose.eye.z) + b.clearance;
                        pose.eye.y = std::max(pose.eye.y, floorY);
                    }
                    builder.key3("camera/position", time, pose.eye);
                    builder.key3("camera/target", time, pose.target);
                }
                if (b.clearance > 0.0f && !options.groundHeightAt) {
                    // Said once per shot rather than swallowed: a clearance that silently does
                    // nothing is a setting the application does not keep (ADR-225).
                    result.warnings.push_back(fmt::format(
                        "shot '{}': camera clearance {:.2f} m was not applied -- this bake has no "
                        "ground to measure against", s.name, b.clearance));
                }
                if (cam.move.composition.focalLength > 0.0f) {
                    builder.key("camera/lens/focalLength", s.startSeconds,
                                cam.move.composition.focalLength, params::KeyInterp::Step);
                }
            }
        } else if (cam.kind == CameraKind::Keys) {
            for (const auto& k : cam.keys) {
                const double time = std::min(s.startSeconds + k.timeSeconds, cameraEnd);
                builder.key3("camera/position", time, k.position, k.interp);
                builder.key3("camera/target", time, aim(time, k.target), k.interp);
                if (k.focalLength > 0.0f) {
                    builder.key("camera/lens/focalLength", time, k.focalLength, k.interp);
                }
            }
        }
        // Shot-local parameter animation (spec 17): times are relative to the shot.
        for (const auto& t : s.tracks) {
            builder.merge(t, s.startSeconds);
        }
    }

    // ---- sequence-level tracks (spec 16, 17) --------------------------------------------------
    for (const auto& t : tracks) {
        builder.merge(t, 0.0);
    }

    // ---- actors (spec 12-16) ------------------------------------------------------------------
    for (const auto& actor : actors) {
        const std::string node = actor.nodeName();
        const std::string positionPath = "nodes/" + node + "/position";
        const std::string rotationPath = "nodes/" + node + "/rotation";

        if (actor.path.active) {
            const int samples = std::clamp(actor.path.samples, 2, 512);
            for (int i = 0; i < samples; ++i) {
                const double w = static_cast<double>(i) / static_cast<double>(samples - 1);
                const double time = std::lerp(actor.path.startSeconds, actor.path.endSeconds, w);
                builder.key3(positionPath, time, actor.positionAt(time));
                if (actor.path.faceTangent) {
                    builder.key3(rotationPath, time, glm::vec3(0.0f, actor.headingAt(time), 0.0f));
                }
            }
        }
        for (const auto& k : actor.keys) {
            builder.key3(positionPath, k.timeSeconds, k.position, k.interp);
            if (k.rotationDegrees) {
                builder.key3(rotationPath, k.timeSeconds, *k.rotationDegrees, k.interp);
            } else if (!actor.path.active && actor.keys.size() > 1) {
                builder.key3(rotationPath, k.timeSeconds,
                             glm::vec3(0.0f, actor.headingAt(k.timeSeconds), 0.0f), k.interp);
            }
            if (k.scale) {
                builder.key3("nodes/" + node + "/scale", k.timeSeconds, *k.scale, k.interp);
            }
        }

        // spec 12: an actor that is not in the piece is hidden, rather than parked off camera.
        if (!actor.visible) {
            builder.key("nodes/" + node + "/visible", 0.0, 0.0f, params::KeyInterp::Step);
        }

        // Animation clips are deliberately *not* baked (spec 13, 33). A clip is a state on the
        // node's skinned rig, and a track carries numbers -- so the only thing a track could
        // carry is a state index, and the index alone loses the one fact the pose needs: the
        // second the state was entered. `animationAt()` keeps that fact and `seq::Director`
        // applies it every frame, which is the same pure function of time the tracks are.
    }

    // ---- overlays: the seam (spec 22-27) ------------------------------------------------------
    // Realised in (order, start) order, because most layer systems draw in creation order and
    // spec 26 requires the ordering to be explicit rather than accidental.
    std::vector<const OverlayCue*> ordered;
    ordered.reserve(overlays.size());
    for (const auto& o : overlays) {
        ordered.push_back(&o);
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const OverlayCue* a, const OverlayCue* b) {
        return a->order != b->order ? a->order < b->order : a->startSeconds < b->startSeconds;
    });
    int deferred = 0;
    for (const OverlayCue* cue : ordered) {
        auto layerId = sink.realise(*cue);
        if (!layerId) {
            return std::unexpected(layerId.error());
        }
        result.overlays.push_back(OverlayBinding{cue->id, *layerId});
        if (layerId->empty()) {
            ++deferred;
            continue;
        }
        for (auto& t : overlayPresetTracks(*cue, sink, *layerId)) {
            builder.merge(std::move(t), 0.0);
        }
    }
    if (deferred > 0) {
        result.warnings.push_back(fmt::format(
            "{} overlay cue(s) were not realised: no layer system is installed, so the sequence's "
            "text and graphics carry timing but draw nothing",
            deferred));
    }

    // ---- shot-driven quality (section 34) -----------------------------------------------------
    //
    // A `Spotlight` already says "this object is the point of this shot" (ADR-062). Letting it
    // raise the subject's level-of-detail floor is what turns that from a note into a decision the
    // renderer acts on: a close-up protagonist gets the expensive treatment for the length of its
    // shot and a distant building does not.
    //
    // Emitted as **Multiply** tracks, which is the whole trick. Every one of these knobs treats
    // zero as "no limit" (procedural.hpp: a ladder threshold of 0 ends the ladder, a minimum
    // screen radius of 0 never culls), so multiplying by 1 - emphasis pins a fully spotlit subject
    // at its best level and leaves an unemphasised one exactly as the author set it -- without the
    // bake ever having to know, or restore, the values the author chose. A Replace track would have
    // had to guess them.
    if (options.spotlightQuality) {
        for (const Shot& s : shots) {
            if (s.camera.kind != CameraKind::Move || !s.camera.move.spotlight.active) {
                continue;
            }
            const float emphasis = std::clamp(s.camera.move.spotlight.emphasis, 0.0f, 1.0f);
            const std::string& subject = s.camera.move.subject.name;
            if (emphasis <= 0.0f) {
                continue;
            }
            if (subject.empty()) {
                result.warnings.push_back(fmt::format(
                    "shot '{}' spotlights an unnamed subject, so nothing can be given its level of "
                    "detail; name the subject after the node it is",
                    s.name));
                continue;
            }
            const float factor = 1.0f - emphasis;
            for (const char* suffix : {"lod/minScreenRadius", "lod/distance1", "lod/distance2",
                                       "lod/distance3"}) {
                // The node's own procedural parameter block. When the subject is not a procedural
                // node the track resolves to nothing, which `seq::install` reports as an unresolved
                // target -- visible, rather than the silent no-op ADR-075 exists to record.
                const std::string path = "procedural/" + subject + "/" + suffix;
                builder.keyN(path, -1, params::TrackMode::Multiply, 0.0, scalar(1.0f),
                             params::KeyInterp::Step);
                builder.keyN(path, -1, params::TrackMode::Multiply, s.startSeconds, scalar(factor),
                             params::KeyInterp::Step);
                builder.keyN(path, -1, params::TrackMode::Multiply, s.endSeconds(), scalar(1.0f),
                             params::KeyInterp::Step);
            }
        }
    }

    // ---- events (section 17) ------------------------------------------------------------------
    //
    // Last, because an overlay event addresses a layer the sink has just issued a path for, and a
    // parameter event that ramps needs to see the keys the shots already wrote in order to know
    // what it is ramping from. Resolution is pure and the fold over it is pure, so the baked tier
    // of the event system is exactly as scrub-safe as the rest of this function -- which is the
    // point, and the argument is in seq/events.hpp.
    result.events = resolveEvents(events, triggerContext(options.beatsPerBar));
    for (const std::string& w : result.events.warnings) {
        result.warnings.push_back(w);
    }
    bakeEventFirings(*this, result.events, builder, sink, result.overlays, result.warnings);
    if (!result.events.dispatches.empty()) {
        result.warnings.push_back(fmt::format(
            "{} event firing(s) act on a live system and cannot be baked; forward play delivers "
            "each once and a seek restores the latest standing one per target (ADR-098)",
            result.events.dispatches.size()));
    }

    // ---- finish -------------------------------------------------------------------------------
    std::vector<params::Track> baked = builder.take();
    for (const auto& t : baked) {
        result.keyCount += static_cast<int>(t.keys.size());
        result.targets.push_back(t.target);
    }
    result.trackCount = static_cast<int>(baked.size());
    std::sort(result.targets.begin(), result.targets.end());
    result.targets.erase(std::unique(result.targets.begin(), result.targets.end()),
                         result.targets.end());
    result.timeline = json{{"enabled", true},
                           {"tracks", tracksToJson(std::move(baked))},
                           {"cues", json::array()}};
    return result;
}

// ---- snapping (spec 21) -----------------------------------------------------------------------

double snapTime(double seconds, SnapMode mode, std::span<const double> points, double fps,
                double toleranceSeconds) {
    switch (mode) {
    case SnapMode::Off:
        return seconds;
    case SnapMode::Frames: {
        if (!(fps > 0.0)) {
            return seconds;
        }
        const double snapped = std::round(seconds * fps) / fps;
        return toleranceSeconds > 0.0 && std::abs(snapped - seconds) > toleranceSeconds ? seconds
                                                                                       : snapped;
    }
    case SnapMode::Beats:
    case SnapMode::Markers:
        break;
    }
    if (points.empty()) {
        return seconds;
    }
    const auto it = std::lower_bound(points.begin(), points.end(), seconds);
    double best = points.back();
    if (it == points.begin()) {
        best = points.front();
    } else if (it == points.end()) {
        best = points.back();
    } else {
        const double after = *it;
        const double before = *(it - 1);
        best = (seconds - before) <= (after - seconds) ? before : after;
    }
    if (toleranceSeconds > 0.0 && std::abs(best - seconds) > toleranceSeconds) {
        return seconds;
    }
    return best;
}

// ---- JSON -------------------------------------------------------------------------------------

namespace {

json cameraToJson(const ShotCamera& cam) {
    json j{{"kind", cameraKindName(cam.kind)}, {"samples", cam.samples}};
    if (cam.kind == CameraKind::Move) {
        // The move is an app::Shot, which already has a serialiser covering fourteen kinds, five
        // look modes and every override. Writing a second one here would be a second thing to keep
        // in step with it.
        app::Sequence wrapper;
        wrapper.name = "move";
        wrapper.shots.push_back(cam.move);
        j["move"] = wrapper.toJson()["shots"][0];
    }
    if (cam.kind == CameraKind::Keys) {
        json keys = json::array();
        for (const auto& k : cam.keys) {
            json e{{"time", k.timeSeconds},
                   {"position", json::array({k.position.x, k.position.y, k.position.z})},
                   {"target", json::array({k.target.x, k.target.y, k.target.z})},
                   {"interp", params::keyInterpName(k.interp)}};
            if (k.focalLength > 0.0f) {
                e["focalLength"] = k.focalLength;
            }
            keys.push_back(std::move(e));
        }
        j["keys"] = std::move(keys);
    }
    if (cam.kind == CameraKind::Behavior) {
        const CameraBehavior& b = cam.behavior;
        json e{{"kind", cameraBehaviorName(b.kind)},
               {"actor", b.actor},
               {"aim", cameraAimName(b.aim)},
               {"aimOffset", json::array({b.aimOffset.x, b.aimOffset.y, b.aimOffset.z})}};
        // Only what the behaviour in hand actually reads. A chase's orbit radius is not a fact about
        // the chase, and writing one would invite somebody to edit it and wonder why nothing moved.
        switch (b.kind) {
        case CameraBehaviorKind::Chase:
            e["offset"] = json::array({b.offset.x, b.offset.y, b.offset.z});
            e["actorSpace"] = b.actorSpace;
            e["lagSeconds"] = b.lagSeconds;
            break;
        case CameraBehaviorKind::Orbit:
            e["radius"] = b.radius;
            e["height"] = b.height;
            e["startDegrees"] = b.startDegrees;
            e["endDegrees"] = b.endDegrees;
            e["easeInOut"] = b.easeInOut;
            break;
        case CameraBehaviorKind::Pov:
            e["eyeOffset"] = json::array({b.eyeOffset.x, b.eyeOffset.y, b.eyeOffset.z});
            break;
        }
        if (b.aim == CameraAim::Custom) {
            e["aimPoint"] = json::array({b.aimPoint.x, b.aimPoint.y, b.aimPoint.z});
        }
        if (b.lookAheadSeconds > 0.0) {
            e["lookAheadSeconds"] = b.lookAheadSeconds;
        }
        if (b.clearance > 0.0f) {
            e["clearance"] = b.clearance;
        }
        j["behavior"] = std::move(e);
    }
    if (!cam.lookAtActor.empty()) {
        j["lookAtActor"] = cam.lookAtActor;
        j["lookAtHeight"] = cam.lookAtHeight;
        j["lookAtWeight"] = cam.lookAtWeight;
    }
    return j;
}

Result<ShotCamera> cameraFromJson(const json& j) {
    ShotCamera cam;
    if (!j.is_object()) {
        return fail("shot camera must be an object");
    }
    if (const auto k = j.find("kind"); k != j.end() && k->is_string()) {
        const auto parsed = cameraKindFromName(k->get<std::string>());
        if (!parsed) {
            return fail("unknown camera kind '{}'", k->get<std::string>());
        }
        cam.kind = *parsed;
    }
    cam.samples = static_cast<int>(readNumber(j, "samples", cam.samples));
    if (const auto m = j.find("move"); m != j.end()) {
        json wrapper{{"name", "move"}, {"shots", json::array({*m})}};
        auto parsed = app::Sequence::fromJson(wrapper);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        if (!parsed->shots.empty()) {
            cam.move = parsed->shots.front();
        }
    }
    if (const auto bj = j.find("behavior"); bj != j.end()) {
        if (!bj->is_object()) {
            return fail("shot camera 'behavior' must be an object");
        }
        CameraBehavior& b = cam.behavior;
        if (const auto k = bj->find("kind"); k != bj->end() && k->is_string()) {
            const auto parsed = cameraBehaviorFromName(k->get<std::string>());
            if (!parsed) {
                return fail("unknown camera behaviour '{}'", k->get<std::string>());
            }
            b.kind = *parsed;
        }
        if (const auto a = bj->find("aim"); a != bj->end() && a->is_string()) {
            const auto parsed = cameraAimFromName(a->get<std::string>());
            if (!parsed) {
                return fail("unknown camera aim '{}'", a->get<std::string>());
            }
            b.aim = *parsed;
        }
        b.actor = bj->value("actor", b.actor);
        b.offset = readVec3(*bj, "offset", b.offset);
        b.actorSpace = bj->value("actorSpace", b.actorSpace);
        b.lagSeconds = readNumber(*bj, "lagSeconds", b.lagSeconds);
        b.radius = static_cast<float>(readNumber(*bj, "radius", b.radius));
        b.height = static_cast<float>(readNumber(*bj, "height", b.height));
        b.startDegrees = static_cast<float>(readNumber(*bj, "startDegrees", b.startDegrees));
        b.endDegrees = static_cast<float>(readNumber(*bj, "endDegrees", b.endDegrees));
        b.easeInOut = bj->value("easeInOut", b.easeInOut);
        b.eyeOffset = readVec3(*bj, "eyeOffset", b.eyeOffset);
        b.aimOffset = readVec3(*bj, "aimOffset", b.aimOffset);
        b.aimPoint = readVec3(*bj, "aimPoint", b.aimPoint);
        b.lookAheadSeconds = readNumber(*bj, "lookAheadSeconds", b.lookAheadSeconds);
        b.clearance = static_cast<float>(readNumber(*bj, "clearance", b.clearance));
    }
    if (const auto keys = j.find("keys"); keys != j.end()) {
        if (!keys->is_array()) {
            return fail("shot camera 'keys' must be an array");
        }
        for (const auto& e : *keys) {
            if (!e.is_object()) {
                return fail("every camera key must be an object");
            }
            CameraKey k;
            k.timeSeconds = readNumber(e, "time", 0.0);
            k.position = readVec3(e, "position", glm::vec3(0.0f));
            k.target = readVec3(e, "target", glm::vec3(0.0f));
            k.focalLength = static_cast<float>(readNumber(e, "focalLength", 0.0));
            if (const auto i = e.find("interp"); i != e.end() && i->is_string()) {
                const auto parsed = params::keyInterpFromName(i->get<std::string>());
                if (!parsed) {
                    return fail("unknown key interpolation '{}'", i->get<std::string>());
                }
                k.interp = *parsed;
            }
            cam.keys.push_back(k);
        }
    }
    cam.lookAtActor = readString(j, "lookAtActor");
    cam.lookAtHeight = static_cast<float>(readNumber(j, "lookAtHeight", static_cast<double>(cam.lookAtHeight)));
    cam.lookAtWeight = static_cast<float>(readNumber(j, "lookAtWeight", static_cast<double>(cam.lookAtWeight)));
    return cam;
}

json actorToJson(const Actor& a) {
    json keys = json::array();
    for (const auto& k : a.keys) {
        json e{{"time", k.timeSeconds},
               {"position", json::array({k.position.x, k.position.y, k.position.z})},
               {"interp", params::keyInterpName(k.interp)}};
        if (k.rotationDegrees) {
            e["rotation"] = json::array({k.rotationDegrees->x, k.rotationDegrees->y, k.rotationDegrees->z});
        }
        if (k.scale) {
            e["scale"] = json::array({k.scale->x, k.scale->y, k.scale->z});
        }
        keys.push_back(std::move(e));
    }
    json clips = json::array();
    for (const auto& c : a.clips) {
        json e{{"time", c.timeSeconds}, {"clip", c.clip}, {"speed", c.speed}};
        if (c.blendSeconds >= 0.0f) {
            e["blend"] = c.blendSeconds;
        }
        clips.push_back(std::move(e));
    }
    json j{{"id", a.id},
           {"node", a.node},
           {"visible", a.visible},
           {"keys", std::move(keys)},
           {"clips", std::move(clips)}};
    if (a.path.active) {
        j["path"] = json{{"spline", a.path.spline.toJson()},
                         {"start", a.path.startSeconds},
                         {"end", a.path.endSeconds},
                         {"samples", a.path.samples},
                         {"faceTangent", a.path.faceTangent},
                         {"startU", a.path.startU},
                         {"endU", a.path.endU}};
    }
    return j;
}

Result<Actor> actorFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("actor must be an object");
    }
    Actor a;
    a.id = readString(j, "id");
    if (a.id.empty()) {
        return fail("actor is missing a string 'id'");
    }
    a.node = readString(j, "node");
    if (const auto v = j.find("visible"); v != j.end() && v->is_boolean()) {
        a.visible = v->get<bool>();
    }
    if (const auto keys = j.find("keys"); keys != j.end()) {
        if (!keys->is_array()) {
            return fail("actor '{}': 'keys' must be an array", a.id);
        }
        for (const auto& e : *keys) {
            ActorKey k;
            k.timeSeconds = readNumber(e, "time", 0.0);
            k.position = readVec3(e, "position", glm::vec3(0.0f));
            if (e.contains("rotation")) {
                k.rotationDegrees = readVec3(e, "rotation", glm::vec3(0.0f));
            }
            if (e.contains("scale")) {
                k.scale = readVec3(e, "scale", glm::vec3(1.0f));
            }
            if (const auto i = e.find("interp"); i != e.end() && i->is_string()) {
                const auto parsed = params::keyInterpFromName(i->get<std::string>());
                if (!parsed) {
                    return fail("actor '{}': unknown key interpolation '{}'", a.id,
                                i->get<std::string>());
                }
                k.interp = *parsed;
            }
            a.keys.push_back(k);
        }
        std::stable_sort(a.keys.begin(), a.keys.end(),
                         [](const ActorKey& x, const ActorKey& y) { return x.timeSeconds < y.timeSeconds; });
    }
    if (const auto clips = j.find("clips"); clips != j.end()) {
        if (!clips->is_array()) {
            return fail("actor '{}': 'clips' must be an array", a.id);
        }
        for (const auto& e : *clips) {
            ClipCue c;
            c.timeSeconds = readNumber(e, "time", 0.0);
            c.clip = readString(e, "clip");
            c.speed = static_cast<float>(readNumber(e, "speed", 1.0));
            c.blendSeconds = static_cast<float>(readNumber(e, "blend", -1.0));
            a.clips.push_back(c);
        }
        std::stable_sort(a.clips.begin(), a.clips.end(),
                         [](const ClipCue& x, const ClipCue& y) { return x.timeSeconds < y.timeSeconds; });
    }
    if (const auto p = j.find("path"); p != j.end()) {
        if (!p->is_object()) {
            return fail("actor '{}': 'path' must be an object", a.id);
        }
        const auto spline = p->find("spline");
        if (spline == p->end()) {
            return fail("actor '{}': path has no spline", a.id);
        }
        auto parsed = spatial::Spline::fromJson(*spline);
        if (!parsed) {
            return fail("actor '{}' path: {}", a.id, parsed.error().message);
        }
        a.path.spline = std::move(*parsed);
        a.path.active = true;
        a.path.startSeconds = readNumber(*p, "start", 0.0);
        a.path.endSeconds = readNumber(*p, "end", 0.0);
        a.path.samples = static_cast<int>(readNumber(*p, "samples", a.path.samples));
        a.path.startU = static_cast<float>(readNumber(*p, "startU", 0.0));
        a.path.endU = static_cast<float>(readNumber(*p, "endU", 1.0));
        if (const auto f = p->find("faceTangent"); f != p->end() && f->is_boolean()) {
            a.path.faceTangent = f->get<bool>();
        }
    }
    return a;
}

} // namespace

json Sequence::toJson() const {
    json scenesJson = json::array();
    for (const auto& s : scenes) {
        scenesJson.push_back(json{{"id", s.id}, {"node", s.node}, {"file", s.file}});
    }
    json shotsJson = json::array();
    for (const auto& s : shots) {
        json j{{"name", s.name},
               {"start", s.startSeconds},
               {"duration", s.durationSeconds},
               {"scene", s.scene},
               {"camera", cameraToJson(s.camera)},
               {"in", json{{"kind", transitionKindName(s.in.kind)}, {"seconds", s.in.seconds}}},
               {"out", json{{"kind", transitionKindName(s.out.kind)}, {"seconds", s.out.seconds}}}};
        if (!s.tracks.empty()) {
            j["tracks"] = tracksToJson(s.tracks);
        }
        // Written only for a directed shot, so a hand-authored project gains nothing it did not
        // have, and the absence of the key means what it looks like it means.
        shotsJson.push_back(std::move(j));
    }
    json actorsJson = json::array();
    for (const auto& a : actors) {
        actorsJson.push_back(actorToJson(a));
    }
    json overlaysJson = json::array();
    for (const auto& o : overlays) {
        overlaysJson.push_back(o.toJson());
    }
    json markersJson = json::array();
    for (const auto& m : markers) {
        // Beat markers are copied from the analysis every time a track is loaded, so writing
        // thousands of them into a project would be storing a derived value -- and a stale one the
        // moment somebody changes the audio.
        if (m.kind == MarkerKind::Beat) {
            continue;
        }
        markersJson.push_back(
            json{{"time", m.timeSeconds}, {"name", m.name}, {"kind", markerKindName(m.kind)}});
    }
    json j{{"name", name},
           {"duration", durationSeconds},
           {"scenes", std::move(scenesJson)},
           {"shots", std::move(shotsJson)},
           {"actors", std::move(actorsJson)},
           {"overlays", std::move(overlaysJson)},
           {"markers", std::move(markersJson)}};
    // ADR-215. Written only when there is one, so a project that was never analyzed keeps the file
    // it had -- and, unlike the beat markers above, this is *not* derived: it is where a person's
    // boundaries and names live, and dropping it would be the data loss the whole model prevents.
    if (!structure.sections.empty()) {
        j["structure"] = songStructureToJson(structure);
    }
    // ADR-247. Two separate blocks, both written only when they hold something, so a project that
    // never analyzed a song and never defined a section type keeps exactly the file it had.
    if (!sectionTimeline.sections.empty()) {
        j["sectionTimeline"] = song::sectionTimelineToJson(sectionTimeline);
    }
    if (!sectionPerformance.empty()) {
        // ADR-216. Written only when authored, so a project nobody has given a director table to
        // does not grow an empty array on every save.
        j["sectionPerformance"] = ::avgen::seq::toJson(sectionPerformance);
    }
    if (shotLanguage.customized()) {
        j["shotLanguage"] = shotLanguage.toJson();
    }
    if (!events.empty()) {
        json eventsJson = json::array();
        for (const auto& e : events) {
            eventsJson.push_back(e.toJson());
        }
        j["events"] = std::move(eventsJson);
    }
    if (!tracks.empty()) {
        j["tracks"] = tracksToJson(tracks);
    }
    return j;
}

Result<Sequence> Sequence::fromJson(const json& j) {
    if (!j.is_object()) {
        return fail("sequence must be an object");
    }
    Sequence seq;
    seq.name = readString(j, "name");
    if (seq.name.empty()) {
        seq.name = "sequence";
    }
    seq.durationSeconds = readNumber(j, "duration", 0.0);
    if (const auto scenes = j.find("scenes"); scenes != j.end()) {
        if (!scenes->is_array()) {
            return fail("sequence '{}': 'scenes' must be an array", seq.name);
        }
        for (const auto& e : *scenes) {
            if (!e.is_object()) {
                return fail("sequence '{}': every scene slot must be an object", seq.name);
            }
            seq.scenes.push_back(SceneSlot{readString(e, "id"), readString(e, "node"), readString(e, "file")});
        }
    }
    if (const auto actors = j.find("actors"); actors != j.end()) {
        if (!actors->is_array()) {
            return fail("sequence '{}': 'actors' must be an array", seq.name);
        }
        for (const auto& e : *actors) {
            auto actor = actorFromJson(e);
            if (!actor) {
                return std::unexpected(actor.error());
            }
            seq.actors.push_back(std::move(*actor));
        }
    }
    if (const auto shots = j.find("shots"); shots != j.end()) {
        if (!shots->is_array()) {
            return fail("sequence '{}': 'shots' must be an array", seq.name);
        }
        for (const auto& e : *shots) {
            if (!e.is_object()) {
                return fail("sequence '{}': every shot must be an object", seq.name);
            }
            Shot s;
            s.name = readString(e, "name");
            s.startSeconds = readNumber(e, "start", 0.0);
            s.durationSeconds = readNumber(e, "duration", 8.0);
            s.scene = readString(e, "scene");
            if (const auto cam = e.find("camera"); cam != e.end()) {
                auto parsed = cameraFromJson(*cam);
                if (!parsed) {
                    return fail("sequence '{}' shot '{}': {}", seq.name, s.name,
                                parsed.error().message);
                }
                s.camera = std::move(*parsed);
            }
            const auto readTransition = [&](const char* key, Transition& out) -> Result<void> {
                const auto t = e.find(key);
                if (t == e.end()) {
                    return {};
                }
                if (!t->is_object()) {
                    return fail("sequence '{}' shot '{}': '{}' must be an object", seq.name, s.name, key);
                }
                const std::string kind = readString(*t, "kind");
                if (!kind.empty()) {
                    const auto parsed = transitionKindFromName(kind);
                    if (!parsed) {
                        return fail("sequence '{}' shot '{}': unknown transition '{}'", seq.name,
                                    s.name, kind);
                    }
                    out.kind = *parsed;
                }
                out.seconds = readNumber(*t, "seconds", out.seconds);
                return {};
            };
            if (auto ok = readTransition("in", s.in); !ok) {
                return std::unexpected(ok.error());
            }
            if (auto ok = readTransition("out", s.out); !ok) {
                return std::unexpected(ok.error());
            }
            if (const auto t = e.find("tracks"); t != e.end()) {
                s.tracks = tracksFromJson(*t, "shot");
                if (s.tracks.empty() && t->is_array() && !t->empty()) {
                    return fail("sequence '{}' shot '{}': 'tracks' could not be read", seq.name, s.name);
                }
            }
            seq.shots.push_back(std::move(s));
        }
    }
    if (const auto overlays = j.find("overlays"); overlays != j.end()) {
        if (!overlays->is_array()) {
            return fail("sequence '{}': 'overlays' must be an array", seq.name);
        }
        for (const auto& e : *overlays) {
            auto cue = OverlayCue::fromJson(e);
            if (!cue) {
                return std::unexpected(cue.error());
            }
            seq.overlays.push_back(std::move(*cue));
        }
    }
    if (const auto markers = j.find("markers"); markers != j.end()) {
        if (!markers->is_array()) {
            return fail("sequence '{}': 'markers' must be an array", seq.name);
        }
        for (const auto& e : *markers) {
            Marker m;
            m.timeSeconds = readNumber(e, "time", 0.0);
            m.name = readString(e, "name");
            const std::string kind = readString(e, "kind");
            if (!kind.empty()) {
                const auto parsed = markerKindFromName(kind);
                if (!parsed) {
                    return fail("sequence '{}': unknown marker kind '{}'", seq.name, kind);
                }
                m.kind = *parsed;
            }
            seq.markers.push_back(std::move(m));
        }
    }
    if (const auto st = j.find("structure"); st != j.end()) {
        auto parsed = songStructureFromJson(*st);
        if (!parsed) {
            return fail("sequence '{}': {}", seq.name, parsed.error().message);
        }
        seq.structure = std::move(*parsed);
    }
    // ADR-247. The language is read *before* the timeline, because a section may name a type this
    // project defined and nothing else knows about.
    if (const auto sl = j.find("shotLanguage"); sl != j.end()) {
        auto parsed = song::ShotLanguage::fromJson(*sl);
        if (!parsed) {
            return fail("sequence '{}': {}", seq.name, parsed.error().message);
        }
        seq.shotLanguage = std::move(*parsed);
    }
    // ADR-216's performer table. One key, no legacy spelling: this project is in heavy development
    // and does not carry compatibility shims -- the one example that used the old name was migrated
    // in the same commit that renamed it.
    if (const auto sp = j.find("sectionPerformance"); sp != j.end()) {
        auto parsed = sectionPerformanceSetFromJson(*sp);
        if (!parsed) {
            return fail("sequence '{}': {}", seq.name, parsed.error().message);
        }
        seq.sectionPerformance = std::move(*parsed);
    }
    if (const auto stl = j.find("sectionTimeline"); stl != j.end()) {
        auto parsed = song::sectionTimelineFromJson(*stl);
        if (!parsed) {
            return fail("sequence '{}': {}", seq.name, parsed.error().message);
        }
        seq.sectionTimeline = std::move(*parsed);
    } else if (!seq.structure.sections.empty()) {
        // Migration, and the only place the two models are allowed to touch. A project saved before
        // the shot language existed has an analysis and no film; deriving one from the other gives
        // it a complete first-pass treatment on open, and cannot lose anything because there was
        // nothing authored to lose. A project that *has* a timeline is never re-derived -- that
        // would be the re-analysis this whole model exists to make safe, performed silently on load.
        seq.sectionTimeline = song::timelineFromStructure(seq.structure, seq.shotLanguage);
    }
    if (const auto ev = j.find("events"); ev != j.end()) {
        if (!ev->is_array()) {
            return fail("sequence '{}': 'events' must be an array", seq.name);
        }
        for (const auto& e : *ev) {
            if (!e.is_object()) {
                return fail("sequence '{}': every event must be an object", seq.name);
            }
            seq.events.push_back(SequenceEvent::fromJson(e));
        }
    }
    if (const auto t = j.find("tracks"); t != j.end()) {
        seq.tracks = tracksFromJson(*t, "sequence");
        if (seq.tracks.empty() && t->is_array() && !t->empty()) {
            return fail("sequence '{}': 'tracks' could not be read", seq.name);
        }
    }
    if (auto ok = seq.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return seq;
}

} // namespace avgen::seq
