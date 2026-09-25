// What the motion stack adds to ADR-758's handoff (ADR-820), agreed with the Director:
//   * a per-actor `entrySeconds`: 0 (the default) takes the body at the authored mark; more blends
//     onto the performance from wherever the simulation had the body -- and a scrub into the blend
//     lands where a play does, because the entry point is entity state a checkpoint carries;
//   * a clip the actor names inside its span owns the rig (the gait yields rather than fighting the
//     sequencer every frame), and past the span the actor's clips stop applying: the body's gait is
//     its own again. With no cue, the gait plays the path from its speed (ADR-758, unchanged).
//
// ADR-758's own contract is `test_directing_handoff.cpp`, adopted unchanged as M1's acceptance
// suite. Play == scrub here is on the composition harness (the Film pattern of
// `test_glowmere_scrub.cpp`), never through `Engine`, whose seek != play on its own account.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "entity/behavior.hpp"
#include "entity/entity.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "seq/sequence.hpp"
#include "signals/signal_bus.hpp"
#include "support/project_assets.hpp"
#include "support/project_round_trip.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>

using namespace avgen;

namespace {

// A wanderer on node "walker", and a performance that takes it from 2 s to 4 s along two linear keys
// that do not start where the wanderer will be.
seq::Actor performance(float entrySeconds) {
    seq::Actor a;
    a.id = "walker";
    a.keys.push_back(seq::ActorKey{2.0, glm::vec3(10.0f, 0.0f, 0.0f), std::nullopt, std::nullopt,
                                   params::KeyInterp::Linear});
    a.keys.push_back(seq::ActorKey{4.0, glm::vec3(20.0f, 0.0f, 0.0f), std::nullopt, std::nullopt,
                                   params::KeyInterp::Linear});
    a.entrySeconds = entrySeconds;
    return a;
}

struct Harness {
    app::Engine engine{app::EngineMode::Offline};
    signals::SignalBus bus;
    long long frame = 0;

    explicit Harness(float entrySeconds, bool withActor = true) {
        engine.newComposition();
        scene::CompositionNode node;
        node.name = "walker";
        node.kind = scene::NodeKind::Group;
        REQUIRE(engine.addNode(std::move(node)).has_value());
        entity::EntityDesc walker;
        walker.name = "walker";
        walker.seed = 424242u;
        entity::BehaviorDesc wander;
        wander.kind = "wander";
        wander.settings = {{"speed", 1.4}, {"minRange", 4.0}, {"maxRange", 9.0}, {"pauseMin", 0.2}, {"pauseMax", 0.5}};
        walker.behaviors.push_back(wander);
        REQUIRE(engine.composition()->setEntities({walker}).has_value());
        if (withActor) {
            seq::Sequence piece;
            piece.actors.push_back(performance(entrySeconds));
            REQUIRE(engine.setSequence(piece).has_value());
        }
    }
    scene::Composition& comp() { return *engine.composition(); }
    // One ordinary frame on the composition, as the Film harness ticks it: the frame at `frame`/60.
    void tick() {
        FrameTime time;
        time.renderTime = static_cast<double>(frame) / 60.0;
        time.deltaTime = frame == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(frame);
        engine.params().resetFinals();
        comp().updateBehaviour(time, bus);
        comp().update(time);
        ++frame;
    }
    void playTo(double seconds) {
        const auto last = static_cast<long long>(std::llround(seconds * 60.0));
        while (frame <= last) {
            tick();
        }
    }
    void seekTo(double seconds) {
        comp().seekWithDirector(seconds, engine.params(),
                                entity::SeekBudget{.maxSeconds = 90.0,
                                                   .maxBodySteps = entity::SeekBudget::kEditorBodySteps,
                                                   .mode = entity::SeekMode::Checkpointed},
                                1.0 / 60.0);
        frame = static_cast<long long>(std::llround(seconds * 60.0)) + 1;
    }
    [[nodiscard]] glm::vec3 body() { return comp().entityWorld().find("walker")->state().position(); }
};

float horizontal(glm::vec3 a, glm::vec3 b) { return glm::length(glm::vec2(a.x - b.x, a.z - b.z)); }

} // namespace

TEST_CASE("entrySeconds 0 takes the body at the authored mark; more blends from where it was",
          "[motion][handoff]") {
    const glm::vec3 mark(10.0f, 0.0f, 0.0f);
    SECTION("0, the default: the first step in the span is on the mark") {
        Harness h(0.0f);
        h.playTo(2.0 - 1.0 / 60.0);
        REQUIRE(horizontal(h.body(), mark) > 1.0f); // it wandered somewhere else first
        h.tick();                                   // t = 2.0
        CHECK(horizontal(h.body(), mark) < 1e-3f);
    }
    SECTION("0.5 s: no step larger than the path's own pace plus the blend's share") {
        Harness h(0.5f);
        h.playTo(2.0 - 1.0 / 60.0);
        const glm::vec3 from = h.body();
        REQUIRE(horizontal(from, mark) > 1.0f);
        float worstStep = 0.0f;
        glm::vec3 last = from;
        while (h.frame <= 150) { // to 2.5 s, the end of the blend
            h.tick();
            worstStep = std::max(worstStep, horizontal(h.body(), last));
            last = h.body();
        }
        INFO(fmt::format("started {:.2f} m from the mark; worst step {:.3f} m", horizontal(from, mark), worstStep));
        // A smoothstep over distance d and time T peaks at 1.5 d / T; the path adds its own 5 m/s.
        // So the analytic ceiling per frame, with 5% slack -- and a snap would be the whole
        // distance in one step, which this is far below.
        const float d = horizontal(from, mark);
        const float ceiling = (1.5f * d / 0.5f + 5.0f) / 60.0f * 1.05f;
        CHECK(worstStep < ceiling);
        CHECK(worstStep < 0.1f * d);
        // And by the end of the blend the body is on the performance.
        CHECK(horizontal(h.body(), performance(0.5f).positionAt(2.5)) < 1e-3f);
    }
}

TEST_CASE("a scrub into an entry blend lands where the play did", "[motion][handoff][seek][determinism]") {
    for (const double target : {2.2, 3.0, 6.0}) {
        CAPTURE(target);
        Harness played(0.5f);
        played.playTo(target);
        played.tick();
        Harness scrubbed(0.5f);
        scrubbed.tick(); // one frame first: an engine that has never stepped seeks differently (ADR-758)
        scrubbed.seekTo(target);
        scrubbed.tick();
        CHECK(glm::length(played.body() - scrubbed.body()) < 1e-5f);
    }

    SECTION("and from a checkpoint: a scrub back into the blend after a scrub past it") {
        Harness played(0.5f);
        played.playTo(2.2);
        played.tick();
        Harness scrubbed(0.5f);
        scrubbed.tick();
        scrubbed.seekTo(3.5); // records checkpoints through the blend
        scrubbed.seekTo(2.2); // restores one from inside the span
        scrubbed.tick();
        CHECK(scrubbed.comp().entityWorld().lastSeekWork().restoredFrom > 0.0);
        CHECK(glm::length(played.body() - scrubbed.body()) < 1e-5f);
    }

    SECTION("control: a scrub whose blend began somewhere else must NOT agree") {
        // Same inputs, but the entry point forgotten: what a checkpoint that failed to carry
        // `PerformanceEntry` would produce. If this agreed, the arm above could not fail.
        Harness played(0.5f);
        played.playTo(2.2);
        played.tick();
        Harness scrubbed(0.5f);
        scrubbed.tick();
        scrubbed.seekTo(2.2);
        entity::Entity* e = scrubbed.comp().entityWorld().find("walker");
        entity::PerformanceEntry wrong = e->performanceEntry();
        REQUIRE(wrong.active);
        wrong.position += glm::vec3(3.0f, 0.0f, 0.0f);
        e->setPerformanceEntry(wrong);
        scrubbed.tick();
        CHECK(glm::length(played.body() - scrubbed.body()) > 0.1f);
    }
}

TEST_CASE("an actor's handoff entry survives the project file", "[motion][handoff]") {
    seq::Sequence piece;
    piece.actors.push_back(performance(0.75f));
    auto again = seq::Sequence::fromJson(piece.toJson());
    REQUIRE(again.has_value());
    CHECK(again->actors.front().entrySeconds == 0.75f);

    SECTION("0 is not written, and absent reads as 0") {
        seq::Sequence plain;
        plain.actors.push_back(performance(0.0f));
        CHECK_FALSE(plain.toJson()["actors"][0].contains("entrySeconds"));
    }
    SECTION("a negative entry is refused, not clamped") {
        nlohmann::json j = piece.toJson();
        j["actors"][0]["entrySeconds"] = -1.0;
        CHECK_FALSE(seq::Sequence::fromJson(j).has_value());
    }
}

TEST_CASE("on the benchmark, a performer's clip owns Rook's rig for its span and no longer",
          "[motion][handoff][benchmark]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(std::filesystem::path(AVGEN_SOURCE_DIR) /
                               "examples/world/glowmere-valley-2-multicam.json"));
    const entity::Entity* rook = engine.composition()->entityWorld().find("rook");
    REQUIRE(rook != nullptr);
    const glm::vec3 start = rook->state().position();
    seq::Sequence piece = engine.sequence();
    seq::Actor actor;
    actor.id = "rook";
    // A run from where Rook was authored, 1 s to 3 s, with a jump clip cued in the middle of it.
    actor.keys.push_back(seq::ActorKey{1.0, start, std::nullopt, std::nullopt, params::KeyInterp::Linear});
    actor.keys.push_back(seq::ActorKey{3.0, start + glm::vec3(12.0f, 0.0f, 0.0f), std::nullopt, std::nullopt,
                                       params::KeyInterp::Linear});
    actor.clips.push_back(seq::ClipCue{1.5, "Jumping", 1.0f, 0.1f});
    piece.actors.push_back(actor);
    REQUIRE(engine.setSequence(piece).has_value());

    const scene::CompositionNode* node = engine.composition()->findNode("rook");
    REQUIRE(node != nullptr);
    REQUIRE_FALSE(node->rigs.empty());
    const auto state = [&] { return engine.composition()->scene().rigs.at(node->rigs.front()).player.currentState(); };

    testsupport::stepFrames(engine, 73); // to t = 1.2: inside the span, before the cue
    CHECK_FALSE(rook->locomotion().clipOwned);
    INFO("action '" << rook->locomotion().action << "' pending " << rook->actions().pending());
    CHECK(rook->locomotion().activity == entity::Activity::Run); // the gait plays the 6 m/s path
    CHECK(state() == "Running");

    testsupport::stepFrames(engine, 48, 1.2 + 1.0 / 60.0); // to t = 2.0: the cue owns the rig
    CHECK(rook->locomotion().clipOwned);
    CHECK(state() == "Jumping");

    testsupport::stepFrames(engine, 120, 2.0 + 1.0 / 60.0); // to t = 4.0: released
    CHECK_FALSE(rook->locomotion().clipOwned);
    CHECK_FALSE(rook->directorMotion().active);
    INFO("rig state after release: " << state());
    CHECK(state() != "Jumping");

    SECTION("control: an actor with clips and no span is not a performer, and its cue still holds") {
        seq::Sequence clipsOnly = engine.sequence();
        clipsOnly.actors.back().keys.clear();
        REQUIRE(engine.setSequence(clipsOnly).has_value());
        testsupport::stepFrames(engine, 30, 4.0 + 1.0 / 60.0);
        CHECK(state() == "Jumping");
    }
}
