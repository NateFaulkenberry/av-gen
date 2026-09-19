#pragma once

// The decision layer (ADR-269, ADR-333). What a character chooses to do, out of everything it
// could.
//
// `src/entity/character_ai.hpp` §3 is the normative declaration of `Option`, `DecisionContext` and
// `IConsiderer`; it compiled and nothing implemented it. This is the first implementation of that
// contract, and the header it belongs to rather than a second copy of the types.
//
// ADR-269's finding was that **nobody decides**: `ActionQueue` is a thing that can be told what to
// do, `stage::Staging` tells it for authored scenarios, and the only autonomous decider in the
// engine was hardcoded inside `Explore` -- 700 lines, one class, which is why every autonomous
// character in Glowmere is an explorer. The shape that removes that is a scored option list:
//
//   * **Considerers score, they do not act.** A considerer reads a `DecisionContext` and appends
//     `Option`s. It writes no state, no transform and no parameter (R3). Several run in order and
//     append to one list, which is how "what this species does" and "what this scene asked for"
//     coexist with no merge rule.
//   * **The selector picks, and owns the hysteresis.** Highest score wins, subject to a dwell and a
//     margin -- the rule `GaitSettings::minDwell` already proved, because a speed band alone does
//     not stop flicker in time and a dwell alone does not stop it in speed.
//   * **The queue keeps its job.** The winner's `ActionDesc` list is pushed onto
//     `Authority::Routine` and the existing queue runs it. Nothing here is a second interpreter.
//
// **Which position (R1, ADR-260).** Everything here reads `EntityState::position()` -- the
// simulation's answer -- and never `visualPosition()`, for the reason perception reads it: a
// decision feeds navigation, and navigation reasons in simulation space. Nothing in this file
// writes any of the three, and nothing in it writes `EntityState` at all.
//
// **Determinism.** Every considerer here is a pure function of its context and its settings: no
// draws from `Entity::rng_`, no wall clock, no frame index (D1, D2). The one thing that carries
// across frames is the selector's committed choice, and it is carried as a **tick index** rather
// than as an accumulator, for exactly the reason `entity::senseTick` is one (ADR-290 §2): a dwell
// counted in seconds off an accumulator expires on a different instant at a different frame rate,
// so a replayed decision would land on a different step from the played one. Counted in decision
// ticks, the boundary is a pure function of the instant and the replay reproduces it.

#include "entity/action.hpp"
#include "entity/character_ai.hpp"
#include "entity/nav_grid.hpp"
#include "params/parameter_set.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::entity {

// ---- the cadence -------------------------------------------------------------------------------

// Which decision tick `time` falls in for a character at this cadence with this seed.
//
// The same function `entity::senseTick` is, and deliberately the same shape: a pure function of
// (time, hertz, seed) with a seed-derived phase in [0, 1) ticks, so a cast does not all decide on
// the same frame. It is repeated here rather than shared because the two cadences are different
// knobs on different stages and a character may sense four times a second while deciding twice.
[[nodiscard]] std::uint64_t decideTick(double time, float hertz, std::uint32_t seed);

// ---- the selector ------------------------------------------------------------------------------

struct SelectorSettings {
    // Decisions per second. Deliberately slower than the senses: a character that re-decided every
    // frame would be re-deciding forty times inside one footfall, and the dwell would be the only
    // thing doing any work.
    float hertz = 2.0f;
    // The minimum number of decision ticks a committed option is held for, however badly it is
    // losing. Ticks rather than seconds because a tick boundary is a pure function of the instant
    // and a second accumulated across frames is not.
    float dwellTicks = 2.0f;
    // How far above the incumbent's *current* score a challenger must be to take the slot. A tie
    // is not enough; that is the whole of the hysteresis rule.
    float margin = 0.08f;
};

// The chooser. One per deciding character, because the committed option and the tick it was
// committed on are per-character facts -- which is exactly the state ADR-269 keeps *out* of a
// considerer so that a considerer can be shared and needs no checkpoint.
//
// `ScoredOption`, the flattened form an overlay reads, is declared in `entity/behavior.hpp` rather
// than here, so that `IBehavior::decisionDebug` can carry it without `behavior.hpp` depending on
// `character_ai.hpp`, which depends on it.
class Selector {
public:
    // Structural quantities, not milliseconds (ADR-170). "It scored 6 options, took 3 decisions and
    // refused 11 switches on dwell" survives a change of machine; a microsecond count does not, and
    // the dwell arm's control is exactly `dwellRejections == 0`.
    struct Counts {
        std::size_t ticks = 0;            // decision ticks that fired
        std::size_t scored = 0;           // options appended across every tick
        std::size_t decisions = 0;        // times the committed option changed
        std::size_t dwellRejections = 0;  // switches refused because the dwell had not expired
        std::size_t marginRejections = 0; // switches refused because the margin was not cleared
        std::size_t empty = 0;            // ticks at which nothing scored above zero
    };

    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

    void setSettings(const SelectorSettings& settings) { settings_ = settings; }
    [[nodiscard]] const SelectorSettings& settings() const { return settings_; }

    // Runs `considerers` in order into one list and picks. Returns true when the committed option
    // *changed* -- which is when the caller has something new to push onto the queue.
    //
    // Off a decision tick this does nothing at all and returns false: the scored list, the chosen
    // index and the counts are whatever the last tick left, which is what an overlay should be
    // drawing between decisions.
    bool select(const DecisionContext& ctx, std::span<const IConsiderer* const> considerers);

    // Every option scored on the last tick, in the order the considerers appended them. The spans
    // inside them point into the considerers and live until their next `consider`.
    [[nodiscard]] std::span<const Option> options() const { return options_; }
    [[nodiscard]] std::size_t chosen() const { return chosen_; }
    [[nodiscard]] std::string_view current() const { return current_; }
    // The tick the current option was committed on, and the tick the last `select` ran at.
    [[nodiscard]] std::uint64_t committedTick() const { return committedTick_; }
    [[nodiscard]] std::uint64_t tick() const { return tick_; }
    [[nodiscard]] bool started() const { return started_; }
    [[nodiscard]] Counts counts() const { return counts_; }

    void reset();

    // Forget the commitment, keep the counts (ADR-348).
    //
    // A decider whose plan has failed needs the *next* tick to be free to choose anything, which
    // `reset()` also gives it -- and `reset()` throws away `Counts`, which is the instrument an
    // overlay and every arm of the decision tests read. So a stall breaker that used `reset()`
    // would erase the evidence that it had fired. This clears the incumbent and nothing else, so
    // the next `select` reports a change, hands the queue a fresh action list, and the decision
    // count goes up by one -- which is the fact worth seeing.
    void forget();

private:
    SelectorSettings settings_{};
    std::vector<Option> options_;
    // The committed option's name, copied rather than viewed: a considerer may rebuild its storage
    // on the next tick and the incumbent has to survive that to be compared against.
    std::string current_;
    std::size_t chosen_ = kNone;
    std::uint64_t committedTick_ = 0;
    std::uint64_t tick_ = 0;
    bool started_ = false;
    Counts counts_{};
};

// ---- the goal model ----------------------------------------------------------------------------

// A character's taste, and the bounds it will walk between. Lifted verbatim out of `Explore`, where
// it was five affinity floats, a novelty radius and three range knobs inside a 700-line class.
//
// `weight` is indexed by `InterestKind`, in the same order `PerceptionSettings::weight` is and with
// the same defaults, so a character's taste is one set of numbers whether its senses or its
// decisions are reading them.
struct GoalTaste {
    float weight[5] = {1.0f, 1.3f, 1.2f, 0.9f, 0.7f};
    float minRange = 12.0f;   // nothing nearer is worth crossing the room for -- and it is also
                              // what stops a body choosing the landmark it is standing on
    float maxRange = 160.0f;
    float homeRadius = 0.0f;  // 0 = this character has no home to stay near
    float noveltyRadius = 22.0f;
    float noveltyPenalty = 0.12f; // what a place it has already visited is worth
};

// One thing worth going to, and how much this character wants to.
struct GoalCandidate {
    glm::vec3 position{0.0f};
    std::string_view name;    // into the interest point; empty for a derived point
    InterestKind kind = InterestKind::Landmark;
    float weight = 0.0f;
};

// **The goal model, as one function.** This is the thing that was extracted (ADR-333 §3): taste
// times the point's own weight, damped by distance, suppressed where the character has recently
// been. `Explore::pickGoal` inlined exactly this arithmetic and now calls it.
//
// The byte-identity of the extraction rests on this function being the same expressions in the same
// order as the code it replaced, so **the expressions here are not to be tidied**: the constant
// folding, the division rather than a reciprocal multiply, and the early `break` out of the visited
// scan are all load-bearing. `tests/unit/test_decision_extraction.cpp` holds a position trace taken
// from the build before the extraction and compares the raw float bits.
//
// Returns the weight, or a negative number for a point this character would not consider at all --
// which is a different fact from a point it considers and scores zero, and the caller wants both.
[[nodiscard]] float goalWeight(const GoalTaste& taste, glm::vec2 flat, glm::vec2 anchor,
                               float lo, float hi, float home, glm::vec3 position,
                               InterestKind kind, float pointWeight,
                               std::span<const glm::vec3> visited);

// The same model over a whole list, appending every candidate that survives. `out` is **not**
// cleared: the caller owns it, the way a considerer's `out` is owned by the selector.
//
// Two sources, deliberately, and the difference between them is the whole of ADR-270's finding.
// `interestPoints()` is the omniscient list every character has always read -- 140 entries in the
// lab's fixture, 505 in Glowmere, with no range, no facing and no notion of having noticed
// something. `percepts` is what this body actually knows. A character scoring the first walks the
// same route as every other character with the same taste; a character scoring the second does not.
std::size_t scoreGoals(const DecisionContext& ctx, const GoalTaste& taste,
                       std::span<const InterestPoint> points, std::vector<GoalCandidate>& out);
std::size_t scoreGoals(const DecisionContext& ctx, const GoalTaste& taste,
                       std::span<const Percept> percepts, std::vector<GoalCandidate>& out);

// ---- the stock considerers ---------------------------------------------------------------------

// What an author configures in a scene file and what the engine registers knobs for.
//
// `IConsiderer` (character_ai.hpp §3) is the contract and says nothing about configuration, because
// a considerer written by hand in a test needs none. This adds the two things a *scene-authored*
// one needs, and adds them here rather than to the normative header: a kind name, and the parameter
// registration that makes an authored weight a setting the application actually keeps (ADR-225).
//
// **"Considerers hold no per-character state" is a rule about mutable state, not about
// configuration.** One `HoldPost` instance is configured with one post and one weight and is then a
// pure function; two guards standing at two different gates are two instances of the same class
// with different settings, which is still no new C++ class per character kind. What a considerer
// may not do is remember anything between calls, and none of these does -- the two memories the
// decider needs (where it has been, what it saw a moment ago) are held by the *behaviour* and
// handed in through `DecisionContext`, precisely so that this stays true.
class StockConsiderer : public IConsiderer {
public:
    [[nodiscard]] virtual std::string_view kind() const = 0;
    // The option name this considerer publishes, which is what the selector compares and what an
    // overlay prints. Defaults to `kind()`, so two considerers of the same kind on one character
    // must be named apart in the scene file -- the same rule `BehaviorDesc::name` has.
    [[nodiscard]] std::string_view name() const { return name_; }
    void setName(std::string name) { name_ = std::move(name); }

    virtual void registerParameters(params::ParameterSet& params, const std::string& prefix) {
        (void)params;
        (void)prefix;
    }
    virtual void collectParameterPaths(std::vector<std::string>& out) const { (void)out; }

protected:
    // Every considerer has one, and it multiplies its whole score. It is the knob an author reaches
    // for first -- "this guard cares more about its post than that one does" -- and it is
    // registered rather than read once, because ADR-225's rule is that a setting the application
    // does not keep is not a setting.
    void registerWeight(params::ParameterSet& params, const std::string& prefix, float def);
    void collectWeightPath(std::vector<std::string>& out) const;
    [[nodiscard]] float weight() const { return weight_ != nullptr ? weight_->value() : weightDefault_; }

    std::string name_;
    params::Parameter<float>* weight_ = nullptr;
    float weightDefault_ = 1.0f;
    std::string weightPath_;
};

// The floor. One option, one constant score, no actions: "this option wins by doing nothing",
// which character_ai.hpp §3 names as the legal empty case.
//
// It earns its place by being the thing every other option is measured against. Without it a
// character whose considerers all score zero has no winner, and the selector's honest answer is
// "nothing" -- which is correct and is also indistinguishable, in the overlay and on screen, from a
// decider that is not running. An explicit idle says the character chose to stand there.
class IdleConsiderer final : public StockConsiderer {
public:
    explicit IdleConsiderer(const nlohmann::json* settings = nullptr);
    [[nodiscard]] std::string_view kind() const override { return "idle"; }
    void registerParameters(params::ParameterSet& params, const std::string& prefix) override;
    void collectParameterPaths(std::vector<std::string>& out) const override;
    void consider(const DecisionContext& ctx, std::vector<Option>& out) const override;

private:
    // What an idling body plays. An activity name, never a clip (R4).
    std::string activity_;
    double duration_ = 0.0; // 0 = until something else wins
    mutable std::vector<ActionDesc> actions_;
};

// Stay where you were put, and go back when you are not there.
//
// This is the guard's baseline and it is the reason the guard needs no C++. `post` names a node or
// an entity to stand at; empty means `EntityState::anchor`, which is where the scene placed this
// body. The score is flat while the body is within `tolerance` of the post and rises with the
// distance beyond it, so a guard that has wandered off wants its post back more than one standing
// on it -- which is what makes the return happen without a second option to express it.
//
// R1: the post is resolved through `EntityWorld::pointOfInterest`, which reports simulation
// positions, and the body is compared against `ctx.state->position()`.
class HoldPostConsiderer final : public StockConsiderer {
public:
    explicit HoldPostConsiderer(const nlohmann::json* settings = nullptr);
    [[nodiscard]] std::string_view kind() const override { return "holdPost"; }
    void registerParameters(params::ParameterSet& params, const std::string& prefix) override;
    void collectParameterPaths(std::vector<std::string>& out) const override;
    void consider(const DecisionContext& ctx, std::vector<Option>& out) const override;

    // Where this considerer thinks the post is, for a test and for the overlay. False when `post`
    // names something the world does not have, which is a fact worth seeing rather than a silent
    // fall back to the anchor.
    [[nodiscard]] bool postAt(const DecisionContext& ctx, glm::vec3& out) const;

private:
    std::string post_;      // "" = the body's own anchor
    std::string activity_;  // what it plays while standing the post
    float tolerance_ = 1.5f;
    // How much a metre away from the post adds to the score. The knob that decides whether a guard
    // strolls back or runs back, and whether it can be pulled away at all once it has been.
    float pullDefault_ = 0.08f;
    float faceYaw_ = 0.0f;
    bool hasFacing_ = false;
    params::Parameter<float>* pull_ = nullptr;
    std::string pullPath_;
    mutable std::vector<ActionDesc> actions_;
};

// Go and look at the most salient thing this body has noticed.
//
// **The first consumer of a percept in this engine** -- ADR-290 §7 left that seam with nothing on
// the far side of it. It scores `ctx.percepts`, never `interestPoints()`: the whole point is that a
// character reacts to what it knows rather than to what exists.
//
// One option, not one per percept. A considerer that appended an option per percept would put the
// selector's margin between two percepts of the same thing rather than between two *courses of
// action*, and the guard would dither between two mushrooms instead of between guarding and
// investigating. The winner among percepts is chosen here -- on salience times taste times
// staleness, ties broken on `(kind, source)` so the answer cannot depend on the order the sense
// stage wrote them -- and one option comes out.
class InvestigateConsiderer final : public StockConsiderer {
public:
    explicit InvestigateConsiderer(const nlohmann::json* settings = nullptr);
    [[nodiscard]] std::string_view kind() const override { return "investigate"; }
    void registerParameters(params::ParameterSet& params, const std::string& prefix) override;
    void collectParameterPaths(std::vector<std::string>& out) const override;
    void consider(const DecisionContext& ctx, std::vector<Option>& out) const override;

    // The percept this considerer would act on, and its score. `false` when nothing qualifies --
    // which a test wants to be able to see without reading the option list back.
    [[nodiscard]] bool best(const DecisionContext& ctx, Percept& out, float& score) const;

private:
    [[nodiscard]] float scoreOf(const DecisionContext& ctx, const Percept& p) const;

    GoalTaste taste_{};       // only `weight` and the ranges are read
    std::string activity_;    // what it plays once it arrives
    float approach_ = 3.0f;   // how close it goes before it stops
    double dwell_ = 4.0;      // seconds it attends to the thing once there
    // Seconds after which a percept this old is worth nothing. A memory fades on the same curve it
    // is remembered on, which is why this and `PerceptMemory::Settings::seconds` are different
    // knobs: one is how long a body keeps the fact, the other is how much it trusts it.
    float staleSeconds_ = 3.0f;
    bool kinds_[5] = {true, true, true, true, true};
    mutable std::vector<ActionDesc> actions_;
};

// The extracted goal model, as a considerer: `Explore`'s taste over the interest registry, or over
// the percepts, appended as one option per candidate.
//
// One option per candidate here, unlike `investigate`, and for the opposite reason: these *are* the
// courses of action. An explorer choosing between a shoreline and a ridge is choosing between two
// errands, and the overlay's whole value is printing the ridge's score beside the shoreline's.
class InterestConsiderer final : public StockConsiderer {
public:
    // Which list this character scores. The difference between them is ADR-270's whole finding, and
    // it is a knob rather than a constant so that the before-arm and the after-arm are the same
    // code path with one word changed (the lab's case 6 against case 14).
    enum class Source : std::uint8_t {
        Omniscient, // `EntityWorld::interestPoints()`: everything there is
        Perceived,  // `DecisionContext::percepts`: what this body noticed
    };

    explicit InterestConsiderer(const nlohmann::json* settings = nullptr);
    [[nodiscard]] std::string_view kind() const override { return "interest"; }
    void registerParameters(params::ParameterSet& params, const std::string& prefix) override;
    void collectParameterPaths(std::vector<std::string>& out) const override;
    void consider(const DecisionContext& ctx, std::vector<Option>& out) const override;

    // The goal model on its own, without the `Option` wrapping. This is the entry point `Explore`
    // calls (ADR-333 §3): it keeps its own weighted roll over these candidates, because that roll
    // draws from `Entity::rng_` and moving it would have re-cast every later choice the stream
    // makes -- which is exactly the drift the byte-identical control exists to catch. Wrapping 140
    // candidates into `ActionDesc`s every selection would also be work done for nothing there.
    [[nodiscard]] std::size_t candidates(const DecisionContext& ctx, const GoalTaste& taste,
                                         std::vector<GoalCandidate>& out) const;

    [[nodiscard]] Source source() const { return source_; }
    void setSource(Source source) { source_ = source; }
    [[nodiscard]] const GoalTaste& taste() const { return taste_; }
    void setTaste(const GoalTaste& taste) { taste_ = taste; }

private:
    GoalTaste taste_{};
    Source source_ = Source::Perceived;
    std::string activity_;
    float approach_ = 0.0f;
    double dwell_ = 0.0;
    mutable std::vector<ActionDesc> actions_;
    mutable std::vector<GoalCandidate> scratch_;
    mutable std::vector<std::string> names_;
};

// Price the *way*, not the place (ADR-336).
//
// The other four score a destination. `holdPost` and `investigate` score where a thing is;
// `interest` scores taste times nearness, and nearness is a straight line -- so a character with a
// river between it and a glow patch scores it exactly as it scores one on the same bank, and the
// choice a viewer is waiting to see, between wading and walking round, is not a choice any of them
// can express. That is what this one is for and it is the whole of what it is for.
//
// **The two requests.** For each destination it asks `Navigator::requestPath` twice with the same
// `PathRequest` and two different `NavPathCost::wadePenalty`:
//
//   * `fordPenalty` (0 by default) -- water is free, so the answer is the shortest way there,
//     through the river if the river is in the way. Call it the **ford**.
//   * `detourPenalty` (40 by default) -- water is ruinous, so the answer is the driest way there,
//     round the end of the river if there is a way round. Call it the **detour**.
//
// Neither penalty is this character's opinion; they are the two probes that *find* the two ways.
// The opinion is `wadePenalty`, and both ways are then scored with it: `length + wadePenalty x the
// metres of the route that are in water`. Measured on the lab's river fixture: a ford of 54.15 m
// with 11.26 weighted wet metres against a detour of 84.56 m with 1.61. At `wadePenalty` 0.4 the
// ford costs 58.65 and wins; at 12.0 it costs 189.29 and loses to the detour's 103.83. **One knob, two options, and the crossover
// is arithmetic rather than a rule.**
//
// Which is also why `wadePenalty` is a registered parameter and the two probes are not (ADR-225):
// the taste is the thing an author tunes and an overlay drives, and the probes are how the
// question is asked.
//
// **Two options, not one, and only when there are two.** When the two requests come back as the
// same way -- which is every destination on this bank, and every destination at all in a world
// with no water -- one option is appended and it is named for the destination. A second option
// identical to the first would put the selector's margin between a route and itself, and would
// print two lines in the overlay that say the same thing.
//
// **The route is the option.** The winner's actions are a `Move` per waypoint of *that* route,
// because `ActionKind::Move` plans its own way to a point and would plan it at the default price:
// an option called "go round" whose single action is "walk to the far bank" is a body that wades
// anyway, and the overlay would say it chose the detour while the film showed the ford.
//
// **Which position (R1).** `ctx.state->position()` for the start, `EntityWorld::pointOfInterest`
// for a named destination, `Percept::position` for a noticed one -- simulation positions, all
// three, because a route is navigation's and navigation reasons in simulation space.
//
// **Determinism.** `Navigator::requestPath` is a pure function of the world and the request, A*
// breaks its ties on cell index, and nothing here draws from a stream or reads a clock (D1, D2).
class RouteConsiderer final : public StockConsiderer {
public:
    // One way to one place, and what it is made of. The overlay prints `score`; a test that only
    // saw the score could not tell a route that got longer from one that got wetter, and the two
    // are the two halves of the thing this class exists to weigh.
    struct Priced {
        std::string_view destination; // the place's name; empty for a derived point
        glm::vec3 at{0.0f};           // where the place is
        bool detour = false;      // the high-penalty answer, rather than the low-penalty one
        float length = 0.0f;      // metres of route
        // Metres of the route that are in water, each weighted by that water's depth over this
        // walker's wade band -- the same 0..1 `NavCell::wade` holds, which is the number `NavGrid`
        // itself prices a ford with. Zero in any world whose walker does not wade.
        float wadeMetres = 0.0f;
        float cost = 0.0f;        // length + wadePenalty * wadeMetres
        float score = 0.0f;       // weight * destination weight / (1 + cost / falloff)
        PathStatus status = PathStatus::NoGraph;
        // Where the route actually ends -- `PathResult::goal`, which is not always the place: a
        // destination inside a solid or under water is answered with the nearest standable point,
        // and the last `Move` is aimed at that rather than at the thing.
        glm::vec2 goal{0.0f};
        // The route itself, by value. Two A* searches cost 50 us and copying a dozen waypoints
        // costs nothing measurable, so this is owned rather than a span into the considerer --
        // which also means a caller may hold a priced route across another `price` call, and the
        // lab's case 9 does exactly that when it compares the same two ways at two tastes.
        std::vector<glm::vec2> waypoints;
    };

    explicit RouteConsiderer(const nlohmann::json* settings = nullptr);
    [[nodiscard]] std::string_view kind() const override { return "route"; }
    void registerParameters(params::ParameterSet& params, const std::string& prefix) override;
    void collectParameterPaths(std::vector<std::string>& out) const override;
    void consider(const DecisionContext& ctx, std::vector<Option>& out) const override;

    // Every way to every destination, priced, without the `Option` wrapping. What the lab's case 9
    // asserts on and what a considerer's own test can read without going through a selector.
    // Appends; does not clear, the way `scoreGoals` does not.
    std::size_t price(const DecisionContext& ctx, std::vector<Priced>& out) const;

    // The character's own price on a wet metre. Reads the registered parameter when there is one,
    // so a test and an overlay drive the same number.
    [[nodiscard]] float wadePenalty() const;

private:
    // A place this character will price a way to, resolved for this tick.
    struct Destination {
        std::string_view name;
        glm::vec3 position{0.0f};
        float weight = 1.0f;  // the goal model's, or 1 for an authored destination
    };
    std::size_t destinationsFor(const DecisionContext& ctx, std::vector<Destination>& out) const;

    // An authored destination: a point, or the name of something `pointOfInterest` can find.
    struct Authored {
        std::string name;
        glm::vec3 point{0.0f};
        bool hasPoint = false;
    };
    std::vector<Authored> authored_;

    GoalTaste taste_{};
    InterestConsiderer::Source source_ = InterestConsiderer::Source::Perceived;
    // How many destinations may be priced in one tick. A hard cap and not a hint: each one costs
    // two A* searches (24.969 us each, ADR-268), so an explorer that priced all twenty of its
    // percepts would spend a millisecond of every decision tick on routes it was never going to
    // take. The goal model ranks them and this takes the top few.
    std::uint16_t maxDestinations_ = 4;
    float fordPenalty_ = 0.0f;
    float detourPenalty_ = 40.0f;
    float wadePenaltyDefault_ = 1.2f; // `NavPathCost::wadePenalty`'s own default (ADR-195)
    // Metres of cost at which an option is worth half what a free one is. The same shape
    // `goalWeight`'s distance damping has, in the same units, so the two scores compose.
    float falloff_ = 40.0f;
    float goalTolerance_ = 2.0f;
    // How near a *middle* waypoint counts as reached. Wider than an arrival and narrower than a
    // cell: too tight and a body oscillates on a corner the steering cannot hold, too loose and it
    // cuts the corner -- and cutting the corner of a detour is walking into the river the detour
    // was chosen to avoid.
    float legTolerance_ = 1.5f;
    float approach_ = 0.0f;
    double dwell_ = 0.0;
    std::string activity_;
    params::Parameter<float>* wadePenalty_ = nullptr;
    std::string wadePenaltyPath_;

    mutable std::vector<ActionDesc> actions_;
    mutable std::vector<std::string> names_;
    mutable std::vector<Destination> places_;
    mutable std::vector<GoalCandidate> scratch_;
    mutable std::vector<Priced> priced_;
};

[[nodiscard]] std::vector<std::string_view> considererKinds();
// Null when `kind` is not a considerer; the caller reports it. `settings` supplies the authored
// knobs and may be null.
[[nodiscard]] std::unique_ptr<StockConsiderer> makeConsiderer(std::string_view kind,
                                                              const nlohmann::json* settings);

// ---- bounded memory ----------------------------------------------------------------------------

// What a character still knows about something it can no longer see.
//
// ADR-290 §7 records that percepts do not accumulate: each sense tick rebuilds the working set from
// scratch, so a thing that leaves the range is gone rather than remembered, and names that as the
// first thing to revisit if a decider needs a character to keep looking for something it lost sight
// of. **This is that decider and it needed it** -- measured, not assumed: a guard walking towards a
// percept loses it the moment the thing steps behind the range boundary, `investigate` falls to
// zero, `holdPost` wins, and the body turns round mid-stride. The dwell delays that by its own
// length and does not fix it, because the incumbent's own score is what collapsed.
//
// So the decider carries a bounded fade, and it is deliberately small:
//
//   * **Bounded in count and in time.** `capacity` entries, `seconds` of life. A memory that grew
//     with the world would be a memory a replay could not reconstruct in a fixed number of steps.
//   * **Reconstructed, never persisted** (D4). It is rebuilt by re-simulating, `reset()` clears it,
//     and nothing writes it to a save file.
//   * **Honest about being a memory.** A remembered percept keeps the `seenAt` it was noticed at,
//     so `time - seenAt` is how stale it is and a considerer can score it down rather than
//     believing it. Its `tested` and `visibility` are kept as they were, because a memory of having
//     seen something clearly is still a memory.
//   * **Never promoted over a live percept.** When the thing is perceived again this tick, the live
//     percept replaces the remembered one rather than sitting beside it.
class PerceptMemory {
public:
    struct Settings {
        float seconds = 0.0f;  // 0 -- the default -- is ADR-290's behaviour exactly: no memory
        std::uint16_t capacity = 8;
    };

    void setSettings(const Settings& settings) { settings_ = settings; }
    [[nodiscard]] const Settings& settings() const { return settings_; }

    // Folds this tick's working set in and returns the merged view: every live percept, then every
    // remembered one that is still within `seconds` and was not superseded. Stable in (kind,
    // source) order within each half, so the merge cannot depend on the order the sense stage
    // happened to write.
    [[nodiscard]] std::span<const Percept> merge(std::span<const Percept> live, double time);

    void reset();
    [[nodiscard]] std::size_t remembered() const { return remembered_.size(); }

private:
    Settings settings_{};
    std::vector<Percept> remembered_;
    std::vector<Percept> merged_;
};

} // namespace avgen::entity
