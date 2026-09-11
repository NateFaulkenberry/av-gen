#pragma once

// Cinematic events (brief section 17): "when X happens, do Y", for a sequencer whose entire
// determinism story rests on *not* doing things when they happen.
//
// ## The problem this file exists to solve
//
// `seq::Sequence` bakes (ADR-089). Shots, actors, transitions and overlay cues become ordinary
// `params::Track`s, and from then on a frame is `Track::evaluate(t)` and nothing else -- which is
// why scrubbing, replay and offline rendering are correct *by construction* rather than by the
// evaluator's own discipline. `Marker`s are labels: the header above them says so, deliberately.
//
// An event system is the opposite shape. It is imperative ("when the bar turns over, shake the
// camera"), it is naturally stateful ("has this fired yet?"), and the obvious implementation --
// walk the event list every frame and fire anything the playhead just crossed -- destroys the
// property the sequencer is built on. A scrub from 10s to 45s would fire thirty-five seconds of
// events in one frame, or none at all, depending on which side of the comparison somebody wrote.
// That is precisely the history dependence ADR-089 refused.
//
// ## The boundary
//
// So the question is not "how do events fire" but **how much of an event can stop being an event**.
// The answer turns out to be most of it, and the split is a property of the event, visible to the
// author before anything runs:
//
//   Tier 1 -- BAKED.  The *when* is knowable ahead of time **and** the *what* is a value over time.
//                     The event stops existing: it becomes keys. "Set the fog to 3 at 0:12" is a
//                     Step key at 12 s. Baking converts *fire once* into *hold from here*, and a
//                     value that holds cannot be skipped by a scrub or fired twice by one, because
//                     there is no firing left -- only an evaluation. This is where the brief's
//                     time, beat, bar, section, shot-start, shot-end and cue triggers live, driving
//                     parameter changes (which is also how a light, a material and a particle
//                     system change -- they are parameter paths), camera shake, clips, overlays and
//                     scene transitions.
//
//   Tier 2 -- SCHEDULED.  The *when* is knowable but the *what* is imperative: hand an action to an
//                     entity, tell the host something was selected. A track cannot carry "ask the
//                     action system to walk to the door". These have exact times, so forward play
//                     delivers each exactly once -- but they are **not** scrub-safe, and pretending
//                     otherwise would be the lie. What a seek does instead is *restore the standing
//                     intent*: the latest dispatch at or before the new playhead, per target, is
//                     re-delivered marked `restored`, and everything before it is dropped. That is
//                     ADR-091's resume-rather-than-reset rule applied to direction, and it is the
//                     most a live system can honestly offer.
//
//   Tier 3 -- LIVE.   The *when* is not knowable at all: an entity crossed a volume, an action
//                     finished. Nobody can compute that from the playhead, because the entity is in
//                     the live tier of ADR-091 and its position at t depends on how it got there.
//                     These are **posted** by whoever owns the fact -- the trigger-volume system
//                     computes an enter/exit edge already, and the action system knows when a queue
//                     drains -- and dispatched by key lookup. Nothing in this file polls an entity,
//                     and nothing in it walks the event list per frame.
//
// The rule in one line: **a trigger decides whether an event can be scheduled; an action decides
// whether it can be baked.** Both halves must be knowable for tier 1. `triggerIsScheduled()` and
// `actionIsBaked()` are pure predicates, so an editor can colour an event row without running it.
//
// ## What this is not
//
// It is not a second sequencer, a second animation system or a second modulation system. An event
// that changes a light, a material, a particle rate or the fog is one action kind -- SetParameter
// -- because in this engine all four are a parameter path, and inventing four verbs for one
// mechanism is the duplication brief section 48 warns about. An event that plays a clip produces a
// `ClipCue`, which is the object ADR-089 already invented for "a state and the second it started".
// An event that moves an overlay writes `layers/<id>/position`, through the same `LayerSink` seam
// the overlay cues already use. The only genuinely new runtime object is `EventDispatcher`, and it
// exists only for tiers 2 and 3.

#include "params/timeline.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace avgen::seq {

// ---- when --------------------------------------------------------------------------------------

// Every *when* the brief names. The first seven are facts about the piece and the analysis, both of
// which exist before the first frame; the last five are facts about a running world.
enum class TriggerKind : std::uint8_t {
    Time,                // an absolute second
    Beat,                // every Nth beat of the analysis
    Bar,                 // every Nth bar
    Section,             // a musical section begins (by name; empty = any)
    ShotStart,           // a named shot begins (empty = every shot)
    ShotEnd,             // ...ends
    Cue,                 // a `Marker` of kind Cue, by name
    ClipEnd,             // an actor's clip cue ends. Scheduled when the sequence bounds it (a
                         // following cue states the end); live when only the rig knows.
    ActionComplete,      // the action system finished an action on an entity
    InteractionComplete, // ...an interaction
    VolumeEnter,         // an entity entered a trigger volume
    VolumeExit,          // ...left one
};
[[nodiscard]] const char* triggerKindName(TriggerKind kind);
[[nodiscard]] std::optional<TriggerKind> triggerKindFromName(std::string_view name);

// True when a trigger of this kind resolves to times before the piece runs. The one honest place an
// author can look to know what a scrub will do to their event. `ClipEnd` answers true because it
// *can* be scheduled; whether a particular one was is reported per event by the resolver.
[[nodiscard]] bool triggerIsScheduled(TriggerKind kind);

struct Trigger {
    TriggerKind kind = TriggerKind::Time;
    double timeSeconds = 0.0; // Time
    // Beat/Bar: every `every`-th one, counting from `index`. every = 4, index = 0 is "every
    // downbeat"; every = 1 is "all of them".
    int every = 1;
    int index = 0;
    // The thing the trigger is about: a section name, a shot name, a cue marker's name, a volume
    // id, an action id, a clip name. Empty means "any of them", where that is meaningful.
    std::string name;
    // Who it is about: an actor id for ClipEnd, an entity name for the live kinds. Empty means
    // "anyone".
    std::string subject;
    // Restrict a repeating trigger to a span of the piece. `toSeconds` <= `fromSeconds` means "to
    // the end", which is also the default.
    double fromSeconds = 0.0;
    double toSeconds = 0.0;
    // Fire this long after the condition. Baked events move their keys; live ones are held.
    double delaySeconds = 0.0;
    // At most this many firings; 0 = unlimited.
    int repeat = 0;

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Trigger fromJson(const nlohmann::json& j);
};

// ---- what --------------------------------------------------------------------------------------

// Every *what* the brief names, folded onto the mechanisms that already exist. There is no
// SetLightIntensity, SetMaterialColor or SetParticleRate: `env/sky/sunIntensity`,
// `procedural/x/parts/2/emissiveColor` and `particles/y/rate` are parameter paths, and one verb
// that writes a parameter path covers all three plus everything added later.
enum class EventActionKind : std::uint8_t {
    SetParameter,    // BAKED. target = path, amount = value, seconds = ramp, holdSeconds = impulse
    CameraShake,     // BAKED. amount = (metres, hertz, degrees), seconds = decay
    PlayClip,        // BAKED. target = actor id, value = clip, amount.x = speed, seconds = blend
    Overlay,         // BAKED. target = cue id, value = property, amount = value, seconds = move
    SceneTransition, // BAKED. target = slot id, value = transition kind, seconds = duration
    EntityAction,    // SCHEDULED/LIVE. target = entity, value = verb, argument = the verb's object
    Notify,          // SCHEDULED/LIVE. target = a name the host knows; the fourth-wall selection
};
[[nodiscard]] const char* eventActionKindName(EventActionKind kind);
[[nodiscard]] std::optional<EventActionKind> eventActionKindFromName(std::string_view name);

// True when this action can become timeline keys (or a clip cue). The other half of the boundary.
[[nodiscard]] bool actionIsBaked(EventActionKind kind);

struct EventAction {
    EventActionKind kind = EventActionKind::SetParameter;
    std::string target;   // what it acts on
    std::string value;    // the verb, clip, property or transition kind, by name
    std::string argument; // the verb's own object, when it needs one (a door, an interaction id)
    glm::vec4 amount{0.0f};
    int component = -1;   // SetParameter: which component, -1 = all
    double seconds = 0.0; // ramp / move / decay / transition length; 0 = a step
    // > 0: return to the value in force before the event, this long after arriving. An impulse
    // expressed as two more keys rather than as a timer.
    double holdSeconds = 0.0;
    params::KeyInterp interp = params::KeyInterp::Smooth;
    // How the event's value meets the value already in force. This is not a detail: it is what
    // makes an event *a change* rather than *an assertion*.
    //
    // `Replace` writes the value, which is right when the event owns the property outright -- but a
    // track holds its first key's value backwards forever, so an event that replaces a value at
    // 12 s has also replaced it at 0 s unless something else stated the value before then. The bake
    // says so out loud rather than leaving it to be found in a render.
    //
    // `Add` and `Multiply` have no such problem, because their *identity* is known: a baseline key
    // of 0 (or 1) at t = 0 means the event contributes nothing until it fires and its delta
    // afterwards, whatever the scene said the value was. That also makes `holdSeconds` exact --
    // returning to the identity needs no knowledge of what was there before -- so an impulse
    // ("flash the lamp for a beat") is two keys and is correct under any scrub.
    params::TrackMode mode = params::TrackMode::Replace;

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static EventAction fromJson(const nlohmann::json& j);
};

// ---- the event -----------------------------------------------------------------------------------

struct SequenceEvent {
    std::string id; // stable; how a warning and a dispatch name it
    Trigger when;
    EventAction what;
    bool enabled = true;
    // Two events at the same instant fire lowest priority first, then in declaration order. Ties
    // are broken deterministically or the ordering test is a coin toss.
    int priority = 0;

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static SequenceEvent fromJson(const nlohmann::json& j);
};

// ---- resolution ----------------------------------------------------------------------------------

// Everything knowable before the piece runs, flattened to spans and times. Deliberately plain data
// rather than a reference to the `Sequence`: the resolver is then testable on its own, and this
// header does not have to include the one that includes it.
struct TriggerContext {
    struct NamedSpan {
        std::string name;
        double startSeconds = 0.0;
        double endSeconds = 0.0;
    };
    struct ClipSpan {
        std::string actor;
        std::string clip;
        double startSeconds = 0.0;
        // The second the clip stops being the actor's state, which the sequence knows only when a
        // following cue states it. Negative means "the sequence does not know", and a ClipEnd
        // trigger on it drops to the live tier rather than guessing at an asset's clip length.
        double endSeconds = -1.0;
    };
    std::vector<double> beatTimes;    // ascending
    std::vector<double> barTimes;     // ascending
    std::vector<NamedSpan> shots;     // in start order
    std::vector<NamedSpan> sections;  // in start order
    std::vector<std::pair<std::string, double>> cues; // author cue markers, in time order
    std::vector<ClipSpan> clips;
    double durationSeconds = 0.0;
};

// One firing of one event at a known second.
struct Firing {
    double timeSeconds = 0.0;
    std::size_t eventIndex = 0;
    int priority = 0;
    // The occurrence that produced it -- the shot, section or cue name. Carried so a dispatched
    // event can say what it was about without the host re-deriving it.
    std::string occurrence;
};

// A `PlayClip` firing, which is not a track. ADR-089 already established why: a clip needs the
// second its phase started from, and a track carries values. The same object the actor clip cues
// produce, so `Sequence::animationAt` merges the two lists and nothing downstream can tell them
// apart.
struct ScheduledClip {
    std::string actor;
    std::string clip;
    double timeSeconds = 0.0;
    float speed = 1.0f;
    float blendSeconds = -1.0f;
};

// What resolution produced, split by tier. The counts are what an editor shows next to the event
// list, because a guarantee nobody can see is a guarantee nobody relies on (ADR-091).
struct EventSchedule {
    // Tier 1: baked. Ascending in (time, priority, declaration order).
    std::vector<Firing> baked;
    // Tier 1, the clip half.
    std::vector<ScheduledClip> clips;
    // Tier 2: a known time, an imperative effect. Same ordering.
    std::vector<Firing> dispatches;
    // Tier 3: events whose trigger only a running world can supply. Indices into the event list.
    std::vector<std::size_t> live;
    // Things an author should be told: a shot name nothing matches, a beat trigger with no
    // analysis, a ClipEnd the sequence could not bound.
    std::vector<std::string> warnings;

    [[nodiscard]] std::size_t firingCount() const { return baked.size() + dispatches.size(); }
};

// Pure: the same events and the same context resolve to the same schedule, every time, in the same
// order. This is the function the whole determinism claim of the baked tier rests on.
[[nodiscard]] EventSchedule resolveEvents(std::span<const SequenceEvent> events,
                                          const TriggerContext& context);

// ---- the live runtime ----------------------------------------------------------------------------

// Something the world reports, on the edge, once. Posted by whoever owns the fact: the trigger
// volume system for Volume*, the action system for *Complete, the animation layer for ClipEnd.
struct TriggerSignal {
    TriggerKind kind = TriggerKind::VolumeEnter;
    std::string name;    // volume id, action id, interaction id, clip name
    std::string subject; // the entity or actor it happened to
    double timeSeconds = 0.0;
};

struct FiredEvent {
    std::size_t eventIndex = 0;
    double timeSeconds = 0.0;
    std::string subject;    // who it happened to (live), or the occurrence's name (scheduled)
    // True when a seek re-delivered a standing intent rather than the event happening now. A host
    // should apply it without its entry animation: the character is already meant to be doing this.
    bool restored = false;
};

// Tiers 2 and 3. Everything here is dispatch, never a poll: a live trigger costs a hash lookup on
// the frame something actually happened, and a scheduled one costs a binary search per playhead
// step. No entity is ever visited by this class.
class EventDispatcher {
public:
    // Takes a **copy** of the events. Deliberately: an editor edits `Sequence::events` in place and
    // re-installs on mouse-release (ADR-089's "a bake is a moment"), and every frame in between
    // would otherwise be reading a span into a vector that has reallocated. `schedule` must have
    // been resolved from the same list.
    void setEvents(std::span<const SequenceEvent> events, const EventSchedule& schedule);
    void clear();

    // Tier 3. Cheap enough to call from the volume system's inner loop: an unmatched signal is one
    // failed hash lookup and no allocation.
    void post(const TriggerSignal& signal);

    // Tier 2. Moves the playhead and releases what it crossed.
    //
    // A *step* -- forward, and no further than `continuitySeconds` -- delivers every dispatch in
    // (previous, now] exactly once, in schedule order. Anything else is a **seek**, and a seek does
    // not replay: it delivers the latest dispatch at or before `now` for each distinct
    // (action kind, target), marked `restored`, and drops the rest. Playing 0 -> 60 and seeking to
    // 60 therefore leave the same standing intents, which is the most a stateful system can
    // promise, and it is stated rather than hidden.
    void advanceTo(double nowSeconds);

    // Everything ready at or before `nowSeconds`, in (time, priority, declaration) order. Delayed
    // live firings are held until their time comes. Clears what it returns.
    [[nodiscard]] std::vector<FiredEvent> drain(double nowSeconds);
    // ...and without a clock: everything pending, however far ahead.
    [[nodiscard]] std::vector<FiredEvent> drain();

    // A seek throws away pending live firings: a live event belongs to the moment it happened and
    // the moment is gone. Scheduled state is rebased rather than cleared, which is what makes the
    // next `advanceTo` a seek rather than a sixty-second replay.
    void reset(double nowSeconds = 0.0);

    [[nodiscard]] std::size_t liveEventCount() const { return liveIndex_.size(); }
    [[nodiscard]] std::size_t dispatchCount() const { return dispatches_.size(); }
    [[nodiscard]] std::size_t pendingCount() const { return pending_.size(); }
    // How many times each event has fired since the last reset, for the editor and for a `repeat`
    // limit that has to survive a drain.
    [[nodiscard]] int firedCount(std::size_t eventIndex) const;

    // How far a single step may move the playhead before it counts as a seek. Generous enough for a
    // stalled frame, short enough that nobody mistakes a scrub for one.
    double continuitySeconds = 0.5;

private:
    struct LiveKey {
        TriggerKind kind = TriggerKind::VolumeEnter;
        std::string name;
        [[nodiscard]] bool operator==(const LiveKey& other) const = default;
    };
    struct LiveKeyHash {
        [[nodiscard]] std::size_t operator()(const LiveKey& k) const;
    };

    void queue(std::size_t eventIndex, double time, std::string subject, bool restored);

    std::vector<SequenceEvent> events_;
    std::vector<Firing> dispatches_;
    std::unordered_map<LiveKey, std::vector<std::size_t>, LiveKeyHash> liveIndex_;
    std::vector<FiredEvent> pending_;
    std::vector<int> fired_;
    double playhead_ = 0.0;
    bool started_ = false;
};

} // namespace avgen::seq
