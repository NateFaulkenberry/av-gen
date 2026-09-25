// The entity/actor handoff (spec §25, ADR-758): how a scripted performance takes a character's body
// and gives it back.
//
// Slice 2.0 began as a probe of the engine as it was. Measured then (ADR-758 records the numbers):
// an actor on an entity's node was SUMMED with the entity's travel every frame; its baked track
// pinned the node for the whole film, before its first key and after its last; a Director-tier Wait
// held nothing a behaviour moved; and `DirectorMotion` -- the staging director's mechanism -- held a
// body exactly and handed it back where it was put, except for `orbit`, which ignored `driven`.
// These cases now assert the contract ADR-758 built on that evidence.

#include "app/engine.hpp"
#include "entity/action.hpp"
#include "entity/behavior.hpp"
#include "entity/entity.hpp"
#include "scene/composition.hpp"
#include "seq/sequence.hpp"
#include "world/terrain_query.hpp"
#include "support/project_round_trip.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <cmath>

using namespace avgen;

namespace {

// A walker: an entity on node "walker" that orbits its anchor, moving its BODY (simulation
// authority), and a sequence actor on the same node with two keys.
struct Probe {
    app::Engine engine{app::EngineMode::Offline};
    seq::Actor actor;

    explicit Probe(bool withActor = true) {
        engine.newComposition();
        scene::CompositionNode node;
        node.name = "walker";
        node.kind = scene::NodeKind::Group;
        REQUIRE(engine.addNode(std::move(node)).has_value());
        entity::EntityDesc walker;
        walker.name = "walker";
        entity::BehaviorDesc orbit;
        orbit.kind = "orbit";
        orbit.settings = {{"radius", 4.0}, {"rate", 30.0}, {"authority", "simulation"}};
        walker.behaviors.push_back(orbit);
        REQUIRE(engine.composition()->setEntities({walker}).has_value());
        if (withActor) {
            actor.id = "walker";
            actor.keys.push_back(seq::ActorKey{2.0, glm::vec3(10.0f, 0.0f, 0.0f), std::nullopt, std::nullopt,
                                               params::KeyInterp::Linear});
            actor.keys.push_back(seq::ActorKey{4.0, glm::vec3(20.0f, 0.0f, 0.0f), std::nullopt, std::nullopt,
                                               params::KeyInterp::Linear});
            seq::Sequence piece;
            piece.actors.push_back(actor);
            REQUIRE(engine.setSequence(piece).has_value());
        }
    }
    void playTo(double seconds) {
        const int frames = static_cast<int>(std::lround(seconds * 60.0));
        for (int i = 0; i <= frames; ++i) {
            engine.update(FrameTime{static_cast<double>(i) / 60.0, i == 0 ? 0.0 : 1.0 / 60.0, static_cast<std::uint64_t>(i)});
        }
    }
    [[nodiscard]] glm::vec3 drawn() const {
        const params::IParameter* p = const_cast<app::Engine&>(engine).params().find("nodes/walker/position");
        REQUIRE(p != nullptr);
        return {p->finalComponent(0), p->finalComponent(1), p->finalComponent(2)};
    }
    [[nodiscard]] glm::vec3 travel() {
        const entity::Entity* e = engine.composition()->entityWorld().find("walker");
        REQUIRE(e != nullptr);
        return e->state().travel;
    }
};

} // namespace

TEST_CASE("inside its span a performance IS the body's position: no sum, the entity yields",
          "[directing][handoff]") {
    Probe p;
    p.playTo(3.0);
    const glm::vec3 key = p.actor.positionAt(3.0); // (15, 0, 0)
    const glm::vec3 drawn = p.drawn();
    INFO(fmt::format("actor ({:.3f},{:.3f},{:.3f}); node ({:.3f},{:.3f},{:.3f})", key.x, key.y, key.z, drawn.x, drawn.y, drawn.z));
    CHECK(glm::length(drawn - key) < 1e-3f);
    // Through the entity, so everything that asks where the body is gets the same answer.
    CHECK(glm::length(p.engine.composition()->entityWorld().find("walker")->state().position() - key) < 1e-3f);
    // And the node carries no track: the sequence does not own it.
    CHECK(p.engine.timeline().findTrack("nodes/walker/position") == nullptr);
}

TEST_CASE("a Director-tier Wait is not a handoff: it holds actions, not a behaviour that moves the body", "[directing][handoff]") {
    // The obvious handoff -- hold the entity with a Director-tier Wait -- does not stop a behaviour
    // that moves the body itself: the action tier governs actions, and `orbit` writes travel whatever
    // the tier says.
    Probe p(false);
    p.playTo(1.0);
    REQUIRE(p.engine.composition()->entityWorld().direct("walker", {entity::ActionDesc{}}, 1.0)); // Wait
    const glm::vec3 held = p.travel();
    p.playTo(2.0);
    CHECK(glm::length(p.travel() - held) > 0.1f);
}

TEST_CASE("outside its span the entity owns its body: a performance is not pinned to the film",
          "[directing][handoff]") {
    Probe early;
    early.playTo(1.0); // the performance begins at 2 s
    CHECK(glm::length(early.drawn() - early.travel()) < 1e-3f); // base (origin) + its own travel
    CHECK(glm::length(early.travel()) > 1.0f);                   // and it is orbiting, not held
}

TEST_CASE("a performance owns even an orbiting body; a staging motion still does not claim it",
          "[directing][handoff]") {
    // `orbit` with simulation authority writes the body after the director. A PERFORMANCE re-asserts
    // after the behaviours (ADR-758); staging's motions are left exactly as they were -- the autonomy
    // demo's saucer, an orbit whose action tier is only timers, depends on that.
    for (const bool performance : {true, false}) {
        INFO("performance " << performance);
        Probe p(false);
        p.playTo(1.0);
        entity::Entity* e = p.engine.composition()->entityWorld().find("walker");
        REQUIRE(e != nullptr);
        entity::DirectorMotion motion;
        motion.active = true;
        motion.performance = performance;
        motion.position = glm::vec3(30.0f, 0.0f, 5.0f);
        e->setDirectorMotion(motion);
        p.engine.update(FrameTime{1.0 + 1.0 / 60.0, 1.0 / 60.0, 61});
        CHECK((glm::length(p.drawn() - motion.position) < 1e-3f) == performance);
    }
}

TEST_CASE("on the benchmark, a DirectorMotion holds Rook exactly, and he resumes from where he was put",
          "[directing][handoff][benchmark]") {
    // Rook's behaviours (decide, lookAt, liveliness, ground) yield to `driven`. Held for a second at
    // a stage mark, he is drawn there -- on the ground: `ground` still decides the height -- and
    // after release his decider carries on from THAT place rather than snapping back.
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(std::filesystem::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    testsupport::stepFrames(engine, 60);
    entity::Entity* rook = engine.composition()->entityWorld().find("rook");
    REQUIRE(rook != nullptr);
    const glm::vec3 before = rook->state().position();
    const glm::vec3 mark = before + glm::vec3(12.0f, 0.0f, -7.0f);
    entity::DirectorMotion motion;
    motion.active = true;
    motion.position = mark;
    for (int i = 60; i < 120; ++i) {
        rook->setDirectorMotion(motion);
        engine.update(FrameTime{static_cast<double>(i) / 60.0, 1.0 / 60.0, static_cast<std::uint64_t>(i)});
    }
    const glm::vec3 held = rook->state().position();
    INFO(fmt::format("mark ({:.2f},{:.2f},{:.2f}) held ({:.2f},{:.2f},{:.2f})", mark.x, mark.y, mark.z, held.x, held.y, held.z));
    CHECK(std::abs(held.x - mark.x) < 1e-3f);
    CHECK(std::abs(held.z - mark.z) < 1e-3f);
    rook->setDirectorMotion(entity::DirectorMotion{});
    for (int i = 120; i < 130; ++i) {
        engine.update(FrameTime{static_cast<double>(i) / 60.0, 1.0 / 60.0, static_cast<std::uint64_t>(i)});
    }
    const glm::vec3 after = rook->state().position();
    INFO(fmt::format("ten frames after release ({:.2f},{:.2f},{:.2f})", after.x, after.y, after.z));
    // Continues from the mark: within what ten frames of his run speed can cover, not back at `before`.
    CHECK(glm::length(glm::vec2(after.x - mark.x, after.z - mark.z)) < 7.4f * 10.0f / 60.0f + 0.05f);
    CHECK(glm::length(glm::vec2(after.x - before.x, after.z - before.z)) > 5.0f);
}

TEST_CASE("on the benchmark, an actor's clip cue wins Rook's rig", "[directing][handoff][benchmark]") {
    // What drives Rook's rig while an actor's clip cue is active: `seq::applyAnimation` runs after the
    // behaviours every frame, so the cue wins the rig (engine.cpp, Engine::update).
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(std::filesystem::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    seq::Sequence piece = engine.sequence();
    seq::Actor rook;
    rook.id = "rook";
    rook.clips.push_back(seq::ClipCue{0.0, "Jumping", 1.0f, 0.0f});
    piece.actors.push_back(rook);
    REQUIRE(engine.setSequence(piece).has_value());
    testsupport::stepFrames(engine, 30);
    const scene::CompositionNode* node = engine.composition()->findNode("rook");
    REQUIRE(node != nullptr);
    REQUIRE_FALSE(node->rigs.empty());
    const auto& rig = engine.composition()->scene().rigs.at(node->rigs.front());
    CHECK(rig.player.currentState() == "Jumping");
}

TEST_CASE("Rook performs a run across the valley on the benchmark, and is handed back where it ends",
          "[directing][handoff][benchmark]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(std::filesystem::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    const entity::Entity* rookEntity = engine.composition()->entityWorld().find("rook");
    REQUIRE(rookEntity != nullptr);
    const glm::vec3 anchor = rookEntity->state().anchor;
    // A run of 12 m in 2 s (6 m/s, under his 7.4 m/s run) from a stage mark beside his anchor.
    const glm::vec3 a = anchor + glm::vec3(3.0f, 0.0f, 0.0f);
    const glm::vec3 b = a + glm::vec3(0.0f, 0.0f, 12.0f);
    seq::Sequence piece = engine.sequence();
    seq::Actor rook;
    rook.id = "rook";
    rook.keys.push_back(seq::ActorKey{1.0, a, std::nullopt, std::nullopt, params::KeyInterp::Linear});
    rook.keys.push_back(seq::ActorKey{3.0, b, std::nullopt, std::nullopt, params::KeyInterp::Linear});
    piece.actors.push_back(rook);
    REQUIRE(engine.setSequence(piece).has_value());
    REQUIRE(engine.composition()->performers().size() == 1);

    testsupport::stepFrames(engine, 121); // to t = 2.0
    const glm::vec3 mid = rookEntity->state().position();
    CHECK(std::abs(mid.x - a.x) < 1e-3f);
    CHECK(std::abs(mid.z - (a.z + 6.0f)) < 1e-2f);
    // Standing on the valley floor, not at his anchor's height carried across it.
    const world::TerrainQuery ground = engine.composition()->terrainQuery();
    REQUIRE(ground.valid());
    CHECK(std::abs(mid.y - ground.surfaceAt(glm::vec2(mid.x, mid.z))) < 1e-3f);
    // The gait read the performance's speed (through his authored acceleration ramp): a second in,
    // he is running, not sliding in an idle.
    CHECK(rookEntity->locomotion().activity == entity::Activity::Run);
    // Facing the way he runs (+Z is yaw 0).
    CHECK(std::abs(rookEntity->state().yaw) < 1e-3f);

    for (int i = 121; i < 181 + 10; ++i) { // across the end at t = 3.0, then ten frames of his own
        engine.update(FrameTime{static_cast<double>(i) / 60.0, 1.0 / 60.0, static_cast<std::uint64_t>(i)});
    }
    const glm::vec3 after = rookEntity->state().position();
    INFO(fmt::format("end ({:.2f},{:.2f}) after ({:.2f},{:.2f})", b.x, b.z, after.x, after.z));
    CHECK(glm::length(glm::vec2(after.x - b.x, after.z - b.z)) < 7.4f * 11.0f / 60.0f + 0.05f);
}

TEST_CASE("a seek lands on the performance: the body is where the actor says, and released after it",
          "[directing][handoff][seek]") {
    // Compared with the ACTOR, a pure function of time -- never with a play (seek == play is a known
    // open defect of the engine, and nothing here depends on it).
    Probe p;
    p.engine.seekSeconds(3.0);
    p.engine.update(FrameTime{3.0, 0.0, 180});
    CHECK(glm::length(p.drawn() - p.actor.positionAt(3.0)) < 1e-3f);
    p.engine.seekSeconds(6.0);
    p.engine.update(FrameTime{6.0, 0.0, 360});
    CHECK_FALSE(p.engine.composition()->entityWorld().find("walker")->directorMotion().active);
}

TEST_CASE("changing a performance drops the checkpoints a seek would otherwise reuse", "[directing][handoff][seek]") {
    // A performance's POSITION is memoryless inside its span, so a stale checkpoint could only show
    // after it: an orbit pauses its angle while it is driven, so where it is at 8 s depends on how
    // long it performed. Two seeks -- never a seek against a play -- one on an engine that already
    // holds checkpoints recorded under the OLD performance, one on a fresh engine.
    const auto withSpan = [](Probe& p, double from) {
        seq::Sequence piece = p.engine.sequence();
        piece.actors[0].keys[0].timeSeconds = from;
        REQUIRE(p.engine.setSequence(piece).has_value());
    };
    // Both engines step one frame first. A seek on an engine that has NEVER stepped lands
    // differently from the same seek once it has (measured: the same engine, the same inputs, seeking
    // to 8 s twice gave (3.864, 1.035) and then (-2.828, 2.828)). That is an engine seek defect in the
    // family the seek investigation owns, reported rather than worked around; this test is about
    // checkpoint invalidation and must not depend on it.
    const auto warm = [](Probe& p) { p.engine.update(FrameTime{0.0, 0.0, 0}); };
    Probe reused;
    warm(reused);
    reused.engine.seekSeconds(8.0); // records checkpoints under a 2-4 s performance
    reused.engine.update(FrameTime{8.0, 0.0, 480});
    withSpan(reused, 0.5);          // now 0.5-4 s: the orbit was held three and a half seconds
    reused.engine.seekSeconds(8.0);
    reused.engine.update(FrameTime{8.0, 0.0, 480});

    Probe fresh;
    withSpan(fresh, 0.5);
    warm(fresh);
    fresh.engine.seekSeconds(8.0);
    fresh.engine.update(FrameTime{8.0, 0.0, 480});
    INFO(fmt::format("reused ({:.3f},{:.3f}) fresh ({:.3f},{:.3f})", reused.drawn().x, reused.drawn().z,
                     fresh.drawn().x, fresh.drawn().z));
    CHECK(glm::length(reused.drawn() - fresh.drawn()) < 1e-4f);
}

TEST_CASE("an actor on a node no entity drives still bakes its tracks", "[directing][handoff]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    scene::CompositionNode node;
    node.name = "prop";
    node.kind = scene::NodeKind::Group;
    REQUIRE(engine.addNode(std::move(node)).has_value());
    seq::Sequence piece;
    seq::Actor prop;
    prop.id = "prop";
    prop.keys.push_back(seq::ActorKey{0.0, glm::vec3(0.0f), std::nullopt, std::nullopt, params::KeyInterp::Linear});
    prop.keys.push_back(seq::ActorKey{2.0, glm::vec3(4.0f, 0.0f, 0.0f), std::nullopt, std::nullopt, params::KeyInterp::Linear});
    piece.actors.push_back(prop);
    REQUIRE(engine.setSequence(piece).has_value());
    CHECK(engine.timeline().findTrack("nodes/prop/position") != nullptr);
    CHECK(engine.composition()->performers().empty());
}

