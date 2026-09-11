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
#include "entity/navigation.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <memory>
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
