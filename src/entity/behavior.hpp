#pragma once

// Reusable behaviours (ADR-088).
//
// A behaviour is a small, named, data-configured piece of autonomous motion. It owns state, it is
// ticked once per frame, and everything it exposes is an ordinary params::Parameter -- so every
// knob a behaviour has is keyframeable on the timeline, presettable, and a legal modulation target,
// without this file knowing any of those systems exist.
//
// Behaviours produce *offsets*, not absolute transforms. The scene file still says where a thing
// is; a behaviour says how it moves around that. That is what lets an author place an object by
// hand, or by a spatial query, and still have it hover -- and what lets a modulation route and a
// behaviour drive the same property without fighting over who owns it.

#include "entity/character_intent.hpp"
#include "core/rng.hpp"
#include "entity/locomotion.hpp"
#include "entity/nav_grid.hpp"
#include "entity/navigation.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::entity {

class Entity;
class EntityWorld;
class ActionQueue;

// What behaviours add to the node the entity drives. Folded onto the node's transform parameters
// once per frame, after modulation, so a route and a behaviour compose instead of overwriting.
struct MotionOffset {
    glm::vec3 position{0.0f};  // metres, world space
    glm::vec3 rotation{0.0f};  // Euler degrees, added
    glm::vec3 scale{1.0f};     // multiplied
};

// The entity's own state, shared between the behaviours on it. Behaviours run in the order they
// were declared and each sees what the ones before it wrote, which is how `interest` points
// `lookAt` at something without either of them knowing about the other.
struct EntityState {
    glm::vec3 anchor{0.0f};   // where the scene put it: the point motion is relative to
    glm::vec3 travel{0.0f};   // how far the entity has walked from its anchor (navigation writes this)
    float yaw = 0.0f;         // radians about +Y, the body's facing
    float speed = 0.0f;       // horizontal m/s -- INTENT: how fast the mover means to go
    float turnRate = 0.0f;    // rad/s, signed
    // ---- the measured velocity (ADR-545) --------------------------------------------------------
    // World-space metres per second, over the step that just ran. **Measured, not authored**: it is
    // the backward difference of `position()` taken once at the end of the entity update, so it is
    // correct for every mover at once -- a behaviour's steering, an action's walk, root motion, the
    // director tier, crowd separation and grounding -- without any of them being asked to maintain
    // a second number that could disagree with the first.
    //
    // It says something `speed` and `yaw` together cannot: **which way the body actually went**.
    // `speed` is a scalar along `yaw`, so the pair can only describe a body facing the way it is
    // travelling. A strafe, a backward step, a body circling a target while watching it, and the
    // future trajectory features motion matching wants are all velocity-with-a-direction, and none
    // of them is expressible as (speed, yaw).
    //
    // Zero on the first step of a body's life and across a transport discontinuity, because a
    // backward difference has nothing to difference against -- and zero rather than a guess, for
    // the reason ADR-521 exists: the first tick of a render reports `deltaTime = 0`, and dividing
    // by it would publish an infinity into the pose layer.
    glm::vec3 velocity{0.0f};
    Activity activity = Activity::Idle;
    // How wide this thing is, in metres. 0 means "not a body": it takes part in nothing that
    // separates crowds, which is right for a craft that flies over them. A behaviour that knows its
    // character's size writes it here, and `EntityWorld` collects them into the crowd field (§11).
    float radius = 0.0f;
    float reaction = 0.0f;    // 0..1, decaying
    glm::vec3 lookTarget{0.0f};
    bool hasLookTarget = false;
    // The surface this body is standing on, in WORLD space: a point on it and its normal, as the
    // body's own `GroundFollower` filtered them (ADR-359). Written by whichever behaviour grounds
    // this entity and by nothing else; false on a body that is flying, swimming or in a scene with
    // no terrain, which is the case a foot IK layer has to be able to tell from "flat ground".
    glm::vec3 groundPoint{0.0f};
    glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
    bool hasGroundPlane = false;
    // How much of a full update this entity is getting. 1 = every frame. Behaviours that integrate
    // must scale by the real dt, which the context carries; this is for behaviours that want to
    // know they are being run coarsely and simplify.
    float detail = 1.0f;
    // A higher authority is driving this entity's body this frame -- an action, a schedule or a
    // director override (ADR-091, ADR-096). Behaviours that own *travel and facing* must yield
    // while it is set, and must yield by **keeping their state**: the rule is that the tier below
    // resumes rather than resets, so a wanderer preempted mid-walk carries on to the same
    // destination afterwards rather than picking a new one. Behaviours that only add an offset --
    // hover, drift, spin, bank -- are unaffected: a craft can hover while it is being told where
    // to go.
    bool driven = false;
    // And it owns this body's *height* too: the body is not standing on anything. Raised by the
    // Director tier (ADR-210) when a shot is flying something -- a craft between targets, an animal
    // going up a tractor beam. `driven` alone is not enough: an action's `move` walks across ground
    // and wants `ground` to keep it on the surface, and a cow twenty metres in the air does not.
    // Without the distinction, `ground` writes `travel.y` back to the terrain every frame and the
    // lift is a cow standing in a beam looking startled.
    bool airborne = false;
    [[nodiscard]] glm::vec3 position() const { return anchor + travel; }
    // The unit vector the body is facing, from `yaw`. This engine's convention, stated in one place
    // rather than re-derived at each call site: `yaw = atan2(direction.x, direction.z)`
    // (`entity/behaviors.cpp`), so yaw zero is +Z and the inverse is (sin, 0, cos).
    [[nodiscard]] glm::vec3 facing() const { return {std::sin(yaw), 0.0f, std::cos(yaw)}; }
    // The horizontal magnitude of the MEASURED velocity, which is not `speed`: `speed` is what the
    // mover intended this step and this is what the body did. They differ whenever something
    // downstream had an opinion -- a crowd push, a penetration resolve, a director, an obstacle.
    [[nodiscard]] float groundSpeed() const { return std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z); }

    // Phase D §15. What this character WANTS, as a vector. `speed` and `yaw` above are the polar
    // form every existing behaviour writes; this is the form that can express a strafe, and it is
    // invalid by default so that a behaviour which writes neither still works exactly as it did.
    CharacterIntent intent;

    // Phase B §19/§35. Measured beside `velocity`, from the same difference, one derivative out.
    // Zero on a body's first two steps and across a seek, for the same reason `velocity` is.
    glm::vec3 acceleration{0.0f};
    // How far the body's travel is from its facing, in radians, 0 when standing still. Zero for a
    // body walking where it looks, pi for one backing up, pi/2 for a pure strafe.
    [[nodiscard]] float strafeAngle() const {
        const float ground = groundSpeed();
        if (ground < 1e-4f) {
            return 0.0f;
        }
        const glm::vec3 f = facing();
        const float dot = std::clamp((velocity.x * f.x + velocity.z * f.z) / ground, -1.0f, 1.0f);
        return std::acos(dot);
    }
};

// A place worth walking to (ADR-093, §6). §6 lists what a character should find interesting --
// glowing plants, water, the UFO, terrain features, scenic locations -- and this is that list, as
// data, so a behaviour can choose among them without knowing where any of them came from.
//
// The kinds exist so a character can have *taste*: one drawn to water and one drawn to high ground
// are the same behaviour with different weights, and the difference is what stops two characters in
// the same world walking the same route.
enum class InterestKind : std::uint8_t {
    Landmark,  // a hero or an authored node: the elder, the monument, the arch
    Character, // another entity, which moves
    Glow,      // a patch of luminous ecology
    Water,     // a point on a shoreline
    Vista,     // a walkable local high point
};
[[nodiscard]] const char* interestKindName(InterestKind kind);

struct InterestPoint {
    glm::vec3 position{0.0f};
    std::string name;   // empty for a derived point; a landmark or entity name otherwise
    InterestKind kind = InterestKind::Landmark;
    float weight = 1.0f;
    // Phase D §25: semantic tags as bits of `EntityWorld::semanticTags()`. Filled by the world in
    // `refreshInterestPoints` -- the kind's own name, plus the entity's tags when the point names
    // one -- so whoever supplies a point does not have to know the vocabulary.
    std::uint64_t tags = 0;
};

struct BehaviorContext {
    double time = 0.0;   // seconds on the engine timeline
    double dt = 0.0;     // seconds since this entity was last updated (not necessarily the frame's)
    const signals::SignalBus* bus = nullptr;
    const Navigator* nav = nullptr;
    const EntityWorld* world = nullptr;
    // Which entity this is, as an index into `EntityWorld::entities()`. What lets a behaviour ask
    // the crowd field to push it away from everyone *except itself* without a name comparison.
    std::size_t self = 0;
    Rng* rng = nullptr;  // this entity's own stream, seeded from the scene seed and the entity name
    // This entity's own action queue, for a behaviour that *decides* rather than moves (ADR-269,
    // ADR-333). A decider's whole output is an `ActionDesc` list pushed onto `Authority::Routine`;
    // without this it would have to move the body itself, which is the second execution engine
    // ADR-269 exists to refuse. Null for every behaviour that does not need it, and the fourteen
    // that came before this one do not touch it.
    //
    // The queue has already ticked this frame when the behaviours run (ADR-091 reads the hierarchy
    // top-down), so an action pushed here begins on the next step. That is a frame of latency and
    // it is deliberate: a decision that took effect inside the frame it was made in would run the
    // queue twice in one step, and the second run would see a `dt` it had already spent.
    ActionQueue* actions = nullptr;

    // Reads a signal by name, 0 when the bus has no such signal. Behaviours resolve names once and
    // cache the id; this is the slow path for the rare read.
    [[nodiscard]] float signal(std::string_view name) const;
    [[nodiscard]] bool event(std::string_view name) const;
};

// What a navigating behaviour is doing, in the terms an overlay draws (ADR-093 §6, ADR-197).
//
// `explore` has published every one of these since ADR-093 -- `route()`, `routeLeg()`,
// `destination()`, `lastPathStatus()`, `phaseName()` -- with a comment saying the editor owns the
// drawing and navigation owes it the data. Nothing ever read them, so the data was true and
// invisible for as long as it existed, which is the same as not having it: "why is it going that
// way" had no answer, and a character standing still because its goal came back `Unreachable`
// looked exactly like one that was idling.
//
// A virtual on IBehavior rather than a dynamic_cast, because the behaviour classes are defined
// inside behaviors.cpp and nothing outside that file can name their types. Defaulting to false is
// what makes the other fourteen behaviours cost nothing and say nothing: `hover` has no route, and
// an overlay that drew one for it would be inventing a fact.
struct NavDebug {
    std::span<const glm::vec2> route;  // the planned waypoints after the walker, world XZ
    std::size_t leg = 0;               // index into `route` of the one it is walking to
    glm::vec3 destination{0.0f};       // where it settled on going; world
    bool hasDestination = false;
    PathStatus status = PathStatus::Ok; // why the last plan came back as it did
    // Seconds this body has been unable to find anywhere at all to go, and seconds it has been
    // walking without getting closer. Both are 0 for a character that is simply pausing between
    // errands, and that is the point of them existing (ADR-296).
    //
    // `status` cannot carry either, because neither is a path request: a body sealed inside a ring
    // of stones never reaches `requestPath` -- every destination it could pick is outside the wall,
    // `pickDestination` rejects all of them, and the last status it published is still the `Ok` of
    // whatever it did before it was penned. The Character Intelligence Lab measured exactly that
    // and recorded the consequence: **being stuck and being idle produced the same frame and the
    // same log.** These two fields are the difference.
    float confinedFor = 0.0f;
    float stuckFor = 0.0f;
    std::string_view phase;             // idle / select / navigate / walk / arrive / observe
    std::string_view goalName;          // what it is going to, when the destination has a name
    std::string_view goalKind;          // landmark / character / glow / water / vista
};

// One option as a decider scored it, flattened for an overlay (ADR-269 §4, ADR-333).
//
// `NavDebug` exists because "why is it going that way" is a question this project has repeatedly
// been unable to answer about its own characters, and it was published and read by nobody for long
// enough that the data was true and invisible -- which is the same as absent. This is the same seam
// for the question one layer up: *what did it turn down to do that?*
//
// A flattened copy rather than `entity::Option` itself, because `Option` lives in
// `character_ai.hpp`, which includes `action.hpp`, which includes this file. An overlay needs the
// name and the number and nothing else.
struct ScoredOption {
    std::string_view name;  // into the considerer; valid until its next `consider`
    float score = 0.0f;
    bool chosen = false;    // the one the selector committed to, which is not always the highest:
                            // a dwell or a margin refusal is exactly the case worth seeing
};

// One decision, as the behaviour trace records it (Phase D §64, §65).
//
// Strings, deliberately, and built only when the choice *changes* -- a handful of times a minute
// per character, never per frame -- because this is the record a person reads and a regression
// test compares: "12.42 wander -> investigate mushroom-2 (0.82; novelty +0.31 ...)".
struct DecisionTraceEntry {
    double time = 0.0;
    std::string option;        // the option committed to
    std::string subject;       // what it is about, by name; empty for nothing in particular
    IntentType intent = IntentType::Custom;
    float score = 0.0f;
    std::string runnerUp;      // the best option it beat
    float runnerUpScore = 0.0f;
    std::string factors;       // "novelty +0.31, salience +0.18, personality x1.20"
    std::string previous;      // the option it replaced
    // How the previous plan ended: "completed", "failed: unreachable", "interrupted", or "" when
    // there was none. §19's lifecycle, made visible.
    std::string previousOutcome;
    std::string attention;     // what the body was attending to at the moment it chose, and why
};

// What a deciding behaviour is doing, in the terms an overlay draws.
//
// **Read by `entity/behavior_trace.cpp`** (Phase D §41, §64): `explainCharacter` turns it into the
// "why is this character doing that" report and `BehaviorTraceRecorder` into the behaviour trace,
// which `tools/behavior_trace.cpp` prints. ADR-615 recorded it as produced and read by nobody;
// that was true until Phase D and is not now. No editor panel draws it yet -- the World editor
// draws `navDebug` only (§40, open).
struct DecisionDebug {
    std::span<const ScoredOption> options; // every option scored on the last decision tick, in order
    std::string_view chosen;               // the committed option's name; empty when nothing scored
    std::uint64_t tick = 0;                // the decision tick the last selection ran at
    std::uint64_t committedTick = 0;       // the tick the current option was committed on
    float margin = 0.0f;                   // how far the winner beat the runner-up by, this tick
    std::size_t decisions = 0;             // times the choice changed, since the last reset
    std::size_t dwellRejections = 0;       // switches the dwell refused
    std::size_t marginRejections = 0;      // switches the margin refused
    std::size_t remembered = 0;            // percepts held past the tick that saw them (ADR-333 §5)
    // Times this decider gave up on a plan it was not executing (ADR-351). Zero on every body
    // that does not opt in, and zero on one that does and never stalls -- which is the
    // distinction that makes the number worth printing rather than a decoration.
    std::size_t stalls = 0;

    // ---- Phase D §40/§41: the rest of "why is this character doing that" -----------------------
    IntentType intent = IntentType::Custom;   // the committed option's intent
    std::string_view subject;                 // what it is about, by name
    std::string_view factors;                 // the committed option's score terms, formatted
    // Attention (§10): what the body is attending to, and the largest reason why. Empty when the
    // decider has no awareness layer or nothing is worth attending to.
    std::string_view attentionSubject;
    std::string_view attentionReason;
    float attentionScore = 0.0f;
    glm::vec3 attentionPosition{0.0f};
    // The plan (§19): whether the committed option's action list is still running, and how the
    // last one ended.
    bool planActive = false;
    std::string_view lastOutcome;
    // The behaviour trace (§64/§65), oldest first, bounded. `historyTotal` counts every entry ever
    // recorded since the last reset, so a reader polling each frame can tell which entries are new
    // even after the oldest have been dropped.
    std::span<const DecisionTraceEntry> history;
    std::size_t historyTotal = 0;
    bool aware = false;                       // the decider runs the awareness layer at all
};

class IBehavior {
public:
    virtual ~IBehavior() = default;
    [[nodiscard]] virtual std::string_view kind() const = 0;
    // Registers this behaviour's knobs under `prefix` (which ends in '/'). Called once, before any
    // route or track is bound.
    virtual void registerParameters(params::ParameterSet& params, const std::string& prefix) = 0;
    // Collects the paths this behaviour registered, so they can be removed on a scene swap.
    virtual void collectParameterPaths(std::vector<std::string>& out) const = 0;
    // Puts the behaviour back to its start. Called on load and on a timeline seek: a behaviour that
    // did not reset here would make a scrubbed frame depend on how the playhead got there.
    virtual void reset(Rng& rng) = 0;
    virtual void update(const BehaviorContext& ctx, EntityState& state, MotionOffset& motion) = 0;

    // How many fixed simulation steps, *ending at the one being asked about*, have to be integrated
    // for this behaviour to be in the state a play would have left it in. 1 means the answer at `t`
    // is a function of `t` and nothing else; 2 means it also needs the step before, because it
    // differences against it. `kAllOfIt` means the state is an accumulation and the only honest
    // answer is the whole replay.
    //
    // This is what lets `EntityWorld::seek` stop paying for history nobody reads. A ninety-second
    // scrub integrates 5,400 steps per body, and for a craft whose whole motion is a noise function
    // of the clock, 5,399 of them produce a number that the 5,400th overwrites. Measured on
    // Glowmere's own population in ADR-273.
    //
    // **The default is `kAllOfIt`, and that is the point.** A behaviour opts *out* of history by
    // saying so, so a new one added tomorrow -- or an existing one that grows a timer -- is
    // replayed in full until somebody has looked at it and can defend the shorter answer. A wrong
    // answer here is a scrubbed frame that differs from a played one, which is the defect the seek
    // exists to prevent; tests/unit/test_entity_seek.cpp checks every kind in the vocabulary
    // against a full replay, with the accumulating kinds as the control that must disagree.
    static constexpr int kAllOfIt = -1;
    [[nodiscard]] virtual int historySteps() const { return kAllOfIt; }

    // Fills `out` and returns true when this behaviour navigates. The spans point into the
    // behaviour and are valid until its next update, which is enough for a UI pass that runs in
    // the same frame and is why nothing is copied here.
    [[nodiscard]] virtual bool navDebug(NavDebug& out) const {
        (void)out;
        return false;
    }

    // Fills `out` and returns true when this behaviour *decides* -- when it scores options and
    // commits to one. The same shape as `navDebug` and for the same reason: the behaviour classes
    // live inside `behaviors.cpp` and nothing outside that file can name their types.
    //
    // Defaulting to false is what makes the other fourteen behaviours say nothing. `explore` scores
    // a goal model and does **not** report here, deliberately: what it does with the scores is a
    // weighted roll over all of them rather than a selection between courses of action, and
    // reporting a list of 140 interest points as "the options it turned down" would be a true list
    // that answered a different question.
    [[nodiscard]] virtual bool decisionDebug(DecisionDebug& out) const {
        (void)out;
        return false;
    }
};

// How a behaviour is declared in a scene file:
//   { "kind": "hover", "name": "lift", "amplitude": 0.8, "rate": 0.07 }
// `name` defaults to `kind` and only matters when one entity carries two of the same kind.
struct BehaviorDesc {
    std::string kind;
    std::string name;
    nlohmann::json settings;            // the knobs the scene file authored, as written
};

// The built-in vocabulary. Adding one is a factory entry plus a class; nothing else changes.
[[nodiscard]] std::vector<std::string_view> behaviorKinds();
// Null when `kind` is not a behaviour; the caller reports it. `settings` supplies the knobs'
// authored defaults and may be null (every knob has a default).
[[nodiscard]] std::unique_ptr<IBehavior> makeBehavior(std::string_view kind,
                                                      const nlohmann::json* settings);

} // namespace avgen::entity
