// The intent layer's contracts (ADR-096): ordering, completion, interruption, resumption,
// schedules, interactions, equipping and gait stability.
//
// Every test that guards a defect class has a negative control beside it -- a case that proves the
// test can *fail*. The project's recurring failure is a feature that quietly does nothing, and a
// test that would pass against a stub is how that ships. So: the ordering test has a reordered
// partner; the resumption test has a `resumable: false` partner that must restart; the hysteresis
// test has a collapsed-band partner that must flicker; the equip test has a missing-socket partner
// that must change nothing.

#include "entity/action.hpp"
#include "entity/entity.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::WithinAbs;

namespace {

params::ParamDesc<glm::vec3> v3(std::string path, glm::vec3 def, float lo, float hi) {
    return params::ParamDesc<glm::vec3>{
        .path = std::move(path), .defaultValue = def, .hardMin = glm::vec3(lo), .hardMax = glm::vec3(hi)};
}

void registerNode(params::ParameterSet& params, const std::string& node) {
    params.add(v3("nodes/" + node + "/position", glm::vec3(0.0f), -1e4f, 1e4f));
    params.add(v3("nodes/" + node + "/rotation", glm::vec3(0.0f), -360.0f, 360.0f));
    params.add(v3("nodes/" + node + "/scale", glm::vec3(1.0f), 0.001f, 100.0f));
}

entity::NodeBinding bindingFor(const std::string& node, glm::vec3 anchor) {
    entity::NodeBinding b;
    b.node = node;
    b.exists = true;
    b.transformPrefix = "nodes/" + node + "/";
    b.anchor = anchor;
    return b;
}

entity::SocketDesc socket(const char* name, glm::vec3 offset = glm::vec3(0.0f)) {
    entity::SocketDesc s;
    s.name = name;
    s.offset.position = offset;
    return s;
}

entity::ActionDesc action(entity::ActionKind kind, std::string name = {}) {
    entity::ActionDesc a;
    a.kind = kind;
    a.name = std::move(name);
    return a;
}

entity::ActionDesc wait(double seconds, std::string name, std::string event = {}) {
    entity::ActionDesc a = action(entity::ActionKind::Wait, std::move(name));
    a.duration = seconds;
    a.onComplete = std::move(event);
    return a;
}

entity::ActionTarget entityTarget(std::string name) {
    entity::ActionTarget t;
    t.kind = entity::TargetKind::EntityRef;
    t.name = std::move(name);
    return t;
}

entity::ActionTarget interactionTarget(std::string prop, std::string verb) {
    entity::ActionTarget t;
    t.kind = entity::TargetKind::Interaction;
    t.name = std::move(prop);
    t.member = std::move(verb);
    return t;
}

entity::ActionTarget pointTarget(glm::vec3 p) {
    entity::ActionTarget t;
    t.kind = entity::TargetKind::Point;
    t.point = p;
    return t;
}

// A world of named nodes at named places, with entities driving them. The whole harness: no
// terrain, so the navigator is invalid and the default path provider answers the y = 0 plane --
// which is exactly the seam a walker gets in a scene with no ground, and keeps these tests about
// intent rather than about noise fields.
struct World {
    params::ParameterSet params;
    signals::SignalBus bus;
    entity::EntityWorld world;
    std::vector<entity::ActionEvent> log;
    double time = 0.0;
    std::uint64_t frame = 0;

    World(std::vector<entity::EntityDesc> descs, std::vector<std::pair<std::string, glm::vec3>> nodes,
          std::uint32_t seed = 7u) {
        std::vector<entity::NodeBinding> bindings;
        for (const auto& [name, anchor] : nodes) {
            registerNode(params, name);
            bindings.push_back(bindingFor(name, anchor));
        }
        world.setEntities(std::move(descs), seed);
        world.setBindings(std::move(bindings));
        world.registerParameters(params);
        world.bind(params);
        world.setActionListener([this](const entity::ActionEvent& e) { log.push_back(e); });
    }

    void tick(double seconds, double hz = 60.0) {
        const int steps = static_cast<int>(seconds * hz + 0.5);
        for (int i = 0; i < steps; ++i) {
            params.resetFinals();
            entity::EntityUpdate u;
            time += 1.0 / hz;
            u.time = time;
            u.dt = 1.0 / hz;
            u.frameIndex = ++frame;
            u.bus = &bus;
            world.update(u, params);
        }
    }

    [[nodiscard]] entity::Entity& actor(const char* name = "hero") {
        entity::Entity* e = world.find(name);
        REQUIRE(e != nullptr);
        return *e;
    }

    [[nodiscard]] std::vector<std::string> completed() const {
        std::vector<std::string> names;
        for (const entity::ActionEvent& e : log) {
            if (e.result == entity::ActionResult::Completed) {
                names.push_back(e.action);
            }
        }
        return names;
    }
};

entity::EntityDesc heroDesc(const char* name = "hero") {
    entity::EntityDesc d;
    d.name = name;
    d.node = name;
    d.seed = 4242;
    d.clips = {{"idle", "Idle"}, {"walk", "Walk"}, {"run", "Run"}};
    return d;
}

// A path provider that says whatever the test needs it to say. The point of the seam: navigation
// can be a stub, a straight line or a search, and the action layer does not change.
class StubPath final : public entity::IPathProvider {
public:
    entity::PathStatus status = entity::PathStatus::Ready;
    mutable int routeCalls = 0;
    [[nodiscard]] entity::PathStatus route(glm::vec2 from, glm::vec2 to,
                                           std::vector<glm::vec2>& out) const override {
        ++routeCalls;
        (void)from;
        if (status != entity::PathStatus::Ready) {
            return status;
        }
        out.clear();
        out.push_back(to);
        return entity::PathStatus::Ready;
    }
    [[nodiscard]] glm::vec2 steer(glm::vec2 from, glm::vec2 to, float) const override {
        const glm::vec2 d = to - from;
        const float len = glm::length(d);
        return len > 1e-5f ? d / len : glm::vec2(0.0f);
    }
    [[nodiscard]] float groundHeight(glm::vec2) const override { return 0.0f; }
};

} // namespace

// ---- §4 ordering and completion ------------------------------------------------------------------

TEST_CASE("an action queue drains in the order it was given", "[entity][action]") {
    entity::EntityDesc hero = heroDesc();
    hero.actions = {wait(0.2, "first", "one"), wait(0.2, "second", "two"), wait(0.2, "third", "three")};
    World w({hero}, {{"hero", glm::vec3(0.0f)}});
    w.tick(1.0);
    CHECK(w.completed() == std::vector<std::string>{"first", "second", "third"});
    CHECK(w.actor().actions().pending() == 0);
}

TEST_CASE("a different order is a different order", "[entity][action]") {
    // The negative control for the test above: if the queue drained by name, by duration or by
    // anything other than the order it was given, both tests could not pass at once.
    entity::EntityDesc hero = heroDesc();
    hero.actions = {wait(0.2, "third"), wait(0.2, "first"), wait(0.2, "second")};
    World w({hero}, {{"hero", glm::vec3(0.0f)}});
    w.tick(1.0);
    CHECK(w.completed() == std::vector<std::string>{"third", "first", "second"});
}

TEST_CASE("a completed action fires the event its author named", "[entity][action]") {
    entity::EntityDesc hero = heroDesc();
    hero.actions = {wait(0.1, "wake", "routine.begin")};
    World w({hero}, {{"hero", glm::vec3(0.0f)}});
    w.tick(0.5);
    REQUIRE(w.log.size() == 1);
    CHECK(w.log[0].entity == "hero");
    CHECK(w.log[0].action == "wake");
    CHECK(w.log[0].event == "routine.begin");
    CHECK(w.log[0].result == entity::ActionResult::Completed);
    // The time is the timeline second, not a wall clock, and it is the second the action ended.
    CHECK_THAT(w.log[0].time, WithinAbs(0.1, 0.02));
}

TEST_CASE("an action whose conditions fail is skipped, and its effects do not happen",
          "[entity][action]") {
    // The negative control for completion: a skipped action must be reported as skipped and must
    // not write what a completed one would have. A queue that reported everything Completed would
    // pass the test above and fail this one.
    entity::EntityDesc hero = heroDesc();
    hero.properties = {entity::PropertyDesc{"awake", 0.0f, 0.0f, 1.0f},
                       entity::PropertyDesc{"dressed", 0.0f, 0.0f, 1.0f}};
    entity::ActionDesc dress = action(entity::ActionKind::Set, "dress");
    dress.when = {entity::Condition{"awake", {}, entity::Test::Set, 0.0f}};
    dress.set = {entity::PropertySet{"dressed", {}, 1.0f}};
    hero.actions = {dress};
    World w({hero}, {{"hero", glm::vec3(0.0f)}});
    w.tick(0.2);
    REQUIRE(w.log.size() == 1);
    CHECK(w.log[0].result == entity::ActionResult::Skipped);
    CHECK(w.actor().property("dressed") == 0.0f);
}

TEST_CASE("a failed condition may branch rather than simply skip", "[entity][action]") {
    entity::EntityDesc hero = heroDesc();
    hero.properties = {entity::PropertyDesc{"awake", 0.0f, 0.0f, 1.0f}};
    entity::ActionDesc leave = action(entity::ActionKind::Wait, "leave");
    leave.duration = 0.1;
    leave.when = {entity::Condition{"awake", {}, entity::Test::Set, 0.0f}};
    leave.otherwise = "sleepIn";
    hero.actions = {leave, wait(0.1, "breakfast"), wait(0.1, "sleepIn")};
    World w({hero}, {{"hero", glm::vec3(0.0f)}});
    w.tick(1.0);
    // Skipped `leave`, jumped over `breakfast` to `sleepIn`.
    CHECK(w.completed() == std::vector<std::string>{"sleepIn"});
}

// ---- §4 interruption and resumption ----------------------------------------------------------

TEST_CASE("a director override preempts an action and the routine resumes where it was",
          "[entity][action][authority]") {
    // ADR-091's rule, in one test: the tier below keeps its place *and its elapsed seconds*, so
    // `Walking -> Dance -> Walking` is what comes back rather than `Walking -> Dance -> Idle`.
    entity::EntityDesc hero = heroDesc();
    hero.actions = {wait(4.0, "long"), wait(0.1, "after")};
    World w({hero}, {{"hero", glm::vec3(0.0f)}});
    w.tick(1.0);
    const double before = w.actor().actions().elapsed(entity::Authority::Routine);
    CHECK_THAT(before, WithinAbs(1.0, 0.05));
    CHECK(w.actor().actions().authority() == entity::Authority::Routine);

    REQUIRE(w.world.direct("hero", {wait(0.5, "cutaway")}, w.time));
    w.tick(0.25);
    CHECK(w.actor().actions().authority() == entity::Authority::Director);
    // The preempted action is untouched while somebody else is driving.
    CHECK_THAT(w.actor().actions().elapsed(entity::Authority::Routine), WithinAbs(before, 1e-9));

    w.tick(0.5); // the override finishes and the routine takes over again
    CHECK(w.actor().actions().authority() == entity::Authority::Routine);
    CHECK(w.actor().actions().current()->name == "long");
    // It resumed, not restarted: roughly a second of credit is still on the clock.
    CHECK_THAT(w.actor().actions().elapsed(entity::Authority::Routine), WithinAbs(before + 0.25, 0.1));
    CHECK(w.completed() == std::vector<std::string>{"cutaway"});
}

TEST_CASE("an action marked unresumable replays from its start instead", "[entity][action][authority]") {
    // The negative control: resumption is a *choice the data makes*, not an accident of the
    // implementation. If the queue simply never rewound, this test could not pass.
    entity::EntityDesc hero = heroDesc();
    entity::ActionDesc once = wait(4.0, "oneShot");
    once.resumable = false;
    hero.actions = {once};
    World w({hero}, {{"hero", glm::vec3(0.0f)}});
    w.tick(1.0);
    CHECK_THAT(w.actor().actions().elapsed(entity::Authority::Routine), WithinAbs(1.0, 0.05));
    REQUIRE(w.world.direct("hero", {wait(0.2, "cutaway")}, w.time));
    w.tick(0.4);
    CHECK(w.actor().actions().authority() == entity::Authority::Routine);
    CHECK(w.actor().actions().current()->name == "oneShot");
    CHECK(w.actor().actions().elapsed(entity::Authority::Routine) < 0.35);
}

TEST_CASE("a behaviour preempted by an action keeps walking where it was going",
          "[entity][action][authority]") {
    // The tier below the action tier is a *behaviour*, and the same rule applies to it: a wanderer
    // interrupted mid-walk must carry on to the destination it had, not pick a new one. Measured
    // as the heading, which is the only thing a wanderer's destination is observable through.
    entity::EntityDesc hero = heroDesc();
    entity::BehaviorDesc wander;
    wander.kind = "wander";
    wander.name = "wander";
    wander.settings = {{"kind", "wander"}, {"speed", 1.5}, {"pauseMin", 0.0}, {"pauseMax", 0.0},
                       {"minRange", 40.0}, {"maxRange", 60.0}};
    hero.behaviors = {wander};
    World w({hero}, {{"hero", glm::vec3(0.0f)}});
    w.tick(3.0);
    const glm::vec3 before = w.actor().state().position();
    w.tick(0.5);
    const glm::vec3 heading = glm::normalize(w.actor().state().position() - before);

    const glm::vec3 atInterrupt = w.actor().state().position();
    REQUIRE(w.world.direct("hero", {wait(1.0, "hold")}, w.time));
    w.tick(1.0);
    // The action held it still: a behaviour that had gone on walking would have moved it.
    CHECK(glm::length(w.actor().state().position() - atInterrupt) < 0.05f);

    w.tick(0.5);
    const glm::vec3 after = glm::normalize(w.actor().state().position() - atInterrupt);
    CHECK(glm::dot(heading, after) > 0.98f); // the same way it was going
}

TEST_CASE("an action dropped by a higher authority says it was cancelled", "[entity][action]") {
    // Not a defect in itself -- a director is allowed to change its mind -- but an action that
    // vanished without a word would make "why did that never finish" unanswerable, which is the
    // failure this project keeps shipping.
    entity::EntityDesc hero = heroDesc();
    World w({hero}, {{"hero", glm::vec3(0.0f)}});
    REQUIRE(w.world.direct("hero", {wait(5.0, "longTake")}, w.time));
    w.tick(0.5);
    CHECK(w.log.empty());
    REQUIRE(w.world.direct("hero", {wait(0.1, "shortTake")}, w.time));
    w.tick(0.3);
    REQUIRE(w.log.size() == 2);
    CHECK(w.log[0].action == "longTake");
    CHECK(w.log[0].result == entity::ActionResult::Cancelled);
    CHECK(w.log[1].action == "shortTake");
    CHECK(w.log[1].result == entity::ActionResult::Completed);
}

// ---- §9 schedules ------------------------------------------------------------------------------

TEST_CASE("a routine can be started, paused and resumed without losing its place",
          "[entity][schedule]") {
    entity::EntityDesc hero = heroDesc();
    hero.schedule.name = "morning";
    hero.schedule.entries = {
        entity::ScheduleEntry{0.0, "wake", {wait(0.1, "wake")}},
        entity::ScheduleEntry{1.0, "wash", {wait(0.1, "wash")}},
        entity::ScheduleEntry{2.0, "leave", {wait(0.1, "leave")}},
    };
    World w({hero}, {{"hero", glm::vec3(0.0f)}});
    REQUIRE(w.world.startRoutine("hero", w.time));
    w.tick(0.5);
    CHECK(w.completed() == std::vector<std::string>{"wake"});

    REQUIRE(w.world.pauseRoutine("hero", w.time));
    w.tick(5.0);
    // Paused means paused: five seconds of timeline passed and the 1 s and 2 s entries did not
    // fire. A schedule keyed on absolute time would have dumped both.
    CHECK(w.completed() == std::vector<std::string>{"wake"});

    REQUIRE(w.world.resumeRoutine("hero", w.time));
    w.tick(0.8);
    CHECK(w.completed() == std::vector<std::string>{"wake", "wash"});
    w.tick(1.1);
    CHECK(w.completed() == std::vector<std::string>{"wake", "wash", "leave"});
}

TEST_CASE("a paused routine holds the action it was in the middle of", "[entity][schedule]") {
    entity::EntityDesc hero = heroDesc();
    hero.schedule.entries = {entity::ScheduleEntry{0.0, "work", {wait(2.0, "work")}}};
    World w({hero}, {{"hero", glm::vec3(0.0f)}});
    REQUIRE(w.world.startRoutine("hero", w.time));
    w.tick(1.0);
    const double elapsed = w.actor().actions().elapsed(entity::Authority::Routine);
    CHECK_THAT(elapsed, WithinAbs(1.0, 0.05));
    REQUIRE(w.world.pauseRoutine("hero", w.time));
    w.tick(3.0);
    // Not advanced by a second of the three, and not finished either.
    CHECK_THAT(w.actor().actions().elapsed(entity::Authority::Routine), WithinAbs(elapsed, 1e-9));
    CHECK(w.completed().empty());
    REQUIRE(w.world.resumeRoutine("hero", w.time));
    w.tick(1.1);
    CHECK(w.completed() == std::vector<std::string>{"work"});
}

TEST_CASE("a routine that was never started never fires", "[entity][schedule]") {
    // The negative control for the schedule: it must be the *start* that starts it. A schedule
    // that fired because the clock passed its entry times would pass every test above.
    entity::EntityDesc hero = heroDesc();
    hero.schedule.entries = {entity::ScheduleEntry{0.0, "wake", {wait(0.1, "wake")}}};
    World w({hero}, {{"hero", glm::vec3(0.0f)}});
    w.tick(5.0);
    CHECK(w.log.empty());
    CHECK_FALSE(w.actor().schedule().running());
}

TEST_CASE("a director can trigger one step of a routine out of order", "[entity][schedule]") {
    entity::EntityDesc hero = heroDesc();
    hero.schedule.entries = {
        entity::ScheduleEntry{0.0, "wake", {wait(0.1, "wake")}},
        entity::ScheduleEntry{600.0, "sleep", {wait(0.1, "sleep")}},
    };
    World w({hero}, {{"hero", glm::vec3(0.0f)}});
    REQUIRE(w.world.startRoutine("hero", w.time));
    w.tick(0.3);
    REQUIRE(w.actor().schedule().trigger("sleep", w.actor().actions()));
    w.tick(0.3);
    CHECK(w.completed() == std::vector<std::string>{"wake", "sleep"});
    CHECK_FALSE(w.actor().schedule().trigger("brunch", w.actor().actions()));
}

// ---- §10 interactions --------------------------------------------------------------------------

TEST_CASE("a prop's verbs belong to the prop and work for any entity", "[entity][interaction]") {
    // The abstraction test. One bench, two entities that have nothing in common -- different
    // seeds, different clip vocabularies, different names -- and neither the bench nor the action
    // system knows what either of them is.
    entity::EntityDesc bench;
    bench.name = "bench";
    bench.node = "bench";
    bench.properties = {entity::PropertyDesc{"occupiedBy", 0.0f, 0.0f, 4.0f}};
    entity::InteractionDesc sit;
    sit.name = "sit";
    sit.activity = "sit";
    sit.duration = 0.2;
    sit.range = 100.0f;
    sit.exclusive = false;
    sit.onComplete = "bench.satOn";
    bench.interactions = {sit};

    entity::EntityDesc deer = heroDesc("deer");
    deer.clips = {{"idle", "Deer_Idle"}, {"sit", "Deer_Lie"}};
    entity::EntityDesc robot = heroDesc("robot");
    robot.seed = 99;
    robot.clips = {{"idle", "Bot_Standby"}, {"sit", "Bot_Fold"}};
    entity::ActionDesc use = action(entity::ActionKind::Interact, "useBench");
    use.target = interactionTarget("bench", "sit");
    deer.actions = {use};
    robot.actions = {use};

    World w({bench, deer, robot},
            {{"bench", glm::vec3(0.0f)}, {"deer", glm::vec3(1.0f, 0.0f, 0.0f)},
             {"robot", glm::vec3(-1.0f, 0.0f, 0.0f)}});
    w.tick(0.5);
    // The interaction's own completion event comes first and the action's second: the prop
    // finishes what it was doing, then the actor's step is done. Both name themselves, so a
    // listener can tell "the bench was sat on" from "the deer finished its instruction".
    CHECK(w.completed() == std::vector<std::string>{"sit", "useBench", "sit", "useBench"});
    // The same interaction chose a different clip for each, because the *entity* owns the mapping.
    CHECK(w.actor("deer").clipFor("sit") == "Deer_Lie");
    CHECK(w.actor("robot").clipFor("sit") == "Bot_Fold");
}

TEST_CASE("an interaction out of range fails rather than teleporting or walking there",
          "[entity][interaction]") {
    entity::EntityDesc bench;
    bench.name = "bench";
    bench.node = "bench";
    entity::InteractionDesc sit;
    sit.name = "sit";
    sit.duration = 0.1;
    sit.range = 1.0f;
    bench.interactions = {sit};
    entity::EntityDesc hero = heroDesc();
    entity::ActionDesc use = action(entity::ActionKind::Interact, "useBench");
    use.target = interactionTarget("bench", "sit");
    hero.actions = {use};
    World w({bench, hero}, {{"bench", glm::vec3(20.0f, 0.0f, 0.0f)}, {"hero", glm::vec3(0.0f)}});
    w.tick(0.2);
    REQUIRE(w.log.size() == 1);
    CHECK(w.log[0].result == entity::ActionResult::Failed);
    CHECK_THAT(w.log[0].reason, ContainsSubstring("range"));
    // And it did not move: an interaction that walked to its own target would be two systems in one.
    CHECK(glm::length(w.actor().state().position()) < 1e-4f);
}

TEST_CASE("an exclusive interaction takes one user at a time", "[entity][interaction]") {
    const auto run = [](bool exclusive) {
        entity::EntityDesc chair;
        chair.name = "chair";
        chair.node = "chair";
        entity::InteractionDesc sit;
        sit.name = "sit";
        sit.duration = 0.5;
        sit.range = 100.0f;
        sit.exclusive = exclusive;
        chair.interactions = {sit};
        entity::EntityDesc a = heroDesc("a");
        entity::EntityDesc b = heroDesc("b");
        entity::ActionDesc use = action(entity::ActionKind::Interact, "sit");
        use.target = interactionTarget("chair", "sit");
        a.actions = {use};
        b.actions = {use};
        World w({chair, a, b},
                {{"chair", glm::vec3(0.0f)}, {"a", glm::vec3(1.0f, 0.0f, 0.0f)},
                 {"b", glm::vec3(2.0f, 0.0f, 0.0f)}});
        w.tick(1.0);
        int failures = 0;
        for (const entity::ActionEvent& e : w.log) {
            failures += e.result == entity::ActionResult::Failed ? 1 : 0;
        }
        return failures;
    };
    CHECK(run(true) == 1);   // the second one is turned away by name
    CHECK(run(false) == 0);  // and the negative control: a shared verb takes both
}

TEST_CASE("an interaction writes the prop's own state, not the actor's", "[entity][interaction]") {
    entity::EntityDesc door;
    door.name = "door";
    door.node = "door";
    door.properties = {entity::PropertyDesc{"open", 0.0f, 0.0f, 1.0f}};
    entity::InteractionDesc open;
    open.name = "open";
    open.duration = 0.1;
    open.range = 100.0f;
    open.set = {entity::PropertySet{"open", {}, 1.0f}};
    open.conditions = {entity::Condition{"open", {}, entity::Test::Clear, 0.0f}};
    door.interactions = {open};
    entity::EntityDesc hero = heroDesc();
    entity::ActionDesc use = action(entity::ActionKind::Interact, "open");
    use.target = interactionTarget("door", "open");
    hero.actions = {use, use};
    World w({door, hero}, {{"door", glm::vec3(0.0f)}, {"hero", glm::vec3(1.0f, 0.0f, 0.0f)}});
    w.tick(0.5);
    CHECK(w.world.property("door", "open") == 1.0f);
    // The second attempt fails its own condition: the door is already open. A prop whose
    // conditions were never read would open twice and say so twice.
    int failures = 0;
    for (const entity::ActionEvent& e : w.log) {
        failures += e.result == entity::ActionResult::Failed ? 1 : 0;
    }
    CHECK(failures == 1);
}

// ---- §37/§38 equipping ---------------------------------------------------------------------------

TEST_CASE("equipping attaches a node to a socket and turns on a property others can read",
          "[entity][equip]") {
    entity::EntityDesc hero = heroDesc();
    hero.sockets = {socket("head", glm::vec3(0.0f, 1.7f, 0.0f))};
    hero.properties = {entity::PropertyDesc{"headphones", 0.0f, 0.0f, 1.0f}};
    entity::ActionDesc equip = action(entity::ActionKind::Equip, "wearHeadphones");
    equip.target = entityTarget("headphones");
    equip.socket = "head";
    equip.set = {entity::PropertySet{"headphones", {}, 1.0f}};
    equip.onComplete = "hero.equipped";
    hero.actions = {equip};
    World w({hero}, {{"hero", glm::vec3(0.0f)}, {"headphones", glm::vec3(5.0f, 0.0f, 0.0f)}});
    CHECK(w.actor().property("headphones") == 0.0f);
    w.tick(0.2);

    REQUIRE(w.log.size() == 1);
    CHECK(w.log[0].result == entity::ActionResult::Completed);
    CHECK(w.log[0].event == "hero.equipped");
    CHECK(w.actor().property("headphones") == 1.0f);
    REQUIRE(w.actor().attachments().size() == 1);
    CHECK(w.actor().attachments()[0].node == "headphones");
    CHECK(w.actor().attachments()[0].socket == "head");
    // "A property other systems can read" means readable through the ordinary parameter set, which
    // is what makes it a legal reaction, keyframe and modulation target without any of them
    // learning about actions.
    const auto* param = w.params.findAs<float>("entity/hero/state/headphones");
    REQUIRE(param != nullptr);
    CHECK(param->base() == 1.0f);
    // And the prop has actually been moved onto the socket.
    const auto* position = w.params.findAs<glm::vec3>("nodes/headphones/position");
    REQUIRE(position != nullptr);
    CHECK_THAT(position->value().y, WithinAbs(1.7, 1e-4));
}

TEST_CASE("equipping to a socket the entity does not have fails and changes nothing",
          "[entity][equip]") {
    // The negative control for the test above. An `attach` that quietly succeeded against a socket
    // nobody declared would put the prop at the origin and look like a bug in the artwork.
    entity::EntityDesc hero = heroDesc();
    hero.sockets = {socket("head")};
    hero.properties = {entity::PropertyDesc{"headphones", 0.0f, 0.0f, 1.0f}};
    entity::ActionDesc equip = action(entity::ActionKind::Equip, "wearHeadphones");
    equip.target = entityTarget("headphones");
    equip.socket = "antenna";
    equip.set = {entity::PropertySet{"headphones", {}, 1.0f}};
    hero.actions = {equip};
    World w({hero}, {{"hero", glm::vec3(0.0f)}, {"headphones", glm::vec3(5.0f, 0.0f, 0.0f)}});
    w.tick(0.2);
    REQUIRE(w.log.size() == 1);
    CHECK(w.log[0].result == entity::ActionResult::Failed);
    CHECK_THAT(w.log[0].reason, ContainsSubstring("antenna"));
    CHECK(w.actor().property("headphones") == 0.0f);
    CHECK(w.actor().attachments().empty());
}

TEST_CASE("a property an entity did not declare is refused rather than invented", "[entity][equip]") {
    entity::EntityDesc hero = heroDesc();
    World w({hero}, {{"hero", glm::vec3(0.0f)}});
    CHECK_FALSE(w.actor().setProperty("headphones", 1.0f));
    CHECK_FALSE(w.actor().hasProperty("headphones"));
}

TEST_CASE("unequipping puts it down again", "[entity][equip]") {
    entity::EntityDesc hero = heroDesc();
    hero.sockets = {socket("hand")};
    hero.properties = {entity::PropertyDesc{"carrying", 0.0f, 0.0f, 1.0f}};
    entity::ActionDesc equip = action(entity::ActionKind::Equip, "pickUp");
    equip.target = entityTarget("cup");
    equip.socket = "hand";
    equip.set = {entity::PropertySet{"carrying", {}, 1.0f}};
    entity::ActionDesc drop = action(entity::ActionKind::Unequip, "putDown");
    drop.target = entityTarget("cup");
    drop.set = {entity::PropertySet{"carrying", {}, 0.0f}};
    hero.actions = {equip, drop, drop};
    World w({hero}, {{"hero", glm::vec3(0.0f)}, {"cup", glm::vec3(1.0f, 0.0f, 0.0f)}});
    w.tick(0.5);
    CHECK(w.actor().attachments().empty());
    CHECK(w.actor().property("carrying") == 0.0f);
    // Putting down what you are not holding fails by name rather than pretending.
    CHECK(w.log.back().result == entity::ActionResult::Failed);
    CHECK_THAT(w.log.back().reason, ContainsSubstring("cup"));
}

// ---- §4 the worked example ---------------------------------------------------------------------

TEST_CASE("Wake, walk to the nightstand, pick up the headphones, equip them, begin the routine",
          "[entity][action][integration]") {
    // The brief's own example, authored entirely as data. Nothing in the engine knows what a
    // nightstand or a pair of headphones is.
    entity::EntityDesc nightstand;
    nightstand.name = "nightstand";
    nightstand.node = "nightstand";
    nightstand.sockets = {socket("top", glm::vec3(0.0f, 0.6f, 0.0f))};
    entity::InteractionDesc pickUp;
    pickUp.name = "pickUp";
    pickUp.socket = "top";
    pickUp.activity = "pickUp";
    pickUp.duration = 0.4;
    pickUp.range = 1.5f;
    pickUp.onComplete = "nightstand.taken";
    nightstand.interactions = {pickUp};

    entity::EntityDesc hero = heroDesc();
    hero.clips = {{"idle", "Idle"}, {"walk", "Walk"}, {"run", "Run"}, {"wake", "GetUp"},
                  {"pickUp", "Reach"}};
    hero.sockets = {socket("head", glm::vec3(0.0f, 1.7f, 0.0f))};
    hero.properties = {entity::PropertyDesc{"awake", 0.0f, 0.0f, 1.0f},
                       entity::PropertyDesc{"headphones", 0.0f, 0.0f, 1.0f}};

    entity::ActionDesc wake = action(entity::ActionKind::Pose, "wake");
    wake.activity = "wake";
    wake.duration = 0.5;
    wake.set = {entity::PropertySet{"awake", {}, 1.0f}};

    entity::ActionDesc walk = action(entity::ActionKind::Move, "walkTo");
    walk.target = entityTarget("nightstand");
    walk.tolerance = 1.0f;
    walk.speed = 2.0f;

    entity::ActionDesc take = action(entity::ActionKind::Interact, "pickUp");
    take.target = interactionTarget("nightstand", "pickUp");

    entity::ActionDesc equip = action(entity::ActionKind::Equip, "equip");
    equip.target = entityTarget("headphones");
    equip.socket = "head";
    equip.set = {entity::PropertySet{"headphones", {}, 1.0f}};

    entity::ActionDesc begin = action(entity::ActionKind::Set, "beginRoutine");
    begin.onComplete = "routine.begin";

    hero.actions = {wake, walk, take, equip, begin};
    World w({hero, nightstand},
            {{"hero", glm::vec3(0.0f)}, {"nightstand", glm::vec3(6.0f, 0.0f, 0.0f)},
             {"headphones", glm::vec3(6.0f, 0.6f, 0.0f)}});
    StubPath path;
    w.world.setPathProvider(&path);
    w.tick(12.0);

    CHECK(w.completed() ==
          std::vector<std::string>{"wake", "walkTo", "pickUp", "pickUp", "equip", "beginRoutine"});
    CHECK(w.actor().property("awake") == 1.0f);
    CHECK(w.actor().property("headphones") == 1.0f);
    CHECK(w.log.back().event == "routine.begin");
    CHECK(path.routeCalls >= 1); // it went through the navigation seam rather than round it
    // It really walked: five metres of ground, not a teleport.
    CHECK_THAT(w.actor().state().position().x, WithinAbs(5.0, 0.6));
}

// ---- the navigation seam ------------------------------------------------------------------------

TEST_CASE("a walk with no route fails with a reason instead of walking at a wall",
          "[entity][action][navigation]") {
    entity::EntityDesc hero = heroDesc();
    entity::ActionDesc walk = action(entity::ActionKind::Move, "walkTo");
    walk.target = pointTarget(glm::vec3(30.0f, 0.0f, 0.0f));
    hero.actions = {walk, wait(0.1, "next")};
    World w({hero}, {{"hero", glm::vec3(0.0f)}});
    StubPath path;
    path.status = entity::PathStatus::Unreachable;
    w.world.setPathProvider(&path);
    w.tick(0.5);
    REQUIRE(w.log.size() >= 1);
    CHECK(w.log[0].result == entity::ActionResult::Failed);
    CHECK(w.log[0].reason == "unreachable");
    CHECK(glm::length(w.actor().state().position()) < 1e-4f);
    // And the queue carried on: a failure is not a deadlock.
    CHECK(w.completed() == std::vector<std::string>{"next"});
}

TEST_CASE("a planner that needs a moment gets one", "[entity][action][navigation]") {
    // `Pending` is the whole reason the seam is an interface: an asynchronous search must be able
    // to say "not yet" without the character either stopping forever or setting off blind.
    entity::EntityDesc hero = heroDesc();
    entity::ActionDesc walk = action(entity::ActionKind::Move, "walkTo");
    walk.target = pointTarget(glm::vec3(4.0f, 0.0f, 0.0f));
    walk.speed = 2.0f;
    hero.actions = {walk};
    World w({hero}, {{"hero", glm::vec3(0.0f)}});
    StubPath path;
    path.status = entity::PathStatus::Pending;
    w.world.setPathProvider(&path);
    w.tick(0.5);
    CHECK(w.log.empty());
    CHECK(glm::length(w.actor().state().position()) < 1e-4f); // waited, did not set off
    path.status = entity::PathStatus::Ready;
    w.tick(6.0);
    CHECK(w.completed() == std::vector<std::string>{"walkTo"});
}

TEST_CASE("a walk that makes no progress gives up and says why", "[entity][action][navigation]") {
    // A treadmill: routes fine, steers nowhere. Without stuck detection this is a character
    // walking on the spot for the rest of the render with nothing in the log.
    class Treadmill final : public entity::IPathProvider {
    public:
        [[nodiscard]] entity::PathStatus route(glm::vec2, glm::vec2 to,
                                               std::vector<glm::vec2>& out) const override {
            out.clear();
            out.push_back(to);
            return entity::PathStatus::Ready;
        }
        [[nodiscard]] glm::vec2 steer(glm::vec2, glm::vec2, float) const override {
            return glm::vec2(0.0f, 1.0f); // always north, whatever was asked
        }
        [[nodiscard]] float groundHeight(glm::vec2) const override { return 0.0f; }
    };
    entity::EntityDesc hero = heroDesc();
    entity::ActionDesc walk = action(entity::ActionKind::Move, "walkTo");
    walk.target = pointTarget(glm::vec3(30.0f, 0.0f, 0.0f));
    hero.actions = {walk};
    World w({hero}, {{"hero", glm::vec3(0.0f)}});
    Treadmill path;
    w.world.setPathProvider(&path);
    w.tick(10.0);
    REQUIRE(w.log.size() == 1);
    CHECK(w.log[0].result == entity::ActionResult::Failed);
    CHECK(w.log[0].reason == "stuck");
}

TEST_CASE("a directed walk is frame-rate independent", "[entity][action][determinism]") {
    const auto walkFor = [](double hz, double seconds) {
        entity::EntityDesc hero = heroDesc();
        entity::ActionDesc walk = action(entity::ActionKind::Move, "walkTo");
        walk.target = pointTarget(glm::vec3(0.0f, 0.0f, 100.0f));
        walk.speed = 2.0f;
        hero.actions = {walk};
        World w({hero}, {{"hero", glm::vec3(0.0f)}});
        StubPath path;
        w.world.setPathProvider(&path);
        w.tick(seconds, hz);
        return w.actor().state().position().z;
    };
    // Ten seconds at 2 m/s, minus the turn and the ramp. What matters is that 120 Hz and 24 Hz
    // agree; an integration that multiplied by a frame rather than by dt would differ five-fold.
    CHECK_THAT(walkFor(120.0, 10.0), WithinAbs(walkFor(24.0, 10.0), 0.2));
}

// ---- §27 determinism ---------------------------------------------------------------------------

TEST_CASE("an action sequence replays identically from the same cues", "[entity][action][determinism]") {
    const auto play = [](double startAt) {
        entity::EntityDesc hero = heroDesc();
        hero.properties = {entity::PropertyDesc{"awake", 0.0f, 0.0f, 1.0f}};
        entity::ActionDesc wake = action(entity::ActionKind::Pose, "wake");
        wake.activity = "wake";
        wake.duration = 0.4;
        wake.set = {entity::PropertySet{"awake", {}, 1.0f}};
        entity::ActionDesc walk = action(entity::ActionKind::Move, "walkTo");
        walk.target = pointTarget(glm::vec3(0.0f, 0.0f, 8.0f));
        walk.speed = 1.8f;
        hero.schedule.entries = {entity::ScheduleEntry{0.0, "a", {wake}},
                                 entity::ScheduleEntry{0.5, "b", {walk, wait(0.2, "done", "fin")}}};
        World w({hero}, {{"hero", glm::vec3(0.0f)}});
        StubPath path;
        w.world.setPathProvider(&path);
        w.tick(startAt);
        REQUIRE(w.world.startRoutine("hero", w.time));
        w.tick(12.0);
        struct Play {
            std::vector<std::string> names;
            std::vector<double> offsets;
            glm::vec3 end{0.0f};
        };
        Play out;
        for (const entity::ActionEvent& e : w.log) {
            out.names.push_back(e.action);
            out.offsets.push_back(e.time - startAt);
        }
        out.end = w.actor().state().position();
        return out;
    };
    const auto first = play(0.0);
    const auto second = play(0.0);
    CHECK(first.names == second.names);
    CHECK(first.offsets == second.offsets);
    CHECK(first.end.z == second.end.z); // exactly, not nearly
    CHECK(first.names.size() >= 3);

    // And the negative control: started three seconds later, the *same* sequence happens three
    // seconds later. If the schedule were keyed on absolute time this would collapse.
    const auto later = play(3.0);
    CHECK(later.names == first.names);
    for (std::size_t i = 0; i < later.offsets.size(); ++i) {
        CHECK_THAT(later.offsets[i], WithinAbs(first.offsets[i], 1e-9));
    }
}

TEST_CASE("a seek puts intent back to what the scene file said", "[entity][action][determinism]") {
    entity::EntityDesc hero = heroDesc();
    hero.sockets = {socket("head")};
    hero.properties = {entity::PropertyDesc{"headphones", 0.0f, 0.0f, 1.0f}};
    entity::ActionDesc equip = action(entity::ActionKind::Equip, "equip");
    equip.target = entityTarget("headphones");
    equip.socket = "head";
    equip.set = {entity::PropertySet{"headphones", {}, 1.0f}};
    hero.actions = {equip, wait(0.2, "after")};
    World w({hero}, {{"hero", glm::vec3(0.0f)}, {"headphones", glm::vec3(3.0f, 0.0f, 0.0f)}});
    w.tick(1.0);
    CHECK(w.actor().property("headphones") == 1.0f);
    CHECK(w.actor().attachments().size() == 1);

    w.world.reset();
    CHECK(w.actor().property("headphones") == 0.0f);
    CHECK(w.actor().attachments().empty());
    CHECK(w.actor().actions().pending() == 2); // the authored list is back, not gone
    w.log.clear();
    w.tick(1.0);
    CHECK(w.completed() == std::vector<std::string>{"equip", "after"});
}

// ---- §7 gait -------------------------------------------------------------------------------------

TEST_CASE("a character hovering at the walk/run threshold does not flicker", "[entity][gait]") {
    // The defect: a gait chosen on a single threshold changes clip every time the speed crosses
    // it, which for a body steering round obstacles is several times a second.
    //
    // The trace is what a real body does: a speed sitting on the threshold with the jitter that
    // steering and terrain put on it. The jitter is smaller than the band -- larger than the band
    // is not a hover, it is a character genuinely changing pace, and it *should* change gait.
    constexpr auto trace = [](int i) { return 3.0f + (i % 2 == 0 ? 0.25f : -0.25f); };

    entity::GaitSettings settings; // runEnter 3.0, runExit 2.2, minDwell 0.25
    entity::Gait gait;
    for (int i = 0; i < 600; ++i) { // ten seconds at 60 Hz
        gait.select(settings, entity::Activity::Walk, trace(i), 0.0f, 1.0 / 60.0);
    }
    // One change: out of the initial Idle into Run, and then it holds.
    CHECK(gait.changes() == 1);
    CHECK(gait.current() == entity::Activity::Run);

    // The negative control. Collapse the band and the dwell -- which is exactly what selecting on
    // one threshold means -- and the same speed trace changes clip on almost every frame. Without
    // this, the test above would pass against a gait that never changed at all.
    entity::GaitSettings naive;
    naive.runExit = naive.runEnter;
    naive.moveExit = naive.moveEnter;
    naive.minDwell = 0.0f;
    entity::Gait flicker;
    for (int i = 0; i < 600; ++i) {
        flicker.select(naive, entity::Activity::Walk, trace(i), 0.0f, 1.0 / 60.0);
    }
    CHECK(flicker.changes() > 500);
}

TEST_CASE("a gait still changes when the speed really changes", "[entity][gait]") {
    // The other half of the hysteresis contract: a band that never lets go is a latch, and a
    // character that never breaks into a run is as wrong as one that flickers.
    entity::GaitSettings settings;
    entity::Gait gait;
    CHECK(gait.select(settings, entity::Activity::Walk, 0.0f, 0.0f, 1.0 / 60.0) == entity::Activity::Idle);
    for (int i = 0; i < 60; ++i) {
        gait.select(settings, entity::Activity::Walk, 1.5f, 0.0f, 1.0 / 60.0);
    }
    CHECK(gait.current() == entity::Activity::Walk);
    for (int i = 0; i < 60; ++i) {
        gait.select(settings, entity::Activity::Run, 5.0f, 0.0f, 1.0 / 60.0);
    }
    CHECK(gait.current() == entity::Activity::Run);
    for (int i = 0; i < 60; ++i) {
        gait.select(settings, entity::Activity::Idle, 0.0f, 0.0f, 1.0 / 60.0);
    }
    CHECK(gait.current() == entity::Activity::Idle);
}

TEST_CASE("a reaction does not lose the gait it interrupted", "[entity][gait]") {
    // The addendum's §14 at the gait's own scale: Walking -> React -> Walking.
    entity::GaitSettings settings;
    entity::Gait gait;
    for (int i = 0; i < 60; ++i) {
        gait.select(settings, entity::Activity::Walk, 1.5f, 0.0f, 1.0 / 60.0);
    }
    REQUIRE(gait.current() == entity::Activity::Walk);
    const std::uint32_t before = gait.changes();
    for (int i = 0; i < 30; ++i) {
        CHECK(gait.select(settings, entity::Activity::React, 1.5f, 0.0f, 1.0 / 60.0) ==
              entity::Activity::React);
    }
    CHECK(gait.current() == entity::Activity::Walk);
    CHECK(gait.changes() == before);
    CHECK(gait.select(settings, entity::Activity::Walk, 1.5f, 0.0f, 1.0 / 60.0) == entity::Activity::Walk);
}

TEST_CASE("a body accelerates and decelerates rather than snapping", "[entity][gait]") {
    const float half = entity::Gait::approach(0.0f, 4.0f, 6.0f, 8.0f, 1.0 / 60.0);
    CHECK(half > 0.0f);
    CHECK(half < 4.0f);
    CHECK_THAT(half, WithinAbs(0.1, 0.001));
    // Deceleration uses its own limit, and neither overshoots what was asked for.
    CHECK_THAT(entity::Gait::approach(4.0f, 0.0f, 6.0f, 8.0f, 1.0 / 60.0), WithinAbs(3.8667, 0.001));
    CHECK(entity::Gait::approach(1.0f, 1.05f, 6.0f, 8.0f, 1.0) == 1.05f);
    CHECK(entity::Gait::approach(1.0f, 0.95f, 6.0f, 8.0f, 1.0) == 0.95f);
}

TEST_CASE("a directed walk reaches its speed over time, not in one frame", "[entity][gait]") {
    entity::EntityDesc hero = heroDesc();
    entity::ActionDesc walk = action(entity::ActionKind::Move, "walkTo");
    walk.target = pointTarget(glm::vec3(0.0f, 0.0f, 60.0f));
    walk.speed = 4.0f;
    hero.actions = {walk};
    World w({hero}, {{"hero", glm::vec3(0.0f)}});
    StubPath path;
    w.world.setPathProvider(&path);
    w.tick(1.0 / 60.0);
    const float firstFrame = w.actor().state().speed;
    CHECK(firstFrame < 1.0f); // accel 6 m/s^2 for one 60 Hz frame is 0.1 m/s, not 4
    w.tick(2.0);
    CHECK_THAT(w.actor().state().speed, WithinAbs(4.0, 0.05));
    CHECK(w.actor().locomotion().activity == entity::Activity::Run);
}

TEST_CASE("the gait is per entity, because a deer is not a person", "[entity][gait]") {
    entity::GaitSettings deer;
    deer.runEnter = 6.0f;
    deer.runExit = 5.0f;
    entity::Gait a;
    entity::Gait b;
    for (int i = 0; i < 60; ++i) {
        a.select(entity::GaitSettings{}, entity::Activity::Walk, 4.0f, 0.0f, 1.0 / 60.0);
        b.select(deer, entity::Activity::Walk, 4.0f, 0.0f, 1.0 / 60.0);
    }
    CHECK(a.current() == entity::Activity::Run);
    CHECK(b.current() == entity::Activity::Walk);
}

// ---- clip indirection ----------------------------------------------------------------------------

TEST_CASE("an action names an activity and never a clip", "[entity][action][animation]") {
    // The rule ADR-088 set and this layer had to keep. Two entities, the same action, different
    // assets: if an action could name a clip, one of these would be wrong.
    entity::EntityDesc alien = heroDesc("alien");
    alien.clips = {{"idle", "Hover"}, {"sit", "Coil"}};
    entity::EntityDesc robot = heroDesc("robot");
    robot.clips = {{"idle", "Standby"}};
    entity::ActionDesc sit = action(entity::ActionKind::Pose, "sit");
    sit.activity = "sit";
    sit.duration = 1.0;
    alien.actions = {sit};
    robot.actions = {sit};
    World w({alien, robot}, {{"alien", glm::vec3(0.0f)}, {"robot", glm::vec3(2.0f, 0.0f, 0.0f)}});
    w.tick(0.5);
    CHECK(w.actor("alien").locomotion().action == "sit");
    CHECK(w.actor("robot").locomotion().action == "sit");
    CHECK(w.actor("alien").clipFor("sit") == "Coil");
    // The robot has no sit clip, so it falls back to idle rather than freezing in its bind pose.
    CHECK(w.actor("robot").clipFor("sit") == "Standby");
}

// ---- §29/§47 budget ------------------------------------------------------------------------------

TEST_CASE("an entity under orders is not culled by distance", "[entity][action][performance]") {
    entity::EntityDesc hero = heroDesc();
    hero.cullDistance = 10.0f;
    hero.actions = {wait(0.5, "work")};
    World w({hero}, {{"hero", glm::vec3(0.0f)}});
    for (int i = 0; i < 60; ++i) {
        w.params.resetFinals();
        entity::EntityUpdate u;
        w.time += 1.0 / 60.0;
        u.time = w.time;
        u.dt = 1.0 / 60.0;
        u.bus = &w.bus;
        u.viewPosition = glm::vec3(500.0f, 0.0f, 0.0f); // far outside cullDistance
        w.world.update(u, w.params);
    }
    CHECK(w.completed() == std::vector<std::string>{"work"});
    // And once it has nothing to do, it is culled like everything else.
    entity::EntityUpdate u;
    u.time = w.time;
    u.dt = 1.0 / 60.0;
    u.bus = &w.bus;
    u.viewPosition = glm::vec3(500.0f, 0.0f, 0.0f);
    w.params.resetFinals();
    w.world.update(u, w.params);
    CHECK(w.world.counts().skipped == 1);
}

TEST_CASE("a crowd under orders costs microseconds per frame, not milliseconds",
          "[entity][action][performance]") {
    std::vector<entity::EntityDesc> descs;
    std::vector<std::pair<std::string, glm::vec3>> nodes;
    constexpr int kCount = 500;
    for (int i = 0; i < kCount; ++i) {
        entity::EntityDesc d = heroDesc();
        d.name = "e" + std::to_string(i);
        d.node = d.name;
        d.seed = static_cast<std::uint32_t>(i + 1);
        entity::ActionDesc walk = action(entity::ActionKind::Move, "walkTo");
        walk.target = pointTarget(glm::vec3(0.0f, 0.0f, 400.0f));
        walk.speed = 1.5f;
        d.actions = {walk, wait(1.0, "rest")};
        descs.push_back(std::move(d));
        nodes.emplace_back("e" + std::to_string(i), glm::vec3(static_cast<float>(i), 0.0f, 0.0f));
    }
    World w(std::move(descs), std::move(nodes));
    StubPath path;
    w.world.setPathProvider(&path);
    w.world.setActionListener({}); // the log is the test harness, not the system under test
    w.tick(0.5); // warm up: routes taken, vectors sized

    const auto start = std::chrono::steady_clock::now();
    constexpr int kFrames = 120;
    w.tick(static_cast<double>(kFrames) / 60.0);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double msPerFrame =
        std::chrono::duration<double, std::milli>(elapsed).count() / static_cast<double>(kFrames);
    // Measured, and reported when it fails, so a regression says how far it regressed. 500 walking
    // characters is a crowd; the budget is a frame's worth of headroom for everything else.
    INFO("500 entities under orders: " << msPerFrame << " ms/frame");
    CHECK(msPerFrame < 1.0);
    CHECK(w.actor("e0").actions().pending() > 0); // they really were all still working
}

// ---- serialisation ---------------------------------------------------------------------------------

TEST_CASE("intent survives a round trip through the scene file", "[entity][action][serialisation]") {
    const char* json = R"({
      "name": "hero",
      "node": "hero",
      "clips": { "idle": "Idle", "walk": "Walk", "sit": "SitDown" },
      "properties": { "awake": 0, "headphones": 0 },
      "sockets": [ { "name": "head", "position": [0, 1.7, 0] } ],
      "gait": { "walkSpeed": 1.4, "runEnter": 3.5, "runExit": 2.4, "minDwell": 0.3 },
      "actions": [
        { "kind": "pose", "name": "wake", "activity": "wake", "duration": 2.0, "set": { "awake": 1 } },
        { "kind": "walkTo", "name": "toStand", "target": { "entity": "nightstand" }, "tolerance": 1.2 },
        { "kind": "interact", "name": "take", "target": "nightstand.pickUp" },
        { "kind": "equip", "name": "wear", "target": { "node": "headphones" }, "socket": "head",
          "set": { "headphones": 1 }, "onComplete": "hero.equipped" },
        { "kind": "set", "name": "begin", "when": [ { "property": "headphones", "test": "set" } ],
          "otherwise": "wake", "onComplete": "routine.begin" }
      ],
      "schedule": {
        "name": "day", "loop": true, "period": 600,
        "entries": [ { "time": 0, "name": "morning", "actions": [ { "kind": "wait", "duration": 1 } ] } ]
      },
      "interactions": [
        { "name": "pickUp", "socket": "head", "activity": "reach", "duration": 0.5, "range": 1.2,
          "set": { "taken": 1 }, "onComplete": "nightstand.taken" }
      ]
    })";
    auto desc = entity::entityFromJson(nlohmann::json::parse(json));
    REQUIRE(desc.has_value());
    CHECK(desc->actions.size() == 5);
    CHECK(desc->actions[1].kind == entity::ActionKind::Move);
    CHECK(desc->actions[2].target.kind == entity::TargetKind::Interaction);
    CHECK(desc->actions[2].target.name == "nightstand");
    CHECK(desc->actions[2].target.member == "pickUp");
    CHECK(desc->actions[4].otherwise == "wake");
    CHECK(desc->properties.size() == 2);
    CHECK(desc->interactions.size() == 1);
    CHECK(desc->schedule.loop);
    CHECK_THAT(desc->gait.runExit, WithinAbs(2.4, 1e-6));

    const nlohmann::json out = entity::entityToJson(*desc);
    auto again = entity::entityFromJson(out);
    REQUIRE(again.has_value());
    CHECK(entity::entityToJson(*again) == out);
    CHECK(again->actions.size() == desc->actions.size());
    CHECK(again->interactions[0].onComplete == "nightstand.taken");
    CHECK(again->schedule.entries.size() == 1);
}

TEST_CASE("an unknown action kind is reported with the ones that exist", "[entity][diagnostics]") {
    // The project's recurring failure is a name that resolves to nothing, quietly. An action kind
    // nobody implements must be an error at load, with the vocabulary, not a no-op at frame one.
    const auto bad = entity::actionFromJson(nlohmann::json::parse(R"({"kind": "levitate"})"));
    REQUIRE_FALSE(bad.has_value());
    CHECK_THAT(bad.error().message, ContainsSubstring("levitate"));
    CHECK_THAT(bad.error().message, ContainsSubstring("interact"));
}

TEST_CASE("a hysteresis band written upside down is refused", "[entity][diagnostics]") {
    // An exit above an enter is not a band, it is a latch that never releases -- and the symptom
    // is a character that starts running and never stops, which nobody would trace back to a JSON
    // typo.
    const auto bad = entity::gaitFromJson(nlohmann::json::parse(R"({"runEnter": 2.0, "runExit": 3.0})"));
    REQUIRE_FALSE(bad.has_value());
    CHECK_THAT(bad.error().message, ContainsSubstring("runExit"));
    const auto good = entity::gaitFromJson(nlohmann::json::parse(R"({"runEnter": 3.0, "runExit": 2.0})"));
    CHECK(good.has_value());
}

TEST_CASE("an interaction with no verb name is refused", "[entity][diagnostics]") {
    const auto bad = entity::interactionFromJson(nlohmann::json::parse(R"({"duration": 1})"));
    REQUIRE_FALSE(bad.has_value());
    CHECK_THAT(bad.error().message, ContainsSubstring("name"));
    const auto badTarget = entity::targetFromJson(nlohmann::json::parse(R"({"thing": "chair"})"));
    REQUIRE_FALSE(badTarget.has_value());
    CHECK_THAT(badTarget.error().message, ContainsSubstring("interaction"));
}
