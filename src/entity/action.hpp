#pragma once

// Actions, schedules and interactions: what an entity has been *told* to do (ADR-096).
//
// The entity layer (ADR-088) gave a thing a behaviour -- something it does because of what it is.
// This gives it an *intention* -- something it does because somebody said so. The shape the brief
// asks for is a pipeline:
//
//     Character -> Action -> Target -> Animation -> Completion -> Next Action
//
// and the whole file is that sentence made into types.
//
// ## Three rules that shaped it
//
// **No verb list.** There is no `PickUpHeadphones`, no `SitOnChair`, and nothing anywhere that
// knows a protagonist exists. There are eight *primitives* -- wait, move, face, pose, interact,
// equip, unequip, set -- and the verbs the brief names (sit, open, pick up, use, enter, sleep) are
// `InteractionDesc`s **authored on the prop**. A chair says "I can be sat on, the actor stands
// here, it plays its own `sit`, it takes two seconds"; the actor says "interact with that". Adding
// a verb is a line of scene JSON, not a line of C++, and the same chair works for an alien, a deer
// and a robot.
//
// **No action names a clip.** `ActionDesc::activity` and `InteractionDesc::activity` are activity
// *names* -- "sit", "sleep", "pickUp" -- which `EntityDesc::clips` maps onto whatever the asset
// actually shipped. This is the indirection ADR-088 argued for and it is the reason a prop may
// name an animation state at all: it is naming the *actor's* vocabulary, not the actor's asset.
//
// **Falling back resumes; it does not reset.** ADR-091's hierarchy is Director -> Cinematic Action
// -> Behavior -> Navigation, read top-down, and the rule when a tier finishes is that the tier
// below carries on from where it was. A queue is therefore a small stack of tiers rather than one
// list: an override does not consume the routine it interrupted, and a routine that is paused and
// resumed resumes the action it was in the middle of, with the seconds it had already spent.
//
// ## Determinism
//
// An action sequence is a pure function of (the cues that started it, the entity's seed). There is
// no wall clock here, nothing is keyed on a frame index, and every integration is against the dt
// the entity was actually given -- including the accumulated dt of a coarsely-updated entity, so
// behaviour LOD does not change the answer. Replaying the same cues at a different frame rate
// produces the same sequence of actions with the same completions.

#include "core/error.hpp"
#include "core/rng.hpp"
#include "entity/behavior.hpp"
#include "entity/gait.hpp"
#include "entity/locomotion.hpp"
#include "entity/navigation.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::entity {

class Entity;
class EntityWorld;

// ---- the navigation seam -----------------------------------------------------------------------
//
// Everything the action layer asks of navigation, in three calls. It is an interface rather than a
// direct call into `Navigator` because §5/§6 (a walkable graph and a search over it) are being
// built separately: when that lands it implements this and nothing in this file changes.
//
// What is deliberately *not* here: anything about how a route is found, whether it is cached,
// whether it is asynchronous (that is what `Pending` is for), or what a waypoint costs.

// Distinct from `entity::PathStatus` (ADR-093), which is the *planner's* verdict and has seven
// cases for the several different ways a route can fail to exist. This is the narrower question a
// caller asks each update -- "do you have one for me yet" -- and its `Pending` is about timing
// rather than reachability, which is why the two are not one enum. A provider backed by the planner
// collapses that enum's failures into `Unreachable`.
enum class RouteStatus : std::uint8_t {
    Ready,       // `out` holds the route
    Pending,     // a planner is working on it; ask again next update
    Unreachable, // there is no route, and the caller should give up rather than walk at a wall
};
[[nodiscard]] const char* routeStatusName(RouteStatus status);

class IPathProvider {
public:
    virtual ~IPathProvider() = default;
    // Waypoints from `from` to `to`: `from` excluded, `to` included, so a straight-line provider
    // returns exactly one. `out` is cleared on Ready and left alone otherwise.
    [[nodiscard]] virtual RouteStatus route(glm::vec2 from, glm::vec2 to,
                                           std::vector<glm::vec2>& out) const = 0;
    // A unit heading that makes progress from `from` towards `to` without walking into anything,
    // or (0,0) when every direction is blocked.
    [[nodiscard]] virtual glm::vec2 steer(glm::vec2 from, glm::vec2 to, float lookahead) const = 0;
    // The surface a walker stands on at `p`.
    [[nodiscard]] virtual float groundHeight(glm::vec2 p) const = 0;
};

// The provider that exists today: `entity::Navigator`, which samples walkability analytically and
// steers locally but has **no graph and no search** (docs/cinematic-world-gap-analysis.md §5/§6).
// Its route is the straight line -- one waypoint -- and the only destination it refuses is one a
// walker could not stand on. So a character gets around a tree (that is `steer`) and does not get
// around a lake (that would need a search). Written down here so the limitation is a seam somebody
// chose rather than a surprise somebody hit.
class NavigatorPath final : public IPathProvider {
public:
    explicit NavigatorPath(const Navigator* nav = nullptr) : nav_(nav) {}
    void setNavigator(const Navigator* nav) { nav_ = nav; }
    [[nodiscard]] RouteStatus route(glm::vec2 from, glm::vec2 to, std::vector<glm::vec2>& out) const override;
    [[nodiscard]] glm::vec2 steer(glm::vec2 from, glm::vec2 to, float lookahead) const override;
    [[nodiscard]] float groundHeight(glm::vec2 p) const override;

private:
    const Navigator* nav_ = nullptr;
};

// ---- targets ----------------------------------------------------------------------------------

// What an action points at. The brief's three -- an entity, a location, a socket on a prop -- plus
// the one the interaction system needs, which is a verb a prop published.
enum class TargetKind : std::uint8_t {
    None,
    Point,       // a world location
    EntityRef,   // another entity, wherever it is now
    Node,        // a node, optionally a named socket on it
    Interaction, // a verb a prop offers: `name` is the prop, `member` is the verb
};
[[nodiscard]] const char* targetKindName(TargetKind kind);

struct ActionTarget {
    TargetKind kind = TargetKind::None;
    std::string name;   // the entity, the node, or the prop the interaction lives on
    std::string member; // the socket on that node, or the interaction's verb
    glm::vec3 point{0.0f};
    [[nodiscard]] bool empty() const { return kind == TargetKind::None; }
};

// ---- state, conditions and effects --------------------------------------------------------------

// A named number on an entity that other systems can read. Declared in the scene file and
// registered as an ordinary params::Parameter under "entity/<name>/state/<property>", which is
// what makes it a legal reaction target, modulation target, keyframe target and save-file value
// without this layer knowing any of those systems exist. Equipping the headphones sets one; the
// lamp reacting to it is `{ "signal": ..., "target": "state/headphones" }` in data.
struct PropertyDesc {
    std::string name;
    float value = 0.0f;
    float min = 0.0f;
    float max = 1.0f;
};

enum class Test : std::uint8_t { Set, Clear, Equal, NotEqual, Less, Greater };
[[nodiscard]] const char* testName(Test test);
[[nodiscard]] std::optional<Test> testFromName(std::string_view name);

// A condition on a property -- of the actor, or of somebody else named by `on`. The conditional
// branching the brief asks for is deliberately this small: a condition is a comparison, and an
// action whose conditions fail is skipped or jumps to a label. Anything richer is a scripting
// language, and a scripting language is not a thing to add by accident.
struct Condition {
    std::string property;
    std::string on;   // "" = the actor; otherwise another entity by name
    Test test = Test::Set;
    float value = 0.0f;
};

// What completing an action or an interaction writes.
struct PropertySet {
    std::string property;
    std::string on;   // "" = the actor
    float value = 1.0f;
};

// ---- interactions (§10) --------------------------------------------------------------------------

// A verb a prop offers. It lives on the **prop**, because "can be sat on" is a fact about a chair,
// and because a verb list that lived on the character would have to grow every time the set
// dressing changed.
struct InteractionDesc {
    std::string name;      // the verb: "sit", "open", "pickUp", "use", "enter", "sleep", anything
    std::string socket;    // where the actor stands, or where the item hangs; "" = the prop's origin
    // The activity the *actor* plays. An activity name, not a clip: the actor's own `clips` turns
    // it into whatever its asset shipped, so one chair drives an alien, a deer and a robot.
    std::string activity;
    double duration = 1.0; // seconds; <= 0 means "until something else ends it"
    float range = 1.5f;    // how close the actor must already be. Getting there is a `move`.
    bool exclusive = true; // one user at a time
    std::vector<Condition> conditions; // checked before an actor may begin
    std::vector<PropertySet> set;      // written on completion; `on` defaults to the **prop**
    std::string onComplete;            // an event name
};

// ---- actions (§4) ---------------------------------------------------------------------------------

// The primitives. Eight, and none of them is a verb: a verb is data (see InteractionDesc).
enum class ActionKind : std::uint8_t {
    Wait,     // hold still for a duration
    Move,     // travel to the target (through IPathProvider)
    Face,     // turn to face the target
    Pose,     // play an activity for a duration
    Interact, // run a verb the target prop published
    Equip,    // hang a node off one of my sockets
    Unequip,  // stop carrying it
    Set,      // write properties and move on
};
[[nodiscard]] const char* actionKindName(ActionKind kind);
[[nodiscard]] std::optional<ActionKind> actionKindFromName(std::string_view name);

struct ActionDesc {
    ActionKind kind = ActionKind::Wait;
    std::string name;    // a label, so a branch and an event can name this step
    ActionTarget target;
    // What to play while this runs. An activity name, never a clip name. Empty means "let the
    // gait decide", which is what a `move` wants: walking or running is a function of speed.
    std::string activity;
    double duration = 0.0; // seconds; 0 = until the action's own completion test passes
    float speed = 0.0f;    // Move: metres per second; 0 = the gait's walk speed
    float tolerance = 0.0f; // Move: metres that count as arrived. Face: radians. 0 = a default
    std::string socket;    // Equip: the socket on the actor the node hangs from
    std::vector<Condition> when; // start conditions; unmet -> skipped
    std::string otherwise;  // the label to continue from when `when` fails; "" = simply skip
    std::vector<PropertySet> set; // written on completion
    std::string onComplete;       // an event name published on completion
    // Whether being preempted and resumed continues this action or replays it from its start.
    // A walk resumes; a one-shot flinch is better replayed.
    bool resumable = true;
};

// ---- completion ------------------------------------------------------------------------------

enum class ActionResult : std::uint8_t {
    Completed, // it finished the way it meant to
    Skipped,   // its conditions were not met
    Failed,    // it could not be done; `reason` says why
    Cancelled, // a higher authority dropped it
};
[[nodiscard]] const char* actionResultName(ActionResult result);

struct ActionEvent {
    std::string entity;
    std::string action; // the action's label, or its kind when it has none
    std::string event;  // ActionDesc::onComplete, empty when the author named none
    ActionResult result = ActionResult::Completed;
    std::string reason; // why it failed
    double time = 0.0;  // the timeline second, never a wall clock
};

// Who filled a tier. ADR-091's hierarchy minus Navigation, which is not an authority over an
// action -- it serves whichever tier is driving.
enum class Authority : std::uint8_t {
    Routine = 0,  // a schedule's own list: what this character does when nobody interferes
    Action = 1,   // somebody pushed a one-off
    Director = 2, // a shot says exactly what happens
};
[[nodiscard]] const char* authorityName(Authority authority);
inline constexpr std::size_t kAuthorityCount = 3;

// ---- the queue -------------------------------------------------------------------------------

struct ActionContext {
    double time = 0.0;
    double dt = 0.0;
    Rng* rng = nullptr;
    Entity* self = nullptr;
    EntityWorld* world = nullptr;
    const IPathProvider* path = nullptr;
    const GaitSettings* gait = nullptr;
    std::vector<ActionEvent>* events = nullptr;
};

// What the queue told the entity this tick.
struct ActionOutput {
    bool driving = false;      // an action is in charge; locomotor behaviours must yield
    bool movement = false;     // and it is the one moving the body
    std::string_view activity; // the activity name to play, or empty to let the gait decide
};

class ActionQueue {
public:
    using Listener = std::function<void(const ActionEvent&)>;

    // Appends to a tier's list, leaving whatever it was doing alone.
    void push(ActionDesc action, Authority authority = Authority::Action);
    void push(std::vector<ActionDesc> actions, Authority authority = Authority::Action);
    // Replaces a tier's list. The tiers *below* are untouched and resume when this one drains --
    // that is the whole point of the stack. Whatever *this* tier was in the middle of is reported
    // Cancelled rather than dropped in silence.
    void override(std::vector<ActionDesc> actions, Authority authority = Authority::Director,
                  double now = 0.0);
    // Drops a tier's list. Anything it was in the middle of is reported Cancelled.
    void cancel(Authority authority, double now = 0.0);
    void clear(double now = 0.0);

    // Freezes a tier: it neither ticks nor counts as driving, and the tier below takes over. What
    // pausing a routine does. Unholding restores the exact action and the seconds it had spent.
    void hold(Authority authority, bool held);
    [[nodiscard]] bool held(Authority authority) const;

    [[nodiscard]] bool running() const;
    [[nodiscard]] Authority authority() const;
    [[nodiscard]] const ActionDesc* current() const;
    [[nodiscard]] const ActionDesc* current(Authority authority) const;
    // Seconds the running action has actually had -- excluding everything it spent preempted or
    // held, which is what makes a resume a resume.
    [[nodiscard]] double elapsed() const;
    [[nodiscard]] double elapsed(Authority authority) const;
    // The route the driving tier's `move` is walking, and which leg of it (ADR-194). Empty when
    // nothing is running, when what is running is not a move, or before the planner has answered.
    // A move with no planner installed routes straight at the goal and is reported as the one
    // waypoint it actually holds -- the overlay draws that line, because it is the line the body
    // is walking, and a straight line through a lake is exactly the thing worth seeing.
    //
    // The span points into the queue and lives until its next update, like `current()`.
    [[nodiscard]] std::span<const glm::vec2> route() const;
    [[nodiscard]] std::size_t routeLeg() const;

    // Actions not yet finished, across every tier.
    [[nodiscard]] std::size_t pending() const;
    [[nodiscard]] std::size_t pending(Authority authority) const;
    [[nodiscard]] bool empty() const { return pending() == 0; }

    void setListener(Listener listener) { listener_ = std::move(listener); }
    // Where an event goes when there is no ActionContext to carry it -- a cancellation raised from
    // outside an update. Borrowed; the caller keeps it alive and names the entity for it, because
    // a cancellation with no ActionContext has no other way to say whose it was.
    void setSink(std::vector<ActionEvent>* sink, std::string owner) {
        sink_ = sink;
        owner_ = std::move(owner);
    }

    // Ticks whichever tier is on top. Writes travel, yaw, speed and activity into `state` when it
    // is driving and leaves them alone when it is not.
    ActionOutput update(const ActionContext& ctx, EntityState& state);

    void reset();

private:
    struct Progress {
        bool routed = false;
        std::vector<glm::vec2> waypoints;
        std::size_t waypoint = 0;
        float speed = 0.0f;        // the accel/decel model's current speed
        float bestDistance = 0.0f; // the closest it has been to the goal
        double sinceProgress = 0.0;
        bool claimed = false;      // it holds an exclusive interaction
    };
    struct Layer {
        std::vector<ActionDesc> actions;
        std::size_t index = 0;
        bool started = false;
        bool heldFlag = false;
        double elapsed = 0.0;
        Progress progress;
        [[nodiscard]] bool live() const { return !heldFlag && index < actions.size(); }
    };

    [[nodiscard]] int topLayer() const;
    void begin(Layer& layer, const ActionContext& ctx, EntityState& state);
    void finish(Layer& layer, const ActionContext& ctx, ActionResult result, std::string reason);
    void release(Layer& layer, const ActionContext& ctx);
    void emit(const ActionContext& ctx, const ActionDesc& action, ActionResult result,
              const std::string& reason);
    [[nodiscard]] std::size_t labelIndex(const Layer& layer, const std::string& label) const;

    Layer layers_[kAuthorityCount];
    int driver_ = -1; // the tier that drove last tick, so a change of driver is noticed
    Listener listener_;
    std::vector<ActionEvent>* sink_ = nullptr;
    std::string owner_;
};

// ---- schedules (§9) --------------------------------------------------------------------------

struct ScheduleEntry {
    double time = 0.0; // seconds from the routine's start
    std::string name;  // so a director can trigger this step out of order
    std::vector<ActionDesc> actions;
};

struct ScheduleDesc {
    std::string name;
    bool loop = false;
    double period = 0.0; // loop length in seconds; 0 = one second past the last entry
    std::vector<ScheduleEntry> entries;
};

// A routine the director can start, pause, override, trigger and resume.
//
// The routine's clock is `now` minus the start minus everything spent paused, so a routine paused
// at 10s and resumed at 400s fires its 12s entry two seconds after the resume -- not immediately,
// and not three hundred and ninety seconds late. That is what makes (start, the pause and resume
// cues) enough to reproduce the whole day.
class Schedule {
public:
    void setDesc(ScheduleDesc desc);
    [[nodiscard]] const ScheduleDesc& desc() const { return desc_; }

    void start(double now);
    void stop();
    void pause(double now, ActionQueue& queue);
    void resume(double now, ActionQueue& queue);
    [[nodiscard]] bool running() const { return running_; }
    [[nodiscard]] bool paused() const { return paused_; }
    [[nodiscard]] double localTime(double now) const;
    [[nodiscard]] std::size_t next() const { return next_; }

    // Pushes every entry whose time has come onto the queue's Routine tier.
    void update(double now, ActionQueue& queue);
    // Fires one entry by name, now, out of order. False when there is no such entry.
    bool trigger(std::string_view entry, ActionQueue& queue, Authority authority = Authority::Action);

    void reset();

private:
    ScheduleDesc desc_;
    bool running_ = false;
    bool paused_ = false;
    double start_ = 0.0;
    double pausedAt_ = 0.0;
    double pausedFor_ = 0.0;
    std::size_t next_ = 0;
    int cycle_ = 0;
};

// ---- serialisation ---------------------------------------------------------------------------
//
// All of it data, all of it in the scene file, none of it scene-specific (addendum §26/§27).

[[nodiscard]] Result<ActionTarget> targetFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json targetToJson(const ActionTarget& target);
[[nodiscard]] Result<ActionDesc> actionFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json actionToJson(const ActionDesc& action);
[[nodiscard]] Result<std::vector<ActionDesc>> actionsFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json actionsToJson(const std::vector<ActionDesc>& actions);
[[nodiscard]] Result<InteractionDesc> interactionFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json interactionToJson(const InteractionDesc& interaction);
[[nodiscard]] Result<ScheduleDesc> scheduleFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json scheduleToJson(const ScheduleDesc& schedule);
[[nodiscard]] Result<GaitSettings> gaitFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json gaitToJson(const GaitSettings& gait);

} // namespace avgen::entity
