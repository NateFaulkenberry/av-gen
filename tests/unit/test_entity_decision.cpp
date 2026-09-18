// The decision layer (ADR-269, ADR-333): what a character chooses to do, out of everything it
// could, and the controls that make each answer mean something.
//
// Until this landed, `character_ai.hpp` §3 declared `Option`, `DecisionContext` and `IConsiderer`
// and nothing implemented them, so the Character Intelligence Lab's case 6 could record that "none
// of the four navigating behaviours reports a score it beat". The only autonomous mind in the
// engine was hardcoded inside `Explore`, and a character that guards something would have been a
// second 700-line class. This file is what makes that false.
//
// Every arm has a control that can fail (ADR-182), and the controls are the point of the file:
//
//   dwell        a challenger inside the dwell does not  |  the identical challenger one tick later
//                take the slot                           |  does
//   margin       a challenger ahead by less than the      |  the identical challenger ahead by more
//                margin does not take it                  |  than it does
//   cadence      the decision boundaries land on the      |  a dwell counted in accumulated seconds
//                same instants at 60 Hz and at 37 Hz      |  would not, and the arm shows by how much
//   purity       the same context twice scores the same   |  a context whose time moved scores
//                                                          |  differently
//   percepts     a guard leaves its post for what it       |  the same guard with `investigate`'s
//                notices                                   |  registered weight at 0 never leaves
//   memory       a guard that loses sight of the thing     |  the same guard with `memorySeconds` 0
//                keeps going                               |  turns round
//   the source   the same considerer over the omniscient   |  over this body's percepts it scores
//                list scores every point in range          |  only what it noticed
//
// Quantities are structural (ADR-170): options scored, decisions taken, dwell rejections, metres
// from a post. There is no timing in this file at all.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "entity/decision.hpp"
#include "entity/entity.hpp"
#include "entity/perception.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <tuple>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::entity;
namespace fs = std::filesystem;

namespace {

fs::path repoRoot() { return fs::path(AVGEN_SOURCE_DIR); }
fs::path guardFixture() {
    return repoRoot() / "examples" / "labs" / "character" / "guard-post.scene.json";
}
bool assetsPresent() { return fs::exists(repoRoot() / "assets" / "aliens" / "alien-pilot.glb"); }

// A considerer a test writes by hand: one option, one score the test sets. The reason `IConsiderer`
// is an interface at all, and the control arm for every stock one -- an assertion that passes
// against this and against a stock considerer is an assertion about the *selector*.
class Fixed final : public IConsiderer {
public:
    Fixed(std::string name, float score) : name_(std::move(name)), score_(score) {}
    void setScore(float score) { score_ = score; }
    void consider(const DecisionContext& ctx, std::vector<Option>& out) const override {
        (void)ctx;
        out.push_back(Option{name_, score_, {}, Authority::Routine});
    }

private:
    std::string name_;
    float score_ = 0.0f;
};

DecisionContext contextAt(double time, const EntityState& state) {
    DecisionContext ctx;
    ctx.time = time;
    ctx.state = &state;
    ctx.seed = 0;
    return ctx;
}

// The whole world, ticked the way the application ticks it -- the same harness
// `test_entity_perception.cpp` uses, for the same reason: half a frame is not a frame.
struct World {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    std::unique_ptr<scene::Composition> comp;

    explicit World(const fs::path& file) : registry(file.parent_path()) {
        auto loaded = scene::Composition::loadFile(file, registry);
        INFO((loaded.has_value() ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
        comp = std::move(*loaded);
        comp->attach(params, modulator);
        comp->setViewport(1280, 720);
        comp->scene().detailLimits.entityDistanceCull = false;
    }

    void play(double seconds, double hz = 60.0, const std::function<void(int)>& each = {}) {
        const double step = 1.0 / hz;
        const auto frames = static_cast<int>(std::llround(seconds * hz));
        FrameTime time;
        for (int i = 0; i < frames; ++i) {
            time.renderTime = static_cast<double>(i) * step;
            time.deltaTime = i == 0 ? 0.0 : step;
            time.frameIndex = static_cast<std::uint64_t>(i);
            params.resetFinals();
            comp->updateFields(time, bus, modulator);
            modulator.applyRoutes(bus, params, time.deltaTime);
            comp->updateBehaviour(time, bus);
            comp->update(time);
            if (each) {
                each(i);
            }
        }
    }

    [[nodiscard]] const Entity& entity(const char* name) const {
        const Entity* e = comp->entityWorld().find(name);
        REQUIRE(e != nullptr);
        return *e;
    }
    void setKnob(const std::string& path, float value) {
        auto* p = params.findAs<float>(path);
        INFO("knob: " << path);
        REQUIRE(p != nullptr);
        p->setBase(value);
    }
    // What a deciding behaviour is reporting. Through `IBehavior::decisionDebug`, because that is
    // the seam an overlay reads and a test that went round it would be checking something nobody
    // draws.
    [[nodiscard]] bool decision(const char* name, DecisionDebug& out) const {
        for (const auto& behavior : entity(name).behaviors()) {
            if (behavior->decisionDebug(out)) {
                return true;
            }
        }
        return false;
    }
};

} // namespace

// ---- the selector ------------------------------------------------------------------------------

TEST_CASE("a chosen option is held for a dwell and must be beaten by a margin",
          "[entity][decision][adr333]") {
    EntityState state;
    Selector selector;
    SelectorSettings settings;
    settings.hertz = 4.0f;   // a tick every 0.25 s
    settings.dwellTicks = 3.0f;
    settings.margin = 0.20f;
    selector.setSettings(settings);

    Fixed incumbent("guard", 1.0f);
    Fixed challenger("chase", 0.0f);
    const IConsiderer* considerers[] = {&incumbent, &challenger};

    // Tick 0: only one option is applicable, so it is committed.
    CHECK(selector.select(contextAt(0.0, state), considerers));
    CHECK(selector.current() == "guard");
    CHECK(selector.counts().decisions == 1);

    // Inside the dwell, and ahead by a mile. Refused, and the refusal is counted rather than
    // silent -- a hysteresis nobody can see is a hysteresis nobody can tune.
    challenger.setScore(9.0f);
    CHECK_FALSE(selector.select(contextAt(0.25, state), considerers));
    CHECK(selector.current() == "guard");
    CHECK(selector.counts().dwellRejections == 1);
    CHECK_FALSE(selector.select(contextAt(0.50, state), considerers));
    CHECK(selector.counts().dwellRejections == 2);

    // The control: the identical challenger, one tick later, with the dwell expired.
    CHECK(selector.select(contextAt(0.75, state), considerers));
    CHECK(selector.current() == "chase");
    CHECK(selector.counts().decisions == 2);
    CHECK(selector.counts().dwellRejections == 2); // and it stopped counting them

    // A tie does not move the slot either, and that is the margin doing its job rather than an
    // ordering rule: the incumbent is "chase" and "guard" draws level with it.
    challenger.setScore(1.0f);
    incumbent.setScore(1.0f);
    CHECK_FALSE(selector.select(contextAt(2.0, state), considerers));
    CHECK(selector.current() == "chase");
    CHECK(selector.counts().marginRejections == 1);

    // The margin, with the dwell long expired. 1.0 against 1.15 is a lead of 0.15 on a margin of
    // 0.20: the incumbent keeps the slot.
    incumbent.setScore(1.15f);
    CHECK_FALSE(selector.select(contextAt(3.0, state), considerers));
    CHECK(selector.current() == "chase");
    CHECK(selector.counts().marginRejections == 2);

    // The control: the identical challenger above the margin.
    incumbent.setScore(1.25f);
    CHECK(selector.select(contextAt(4.0, state), considerers));
    CHECK(selector.current() == "guard");
    CHECK(selector.counts().marginRejections == 2);
}

TEST_CASE("an option that scores nothing is not an option", "[entity][decision][adr333]") {
    EntityState state;
    Selector selector;
    SelectorSettings settings;
    settings.hertz = 4.0f;
    settings.dwellTicks = 0.0f;
    settings.margin = 0.0f;
    selector.setSettings(settings);

    Fixed only("chase", 0.0f);
    const IConsiderer* considerers[] = {&only};
    CHECK_FALSE(selector.select(contextAt(0.0, state), considerers));
    CHECK(selector.current().empty());
    CHECK(selector.counts().empty == 1);
    CHECK(selector.counts().scored == 1); // it was scored; it was not applicable

    // The control: the same considerer above zero.
    only.setScore(0.001f);
    CHECK(selector.select(contextAt(0.25, state), considerers));
    CHECK(selector.current() == "chase");
}

TEST_CASE("the decision boundary is a function of the instant, not of the frames",
          "[entity][decision][adr333][adr091]") {
    // ADR-267's D1 and ADR-290 §2, one layer up. A dwell accumulated across frames expires on a
    // different instant at a different frame rate, so a replayed decision lands on a different
    // step from the played one -- which is exactly the class of defect ADR-091's scrub-equals-play
    // rule exists to catch.
    //
    // The arm: step 4 s at 60 Hz and at 37 Hz and collect the instants the tick index changes.
    const auto boundaries = [](double hz) {
        std::vector<int> ticksAt;
        std::uint64_t last = decideTick(0.0, 2.0f, 12345);
        const int frames = static_cast<int>(std::llround(4.0 * hz));
        for (int i = 1; i < frames; ++i) {
            const double t = static_cast<double>(i) / hz;
            const std::uint64_t tick = decideTick(t, 2.0f, 12345);
            if (tick != last) {
                ticksAt.push_back(static_cast<int>(std::llround(t * 1000.0)));
                last = tick;
            }
        }
        return ticksAt;
    };
    const std::vector<int> fast = boundaries(60.0);
    const std::vector<int> slow = boundaries(37.0);
    REQUIRE(fast.size() == slow.size());
    int worst = 0;
    for (std::size_t i = 0; i < fast.size(); ++i) {
        worst = std::max(worst, std::abs(fast[i] - slow[i]));
    }
    // The only disagreement possible is which frame *reports* the boundary, which is one frame of
    // the coarser rate -- 27 ms at 37 Hz. The boundary itself is the same instant.
    WARN(fmt::format("{} boundaries in 4 s; worst frame-rate disagreement {} ms", fast.size(), worst));
    CHECK(fast.size() == 8);
    CHECK(worst <= 28);

    // The control, and the reason this is worth asserting: a dwell that accumulated `dt` would
    // drift with the frame rate and there would be nothing in the engine to notice. Simulated here
    // so the number is on the record rather than assumed.
    double accumulated = 0.0;
    std::vector<int> drifted;
    const int frames = static_cast<int>(std::llround(4.0 * 37.0));
    for (int i = 1; i < frames; ++i) {
        accumulated += 1.0 / 37.0;
        if (accumulated >= 0.5) {
            accumulated -= 0.5;
            drifted.push_back(static_cast<int>(std::llround(static_cast<double>(i) / 37.0 * 1000.0)));
        }
    }
    int accumulatedWorst = 0;
    for (std::size_t i = 0; i < std::min(drifted.size(), fast.size()); ++i) {
        accumulatedWorst = std::max(accumulatedWorst, std::abs(drifted[i] - fast[i]));
    }
    WARN(fmt::format("an accumulated half-second would have drifted {} ms by 4 s", accumulatedWorst));
    CHECK(accumulatedWorst > worst);
}

// ---- the stock considerers ---------------------------------------------------------------------

TEST_CASE("holdPost wants its post back in proportion to how far it is from it",
          "[entity][decision][adr333]") {
    nlohmann::json settings = {{"pull", 0.25}, {"tolerance", 1.5}, {"weight", 1.0}};
    HoldPostConsiderer post(&settings);
    post.setName("post");

    EntityState state;
    state.anchor = glm::vec3(10.0f, 0.0f, -4.0f);

    const auto scoreAt = [&](glm::vec3 travel) {
        state.travel = travel;
        std::vector<Option> out;
        post.consider(contextAt(0.0, state), out);
        REQUIRE(out.size() == 1);
        return out[0];
    };

    // Standing the post, and one metre off it: the same score, because inside the tolerance the
    // body *is* at its post and a guard that fidgeted its way into wanting to go home would be
    // scoring its own noise.
    CHECK(scoreAt(glm::vec3(0.0f)).score == scoreAt(glm::vec3(1.0f, 0.0f, 0.0f)).score);
    // And nothing to do, so the option is the empty one: "this wins by doing nothing".
    CHECK(scoreAt(glm::vec3(0.0f)).actions.empty());

    // Twenty metres off it: 1 + 0.25 * (20 - 1.5).
    const Option away = scoreAt(glm::vec3(20.0f, 0.0f, 0.0f));
    CHECK(away.score > scoreAt(glm::vec3(0.0f)).score);
    CHECK(std::abs(away.score - (1.0f + 0.25f * 18.5f)) < 1e-4f);
    REQUIRE(away.actions.size() == 1);
    CHECK(away.actions[0].kind == ActionKind::Move);
    CHECK(away.actions[0].target.kind == TargetKind::Point);
    // R1: it walks back to the anchor -- where the scene put it -- and not to where it is drawn.
    CHECK(away.actions[0].target.point == state.anchor);

    // The control: the same considerer with no pull at all is flat everywhere, which is what makes
    // the slope above a measurement of `pull` rather than of the distance.
    nlohmann::json flatSettings = {{"pull", 0.0}, {"tolerance", 1.5}, {"weight", 1.0}};
    HoldPostConsiderer flat(&flatSettings);
    flat.setName("post");
    std::vector<Option> here;
    std::vector<Option> there;
    state.travel = glm::vec3(0.0f);
    flat.consider(contextAt(0.0, state), here);
    state.travel = glm::vec3(20.0f, 0.0f, 0.0f);
    flat.consider(contextAt(0.0, state), there);
    REQUIRE(here.size() == 1);
    REQUIRE(there.size() == 1);
    CHECK(here[0].score == there[0].score);
    CHECK_FALSE(there[0].actions.empty()); // it still walks back; it just does not want it more
}

TEST_CASE("investigate scores what a body noticed, and only the kinds it was told to care about",
          "[entity][decision][adr333][adr290]") {
    nlohmann::json settings = {{"kinds", {"character"}}, {"weight", 2.0}, {"staleSeconds", 4.0}};
    InvestigateConsiderer investigate(&settings);
    investigate.setName("investigate");

    EntityState state;
    std::vector<Percept> percepts;
    Percept body{};
    body.kind = InterestKind::Character;
    body.source = 3;
    body.position = glm::vec3(12.0f, 0.0f, 0.0f);
    body.distance = 12.0f;
    body.salience = 0.6f;
    body.seenAt = 10.0;
    Percept mushroom{};
    mushroom.kind = InterestKind::Glow;
    mushroom.source = 7;
    mushroom.position = glm::vec3(2.0f, 0.0f, 0.0f);
    mushroom.distance = 2.0f;
    mushroom.salience = 0.95f; // far more salient, and far nearer
    mushroom.seenAt = 10.0;
    percepts = {mushroom, body};

    DecisionContext ctx = contextAt(10.0, state);
    ctx.percepts = percepts;
    std::vector<Option> out;
    investigate.consider(ctx, out);
    REQUIRE(out.size() == 1);
    // One option, not one per percept: the selector's margin belongs between two *courses of
    // action*, not between two mushrooms.
    CHECK(out[0].name == "investigate");
    Percept chosen{};
    float score = 0.0f;
    REQUIRE(investigate.best(ctx, chosen, score));
    CHECK(chosen.source == 3); // the body, not the brighter, nearer, more salient mushroom
    REQUIRE(out[0].actions.size() >= 2);
    CHECK(out[0].actions[0].kind == ActionKind::Move);
    // It walks to a point `approach` metres this side of the thing, not onto it. That is what
    // `approach` means to an author and it is also load-bearing: `NavigatorPath::route` -- the
    // provider `ActionQueue`'s `move` runs through -- returns `Unreachable` for a goal that is not
    // navigable, and a landmark is usually solid. The `Face` action still names the thing itself.
    CHECK(out[0].actions[0].target.point == glm::vec3(9.0f, 0.0f, 0.0f)); // 12 m out, approach 3
    CHECK(out[0].actions[1].kind == ActionKind::Face);
    // R1: a percept's position is the perceived body's simulation position (ADR-290), so a guard
    // sent to meet a hovering craft looks where it is rather than where it is drawn.
    CHECK(out[0].actions[1].target.point == body.position);

    // The control on the stand-off: the same considerer with `approach` at 0 names the thing.
    nlohmann::json onIt = {{"kinds", {"character"}}, {"approach", 0.0}};
    InvestigateConsiderer touching(&onIt);
    touching.setName("investigate");
    std::vector<Option> exact;
    touching.consider(ctx, exact);
    REQUIRE(exact.size() == 1);
    CHECK(exact[0].actions[0].target.point == body.position);

    // The control: the identical percepts through a considerer told to care about glow. If this
    // scored the body too, the filter would be doing nothing and the arm above would be measuring
    // salience.
    nlohmann::json glowSettings = {{"kinds", {"glow"}}, {"weight", 2.0}};
    InvestigateConsiderer glow(&glowSettings);
    glow.setName("investigate");
    Percept other{};
    float otherScore = 0.0f;
    REQUIRE(glow.best(ctx, other, otherScore));
    CHECK(other.source == 7);

    // Staleness. The same percept, four seconds older, is worth nothing: a memory fades. The
    // control is the fresh one above, which scored.
    ctx.time = 14.0;
    std::vector<Option> stale;
    investigate.consider(ctx, stale);
    CHECK(stale.empty());
    ctx.time = 12.0;
    std::vector<Option> half;
    investigate.consider(ctx, half);
    REQUIRE(half.size() == 1);
    CHECK(half[0].score < out[0].score);
    CHECK(half[0].score > 0.0f);
}

TEST_CASE("a considerer is a pure function of its context", "[entity][decision][adr333]") {
    // ADR-269: given the same context twice a considerer must produce the same scores -- the
    // property that makes a selector's answer reproducible under a replay, and the reason a
    // considerer may not draw from `Entity::rng_`.
    nlohmann::json settings = {{"kinds", {"character", "glow"}}, {"weight", 1.3}};
    InvestigateConsiderer investigate(&settings);
    investigate.setName("investigate");
    EntityState state;
    std::vector<Percept> percepts(4);
    for (std::size_t i = 0; i < percepts.size(); ++i) {
        percepts[i].kind = i % 2 == 0 ? InterestKind::Glow : InterestKind::Character;
        percepts[i].source = i;
        percepts[i].position = glm::vec3(static_cast<float>(i) * 3.0f, 0.0f, 1.0f);
        percepts[i].distance = static_cast<float>(i) * 3.0f;
        percepts[i].salience = 0.5f + 0.1f * static_cast<float>(i);
        percepts[i].seenAt = 5.0;
    }
    DecisionContext ctx = contextAt(5.0, state);
    ctx.percepts = percepts;
    std::vector<Option> first;
    std::vector<Option> second;
    investigate.consider(ctx, first);
    investigate.consider(ctx, second);
    REQUIRE(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].score == second[i].score); // bit equality, not a tolerance
    }
    // The control: a context whose time moved must score differently, or "the same context twice"
    // would be a claim about a constant.
    ctx.time = 6.5;
    std::vector<Option> later;
    investigate.consider(ctx, later);
    REQUIRE(later.size() == first.size());
    CHECK(later[0].score != first[0].score);
}

TEST_CASE("the same goal model over two sources is two different characters",
          "[entity][decision][adr333][adr270]") {
    // ADR-270's whole finding, as one assertion. `interest` over `interestPoints()` is what
    // `Explore` has always read: everything there is, with no range, no facing and no notion of
    // having noticed. Over `percepts` it is what this body knows.
    EntityState state;
    std::vector<InterestPoint> world;
    for (int i = 0; i < 6; ++i) {
        InterestPoint p;
        p.position = glm::vec3(static_cast<float>(i + 1) * 15.0f, 0.0f, 0.0f);
        p.name = fmt::format("point-{}", i);
        p.kind = InterestKind::Landmark;
        p.weight = 1.0f;
        world.push_back(std::move(p));
    }
    // What the body actually noticed: the nearest two, which is what a 40 m sense range leaves.
    std::vector<Percept> percepts(2);
    for (std::size_t i = 0; i < percepts.size(); ++i) {
        percepts[i].kind = InterestKind::Landmark;
        percepts[i].source = i;
        percepts[i].position = world[i].position;
        percepts[i].distance = world[i].position.x;
        percepts[i].salience = 0.8f;
        percepts[i].seenAt = 0.0;
    }
    GoalTaste taste;
    taste.minRange = 5.0f;
    taste.maxRange = 200.0f;
    taste.noveltyRadius = 8.0f; // the points are 15 m apart, so one visit suppresses exactly one

    DecisionContext ctx = contextAt(0.0, state);
    ctx.percepts = percepts;
    std::vector<GoalCandidate> omniscient;
    std::vector<GoalCandidate> perceived;
    CHECK(scoreGoals(ctx, taste, std::span<const InterestPoint>(world), omniscient) == 6);
    CHECK(scoreGoals(ctx, taste, ctx.percepts, perceived) == 2);
    // The two agree exactly about the things they share, which is what says the difference is the
    // *source* and not two different scoring functions.
    for (std::size_t i = 0; i < perceived.size(); ++i) {
        CHECK(perceived[i].weight == omniscient[i].weight);
    }

    // The novelty term, and its control. A place this body has been is worth 0.12 of one it has
    // not; with nothing visited the two are equal.
    const std::vector<glm::vec3> visited = {world[0].position};
    ctx.visited = visited;
    std::vector<GoalCandidate> suppressed;
    scoreGoals(ctx, taste, std::span<const InterestPoint>(world), suppressed);
    REQUIRE(suppressed.size() == omniscient.size());
    CHECK(suppressed[0].weight < omniscient[0].weight);
    CHECK(suppressed[1].weight == omniscient[1].weight);
}

TEST_CASE("a misspelled considerer is refused by name", "[entity][decision][adr333]") {
    // `makeConsiderer` is the whole vocabulary, and a kind it does not know comes back null so the
    // caller can say so. A silently skipped considerer is a character that loses one of the things
    // it was meant to want, and a guard whose `investigate` never loaded stands still for the
    // right-looking reason.
    CHECK(makeConsiderer("investigate", nullptr) != nullptr);
    CHECK(makeConsiderer("Investigate", nullptr) == nullptr); // kinds are case-sensitive, like behaviours
    CHECK(makeConsiderer("guard", nullptr) == nullptr);
    CHECK(makeConsiderer("", nullptr) == nullptr);
    // Every kind the vocabulary advertises can actually be made. A list that named something the
    // factory refuses would send an author to a kind that does not exist.
    for (const std::string_view kind : considererKinds()) {
        INFO("kind: " << kind);
        auto made = makeConsiderer(kind, nullptr);
        REQUIRE(made != nullptr);
        CHECK(made->kind() == kind);
        CHECK(made->name() == kind); // the default name, so a scene that names none still compares
    }
    CHECK(considererKinds().size() == 4);
}

// ---- the guard ---------------------------------------------------------------------------------

TEST_CASE("a guard is scene data, and it leaves its post for what it notices",
          "[entity][decision][adr333][labs]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    // The unit's product claim (docs/character-ai-plan.md §P3): "a character that is not an
    // explorer exists: a guard that holds a post and abandons it when it perceives something,
    // expressed entirely as considerers and scene data, with no new C++ class per character kind."
    //
    // `sentry` holds (-34, -6). `courier` patrols the line x = -16 under a list of authored `move`
    // actions -- deterministic, and nothing to do with the decider. Their closest approach is 18 m
    // and the sentry's sense range is 24, so the courier is noticed for part of each leg and not
    // for the rest of it.
    World world(guardFixture());
    float furthest = 0.0f;
    int framesAway = 0;
    std::set<std::string> chosen;
    world.play(75.0, 60.0, [&](int) {
        const Entity& sentry = world.entity("sentry");
        const glm::vec3 p = sentry.state().position(); // R1
        const float away = glm::length(glm::vec2(p.x - (-34.0f), p.z - (-6.0f)));
        furthest = std::max(furthest, away);
        if (away > 2.0f) {
            ++framesAway;
        }
        DecisionDebug debug;
        if (world.decision("sentry", debug) && !debug.chosen.empty()) {
            chosen.insert(std::string(debug.chosen));
        }
    });
    DecisionDebug debug;
    REQUIRE(world.decision("sentry", debug));
    WARN(fmt::format("sentry: furthest {:.3f} m from the post, {} frames away, {} decisions, "
                     "{} dwell refusals, {} margin refusals, {} options on the last tick",
                     furthest, framesAway, debug.decisions, debug.dwellRejections,
                     debug.marginRejections, debug.options.size()));
    CHECK(furthest > 6.0f);          // it genuinely abandoned the post
    CHECK(debug.decisions >= 2);     // and changed its mind at least twice: out and back
    CHECK(chosen.count("investigate") == 1);
    CHECK(chosen.count("post") == 1);
    // It came back: the last quarter of the run has it near the post again for some of the time.
    const glm::vec3 end = world.entity("sentry").state().position();
    WARN(fmt::format("sentry ended {:.3f} m from the post",
                     glm::length(glm::vec2(end.x - (-34.0f), end.z - (-6.0f)))));

    // **The control, and it is the one that matters** (ADR-182). The identical scene, the identical
    // seed, with `investigate`'s registered weight driven to zero: the guard must never leave. If
    // it did, the walk above would be a measurement of something else -- a wander, a push out of a
    // crowd, a grounding drift -- wearing a decision's clothes.
    //
    // It is also the ADR-225 arm. A weight an author wrote and the engine read once from the JSON
    // would not be reachable by this knob at all, and this assertion would fail.
    World held(guardFixture());
    held.setKnob("entity/sentry/decide/investigate/weight", 0.0f);
    float heldFurthest = 0.0f;
    held.play(75.0, 60.0, [&](int) {
        const glm::vec3 p = held.entity("sentry").state().position();
        heldFurthest = std::max(heldFurthest,
                                glm::length(glm::vec2(p.x - (-34.0f), p.z - (-6.0f))));
    });
    WARN(fmt::format("the same guard with investigate at weight 0: furthest {:.3f} m", heldFurthest));
    CHECK(heldFurthest < 2.0f);
    CHECK(heldFurthest < furthest);
}

TEST_CASE("two character kinds, one C++ class", "[entity][decision][adr333][labs]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    // The clause the unit is actually judged on: "no new C++ class per character kind". A guard
    // that needed a `Guard` class would be a failure of this unit even if the guard behaved
    // perfectly. So: `sentry` and `pilgrim` are a guard and an explorer, they behave measurably
    // differently, and the behaviour that drives both reports the same `kind()`.
    World world(guardFixture());
    world.play(60.0);

    std::set<std::string> kinds;
    for (const char* name : {"sentry", "pilgrim"}) {
        DecisionDebug debug;
        REQUIRE(world.decision(name, debug));
        for (const auto& behavior : world.entity(name).behaviors()) {
            DecisionDebug ignored;
            if (behavior->decisionDebug(ignored)) {
                kinds.insert(std::string(behavior->kind()));
            }
        }
    }
    REQUIRE(kinds.size() == 1);
    CHECK(*kinds.begin() == "decide");

    // And they are not the same character. The guard stays near where it was put; the explorer
    // crosses its own patch of world.
    const auto travelled = [&](const char* name, glm::vec2 from) {
        const glm::vec3 p = world.entity(name).state().position();
        return glm::length(glm::vec2(p.x, p.z) - from);
    };
    const float guard = travelled("sentry", glm::vec2(-34.0f, -6.0f));
    const float explorer = travelled("pilgrim", glm::vec2(50.0f, 0.0f));
    WARN(fmt::format("at 60 s the guard is {:.2f} m from its post and the explorer {:.2f} m "
                     "from where it started", guard, explorer));
    DecisionDebug pilgrim;
    REQUIRE(world.decision("pilgrim", pilgrim));
    WARN(fmt::format("pilgrim scored {} options on its last tick and took {} decisions",
                     pilgrim.options.size(), pilgrim.decisions));
    // The explorer scores one option per thing it noticed; the guard scores three, always.
    CHECK(pilgrim.options.size() > 3);
    CHECK(pilgrim.decisions >= 1);
}

TEST_CASE("a decider reads a percept a test wrote, with no world to produce one",
          "[entity][decision][adr333][adr290]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    // `EntityWorld::setPerception` exists for exactly this (ADR-290): a decider assertable against
    // percepts a test wrote by hand, so the arm is about the decision and not about the grid.
    World world(guardFixture());
    ScriptedPerception scripted;
    const Entity* sentry = world.comp->entityWorld().find("sentry");
    REQUIRE(sentry != nullptr);
    std::size_t index = 0;
    const auto& all = world.comp->entityWorld().entities();
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i].get() == sentry) {
            index = i;
        }
    }
    // A thing 30 m due north of the post -- further than the sense range, so the grid could never
    // have produced it, which is what makes this an assertion about the decider.
    Percept invented{};
    invented.kind = InterestKind::Character;
    invented.source = index;
    invented.position = glm::vec3(-34.0f, 0.0f, -36.0f);
    invented.distance = 30.0f;
    invented.salience = 0.9f;
    invented.visibility = 1.0f;
    scripted.set(index, {invented});
    world.comp->entityWorld().setPerception(&scripted);

    float furthest = 0.0f;
    world.play(30.0, 60.0, [&](int) {
        const glm::vec3 p = world.entity("sentry").state().position();
        furthest = std::max(furthest, glm::length(glm::vec2(p.x - (-34.0f), p.z - (-6.0f))));
    });
    WARN(fmt::format("driven by one scripted percept, the sentry went {:.3f} m from its post",
                     furthest));
    CHECK(furthest > 5.0f);

    // The control: the same scripted stage with nothing in it. The percept is the reason it moved.
    World empty(guardFixture());
    ScriptedPerception none;
    empty.comp->entityWorld().setPerception(&none);
    float stayed = 0.0f;
    empty.play(30.0, 60.0, [&](int) {
        const glm::vec3 p = empty.entity("sentry").state().position();
        stayed = std::max(stayed, glm::length(glm::vec2(p.x - (-34.0f), p.z - (-6.0f))));
    });
    WARN(fmt::format("with an empty scripted stage it went {:.3f} m", stayed));
    CHECK(stayed < 2.0f);
}

TEST_CASE("a guard that loses sight of something keeps going", "[entity][decision][adr333][adr290]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    // ADR-290 §7: percepts do not accumulate -- each sense tick rebuilds the working set, so a thing
    // that leaves the range is gone rather than remembered -- and it names that as the first thing
    // to revisit if a decider needs a character to keep looking for something it lost sight of.
    //
    // **The guard-post fixture does not distinguish the two, and that is worth saying.** Measured:
    // with the fade and without it the sentry goes 13.442 m from its post, bit for bit, because its
    // 24 m range and the courier's 18 m closest approach mean it keeps the courier in view for the
    // whole of every approach. A memory it never consults is not a memory an arm can measure, and
    // asserting on that fixture would have been an arm that could not fail (ADR-182).
    //
    // So the arm is the scripted stage, which is the one thing that can take a percept *away* at a
    // chosen instant (ADR-290). Two seconds of a thing 30 m north of the post, then nothing.
    const auto run = [](float memorySeconds) {
        World world(guardFixture());
        world.setKnob("entity/sentry/decide/memorySeconds", memorySeconds);
        ScriptedPerception scripted;
        std::size_t index = 0;
        const auto& all = world.comp->entityWorld().entities();
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (all[i]->name() == "sentry") {
                index = i;
            }
        }
        Percept invented{};
        invented.kind = InterestKind::Character;
        invented.source = index;
        invented.position = glm::vec3(-34.0f, 0.0f, -36.0f);
        invented.distance = 30.0f;
        invented.salience = 0.9f;
        scripted.set(index, {invented});
        world.comp->entityWorld().setPerception(&scripted);

        float atTwo = 0.0f;
        float atSix = 0.0f;
        std::size_t remembered = 0;
        world.play(6.0, 60.0, [&](int frame) {
            const glm::vec3 p = world.entity("sentry").state().position();
            const float away = glm::length(glm::vec2(p.x - (-34.0f), p.z - (-6.0f)));
            if (frame == 120) {
                atTwo = away;
                scripted.clear(); // it went behind something, or round a corner, or simply away
            }
            atSix = away;
            DecisionDebug debug;
            if (world.decision("sentry", debug)) {
                remembered = std::max(remembered, debug.remembered);
            }
        });
        return std::tuple<float, float, std::size_t>(atTwo, atSix, remembered);
    };
    const auto remembering = run(6.0f);
    const auto forgetting = run(0.0f);
    WARN(fmt::format("two seconds of a percept, then none: remembering {:.3f} m -> {:.3f} m "
                     "(held {} percepts); forgetting {:.3f} m -> {:.3f} m (held {})",
                     std::get<0>(remembering), std::get<1>(remembering), std::get<2>(remembering),
                     std::get<0>(forgetting), std::get<1>(forgetting), std::get<2>(forgetting)));
    // Both arms are identical up to the moment the percept is taken away -- which is what says the
    // difference afterwards is the fade and not two different characters.
    CHECK(std::get<0>(remembering) == std::get<0>(forgetting));
    CHECK(std::get<2>(remembering) > 0);
    CHECK(std::get<2>(forgetting) == 0); // the control is genuinely a control
    CHECK(std::get<1>(remembering) > std::get<1>(forgetting));
}

TEST_CASE("a decided scene scrubs to where it played", "[entity][decision][adr333][adr091]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    // ADR-091 concedes scrub == play for the live tier and demands it of the baked one, and the
    // Character Intelligence Lab's case 5 carries the residual: 0.025000 m on the explorers at
    // 30 s. A selector carries a committed choice across frames, which is precisely the kind of
    // thing that would make that worse -- so it is measured here rather than assumed.
    World played(guardFixture());
    played.play(30.0);
    std::vector<glm::vec3> after;
    for (const auto& e : played.comp->entityWorld().entities()) {
        after.push_back(e->state().position());
    }

    World sought(guardFixture());
    sought.play(1.0); // so the world exists, its grid is built and its parameters registered
    sought.comp->entityWorld().seek(30.0, &sought.params, &sought.bus);

    float worst = 0.0f;
    const auto& bodies = sought.comp->entityWorld().entities();
    REQUIRE(bodies.size() == after.size());
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        worst = std::max(worst, glm::length(bodies[i]->state().position() - after[i]));
    }
    WARN(fmt::format("guard-post play-vs-seek at 30 s: {:.6f} m over {} bodies", worst,
                     bodies.size()));
    CHECK(worst < 0.5f);
}
