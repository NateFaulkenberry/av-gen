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

#include "core/rng.hpp"
#include "entity/locomotion.hpp"
#include "entity/nav_grid.hpp"
#include "entity/navigation.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::entity {

class Entity;
class EntityWorld;

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
    float speed = 0.0f;       // horizontal m/s
    float turnRate = 0.0f;    // rad/s, signed
    Activity activity = Activity::Idle;
    // How wide this thing is, in metres. 0 means "not a body": it takes part in nothing that
    // separates crowds, which is right for a craft that flies over them. A behaviour that knows its
    // character's size writes it here, and `EntityWorld` collects them into the crowd field (§11).
    float radius = 0.0f;
    float reaction = 0.0f;    // 0..1, decaying
    glm::vec3 lookTarget{0.0f};
    bool hasLookTarget = false;
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
