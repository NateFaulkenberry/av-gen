#include "seq/events.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace avgen::seq {
namespace {
using nlohmann::json;

constexpr std::array<std::pair<TriggerKind, const char*>, 12> kTriggerKinds{{
    {TriggerKind::Time, "time"},
    {TriggerKind::Beat, "beat"},
    {TriggerKind::Bar, "bar"},
    {TriggerKind::Section, "section"},
    {TriggerKind::ShotStart, "shotStart"},
    {TriggerKind::ShotEnd, "shotEnd"},
    {TriggerKind::Cue, "cue"},
    {TriggerKind::ClipEnd, "clipEnd"},
    {TriggerKind::ActionComplete, "actionComplete"},
    {TriggerKind::InteractionComplete, "interactionComplete"},
    {TriggerKind::VolumeEnter, "volumeEnter"},
    {TriggerKind::VolumeExit, "volumeExit"},
}};

constexpr std::array<std::pair<EventActionKind, const char*>, 8> kActionKinds{{
    {EventActionKind::SetParameter, "setParameter"},
    {EventActionKind::CameraShake, "cameraShake"},
    {EventActionKind::PlayClip, "playClip"},
    {EventActionKind::Overlay, "overlay"},
    {EventActionKind::SceneTransition, "sceneTransition"},
    {EventActionKind::EntityAction, "entityAction"},
    {EventActionKind::Notify, "notify"},
    {EventActionKind::CharacterGoal, "characterGoal"},
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
std::optional<E> valueOf(const std::array<std::pair<E, const char*>, N>& table,
                         std::string_view name) {
    for (const auto& [e, n] : table) {
        if (name == n) {
            return e;
        }
    }
    return std::nullopt;
}

double readNumber(const json& j, const char* key, double fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<double>() : fallback;
}

int readInt(const json& j, const char* key, int fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<int>() : fallback;
}

std::string readString(const json& j, const char* key) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

bool readBool(const json& j, const char* key, bool fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

glm::vec4 readVec4(const json& j, const char* key) {
    glm::vec4 out(0.0f);
    const auto it = j.find(key);
    if (it == j.end()) {
        return out;
    }
    if (it->is_number()) {
        out.x = it->get<float>();
        return out;
    }
    if (it->is_array()) {
        for (std::size_t i = 0; i < std::min<std::size_t>(4, it->size()); ++i) {
            if ((*it)[i].is_number()) {
                out[static_cast<glm::length_t>(i)] = (*it)[i].get<float>();
            }
        }
    }
    return out;
}

// A trigger's window is an *optional filter*, not a default clip. An untouched window admits
// everything, including an event an author deliberately placed past the sequence's derived
// duration: clamping to the piece length would have made "set this at 0:42" in a piece the shots
// only fill to 0:16 vanish without a word, which is the class of failure ADR-075 exists to stop.
bool windowed(const Trigger& t) { return t.fromSeconds > 0.0 || t.toSeconds > t.fromSeconds; }

bool inWindow(const Trigger& t, double seconds) {
    if (!windowed(t)) {
        return true;
    }
    const double to = t.toSeconds > t.fromSeconds ? t.toSeconds
                                                  : std::numeric_limits<double>::infinity();
    return seconds >= t.fromSeconds - 1e-9 && seconds <= to + 1e-9;
}

bool matches(const std::string& pattern, const std::string& candidate) {
    return pattern.empty() || pattern == candidate;
}

} // namespace

const char* triggerKindName(TriggerKind kind) { return nameOf(kTriggerKinds, kind); }
std::optional<TriggerKind> triggerKindFromName(std::string_view name) {
    return valueOf(kTriggerKinds, name);
}
const char* eventActionKindName(EventActionKind kind) { return nameOf(kActionKinds, kind); }
std::optional<EventActionKind> eventActionKindFromName(std::string_view name) {
    return valueOf(kActionKinds, name);
}

bool triggerIsScheduled(TriggerKind kind) {
    switch (kind) {
    case TriggerKind::Time:
    case TriggerKind::Beat:
    case TriggerKind::Bar:
    case TriggerKind::Section:
    case TriggerKind::ShotStart:
    case TriggerKind::ShotEnd:
    case TriggerKind::Cue:
    case TriggerKind::ClipEnd:
        return true;
    case TriggerKind::ActionComplete:
    case TriggerKind::InteractionComplete:
    case TriggerKind::VolumeEnter:
    case TriggerKind::VolumeExit:
        return false;
    }
    return false;
}

bool actionIsBaked(EventActionKind kind) {
    switch (kind) {
    case EventActionKind::SetParameter:
    case EventActionKind::CameraShake:
    case EventActionKind::PlayClip:
    case EventActionKind::Overlay:
    case EventActionKind::SceneTransition:
        return true;
    case EventActionKind::EntityAction:
    case EventActionKind::Notify:
    case EventActionKind::CharacterGoal:
        return false;
    }
    return false;
}

// ---- JSON --------------------------------------------------------------------------------------

json Trigger::toJson() const {
    json j{{"kind", triggerKindName(kind)}};
    if (kind == TriggerKind::Time) {
        j["time"] = timeSeconds;
    }
    if (kind == TriggerKind::Beat || kind == TriggerKind::Bar) {
        j["every"] = every;
        j["index"] = index;
    }
    if (!name.empty()) {
        j["name"] = name;
    }
    if (!subject.empty()) {
        j["subject"] = subject;
    }
    if (fromSeconds != 0.0) {
        j["from"] = fromSeconds;
    }
    if (toSeconds != 0.0) {
        j["to"] = toSeconds;
    }
    if (delaySeconds != 0.0) {
        j["delay"] = delaySeconds;
    }
    if (repeat != 0) {
        j["repeat"] = repeat;
    }
    return j;
}

Trigger Trigger::fromJson(const json& j) {
    Trigger t;
    if (!j.is_object()) {
        return t;
    }
    if (auto kind = triggerKindFromName(readString(j, "kind"))) {
        t.kind = *kind;
    }
    t.timeSeconds = readNumber(j, "time", 0.0);
    t.every = std::max(1, readInt(j, "every", 1));
    t.index = readInt(j, "index", 0);
    t.name = readString(j, "name");
    t.subject = readString(j, "subject");
    t.fromSeconds = readNumber(j, "from", 0.0);
    t.toSeconds = readNumber(j, "to", 0.0);
    t.delaySeconds = readNumber(j, "delay", 0.0);
    t.repeat = readInt(j, "repeat", 0);
    return t;
}

json EventAction::toJson() const {
    json j{{"kind", eventActionKindName(kind)}};
    if (!target.empty()) {
        j["target"] = target;
    }
    if (!value.empty()) {
        j["value"] = value;
    }
    if (!argument.empty()) {
        j["argument"] = argument;
    }
    j["amount"] = json::array({amount.x, amount.y, amount.z, amount.w});
    if (component >= 0) {
        j["component"] = component;
    }
    if (seconds != 0.0) {
        j["seconds"] = seconds;
    }
    if (holdSeconds != 0.0) {
        j["hold"] = holdSeconds;
    }
    j["interp"] = params::keyInterpName(interp);
    j["mode"] = params::trackModeName(mode);
    if (kind == EventActionKind::CharacterGoal) {
        json g = json::object();
        if (!goal.intent.empty()) {
            g["intent"] = goal.intent;
        }
        if (!goal.activity.empty()) {
            g["activity"] = goal.activity;
        }
        if (goal.approach >= 0.0f) {
            g["approach"] = goal.approach;
        }
        if (goal.dwell >= 0.0f) {
            g["dwell"] = goal.dwell;
        }
        j["goal"] = std::move(g);
    }
    return j;
}

EventAction EventAction::fromJson(const json& j) {
    EventAction a;
    if (!j.is_object()) {
        return a;
    }
    if (auto kind = eventActionKindFromName(readString(j, "kind"))) {
        a.kind = *kind;
    }
    a.target = readString(j, "target");
    a.value = readString(j, "value");
    a.argument = readString(j, "argument");
    a.amount = readVec4(j, "amount");
    a.component = readInt(j, "component", -1);
    a.seconds = readNumber(j, "seconds", 0.0);
    a.holdSeconds = readNumber(j, "hold", 0.0);
    if (auto interp = params::keyInterpFromName(readString(j, "interp"))) {
        a.interp = *interp;
    }
    if (auto mode = params::trackModeFromName(readString(j, "mode"))) {
        a.mode = *mode;
    }
    if (const auto g = j.find("goal"); g != j.end() && g->is_object()) {
        a.goal.intent = readString(*g, "intent");
        a.goal.activity = readString(*g, "activity");
        a.goal.approach = static_cast<float>(readNumber(*g, "approach", -1.0));
        a.goal.dwell = static_cast<float>(readNumber(*g, "dwell", -1.0));
    }
    return a;
}

json SequenceEvent::toJson() const {
    json j{{"id", id}, {"when", when.toJson()}, {"what", what.toJson()}};
    if (!enabled) {
        j["enabled"] = false;
    }
    if (priority != 0) {
        j["priority"] = priority;
    }
    return j;
}

SequenceEvent SequenceEvent::fromJson(const json& j) {
    SequenceEvent e;
    if (!j.is_object()) {
        return e;
    }
    e.id = readString(j, "id");
    if (const auto it = j.find("when"); it != j.end()) {
        e.when = Trigger::fromJson(*it);
    }
    if (const auto it = j.find("what"); it != j.end()) {
        e.what = EventAction::fromJson(*it);
    }
    e.enabled = readBool(j, "enabled", true);
    e.priority = readInt(j, "priority", 0);
    return e;
}

// ---- resolution --------------------------------------------------------------------------------

namespace {

// One occurrence of a trigger: the second it happens and what it was.
struct Occurrence {
    double timeSeconds = 0.0;
    std::string name;
};

// Every occurrence of a scheduled trigger, in time order. Pure in (trigger, context) -- which is
// what makes the whole baked tier pure, because the bake is a fold over this list.
std::vector<Occurrence> occurrencesOf(const Trigger& t, const TriggerContext& ctx,
                                      std::vector<std::string>& warnings, const std::string& id) {
    std::vector<Occurrence> out;
    const auto everyNth = [&](std::span<const double> times, const char* what) {
        if (times.empty()) {
            warnings.push_back(fmt::format(
                "event '{}' triggers on a {}, but the sequence carries no {} times -- run the "
                "analysis and set the markers, or the event never fires",
                id, what, what));
            return;
        }
        const int every = std::max(1, t.every);
        for (std::size_t i = 0; i < times.size(); ++i) {
            const int n = static_cast<int>(i) - t.index;
            if (n < 0 || n % every != 0) {
                continue;
            }
            out.push_back(Occurrence{times[i], std::to_string(i)});
        }
    };
    switch (t.kind) {
    case TriggerKind::Time:
        out.push_back(Occurrence{t.timeSeconds, t.name});
        break;
    case TriggerKind::Beat:
        everyNth(ctx.beatTimes, "beat");
        break;
    case TriggerKind::Bar:
        everyNth(ctx.barTimes, "bar");
        break;
    case TriggerKind::Section: {
        bool found = false;
        for (const auto& s : ctx.sections) {
            if (matches(t.name, s.name)) {
                out.push_back(Occurrence{s.startSeconds, s.name});
                found = true;
            }
        }
        if (!found && !t.name.empty()) {
            warnings.push_back(fmt::format(
                "event '{}' triggers on section '{}', which the structure does not contain", id,
                t.name));
        }
        break;
    }
    case TriggerKind::ShotStart:
    case TriggerKind::ShotEnd: {
        bool found = false;
        for (const auto& s : ctx.shots) {
            if (!matches(t.name, s.name)) {
                continue;
            }
            found = true;
            // Exactly one occurrence per shot per edge. A shot has one start and one end, so an
            // event on a shot edge fires once however the playhead reaches it -- the property the
            // brief's testing section asks for, and here it is a property of the list rather than
            // of a comparison somebody has to get right every frame.
            out.push_back(Occurrence{
                t.kind == TriggerKind::ShotStart ? s.startSeconds : s.endSeconds, s.name});
        }
        if (!found) {
            warnings.push_back(fmt::format("event '{}' triggers on shot '{}', which does not exist",
                                           id, t.name.empty() ? "<any>" : t.name));
        }
        break;
    }
    case TriggerKind::Cue: {
        bool found = false;
        for (const auto& [name, time] : ctx.cues) {
            if (matches(t.name, name)) {
                out.push_back(Occurrence{time, name});
                found = true;
            }
        }
        if (!found) {
            warnings.push_back(fmt::format("event '{}' triggers on cue '{}', which is not a marker",
                                           id, t.name.empty() ? "<any>" : t.name));
        }
        break;
    }
    case TriggerKind::ClipEnd: {
        for (const auto& c : ctx.clips) {
            if (!matches(t.subject, c.actor) || !matches(t.name, c.clip)) {
                continue;
            }
            if (c.endSeconds < 0.0) {
                // The sequence put the actor into this clip and never said when it stops. Only the
                // rig knows the clip's length, and the rig is not a fact about the piece -- so this
                // one occurrence drops to the live tier rather than being guessed at.
                warnings.push_back(fmt::format(
                    "event '{}': actor '{}' enters clip '{}' at {:.3f}s and nothing states when it "
                    "ends, so this trigger is live rather than scheduled",
                    id, c.actor, c.clip, c.startSeconds));
                continue;
            }
            out.push_back(Occurrence{c.endSeconds, c.clip});
        }
        break;
    }
    case TriggerKind::ActionComplete:
    case TriggerKind::InteractionComplete:
    case TriggerKind::VolumeEnter:
    case TriggerKind::VolumeExit:
        break;
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const Occurrence& a, const Occurrence& b) { return a.timeSeconds < b.timeSeconds; });
    return out;
}

void sortFirings(std::vector<Firing>& firings) {
    std::stable_sort(firings.begin(), firings.end(), [](const Firing& a, const Firing& b) {
        if (a.timeSeconds != b.timeSeconds) {
            return a.timeSeconds < b.timeSeconds;
        }
        if (a.priority != b.priority) {
            return a.priority < b.priority;
        }
        return a.eventIndex < b.eventIndex;
    });
}

} // namespace

EventSchedule resolveEvents(std::span<const SequenceEvent> events, const TriggerContext& context) {
    EventSchedule schedule;
    for (std::size_t i = 0; i < events.size(); ++i) {
        const SequenceEvent& e = events[i];
        if (!e.enabled) {
            continue;
        }
        const std::string id = e.id.empty() ? fmt::format("#{}", i) : e.id;
        const bool scheduled = triggerIsScheduled(e.when.kind);
        if (!scheduled) {
            schedule.live.push_back(i);
            continue;
        }
        std::vector<Occurrence> occurrences =
            occurrencesOf(e.when, context, schedule.warnings, id);
        // A ClipEnd whose every occurrence was unbounded is a live event, not an event that does
        // nothing. Everything else with no occurrences already warned.
        if (occurrences.empty() && e.when.kind == TriggerKind::ClipEnd) {
            schedule.live.push_back(i);
            continue;
        }
        int fired = 0;
        for (const Occurrence& o : occurrences) {
            if (!inWindow(e.when, o.timeSeconds)) {
                continue;
            }
            if (e.when.repeat > 0 && fired >= e.when.repeat) {
                break;
            }
            ++fired;
            const double time = o.timeSeconds + e.when.delaySeconds;
            Firing f{time, i, e.priority, o.name};
            if (actionIsBaked(e.what.kind)) {
                if (e.what.kind == EventActionKind::PlayClip) {
                    schedule.clips.push_back(ScheduledClip{.actor = e.what.target,
                                                           .clip = e.what.value,
                                                           .timeSeconds = time,
                                                           .speed = e.what.amount.x > 0.0f
                                                                        ? e.what.amount.x
                                                                        : 1.0f,
                                                           .blendSeconds = e.what.seconds > 0.0
                                                                               ? static_cast<float>(e.what.seconds)
                                                                               : -1.0f});
                }
                schedule.baked.push_back(std::move(f));
            } else {
                schedule.dispatches.push_back(std::move(f));
            }
        }
    }
    sortFirings(schedule.baked);
    sortFirings(schedule.dispatches);
    std::stable_sort(schedule.clips.begin(), schedule.clips.end(),
                     [](const ScheduledClip& a, const ScheduledClip& b) {
                         return a.timeSeconds < b.timeSeconds;
                     });
    return schedule;
}

// ---- the dispatcher ------------------------------------------------------------------------------

std::size_t EventDispatcher::LiveKeyHash::operator()(const LiveKey& k) const {
    const std::size_t a = std::hash<std::string>{}(k.name);
    const std::size_t b = static_cast<std::size_t>(k.kind);
    return a ^ (b * 0x9e3779b97f4a7c15ULL);
}

void EventDispatcher::setEvents(std::span<const SequenceEvent> events,
                                const EventSchedule& schedule) {
    events_.assign(events.begin(), events.end());
    dispatches_ = schedule.dispatches;
    liveIndex_.clear();
    for (const std::size_t index : schedule.live) {
        if (index >= events.size()) {
            continue;
        }
        // Indexed by (kind, name), so a signal naming a volume nobody listens to costs one failed
        // hash lookup. An event with an empty name listens to every signal of its kind and is filed
        // under the empty string, which the post() path looks up as a second, smaller bucket.
        liveIndex_[LiveKey{events[index].when.kind, events[index].when.name}].push_back(index);
    }
    pending_.clear();
    fired_.assign(events.size(), 0);
    playhead_ = 0.0;
    started_ = false;
}

void EventDispatcher::clear() {
    events_.clear();
    dispatches_.clear();
    liveIndex_.clear();
    pending_.clear();
    fired_.clear();
    playhead_ = 0.0;
    started_ = false;
}

int EventDispatcher::firedCount(std::size_t eventIndex) const {
    return eventIndex < fired_.size() ? fired_[eventIndex] : 0;
}

void EventDispatcher::queue(std::size_t eventIndex, double time, std::string subject,
                            bool restored) {
    if (eventIndex >= events_.size()) {
        return;
    }
    const SequenceEvent& e = events_[eventIndex];
    if (!e.enabled) {
        return;
    }
    if (e.when.repeat > 0 && firedCount(eventIndex) >= e.when.repeat) {
        return;
    }
    if (eventIndex < fired_.size()) {
        ++fired_[eventIndex];
    }
    pending_.push_back(FiredEvent{eventIndex, time, std::move(subject), restored});
}

void EventDispatcher::post(const TriggerSignal& signal) {
    const auto consider = [&](const LiveKey& key) {
        const auto it = liveIndex_.find(key);
        if (it == liveIndex_.end()) {
            return;
        }
        for (const std::size_t index : it->second) {
            const SequenceEvent& e = events_[index];
            if (!matches(e.when.subject, signal.subject)) {
                continue;
            }
            if (!inWindow(e.when, signal.timeSeconds)) {
                continue;
            }
            queue(index, signal.timeSeconds + e.when.delaySeconds, signal.subject, false);
        }
    };
    consider(LiveKey{signal.kind, signal.name});
    if (!signal.name.empty()) {
        consider(LiveKey{signal.kind, std::string{}}); // the listen-to-everything bucket
    }
}

void EventDispatcher::advanceTo(double nowSeconds) {
    if (dispatches_.empty()) {
        playhead_ = nowSeconds;
        started_ = true;
        return;
    }
    const bool step = started_ && nowSeconds >= playhead_ &&
                      nowSeconds - playhead_ <= continuitySeconds;
    if (step) {
        for (const Firing& f : dispatches_) {
            if (f.timeSeconds > playhead_ && f.timeSeconds <= nowSeconds) {
                queue(f.eventIndex, f.timeSeconds, f.occurrence, false);
            }
        }
        playhead_ = nowSeconds;
        return;
    }
    // A seek. Replaying every dispatch the jump flew over would be wrong twice: an entity cannot
    // perform sixty seconds of actions in one frame, and the ones it did perform would be in the
    // wrong order relative to the world's own state. What survives a jump is the *standing* intent:
    // the last thing each target was told to do. Everything else is discarded, and what is
    // delivered is marked so the host applies it as a state rather than as a beat.
    fired_.assign(events_.size(), 0);
    std::vector<std::pair<std::string, const Firing*>> standing;
    for (const Firing& f : dispatches_) {
        if (f.timeSeconds > nowSeconds) {
            break; // sorted
        }
        if (f.eventIndex >= events_.size()) {
            continue;
        }
        const EventAction& a = events_[f.eventIndex].what;
        const std::string key = std::string(eventActionKindName(a.kind)) + "/" + a.target;
        const auto it = std::find_if(standing.begin(), standing.end(),
                                     [&](const auto& e) { return e.first == key; });
        if (it == standing.end()) {
            standing.emplace_back(key, &f);
        } else {
            it->second = &f;
        }
    }
    for (const auto& [key, f] : standing) {
        queue(f->eventIndex, nowSeconds, f->occurrence, true);
    }
    playhead_ = nowSeconds;
    started_ = true;
}

std::vector<FiredEvent> EventDispatcher::drain(double nowSeconds) {
    std::vector<FiredEvent> out;
    std::vector<FiredEvent> held;
    out.reserve(pending_.size());
    for (FiredEvent& f : pending_) {
        if (f.timeSeconds <= nowSeconds + 1e-9) {
            out.push_back(std::move(f));
        } else {
            held.push_back(std::move(f));
        }
    }
    pending_ = std::move(held);
    std::stable_sort(out.begin(), out.end(), [&](const FiredEvent& a, const FiredEvent& b) {
        if (a.timeSeconds != b.timeSeconds) {
            return a.timeSeconds < b.timeSeconds;
        }
        const int pa = a.eventIndex < events_.size() ? events_[a.eventIndex].priority : 0;
        const int pb = b.eventIndex < events_.size() ? events_[b.eventIndex].priority : 0;
        if (pa != pb) {
            return pa < pb;
        }
        return a.eventIndex < b.eventIndex;
    });
    return out;
}

std::vector<FiredEvent> EventDispatcher::drain() {
    return drain(std::numeric_limits<double>::infinity());
}

void EventDispatcher::reset(double nowSeconds) {
    pending_.clear();
    fired_.assign(events_.size(), 0);
    playhead_ = nowSeconds;
    started_ = false;
}

} // namespace avgen::seq
