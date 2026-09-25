// One jump arc for the whole engine (ADR-822): an autonomous hop flies it, a director plans and bakes
// it, and a validator asks it the two questions a stunt needs answered -- how high must the body go
// to clear this, and does the ground let it land.
//
// The arithmetic is closed form, so the arms here compare against the formula's own promises (the
// landing point exactly, the apex exactly) and, for the hop, against the same arc sampled by hand:
// an integration that drifted would be caught by the first, a hop flying some other curve by the
// second.

#include "app/engine.hpp"
#include "entity/airborne.hpp"
#include "entity/entity.hpp"
#include "entity/navigation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "seq/director.hpp"
#include "seq/jump.hpp"
#include "seq/sequence.hpp"
#include "signals/signal_bus.hpp"
#include "support/project_assets.hpp"
#include "support/project_round_trip.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace avgen;
using Catch::Approx;

namespace {

float highest(const entity::JumpArc& arc, float from = 0.0f, float to = -1.0f) {
    if (to < 0.0f) {
        to = arc.duration;
    }
    float best = -1e9f;
    for (float t = from; t <= to; t += arc.duration / 2000.0f) {
        best = std::max(best, arc.at(t).y);
    }
    return best;
}

} // namespace

TEST_CASE("the arc lands exactly where it was aimed and peaks exactly as high as it was asked",
          "[motion][jump]") {
    SECTION("level: the textbook hop") {
        const auto arc = entity::planJump({0, 0, 0}, {4, 0, 0}, 1.1f, 18.0f);
        REQUIRE(arc.has_value());
        CHECK(arc->duration == Approx(2.0f * std::sqrt(2.0f * 1.1f / 18.0f)));
        CHECK(arc->apexTime() == Approx(arc->duration / 2.0f));
        CHECK(glm::length(arc->at(arc->duration) - glm::vec3(4, 0, 0)) < 1e-5f);
        CHECK(highest(*arc) == Approx(1.1f).margin(1e-4));
        CHECK(arc->velocityAt(0.0f).y == Approx(std::sqrt(2.0f * 18.0f * 1.1f)));
    }
    SECTION("down a bank and up one") {
        for (const float landing : {-2.0f, 0.7f}) {
            CAPTURE(landing);
            const auto arc = entity::planJump({1, 5, 2}, {6, 5 + landing, -1}, 1.5f, 18.0f);
            REQUIRE(arc.has_value());
            CHECK(glm::length(arc->at(arc->duration) - glm::vec3(6, 5 + landing, -1)) < 1e-4f);
            CHECK(highest(*arc) == Approx(6.5f).margin(1e-3));
            CHECK(arc->at(arc->apexTime()).y == Approx(6.5f).margin(1e-4));
            // Launch speed still obeys gravity: v0 = sqrt(2 g h).
            CHECK(arc->velocityAt(0.0f).y == Approx(std::sqrt(2.0f * 18.0f * 1.5f)).margin(1e-3));
        }
    }
    SECTION("control: an apex below the landing cannot exist, and says so") {
        CHECK_FALSE(entity::planJump({0, 0, 0}, {3, 2, 0}, 1.1f, 18.0f).has_value());
        CHECK_FALSE(entity::planJump({0, 0, 0}, {0, 0, 0}, 1.1f, 18.0f).has_value());
    }
}

TEST_CASE("the minimum apex clears an obstacle's whole footprint, and a hair less does not", "[motion][jump]") {
    // A cap of radius 3 topping out at 5.0, straight across the middle of a 16 m run-up-free leap.
    const glm::vec3 from{0, 0, 0};
    const glm::vec3 to{16, 0, 0};
    const glm::vec3 cap{8, 0, 0.5f};
    const float radius = 3.0f;
    const float top = 5.0f;
    const float clearance = 0.3f;
    const auto apex = entity::minimumApex(from, to, cap, radius, top, clearance);
    REQUIRE(apex.has_value());
    const auto overFootprint = [&](const entity::JumpArc& arc) {
        float lowest = 1e9f;
        for (float t = 0.0f; t <= arc.duration; t += arc.duration / 4000.0f) {
            const glm::vec3 p = arc.at(t);
            if (glm::length(glm::vec2(p.x - cap.x, p.z - cap.z)) <= radius) {
                lowest = std::min(lowest, p.y);
            }
        }
        return lowest;
    };
    const auto clears = entity::planJump(from, to, *apex, 18.0f);
    REQUIRE(clears.has_value());
    CHECK(overFootprint(*clears) >= top + clearance - 1e-3f);

    SECTION("control: 97% of it clips the cap") {
        const auto shy = entity::planJump(from, to, *apex * 0.97f, 18.0f);
        REQUIRE(shy.has_value());
        CHECK(overFootprint(*shy) < top + clearance);
    }
    SECTION("a path that passes beside it, or stops short of it, asks nothing") {
        CHECK_FALSE(entity::minimumApex(from, {16, 0, 10}, {8, 0, -6}, radius, top, clearance).has_value());
        CHECK_FALSE(entity::minimumApex(from, {4, 0, 0}, cap, radius, top, clearance).has_value());
    }
    SECTION("the Rook benchmark's shape: a default hop does not clear a 5 m cap") {
        const entity::JumpSettings rook{}; // no jump block: the defaults the lead said to keep
        CHECK(rook.apexLimit() == Approx(1.1f));
        CHECK(*apex > rook.apexLimit());
    }
}

TEST_CASE("the ground has its say about an arc", "[motion][jump]") {
    const auto arc = entity::planJump({0, 0, 0}, {6, 0, 0}, 1.1f, 18.0f);
    REQUIRE(arc.has_value());
    const auto flat = [](float, float) { return 0.0f; };
    const entity::ArcCheck ok = entity::checkArc(*arc, flat);
    CHECK(ok.clear);
    CHECK(ok.landingError == Approx(0.0f));

    SECTION("a ridge under the middle is met on the way over") {
        const auto ridge = [](float x, float) { return x > 2.5f && x < 3.5f ? 2.0f : 0.0f; };
        const entity::ArcCheck hit = entity::checkArc(*arc, ridge);
        CHECK_FALSE(hit.clear);
        const glm::vec3 at = arc->at(hit.firstContact);
        CHECK(at.x > 2.4f);
        CHECK(at.x < 3.6f);
    }
    SECTION("a landing aimed above a hollow says how far it would fall short of the ground") {
        const auto hollow = [](float x, float) { return x > 5.0f ? -0.8f : 0.0f; };
        CHECK(entity::checkArc(*arc, hollow).landingError == Approx(0.8f));
    }
}

TEST_CASE("an autonomous hop flies the shared arc, step for step", "[motion][jump][airborne]") {
    entity::JumpSettings settings;
    settings.apex = 1.4f;
    settings.maxDistance = 5.0f;
    entity::Airborne air;
    const glm::vec3 from{2, 0, 1};
    const glm::vec3 to{5.5f, 0, 1};
    REQUIRE(air.launch(from, to, settings));
    const auto planned = entity::planJump(from, to, 1.4f, settings.gravity);
    REQUIRE(planned.has_value());
    const entity::Navigator flat{};
    glm::vec3 at{0.0f};
    float worst = 0.0f;
    int steps = 0;
    for (int i = 1; air.airborne(); ++i) {
        REQUIRE(air.update(flat, 1.0 / 60.0, at, settings));
        if (air.airborne()) {
            worst = std::max(worst, glm::length(at - planned->at(static_cast<float>(i) / 60.0f)));
            ++steps;
        }
        REQUIRE(i < 600);
    }
    CHECK(steps > 20);
    CHECK(worst < 1e-5f);
    CHECK(glm::length(glm::vec2(at.x - to.x, at.z - to.z)) < 0.1f); // it came down on its target
}

TEST_CASE("a character's jump is data, and explore hops as high as the character can", "[motion][jump][entity]") {
    SECTION("the block round-trips, defaults are not written, nonsense is refused") {
        nlohmann::json j = {{"name", "ember"}, {"jump", {{"apex", 2.6}, {"maxDistance", 7.0}, {"landSeconds", 0.35}}}};
        auto desc = entity::entityFromJson(j, {});
        REQUIRE(desc.has_value());
        CHECK(desc->jump.apex == Approx(2.6f));
        CHECK(desc->jump.maxDistance == Approx(7.0f));
        CHECK(desc->jump.apexLimit() == Approx(2.6f));
        const nlohmann::json back = entity::entityToJson(*desc);
        CHECK(back["jump"]["apex"].get<float>() == Approx(2.6f));
        CHECK_FALSE(back["jump"].contains("gravity"));
        CHECK_FALSE(entity::entityToJson(entity::EntityDesc{.name = "rook"}).contains("jump"));
        nlohmann::json bad = j;
        bad["jump"]["maxApex"] = 1.0; // below its own apex
        CHECK_FALSE(entity::entityFromJson(bad, {}).has_value());
    }
    SECTION("a beat hop rises to the character's apex") {
        const auto peakFor = [](float apex) {
            params::ParameterSet params;
            signals::SignalBus bus;
            const signals::SignalId beat = bus.declare("audio.beat");
            entity::EntityWorld world;
            params.add(params::ParamDesc<glm::vec3>{.path = "nodes/ember/position", .defaultValue = glm::vec3(0.0f),
                                                    .hardMin = glm::vec3(-1e4f), .hardMax = glm::vec3(1e4f)});
            params.add(params::ParamDesc<glm::vec3>{.path = "nodes/ember/rotation", .defaultValue = glm::vec3(0.0f),
                                                    .hardMin = glm::vec3(-360.0f), .hardMax = glm::vec3(360.0f)});
            entity::EntityDesc d;
            d.name = "ember";
            d.seed = 7u;
            d.jump.apex = apex;
            d.jump.maxDistance = 4.0f;
            d.behaviors.push_back(entity::BehaviorDesc{
                "explore", "explore", {{"kind", "explore"}, {"jumpRange", 4.0}, {"jumpSignal", "audio.beat"}, {"speed", 2.0}}});
            world.setEntities({d}, 7u);
            entity::NodeBinding b;
            b.node = "ember";
            b.exists = true;
            b.transformPrefix = "nodes/ember/";
            world.setBindings({b});
            world.registerParameters(params);
            world.bind(params);
            float peak = 0.0f;
            // Beats every half second for twelve seconds: whenever the walker is walking, the next
            // rising edge launches a hop.
            for (int i = 1; i <= 720; ++i) {
                params.resetFinals();
                bus.set(beat, (i / 15) % 2 == 1 ? 1.0f : 0.0f);
                entity::EntityUpdate u;
                u.time = static_cast<double>(i) / 60.0;
                u.dt = 1.0 / 60.0;
                u.bus = &bus;
                u.distanceDetail = false;
                world.update(u, params);
                peak = std::max(peak, world.entities().front()->state().position().y);
            }
            return peak;
        };
        const float high = peakFor(2.6f);
        const float low = peakFor(1.1f);
        INFO("peaks " << high << " and " << low);
        CHECK(high == Approx(2.6f).margin(0.05));
        CHECK(low == Approx(1.1f).margin(0.05)); // control: the apex is the character's, not a constant
    }
}

TEST_CASE("a jump compiles to keys, a span and a cue that line up", "[motion][jump][seq]") {
    const auto arc = entity::planJump({0, 2, 0}, {5, 1, 0}, 1.2f, 18.0f);
    REQUIRE(arc.has_value());
    seq::Actor actor;
    actor.id = "rook";
    actor.keys = seq::jumpKeys(*arc, 10.0);
    CHECK(actor.keys.front().timeSeconds == Approx(10.0));
    CHECK(glm::length(actor.keys.back().position - arc->to) == 0.0f);
    float chord = 0.0f;
    for (double t = 10.0; t <= 10.0 + arc->duration; t += 0.003) {
        chord = std::max(chord, glm::length(actor.positionAt(t) - arc->at(static_cast<float>(t - 10.0))));
    }
    CHECK(chord < 0.01f); // 60 Hz chords of a parabola this size
    const auto [from, to] = seq::jumpSpan(*arc, 10.0);
    actor.airborne.emplace_back(from, to);
    CHECK(actor.airborneAt(10.0 + arc->apexTime()));
    CHECK_FALSE(actor.airborneAt(9.9));

    scene::ClipSemantics jumping;
    jumping.clip = "Jumping";
    jumping.length = 1.9f;
    jumping.events = {{"takeoff", 0.567f}, {"peak", 0.767f}, {"touchdown", 1.1f}};
    const auto cue = seq::jumpClipCue(jumping, *arc, 10.0);
    REQUIRE(cue.has_value());
    const seq::AnimationCue as{.node = "rook", .clip = cue->clip, .startSeconds = cue->timeSeconds, .speed = cue->speed};
    CHECK(seq::clipEventSeconds(as, 0.567f) == Approx(10.0));                  // the clip leaves the ground at launch
    CHECK(seq::clipEventSeconds(as, 1.1f) == Approx(10.0 + arc->duration));   // and lands at the landing
    CHECK(cue->playback == seq::ClipPlayback::Once);
    CHECK(cue->then == "gait");
    const seq::JumpTimes times = seq::jumpTimes(*arc, 10.0);
    CHECK(times.peak == Approx(10.0 + arc->apexTime()));

    SECTION("the airborne spans survive the project file") {
        seq::Sequence piece;
        piece.actors.push_back(actor);
        auto again = seq::Sequence::fromJson(piece.toJson());
        REQUIRE(again.has_value());
        REQUIRE(again->actors[0].airborne.size() == 1);
        CHECK(again->actors[0].airborne[0].second == Approx(to));
    }
}

TEST_CASE("on the benchmark, a performed jump keeps the arc's height and the ground gets the rest",
          "[motion][jump][handoff][benchmark]") {
    testsupport::skipUnlessGlowmereBenchmarkAssetsPresent();
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(std::filesystem::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    const entity::Entity* rook = engine.composition()->entityWorld().find("rook");
    REQUIRE(rook != nullptr);
    CHECK(rook->desc().jump.apexLimit() == Approx(1.1f)); // unchanged until the owner says otherwise
    const auto ground = engine.composition()->terrainQuery();
    REQUIRE(ground.valid());
    const glm::vec3 start = rook->state().position();
    const glm::vec3 landing(start.x + 4.0f, ground.surfaceAt(glm::vec2(start.x + 4.0f, start.z)), start.z);
    const auto arc = entity::planJump(glm::vec3(start.x, ground.surfaceAt(glm::vec2(start.x, start.z)), start.z),
                                      landing, std::max(1.1f, landing.y - start.y + 0.3f), rook->desc().jump.gravity);
    REQUIRE(arc.has_value());

    const auto peakHeight = [&](bool declareAirborne) {
        app::Engine e(app::EngineMode::Offline);
        REQUIRE(e.loadProject(std::filesystem::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
        seq::Sequence piece = e.sequence();
        seq::Actor actor;
        actor.id = "rook";
        actor.keys.push_back(seq::ActorKey{0.5, arc->from, std::nullopt, std::nullopt, params::KeyInterp::Linear});
        for (const seq::ActorKey& k : seq::jumpKeys(*arc, 1.0)) {
            actor.keys.push_back(k);
        }
        actor.keys.push_back(seq::ActorKey{2.0, arc->to, std::nullopt, std::nullopt, params::KeyInterp::Linear});
        if (declareAirborne) {
            const auto [a, b] = seq::jumpSpan(*arc, 1.0);
            actor.airborne.emplace_back(a, b);
        }
        piece.actors.push_back(actor);
        REQUIRE(e.setSequence(piece).has_value());
        const double apexAt = 1.0 + arc->apexTime();
        testsupport::stepFrames(e, static_cast<int>(std::lround(apexAt * 60.0)) + 1);
        const entity::Entity* r = e.composition()->entityWorld().find("rook");
        const glm::vec3 p = r->state().position();
        return p.y - ground.surfaceAt(glm::vec2(p.x, p.z));
    };
    const float flying = peakHeight(true);
    const float skidding = peakHeight(false);
    INFO("height over the ground at the apex: " << flying << " declared airborne, " << skidding << " not");
    CHECK(flying > 1.0f);
    CHECK(std::abs(skidding) < 1e-3f); // control: without the span the terrain wins, as it should on foot
}
