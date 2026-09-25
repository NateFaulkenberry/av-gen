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
#include "entity/airborne.hpp"
#include "entity/locomotion_plan.hpp"
#include "entity/motion_chain.hpp"
#include "entity/motion_controller.hpp"
#include "entity/action.hpp"
#include "entity/behavior.hpp"
#include "entity/gait.hpp"
#include "entity/field.hpp"
#include "entity/character_ai.hpp"
#include "entity/perception.hpp"
#include "entity/mind.hpp"
#include "entity/locomotion.hpp"
#include "entity/navigation.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/scene_types.hpp"
#include "signals/signal_bus.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
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

// ADR-623: this body's base pose chosen by the Phase C motion matcher, with the clip provider as
// its fallback. **Opt-in per character, default off.** Present in a scene as
//
//     "motionMatching": { "joints": ["foot.l", "foot.r", "head.x"],
//                         "contacts": ["foot.l", "foot.r"],
//                         "trajectory": [0.2, 0.4, 0.6] }
//
// `joints` are the feature joints and are required: which joints carry a character's identity is
// a property of the character (§8). `contacts` are the joints analysed for foot plants, which is
// where an in-place clip's implied travel comes from. They default to `joints`. `trajectory` is
// the horizons in seconds, and defaults to the three §24 validated. The key implies
// `proceduralMotion`, because the matcher is a provider and providers only run on that path.
struct MotionMatchingDesc {
    bool enabled = false;
    std::vector<std::string> joints;
    std::vector<std::string> contacts;
    std::vector<float> trajectory{0.2f, 0.4f, 0.6f};
    // §64/§92: a MotionPack to match on instead of the rig's own clips, for motion retargeted onto
    // this rig (e.g. 100STYLE through `--positional-legs`). As authored (relative to the scene file)
    // and as resolved at load. It must be built for this rig's skeleton; if it is missing or is
    // not, the body falls back to its clip provider and the log says why.
    std::string pack;
    std::string packResolved;
    // §14: which clips the matcher may use, as name prefixes ("Walking" admits `Walking_crouch` and
    // `Walking~turn+1.40`). Empty admits every clip. The scout's pack has no authored tags, so an
    // action clip like `Button_push` otherwise serves as an idle; this is the authoring surface
    // that says it may not.
    std::vector<std::string> clips;
    // §45: the search weights, as a **versioned** block so a scene saved under one meaning of a
    // weight is not silently read under another. `weightsVersion` 0 means "no block": the engine's
    // defaults. Version 1 is the seven feature-term weights of `MotionFeatureConfig` by their
    // names there, plus the two search penalties of `MotionCostWeights`.
    std::uint32_t weightsVersion = 0;
    float jointPositionWeight = 1.0f;
    float jointVelocityWeight = 0.4f;
    float trajectoryPositionWeight = 1.0f;
    float trajectoryFacingWeight = 0.5f;
    float rootVelocityWeight = 1.0f;
    float phaseWeight = 0.0f;
    float contactWeight = 0.0f;
    float continuityWeight = -1.0f; // below zero: the engine default
    float transitionWeight = -1.0f;
    float styleWeight = -1.0f;      // §44, a fraction of the cost spread; below zero: the default
    float switchMargin = -1.0f;     // §28, a fraction of the cost spread; below zero: the default
    // §44: this character's style, and which clips carry which style, as name prefixes like
    // `clips`. The matcher prefers clips in the style (a cost, not a filter). Empty style: none.
    std::string style;
    std::vector<std::pair<std::string, std::vector<std::string>>> styles;
    friend bool operator==(const MotionMatchingDesc&, const MotionMatchingDesc&) = default;
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
    // Phase D §18: what this body can do -- "can_walk", "can_inspect", "can_touch". An interaction a
    // prop offers lists what it `required`; the two meeting is a valid interaction. Data, never a
    // branch on what kind of character this is (§77).
    std::vector<std::string> capabilities;
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

    // ---- procedural motion (Phase B) -------------------------------------------------------
    //
    // **Opt-in, and default off.** When set, this body's base pose comes from its
    // `entity::MotionChain` instead of from `scene::AnimationPlayer`. Off by default because the
    // change touches the path every shipping scene renders through, and "default off is provably
    // inert" is a claim that can be measured rather than asserted -- see the rendered-hash
    // comparison in `docs/design/procedural-character-motion.md`.
    bool proceduralMotion = false;
    // ADR-822: what this body can do off the ground -- its hop's apex and gravity, the farthest it
    // will leap, how long it recovers, and the highest it CAN leap (`maxApex`, for a director's
    // validator). One block per character, read by `explore`'s autonomous hops and by the Director's
    // capability card alike, so the two cannot disagree about what the body is capable of.
    JumpSettings jump{};
    // ADR-623. The matcher in front of the clip provider; see `MotionMatchingDesc`.
    MotionMatchingDesc motionMatching;
    // Phase B §8-§11. Start, stop, turn-in-place and strafe, on top of the gait's clip family.
    LocomotionPlanSettings locomotion;

    // ---- the senses (ADR-270, ADR-290) ----
    //
    // What this body can notice, and whether it notices anything at all. `perceives` is the whole
    // of the opt-in: it is set by the presence of the scene file's `"perception"` key and by
    // nothing else, so a craft that hovers, a rock that spins and the nine farm animals of
    // Glowmere pay exactly nothing for a stage they were never going to read.
    //
    // A flag rather than `range <= 0` as the sentinel, because a character whose range an author
    // has keyframed down to zero for a shot is a character that has been *blinded*, and that is a
    // different fact from one that has no senses. `perceptionFromJson` refuses a range of zero for
    // the same reason.
    PerceptionSettings perception;
    bool perceives = false;
    // Phase D §9 "target categories, attention weighting": the perception block's `"tags"` --
    // `{"ufo": 4.0}` -- a taste weight per semantic tag, alongside the per-kind `weight`s. A percept
    // carrying a weighted tag is ranked by the larger of its kind's weight and its tags', so a
    // saucer forty metres off is not cut by the capacity limit in favour of the twelve shore points
    // nearer the body. Empty -- every scene before Phase D -- and the ranking is exactly the old one.
    std::vector<std::pair<std::string, float>> perceptionTags;

    // ---- personality (Phase D §29) ----
    //
    // Nine traits, 0.5 neutral, and a neutral trait changes no score anywhere (`mind.hpp`). The
    // key's presence is the opt-in, exactly as `perception`'s is: registered as parameters under
    // `entity/<name>/personality/<trait>` only for a body that declared one, so a reaction can make
    // a character bolder on a drop and the path set says which bodies have a temperament.
    Personality personality;
    bool hasPersonality = false;

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
    // The node's authored world facing, radians about +Y, measured the way `EntityState::yaw` is.
    //
    // The counterpart of `anchor`, and it exists for the same reason (ADR-240). An entity's
    // *position* was always absolute -- `anchor` is where the author put it and `travel` is
    // displacement from there -- while its *facing* was purely additive: the drawn rotation was
    // the author's rotation plus `yaw`, and `yaw` started at zero. So a body placed at a random
    // heading, which is what a scattered animal is, walked along `yaw` and was drawn along
    // `placement + yaw`, permanently that many degrees off. Every farm animal in Glowmere Valley 2
    // carries a scatter facing between -165 and +156 degrees; the four aliens carry none, which is
    // the only reason ADR-204's facing work did not find this.
    //
    // With this, `yaw` *starts* at the authored facing and the write is the difference, so the
    // placement still places the body and the steering still steers it -- and they are the same
    // angle rather than two angles added together.
    float facing = 0.0f;
};

// `InterestKind`, `InterestPoint` and `interestKindName` used to be declared here and are now in
// `entity/behavior.hpp` (ADR-290). They moved because `entity/character_ai.hpp` §2 declares a
// `Percept` carrying an `InterestKind`, and that header has to be includable by *this* one now that
// `EntityDesc` carries a `PerceptionSettings`. Nothing about them changed and every name still
// resolves through this header, which includes `behavior.hpp`.

// ---- runtime ---------------------------------------------------------------------------------

struct EntityUpdate {
    double time = 0.0;
    double dt = 0.0;
    std::uint64_t frameIndex = 0;
    const signals::SignalBus* bus = nullptr;
    glm::vec3 viewPosition{0.0f}; // where the camera is, for behaviour level of detail
    // ADR-186: false lifts `EntityDesc::cullDistance` and `fullDetailDistance` -- every entity is
    // updated every frame at full detail however far from the view it is. Set by an offline render,
    // where the budget that made the bands worth having does not apply, and where a motionless far
    // herd is a visible defect rather than a saving.
    bool distanceDetail = true;
};

// What the field pass needs. The bus is not const here because a field *publishes*: occupancy and
// the enter/exit edges become ordinary named signals, which is how a light, a material or a
// particle system reacts to a volume without any of them learning what a volume is (§39-§43).
struct FieldUpdate {
    double time = 0.0;
    double dt = 0.0;
    signals::SignalBus* bus = nullptr;
    glm::vec3 viewPosition{0.0f}; // the same three-band behaviour LOD the behaviour pass uses
    bool distanceDetail = true;   // ADR-186; see EntityUpdate
};

// ---- the Director tier's hold on a body (ADR-210) ----------------------------------------------
//
// ADR-091's hierarchy is Director -> Cinematic Action -> Behavior -> Navigation, and `ActionQueue`
// already implements the top three for anything that *walks*: a `move` routes over the navigation
// layer, snaps to the ground it is crossing and hands the gait a speed. A craft in the air and a
// cow going up a tractor beam are neither walking nor standing on anything, and expressing them as
// `move` would mean teaching the walker's path provider to lie about where the ground is.
//
// So this is the other half of the same tier, and deliberately the *smallest* half: a director says
// where a body **is**, absolutely, and everything below composes around that. `position` becomes
// `travel` (so `EntityState::position()`, the crowd field, the fields pass and every query that
// reads an entity's place all agree), `driven` is raised (so `wander` and `explore` yield by
// keeping their destination, exactly as they do under an action), and `rotation` is added to the
// behaviours' own offsets *after* they run -- which is what lets the visitor keep hovering, drifting
// and banking while it is being flown somewhere.
//
// Absolute rather than relative because a director's claim is "the saucer is here now", not "the
// saucer has drifted nine metres from where the scene file put it": an offset would make the answer
// depend on the anchor, and an anchor is an authoring decision that must be free to change.
struct DirectorMotion {
    bool active = false;
    glm::vec3 position{0.0f}; // world
    float yaw = 0.0f;         // radians about +Y
    bool hasYaw = false;
    glm::vec3 rotation{0.0f}; // extra Euler degrees, added after the behaviours have had their turn
    // The horizontal speed the gait should read. What makes an animal being carried up a beam keep
    // its legs going: the gait machine picks a clip from a speed, and it does not care whether the
    // speed came from navigation or from a director.
    float speed = 0.0f;
    bool hasSpeed = false;
    // A scripted performance (ADR-758) owns the body OUTRIGHT, which staging's motions do not claim:
    //  * its position is re-asserted after the behaviours, so none that moves the body itself (an
    //    `orbit` with simulation authority, `explore`'s airborne hop) can overwrite it on the step;
    //  * its speed IS the body's speed, read by the gait unramped -- through ADR-620's acceleration
    //    limit a run-in cut to at 6 m/s read as a walk for its first second.
    // Off for staging, so a carried animal's legs still ramp and nothing staged changes.
    bool performance = false;
    // ADR-820, performances only: the actor names a clip now, so the gait must not push its own.
    bool clipOwned = false;
    // ADR-823, performances only: the actor is in a local slow-motion (or fast-motion) window at
    // this rate. `speed` is the path's speed ON THE TIMELINE; the gait chooses its clip from
    // `speed / timeScale` -- the speed the performance was authored at -- and plays it at
    // `timeScale`, so a slowed run stays a run, slowed, rather than becoming a walk.
    float timeScale = 1.0f;
};

// ADR-824 (Phase D §35): a goal a director gives a character at runtime -- "investigate the Umbra
// cap" from 32 s. It fills the character's `goal` considerer instead of the authored subject, so the
// character still decides the how (path, approach, affordance) and still weighs it against
// everything else it wants: a goal is a bias, never an order. State, so a checkpoint carries it and
// a scrub lands on the same goal a play had. Needs a `goal` considerer in the decider -- authored
// with no subject, it is an empty slot a director can fill.
struct DirectorGoal {
    bool active = false;
    std::string subject;    // an entity or a landmark
    std::string affordance; // the verb to use when offered and capable; empty: observe it
    double since = 0.0;     // when it was given: the "done it since" test counts from here
};

// ADR-820: where a body was when a performance with an entry blend took it -- the point the blend
// starts from. Entity state, so a checkpoint carries it and a seek into the blend lands where a
// play does.
struct PerformanceEntry {
    bool active = false;
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
};

// What answered a `socketTransform` call (ADR-274). Three outcomes, because the two that used to
// share `true` are different facts about the frame a prop is about to be put in.
enum class SocketResolution : std::uint8_t {
    // This entity declares no socket of that name. `out` is untouched.
    None,
    // The socket resolved against the entity's own frame: no skeleton is installed, the socket
    // named no joint, or the skeleton has no joint of that name / is not posed yet. Correct for a
    // craft, approximate for a character, and now sayable.
    EntityFrame,
    // The socket resolved against a posed joint of an installed skeleton.
    Joint,
};
// True when `out` was written -- the predicate the old `bool` return meant, for the callers that
// only need to know whether there is a place to put something.
[[nodiscard]] constexpr bool resolved(SocketResolution r) { return r != SocketResolution::None; }

// An entity's behaviours, as a *value* (ADR-700). A plain vector of `unique_ptr` cannot be copied,
// and that made `Entity` uncopyable -- which is the only thing that stood between a checkpoint and
// "copy the whole entity". With this the compiler writes `Entity`'s copy constructor and copy
// assignment, so a checkpoint holds every member an entity has, including ones added after
// ADR-700 was written. Copy-constructing clones each behaviour; copy-assigning copies each
// behaviour's state into the one already there (`IBehavior::assignState`), so the live objects keep
// their addresses across a restore.
class BehaviorList : public std::vector<std::unique_ptr<IBehavior>> {
public:
    BehaviorList() = default;
    BehaviorList(BehaviorList&&) noexcept = default;
    BehaviorList& operator=(BehaviorList&&) noexcept = default;
    BehaviorList(const BehaviorList& other);
    BehaviorList& operator=(const BehaviorList& other);
    ~BehaviorList() = default;
};

class Entity {
public:
    Entity(EntityDesc desc, std::uint32_t sceneSeed);
    // Copyable, member for member, for a checkpoint (ADR-700). Deliberately implicit: every member
    // is a value or a borrowed pointer the host keeps alive, and a checkpoint is only restored into
    // the entity it was taken from while the inputs that produced it are unchanged. Anything new
    // added below is copied without anyone having to remember it -- which is the point.
    Entity(const Entity&) = default;
    Entity& operator=(const Entity&) = default;
    Entity(Entity&&) = default;
    Entity& operator=(Entity&&) = default;
    ~Entity() = default;

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
    // ADR-541: the provider's memory is a value the ENTITY owns, because `EntityWorld::seek`
    // reconstructs it by replay and a provider that owned it would be the one object in the
    // character pipeline a scrub could not rewind.
    [[nodiscard]] const MotionMemory& motionMemory() const { return motionMemory_; }
    // **Staged and dark** (ADR-615): `motionState_` is touched in exactly one place in the whole
    // tree -- `EntityWorld::reset` calls `.reset()` on it -- and this accessor has no callers. The
    // controller that would fill it, `entity::stepMotion`, is reached through `predictTrajectory`,
    // whose product caller is the matcher's query (§25), which predicts from the body's state
    // rather than moving it. Behaviours move the body; this integrator does not.
    [[nodiscard]] const MotionState& motionState() const { return motionState_; }
    [[nodiscard]] const MotionChainResult& motionChainResult() const { return motionChainResult_; }
    [[nodiscard]] const LocomotionPlanState& locomotionPlan() const { return locomotionPlan_; }
    // Borrowed, owned by whoever built the providers (the composition). Null means this body is
    // driven by its clips, which is every body until a scene opts one in.
    void setMotionChain(const MotionChain* chain) { motionChain_ = chain; }
    [[nodiscard]] const MotionChain* motionChain() const { return motionChain_; }
    // Advance the provider memory one step. Called from BOTH publish paths -- ADR-554's rule,
    // applied to the very thing that rule was discovered by.
    void advanceMotion(double time, float dt);
    // Phase B §46. Publishes `hasLookTarget` **and the schedule the pose tier blends against**, in
    // one place called by both `EntityWorld::update` and `EntityWorld::seek`. One function rather
    // than two copies of three lines for ADR-554's reason: this struct is the one that rule was
    // discovered on, and a schedule written by only one of its two publishers is the same defect
    // with a longer name.
    void publishLookSchedule();
    // Where the body is **drawn**, as distinct from where the simulation says it is.
    //
    // `state().position()` is the anchor plus `travel` -- what navigation and a director wrote. The
    // behaviours' offsets (hover, drift, bank, an authored sway) are folded onto the node's
    // transform *afterwards* (`applyOffsets`), deliberately, so that a craft keeps hovering and
    // drifting while it is being flown somewhere. The consequence is that the two numbers are not
    // the same place: Glowmere's saucer carries `drift` with a radius of 2.4 m, so its node -- and
    // everything parented to it, a tractor beam included -- is drawn up to 2.4 m away from
    // `state().position()`.
    //
    // Anything that has to line up with what is on screen wants this one.
    // `EntityWorld::pointOfInterest` has always returned exactly this sum; this is it, named.
    [[nodiscard]] glm::vec3 visualPosition() const { return state_.position() + motion_.position; }
    [[nodiscard]] std::uint32_t seed() const { return seed_; }

    // Where an attached prop should sit, and -- the half that was missing -- *what answered*.
    //
    // ADR-274. This used to return a bare `bool`, and it returned `true` for a socket resolved
    // against a real posed joint and `true` for one that fell back to the entity's own frame
    // because nothing had installed an `ISkeletonQuery`. Since nothing ever did, every socket in
    // this engine took the fallback and said it had not: a beam authored onto a hand was drawn at
    // the body's origin and reported success (ADR-262 is what that costs). A caller that cannot
    // tell an approximation from an answer cannot choose to refuse the approximation, which is the
    // whole of why the return type changed rather than gaining an optional out-parameter.
    [[nodiscard]] SocketResolution socketTransform(std::string_view socket, scene::Transform& out) const;

    // Installed by the animation layer; all three may stay null forever (see locomotion.hpp).
    void setPoseSink(IPoseSink* sink) { pose_ = sink; }
    void setSkeleton(const ISkeletonQuery* skeleton) { skeleton_ = skeleton; }
    // ADR-337. The return half of the seam: what the clip has carried the body by, so the
    // simulation can stop disagreeing with the drawing about where the body went.
    void setRootMotionSource(const IRootMotionSource* source) { rootMotion_ = source; }
    // How much root motion this body took from the animation on its last update, in world metres.
    // Zero for every body playing a clip nobody opted in, which is every body in this repository
    // outside the Character Intelligence Lab. Reported rather than inferred, because "it did
    // nothing" and "it was asked for nothing" have to be different sentences (ADR-274).
    [[nodiscard]] const glm::vec3& rootMotionStep() const { return rootMotionStep_; }

    // The entity's behaviours, in declaration order.
    [[nodiscard]] const std::vector<std::unique_ptr<IBehavior>>& behaviors() const { return behaviors_; }

    // ---- the senses (ADR-270, ADR-290) -------------------------------------------------------

    // What this body noticed at its last sense tick, best first. Empty for a body that declared no
    // `perception` block, and empty for one that has not been ticked yet.
    //
    // A span over storage the entity owns and reuses, so reading it costs nothing and producing it
    // allocates nothing in steady state -- the same arrangement `actionEvents` has. It is
    // deliberately **not** a memory: it holds what the last tick found, and nothing accumulates
    // across ticks. ADR-267's D4 is what decides that -- anything a character remembers must be
    // recoverable by re-simulating from `t - maxSeconds`, and a working set rebuilt from scratch
    // every tick is recoverable by construction.
    [[nodiscard]] std::span<const Percept> percepts() const {
        return std::span<const Percept>(percepts_.data(), perceptCount_);
    }
    // The settings this body senses with **as the parameters currently read**, not as the scene
    // file wrote them (ADR-225). `desc().perception` is the author's value; this is the one the
    // last update used, after any keyframe, preset or panel edit.
    [[nodiscard]] const PerceptionSettings& perception() const { return perceptionLive_; }
    [[nodiscard]] bool perceives() const { return desc_.perceives; }
    // Sense ticks are not consecutive. A cadence above the step rate skips them, a cadence of zero
    // means "every step" and advances the index by a whole frame of microseconds, and a dropped
    // frame skips them at any cadence -- so the tick a body last sensed at is the only thing that
    // says how much of a per-second budget this tick is owed. `IPerception::perceive` reads it
    // through here, and `EntityWorld::perceiveOne` writes it *after* the call for exactly that
    // reason. Cleared by `reset`, so a replay rebuilds it rather than inheriting it (D4).
    static constexpr std::uint64_t kNoSenseTick = 0xFFFFFFFFFFFFFFFFull;
    [[nodiscard]] std::uint64_t lastSenseTick() const { return senseTick_; }

    // ---- Phase D: what this body is, and what kind of creature it is --------------------------

    // Its semantic tags as bits of the world's `SemanticTags` (§25). Computed once at
    // `setEntities`; the words are `desc().tags`.
    [[nodiscard]] std::uint64_t tagMask() const { return tagMask_; }
    // Phase D §18: whether this body has every capability in `required` (empty: always).
    [[nodiscard]] bool can(std::span<const std::string> required) const {
        for (const std::string& r : required) {
            if (std::find(desc_.capabilities.begin(), desc_.capabilities.end(), r) ==
                desc_.capabilities.end()) {
                return false;
            }
        }
        return true;
    }
    // `desc().perceptionTags`, resolved against the world's vocabulary: (bit, weight).
    [[nodiscard]] std::span<const std::pair<std::uint64_t, float>> perceptionTagWeights() const {
        return perceptionTagBits_;
    }
    // The personality **as the parameters currently read** (ADR-225), falling back to the authored
    // value when unregistered. Neutral for a body that declared none.
    [[nodiscard]] Personality personality() const;

    // ---- intent (ADR-096) ------------------------------------------------------------------

    [[nodiscard]] ActionQueue& actions() { return actions_; }
    [[nodiscard]] const ActionQueue& actions() const { return actions_; }
    [[nodiscard]] Schedule& schedule() { return schedule_; }
    [[nodiscard]] const Schedule& schedule() const { return schedule_; }
    [[nodiscard]] const Gait& gait() const { return gait_; }

    // ---- the Director tier (ADR-210) ---------------------------------------------------------
    //
    // Set by `stage::Staging` and by nothing else in this layer. An entity under a director motion
    // is never distance-culled, for the same reason an entity under orders is not: a body a shot is
    // moving must not stop moving because the camera looked away.
    void setDirectorMotion(const DirectorMotion& motion) { director_ = motion; }
    void clearDirectorMotion() { director_ = DirectorMotion{}; }
    [[nodiscard]] const DirectorMotion& directorMotion() const { return director_; }
    // ADR-820: set by the composition's performers when a blended entry begins, cleared at release.
    void setPerformanceEntry(const PerformanceEntry& entry) { performanceEntry_ = entry; }
    [[nodiscard]] const PerformanceEntry& performanceEntry() const { return performanceEntry_; }
    [[nodiscard]] const DirectorGoal& directorGoal() const { return directorGoal_; }

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
    std::uint64_t tagMask_ = 0;
    std::vector<std::pair<std::uint64_t, float>> perceptionTagBits_;
    std::array<params::Parameter<float>*, Personality::kCount> personalityParams_{};
    EntityState state_{};
    // ADR-545: where this body was at the end of the previous step, and whether there was one.
    // The measured velocity is a backward difference and this is the thing it differences against;
    // `EntityWorld::reset` clears both, so a seek rebuilds them by replay rather than carrying a
    // difference across a discontinuity.
    glm::vec3 lastPosition_{0.0f};
    bool hasLastPosition_ = false;
    bool hasLastVelocity_ = false;
    MotionOffset motion_{};
    DirectorMotion director_{};
    PerformanceEntry performanceEntry_{}; // ADR-820
    DirectorGoal directorGoal_{};         // ADR-824
    LocomotionState locomotion_{};
    BehaviorList behaviors_;

    // Resolved once at bind; null until then and after a parameter set is cleared.
    params::Parameter<glm::vec3>* positionParam_ = nullptr;
    params::Parameter<glm::vec3>* rotationParam_ = nullptr;
    params::Parameter<glm::vec3>* scaleParam_ = nullptr;

    double coarseAccum_ = 0.0;
    // The facing the author placed this body at (ADR-240). `state_.yaw` starts here and the node's
    // rotation is written as the difference, so a placement heading is a starting facing rather
    // than a permanent offset between where a body walks and where it is drawn.
    float facing_ = 0.0f;
    bool active_ = true;
    // Said once per entity per session, never per frame. A body whose travel speed the gait cannot
    // represent is a persistent authoring fault, not an event, and printing it sixty times a second
    // would make it something people filter out rather than fix.
    bool warnedFootSlip_ = false;
    // Whether this entity has ever been ticked. Behaviour level of detail may not suppress the
    // *first* update: an entity that has never run has never published a LocomotionState, so
    // skipping it hands the animation layer a position of (0,0,0) and a character pops in from the
    // world origin on whichever frame it first comes close enough to matter.
    bool everUpdated_ = false;

    // ---- intent ----
    ActionQueue actions_;
    Schedule schedule_;
    Gait gait_;
    // Phase B. Reset with everything else on a seek, and advanced on both publish paths, which is
    // ADR-554's rule applied to the thing ADR-554 was found by.
    MotionMemory motionMemory_;
    MotionState motionState_;
    MotionChainResult motionChainResult_;
    LocomotionPlanState locomotionPlan_;
    const MotionChain* motionChain_ = nullptr;
    // Authoritative here rather than in the parameter set: an entity may be ticked before anything
    // registers a parameter, and a state that only existed as a parameter would vanish on a scene
    // swap. The parameter mirrors this, not the other way round.
    std::vector<std::pair<std::string, float>> propertyValues_;
    std::vector<params::Parameter<float>*> propertyParams_;
    std::vector<AttachmentDesc> attachments_;
    // verb -> the entity currently using it. A vector because a prop offers two or three verbs.
    std::vector<std::pair<std::string, std::string>> claims_;

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

    // ---- the senses (ADR-270, ADR-290) -------------------------------------------------------
    //
    // Sized once at bind to the hard maximum a `capacity` parameter may be raised to, so the
    // working set never reallocates however the timeline drives the knob. `perceptCount_` is how
    // many of them the last tick actually wrote.
    std::vector<Percept> percepts_;
    std::size_t perceptCount_ = 0;
    // The sense tick the working set belongs to (`entity::senseTick`), and a sentinel meaning
    // "never sensed". Not an accumulator: an accumulator drifts with the frame rate, so a replayed
    // sense tick would land on a different instant from the played one and a character's working
    // set would depend on how the frames happened to fall (D1).
    std::uint64_t senseTick_ = kNoSenseTick;
    PerceptionSettings perceptionLive_{};
    // The live knobs, resolved at registerParameters(). Null until then; `bind` fixes them up.
    struct PerceptionParams {
        params::Parameter<float>* range = nullptr;
        params::Parameter<float>* fieldOfView = nullptr;
        params::Parameter<float>* proximityRange = nullptr;
        params::Parameter<float>* capacity = nullptr;
        params::Parameter<float>* hertz = nullptr;
        params::Parameter<float>* occlusionTestsPerSecond = nullptr;
        params::Parameter<float>* weight[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    };
    PerceptionParams perceptionParams_{};

    IPoseSink* pose_ = nullptr;
    const ISkeletonQuery* skeleton_ = nullptr;

    // ---- root motion (ADR-337) ----
    const IRootMotionSource* rootMotion_ = nullptr;
    // The previous sample, which is the only state this needs. `IRootMotionSource::rootMotion`
    // answers "how far from the clip's first key", always, so a step is the difference between
    // two of those and nothing here is a running total. A running total is the obvious
    // implementation and it is precisely the one that could not have survived a seek: ADR-267's
    // D4 says what a body knows must be recoverable by replaying the steps, and `reset()` clears
    // these two so a replay rebuilds them from the first step it runs.
    glm::vec3 rootMotionLast_{0.0f};
    std::uint64_t rootMotionGeneration_ = 0;
    bool rootMotionHeld_ = false;
    // What the last update actually added to `travel`, in world metres, for reporting.
    glm::vec3 rootMotionStep_{0.0f};
};

// How much of the past one `EntityWorld::seek` may replay, and what that replay is allowed to cost.
//
// It replaces a bare `maxSeconds = 90.0` default argument, and the reason is the unit. Ninety
// seconds is not an amount of work: a replay costs steps x bodies, so the same literal buys 8,100
// body-steps in a fifteen-body scene and 1,350,000 in the cast the character-intelligence plan
// asks for. ADR-267 priced that second one at 161 s for a hundred explorers and 594 s for two
// hundred and fifty -- a ten-minute stall for one timeline click -- and the cause is not that the
// simulation is slow, it is that the ceiling was written in the wrong currency. A scene does not
// get to take longer because it is bigger. It gets less history.
//
// `maxSeconds` is the policy: the longest stretch of timeline anybody thinks a character's past is
// worth. `maxBodySteps` is the price cap, and the *smaller* of the two wins, so a big cast keeps
// what it can afford and a small one keeps the lot.
//
// **Zero means no cap**, and that is deliberate: an offline render, a test and the command hub all
// want the whole window whatever it costs, because none of them is waiting for a person. Only the
// live editor sets a ceiling, and it sets it explicitly (see `Engine::seekSeconds`).
//
// **ADR-700 retired the ninety-second window.** A seek now restores the nearest simulation
// checkpoint at or before the target and replays forward from it, so it is exact at any time rather
// than only inside the window; `maxSeconds` is read only by `SeekMode::Window`, which is kept as a
// test control and an A/B arm, not as the product. `maxBodySteps` keeps its meaning in the unit it
// always had -- the most one seek may integrate -- and applies to the replay *from the checkpoint*:
// when even that is more than a click may cost (a first scrub deep into a big cast with nothing
// recorded yet), the seek falls back to the window and says so (`SeekWork::exact == false`).
enum class SeekMode : std::uint8_t {
    // Restore the nearest checkpoint, replay forward, record checkpoints as the replay passes them.
    Checkpointed,
    // The pre-ADR-700 replay: at most `maxSeconds` from a reset state. A control, not the product.
    Window,
    // Every step from t = 0, touching no checkpoint. The reference a checkpointed seek must equal.
    FullHistory,
};
[[nodiscard]] std::string_view seekModeName(SeekMode mode);

struct SeekBudget {
    double maxSeconds = 90.0;
    std::uint64_t maxBodySteps = 0;
    SeekMode mode = SeekMode::Checkpointed;
    // The live editor's ceiling since ADR-700, raised from ADR-273's 180,000. That number bought
    // "the whole ninety-second window for up to 33 bodies"; with the window retired, the question
    // is instead whether a first scrub with no checkpoint yet can replay the whole film exactly.
    // Glowmere's multicam is 19 entities over 13,578 steps -- 257,982 body-steps to its last frame
    // -- so 180,000 would have made its first scrub to the end inexact. 400,000 covers 19 entities
    // for 351 s and still bounds ADR-267's 250-character cast to 27 s of exact history per click.
    static constexpr std::uint64_t kEditorBodySteps = 400000;
    // `AVGEN_SEEK_MODE` = checkpointed | window | full, for an A/B out of one binary. Unset or
    // unrecognised is `Checkpointed`.
    [[nodiscard]] static SeekMode modeFromEnvironment();
};

// How often a checkpoint is taken and how much memory the set may hold (ADR-700). Measured on
// `glowmere-valley-2-multicam`; see the ADR for the numbers behind the defaults.
struct CheckpointSettings {
    bool enabled = true;
    // One second. Measured on the multicam (ADR-700): a checkpoint is 55 KB and takes 0.2 ms to
    // take, so a second's interval is 12 MB over the whole 226 s film, and the worst scrub after
    // the first -- one second of replay -- measured 17.9 ms against 104.6 ms at five seconds.
    double intervalSeconds = 1.0;
    // The ceiling on the whole set. Past it every other checkpoint is dropped and the interval
    // doubles, so the set thins out evenly rather than losing its far end.
    std::size_t maxBytes = std::size_t{256} << 20;
    // **Test control only.** Keeps the set across a change of inputs, which is exactly the defect
    // the input key exists to prevent; the invalidation test uses it to show the stale answer is
    // wrong. Nothing in the product sets it.
    bool ignoreInputKey = false;
};

// What a host (the composition) keeps in a checkpoint beside the entities: the director and
// whatever the replay hooks drive. Opaque here; the host that captured it is the one that restores it.
struct HostCheckpoint {
    virtual ~HostCheckpoint() = default;
    [[nodiscard]] virtual std::size_t bytes() const = 0;
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
        ++inputEpoch_;
        nav_ = std::move(nav);
        refreshInterestPoints();
        navPath_.setNavigator(&nav_);
    }
    [[nodiscard]] const Navigator& navigator() const { return nav_; }

    // ---- intent (ADR-096) ------------------------------------------------------------------

    // How a `move` action finds its way. Defaults to the straight-line provider over the
    // navigator set above, which is everything that exists today; §5/§6's planner installs itself
    // here and nothing else changes. The pointer is borrowed: the caller keeps it alive.
    void setPathProvider(const IPathProvider* path) {
        ++inputEpoch_;
        path_ = path;
    }
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
    // ADR-824: the other half of `direct` -- drops the Director tier, so the body resumes whatever the
    // tiers below were doing (ADR-091). What a timeline "release" and a Director's hand-back call.
    // Anything the tier was in the middle of is reported Cancelled. False for an unknown entity.
    bool release(std::string_view entity, double now);
    // ADR-824 (§35): gives `entity` a runtime goal (see `DirectorGoal`), replacing any earlier one.
    // An empty subject clears it. False for an unknown entity.
    bool setGoal(std::string_view entity, std::string subject, std::string affordance, double now);
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
        ++inputEpoch_;
        landmarks_ = std::move(landmarks);
        refreshInterestPoints();
    }
    // Phase D §23: landmarks that are the ground itself. Their interest points carry the semantic
    // tag "terrain", which an aware decider refuses as a destination.
    void setTerrainLandmarks(std::vector<std::string> names) {
        ++inputEpoch_;
        terrainLandmarks_ = std::move(names);
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

    // ---- perception (ADR-270, ADR-290) -------------------------------------------------------

    // The sense stage every perceiving body in this world runs. Defaults to the grid-backed one
    // below, which is everything that exists today; a test installs a `ScriptedPerception` here so
    // a decision layer can be asserted without a world, exactly as `setPathProvider` lets an action
    // be. The pointer is borrowed: the caller keeps it alive.
    void setPerception(const IPerception* perception) {
        ++inputEpoch_;
        perception_ = perception;
    }
    [[nodiscard]] const IPerception& perception() const {
        return perception_ != nullptr ? *perception_ : gridPerception_;
    }
    // Whether any entity in this world declared a `perception` block. False is the ordinary answer
    // and it is what makes the stage free: the grids are not built, the loop is not entered, and a
    // scene of hovering craft is bit-for-bit what it was before perception existed.
    [[nodiscard]] bool perceiving() const { return perceiving_; }

    // What the last update's sense stage actually did. Structural quantities rather than
    // milliseconds (ADR-170): "eleven bodies sensed, 274 candidates scanned, 2 occlusion tests"
    // survives a change of machine, and it is what the acceptance arms assert on -- the control at
    // `occlusionTestsPerSecond = 0` is exactly `occlusionTests == 0`.
    struct PerceptionCounts {
        std::size_t perceivers = 0;     // entities that declared a perception block and were ticked
        std::size_t sensed = 0;         // ...of those, the ones whose sense tick fired this update
        std::size_t candidates = 0;     // points the grid queries returned, before any filter
        std::size_t percepts = 0;       // percepts produced
        std::size_t dropped = 0;        // candidates the capacity cut discarded
        std::size_t occlusionTests = 0; // `world::heroSightline` calls performed
        std::size_t occlusionDeferred = 0; // percepts left `tested == false` by a spent budget
    };
    [[nodiscard]] PerceptionCounts perceptionCounts() const { return perceptionCounts_; }
    // The snapshot indices the sense stage scans. Public because a `IPerception` written outside
    // this file needs them and because a test that wants to price a scan needs to be able to run
    // one; rebuilt at the top of every update in which anything perceives.
    [[nodiscard]] PerceptionIndex perceptionIndex() const;

    // ---- semantics and events (Phase D §25, §26) ---------------------------------------------

    // The world's vocabulary: every word an entity's `tags` uses, plus the five interest kinds'
    // names, interned to bits. Percepts carry masks of these (`Percept::tags`).
    [[nodiscard]] const SemanticTags& semanticTags() const { return tags_; }

    // Things that happened, newest last, for the last `kEventWindowSeconds` and at most
    // `kEventCapacity` of them. Raised by the world, not by a character: any named `ActionEvent`
    // (an action's or an interaction's `onComplete`) becomes one at the position of the entity that
    // raised it, so a mushroom whose schedule sets `"onComplete": "bloom"` is heard blooming by
    // anyone in earshot. Cleared by `reset`, and re-raised identically by `seek`'s replay, which
    // runs the same action tier -- so an event's `sequence` is the same number on both paths.
    static constexpr std::size_t kEventCapacity = 128;
    static constexpr double kEventWindowSeconds = 60.0;
    [[nodiscard]] std::span<const WorldEvent> worldEvents() const { return worldEvents_; }
    // Interns an event name to its type id (never 0). Names are few and fixed per scene.
    std::uint32_t eventType(std::string_view name);
    [[nodiscard]] std::uint32_t findEventType(std::string_view name) const;
    [[nodiscard]] std::string_view eventName(std::uint32_t type) const;
    // Raises an event. `sequence` is assigned here. Public so a host system -- an audio analyser's
    // semantic events (§28), a director -- can raise one; the world raises its own from actions.
    void emitEvent(WorldEvent event);
    // How far an event named `name` carries and how loud it is, when an author wants it different
    // from the default 60 m at full strength. Scene-level data (`"worldEvents"`), never code.
    struct EventProfile {
        std::string name;
        float radius = 60.0f;
        float magnitude = 1.0f;
        // Phase D §28: raise this event from a signal-bus event -- `"signal": "music.drop"` -- so
        // the audio analyser's structure reaches characters as a *semantic* event rather than as a
        // band level a behaviour thresholds (§28: "do not directly tie behaviour to arbitrary audio
        // bands"). Raised at `at` (an entity or landmark; the world origin when empty) whenever the
        // signal fires at or above `threshold`, at most once per `minInterval` seconds, and with
        // the signal's value as its magnitude.
        //
        // **Not reproduced by a scrub**: `seek` does not replay the bus's history, so a
        // signal-raised event is heard on a play and not on a replay. Action-raised events are.
        std::string signal;
        float threshold = 0.0f;
        std::string at;
        float minInterval = 1.0f;
    };
    void setEventProfiles(std::vector<EventProfile> profiles);
    [[nodiscard]] const std::vector<EventProfile>& eventProfiles() const { return eventProfiles_; }

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
    // `budget` bounds the work; see `SeekBudget`. `params`, when given, is put back to its authored
    // values first: behaviours read parameter *finals*, and a final still holding the last played
    // frame's modulation would make the answer depend on where the playhead came from -- which is
    // the whole thing being fixed. The next ordinary frame recomputes them.
    //
    // **There is no `viewPosition` and no `distanceDetail`.** There was, and the culling test inside
    // the loop read them, which made a scrubbed frame a function of (time, where the camera
    // happened to be). ADR-267 measured what that is worth in a *played* frame -- 50.263 m over
    // eight explorers at thirty seconds between a camera at the origin and one 200 m away -- and a
    // seek has no business inheriting it, because the whole promise of a seek is that the same
    // second gives the same frame. What used to be saved by skipping distant bodies is now bounded
    // by `SeekBudget::maxBodySteps` instead, which does it without consulting a camera.
    // Phase D §63 / ADR-671: what a caller replays alongside the entities, one fixed step at a
    // time -- the director above them (`before`, as `Composition::updateBehaviour` runs it before
    // the entity update) and whatever must see the step's settled bodies (`after`). Null, which is
    // every caller before ADR-671, replays the entities alone.
    //
    // ADR-700 adds the host's half of a checkpoint. `capture` is called after `after` on each step
    // a checkpoint is recorded at and returns everything the host's replay reads across steps;
    // `restore` puts that back before the replay resumes from the checkpoint. When the replay
    // starts from zero instead, the host is expected to have reset itself before calling `seek`
    // (`seekWithDirector` does). `inputKey` is the host's contribution to the checkpoints'
    // validity: anything the host's replay reads that is not a parameter base must change it.
    struct SeekHooks {
        std::function<void(double now, double dt)> before;
        std::function<void(double now, double dt)> after;
        std::function<std::shared_ptr<const HostCheckpoint>()> capture;
        std::function<void(const HostCheckpoint&)> restore;
        std::uint64_t inputKey = 0;
    };
    void seek(double time, params::ParameterSet* params = nullptr,
              const signals::SignalBus* bus = nullptr, double step = 1.0 / 60.0,
              SeekBudget budget = {}, const SeekHooks* hooks = nullptr);
    // Writes every entity's offsets onto its node's parameter finals, exactly as `update` does
    // after each body's step. For a replay hook that has to see the frame a play would have drawn.
    void applyAllOffsets();

    // What the last `seek` actually integrated. Structural quantities rather than milliseconds
    // (ADR-170): "it replayed 5,400 steps over 23 bodies" survives a change of machine in a way
    // that "it took 2,437 ms" does not, and it is what a test can assert on.
    struct SeekWork {
        double spanSeconds = 0.0;      // how far back the replay actually started
        std::uint64_t steps = 0;       // fixed steps in the deep window
        std::uint64_t bodySteps = 0;   // steps x bodies: the inner-loop count that was paid
        std::uint64_t fullBodySteps = 0; // ...and what it would have been with no classification
        std::size_t deepBodies = 0;    // bodies that needed the whole window
        std::size_t shallowBodies = 0; // ...and bodies whose answer is a function of the target
        bool budgetBound = false;      // true when the step budget, not the policy, chose the span
        // ADR-700.
        SeekMode mode = SeekMode::Window;  // the mode that actually ran (a fallback reports Window)
        bool exact = false;            // the answer equals a replay of the whole history
        bool fellBack = false;         // Checkpointed was asked for and the budget forced Window
        double restoredFrom = -1.0;    // the checkpoint's time, or -1 when it replayed from zero
        std::size_t recorded = 0;      // checkpoints this seek added
        bool invalidated = false;      // this seek found the inputs changed and dropped the set
    };
    [[nodiscard]] SeekWork lastSeekWork() const { return seekWork_; }

    // ---- simulation checkpoints (ADR-700) ----------------------------------------------------
    void setCheckpointSettings(const CheckpointSettings& settings);
    [[nodiscard]] const CheckpointSettings& checkpointSettings() const { return checkpointSettings_; }
    // Drops every checkpoint. The input key already does this for any change it can see; this is
    // for a caller that knows of one it cannot.
    void dropCheckpoints();
    struct CheckpointStats {
        std::size_t count = 0;
        std::size_t bytes = 0;          // the whole set, measured as heap bytes allocated to copy it
        std::size_t lastBytes = 0;      // the most recent checkpoint alone
        double latestSeconds = -1.0;    // the furthest one, or -1
        double intervalSeconds = 0.0;   // the interval in force (it doubles when the cap thins)
        std::uint64_t invalidations = 0;
        double lastCaptureMs = 0.0;
        double lastRestoreMs = 0.0;
    };
    [[nodiscard]] CheckpointStats checkpointStats() const;
    // The key the current inputs would give a checkpoint: the world's structure epoch, every
    // entity's borrowed providers, every parameter base, the step and the host's key. Public so a
    // test can show that an edit moves it.
    [[nodiscard]] std::uint64_t checkpointInputKey(const params::ParameterSet* params, double step,
                                                   std::uint64_t hostKey) const;
    // Bumped by everything that changes what a replay would compute other than a parameter base:
    // the entity set, bindings, the navigator, landmarks, interest points, fields, event profiles,
    // parameter registration, the path provider and the sense stage.
    [[nodiscard]] std::uint64_t structureEpoch() const { return inputEpoch_; }

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

    SeekWork seekWork_{};
    // Scratch reused across the steps of one seek, so a 5,400-step replay allocates once rather
    // than 5,400 times: what each body needs (in steps ending at the target) and the events the
    // action tier raises while it is being replayed. The events are *discarded* -- a seek must not
    // re-fire a door opening that happened eighty seconds ago into an application that has already
    // seen it -- but the queue still has to be given somewhere to put them or it cannot run.
    std::vector<std::uint64_t> seekFirstStep_;
    std::vector<ActionEvent> seekEvents_;

    // ---- simulation checkpoints (ADR-700) ----
    //
    // A checkpoint is the state after grid step `step` (the instant `step / rate`), complete:
    // every entity copied whole (`Entity`'s implicit copy -- see `BehaviorList`), the world's own
    // simulation state listed below, and the host's half. **The world-level list is the one
    // hand-written part**, so it is guarded: `test_seek_checkpoints.cpp` fails when `EntityWorld`
    // changes size, and the message says to decide whether the new member is state (add it here
    // and to `captureCheckpoint`/`restoreCheckpoint`) or configuration (bump `inputEpoch_` where
    // it is set).
    struct Checkpoint {
        std::uint64_t step = 0;
        std::vector<Entity> entities;
        std::vector<WorldEvent> worldEvents;
        std::uint64_t eventSequence = 0;
        std::vector<double> eventSignalLast;
        std::vector<ActionEvent> actionEvents;
        std::vector<ActionEvent> pendingEvents;
        std::shared_ptr<const HostCheckpoint> host;
        std::size_t bytes = 0;
    };
    std::vector<Checkpoint> checkpoints_; // ascending by step
    CheckpointSettings checkpointSettings_{};
    std::uint64_t checkpointKey_ = 0;
    double checkpointStep_ = 0.0;
    std::uint64_t checkpointInterval_ = 0; // in steps; doubles when the byte cap thins the set
    std::uint64_t checkpointInvalidations_ = 0;
    double lastCaptureMs_ = 0.0;
    double lastRestoreMs_ = 0.0;
    std::uint64_t inputEpoch_ = 1;
    void captureCheckpoint(std::uint64_t step, const SeekHooks* hooks);
    void restoreCheckpoint(const Checkpoint& checkpoint, const SeekHooks* hooks);
    void thinCheckpoints();
    // One fixed replay step, shared by every seek mode so they cannot disagree (testing.md #31/#38):
    // the host's `before`, the crowd and sense snapshots, every body's step, the host's `after`.
    // `i` is compared against `seekFirstStep_` (the window's shallow bodies); `buildCrowd` is false
    // only when the window found no deep body.
    void replayStep(double now, double stepDt, std::uint64_t i, const signals::SignalBus* bus,
                    const SeekHooks* hooks, bool buildCrowd);
    // What every seek publishes on the way out, so the next frame builds on it.
    void publishSeek(double target, double dt, bool replayedNothing);
    void seekWindow(double time, params::ParameterSet* params, const signals::SignalBus* bus,
                    double step, SeekBudget budget, const SeekHooks* hooks);
    void seekExact(double time, params::ParameterSet* params, const signals::SignalBus* bus,
                   double step, SeekBudget budget, const SeekHooks* hooks);

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
    std::vector<std::string> terrainLandmarks_;
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

    // ---- perception (ADR-270, ADR-290) -------------------------------------------------------
    //
    // Two indices, both snapshots taken before anything in the step moves -- the same rule the
    // crowd field follows, for the same reason: building them as the bodies go would make what a
    // character notices depend on the order the entities happen to be stored in.
    //
    // The interest grid is rebuilt with the list, which is rare. The body grid is rebuilt every
    // update in which anything perceives, because bodies move.
    GridPerception gridPerception_;
    const IPerception* perception_ = nullptr;
    spatial::PointGrid interestGrid_;
    std::vector<glm::vec3> interestGridPoints_;
    std::vector<std::uint32_t> interestGridSource_; // grid point -> interests_ index
    spatial::PointGrid bodyGrid_;
    std::vector<glm::vec3> bodyPoints_;             // entity index -> its snapshot position
    std::vector<Percept> perceptScratch_;
    PerceptionCounts perceptionCounts_{};
    bool perceiving_ = false;

    // ---- semantics and events (Phase D) ----
    SemanticTags tags_;
    std::vector<WorldEvent> worldEvents_;
    std::uint64_t eventSequence_ = 0;
    std::vector<std::string> eventNames_; // type id - 1 -> name
    std::vector<EventProfile> eventProfiles_;
    std::vector<double> eventSignalLast_; // per profile: when its signal last raised it
    void raiseSignalEvents(const signals::SignalBus* bus, double time);
    void internSemantics();
    // Turns every named `ActionEvent` in `events` from `first` onward into a `WorldEvent` raised at
    // `entityIndex`. Shared by `update` and `seek` so the two paths cannot disagree (testing.md #31).
    void raiseActionEvents(const std::vector<ActionEvent>& events, std::size_t first,
                           std::size_t entityIndex, double time);
    static void measureStepVelocity(Entity& entity, double dt);
    static void applyNodeOffsets(Entity& entity);
    // The Director tier's two halves (ADR-210), shared by `update` and `seek`.
    static void directorBefore(Entity& entity);
    // ADR-758/820: a scripted performance holds this body now.
    [[nodiscard]] static bool performing(const Entity& entity);
    static void directorAfter(Entity& entity);
    static void rescaleIntent(EntityState& state);
    // Rebuilds `bodyGrid_` and `bodyPoints_` from where every entity's simulation stands now, and
    // runs one sense tick for `entityIndex` when its cadence says it is due. Shared by `update` and
    // `seek` so a scrubbed character's working set is built by the same code as a played one's.
    void buildBodyIndex();
    void perceiveOne(std::size_t entityIndex, double time);

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
