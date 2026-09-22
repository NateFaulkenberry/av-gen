// The sense stage (ADR-270, ADR-290): what one character notices, out of everything there is.
//
// Until this landed, every character in this engine read one global list of interest points -- 140
// of them in this fixture, 505 in Glowmere -- with no range, no facing, no occlusion and no notion
// of having noticed something. Two characters in the same world took different routes only because
// they weighted the same omniscient list differently. The question the Character Intelligence Lab's
// case 6 asks -- "what does one character know that another does not?" -- had the answer "nothing",
// and this file is what makes the answer something else.
//
// Every arm has a control that can fail (ADR-182), and the controls are the point of the file:
//
//   range        a far-sighted body notices more than  |  the same body at the short range notices
//                a short-sighted one in the same scene |  a strict subset, and fewer of them
//   facing       a blinkered body misses what is       |  the same body at 360 degrees does not
//                behind it                             |
//   cadence      a 4 Hz body senses 4 times a second   |  an 8 Hz body senses twice as often
//   occlusion    a budgeted body performs exactly the  |  the same crowd at 0 tests per second
//                tests its rate buys                   |  reports `tested == false` everywhere
//   parameters   the registered `range` knob changes   |  reading the authored value instead would
//                what is perceived                     |  make both arms agree (ADR-225)
//   inertness    nothing reads a percept yet, so no    |  the lab's own route measurements are the
//                character's route moved               |  arm, and they are unchanged
//
// Quantities are structural (ADR-170): percepts produced, candidates scanned, occlusion tests
// performed, sense ticks fired. The one timing is reported and never asserted on.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "entity/perception.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path repoRoot() { return fs::path(AVGEN_SOURCE_DIR); }
fs::path labFixture() {
    return repoRoot() / "examples" / "labs" / "character" / "character-intelligence-lab.scene.json";
}
fs::path crowdFixture() {
    return repoRoot() / "examples" / "labs" / "character" / "perception-crowd.scene.json";
}
bool assetsPresent() { return fs::exists(repoRoot() / "assets" / "aliens" / "alien-scout.glb"); }

// The whole world, ticked the way the application ticks it -- the same harness
// `test_character_intelligence_lab.cpp` uses, for the same reason: half a frame is not a frame, and
// the fixture's obstacles are read from the flattened scene.
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
        // ADR-186's offline setting. With the distance cull on, "it did not perceive" would be a
        // measurement of the level-of-detail band rather than of the senses.
        comp->scene().detailLimits.entityDistanceCull = false;
    }

    void play(double seconds, double hz = 60.0) {
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
        }
    }

    [[nodiscard]] const entity::Entity& entity(const char* name) const {
        const entity::Entity* e = comp->entityWorld().find(name);
        REQUIRE(e != nullptr);
        return *e;
    }
    // Rewrites a registered perception knob's *base*, which is what survives the `resetFinals` at
    // the top of every frame. Writing the final would be overwritten before anything read it, which
    // is the difference between a setting and a decoration (ADR-225).
    void setKnob(const char* name, const char* knob, float value) {
        auto* p = params.findAs<float>(fmt::format("entity/{}/perception/{}", name, knob));
        REQUIRE(p != nullptr);
        p->setBase(value);
    }
};

// What a body noticed, as a set of identities, so two working sets can be compared without
// depending on the order they came out in.
std::set<std::pair<int, std::size_t>> identities(std::span<const entity::Percept> percepts) {
    std::set<std::pair<int, std::size_t>> out;
    for (const entity::Percept& p : percepts) {
        out.insert({static_cast<int>(p.kind), p.source});
    }
    return out;
}

float farthest(std::span<const entity::Percept> percepts) {
    float out = 0.0f;
    for (const entity::Percept& p : percepts) {
        out = std::max(out, p.distance);
    }
    return out;
}

} // namespace

TEST_CASE("two characters with different ranges notice different things", "[entity][perception]") {
    if (!assetsPresent()) {
        WARN("character assets are absent; skipping");
        return;
    }
    World w(labFixture());
    w.play(2.0);

    const std::span<const entity::Percept> far = w.entity("scout").percepts();
    const std::span<const entity::Percept> near = w.entity("rover").percepts();

    // The scene, so the numbers below are readable as fractions of it rather than as absolutes.
    const std::size_t interests = w.comp->entityWorld().interestPoints().size();
    INFO(fmt::format("interest points {}  scout {} percepts (farthest {:.2f} m)  rover {} percepts "
                     "(farthest {:.2f} m)",
                     interests, far.size(), farthest(far), near.size(), farthest(near)));

    // The headline: the same world, the same frame, two different answers.
    CHECK(far.size() > near.size());
    CHECK_FALSE(far.empty());
    CHECK_FALSE(near.empty());
    // And the difference is the range and not a capacity cut: neither working set is full, so
    // "fewer" means "noticed fewer things" rather than "was allowed to hold fewer".
    CHECK(far.size() < 24u);
    CHECK(near.size() < 24u);
    // Nothing beyond the range is ever a percept, and the authored ranges are 60 m and 12 m.
    CHECK(farthest(far) <= 60.0f);
    CHECK(farthest(near) <= 12.0f);
    CHECK(farthest(far) > 12.0f);

    // And a percept is **stale**, which is the point of the cadence rather than a defect of it. At
    // 4 Hz a working set is up to fifteen frames old, so a walking body is remembered a few
    // centimetres behind where it now stands -- and that is the whole of what this buys over the
    // omniscient list it replaces, because being wrong about where something is is the only thing
    // a character could not previously be. The first version of this arm asserted the opposite,
    // that a Character percept sits exactly on its body, and it failed by 0.225 m: one cadence
    // period of `rover` walking at 1.5 m/s. The assertion was wrong and the engine was right.
    float drift = 0.0f;
    double oldest = 0.0;
    for (const entity::Percept& p : far) {
        if (p.kind != entity::InterestKind::Character) {
            continue;
        }
        const entity::Entity& them = *w.comp->entityWorld().entities()[p.source];
        drift = std::max(drift, glm::length(p.position - them.state().position()));
        oldest = std::max(oldest, 2.0 - p.seenAt);
    }
    INFO(fmt::format("stalest character percept {:.3f} s old, {:.3f} m behind the body", oldest,
                     drift));
    // A quarter of a second of cadence plus a frame, against bodies that walk at 1.0 to 1.5 m/s.
    CHECK(oldest <= 0.26);
    CHECK(drift < 0.5f);
}

TEST_CASE("a percept is built from the simulation's position and never the drawn one",
          "[entity][perception]") {
    // R1 (ADR-260), as the arm that can fail. Glowmere's saucer carries a `drift` of radius 2.4 m,
    // so its node -- and everything parented to it -- is drawn up to 2.4 m from where the
    // simulation says it stands. A percept built from the drawn position would have a character
    // walk to the wrong place by up to 2.4 m for reasons nothing could explain.
    //
    // Built here rather than on the lab fixture because the fixture's four explorers carry no
    // `MotionOffset` at all, so an assertion on it there would pass whichever position was read --
    // which is the same lie as an arm that cannot fail.
    entity::EntityWorld world;
    std::vector<entity::EntityDesc> descs(2);
    descs[0].name = "watcher";
    descs[0].perceives = true;
    descs[0].perception.range = 40.0f;
    descs[0].perception.hertz = 60.0f; // every step, so the stale window cannot hide the answer
    descs[1].name = "floater";
    entity::BehaviorDesc hover;
    hover.kind = "hover";
    hover.name = "hover";
    hover.settings = nlohmann::json{{"kind", "hover"}, {"amplitude", 3.0f}, {"rate", 1.0f}};
    descs[1].behaviors.push_back(hover);
    world.setEntities(std::move(descs), 11);

    std::vector<entity::NodeBinding> bindings(2);
    bindings[0].node = "watcher";
    bindings[0].exists = true;
    bindings[0].anchor = glm::vec3(0.0f);
    bindings[1].node = "floater";
    bindings[1].exists = true;
    bindings[1].anchor = glm::vec3(6.0f, 0.0f, 0.0f);
    world.setBindings(bindings);

    params::ParameterSet params;
    world.registerParameters(params);
    world.bind(params);
    world.reset();

    entity::EntityUpdate ctx;
    for (int i = 0; i < 60; ++i) {
        ctx.time = static_cast<double>(i) / 60.0;
        ctx.dt = i == 0 ? 0.0 : 1.0 / 60.0;
        ctx.frameIndex = static_cast<std::uint64_t>(i);
        params.resetFinals();
        world.update(ctx, params);
    }

    const entity::Entity& floater = *world.entities()[1];
    const float offset = glm::length(floater.visualPosition() - floater.state().position());
    INFO(fmt::format("hover offset {:.3f} m", offset));
    // The control. Without a body whose two positions differ, the assertion below passes whichever
    // one the sense stage read.
    REQUIRE(offset > 0.1f);

    const std::span<const entity::Percept> got = world.entities()[0]->percepts();
    REQUIRE(got.size() == 1u);
    CHECK(got[0].kind == entity::InterestKind::Character);
    CHECK(got[0].source == 1u);
    CHECK(glm::length(got[0].position - floater.state().position()) < 1e-4f);
    CHECK(glm::length(got[0].position - floater.visualPosition()) > 0.1f);
}

TEST_CASE("the range is the variable, and the same body at a shorter range notices a subset",
          "[entity][perception]") {
    if (!assetsPresent()) {
        WARN("character assets are absent; skipping");
        return;
    }
    // The control for the arm above. Two *different* bodies stand in two different places, so they
    // would notice different things whatever their ranges were. This is the same body, in the same
    // place, at the same instant, with one number changed -- and it is also the ADR-225 arm: the
    // number that changes is the registered parameter, not the authored value.
    World wide(labFixture());
    wide.play(2.0);
    const auto far = identities(wide.entity("scout").percepts());
    const float farDistance = farthest(wide.entity("scout").percepts());

    World narrow(labFixture());
    narrow.setKnob("scout", "range", 12.0f);
    narrow.play(2.0);
    const auto near = identities(narrow.entity("scout").percepts());
    const float nearDistance = farthest(narrow.entity("scout").percepts());

    INFO(fmt::format("scout at 60 m: {} percepts (farthest {:.2f} m); at 12 m: {} (farthest {:.2f} m)",
                     far.size(), farDistance, near.size(), nearDistance));
    CHECK(near.size() < far.size());
    CHECK_FALSE(near.empty());
    CHECK(nearDistance <= 12.0f);
    // A strict subset. Narrowing the senses may only *remove* things: a percept that appeared when
    // the range was cut would mean the scan's answer depended on something other than the range.
    CHECK(std::includes(far.begin(), far.end(), near.begin(), near.end()));

    // The two bodies walked identically for those two seconds, so the difference is not a
    // difference of position. Without this the subset above could be two bodies standing in two
    // slightly different places and agreeing by luck.
    CHECK(glm::length(wide.entity("scout").state().position()
                      - narrow.entity("scout").state().position()) < 1e-6f);
}

TEST_CASE("a blinkered body misses what is behind it", "[entity][perception]") {
    if (!assetsPresent()) {
        WARN("character assets are absent; skipping");
        return;
    }
    // `diver` is authored at the same 60 m range as `scout` and a quarter of its field of view.
    World narrow(labFixture());
    narrow.play(2.0);
    const auto blinkered = identities(narrow.entity("diver").percepts());

    World wide(labFixture());
    wide.setKnob("diver", "fieldOfView", 360.0f);
    wide.play(2.0);
    const auto allRound = identities(wide.entity("diver").percepts());

    INFO(fmt::format("diver at 90 deg: {} percepts; at 360 deg: {}", blinkered.size(),
                     allRound.size()));
    CHECK(blinkered.size() < allRound.size());
    CHECK(std::includes(allRound.begin(), allRound.end(), blinkered.begin(), blinkered.end()));
    // And the facing is what moved, not the body.
    CHECK(glm::length(narrow.entity("diver").state().position()
                      - wide.entity("diver").state().position()) < 1e-6f);
}

TEST_CASE("the sense cadence is a rate and not a frame count", "[entity][perception]") {
    if (!assetsPresent()) {
        WARN("character assets are absent; skipping");
        return;
    }
    // 4 Hz over 10 s is 40 sense ticks per body, whatever the frame rate is. The control is the
    // same ten seconds at 30 Hz: half the frames, the same number of sense ticks -- which is the
    // whole distinction ADR-267's D3 draws between *which stages run* and *the integration step*.
    const auto ticksOver = [](double seconds, double frameHz) {
        World w(labFixture());
        std::size_t sensed = 0;
        const double step = 1.0 / frameHz;
        const auto frames = static_cast<int>(std::llround(seconds * frameHz));
        FrameTime time;
        for (int i = 0; i < frames; ++i) {
            time.renderTime = static_cast<double>(i) * step;
            time.deltaTime = i == 0 ? 0.0 : step;
            time.frameIndex = static_cast<std::uint64_t>(i);
            w.params.resetFinals();
            w.comp->updateFields(time, w.bus, w.modulator);
            w.modulator.applyRoutes(w.bus, w.params, time.deltaTime);
            w.comp->updateBehaviour(time, w.bus);
            w.comp->update(time);
            sensed += w.comp->entityWorld().perceptionCounts().sensed;
        }
        return sensed;
    };
    const std::size_t at60 = ticksOver(10.0, 60.0);
    const std::size_t at30 = ticksOver(10.0, 30.0);
    INFO(fmt::format("sense ticks over 10 s: {} at 60 Hz, {} at 30 Hz (4 bodies at 4 Hz)", at60, at30));
    // Four bodies at 4 Hz for ten seconds, plus the first tick each body takes on the frame it is
    // first updated on.
    CHECK(at60 == 164u);
    CHECK(at30 == at60);

    // And the control that can fail: doubling the cadence doubles the ticks. Without it "164" could
    // be any constant the implementation happened to produce.
    World fast(labFixture());
    for (const char* name : {"scout", "rover", "diver", "penned"}) {
        fast.setKnob(name, "hertz", 8.0f);
    }
    std::size_t sensed = 0;
    FrameTime time;
    for (int i = 0; i < 600; ++i) {
        time.renderTime = static_cast<double>(i) / 60.0;
        time.deltaTime = i == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(i);
        fast.params.resetFinals();
        fast.comp->updateFields(time, fast.bus, fast.modulator);
        fast.modulator.applyRoutes(fast.bus, fast.params, time.deltaTime);
        fast.comp->updateBehaviour(time, fast.bus);
        fast.comp->update(time);
        sensed += fast.comp->entityWorld().perceptionCounts().sensed;
    }
    INFO(fmt::format("at 8 Hz: {} ticks", sensed));
    CHECK(sensed == 324u);
}

TEST_CASE("the occlusion budget is respected under a crowd", "[entity][perception][occlusion]") {
    // 24 bodies, six twelve-metre pillars, one occlusion test per body per second. The pillars are
    // there because `world::heroSightline` knows the ground and the heroes *exactly* and nothing
    // else: a crowd fixture with no heroes over flat ground would measure a function that can only
    // ever answer 1, which is the shape of probe this repository has already been caught by twice.
    constexpr double kSeconds = 6.0;
    World w(crowdFixture());
    const std::size_t bodies = w.comp->entityWorld().size();
    REQUIRE(bodies == 24u);

    std::size_t tests = 0;
    std::size_t deferred = 0;
    std::size_t candidates = 0;
    std::size_t percepts = 0;
    std::size_t sensed = 0;
    std::size_t testedBlocked = 0;
    const auto begin = std::chrono::steady_clock::now();
    const auto frames = static_cast<int>(std::llround(kSeconds * 60.0));
    FrameTime time;
    for (int i = 0; i < frames; ++i) {
        time.renderTime = static_cast<double>(i) / 60.0;
        time.deltaTime = i == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(i);
        w.params.resetFinals();
        w.comp->updateFields(time, w.bus, w.modulator);
        w.modulator.applyRoutes(w.bus, w.params, time.deltaTime);
        w.comp->updateBehaviour(time, w.bus);
        w.comp->update(time);
        const auto counts = w.comp->entityWorld().perceptionCounts();
        tests += counts.occlusionTests;
        deferred += counts.occlusionDeferred;
        candidates += counts.candidates;
        percepts += counts.percepts;
        sensed += counts.sensed;
        // Sampled every frame rather than at the end, because a working set is rebuilt each tick
        // and the last frame's is one of six hundred.
        for (const auto& e : w.comp->entityWorld().entities()) {
            for (const entity::Percept& p : e->percepts()) {
                if (p.tested && p.visibility < 1.0f) {
                    ++testedBlocked;
                }
            }
        }
    }
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    INFO(fmt::format("{} bodies / {:.0f} s: {} sense ticks, {} candidates, {} percepts, {} "
                     "occlusion tests, {} deferred, {} frames of a tested percept coming back "
                     "occluded; {:.2f} s wall",
                     bodies, kSeconds, sensed, candidates, percepts, tests, deferred, testedBlocked,
                     seconds));

    // **The budget, exactly.** `allowed(n) = floor(n * testsPerSecond / hertz)` is a pure function
    // of the tick index, so over T seconds a body performs exactly `floor(T * testsPerSecond)`
    // tests -- not "about that on average". 24 bodies at 1 test/s for 6 s is 144, and the ceiling
    // below is what the assertion would be if the schedule were only approximately respected.
    CHECK(tests == 144u);
    CHECK(tests <= static_cast<std::size_t>(std::ceil(kSeconds * 1.0)) * bodies);
    // And the rest were **left untested, never dropped** (ADR-270). A percept discarded because a
    // budget ran out is a character whose behaviour depends on how many other characters exist.
    CHECK(deferred > 0u);
    CHECK(tests + deferred == percepts);
    // The arm that says the sightline is measuring something. Without it, "144 tests performed"
    // would be consistent with a `heroSightline` that returns 1 for everything -- which is exactly
    // what it does when its `ClearanceField::map` is null, and what it would have done here if the
    // navigator had never reached the sense stage.
    CHECK(testedBlocked > 0u);
    CHECK(candidates > percepts);
}

TEST_CASE("the control: at zero tests per second nothing is tested anywhere",
          "[entity][perception][occlusion]") {
    // The same crowd, the same six seconds, one parameter set to zero. This is the arm the spec
    // names and it is the one that can fail: if `tested` were being set optimistically -- or if
    // `visibility` were being reported as measured when it was assumed -- this is where it shows.
    World w(crowdFixture());
    for (const auto& e : w.comp->entityWorld().entities()) {
        w.setKnob(e->name().c_str(), "occlusionTestsPerSecond", 0.0f);
    }
    std::size_t tests = 0;
    FrameTime time;
    for (int i = 0; i < 360; ++i) {
        time.renderTime = static_cast<double>(i) / 60.0;
        time.deltaTime = i == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(i);
        w.params.resetFinals();
        w.comp->updateFields(time, w.bus, w.modulator);
        w.modulator.applyRoutes(w.bus, w.params, time.deltaTime);
        w.comp->updateBehaviour(time, w.bus);
        w.comp->update(time);
        tests += w.comp->entityWorld().perceptionCounts().occlusionTests;
        // Not once, on any frame, on any body.
        for (const auto& e : w.comp->entityWorld().entities()) {
            for (const entity::Percept& p : e->percepts()) {
                REQUIRE_FALSE(p.tested);
                REQUIRE(p.visibility == 1.0f);
            }
        }
    }
    INFO(fmt::format("occlusion tests at 0 per second: {}", tests));
    CHECK(tests == 0u);
    // And percepts were still produced: "nothing was tested" must not be "nothing was perceived".
    std::size_t total = 0;
    for (const auto& e : w.comp->entityWorld().entities()) {
        total += e->percepts().size();
    }
    CHECK(total > 0u);
}

TEST_CASE("a scripted perception answers where a world would", "[entity][perception]") {
    // Why `IPerception` is an interface at all: a decision layer has to be assertable without a
    // world, exactly as `IPathProvider` lets an action be. P3 is what will use this.
    if (!assetsPresent()) {
        WARN("character assets are absent; skipping");
        return;
    }
    World w(labFixture());
    entity::ScriptedPerception script;
    entity::Percept written;
    written.kind = entity::InterestKind::Glow;
    written.source = 3;
    written.position = glm::vec3(1.0f, 2.0f, 3.0f);
    written.distance = 4.0f;
    written.salience = 0.5f;
    written.visibility = 0.25f;
    written.tested = true;
    script.set(0, {written});
    w.comp->entityWorld().setPerception(&script);
    w.play(1.0);

    const std::span<const entity::Percept> got = w.entity("scout").percepts();
    REQUIRE(got.size() == 1u);
    CHECK(got[0].kind == entity::InterestKind::Glow);
    CHECK(got[0].source == 3u);
    CHECK(got[0].visibility == 0.25f);
    CHECK(got[0].tested);
    // And the bodies the script said nothing about hold nothing, rather than falling back to the
    // grid: an implementation that answered from two places at once would be untestable.
    CHECK(w.entity("rover").percepts().empty());
}

TEST_CASE("a seek reconstructs the working set rather than inheriting it", "[entity][perception]") {
    if (!assetsPresent()) {
        WARN("character assets are absent; skipping");
        return;
    }
    // ADR-267's D4: anything a character remembers must be recoverable by re-simulating. The
    // sharpest form of that is that seeking twice to the same second gives the same percepts -- and
    // the control is that the working set is not simply whatever the last played frame left behind.
    World w(labFixture());
    w.play(20.0);
    const auto played = identities(w.entity("scout").percepts());

    w.comp->entityWorld().seek(8.0, &w.params);
    const auto first = identities(w.entity("scout").percepts());
    const float firstFar = farthest(w.entity("scout").percepts());
    w.comp->entityWorld().seek(8.0, &w.params);
    const auto second = identities(w.entity("scout").percepts());

    INFO(fmt::format("played to 20 s: {} percepts; sought to 8 s: {} (farthest {:.2f} m)",
                     played.size(), first.size(), firstFar));
    CHECK(first == second);
    CHECK_FALSE(first.empty());
    // The control: the state at 8 s is not the state at 20 s. A seek that left the played frame's
    // working set in place would pass the equality above and be wrong.
    CHECK(first != played);
}

TEST_CASE("the senses cost nothing to a body that declared none", "[entity][perception]") {
    // The opt-in, as a measurement rather than as an intention. `perception-crowd` declares senses
    // on every body; a world with none must report a sense stage that did not run at all, which is
    // what keeps every scene this repository already ships bit-for-bit what it was.
    World w(repoRoot() / "examples" / "labs" / "character" / "perception-crowd.scene.json");
    CHECK(w.comp->entityWorld().perceiving());

    // `Composition::fromJson` builds the entity world from the scene; an entity with no
    // `perception` key sets no flag and the whole stage is skipped.
    entity::EntityWorld bare;
    std::vector<entity::EntityDesc> descs(1);
    descs[0].name = "rock";
    bare.setEntities(std::move(descs), 7);
    CHECK_FALSE(bare.perceiving());
    params::ParameterSet params;
    bare.registerParameters(params);
    CHECK(params.find("entity/rock/perception/range") == nullptr);
    entity::EntityUpdate ctx;
    bare.update(ctx, params);
    CHECK(bare.perceptionCounts().perceivers == 0u);
    CHECK(bare.perceptionCounts().sensed == 0u);
    CHECK(bare.perceptionCounts().candidates == 0u);
}

TEST_CASE("the perception settings survive a round trip through the scene file",
          "[entity][perception]") {
    entity::EntityDesc desc;
    desc.name = "watcher";
    desc.perceives = true;
    desc.perception.range = 21.5f;
    desc.perception.fieldOfView = 110.0f;
    desc.perception.capacity = 12;
    desc.perception.hertz = 6.0f;
    desc.perception.occlusionTestsPerSecond = 3.0f;
    desc.perception.weight[2] = 2.5f;

    const nlohmann::json j = entity::entityToJson(desc);
    REQUIRE(j.contains("perception"));
    auto back = entity::entityFromJson(j, {}, nullptr);
    INFO((back.has_value() ? std::string() : back.error().message));
    REQUIRE(back.has_value());
    CHECK(back->perceives);
    CHECK(back->perception.range == 21.5f);
    CHECK(back->perception.fieldOfView == 110.0f);
    CHECK(back->perception.capacity == 12);
    CHECK(back->perception.hertz == 6.0f);
    CHECK(back->perception.occlusionTestsPerSecond == 3.0f);
    CHECK(back->perception.weight[2] == 2.5f);

    // The control: an entity that declared no senses writes no key and reads back with none. A
    // writer that emitted a default block would give every body in every scene this repository
    // ships a sense stage on the next save.
    entity::EntityDesc plain;
    plain.name = "rock";
    CHECK_FALSE(entity::entityToJson(plain).contains("perception"));
    auto plainBack = entity::entityFromJson(entity::entityToJson(plain), {}, nullptr);
    REQUIRE(plainBack.has_value());
    CHECK_FALSE(plainBack->perceives);

    // And the refusals, each of which is a scene an author would otherwise have to debug from a
    // character that never notices anything.
    nlohmann::json bad = j;
    bad["perception"]["range"] = 0.0;
    CHECK_FALSE(entity::entityFromJson(bad, {}, nullptr).has_value());
    bad = j;
    bad["perception"]["capacity"] = 0;
    CHECK_FALSE(entity::entityFromJson(bad, {}, nullptr).has_value());
    bad = j;
    bad["perception"]["weights"] = {{"mushroom", 2.0}};
    CHECK_FALSE(entity::entityFromJson(bad, {}, nullptr).has_value());
}

TEST_CASE("what the cadence costs a replay", "[entity][perception]") {
    // ADR-170: the assertion is structural and the timing is reported. The structural claim is the
    // one that can fail -- a replay at 60 Hz senses fifteen times as often as one at 4 -- and the
    // milliseconds beside it are why anyone should care, on this machine, at this load.
    //
    // This is the arm that corrects ADR-270 §4. It says the 4 Hz cadence "is not a budget" because
    // an isolated `spatial::PointGrid` query is 0.098 us, and for a *played frame* that is right:
    // twenty-four bodies sensing every frame is twenty microseconds. What it does not price is the
    // **replay**, where the same per-tick cost is multiplied by the number of fixed steps in the
    // scrub window -- and that is precisely where ADR-273 says the editor's latency lives.
    constexpr double kSeek = 30.0;
    const auto seekAt = [&](float hertz, std::size_t& ticks) {
        double best = 1.0e30;
        for (int run = 0; run < 5; ++run) {
            World w(crowdFixture());
            for (const auto& e : w.comp->entityWorld().entities()) {
                w.setKnob(e->name().c_str(), "hertz", hertz);
                w.setKnob(e->name().c_str(), "occlusionTestsPerSecond", 0.0f);
            }
            w.params.resetFinals();
            const auto begin = std::chrono::steady_clock::now();
            w.comp->entityWorld().seek(kSeek, &w.params);
            best = std::min(best, std::chrono::duration<double, std::milli>(
                                      std::chrono::steady_clock::now() - begin)
                                      .count());
            ticks = w.comp->entityWorld().perceptionCounts().sensed;
        }
        return best;
    };
    std::size_t slowTicks = 0;
    std::size_t fastTicks = 0;
    const double slow = seekAt(4.0f, slowTicks);
    const double fast = seekAt(60.0f, fastTicks);
    INFO(fmt::format("seek({:.0f} s) over 24 perceiving bodies, minima of 5: {:.1f} ms at 4 Hz "
                     "({} sense ticks), {:.1f} ms at 60 Hz ({} ticks)",
                     kSeek, slow, slowTicks, fast, fastTicks));
    // 1,801 replayed instants x 24 bodies: the 1,800 fixed steps ending at 30 s, and -- since
    // ADR-670 -- the instant t = 0 that a play integrates with a zero delta and the replay used to
    // skip. At 60 Hz every body senses on every instant: 43,224 ticks. At 4 Hz the replay spans
    // tick indices 0 through 120, 121 a body, 2,904. (Before ADR-670 one body's phase put its tick
    // 0 only at t = 0, which the replay never reached, so it fired 120 times and the total was
    // 2,903: the missing instant, visible in the count.)
    CHECK(fastTicks == 43224u);
    CHECK(slowTicks == 2904u);
    // Which is the cadence, to within the one phase-shifted body: 14.9 times fewer sense ticks for
    // the same replay.
    CHECK(fastTicks > slowTicks * 14u);
    CHECK(fastTicks < slowTicks * 16u);
}

TEST_CASE("the occlusion rate is a rate, whatever the cadence is", "[entity][perception][occlusion]") {
    // The budget divides the tests by the *tick rate*, and `hertz <= 0` means "sense every step",
    // where `senseTick` counts microseconds rather than sense ticks. A budget that assumed the tick
    // rate was the cadence would grant such a body sixteen thousand times its rate, which is the
    // shape of bug that would never show up at the default and would be catastrophic the first time
    // somebody turned the cadence off to see what it was buying.
    World w(crowdFixture());
    for (const auto& e : w.comp->entityWorld().entities()) {
        w.setKnob(e->name().c_str(), "hertz", 0.0f);
        w.setKnob(e->name().c_str(), "occlusionTestsPerSecond", 2.0f);
    }
    std::size_t tests = 0;
    std::size_t sensed = 0;
    FrameTime time;
    for (int i = 0; i < 180; ++i) {
        time.renderTime = static_cast<double>(i) / 60.0;
        time.deltaTime = i == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(i);
        w.params.resetFinals();
        w.comp->updateFields(time, w.bus, w.modulator);
        w.modulator.applyRoutes(w.bus, w.params, time.deltaTime);
        w.comp->updateBehaviour(time, w.bus);
        w.comp->update(time);
        tests += w.comp->entityWorld().perceptionCounts().occlusionTests;
        sensed += w.comp->entityWorld().perceptionCounts().sensed;
    }
    INFO(fmt::format("hertz 0: {} sense ticks over 180 frames x 24 bodies, {} occlusion tests",
                     sensed, tests));
    // Every body on every frame: the cadence really is off.
    CHECK(sensed == 24u * 180u);
    // And the rate is still the rate. The last frame is at 179/60 = 2.9833 s, so each body is owed
    // floor(2.9833 x 2) = 5 tests and the crowd is owed 120.
    CHECK(tests == 120u);

    // The same hole in the form that would actually have shipped: a **frame long enough to cross
    // two sense ticks**. `hertz` is capped at 60 -- sensing more often than the simulation steps is
    // not a thing a cadence can mean -- so ticks cannot be outrun by the knob; they are outrun by
    // the frame rate. At a 60 Hz cadence and a 30 Hz step the tick index advances by two a frame,
    // and a budget priced on `tick` against `tick + 1` would have spent about half of what it was
    // owed. Three seconds at 30 Hz: the last frame is 89/30 = 2.9667 s, each body is owed
    // floor(2.9667 x 2) = 5, and the crowd is owed 120 -- the same 120 the 4 Hz arm gets, which is
    // the point of a rate.
    World slow(crowdFixture());
    for (const auto& e : slow.comp->entityWorld().entities()) {
        slow.setKnob(e->name().c_str(), "hertz", 60.0f);
        slow.setKnob(e->name().c_str(), "occlusionTestsPerSecond", 2.0f);
    }
    std::size_t slowTests = 0;
    std::size_t slowSensed = 0;
    for (int i = 0; i < 90; ++i) {
        time.renderTime = static_cast<double>(i) / 30.0;
        time.deltaTime = i == 0 ? 0.0 : 1.0 / 30.0;
        time.frameIndex = static_cast<std::uint64_t>(i);
        slow.params.resetFinals();
        slow.comp->updateFields(time, slow.bus, slow.modulator);
        slow.modulator.applyRoutes(slow.bus, slow.params, time.deltaTime);
        slow.comp->updateBehaviour(time, slow.bus);
        slow.comp->update(time);
        slowTests += slow.comp->entityWorld().perceptionCounts().occlusionTests;
        slowSensed += slow.comp->entityWorld().perceptionCounts().sensed;
    }
    INFO(fmt::format("hertz 60 against a 30 Hz step: {} sense ticks, {} occlusion tests", slowSensed,
                     slowTests));
    CHECK(slowSensed == 24u * 90u); // every frame, and every frame skips a tick
    CHECK(slowTests == 120u);
}
