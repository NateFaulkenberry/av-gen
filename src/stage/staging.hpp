#pragma once

// The director: deciding, at `Authority::Director`, what a scene's actors do (ADR-210).
//
// ## What this is not
//
// It is not a behaviour system, and it is emphatically not a second one. ADR-096 already built the
// pipeline the brief asks for --
//
//     Character -> Action -> Target -> Animation -> Completion -> Next Action
//
// -- with eight primitives, targets, conditions, completion events, schedules and a three-tier
// queue whose top tier is called `Authority::Director`. What was missing was anybody *standing* in
// that tier. `entity::ActionQueue` is a thing that can be **told** what to do; nothing in the
// repository **decided**. So this file is the decision layer and nothing else: scene queries, the
// claim that stops two actors grabbing the same cow, and the loop
//
//     pick a target -> issue the cues -> wait for them to finish -> pick again.
//
// Three consequences of that scope, each of which is a thing deliberately *not* here:
//
// * **No second transform system.** A director moves a body by writing `entity::DirectorMotion`,
//   which becomes the entity's `travel` -- the same field navigation writes -- and by handing
//   `entity::ActionDesc`s to `EntityWorld::direct`, which is the same override an ADR-096 shot
//   uses. Nothing here touches a node's transform directly.
// * **No second parenting.** "UFO + tractor beam as one thing" is `"parent": "visitor"` in the
//   scene file and has been since the beam was authored. What was missing was a *name* for the
//   group, which is `ActorDesc`: a body, and the parts that come with it.
// * **No second timeline.** A cue's timing is seconds against the engine's timeline clock, the
//   same clock `Schedule` uses.
//
// ## The shape
//
//     StagingDesc
//       actors[]     -- a body entity plus its named parts (beam, lights)
//       scenarios[]
//         params[]   -- every number the scenario reads, as ordinary params::Parameters
//         beats[]
//           find[]   -- scene queries; each binds a role ("target") or the beat fails
//           cues[]   -- run in PARALLEL, one per role
//             steps[] -- run in SEQUENCE
//           then / otherwise
//
// Sequence is a cue's step list. Parallel is a beat's cue list, and it is at the beat rather than
// inside a cue because the thing the brief wants concurrent -- a saucer hovering while a cow rises
// -- is two *different entities*, which a per-entity action queue structurally cannot express.
// Repeat is the scenario's own cycle. That is the whole of the timing vocabulary, and it is three
// containers rather than a behaviour tree because the brief asked for the small version.
//
// ## Roles
//
// A step never names an entity. It names a **role**: `actor`, or whatever a `find` bound. `actor`
// resolves to the actor's body; `actor.beam` resolves to the part called `beam`. This is the whole
// of why the UFO abduction contains no UFO-specific C++: swap the actor and the tag the query
// filters on and the same beats abduct something else, or follow it, or land next to it.
//
// ## Determinism
//
// Every number is a parameter or a literal; the only randomness is `Rng`, seeded from the
// scenario's seed, and it is drawn in a fixed order. Nothing reads a wall clock. The candidate
// index is rebuilt on a fixed interval measured against the timeline, and a `PointGrid` query
// visits points in an order that is a pure function of the point set -- so the same seed and the
// same second choose the same cow, offline and in the editor alike.
//
// ## Cost
//
// No per-frame scan over the scene. The candidate index is built from the entities whose tags any
// query in the description actually mentions, on `searchInterval` seconds, and a query is a
// `spatial::PointGrid` disc lookup over that. With no scenario running, `update` returns after one
// branch.

#include "core/error.hpp"
#include "core/rng.hpp"
#include "entity/action.hpp"
#include "entity/locomotion.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"
#include "spatial/point_grid.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::entity {
class Entity;
class EntityWorld;
} // namespace avgen::entity

namespace avgen::stage {

// ---- values -------------------------------------------------------------------------------------

// A number a beat reads: a literal, or the name of one of the scenario's own parameters.
//
// This is the mechanism behind "do not hardcode these values throughout the implementation". Every
// knob the brief lists -- search radius, preferred height, travel speed, hover duration, beam
// activation time, abduction duration, lift height, rotation, time between targets -- is a
// `ScenarioParam`, registered as an ordinary `params::Parameter<float>`, and therefore keyframeable,
// presettable, modulatable and visible to the editor without this file knowing any of that exists.
struct Value {
    float literal = 0.0f;
    std::string param; // "" = use the literal
    int index = -1;    // resolved against the scenario's params by setDesc(); -1 = a literal

    [[nodiscard]] bool bound() const { return !param.empty(); }
};
[[nodiscard]] Value literal(float v);
[[nodiscard]] Value bound(std::string param, float fallback = 0.0f);

// ---- scene queries ------------------------------------------------------------------------------

// How a query chooses among the candidates that passed its filters.
enum class Pick : std::uint8_t {
    Nearest,  // FindNearest
    Farthest,
    Random,   // FindRandom
    First,    // the candidate order, which is the entity order: stable, and what FindEntity wants
};
[[nodiscard]] const char* pickName(Pick pick);
[[nodiscard]] std::optional<Pick> pickFromName(std::string_view name);

// One scene query. The brief's six -- FindEntity, FindNearest, FindRandom, FindByTag,
// FindWithinRadius, FindInRegion -- are this one struct with different fields set, because they are
// the same question with different filters and a different tie-break, and six functions would have
// been six copies of the same walk.
struct QueryDesc {
    std::string bind;  // the role the winner is bound to; a query that binds nothing is an error
    std::string name;  // FindEntity: an exact entity name. Skips the index entirely.
    std::string tag;   // FindByTag: an `EntityDesc::tags` entry, or the profile it was built from
    // FindWithinRadius / FindNearest: the role the search is centred on. "" centres it on `center`.
    std::string from;
    glm::vec3 center{0.0f}; // FindInRegion: the centre, when `from` is empty
    Value radius;           // metres; 0 = the whole candidate set
    Value minRadius;        // metres; keeps an actor from picking what it is standing on
    Pick pick = Pick::Nearest;
    // Targets another cue is already working on are not candidates. This is the whole of "two
    // actors never grab the same animal", and it is a director-level fact rather than an entity one
    // -- `Entity::claim` exists but is about an *interaction a prop published*, which is a
    // different question with a different lifetime.
    bool excludeClaimed = true;
    bool excludeRetired = true; // something a `retire` step consumed is gone for good
    bool claim = true;          // hold the winner until the beat that claimed it releases it
    // Metres of clear air a candidate must have above it, against the navigation layer's canopy
    // model. 0 = do not ask. This is "not obstructed by major geometry", asked of the world rather
    // than assumed: a cow under a nine-metre tree is not abductable.
    Value clearance;
    // Rejects a candidate outside the navigable world -- off the map, in the lake, up a cliff.
    bool requireNavigable = false;

    [[nodiscard]] bool valid() const { return !bind.empty(); }
};

// ---- steps --------------------------------------------------------------------------------------

// What one step does. Eleven, and the mapping onto the brief's list is deliberate rather than
// coincidental:
//
//   Transform  MoveTo / MoveBy -> MoveTo (`relative`)      RotateTo / LookAt -> LookAt
//              Follow / Hover  -> Follow (`height`)        Attach -> Attach / Detach
//   Animation  PlayAnimation / StopAnimation / SetAnimationState -> Play
//   Visibility Show / Hide -> Show / Hide                  Fade -> Set, over a duration
//   Timing     Wait -> Wait   Sequence -> a cue   Parallel -> a beat   Repeat -> a scenario cycle
//
// `Actions` is the bridge back to ADR-096: it hands a list of `entity::ActionDesc`s to the entity's
// own queue at `Authority::Director` and waits for it to drain. Everything a walking character can
// be told -- route, steer, gait, interact, equip, branch on a condition -- is reachable through it
// without a line of it being re-implemented here.
enum class StepKind : std::uint8_t {
    Wait,
    MoveTo,  // travel to a resolved point, easing in and out
    Follow,  // hold a resolved point for a duration, tracking it as it moves
    LookAt,  // turn to face a resolved point
    Play,    // an activity name, never a clip name
    Show,
    Hide,
    Set,     // write a parameter, optionally ramping to it
    Actions, // hand ADR-096 actions to the role's entity at Authority::Director
    Attach,
    Detach,
    Release, // give up the claim on a role
    Retire,  // hide it, release it, and never offer it to a query again
};
[[nodiscard]] const char* stepKindName(StepKind kind);
[[nodiscard]] std::optional<StepKind> stepKindFromName(std::string_view name);

// How a `MoveTo` gets there.
enum class Travel : std::uint8_t {
    // A director tween: the body is put where the shot wants it, easing in and out, holding a floor
    // above the terrain. What a craft, a lifted animal and a camera all want, and what the walker's
    // `move` structurally cannot do because it snaps to the ground it is crossing.
    Fly,
    // Delegated to `entity::ActionKind::Move`, which routes over the navigation layer, steers round
    // trunks, snaps to the surface and drives the gait. What a character wants. Identical in every
    // respect to a walk the entity chose for itself, because it is one.
    Walk,
};
[[nodiscard]] const char* travelName(Travel travel);
[[nodiscard]] std::optional<Travel> travelFromName(std::string_view name);

// Which of a body's two positions a step measures a destination from.
//
// They are not the same place, and the difference is not small. `Entity::state().position()` is the
// anchor plus `travel` -- what navigation and this director wrote. The behaviours' offsets are
// folded onto the node's transform *afterwards*, on purpose (ADR-210: "a craft keeps hovering,
// drifting and banking while it is being flown somewhere"), so what is *drawn* is
// `Entity::visualPosition()`. Glowmere's saucer carries `drift` with a radius of 2.4 m, and its
// tractor beam is a particle node parented to its transform -- so the beam column stands up to
// 2.4 m from where the director thinks the craft is, and an animal lifted to the director's number
// rises beside the beam rather than up it.
//
// `Travel` is the default because it is what every step did before this existed, and because it is
// the right answer for a query: "how far is the nearest cow" is a fact about the simulation.
//
// ## And why `Visual` was still not the picture
//
// `Visual` is arithmetic on an *entity*. It is `anchor + travel + the behaviours' offsets`, and it
// is right about all three. What it cannot see is everything between an entity and a pixel:
//
//  * **the parent chain.** A node parented to another is drawn at its parent's world transform
//    times its own local one. An entity's anchor is the node's *local* position, so `visualPosition()`
//    on anything parented answers in the wrong space entirely -- for the tractor beam, whose local
//    position is `[0,0,0]`, it answers "the world origin".
//  * **the node's own contents.** The saucer's beam is a particle emitter authored 2.05 m *under*
//    the node, so the column's axis is the node's world matrix applied to that offset -- and the
//    saucer is tilted, so the axis stands up to 0.55 m from the node origin. An animal lifted to the
//    node origin is lifted beside the column by exactly that.
//  * **what the mesh does inside its own node.** A farm GLB is not centred on its own origin: a
//    cow's bind-pose box centre is 0.223 model units behind it, and ADR-213's 3.6x makes that
//    0.89 m of world. The scenario spins the animal at 230 deg/s about the origin, so the *body*
//    travels a 1.8 m circle while the origin the director aimed does not move at all.
//  * **routes and reactions.** Both write the node's position *parameter*, and `applyOffsets` adds
//    the entity's own offsets on top of whatever they left -- so whatever a route put there is in
//    the drawn position and is not in `visualPosition()`. Glowmere's saucer carries two audio
//    reactions on `position` component 1 with a combined depth of 1.23 m. Measured against a silent
//    bus the gap is 0.000 m, which is the honest number and not the reassuring one: it is zero
//    because nothing was driving the routes, not because the two agree.
//
// `Drawn` is the answer to all four at once, and it is not more arithmetic -- it is a *question put
// to the flattened scene*: "where is this node's contribution to the picture centred?" One frame
// old, because the director decides before the entity pass writes the finals the flattening reads;
// measured, that costs millimetres while a craft holds station and is reported rather than assumed.
enum class Anchor : std::uint8_t {
    Travel, // state().position(): the simulation's number. The default.
    Visual, // visualPosition(): the simulation's number plus the behaviours' offsets.
    // The flattened scene's answer: the parent chain, the parameters, and the centre of what the
    // node actually draws. What anything that has to line up with a picture wants.
    Drawn,
};
[[nodiscard]] const char* anchorName(Anchor anchor);
[[nodiscard]] std::optional<Anchor> anchorFromName(std::string_view name);

struct StepDesc {
    StepKind kind = StepKind::Wait;
    std::string name; // a label, for the report and for a test to assert on
    std::string role; // whose body this acts on; "" = the cue's own role
    Value duration;   // seconds; 0 = until the step's own completion test passes

    // ---- where (MoveTo / Follow / LookAt) ----
    // The destination is `resolve(toRole) + point`, or just `point` when `toRole` is empty. So a
    // world coordinate, a point relative to another actor, and "directly above whatever I just
    // found" are the same three fields.
    std::string toRole;
    glm::vec3 point{0.0f};
    Value height;             // metres added to the destination's y
    bool aboveGround = false; // measure `height` from the terrain under the destination instead
    bool relative = false;    // MoveBy: `point` is an offset from where the body is *now*
    // Which of the destination role's positions the point is measured from. Default `Travel`,
    // which is what every step did before this field existed.
    Anchor anchor = Anchor::Travel;
    // And the other half of the same question, which had no way of being asked: which point *of the
    // body being moved* is put on that destination.
    //
    // A director moves an entity, and an entity drives a node's origin -- so every `moveTo` before
    // this field put the node's **origin** on the destination. For a body whose mesh is centred on
    // its origin that is the same thing; for one that is not, the difference is the asset's own
    // model-space offset, turned into world by the body's rotation, and it therefore *changes with
    // the body's facing*. Four identical cows at 0/90/180/270 degrees, aimed identically, end up in
    // four different places relative to the beam: measured in the lab, 1.06, 1.18, 1.20 and 1.02 m
    // off the axis, a 0.18 m spread between bodies that differ in nothing but which way they face.
    // That is the signature of this defect and nothing else produces it -- a constant error is the
    // same for all four.
    //
    // `Travel` and `Visual` both mean the node's origin here, because that is what the director
    // writes. `Drawn` means the centre of the box the node's meshes occupy -- the thing a viewer
    // is looking at when they say the animal is not in the beam.
    Anchor place = Anchor::Travel;
    // `Follow` only: resolve the destination **once**, on the step's first frame, and hold it.
    //
    // The brief's Hover, as distinct from its Follow, and the difference is load-bearing rather than
    // cosmetic. A craft that keeps taking its station from a body which is simultaneously taking its
    // own from the craft is a loop, and ADR-210 already paid for the vertical half of one (488 m of
    // "lift" in four and a half seconds). The horizontal half is benign only while whatever is added
    // on the way round has a small time integral; a `drift` of 2.4 m at 0.031 Hz integrates to about
    // twelve metres, which is not benign. A held point has nothing going round it.
    bool hold = false;
    Travel travel = Travel::Fly;
    Value speed;              // m/s; ignored when `duration` is set
    Value tolerance;          // metres that count as arrived (Walk only; Fly arrives exactly)
    // Never fly closer than this to the ground. The brief's "the UFO should not fly through major
    // environmental objects", asked of the navigation layer's own height query rather than hoped
    // for. 0 = do not ask.
    Value clearance;

    // ---- the comedy, as parameters rather than as code ----
    Value spin;       // degrees per second about +Y while the step runs
    Value wobble;     // metres of horizontal sway
    Value wobbleRate; // sways per second

    // ---- Set / Show / Hide ----
    // A parameter path. Bare names resolve against the role's node exactly as an entity reaction's
    // target does (`EntityWorld::resolveTarget`), so "spawnRate" on a particle node finds
    // `particles/<node>/spawnRate` and "visible" finds `nodes/<node>/visible`. A path containing a
    // '/' is taken as absolute. Empty, with Show/Hide, means the role's own visibility.
    std::string target;
    Value to;
    Value from;
    bool hasFrom = false; // false: ramp from wherever the parameter already is

    // ---- Play ----
    std::string activity; // an activity name ("walk", "idle"); `EntityDesc::clips` maps it
    Value rate;           // the speed to hand the gait, so a carried body keeps its legs going

    // ---- Actions ----
    std::vector<entity::ActionDesc> actions;

    // ---- Attach ----
    std::string socket;
};

// One role's steps, in order. The sequence.
struct CueDesc {
    std::string role; // the default `role` for every step in it
    std::vector<StepDesc> steps;
};

// One moment of the scenario: bind some roles, then run every cue at once.
struct BeatDesc {
    std::string name;
    std::vector<QueryDesc> find; // run in order, before the cues; any failing fails the beat
    std::vector<CueDesc> cues;   // the parallel
    std::string then;      // the beat to continue from when this one ends; "" = the next one
    std::string otherwise; // where a failed `find` goes; "" = end the cycle
    bool release = false;  // drop every claim this scenario holds as the beat ends
};

// A director parameter. Registered under `<prefix><scenario>/<name>`.
struct ScenarioParam {
    std::string name;
    float value = 0.0f;
    float min = 0.0f;
    float max = 1.0f;
};

struct ScenarioDesc {
    std::string name;
    std::string actor;             // the actor bound to the role `actor` at the start of a cycle
    std::uint32_t seed = 0;        // 0 = derived from the name
    bool autoStart = false;
    // The audio and event seam the brief asks for, and it is one line rather than a subsystem
    // because the signal bus already carries every musical event, every analysis band and every
    // OSC and MIDI message under a name. A scenario that starts on "audio.beat" starts on the beat;
    // one that starts on a sequencer event starts when the sequencer publishes it. Edge-triggered:
    // `startOn` only starts a scenario that is not running, and only on the frame the event fires.
    std::string startOn;
    std::string stopOn;
    int maxCycles = 0;             // 0 = for ever. The brief's "Maximum Abductions".
    double searchInterval = 0.5;   // seconds between candidate-index rebuilds
    std::vector<ScenarioParam> params;
    std::vector<BeatDesc> beats;
};

// A logical group of existing scene entities the director moves as one thing.
//
// The transform relationship is the scene's own parenting and is not repeated here -- moving the
// body moves the beam because the beam's node says `"parent": "visitor"`. What this adds is the
// *name*: a step that addresses `actor.beam` does not have to know the entity is called
// `visitor-beam`, which is what makes the same scenario drive a different craft.
struct ActorPart {
    std::string name;   // "beam"
    std::string entity; // "visitor-beam"
};
struct ActorDesc {
    std::string name;
    std::string body; // the entity the director moves; "" = `name`
    std::vector<ActorPart> parts;

    [[nodiscard]] const std::string& driven() const { return body.empty() ? name : body; }
};

struct StagingDesc {
    std::vector<ActorDesc> actors;
    std::vector<ScenarioDesc> scenarios;
    [[nodiscard]] bool empty() const { return scenarios.empty(); }
};

// Every composition node a scenario can move: its actors, their parts, and everything its queries
// can bind -- by name, or by a tag any entity carries.
//
// This exists so a *save* can leave those nodes alone (ADR-264). A scenario hides an animal it has
// abducted and shows a beam while it fires, so the value such a node has at the moment somebody
// presses save is a photograph of a run rather than anything an author wrote -- and a project's
// `parameters` block is applied over its scene, so saving it means the next load starts with an
// invisible goat and a beam that is always on. That is not hypothetical: it is what four
// consecutive saves of Glowmere did while this was being written.
//
// `tagsOf` is a callback rather than a container because the two callers hold their entity
// descriptors differently, and copying every tag list to ask one question is a poor trade.
[[nodiscard]] std::set<std::string> scenarioOwnedNodes(
    const StagingDesc& staging,
    const std::function<std::string(const std::string&)>& nodeOf,
    const std::function<std::vector<std::string>(const std::string&)>& tagsOf,
    const std::vector<std::string>& everyEntity);

// ---- runtime ------------------------------------------------------------------------------------

// What the director may ask about the *picture*, as distinct from about the simulation.
//
// An interface rather than a `scene::Composition&` for the reason every other seam in this file is
// one: `stage/` decides what actors do and knows about entities, parameters and signals. It does
// not know what a mesh is, and the day it does is the day the director cannot be tested without a
// scene. `Composition` implements this in one function.
//
// Both answers are for the same node and are returned together because they are the same lookup,
// and because a caller that took one without the other would be a caller that had re-derived the
// second from stale halves of the first.
struct VisualPlacement {
    glm::vec3 centre{0.0f}; // world: where this node's contribution to the picture is centred
    glm::vec3 origin{0.0f}; // world: the node's own origin, parent chain and parameters included
    // `centre - origin`: the world-space vector from the node's origin to that centre. Constant in
    // the node's own space and therefore a property of the asset; it is returned already rotated
    // because the caller wants it in world and the rotation is the flattening's to know.
    [[nodiscard]] glm::vec3 offset() const { return centre - origin; }
};

class IVisualPlacement {
public:
    virtual ~IVisualPlacement() = default;
    // False when there is no such node, or when the scene has not been flattened yet -- on frame
    // zero there is nothing drawn to ask about. A caller that gets false must fall back to the
    // simulation's answer rather than to a zero, which is the whole reason this returns a bool.
    [[nodiscard]] virtual bool visualPlacement(std::string_view node, VisualPlacement& out) const = 0;
};

struct StageContext {
    double time = 0.0; // the timeline second, never a wall clock
    double dt = 0.0;
    entity::EntityWorld* world = nullptr;
    params::ParameterSet* params = nullptr;
    // Null is legal and means `Anchor::Drawn` falls back to `Visual`: a headless tool that drives
    // the director without a composition still runs, and gets the answer it had before this existed.
    const IVisualPlacement* visuals = nullptr;
    // What a scenario's `startOn` / `stopOn` are read against. Null is legal and means a scenario
    // with either of those never fires -- which is honest, and is what a headless tool that never
    // built a bus gets.
    const signals::SignalBus* bus = nullptr;
};

// What the director did, for a test, a log and an overlay. Every one of these is a fact somebody
// asked for rather than a number that happened to be available: "did it pick a different cow each
// time" and "did it ever scan the whole scene" are the two questions this work has to answer.
struct StageReport {
    std::size_t searches = 0;   // candidate-index rebuilds
    std::size_t candidates = 0; // entities in the index at the last rebuild
    std::size_t queries = 0;
    std::size_t tested = 0;     // candidates those queries actually examined
    std::size_t bound = 0;      // queries that found something
    std::size_t unbound = 0;    // queries that did not
    std::size_t retired = 0;
    std::size_t cycles = 0;     // completed scenario cycles, across every scenario
    std::size_t stepsDone = 0;
    std::size_t stepsFailed = 0;
};

enum class StageEventKind : std::uint8_t {
    Started,
    Bound,     // a query bound a role; `detail` is the entity
    Unbound,   // a query found nothing
    Beat,      // a beat was entered
    StepDone,
    StepFailed,
    Retired,
    Cycled,
    Finished,
    Cancelled,
};
[[nodiscard]] const char* stageEventKindName(StageEventKind kind);

struct StageEvent {
    StageEventKind kind = StageEventKind::Started;
    std::string scenario;
    std::string beat;
    std::string step;
    std::string role;
    std::string detail; // the entity a role bound to, or why a step failed
    double time = 0.0;
};

class Staging {
public:
    Staging();
    ~Staging();
    Staging(const Staging&) = delete;
    Staging& operator=(const Staging&) = delete;

    // Validates and installs. Rejects a scenario whose actor does not exist, a query that binds
    // nothing, a `Value` naming a parameter the scenario never declared, and a `then`/`otherwise`
    // naming a beat that is not there -- because every one of those is a scenario that would do
    // nothing, silently, for ever, which is the failure this project keeps writing ADRs about.
    [[nodiscard]] Result<void> setDesc(StagingDesc desc);
    [[nodiscard]] const StagingDesc& desc() const { return desc_; }
    void clear();

    // Ordinary parameters, so every director knob is keyframeable and presettable. Must run before
    // any route or track binds against one.
    void registerParameters(params::ParameterSet& params, const std::string& prefix = "staging/");
    void unregisterParameters(params::ParameterSet& params);
    [[nodiscard]] const std::string& prefix() const { return prefix_; }

    // ---- "why is this parameter moving?" ---------------------------------------------------------
    //
    // A scenario reaches a parameter in two ways and the Inspector has to be able to name both
    // (ADR-211 recorded this as not done; ADR-241 does it). `PathWriter` is one answer: the scenario,
    // the role that resolved it when a role did, and whether the scenario is running right now.
    //
    // `live` distinguishes the two sources deliberately. A step whose target is an *absolute* path is
    // known from the description alone, before anything runs -- that is the tractor beam's
    // `nodes/<beam>/visible`, the case this exists for. A step whose target is a role-relative name
    // cannot be resolved until the role binds, so it can only be reported once it has been written.
    // Reporting the first as "not yet" would answer "what can move this" with "nothing".
    struct PathWriter {
        std::string path;
        std::string scenario;
        std::string role;   // empty when the step named an absolute path
        bool running = false;
        bool live = false;  // true: observed being written. false: read off the description.
    };
    [[nodiscard]] std::vector<PathWriter> writersOf(std::string_view path) const;

    // ---- the API the sequencer calls -------------------------------------------------------------
    //
    // A sequencer track is "start this scenario at 12.5 s", "stop it at 40 s", "jump it to the beat
    // called `depart`". All three are here and none of them needs the sequencer to know what a beat
    // is. `now` is the timeline second.
    bool start(std::string_view scenario, double now);
    bool stop(std::string_view scenario, double now);  // ends it and releases everything it held
    bool trigger(std::string_view scenario, std::string_view beat, double now); // jump to a beat
    [[nodiscard]] bool running(std::string_view scenario) const;
    [[nodiscard]] int cycles(std::string_view scenario) const;
    [[nodiscard]] std::string_view beat(std::string_view scenario) const; // the beat it is in, or ""
    // The entity a running scenario has bound to a role, or "".
    [[nodiscard]] std::string_view binding(std::string_view scenario, std::string_view role) const;

    // One director parameter, by scenario and name. The same value the parameter set holds; this is
    // the spelling for a caller that does not want to build the path.
    [[nodiscard]] float parameter(std::string_view scenario, std::string_view name) const;
    bool setParameter(std::string_view scenario, std::string_view name, float value);

    // ---- per frame -------------------------------------------------------------------------------

    // Decides, then writes. Called before `EntityWorld::update` so an override issued this frame
    // takes effect this frame; it reads the action events the *previous* update produced, which is
    // the one-frame latency that makes the whole thing a pure function of the frame sequence.
    void update(const StageContext& ctx);

    // Everything back to the start: no scenario running, every claim dropped, every parameter this
    // director wrote put back to the value the scene authored, every director hold on a body
    // released. What a timeline seek needs, and what a test needs between arms.
    void reset(entity::EntityWorld* world = nullptr, params::ParameterSet* params = nullptr);

    [[nodiscard]] const StageReport& report() const { return report_; }
    // Everything that happened in the last update, in order.
    [[nodiscard]] const std::vector<StageEvent>& events() const { return events_; }
    // Every event since the last `clearLog()`, capped. What a headless run counts abductions from.
    [[nodiscard]] const std::vector<StageEvent>& log() const { return log_; }
    void clearLog() { log_.clear(); }
    void setLogLimit(std::size_t limit) { logLimit_ = limit; }
    // Entities a `retire` step has consumed, in the order it consumed them. The abduction count.
    [[nodiscard]] const std::vector<std::string>& retired() const { return retired_; }
    // Whatever did not resolve. Never silently empty because a problem was swallowed.
    [[nodiscard]] const std::vector<std::string>& problems() const { return problems_; }

private:
    struct Claim {
        std::string entity;
        std::string holder; // "<scenario>/<role>"
    };
    struct Candidate {
        std::string name;
        glm::vec3 position{0.0f};
        std::size_t entity = 0; // index into EntityWorld::entities()
    };
    struct Binding {
        std::string role;
        std::string actor;  // "" when the role is a plain entity
        std::string entity;
    };
    struct CueRun {
        std::size_t step = 0;
        bool done = false;
        bool started = false;
        double startedAt = 0.0;
        // MoveTo's captured start, so the tween is a lerp with easing rather than a chase.
        glm::vec3 from{0.0f};
        float span = 0.0f;   // the distance the tween has to cover
        float progress = 0.0f;
        double phase = 0.0;  // the wobble's own clock, so it is continuous across a step
        bool issued = false;      // an `Actions` step has handed its list over
        std::string reason;       // why the step failed, when it did
    };
    struct Run {
        std::size_t scenario = 0;
        bool running = false;
        std::size_t beat = 0;
        bool entered = false;
        int cycle = 0;
        double beatStart = 0.0;
        Rng rng{1u};
        std::vector<Binding> bindings;
        std::vector<CueRun> cues;
        std::vector<params::Parameter<float>*> params;
        std::optional<signals::SignalId> startId;
        std::optional<signals::SignalId> stopId;
    };
    struct Written {
        std::string path;
        std::vector<float> base; // what it held before this director first wrote it
        std::string scenario;    // which scenario reached this path
        std::string role;        // the role whose entity resolved it, empty for an absolute path
    };

    enum class StepStatus : std::uint8_t { Running, Done, Failed };

    void refreshCandidates(const StageContext& ctx);
    void enterBeat(Run& run, const StageContext& ctx);
    void leaveBeat(Run& run, const StageContext& ctx);
    void finish(Run& run, const StageContext& ctx, StageEventKind why);
    [[nodiscard]] bool runQuery(Run& run, const QueryDesc& query, const StageContext& ctx);
    [[nodiscard]] StepStatus advance(Run& run, CueRun& cue, const CueDesc& desc, const StepDesc& step,
                                     const StageContext& ctx);

    [[nodiscard]] float value(const Run& run, const Value& v) const;
    [[nodiscard]] entity::Entity* resolve(const Run& run, std::string_view role,
                                          const StageContext& ctx) const;
    [[nodiscard]] std::string resolveName(const Run& run, std::string_view role) const;
    // Which world point of a body an `Anchor` names, and how far that point is from the node origin
    // the director actually writes. See staging.cpp; the pair exists because a destination and the
    // thing put on it are two halves of one question.
    [[nodiscard]] glm::vec3 pointOn(const entity::Entity& e, Anchor anchor,
                                    const StageContext& ctx) const;
    [[nodiscard]] glm::vec3 placementOffset(const entity::Entity& e, Anchor place,
                                            const StageContext& ctx) const;
    [[nodiscard]] bool resolvePoint(const Run& run, const StepDesc& step, const StageContext& ctx,
                                    glm::vec3& out) const;
    [[nodiscard]] std::string parameterPath(const Run& run, std::string_view role,
                                            std::string_view target, const StageContext& ctx) const;
    void writeParameter(const Run& run, std::string_view role, const std::string& path,
                        float value, const StageContext& ctx);
    void bindRole(Run& run, const std::string& role, const std::string& entity,
                  const std::string& actor);
    void releaseClaims(const Run& run);
    [[nodiscard]] bool claimed(std::string_view entity) const;
    [[nodiscard]] bool isRetired(std::string_view entity) const;
    // An actor, or one of an actor's parts -- never a target, and exempt from the visibility rule
    // below because Glowmere's beam is authored invisible on purpose.
    [[nodiscard]] bool isDirectorsOwn(std::string_view name) const;
    // Hidden by something that stuck: a query never offers one. See the note at the definition for
    // why this reads the base rather than the final.
    [[nodiscard]] bool hiddenForGood(const entity::Entity& e, const StageContext& ctx) const;
    void emit(StageEventKind kind, const Run& run, const StageContext& ctx, std::string step,
              std::string role, std::string detail);

    StagingDesc desc_;
    std::vector<Run> runs_;
    std::vector<Claim> claims_;
    std::vector<std::string> retired_;
    std::vector<Candidate> candidates_;
    std::vector<glm::vec3> candidatePoints_;
    std::vector<std::string> searchTags_; // the tags any query in the description mentions
    spatial::PointGrid grid_;
    double lastSearch_ = -1.0e30;
    bool needSearch_ = true;

    std::vector<Written> written_;
    std::vector<std::string> registered_;
    std::string prefix_ = "staging/";
    // Said once per Staging rather than once per frame: a director running sixty times a second
    // against a caller that never flattens would otherwise be a log file.
    mutable bool warnedNoDrawn_ = false;

    std::vector<StageEvent> events_;
    std::vector<StageEvent> log_;
    std::size_t logLimit_ = 4096;
    std::vector<std::string> problems_;
    StageReport report_;

    // The last update's world and parameter set, so `stop` and `trigger` -- which the sequencer
    // calls between frames, with no context of their own -- can release the bodies and the claims a
    // running scenario was holding. Borrowed, and only ever the ones `update` was just handed.
    entity::EntityWorld* lastWorld_ = nullptr;
    params::ParameterSet* lastParams_ = nullptr;

    mutable std::vector<std::uint32_t> hits_; // scratch for a grid query; never grows in steady state
};

// ---- serialisation ------------------------------------------------------------------------------
//
// All of it data, all of it in the scene file, none of it scene-specific.

[[nodiscard]] Result<StagingDesc> stagingFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json stagingToJson(const StagingDesc& desc);
[[nodiscard]] Result<ScenarioDesc> scenarioFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json scenarioToJson(const ScenarioDesc& scenario);
[[nodiscard]] Result<ActorDesc> actorFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json actorToJson(const ActorDesc& actor);

} // namespace avgen::stage
