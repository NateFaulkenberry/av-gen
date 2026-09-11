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
#include "entity/field.hpp"
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
    // Whether a music influence field is allowed to scale this reaction's depth (ADR-097). True by
    // default, which is what makes an existing `audio.bass -> emissiveGain` spatial for free. Set
    // false for a reaction whose neutral output is not zero -- a Multiply route whose chain rests
    // at 1 is scaled to silence by a gain of 0, which is a property being *muted* rather than a
    // reaction being *quiet*, and an author has to be able to say so.
    bool spatial = true;
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
    // What this entity *is*, for a trigger volume or a field to filter on (ADR-097): "dancer",
    // "pedestrian", "vehicle". A field matches an entity on its tags or on the name of the profile
    // it was built from, so "every NPC built from the dancer profile" is one word in a scene file
    // rather than a list of forty names that goes stale.
    std::vector<std::string> tags;
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
    std::size_t profileTags = 0;

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

// What the field pass needs. The bus is not const here because a field *publishes*: occupancy and
// the enter/exit edges become ordinary named signals, which is how a light, a material or a
// particle system reacts to a volume without any of them learning what a volume is (§39-§43).
struct FieldUpdate {
    double time = 0.0;
    double dt = 0.0;
    signals::SignalBus* bus = nullptr;
    glm::vec3 viewPosition{0.0f}; // the same three-band behaviour LOD the behaviour pass uses
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

    // ---- fields (ADR-097) --------------------------------------------------------------------

    // The spatial gain the fields governing this entity settled on this frame. Exactly 1 when no
    // field governs it, so an entity in a scene with no fields is bit-for-bit what it was before
    // fields existed. Larger than 1 when a field's `strength` boosts it.
    [[nodiscard]] float influence() const { return influence_; }
    // False when no field's filter matches this entity at all, which is the case that costs
    // nothing: an ungoverned entity never enters the broad phase.
    [[nodiscard]] bool governedByField() const { return governed_; }
    // Where this entity is, for a field query. The parameter delta is what makes a node the
    // timeline drives report the position it has *at this instant* rather than where it was
    // authored -- which is what makes a field over a baked actor a pure function of time.
    [[nodiscard]] glm::vec3 fieldPosition() const;

    // §21. The reaction arc: 0 when nothing is happening, rising to the drawn intensity for the
    // hold and falling back over the release. The animation layer reads it as LocomotionState's
    // `reaction`; nothing here ever resets a behaviour, which is what makes the arc end in
    // `Walking` rather than in `Idle`.
    [[nodiscard]] float arcLevel() const { return arcLevel_; }
    [[nodiscard]] bool arcActive() const { return arcPhase_ != ArcPhase::None; }
    // Which field triggered the arc that is running, or kNoField.
    static constexpr std::uint32_t kNoField = 0xFFFFFFFFu;
    [[nodiscard]] std::uint32_t arcField() const { return arcField_; }
    // How many times this entity has entered each field, in field order. The seeded draw for an
    // arc is a pure function of (seed, field name, this count), so a replay redraws exactly.
    [[nodiscard]] const std::vector<std::uint32_t>& fieldEntryCounts() const { return entryCount_; }

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

    // ---- field state (ADR-097) ---------------------------------------------------------------
    enum class ArcPhase : std::uint8_t { None, Delay, Hold, Release };

    float influence_ = 1.0f;
    float fieldFloor_ = 0.0f;  // the largest `floorGain` of the fields that govern this entity
    bool governed_ = false;
    std::vector<std::uint8_t> insideField_;   // per field: was this entity inside it last frame
    std::vector<std::uint8_t> insideNow_;     // per field: is it inside it this frame
    std::vector<std::uint8_t> governedBy_;    // per field: does this field's filter match at all
    std::vector<std::uint32_t> entryCount_;   // per field: how many enter edges so far
    ArcPhase arcPhase_ = ArcPhase::None;
    std::uint32_t arcField_ = kNoField;
    double arcTimer_ = 0.0;
    float arcDelay_ = 0.0f;
    float arcHold_ = 0.0f;
    float arcRelease_ = 0.0f;
    float arcIntensity_ = 0.0f;
    float arcLevel_ = 0.0f;
    bool arcHoldStill_ = false;
    Activity arcActivity_ = Activity::React;
    double arcCooldownUntil_ = -1.0e30;
    double fieldAccum_ = 0.0;

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

    void setNavigator(Navigator nav) { nav_ = std::move(nav); }
    [[nodiscard]] const Navigator& navigator() const { return nav_; }

    // ---- trigger volumes and music influence fields (ADR-097) --------------------------------

    // Replaces the field set. Call *before* registerParameters(): a field's strength, scale, inner
    // ratio, floor and centre are ordinary params::Parameters, registered there, for the same
    // reason a behaviour's knobs are (ADR-088) -- an authored number that cannot be keyframed is a
    // number that stops being interesting the moment a shot needs it to change.
    //
    // Which fields govern which entities is decided at bind(), because a filter is a function of an
    // entity's tags and its profile and neither changes at runtime, so matching costs nothing per
    // frame.
    void setFields(std::vector<FieldDesc> fields);
    [[nodiscard]] const std::vector<FieldDesc>& fields() const { return fields_; }
    [[nodiscard]] const std::vector<FieldRuntime>& fieldRuntime() const { return fieldRuntime_; }
    // One line per field, saying what it resolved to and which determinism guarantee it therefore
    // has (ADR-091). Written at bind and logged at install, unconditionally, and available to the
    // editor -- because "a field on a baked actor is scrub-exact and a field on a live entity is
    // not" is a distinction an author has to be able to *read*, not one they discover by rendering
    // the same frame twice. No panel draws it yet; the log and `requireScrubExact` are what exist.
    [[nodiscard]] const std::vector<std::string>& fieldReport() const { return fieldReport_; }
    // The parameter namespace a field's knobs live in -- `<prefix>fields/<name>/strength` and the
    // rest. Public because the host has to be able to tell an author that a *modulation route*
    // pointed here does nothing: the field pass runs before the routes, deliberately, so a field
    // knob is keyframeable and presettable but not modulatable, and that is worth one warning
    // rather than an afternoon.
    [[nodiscard]] std::string fieldParameterPrefix() const { return prefix_ + "fields/"; }

    // The field pass. Runs *before* the modulation routes, unlike update(), because its whole
    // output is a gain on those routes: a frame late here is a frame late on every reaction in the
    // scene, and it would make a scrubbed frame depend on the frame before it.
    void updateFields(const FieldUpdate& ctx, params::ParameterSet& params);
    // Writes each entity's influence onto the routes its own `reactions` produced. O(routes), and
    // it leaves every route that is not an entity reaction exactly alone.
    void applySpatialGain(std::vector<params::ModRoute>& routes) const;
    // The enter and exit edges of the last field pass, in field-then-entity order. Cleared and
    // refilled every pass; empty in a scene with no fields.
    [[nodiscard]] const std::vector<TriggerEvent>& triggerEvents() const { return triggerEvents_; }

    // What the last field pass actually did. Read by the budget test; the material an editor
    // overlay would draw from (§45/§46), which does not exist yet.
    struct FieldCounts {
        std::size_t governed = 0;  // entities at least one field's filter matches
        std::size_t queried = 0;   // entities the broad phase put in the grid this frame
        std::size_t tested = 0;    // exact point-in-volume tests performed
        std::size_t inside = 0;    // (entity, field) pairs that came out inside
        std::size_t cells = 0;     // grid cells the build produced
    };
    [[nodiscard]] FieldCounts fieldCounts() const { return fieldCounts_; }

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
    // The set the paths are resolved against. Cached because resolveTarget is const and is called
    // from compileReactions, which has one; refreshed by bind() and update().
    mutable const params::ParameterSet* params_ = nullptr;
    std::vector<std::pair<std::string, glm::vec3>> landmarks_;
    std::vector<std::string> problems_;
    std::vector<std::string> registered_;
    std::string prefix_ = "entity/";
    Counts counts_{};

    // ---- fields (ADR-097) --------------------------------------------------------------------
    std::vector<FieldDesc> fields_;
    std::vector<FieldRuntime> fieldRuntime_;
    std::vector<std::string> fieldReport_;
    std::vector<TriggerEvent> triggerEvents_;
    EntityGrid grid_;
    FieldCounts fieldCounts_{};
    // Scratch reused between frames so a field pass allocates nothing once it has run once.
    std::vector<glm::vec3> gridPoints_;
    std::vector<std::uint32_t> gridEntity_;
    std::vector<std::uint8_t> enteredThisFrame_;
    std::vector<std::uint8_t> exitedThisFrame_;
    bool fieldsBound_ = false;

    [[nodiscard]] static bool needsNode(const Entity& entity);
    void bindFields();
    // One field's live knobs. Resolved at registerParameters(); null when a field was added after
    // it, which bind() then fixes.
    struct FieldParams {
        params::Parameter<float>* strength = nullptr;
        params::Parameter<float>* scale = nullptr;
        params::Parameter<float>* inner = nullptr;
        params::Parameter<float>* floorGain = nullptr;
        params::Parameter<glm::vec3>* center = nullptr;
    };
    std::vector<FieldParams> fieldParams_;
    // This frame's fields with their live knobs folded in. A member so the pass allocates nothing.
    std::vector<FieldDesc> resolved_;
    [[nodiscard]] FieldDesc resolvedField(std::size_t index) const;
    [[nodiscard]] bool fieldMatches(const FieldDesc& field, const Entity& entity) const;
    [[nodiscard]] bool resolveFieldSource(const FieldDesc& field, glm::vec3& out,
                                          FieldAuthority& authority) const;
    void beginArc(Entity& entity, std::uint32_t fieldIndex, double time);
    void advanceArc(Entity& entity, double dt);

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

class ProfileLibrary;

// `baseDir` is the folder a `"profile"` reference is resolved against -- the scene file's own, the
// same rule every other asset path in a scene file follows. `library`, when given, is consulted
// *first*: a `"profile"` that names an entry in it is a library reference, and only a name the
// library does not have is treated as a path.
[[nodiscard]] Result<std::vector<EntityDesc>> entitiesFromJson(const nlohmann::json& j,
                                                               const std::filesystem::path& baseDir = {},
                                                               const ProfileLibrary* library = nullptr);
[[nodiscard]] nlohmann::json entitiesToJson(const std::vector<EntityDesc>& entities);
[[nodiscard]] Result<EntityDesc> entityFromJson(const nlohmann::json& j,
                                                const std::filesystem::path& baseDir = {},
                                                const ProfileLibrary* library = nullptr);
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

// A profile *library*: many named profiles in one file, referenced by name (ADR-097).
//
//     { "format": "avgen-entity-profile-library", "version": 1,
//       "profiles": { "dancer": { "behaviors": [...], "reactions": [...], "clips": {...} },
//                     "barfly": { ... } } }
//
// A scene names the library once, as `"entityProfiles": "profiles/night-shift.json"`, and each
// entity then says `"profile": "dancer"`. One file is read once for a crowd of forty, instead of
// forty path resolutions of forty copies of the same twenty lines -- which is §20's actual
// complaint. A per-entity `"profile"` that is a *path* still works and is still resolved against
// the scene's folder, so nothing written before this file existed has to change.
//
// A library entry is an ordinary profile and obeys the same rule: profiles do not chain. One level
// of indirection is a library, two is a maze.
class ProfileLibrary {
public:
    [[nodiscard]] const EntityDesc* find(std::string_view name) const;
    void add(std::string name, EntityDesc profile);
    [[nodiscard]] bool empty() const { return profiles_.empty(); }
    [[nodiscard]] std::size_t size() const { return profiles_.size(); }
    // Every name it holds, in file order. What a diagnostic prints when a lookup misses, because
    // "no such profile" without the list is how a typo costs an afternoon.
    [[nodiscard]] std::vector<std::string> names() const;
    [[nodiscard]] const std::string& source() const { return source_; }
    void setSource(std::string source) { source_ = std::move(source); }

private:
    std::vector<std::pair<std::string, EntityDesc>> profiles_;
    std::string source_;
};

[[nodiscard]] Result<ProfileLibrary> profileLibraryFromJson(const nlohmann::json& j);
[[nodiscard]] Result<ProfileLibrary> loadProfileLibrary(const std::filesystem::path& path);

} // namespace avgen::entity
