#pragma once

// The shared interfaces for the Autonomous Character Intelligence work (ADR-266..ADR-270).
//
// **This file is normative and it is the only place these types are defined.** The brief warns
// that several agents working in parallel must not each invent their own version of character
// state, perception, navigation, behaviour state or animation intent. Three of those five already
// exist under other names and are *not* redefined here -- §0 says which type is the real one and
// what an implementation agent owes it. Two do not exist and are declared here, with contracts and
// no implementation, so that the agent who writes the first one writes it against this.
//
// Nothing in this file compiles into the engine. `src/CMakeLists.txt` globs `entity/*.cpp`; a
// header nobody includes is inert by construction, which is the point: Phase 0 ships a contract,
// not a runtime.
//
// ------------------------------------------------------------------------------------------------
// §0. What already exists. Do not build a second one of any of these.
// ------------------------------------------------------------------------------------------------
//
// | the brief's name  | the type that already is it        | where                       |
// |-------------------|------------------------------------|-----------------------------|
// | character state   | `entity::EntityState`              | entity/behavior.hpp:48      |
// |                   | + `entity::Entity`                 | entity/entity.hpp:256       |
// | navigation        | `entity::Navigator`                | entity/navigation.hpp:113   |
// |                   | + `NavGrid`, `PathRequest/Result`  | entity/nav_grid.hpp         |
// |                   | + `entity::IPathProvider`          | entity/action.hpp:83        |
// | behaviour state   | `entity::IBehavior`                | entity/behavior.hpp:126     |
// |                   | + `entity::ActionQueue`/`Authority`| entity/action.hpp:253,281   |
// |                   | + `entity::Schedule`               | entity/action.hpp:393       |
// | animation intent  | `entity::LocomotionState`          | entity/locomotion.hpp:52    |
// |                   | + `IPoseSink`, `ISkeletonQuery`    | entity/locomotion.hpp:90,99 |
// | directed staging  | `stage::Staging`                   | stage/staging.hpp:485       |
//
// The four rules that go with them, each of which has cost this project a shipped defect:
//
// **R1 (ADR-260). A character has three positions and only one is drawn.** `state().position()` is
// `anchor + travel` -- the simulation, and what navigation, the crowd field and every query read.
// `visualPosition()` adds the behaviours' `MotionOffset`. The node's `position` parameter is what
// the renderer draws. Every new type below states which one it reads and which one it writes, in
// its own comment, because "the position" is not a thing this engine has.
//
// **R2 (ADR-240). Facing starts at the placement, it is not added to it.** `EntityState::yaw` is
// an absolute heading that begins at `NodeBinding::facing`; `applyOffsets` writes the difference.
// A new layer that writes yaw writes an absolute heading.
//
// **R3 (ADR-210). A decision layer writes intent, never a transform.** `stage::Staging` moves a
// body by writing `entity::DirectorMotion` and by handing `entity::ActionDesc`s to
// `EntityWorld::direct`. Nothing outside `EntityWorld::update` touches a node's transform. A new
// decision layer obeys the same rule and for the same reason: two writers of one transform is a
// class of bug this repository has already paid for twice.
//
// **R4 (ADR-096). No layer names a clip.** An action, an interaction and a decision name an
// *activity* -- "sit", "observe", "flee" -- and `EntityDesc::clips` maps it onto whatever the asset
// shipped. The nine farm animals ship one clip called `Walk`; the six aliens ship twenty-six.
//
// ------------------------------------------------------------------------------------------------
// §1. Determinism: the contract every type in this file is written to satisfy
// ------------------------------------------------------------------------------------------------
//
// Measured, not assumed (tools/charai_probe.cpp, `glowmere-valley-2`, load average 3.75):
//
//     play(60 Hz, 30 s) vs seek(60 Hz step, 30 s)          0.000022 m
//     play(jittered 45-90 Hz) vs seek(60 Hz step)          0.094877 m
//     play(60 Hz) vs play(30 Hz)                           0.955805 m
//     play(LOD on, camera at origin) vs (camera at 200 m)  50.263096 m
//
// So the existing entity simulation *already* reproduces a scrub to within 22 micrometres, and the
// two things that break it are (a) the frame rate and (b) the behaviour LOD band. Neither is about
// autonomy. The conclusion that shapes every interface here:
//
//   **The AI is not a pure function of (seed, time), and does not need to be. It is a pure function
//   of (seed, the ordered sequence of fixed simulation steps).**
//
// which yields four obligations on anything below:
//
// **D1. No wall clock, no frame index, no global randomness.** Integrate against the `dt` in the
// context, which is the simulation step and not the frame's.
//
// **D2. Draw from a seed and an index, never from a stream, for anything a *decision* depends on.**
// `src/app/cinematic.cpp:1555-1587` is the model: `hash(seed) + i * 0x9E3779B1`. A PRNG stream
// makes every later choice depend on how many earlier ones were made, so one extra rejection in
// `pickDestination` re-casts everything after it. `Entity::rng_` stays where it is for the existing
// behaviours; nothing new takes a draw from it.
//
// **D3. Level of detail may change *which stages run*, never the integration step.** The 50.263 m
// above is the coarse band integrating one 0.1 s step where full detail integrates six of 1/60,
// through a steering function that is not linear in dt. A perception layer that runs at 4 Hz
// instead of 60 is fine; a locomotion integrator that takes bigger steps when the camera looks away
// is not, and `src/entity/action.hpp`'s claim that "behaviour LOD does not change the answer" is
// measurably false for behaviours.
//
// **D4. Memory is bounded, and it is reconstructed by the replay rather than persisted.** Anything
// a character remembers must be recoverable by re-simulating from `t - maxSeconds`. A memory that
// only a saved file could restore is a memory that makes a scrub differ from a play.

// ADR-290 changed exactly one thing here and changed nothing it says: `entity/entity.hpp` was
// included and is now `entity/action.hpp`, with `EntityWorld` left to the forward declaration
// `entity/behavior.hpp` already carries. Nothing below needs `EntityWorld` to be complete -- every
// use of it is a reference or a pointer -- and `entity.hpp` has to be able to include *this*, now
// that `EntityDesc` carries a `PerceptionSettings`. A normative header the thing it describes
// cannot include is a header that stays inert, which is what it was.
#include "entity/action.hpp"
#include "entity/behavior.hpp"
#include "entity/locomotion.hpp"
#include "entity/navigation.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace avgen::entity {

// ------------------------------------------------------------------------------------------------
// §2. Perception -- the layer that genuinely does not exist
// ------------------------------------------------------------------------------------------------
//
// Today every character sees everything. `EntityWorld::interestPoints()` is one global list --
// **505 entries** on `glowmere-valley-2` -- and `Explore` scores all of them every time it picks a
// goal, with no distance limit, no facing, no occlusion and no notion of having noticed something.
// That is why two characters in the same world walk the same route: they are reading the same
// omniscient list with different weights.
//
// The cost constraint, measured, is what shapes this interface more than anything else:
//
//     world::heroSightline, 9 rays @  20 m     1720.779 us
//     world::heroSightline, 9 rays @  60 m     5391.917 us
//     Navigator::sample (analytic world)         10.325 us
//     PointGrid candidate scan @ 60 m             0.098 us   (14.8 candidates, over the real 505)
//     Navigator::clearanceAt (grid lookup)        0.024 us
//     Navigator::obstructed  (grid lookup)        0.011 us
//
// One sightline per character per frame at 100 characters is **172 ms a frame**. The camera can
// afford `heroSightline` because there is one camera. A crowd cannot, and any design that assumes
// per-character ray-marched line of sight is priced out before it is written. The grid, by
// contrast, is four hundred thousand times cheaper per query than the sightline and four hundred
// times cheaper than the analytic world -- and nothing but the path planner reads it.
//
// So: perception is a **budgeted, cadenced, grid-backed** stage. It is not a sense simulation.

// What one character noticed about one thing.
//
// A value, deliberately: a percept is a fact about a moment, it is copied into the character's
// working set, and nothing holds a pointer into the world across a frame boundary. `source` is an
// index into `EntityWorld::entities()` for a Character percept and into `interestPoints()`
// otherwise, so a consumer can go back to the thing itself without this struct carrying a name.
struct Percept {
    InterestKind kind = InterestKind::Landmark;
    std::size_t source = 0;
    // World position, and **which** position (R1). For a Character percept this is
    // `state().position()` -- the simulation's answer -- because a percept feeds a decision and a
    // decision feeds navigation, and navigation reasons in simulation space. A percept is never
    // built from `visualPosition()`: a saucer carrying a 2.4 m drift would be perceived somewhere
    // it is not standing.
    glm::vec3 position{0.0f};
    float distance = 0.0f;       // metres, from the perceiver's own simulation position
    // 0..1. How much of the thing the perceiver can see, in the sense `world::Sightline::visible`
    // means it. **1 when occlusion was not tested**, which is the usual answer -- see `tested`.
    float visibility = 1.0f;
    bool tested = false;         // whether `visibility` was measured or assumed
    // Seconds on the engine timeline when this was last refreshed. A timeline second, never a wall
    // clock (D1), so a percept that survives into a coarser update still says how stale it is.
    double seenAt = 0.0;
    float salience = 0.0f;       // 0..1, the sense stage's own ranking; see `PerceptionSettings`
};

// What a body can notice. Per character, because a six-metre alien and a chicken do not have the
// same senses, and because these are the numbers an author tunes when a character is not reacting
// to something they expected it to react to.
//
// Every field is a plain number rather than a `params::Parameter` **in this struct**. The parameter
// registration belongs to whatever owns the perception stage, for the reason `GaitSettings` is a
// plain struct: the settings are a value that can be compared, defaulted and serialised, and the
// keyboarding of them onto the timeline is a separate concern.
struct PerceptionSettings {
    float range = 60.0f;         // metres. Nothing beyond this is ever a percept.
    // Degrees of horizontal field of view, centred on `EntityState::yaw`. 360 is a body that
    // notices what is behind it, which is right for a herd animal and wrong for a character an
    // author wants to surprise.
    float fieldOfView = 200.0f;
    // Metres within which `fieldOfView` does not apply. A thing standing next to you is noticed.
    float proximityRange = 4.0f;
    // How many percepts this character may hold. A hard cap, not a hint: the sense stage sorts by
    // salience and keeps the top `capacity`. Bounded working sets are what make D4 possible.
    std::uint16_t capacity = 8;
    // Sense updates per second. 4 Hz is fifteen frames of staleness at 60, which is below the time
    // it takes a character to turn its head.
    //
    // **This is not the budget.** A candidate scan measures 0.098 us at 60 m over the real 505
    // interest points -- a `spatial::PointGrid` lookup over 27 cells -- so even at a full 60 Hz it
    // is 0.45 us a character against the 89 us the character's behaviours already cost. The cadence
    // exists for two other reasons: a character that re-senses every frame reacts instantaneously
    // and reads as a machine, and `Percept::seenAt` is meaningless if it is always now. Staleness
    // is the only thing that lets a character be *wrong* about where something is, and being wrong
    // is most of what this buys over the omniscient list it replaces.
    //
    // D3 permits this to fall with distance. It does not permit the integrator's step to change.
    float hertz = 4.0f;
    // Whether this character's percepts are occlusion-tested at all, and how many tests per second
    // the whole world may spend on it. 0 -- the default -- means `visibility` is always 1 and
    // `tested` always false, which is honest and free. A test is `world::heroSightline` and costs
    // 1.7 ms at 20 m; a world that turns this on for a crowd has chosen to spend its frame on it.
    float occlusionTestsPerSecond = 0.0f;
    // Per-kind weights, the same taste model `Explore` already carries as `landmarkAffinity` and
    // the rest -- lifted out of that class so two behaviours can share one character's taste.
    float weight[5] = {1.0f, 1.3f, 1.2f, 0.9f, 0.7f};
};

// The sense stage.
//
// An interface rather than a concrete class for one reason: the Character Intelligence Lab needs a
// scripted implementation whose percepts a test writes by hand, so a decision layer can be asserted
// without a world. That is the same reason `IPathProvider` is an interface, and the same reason
// `NavigatorPath` sits next to it.
//
// **The implementation contract, which is the part that matters:**
//
//   * `perceive` is called at most `PerceptionSettings::hertz` times a second per character, from
//     inside the entity update, before behaviours run. It may be called less often; it is never
//     called with a wall clock.
//   * It must be a pure function of (the world's state at this step, `self`, `settings`, `seed`).
//     No draws from `Entity::rng_` (D2). Where a choice among equals is needed -- two landmarks at
//     the same salience -- break it on the lower `source` index, the way `NavGrid`'s A* breaks ties
//     on cell index, so the answer does not depend on iteration order.
//   * It writes into `out` and returns how many percepts it wrote, never exceeding
//     `settings.capacity`. It does not allocate: the caller owns the storage and reuses it.
//   * It reads `state().position()` and `state().yaw` of every candidate (R1), and it never reads a
//     node parameter.
//
// **What it must not do:** it must not test occlusion for more bodies than
// `occlusionTestsPerSecond` allows, and when the budget is spent it must leave `tested` false
// rather than skipping the percept. A percept silently dropped because a budget ran out is the
// failure mode that makes a character's behaviour depend on how many other characters exist.
class IPerception {
public:
    virtual ~IPerception() = default;
    [[nodiscard]] virtual std::size_t perceive(const EntityWorld& world, std::size_t self,
                                               const PerceptionSettings& settings,
                                               std::uint32_t seed, double time,
                                               std::span<Percept> out) const = 0;
};

// ------------------------------------------------------------------------------------------------
// §3. Decision -- a chooser, not a second execution engine
// ------------------------------------------------------------------------------------------------
//
// The brief names Unreal's Behavior Tree and StateTree. Neither is adopted, and the reason is that
// this repository already has the half of a behaviour tree that is hard and none of the half that
// is easy. `ActionQueue` (entity/action.hpp) already provides: three authority tiers with
// preemption and *resumption* including elapsed time; eight primitives; targets; start conditions;
// branch labels; completion events; exclusive interaction claims; a pause-aware `Schedule`. A
// behaviour tree would be a second interpreter over the same primitives, with its own notion of
// running/succeeded/failed alongside `ActionResult`, and the two would drift.
//
// What is missing is the thing above it: **nobody decides.** `stage::Staging` decides for authored
// scenarios and is better at it than a per-entity tree could be, because its parallelism is across
// *different entities* -- which a per-entity tree structurally cannot express (ADR-210 §3). The
// only autonomous decider in the engine is hardcoded inside `Explore`, 700 lines, one class, which
// is why every autonomous character in Glowmere is an explorer.
//
// So the decision layer is a **scored option list**, and its output is an `ActionDesc` list pushed
// onto a tier the existing queue already understands. That is the whole of it.

// One thing a character could do, and how much it wants to.
//
// `score` is the only comparison. There is deliberately no priority band, no interrupt flag and no
// cooldown field: a cooldown is a term in the score, and an option that must win is an option that
// scores higher. One axis is what makes the result explicable -- an overlay can print the losing
// scores next to the winner, and "why is it doing that" has an answer.
struct Option {
    std::string_view name;          // stable, for the overlay and the test
    float score = 0.0f;             // higher wins; <= 0 is "not applicable now"
    // What winning means, as actions the existing queue runs. Empty is legal and means "this option
    // wins by doing nothing", which is what an idle is.
    std::span<const ActionDesc> actions;
    Authority tier = Authority::Routine;
};

// What a considerer is given. Everything it may read, and nothing else: an option scorer that
// reached into the world directly could not be tested against a scripted perception.
struct DecisionContext {
    double time = 0.0;              // timeline seconds (D1)
    double dt = 0.0;                // the simulation step since this character last decided
    std::size_t self = 0;
    const EntityState* state = nullptr;   // R1: `position()` is the simulation's answer
    // What this body knows. ADR-290 builds this fresh every sense tick and deliberately does not
    // accumulate it; ADR-310 §5 is where the decider folds a bounded fade back in, so what arrives
    // here may include a percept whose `seenAt` is older than the last tick. That is why `seenAt`
    // is on a percept at all.
    std::span<const Percept> percepts;
    // Where this character has recently been. ADR-310 §3: the goal model suppresses a place the
    // body has already visited, and that history is the one thing in the model that is not a fact
    // about the world. It is carried **in the context rather than in the considerer** because a
    // considerer holds no per-character state -- that rule is what makes D4 free, and a novelty
    // memory living inside a shared scorer would break it for every character at once.
    //
    // Bounded by the caller, reconstructed by a replay, never persisted (D4).
    std::span<const glm::vec3> visited;
    const Navigator* nav = nullptr;
    const EntityWorld* world = nullptr;
    const signals::SignalBus* bus = nullptr;
    // The character's seed. Draws are (seed, index) hashes (D2), never a stream.
    std::uint32_t seed = 0;
};

// Scores options. One instance per *kind* of character, shared across every character of that kind:
// it holds no per-character state, which is what makes D4 free -- there is nothing in a considerer
// to checkpoint.
//
// **The implementation contract:**
//
//   * `consider` appends to `out` and must not clear it: several considerers compose by being run
//     in order, which is how "what this species does" and "what this scene asked for" coexist.
//   * It must be a pure function of `ctx` and its own settings. Given the same context twice it
//     must produce the same scores. That is the property the Character Intelligence Lab asserts,
//     and it is the reason a considerer may not draw from `Entity::rng_`.
//   * Spans in `Option::actions` must point at storage the considerer owns and keeps until its next
//     `consider`, exactly as `NavDebug`'s spans do.
//   * It must never write to `EntityState`, `MotionOffset` or any parameter (R3). Scoring is a
//     read; acting is the queue's job.
//
// **Hysteresis is the caller's, not the considerer's.** A selector that switched to whichever
// option scored highest each tick would thrash exactly the way the gait machine thrashed before
// `GaitSettings::minDwell`, and for the same reason. The rule is the one `Gait` already proved: a
// chosen option holds for a minimum dwell and must be beaten by a margin, not by a tie.
class IConsiderer {
public:
    virtual ~IConsiderer() = default;
    virtual void consider(const DecisionContext& ctx, std::vector<Option>& out) const = 0;
};

// ------------------------------------------------------------------------------------------------
// §4. Animation intent -- `LocomotionState` is the interface; here is what it still owes
// ------------------------------------------------------------------------------------------------
//
// `entity::LocomotionState` (entity/locomotion.hpp:52) is already the animation-intent seam and it
// is already right. It carries activity, the action's activity name, playback rate, blend, the
// timeline second the decision was made at, plus position, yaw, speed, turnRate, reaction and a
// look target. It is published every frame into `IPoseSink`.
//
// The problem is not the interface. It is that **four of its fields are read by nothing**:
// `reaction`, `lookTarget`, `hasLookTarget` and the kinematics are published and ignored
// (`scene/composition.cpp:1892-1906` uses only `action`, `activity`, `time`, `blend`,
// `playbackRate`). The seam is wider than the implementation on the far side of it.
//
// And underneath, the animation layer is smaller than the brief assumes. Verified: one cross-fade
// between exactly two clip slots (`scene/animation.cpp:346-356`); **no additive, no masks, no layer
// stack** (grep for `additive`, `jointMask`, `layerWeight` in `src/scene/animation.*` and
// `src/scene/skeleton.*` returns nothing); **no IK of any kind**; **no morph targets** -- they are
// refused at import (`assets/gltf_loader.cpp:404,548`); **root motion is never extracted**, though
// five alien clips genuinely travel and `Landing`'s -0.567 m is discarded in production; and
// `ISkeletonQuery` has **zero implementations and `Entity::setSkeleton` has zero call sites**, so
// `Entity::socketTransform` returns the entity frame for every socket and returns `true` while
// doing it, which is worse than returning false.
//
// So the animation-intent work is not "design an interface". It is:
//
//   A1. Implement `ISkeletonQuery` over `scene::SkinnedRig` and call `Entity::setSkeleton`. This is
//       the smallest change with the largest reach: it is the only thing between the engine and
//       every socket, every attachment and every aim.
//   A2. Make `socketTransform` report the fallback instead of hiding it.
//   A3. Consume `reaction` and `lookTarget`, which needs a second animation layer -- so a layer
//       stack with a joint mask is the prerequisite, not an enhancement.
//   A4. Extract root motion behind a per-clip opt-in, because the content has it and the engine
//       throws it away.
//
// None of those needs a new type in this file. What does need one is the statement of *which
// position an animation layer may write*, because that is R1 and it is where the bugs are:

// Which of a character's three positions a layer is permitted to write (R1, ADR-260).
//
// Carried as an explicit value on any new animation or behaviour layer, and asserted by the
// Character Intelligence Lab, because "which position did you mean" has been the shape of four
// separate defects in this repository -- the tractor beam 28.661 m from the body it was lifting
// being the most expensive.
enum class MotionAuthority : std::uint8_t {
    // Writes `EntityState::travel`. Navigation, actions and the director tier. The simulation moves.
    Simulation,
    // Writes `MotionOffset`. Hover, drift, bank, grounding's lean. The drawing moves and the
    // simulation does not, which is why a saucer keeps hovering while it is being flown somewhere.
    VisualOffset,
    // Writes neither: it produces a pose, and the pose is drawn where the node already is. Every
    // animation clip is this, and root motion extraction (A4) is the one case where an animation
    // layer would have to ask for `Simulation` instead -- which is exactly why it needs an opt-in.
    PoseOnly,
};

} // namespace avgen::entity
