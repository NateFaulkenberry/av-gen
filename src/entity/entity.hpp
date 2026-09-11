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
#include "entity/behavior.hpp"
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
#include <span>
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
    // Whether this entity has ever been ticked. Behaviour level of detail may not suppress the
    // *first* update: an entity that has never run has never published a LocomotionState, so
    // skipping it hands the animation layer a position of (0,0,0) and a character pops in from the
    // world origin on whichever frame it first comes close enough to matter.
    bool everUpdated_ = false;

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
        refreshInterestPoints();
    }
    [[nodiscard]] const Navigator& navigator() const { return nav_; }

    // Named places a behaviour may attend to: the scene's heroes, and any node an entity drives.
    // Set by the host, because only the host knows what the scene contains.
    void setLandmarks(std::vector<std::pair<std::string, glm::vec3>> landmarks) {
        landmarks_ = std::move(landmarks);
        refreshInterestPoints();
    }
    // Where `name` is, looking first at entities (which move) and then at landmarks (which do not).
    [[nodiscard]] bool pointOfInterest(std::string_view name, glm::vec3& out) const;

    // Places worth going to (§6). Assembled from three sources, which is why it is derived rather
    // than set: the host's landmarks, the entities that move, and whatever the navigation grid
    // noticed about the terrain while it was being built. `extras` is for what only the host knows
    // -- a patch of glowing ecology is a point of interest and nothing else in here can see one.
    void setExtraInterestPoints(std::vector<InterestPoint> extras);
    [[nodiscard]] std::span<const InterestPoint> interestPoints() const { return interests_; }
    // Recomputes the list. Called by setLandmarks and setNavigator, so a host that uses those gets
    // interests without asking; call it directly after moving something that is one.
    void refreshInterestPoints();

    // Characters not walking through each other (§11 of the world-authoring brief).
    //
    // Rebuilt once per update from every entity that declared a radius, into the same uniform grid
    // the static obstacles use -- so separation costs a disc query per character rather than a pass
    // over every other character. With three entities that distinction is academic; with a crowd it
    // is the whole thing, and building it on N^2 now would mean rewriting it later.
    //
    // It is deliberately *not* part of the navigator's obstacle set. A route is planned over a world
    // that is not moving; who is standing where is a fact about this frame, and folding it into the
    // graph would have every character replanning every time anyone walked past.
    [[nodiscard]] const spatial::ObstacleField& crowd() const { return crowd_; }
    // The push that takes entity `self` out of the other bodies it is overlapping, in world XZ.
    // Zero when it is clear, which is the usual answer.
    [[nodiscard]] glm::vec2 crowdSeparation(std::size_t self, glm::vec2 p, float radius) const;

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

    // Puts every entity back to its start.
    void reset();

    // Puts every entity where it would have been at `time` had the timeline been played from zero
    // (ADR-093). Resets, then re-simulates the behaviour layer at a fixed step.
    //
    // Why re-simulation rather than evaluation. A character that plans a route, steers round a
    // trunk and is pushed out of a rock has a position that depends on its history: there is no
    // closed form for "where would it be at t = 94 s", and pretending otherwise would mean throwing
    // away the obstacle avoidance that makes it worth watching. Re-simulation is the honest answer,
    // and at a fixed step it is *reproducible*, which is the property that was actually missing --
    // before this, seeking to the same second twice could leave a character in two different
    // places, and nothing called `reset` at all.
    //
    // Two things it deliberately does not do. It does not write to the parameter set: the next
    // ordinary update folds the offsets on, and writing them here would accumulate onto finals that
    // only a real frame clears. And it does not replay the audio analysis -- the default `bus` is
    // null, so a re-simulated character walks its autonomous walk without the music.
    //
    // That second one is a choice, and it is the one that makes the guarantee statable. Handing the
    // *current* bus to eighteen hundred re-simulated steps is not a replay of anything: it applies
    // one instant of the music uniformly across half a minute, and it makes the answer depend on
    // where the playhead happened to be when the seek was requested -- so seeking twice to the same
    // second would still give two different frames, which is the defect. What is promised here is
    // that the same seek time always produces the same state. Matching a played-through timeline
    // exactly would need the analysis replayed too; an offline render plays from zero and never
    // seeks, so it is exact either way.
    //
    // `maxSeconds` bounds the work: seeking an hour into a piece must not stall for a minute.
    // Beyond it the simulation starts from `time - maxSeconds`, which costs a character its
    // accumulated history and keeps the editor responsive.
    // `params`, when given, is put back to its authored values first: behaviours read parameter
    // *finals*, and a final still holding the last played frame's modulation would make the answer
    // depend on where the playhead came from -- which is the whole thing being fixed. The next
    // ordinary frame recomputes them.
    void seek(double time, params::ParameterSet* params = nullptr,
              const signals::SignalBus* bus = nullptr, glm::vec3 viewPosition = {},
              double step = 1.0 / 60.0, double maxSeconds = 90.0);

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
    // The set the paths are resolved against. Cached because resolveTarget is const and is called
    // from compileReactions, which has one; refreshed by bind() and update().
    mutable const params::ParameterSet* params_ = nullptr;
    std::vector<std::pair<std::string, glm::vec3>> landmarks_;
    std::vector<InterestPoint> interests_;
    std::vector<InterestPoint> extraInterests_;
    // One obstacle per entity with a body, in entity order, so an index into `entities()` is an
    // index into this. Rebuilt every update: bodies move.
    spatial::ObstacleField crowd_;
    std::vector<std::size_t> crowdOwner_; // crowd obstacle index -> entity index
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
