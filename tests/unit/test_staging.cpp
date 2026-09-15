// The director's contracts (ADR-209): sequence, parallel, scene queries, claims, and the failure
// cases that decide whether a shot can be shipped.
//
// Every test that guards a defect class has a negative control beside it, for the reason
// `test_entity_action.cpp` states at the top of itself: this project's recurring failure is a
// feature that quietly does nothing, and a test that would pass against a stub is how that ships.
// So the ordering test has a reordered partner; the parallel test has a sequential partner that must
// take twice as long; the `FindNearest` test has a `farthest` partner that must pick the other
// animal; the claim test has an `excludeClaimed: false` partner that must pick the same one twice.
//
// The harness is deliberately terrain-free. A director that only works over a real world would be a
// director nobody could test, and every question here -- did the steps run in order, did the claim
// hold, did the parallel cues finish together -- is a question about the decision layer rather than
// about the ground.

#include "entity/entity.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"
#include "stage/staging.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;
using Catch::Matchers::ContainsSubstring;

namespace {

params::ParamDesc<glm::vec3> v3(std::string path, glm::vec3 def, float lo, float hi) {
    return params::ParamDesc<glm::vec3>{.path = std::move(path),
                                        .defaultValue = def,
                                        .hardMin = glm::vec3(lo),
                                        .hardMax = glm::vec3(hi)};
}

void registerNode(params::ParameterSet& params, const std::string& node) {
    params.add(v3("nodes/" + node + "/position", glm::vec3(0.0f), -1e4f, 1e4f));
    params.add(v3("nodes/" + node + "/rotation", glm::vec3(0.0f), -360.0f, 360.0f));
    params.add(v3("nodes/" + node + "/scale", glm::vec3(1.0f), 0.001f, 100.0f));
    // hardMin/hardMax spelled out. A `ParamDesc<bool>` left at its defaults has a hard range of
    // [false, false], which clamps every write to 0 -- and then a test that asserts "hidden" passes
    // against a director that did nothing at all.
    params.add(params::ParamDesc<bool>{
        .path = "nodes/" + node + "/visible", .defaultValue = true, .hardMin = false, .hardMax = true});
    params.add(params::ParamDesc<float>{
        .path = "particles/" + node + "/spawnRate", .defaultValue = 10.0f, .hardMax = 100000.0f});
}

entity::NodeBinding bindingFor(const std::string& node, glm::vec3 anchor) {
    entity::NodeBinding b;
    b.node = node;
    b.exists = true;
    b.transformPrefix = "nodes/" + node + "/";
    b.geometryPrefix = "particles/" + node + "/";
    b.anchor = anchor;
    return b;
}

entity::EntityDesc animal(std::string name, std::vector<std::string> tags = {"animal"}) {
    entity::EntityDesc d;
    d.name = std::move(name);
    d.tags = std::move(tags);
    d.clips = {{"idle", "Walk"}, {"walk", "Walk"}};
    return d;
}

stage::Value lit(float v) { return stage::literal(v); }

stage::StepDesc step(stage::StepKind kind, std::string name) {
    stage::StepDesc s;
    s.kind = kind;
    s.name = std::move(name);
    return s;
}

stage::StepDesc waitStep(std::string name, float seconds) {
    stage::StepDesc s = step(stage::StepKind::Wait, std::move(name));
    s.duration = lit(seconds);
    return s;
}

// A world of named nodes, entities driving them, and a director over the top. Ticked at a fixed
// rate, which is what makes "how long did this take" a number rather than an impression.
struct Stage {
    params::ParameterSet params;
    signals::SignalBus bus;
    entity::EntityWorld world;
    stage::Staging staging;
    double time = 0.0;
    std::uint64_t frame = 0;

    Stage(std::vector<entity::EntityDesc> entities,
          std::vector<std::pair<std::string, glm::vec3>> nodes, stage::StagingDesc desc) {
        std::vector<entity::NodeBinding> bindings;
        for (const auto& [name, anchor] : nodes) {
            registerNode(params, name);
            bindings.push_back(bindingFor(name, anchor));
        }
        world.setEntities(std::move(entities), 7u);
        world.setBindings(std::move(bindings));
        world.registerParameters(params);
        world.bind(params);
        const auto ok = staging.setDesc(std::move(desc));
        REQUIRE(ok.has_value());
        staging.registerParameters(params);
        // One tick so every entity has an anchor and a state before anything is decided.
        tick(1.0 / 60.0);
    }

    // Published on the next tick only, so a test can fire a one-frame event the way the analysis
    // layer does.
    std::string pending;
    void fire(std::string signal) { pending = std::move(signal); }

    void tick(double seconds, double hz = 60.0) {
        const double step = 1.0 / hz;
        const int steps = std::max(1, static_cast<int>(std::round(seconds * hz)));
        for (int i = 0; i < steps; ++i) {
            params.resetFinals();
            bus.clearEvents();
            if (!pending.empty()) {
                bus.setEvent(bus.declare(pending, 0.0f, 1.0f, true), true);
                pending.clear();
            }
            stage::StageContext sc;
            sc.time = time;
            sc.dt = step;
            sc.world = &world;
            sc.params = &params;
            sc.bus = &bus;
            staging.update(sc);
            entity::EntityUpdate u;
            u.time = time;
            u.dt = step;
            u.frameIndex = frame++;
            u.bus = &bus;
            u.distanceDetail = false;
            world.update(u, params);
            time += step;
        }
    }

    [[nodiscard]] glm::vec3 where(const char* name) const {
        const entity::Entity* e = world.find(name);
        return e != nullptr ? e->state().position() : glm::vec3(0.0f);
    }
    // Every step that finished, in order, as "<beat>/<step>".
    [[nodiscard]] std::vector<std::string> done() const {
        std::vector<std::string> out;
        for (const stage::StageEvent& e : staging.log()) {
            if (e.kind == stage::StageEventKind::StepDone) {
                out.push_back(e.beat + "/" + e.step);
            }
        }
        return out;
    }
    [[nodiscard]] std::vector<std::string> boundTo(const char* role) const {
        std::vector<std::string> out;
        for (const stage::StageEvent& e : staging.log()) {
            if (e.kind == stage::StageEventKind::Bound && e.role == role) {
                out.push_back(e.detail);
            }
        }
        return out;
    }
};

// A scenario of one beat and one cue: the plainest thing the machine can run.
stage::StagingDesc oneCue(std::vector<stage::StepDesc> steps, std::string actorEntity = "hero") {
    stage::ActorDesc actor;
    actor.name = "star";
    actor.body = std::move(actorEntity);

    stage::CueDesc cue;
    cue.role = "actor";
    cue.steps = std::move(steps);

    stage::BeatDesc beat;
    beat.name = "only";
    beat.cues = {std::move(cue)};

    stage::ScenarioDesc scenario;
    scenario.name = "test";
    scenario.actor = "star";
    scenario.seed = 11u;
    scenario.maxCycles = 1;
    scenario.beats = {std::move(beat)};

    stage::StagingDesc desc;
    desc.actors = {std::move(actor)};
    desc.scenarios = {std::move(scenario)};
    return desc;
}

} // namespace

// ---- behaviour execution ------------------------------------------------------------------------

TEST_CASE("a cue runs its steps in the order it was given", "[stage][director]") {
    Stage s({animal("hero", {})}, {{"hero", glm::vec3(0.0f)}},
            oneCue({waitStep("first", 0.2f), waitStep("second", 0.2f), waitStep("third", 0.2f)}));
    s.staging.start("test", s.time);
    s.tick(1.2);
    CHECK(s.done() == std::vector<std::string>{"only/first", "only/second", "only/third"});
}

TEST_CASE("a different order is a different order", "[stage][director]") {
    // The negative control. If the machine ran steps in any order but the authored one -- or ran
    // them all at once -- the test above would pass against a stub that emitted a fixed list.
    Stage s({animal("hero", {})}, {{"hero", glm::vec3(0.0f)}},
            oneCue({waitStep("third", 0.2f), waitStep("first", 0.2f), waitStep("second", 0.2f)}));
    s.staging.start("test", s.time);
    s.tick(1.2);
    CHECK(s.done() == std::vector<std::string>{"only/third", "only/first", "only/second"});
}

TEST_CASE("wait delays the step after it", "[stage][director]") {
    Stage s({animal("hero", {})}, {{"hero", glm::vec3(0.0f)}},
            oneCue({waitStep("hold", 1.0f), waitStep("after", 0.05f)}));
    s.staging.start("test", s.time);
    s.tick(0.5);
    CHECK(s.done().empty()); // still waiting
    s.tick(0.7);
    CHECK(s.done() == std::vector<std::string>{"only/hold", "only/after"});
}

TEST_CASE("parallel cues run at the same time, and a sequence does not", "[stage][director]") {
    // Two one-second steps. In one beat with two cues they take a second; in one cue they take two.
    // That difference is the whole of what `Parallel` means here, and it is across two *entities*,
    // which a per-entity action queue structurally cannot express.
    const auto build = [](bool parallel) {
        stage::StepDesc a = waitStep("a", 1.0f);
        stage::StepDesc b = waitStep("b", 1.0f);
        b.role = "other";
        stage::BeatDesc beat;
        beat.name = "only";
        if (parallel) {
            stage::CueDesc c1;
            c1.role = "actor";
            c1.steps = {a};
            stage::CueDesc c2;
            c2.role = "other";
            c2.steps = {b};
            beat.cues = {c1, c2};
        } else {
            stage::CueDesc c1;
            c1.role = "actor";
            c1.steps = {a, b};
            beat.cues = {c1};
        }
        stage::ActorDesc actor;
        actor.name = "star";
        actor.body = "hero";
        stage::ScenarioDesc sc;
        sc.name = "test";
        sc.actor = "star";
        sc.maxCycles = 1;
        sc.beats = {std::move(beat)};
        stage::StagingDesc d;
        d.actors = {std::move(actor)};
        d.scenarios = {std::move(sc)};
        return d;
    };

    Stage par({animal("hero", {}), animal("other", {})},
              {{"hero", glm::vec3(0.0f)}, {"other", glm::vec3(10.0f, 0.0f, 0.0f)}}, build(true));
    par.staging.start("test", par.time);
    par.tick(1.3);
    INFO("parallel: " << par.done().size() << " steps done");
    CHECK(par.done().size() == 2);
    CHECK_FALSE(par.staging.running("test")); // the one cycle is over

    Stage seq({animal("hero", {}), animal("other", {})},
              {{"hero", glm::vec3(0.0f)}, {"other", glm::vec3(10.0f, 0.0f, 0.0f)}}, build(false));
    seq.staging.start("test", seq.time);
    seq.tick(1.3);
    CHECK(seq.done().size() == 1); // only the first has had time
    CHECK(seq.staging.running("test"));
    seq.tick(1.1);
    CHECK(seq.done().size() == 2);
}

TEST_CASE("moveTo arrives where the shot said", "[stage][director]") {
    stage::StepDesc move = step(stage::StepKind::MoveTo, "go");
    move.point = glm::vec3(40.0f, 12.0f, -25.0f);
    move.duration = lit(2.0f);
    Stage s({animal("hero", {})}, {{"hero", glm::vec3(0.0f)}}, oneCue({move}));
    s.staging.start("test", s.time);
    s.tick(1.0);
    const glm::vec3 half = s.where("hero");
    // Eased, so half the time is not half the distance -- but it has certainly left, and it has
    // certainly not arrived. A linear tween would put it at exactly the midpoint; the point of
    // checking both ends is that "it moved" and "it eased" are different claims.
    INFO("half way: " << half.x << ", " << half.y << ", " << half.z);
    CHECK(glm::length(half) > 1.0f);
    CHECK(glm::length(half - glm::vec3(40.0f, 12.0f, -25.0f)) > 1.0f);
    s.tick(1.2);
    const glm::vec3 there = s.where("hero");
    INFO("arrived: " << there.x << ", " << there.y << ", " << there.z);
    CHECK(glm::length(there - glm::vec3(40.0f, 12.0f, -25.0f)) < 0.01f);
    CHECK(s.done() == std::vector<std::string>{"only/go"});
}

TEST_CASE("moveTo does not arrive when it is not given the time", "[stage][director]") {
    // The negative control for the one above: the same step with ten times the distance and the
    // same two seconds must still be travelling, not teleported to the goal.
    stage::StepDesc move = step(stage::StepKind::MoveTo, "go");
    move.point = glm::vec3(400.0f, 0.0f, 0.0f);
    move.speed = lit(20.0f); // 20 s of travel
    Stage s({animal("hero", {})}, {{"hero", glm::vec3(0.0f)}}, oneCue({move}));
    s.staging.start("test", s.time);
    s.tick(2.0);
    CHECK(s.done().empty());
    CHECK(s.where("hero").x < 300.0f);
}

TEST_CASE("an actor's parts move with its body without a second transform system",
          "[stage][director][actor]") {
    // The beam is a *scene child* of the craft. The director moves the craft; nothing anywhere tells
    // the beam where to go, and it arrives anyway. This asserts the thing that would break if the
    // director had grown its own parenting: the part's own entity is untouched, and the composition
    // is what carries it.
    stage::StepDesc move = step(stage::StepKind::MoveTo, "go");
    move.point = glm::vec3(60.0f, 30.0f, 0.0f);
    move.duration = lit(1.0f);

    stage::ActorDesc actor;
    actor.name = "star";
    actor.body = "craft";
    actor.parts = {{"beam", "craft-beam"}};

    stage::CueDesc cue;
    cue.role = "actor";
    cue.steps = {move};
    stage::BeatDesc beat;
    beat.name = "only";
    beat.cues = {cue};
    stage::ScenarioDesc sc;
    sc.name = "test";
    sc.actor = "star";
    sc.maxCycles = 1;
    sc.beats = {beat};
    stage::StagingDesc d;
    d.actors = {actor};
    d.scenarios = {sc};

    Stage s({animal("craft", {}), animal("craft-beam", {})},
            {{"craft", glm::vec3(0.0f)}, {"craft-beam", glm::vec3(0.0f)}}, std::move(d));
    s.staging.start("test", s.time);
    s.tick(1.3);
    CHECK(glm::length(s.where("craft") - glm::vec3(60.0f, 30.0f, 0.0f)) < 0.01f);
    // The part's *entity* was never driven -- it has no director motion and no travel of its own.
    // Which is the point: the transform relationship is the scene's, not the director's.
    const entity::Entity* beamEntity = s.world.find("craft-beam");
    REQUIRE(beamEntity != nullptr);
    CHECK_FALSE(beamEntity->directorMotion().active);
    CHECK(glm::length(beamEntity->state().travel) < 1e-5f);
    // And the role was bound to the actor's body while the shot was running. (A finished scenario
    // drops its bindings, which is why this is checked against the log rather than against the live
    // binding.)
    bool sawActor = false;
    for (const stage::StageEvent& e : s.staging.log()) {
        sawActor = sawActor || (e.kind == stage::StageEventKind::StepDone && e.role == "actor");
    }
    CHECK(sawActor);
}

TEST_CASE("a play step asks for an activity, never a clip", "[stage][director][animation]") {
    // The indirection ADR-096 insists on. The step says "walk"; what actually plays is whatever
    // `EntityDesc::clips` maps that onto, which for the farm animals is the one clip they shipped.
    stage::StepDesc play = step(stage::StepKind::Play, "go");
    play.activity = "walk";
    play.duration = lit(0.5f);
    entity::EntityDesc hero = animal("hero", {});
    hero.clips = {{"walk", "Gallop"}, {"idle", "Standing"}};
    Stage s({hero}, {{"hero", glm::vec3(0.0f)}}, oneCue({play}));
    s.staging.start("test", s.time);
    s.tick(0.2);
    const entity::Entity* e = s.world.find("hero");
    REQUIRE(e != nullptr);
    const entity::ActionDesc* current = e->actions().current(entity::Authority::Director);
    REQUIRE(current != nullptr);
    CHECK(current->kind == entity::ActionKind::Pose);
    CHECK(current->activity == "walk"); // the activity, not "Gallop"
    CHECK(e->clipFor("walk") == "Gallop");
}

TEST_CASE("show, hide and set write the parameters an author can see", "[stage][director]") {
    stage::StepDesc hide = step(stage::StepKind::Hide, "off");
    stage::StepDesc set = step(stage::StepKind::Set, "ramp");
    set.target = "spawnRate";
    set.to = lit(500.0f);
    set.duration = lit(1.0f);
    Stage s({animal("hero", {})}, {{"hero", glm::vec3(0.0f)}}, oneCue({hide, set}));
    s.staging.start("test", s.time);
    const params::IParameter* visible = s.params.find("nodes/hero/visible");
    REQUIRE(visible != nullptr);
    // Visible before the step runs, so "hidden afterwards" is a change rather than a coincidence.
    REQUIRE(visible->baseComponent(0) == Approx(1.0f));
    s.tick(0.1);
    CHECK(visible->baseComponent(0) == Approx(0.0f));
    s.tick(0.5);
    const params::IParameter* rate = s.params.find("particles/hero/spawnRate");
    REQUIRE(rate != nullptr);
    INFO("half way through the ramp: " << rate->baseComponent(0));
    CHECK(rate->baseComponent(0) > 100.0f);
    CHECK(rate->baseComponent(0) < 450.0f);
    s.tick(0.7);
    CHECK(rate->baseComponent(0) == Approx(500.0f));
    // And reset puts the scene back exactly as the file described it, which is what a timeline seek
    // needs: a beam left lit would make a scrubbed frame depend on how the playhead got there.
    s.staging.reset(&s.world, &s.params);
    CHECK(visible->baseComponent(0) == Approx(1.0f));
    CHECK(rate->baseComponent(0) == Approx(10.0f));
}

// ---- target selection ---------------------------------------------------------------------------

namespace {

// A scenario that finds one animal and then waits, so what it picked is observable and nothing else
// happens. `pick` and the exclusions are what each test varies.
stage::StagingDesc finder(stage::Pick pick, float radius, bool excludeClaimed = true,
                          int cycles = 1, float clearance = 0.0f) {
    stage::QueryDesc q;
    q.bind = "target";
    q.tag = "animal";
    q.from = "actor";
    q.radius = lit(radius);
    q.clearance = lit(clearance);
    q.pick = pick;
    q.excludeClaimed = excludeClaimed;
    q.claim = true;

    stage::CueDesc cue;
    cue.role = "actor";
    cue.steps = {waitStep("beat", 0.1f)};

    stage::BeatDesc beat;
    beat.name = "acquire";
    beat.find = {q};
    beat.cues = {cue};

    stage::ActorDesc actor;
    actor.name = "star";
    actor.body = "hero";
    stage::ScenarioDesc sc;
    sc.name = "test";
    sc.actor = "star";
    sc.seed = 5u;
    sc.maxCycles = cycles;
    sc.searchInterval = 0.05;
    sc.beats = {std::move(beat)};
    stage::StagingDesc d;
    d.actors = {std::move(actor)};
    d.scenarios = {std::move(sc)};
    return d;
}

std::vector<entity::EntityDesc> herd() {
    return {animal("hero", {}), animal("near"), animal("far"), animal("rock", {"prop"})};
}
std::vector<std::pair<std::string, glm::vec3>> herdNodes() {
    return {{"hero", glm::vec3(0.0f)},
            {"near", glm::vec3(10.0f, 0.0f, 0.0f)},
            {"far", glm::vec3(90.0f, 0.0f, 0.0f)},
            {"rock", glm::vec3(2.0f, 0.0f, 0.0f)}};
}

} // namespace

TEST_CASE("findNearest picks the nearest, and farthest picks the other one",
          "[stage][director][query]") {
    Stage near(herd(), herdNodes(), finder(stage::Pick::Nearest, 500.0f));
    near.staging.start("test", near.time);
    near.tick(0.4);
    CHECK(near.boundTo("target") == std::vector<std::string>{"near"});

    // The negative control. Same world, same query, one field different: if the pick were ignored
    // the test above would pass against a director that always returned the first candidate.
    Stage far(herd(), herdNodes(), finder(stage::Pick::Farthest, 500.0f));
    far.staging.start("test", far.time);
    far.tick(0.4);
    CHECK(far.boundTo("target") == std::vector<std::string>{"far"});
}

TEST_CASE("a query filters on the tag, and never picks something that is not one",
          "[stage][director][query]") {
    // `rock` is two metres away -- nearer than anything else -- and tagged `prop`. A director that
    // ignored the tag would pick it every time.
    Stage s(herd(), herdNodes(), finder(stage::Pick::Nearest, 500.0f));
    s.staging.start("test", s.time);
    s.tick(0.4);
    CHECK(s.boundTo("target") == std::vector<std::string>{"near"});
}

TEST_CASE("the search radius is respected", "[stage][director][query]") {
    // 50 m reaches `near` (10 m) and not `far` (90 m).
    Stage inside(herd(), herdNodes(), finder(stage::Pick::Farthest, 50.0f));
    inside.staging.start("test", inside.time);
    inside.tick(0.4);
    CHECK(inside.boundTo("target") == std::vector<std::string>{"near"});

    // And a radius that reaches nothing binds nothing, and the scenario ends rather than hanging.
    Stage outside(herd(), herdNodes(), finder(stage::Pick::Nearest, 2.0f));
    outside.staging.start("test", outside.time);
    outside.tick(0.4);
    CHECK(outside.boundTo("target").empty());
    CHECK_FALSE(outside.staging.running("test"));
}

TEST_CASE("a claimed target is not offered to the next cycle", "[stage][director][claim]") {
    // Two cycles, and the claim is never released -- so the second must find the *other* animal.
    // This is "two actors never grab the same animal", asserted at the level the rule lives at.
    Stage s(herd(), herdNodes(), finder(stage::Pick::Nearest, 500.0f, true, 2));
    s.staging.start("test", s.time);
    s.tick(1.0);
    const auto picked = s.boundTo("target");
    INFO("picked: " << (picked.empty() ? "" : picked[0]) << ", "
                    << (picked.size() > 1 ? picked[1] : ""));
    REQUIRE(picked.size() == 2);
    CHECK(picked[0] == "near");
    CHECK(picked[1] == "far");
    CHECK(picked[0] != picked[1]);
}

TEST_CASE("without the exclusion, the same target is picked twice", "[stage][director][claim]") {
    // The negative control for the claim. If claiming did nothing, the test above would pass
    // whenever the nearest animal happened to be different on the second pass.
    Stage s(herd(), herdNodes(), finder(stage::Pick::Nearest, 500.0f, false, 2));
    s.staging.start("test", s.time);
    s.tick(1.0);
    const auto picked = s.boundTo("target");
    REQUIRE(picked.size() == 2);
    CHECK(picked[0] == "near");
    CHECK(picked[1] == "near");
}

TEST_CASE("the search does not scan the scene every frame", "[stage][director][performance]") {
    // The brief's performance requirement, as a measurement rather than a promise. Sixty frames at
    // a half-second search interval is at most three rebuilds, and the candidate set is the tagged
    // entities rather than everything in the world.
    stage::StagingDesc d = finder(stage::Pick::Nearest, 500.0f, true, 1);
    d.scenarios[0].searchInterval = 0.5;
    d.scenarios[0].beats[0].cues[0].steps[0].duration = lit(1.0f);
    Stage s(herd(), herdNodes(), std::move(d));
    s.staging.start("test", s.time);
    s.tick(1.0); // 60 frames
    INFO("searches " << s.staging.report().searches << ", candidates "
                     << s.staging.report().candidates << ", tested " << s.staging.report().tested);
    CHECK(s.staging.report().searches <= 3);
    // Three animals are tagged; `hero` and `rock` are not.
    CHECK(s.staging.report().candidates == 2);
    CHECK(s.staging.report().tested <= 6);
}

// ---- failure cases ------------------------------------------------------------------------------

TEST_CASE("no valid target leaves the director idle and restartable", "[stage][director][failure]") {
    Stage s({animal("hero", {})}, {{"hero", glm::vec3(0.0f)}},
            finder(stage::Pick::Nearest, 500.0f));
    s.staging.start("test", s.time);
    s.tick(0.5);
    CHECK_FALSE(s.staging.running("test"));
    CHECK(s.staging.report().unbound == 1);
    // Not stuck: restartable, and it fails the same way rather than a different one.
    CHECK(s.staging.start("test", s.time));
    s.tick(0.5);
    CHECK_FALSE(s.staging.running("test"));
    CHECK(s.staging.report().unbound == 2);
}

TEST_CASE("a target that vanishes mid-sequence ends its cue, not the shot",
          "[stage][director][failure]") {
    // The target is retired by a `retire` step and then a later step still names it. A director that
    // stopped dead the first time a target went away would be a director nobody could ship.
    stage::QueryDesc q;
    q.bind = "target";
    q.tag = "animal";
    q.from = "actor";
    q.radius = lit(500.0f);

    stage::StepDesc lift = step(stage::StepKind::MoveTo, "lift");
    lift.role = "target";
    lift.toRole = "actor";
    lift.duration = lit(0.3f);
    stage::StepDesc gone = step(stage::StepKind::Retire, "vanish");
    gone.role = "target";
    stage::StepDesc after = step(stage::StepKind::MoveTo, "after");
    after.role = "target";
    after.toRole = "nobody"; // a role nothing ever bound: the destination cannot resolve
    after.duration = lit(0.2f);

    stage::CueDesc targetCue;
    targetCue.role = "target";
    targetCue.steps = {lift, gone, after};
    stage::CueDesc actorCue;
    actorCue.role = "actor";
    actorCue.steps = {waitStep("hold", 0.8f)};

    stage::BeatDesc beat;
    beat.name = "abduct";
    beat.find = {q};
    beat.cues = {actorCue, targetCue};

    stage::ActorDesc actor;
    actor.name = "star";
    actor.body = "hero";
    stage::ScenarioDesc sc;
    sc.name = "test";
    sc.actor = "star";
    sc.maxCycles = 1;
    sc.beats = {beat};
    stage::StagingDesc d;
    d.actors = {actor};
    d.scenarios = {sc};

    Stage s(herd(), herdNodes(), std::move(d));
    s.staging.start("test", s.time);
    s.tick(1.2);
    // The step that could not resolve failed and said so; the beat still ended; the shot is over
    // rather than hung.
    CHECK(s.staging.report().stepsFailed >= 1);
    CHECK(s.staging.report().retired == 1);
    CHECK_FALSE(s.staging.running("test"));
    CHECK(s.staging.retired() == std::vector<std::string>{"near"});
}

TEST_CASE("a route that does not exist fails the step rather than reading as arrival",
          "[stage][director][failure]") {
    // `travel: "walk"` hands the move to ADR-096, which routes it over the navigation layer. A
    // provider that refuses every destination is what "path unavailable" looks like from here, and
    // the director has to hear it: a queue that drained is not the same claim as a body that
    // arrived, and treating them the same is the silent no-op this project keeps finding.
    struct NoRoute final : entity::IPathProvider {
        [[nodiscard]] entity::RouteStatus route(glm::vec2, glm::vec2,
                                                std::vector<glm::vec2>&) const override {
            return entity::RouteStatus::Unreachable;
        }
        [[nodiscard]] glm::vec2 steer(glm::vec2, glm::vec2, float) const override { return {}; }
        [[nodiscard]] float groundHeight(glm::vec2) const override { return 0.0f; }
    };
    NoRoute blocked;

    stage::StepDesc move = step(stage::StepKind::MoveTo, "walkThere");
    move.travel = stage::Travel::Walk;
    move.point = glm::vec3(50.0f, 0.0f, 0.0f);
    move.speed = lit(4.0f);
    Stage s({animal("hero", {})}, {{"hero", glm::vec3(0.0f)}}, oneCue({move, waitStep("after", 0.1f)}));
    s.world.setPathProvider(&blocked);
    s.staging.start("test", s.time);
    s.tick(1.0);
    INFO("done: " << s.done().size() << ", failed: " << s.staging.report().stepsFailed);
    CHECK(s.staging.report().stepsFailed == 1);
    // And the reason came through rather than being flattened to "something went wrong".
    bool sawReason = false;
    for (const stage::StageEvent& e : s.staging.log()) {
        if (e.kind == stage::StageEventKind::StepFailed) {
            INFO("reason: " << e.detail);
            sawReason = sawReason || e.detail == "unreachable";
        }
    }
    CHECK(sawReason);
    CHECK(s.where("hero").x < 1.0f); // it never set off
    CHECK_FALSE(s.staging.running("test"));
}

TEST_CASE("a route that does exist is not reported as a failure", "[stage][director][failure]") {
    // The negative control. Without it the test above would pass against a director that called
    // every walk a failure.
    stage::StepDesc move = step(stage::StepKind::MoveTo, "walkThere");
    move.travel = stage::Travel::Walk;
    move.point = glm::vec3(4.0f, 0.0f, 0.0f);
    move.speed = lit(8.0f);
    move.tolerance = lit(0.6f);
    Stage s({animal("hero", {})}, {{"hero", glm::vec3(0.0f)}}, oneCue({move}));
    s.staging.start("test", s.time);
    s.tick(3.0);
    INFO("arrived at x = " << s.where("hero").x << ", failures " << s.staging.report().stepsFailed);
    CHECK(s.staging.report().stepsFailed == 0);
    CHECK(s.done() == std::vector<std::string>{"only/walkThere"});
    CHECK(s.where("hero").x > 3.0f);
}

TEST_CASE("an actor that is not there fails the step rather than the process",
          "[stage][director][failure]") {
    stage::StepDesc move = step(stage::StepKind::MoveTo, "go");
    move.point = glm::vec3(10.0f);
    move.duration = lit(0.2f);
    // The actor names an entity the world does not contain.
    Stage s({animal("someone-else", {})}, {{"someone-else", glm::vec3(0.0f)}},
            oneCue({move}, "ghost"));
    s.staging.start("test", s.time);
    s.tick(0.5);
    CHECK(s.staging.report().stepsFailed >= 1);
    CHECK_FALSE(s.staging.running("test"));
}

TEST_CASE("cancelling a scenario drops every claim and every hold", "[stage][director][failure]") {
    stage::StagingDesc d = finder(stage::Pick::Nearest, 500.0f, true, 1);
    d.scenarios[0].beats[0].cues[0].steps[0].duration = lit(10.0f);
    Stage s(herd(), herdNodes(), std::move(d));
    s.staging.start("test", s.time);
    s.tick(0.3);
    REQUIRE(s.staging.running("test"));
    REQUIRE(s.staging.binding("test", "target") == "near");

    CHECK(s.staging.stop("test", s.time));
    CHECK_FALSE(s.staging.running("test"));
    // The claim is gone, which a second run proves by picking the same animal again -- if the claim
    // had survived the cancellation it would have to pick `far`.
    CHECK(s.staging.start("test", s.time));
    s.tick(0.3);
    CHECK(s.staging.binding("test", "target") == "near");
}

TEST_CASE("cancelling a scenario lets go of the body it was holding",
          "[stage][director][failure]") {
    // A `stop` that left a cow twenty metres in the air, held by a shot that is over, is exactly the
    // permanently-broken state the brief asks the director not to end up in. Asserted by moving a
    // body first -- so the hold is real and the release is a change -- rather than by checking a
    // flag that was never set.
    stage::StepDesc move = step(stage::StepKind::MoveTo, "go");
    move.point = glm::vec3(0.0f, 40.0f, 0.0f);
    move.duration = lit(10.0f);
    Stage s({animal("hero", {})}, {{"hero", glm::vec3(0.0f)}}, oneCue({move}));
    s.staging.start("test", s.time);
    s.tick(1.0);
    const entity::Entity* hero = s.world.find("hero");
    REQUIRE(hero != nullptr);
    REQUIRE(hero->directorMotion().active);
    REQUIRE(hero->state().driven);
    const glm::vec3 held = hero->state().position();
    REQUIRE(held.y > 0.5f); // it really is off the ground

    s.staging.stop("test", s.time);
    CHECK_FALSE(hero->directorMotion().active);
    s.tick(0.1);
    // Released: the body is its own again, and it kept where the shot left it rather than snapping
    // back to the anchor -- because a director writes `travel`, which persists.
    CHECK_FALSE(hero->state().driven);
    CHECK(hero->state().position().y == Approx(held.y).margin(0.5));
}

TEST_CASE("a scenario can be cued by a signal, and stopped by one",
          "[stage][director][signals]") {
    // The brief asks the director to operate on "existing audio/event systems". It already does:
    // the signal bus carries every musical event, analysis band, OSC message and MIDI note under a
    // name, so this is two fields rather than a subsystem.
    stage::StagingDesc d = oneCue({waitStep("hold", 30.0f)});
    d.scenarios[0].startOn = "audio.beat";
    d.scenarios[0].stopOn = "song.end";
    Stage s({animal("hero", {})}, {{"hero", glm::vec3(0.0f)}}, std::move(d));

    s.tick(0.5);
    CHECK_FALSE(s.staging.running("test")); // nothing has fired

    s.fire("audio.beat");
    s.tick(0.1);
    CHECK(s.staging.running("test"));

    s.fire("song.end");
    s.tick(0.1);
    CHECK_FALSE(s.staging.running("test"));
}

TEST_CASE("a scenario cued by a signal does not start on a different one",
          "[stage][director][signals]") {
    // The negative control: without it the test above would pass against a director that started
    // on any event at all.
    stage::StagingDesc d = oneCue({waitStep("hold", 30.0f)});
    d.scenarios[0].startOn = "audio.beat";
    Stage s({animal("hero", {})}, {{"hero", glm::vec3(0.0f)}}, std::move(d));
    s.fire("audio.onset");
    s.tick(0.5);
    CHECK_FALSE(s.staging.running("test"));
}

TEST_CASE("the same seed and the same world decide the same way", "[stage][director][determinism]") {
    // Determinism is non-negotiable (ADR-091): identical seed and configuration must produce
    // identical behaviour, or an offline render is not a render of what was played.
    const auto run = [] {
        stage::StagingDesc d = finder(stage::Pick::Random, 500.0f, true, 3);
        Stage s(herd(), herdNodes(), std::move(d));
        s.staging.start("test", s.time);
        s.tick(2.0);
        return std::make_pair(s.boundTo("target"), s.where("hero"));
    };
    const auto a = run();
    const auto b = run();
    CHECK(a.first == b.first);
    CHECK(a.second == b.second);
    CHECK_FALSE(a.first.empty());
}

// ---- serialisation ------------------------------------------------------------------------------

TEST_CASE("a staging description round-trips through JSON", "[stage][director][json]") {
    // Everything in the format is authorable, because the architectural claim is that a different
    // sequence needs no new C++. A field that did not round-trip would be a field an editor's save
    // silently dropped.
    stage::StagingDesc d = finder(stage::Pick::Farthest, 123.0f, false, 4, 7.5f);
    d.scenarios[0].params = {{"speed", 12.0f, 0.0f, 40.0f}};
    stage::StepDesc move = step(stage::StepKind::MoveTo, "go");
    move.toRole = "target";
    move.height = stage::bound("speed", 3.0f);
    move.spin = lit(90.0f);
    move.aboveGround = true;
    d.scenarios[0].beats[0].cues[0].steps.push_back(move);

    const nlohmann::json j = stage::stagingToJson(d);
    const auto back = stage::stagingFromJson(j);
    REQUIRE(back.has_value());
    REQUIRE(back->scenarios.size() == 1);
    const stage::ScenarioDesc& s = back->scenarios[0];
    CHECK(s.name == "test");
    CHECK(s.actor == "star");
    CHECK(s.maxCycles == 4);
    REQUIRE(s.beats.size() == 1);
    REQUIRE(s.beats[0].find.size() == 1);
    CHECK(s.beats[0].find[0].pick == stage::Pick::Farthest);
    CHECK(s.beats[0].find[0].radius.literal == Approx(123.0f));
    CHECK(s.beats[0].find[0].clearance.literal == Approx(7.5f));
    CHECK_FALSE(s.beats[0].find[0].excludeClaimed);
    REQUIRE(s.beats[0].cues[0].steps.size() == 2);
    const stage::StepDesc& round = s.beats[0].cues[0].steps[1];
    CHECK(round.kind == stage::StepKind::MoveTo);
    CHECK(round.toRole == "target");
    CHECK(round.height.param == "speed");
    CHECK(round.spin.literal == Approx(90.0f));
    CHECK(round.aboveGround);
    CHECK(stage::stagingToJson(*back) == j);
}

TEST_CASE("a value naming a parameter the scenario never declared is refused",
          "[stage][director][json]") {
    // A parameter that silently read zero would be a hover duration of nothing and a search radius
    // of nothing -- which looks exactly like a director that is broken for a reason nobody can find.
    stage::StagingDesc d = finder(stage::Pick::Nearest, 10.0f);
    d.scenarios[0].beats[0].find[0].radius = stage::bound("noSuchKnob");
    stage::Staging staging;
    const auto ok = staging.setDesc(std::move(d));
    REQUIRE_FALSE(ok.has_value());
    CHECK_THAT(ok.error().message, ContainsSubstring("noSuchKnob"));
}

TEST_CASE("a beat naming a beat that is not there is refused", "[stage][director][json]") {
    stage::StagingDesc d = finder(stage::Pick::Nearest, 10.0f);
    d.scenarios[0].beats[0].then = "nowhere";
    stage::Staging staging;
    const auto ok = staging.setDesc(std::move(d));
    REQUIRE_FALSE(ok.has_value());
    CHECK_THAT(ok.error().message, ContainsSubstring("nowhere"));
}
