// Simulation checkpoints (ADR-700): a seek restores the nearest checkpoint at or before the target
// and replays forward from it, so it is exact at any time -- and it is only exact if a checkpoint
// holds every piece of state a step reads. These arms are the scene-free half of that claim; the
// Glowmere film's half (sixteen bodies, a director, the awareness layer) is in
// test_glowmere_scrub.cpp.
//
// The design leans on whole-object copies so that a new member is covered without anyone listing
// it: every `Entity` is copied by its implicit copy constructor (`BehaviorList` clones behaviours),
// and the director by its own. The one hand-written list is `EntityWorld`'s world-level state, and
// the first test below is what stops that list going stale.

#include "entity/entity.hpp"
#include "params/parameter_set.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

using namespace avgen;

namespace {

constexpr double kStep = 1.0 / 60.0;

params::ParamDesc<glm::vec3> v3(std::string path, glm::vec3 def, float lo, float hi) {
    return params::ParamDesc<glm::vec3>{
        .path = std::move(path), .defaultValue = def, .hardMin = glm::vec3(lo), .hardMax = glm::vec3(hi)};
}

entity::BehaviorDesc behavior(const char* kind, nlohmann::json settings = nlohmann::json::object()) {
    entity::BehaviorDesc d;
    d.kind = kind;
    d.name = kind;
    settings["kind"] = kind;
    d.settings = std::move(settings);
    return d;
}

// Accumulating, differencing and seeded behaviours, and an action routine, so every kind of state
// an entity carries is exercised: `spin` and `bank` integrate, `drift` differences against the
// previous step, `liveliness` draws from the entity's rng, and the routine is an action queue part
// way through a list.
std::vector<entity::BehaviorDesc> cast() {
    return {behavior("hover", {{"amplitude", 1.0}, {"rate", 0.3}}),
            behavior("drift", {{"radius", 2.0}, {"rate", 0.2}}),
            behavior("bank", {{"degrees", 8.0}}),
            behavior("spin", {{"signal", "none"}, {"baseRate", 40.0}, {"damping", 1.5}}),
            behavior("liveliness")};
}

std::vector<entity::ActionDesc> routine() {
    std::vector<entity::ActionDesc> out;
    for (int i = 0; i < 12; ++i) {
        entity::ActionDesc a;
        a.kind = entity::ActionKind::Wait;
        a.name = "wait" + std::to_string(i);
        a.duration = 1.7;
        out.push_back(a);
    }
    return out;
}

struct World {
    params::ParameterSet params;
    signals::SignalBus bus;
    entity::EntityWorld world;

    explicit World(int count = 4) {
        std::vector<entity::EntityDesc> descs;
        std::vector<entity::NodeBinding> bindings;
        for (int i = 0; i < count; ++i) {
            const std::string node = "body" + std::to_string(i);
            params.add(v3("nodes/" + node + "/position", glm::vec3(0.0f), -1e4f, 1e4f));
            params.add(v3("nodes/" + node + "/rotation", glm::vec3(0.0f), -360.0f, 360.0f));
            params.add(v3("nodes/" + node + "/scale", glm::vec3(1.0f), 0.0f, 100.0f));
            entity::EntityDesc d;
            d.name = node;
            d.node = node;
            d.seed = static_cast<std::uint32_t>(500 + i);
            d.behaviors = cast();
            d.actions = routine();
            descs.push_back(std::move(d));
            entity::NodeBinding b;
            b.node = node;
            b.exists = true;
            b.transformPrefix = "nodes/" + node + "/";
            b.anchor = glm::vec3(static_cast<float>(i) * 3.0f, 0.0f, 0.0f);
            bindings.push_back(std::move(b));
        }
        world.setEntities(std::move(descs), 11u);
        world.setBindings(bindings);
        world.registerParameters(params);
        world.bind(params);
    }

    void seek(double t, entity::SeekMode mode = entity::SeekMode::Checkpointed, std::uint64_t maxBodySteps = 0) {
        world.seek(t, &params, nullptr, kStep,
                   entity::SeekBudget{.maxSeconds = 90.0, .maxBodySteps = maxBodySteps, .mode = mode});
    }
    void play(int frames, double from) {
        for (int i = 1; i <= frames; ++i) {
            params.resetFinals();
            entity::EntityUpdate u;
            u.time = from + static_cast<double>(i) * kStep;
            u.dt = kStep;
            u.bus = &bus;
            u.distanceDetail = false;
            world.update(u, params);
        }
    }

    // Everything a later step could read that the public surface shows, exactly, plus one played
    // frame's drawn transforms -- which fold in the behaviours' offsets, the half of the answer that
    // lives in behaviour state rather than in `EntityState`.
    [[nodiscard]] std::string digest(double t) {
        std::ostringstream o;
        o << std::hexfloat;
        for (const auto& e : world.entities()) {
            const entity::EntityState& s = e->state();
            const glm::vec3 p = s.position();
            o << e->name() << ':' << p.x << ',' << p.y << ',' << p.z << ';' << s.yaw << ';' << s.speed << ';'
              << s.velocity.x << ',' << s.velocity.z << ';' << e->actions().pending() << ';'
              << e->actions().serial(entity::Authority::Routine) << '\n';
        }
        play(1, t);
        for (const params::IParameter* p : params.ordered()) {
            for (std::size_t c = 0; c < p->componentCount(); ++c) {
                o << p->finalComponent(c) << ';';
            }
        }
        return o.str();
    }
};

} // namespace

TEST_CASE("a checkpoint holds every member EntityWorld has, or the author was made to decide",
          "[entity][seek][checkpoint][adr700]") {
    // THE COMPLETENESS GUARD. Entities and the director are copied whole, so a member added to them
    // is in every checkpoint without anyone remembering it. `EntityWorld`'s own simulation state is
    // the one hand-written list (`EntityWorld::Checkpoint`, `captureCheckpoint`,
    // `restoreCheckpoint`), and C++ has no reflection to check it against the class.
    //
    // So this fails when `EntityWorld` changes size. If it failed for you: you added (or removed) a
    // member. Decide what it is --
    //   * **simulation state** a replay step reads or writes (an event list, a counter a step
    //     consults, an rng): add it to `EntityWorld::Checkpoint` and to both functions, or a scrub
    //     will disagree with the play on some scenes at some times;
    //   * **configuration** (something set between runs that changes what a step computes): bump
    //     `inputEpoch_` wherever it is set, so the checkpoints taken before it are dropped;
    //   * **scratch or a report** rebuilt before it is read (a grid rebuilt every step, a count):
    //     nothing to do.
    // -- then update the number below and say which in the commit.
    //
    // A size is a crude fingerprint: two edits that cancel would pass it. It is the tripwire, not the
    // proof; the proof is the digest test in test_glowmere_scrub.cpp, which restores into a world
    // that has been somewhere else and compares everything.
    constexpr std::size_t kEntityWorldSize = 2080; // ADR-833: landmarkTags_, configuration (setLandmarkTags bumps inputEpoch_)
    INFO("sizeof(EntityWorld) = " << sizeof(entity::EntityWorld));
    CHECK(sizeof(entity::EntityWorld) == kEntityWorldSize);

    // And the whole-object half is whole-object: an entity is copyable, which is what makes
    // `Entity`'s implicit copy the checkpoint. If a member makes it uncopyable, this fails to
    // compile rather than a checkpoint silently leaving something out.
    STATIC_REQUIRE(std::is_copy_constructible_v<entity::Entity>);
    STATIC_REQUIRE(std::is_copy_assignable_v<entity::Entity>);
}

TEST_CASE("a checkpointed seek equals the whole-history replay, however the world got there",
          "[entity][seek][checkpoint][determinism][adr700]") {
    World reference;
    World subject;
    // Somewhere else first: far past every target, which records every checkpoint and leaves the
    // state as unlike the targets' as it gets; then ordinary frames on top.
    subject.seek(60.0);
    REQUIRE(subject.world.checkpointStats().count == 60);
    subject.play(45, 60.0);
    for (const double t : {7.0, 19.5, 33.0 + 1.0 / 60.0, 41.004, 0.0}) {
        reference.seek(t, entity::SeekMode::FullHistory);
        subject.seek(t);
        const auto work = subject.world.lastSeekWork();
        INFO("t = " << t << " s, restored from " << work.restoredFrom << " s");
        if (t >= 1.0 + kStep) {
            // Subject: it resumed from a checkpoint no more than an interval behind the target.
            REQUIRE(work.restoredFrom >= 0.0);
            REQUIRE(t - work.restoredFrom <= 1.0 + 1e-9);
        }
        CHECK(work.exact);
        CHECK(reference.digest(t) == subject.digest(t));
    }

    SECTION("control: the digest can tell one step from the next") {
        World a;
        World b;
        a.seek(20.0, entity::SeekMode::FullHistory);
        b.seek(20.0 + kStep, entity::SeekMode::FullHistory);
        CHECK(a.digest(20.0) != b.digest(20.0));
    }
}

TEST_CASE("the checkpoint set keeps under its byte cap by thinning evenly", "[entity][seek][checkpoint][adr700]") {
    World w;
    w.seek(40.0);
    const auto full = w.world.checkpointStats();
    REQUIRE(full.count == 40);
    REQUIRE(full.lastBytes > 0);
    // Measured, not guessed: a checkpoint holds at least the inline size of every entity it copied.
    CHECK(full.lastBytes > w.world.size() * sizeof(entity::Entity));

    World capped;
    entity::CheckpointSettings cs = capped.world.checkpointSettings();
    cs.maxBytes = full.bytes / 3; // room for about a third of them
    capped.world.setCheckpointSettings(cs);
    capped.seek(40.0);
    const auto thin = capped.world.checkpointStats();
    INFO(thin.count << " checkpoints, " << thin.bytes << " bytes against a cap of " << cs.maxBytes
                    << ", interval " << thin.intervalSeconds << " s");
    CHECK(thin.bytes <= cs.maxBytes);
    CHECK(thin.intervalSeconds == 4.0);
    CHECK(thin.count == 10);
    // Thinner is slower, never wrong.
    World reference;
    reference.seek(29.5, entity::SeekMode::FullHistory);
    capped.seek(29.5);
    CHECK(capped.world.lastSeekWork().restoredFrom == 28.0);
    CHECK(reference.digest(29.5) == capped.digest(29.5));
}

TEST_CASE("a seek past what one click may cost falls back to the window and says so",
          "[entity][seek][checkpoint][adr700]") {
    World w(4);
    // 4 bodies x 1,201 steps is 4,804 body-steps; allow 1,000.
    w.seek(20.0, entity::SeekMode::Checkpointed, 1000);
    const auto work = w.world.lastSeekWork();
    CHECK(work.fellBack);
    CHECK_FALSE(work.exact);
    CHECK(work.mode == entity::SeekMode::Window);
    CHECK(w.world.checkpointStats().count == 0); // an inexact replay records nothing

    SECTION("and within the budget it is exact, and records as it goes") {
        World v(4);
        v.seek(20.0, entity::SeekMode::Checkpointed, 10000);
        CHECK_FALSE(v.world.lastSeekWork().fellBack);
        CHECK(v.world.lastSeekWork().exact);
        CHECK(v.world.checkpointStats().count == 20);
        // A second click near there costs one interval, well inside a budget the first could not meet.
        v.seek(19.5, entity::SeekMode::Checkpointed, 1000);
        CHECK(v.world.lastSeekWork().exact);
        CHECK(v.world.lastSeekWork().bodySteps == 4u * 30u);
    }
}

TEST_CASE("the checkpoint key moves with every input a replay reads", "[entity][seek][checkpoint][invalidation][adr700]") {
    World w;
    const std::uint64_t before = w.world.checkpointInputKey(&w.params, kStep, 0);
    CHECK(w.world.checkpointInputKey(&w.params, kStep, 0) == before); // and only with them

    SECTION("a parameter base") {
        params::IParameter* p = w.params.find("entity/body1/hover/amplitude");
        REQUIRE(p != nullptr);
        p->setBaseComponent(0, p->baseComponent(0) + 0.25f);
        CHECK(w.world.checkpointInputKey(&w.params, kStep, 0) != before);
    }
    SECTION("the step") { CHECK(w.world.checkpointInputKey(&w.params, 1.0 / 30.0, 0) != before); }
    SECTION("the host's key") { CHECK(w.world.checkpointInputKey(&w.params, kStep, 7) != before); }
    SECTION("a re-bind") {
        w.world.bind(w.params);
        CHECK(w.world.checkpointInputKey(&w.params, kStep, 0) != before);
    }
    SECTION("the landmarks") {
        w.world.setLandmarks({{"cairn", glm::vec3(3.0f)}});
        CHECK(w.world.checkpointInputKey(&w.params, kStep, 0) != before);
    }
}

TEST_CASE("an edited knob drops the checkpoints and the seek equals a fresh replay with the edit",
          "[entity][seek][checkpoint][invalidation][adr700]") {
    // `spin`'s base rate integrates, so an edit changes every step after zero -- the case where a
    // stale checkpoint would be most wrong.
    const auto edit = [](World& w) {
        params::IParameter* p = w.params.find("entity/body2/spin/baseRate");
        REQUIRE(p != nullptr);
        p->setBaseComponent(0, 95.0f);
    };
    World fresh;
    edit(fresh);
    fresh.seek(30.5, entity::SeekMode::FullHistory);

    World edited;
    edited.seek(50.0);
    REQUIRE(edited.world.checkpointStats().count == 50);
    edit(edited);
    edited.seek(30.5);
    CHECK(edited.world.lastSeekWork().invalidated);
    CHECK(edited.world.lastSeekWork().restoredFrom < 0.0);
    CHECK(edited.world.checkpointStats().invalidations >= 1);
    CHECK(fresh.digest(30.5) == edited.digest(30.5));

    SECTION("control: kept across the edit, the stale checkpoints give a different answer") {
        World stale;
        entity::CheckpointSettings cs = stale.world.checkpointSettings();
        cs.ignoreInputKey = true;
        stale.world.setCheckpointSettings(cs);
        stale.seek(50.0);
        edit(stale);
        stale.seek(30.5);
        REQUIRE(stale.world.lastSeekWork().restoredFrom == 30.0);
        CHECK(fresh.digest(30.5) != stale.digest(30.5));
    }
}

TEST_CASE("a restore keeps every live behaviour where it was", "[entity][seek][checkpoint][adr700]") {
    // A UI pass or an overlay may hold a behaviour's address across a scrub. A restore copies state
    // *into* the live objects (`IBehavior::assignState`) rather than replacing them.
    World w(1);
    std::vector<const entity::IBehavior*> before;
    for (const auto& b : w.world.entities().front()->behaviors()) {
        before.push_back(b.get());
    }
    w.seek(12.0);
    w.seek(5.5);
    REQUIRE(w.world.lastSeekWork().restoredFrom == 5.0);
    const auto& after = w.world.entities().front()->behaviors();
    REQUIRE(after.size() == before.size());
    for (std::size_t i = 0; i < before.size(); ++i) {
        CHECK(after[i].get() == before[i]);
    }
}
