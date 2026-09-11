#pragma once

// Entities: things in a world that move, attend and answer the music (ADR-088).
//
// An entity is not a new kind of geometry and not a new scene graph. It is a *driver* for a node
// that already exists: the scene file places the node, and the entity says how it behaves. That
// split is the whole design. It means an entity can drive an imported craft, a procedural rock or
// a skinned character without knowing which it is, and it means nothing in the composition had to
// learn about behaviour to gain it.
//
// Two mechanisms, kept separate because they answer different questions:
//
//   * **Behaviours** are autonomous, stateful motion -- hover, drift, wander, look-at, interest.
//     They are code, because a hover that is not a pure function of time is the only hover that
//     does not read as a metronome. Every knob they own is a params::Parameter.
//
//   * **Reactions** are `property <- signal`, declared in data. They are not code, and they are
//     not a second modulation system: a reaction compiles to an ordinary params::ModRoute with an
//     ordinary ProcessorChain. What this layer adds is *addressing* -- an entity resolves
//     "parts/Blue/emissiveGain" or "hover/amplitude" against the node it drives -- and a
//     diagnostic when it cannot, because a binding that resolves to nothing and says nothing is
//     how this project has lost five features.
//
// The acceptance test for the design is that a future entity can say, in a scene file and with no
// C++ anywhere:
//
//     "reactions": [ { "signal": "audio.bass", "target": "parts/Lamp/emissiveGain", "depth": 3 } ]

#include "core/rng.hpp"
#include "entity/action.hpp"
#include "entity/behavior.hpp"
#include "entity/gait.hpp"
#include "entity/locomotion.hpp"
#include "entity/navigation.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/scene_types.hpp"
#include "signals/signal_bus.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace avgen::entity {

// ---- declarations ----------------------------------------------------------------------------

// One `property <- signal` binding, as written in the scene file.
struct ReactionDesc {
    std::string signal;   // any bus signal: "audio.bass", "music.drop", "lfo.drift", "beat.phase"
    std::string target;   // a property path relative to the entity (see resolveTarget)
    int component = -1;   // -1 = every component
    float depth = 1.0f;   // how far the property travels at full signal
    params::ModOp op = params::ModOp::Add;
    params::Polarity polarity = params::Polarity::Unipolar;
    params::ProcessorChain chain{};
    bool enabled = true;
};

// A named place on an entity that a prop can hang from. `joint` names a skeleton joint; an entity
// with no skeleton resolves every socket against its own origin, which is the right answer for a
// craft and a serviceable one for a character until the skinning layer lands.
struct SocketDesc {
    std::string name;
    std::string joint;
    scene::Transform offset;
};

// A composition node carried by a socket. The entity writes the node's transform parameters every
// frame, so an attached prop needs no new node kind and no new parenting rule.
struct AttachmentDesc {
    std::string node;
    std::string socket;
};

struct EntityDesc {
    std::string name;
    std::string node;             // the composition node this entity drives; defaults to `name`
    std::uint32_t seed = 0;       // 0 = derive from the scene seed and the entity's name
    std::vector<BehaviorDesc> behaviors;
    std::vector<ReactionDesc> reactions;
    std::vector<SocketDesc> sockets;
    std::vector<AttachmentDesc> attachments;
    // Which animation state plays for each activity, by activity name ("idle", "walk", "run",
    // "turn", "observe", "react"). Declared here rather than chosen in a behaviour because a clip
    // name belongs to an asset: a behaviour that named one would break the day a character shipped
    // with a different set, and the same `wander` has to drive an alien, a deer and a robot.
    std::vector<std::pair<std::string, std::string>> clips;

    // ---- intent (ADR-096) ----
    // Named numbers other systems can read: registered as ordinary parameters under
    // "entity/<name>/state/<property>", so a reaction, a keyframe or a modulation route may
    // address one without any of them knowing an action wrote it.
    std::vector<PropertyDesc> properties;
    // The verbs this node offers anyone who asks -- "sit", "open", "pickUp". Declared on the prop,
    // never on the character, so the set dressing can grow without the character layer changing.
    std::vector<InteractionDesc> interactions;
    // What this entity does at load: the opening action list, drained in order.
    std::vector<ActionDesc> actions;
    // The routine it runs, if it has one. Started by the director, not by the load.
    ScheduleDesc schedule;
    // How this body's speed becomes a gait. Per entity because a deer, a robot and a person cross
    // from walking to running at different speeds.
    GaitSettings gait;

    // The profile this entity was built from, as written. Round-tripped so saving a scene does not
    // inline what the author deliberately shared -- `profileCount` records how many of each list
    // came from it, so the writer emits only what this entity added. Runtime, never serialised.
    std::string profile;
    std::size_t profileBehaviors = 0;
    std::size_t profileReactions = 0;
    std::size_t profileClips = 0;
    std::size_t profileSockets = 0;

    // Behaviour level of detail. Beyond `fullDetailDistance` metres from the view, the entity is
    // updated every `coarseInterval` seconds instead of every frame, with the accumulated dt; past
    // `cullDistance` it is not updated at all. 0 disables that stage.
    float fullDetailDistance = 0.0f;
    float coarseInterval = 0.1f;
    float cullDistance = 0.0f;

    [[nodiscard]] const std::string& driven() const { return node.empty() ? name : node; }
};

// What the host (a Composition) tells the entity layer about the node it drives. A table rather
// than an interface: the host rebuilds it when it rebuilds, the entity layer reads it, and neither
// has to include the other's header.
struct NodeBinding {
    std::string node;
    bool exists = false;
    std::string transformPrefix; // "nodes/<node>/"      -- position, rotation, scale, visible, ...
    std::string geometryPrefix;  // "procedural/<node>/" -- material and part knobs; "" when none
    // Part index -> the material name the asset gave it. Empty strings for assets that named none.
    // This is what turns "parts/Blue/emissiveGain" into "procedural/ufo/parts/3/emissiveGain".
    std::vector<std::string> partNames;
    glm::vec3 anchor{0.0f};      // the node's authored world position
};

// ---- runtime ---------------------------------------------------------------------------------

struct EntityUpdate {
    double time = 0.0;
    double dt = 0.0;
    std::uint64_t frameIndex = 0;
    const signals::SignalBus* bus = nullptr;
    glm::vec3 viewPosition{0.0f}; // where the camera is, for behaviour level of detail
};

class Entity {
public:
    Entity(EntityDesc desc, std::uint32_t sceneSeed);

    [[nodiscard]] const EntityDesc& desc() const { return desc_; }
    // The animation state for `activity`, or empty when the entity declared none. Falls back to
    // "idle" so a character with one clip still plays it rather than standing in its bind pose.
    [[nodiscard]] const std::string& clipFor(Activity activity) const;
    // The same lookup for an activity an action named ("sit", "sleep", "pickUp"). Still an
    // activity name and never a clip name: this is the indirection that lets one prop's "sit"
    // drive an alien, a deer and a robot.
    [[nodiscard]] const std::string& clipFor(std::string_view activity) const;
    [[nodiscard]] const std::string& name() const { return desc_.name; }
    [[nodiscard]] const EntityState& state() const { return state_; }
    [[nodiscard]] const LocomotionState& locomotion() const { return locomotion_; }
    [[nodiscard]] std::uint32_t seed() const { return seed_; }

    // Where an attached prop should sit. False when this entity has no such socket.
    [[nodiscard]] bool socketTransform(std::string_view socket, scene::Transform& out) const;

    // Installed by the animation layer; both may stay null forever (see locomotion.hpp).
    void setPoseSink(IPoseSink* sink) { pose_ = sink; }
    void setSkeleton(const ISkeletonQuery* skeleton) { skeleton_ = skeleton; }

    // The entity's behaviours, in declaration order.
    [[nodiscard]] const std::vector<std::unique_ptr<IBehavior>>& behaviors() const { return behaviors_; }

    // ---- intent (ADR-096) ------------------------------------------------------------------

    [[nodiscard]] ActionQueue& actions() { return actions_; }
    [[nodiscard]] const ActionQueue& actions() const { return actions_; }
    [[nodiscard]] Schedule& schedule() { return schedule_; }
    [[nodiscard]] const Schedule& schedule() const { return schedule_; }
    [[nodiscard]] const Gait& gait() const { return gait_; }

    // A named number this entity declared. `setProperty` refuses a name the entity did not
    // declare rather than inventing one, because a property invented at runtime is a property no
    // reaction could have been bound to -- which is this project's recurring failure, in miniature.
    [[nodiscard]] bool hasProperty(std::string_view property) const;
    [[nodiscard]] float property(std::string_view property) const;
    bool setProperty(std::string_view property, float value);
    [[nodiscard]] const std::vector<std::pair<std::string, float>>& properties() const {
        return propertyValues_;
    }

    // The verb this node offers under that name, or null.
    [[nodiscard]] const InteractionDesc* interaction(std::string_view verb) const;
    // Who is using an exclusive interaction right now, or "".
    [[nodiscard]] std::string_view occupant(std::string_view verb) const;
    // Takes an exclusive interaction, if it is free or already this claimant's.
    bool claim(std::string_view verb, std::string_view who);
    void release(std::string_view verb, std::string_view who);

    // What the entity is carrying: what the scene attached, plus whatever `equip` added since.
    [[nodiscard]] const std::vector<AttachmentDesc>& attachments() const { return attachments_; }
    bool attach(const std::string& node, const std::string& socket);
    bool detach(const std::string& node);

private:
    friend class EntityWorld;

    EntityDesc desc_;
    std::uint32_t seed_ = 0;
    Rng rng_;
    EntityState state_{};
    MotionOffset motion_{};
    LocomotionState locomotion_{};
    std::vector<std::unique_ptr<IBehavior>> behaviors_;

    // Resolved once at bind; null until then and after a parameter set is cleared.
    params::Parameter<glm::vec3>* positionParam_ = nullptr;
    params::Parameter<glm::vec3>* rotationParam_ = nullptr;
    params::Parameter<glm::vec3>* scaleParam_ = nullptr;

    double coarseAccum_ = 0.0;
    bool active_ = true;

    // ---- intent ----
    ActionQueue actions_;
    Schedule schedule_;
    Gait gait_;
    // Authoritative here rather than in the parameter set: an entity may be ticked before anything
    // registers a parameter, and a state that only existed as a parameter would vanish on a scene
    // swap. The parameter mirrors this, not the other way round.
    std::vector<std::pair<std::string, float>> propertyValues_;
    std::vector<params::Parameter<float>*> propertyParams_;
    std::vector<AttachmentDesc> attachments_;
    // verb -> the entity currently using it. A vector because a prop offers two or three verbs.
    std::vector<std::pair<std::string, std::string>> claims_;

    IPoseSink* pose_ = nullptr;
    const ISkeletonQuery* skeleton_ = nullptr;
};

class EntityWorld {
public:
    EntityWorld() = default;

    // Replaces the entity set. Behaviours are constructed here, so an unknown behaviour kind is
    // reported now rather than at the first frame that needed it.
    void setEntities(std::vector<EntityDesc> descs, std::uint32_t sceneSeed);
    void clear();
    [[nodiscard]] bool empty() const { return entities_.empty(); }
    [[nodiscard]] std::size_t size() const { return entities_.size(); }
    [[nodiscard]] const std::vector<std::unique_ptr<Entity>>& entities() const { return entities_; }
    [[nodiscard]] Entity* find(std::string_view name);
    [[nodiscard]] const Entity* find(std::string_view name) const;

    // The host's view of the nodes entities drive. Set before registerParameters().
    void setBindings(std::vector<NodeBinding> bindings);
    [[nodiscard]] const NodeBinding* binding(const std::string& node) const;

    void setNavigator(Navigator nav) {
        nav_ = std::move(nav);
        navPath_.setNavigator(&nav_);
    }
    [[nodiscard]] const Navigator& navigator() const { return nav_; }

    // ---- intent (ADR-096) ------------------------------------------------------------------

    // How a `move` action finds its way. Defaults to the straight-line provider over the
    // navigator set above, which is everything that exists today; §5/§6's planner installs itself
    // here and nothing else changes. The pointer is borrowed: the caller keeps it alive.
    void setPathProvider(const IPathProvider* path) { path_ = path; }
    [[nodiscard]] const IPathProvider& pathProvider() const { return path_ != nullptr ? *path_ : navPath_; }

    // Every completion, failure, skip and cancellation from the last update(), in the order they
    // happened. Cleared at the top of each update rather than freed, so reading them costs nothing
    // and producing them allocates nothing in steady state. This is the seam §17's event system
    // consumes; it is a list rather than a callback so a reader may run at its own cadence.
    [[nodiscard]] const std::vector<ActionEvent>& actionEvents() const { return actionEvents_; }
    // And a callback, for a caller that wants one the instant it happens.
    void setActionListener(ActionQueue::Listener listener) { actionListener_ = std::move(listener); }

    // Reads a property off another entity. Returns `fallback` when there is no such entity or it
    // declared no such property -- the caller that cares asks `Entity::hasProperty` first.
    [[nodiscard]] float property(std::string_view entity, std::string_view property,
                                 float fallback = 0.0f) const;

    // Starts / pauses / resumes an entity's routine. Named here because a director talks to the
    // world, not to an entity it had to find first.
    bool startRoutine(std::string_view entity, double now);
    bool pauseRoutine(std::string_view entity, double now);
    bool resumeRoutine(std::string_view entity, double now);
    // A director override: it preempts whatever the entity was doing and, when it drains, the
    // entity resumes rather than resets (ADR-091).
    bool direct(std::string_view entity, std::vector<ActionDesc> actions, double now);

    // Named places a behaviour may attend to: the scene's heroes, and any node an entity drives.
    // Set by the host, because only the host knows what the scene contains.
    void setLandmarks(std::vector<std::pair<std::string, glm::vec3>> landmarks) {
        landmarks_ = std::move(landmarks);
    }
    // Where `name` is, looking first at entities (which move) and then at landmarks (which do not).
    [[nodiscard]] bool pointOfInterest(std::string_view name, glm::vec3& out) const;

    // Registers every behaviour's knobs. Must run before any route or track is bound: a route
    // bound before its target exists is a route that does nothing, silently, forever.
    void registerParameters(params::ParameterSet& params, const std::string& prefix = "entity/");
    void unregisterParameters(params::ParameterSet& params);

    // Turns every entity's `reactions` into ordinary modulation routes. Targets that cannot be
    // resolved are appended to `problems` with the candidates that were tried -- never dropped in
    // silence. The returned routes are ready for Modulator::addRoute.
    [[nodiscard]] std::vector<params::ModRoute> compileReactions(const params::ParameterSet& params,
                                                                 std::vector<std::string>& problems) const;

    // Resolves a property path written on an entity against the node it drives. Returns an empty
    // string when nothing matches, filling `tried` with the candidates.
    [[nodiscard]] std::string resolveTarget(const Entity& entity, const std::string& target,
                                            const std::string& prefix,
                                            std::vector<std::string>* tried = nullptr) const;

    // Caches the parameter pointers entities write to. Safe to call repeatedly.
    void bind(params::ParameterSet& params, const std::string& prefix = "entity/");

    // Ticks behaviours and folds their offsets onto the driven nodes' transform *finals*. Runs
    // after modulation, so a route and a behaviour compose rather than overwrite.
    void update(const EntityUpdate& ctx, params::ParameterSet& params);

    // Puts every entity back to its start. Called on a seek.
    void reset();

    // Everything that could not be resolved, for the editor and the log. Never silently empty
    // because a problem was swallowed.
    [[nodiscard]] const std::vector<std::string>& problems() const { return problems_; }
    // Adds the host's own findings to the list the editor and the log read, so "what did not
    // resolve" has one answer rather than one per caller.
    void recordProblems(const std::vector<std::string>& problems) {
        problems_.insert(problems_.end(), problems.begin(), problems.end());
    }

    // How many entities ran at full rate, coarsely, and not at all on the last update.
    struct Counts {
        std::size_t full = 0;
        std::size_t coarse = 0;
        std::size_t skipped = 0;
    };
    [[nodiscard]] Counts counts() const { return counts_; }

private:
    void applyAttachments(const Entity& entity, params::ParameterSet& params) const;

    std::vector<std::unique_ptr<Entity>> entities_;
    std::vector<NodeBinding> bindings_;
    Navigator nav_{};
    NavigatorPath navPath_{&nav_};
    const IPathProvider* path_ = nullptr;
    std::vector<ActionEvent> actionEvents_;
    // Events raised between updates -- a director cancelling an override, a routine stopped --
    // held until the next update folds them in. Clearing at the top of an update without this
    // would throw away everything that happened while the engine was not ticking, which is
    // precisely when a director does its work.
    std::vector<ActionEvent> pendingEvents_;
    ActionQueue::Listener actionListener_;
    // The set the paths are resolved against. Cached because resolveTarget is const and is called
    // from compileReactions, which has one; refreshed by bind() and update().
    mutable const params::ParameterSet* params_ = nullptr;
    std::vector<std::pair<std::string, glm::vec3>> landmarks_;
    std::vector<std::string> problems_;
    std::vector<std::string> registered_;
    std::string prefix_ = "entity/";
    Counts counts_{};
    std::uint32_t sceneSeed_ = 0;
    // False between unregisterParameters() and the next registerParameters(). update() does
    // nothing while it is false: the behaviours' cached parameter pointers are stale then, and a
    // stale pointer that is merely usually fine is the kind of bug that surfaces on a scene swap.
    bool parametersLive_ = false;
};

// ---- serialisation ---------------------------------------------------------------------------
//
// The `entities` array of an avgen-scene document. Sibling of `nodes` and `heroes`, because an
// entity describes a node that the scene has already placed.

// `baseDir` is the folder a `"profile"` reference is resolved against -- the scene file's own, the
// same rule every other asset path in a scene file follows.
[[nodiscard]] Result<std::vector<EntityDesc>> entitiesFromJson(const nlohmann::json& j,
                                                               const std::filesystem::path& baseDir = {});
[[nodiscard]] nlohmann::json entitiesToJson(const std::vector<EntityDesc>& entities);
[[nodiscard]] Result<EntityDesc> entityFromJson(const nlohmann::json& j,
                                                const std::filesystem::path& baseDir = {});
[[nodiscard]] nlohmann::json entityToJson(const EntityDesc& entity);

// A behaviour profile: the reusable half of an entity, in its own file.
//
//     { "format": "avgen-entity-profile", "version": 1,
//       "behaviors": [...], "reactions": [...], "clips": {...} }
//
// "A hovering craft that answers the music" is a thing two scenes want, and copying twenty lines of
// JSON between them is how they stop being the same thing. An entity naming a profile takes its
// behaviours, reactions and clips, and may then add its own: the profile's come first, so a local
// reaction lands on top of a profile's on the same property rather than instead of it.
[[nodiscard]] Result<EntityDesc> profileFromJson(const nlohmann::json& j);
[[nodiscard]] Result<EntityDesc> loadProfile(const std::filesystem::path& path);

} // namespace avgen::entity
