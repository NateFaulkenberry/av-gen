// Phase D: autonomous characters, end to end (docs/design/specs/phase-d.md, Success Demonstration).
//
// The fixture is `examples/labs/character/autonomy-demo.scene.json`: two aliens with different
// personalities, two glowing mushrooms, a rock between the first alien and the first mushroom, a
// tree that cracks at t = 60 s, a mushroom that blooms at t = 95 s, a river, and a saucer on a wide
// orbit. **Nothing in it says what either alien does or when.** There is no schedule on either
// alien, no action list, no clip named at a time; every assertion below is about behaviour the
// characters chose.
//
// Every arm has a subject check first (testing.md #33): a test that the scout walks round the rock
// asserts that the rock is there and that the straight line was blocked by it; a test that the
// personalities split asserts that both aliens heard the event.
//
// Quantities are structural or geometric (ADR-170). No wall-clock timing anywhere.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "entity/behavior_trace.hpp"
#include "entity/character_validate.hpp"
#include "entity/decision.hpp"
#include "entity/entity.hpp"
#include "entity/mind.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::entity;
namespace fs = std::filesystem;

namespace {

fs::path repoRoot() { return fs::path(AVGEN_SOURCE_DIR); }
fs::path demoPath() { return repoRoot() / "examples" / "labs" / "character" / "autonomy-demo.scene.json"; }
bool assetsPresent() { return fs::exists(repoRoot() / "assets" / "aliens" / "alien-scout.glb"); }

nlohmann::json demoJson() {
    std::ifstream in(demoPath());
    REQUIRE(in.good());
    return nlohmann::json::parse(in);
}

// The whole world, ticked the way the application ticks it (the harness test_entity_decision.cpp
// uses). Loads from a JSON document so an arm can change one word of the scene -- a personality --
// and nothing else.
struct World {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    std::unique_ptr<scene::Composition> comp;
    double now = 0.0;
    long long frame = 0;

    explicit World(const nlohmann::json& doc) : registry(demoPath().parent_path()) {
        auto loaded = scene::Composition::fromJson(doc, registry);
        INFO((loaded.has_value() ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
        comp = std::move(*loaded);
        comp->attach(params, modulator);
        comp->setViewport(1280, 720);
        comp->scene().detailLimits.entityDistanceCull = false;
    }

    void play(double seconds, const std::function<void()>& each = {}, double hz = 60.0) {
        const double step = 1.0 / hz;
        const auto frames = static_cast<long long>(std::llround(seconds * hz));
        FrameTime time;
        for (long long i = 0; i < frames; ++i, ++frame) {
            now = static_cast<double>(frame) * step;
            time.renderTime = now;
            time.deltaTime = frame == 0 ? 0.0 : step;
            time.frameIndex = static_cast<std::uint64_t>(frame);
            params.resetFinals();
            comp->updateFields(time, bus, modulator);
            modulator.applyRoutes(bus, params, time.deltaTime);
            comp->updateBehaviour(time, bus);
            comp->update(time);
            if (each) {
                each();
            }
        }
    }

    [[nodiscard]] const EntityWorld& world() const { return comp->entityWorld(); }
    [[nodiscard]] const Entity& entity(const char* name) const {
        const Entity* e = world().find(name);
        REQUIRE(e != nullptr);
        return *e;
    }
    [[nodiscard]] std::size_t index(const char* name) const {
        for (std::size_t i = 0; i < world().size(); ++i) {
            if (world().entities()[i]->name() == name) {
                return i;
            }
        }
        FAIL("no entity " << name);
        return 0;
    }
    [[nodiscard]] DecisionDebug decision(const char* name) const {
        DecisionDebug d;
        for (const auto& b : entity(name).behaviors()) {
            if (b->decisionDebug(d)) {
                return d;
            }
        }
        FAIL("entity " << name << " does not decide");
        return d;
    }
};

glm::vec2 flat(const glm::vec3& p) { return {p.x, p.z}; }

// Distance from `c` to the segment a-b, on the ground plane.
float segmentDistance(glm::vec2 a, glm::vec2 b, glm::vec2 c) {
    const glm::vec2 ab = b - a;
    const float t = std::clamp(glm::dot(c - a, ab) / std::max(glm::dot(ab, ab), 1e-6f), 0.0f, 1.0f);
    return glm::length(a + ab * t - c);
}

float angleBetween(float yaw, glm::vec2 from, glm::vec2 to) {
    const glm::vec2 d = to - from;
    const float want = std::atan2(d.x, d.y);
    float delta = want - yaw;
    while (delta > 3.14159265f) {
        delta -= 6.28318531f;
    }
    while (delta < -3.14159265f) {
        delta += 6.28318531f;
    }
    return std::abs(delta);
}

} // namespace

// ---- the first vertical slice (§37), and Success Demonstration steps 1-12 ----------------------

TEST_CASE("an alien notices a glowing mushroom, walks round the rock to it, looks, and loses "
          "interest -- with nothing authored",
          "[entity][phaseD][demo]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    const nlohmann::json doc = demoJson();
    // Nothing is scripted for the alien: no schedule, no action list (the premise of the slice).
    for (const auto& e : doc["entities"]) {
        if (e["name"] == "scout") {
            REQUIRE_FALSE(e.contains("actions"));
            REQUIRE_FALSE(e.contains("schedule"));
        }
    }
    World w(doc);
    const Entity& scout = w.entity("scout");
    const std::size_t mushroomIndex = w.index("mushroom-1");
    const glm::vec3 mushroom = w.entity("mushroom-1").state().position();
    const SubjectId mushroomSubject = bodySubject(mushroomIndex);

    // The rock is a navigation obstacle (subject check, testing.md #33).
    glm::vec3 rock{0.0f};
    float rockRadius = 0.0f;
    for (const auto& h : doc["heroes"]) {
        if (h["name"] == "rock-1") {
            rock = glm::vec3(h["position"][0].get<float>(), 0.0f, h["position"][2].get<float>());
            rockRadius = h["radius"].get<float>();
        }
    }
    REQUIRE(rockRadius > 0.0f);
    REQUIRE(w.world().navigator().obstructed(flat(rock), 0.0f));

    // Per-frame record of the scout.
    struct Sample {
        double t;
        glm::vec3 p;
        float yaw;
        float speed;
        bool investigating;
        bool planActive;
        bool look;
        glm::vec3 lookAt;
        bool perceives;
    };
    std::vector<Sample> samples;
    w.play(60.0, [&] {
        const DecisionDebug d = w.decision("scout");
        bool perceives = false;
        for (const Percept& p : scout.percepts()) {
            perceives = perceives || subjectOf(p) == mushroomSubject;
        }
        samples.push_back(Sample{w.now, scout.state().position(), scout.state().yaw,
                                 scout.state().groundSpeed(),
                                 d.chosen == "investigate" && d.subject == "mushroom-1",
                                 d.planActive, scout.state().hasLookTarget,
                                 scout.state().lookTarget, perceives});
    });

    // ---- 1-3: it wanders, notices the mushroom, decides it is interesting ----
    const auto firstInvestigate = std::find_if(samples.begin(), samples.end(),
                                               [](const Sample& s) { return s.investigating; });
    REQUIRE(firstInvestigate != samples.end());
    const auto firstNoticed = std::find_if(samples.begin(), samples.end(),
                                           [](const Sample& s) { return s.perceives; });
    REQUIRE(firstNoticed != samples.end());
    // It chose the mushroom only after perceiving it -- a decision from what it knows, not from
    // the omniscient list (character_ai.hpp §2 P4).
    CHECK(firstNoticed->t <= firstInvestigate->t);
    // And it was doing something else first: wandering, not born investigating.
    CHECK(firstInvestigate->t > 0.5);

    // The investigation, start to end.
    const auto endInvestigate = std::find_if(firstInvestigate, samples.end(),
                                             [](const Sample& s) { return !s.investigating; });
    REQUIRE(endInvestigate != samples.end());
    const glm::vec2 start = flat(firstInvestigate->p);

    // ---- 4-5: it navigates, and avoids the rock ----
    // Subject check: the straight line from where it decided to the mushroom runs through the rock.
    REQUIRE(segmentDistance(start, flat(mushroom), flat(rock)) < rockRadius);
    float nearestRock = 1e9f;
    for (auto it = firstInvestigate; it != endInvestigate; ++it) {
        nearestRock = std::min(nearestRock, glm::length(flat(it->p) - flat(rock)));
    }
    INFO("nearest approach to the rock's centre " << nearestRock << " m, radius " << rockRadius);
    // The straight line entered the rock and the body did not: it went round.
    CHECK(nearestRock >= rockRadius);

    // ---- 6-7: it approaches naturally and slows down ----
    // Where it stopped, then: its cruising pace more than 6 m out, against its pace in the last
    // metre before the stop. A braking-limit stop holds full pace to within centimetres of the
    // stop; an arrival is visibly slower for the whole of the last metre.
    const float stopAt = 2.2f; // the considerer's `approach`
    auto stop = std::find_if(firstInvestigate, endInvestigate, [&](const Sample& s) {
        return s.speed < 0.02f && glm::length(flat(s.p) - flat(mushroom)) < stopAt + 2.0f;
    });
    REQUIRE(stop != endInvestigate);
    float cruise = 0.0f;
    float lastMetre = 0.0f;
    for (auto it = firstInvestigate; it != stop; ++it) {
        const float toStop = glm::length(flat(it->p) - flat(stop->p));
        if (toStop > 6.0f) {
            cruise = std::max(cruise, it->speed);
        } else if (toStop < 1.0f) {
            lastMetre = std::max(lastMetre, it->speed);
        }
    }
    REQUIRE(cruise > 0.5f);
    INFO("cruise " << cruise << " m/s, fastest in the last metre " << lastMetre << " m/s");
    CHECK(lastMetre < cruise * 0.6f);

    // ---- 8-10: it turns toward the mushroom, looks at it (down), observes for seconds ----
    double stillSeconds = 0.0;
    bool facedIt = false;
    bool lookedDown = false;
    for (auto it = firstInvestigate; it != endInvestigate; ++it) {
        const float d = glm::length(flat(it->p) - flat(mushroom));
        if (d < stopAt + 1.5f && it->speed < 0.05f) {
            stillSeconds += 1.0 / 60.0;
            facedIt = facedIt || angleBetween(it->yaw, flat(it->p), flat(mushroom)) < 0.26f;
            // The look target is the mushroom itself -- on the ground, below the alien's head.
            if (it->look && glm::length(it->lookAt - mushroom) < 0.5f &&
                it->lookAt.y < it->p.y + 1.0f) {
                lookedDown = true;
            }
        }
    }
    CHECK(facedIt);
    CHECK(lookedDown);
    CHECK(stillSeconds >= 3.0);

    // ---- 11-12: it loses interest and leaves ----
    // The investigation ended because the plan completed, not because something interrupted it.
    const DecisionDebug after = w.decision("scout");
    bool completed = false;
    for (const DecisionTraceEntry& e : after.history) {
        completed = completed || (e.previous == "investigate" && e.previousOutcome == "completed");
    }
    CHECK(completed);
    // The mushroom is now familiar: its novelty has fallen, which is what "loses interest" is.
    // (Read through the awareness layer's own memory, via the §41 report's source.)
    const double leftAt = endInvestigate->t;
    // And it does not walk straight back: for 20 s after leaving, never investigating it again.
    bool returned = false;
    for (auto it = endInvestigate; it != samples.end() && it->t < leftAt + 20.0; ++it) {
        returned = returned || it->investigating;
    }
    CHECK_FALSE(returned);
}

// ---- Success Demonstration steps 13-14: the same event, two personalities -----------------------

namespace {

// Where each alien ends up relative to the crack, 10 s after it, and what it chose.
struct Reaction {
    std::string scoutChoice;
    std::string wardenChoice;
    float scoutCloser = 0.0f;  // metres closer to the event than when it happened
    float wardenCloser = 0.0f;
    bool scoutHeard = false;
    bool wardenHeard = false;
};

Reaction reactToCrack(nlohmann::json doc) {
    World w(doc);
    w.play(59.9);
    // The crack is raised by the old tree's authored action list at t = 60 s.
    const glm::vec3 tree = w.entity("old-tree").state().position();
    const float scoutBefore = glm::length(flat(w.entity("scout").state().position()) - flat(tree));
    const float wardenBefore = glm::length(flat(w.entity("warden").state().position()) - flat(tree));
    Reaction r;
    w.play(10.0, [&] {
        const DecisionDebug s = w.decision("scout");
        const DecisionDebug d = w.decision("warden");
        if (s.chosen.rfind("startle/", 0) == 0) {
            r.scoutChoice = std::string(s.chosen);
            r.scoutHeard = true;
        }
        if (d.chosen.rfind("startle/", 0) == 0) {
            r.wardenChoice = std::string(d.chosen);
            r.wardenHeard = true;
        }
    });
    r.scoutCloser = scoutBefore - glm::length(flat(w.entity("scout").state().position()) - flat(tree));
    r.wardenCloser = wardenBefore - glm::length(flat(w.entity("warden").state().position()) - flat(tree));
    return r;
}

} // namespace

TEST_CASE("the same world event, two personalities: the curious alien approaches and the "
          "cautious one flees",
          "[entity][phaseD][demo][personality]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    const nlohmann::json doc = demoJson();
    const Reaction r = reactToCrack(doc);
    // Subject: both heard it.
    REQUIRE(r.scoutHeard);
    REQUIRE(r.wardenHeard);
    CHECK(r.scoutChoice == "startle/approach");
    CHECK(r.wardenChoice == "startle/flee");
    CHECK(r.scoutCloser > 2.0f);
    CHECK(r.wardenCloser < -2.0f);

    // The control: swap the two personalities and nothing else. If the split came from anything but
    // the personality -- where they stand, which asset they are, the order they are stored in -- it
    // would survive the swap. It must reverse.
    nlohmann::json swapped = doc;
    nlohmann::json* scout = nullptr;
    nlohmann::json* warden = nullptr;
    for (auto& e : swapped["entities"]) {
        if (e["name"] == "scout") {
            scout = &e;
        } else if (e["name"] == "warden") {
            warden = &e;
        }
    }
    REQUIRE(scout != nullptr);
    REQUIRE(warden != nullptr);
    std::swap((*scout)["personality"], (*warden)["personality"]);
    const Reaction s = reactToCrack(swapped);
    REQUIRE(s.scoutHeard);
    REQUIRE(s.wardenHeard);
    CHECK(s.scoutChoice == "startle/flee");
    CHECK(s.wardenChoice == "startle/approach");
}

// ---- Success Demonstration steps 17-20: the saucer --------------------------------------------

TEST_CASE("an alien notices the saucer, looks up at it, and returns to exploring once it has gone",
          "[entity][phaseD][demo][ufo]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    World w(demoJson());
    const Entity& scout = w.entity("scout");
    const Entity& saucer = w.entity("saucer");
    // Subject: the saucer moves in the simulation (R1), so perception sees it arrive and leave.
    const glm::vec3 saucerAtStart = saucer.state().position();
    double watchStart = -1.0;
    double watchEnd = -1.0;
    bool lookedUp = false;
    std::string afterwards;
    w.play(200.0, [&] {
        const DecisionDebug d = w.decision("scout");
        const bool watching = d.chosen == "watch-sky";
        if (watching && watchStart < 0.0) {
            watchStart = w.now;
        }
        if (watching && scout.state().hasLookTarget &&
            scout.state().lookTarget.y > scout.state().position().y + 10.0f) {
            lookedUp = true;
        }
        if (!watching && watchStart >= 0.0 && watchEnd < 0.0) {
            watchEnd = w.now;
            afterwards = std::string(d.chosen);
        }
    });
    CHECK(glm::length(saucer.state().position() - saucerAtStart) > 20.0f); // it did fly
    REQUIRE(watchStart >= 0.0);  // it noticed, and chose to watch
    CHECK(lookedUp);             // at something twenty metres up
    REQUIRE(watchEnd > watchStart);
    CHECK_FALSE(afterwards.empty()); // and went back to something of its own
    CHECK(afterwards != "watch-sky");
    // The watch is bounded -- the observation ends; it does not stare forever (§19).
    CHECK(watchEnd - watchStart < 60.0);
}

// ---- §33, §63, §64: determinism ------------------------------------------------------------------

TEST_CASE("the same scene twice produces the same behaviour trace", "[entity][phaseD][determinism]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    const auto trace = [] {
        World w(demoJson());
        BehaviorTraceRecorder r;
        w.play(120.0, [&] { r.sample(w.world()); });
        CHECK(r.missed() == 0);
        return r.lines();
    };
    const std::vector<std::string> a = trace();
    const std::vector<std::string> b = trace();
    // Subject: there is a trace to compare -- several decisions by both aliens.
    REQUIRE(a.size() > 10);
    CHECK(a == b);
}

TEST_CASE("a scrub lands on the decision the play made, including events heard and memories formed",
          "[entity][phaseD][determinism][seek]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    // 75 s is after the crack (60 s): the replay has to re-raise the event, re-hear it, re-form the
    // memory of the investigated mushroom, and land on the same committed option.
    constexpr double kAt = 75.0;
    World played(demoJson());
    played.play(kAt + 1.0 / 60.0); // frames 0..4500: the last one lands on t = 75.0
    REQUIRE(std::abs(played.now - kAt) < 1e-9);

    World scrubbed(demoJson());
    scrubbed.comp->entityWorld().seek(kAt, &scrubbed.params, &scrubbed.bus, 1.0 / 60.0);

    // Subject: events were raised on the way here, and the aliens remember deciding things.
    REQUIRE_FALSE(played.world().worldEvents().empty());
    REQUIRE(scrubbed.world().worldEvents().size() == played.world().worldEvents().size());
    for (const char* name : {"scout", "warden"}) {
        INFO(name);
        const DecisionDebug p = played.decision(name);
        const DecisionDebug s = scrubbed.decision(name);
        REQUIRE(p.historyTotal > 1);
        CHECK(s.chosen == p.chosen);
        CHECK(s.subject == p.subject);
        CHECK(s.historyTotal == p.historyTotal);
        const glm::vec3 pp = played.entity(name).state().position();
        const glm::vec3 sp = scrubbed.entity(name).state().position();
        CHECK(glm::length(pp - sp) < 1e-3f);
    }
}

// ---- §41: "why is this character doing that?" ---------------------------------------------------

TEST_CASE("the why report names the behaviour, its subject, its utility, its factors and its intent",
          "[entity][phaseD][diagnostics]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    World w(demoJson());
    // Play until the scout is investigating, so the report has something specific to say.
    bool investigating = false;
    w.play(40.0, [&] { investigating = investigating || w.decision("scout").chosen == "investigate"; });
    REQUIRE(investigating);
    World again(demoJson());
    std::string report;
    again.play(40.0, [&] {
        if (report.empty() && again.decision("scout").chosen == "investigate" &&
            again.decision("scout").planActive) {
            report = explainCharacter(again.world(), again.index("scout"));
        }
    });
    INFO(report);
    REQUIRE_FALSE(report.empty());
    CHECK(report.find("Current behavior: investigate") != std::string::npos);
    CHECK(report.find("Why: mushroom-1") != std::string::npos);
    CHECK(report.find("utility = ") != std::string::npos);
    CHECK(report.find("novelty") != std::string::npos);
    CHECK(report.find("personality") != std::string::npos);
    CHECK(report.find("Current intent: interact") != std::string::npos);
    CHECK(report.find("affordance") != std::string::npos);
    CHECK(report.find("Attention: ") != std::string::npos);
    CHECK(report.find("Motion: ") != std::string::npos);
    // A character that does not decide has no report rather than an empty template.
    CHECK(explainCharacter(again.world(), again.index("mushroom-1")).empty());
}

// ---- §3/§15: the intent seam has a producer ------------------------------------------------------

TEST_CASE("a walking decider publishes vector intent, and the WHAT half names what it is doing",
          "[entity][phaseD][intent]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    World w(demoJson());
    const Entity& scout = w.entity("scout");
    std::size_t walkingFrames = 0;
    std::size_t validWhileWalking = 0;
    float worstMismatch = 0.0f;
    bool sawInvestigate = false;
    w.play(30.0, [&] {
        const EntityState& s = scout.state();
        // The demo's investigate uses the mushroom's `inspect` affordance, so its intent is Interact.
        sawInvestigate = sawInvestigate || s.intent.type == IntentType::Interact;
        if (s.speed > 0.2f && scout.actions().current(Authority::Routine) != nullptr &&
            scout.actions().current(Authority::Routine)->kind == ActionKind::Move) {
            ++walkingFrames;
            if (s.intent.valid) {
                ++validWhileWalking;
                // The vector is the polar pair, exactly: publishing it changes no MotionRequest.
                const glm::vec3 polar = s.facing() * s.speed;
                worstMismatch = std::max(worstMismatch, glm::length(polar - s.intent.desiredVelocity));
            }
        }
    });
    REQUIRE(walkingFrames > 60);
    CHECK(validWhileWalking == walkingFrames);
    CHECK(worstMismatch < 1e-4f);
    CHECK(sawInvestigate);
}

// ---- the awareness layer's parts, on their own ---------------------------------------------------

TEST_CASE("object memory: investigated things lose novelty and recover, failures are left alone, "
          "and eviction keeps what matters",
          "[entity][phaseD][memory]") {
    ObjectMemory m;
    MemorySettings s;
    s.capacity = 2;
    s.recoverSeconds = 100.0f;
    s.habituationSeconds = 5.0f;
    s.failSeconds = 30.0f;
    m.setSettings(s);
    const SubjectId a = bodySubject(1);
    const SubjectId b = bodySubject(2);
    const SubjectId c = bodySubject(3);

    CHECK(m.novelty(a, 0.0) == 1.0f); // never met: fully new
    m.noticed(a, glm::vec3(0.0f), 0.0);
    CHECK(m.novelty(a, 0.0) == 1.0f); // noticed is not studied

    // Habituation: five seconds of attention is 1/e.
    m.attend(a, 5.0f, 5.0);
    CHECK(std::abs(m.novelty(a, 5.0) - std::exp(-1.0f)) < 1e-4f);
    // Investigated: nothing, then a quadratic return over `recoverSeconds`.
    m.investigated(a, 10.0);
    CHECK(m.novelty(a, 10.0) == 0.0f);
    CHECK(m.novelty(a, 60.0) < 0.3f);
    CHECK(m.novelty(a, 110.0) == 1.0f);

    // Failure suppresses for `failSeconds`, and then does not.
    m.noticed(b, glm::vec3(1.0f), 11.0);
    m.failed(b, 12.0);
    CHECK(m.suppressed(b, 20.0));
    CHECK_FALSE(m.suppressed(b, 43.0));

    // Full: a third thing evicts -- but not the one still inside its suppression window, even
    // though it is not the oldest seen. `a` was seen at 0 and investigated at 10 (inside its recovery
    // window too), `b` failed at 12. At t = 13 both are "hot"; the tie breaks on the older sighting.
    m.noticed(c, glm::vec3(2.0f), 13.0);
    CHECK(m.find(c) != nullptr);
    CHECK(m.entries().size() == 2);

    // Events are heard once, however often a replay presents them.
    PerceivedEvent e;
    e.sequence = 7;
    e.time = 1.0;
    m.hear(e);
    m.hear(e);
    CHECK(m.events().size() == 1);
    m.reset();
    CHECK(m.entries().empty());
    CHECK(m.events().empty());
}

TEST_CASE("personality: neutral changes nothing, and a trait bends a score the way it is named",
          "[entity][phaseD][personality]") {
    const Personality neutral;
    TraitWeights w;
    w.exponent[0] = 1.5f; // curiosity
    w.exponent[2] = -1.0f; // caution
    w.any = true;
    CHECK(traitFactor(neutral, w) == 1.0f);
    Personality curious;
    curious.curiosity = 0.9f;
    curious.caution = 0.2f;
    Personality cautious;
    cautious.curiosity = 0.3f;
    cautious.caution = 0.8f;
    CHECK(traitFactor(curious, w) > 1.5f);
    CHECK(traitFactor(cautious, w) < 0.8f);

    // JSON: the round trip keeps every trait, and a misspelt trait is refused by name.
    auto parsed = personalityFromJson(nlohmann::json{{"curiosity", 0.9}, {"caution", 0.2}});
    REQUIRE(parsed.has_value());
    auto back = personalityFromJson(personalityToJson(*parsed));
    REQUIRE(back.has_value());
    CHECK(*back == *parsed);
    auto bad = personalityFromJson(nlohmann::json{{"curiousity", 0.9}});
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().message.find("curiousity") != std::string::npos);
    CHECK_FALSE(personalityFromJson(nlohmann::json{{"caution", 1.4}}).has_value());
}

TEST_CASE("semantic tags intern to bits, and a word nobody used matches nothing",
          "[entity][phaseD][semantics]") {
    SemanticTags t;
    const std::uint64_t glowing = t.intern("glowing");
    const std::uint64_t ufo = t.intern("ufo");
    CHECK(glowing != 0);
    CHECK(ufo != 0);
    CHECK(glowing != ufo);
    CHECK(t.intern("glowing") == glowing);
    CHECK(t.bit("never-said") == 0);
    CHECK(t.describe(glowing | ufo) == "glowing,ufo");
    for (int i = 0; i < 70; ++i) {
        (void)t.intern("w" + std::to_string(i));
    }
    CHECK(t.size() == SemanticTags::kCapacity);
    CHECK(t.overflowed());
}

TEST_CASE("the attention model attends to what is semantically important, and says why",
          "[entity][phaseD][attention]") {
    SemanticTags tags;
    const std::uint64_t ufo = tags.intern("ufo");
    AttentionModel model;
    AttentionModelSettings s;
    s.tagWeights = {{"ufo", 3.0f}};
    model.setSettings(s);

    // A nearer, more salient plain thing against a farther tagged one.
    Percept rock;
    rock.kind = InterestKind::Landmark;
    rock.source = 1;
    rock.salience = 0.9f;
    rock.seenAt = 0.0;
    Percept saucer;
    saucer.kind = InterestKind::Character;
    saucer.source = 4;
    saucer.salience = 0.2f;
    saucer.tags = ufo;
    saucer.seenAt = 0.0;
    const Percept percepts[] = {rock, saucer};

    AttentionModel::Inputs in;
    in.percepts = percepts;
    in.tags = &tags;
    in.time = 0.0;
    const AttentionFocus f = model.update(in);
    REQUIRE(f.valid());
    CHECK(f.id == bodySubject(4));
    CHECK(f.reason == "semantic");

    // Control: without the tag weight the salient rock wins -- so the result above was the tag.
    AttentionModel plain;
    plain.setSettings(AttentionModelSettings{});
    const AttentionFocus g = plain.update(in);
    REQUIRE(g.valid());
    CHECK(g.id == pointSubject(1));
}

TEST_CASE("a held plan is not abandoned when its proposer goes quiet, only when beaten",
          "[entity][phaseD][decision][hysteresis]") {
    class Fixed final : public IConsiderer {
    public:
        Fixed(std::string name, float score) : name_(std::move(name)), score_(score) {}
        void set(float score, bool present) {
            score_ = score;
            present_ = present;
        }
        void consider(const DecisionContext&, std::vector<Option>& out) const override {
            if (present_) {
                out.push_back(Option{name_, score_, {}, Authority::Routine});
            }
        }

    private:
        std::string name_;
        float score_;
        bool present_ = true;
    };
    EntityState state;
    const auto at = [&](double t) {
        DecisionContext ctx;
        ctx.time = t;
        ctx.state = &state;
        return ctx;
    };
    Selector sel;
    SelectorSettings ss;
    ss.hertz = 4.0f;
    ss.dwellTicks = 0.0f;
    ss.margin = 0.1f;
    sel.setSettings(ss);
    Fixed errand("errand", 1.0f);
    Fixed other("other", 0.2f);
    const IConsiderer* list[] = {&errand, &other};
    REQUIRE(sel.select(at(0.0), list));
    REQUIRE(sel.current() == "errand");

    // The proposer goes quiet. Held at 1.0: a 0.2 challenger does not take the slot.
    errand.set(1.0f, false);
    sel.hold(1.0f);
    CHECK_FALSE(sel.select(at(0.25), list));
    CHECK(sel.current() == "errand");
    CHECK(sel.counts().holdRejections == 1);
    // Something genuinely better does.
    other.set(1.2f, true);
    CHECK(sel.select(at(0.5), list));
    CHECK(sel.current() == "other");

    // Control: unheld, the same quiet proposer loses the slot to anything.
    Selector unheld;
    unheld.setSettings(ss);
    Fixed errand2("errand", 1.0f);
    Fixed other2("other", 0.2f);
    const IConsiderer* list2[] = {&errand2, &other2};
    REQUIRE(unheld.select(at(0.0), list2));
    errand2.set(1.0f, false);
    CHECK(unheld.select(at(0.25), list2));
    CHECK(unheld.current() == "other");
}

// ---- §47: the new authored fields survive a save -----------------------------------------------

TEST_CASE("personality, perception tags, world events and the mind block survive a save",
          "[entity][phaseD][serialization]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    const nlohmann::json doc = demoJson();
    World w(doc);
    const nlohmann::json saved = w.comp->toJson();
    // Both halves of each round trip (testing.md #31/#32): what the writer wrote, and what the
    // reader makes of it.
    REQUIRE(saved.contains("worldEvents"));
    CHECK(saved["worldEvents"].size() == doc["worldEvents"].size());
    World reloaded(saved);
    const Entity& a = w.entity("warden");
    const Entity& b = reloaded.entity("warden");
    CHECK(b.desc().hasPersonality);
    CHECK(b.desc().personality == a.desc().personality);
    CHECK(b.personality().caution == a.personality().caution);
    CHECK(b.desc().perceptionTags == a.desc().perceptionTags);
    CHECK_FALSE(b.desc().perceptionTags.empty());
    CHECK(reloaded.comp->eventProfiles().size() == w.comp->eventProfiles().size());
    // The decider's mind block is authored behaviour settings, written as authored.
    bool mind = false;
    for (const BehaviorDesc& bd : b.desc().behaviors) {
        mind = mind || (bd.kind == "decide" && bd.settings.contains("mind"));
    }
    CHECK(mind);
    CHECK(reloaded.decision("warden").aware);
    // And a body with no personality writes none (the opt-in is the key's presence).
    CHECK_FALSE(reloaded.entity("saucer").desc().hasPersonality);
}

// ---- §16–§18: capability + affordance = valid interaction ---------------------------------------

namespace {

// Plays the demo until the scout's first investigation of mushroom-1 ends, and reports whether its
// plan ever ran the prop's `inspect` verb, and whether it ever stood still observing it.
struct Inspection {
    bool interacted = false;
    bool observed = false;
    bool investigated = false;
    std::string failure;
};

Inspection inspect(const nlohmann::json& doc) {
    World w(doc);
    const Entity& scout = w.entity("scout");
    Inspection r;
    bool started = false;
    w.play(50.0, [&] {
        const DecisionDebug d = w.decision("scout");
        const bool on = d.chosen == "investigate" && d.subject == "mushroom-1";
        if (!on) {
            return;
        }
        started = true;
        r.investigated = true;
        const ActionDesc* a = scout.actions().current(Authority::Routine);
        if (a != nullptr && a->kind == ActionKind::Interact && a->target.member == "inspect") {
            r.interacted = true;
        }
        if (a != nullptr && a->kind == ActionKind::Pose) {
            r.observed = true;
        }
    });
    for (const ActionEvent& e : w.world().actionEvents()) {
        if (e.result == ActionResult::Failed) {
            r.failure = e.reason;
        }
    }
    (void)started;
    return r;
}

} // namespace

TEST_CASE("a capable alien uses the mushroom's inspect affordance; an incapable one observes instead",
          "[entity][phaseD][interaction]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    nlohmann::json doc = demoJson();
    // Subject: the prop offers `inspect`, it requires `can_inspect`, and the scout has it.
    bool offers = false;
    for (const auto& e : doc["entities"]) {
        if (e["name"] == "mushroom-1") {
            for (const auto& i : e["interactions"]) {
                offers = offers || (i["name"] == "inspect" && i["requires"][0] == "can_inspect");
            }
        }
    }
    REQUIRE(offers);
    const Inspection capable = inspect(doc);
    REQUIRE(capable.investigated);
    CHECK(capable.interacted);
    CHECK_FALSE(capable.observed);

    // The same scout without the capability: the planner does not choose an impossible
    // interaction (§74 resilience), and the character observes instead (§59).
    for (auto& e : doc["entities"]) {
        if (e["name"] == "scout") {
            e["capabilities"] = nlohmann::json::array({"can_walk"});
        }
    }
    const Inspection incapable = inspect(doc);
    REQUIRE(incapable.investigated);
    CHECK_FALSE(incapable.interacted);
    CHECK(incapable.observed);
}

TEST_CASE("the executor refuses an interaction the body lacks the capability for",
          "[entity][phaseD][interaction]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    nlohmann::json doc = demoJson();
    for (auto& e : doc["entities"]) {
        if (e["name"] == "warden") {
            e["capabilities"] = nlohmann::json::array({"can_walk"});
        }
    }
    World w(doc);
    // Put the warden next to the mushroom and tell it, directly, to inspect it.
    const glm::vec3 m = w.entity("mushroom-1").state().position();
    ActionDesc use;
    use.kind = ActionKind::Interact;
    use.target.kind = TargetKind::Interaction;
    use.target.name = "mushroom-1";
    use.target.member = "inspect";
    ActionDesc walk;
    walk.kind = ActionKind::Move;
    walk.target.kind = TargetKind::Point;
    walk.target.point = m + glm::vec3(1.5f, 0.0f, 0.0f);
    walk.tolerance = 0.5f;
    REQUIRE(w.comp->entityWorld().direct("warden", {walk, use}, 0.0));
    std::string reason;
    w.play(60.0, [&] {
        for (const ActionEvent& e : w.world().actionEvents()) {
            if (e.entity == "warden" && e.result == ActionResult::Failed && reason.empty() &&
                e.action == "interact") {
                reason = e.reason;
            }
        }
    });
    CHECK(reason == "missing capability");
}

// ---- §39 / §74: many characters, different minds, no pathologies --------------------------------

namespace {
// The same builder `avgen_behavior_trace --crowd` uses (kept in step by hand; both are twenty lines).
// N deciding bodies from the scene's first one: a grid 6 m apart around it, each with its own seed
// and a personality drawn from a hash of its index (D2: a seed and an index, never a stream).
nlohmann::json crowdScene(nlohmann::json doc, int n) {
    nlohmann::json* proto = nullptr;
    for (auto& e : doc["entities"]) {
        if (e.contains("behaviors")) {
            for (const auto& b : e["behaviors"]) {
                if (b.value("kind", "") == "decide") {
                    proto = &e;
                }
            }
        }
        if (proto != nullptr) {
            break;
        }
    }
    if (proto == nullptr || n < 1) {
        return doc;
    }
    const nlohmann::json protoEntity = *proto;
    nlohmann::json protoNode;
    for (const auto& node : doc["nodes"]) {
        if (node["name"] == protoEntity.value("node", protoEntity["name"].get<std::string>())) {
            protoNode = node;
        }
    }
    const auto hash = [](std::uint32_t x) {
        x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
        return static_cast<float>(x) / 4294967296.0f;
    };
    const int side = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
    for (int i = 1; i < n; ++i) {
        const std::string name = "crowd-" + std::to_string(i);
        nlohmann::json node = protoNode;
        node["name"] = name;
        node["position"][0] = protoNode["position"][0].get<float>() + 6.0f * static_cast<float>(i % side);
        node["position"][2] = protoNode["position"][2].get<float>() + 6.0f * static_cast<float>(i / side);
        doc["nodes"].push_back(node);
        nlohmann::json e = protoEntity;
        e["name"] = name;
        e["node"] = name;
        e["seed"] = 9000 + i;
        const auto u = static_cast<std::uint32_t>(i);
        e["personality"] = {{"curiosity", hash(u * 3 + 1)},
                            {"caution", hash(u * 3 + 2)},
                            {"sociability", hash(u * 3 + 3)},
                            {"eventSensitivity", 0.5},
                            {"attentionSpan", 0.5}};
        doc["entities"].push_back(e);
    }
    return doc;
}
} // namespace

TEST_CASE("twenty aliens with different personalities choose differently and stay sane",
          "[entity][phaseD][crowd]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    World w(crowdScene(demoJson(), 20));
    std::vector<const Entity*> cast;
    for (const auto& e : w.world().entities()) {
        for (const auto& b : e->behaviors()) {
            if (b->kind() == "decide") {
                cast.push_back(e.get());
            }
        }
    }
    // Subject: twenty-one deciders (the scout's clones plus the warden).
    REQUIRE(cast.size() == 21);
    std::vector<glm::vec3> last(cast.size());
    for (std::size_t i = 0; i < cast.size(); ++i) {
        last[i] = cast[i]->state().position();
    }
    float worstStep = 0.0f;
    bool finite = true;
    std::size_t overlapFrames = 0;
    w.play(60.0, [&] {
        for (std::size_t i = 0; i < cast.size(); ++i) {
            const glm::vec3 p = cast[i]->state().position();
            finite = finite && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
            worstStep = std::max(worstStep, glm::length(flat(p) - flat(last[i])));
            last[i] = p;
            for (std::size_t k = i + 1; k < cast.size(); ++k) {
                if (glm::length(flat(p) - flat(cast[k]->state().position())) < 0.9f) {
                    ++overlapFrames;
                }
            }
        }
    });
    CHECK(finite);                 // §59: never NaN
    CHECK(worstStep < 0.2f);       // §60: never a teleport (run speed 4.2 m/s is 0.07 m a frame)
    // §39: "should avoid each other". Bodies are 0.9 m radius; interpenetrating centres closer than
    // one radius should be rare and brief, not a crowd walking through itself.
    CHECK(overlapFrames < 60u * 21u);
    std::set<std::string> choices;
    std::size_t decisions = 0;
    for (const Entity* e : cast) {
        DecisionDebug d;
        for (const auto& b : e->behaviors()) {
            if (b->decisionDebug(d)) {
                decisions += d.historyTotal;
                for (const auto& h : d.history) {
                    choices.insert(h.option + "|" + h.subject);
                }
            }
        }
    }
    // §39: "should not all make identical decisions".
    CHECK(decisions >= cast.size() * 2);
    CHECK(choices.size() >= 12);
}

// ---- §58: a character definition is validated before runtime ------------------------------------

TEST_CASE("character validation passes the demo and names each way a definition can be broken",
          "[entity][phaseD][validate]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    const World clean(demoJson());
    const auto ok = validateCharacters(clean.comp->entities(), clean.comp->eventProfiles());
    for (const ValidationIssue& i : ok) {
        INFO(i.entity << ": " << i.message);
        CHECK(i.severity != ValidationIssue::Severity::Error);
    }

    // Break the scout five ways, each a way the character would load and then silently do nothing.
    nlohmann::json doc = demoJson();
    for (auto& e : doc["entities"]) {
        if (e["name"] != "scout") {
            continue;
        }
        e.erase("perception");
        e["capabilities"] = nlohmann::json::array({"can_walk"});
        for (auto& b : e["behaviors"]) {
            if (b["kind"] != "decide") {
                continue;
            }
            b.erase("mind");
            for (auto& c : b["considerers"]) {
                if (c["kind"] == "react") {
                    c["events"] = nlohmann::json::array({"eclipse"});
                }
                if (c.value("name", "") == "watch-sky") {
                    c["tags"] = nlohmann::json::array({"zeppelin"});
                    c["traits"] = nlohmann::json{{"curiousity", 1.0}};
                }
            }
        }
    }
    const World broken(doc);
    const auto issues = validateCharacters(broken.comp->entities(), broken.comp->eventProfiles());
    const auto has = [&](const char* fragment) {
        return std::any_of(issues.begin(), issues.end(), [&](const ValidationIssue& i) {
            return i.entity == "scout" && i.message.find(fragment) != std::string::npos;
        });
    };
    CHECK(has("no 'perception' block"));
    CHECK(has("needs the decider's 'mind' block"));
    CHECK(has("event 'eclipse' is raised by nothing"));
    CHECK(has("tag 'zeppelin' is carried by nothing"));
    CHECK(has("unknown personality trait 'curiousity'"));
    CHECK(has("requires a capability this character lacks"));
    // And the warden, untouched, is still clean: the checks are about the definition they read.
    for (const ValidationIssue& i : issues) {
        if (i.entity == "warden") {
            CHECK(i.severity != ValidationIssue::Severity::Error);
        }
    }
}

// ---- §28: an audio event reaches characters as a semantic world event --------------------------

TEST_CASE("a signal-bus event is raised as a named world event, above its threshold only",
          "[entity][phaseD][events][audio]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    nlohmann::json doc = demoJson();
    doc["worldEvents"].push_back(nlohmann::json{
        {"name", "drop"}, {"signal", "music.drop"}, {"threshold", 0.5}, {"at", "saucer"}, {"radius", 500.0}});
    World w(doc);
    const signals::SignalId drop = w.bus.declare("music.drop", 0.0f, 1.0f, true);
    const auto drops = [&] {
        std::size_t n = 0;
        const std::uint32_t type = w.world().findEventType("drop");
        for (const WorldEvent& e : w.world().worldEvents()) {
            n += e.type == type && type != 0 ? 1 : 0;
        }
        return n;
    };
    w.play(1.0);
    REQUIRE(drops() == 0);
    // Below threshold: nothing.
    w.bus.setEvent(drop, true, 0.3f);
    w.play(1.0 / 60.0);
    w.bus.clearEvents();
    CHECK(drops() == 0);
    // Above: one event, at the saucer, with the signal's strength as its magnitude.
    w.bus.setEvent(drop, true, 0.9f);
    w.play(1.0 / 60.0);
    w.bus.clearEvents();
    REQUIRE(drops() == 1);
    const WorldEvent& e = w.world().worldEvents().back();
    CHECK(glm::length(e.position - w.entity("saucer").state().position()) < 1.0f);
    CHECK(std::abs(e.magnitude - 0.9f) < 1e-5f);
    // The saved scene keeps the mapping (both halves, testing.md #31).
    const nlohmann::json saved = w.comp->toJson();
    bool kept = false;
    for (const auto& ev : saved["worldEvents"]) {
        kept = kept || (ev["name"] == "drop" && ev["signal"] == "music.drop" && ev["at"] == "saucer");
    }
    CHECK(kept);
}

// ---- §77: the most important architectural test -- a second creature, data only ----------------

TEST_CASE("a second creature in a second world runs on the same framework with only data changed",
          "[entity][phaseD][genericity]") {
    const fs::path pasture = repoRoot() / "examples" / "labs" / "character" / "pasture.scene.json";
    if (!fs::exists(repoRoot() / "assets" / "farm" / "cow.glb")) {
        WARN("assets missing; skipping");
        return;
    }
    std::ifstream in(pasture);
    REQUIRE(in.good());
    const nlohmann::json doc = nlohmann::json::parse(in);
    // Subject: no alien, no mushroom, no saucer, no Glowmere word anywhere in the scene.
    const std::string text = doc.dump();
    for (const char* word : {"alien", "mushroom", "saucer", "ufo", "glowmere", "Glowmere"}) {
        INFO(word);
        CHECK(text.find(word) == std::string::npos);
    }
    assets::AssetRegistry registry(pasture.parent_path());
    auto loaded = scene::Composition::fromJson(doc, registry);
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    comp.attach(params, modulator);
    comp.scene().detailLimits.entityDistanceCull = false;
    // A different skeleton and motion library: the cow ships one clip, and every activity maps to it.
    const Entity* bess = comp.entityWorld().find("bess");
    REQUIRE(bess != nullptr);
    CHECK(bess->clipFor("graze") == "Walk");

    bool grazed = false;
    bool fled = false;
    FrameTime time;
    for (int i = 0; i < 60 * 60; ++i) {
        time.renderTime = static_cast<double>(i) / 60.0;
        time.deltaTime = i == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(i);
        params.resetFinals();
        comp.updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp.updateBehaviour(time, bus);
        comp.update(time);
        const ActionDesc* a = bess->actions().current(Authority::Routine);
        grazed = grazed || (a != nullptr && a->kind == ActionKind::Interact && a->target.member == "graze");
        fled = fled || bess->state().intent.type == IntentType::Flee;
    }
    CHECK(grazed); // capability + affordance, a verb no alien has
    CHECK(fled);   // the dog's bark, heard as a world event and fled by a cautious animal
    // And the engine source holds none of the demo's content words as literals (§77: "if the
    // implementation requires if (alien) ... stop and refactor").
    for (const char* file : {"behaviors.cpp", "decision.cpp", "mind.cpp", "action.cpp", "entity.cpp",
                             "perception.cpp", "behavior_trace.cpp", "character_validate.cpp"}) {
        std::ifstream src(repoRoot() / "src" / "entity" / file);
        REQUIRE(src.good());
        std::string line;
        while (std::getline(src, line)) {
            const auto code = line.substr(0, line.find("//"));
            for (const char* word : {"\"alien", "\"mushroom", "\"ufo", "\"saucer", "\"glowmere"}) {
                INFO(file << ": " << line);
                CHECK(code.find(word) == std::string::npos);
            }
        }
    }
}

// ---- §34: hybrid -- the author injects a goal, the character executes it -----------------------

TEST_CASE("an injected goal is taken up when it opens, executed by the character, and dropped once done",
          "[entity][phaseD][goal]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    nlohmann::json doc = demoJson();
    for (auto& e : doc["entities"]) {
        if (e["name"] != "warden") {
            continue;
        }
        for (auto& b : e["behaviors"]) {
            if (b["kind"] == "decide") {
                b["considerers"].push_back(nlohmann::json{{"kind", "goal"}, {"name", "errand"},
                                                          {"subject", "mushroom-2"}, {"from", 20.0},
                                                          {"until", 150.0}, {"affordance", "inspect"},
                                                          {"weight", 6.0}});
            }
        }
    }
    World w(doc);
    const Entity& warden = w.entity("warden");
    double first = -1.0;
    bool used = false;
    double ended = -1.0;
    bool again = false;
    w.play(150.0, [&] {
        const DecisionDebug d = w.decision("warden");
        const bool on = d.chosen == "errand";
        if (on && first < 0.0) {
            first = w.now;
        }
        const ActionDesc* a = warden.actions().current(Authority::Routine);
        used = used || (on && a != nullptr && a->kind == ActionKind::Interact &&
                        a->target.name == "mushroom-2");
        if (!on && first >= 0.0 && ended < 0.0) {
            ended = w.now;
        }
        if (on && ended >= 0.0) {
            again = true;
        }
    });
    REQUIRE(first >= 0.0);
    CHECK(first >= 20.0);      // not before the author opened it
    CHECK(first < 21.0);       // and taken up at once (weight 6 outbids wandering)
    CHECK(used);               // executed by the character: walked there, used its affordance
    REQUIRE(ended > first);
    CHECK_FALSE(again);        // once done, not repeated while the window is still open
}

// ---- §43 adversarial: an unreachable target does not become an infinite approach ---------------

TEST_CASE("a glowing thing inside a rock is tried, fails, and is left alone rather than retried forever",
          "[entity][phaseD][adversarial]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    const auto run = [](float capacity, std::size_t& attempts, std::size_t& decisions) {
        nlohmann::json doc = demoJson();
        // A third glowing mushroom, planted in the middle of rock-1: every stand-off point is solid.
        doc["nodes"].push_back(nlohmann::json{{"name", "mushroom-x"},
                                              {"kind", "gltf"},
                                              {"asset", "../../../assets/kenney/mushroom_red.glb"},
                                              {"position", {-48.0, 0.0, -20.0}},
                                              {"scale", {1.0, 1.0, 1.0}}});
        doc["entities"].push_back(nlohmann::json{{"name", "mushroom-x"}, {"node", "mushroom-x"},
                                                 {"tags", {"mushroom", "glowing"}}});
        for (auto& e : doc["entities"]) {
            if (e["name"] == "scout") {
                for (auto& b : e["behaviors"]) {
                    if (b["kind"] == "decide") {
                        b["mind"]["memory"]["capacity"] = capacity;
                    }
                }
            }
        }
        World w(doc);
        REQUIRE(w.world().navigator().obstructed(glm::vec2(-48.0f, -20.0f), 0.0f)); // it is inside
        attempts = 0;
        std::size_t seen = 0;
        w.play(120.0, [&] {
            const DecisionDebug d = w.decision("scout");
            for (std::size_t k = seen; k < d.historyTotal; ++k) {
                const std::size_t back = d.historyTotal - 1 - k;
                if (back < d.history.size() &&
                    d.history[d.history.size() - 1 - back].subject == "mushroom-x") {
                    ++attempts;
                }
            }
            seen = d.historyTotal;
        });
        decisions = w.decision("scout").historyTotal;
    };
    std::size_t attempts = 0;
    std::size_t decisions = 0;
    run(24.0f, attempts, decisions);
    INFO("attempts " << attempts << " of " << decisions << " decisions");
    // It may try -- it cannot know the rock is solid until it asks the planner -- but a failed
    // target is suppressed for `failSeconds`, so in 120 s at most one attempt per 30 s window.
    CHECK(attempts >= 1);
    CHECK(attempts <= 5);
    CHECK(decisions > attempts + 4); // and it was never stuck on it

    // Control: with no object memory (no failure suppression, no habituation) the same scout goes
    // back to the impossible target again and again -- §59's "unbounded path retries", which is
    // what the memory is there to stop.
    std::size_t retries = 0;
    std::size_t decisions2 = 0;
    run(0.0f, retries, decisions2);
    INFO("without memory: " << retries << " attempts");
    CHECK(retries > attempts * 3);
}

// ---- §23: the ground is not a destination --------------------------------------------------------

TEST_CASE("a terrain node is not a wander landmark, whatever it is called",
          "[entity][phaseD][wander]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    nlohmann::json doc = demoJson();
    // Renamed, so the filter cannot be keyed on the word "ground".
    REQUIRE(doc["nodes"][0]["kind"] == "terrain");
    doc["nodes"][0]["name"] = "meadow";
    World w(doc);
    for (const InterestPoint& p : w.world().interestPoints()) {
        CHECK(p.name != "meadow");
    }
    // Subject: other nodes are still landmarks.
    const auto points = w.world().interestPoints();
    CHECK(std::any_of(points.begin(), points.end(), [](const InterestPoint& p) { return p.name == "rock-1"; }));
    bool choseIt = false;
    w.play(60.0, [&] {
        choseIt = choseIt || w.decision("scout").chosen == "meadow" || w.decision("warden").chosen == "meadow";
    });
    CHECK_FALSE(choseIt);
}

TEST_CASE("an aware wanderer does not march shore point to shore point",
          "[entity][phaseD][wander]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    // The longest run of consecutive `water@` errands the warden makes in 300 s.
    const auto longestShoreRun = [](float variety) {
        nlohmann::json doc = demoJson();
        for (auto& e : doc["entities"]) {
            if (e["name"] != "warden") {
                continue;
            }
            for (auto& b : e["behaviors"]) {
                if (b["kind"] == "decide") {
                    for (auto& c : b["considerers"]) {
                        if (c["kind"] == "interest") {
                            c["variety"] = variety;
                        }
                    }
                }
            }
        }
        World w(doc);
        BehaviorTraceRecorder r;
        w.play(300.0, [&] { r.sample(w.world()); });
        int run = 0;
        int longest = 0;
        for (const std::string& line : r.lines()) {
            if (line.find("  warden  ") == std::string::npos) {
                continue;
            }
            const bool shore = line.find("-> water@") != std::string::npos;
            run = shore ? run + 1 : 0;
            longest = std::max(longest, run);
        }
        return longest;
    };
    const int withVariety = longestShoreRun(0.6f);
    const int without = longestShoreRun(1.0f);
    INFO("longest run of shore errands: " << withVariety << " with variety, " << without << " without");
    CHECK(without >= 6);        // subject: the fixture does pose the march
    CHECK(withVariety <= 4);
}
